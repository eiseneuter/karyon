"""Tray system: mirror the real Plasma tray (shown/hidden/showAllItems + SNI)."""
from __future__ import annotations

import array
import json
import logging
import os
import subprocess
import sys
import threading
import time
from dataclasses import dataclass, field
from pathlib import Path

from PyQt6.QtCore import Qt, QObject, QThread, QTimer, pyqtSignal, pyqtSlot
from PyQt6.QtDBus import QDBusConnection, QDBusInterface, QDBusMessage
from PyQt6.QtGui import QIcon, QImage, QPixmap

from .procenv import child_env, run_detached

log = logging.getLogger(__name__)

APPLETSRC = Path(os.path.expanduser(
    "~/.config/plasma-org.kde.plasma.desktop-appletsrc"))

# Plasma applets Plasma always shows by default even when not in shownItems.
_ALWAYS_SHOWN = ("org.kde.plasma.notifications",)

# id -> (glyph_key, label, data, icon_name, control_key)
# data: tuple consumed by run on release.
PLASMA_ITEMS: dict[str, tuple] = {
    "org.kde.plasma.volume": ("volume", "Volume",
                              ("builtin", ["plasmawindowed", "org.kde.plasma.volume"]),
                              "audio-volume-high", "volume"),
    "org.kde.plasma.networkmanagement": ("network", "Networks",
                              ("builtin", ["plasmawindowed", "org.kde.plasma.networkmanagement"]),
                              "network-wireless", None),   # no drill/on-off bar
    "org.kde.plasma.bluetooth": ("bluetooth", "Bluetooth",
                              ("builtin", ["plasmawindowed", "org.kde.plasma.bluetooth"]),
                              "preferences-system-bluetooth", None),   # no drill/on-off bar
    "org.kde.kscreen": ("display", "Display",
                              ("builtin", ["kcmshell6", "kcm_kscreen"]),
                              "preferences-desktop-display", None),
    "org.kde.plasma.clipboard": ("clipboard", "Clipboard",
                              ("klipper", None), "klipper", None),
    "org.kde.plasma.notifications": ("notifications", "Notifications",
                              ("builtin", ["plasmawindowed", "org.kde.plasma.notifications"]),
                              "preferences-desktop-notification", None),
    "org.kde.plasma.battery": ("battery", "Battery & Brightness",
                              ("builtin", ["plasmawindowed", "org.kde.plasma.battery"]),
                              "battery", None),
    "org.kde.plasma.brightness": ("brightness", "Brightness",
                              ("builtin", ["plasmawindowed", "org.kde.plasma.brightness"]),
                              "brightness-high", None),
    "org.kde.plasma.devicenotifier": ("devices", "Devices",
                              ("builtin", ["plasmawindowed", "org.kde.plasma.devicenotifier"]),
                              "drive-removable-media", None),
    "org.kde.kdeconnect": ("kdeconnect", "KDE Connect",
                              ("builtin", ["plasmawindowed", "org.kde.kdeconnect"]),
                              "kdeconnect", None),
    "org.kde.plasma.printmanager": ("printer", "Printers",
                              ("builtin", ["kcmshell6", "kcm_printer_manager"]),
                              "printer", None),
    "org.kde.plasma.keyboardlayout": ("keyboard", "Keyboard Layout",
                              ("builtin", ["kcmshell6", "kcm_keyboard"]),
                              "input-keyboard", None),
    "org.kde.plasma.mediacontroller": ("media", "Media Player",
                              ("noop", None), "media-playback-start", None),
    "org.kde.plasma.cameraindicator": ("camera", "Camera",
                              ("noop", None), "camera-web", None),
    "org.kde.plasma.keyboardindicator": ("lockkeys", "Keyboard Indicator",
                              ("noop", None), "input-keyboard", None),
    "org.kde.plasma.manage-inputmethod": ("inputmethod", "Input Method",
                              ("noop", None), "input-keyboard", None),
    "org.kde.plasma.weather": ("weather", "Weather",
                              ("noop", None), "weather-clear", None),
}

# Control buttons: control_key -> [(glyph, label, mode, argv), ...]
CONTROLS: dict[str, list[tuple]] = {
    "volume": [
        ("ctrl_minus", "Volume -", "repeat",
         ["wpctl", "set-volume", "@DEFAULT_AUDIO_SINK@", "1%-"]),
        # Mute sits BETWEEN - and +, and (unlike the held +/- repeat) toggles
        # only when the trigger is RELEASED on it.
        ("mute", "Mute", "toggle",
         ["wpctl", "set-mute", "@DEFAULT_AUDIO_SINK@", "toggle"]),
        ("ctrl_plus", "Volume +", "repeat",
         ["wpctl", "set-volume", "@DEFAULT_AUDIO_SINK@", "1%+"]),
    ],
    "brightness": [
        ("ctrl_minus", "Brightness -", "repeat", ["brightnessctl", "set", "1%-"]),
        ("ctrl_plus", "Brightness +", "repeat", ["brightnessctl", "set", "1%+"]),
    ],
    "bluetooth": [
        ("ctrl_off", "Bluetooth Off", "toggle", ["rfkill", "block", "bluetooth"]),
        ("ctrl_on", "Bluetooth On", "toggle", ["rfkill", "unblock", "bluetooth"]),
    ],
    "networks": [
        ("ctrl_off", "Networks Off", "toggle", ["nmcli", "networking", "off"]),
        ("ctrl_on", "Networks On", "toggle", ["nmcli", "networking", "on"]),
    ],
}


@dataclass
class TrayItem:
    glyph: str | None
    label: str
    data: tuple
    icon_name: str = ""
    controls: list = field(default_factory=list)
    control_key: str | None = None
    qicon: QIcon | None = None
    service: str = ""
    hidden: bool = False   # lives in the collapsed tray popup (not on the bar)
    attention: bool = False  # SNI Status == NeedsAttention (new message)
    icon_sig: str = ""     # fingerprint of the current icon (change detection)
    menu_bus: str = ""     # D-Bus name hosting the context menu
    menu_path: str = ""    # com.canonical.dbusmenu object path
    menu: list = field(default_factory=list)   # parsed MenuEntry tree


def _humanize(item_id: str) -> str:
    name = item_id.split(".")[-1]
    name = name.replace("-", " ").replace("_", " ")
    return name[:1].upper() + name[1:]


def _read_appletsrc() -> dict:
    """Merge tray applet visibility config across all panels."""
    result = {
        "shown": [], "hidden": set(), "extra": [], "known": [],
        "show_all": False,
    }
    if not APPLETSRC.exists():
        return result
    cur_general = False
    try:
        for raw in APPLETSRC.read_text(encoding="utf-8", errors="replace").splitlines():
            line = raw.strip()
            if line.startswith("["):
                cur_general = line.endswith("[General]") or "systemtray" in line.lower()
                continue
            if "=" not in line:
                continue
            key, _, val = line.partition("=")
            key = key.strip()
            val = val.strip()
            if key == "shownItems":
                result["shown"] += [v for v in val.split(",") if v]
            elif key == "hiddenItems":
                result["hidden"].update(v for v in val.split(",") if v)
            elif key == "extraItems":
                result["extra"] += [v for v in val.split(",") if v]
            elif key == "knownItems":
                result["known"] += [v for v in val.split(",") if v]
            elif key == "showAllItems":
                if val.lower() == "true":
                    result["show_all"] = True
    except Exception:  # noqa: BLE001
        log.debug("appletsrc konnte nicht gelesen werden", exc_info=True)
    return result


# Always on the bar (Volume is re-centred to 0deg later); the curated rest goes
# into the "..." popup drawer.  Shown WITHOUT needing a Plasma panel tray, since
# the launcher is the tray now.
_MAIN_ITEMS = (
    "org.kde.plasma.volume", "org.kde.plasma.networkmanagement",
    "org.kde.plasma.bluetooth", "org.kde.plasma.clipboard",
)
# The curated standard tray functions behind the "..." drawer (those we have a
# real action for).  Filtered by hardware relevance (battery/brightness/bt).
_DRAWER_ITEMS = (
    "org.kde.plasma.keyboardlayout", "org.kde.kscreen",
    "org.kde.plasma.brightness", "org.kde.plasma.battery",
    "org.kde.kdeconnect", "org.kde.plasma.printmanager",
)


def _hw_present(item_id: str) -> bool:
    import glob
    import os
    if item_id == "org.kde.plasma.battery":
        return bool(glob.glob("/sys/class/power_supply/BAT*"))
    if item_id == "org.kde.plasma.brightness":
        return bool(glob.glob("/sys/class/backlight/*"))
    if item_id == "org.kde.plasma.bluetooth":
        return os.path.isdir("/sys/class/bluetooth") and \
            bool(os.listdir("/sys/class/bluetooth"))
    return True


# Never offered anywhere (main fan nor popup drawer).
_EXCLUDE = {
    "org.kde.plasma.keyboardindicator",
    "org.kde.plasma.notifications",
    "org.kde.plasma.cameraindicator",
    "org.kde.plasma.mediacontroller",
    "org.kde.plasma.manage-inputmethod",
    "org.kde.plasma.devicenotifier",
    "org.kde.plasma.weather",
}


def _items_for(ids) -> list[TrayItem]:
    items = []
    for item_id in ids:
        if item_id in _EXCLUDE:
            continue
        if item_id in PLASMA_ITEMS:
            glyph, label, data, icon_name, control_key = PLASMA_ITEMS[item_id]
            controls = CONTROLS.get(control_key, []) if control_key else []
            items.append(TrayItem(glyph=glyph, label=label, data=data,
                                  icon_name=icon_name, controls=controls,
                                  control_key=control_key))
        else:
            items.append(TrayItem(glyph=None, label=_humanize(item_id),
                                  data=("noop", None), icon_name=item_id))
    return items


def enabled_tray_items() -> list[TrayItem]:
    # The core controls, always on the main fan (no Plasma panel required).
    return _items_for([i for i in _MAIN_ITEMS if _hw_present(i)])


def hidden_tray_items() -> list[TrayItem]:
    """The curated rest of the standard tray functions -> the "..." drawer."""
    return _items_for([i for i in _DRAWER_ITEMS if _hw_present(i)])


_MESSENGER_KEYWORDS = (
    "rambox", "signal", "whatsapp", "telegram", "discord", "element", "slack",
    "thunderbird", "evolution", "kmail", "skype", "teams", "zoom", "matrix",
    "session", "threema", "viber", "messenger", "chat", "mail", "outlook",
    "protonmail", "gmail", "icq", "pidgin", "hexchat", "mumble", "riot",
    "wechat", "line"
)


def _is_messenger(label: str) -> bool:
    low = label.lower()
    return any(kw in low for kw in _MESSENGER_KEYWORDS)


# ---------------------------------------------------------------------------
# SNI worker (busctl in a worker thread -- never block the GUI thread).
# ---------------------------------------------------------------------------
class _SniWorker(QThread):
    done = pyqtSignal(list)

    def __init__(self, services: list[str]):
        super().__init__()
        self._services = services

    def run(self) -> None:
        items = []
        for svc in self._services:
            try:
                item = self._read_item(svc)
                if item is not None:
                    items.append(item)
            except Exception:  # noqa: BLE001
                continue
        self.done.emit(items)

    def _read_item(self, service: str) -> TrayItem | None:
        if "/" in service:
            bus, path = service.split("/", 1)
            path = "/" + path
        else:
            bus, path = service, "/StatusNotifierItem"

        def prop(name: str):
            try:
                out = subprocess.run(
                    ["busctl", "--user", "--json=short", "get-property",
                     bus, path, "org.kde.StatusNotifierItem", name],
                    capture_output=True, text=True, timeout=1.5, env=child_env())
                if out.returncode != 0:
                    return None
                return json.loads(out.stdout)
            except Exception:  # noqa: BLE001
                return None

        status = prop("Status")
        status_val = (status or {}).get("data", "") if status else ""
        passive = status_val == "Passive"   # lives in the collapsed popup
        attention = status_val == "NeedsAttention"   # new message / notification

        def strprop(name: str) -> str:
            p = prop(name)
            return (p.get("data") if isinstance(p, dict) else "") or ""

        # Title is often an empty string for app SNIs -> fall back to the Id.
        label = self._clean_label(strprop("Title") or strprop("Id") or "App")
        norm_label = label.lower()
        if "karyon" in norm_label:
            return None

        icon_name_prop = prop("IconName")
        icon_name = (icon_name_prop or {}).get("data", "") if icon_name_prop else ""
        # ALWAYS read the pixmap: many messengers (e.g. Rambox) expose NO icon
        # name and signal a new message ONLY by swapping their IconPixmap.
        # Furthermore, the theme might not have the IconName, so we need the pixmap as fallback.
        px = prop("IconPixmap")
        qicon = self._pixmap_icon(px)
        # Icon fingerprint for change-detection.
        import hashlib
        parts = [icon_name or "", strprop("AttentionIconName")]
        if px is not None:
            try:
                parts.append(hashlib.md5(
                    json.dumps(px, sort_keys=True).encode()).hexdigest())
            except Exception:  # noqa: BLE001
                pass
        icon_sig = "|".join(parts)

        # The app's context menu (com.canonical.dbusmenu), so the launcher can be
        # a full tray replacement -- read here in the worker thread (off the UI).
        from . import dbusmenu
        menu_path = strprop("Menu")
        menu = dbusmenu.read_menu(bus, menu_path) if menu_path else []

        return TrayItem(glyph=None, label=label, data=("sni", service),
                        icon_name=icon_name or "", qicon=qicon, service=service,
                        hidden=passive, attention=attention, icon_sig=icon_sig,
                        menu_bus=bus, menu_path=menu_path, menu=menu)

    @staticmethod
    def _clean_label(label: str) -> str:
        label = label.split("_status_icon_1")[0]
        label = label.replace("_", " ").strip()
        if label:
            label = label[:1].upper() + label[1:]
        return label

    @staticmethod
    def _pixmap_icon(pixmap_data) -> QIcon | None:
        try:
            if not pixmap_data:
                return None
            data = pixmap_data.get("data") if isinstance(pixmap_data, dict) else None
            if not data:
                return None
            # data is array of (width, height, bytes); take the largest.
            best = max(data, key=lambda t: int(t[0]) * int(t[1]))
            w, h, raw = int(best[0]), int(best[1]), bytes(best[2])
            if w <= 0 or h <= 0 or len(raw) < w * h * 4:
                return None
            # SNI IconPixmap is ARGB32 in NETWORK (big-endian) byte order.
            # Byte-swap each 32-bit pixel to native order for Format_ARGB32 --
            # NOT an R/B channel swap (that turns blue icons red).
            words = array.array("I")
            words.frombytes(raw[:w * h * 4])
            if sys.byteorder == "little":
                words.byteswap()
            img = QImage(words.tobytes(), w, h, QImage.Format.Format_ARGB32).copy()
            pm = QPixmap.fromImage(img)
            # SNI pixmaps are small (16/32px); smooth-upscale so they are not
            # tiny / jagged in the larger overlay segments.
            if 0 < pm.width() < 48:
                pm = pm.scaled(48, 48, Qt.AspectRatioMode.KeepAspectRatio,
                               Qt.TransformationMode.SmoothTransformation)
            return QIcon(pm)
        except Exception:  # noqa: BLE001
            return None


def _norm_app(name: str) -> str:
    return "".join(c for c in (name or "").lower() if c.isalnum())


class NotificationMonitor:
    """Eavesdrop org.freedesktop.Notifications.Notify (via dbus-monitor) to learn
    which apps just posted a notification -- a reliable, icon-independent 'new
    message' signal that works for any messenger.  A flag clears when the matching
    tray app is opened."""

    def __init__(self) -> None:
        self._new: list[str] = []           # app names notified since last poll
        self._lock = threading.Lock()
        self._stop = threading.Event()
        self._proc = None

    def start(self) -> None:
        threading.Thread(target=self._run, daemon=True).start()

    def stop(self) -> None:
        self._stop.set()
        if self._proc is not None:
            try:
                self._proc.terminate()
            except Exception:  # noqa: BLE001
                pass

    def _run(self) -> None:
        rule = "interface='org.freedesktop.Notifications',member='Notify'"
        while not self._stop.is_set():
            try:
                self._proc = subprocess.Popen(
                    ["dbus-monitor", "--session", rule],
                    stdout=subprocess.PIPE, text=True, bufsize=1, env=child_env())
                expect = False
                for line in self._proc.stdout:
                    if self._stop.is_set():
                        break
                    if "member=Notify" in line:
                        expect = True       # next 'string' is the app_name
                        continue
                    if expect:
                        s = line.strip()
                        if s.startswith('string "') and s.endswith('"'):
                            self._note(s[8:-1])
                            expect = False
            except Exception:  # noqa: BLE001
                pass
            if self._stop.wait(2.0):
                return

    def _note(self, app: str) -> None:
        n = _norm_app(app)
        if n:
            with self._lock:
                self._new.append(n)

    def pop_new(self) -> list[str]:
        """Normalised app names that posted a notification since the last call."""
        with self._lock:
            out, self._new = self._new, []
        return out


class TrayManager(QObject):
    changed = pyqtSignal()

    def __init__(self) -> None:
        super().__init__()
        self.sni_items: list[TrayItem] = []
        self.hidden_sni_items: list[TrayItem] = []
        self._status_connected: set[str] = set()   # services whose NewStatus we watch
        # Unread tracking: a notification arms an app; we remember the app's
        # "unread" icon and keep the mail badge until that icon changes (= read).
        self._unread: dict[str, str] = {}           # service -> unread icon sig
        self._muted: dict[str, str] = {}            # service -> muted icon sig
        self._pending_notif: set[str] = set()       # norm app names awaiting capture
        self._notif = NotificationMonitor()
        self._notif.start()
        self._notif_timer = QTimer(self)
        self._notif_timer.setInterval(500)
        self._notif_timer.timeout.connect(self._poll_notif)
        self._notif_timer.start()
        self._bus = QDBusConnection.sessionBus()
        self._worker: _SniWorker | None = None
        self._debounce = QTimer(self)
        self._debounce.setSingleShot(True)
        self._debounce.setInterval(200)
        self._debounce.timeout.connect(self._refresh)
        self._register_host()
        self._listen()
        QTimer.singleShot(500, self._refresh)

    def _register_host(self) -> None:
        """Register as a StatusNotifierHost.  Without a host (e.g. the user has
        no Plasma tray on the panel), many apps hide their tray icon entirely --
        registering makes them publish their SNI so we can show them."""
        try:
            import os
            name = f"org.kde.StatusNotifierHost-{os.getpid()}-Karyon"
            self._bus.registerService(name)
            iface = QDBusInterface("org.kde.StatusNotifierWatcher",
                                   "/StatusNotifierWatcher",
                                   "org.kde.StatusNotifierWatcher", self._bus)
            iface.setTimeout(800)
            iface.call("RegisterStatusNotifierHost", name)
            # New items appearing once apps notice the host -> refresh.
            self._bus.connect("org.kde.StatusNotifierWatcher",
                              "/StatusNotifierWatcher",
                              "org.kde.StatusNotifierWatcher",
                              "StatusNotifierHostRegistered", self._on_sni_change)
        except Exception:  # noqa: BLE001
            log.exception("StatusNotifierHost-Registrierung fehlgeschlagen")

    def _listen(self) -> None:
        watcher = "org.kde.StatusNotifierWatcher"
        path = "/StatusNotifierWatcher"
        iface = "org.kde.StatusNotifierWatcher"
        for sig in ("StatusNotifierItemRegistered", "StatusNotifierItemUnregistered"):
            self._bus.connect(watcher, path, iface, sig, self._on_sni_change)

    @pyqtSlot(str)
    def _on_sni_change(self, *_args) -> None:
        self._debounce.start()

    def _registered_services(self) -> list[str]:
        # Read the property via the standard Properties interface -- calling
        # "org.freedesktop.DBus.Properties.Get" on the SNI interface itself is
        # parsed as a method literally named "org" and fails (-> no items).
        props = QDBusInterface("org.kde.StatusNotifierWatcher",
                               "/StatusNotifierWatcher",
                               "org.freedesktop.DBus.Properties", self._bus)
        props.setTimeout(800)   # never block (default 25s) -> never wedge a caller
        reply = props.call("Get", "org.kde.StatusNotifierWatcher",
                           "RegisteredStatusNotifierItems")
        args = reply.arguments()
        if not args:
            return []
        val = args[0]
        if isinstance(val, (list, tuple)):
            return [str(v) for v in val]
        return []

    def _refresh(self) -> None:
        try:
            services = self._registered_services()
        except Exception:  # noqa: BLE001
            services = []
        if self._worker is not None and self._worker.isRunning():
            return
        self._worker = _SniWorker(services)
        self._worker.done.connect(self._on_items)
        self._worker.start()

    def _poll_notif(self) -> None:
        new = self._notif.pop_new()
        if not new:
            return
        self._pending_notif.update(new)
        # Let the app update its tray icon to the unread look, THEN re-read so we
        # capture that "unread" icon as the reference.
        QTimer.singleShot(800, self._refresh)

    def _on_items(self, items: list) -> None:
        for it in items:
            nm = _norm_app(it.label)
            if not _is_messenger(nm):
                continue
            matched = next((p for p in self._pending_notif
                            if p and (p in nm or nm in p)), None)
            if matched:
                # Capture the current (unread) icon as the reference for this app.
                self._pending_notif.discard(matched)
                self._unread[it.service] = it.icon_sig
                self._muted.pop(it.service, None)
            elif it.service in self._unread and it.icon_sig != self._unread[it.service]:
                # The app changed its icon back -> messages were read.
                self._unread.pop(it.service, None)
        
        live = {it.service for it in items}
        for svc in list(self._unread):
            if svc not in live:
                self._unread.pop(svc, None)
                self._muted.pop(svc, None)
                
        for it in items:
            # If an item is neither natively requesting attention nor artificially unread,
            # it means there are no unread messages. Clear any previous mute.
            if not it.attention and it.service not in self._unread:
                self._muted.pop(it.service, None)

        self.sni_items = [it for it in items if not it.hidden]
        self.hidden_sni_items = [it for it in items if it.hidden]
        # Watch each item's NewStatus / NewIcon so reads (icon revert) and
        # NeedsAttention changes are picked up promptly.
        for it in items:
            self._watch_status(it.service)
        self.changed.emit()

    def mute_attention(self, service: str, icon_sig: str) -> None:
        self._muted[service] = icon_sig
        self.changed.emit()

    def _watch_status(self, service: str) -> None:
        if not service or service in self._status_connected:
            return
        if "/" in service:
            bus, path = service.split("/", 1)
            path = "/" + path
        else:
            bus, path = service, "/StatusNotifierItem"
        for sig in ("NewStatus", "NewAttentionIcon", "NewIcon"):
            self._bus.connect(bus, path, "org.kde.StatusNotifierItem", sig,
                              self._on_sni_change)
        self._status_connected.add(service)

    def attention_items(self) -> list[TrayItem]:
        """Tray apps that still hold unread messages: SNI NeedsAttention, or an app
        that got a notification and whose icon still shows the unread state.  The
        badge stays until the app's icon reverts (= read) -- not when merely
        opened."""
        return [it for it in (self.sni_items + self.hidden_sni_items)
                if (it.attention or it.service in self._unread) and
                   self._muted.get(it.service) != it.icon_sig]

    def refresh_now(self) -> None:
        """Force a fresh SNI read (e.g. when the overlay opens) so attention
        state is current."""
        self._refresh()

    def enabled_tray_items(self) -> list[TrayItem]:
        return enabled_tray_items()

    def hidden_tray_items(self) -> list[TrayItem]:
        """Popup-drawer contents: non-main plasmoids + passive SNI app icons."""
        return hidden_tray_items() + list(self.hidden_sni_items)

    @staticmethod
    def _valid_path(path: str) -> bool:
        if not path or not path.startswith("/"):
            return False
        body = path.strip("/")
        return all(seg and all(c.isalnum() or c == "_" for c in seg)
                   for seg in body.split("/")) if body else True

    # -- actions ------------------------------------------------------------
    def activate_sni(self, service: str) -> None:
        if "/" in service:
            bus, path = service.split("/", 1)
            path = "/" + path
        else:
            bus, path = service, "/StatusNotifierItem"
        if not bus or not self._valid_path(path):
            log.warning("activate_sni: ungueltiger Service/Pfad %r", service)
            return
        iface = QDBusInterface(bus, path, "org.kde.StatusNotifierItem", self._bus)
        iface.setTimeout(200)
        msg = QDBusMessage.createMethodCall(bus, path,
                                            "org.kde.StatusNotifierItem", "Activate")
        msg.setArguments([0, 0])
        self._bus.asyncCall(msg)
        # NOTE: opening the app does NOT clear the mail badge -- it stays until the
        # app itself drops its unread indicator (icon revert), detected elsewhere.

    def show_clipboard(self) -> None:
        # Klipper's old /klipper popup method is gone when there is no panel
        # applet exporting it; open the clipboard plasmoid in a window instead
        # (shows the same history list, works without a Plasma panel).
        self.run_builtin(["plasmawindowed", "org.kde.plasma.clipboard"])

    @staticmethod
    def run_builtin(argv) -> None:
        if not argv:
            return
        try:
            run_detached(list(argv))
        except Exception:  # noqa: BLE001
            log.exception("run_builtin fehlgeschlagen: %s", argv)
