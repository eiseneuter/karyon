"""JSON configuration: load/save with type-checked merge of known keys only."""
from __future__ import annotations

import json
import logging
import os
from pathlib import Path

log = logging.getLogger(__name__)

CONFIG_DIR = Path(os.path.expanduser("~/.config/karyon"))
CONFIG_PATH = CONFIG_DIR / "config.json"
DATA_DIR = Path(os.path.expanduser("~/.local/share/karyon"))

DEFAULTS: dict = {
    "hold_ms": 121,
    "drag_threshold_px": 14,
    "trigger_button": "right",
    "cancel_button": "left",
    "trigger_key": 125,
    "mouse_speed": 0.0,
    "scale": 1.5,
    "accent": "#37d0ff",
    "game_mode": True,
    "performance_mode": False,
    "max_recent_apps": 7,
    "max_recent_files": 12,
    "adjust_volume_with_trigger_wheel": True,
    "volume_steps": 1,
    "show_desktop": True,
    "show_all_apps": True,
    "show_tray": True,
    "show_session": True,
    "show_favorites": True,
    "show_apps": True,
    "show_recent_apps": True,
    "show_recent_files": True,
    "focus_window_switcher": True,
    "hub_show_clock": True,
    "hub_show_date": False,
    "hub_show_charge": False,
    "hub_show_monitor": True,
    "mail_notification": True,
    "transparency": 20,
    "category_anim": True,
    "gestures_enabled": True,
    "gesture_left": "select_all",
    "gesture_left_up": "copy",
    "gesture_left_down": "cut",
    "gesture_right": "paste",
    "gesture_right_up": "redo",
    "gesture_right_down": "undo",
    "gesture_up": "maximize",
    "gesture_down": "enter",
    "gesture_custom_apps": {},
    "gesture_custom_keys": {},   # direction -> evdev keycode for "Custom Key" gesture
    "gesture_min_speed": 1300,
    "gesture_diagonal_size": 51,
    "gesture_time_window": 200,
    "game_inhibit_apps": [
        "steam_app_", "wine", "retroarch", "gzdoom", "dosbox", "scummvm",
        "minecraft", "ryujinx", "yuzu", "dolphin-emu", "pcsx2", "rpcs3",
        "citra", "cemu", ".exe"
    ],
    "game_allow_apps": [
        "steam",
    ],
    "overlay_cursor": "ring",
}


class Config:
    def __init__(self) -> None:
        self._data = dict(DEFAULTS)
        self.load()

    # -- dict-like access ---------------------------------------------------
    def __getitem__(self, key):
        return self._data[key]

    def __setitem__(self, key, value):
        self._data[key] = value

    def get(self, key, default=None):
        return self._data.get(key, default)

    def as_dict(self) -> dict:
        return dict(self._data)

    # -- persistence --------------------------------------------------------
    def load(self) -> None:
        try:
            raw = json.loads(CONFIG_PATH.read_text(encoding="utf-8"))
        except FileNotFoundError:
            return
        except Exception as exc:  # noqa: BLE001
            log.warning("Konfiguration konnte nicht gelesen werden: %s", exc)
            return
        if not isinstance(raw, dict):
            return
        for key, default in DEFAULTS.items():
            if key not in raw:
                continue
            value = raw[key]
            # Only accept values whose type matches the default's type.
            if isinstance(default, bool):
                if isinstance(value, bool):
                    self._data[key] = value
            elif isinstance(default, int) and not isinstance(default, bool):
                if isinstance(value, (int, float)) and not isinstance(value, bool):
                    self._data[key] = type(default)(value)
            elif isinstance(default, float):
                if isinstance(value, (int, float)) and not isinstance(value, bool):
                    self._data[key] = float(value)
            elif isinstance(default, str):
                if isinstance(value, str):
                    self._data[key] = value
            elif isinstance(default, dict):
                if isinstance(value, dict):
                    self._data[key] = dict(value)
            elif isinstance(default, list):
                if isinstance(value, list):
                    self._data[key] = list(value)
        # Keyboard triggering was removed -> migrate legacy custom_key to a mouse
        # button so such configs keep working.
        if self._data.get("trigger_button") == "custom_key":
            self._data["trigger_button"] = "right"

    def save(self) -> None:
        try:
            CONFIG_DIR.mkdir(parents=True, exist_ok=True)
            CONFIG_PATH.write_text(
                json.dumps(self._data, indent=2, ensure_ascii=False),
                encoding="utf-8",
            )
        except Exception as exc:  # noqa: BLE001
            log.warning("Konfiguration konnte nicht gespeichert werden: %s", exc)
