"""KWin bridge: window/cursor snapshot, activate/minimize/close, show-desktop.

KWin scripting has no direct return value, so we register our own DBus service
(object ``/result``, method ``result(data)``) that loaded scripts call back into.
loadScript/run ACKs are synchronous (~ms); script *execution* runs async.
An optimistic cache lets the menu open without waiting on KWin.
"""
from __future__ import annotations

import itertools
import json
import logging
import os
import tempfile
from pathlib import Path

from PyQt6.QtCore import QObject, QTimer, pyqtSlot
from PyQt6.QtDBus import QDBusConnection, QDBusConnectionInterface, QDBusInterface

log = logging.getLogger(__name__)

RESULT_SERVICE = "org.dumblauncher.Result"
RESULT_PATH = "/result"
RESULT_IFACE = "org.dumblauncher.Result"

_SNAPSHOT_JS = """
var wins = workspace.windowList ? workspace.windowList() : workspace.stackingOrder;
var stack = workspace.stackingOrder || [];
function stackIndex(w) {
    for (var k = 0; k < stack.length; k++) { if (stack[k] === w) return k; }
    return -1;
}
var out = [];
for (var i = 0; i < wins.length; i++) {
    var w = wins[i];
    out.push({
        id: w.internalId.toString(),
        rc: w.resourceClass,
        caption: w.caption,
        df: w.desktopFileName,
        pid: w.pid,
        min: w.minimized,
        minz: w.minimizable,
        normal: w.normalWindow,
        act: w.active,
        stk: stackIndex(w)
    });
}
var c = workspace.cursorPos;
var payload = JSON.stringify({rid: "__RID__", cursor: [c.x, c.y], wins: out});
callDBus("%(svc)s", "%(path)s", "%(iface)s", "result", payload);
""" % {"svc": RESULT_SERVICE, "path": RESULT_PATH, "iface": RESULT_IFACE}

_WITH_WINDOW_JS = """
var wins = workspace.windowList ? workspace.windowList() : workspace.stackingOrder;
for (var i = 0; i < wins.length; i++) {
    var w = wins[i];
    if (w.internalId.toString() == "__ID__") {
        __ACTION__
        break;
    }
}
"""

_VERIFY_JS = """
var wins = workspace.windowList ? workspace.windowList() : workspace.stackingOrder;
var stack = workspace.stackingOrder || [];
var a = workspace.activeWindow;
var target = null;
for (var i = 0; i < wins.length; i++) {
    if (wins[i].internalId.toString() == "__ID__") { target = wins[i]; break; }
}
var top = false;
if (target && stack.length) {
    for (var k = stack.length - 1; k >= 0; k--) {
        var sw = stack[k];
        if (sw.minimized) continue;
        if (sw.normalWindow === false) continue;        // docks/panels/osd
        if (sw.resourceClass === "karyon") continue;
        top = (sw.internalId.toString() == "__ID__");
        break;
    }
}
var payload = JSON.stringify({rid: "__RID__",
    active: a ? a.internalId.toString() : "",
    top: top, found: target != null});
callDBus("%(svc)s", "%(path)s", "%(iface)s", "result", payload);
""" % {"svc": RESULT_SERVICE, "path": RESULT_PATH, "iface": RESULT_IFACE}

# Check whether the active window is fullscreen (for game / 3D-app detection).
_FULLSCREEN_JS = """
var a = workspace.activeWindow;
var fs = a ? (a.fullScreen === true) : false;
callDBus("%(svc)s", "%(path)s", "%(iface)s", "result",
         JSON.stringify({rid: "__RID__", fullscreen: fs}));
""" % {"svc": RESULT_SERVICE, "path": RESULT_PATH, "iface": RESULT_IFACE}

_SHOW_DESKTOP_JS = """
var wins = workspace.windowList ? workspace.windowList() : workspace.stackingOrder;
var ignore = ["karyon", "python3", "plasmashell", "org.kde.plasmashell"];
var visible = []; var minimized = [];
for (var i = 0; i < wins.length; i++) {
    var w = wins[i];
    if (ignore.indexOf(w.resourceClass) >= 0) continue;
    if (w.normalWindow === false) continue;
    if (w.minimizable === false) continue;
    if (w.minimized) minimized.push(w); else visible.push(w);
}
// Decide AND act on the live state (no racy cache): any window showing -> hide
// them all; otherwise restore every minimized window.
if (visible.length > 0) {
    for (var j = 0; j < visible.length; j++) visible[j].minimized = true;
} else {
    for (var k = 0; k < minimized.length; k++) minimized[k].minimized = false;
}
"""


class _ResultReceiver(QObject):
    def __init__(self, bridge: "KWinBridge"):
        super().__init__()
        self._bridge = bridge

    @pyqtSlot(str)
    def result(self, data: str) -> None:  # noqa: D401 - DBus slot
        self._bridge._on_result(data)


class KWinBridge:
    def __init__(self) -> None:
        self._bus = QDBusConnection.sessionBus()
        self._counter = itertools.count(1)
        self._pending: dict[str, tuple] = {}
        self._tmpdir = Path(tempfile.mkdtemp(prefix="karyon-kwin-"))

        # Optimistic cache of the last snapshot.
        self.cached_snapshot: dict | None = None
        self._desktop_minimized: list[str] = []
        self._activate_gen = 0
        self._daemon_plugin: str | None = None
        self._daemon_path: Path | None = None

        self._receiver = _ResultReceiver(self)
        # Use ReplaceExistingService so a restart after a crash/update never
        # blocks on the previous (dying) instance still holding the name.
        reg = self._bus.interface().registerService(
            RESULT_SERVICE,
            QDBusConnectionInterface.ServiceQueueOptions.ReplaceExistingService,
            QDBusConnectionInterface.ServiceReplacementOptions.AllowReplacement,
        )
        if reg.value() == QDBusConnectionInterface.RegisterServiceReply.ServiceNotRegistered:
            log.warning("DBus-Result-Service konnte nicht registriert werden")
        ok = self._bus.registerObject(
            RESULT_PATH, RESULT_IFACE, self._receiver,
            QDBusConnection.RegisterOption.ExportAllSlots,
        )
        if not ok:
            log.warning("DBus-Result-Objekt konnte nicht registriert werden")

    # -- DBus callback ------------------------------------------------------
    def _on_result(self, data: str) -> None:
        try:
            payload = json.loads(data)
        except Exception:  # noqa: BLE001
            return
        rid = str(payload.get("rid", ""))
        if rid == "fullscreen_event":
            if hasattr(self, "_on_fullscreen_event") and self._on_fullscreen_event:
                try:
                    self._on_fullscreen_event(payload)
                except Exception:  # noqa: BLE001
                    log.exception("Fullscreen-Event-Callback fehlgeschlagen")
            return
        entry = self._pending.pop(rid, None)
        if entry is None:
            return
        callback, timer = entry
        if timer is not None:
            timer.stop()
        try:
            callback(payload)
        except Exception:  # noqa: BLE001
            log.exception("Snapshot-Callback fehlgeschlagen")

    # -- script execution ---------------------------------------------------
    def _run_script(self, js: str, on_result=None, rid: str = "", timeout_ms: int = 300):
        # Process-unique plugin name so we never collide with scripts left loaded
        # by a previous (e.g. crashed) instance -> loadScript would return -1.
        plugin = f"dl_{os.getpid()}_{next(self._counter)}"
        path = self._tmpdir / f"{plugin}.js"
        try:
            path.write_text(js, encoding="utf-8")
        except Exception:  # noqa: BLE001
            log.exception("KWin-Script konnte nicht geschrieben werden")
            return

        if on_result is not None and rid:
            timer = QTimer()
            timer.setSingleShot(True)
            timer.timeout.connect(lambda r=rid: self._timeout(r))
            timer.start(timeout_ms)
            self._pending[rid] = (on_result, timer)

        # Synchronous loadScript, but with a SHORT timeout so a busy/hung KWin can
        # never wedge the launcher for the default 25s.  Combined with the prompt
        # unload + no verify, KWin stays responsive, so this returns in a few ms.
        iface = QDBusInterface("org.kde.KWin", "/Scripting",
                               "org.kde.kwin.Scripting", self._bus)
        iface.setTimeout(900)
        reply = iface.call("loadScript", str(path), plugin)
        args = reply.arguments()
        if not args:
            log.warning("loadScript ohne Antwort: %s", reply.errorMessage())
            self._pending.pop(rid, None)
            self._unload(plugin, path)
            return
        sid = args[0]
        # A negative/invalid id would build an invalid DBus path.
        if not isinstance(sid, int) or sid < 0:
            log.warning("loadScript lieferte ungueltige id: %r", sid)
            self._pending.pop(rid, None)
            self._unload(plugin, path)
            return
        script = QDBusInterface("org.kde.KWin", f"/Scripting/Script{sid}",
                                "org.kde.kwin.Script", self._bus)
        script.setTimeout(900)
        script.call("run")
        # Unload promptly (the script runs in a few ms) so loaded scripts never
        # pile up across rapid activations and overload KWin's engine.
        QTimer.singleShot(500, lambda p=plugin, f=path: self._unload(p, f))

    def _run_fire(self, js: str) -> None:
        self._run_script(js, on_result=None)

    def _unload(self, plugin: str, path: Path) -> None:
        try:
            iface = QDBusInterface("org.kde.KWin", "/Scripting",
                                   "org.kde.kwin.Scripting", self._bus)
            iface.setTimeout(900)
            iface.call("unloadScript", plugin)
        except Exception:  # noqa: BLE001
            pass
        try:
            path.unlink(missing_ok=True)
        except Exception:  # noqa: BLE001
            pass

    def _timeout(self, rid: str) -> None:
        self._pending.pop(rid, None)

    # -- snapshot -----------------------------------------------------------
    def snapshot_async(self, on_result, timeout_ms: int = 300) -> None:
        rid = f"snap{next(self._counter)}"
        js = _SNAPSHOT_JS.replace("__RID__", rid)

        def wrapped(payload):
            snap = self._normalize(payload)
            self.cached_snapshot = snap
            on_result(snap)

        self._run_script(js, on_result=wrapped, rid=rid, timeout_ms=timeout_ms)

    def _normalize(self, payload: dict) -> dict:
        cursor = payload.get("cursor") or [0, 0]
        windows = []
        seen_ids: set[str] = set()
        for w in payload.get("wins", []):
            wid = w.get("id", "")
            # KWin's windowList() can occasionally list the same window twice
            # (transient stacking hiccups); a duplicate id would split one app
            # into two ring-2 groups (e.g. two Dolphin symbols).  Drop repeats.
            if not wid or wid in seen_ids:
                continue
            seen_ids.add(wid)
            windows.append({
                "id": wid,
                "rc": w.get("rc", "") or "",
                "caption": w.get("caption", "") or "",
                "desktop_file": w.get("df", "") or "",
                "pid": int(w.get("pid", 0) or 0),
                "minimized": bool(w.get("min", False)),
                "minimizable": bool(w.get("minz", True)),
                "normal": bool(w.get("normal", True)),
                "active": bool(w.get("act", False)),
                "stack": int(w.get("stk", -1)),
            })
        return {"cursor": (int(cursor[0]), int(cursor[1])), "windows": windows}

    def _prime_windows(self, snap: dict) -> None:
        self.cached_snapshot = snap

    # -- window actions -----------------------------------------------------
    def _with_window(self, win_id: str, action_js: str) -> None:
        js = _WITH_WINDOW_JS.replace("__ID__", win_id).replace("__ACTION__", action_js)
        self._run_fire(js)

    # Re-assert the activation a FEW times.  The overlay steals focus and KWin
    # restores the previous window once when the overlay closes (~50-150ms
    # later); a shot after that restore wins.  Kept deliberately sparse: hammering
    # activeWindow many times in a burst desynced KWin's stacking from its focus
    # (windows stacked at the same spot received clicks meant for the focused one).
    _ACTIVATE_SHOTS = (0, 100, 240)

    def activate(self, win_id: str) -> None:
        self._activate_gen += 1
        gen = self._activate_gen
        for delay in self._ACTIVATE_SHOTS:
            QTimer.singleShot(delay, lambda g=gen: self._do_activate(win_id, g))

    def _do_activate(self, win_id: str, gen: int) -> None:
        if gen != self._activate_gen:
            return  # superseded by a newer activation
        self._cache_activate(win_id)
        # Assigning activeWindow both focuses AND raises the window on KWin -- no
        # keepAbove toggle.  The old true->false force-raise stuck keepAbove=true
        # on some KWin versions (window left always-on-top: "click X, Dolphin
        # stays in front"), and the separate un-pin could be dropped under the
        # script load.  The multi-shot timing alone wins the focus battle against
        # KWin restoring the previous window when the overlay closes.
        action = ("w.minimized = false;"
                  " workspace.activeWindow = w;")
        self._with_window(win_id, action)

    def _unpin(self, win_id: str) -> None:
        # Only clears a stuck pin; does not un-raise (keepAbove already false ->
        # no restacking).
        self._with_window(win_id, "w.keepAbove = false;")

    def minimize(self, win_id: str) -> None:
        self._cache_set_minimized(win_id, True)
        self._with_window(win_id, "w.minimized = true;")

    def toggle_maximize(self, win_id: str) -> None:
        action = """
            if (w.minimized) w.minimized = false;
            var maxed = (typeof w.maximizeMode !== 'undefined' && w.maximizeMode === 3) || 
                        (w.width >= workspace.clientArea(0, w).width - 10 && w.height >= workspace.clientArea(0, w).height - 10);
            if (maxed) {
                w.setMaximize(false, false);
            } else {
                w.setMaximize(true, true);
            }
        """
        self._with_window(win_id, action)

    def close(self, win_id: str) -> None:
        self._cache_remove(win_id)
        self._with_window(win_id, "w.closeWindow();")

    def _cache_remove(self, win_id: str) -> None:
        snap = self.cached_snapshot
        if not snap:
            return
        snap["windows"] = [w for w in snap["windows"] if w["id"] != win_id]

    def focus_window(self, win_id: str) -> None:
        # Gentle focus: set keyboard focus only, do NOT raise (keepAbove toggle)
        # -- raising clears the selection in some apps (e.g. Dolphin), which
        # would break a copy/cut/paste gesture.
        self._cache_activate(win_id)
        self._with_window(win_id, "workspace.activeWindow = w;")

    def _cache_activate(self, win_id: str) -> None:
        snap = self.cached_snapshot
        if not snap:
            return
        max_stack = max((w.get("stack", -1) for w in snap["windows"]), default=0)
        for w in snap["windows"]:
            w["active"] = (w["id"] == win_id)
            if w["id"] == win_id:
                w["minimized"] = False
                w["stack"] = max_stack + 1

    def _cache_set_minimized(self, win_id: str, value: bool) -> None:
        snap = self.cached_snapshot
        if not snap:
            return
        for w in snap["windows"]:
            if w["id"] == win_id:
                w["minimized"] = value

    # -- show desktop (minimise toggle; decision+action atomic in KWin) ------
    def toggle_show_desktop(self) -> None:
        # The decision (minimise all vs. restore all) is made INSIDE the KWin
        # script on the live window state -- our cached snapshot is too racy
        # (a stale late snapshot could overwrite the optimistic update and leave
        # the toggle stuck restoring forever).  Own minimise (not KWin's Show
        # Desktop mode, which is cancelled the moment our overlay maps).
        self._run_fire(_SHOW_DESKTOP_JS)

    # -- fullscreen detection (game / 3D-app inhibit) -----------------------
    def start_fullscreen_daemon(self, callback) -> None:
        """Load a persistent KWin script to monitor active window changes and
        fullscreen state changes, notifying us via DBus results.
        """
        self._on_fullscreen_event = callback
        js = """
        var connected = {};
        var prevActive = workspace.activeWindow;
        function reportFS(w) {
            var fs = w ? (w.fullScreen === true) : false;
            var rc = w ? (w.resourceClass || "") : "";
            callDBus("%(svc)s", "%(path)s", "%(iface)s", "result",
                     JSON.stringify({rid: "fullscreen_event", fullscreen: fs, rc: rc}));
        }
        workspace.windowActivated.connect(function(w) {
            if (prevActive && prevActive !== w) {
                try {
                    prevActive.keepAbove = false;
                } catch (e) {}
            }
            prevActive = w;
            if (w) {
                var id = w.internalId.toString();
                if (!connected[id]) {
                    w.fullScreenChanged.connect(function() {
                        if (workspace.activeWindow === w) {
                            reportFS(w);
                        }
                    });
                    connected[id] = true;
                }
            }
            reportFS(w);
        });
        // Initial state report
        reportFS(workspace.activeWindow);
        """ % {"svc": RESULT_SERVICE, "path": RESULT_PATH, "iface": RESULT_IFACE}
        self._run_persistent_script(js)

    def _run_persistent_script(self, js: str) -> None:
        plugin = f"dl_daemon_{os.getpid()}_{next(self._counter)}"
        path = self._tmpdir / f"{plugin}.js"
        try:
            path.write_text(js, encoding="utf-8")
        except Exception:  # noqa: BLE001
            log.exception("KWin-Daemon-Script konnte nicht geschrieben werden")
            return

        iface = QDBusInterface("org.kde.KWin", "/Scripting",
                               "org.kde.kwin.Scripting", self._bus)
        iface.setTimeout(900)
        reply = iface.call("loadScript", str(path), plugin)
        args = reply.arguments()
        if not args:
            log.warning("loadScript fuer Daemon ohne Antwort: %s", reply.errorMessage())
            return
        sid = args[0]
        if not isinstance(sid, int) or sid < 0:
            log.warning("loadScript fuer Daemon lieferte ungueltige id: %r", sid)
            return
        script = QDBusInterface("org.kde.KWin", f"/Scripting/Script{sid}",
                                "org.kde.kwin.Script", self._bus)
        script.setTimeout(900)
        script.call("run")
        self._daemon_plugin = plugin
        self._daemon_path = path

    def stop_fullscreen_daemon(self) -> None:
        if self._daemon_plugin and self._daemon_path:
            self._unload(self._daemon_plugin, self._daemon_path)
            self._daemon_plugin = None
            self._daemon_path = None
