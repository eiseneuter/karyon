"""Clean environment for spawning foreign processes.

When running from the AppImage we inject our bundled libs via LD_LIBRARY_PATH.
Foreign system processes (Qt apps, udevadm, kcmshell6, ...) must NOT inherit
those bundled paths or they fail to start.  ``child_env`` restores the system
values that AppRun stashed away.
"""
from __future__ import annotations

import os


def child_env() -> dict:
    env = dict(os.environ)

    # Restore the system LD_LIBRARY_PATH that AppRun saved before overriding it.
    saved = env.pop("KARYON_SYS_LD_LIBRARY_PATH", None)
    if saved is not None:
        if saved:
            env["LD_LIBRARY_PATH"] = saved
        else:
            env.pop("LD_LIBRARY_PATH", None)

    appdir = env.get("KARYON_APPDIR")
    if appdir:
        # Strip any path that points back into our AppDir from loader-relevant vars.
        for var in ("LD_LIBRARY_PATH", "PATH", "PYTHONPATH", "QT_PLUGIN_PATH",
                    "QML2_IMPORT_PATH", "GTK_PATH", "GST_PLUGIN_PATH"):
            value = env.get(var)
            if not value:
                continue
            parts = [p for p in value.split(os.pathsep)
                     if p and not p.startswith(appdir)]
            if parts:
                env[var] = os.pathsep.join(parts)
            else:
                env.pop(var, None)

    env.pop("KARYON_APPDIR", None)
    env.pop("APPDIR", None)
    env.pop("APPIMAGE", None)
    env.pop("ARGV0", None)

    # Always hand launched apps the user's real login shell (the launcher may
    # have been started from a different shell, e.g. during development).
    try:
        import pwd
        env["SHELL"] = pwd.getpwuid(os.getuid()).pw_shell
    except Exception:  # noqa: BLE001
        pass
    return env
