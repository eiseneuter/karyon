"""Global input grab: evdev exclusive grab of mice, uinput forwarding.

A dedicated thread grabs every real mouse exclusively and forwards every event
1:1 through a per-device virtual uinput device â EXCEPT the trigger button,
which is consumed and evaluated.  The virtual device carries VIRTUAL_MARKER in
its name so it never re-grabs itself.

If grabbing fails (no rights) we fall back to read-only mode: still detect the
trigger, but the button "leaks" to the system.
"""
from __future__ import annotations

import logging
import math
import os
import select
import threading
import time

try:
    import evdev
    from evdev import InputDevice, UInput, ecodes, list_devices
except Exception:  # noqa: BLE001 - evdev optional at import time
    evdev = None

log = logging.getLogger(__name__)

VIRTUAL_MARKER = "karyon-virtual"

# State machine
IDLE = 0
PENDING = 1
MENU = 2

# Minimum raw travel (px) before a fast movement counts as a flick gesture.
_GESTURE_MIN_DIST = 45


RESCAN_INTERVAL = 3.0

# Trigger button -> evdev key code(s)
def _trigger_codes(config) -> tuple:
    if evdev is None:
        return ()
    e = ecodes
    name = config.get("trigger_button", "middle")
    if name == "middle":
        return (e.BTN_MIDDLE,)
    if name == "right":
        return (e.BTN_RIGHT,)
    if name == "forward":
        return (e.BTN_FORWARD, e.BTN_EXTRA)
    if name == "backward":
        return (e.BTN_BACK, e.BTN_SIDE)
    if name == "lmb_rmb":
        return (e.BTN_LEFT, e.BTN_RIGHT)  # chord
    if name == "custom_key":
        code = int(config.get("trigger_key", 0) or 0)
        return (code,) if code else ()
    if name in ("tp_left", "tp_right"):
        return ()            # handled on the touchpad device, not a mouse
    return (e.BTN_MIDDLE,)


def _cancel_codes(config) -> tuple:
    if evdev is None:
        return ()
    e = ecodes
    name = config.get("cancel_button", "right")
    if name == "left":
        return (e.BTN_LEFT,)
    if name == "middle":
        return (e.BTN_MIDDLE,)
    if name == "forward":
        return (e.BTN_FORWARD, e.BTN_EXTRA)
    if name == "backward":
        return (e.BTN_BACK, e.BTN_SIDE)
    if name in ("tp_left", "tp_right"):
        return ()
    return (e.BTN_RIGHT,)


def _tp_button_code(name) -> int | None:
    """Touchpad trigger/cancel value -> raw button code (or None)."""
    if evdev is None:
        return None
    if name == "tp_left":
        return ecodes.BTN_LEFT
    if name == "tp_right":
        return ecodes.BTN_RIGHT
    return None


# Keycodes the injection device must be able to emit.  Declare the WHOLE
# standard keyboard range (codes 1..255) so any user-chosen "Custom Key" gesture
# can be emitted -- a fixed shortlist silently dropped events for keys not in it.
def _injection_keycodes() -> list:
    e = ecodes
    codes = set()
    for code, names in e.keys.items():
        if not isinstance(code, int) or code >= 256:
            continue
        name = names[0] if isinstance(names, (list, tuple)) else names
        if isinstance(name, str) and name.startswith("KEY_"):
            codes.add(code)
    # Guarantee the combos we rely on, even if some name lookup missed them.
    codes.update([
        e.KEY_LEFTCTRL, e.KEY_LEFTALT, e.KEY_LEFTSHIFT, e.KEY_LEFTMETA,
        e.KEY_A, e.KEY_C, e.KEY_V, e.KEY_X, e.KEY_Z, e.KEY_Y, e.KEY_Q,
        e.KEY_W, e.KEY_ENTER, e.KEY_LEFT, e.KEY_RIGHT, e.KEY_UP, e.KEY_DOWN,
        e.KEY_F5, e.KEY_SYSRQ, e.KEY_VOLUMEUP, e.KEY_VOLUMEDOWN, e.KEY_MUTE,
        e.KEY_PLAYPAUSE, e.KEY_NEXTSONG, e.KEY_PREVIOUSSONG,
    ])
    return sorted(codes)


class InputProxy:
    def __init__(self, config: dict,
                 on_hold=None, on_release=None, on_cancel=None,
                 on_motion=None, on_gesture=None, on_key_captured=None,
                 on_press=None, on_volume_scroll=None,
                 on_trigger_mute=None):
        super().__init__()
        self.config = config
        self.on_hold = on_hold
        self.on_release = on_release
        self.on_cancel = on_cancel
        self.on_motion = on_motion
        self.on_gesture = on_gesture
        self.on_key_captured = on_key_captured
        self.on_press = on_press
        self.on_volume_scroll = on_volume_scroll
        self.on_trigger_mute = on_trigger_mute

        self._thread: threading.Thread | None = None
        self._stop = threading.Event()

        self._devices: dict[str, InputDevice] = {}   # path -> mouse
        self._virtuals: dict[str, UInput] = {}        # path -> virtual uinput
        self._keyboards: dict[str, InputDevice] = {}  # for custom_key (read-only)
        # Touchpads: kept OPEN but only exclusively grabbed while the overlay is
        # up, so normal touchpad use is untouched otherwise.  scale maps absolute
        # finger units to roughly-screen pixels.
        self._touchpads: dict[str, InputDevice] = {}
        self._tp_scale: dict[str, tuple] = {}         # path -> (sx, sy)
        self._tp_grabbed = False
        self._tp_last: dict[str, list] = {}           # path -> [last_x, last_y]
        self._tp_full_active = False                  # current touchpad mode
        self._ui_keys: UInput | None = None
        self._ui_abs: UInput | None = None

        self.read_only = False
        self.capture_key = False  # capture-next-key mode for settings
        # When True the trigger is not consumed and all input is forwarded 1:1.
        # Used to pass mouse through while a fullscreen game/3D-app is active.
        self.inhibit = False

        # runtime state
        self.state = IDLE
        self._press_time = 0.0
        self._rel = [0.0, 0.0]          # accumulated raw deltas during press
        self._drag = False
        self._vpointer = [0.0, 0.0]     # virtual pointer (screen coords)
        self._center = [0.0, 0.0]
        self._menu_rel = [0.0, 0.0]     # virtual delta since menu opened
        self._path: list[tuple] = []    # (t, x, y)
        self._gpath: list[tuple] = []   # (t, raw_x, raw_y) for flick detection
        self._chord_down: set = set()
        self._gesture_done = False      # a flick already fired this press
        self._last_gesture_rel = (0.0, 0.0)   # raw travel of the last flick
        self._press_path = ""

    def set_inhibit(self, value: bool) -> None:
        """Enable or disable input inhibit (thread-safe).

        When inhibited the trigger button is no longer consumed and all mouse
        events are forwarded 1:1.  Used to keep the mouse working normally
        while a fullscreen game / 3-D app holds exclusive pointer grab.
        """
        self.inhibit = bool(value)
        if value and self.state != IDLE:
            # Drop any in-progress press so the overlay doesn't get stuck open.
            self.state = IDLE

    # -- lifecycle ----------------------------------------------------------
    def start(self) -> None:
        if evdev is None:
            log.error("python-evdev nicht verfÃ¼gbar â Eingabe-Proxy deaktiviert")
            return
        self._thread = threading.Thread(target=self._run, name="input-proxy",
                                        daemon=True)
        self._thread.start()

    def stop(self) -> None:
        self._stop.set()
        if self._thread is not None:
            self._thread.join(timeout=2.0)
        self._release_all()

    def _release_all(self) -> None:
        for path, dev in list(self._devices.items()):
            try:
                dev.ungrab()
            except Exception:  # noqa: BLE001
                pass
            try:
                dev.close()
            except Exception:  # noqa: BLE001
                pass
        self._devices.clear()
        for ui in list(self._virtuals.values()):
            try:
                ui.close()
            except Exception:  # noqa: BLE001
                pass
        self._virtuals.clear()
        for kb in list(self._keyboards.values()):
            try:
                kb.close()
            except Exception:  # noqa: BLE001
                pass
        self._keyboards.clear()
        for tp in list(self._touchpads.values()):
            try:
                tp.ungrab()
            except Exception:  # noqa: BLE001
                pass
            try:
                tp.close()
            except Exception:  # noqa: BLE001
                pass
        self._touchpads.clear()
        self._tp_grabbed = False
        for ui in (self._ui_keys, self._ui_abs):
            try:
                if ui is not None:
                    ui.close()
            except Exception:  # noqa: BLE001
                pass
        self._ui_keys = None
        self._ui_abs = None

    # Names of remote-input / notification input forwarders that must never be
    # exclusively grabbed -- grabbing them stalls the host when the remote device
    # is idle (KDE Connect), or makes the remote input unusable (Barrier/Synergy).
    _REMOTE_INPUT_NAMES = ("kde connect", "kdeconnect", "barrier", "synergy",
                           "input-leap", "deskflow")

    @classmethod
    def _is_remote_input(cls, dev: "InputDevice") -> bool:
        name = (dev.name or "").lower()
        return any(kw in name for kw in cls._REMOTE_INPUT_NAMES)

    @staticmethod
    def _is_mouse(dev: "InputDevice") -> bool:
        if VIRTUAL_MARKER in (dev.name or ""):
            return False
        caps = dev.capabilities()
        keys = caps.get(ecodes.EV_KEY, [])
        rels = caps.get(ecodes.EV_REL, [])
        # Any relative pointer that can left-click.  We must grab EVERY such node:
        # a sibling node that emits motion/clicks but is left un-grabbed feeds the
        # real cursor directly (system cursor moves, clicks hit windows under the
        # overlay).  Do NOT require BTN_MIDDLE -- touchpads' "Mouse" relative node
        # and many wireless mice expose a pointer node without a middle button.
        return ecodes.REL_X in rels and ecodes.BTN_LEFT in keys

    @staticmethod
    def _is_extra_mouse_buttons(dev: "InputDevice") -> bool:
        if VIRTUAL_MARKER in (dev.name or ""):
            return False
        caps = dev.capabilities()
        keys = caps.get(ecodes.EV_KEY, [])
        # If it has standard letter keys, it is a keyboard (even if it has multimedia keys).
        # We must NOT grab it as a mouse, otherwise typing can result in stuck keys on SYN_DROPPED.
        if ecodes.KEY_A in keys and ecodes.KEY_Z in keys:
            return False
        for k in (ecodes.BTN_SIDE, ecodes.BTN_EXTRA, ecodes.BTN_BACK, ecodes.BTN_FORWARD,
                  ecodes.KEY_BACK, ecodes.KEY_FORWARD, ecodes.KEY_PREVIOUS, ecodes.KEY_NEXT,
                  ecodes.KEY_PREVIOUSSONG, ecodes.KEY_NEXTSONG, ecodes.BTN_TASK):
            if k in keys:
                return True
        return False

    @staticmethod
    def _is_keyboard(dev: "InputDevice") -> bool:
        if VIRTUAL_MARKER in (dev.name or ""):
            return False
        caps = dev.capabilities()
        keys = caps.get(ecodes.EV_KEY, [])
        rels = caps.get(ecodes.EV_REL, [])
        return (ecodes.KEY_A in keys and ecodes.KEY_Z in keys
                and ecodes.REL_X not in rels)

    # -- device open/reconcile ---------------------------------------------
    def _open_devices(self) -> None:
        try:
            paths = list_devices()
        except Exception:  # noqa: BLE001
            return
        want_kb = self.config.get("trigger_button") == "custom_key" or self.capture_key
        # Re-open touchpads if the full/nav mode changed (trigger reconfigured).
        if self._tp_full() != self._tp_full_active:
            for tp_path in list(self._touchpads):
                tp = self._touchpads.pop(tp_path)
                self._tp_scale.pop(tp_path, None)
                self._tp_last.pop(tp_path, None)
                ui = self._virtuals.pop(tp_path, None)
                for obj in (tp, ui):
                    try:
                        if obj is tp:
                            tp.ungrab()
                    except Exception:  # noqa: BLE001
                        pass
                    try:
                        if obj is not None:
                            obj.close()
                    except Exception:  # noqa: BLE001
                        pass
            self._tp_grabbed = False
            self._tp_full_active = self._tp_full()
        seen_mice, seen_kb, seen_tp = set(), set(), set()
        for path in paths:
            try:
                dev = InputDevice(path)
            except Exception:  # noqa: BLE001
                continue
            try:
                # Remote-input forwarders (KDE Connect, Barrier, …) must never
                # be grabbed: an exclusive grab stalls the host when the remote
                # is idle and causes the system cursor to freeze.
                if self._is_remote_input(dev):
                    dev.close()
                    continue
                if self._is_mouse(dev) or self._is_extra_mouse_buttons(dev):
                    seen_mice.add(path)
                    if path not in self._devices:
                        self._add_mouse(path, dev)
                    else:
                        dev.close()
                elif self._is_touchpad(dev):
                    seen_tp.add(path)
                    if path not in self._touchpads:
                        self._add_touchpad(path, dev)
                    else:
                        dev.close()
                elif want_kb and self._is_keyboard(dev):
                    seen_kb.add(path)
                    if path not in self._keyboards:
                        self._keyboards[path] = dev
                    else:
                        dev.close()
                else:
                    dev.close()
            except Exception:  # noqa: BLE001
                try:
                    dev.close()
                except Exception:  # noqa: BLE001
                    pass
        self._reconcile(seen_mice, seen_kb, seen_tp)


    def _add_mouse(self, path: str, dev: "InputDevice") -> None:
        try:
            dev.grab()
            grabbed = True
        except Exception:  # noqa: BLE001
            grabbed = False
            self.read_only = True
        self._devices[path] = dev
        if grabbed:
            try:
                ui = UInput.from_device(dev, name=f"{VIRTUAL_MARKER} {dev.name}")
                self._virtuals[path] = ui
            except Exception:  # noqa: BLE001
                log.warning("Virtuelles GerÃ¤t fÃ¼r %s fehlgeschlagen", dev.name)
        log.info("Mouse captured: %s (%s)", dev.name, "grab" if grabbed else "read-only")

    @staticmethod
    def _is_touchpad(dev: "InputDevice") -> bool:
        if VIRTUAL_MARKER in (dev.name or ""):
            return False
        name_lower = (dev.name or "").lower()
        if "touchpad" in name_lower or "trackpad" in name_lower or "synaptics" in name_lower:
            return True
        caps = dev.capabilities()
        keys = caps.get(ecodes.EV_KEY, [])
        abs_codes = [a[0] if isinstance(a, tuple) else a
                     for a in caps.get(ecodes.EV_ABS, [])]
        has_xy = (ecodes.ABS_X in abs_codes
                  or ecodes.ABS_MT_POSITION_X in abs_codes)
        is_touch = (ecodes.BTN_TOUCH in keys
                    or ecodes.BTN_TOOL_FINGER in keys
                    or ecodes.BTN_TOOL_DOUBLETAP in keys)
        return has_xy and is_touch

    def _tp_full(self) -> bool:
        """A touchpad button is configured as trigger/cancel -> the touchpad must
        be grabbed full-time and its normal events forwarded (so the button can be
        consumed cleanly), instead of only grabbed while the overlay is up."""
        return (self.config.get("trigger_button") in ("tp_left", "tp_right")
                or self.config.get("cancel_button") in ("tp_left", "tp_right"))

    def _add_touchpad(self, path: str, dev: "InputDevice") -> None:
        self._touchpads[path] = dev
        sx = sy = 0.25
        try:
            abs_codes = [a[0] for a in dev.capabilities().get(ecodes.EV_ABS, [])]
            xc = ecodes.ABS_X if ecodes.ABS_X in abs_codes else ecodes.ABS_MT_POSITION_X
            yc = ecodes.ABS_Y if ecodes.ABS_Y in abs_codes else ecodes.ABS_MT_POSITION_Y
            xa, ya = dev.absinfo(xc), dev.absinfo(yc)
            sx = 780.0 / ((xa.max - xa.min) or 1)
            sy = 780.0 / ((ya.max - ya.min) or 1)
        except Exception:  # noqa: BLE001
            pass
        self._tp_scale[path] = (sx, sy)
        self._tp_last[path] = [None, None]
        if self._tp_full():
            # Grab full-time + forward normal events through a virtual device, so a
            # touchpad button can serve as trigger/cancel.
            try:
                dev.grab()
                self._virtuals[path] = UInput.from_device(
                    dev, name=f"{VIRTUAL_MARKER} {dev.name}")
                self._tp_grabbed = True
                log.info("Touchpad captured: %s (Trigger+Navigation)", dev.name)
                return
            except Exception:  # noqa: BLE001
                log.warning("Touchpad full mode failed: %s", dev.name)
                try:
                    dev.ungrab()
                except Exception:  # noqa: BLE001
                    pass
        log.info("Touchpad captured: %s (Navigation)", dev.name)

    def _reconcile(self, seen_mice: set, seen_kb: set, seen_tp: set) -> None:
        disconnected = False
        for path in list(self._touchpads):
            if path not in seen_tp:
                disconnected = True
                tp = self._touchpads.pop(path)
                self._tp_scale.pop(path, None)
                self._tp_last.pop(path, None)
                try:
                    tp.ungrab()
                except Exception:  # noqa: BLE001
                    pass
                try:
                    tp.close()
                except Exception:  # noqa: BLE001
                    pass
                ui = self._virtuals.pop(path, None)
                if ui is not None:
                    try:
                        ui.close()
                    except Exception:  # noqa: BLE001
                        pass
        for path in list(self._devices):
            if path not in seen_mice:
                disconnected = True
                dev = self._devices.pop(path)
                try:
                    dev.ungrab()
                except Exception:  # noqa: BLE001
                    pass
                try:
                    dev.close()
                except Exception:  # noqa: BLE001
                    pass
                ui = self._virtuals.pop(path, None)
                if ui is not None:
                    try:
                        ui.close()
                    except Exception:  # noqa: BLE001
                        pass
                log.info("Mouse removed: %s", path)
        for path in list(self._keyboards):
            if path not in seen_kb:
                disconnected = True
                kb = self._keyboards.pop(path)
                try:
                    kb.ungrab()
                except Exception:  # noqa: BLE001
                    pass
        if disconnected and self.state != IDLE:
            self._on_trigger_release()

    def _ensure_injection_devices(self) -> None:
        if self._ui_keys is None:
            try:
                cap = {ecodes.EV_KEY: _injection_keycodes()}
                self._ui_keys = UInput(cap, name=f"{VIRTUAL_MARKER} keys")
            except Exception:  # noqa: BLE001
                log.warning("Tastatur-InjektionsgerÃ¤t nicht verfÃ¼gbar")
        if self._ui_abs is None:
            try:
                cap = {
                    ecodes.EV_ABS: [
                        (ecodes.ABS_X, evdev.AbsInfo(0, 0, 65535, 0, 0, 0)),
                        (ecodes.ABS_Y, evdev.AbsInfo(0, 0, 65535, 0, 0, 0)),
                    ],
                    ecodes.EV_KEY: [ecodes.BTN_LEFT],
                }
                self._ui_abs = UInput(cap, name=f"{VIRTUAL_MARKER} abs")
            except Exception:  # noqa: BLE001
                pass

    # -- main loop ----------------------------------------------------------
    def _run(self) -> None:
        self._open_devices()
        self._ensure_injection_devices()
        last_scan = time.monotonic()
        while not self._stop.is_set():
            now = time.monotonic()
            if now - last_scan >= RESCAN_INTERVAL:
                self._open_devices()
                self._ensure_injection_devices()
                last_scan = now

            fds = {}
            for path, dev in self._devices.items():
                fds[dev.fd] = (dev, path, "mouse")
            for path, kb in self._keyboards.items():
                fds[kb.fd] = (kb, path, "kb")
            for path, tp in self._touchpads.items():
                fds[tp.fd] = (tp, path, "tp")
            if not fds:
                time.sleep(0.1)
                self._check_hold(time.monotonic())
                continue

            try:
                r, _, _ = select.select(list(fds), [], [], 0.02)
            except Exception:  # noqa: BLE001
                time.sleep(0.05)
                continue

            for fd in r:
                dev, path, kind = fds[fd]
                try:
                    for ev in dev.read():
                        if kind == "kb":
                            self._handle_kb_event(ev)
                        elif kind == "tp":
                            self._handle_touchpad_event(ev, path)
                        else:
                            self._handle_mouse_event(ev, path)
                except OSError:
                    continue

            self._check_hold(time.monotonic())
            # In nav mode touchpads are grabbed only while the overlay is up; release
            # them as soon as it closes.  In full mode they stay grabbed.
            if (not self._tp_full() and self.state != MENU and self._tp_grabbed):
                self._ungrab_touchpads()

    # -- touchpad navigation ------------------------------------------------
    def _grab_touchpads(self) -> None:
        if self._tp_grabbed:
            return
        for path, tp in self._touchpads.items():
            try:
                tp.grab()
            except Exception:  # noqa: BLE001
                pass
            self._tp_last[path] = [None, None]
        self._tp_grabbed = True

    def _ungrab_touchpads(self) -> None:
        if not self._tp_grabbed:
            return
        for tp in self._touchpads.values():
            try:
                tp.ungrab()
            except Exception:  # noqa: BLE001
                pass
        self._tp_grabbed = False

    def _handle_touchpad_event(self, ev, path: str) -> None:
        # Touchpad trigger button (full mode): drive the state machine, consume it.
        tp_trig = _tp_button_code(self.config.get("trigger_button"))
        if tp_trig is not None and ev.type == ecodes.EV_KEY and ev.code == tp_trig:
            if ev.value == 1:
                self._on_trigger_press(path)
            elif ev.value == 0:
                self._on_trigger_release()
            return
        # Touchpad cancel button while the menu is open.
        tp_canc = _tp_button_code(self.config.get("cancel_button"))
        if (self.state == MENU and tp_canc is not None and tp_canc != tp_trig
                and ev.type == ecodes.EV_KEY and ev.code == tp_canc):
            if ev.value == 1:
                self.state = IDLE
                if self.on_cancel:
                    self.on_cancel()
            return
        # While the overlay is up: finger motion steers it; consume everything.
        if self.state == MENU:
            last = self._tp_last.get(path)
            if last is None:
                return
            if ev.type == ecodes.EV_KEY and ev.code == ecodes.BTN_TOUCH:
                last[0] = last[1] = None
                return
            if ev.type == ecodes.EV_ABS:
                sx, sy = self._tp_scale.get(path, (0.25, 0.25))
                if ev.code in (ecodes.ABS_X, ecodes.ABS_MT_POSITION_X):
                    if last[0] is not None:
                        self._on_delta((ev.value - last[0]) * sx, 0.0)
                    last[0] = ev.value
                elif ev.code in (ecodes.ABS_Y, ecodes.ABS_MT_POSITION_Y):
                    if last[1] is not None:
                        self._on_delta(0.0, (ev.value - last[1]) * sy)
                    last[1] = ev.value
            return
        # IDLE/PENDING: in full mode forward to the virtual so the touchpad keeps
        # working normally; in nav mode it is not grabbed, so nothing to forward.
        if path in self._virtuals:
            self._forward(ev, path)

    # -- event handling -----------------------------------------------------
    def _forward(self, ev, path: str) -> None:
        ui = self._virtuals.get(path)
        if ui is None:
            return
        try:
            ui.write_event(ev)
        except Exception:  # noqa: BLE001
            pass

    def jiggle_virtual_mice(self) -> None:
        for ui in self._virtuals.values():
            try:
                ui.write(ecodes.EV_REL, ecodes.REL_X, 1)
                ui.syn()
                ui.write(ecodes.EV_REL, ecodes.REL_X, -1)
                ui.syn()
            except Exception:
                pass

    def _handle_mouse_event(self, ev, path: str) -> None:
        # Kernel buffer overflowed and dropped events. Force a release to prevent
        # getting permanently stuck if the ButtonRelease was among the dropped events.
        if ev.type == ecodes.EV_SYN and ev.code == ecodes.SYN_DROPPED:
            self._chord_down.clear()
            if self.state != IDLE:
                log.warning("SYN_DROPPED detected! Forcing trigger release.")
                self._on_trigger_release()
            return

        # When inhibited (fullscreen game active) forward everything 1:1 so the
        # game keeps exclusive mouse control.
        if self.inhibit:
            self._forward(ev, path)
            return
        codes = _trigger_codes(self.config)
        chord = self.config.get("trigger_button") == "lmb_rmb"

        # The configured cancel button closes an open menu without action, as
        # long as it is not itself the trigger.
        cancel = _cancel_codes(self.config)
        if (ev.type == ecodes.EV_KEY and ev.code == ecodes.BTN_MIDDLE
                and ev.value in (0, 1) and self.state in (PENDING, MENU)
                and ev.code not in codes):
            if self.state == PENDING:
                self._drag = True
            if ev.value == 1 and self.on_trigger_mute:
                self.on_trigger_mute()
            return
            

        if (self.state == MENU and ev.type == ecodes.EV_KEY
                and ev.code in cancel and ev.code not in codes):
            if ev.value == 1:
                self.state = IDLE
                if self.on_cancel:
                    self.on_cancel()
            return  # consume press and release

        if ev.type == ecodes.EV_KEY and ev.code in codes:
            if chord:
                self._handle_chord(ev, path, codes)
                return
            if ev.value == 1:
                self._on_trigger_press(path)
            elif ev.value == 0:
                self._on_trigger_release()
            return  # consume the trigger, never forward (replayed if it was a click)

        if ev.type == ecodes.EV_REL:
            if (ev.code == ecodes.REL_WHEEL and self.state in (PENDING, MENU)):
                self._on_volume_wheel(ev.value)
                return
            if self.state == MENU:
                # Update the virtual pointer; do NOT forward (keep real cursor
                # parked so it does not drift across the screen while navigating).
                if ev.code == ecodes.REL_X:
                    self._on_delta(ev.value, 0)
                elif ev.code == ecodes.REL_Y:
                    self._on_delta(0, ev.value)
                return
            if self.state == PENDING:
                if ev.code == ecodes.REL_X:
                    self._on_delta(ev.value, 0)
                elif ev.code == ecodes.REL_Y:
                    self._on_delta(0, ev.value)
                # keep forwarding so a middle-drag still works for the system
            self._forward(ev, path)
            return

        # Everything else forwards 1:1 to the system.
        self._forward(ev, path)

    def _on_volume_wheel(self, value: int) -> None:
        if value == 0:
            return
        if self.state == PENDING:
            self._drag = True
        if self.on_volume_scroll:
            self.on_volume_scroll(value)

    def _handle_chord(self, ev, path: str, codes: tuple) -> None:
        if ev.value == 1:
            self._chord_down.add(ev.code)
            if all(c in self._chord_down for c in codes) and self.state == IDLE:
                self._on_trigger_press()
            else:
                self._forward(ev, path)
        elif ev.value == 0:
            self._chord_down.discard(ev.code)
            if self.state != IDLE:
                self._on_trigger_release()
            else:
                self._forward(ev, path)

    def _handle_kb_event(self, ev) -> None:
        if ev.type == ecodes.EV_SYN and ev.code == ecodes.SYN_DROPPED:
            if self.state != IDLE:
                log.warning("SYN_DROPPED detected on keyboard! Forcing trigger release.")
                self._on_trigger_release()
            return
            
        if ev.type != ecodes.EV_KEY:
            return
        if self.capture_key and ev.value == 1:
            self.capture_key = False
            if self.on_key_captured:
                self.on_key_captured(ev.code)
            return
        codes = _trigger_codes(self.config)
        if self.config.get("trigger_button") == "custom_key" and ev.code in codes:
            if ev.value == 1:
                self._on_trigger_press()
            elif ev.value == 0:
                self._on_trigger_release()

    # -- state machine ------------------------------------------------------
    def _on_trigger_press(self, path: str = "") -> None:
        self.state = PENDING
        self._press_time = time.monotonic()
        self._rel = [0.0, 0.0]
        self._drag = False
        self._gesture_done = False
        self._press_path = path
        self._trigger_replayed = False
        self._path = [(self._press_time, 0.0, 0.0)]
        self._gpath = [(self._press_time, 0.0, 0.0)]
        if self.on_press:
            self.on_press()

    def _replay_trigger(self, value: int) -> None:
        """Re-inject the trigger button to the system (so a real click/drag works)."""
        tb = self.config.get("trigger_button")
        if tb == "lmb_rmb":
            return  # chords are not replayed
        code = _tp_button_code(tb)   # touchpad trigger?
        if code is None:
            codes = _trigger_codes(self.config)
            code = codes[0] if codes else None
        if code is None:
            return
        ui = self._virtuals.get(self._press_path)
        if ui is None:
            return
        try:
            ui.write(ecodes.EV_KEY, code, value)
            ui.syn()
        except Exception:  # noqa: BLE001
            pass

    def nudge_cursor(self) -> None:
        """Wiggle the compositor pointer by a net-zero move.  Our evdev grab
        freezes the real pointer, so KWin never re-evaluates pointer focus when
        the overlay maps and keeps the (hardware-plane) system cursor lit on top
        of it.  Two opposite 1px moves make KWin process motion -> send enter to
        the overlay surface -> apply its blank cursor, without shifting anything."""
        if evdev is None:
            return
        ui = next(iter(self._virtuals.values()), None)
        if ui is None:
            return
        try:
            ui.write(ecodes.EV_REL, ecodes.REL_X, 1)
            ui.write(ecodes.EV_REL, ecodes.REL_Y, 1)
            ui.syn()
            ui.write(ecodes.EV_REL, ecodes.REL_X, -1)
            ui.write(ecodes.EV_REL, ecodes.REL_Y, -1)
            ui.syn()
        except Exception:  # noqa: BLE001
            pass

    def _on_delta(self, dx: int, dy: int) -> None:
        factor = 2.0 ** float(self.config.get("mouse_speed", -0.7))
        # Raw accumulation (for drag detection during PENDING).
        self._rel[0] += dx
        self._rel[1] += dy
        # A gesture is triggered by the MOVEMENT itself (a sharp flick), during
        # the motion -- never by releasing the trigger.  Detected the same way
        # whether or not the radial menu has opened yet.
        if self.state in (PENDING, MENU):
            self._gpath.append((time.monotonic(), self._rel[0], self._rel[1]))
            if len(self._gpath) > 500:
                del self._gpath[0]
            if self._maybe_fire_gesture():
                return
        if self.state == PENDING:
            self._path.append((time.monotonic(), self._rel[0], self._rel[1]))
            if not self._drag:
                dist = math.hypot(self._rel[0], self._rel[1])
                if dist > float(self.config.get("drag_threshold_px", 14)):
                    # Movement that is not (yet) a flick: suppress the menu, but
                    # do NOT replay the trigger to the system.  This is a gesture
                    # launcher -- a middle-press injected into the app (e.g.
                    # Dolphin) would clear the selection / paste.  A still
                    # middle-CLICK (no movement) still passes through on release.
                    self._drag = True
        if self.state == MENU:
            # Menu-relative virtual delta from the moment the menu opened.
            self._menu_rel[0] += dx * factor
            self._menu_rel[1] += dy * factor
            self._path.append((time.monotonic(),
                self._menu_rel[0], self._menu_rel[1]))
            if len(self._path) > 500:
                del self._path[0]
            if self.on_motion:
                self.on_motion(self._menu_rel[0], self._menu_rel[1])

    def _check_hold(self, now: float) -> None:
        if self.state != PENDING or self._drag:
            return
        hold_ms = float(self.config.get("hold_ms", 200))
        if (now - self._press_time) * 1000.0 >= hold_ms:
            self.state = MENU
            self._menu_rel = [0.0, 0.0]
            self._path = [(now, 0.0, 0.0)]
            self._grab_touchpads()   # finger-on-touchpad now steers the overlay
            for p in self._tp_last:  # fresh navigation baseline (full mode too)
                self._tp_last[p] = [None, None]
            if self.on_hold:
                self.on_hold()

    def _maybe_fire_gesture(self) -> bool:
        """Fire a gesture the instant a sharp flick is detected DURING motion
        (the movement is the trigger, not the button release)."""
        if self._gesture_done or not self.config.get("gestures_enabled", True):
            return False
        # Only within the activation window after the press.
        window_ms = float(self.config.get("gesture_time_window", 450))
        if (time.monotonic() - self._press_time) * 1000.0 > window_ms:
            return False
        if math.hypot(self._rel[0], self._rel[1]) < _GESTURE_MIN_DIST:
            return False
        if self._gesture_speed() < float(self.config.get("gesture_min_speed", 1300)):
            return False
        direction = classify_direction(self._rel[0], self._rel[1],
                                       self.config.get("gesture_diagonal_size", 51))
        # Remember the raw travel so the flash can be anchored to the real path
        # (forwarded 1:1 during a flick, so it equals the cursor's screen move).
        self._last_gesture_rel = (self._rel[0], self._rel[1])
        self._gesture_done = True
        self.state = IDLE
        if self.on_gesture:
            self.on_gesture(direction)
        return True

    def _gesture_speed(self) -> float:
        if len(self._gpath) < 2:
            return 0.0
        now = self._gpath[-1][0]
        window = [p for p in self._gpath if now - p[0] <= 0.05]
        if len(window) < 2:
            window = self._gpath[-2:]
        (t0, x0, y0), (t1, x1, y1) = window[0], window[-1]
        dt = max(1e-4, t1 - t0)
        return math.hypot(x1 - x0, y1 - y0) / dt

    def _on_trigger_release(self) -> None:
        # Gestures fire during motion, so a release never triggers one.
        if self.state == PENDING:
            if self._drag:
                # A real (slow) drag: we replayed the press, now replay release.
                if self._trigger_replayed:
                    self._replay_trigger(0)
                self.state = IDLE
                return
            # A plain click: replay it as a normal trigger click.
            self.state = IDLE
            self._replay_trigger(1)
            self._replay_trigger(0)
            if self.on_cancel:
                self.on_cancel()
        elif self.state == MENU:
            self.state = IDLE
            if self.on_release:
                self.on_release()
        else:
            self.state = IDLE

    # -- gesture helpers (basic; refined in gestures.py for open menu) -------
    def _recent_speed(self) -> float:
        if len(self._path) < 2:
            return 0.0
        now = self._path[-1][0]
        window = [p for p in self._path if now - p[0] <= 0.05]
        if len(window) < 2:
            window = self._path[-2:]
        (t0, x0, y0), (t1, x1, y1) = window[0], window[-1]
        dt = max(1e-4, t1 - t0)
        return math.hypot(x1 - x0, y1 - y0) / dt


    # -- public helpers -----------------------------------------------------
    def send_keys(self, keys: list) -> None:
        """Inject a key combination, each event in its own syn report, 12ms apart."""
        self._ensure_injection_devices()
        ui = self._ui_keys
        if ui is None:
            return
        try:
            for code in keys:
                ui.write(ecodes.EV_KEY, code, 1)
                ui.syn()
                time.sleep(0.012)
            for code in reversed(keys):
                ui.write(ecodes.EV_KEY, code, 0)
                ui.syn()
                time.sleep(0.012)
        except Exception:  # noqa: BLE001
            log.exception("send_keys failed")

    def warp_cursor(self, x: int, y: int, bounds) -> None:
        self._ensure_injection_devices()
        ui = self._ui_abs
        if ui is None:
            return
        try:
            w, h = bounds
            ax = int(max(0, min(65535, x / max(1, w) * 65535)))
            ay = int(max(0, min(65535, y / max(1, h) * 65535)))
            ui.write(ecodes.EV_ABS, ecodes.ABS_X, ax)
            ui.write(ecodes.EV_ABS, ecodes.ABS_Y, ay)
            ui.syn()
        except Exception:  # noqa: BLE001
            pass

    def begin_key_capture(self) -> None:
        self.capture_key = True


def classify_direction(dx: float, dy: float, diagonal_size: float = 51) -> str:
    """Classify a vector into one of 8 directions (screen coords: y down)."""
    ang = math.degrees(math.atan2(dy, dx))  # -180..180, 0=right, 90=down
    # Diagonal zones width controlled by diagonal_size (% of the 45Â° band).
    diag_half = max(5.0, min(40.0, 45.0 * (float(diagonal_size) / 100.0)))
    centers = {
        "right": 0, "right_down": 45, "down": 90, "left_down": 135,
        "left": 180, "left_up": -135, "up": -90, "right_up": -45,
    }
    best, bestd = "right", 999.0
    for name, c in centers.items():
        d = abs((ang - c + 180) % 360 - 180)
        if d < bestd:
            best, bestd = name, d
    return best
