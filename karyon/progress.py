"""App task progress via the Unity LauncherEntry DBus API.

This is the same broadcast signal Plasma's task manager / system tray use to show
a progress bar on an app's launcher icon (Dolphin file operations, downloads,
Kdenlive renders, ...).  We listen for it and expose progress per app so the
overlay can draw a ring around the matching window symbol.
"""
from __future__ import annotations

import logging

from PyQt6.QtCore import QObject, pyqtSignal, pyqtSlot
from PyQt6.QtDBus import QDBusConnection, QDBusVariant

log = logging.getLogger(__name__)

_IFACE = "com.canonical.Unity.LauncherEntry"


def _norm(name: str) -> str:
    """Normalise an app id / launcher uri to a bare lowercase desktop id."""
    s = name or ""
    if s.startswith("application://"):
        s = s[len("application://"):]
    s = s.rsplit("/", 1)[-1]
    if s.endswith(".desktop"):
        s = s[:-len(".desktop")]
    return s.lower()


def _val(v):
    """Unwrap a QDBusVariant / QVariant to a plain Python value."""
    if isinstance(v, QDBusVariant):
        return v.variant()
    return v


class ProgressMonitor(QObject):
    changed = pyqtSignal()

    def __init__(self) -> None:
        super().__init__()
        self._progress: dict[str, float] = {}
        self._bus = QDBusConnection.sessionBus()
        ok = self._bus.connect("", "", _IFACE, "Update", self._on_update)
        if not ok:
            log.info("LauncherEntry-Signal nicht verbunden (kein Fortschritt)")

    @pyqtSlot(str, "QVariantMap")
    def _on_update(self, uri: str, props) -> None:  # noqa: N802
        try:
            before = dict(self._progress)
            key = _norm(uri)
            visible = bool(_val(props.get("progress-visible", False)))
            progress = _val(props.get("progress", 0.0))
            if visible and progress is not None:
                self._progress[key] = max(0.0, min(1.0, float(progress)))
            else:
                self._progress.pop(key, None)
            if self._progress != before:
                self.changed.emit()
        except Exception:  # noqa: BLE001
            pass

    def get(self, *names: str):
        """First known progress (0..1) for any of the given app ids, else None."""
        for n in names:
            if not n:
                continue
            p = self._progress.get(_norm(n))
            if p is not None:
                return p
        return None

    def any_active(self) -> bool:
        return bool(self._progress)
