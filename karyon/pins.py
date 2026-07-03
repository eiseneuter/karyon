"""Pin storage: persist pinned apps and files across sessions."""
from __future__ import annotations

import json
import logging
import os
from pathlib import Path

from .config import DATA_DIR

log = logging.getLogger(__name__)

PINS_PATH = DATA_DIR / "pins.json"


class PinStore:
    """Manages pinned app IDs and file paths with JSON persistence."""

    def __init__(self) -> None:
        self._apps: list[str] = []
        self._files: list[str] = []
        self.load()

    # -- public API ---------------------------------------------------------
    @property
    def pinned_apps(self) -> list[str]:
        return list(self._apps)

    @property
    def pinned_files(self) -> list[str]:
        return list(self._files)

    def is_app_pinned(self, app_id: str) -> bool:
        return app_id in self._apps

    def is_file_pinned(self, path: str) -> bool:
        return path in self._files

    def pin_app(self, app_id: str) -> None:
        if app_id and app_id not in self._apps:
            self._apps.append(app_id)
            self._save()

    def unpin_app(self, app_id: str) -> None:
        if app_id in self._apps:
            self._apps.remove(app_id)
            self._save()

    def pin_file(self, path: str) -> None:
        if path and path not in self._files:
            self._files.append(path)
            self._save()

    def unpin_file(self, path: str) -> None:
        if path in self._files:
            self._files.remove(path)
            self._save()

    # -- persistence --------------------------------------------------------
    def load(self) -> None:
        if not PINS_PATH.exists():
            return
        try:
            with open(PINS_PATH, "r") as f:
                data = json.load(f)
            if isinstance(data.get("apps"), list):
                self._apps = [s for s in data["apps"] if isinstance(s, str)]
            if isinstance(data.get("files"), list):
                self._files = [s for s in data["files"] if isinstance(s, str)]
        except Exception:
            log.warning("Failed to load pins from %s", PINS_PATH, exc_info=True)

    def _save(self) -> None:
        try:
            DATA_DIR.mkdir(parents=True, exist_ok=True)
            with open(PINS_PATH, "w") as f:
                json.dump({"apps": self._apps, "files": self._files}, f, indent=2)
        except Exception:
            log.warning("Failed to save pins to %s", PINS_PATH, exc_info=True)
