"""Check and self-install input access (udev uaccess + uinput)."""
from __future__ import annotations

import logging
import os
import shutil
import stat
import subprocess
import tempfile

from .procenv import child_env

log = logging.getLogger(__name__)

_SETUP_SCRIPT = r"""#!/bin/bash
set -e
cat > /etc/udev/rules.d/70-karyon.rules <<'RULES'
KERNEL=="event*", SUBSYSTEM=="input", TAG+="uaccess"
KERNEL=="uinput", SUBSYSTEM=="misc", TAG+="uaccess", OPTIONS+="static_node=uinput"
RULES
modprobe uinput || true
echo uinput > /etc/modules-load.d/karyon-uinput.conf
udevadm control --reload-rules
udevadm trigger --subsystem-match=input
udevadm trigger --subsystem-match=misc --name-match=uinput
"""


def has_input_access() -> bool:
    """True only if a real MOUSE is readable AND /dev/uinput is writable."""
    if not _uinput_writable():
        return False
    return _mouse_readable()


def _uinput_writable() -> bool:
    return os.access("/dev/uinput", os.W_OK)


def _mouse_readable() -> bool:
    try:
        from .input_proxy import InputProxy
        from evdev import InputDevice, list_devices
    except Exception:  # noqa: BLE001
        return False
    for path in list_devices():
        try:
            dev = InputDevice(path)
        except Exception:  # noqa: BLE001
            continue
        try:
            if InputProxy._is_mouse(dev):
                dev.close()
                return True
        except Exception:  # noqa: BLE001
            pass
        finally:
            try:
                dev.close()
            except Exception:  # noqa: BLE001
                pass
    return False


def setup_available() -> bool:
    return shutil.which("pkexec") is not None


def run_setup() -> bool:
    if not setup_available():
        return False
    # Write the setup script to a temp file (root cannot read a FUSE AppImage).
    fd, path = tempfile.mkstemp(prefix="karyon-setup-", suffix=".sh")
    try:
        with os.fdopen(fd, "w") as f:
            f.write(_SETUP_SCRIPT)
        os.chmod(path, stat.S_IRWXU | stat.S_IRGRP | stat.S_IROTH)
        result = subprocess.run(["pkexec", "bash", path], env=child_env())
        return result.returncode == 0
    except Exception:  # noqa: BLE001
        log.exception("Failed to set up permissions")
        return False
    finally:
        try:
            os.unlink(path)
        except Exception:  # noqa: BLE001
            pass
