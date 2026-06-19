"""Session actions (Logout/Lock/Suspend/Hibernate/Restart/Shutdown)."""
from __future__ import annotations

import logging
import subprocess

from .procenv import child_env

log = logging.getLogger(__name__)

# (key, label, destructive)
SESSION_ACTIONS = [
    ("logout", "Log Out", True),
    ("lock", "Lock", False),
    ("suspend", "Suspend", False),
    ("hibernate", "Hibernate", False),
    ("reboot", "Restart", True),
    ("shutdown", "Shut Down", True),
]


def _dbus(service: str, path: str, iface_method: str, *args: str) -> None:
    cmd = ["qdbus6", service, path, iface_method, *args]
    try:
        subprocess.Popen(cmd, env=child_env())
    except Exception:  # noqa: BLE001
        log.exception("Session-DBus fehlgeschlagen: %s", iface_method)


def is_destructive(key: str) -> bool:
    for k, _label, destructive in SESSION_ACTIONS:
        if k == key:
            return destructive
    return False


def run_action(key: str) -> None:
    if key == "logout":
        _dbus("org.kde.Shutdown", "/Shutdown",
              "org.kde.Shutdown.logout")
    elif key == "reboot":
        _dbus("org.kde.Shutdown", "/Shutdown",
              "org.kde.Shutdown.logoutAndReboot")
    elif key == "shutdown":
        _dbus("org.kde.Shutdown", "/Shutdown",
              "org.kde.Shutdown.logoutAndShutdown")
    elif key == "lock":
        _dbus("org.freedesktop.ScreenSaver", "/ScreenSaver",
              "org.freedesktop.ScreenSaver.Lock")
    elif key == "suspend":
        _dbus("org.freedesktop.login1", "/org/freedesktop/login1",
              "org.freedesktop.login1.Manager.Suspend", "true")
    elif key == "hibernate":
        _dbus("org.freedesktop.login1", "/org/freedesktop/login1",
              "org.freedesktop.login1.Manager.Hibernate", "true")
