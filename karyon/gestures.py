"""Mouse gestures: direction classification, action list, execution."""
from __future__ import annotations

import logging
import subprocess

from .input_proxy import classify_direction  # re-exported for callers
from .procenv import child_env, run_detached

log = logging.getLogger(__name__)

DIRECTIONS = ["left", "left_up", "left_down", "right", "right_up", "right_down",
              "up", "down"]

# Action list in exact order (KDE English labels).
ACTIONS: list[tuple[str, str]] = [
    ("none", "None"),
    ("forward", "Forward"),
    ("back", "Back"),
    ("reload", "Reload"),
    ("minimize", "Minimize"),
    ("maximize", "Maximize/Restore"),
    ("close", "Close Window"),
    ("show_desktop", "Show Desktop"),
    ("next_desktop", "Switch to Next Desktop"),
    ("prev_desktop", "Switch to Previous Desktop"),
    ("custom_app", "Custom Application…"),
    ("krunner", "KRunner"),
    ("printscreen", "Print Screen"),
    ("copy", "Copy"),
    ("cut", "Cut"),
    ("paste", "Paste"),
    ("select_all", "Select All"),
    ("undo", "Undo"),
    ("redo", "Redo"),
    ("enter", "Enter"),
    ("clipboard", "Show Clipboard"),
    ("playpause", "Play/Pause"),
    ("next_track", "Next Track"),
    ("prev_track", "Previous Track"),
    ("mute", "Mute"),
    ("custom_key", "Custom Key…"),
]

ACTION_LABELS = {key: label for key, label in ACTIONS}

# Actions that require the previously-focused window before executing.
FOCUS_ACTIONS = {"minimize", "maximize", "close", "back", "forward", "reload",
                 "copy", "cut", "paste", "select_all", "undo", "redo", "enter",
                 "custom_key"}

# Layouts whose letter positions differ from US QWERTY.  evdev keycodes are
# POSITIONAL, so injecting KEY_Z on a German (QWERTZ) keyboard produces 'y'.
_QWERTZ = {"de", "at", "ch", "cz", "sk", "hu", "hr", "si", "rs", "ba", "me"}
_AZERTY = {"fr", "be"}


def _active_layout() -> str:
    """Primary keyboard layout code (e.g. 'de'); 'us' if unknown."""
    import os
    try:
        p = os.path.expanduser("~/.config/kxkbrc")
        if os.path.exists(p):
            for line in open(p, encoding="utf-8", errors="replace"):
                if line.startswith("LayoutList="):
                    val = line.split("=", 1)[1].strip()
                    if val:
                        return val.split(",")[0].strip().lower()
    except Exception:  # noqa: BLE001
        pass
    try:
        out = subprocess.run(["localectl", "status"], capture_output=True,
                             text=True, timeout=1).stdout
        for line in out.splitlines():
            if "X11 Layout:" in line:
                return line.split(":", 1)[1].strip().split(",")[0].strip().lower()
    except Exception:  # noqa: BLE001
        pass
    return "us"


def _letter(e, ch: str):
    """evdev keycode that produces ``ch`` on the active layout (handles the
    QWERTZ Y/Z swap and the AZERTY a/q/z/w/m shifts)."""
    base = _active_layout().split("(")[0]
    if base in _QWERTZ:
        ch = {"z": "y", "y": "z"}.get(ch, ch)
    elif base in _AZERTY:
        ch = {"a": "q", "q": "a", "z": "w", "w": "z"}.get(ch, ch)
    return getattr(e, "KEY_" + ch.upper())


# Key combos via uinput (resolved lazily so evdev import stays optional).
def _key_combos():
    from evdev import ecodes as e
    return {
        "forward": [e.KEY_LEFTALT, e.KEY_RIGHT],
        "back": [e.KEY_LEFTALT, e.KEY_LEFT],
        "reload": [e.KEY_F5],
        "printscreen": [e.KEY_SYSRQ],
        "volume_up": [e.KEY_VOLUMEUP],
        "volume_down": [e.KEY_VOLUMEDOWN],
        "playpause": [e.KEY_PLAYPAUSE],
        "mute": [e.KEY_MUTE],
        "next_track": [e.KEY_NEXTSONG],
        "prev_track": [e.KEY_PREVIOUSSONG],
        "copy": [e.KEY_LEFTCTRL, _letter(e, "c")],
        "cut": [e.KEY_LEFTCTRL, _letter(e, "x")],
        "paste": [e.KEY_LEFTCTRL, _letter(e, "v")],
        "select_all": [e.KEY_LEFTCTRL, _letter(e, "a")],
        "undo": [e.KEY_LEFTCTRL, _letter(e, "z")],
        "redo": [e.KEY_LEFTCTRL, e.KEY_LEFTSHIFT, _letter(e, "z")],
        "enter": [e.KEY_ENTER],
    }


# KWin shortcuts via kglobalaccel.
_KWIN = {
    "minimize": "Window Minimize",
    "maximize": "Window Maximize",
    "close": "Window Close",
    "show_desktop": "Show Desktop",
    "next_desktop": "Switch to Next Desktop",
    "prev_desktop": "Switch to Previous Desktop",
}


def invoke_kwin_shortcut(name: str) -> None:
    cmd = ["qdbus6", "org.kde.kglobalaccel", "/component/kwin",
           "org.kde.kglobalaccel.Component.invokeShortcut", name]
    try:
        run_detached(cmd)
    except Exception:  # noqa: BLE001
        log.exception("kglobalaccel-Shortcut fehlgeschlagen: %s", name)


class GestureExecutor:
    def __init__(self, proxy, config) -> None:
        self.proxy = proxy
        self.config = config

    def direction_action(self, direction: str) -> str:
        return self.config.get(f"gesture_{direction}", "none")

    def execute(self, action: str, direction: str = "") -> None:
        if action in ("none", ""):
            return
        combos = None
        try:
            combos = _key_combos()
        except Exception:  # noqa: BLE001
            combos = {}
        if action in combos:
            self.proxy.send_keys(combos[action])
        elif action in _KWIN:
            invoke_kwin_shortcut(_KWIN[action])
        elif action == "krunner":
            self._dbus("org.kde.krunner", "/App",
                       "org.kde.krunner.App.display")
        elif action == "clipboard":
            # /klipper's popup method is unavailable without a panel applet;
            # open the clipboard plasmoid in a window (same history list).
            try:
                run_detached(["plasmawindowed", "org.kde.plasma.clipboard"])
            except Exception:  # noqa: BLE001
                log.exception("Clipboard-Geste fehlgeschlagen")
        elif action == "custom_app":
            self._launch_custom(direction)
        elif action == "custom_key":
            keys = self.config.get("gesture_custom_keys", {}) or {}
            code = keys.get(direction)
            if code:
                self.proxy.send_keys([int(code)])

    def _launch_custom(self, direction: str) -> None:
        mapping = self.config.get("gesture_custom_apps", {}) or {}
        exec_line = mapping.get(direction)
        if not exec_line:
            return
        try:
            import shlex
            parts = [p for p in shlex.split(exec_line) if not p.startswith("%")]
            run_detached(parts)
        except Exception:  # noqa: BLE001
            log.exception("Custom-App-Geste fehlgeschlagen")

    @staticmethod
    def _dbus(service: str, path: str, method: str) -> None:
        try:
            run_detached(["qdbus6", service, path, method])
        except Exception:  # noqa: BLE001
            log.exception("Gesten-DBus fehlgeschlagen: %s", method)
