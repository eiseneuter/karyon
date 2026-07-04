"""Entry point: wires everything together."""
from __future__ import annotations

import logging
import os
import signal
import subprocess
import sys
import time

from PyQt6.QtCore import QLockFile, QObject, Qt, QTimer, pyqtSignal
from PyQt6.QtGui import QIcon
from PyQt6.QtWidgets import QApplication, QMenu, QMessageBox, QSystemTrayIcon

from . import APP_NAME, permissions
from .apps import AppIndex
from .config import Config, DATA_DIR
from .flash import GestureFlash
from .gestures import FOCUS_ACTIONS, GestureExecutor
from .input_proxy import InputProxy
from .kwin import KWinBridge
from .overlay import RadialOverlay
from .perf import timed
from .procenv import child_env, run_detached
from .recent_files import RecentFiles
from .tray import TrayManager

log = logging.getLogger(__name__)

KWIN_RULE_UUID = "dab10000-0000-4000-8000-000000000001"


def app_icon() -> QIcon:
    """The launcher's own icon (bundled karyon.svg) for the window /
    taskbar / tray; falls back to a theme icon if the svg is missing."""
    svg = os.path.join(os.path.dirname(__file__), "karyon.svg")
    if os.path.exists(svg):
        ic = QIcon(svg)
        if not ic.isNull():
            return ic
    return QIcon.fromTheme("input-mouse")


# ---------------------------------------------------------------------------
def _sanitize_environment() -> None:
    # Remove env vars that break foreign processes / Qt under the AppImage.
    for var in ("QT_QPA_PLATFORMTHEME",):
        pass  # keep platform theme; nothing destructive here
    os.environ.pop("SESSION_MANAGER", None)


def _ensure_kwin_taskbar_rule() -> None:
    """Idempotently write the KWin window rule so the overlay never appears in
    taskbar/pager/switcher."""
    try:
        out = subprocess.run(
            ["kreadconfig6", "--file", "kwinrulesrc", "--group", "General",
             "--key", "rules"],
            capture_output=True, text=True, env=child_env())
        rules = (out.stdout or "").strip()
        new_rules = rules
        if KWIN_RULE_UUID not in rules:
            new_rules = f"{rules},{KWIN_RULE_UUID}" if rules else KWIN_RULE_UUID

        def w(key, value):
            subprocess.run(["kwriteconfig6", "--file", "kwinrulesrc",
                            "--group", KWIN_RULE_UUID, "--key", key, value],
                           env=child_env())
        w("Description", "karyon overlay")
        w("wmclass", "karyon")
        w("wmclassmatch", "2")
        w("wmclasscomplete", "false")
        w("skiptaskbar", "true")
        w("skiptaskbarrule", "2")
        w("skippager", "true")
        w("skippagerrule", "2")
        w("skipswitcher", "true")
        w("skipswitcherrule", "2")
        w("above", "true")
        w("aboverule", "2")
        # Force into KWin's OnScreenDisplay layer (8) which is above the
        # fullscreen/active layer, so the overlay renders over any fullscreen
        # window (video player, image viewer, browser fullscreen, etc.).
        w("layer", "8")
        w("layerrule", "2")
        subprocess.run(["kwriteconfig6", "--file", "kwinrulesrc",
                        "--group", "General", "--key", "count",
                        str(len(new_rules.split(",")))], env=child_env())
        subprocess.run(["kwriteconfig6", "--file", "kwinrulesrc",
                        "--group", "General", "--key", "rules", new_rules],
                       env=child_env())
        subprocess.run(["qdbus6", "org.kde.KWin", "/KWin", "reconfigure"],
                       env=child_env())
    except Exception:  # noqa: BLE001
        log.debug("KWin-Regel konnte nicht geschrieben werden", exc_info=True)


def _ensure_overlay_focus_rule() -> None:
    """The overlay must never accept focus, so closing it never makes KWin
    restore focus to the previously-active window (which would override our
    window activation -- the cause of the unreliable switching)."""
    uuid = KWIN_RULE_UUID
    try:
        out = subprocess.run(
            ["kreadconfig6", "--file", "kwinrulesrc", "--group", uuid,
             "--key", "acceptfocus"],
            capture_output=True, text=True, env=child_env())
        if (out.stdout or "").strip() == "false":
            return

        def w(key, value):
            subprocess.run(["kwriteconfig6", "--file", "kwinrulesrc",
                            "--group", uuid, "--key", key, value], env=child_env())
        w("acceptfocus", "false")
        w("acceptfocusrule", "2")
        subprocess.run(["qdbus6", "org.kde.KWin", "/KWin", "reconfigure"],
                       env=child_env())
        log.info("Overlay-acceptfocus=false gesetzt (Fensterwechsel).")
    except Exception:  # noqa: BLE001
        log.debug("acceptfocus-Regel konnte nicht gesetzt werden", exc_info=True)


def _ensure_focus_stealing_off() -> None:
    """Reliable window switching needs focus-stealing prevention off, otherwise
    activating a window from the launcher is delayed ~1s (§0.1)."""
    try:
        out = subprocess.run(
            ["kreadconfig6", "--file", "kwinrc", "--group", "Windows",
             "--key", "FocusStealingPreventionLevel"],
            capture_output=True, text=True, env=child_env())
        if (out.stdout or "").strip() == "0":
            return
        subprocess.run(["kwriteconfig6", "--file", "kwinrc", "--group", "Windows",
                        "--key", "FocusStealingPreventionLevel", "0"],
                       env=child_env())
        subprocess.run(["qdbus6", "org.kde.KWin", "/KWin", "reconfigure"],
                       env=child_env())
        log.info("Focus-Stealing-Prevention auf 0 gesetzt (Fensterwechsel).")
    except Exception:  # noqa: BLE001
        log.debug("FSP-Level konnte nicht gesetzt werden", exc_info=True)


# ---------------------------------------------------------------------------
class _Bridge(QObject):
    """Lift input-proxy callbacks (other thread) into the GUI thread."""
    hold = pyqtSignal()
    release = pyqtSignal()
    cancel = pyqtSignal()
    motion = pyqtSignal(float, float)
    gesture = pyqtSignal(str)
    key_captured = pyqtSignal(int)
    press = pyqtSignal()
    volume_scroll = pyqtSignal(int)
    trigger_mute = pyqtSignal()
    external_update = pyqtSignal()


class Launcher:
    def __init__(self, app: QApplication) -> None:
        self.app = app
        with timed("Config"):
            self.config = Config()
        with timed("KWinBridge"):
            self.kwin = KWinBridge()
        with timed("AppIndex.scan"):
            self.app_index = AppIndex()
            self.app_index.scan()
        with timed("AppIndex.prewarm"):
            self.app_index.prewarm()
        with timed("RecentFiles"):
            self.recent_files = RecentFiles()
        with timed("TrayManager"):
            self.tray = TrayManager()
        with timed("ProgressMonitor"):
            from .progress import ProgressMonitor
            self.progress = ProgressMonitor()
        with timed("AudioMonitor"):
            from .audio import AudioMonitor
            self.audio = AudioMonitor()
            self.audio.start()
        with timed("RadialOverlay"):
            self.overlay = RadialOverlay(self.config, self.kwin, self.app_index,
                                         self.tray, self.recent_files,
                                         self.progress, self.audio)
        with timed("GestureFlash"):
            self.flash = GestureFlash()
        with timed("SettingsPanel"):
            from .panels import SettingsPanel
            self.settings = SettingsPanel(self.config)
            self.settings.closed.connect(self._on_settings_closed)

        self._gesture_win_id = ""
        self._last_fs_payload = None
        self._fresh_snap_pending = False
        self._press_time = 0.0
        self._wire_overlay()
        self._init_tray()

        # Signal bridge + input proxy
        self.bridge = _Bridge()
        self.bridge.hold.connect(self._on_hold, Qt.ConnectionType.QueuedConnection)
        self.bridge.release.connect(self._on_release, Qt.ConnectionType.QueuedConnection)
        self.bridge.cancel.connect(self._on_cancel, Qt.ConnectionType.QueuedConnection)
        self.bridge.motion.connect(self._on_motion, Qt.ConnectionType.QueuedConnection)
        self.bridge.gesture.connect(self._on_gesture, Qt.ConnectionType.QueuedConnection)
        self.bridge.key_captured.connect(self._on_key_captured,
                                         Qt.ConnectionType.QueuedConnection)
        self.bridge.press.connect(self._on_press, Qt.ConnectionType.QueuedConnection)
        self.bridge.volume_scroll.connect(self._on_volume_scroll,
                                          Qt.ConnectionType.QueuedConnection)
        self.bridge.trigger_mute.connect(self._on_trigger_mute,
                                         Qt.ConnectionType.QueuedConnection)
        self.bridge.external_update.connect(self.overlay.request_repaint,
                                            Qt.ConnectionType.QueuedConnection)
        self.audio.set_on_change(self.bridge.external_update.emit)
        self.progress.changed.connect(self.overlay.request_repaint)
        self.tray.changed.connect(self._on_tray_changed)

        self.proxy = InputProxy(
            self.config,
            on_hold=self.bridge.hold.emit,
            on_release=self.bridge.release.emit,
            on_cancel=self.bridge.cancel.emit,
            on_motion=lambda x, y: self.bridge.motion.emit(x, y),
            on_gesture=self.bridge.gesture.emit,
            on_key_captured=self.bridge.key_captured.emit,
            on_press=self.bridge.press.emit,
            on_volume_scroll=self.bridge.volume_scroll.emit,
            on_trigger_mute=self.bridge.trigger_mute.emit,
        )
        self.proxy.start()
        self.executor = GestureExecutor(self.proxy, self.config)
        self.settings.bind_proxy(self.proxy)

        # Start persistent KWin fullscreen monitor daemon script.
        def on_fullscreen(payload: dict) -> None:
            self._last_fs_payload = payload
            self._trigger_fs_check(payload)
        self.kwin.start_fullscreen_daemon(on_fullscreen)

    # -- wiring -------------------------------------------------------------
    def _wire_overlay(self) -> None:
        self.overlay.request_session.connect(self._on_session)
        self.overlay.request_settings.connect(self._open_settings)
        self.overlay.request_setup_input.connect(self._setup_input)
        self.overlay.request_quit.connect(self.quit)

    def _on_tray_changed(self) -> None:
        if self.overlay.isVisible():
            snap = self.kwin.cached_snapshot
            if snap is None:
                snap = {"cursor": self._fallback_cursor(), "windows": []}
            self.overlay.refresh_model(snap)

    def _init_tray(self) -> None:
        self.tray_icon = QSystemTrayIcon(app_icon(), self.app)
        self.tray_icon.setToolTip("Karyon")
        menu = QMenu()
        menu.addAction("Settings", self._open_settings)
        menu.addAction("Set up input access…", self._setup_input)
        menu.addSeparator()
        menu.addAction("Quit", self.quit)
        self.tray_icon.setContextMenu(menu)
        self._tray_menu = menu  # keep ref
        self.tray_icon.activated.connect(self._on_tray_activated)
        self.tray_icon.show()

    def _on_tray_activated(self, reason) -> None:
        if reason == QSystemTrayIcon.ActivationReason.Trigger:
            self._open_settings()

    # -- input proxy callbacks (now in GUI thread) --------------------------
    def _on_press(self) -> None:
        self._press_time = time.monotonic()
        self._fresh_snap_pending = True
        # Prime a fresh snapshot the moment the trigger goes down.
        # If the hold completes, the pending callback will update the overlay.
        def on_snap_done(snap):
            self._fresh_snap_pending = False
            if self.overlay.isVisible():
                self._late_snapshot(snap)
        self.kwin.snapshot_async(on_snap_done, timeout_ms=300)

    def _on_hold(self) -> None:
        snap = self.kwin.cached_snapshot
        self._open_with(snap)
        # Once the overlay surface is mapped, nudge the compositor pointer so KWin
        # hides the frozen hardware cursor sitting on top of the overlay.
        QTimer.singleShot(50, self.proxy.nudge_cursor)
        
        # If the snapshot from _on_press has not completed yet, and has not timed out,
        # we do not trigger a second snapshot. The callback will handle the update.
        timed_out = (time.monotonic() - self._press_time) > 0.35
        if not self._fresh_snap_pending or timed_out:
            self._fresh_snap_pending = True
            self.kwin.snapshot_async(self._late_snapshot, timeout_ms=300)

    def _open_with(self, snap) -> None:
        if snap is None:
            snap = {"cursor": self._fallback_cursor(), "windows": []}
        self._gesture_win_id = next(
            (w["id"] for w in snap.get("windows", []) if w.get("active")), "")
        self.overlay.open(snap)

    def _late_snapshot(self, snap) -> None:
        self._fresh_snap_pending = False
        # Rebuild the model in place with fresh data while the menu is open,
        # keeping the user's current sector/pointer.
        if self.overlay.isVisible():
            self._gesture_win_id = next(
                (w["id"] for w in snap.get("windows", []) if w.get("active")),
                self._gesture_win_id)
            self.overlay.refresh_model(snap)

    def _fallback_cursor(self) -> tuple:
        screen = self.app.primaryScreen().geometry()
        return (screen.width() // 2, screen.height() // 2)

    def _on_motion(self, x: float, y: float) -> None:
        if self.overlay.isVisible():
            self.overlay.set_pointer(x, y)

    def _on_release(self) -> None:
        # Gestures are triggered by the movement itself (during the flick), never
        # by releasing the trigger -- so on release we only activate a symbol.
        if not self.overlay.isVisible():
            return
        self.overlay.on_release()

    def _on_cancel(self) -> None:
        if self.overlay.isVisible():
            self.overlay.close_menu()

    def _on_gesture(self, direction: str) -> None:
        # Fired during the flick motion (menu may or may not be open).
        self._run_gesture(direction)

    def _run_gesture(self, direction: str) -> None:
        # Close the menu first so no highlighted symbol is activated.
        if self.overlay.isVisible():
            self.overlay.close_menu()
        
        # A flick gesture can fire without the menu ever opening, so refresh the
        # focus target from the live snapshot -- a stale id would re-activate the
        # WRONG window and break copy/paste (and clear the selection).
        snap = self.kwin.cached_snapshot
        if snap:
            self._gesture_win_id = next(
                (w["id"] for w in snap.get("windows", []) if w.get("active")),
                self._gesture_win_id)
        action = self.executor.direction_action(direction)
        accent = self.config.get("accent", "#37d0ff")
        rel = getattr(self.proxy, "_last_gesture_rel", (0.0, 0.0))

        # Flash immediately (no snapshot round-trip, so it always shows).
        path, geo = self._gesture_flash_path(rel)
        self.flash.play(path, accent, geo)

        self.executor.execute(action, direction)

    def _gesture_flash_path(self, rel):
        """Light-streak along the gesture travel, in coordinates local to the
        screen.  ``rel`` is the raw flick delta (screen px, forwarded 1:1)."""
        cursor = (self.kwin.cached_snapshot or {}).get("cursor")
        screen = self.overlay._screen_for_cursor(cursor)
        geo = screen.geometry()
        
        if cursor:
            # Pure flick: the cursor moved 1:1, so it is now at the END.
            cx, cy = cursor[0] - geo.x(), cursor[1] - geo.y()
            sx, sy, ex, ey = cx - rel[0], cy - rel[1], cx, cy
        else:
            # No cursor known: fall back to the screen centre.
            cx, cy = geo.width() // 2, geo.height() // 2
            sx, sy, ex, ey = cx - rel[0], cy - rel[1], cx, cy
            
        # Shift the whole streak 200px further along the gesture direction.
        import math
        dist = math.hypot(rel[0], rel[1])
        if dist > 1:
            ox, oy = rel[0] / dist * 200.0, rel[1] / dist * 200.0
            sx, sy, ex, ey = sx + ox, sy + oy, ex + ox, ey + oy
        pts = []
        for i in range(13):
            f = i / 12.0
            pts.append((f, sx + f * (ex - sx), sy + f * (ey - sy)))
        return pts, geo

    def _on_key_captured(self, code: int) -> None:
        self.settings.set_captured_key(code)

    def _on_volume_scroll(self, direction: int) -> None:
        step = max(1, int(self.config.get("volume_steps", 1)))
        suffix = "+" if direction > 0 else "-"
        amount = step * max(1, abs(int(direction)))
        try:
            run_detached(
                ["wpctl", "set-volume", "@DEFAULT_AUDIO_SINK@", f"{amount}%{suffix}"]
            )
        except Exception:  # noqa: BLE001
            log.debug("Lautstärke konnte nicht per Mausrad geändert werden",
                      exc_info=True)

    def _on_trigger_mute(self) -> None:
        try:
            run_detached(
                ["wpctl", "set-mute", "@DEFAULT_AUDIO_SINK@", "toggle"]
            )
        except Exception:  # noqa: BLE001
            log.debug("Mute konnte nicht per Maus-Trigger geändert werden",
                      exc_info=True)

    # -- session ------------------------------------------------------------
    def _on_session(self, key: str) -> None:
        from . import session
        if session.is_destructive(key):
            box = QMessageBox()
            box.setWindowTitle("Karyon")
            box.setText(f"Really {key}?")
            box.setStandardButtons(QMessageBox.StandardButton.Yes
                                   | QMessageBox.StandardButton.No)
            box.setDefaultButton(QMessageBox.StandardButton.No)
            box.raise_()
            box.activateWindow()
            if box.exec() != QMessageBox.StandardButton.Yes:
                return
        session.run_action(key)

    # -- settings / tray menu actions ---------------------------------------
    def _open_settings(self) -> None:
        self.settings.show()
        self.settings.raise_()
        self.settings.activateWindow()

    def _on_settings_closed(self) -> None:
        self.overlay._compute_geometry()
        if self._last_fs_payload is not None:
            self._trigger_fs_check(self._last_fs_payload)
        else:
            self.proxy.set_inhibit(False)

    def _trigger_fs_check(self, payload: dict) -> None:
        if not self.config.get("game_mode", True):
            self._set_game_mode(False)
            return
            
        rc = str(payload.get("rc", "")).lower()
        inhibit = False
        
        if rc:
            # 1. Fallback: manual inhibit list
            games = self.config.get("game_inhibit_apps", [])
            inhibit = any(g.lower() in rc for g in games)
            
            # 2. Smart detection: .desktop category
            if not inhibit:
                app = self.app_index.match_window(rc)
                if app and "Game" in getattr(app, "categories", set()):
                    inhibit = True
                    
        self._set_game_mode(inhibit, rc)

    def _set_game_mode(self, active: bool, rc: str = "") -> None:
        if self.proxy.inhibit == active:
            return
            
        log.info("Game Mode (rc=%s): %s", rc, active)
        self.proxy.set_inhibit(active)
        
        # Deep Sleep: freeze background polling while playing a game
        if active:
            self.tray._notif_timer.stop()
        else:
            self.tray._notif_timer.start()

    def _setup_input(self) -> None:
        _prompt_input_setup(self.app, force=True)

    # -- lifecycle ----------------------------------------------------------
    def quit(self) -> None:
        try:
            self.kwin.stop_fullscreen_daemon()
        except Exception:  # noqa: BLE001
            pass
        try:
            self.proxy.stop()
        except Exception:  # noqa: BLE001
            pass
        self.app.quit()


# ---------------------------------------------------------------------------
def _prompt_input_setup(app: QApplication, force: bool = False) -> None:
    if not force and permissions.has_input_access():
        return
    if not permissions.setup_available():
        box = QMessageBox()
        box.setWindowTitle("Karyon")
        box.setIcon(QMessageBox.Icon.Warning)
        box.setText("pkexec is not available, so input access cannot be set up "
                    "automatically. Please install polkit (pkexec).")
        box.exec()
        return
    box = QMessageBox()
    box.setWindowTitle("Karyon")
    box.setText("Karyon needs one-time access to mouse input in order to "
                "detect the middle mouse button globally.")
    box.setInformativeText("This installs a udev rule. A password dialog "
                           "(administrator rights) will appear next.")
    set_up = box.addButton("Set up", QMessageBox.ButtonRole.AcceptRole)
    box.addButton("Later", QMessageBox.ButtonRole.RejectRole)
    box.exec()
    if box.clickedButton() is not set_up:
        return
    if not permissions.run_setup():
        return
    # Poll up to 2s for access.
    import time
    for _ in range(20):
        if permissions.has_input_access():
            return
        time.sleep(0.1)
    note = QMessageBox()
    note.setWindowTitle("Karyon")
    note.setText("Input access was set up. If the middle mouse button is not "
                 "detected, please log out and back in once.")
    note.exec()


# ---------------------------------------------------------------------------
def main() -> int:
    _sanitize_environment()
    _ensure_kwin_taskbar_rule()
    _ensure_overlay_focus_rule()
    _ensure_focus_stealing_off()

    debug = ("--debug" in sys.argv
             or os.environ.get("KARYON_DEBUG") == "1")
    level = logging.DEBUG if debug else logging.INFO
    handlers = [logging.StreamHandler()]
    if debug:
        handlers.append(logging.FileHandler("/tmp/karyon-debug.log"))
    logging.basicConfig(level=level,
                        format="%(asctime)s %(levelname)s %(name)s: %(message)s",
                        handlers=handlers)

    # Dump every thread's stack on SIGUSR1 -- lets us diagnose a wedge/hang
    # (e.g. an overlay stuck open) without py-spy: `pkill -USR1 -f karyon`.
    try:
        import faulthandler
        faulthandler.register(signal.SIGUSR1, all_threads=True)
    except Exception:  # noqa: BLE001
        pass

    app = QApplication(sys.argv)
    app.setApplicationName(APP_NAME)
    app.setDesktopFileName(APP_NAME)
    app.setQuitOnLastWindowClosed(False)
    # Dark palette as a baseline.  Do NOT force Fusion: under Fusion combos render
    # from the palette, which KDE's platform theme re-applies from the system
    # colour scheme (-> black).  The native style honours stylesheet backgrounds,
    # so combos are coloured by per-widget stylesheets (see panels._style_combo)
    # and the window stays dark via the panel's explicit stylesheet backgrounds.
    from .panels import _dark_palette
    app.setPalette(_dark_palette())
    QIcon.setThemeName(QIcon.themeName() or "breeze-dark")
    app.setWindowIcon(app_icon())

    from PyQt6.QtDBus import QDBusConnection, QDBusConnectionInterface
    reply = QDBusConnection.sessionBus().interface().registerService(
        "org.eisen.karyon.instance",
        QDBusConnectionInterface.ServiceQueueOptions.DontQueueService,
        QDBusConnectionInterface.ServiceReplacementOptions.DontAllowReplacement
    )
    if reply.value() != QDBusConnectionInterface.RegisterServiceReply.ServiceRegistered:
        from PyQt6.QtWidgets import QMessageBox
        box = QMessageBox()
        box.setWindowTitle("Karyon")
        box.setIcon(QMessageBox.Icon.Information)
        box.setText("Karyon is already running.")
        box.setInformativeText("Only one instance of Karyon can run at a time.")
        box.addButton(QMessageBox.StandardButton.Ok)
        box.exec()
        return 1

    _access = permissions.has_input_access()
    log.info("Eingabe-Zugriff vorhanden: %s (pkexec: %s)",
             _access, permissions.setup_available())
    if not _access:
        _prompt_input_setup(app)

    launcher = Launcher(app)
    launcher.overlay.play_intro()

    signal.signal(signal.SIGINT, lambda *a: launcher.quit())
    signal.signal(signal.SIGTERM, lambda *a: launcher.quit())
    keepalive = QTimer()
    keepalive.start(1000)
    keepalive.timeout.connect(lambda: None)

    log.info("Karyon ready - hold %s trigger for %d ms.",
             launcher.config.get("trigger_button", "right"), launcher.config["hold_ms"])
    res = app.exec()
    os._exit(res)
