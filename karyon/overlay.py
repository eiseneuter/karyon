"""The radial overlay menu -- geometry, model, hover/drill, drawing, glyphs.

This is the heart of the launcher.  Coordinates: 0deg = right, 90deg = down
(screen coordinates).  All sizes scale with ``self.s``.
"""
from __future__ import annotations

from .pins import PinStore

import logging
import math
import os
import time
from dataclasses import dataclass, field

from PyQt6.QtCore import (Qt, QTimer, QPoint, QPointF, QRect, QRectF,
                          QMimeDatabase, pyqtSignal)
from PyQt6.QtGui import (QColor, QGuiApplication, QIcon, QPainter, QPainterPath,
                         QPen, QPolygonF, QRadialGradient, QFont, QFontMetrics)
from PyQt6.QtWidgets import QWidget

log = logging.getLogger(__name__)

# -- colors -----------------------------------------------------------------
CYAN = QColor("#37d0ff")
RED = QColor("#ff5c6a")
GREEN = QColor("#4cdf6b")

# -- sectors ----------------------------------------------------------------
SEC_WINDOWS = 0
SEC_APPS = 1
SEC_FILES = 2

# -- node kinds -------------------------------------------------------------
IT_WINDOW_GROUP = "window_group"
IT_WINDOW = "window"
IT_SHOW_DESKTOP = "show_desktop"
IT_TRAY_MENU = "tray_menu"
IT_TRAY = "tray"
IT_MENU = "menu"
IT_APP = "app"
IT_CATEGORY = "category"
IT_SESSION = "session"
IT_FAV_MENU = "fav_menu"
IT_FILE = "file"
IT_CTRL_BTN = "ctrl_btn"
IT_TRAY_MENUITEM = "tray_menuitem"   # an entry from an app's DBus context menu
IT_MAIL = "mail"                     # new-message segment at the end of ring 2
IT_MEDIA_BTN = "media_btn"           # MPRIS media controls

# -- angle constants --------------------------------------------------------
ANG_GAP = 0.6
RING2_SLOT = 28.0
APPS_SPAN = 180.0
SECTOR_MARGIN = 0.66
RING2_SPAN = 180.0 - 2 * SECTOR_MARGIN
RING3_SPAN = 200.0
RING2_FIT = 8


@dataclass
class Node:
    kind: str
    label: str = ""
    sublabel: str = ""
    icon: object = None            # QIcon or None
    data: object = None
    angle: float = 0.0             # center angle in degrees
    radius: float = 0.0            # center radius
    size: float = 0.0
    half_deg: float = 0.0          # half angular width
    closable: bool = False
    card: bool = False
    passive: bool = False
    icon_scale: float = 1.0
    render_icon: bool = False      # draw a glyph instead of label text
    hover_t: float = 0.0
    control: object = None         # control_key for drilldown controls
    glyph: str = ""                # named glyph to draw
    children: list = field(default_factory=list)
    sector: int = -1
    row: int = 0
    count: int = 0
    pinned: bool = False
    pending_close: bool = False


def approach(value: float, target: float, rate: float) -> float:
    nv = value + (target - value) * rate
    if abs(nv - target) < 0.0008:
        return target
    return nv


def _norm(a: float) -> float:
    while a <= -180:
        a += 360
    while a > 180:
        a -= 360
    return a


def _ang_dist(a: float, b: float) -> float:
    return abs(_norm(a - b))


# Icon resolution fallback.  QIcon.fromTheme() only finds an icon if it lives in
# the ACTIVE theme (or a theme it inherits).  Many apps ship their icon outside
# that theme -- as a bare pixmap in /usr/share/pixmaps, inside another theme's
# directory tree (e.g. a monochrome theme with no Inherits), or as an absolute
# path in the .desktop file.  For all of those fromTheme() returns null and the
# app would render as plain text.  _icon_for closes that gap with a direct file
# search across every standard freedesktop icon location.

_PIXMAP_EXTS = (".png", ".svg", ".xpm", ".svgz")
# Prefer larger sizes; the painter scales down to the segment.
_ICON_SIZES = ("48", "32", "24", "22", "16", "scalable", "symbolic")
_ICON_CATS = ("apps", "devices", "places", "categories", "mimetypes", "actions")
_icon_cache: dict[str, QIcon] = {}


def _icon_search_roots() -> list[str]:
    """All directories that may hold pixmaps or icon-theme trees."""
    roots = ["/usr/share/pixmaps", os.path.expanduser("~/.local/share/pixmaps")]
    for base in (os.environ.get("XDG_DATA_DIRS")
                 or "/usr/local/share:/usr/share").split(":"):
        base = base.strip()
        if base:
            roots.append(os.path.join(base, "icons"))
    home_data = os.environ.get("XDG_DATA_HOME") or os.path.expanduser("~/.local/share")
    roots.append(os.path.join(home_data, "icons"))
    seen, out = set(), []
    for r in roots:
        if r and r not in seen:
            seen.add(r)
            out.append(r)
    return out


def _icon_for(name: str) -> QIcon:
    """Resolve an app icon generically.

    Handles three shapes an icon can take:
      1. a theme name present in the active theme  -> QIcon.fromTheme
      2. an absolute file path                      -> load the file directly
      3. a name only present as a bare pixmap or in
         another theme's directory tree             -> direct file search

    Results are cached per name."""
    if not name:
        return QIcon()
    
    # Map common window classes/executables without direct icons to standard icons
    if name.lower() in ("soffice", "soffice.bin"):
        name = "libreoffice"

    cached = _icon_cache.get(name)
    if cached is not None:
        return cached
    # 2) absolute path (some .desktop files set Icon=/opt/app/icon.png).
    if name.startswith("/"):
        qi = QIcon(name)
        _icon_cache[name] = qi
        return qi
    # 1) active theme.
    ic = QIcon.fromTheme(name)
    if not ic.isNull():
        _icon_cache[name] = ic
        return ic
    low = name.lower()
    roots = _icon_search_roots()
    # 3a) bare pixmap: <root>/<name>.<ext>
    for root in roots:
        for ext in _PIXMAP_EXTS:
            p = os.path.join(root, low + ext)
            if os.path.exists(p):
                qi = QIcon(p)
                if not qi.isNull():
                    _icon_cache[name] = qi
                    return qi
    # 3b) inside any icon-theme tree: <root>/<theme>/<size>/<cat>/<name>.<ext>
    for root in roots:
        if not os.path.isdir(root):
            continue
        for theme in os.listdir(root):
            tdir = os.path.join(root, theme)
            if not os.path.isdir(tdir):
                continue
            for cat in _ICON_CATS:
                for size in _ICON_SIZES:
                    # Try both <size>/<cat> (standard) and <cat>/<size>
                    for cdir in (os.path.join(tdir, size, cat), os.path.join(tdir, cat, size)):
                        if not os.path.isdir(cdir):
                            continue
                        for ext in _PIXMAP_EXTS:
                            p = os.path.join(cdir, low + ext)
                            if os.path.exists(p):
                                qi = QIcon(p)
                                if not qi.isNull():
                                    _icon_cache[name] = qi
                                    return qi
    _icon_cache[name] = ic
    return ic


class RadialOverlay(QWidget):
    def _is_light(self) -> bool:
        return getattr(self, "config", {}).get("theme", "dark") == "light"
        
    def _seg_base(self) -> QColor:
        return QColor(238, 242, 248) if self._is_light() else QColor(28, 32, 42)

    def _seg_hover(self) -> QColor:
        return QColor(190, 200, 215) if self._is_light() else QColor(72, 80, 97)

    def _glyph_color(self) -> QColor:
        return QColor(40, 48, 60) if self._is_light() else QColor(232, 238, 246)

    def _cyan(self) -> QColor:
        return QColor("#008eb3") if self._is_light() else QColor(CYAN)

    def _green(self) -> QColor:
        return QColor("#1f9b3c") if self._is_light() else QColor(GREEN)

    def _red(self) -> QColor:
        return QColor("#db2b3b") if self._is_light() else QColor(RED)

    def _orange(self) -> QColor:
        return QColor("#d18300") if self._is_light() else QColor(255, 170, 0)

    request_gesture = pyqtSignal(str)
    request_session = pyqtSignal(str)
    request_reactivate = pyqtSignal()
    request_settings = pyqtSignal()
    request_setup_input = pyqtSignal()
    request_quit = pyqtSignal()
    closed = pyqtSignal()
    request_media = pyqtSignal(str)

    def __init__(self, config, kwin, app_index, tray, recent_files, progress=None,
                 audio=None):
        super().__init__(None)
        self.config = config
        self.kwin = kwin
        self.app_index = app_index
        self.tray = tray
        self.recent_files = recent_files
        self.progress = progress
        self.audio = audio
        self.pins = PinStore()

        self.setWindowFlags(
            Qt.WindowType.Tool
            | Qt.WindowType.FramelessWindowHint
            | Qt.WindowType.WindowStaysOnTopHint
            | Qt.WindowType.WindowDoesNotAcceptFocus
        )
        self.setAttribute(Qt.WidgetAttribute.WA_TranslucentBackground)
        self.setAttribute(Qt.WidgetAttribute.WA_NoSystemBackground)
        self.setAttribute(Qt.WidgetAttribute.WA_ShowWithoutActivating)
        self.setFocusPolicy(Qt.FocusPolicy.NoFocus)
        self.setCursor(Qt.CursorShape.BlankCursor)
        self.setWindowTitle("Karyon")

        self._tick_timer = QTimer(self)
        self._tick_timer.setSingleShot(True)
        self._tick_timer.timeout.connect(self._tick)

        self._mail_blink_timer = QTimer(self)
        self._mail_blink_timer.setSingleShot(True)
        self._mail_blink_timer.timeout.connect(self._on_mail_blink_timeout)
        self._mail_blink_state = 0
        self.switch_mode_active_category = SEC_WINDOWS

        self._intro_stage = -1
        self._intro_word = "KARYON"
        self._intro_timer = QTimer(self)
        self._intro_timer.timeout.connect(self._intro_tick)

        self._glyph_cache: dict = {}
        self._icon_pixmap_cache: dict = {}
        self._path_cache: dict = {}
        self._mimedb = QMimeDatabase()
        # Authoritative most-recently-used window list, maintained by US (the
        # snapshot is racy right after an activation), most-recent first.
        self._mru: list[str] = []
        # App-group MRU: tracks resourceClass strings so that ALL windows of an
        # app inherit the recency of the most recently activated sibling.
        self._mru_rc: list[str] = []
        self._last_activate_time = 0.0
        self._reset_state()
        self._compute_geometry()

    # -- geometry -----------------------------------------------------------
    def _compute_geometry(self) -> None:
        self._static_frame_cache = None
        self._current_frame = None
        # Scale adjusted: user requested that the new 1.0 = old 1.5.
        s = float(self.config["scale"]) * 1.5
        self.s = s
        gap = 2 * s                       # uniform gap between ALL rings
        self.r_neutral = (2.5 / 1.5) * s  # 5px-diameter neutral centre at s=1.5
        self.r_hub = 46 * s
        self.seg_depth = 45 * s
        self.seg3_depth = 45 * s
        self.r2_in = self.r_hub + gap
        self.r2_out = self.r2_in + self.seg_depth
        self.r2c = self.r2_in + self.seg_depth / 2
        self.r3_in = self.r2_out + gap
        self.r3_out = self.r3_in + self.seg3_depth
        self.r3c = self.r3_in + self.seg3_depth / 2
        self.r4c = self.r3_out + gap + self.seg3_depth / 2
        # Close + drill bars share the same width: 13px at scale 1.5, growing
        # with the configured menu size.
        self.close_zone = (13 / 1.5) * s
        self.drill_zone = (13 / 1.5) * s
        self.bar_w = (13 / 1.5) * s
        # Standard PHYSICAL segment width at the outer edge (a ring-2 segment,
        # 8 per half-circle).  EVERY ring/row uses this same physical width, so
        # the angular slot is derived as width/radius -> ring-3/4 and overflow
        # rows are exactly as wide as ring 2 (not bigger at larger radius).
        self._seg_phys_w = self.r2_out * math.radians(180.0 / RING2_FIT)
        self._gap_phys = self.r2_out * math.radians(ANG_GAP)
        self._seg_arc_std = self.r2c * math.radians(
            (180.0 - 2 * SECTOR_MARGIN) / RING2_FIT - ANG_GAP)

    # -- state --------------------------------------------------------------
    def _reset_state(self) -> None:
        self.ring2: dict[int, list[Node]] = {SEC_WINDOWS: [], SEC_APPS: [], SEC_FILES: []}
        self._ring2_base: dict[int, list[Node]] = {SEC_WINDOWS: [], SEC_APPS: [], SEC_FILES: []}
        self.active_sectors: list[int] = []
        self.open_sector = -1
        self._sector_arc: dict[int, tuple] = {}
        self._sector_vis: dict[int, tuple] = {}    # visual target arc (ring 1)
        self._sector_draw: dict[int, list] = {}    # animated drawn arc
        self.hover_node: Node | None = None
        self.hover_close = False
        self.hover_close_all = False
        self.hover_pin = False
        self.hover_mute = False
        self.hover_mail = False
        self.hover_volume_area = False
        self.open_group: Node | None = None
        self._ctrl_node: Node | None = None
        self._ctrl_t = 0.0
        self._repeat_last = 0.0
        self._ctrl_on_button = False   # pointer strictly ON a control segment
        self._drill_armed = False
        self._win_lock = False
        self._cursor_oy = 0.0         # persistent vertical cursor offset (px)
        self._apps_stage = 0          # 0 = recents, 1 = categories
        self._apps_recent_nodes: list = []
        self._apps_cat_nodes: list = []
        self._apps_morph_t = 1.0      # 0->1 width-grow animation on morph
        self._center = QPointF(0, 0)
        self._pointer = QPointF(0, 0)
        self._hover_dirty = False             # pointer moved since last hover recompute
        self._prev_pointer = QPointF(0, 0)   # last painted cursor, for region erase
        self._last_title_rect = QRect()      # last painted title pill, for erase
        self._frame_requested = False
        self._cursor_overridden = False      # app override-cursor (hide sys cursor)
        self._origin = QPointF(0, 0)
        self._open_time = 0.0
        self._path: list[tuple] = []
        self._gesture_fired = False
        self.open_t = 0.0
        self._active_window_id = ""
        self._desktop_min: list[str] = []

    # -- open / close -------------------------------------------------------
    def _screen_for_cursor(self, cursor):
        if cursor:
            s = QGuiApplication.screenAt(QPoint(int(cursor[0]), int(cursor[1])))
            if s is not None:
                return s
        return QGuiApplication.primaryScreen()

    def play_intro(self) -> None:
        self._intro_stage = 0
        self._compute_geometry()
        
        screen = QGuiApplication.primaryScreen()
        geo = screen.geometry()
        self.setGeometry(geo.x(), geo.y(), geo.width() - 1, geo.height() - 1)
        self.winId()
        wh = self.windowHandle()
        if wh is not None:
            wh.setScreen(screen)
            
        self._origin = QPointF(geo.x(), geo.y())
        self._center = QPointF(geo.width() / 2, geo.height() / 2)
        
        self._intro_timer.setSingleShot(False)
        self._intro_timer.start(300)
        
        self.setWindowOpacity(1.0)
        self.show()
        self.raise_()
        self.update()

    def _intro_tick(self) -> None:
        self._intro_stage += 1
        self.update()
        if self._intro_stage == len(self._intro_word) - 1:
            self._intro_timer.stop()
            self._intro_timer.setSingleShot(True)
            self._intro_timer.start(500)
        elif self._intro_stage >= len(self._intro_word):
            self.hide()
            self._intro_stage = -1

    def open(self, snapshot: dict) -> None:
        self._reset_state()
        self.media_status = snapshot.get("media")
        self._compute_geometry()
        cursor = snapshot.get("cursor")
        screen = self._screen_for_cursor(cursor)
        geo = screen.geometry()                 # global coordinates
        self.setGeometry(geo.x(), geo.y(), geo.width() - 1, geo.height() - 1)
        # Pin the window onto the cursor's screen so showFullScreen() does not
        # snap back to the primary monitor.
        self.winId()
        wh = self.windowHandle()
        if wh is not None:
            wh.setScreen(screen)
        if not cursor:
            cursor = (geo.x() + geo.width() // 2, geo.y() + geo.height() // 2)
        gx, gy = self._clamp_center(cursor, geo)
        # Everything internal is in screen-local coordinates.
        self._origin = QPointF(geo.x(), geo.y())
        self._center = QPointF(gx - geo.x(), gy - geo.y())
        self._pointer = QPointF(self._center)
        self._build_model(snapshot)
        self._icon_pixmap_cache = {}
        # With "Focus on Window-Switcher" off, do NOT pre-select the last window
        # or auto-open the windows category -- the cursor stays neutral (centre).
        # With it on, start the cursor ~15px ABOVE the centre, toward the
        # pre-selected window at the top.  The offset persists as the cursor
        # moves (applied in set_pointer), so it does not snap back to centre.
        self._init_active_category()
        if self.config.get("show_windows", True):
            self._preselect_window()
            self._cursor_oy = -10 * self.s
        self._pointer = QPointF(self._center.x(),
                                self._center.y() + self._cursor_oy)
        self._open_time = time.monotonic()
        self._path = [(self._open_time, self._pointer.x(), self._pointer.y())]
        if self.audio is not None:
            self.audio.set_overlay_active(True)
        # Pull a fresh tray read so the mail badge reflects new messages -- but
        # DEFERRED (singleShot) so its DBus round-trip never blocks the open path
        # (a synchronous call here wedged the grab and broke clicking).
        if getattr(self.tray, "refresh_now", None):
            QTimer.singleShot(0, self.tray.refresh_now)
        self.setWindowOpacity(0.0)
        self.show()
        self.raise_()
        self._check_start_mail_blink()
        # Hide the system cursor.  Because we grab the mouse via evdev the
        # compositor pointer never "enters" this surface, so a per-widget blank
        # cursor set before show can be ignored on Wayland -- re-assert it after
        # mapping AND push an application override cursor, which is honoured even
        # without an enter event.
        self.setCursor(Qt.CursorShape.BlankCursor)
        if not self._cursor_overridden:
            QGuiApplication.setOverrideCursor(Qt.CursorShape.BlankCursor)
            self._cursor_overridden = True
        # Force a synchronous repaint while the window is invisible
        self.repaint()
        # Make the window visible in the next tick once the first frame is fully drawn
        QTimer.singleShot(0, lambda: self.setWindowOpacity(0.99))
        if hasattr(self, "on_shown"):
            QTimer.singleShot(50, self.on_shown)
        self._schedule_next_idle_tick()

    def _clamp_center(self, cursor, screen) -> tuple:
        # Centre on the cursor; near edges shift inward so all four rings fit.
        margin = self.r4c + 30 * self.s
        x = max(screen.x() + margin,
                min(screen.x() + screen.width() - margin, cursor[0]))
        y = max(screen.y() + margin,
                min(screen.y() + screen.height() - margin, cursor[1]))
        # Drawn offset from the cursor -> lock window selection (the other two
        # sectors can then only be reached via the drawn hub).
        if abs(x - cursor[0]) > 20 or abs(y - cursor[1]) > 20:
            self._win_lock = True
        return x, y

    def _note_activated(self, win_id: str, rc: str = "") -> None:
        if not win_id:
            return
        self._mru = [win_id] + [x for x in self._mru if x != win_id]
        if rc:
            self._mru_rc = [rc] + [x for x in self._mru_rc if x != rc]
        self._last_activate_time = time.monotonic()

    def _init_active_category(self) -> None:
        wins = self.config.get("show_windows", True)
        valid = self.active_sectors

        if self.config.get("overlay_mode", "pie") == "switch":
            if wins and SEC_WINDOWS in valid:
                self.switch_mode_active_category = SEC_WINDOWS
            else:
                cur = getattr(self, "switch_mode_active_category", valid[0])
                if cur not in valid:
                    self.switch_mode_active_category = valid[0]
            self.open_sector = self.switch_mode_active_category
        else:
            self.open_sector = SEC_WINDOWS if (wins and SEC_WINDOWS in valid) else valid[0]
        self._layout_sectors()

    def _preselect_window(self) -> None:
        """Pre-select the previously active window so a press-and-release with no
        mouse movement toggles between the two most-recent windows.

        Uses the APP-GROUP MRU (_mru_rc) so that closing one window of a
        multi-window app doesn't lose the recency of the entire app or the
        app the user was switching with."""
        nodes = self.ring2.get(SEC_WINDOWS, [])
        groups = [n for n in nodes if n.kind == IT_WINDOW_GROUP and n.data]
        if not groups:
            return

        id_to_group: dict[str, Node] = {}
        rc_to_group: dict[str, Node] = {}
        for g in groups:
            for w in (g.data or []):
                id_to_group[w["id"]] = g
            if g.app_rc:
                rc_to_group[g.app_rc] = g

        cur = self._active_window_id
        cur_rc = id_to_group[cur].app_rc if cur and cur in id_to_group else ""

        # 1) Find the previous APP via _mru_rc (skipping the current app)
        prev_rc = next((rc for rc in self._mru_rc
                        if rc in rc_to_group and rc != cur_rc), None)
        if prev_rc is not None:
            anchor = rc_to_group[prev_rc]
            # Within that group, prefer a window that is in the window-level _mru;
            # otherwise just take the group's main (top-stacked) window.
            target = anchor
            for wid in self._mru:
                if wid in id_to_group and id_to_group[wid] is anchor:
                    if anchor.data and anchor.data[0]["id"] == wid:
                        target = anchor
                    else:
                        target = next((c for c in anchor.children if c.data == wid),
                                      anchor)
                    break
        else:
            # 2) Fallback: window-level _mru
            prev_id = next((x for x in self._mru[1:] if x in id_to_group), None)
            if prev_id is not None:
                anchor = id_to_group[prev_id]
                if anchor.data and anchor.data[0]["id"] == prev_id:
                    target = anchor
                else:
                    target = next((c for c in anchor.children if c.data == prev_id),
                                   anchor)
            else:
                # 3) Fallback: stacking-based previous window
                active_group = id_to_group.get(cur)
                others = [g for g in groups if g is not active_group]
                if others:
                    target = anchor = others[0]
                elif active_group is not None and active_group.children:
                    target = active_group.children[0]
                    anchor = active_group
                else:
                    target = anchor = groups[0]
        self.hover_node = target
        target.hover_t_target = 1.0
        target.hover_t = 1.0
        # Highlighting the main symbol immediately reveals all its windows.
        if anchor.children:
            self.open_group = anchor
            self._drill_armed = False
            self._layout_children(anchor)

    def refresh_model(self, snapshot: dict) -> None:
        """Rebuild ring2 from a fresher snapshot while the menu stays open,
        preserving the user's current sector and apps stage."""
        if getattr(self, "pending_close_wids", set()):
            snapshot["windows"] = [w for w in snapshot.get("windows", []) if w["id"] not in self.pending_close_wids]
        self.media_status = snapshot.get("media")
        keep_sector = self.open_sector
        keep_stage = self._apps_stage
        self.open_group = None
        self._ctrl_node = None
        self._icon_pixmap_cache = {}
        self._path_cache = {}
        self.ring2 = {SEC_WINDOWS: [], SEC_APPS: [], SEC_FILES: []}
        self._ring2_base = {SEC_WINDOWS: [], SEC_APPS: [], SEC_FILES: []}
        self._build_model(snapshot)
        self._apps_stage = keep_stage if keep_sector == SEC_APPS else 0
        self._rebuild_apps_stage()
        self.open_sector = keep_sector if keep_sector in self.active_sectors else -1
        self._layout_sectors()
        # If the user has not moved yet, keep the window pre-selection fresh and
        # snap the menu onto the up-to-date cursor (the first snapshot after an
        # idle period can be stale, which would otherwise draw the menu offset).
        if len(self._path) <= 1:
            self._recenter_on_cursor(snapshot.get("cursor"))
            if self.config.get("show_windows", True):
                self._preselect_window()
        self._check_start_mail_blink()
        self._update_hover()
        self.request_repaint()

    def _recenter_on_cursor(self, cursor) -> None:
        """Re-place the still-fresh menu on an up-to-date cursor position.  Only
        called before the user has started navigating, so it is safe to move the
        whole menu; it also re-evaluates ``_win_lock`` for the new position."""
        if not cursor:
            return
        screen = self.screen()
        if not screen:
            return
        geo = screen.geometry()
        # Only re-centre when the cursor is on the screen we already occupy.
        if not (geo.x() <= cursor[0] < geo.x() + geo.width()
                and geo.y() <= cursor[1] < geo.y() + geo.height()):
            return
        self._win_lock = False
        gx, gy = self._clamp_center(cursor, geo)   # may re-arm _win_lock
        new_center = QPointF(gx - geo.x(), gy - geo.y())
        if (abs(new_center.x() - self._center.x()) < 1.0
                and abs(new_center.y() - self._center.y()) < 1.0):
            return
        self._center = new_center
        self._pointer = QPointF(self._center.x(),
                                self._center.y() + self._cursor_oy)
        self._open_time = time.monotonic()
        self._path = [(self._open_time, self._pointer.x(), self._pointer.y())]

    def close_menu(self) -> None:
        if self.isVisible():
            self.hide()
        self._restore_cursor()
        self._tick_timer.stop()
        if self.audio is not None:
            self.audio.set_overlay_active(False)
        self.closed.emit()

    def _restore_cursor(self) -> None:
        if self._cursor_overridden:
            QGuiApplication.restoreOverrideCursor()
            self._cursor_overridden = False

    def hideEvent(self, event) -> None:  # noqa: N802
        # Safety net: never leave the app override (blank) cursor stuck, whatever
        # path hid the overlay.
        self._restore_cursor()
        super().hideEvent(event)

    # -- model build --------------------------------------------------------
    def _build_model(self, snapshot: dict) -> None:
        self._model_revision = getattr(self, "_model_revision", 0) + 1
        cfg = self.config
        windows = snapshot.get("windows", [])
        if cfg.get("show_windows", True):
            self._build_windows(windows)
        if cfg["show_apps"]:
            self._build_apps(windows)
        if cfg["show_recent_files"]:
            self._build_files()
        self.active_sectors = [sec for sec in (SEC_WINDOWS, SEC_APPS, SEC_FILES)
                               if self.ring2[sec]]
        if not self.active_sectors:
            self.active_sectors = [SEC_WINDOWS]
        self._layout_sectors()

    def switch_category(self, direction: int) -> None:
        """Cycle the active category in Switch Mode."""
        order = list(self.active_sectors)
        if not order:
            return
            
        try:
            idx = order.index(getattr(self, "switch_mode_active_category", SEC_WINDOWS))
        except ValueError:
            idx = 0
            
        if direction < 0:
            idx = (idx + 1) % len(order)
        elif direction > 0:
            idx = (idx - 1) % len(order)
            
        new_cat = order[idx]
        self.switch_mode_active_category = new_cat
        self._set_open_sector(new_cat)
        self._update_hover()
        self.update()

    def _build_windows(self, windows: list) -> None:
        cfg = self.config
        ignore = {"karyon", "python3", "plasmashell", "org.kde.plasmashell", "xembedsniproxy"}
        nodes: list[Node] = []
        # group windows by desktop_file or resourceClass; order by stacking (most-recently-used
        # first), since workspace.windowList() is NOT in MRU order.
        groups_dict: dict[str, list] = {}
        for w in windows:
            rc = w["rc"]
            df = w.get("desktop_file", "")
            if rc in ignore or not rc:
                continue
            if w["active"]:
                self._active_window_id = w["id"]
            
            # Fix KWin bug: apps launched from file managers/terminals sometimes inherit
            # their desktop file via startup_id, causing them to be grouped together.
            if df:
                df_name = df.replace(".desktop", "").lower()
                rc_low = rc.lower()
                if df_name in ("org.kde.dolphin", "dolphin", "org.kde.konsole", "konsole"):
                    if df_name.split(".")[-1] not in rc_low:
                        df = ""
            
            group_key = df.replace(".desktop", "") if df else rc
            groups_dict.setdefault(group_key, []).append(w)
        for k in groups_dict:
            groups_dict[k].sort(key=lambda w: w.get("stack", -1), reverse=True)
        order = sorted(groups_dict, key=lambda k: groups_dict[k][0].get("stack", -1),
                       reverse=True)
        groups: list[Node] = []
        for k in order:
            wins = groups_dict[k]
            primary_rc = wins[0]["rc"]
            app = self.app_index.match_window(primary_rc, wins[0].get("desktop_file", ""),
                                              pid=int(wins[0].get("pid", 0) or 0))
            if app is not None:
                self.app_index.note_seen(app.app_id)
                label = app.name
                icon = _icon_for(app.icon)
            else:
                # App without a .desktop (e.g. AppImage): pseudo entry from /proc.
                label, icon = self._pseudo_from_proc(primary_rc, wins)
                icon = icon or _icon_for(primary_rc)
            # Every group has a RED close bar on its main symbol (closes the
            # active/main window); multi-window groups additionally drill to the
            # others on hover and close ALL via the count badge.
            app_name = label
            node = Node(kind=IT_WINDOW_GROUP,
                        label=self._window_label(app_name,
                                                 wins[0].get("caption", "")),
                        icon=icon, data=wins, closable=True,
                        sector=SEC_WINDOWS)
            # Candidate app ids for matching LauncherEntry progress signals.
            node.progress_keys = [wins[0].get("desktop_file", ""),
                                  (app.app_id if app is not None else ""), primary_rc]
            node.app_pid = int(wins[0].get("pid", 0) or 0)
            node.app_rc = primary_rc
            if len(wins) > 1:
                node.sublabel = f"{len(wins)} windows"
                node.count = len(wins)        # count badge on the group symbol
                # other windows (besides most-recent active) as children: same
                # app icon, no text; the caption is only used for the title pill.
                for w in wins[1:]:
                    node.children.append(Node(
                        kind=IT_WINDOW,
                        label=self._window_label(app_name, w.get("caption", "")),
                        icon=icon, data=w["id"], closable=True))
            groups.append(node)

        # Sort window groups by APP recency (not individual window recency).
        # When any window of an app is activated, the entire app group is
        # considered "most recently used".
        if groups:
            id_to_group: dict[str, Node] = {}
            rc_to_group: dict[str, Node] = {}
            for g in groups:
                for w in (g.data or []):
                    id_to_group[w["id"]] = g
                if g.app_rc:
                    rc_to_group[g.app_rc] = g

            cur = self._active_window_id
            settled = (time.monotonic() - self._last_activate_time) > 2.0
            if cur and cur in id_to_group:
                cur_rc = id_to_group[cur].app_rc or ""
                if not self._mru or (settled and self._mru[0] != cur):
                    self._mru = [cur] + [x for x in self._mru if x != cur]
                if cur_rc and (not self._mru_rc or (settled and self._mru_rc[0] != cur_rc)):
                    self._mru_rc = [cur_rc] + [x for x in self._mru_rc if x != cur_rc]
            # prune dead windows / app classes from MRU lists
            self._mru = [x for x in self._mru if x in id_to_group]
            live_rcs = set(rc_to_group)
            self._mru_rc = [x for x in self._mru_rc if x in live_rcs]

            # Rank function: uses app-group MRU (resourceClass level)
            def grank(g):
                rc = getattr(g, 'app_rc', '') or ''
                if rc in self._mru_rc:
                    return self._mru_rc.index(rc)
                return len(self._mru_rc) + 1

            # Determine current and previous APP GROUP from the rc-level MRU:
            cur_rc = self._mru_rc[0] if self._mru_rc else None
            prev_rc = self._mru_rc[1] if len(self._mru_rc) > 1 else None

            cur_group = rc_to_group.get(cur_rc) if cur_rc else None
            prev_group = rc_to_group.get(prev_rc) if prev_rc else None

            # Build "middle" pair: [prev, cur] = [..., prev, cur, ...]
            middle = []
            if cur_group is not None:
                middle.append(cur_group)
            if prev_group is not None and prev_group is not cur_group:
                middle.insert(0, prev_group)  # prev to the left of cur

            # Sort remaining by app-group MRU rank (most recent first)
            center_set = set(id(g) for g in middle)
            others = sorted(
                (g for g in groups if id(g) not in center_set),
                key=grank,
            )

            # Split: right gets first half (more recent), left gets second half (older)
            # Result: [...older_left | prev | cur | newer_right...]
            half = (len(others) + 1) // 2  # favour right with extra item if odd
            right = others[:half]           # most recent continue right of center
            left = others[half:]            # older continue left (reversed so oldest is leftmost)
            arranged = left[::-1] + middle + right
        else:
            arranged = []

        # Mark open window groups as pinned if their app is pinned
        open_pinned_app_ids = set()
        for g in arranged:
            g_rc = getattr(g, "app_rc", "")
            if g_rc:
                app = self.app_index.match_window(g_rc)
                app_id = app.app_id if app else f"pseudo:{g_rc.lower()}"
                if self.pins.is_app_pinned(app_id):
                    g.pinned = True
                    open_pinned_app_ids.add(app_id)

        # Build placeholders for closed pinned apps
        closed_pinned_nodes = []
        for app_id in self.pins.pinned_apps:
            if app_id in open_pinned_app_ids:
                continue
            app = self.app_index.apps.get(app_id) or self.app_index._pseudo.get(app_id)
            if not app:
                app = self.app_index._find_app(app_id)
            if app:
                placeholder = Node(kind=IT_WINDOW_GROUP, label=app.name,
                                   icon=_icon_for(app.icon),
                                   pinned=True, closable=False, data=None,
                                   sector=SEC_WINDOWS)
                placeholder.app_rc = app_id
                closed_pinned_nodes.append(placeholder)

        nodes.extend(closed_pinned_nodes)
        if cfg["show_desktop"]:
            nodes.append(Node(kind=IT_SHOW_DESKTOP, label="Show Desktop",
                               passive=True, glyph="show_desktop",
                               sector=SEC_WINDOWS))
        nodes.extend(arranged)
        # Mail segment: a dedicated ring-2 segment at the very END of the
        # windows row, shown only when a tray app signals a new message.  It
        # carries the first attention item's data so releasing on it activates
        # that app.  Pinned to ring 2 (never wraps to ring 3) by the layout.
        if self.config.get("mail_notification", True):
            att = self._tray_attention()
        else:
            att = []
        if att:
            mail_node = Node(kind=IT_MAIL, label="Mail",
                             passive=True, glyph="mail",
                             data=att[0].data, closable=True)
            mail_node.tray_item = att[0]
            nodes.append(mail_node)
        self.ring2[SEC_WINDOWS] = nodes
        self._ring2_base[SEC_WINDOWS] = list(nodes)
        if att:
            self._check_start_mail_blink()

    # Untitled / unsaved-document markers across common apps and locales.
    _UNSAVED_MARKERS = ("unsaved", "untitled", "no name", "new document",
                        "ungespeichert", "nicht gespeichert", "neues dokument",
                        "neues textdokument", "unbenannt", "namenlos")

    @staticmethod
    def _clean_caption(cap: str) -> str:
        if not cap:
            return ""
        # Try to recover double-encoded UTF-8 (common in GTK window titles under KWin/XWayland)
        try:
            cap = cap.encode('latin-1').decode('utf-8')
        except (UnicodeEncodeError, UnicodeDecodeError):
            pass

        # Drop Unicode control/format chars (directional marks etc.) that some
        # apps put in their title and that render as stray boxes/symbols.
        # Also drop:
        #   Co = Private Use Area (Nerd Fonts icons, …)
        #   Cs = Surrogate halves (should not appear in Python str, but guard anyway)
        #   Cn = Unassigned codepoints (render as boxes / replacement chars)
        import re
        import unicodedata
        cleaned = []
        for ch in (cap or ""):
            cat = unicodedata.category(ch)
            if cat in ("Cc", "Cf", "Co", "Cs", "Cn") or ch == "\ufffd":
                continue
            if cat == "Zs" and ch != " ":
                # Normalize non-standard spaces to standard space
                cleaned.append(" ")
            else:
                cleaned.append(ch)
        res = "".join(cleaned)
        return re.sub(r"\s+", " ", res).strip()


    def _normalize_loc(self, loc: str) -> str:
        loc = loc.strip()
        bare = loc.lstrip("*").strip()
        low = bare.lower()
        if any(m in low for m in self._UNSAVED_MARKERS):
            return "Nicht gespeichert"
        if not any(ch.isalnum() for ch in bare):   # nothing meaningful left
            return ""
        return bare

    def _window_label(self, fallback_app: str, caption: str) -> str:
        """'<file/location> - <App>' from a window caption, like the level-2
        window symbols: strips the trailing ' — App' suffix, normalises unsaved
        documents to 'Nicht gespeichert', caps the location at 30 chars, and uses
        a short hyphen (never the long em/en dash)."""
        import re
        cap = self._clean_caption(caption)
        if not cap:
            return fallback_app
        app = fallback_app
        parts = re.split(r"\s+[—–\-]\s+", cap)   # em / en / hyphen
        if len(parts) >= 2 and self._looks_like_app_suffix(parts[-1], fallback_app):
            loc = " - ".join(p.strip() for p in parts[:-1])
        else:
            loc = "" if cap.strip().lower() == fallback_app.strip().lower() else cap
        app = (app[:1].upper() + app[1:]) if app else fallback_app
        loc = self._normalize_loc(loc)
        if not loc:
            return app
        if len(loc) > 30:
            loc = loc[:27].rstrip() + "..."
        return f"{loc} - {app}"

    @staticmethod
    def _looks_like_app_suffix(suffix: str, app: str) -> bool:
        suffix = (suffix or "").strip().lower()
        app = (app or "").strip().lower()
        return bool(suffix and app and (suffix in app or app in suffix))

    def _pseudo_from_proc(self, rc: str, wins: list):
        """Derive a name/icon for a window whose app has no .desktop, and record
        it as a pseudo recent so it can resurface in the Apps sector."""
        import os
        label = wins[0].get("caption") or rc
        icon = QIcon()
        pid = 0
        for w in wins:
            if w.get("pid"):
                pid = int(w["pid"])
                break
        exe = ""
        if pid:
            try:
                exe = os.readlink(f"/proc/{pid}/exe")
            except Exception:  # noqa: BLE001
                exe = ""
        name = rc.capitalize() if rc else (os.path.basename(exe) if exe else label)
        
        # Determine fallback icon name
        icon_name = rc.lower() if rc else (os.path.basename(exe).lower() if exe else "")
        if "soffice" in icon_name or "libreoffice" in icon_name:
            cap = (wins[0].get("caption") or "").lower()
            if "writer" in cap or "document" in cap or "dokument" in cap or "unbenannt" in cap or "untitled" in cap:
                icon_name = "libreoffice-writer"
                # Check for calc/impress keywords inside the untitled string
                if "tabelle" in cap or "calc" in cap or "spreadsheet" in cap:
                    icon_name = "libreoffice-calc"
                elif "präsentation" in cap or "presentation" in cap or "impress" in cap:
                    icon_name = "libreoffice-impress"
                elif "zeichnung" in cap or "drawing" in cap or "draw" in cap:
                    icon_name = "libreoffice-draw"
                elif "formel" in cap or "math" in cap:
                    icon_name = "libreoffice-math"
            elif "calc" in cap or "spreadsheet" in cap or "tabelle" in cap:
                icon_name = "libreoffice-calc"
            elif "impress" in cap or "presentation" in cap or "präsentation" in cap:
                icon_name = "libreoffice-impress"
            elif "draw" in cap or "zeichnung" in cap:
                icon_name = "libreoffice-draw"
            elif "math" in cap or "formel" in cap:
                icon_name = "libreoffice-math"
            else:
                icon_name = "libreoffice"

        if icon_name:
            ic = _icon_for(icon_name)
            if not ic.isNull():
                icon = ic

        if exe:
            app_id = f"pseudo:{rc or os.path.basename(exe)}"
            try:
                self.app_index.add_pseudo(app_id, name, exe, "")
                self.app_index.note_seen(app_id)
            except Exception:  # noqa: BLE001
                pass
        return name, icon

    def _build_tray_node(self) -> Node:
        node = Node(kind=IT_TRAY_MENU, label="Tray", passive=True,
                    glyph="tray")
        for item in self.tray.enabled_tray_items():
            child = Node(kind=IT_TRAY, label=item.label, data=item.data,
                         glyph=item.glyph or "", icon=item.qicon,
                         control=item.control_key)
            child.icon_name = item.icon_name
            child.controls = item.controls
            node.children.append(child)
        for item in self.tray.sni_items:
            # Skip the launcher's own SNI item -- it gets an explicit entry below.
            norm = "".join(ch for ch in (item.label or "").lower() if ch.isalnum())
            if "dumblauncher" in norm:
                continue
            ic = None
            if item.icon_name:
                ic = _icon_for(item.icon_name)
            if not ic or ic.isNull():
                ic = item.qicon
            child = Node(kind=IT_TRAY, label=item.label, data=item.data,
                         icon=ic)
            child.icon_name = item.icon_name
            child.icon_sig = item.icon_sig
            # The app's context menu -> drill into the symbol to fan it out.
            child.menu = item.menu or []
            child.menu_bus = item.menu_bus
            child.menu_path = item.menu_path
            node.children.append(child)
        # The launcher's own icon (always in the system tray) -> opens Settings.
        import os
        svg = os.path.join(os.path.dirname(__file__), "karyon.svg")
        dl = Node(kind=IT_TRAY, label="Karyon", data="__dl_settings__",
                  icon=QIcon(svg) if os.path.exists(svg) else QIcon())
        dl.icon_name = ""
        dl.local_menu = [
            ("Settings", ("dl_settings",)),
            ("Set up input access...", ("dl_setup_input",)),
            ("Quit", ("dl_quit",)),
        ]
        node.children.append(dl)
        # Volume always sits dead-centre of the tray fan (on the hub line).
        vol = next((c for c in node.children if c.glyph == "volume"), None)
        if vol is not None:
            node.children.remove(vol)
            node.children.insert(len(node.children) // 2, vol)
        # "..." popup drawer (FIRST, ALWAYS present): the remaining system-tray
        # items, fanned one ring further (ring 4) when its drill bar is touched.
        drawer = Node(kind=IT_TRAY, label="Tray Popup", glyph="dots",
                      data=("noop", None), control="drawer")
        drawer.drawer_children = []
        for it in self.tray.hidden_tray_items():
            ic = None
            if it.icon_name:
                ic = _icon_for(it.icon_name)
            if not ic or ic.isNull():
                ic = it.qicon
            k = Node(kind=IT_TRAY, label=it.label, data=it.data,
                     glyph=it.glyph or "", icon=ic)
            k.icon_name = it.icon_name
            k.icon_sig = it.icon_sig
            drawer.drawer_children.append(k)
        node.children.insert(0, drawer)
        return node

    def _tray_attention(self) -> list:
        """Tray apps currently flagging a new message -- read live so the mail
        badge appears/clears in (near) real time while the overlay is open."""
        try:
            return self.tray.attention_items()
        except Exception:  # noqa: BLE001
            return []

    def _layout_drawer_children(self, node: Node) -> None:
        kids = getattr(node, "drawer_children", []) or []
        r_out = self.r4c + self.seg3_depth / 2
        slot = math.degrees(self._seg_phys_w / r_out)
        gap = math.degrees(self._gap_phys / r_out)
        start = node.angle - (len(kids) - 1) * slot / 2
        for i, kid in enumerate(kids):
            kid.angle = start + i * slot
            kid.radius = self.r4c
            kid.half_deg = (slot - gap) / 2
            kid.hover_t = 0.0
        node.children_ctrl = list(kids)

    def _menu_nodes(self, node: Node) -> list:
        """Build ring-4 nodes from an SNI app's DBus context menu (separators
        dropped, a checkmark prefixed for active toggles).  No 'Öffnen' entry --
        releasing on the app symbol itself already activates it (left-click)."""
        local = getattr(node, "local_menu", None)
        if local:
            return [Node(kind=IT_TRAY_MENUITEM, label=label, data=data)
                    for label, data in local]
        kids = []
        for e in getattr(node, "menu", []) or []:
            if e.separator or not e.visible or not (e.label or "").strip():
                continue
            label = e.label
            if e.toggle_type and e.toggle_state == 1:
                label = "✓ " + label
            m = Node(kind=IT_TRAY_MENUITEM, label=label,
                     data=("menu_click", node.menu_bus, node.menu_path, e.id))
            m.passive = not e.enabled
            kids.append(m)
        return kids

    def _layout_menu_children(self, node: Node) -> None:
        kids = self._menu_nodes(node)
        r_out = self.r4c + self.seg3_depth / 2
        slot = math.degrees(self._seg_phys_w / r_out)
        gap = math.degrees(self._gap_phys / r_out)
        start = node.angle - (len(kids) - 1) * slot / 2
        for i, kid in enumerate(kids):
            kid.angle = start + i * slot
            kid.radius = self.r4c
            kid.half_deg = (slot - gap) / 2
            kid.hover_t = 0.0
        node.children_ctrl = list(kids)

    def _build_apps(self, windows: list) -> None:
        cfg = self.config
        nodes: list[Node] = []
        # The centred pivot of the row: "All Applications" -- or, when that is
        # disabled, "Favorites" promotes into exactly its place.
        pivot = None
        if cfg["show_all_apps"]:
            # Menu button: drilling it morphs the row into the category list.
            pivot = Node(kind=IT_MENU, label="All Applications",
                         passive=True, icon_scale=0.8,
                         glyph="hamburger")
        elif cfg["show_favorites"]:
            pivot = Node(kind=IT_FAV_MENU, label="Favorites",
                         glyph="star", children=self._fav_nodes())
        running = set()
        for w in windows:
            app = self.app_index.match_window(w["rc"], w.get("desktop_file", ""),
                                              pid=int(w.get("pid", 0) or 0))
            if app:
                running.add(app.app_id)
        for app in self.app_index.frequent(cfg["max_recent_apps"], exclude=running):
            nodes.append(Node(kind=IT_APP, label=app.name,
                              icon=_icon_for(app.icon),
                              data=app,
                              pinned=self.pins.is_app_pinned(app.app_id),
                              sector=SEC_APPS))
        if cfg["show_session"]:
            nodes.append(self._session_node())
        if cfg["show_tray"]:
            nodes.append(self._build_tray_node())
        # The pivot is centred: most recently used apps go right, older apps fill left.
        # Result: [...older_left | pivot | recent_right...]
        if pivot is not None:
            half = (len(nodes) + 1) // 2   # favour right with extra if odd
            right = nodes[:half]            # most recent on the right of pivot
            left = nodes[half:]             # older on the left (reversed so oldest is leftmost)
            nodes = left[::-1] + [pivot] + right
        self._apps_recent_nodes = nodes
        self._apps_cat_nodes = self._cat_nodes() if cfg["show_all_apps"] else []
        self.ring2[SEC_APPS] = nodes
        self._ring2_base[SEC_APPS] = list(nodes)

    def _rebuild_apps_stage(self) -> None:
        if SEC_APPS not in self.active_sectors:
            return
        if self._apps_stage == 1 and self._apps_cat_nodes:
            self._ring2_base[SEC_APPS] = list(self._apps_cat_nodes)
            self.ring2[SEC_APPS] = self._apps_cat_nodes
        else:
            self._ring2_base[SEC_APPS] = list(self._apps_recent_nodes)
            self.ring2[SEC_APPS] = self._apps_recent_nodes

    def _session_node(self) -> Node:
        from .session import SESSION_ACTIONS
        node = Node(kind=IT_SESSION, label="Session", glyph="session")
        for key, label, _destr in SESSION_ACTIONS:
            node.children.append(Node(kind=IT_SESSION, label=label, data=key,
                                      glyph=f"session_{key}"))
        return node

    def _cat_nodes(self) -> list[Node]:
        nodes = []
        cats = self.app_index.categories()
        fav_index = len(cats) // 2
        items = list(cats.items())
        for i, (name, apps) in enumerate(items):
            if self.config["show_favorites"] and i == fav_index:
                # In the category list (after All Applications) Favorites opens on
                # aim with NO drill bar, like the other categories.  (As the apps
                # pivot -- show_all_apps off -- it is IT_FAV_MENU with a bar.)
                nodes.append(Node(kind=IT_CATEGORY, label="Favorites",
                                  render_icon=True, glyph="star",
                                  children=self._fav_nodes()))
            node = Node(kind=IT_CATEGORY, label=name, sublabel=f"{len(apps)} apps",
                        children=[Node(kind=IT_APP, label=a.name, data=a,
                                       icon=_icon_for(a.icon),
                                       pinned=self.pins.is_app_pinned(a.app_id))
                                  for a in apps])
            nodes.append(node)
        return nodes

    def _fav_nodes(self) -> list[Node]:
        return [Node(kind=IT_APP, label=a.name, data=a,
                     icon=_icon_for(a.icon),
                     pinned=self.pins.is_app_pinned(a.app_id))
                for a in self.app_index.favorites()]

    def _file_icon(self, path: str, icon_name: str = "") -> QIcon:
        if icon_name:
            ic = QIcon.fromTheme(icon_name)
            if not ic.isNull():
                return ic
        mt = self._mimedb.mimeTypeForFile(path)
        for name in (mt.iconName(), mt.genericIconName(), "text-x-generic"):
            if not name:
                continue
            ic = QIcon.fromTheme(name)
            if not ic.isNull():
                return ic
        return QIcon()

    def _build_files(self) -> None:
        recent_items = self.recent_files.items(self.config["max_recent_files"])
        
        pinned_nodes = []
        for path in self.pins.pinned_files:
            rf = next((x for x in recent_items if x.path == path), None)
            if rf:
                name = rf.name
                icon_name = rf.icon_name
            else:
                name = os.path.basename(path)
                icon_name = None
            pinned_nodes.append(Node(kind=IT_FILE, label=name, data=path,
                                     icon=self._file_icon(path, icon_name),
                                     pinned=True, sector=SEC_FILES))
            
        unpinned_nodes = []
        for rf in recent_items:
            if self.pins.is_file_pinned(rf.path):
                continue
            unpinned_nodes.append(Node(kind=IT_FILE, label=rf.name, data=rf.path,
                                       icon=self._file_icon(rf.path, rf.icon_name),
                                       pinned=False, sector=SEC_FILES))
            
        nodes = pinned_nodes + unpinned_nodes[:max(0, self.config["max_recent_files"] - len(pinned_nodes))]
        
        if not self.config["show_apps"] and self.config["show_session"]:
            nodes.append(self._session_node())
        if not self.config["show_apps"] and self.config.get("show_tray", True):
            tray_node = self._build_tray_node()
            tray_node.sector = SEC_FILES
            nodes.append(tray_node)
        self.ring2[SEC_FILES] = nodes
        self._ring2_base[SEC_FILES] = list(nodes)

    # -- sector layout ------------------------------------------------------
    def _nominal_for(self, secs: list) -> dict:
        """Centre angle of each active sector (0=right, 90=down)."""
        n = len(secs)
        if n == 3:
            # Straight orientation: Windows up, Apps right, Files left.
            # Opened state spans will be centered around these nominals.
            return {SEC_WINDOWS: -90.0, SEC_APPS: 0.0, SEC_FILES: 180.0}
        if n == 2:
            # If Windows is missing, Apps moves to the top (-90).
            # Otherwise Windows is top, and the other is bottom (90).
            out = {}
            if SEC_WINDOWS in secs:
                for s in secs:
                    out[s] = -90.0 if s == SEC_WINDOWS else 90.0
            else:
                for s in secs:
                    out[s] = -90.0 if s == SEC_APPS else 90.0
            return out
        return {secs[0]: -90.0}

    def _layout_sectors(self) -> None:
        """Assign each active sector an arc, then lay out the ring2 nodes of the
        currently selected sector (or all, when none is selected yet)."""
        self._sector_arc: dict[int, tuple] = {}
        secs = self.active_sectors
        n = len(secs)
        nominal = self._nominal_for(secs)
        
        if self.config.get("overlay_mode", "pie") == "switch":
            active = getattr(self, "switch_mode_active_category", SEC_WINDOWS)
            # Center it at 270 degrees (which is -90 in qt)
            self._nominal = {s: -90.0 for s in (SEC_WINDOWS, SEC_APPS, SEC_FILES)}
            self._sector_arc[active] = (-270.0, 90.0) # Full 360 degrees
            # Hide the others
            for s in secs:
                if s != active:
                    self._sector_arc[s] = (0.0, 0.0)
            self._layout_ring2(active)
            self._update_sector_vis()
            return
            
        self._nominal = nominal

        if n == 1:
            s = secs[0]
            self._sector_arc[s] = (nominal[s] - 180.0, nominal[s] + 180.0)
            self._layout_ring2(s)
            self._update_sector_vis()
            return

        if self.open_sector == -1:
            if n == 3:
                # User specifically requested perfect thirds (120/120/120) when unselected.
                self._sector_arc[SEC_WINDOWS] = (210.0, 330.0)
                self._sector_arc[SEC_APPS] = (330.0, 450.0) # 330 to 90
                self._sector_arc[SEC_FILES] = (90.0, 210.0)
                for s in secs:
                    self._layout_ring2(s)
                self._update_sector_vis()
                return
            
            # Boundaries at the midpoints between consecutive sector centres
            # (tiles the circle with no gap, even for uneven spacing).
            order = sorted(secs, key=lambda s: nominal[s] % 360.0)
            ns = [nominal[s] % 360.0 for s in order]
            m = len(order)
            bounds = []
            for i in range(m):
                a = ns[i]
                b = ns[(i + 1) % m] + (360.0 if i + 1 == m else 0.0)
                bounds.append((a + b) / 2)
            for i, s in enumerate(order):
                a0 = bounds[(i - 1) % m]
                a1 = bounds[i]
                if a1 < a0:
                    a1 += 360.0
                self._sector_arc[s] = (a0, a1)
                self._layout_ring2(s)
            self._update_sector_vis()
            return

        # A sector is selected: it expands to a half-circle, the others tile the
        # rest contiguously as quarters (n=3) / the remaining half (n=2).
        sel = self.open_sector
        big = 180.0
        rest = (360.0 - big) / (n - 1)
        order = sorted(secs, key=lambda s: nominal[s] % 360.0)
        self._sector_arc[sel] = (nominal[sel] - big / 2, nominal[sel] + big / 2)
        idx = order.index(sel)
        cur = nominal[sel] + big / 2
        for k in range(1, n):
            s = order[(idx + k) % n]
            self._sector_arc[s] = (cur, cur + rest)
            cur += rest
        self._layout_ring2(sel)
        self._update_sector_vis()

    def _update_sector_vis(self) -> None:
        """Visual ring-1 arcs as (centre, half-width): selected stays wide,
        collapsed sectors shrink to a thin line.  Animated (centre + half) so the
        transformation is smooth and never inverts at angle wrap-around."""
        # Rings snap straight to their settled arcs (no build-up animation).
        anim = False
        self._sector_vis = {}
        for sec in self.active_sectors:
            a0, a1 = self._sector_arc.get(sec, (0.0, 0.0))
            center = (a0 + a1) / 2
            half = (a1 - a0) / 2
            if self.open_sector == -1 or sec == self.open_sector:
                self._sector_vis[sec] = (center, half)
            else:
                self._sector_vis[sec] = (center, 0.0)   # shrink away completely
        for sec in list(self._sector_draw):
            if sec not in self.active_sectors:
                del self._sector_draw[sec]
        for sec in self.active_sectors:
            if sec not in self._sector_draw or not anim:
                self._sector_draw[sec] = list(self._sector_vis[sec])

    def _center_on(self, sec: int, anchor) -> None:
        """Rotate a sector's ring-2 row so ``anchor`` sits at the sector nominal
        (Windows -> straight up, Apps -> straight right)."""
        nodes = self.ring2.get(sec, [])
        if anchor is None or anchor not in nodes:
            return
        nominal = self._nominal.get(sec)
        if nominal is None:
            return
        delta = nominal - anchor.angle
        for n in nodes:
            n.angle += delta

    def _sector_containing(self, a: float) -> int:
        """Which active sector's arc currently contains angle a."""
        for sec, (a0, a1) in self._sector_arc.items():
            lo = _norm(a0)
            span = a1 - a0
            d = _norm(a - a0)
            if d < 0:
                d += 360.0
            if 0 <= d <= span:
                return sec
        # fallback: nearest nominal
        best, bestd = self.active_sectors[0], 999.0
        for sec in self.active_sectors:
            d = _ang_dist(a, self._nominal.get(sec, 0.0))
            if d < bestd:
                best, bestd = sec, d
        return best
        
    def _has_progress(self, node) -> bool:
        if self.progress is None or getattr(node, "kind", None) != IT_WINDOW_GROUP:
            return False
        keys = getattr(node, "progress_keys", ())
        if not keys:
            return False
        frac = self.progress.get(*keys)
        return frac is not None and frac > 0.001

    def _ring_gap(self, ring: int) -> int:
        """Empty segments left before a row wraps outward, per absolute ring:
        ring 2 -> 4, ring 3 -> 7, ring 4 and beyond -> 10."""
        if ring <= 2:
            return max(3, self.config.get("gap_ring1", 4))
        if ring == 3:
            return 7
        return 10

    def _layout_radial(self, nodes, center, base_r, depth, base_span,
                       start_ring=2, single_row=False, gap_override=None) -> None:
        """Lay nodes on arcs of identical physical-width segments.  Each row
        leaves a per-ring number of empty segments before wrapping outward
        (see :meth:`_ring_gap`); ``gap_override`` forces a fixed count.  With
        ``single_row`` the overflow is dropped instead of wrapped."""
        count = len(nodes)
        # Every segment has the SAME physical width (self._seg_phys_w); the
        # angular slot is width/radius, so segments are identical across rings.
        i = 0
        row = 0
        while i < count:
            ring = start_ring + row
            gap_segs = gap_override if gap_override is not None else self._ring_gap(ring)
            row_radius = base_r + row * (depth + 2 * self.s)   # uniform 2s gap
            r_out = row_radius + depth / 2
            slot = math.degrees(self._seg_phys_w / r_out)
            gap = math.degrees(self._gap_phys / r_out)
            cap = max(1, int(360.0 / slot) - gap_segs)
            take = min(cap, count - i)
            total = slot * take
            for col in range(take):
                node = nodes[i + col]
                node.angle = center - total / 2 + slot / 2 + col * slot
                node.radius = row_radius
                node.half_deg = (slot - gap) / 2
                node.row = row
            i += take
            row += 1
            if single_row:
                break
        for node in nodes[i:]:      # dropped overflow (single_row only)
            node.half_deg = 0.0
            node.row = -1

    def _per_row(self, base_r: float, depth: float, gap_segs: int) -> int:
        """Segments that fit on one row before ``gap_segs`` are left free."""
        slot_base = math.degrees(self._seg_phys_w / (base_r + depth / 2))
        return max(1, int(360.0 / slot_base) - gap_segs)

    def _layout_pinned_row(self, nodes: list, center: float, sec: int = -1) -> None:
        """Ring-2 row with pinned symbols: the apps pivot ('All Applications' or
        'Favorites') to the EXACT centre, and 'Session' to the very end of
        ring 2 -- Session never wraps onto ring 3.  Surplus items overflow to
        ring 3+ with the usual per-ring gaps.  Also used for the Recent Files
        row when it carries the relocated Session symbol."""
        pivot = next((n for n in nodes if n.kind in (IT_MENU, IT_FAV_MENU)), None)
        session = next((n for n in nodes if n.kind == IT_SESSION), None)
        tray = next((n for n in nodes if n.kind == IT_TRAY_MENU), None)
        recents = [n for n in nodes if n is not pivot and n is not session and n is not tray]

        r_out = self.r2c + self.seg_depth / 2
        slot = math.degrees(self._seg_phys_w / r_out)
        gap = math.degrees(self._gap_phys / r_out)
        cap = max(1, int(360.0 / slot) - self._ring_gap(2))

        reserved = (1 if pivot else 0) + (1 if session else 0) + (1 if tray else 0)
        target = len(recents) + reserved
        L = min(cap, target)
        # Force an ODD row so a true centre slot exists for the pivot symbol.
        if pivot is not None and L % 2 == 0:
            L -= 1
        L = max(L, reserved)

        recents_cap = L - reserved
        on_ring2 = recents[:recents_cap]
        overflow = recents[recents_cap:]

        # Apply ping-pong sorting to the items on ring 2
        left, right = [], []
        for i, r in enumerate(on_ring2):
            (left if i % 2 == 0 else right).append(r)
        arranged_on_ring2 = left[::-1] + right

        # Compose row 0: pivot at the exact centre column, session at the last.
        row0 = [None] * L
        if pivot is not None:
            mid = (L - 1) // 2
            row0[mid] = pivot
            if tray is not None:
                if mid - 1 >= 0 and row0[mid - 1] is None:
                    row0[mid - 1] = tray
                elif mid + 1 < L and row0[mid + 1] is None:
                    row0[mid + 1] = tray
        else:
            if tray is not None:
                row0[0] = tray
        if session is not None:
            row0[L - 1] = session

        ri = 0
        for col in range(L):
            if row0[col] is None and ri < len(arranged_on_ring2):
                row0[col] = arranged_on_ring2[ri]
                ri += 1

        total = slot * L
        max_drilldown_col = -1
        for col, node in enumerate(row0):
            if node is None:
                continue
            node.angle = center - total / 2 + slot / 2 + col * slot
            node.radius = self.r2c
            node.half_deg = (slot - gap) / 2
            node.row = 0
            if node.kind in (IT_MENU, IT_FAV_MENU, IT_TRAY_MENU):
                max_drilldown_col = max(max_drilldown_col, col)
        # Mutate the input nodes list in-place so that the visual/spatial order is correct
        nodes[:] = [n for n in row0 if n is not None] + overflow
        # Remaining recents start on ring 3 (gap 7), then ring 4 (gap 10)...
        if overflow:
            base = self.r2c + (self.seg_depth + 2 * self.s)
            if sec == SEC_APPS and max_drilldown_col >= 0:
                # Align overflow to the right of the last drilldown segment (Tray / All Apps)
                # to prevent crossing the middle/right drilldown buttons when navigating to Ring 3+.
                last_drilldown_angle = center - total / 2 + slot / 2 + max_drilldown_col * slot
                start_angle = last_drilldown_angle + slot / 2
                
                i = 0
                row = 0
                count = len(overflow)
                while i < count:
                    ring = 3 + row
                    gap_segs = self._ring_gap(ring)
                    row_radius = base + row * (self.seg_depth + 2 * self.s)
                    r_out = row_radius + self.seg_depth / 2
                    slot_r = math.degrees(self._seg_phys_w / r_out)
                    gap_r = math.degrees(self._gap_phys / r_out)
                    cap = max(1, int(360.0 / slot_r) - gap_segs)
                    take = min(cap, count - i)
                    for col in range(take):
                        node = overflow[i + col]
                        node.angle = start_angle + slot_r / 2 + col * slot_r
                        node.radius = row_radius
                        node.half_deg = (slot_r - gap_r) / 2
                        node.row = row
                    i += take
                    row += 1
            else:
                self._layout_radial(overflow, center, base, self.seg_depth,
                                    RING3_SPAN, start_ring=3)

    def _layout_windows_row(self, nodes: list, center: float) -> None:
        """Windows sector row: Show Desktop leads and the Tray symbol closes
        ring 2 -- both are PINNED to ring 2 and never wrap onto ring 3, no
        matter how many window groups overflow.  The window groups fill the
        space between them on ring 2; surplus groups fan out to ring 3+ with
        the usual per-ring gaps."""
        sd = next((n for n in nodes if n.kind == IT_SHOW_DESKTOP), None)
        tray = next((n for n in nodes if n.kind == IT_TRAY_MENU), None)
        mail = next((n for n in nodes if n.kind == IT_MAIL), None)
        
        # Split groups into closed pinned nodes and open window groups
        closed_pinned = [n for n in nodes if n.kind == IT_WINDOW_GROUP and n.pinned and n.data is None]
        open_groups = [n for n in nodes if n.kind == IT_WINDOW_GROUP and n not in closed_pinned]
        def grank(g):
            rc = getattr(g, "app_rc", "") or ""
            if rc in self._mru_rc:
                return self._mru_rc.index(rc)
            wins = getattr(g, "data", None)
            stack = wins[0].get("stack", -1) if wins else -1
            return 999 - stack
        open_groups.sort(key=grank)

        r_out = self.r2c + self.seg_depth / 2
        slot = math.degrees(self._seg_phys_w / r_out)
        gap = math.degrees(self._gap_phys / r_out)
        cap = max(1, int(360.0 / slot) - self._ring_gap(2))

        reserved = (1 if sd else 0) + (1 if tray else 0) + (1 if mail else 0)
        reserved += 3 # Immer 3 Segmentplätze für Medienkontrolle freihalten
        group_cap = max(0, cap - reserved)
        
        groups_fitted = (closed_pinned + open_groups)[:group_cap]
        overflow = (closed_pinned + open_groups)[group_cap:]
        
        on_ring2_closed = [g for g in groups_fitted if g in closed_pinned]
        on_ring2_open = [g for g in groups_fitted if g in open_groups]
        
        # Rearrange the active window groups on Ring 2 in a top-centered peak layout:
        # G_1 and G_2 in the middle, older groups split: younger remaining (G_3...G_6) to the right,
        # older remaining (G_7...G_10) to the left in reverse.
        active_segments = []
        L_left = 0
        if len(on_ring2_open) >= 2:
            rem = on_ring2_open[2:]
            # Younger remaining goes to the right (length: half, rounded up if odd)
            half = (len(rem) + 1) // 2
            right = rem[:half]
            # Older remaining goes to the left (reversed)
            left = list(reversed(rem[half:]))
            L_left = len(left)
            active_segments = left + [on_ring2_open[0], on_ring2_open[1]] + right
        else:
            active_segments = list(on_ring2_open)

        # Compose row 0: [closed pinned] [Show Desktop] [active segments] [Tray] [Mail]
        row0 = []
        row0.extend(on_ring2_closed)
        if sd is not None:
            row0.append(sd)
        row0.extend(active_segments)
        if tray is not None:
            row0.append(tray)
        if mail is not None:
            row0.append(mail)

        # Find the index of the first active segment in row0
        idx_active_start = next((i for i, n in enumerate(row0) if n in active_segments), -1)
        
        for c, node in enumerate(row0):
            # Calculate angle based on centering the boundary of the two most recent active segments straight UP
            if idx_active_start != -1 and len(active_segments) >= 2:
                midpoint = idx_active_start + L_left + 0.5
                node.angle = center + (c - midpoint) * slot
            elif idx_active_start != -1 and len(active_segments) == 1:
                # Center G1 exactly
                node.angle = center + (c - idx_active_start) * slot
            else:
                # Fallback: center the entire row0
                total = slot * len(row0)
                node.angle = center - total / 2 + slot / 2 + c * slot
                
            node.radius = self.r2c
            node.half_deg = (slot - gap) / 2
            node.row = 0
            
        nodes[:] = row0 + overflow
        # Surplus window groups fan out to ring 3 (gap 7), then ring 4 (gap 10)...
        if overflow:
            base = self.r2c + (self.seg_depth + 2 * self.s)
            self._layout_radial(overflow, center, base, self.seg_depth,
                                RING3_SPAN, start_ring=3)

    def _layout_ring2(self, sec: int) -> None:
        self.ring2[sec] = list(self._ring2_base.get(sec, []))
        nodes = self.ring2[sec]
        # We must not return early if there are media controls to be drawn
        has_media = (sec == SEC_WINDOWS and getattr(self, "media_status", None) and getattr(self.media_status, "get", lambda x: None)("status"))
        if not nodes and not has_media:
            return
        a0, a1 = self._sector_arc[sec]
        c = self._nominal.get(sec, (a0 + a1) / 2)
        span = (a1 - a0) - 2 * SECTOR_MARGIN
        
        if sec == SEC_FILES and (any(n.pinned for n in nodes) or any(n.kind in (IT_SESSION, IT_TRAY_MENU) for n in nodes)):
            pinned = [n for n in nodes if n.pinned]
            unpinned = [n for n in nodes if not n.pinned and n.kind not in (IT_SESSION, IT_TRAY_MENU)]
            session = next((n for n in nodes if n.kind == IT_SESSION), None)
            tray = next((n for n in nodes if n.kind == IT_TRAY_MENU), None)
            
            r_out = self.r2c + self.seg_depth / 2
            slot = math.degrees(self._seg_phys_w / r_out)
            gap = math.degrees(self._gap_phys / r_out)
            cap = max(1, int(360.0 / slot) - self._ring_gap(2))
            
            reserved = (1 if session else 0) + (1 if tray else 0)
            group_cap = max(0, cap - reserved)
            
            pinned_fit_count = min(len(pinned), group_cap)
            unpinned_fit_count = group_cap - pinned_fit_count
            
            on_ring2_unpinned = unpinned[:unpinned_fit_count]
            on_ring2_pinned = pinned[:pinned_fit_count]
            on_ring2 = on_ring2_unpinned + on_ring2_pinned
            
            overflow = unpinned[unpinned_fit_count:] + pinned[pinned_fit_count:]
            
            row0 = on_ring2
            if session:
                row0 = [session] + row0
            if tray:
                row0 = row0 + [tray]
                
            total = slot * len(row0)
            for col, node in enumerate(row0):
                node.angle = c - total / 2 + slot / 2 + col * slot
                node.radius = self.r2c
                node.half_deg = (slot - gap) / 2
                node.row = 0
                
            nodes[:] = row0 + overflow
            for node in nodes:
                node.sector = SEC_FILES
                
            if overflow:
                base = self.r2c + (self.seg_depth + 2 * self.s)
                self._layout_radial(overflow, c, base, self.seg_depth,
                                    RING3_SPAN, start_ring=3)
            return
        # Start-menu category text segments: single row, only 1 free segment,
        # extras at the end are dropped (not wrapped).  All other lists use the
        # per-ring gaps (ring 2: 4, ring 3: 7, ring 4+: 10).
        if sec == SEC_APPS and self._apps_stage == 1:
            self._layout_radial(nodes, c, self.r2c, self.seg_depth, span,
                                single_row=True, gap_override=1)
        elif sec == SEC_APPS and self._apps_stage == 0:
            # Keep "All Applications" centred and "Session" at the end of ring 2
            # even when recents overflow onto ring 3.
            self._layout_pinned_row(nodes, c, sec)

        elif sec == SEC_WINDOWS:
            # The tray symbol stays at the very end of ring 2 (and Show Desktop
            # at the start) -- they never wrap onto ring 3, no matter how many
            # window groups overflow.
            self._layout_windows_row(nodes, c)
        else:
            if sec == SEC_FILES:
                # Rearrange recent files for layout_radial using center-split ordering:
                # most recent file at center, next files continue right, oldest fill left.
                r_out = self.r2c + self.seg_depth / 2
                slot = math.degrees(self._seg_phys_w / r_out)
                cap = max(1, int(360.0 / slot) - self._ring_gap(2))

                on_ring2 = nodes[:cap]
                overflow = nodes[cap:]

                if on_ring2:
                    anchor = on_ring2[0]   # most recent = center
                    others = on_ring2[1:]  # remaining in recency order (2nd, 3rd, ...)
                    half = (len(others) + 1) // 2
                    right = others[:half]        # 2nd, 3rd, ... go right of center
                    left = others[half:]         # older fill left (oldest furthest left)
                    nodes[:] = left[::-1] + [anchor] + right + overflow
            
            self._layout_radial(nodes, c, self.r2c, self.seg_depth, span)
        for node in nodes:
            node.sector = sec

        is_active_sec = (self.open_sector == -1 and sec == SEC_WINDOWS) or (self.open_sector != -1 and sec == self.open_sector)
        if self.config.get("overlay_mode", "pie") == "switch":
            is_active_sec = True
            
        if is_active_sec and getattr(self, "media_status", None) and getattr(self.media_status, "get", lambda x: None)("status"):
            r_out = self.r2c + self.seg_depth / 2
            slot = math.degrees(self._seg_phys_w / r_out)
            gap = math.degrees(self._gap_phys / r_out)
            
            status = self.media_status.get("status")
            app_name = self.media_status.get("app_name", "Media")
            play_label = "Pause" if status == "Playing" else "Play"
            
            m_prev = Node(kind=IT_MEDIA_BTN, label=f"{app_name} - Previous", glyph="media_prev", data="Previous")
            m_play = Node(kind=IT_MEDIA_BTN, label=f"{app_name} - {play_label}", glyph="media_pause" if status == "Playing" else "media_play", data="PlayPause")
            m_next = Node(kind=IT_MEDIA_BTN, label=f"{app_name} - Next", glyph="media_next", data="Next")
            
            if self.config.get("overlay_mode", "pie") == "switch":
                # In Switch Mode, place opposite to the center (bottom of the ring)
                media_center = c + 180.0
            else:
                # In Pie Mode, place in the free space alongside the windows
                ring2_wins = [n for n in nodes if getattr(n, "row", -1) == 0 and getattr(n, "sector", -1) == sec]
                L = len(ring2_wins)
                cap = max(1, int(span / slot))
                free = cap - L - 3
                
                mid_gap = slot * (1 + free / 3.0) if free > 1 else slot * 0.5
                
                # Shift windows to the left to make room on the right
                shift = (3 * slot + mid_gap) / 2
                for n in ring2_wins:
                    n.angle -= shift
                    
                media_center = c + L * slot / 2.0 + mid_gap / 2.0
            
            # In Qt, 0 is right, 90 is bottom, 180 is left, 270 is top.
            # At the bottom (0 to 180), +angle goes LEFT.
            # At the top (180 to 360), +angle goes RIGHT.
            media_center_norm = media_center % 360.0
            if 0 < media_center_norm < 180:
                m_prev.angle = media_center + slot
                m_next.angle = media_center - slot
            else:
                m_prev.angle = media_center - slot
                m_next.angle = media_center + slot
                
            m_play.angle = media_center
            
            for n in (m_prev, m_play, m_next):
                n.radius = self.r2c
                n.half_deg = (slot - gap) / 2
                n.row = 0
                n.sector = sec
                nodes.append(n)

    # -- pointer input ------------------------------------------------------
    def set_pointer(self, dx: float, dy: float) -> None:
        # dx/dy are the virtual delta from the menu center (since open); the
        # persistent vertical offset keeps the start point above centre.
        x = self._center.x() + dx
        y = self._center.y() + dy + self._cursor_oy
        self._pointer = QPointF(x, y)
        self._path.append((time.monotonic(), x, y))
        # Hit-testing is throttled to the 30 fps tick (see _tick): a high-rate
        # mouse (125-1000 Hz) would otherwise recompute the full nearest-node
        # hover search on every raw event.  We only record that the pointer moved;
        # _update_hover() runs once per frame, which is visually identical.
        self._hover_dirty = True
        self.request_repaint()

    def request_repaint(self) -> None:
        if not self.isVisible():
            return
        self._frame_requested = True
        if (not self._tick_timer.isActive()
                or self._tick_timer.remainingTime() > 0):
            # Start loop instantly if idle, but _tick will enforce 16ms pacing.
            self._tick_timer.start(0)

    def _schedule_next_idle_tick(self) -> None:
        if not self.isVisible() or self._tick_timer.isActive():
            return
        if self._repeat_active():
            self._tick_timer.start(80)
            return
        if self.config.get("hub_show_clock", True) or self.config.get("hub_show_date", True):
            self._tick_timer.start(1000)

    def _repeat_active(self) -> bool:
        hn = self.hover_node
        return (self._ctrl_node is not None and hn is not None
                and hn.kind == IT_CTRL_BTN and hn.data and hn.data[0] == "repeat"
                and self._ctrl_on_button)

    def _pointer_polar(self) -> tuple:
        dx = self._pointer.x() - self._center.x()
        dy = self._pointer.y() - self._center.y()
        r = math.hypot(dx, dy)
        a = math.degrees(math.atan2(dy, dx))
        return r, a

    def _clear_hover_targets(self) -> None:
        for sec in self.active_sectors:
            for node in self.ring2.get(sec, []):
                node.hover_t_target = 0.0
                for kid in node.children:
                    kid.hover_t_target = 0.0
        if self._ctrl_node is not None:
            for btn in getattr(self._ctrl_node, "children_ctrl", []):
                btn.hover_t_target = 0.0

    def _set_open_sector(self, sec: int) -> None:
        if sec == self.open_sector:
            return
        self.open_sector = sec
        self.open_group = None
        self._ctrl_node = None
        if sec != SEC_APPS:
            self._apps_stage = 0
        self._rebuild_apps_stage()
        self._layout_sectors()
        self._check_start_mail_blink()

    def _update_hover(self) -> None:
        r, a = self._pointer_polar()

        # Neutral centre dot: NO trigger -- keep the current selection (and its
        # highlight) so a press+release without moving fires the pre-selection.
        if r <= self.r_neutral:
            return

        self._clear_hover_targets()
        self.hover_close = self.hover_close_all = self.hover_mute = False
        self.hover_pin = False
        self.hover_mail = False

        # Offset-open lock: only the initial sector is selectable by moving; the
        # other two sectors can only be reached via the drawn hub (which unlocks).
        if self._win_lock:
            if r <= self.r_hub:
                self._win_lock = False        # reached the hub -> unlock
            else:
                sec = self.open_sector if self.open_sector != -1 else self.active_sectors[0]
                self.open_sector = sec
                self._hover_in_sector(sec, r, a)
                return

        self.hover_volume_area = False
        
        # Inner circle: trigger zones for switching AND a compressed selection
        # field for the active sector's symbols.
        if r <= self.r_hub:
            if self.config.get("overlay_mode", "pie") == "switch" and self.config.get("adjust_volume_with_trigger_wheel", True):
                dy = r * math.sin(math.radians(a))
                if dy > 25 * self.s:
                    self.hover_volume_area = True
            
            here = self._sector_containing(a)
            if self.open_sector == -1 or here != self.open_sector:
                self._set_open_sector(here)
                self.hover_node = None
                self.open_group = None
                self._ctrl_node = None
                return
            # within the active sector -> select its symbols (like ring 1)
            self._hover_in_sector(self.open_sector, r, a)
            return

        if self.open_sector == -1:
            self._set_open_sector(self._sector_containing(a))

        self._hover_in_sector(self.open_sector, r, a)

    def _hover_window_lock(self, r: float, a: float) -> None:
        nodes = [n for n in self.ring2[SEC_WINDOWS]
                 if n.kind in (IT_WINDOW_GROUP, IT_SHOW_DESKTOP, IT_TRAY_MENU)]
        self._pick_nearest(nodes, r, a)

    def _hover_in_sector(self, sec: int, r: float, a: float) -> None:
        if sec == -1 or sec not in self.ring2:
            self.hover_node = None
            return

        # Control submenu has its own captured state.
        if self._ctrl_node is not None:
            if self._hover_control(r, a):
                return

        # In the Apps category stage, ring 1 (the band below ring 2) is the way
        # back: touching it morphs the row back to the recent apps.
        if (sec == SEC_APPS and self._apps_stage == 1 and r < self.r2_in):
            self._apps_stage = 0
            self.open_group = None
            self._ctrl_node = None
            self._rebuild_apps_stage()
            self._layout_sectors()
            self.hover_node = None
            return

        # Sticky open group: while the pointer is beyond the band, keep it open
        # and select among its children.
        if self.open_group is not None:
            threshold = self.r3_out if self.open_group.radius > self.r2c + 0.1 else self.r2_out
            if r > threshold:
                self._hover_open_group_children(a)
                return

        nodes = self.ring2[sec]
        if self.open_group is not None:
            nodes = [n for n in nodes if n.radius <= self.r2c + 0.1 or n is self.open_group]
        node = self._pick_nearest(nodes, r, a)
        if node is None:
            self.open_group = None
            return

        if node.kind == IT_MAIL:
            self._mail_blink_state = 0
            self._mail_blink_timer.stop()

        # The outer-edge zone of THIS node's row (close / drill bar).  The bar
        # only spans the segment's own angular width -- require the cursor to be
        # angularly WITHIN it, else aiming at empty space beyond the last element
        # (but at the outer radius) would falsely trigger the nearest node's bar.
        row = getattr(node, "row", 0)
        node_rout = node.radius + self.seg_depth / 2
        # The bar is a BAND at the segment's outer edge: within the drill zone
        # radially AND within the segment angularly.  Beyond the edge (toward the
        # next ring) or beside the segment is empty space -> never triggers.
        in_outer = (node_rout - self.drill_zone <= r <= node_rout + self.s
                    and _ang_dist(a, node.angle) <= node.half_deg)

        # Pin badge hover check
        if node.kind in (IT_WINDOW_GROUP, IT_APP, IT_FILE):
            if self._over_pin(node, r, a):
                self.hover_pin = True
                return

        # Apps menu button: drilling morphs the row into categories.
        if node.kind == IT_MENU and in_outer:
            if self._apps_stage != 1:
                self._apps_stage = 1
                self._ctrl_node = None
                self._rebuild_apps_stage()
                self._layout_sectors()
                self._apps_morph_t = 0.0 if self.config.get("category_anim", True) else 1.0
            return

        # Control drilldown.
        if node.control and in_outer:
            if self._ctrl_node is not node:
                self._ctrl_node = node
                self._ctrl_t = 0.0
                self._layout_control_buttons(node)
            return

        # Multi-window group: a round count badge on the symbol closes ALL its
        # windows when the pointer is over it (outer edge still drills).
        if node.kind == IT_WINDOW_GROUP and node.children:
            if self._over_badge(node, r, a):
                self.hover_close_all = True
                return

        # Speaker badge (opposite side from the count badge): mute the app.
        if (node.kind == IT_WINDOW_GROUP and self.audio is not None
                and self.audio.has_stream(getattr(node, "app_pid", 0),
                                          getattr(node, "app_rc", ""))
                and self._over_speaker(node, r, a)):
            self.hover_mute = True
            return

        # Drilldown into children.
        if node.children:
            if self._opens_on_hover(node) or (self._needs_bar(node) and in_outer):
                if self.open_group is not node:
                    self.open_group = node
                    self._ctrl_node = None
                    self._drill_armed = False
                    self._layout_children(node)

    def _hover_open_group_children(self, a: float) -> None:
        r, _a = self._pointer_polar()
        best = self._pick_nearest(self.open_group.children, r, a)
        if best is None:
            return
        
        # Pin badge hover check for child app/file nodes (e.g. in Favorites / Categories)
        if best.kind in (IT_WINDOW_GROUP, IT_APP, IT_FILE):
            if self._over_pin(best, r, a):
                self.hover_pin = True
                return

        edge = best.radius + self.seg3_depth / 2
        # Same rule as ring 2: the outer bar is a BAND (within the drill zone
        # radially AND within the segment angularly), so a ring-3 control / close
        # bar only triggers on the bar itself -- not from empty space beside or
        # beyond it.
        on_bar = (edge - self.drill_zone <= r <= edge + self.s
                  and _ang_dist(a, best.angle) <= best.half_deg)
        # SNI tray app or local launcher symbol: drilling its bar fans out its
        # context menu one ring further.
        if (getattr(best, "menu", None) or getattr(best, "local_menu", None)) and on_bar:
            if self._ctrl_node is not best:
                self._ctrl_node = best
                self._ctrl_t = 0.0
                self._layout_menu_children(best)
            return
        # "..." popup drawer: detected by its control marker, NOT by a non-empty
        # child list -- an empty drawer must still open (and may fill in as tray
        # items appear), and must never fall through to the generic control branch.
        if getattr(best, "control", None) == "drawer" and on_bar:
            if self._ctrl_node is not best:
                self._ctrl_node = best
                self._ctrl_t = 0.0
                self._layout_drawer_children(best)
            return
        if getattr(best, "control", None) and on_bar:
            if self._ctrl_node is not best:
                self._ctrl_node = best
                self._ctrl_t = 0.0
                self._layout_control_buttons(best)
            return

    def _pick_nearest(self, nodes: list, r: float, a: float) -> Node | None:
        best, bestscore = None, 1e9
        for n in nodes:
            if n.half_deg <= 0.01:      # skip dropped nodes
                continue
            score = _ang_dist(a, n.angle) + 0.5 * abs(r - n.radius) / max(self.s, 0.1)
            if score < bestscore:
                best, bestscore = n, score
        for n in nodes:
            n.hover_t_target = 1.0 if n is best else 0.0
        self.hover_node = best
        return best

    def _opens_on_hover(self, node: Node) -> bool:
        # Window groups and categories open on aim (no drill bar); favorites,
        # tray, session, menu and control need the cyan drill bar.
        return node.kind in (IT_WINDOW_GROUP, IT_CATEGORY)

    def _needs_bar(self, node: Node) -> bool:
        # Items that reveal ring 3 only via the cyan drill bar.
        if node.kind in (IT_MENU, IT_FAV_MENU, IT_TRAY_MENU):
            return True
        if node.kind == IT_SESSION and not node.data:
            return True
        if (node.kind == IT_TRAY
                and (getattr(node, "menu", None) or getattr(node, "local_menu", None))):
            return True
        return bool(node.control)

    def _is_drillable(self, node: Node) -> bool:
        # Favorites, menu and tray get a cyan drill bar; categories open on aim.
        if node.kind in (IT_MENU, IT_FAV_MENU, IT_TRAY_MENU):
            return True
        if node.kind == IT_SESSION and not node.data:
            return True
        if node.kind == IT_WINDOW_GROUP and node.children:
            return True
        # An SNI tray app or local launcher symbol with a context menu drills into
        # that menu.
        if (node.kind == IT_TRAY
                and (getattr(node, "menu", None) or getattr(node, "local_menu", None))):
            return True
        return bool(node.control)

    def _in_drill_zone(self, r: float) -> bool:
        return r >= self.r2_out - self.drill_zone

    def _layout_children(self, node: Node) -> None:
        kids = node.children
        if not kids:
            return
        if node.radius > self.r2c + 0.1:
            self._layout_radial(kids, node.angle, self.r4c, self.seg3_depth,
                                RING3_SPAN, start_ring=4)
        else:
            self._layout_radial(kids, node.angle, self.r3c, self.seg3_depth,
                                RING3_SPAN, start_ring=3)

    # -- control buttons ----------------------------------------------------
    def _layout_control_buttons(self, node: Node) -> None:
        controls = getattr(node, "controls", []) or []
        node.children_ctrl = []
        # Standard physical width (same as every other segment).
        r_out = self.r4c + self.seg3_depth / 2
        slot = math.degrees(self._seg_phys_w / r_out)
        gap = math.degrees(self._gap_phys / r_out)
        count = len(controls)
        start = node.angle - (count - 1) * slot / 2
        for i, (glyph, label, mode, argv) in enumerate(controls):
            btn = Node(kind=IT_CTRL_BTN, label=label, glyph=glyph,
                       data=(mode, argv), angle=start + i * slot,
                       radius=self.r4c, half_deg=(slot - gap) / 2)
            node.children_ctrl.append(btn)

    def _hover_control(self, r: float, a: float) -> bool:
        node = self._ctrl_node
        if node is None:
            return False
        depth = self.seg3_depth
        band = self.r4c
        buttons = getattr(node, "children_ctrl", [])
        in_band = abs(r - band) <= depth / 2 + 8 * self.s
        self._ctrl_on_button = False
        best, bestd = None, 999.0
        if in_band:
            for btn in buttons:
                d = _ang_dist(a, btn.angle)
                if d <= btn.half_deg + 12 and d < bestd:
                    best, bestd = btn, d
        if best is not None:
            self.hover_node = best
            # Strict containment: the auto-repeat (volume/brightness) fires ONLY
            # when the pointer is really ON the segment, not merely nearest.
            self._ctrl_on_button = (bestd <= best.half_deg
                                    and abs(r - band) <= depth / 2)
            return True  # the repeat itself is driven from _tick (fires while still)
        # Not on a control button: keep this control open ONLY while its owning
        # symbol is still the nearest ring-3 item.  Sliding onto a different
        # drilldown sibling drops it, so that sibling opens its own next frame.
        if r >= self.r3_in:
            sibs = [n for n in (self.open_group.children if self.open_group else [node])
                    if n.half_deg > 0.01]
            nearest = min(sibs, key=lambda n: _ang_dist(a, n.angle), default=node)
            if nearest is node and _ang_dist(a, node.angle) <= node.half_deg + 14:
                self.hover_node = node     # still on the owning symbol
                return True
        # Moved off (toward the hub or onto another drilldown). 
        # Do NOT drop the control, so it stays visible while hovering other items.
        return False

    # -- release / activate -------------------------------------------------
    def on_release(self) -> None:
        # Flush any throttled pointer move so the selection reflects the final
        # cursor position, not the last frame's (a fast release right after a
        # move must not pick a stale node).
        if self._hover_dirty:
            self._hover_dirty = False
            self._update_hover()
        node = self.hover_node
        if self._gesture_fired:
            self.close_menu()
            return
        if node is None:
            self.close_menu()
            return
        if self.hover_mute and self.audio is not None:
            self.audio.toggle_mute(getattr(node, "app_pid", 0),
                                   getattr(node, "app_rc", ""))
            self.close_menu()
            return
        if self.hover_pin:
            self._toggle_pin(node)
            self.close_menu()
            return
        self._activate(node, self.hover_close, self.hover_close_all)

    def _toggle_pin(self, node: Node) -> None:
        if node.kind == IT_FILE:
            path = node.data
            if not path:
                return
            if self.pins.is_file_pinned(path):
                self.pins.unpin_file(path)
                log.info("UNPINNED FILE: %s", path)
            else:
                self.pins.pin_file(path)
                log.info("PINNED FILE: %s", path)
        elif node.kind in (IT_APP, IT_WINDOW_GROUP):
            app_id = None
            if node.kind == IT_APP:
                app_id = node.data.app_id if node.data else None
            elif node.kind == IT_WINDOW_GROUP:
                rc = getattr(node, "app_rc", "")
                if rc:
                    app = self.app_index.match_window(rc)
                    app_id = app.app_id if app else f"pseudo:{rc.lower()}"
            if not app_id:
                return
            if self.pins.is_app_pinned(app_id):
                self.pins.unpin_app(app_id)
                log.info("UNPINNED APP: %s", app_id)
            else:
                self.pins.pin_app(app_id)
                log.info("PINNED APP: %s", app_id)

    def _activate(self, node: Node, close_one: bool, close_all: bool) -> None:
        kind = node.kind
        if kind == IT_MEDIA_BTN:
            self.close_menu()
            self.request_media.emit(node.data)
            return
        if kind == IT_MAIL:
            self.close_menu()
            if close_one:
                it = getattr(node, "tray_item", None)
                if it:
                    self.tray.mute_attention(it.service, it.icon_sig)
            else:
                self._activate_tray(node)
            return
        if kind in (IT_WINDOW_GROUP, IT_WINDOW):
            if kind == IT_WINDOW_GROUP and node.data is None:
                self.close_menu()
                app_id = getattr(node, "app_rc", "")
                if app_id:
                    app = self.app_index.apps.get(app_id) or self.app_index._pseudo.get(app_id)
                    if not app:
                        app = self.app_index._find_app(app_id)
                    if app:
                        self.app_index.launch(app)
                return
            if kind == IT_WINDOW_GROUP and node.data:
                win_id = node.data[0]["id"]
            else:
                win_id = node.data
            if close_all and kind == IT_WINDOW_GROUP:
                self.close_menu()
                # Close EVERY window of the group (incl. the active main one),
                # staggered so KWin reliably processes each close.
                ids = [w["id"] for w in node.data]
                for i, wid in enumerate(ids):
                    QTimer.singleShot(i * 70, lambda x=wid: self.kwin.close(x))
            elif close_one:
                self.close_menu()
                self.kwin.close(win_id)
            else:
                # Close first, then kwin.activate re-asserts the activation
                # several times so it lands after KWin's focus-restore-on-close.
                log.info("ACTIVATE window %s", win_id[:14])
                self._note_activated(win_id, rc=getattr(node, 'app_rc', ''))
                self.close_menu()
                self.kwin.activate(win_id)
            return
        if kind == IT_APP:
            self.close_menu()
            self.app_index.launch(node.data)
            return
        if kind == IT_FILE:
            self.close_menu()
            self.recent_files.open(node.data)
            return
        if kind == IT_TRAY_MENUITEM:
            self.close_menu()
            data = node.data or ()
            if data and data[0] == "menu_click":
                _tag, bus, path, item_id = data
                from . import dbusmenu
                dbusmenu.send_clicked(bus, path, item_id)
            elif data and data[0] == "sni":
                self._activate_tray(node)   # "Öffnen" entry
            elif data and data[0] == "dl_settings":
                self.request_settings.emit()
            elif data and data[0] == "dl_setup_input":
                self.request_setup_input.emit()
            elif data and data[0] == "dl_quit":
                self.request_quit.emit()
            return
        if kind == IT_TRAY:
            if node.data == "__dl_settings__":
                self.close_menu()
                self.request_settings.emit()
                return
            # Releasing on a tray app's symbol behaves like a left-click in a
            # real tray: activate it (almost always = bring the app to the
            # front).  Its context menu is reached via the cyan drill bar.
            self._activate_tray(node)
            self.close_menu()
            return
        if kind == IT_CTRL_BTN:
            mode, argv = node.data
            if mode == "toggle":
                self._run_control(argv)
            self.close_menu()
            return
        if kind == IT_SESSION and node.data:
            self.close_menu()
            self.request_session.emit(node.data)
            return
        if kind == IT_SHOW_DESKTOP:
            self.close_menu()
            self.kwin.toggle_show_desktop()
            return
        # passive / drilldown-only nodes -> no action, but ALWAYS close so the
        # overlay never stays up with the mouse grabbed (was a freeze).
        self.close_menu()

    def _service_pid(self, service: str) -> int:
        if not service:
            return 0
        if "/" in service:
            bus = service.split("/", 1)[0]
        else:
            bus = service
        from PyQt6.QtDBus import QDBusConnection
        reply = QDBusConnection.sessionBus().interface().servicePid(bus)
        if reply.isValid():
            return int(reply.value())
        return 0

    def _find_tray_item(self, service: str):
        for it in getattr(self.tray, "sni_items", []) + getattr(self.tray, "hidden_sni_items", []):
            if it.service == service:
                return it
        return None

    def _find_window_node_for_tray(self, tray_item) -> Node | None:
        if not tray_item:
            return None
        pid = self._service_pid(tray_item.service)
        if pid > 0:
            for node in self.ring2.get(SEC_WINDOWS, []):
                if node.kind == IT_WINDOW_GROUP and getattr(node, "app_pid", 0) == pid:
                    return node
        label = (getattr(tray_item, "label", "") or "").lower()
        if label:
            for node in self.ring2.get(SEC_WINDOWS, []):
                if node.kind == IT_WINDOW_GROUP:
                    rc = (getattr(node, "app_rc", "") or "").lower()
                    if rc and (rc in label or label in rc):
                        return node
        return None

    def _activate_tray_item(self, tray_item, fallback_service=None) -> None:
        service = tray_item.service if tray_item else fallback_service
        if not service:
            return
        win_node = self._find_window_node_for_tray(tray_item)
        if win_node and win_node.data:
            win_id = win_node.data[0]["id"] if isinstance(win_node.data, list) else win_node.data
            log.info("Tray activation: bringing window %s to foreground", win_id[:14])
            self._note_activated(win_id, rc=getattr(win_node, 'app_rc', ''))
            self.kwin.activate(win_id)
        else:
            log.info("Tray activation: calling activate_sni for %s", service)
            self.tray.activate_sni(service)

    def _activate_tray(self, node: Node) -> None:
        data = node.data or ("noop", None)
        kind, payload = data
        if kind == "sni":
            tray_item = self._find_tray_item(payload)
            self._activate_tray_item(tray_item, fallback_service=payload)
        elif kind == "klipper":
            self.tray.show_clipboard()
        elif kind == "builtin":
            self.tray.run_builtin(payload)
        # noop -> nothing

    def _run_control(self, argv) -> None:
        """Run a control command, substituting the configured volume step into
        wpctl volume commands ('1%-' -> '<volume_steps>%-')."""
        argv = list(argv)
        if (len(argv) >= 2 and argv[0] == "wpctl"
                and argv[-1] and argv[-1][-1] in "+-"):
            step = max(1, int(self.config.get("volume_steps", 1)))
            argv[-1] = f"{step}%{argv[-1][-1]}"
        self.tray.run_builtin(argv)

    # -- gesture check ------------------------------------------------------
    def check_gesture(self) -> str | None:
        if not self.config.get("gestures_enabled", True):
            return None
        if (time.monotonic() - self._open_time) * 1000.0 > self.config["gesture_time_window"]:
            return None
        from .input_proxy import classify_direction
        r, a = self._pointer_polar()
        if r < self.r2_out:
            return None
        speed = self._recent_speed()
        if speed < self.config["gesture_min_speed"]:
            return None
        dx = self._pointer.x() - self._center.x()
        dy = self._pointer.y() - self._center.y()
        self._gesture_fired = True
        return classify_direction(dx, dy, self.config["gesture_diagonal_size"])

    def _recent_speed(self) -> float:
        if len(self._path) < 2:
            return 0.0
        now = self._path[-1][0]
        window = [p for p in self._path if now - p[0] <= 0.05]
        if len(window) < 2:
            window = self._path[-2:]
        (t0, x0, y0), (t1, x1, y1) = window[0], window[-1]
        dt = max(1e-4, t1 - t0)
        return math.hypot(x1 - x0, y1 - y0) / dt



    def _check_start_mail_blink(self) -> None:
        """Start the mail blink animation whenever the overlay opens with a
        mail badge present, as long as a blink is not already running."""
        if self._mail_blink_state != 0:
            return
        nodes = self.ring2.get(SEC_WINDOWS, [])
        has_mail = any(n.kind == IT_MAIL for n in nodes)
        if has_mail:
            self._mail_blink_state = 1
            self._mail_blink_timer.start(250)
            self.update()

    def _on_mail_blink_timeout(self) -> None:
        if self._mail_blink_state > 0 and self._mail_blink_state < 8:
            self._mail_blink_state += 1
            self._mail_blink_timer.start(250)
            self.update()
        else:
            self._mail_blink_state = 0
            self.update()

    # -- animation tick -----------------------------------------------------
    def _approach_angle(self, cur: float, tgt: float, rate: float = 0.32) -> float:
        d = _norm(tgt - cur)
        if abs(d) < 0.15:
            return tgt
        return cur + d * rate

    def _tick(self) -> None:
        repaint = self._frame_requested
        self._frame_requested = False
        # Throttled hit-testing: set_pointer() only flags movement; recompute the
        # hover target at most once per frame (here), not on every raw input event.
        if self._hover_dirty:
            self._hover_dirty = False
            self._update_hover()
            repaint = True
        # Snap animations straight to their targets (rate 1.0) unconditionally,
        # as requested, to minimize CPU usage and maximize responsiveness.
        ang_r = 1.0

        def R(base):
            return 1.0

        animating = False

        old_open = self.open_t
        self.open_t = approach(self.open_t, 1.0, R(0.35))
        if self.open_t != old_open: animating = True
        
        # Control repeat (Volume/Brightness +/-): time-based 5%/s (1% every 0.2s),
        # fps-independent, fires while the mouse is held still on the button.
        hn = self.hover_node
        if self._repeat_active():
            now = time.monotonic()
            if now - self._repeat_last >= 0.5:
                self._repeat_last = now
                self._run_control(hn.data[1])
                repaint = True
        # animate the ring-1 sector transformation (centre + half-width)
        for sec in self.active_sectors:
            tgt = self._sector_vis.get(sec)
            cur = self._sector_draw.get(sec)
            if tgt and cur:
                old0, old1 = cur[0], cur[1]
                cur[0] = self._approach_angle(cur[0], tgt[0], ang_r)
                cur[1] = approach(cur[1], tgt[1], R(0.32))
                if cur[0] != old0 or cur[1] != old1: animating = True
                
        for sec in self.active_sectors:
            for node in self.ring2[sec]:
                target = getattr(node, "hover_t_target", 0.0)
                old_t = node.hover_t
                node.hover_t = approach(node.hover_t, target, R(0.35))
                if node.hover_t != old_t: animating = True
                
        if self.open_group is not None:
            for kid in self.open_group.children:
                target = 1.0 if kid is self.hover_node else 0.0
                old_t = kid.hover_t
                kid.hover_t = approach(kid.hover_t, target, R(0.35))
                if kid.hover_t != old_t: animating = True
                
        if self._ctrl_node is not None:
            old_c = self._ctrl_t
            self._ctrl_t = approach(self._ctrl_t, 1.0, R(0.3))
            if self._ctrl_t != old_c: animating = True
            for btn in getattr(self._ctrl_node, "children_ctrl", []):
                target = 1.0 if btn is self.hover_node else 0.0
                old_t = btn.hover_t
                btn.hover_t = approach(btn.hover_t, target, R(0.35))
                if btn.hover_t != old_t: animating = True
        else:
            old_c = self._ctrl_t
            self._ctrl_t = approach(self._ctrl_t, 0.0, R(0.3))
            if self._ctrl_t != old_c: animating = True
            
        if animating:
            repaint = True
            fps_delay = 33 # Standardized to ~30 FPS
            self._tick_timer.start(fps_delay)
            
        self._is_idle_clock = False
        if self.config.get("hub_show_clock", True) or self.config.get("hub_show_date", True):
            if not repaint:
                self._is_idle_clock = True
            repaint = True
            
        if repaint:
            self.update(self._dirty_rect())
            self._prev_pointer = QPointF(self._pointer)
            self._last_title_rect = self._current_title_rect()
            
        if not animating:
            self._schedule_next_idle_tick()

    def _dirty_rect(self) -> QRect:
        s = self.s
        cx, cy = self._center.x(), self._center.y()
        
        # Optimization: If ONLY the clock/date changes (idle tick), restrict 
        # damage rect to the inner hub to prevent full-screen flickering.
        if getattr(self, "_is_idle_clock", False):
            hr = int(self.r_hub + 4 * s)
            return QRect(int(cx - hr), int(cy - hr), 2 * hr, 2 * hr)
            
        # Generously cover the outermost drawable: ring-4 outer edge + drill/close
        # bar + margin.
        max_r = self.r3_out + 2 * s + self.seg3_depth + self.bar_w + 24 * s
        rect = QRect(int(cx - max_r), int(cy - max_r),
                     int(2 * max_r), int(2 * max_r))
        cr = int(16 * s)   # cursor dot + pen + margin

        def crect(pt):
            return QRect(int(pt.x() - cr), int(pt.y() - cr), 2 * cr, 2 * cr)

        # Union current AND previous cursor/title so old pixels are erased when
        # the pointer or hovered node moves.  Long title pills can extend far
        # beyond the radial menu, so the menu-only dirty rect is not enough.
        return (rect.united(crect(self._pointer))
                .united(crect(self._prev_pointer))
                .united(self._current_title_rect())
                .united(self._last_title_rect))

    # -- painting -----------------------------------------------------------
    def paintEvent(self, event) -> None:  # noqa: N802
        p = QPainter(self)
        
        # Always use Antialiasing. The old performance mode was removed.
        self._aa = True
        p.setRenderHint(QPainter.RenderHint.Antialiasing, True)
        
        # Clear the damaged region to fully transparent first.  With partial
        # (region) repaints on a translucent surface this is mandatory -- without
        # it the new frame's semi-transparent pixels blend onto the old frame's
        # and alpha accumulates (ghosting/trails).
        p.setCompositionMode(QPainter.CompositionMode.CompositionMode_Source)
        p.fillRect(event.rect(), Qt.GlobalColor.transparent)
        p.setCompositionMode(QPainter.CompositionMode.CompositionMode_SourceOver)
        cx, cy = self._center.x(), self._center.y()

        if getattr(self, "_intro_stage", -1) >= 0:
            # Filigree Azonix-style vector font (100x100 grid) with perfect uniform line width
            H = self.seg_depth * 0.5
            W = self.seg_depth * 0.6
            
            custom_font = {
                'K': {'outer': [(0,0), (10,0), (10,40), (80,0), (101,0), (14,50), (101,100), (80,100), (10,60), (10,100), (0,100)]},
                'A': {'outer': [(0,100), (45,0), (55,0), (100,100), (85,100), (50, 22.2), (15,100)]},
                'R': {'outer': [(0,0), (80,0), (100,20), (100,40), (80,60), (65,60), (100,100), (85,100), (50,60), (10,60), (10,100), (0,100)],
                      'hole': [(10,10), (75,10), (90,25), (90,35), (75,50), (10,50)]},
                'Y': {'outer': [(0,0), (15,0), (50,42), (85,0), (100,0), (55,54), (55,100), (45,100), (45,54)]},
                'O': {'outer': [(20,0), (80,0), (100,20), (100,80), (80,100), (20,100), (0,80), (0,20)],
                      'hole': [(25,10), (75,10), (90,25), (90,75), (75,90), (25,90), (10,75), (10,25)]},
                'N': {'outer': [(0,0), (10,0), (90,90), (90,0), (100,0), (100,100), (90,100), (10,10), (10,100), (0,100)]}
            }
            
            num_letters = len(self._intro_word)
            spacing = W * 0.4
            total_width = num_letters * W + (num_letters - 1) * spacing
            start_x = cx - total_width / 2.0 + W / 2.0
            
            from PyQt6.QtGui import QPainterPath, QPainterPathStroker
            
            stroker = QPainterPathStroker()
            # Very slight rounding to soften the sharp geometric edges without bloating the thin lines
            stroker.setWidth(H * 0.05)
            stroker.setJoinStyle(Qt.PenJoinStyle.RoundJoin)
            stroker.setCapStyle(Qt.PenCapStyle.RoundCap)
            
            for i in range(min(self._intro_stage + 1, num_letters)):
                x = start_x + i * (W + spacing)
                y = cy
                
                p.save()
                p.translate(x, y)
                
                letter = self._intro_word[i]
                data = custom_font.get(letter, {'outer': []})
                
                path = QPainterPath()
                path.setFillRule(Qt.FillRule.OddEvenFill)
                
                for k in ('outer', 'hole'):
                    pts = data.get(k, [])
                    if not pts: continue
                    for j, pt in enumerate(pts):
                        px = (pt[0] / 100.0) * W - W/2
                        py = (pt[1] / 100.0) * H - H/2
                        if j == 0: path.moveTo(px, py)
                        else: path.lineTo(px, py)
                    path.closeSubpath()
                
                # Apply slight rounding
                stroked = stroker.createStroke(path)
                rounded_path = path.united(stroked)
                
                # Black-blue fill with ~25% transparency (alpha ~190)
                p.setBrush(QColor(10, 30, 80, 190))
                # Thin cyan outer border
                p.setPen(QPen(QColor(0, 255, 255, 255), 1.0))
                p.setRenderHint(QPainter.RenderHint.Antialiasing, True)
                
                p.drawPath(rounded_path)
                p.restore()
            return

        # global transparency
        trans = self.config.get("transparency", 10) / 100.0
        p.setOpacity(1.0 - trans * 0.5)

        active_progress_nodes = frozenset(
            id(node) for node in self.ring2.get(self.open_sector, []) if self._has_progress(node)
        )
        pending_close_nodes = frozenset(
            id(node) for node in self.ring2.get(self.open_sector, []) if getattr(node, "pending_close", False)
        )
        current_state = (self.open_sector, id(self.open_group), id(self._ctrl_node), self.size().width(), active_progress_nodes, pending_close_nodes, getattr(self, "_mail_blink_state", 0), getattr(self, "_model_revision", 0))
        if not getattr(self, "_static_frame_cache", None) or getattr(self, "_cache_state", None) != current_state:
            from PyQt6.QtGui import QPixmap
            self._static_frame_cache = QPixmap(self.size())
            self._static_frame_cache.fill(Qt.GlobalColor.transparent)
            cp = QPainter(self._static_frame_cache)
            cp.setRenderHint(QPainter.RenderHint.Antialiasing, True)
            
            # Temporarily clear hover_node so no hover-specific visuals (like opaque pins)
            # are baked into the static cache.
            old_hover_node = self.hover_node
            self.hover_node = None
            
            self._paint_hub(cp, cx, cy)
            
            active_in_ring2 = (
                (self.open_group is not None and self.open_group.radius <= self.r2c + 0.1)
                or (self._ctrl_node is not None and self._ctrl_node.radius <= self.r2c + 0.1)
            )
            for sec in self.active_sectors:
                # If a sector is open, only draw that sector's nodes
                if self.open_sector != -1 and sec != self.open_sector:
                    continue
                for node in self.ring2.get(sec, []):
                    if active_in_ring2 and node.radius > self.r2c + 0.1:
                        if node is not self.open_group and node is not self._ctrl_node:
                            continue
                    if self._has_progress(node):
                        continue
                    old_h = node.hover_t; node.hover_t = 0.0
                    self._paint_node(cp, cx, cy, node)
                    node.hover_t = old_h

            if self.open_group is not None:
                for kid in self.open_group.children:
                    old_h = kid.hover_t; kid.hover_t = 0.0
                    self._paint_node(cp, cx, cy, kid, ring3=True)
                    kid.hover_t = old_h
                if (self._is_drillable(self.open_group) and self.open_group.kind != IT_WINDOW_GROUP):
                    self._paint_drill_bar(cp, cx, cy, self.open_group)

            if self._ctrl_node is not None and self._ctrl_t > 0.05:
                for btn in getattr(self._ctrl_node, "children_ctrl", []):
                    old_h = btn.hover_t; btn.hover_t = 0.0
                    self._paint_node(cp, cx, cy, btn, ring3=True)
                    btn.hover_t = old_h
                    
            self.hover_node = old_hover_node
            cp.end()
            self._cache_state = current_state

        if not getattr(self, "_current_frame", None) or self._current_frame.size() != self.size():
            from PyQt6.QtGui import QPixmap
            self._current_frame = QPixmap(self.size())

        tp = QPainter(self._current_frame)
        tp.setCompositionMode(QPainter.CompositionMode.CompositionMode_Source)
        tp.drawPixmap(0, 0, self._static_frame_cache)
        tp.setCompositionMode(QPainter.CompositionMode.CompositionMode_SourceOver)
        tp.setRenderHint(QPainter.RenderHint.Antialiasing, True)

        if self.hover_node is not None:
            ring3 = (self.open_group is not None and self.hover_node in self.open_group.children) or \
                    (self._ctrl_node is not None and self.hover_node in getattr(self._ctrl_node, "children_ctrl", []))
            self._paint_node(tp, cx, cy, self.hover_node, ring3=ring3)
            
        # LIVE nodes: nodes with progress animate independently of hover, so we
        # bypass the static cache and draw them here.
        active_in_ring2 = (
            (self.open_group is not None and self.open_group.radius <= self.r2c + 0.1)
            or (self._ctrl_node is not None and self._ctrl_node.radius <= self.r2c + 0.1)
        )
        for sec in self.active_sectors:
            if self.open_sector != -1 and sec != self.open_sector:
                continue
            for node in self.ring2.get(sec, []):
                if node is self.hover_node:  # Already drawn above
                    continue
                if self._has_progress(node):
                    if active_in_ring2 and node.radius > self.r2c + 0.1:
                        if node is not self.open_group and node is not self._ctrl_node:
                            continue
                    self._paint_node(tp, cx, cy, node)
                    
        tp.end()
        
        p.drawPixmap(0, 0, self._current_frame)

        # title label for the hovered item (black on white), at the element
        if self.hover_node is not None and self.hover_node.label:
            self._paint_title(p, cx, cy, self.hover_node)
            # Re-draw the hovered node's badges so the white title pill can never
            # cover its count / audio / mail badges.
            self._paint_badges(p, cx, cy, self.hover_node)

        # virtual pointer (the real cursor is parked, so this is the only one)
        self._paint_cursor(p)
        p.end()

    def _paint_title(self, p, cx, cy, node) -> None:
        box, f, text = self._title_box_and_font(cx, cy, node)
        if box.isEmpty():
            return
        p.setFont(f)
        if self._is_light():
            p.setPen(QPen(QColor(200, 200, 200), max(1.0, 1.0 * self.s)))
            p.setBrush(QColor(40, 48, 60))
            p.drawRoundedRect(box, 5 * self.s, 5 * self.s)
            p.setPen(QPen(QColor(245, 247, 250)))
        else:
            p.setPen(QPen(QColor(0, 0, 0), max(1.0, 1.0 * self.s)))
            p.setBrush(QColor(245, 247, 250))
            p.drawRoundedRect(box, 5 * self.s, 5 * self.s)
            p.setPen(QPen(QColor(16, 18, 24)))
        p.drawText(box, Qt.AlignmentFlag.AlignCenter, text)

    def _current_title_rect(self) -> QRect:
        if self.hover_node is None or not self.hover_node.label:
            return QRect()
        cx, cy = int(self._pointer.x()), int(self._pointer.y())
        box, _font, _text = self._title_box_and_font(cx, cy, self.hover_node)
        if box.isEmpty():
            return QRect()
        margin = int(math.ceil(8 * self.s))
        return box.toAlignedRect().adjusted(-margin, -margin, margin, margin)

    def _title_box_and_font(self, cx, cy, node) -> tuple[QRectF, QFont, str]:
        text = node.label
        f = QFont()
        f.setPointSizeF(4.5 * self.s)
        f.setBold(True)
        fm = QFontMetrics(f)
        pad = 6 * self.s
        rect = self.rect()
        
        # Limit text width to a reasonable max (e.g. 300 scaled px) to prevent giant boxes
        max_text_w = min(300.0 * self.s, max(40.0, max(80.0, rect.width() - 8.0) - 2 * pad))
        
        # Elide middle ensures the file extension is preserved
        text = fm.elidedText(text, Qt.TextElideMode.ElideMiddle, int(max_text_w))
        
        tw = fm.horizontalAdvance(text)
        th = fm.height()
        w = tw + 2 * pad
        h = th + pad * 0.7
        
        depth = self.seg3_depth if getattr(node, "ring3", False) else self.seg_depth
        angle = node.angle
        if node.kind in (IT_TRAY_MENU, IT_FAV_MENU):
            angle -= 4.0
        elif node.kind in (IT_APP, IT_FILE, IT_WINDOW_GROUP, IT_WINDOW) and getattr(node, "depth", 0) == 0:
            angle -= 3.0
            
        # Calculate the proper distance so the box edge doesn't overlap the segment
        rad = math.radians(angle)
        cos_a = math.cos(rad)
        sin_a = math.sin(rad)
        abs_cos = abs(cos_a)
        abs_sin = abs(sin_a)
        d_x = (w / 2) / abs_cos if abs_cos > 1e-5 else float('inf')
        d_y = (h / 2) / abs_sin if abs_sin > 1e-5 else float('inf')
        r_rect = min(d_x, d_y)
        
        # Add 8px gap between segment and text box
        r_out = node.radius + depth / 2 + 8 * self.s + r_rect
            
        cxr = cx + r_out * cos_a
        cyr = cy + r_out * sin_a
        rx = cxr - w / 2
        ry = cyr - h / 2
        
        # Keep inside overlay rect
        if rx < pad: rx = pad
        elif rx + w > rect.width() - pad: rx = rect.width() - pad - w
        if ry < pad: ry = pad
        elif ry + h > rect.height() - pad: ry = rect.height() - pad - h
            
        return QRectF(rx, ry, w, h), f, text

    def _paint_cursor(self, p) -> None:
        px, py = self._pointer.x(), self._pointer.y()
        
        old_aa = p.renderHints() & QPainter.RenderHint.Antialiasing
        p.setRenderHint(QPainter.RenderHint.Antialiasing, True)
        
        dark_col = QColor(12, 16, 22, 200)
        white_col = QColor(255, 255, 255)
        
        r = 5.2 * self.s
        p.setBrush(Qt.BrushStyle.NoBrush)
        # 1. Dark outline
        p.setPen(QPen(dark_col, 4.0 * self.s))
        p.drawEllipse(QPointF(px, py), r, r)
        # 2. White core
        p.setPen(QPen(white_col, 2.0 * self.s))
        p.drawEllipse(QPointF(px, py), r, r)
        
        p.setRenderHint(QPainter.RenderHint.Antialiasing, bool(old_aa))


    def _paint_hub_volume_area(self, p, cx, cy) -> None:
        p.setPen(Qt.PenStyle.NoPen)
        if self._is_light():
            col = QColor(0, 0, 0, 15)
            if getattr(self, "hover_volume_area", False):
                col = QColor(0, 0, 0, 30)
        else:
            col = QColor(255, 255, 255, 24)
            if getattr(self, "hover_volume_area", False):
                col = QColor(255, 255, 255, 45)
        p.setBrush(col)
        # Draw a horizontal chord at the bottom (-145 deg to -35 deg, making it ~7px higher)
        p.drawChord(QRectF(cx - self.r_hub, cy - self.r_hub, self.r_hub * 2, self.r_hub * 2), -145 * 16, 110 * 16)
        
        # Speaker symbol
        s = 8.0 * self.s
        bx = cx
        by = cy + self.r_hub * 0.75
        symbol_color = QColor(self._glyph_color())
        symbol_color.setAlpha(180)
        
        body = QPolygonF([
            QPointF(bx - 0.46 * s, by - 0.16 * s),
            QPointF(bx - 0.16 * s, by - 0.16 * s),
            QPointF(bx + 0.14 * s, by - 0.42 * s),
            QPointF(bx + 0.14 * s, by + 0.42 * s),
            QPointF(bx - 0.16 * s, by + 0.16 * s),
            QPointF(bx - 0.46 * s, by + 0.16 * s),
        ])
        p.setBrush(symbol_color)
        p.drawPolygon(body)
        
        p.setBrush(Qt.BrushStyle.NoBrush)
        p.setPen(QPen(symbol_color, max(0.8, 0.9 * self.s)))
        p.drawArc(QRectF(bx + 0.06 * s, by - 0.30 * s, 0.5 * s, 0.6 * s), -55 * 16, 110 * 16)

    def _paint_hub(self, p, cx, cy) -> None:
        # Plain disc; the trigger thirds/quarters are invisible hit zones.
        p.setPen(Qt.PenStyle.NoPen)
        p.setBrush(self._seg_base())
        p.drawEllipse(QPointF(cx, cy), self.r_hub, self.r_hub)

        if self.config.get("overlay_mode", "pie") == "switch" and self.config.get("adjust_volume_with_trigger_wheel", True):
            self._paint_hub_volume_area(p, cx, cy)

        if self.config.get("overlay_mode", "pie") != "switch":
            self._paint_hub_ticks(p, cx, cy)
            
        self._paint_info_card(p, cx, cy)
        # (Mail badge moved to a dedicated ring-2 segment -- see _build_windows)

    def _paint_hub_ticks(self, p, cx, cy) -> None:
        # 20px ticks at the trigger-zone boundaries, starting at the hub edge and
        # going inward, fading from opaque (edge) to transparent (inner).
        # Selective AA: these are short radial lines -- jaggies don't read, so
        # draw them aliased to save the AA edge work.
        p.setRenderHint(QPainter.RenderHint.Antialiasing, False)
        bounds = set()
        for (a0, a1) in self._sector_arc.values():
            bounds.add(round(_norm(a0), 1))
        tick = 20.0
        segs = 10
        for b in bounds:
            ca, sa = math.cos(math.radians(b)), math.sin(math.radians(b))
            for i in range(segs):
                f0, f1 = i / segs, (i + 1) / segs   # 0 = edge, 1 = inner
                ra = self.r_hub - f0 * tick
                rb = self.r_hub - f1 * tick
                col = QColor(self._glyph_color())
                col.setAlpha(int(170 * (1.0 - f0)))
                pen = QPen(col, 3.0)   # a touch wider than before (was 2.0)
                pen.setCapStyle(Qt.PenCapStyle.FlatCap)
                p.setPen(pen)
                p.drawLine(QPointF(cx + ra * ca, cy + ra * sa),
                           QPointF(cx + rb * ca, cy + rb * sa))
        p.setRenderHint(QPainter.RenderHint.Antialiasing,
                        getattr(self, "_aa", True))

    def _paint_info_card(self, p, cx, cy) -> None:
        import datetime
        cfg = self.config
        now = datetime.datetime.now()
        r = self.r_hub
        # category icon (top slot)
        self._paint_category_icon(p, cx, cy)
        # battery charge (below the monitor): yellow bolt + charge in the accent
        if cfg.get("hub_show_charge", True):
            try:
                from . import sysinfo
                bat = sysinfo.battery_status()
            except Exception:  # noqa: BLE001
                bat = None
            if bat is not None:
                fb = QFont(); fb.setPointSizeF(6.8 * self.s)
                p.setFont(fb)
                txt = f"{bat['percent']}%"
                rect = QRectF(cx - r, cy - r * 0.30, 2 * r, r * 0.26)
                tw = p.fontMetrics().horizontalAdvance(txt)
                cyr = rect.center().y()
                self._draw_lightning(p, cx - tw / 2 - 6 * self.s, cyr, 8 * self.s)
                p.setPen(QPen(self._cyan()))
                p.drawText(rect, Qt.AlignmentFlag.AlignCenter, txt)
        # time (middle, big)
        if cfg.get("hub_show_clock", True):
            ft = QFont(); ft.setPointSizeF(14 * self.s); ft.setBold(True)
            p.setFont(ft)
            p.setPen(QPen(self._glyph_color()))
            p.drawText(QRectF(cx - r, cy - r * 0.16, 2 * r, r * 0.52),
                       Qt.AlignmentFlag.AlignCenter, now.strftime("%H:%M"))
        # date (bottom)
        if cfg.get("hub_show_date", True):
            fd = QFont(); fd.setPointSizeF(7.5 * self.s)
            p.setFont(fd)
            p.setPen(QPen(self._glyph_color()))
            p.drawText(QRectF(cx - r, cy + r * 0.24, 2 * r, r * 0.34),
                       Qt.AlignmentFlag.AlignCenter, now.strftime("%a %d %b"))

    def _paint_category_icon(self, p, cx, cy) -> None:
        r = self.r_hub
        # size and position of the drawing area
        size = r * 0.35
        px = cx
        # The battery charge indicator text box starts at cy - r * 0.30.
        # The bottom-most point of our drawn icons is at py + size * 0.36.
        # We want: py + size * 0.36 = cy - r * 0.30 - 5 * self.s
        py = cy - r * 0.30 - size * 0.36 - 5 * self.s
        
        p.save()
        p.translate(px, py)
        
        if self.open_sector == SEC_WINDOWS:
            color = self._green()
        elif self.open_sector == SEC_APPS:
            color = self._cyan()
        elif self.open_sector == SEC_FILES:
            color = self._orange()
        else:
            color = self._cyan()
            
        # 1px thinner lines
        p.setPen(QPen(color, max(1.0, 1.2 * self.s - 1.0)))
        p.setBrush(Qt.BrushStyle.NoBrush)
        
        w = size * 0.6
        h = size * 0.6
        
        if self.open_sector == SEC_WINDOWS:
            # Make the window icon a little smaller
            w = w * 0.85
            h = h * 0.85
            
            bx = -w/2 + w*0.4
            by = -h/2 - h*0.3
            fx = -w/2 - w*0.1
            fy = -h/2 + h*0.1
            
            # Back window lines (avoids intersecting the front window)
            p.drawLine(QPointF(bx, by), QPointF(bx, fy)) # left
            p.drawLine(QPointF(bx, by), QPointF(bx + w, by)) # top
            p.drawLine(QPointF(bx + w, by), QPointF(bx + w, by + h)) # right
            p.drawLine(QPointF(bx + w, by + h), QPointF(fx + w, by + h)) # bottom
            
            # Fill back window titlebar
            p.fillRect(QRectF(bx, by, w, h*0.25), color)
            
            # Front window
            p.drawRect(QRectF(fx, fy, w, h))
            # Fill front window titlebar
            p.fillRect(QRectF(fx, fy, w, h*0.25), color)
            
        elif self.open_sector == SEC_APPS:
            # 3x3 grid
            sq = w * 0.22
            gap = w * 0.17
            startX = -(sq * 3 + gap * 2) / 2
            startY = -(sq * 3 + gap * 2) / 2
            for row in range(3):
                for col in range(3):
                    p.drawRect(QRectF(startX + col * (sq + gap), startY + row * (sq + gap), sq, sq))
                    
        elif self.open_sector == SEC_FILES:
            dw = size * 0.5
            dh = dw * 1.3
            x0 = -dw/2
            y0 = -dh/2
            fold = dw * 0.35
            
            # Outline
            p.drawPolyline(QPolygonF([
                QPointF(x0, y0),
                QPointF(x0 + dw - fold, y0),
                QPointF(x0 + dw, y0 + fold),
                QPointF(x0 + dw, y0 + dh),
                QPointF(x0, y0 + dh),
                QPointF(x0, y0)
            ]))
            # Fold inner lines
            p.drawLine(QPointF(x0 + dw - fold, y0), QPointF(x0 + dw - fold, y0 + fold))
            p.drawLine(QPointF(x0 + dw - fold, y0 + fold), QPointF(x0 + dw, y0 + fold))
            
        p.restore()

    def _draw_lightning(self, p, cx, cy, size) -> None:
        h = size
        pts = [(0.15, -0.5), (-0.25, 0.06), (0.02, 0.06),
               (-0.15, 0.5), (0.30, -0.10), (0.0, -0.10)]
        poly = QPolygonF([QPointF(cx + x * h, cy + y * h) for x, y in pts])
        p.setPen(Qt.PenStyle.NoPen)
        p.setBrush(QColor(255, 209, 48))
        p.drawPolygon(poly)



    def _arc_rect(self, cx, cy, r) -> QRectF:
        return QRectF(cx - r, cy - r, 2 * r, 2 * r)

    def _band_path(self, cx, cy, rin, rout, a0, a1) -> QPainterPath:
        key = ("band", round(cx, 2), round(cy, 2), round(rin, 2),
               round(rout, 2), round(a0, 2), round(a1, 2))
        path = self._path_cache.get(key)
        if path is None:
            span = a1 - a0
            path = QPainterPath()
            path.arcMoveTo(self._arc_rect(cx, cy, rin), -a0)
            path.arcTo(self._arc_rect(cx, cy, rin), -a0, -span)
            path.arcTo(self._arc_rect(cx, cy, rout), -a1, span)
            path.closeSubpath()
            self._path_cache[key] = path
        return path

    def _arc_path(self, cx, cy, r, a0, a1) -> QPainterPath:
        key = ("arc", round(cx, 2), round(cy, 2), round(r, 2),
               round(a0, 2), round(a1, 2))
        path = self._path_cache.get(key)
        if path is None:
            path = QPainterPath()
            path.arcMoveTo(self._arc_rect(cx, cy, r), -a1)
            path.arcTo(self._arc_rect(cx, cy, r), -a1, (a1 - a0))
            self._path_cache[key] = path
        return path



    def _paint_node(self, p, cx, cy, node: Node, ring3=False, wf=1.0) -> None:
        if getattr(node, "pending_close", False):
            return
        if node.half_deg <= 0.01:   # dropped (overflowed single-row) node
            return
            
        if node.kind == "media_btn":
            with open("/tmp/karyon_media.log", "a") as f:
                f.write(f"PAINTING media_btn: angle={node.angle}, radius={node.radius}, half_deg={node.half_deg}\n")
        depth = self.seg3_depth if (ring3 or node.kind == IT_CTRL_BTN) else self.seg_depth
        rc = node.radius
        rin = rc - depth / 2
        rout = rc + depth / 2
        hw = node.half_deg * max(0.04, wf)   # width-grow animation (morph)
        a0 = node.angle - hw
        a1 = node.angle + hw
        # dark segment background (interpolates lighter on hover)
        t = node.hover_t
        if node.kind == IT_MAIL and getattr(self, "_mail_blink_state", 0) in (1, 3, 5, 7):
            t = 1.0
        if node is getattr(self, "open_group", None) or node is getattr(self, "_ctrl_node", None):
            t = 1.0
            
        col = QColor(
            int(self._seg_base().red() + (self._seg_hover().red() - self._seg_base().red()) * t),
            int(self._seg_base().green() + (self._seg_hover().green() - self._seg_base().green()) * t),
            int(self._seg_base().blue() + (self._seg_hover().blue() - self._seg_base().blue()) * t),
        )
        
        # Open windows get a slight green tint to distinguish them from closed pinned items
        is_open_window = (node.kind == IT_WINDOW_GROUP and getattr(node, "data", None) is not None) or node.kind == IT_WINDOW
        if is_open_window:
            green_col = self._green()
            mix = 0.10
            col.setRed(int(col.red() * (1 - mix) + green_col.red() * mix))
            col.setGreen(int(col.green() * (1 - mix) + green_col.green() * mix))
            col.setBlue(int(col.blue() * (1 - mix) + green_col.blue() * mix))
        path = self._band_path(cx, cy, rin, rout, a0, a1)
        p.setBrush(col)
        p.setPen(Qt.PenStyle.NoPen)
        p.drawPath(path)
        self._paint_edge_glow(p, cx, cy, path, rout, node, a0, a1)

        self._paint_progress_bar(p, cx, cy, node, rout)

        # icon or glyph at node center -- sized to fit the segment.
        gx = cx + rc * math.cos(math.radians(node.angle))
        gy = cy + rc * math.sin(math.radians(node.angle))
        # Cap the icon to the ring-2 standard size, so ring-3/4 and overflow rows
        # (which sit at a larger radius / wider span) never draw bigger icons.
        arc_w = min(rc * math.radians(max(2.0, 2 * node.half_deg)),
                    self._seg_arc_std)
        avail = min(depth, arc_w)
        if node.glyph:
            fit = min(node.icon_scale, (avail * 0.66) / (36 * self.s))
            self._paint_glyph(p, gx, gy, node.glyph, max(0.25, fit))
        elif isinstance(node.icon, QIcon) and not node.icon.isNull():
            size = int(min(28 * self.s * node.icon_scale, avail * 0.7))
            size = max(12, size)
            key = (id(node.icon), node.icon.cacheKey(), size,
                   getattr(node, "icon_sig", ""))
            pm = self._icon_pixmap_cache.get(key)
            if pm is None:
                pm = node.icon.pixmap(size, size)
                self._icon_pixmap_cache[key] = pm
            p.drawPixmap(int(gx - size / 2), int(gy - size / 2), pm)
        else:
            self._paint_node_text(p, cx, cy, node, rin, rout)



        # Always-visible bars (same width): cyan drill on drillable nodes, red
        # close on closable single windows.  A MULTI-window group shows neither
        # on its main symbol -- it opens on hover and is closed via its badge.
        if self._is_drillable(node) and node.kind != IT_WINDOW_GROUP:
            self._paint_bar(p, cx, cy, node, rout, self._cyan())
        # Count / mail / speaker badges.  Drawn last here AND re-drawn on top of
        # the title pill (see paintEvent) so the white title can never cover them.
        self._paint_badges(p, cx, cy, node)

    def _paint_badges(self, p, cx, cy, node: Node) -> None:
        # Multi-window group: round count badge (cyan/black) -> red with white X
        # when hovered, to close ALL windows.
        if node.kind == IT_WINDOW_GROUP and node.children:
            hot = node is self.hover_node and self.hover_close_all
            self._paint_count_badge(p, cx, cy, node, hot)

        # Speaker badge for any app with a live audio stream (incl. while muted,
        # so it can be un-muted); opposite side from the count badge.
        if (node.kind == IT_WINDOW_GROUP and self.audio is not None
                and self.audio.has_stream(getattr(node, "app_pid", 0),
                                          getattr(node, "app_rc", ""))):
            muted = self.audio.is_muted(getattr(node, "app_pid", 0),
                                        getattr(node, "app_rc", ""))
            hovering = node is self.hover_node and self.hover_mute
            # Background previews the RESULT of releasing here: red = will be (or
            # is) muted, cyan = will be (or is) playing.  Muted-at-rest is red;
            # hovering a muted badge turns it cyan (= will un-mute), and vice
            # versa.  The speaker symbol itself never changes.
            self._paint_speaker_badge(p, cx, cy, node, muted, hovering)

        # Pin badge (inner-left corner of the segment)
        is_pinnable = node.kind in (IT_WINDOW_GROUP, IT_APP, IT_FILE)
        if is_pinnable:
            is_pinned_view = (node.sector in (SEC_WINDOWS, SEC_FILES))
            if node.pinned and is_pinned_view:
                state = "unpin" if (node is self.hover_node and self.hover_pin) else "pinned"
                self._paint_pin_badge(p, cx, cy, node, state)
            elif node is self.hover_node:
                r, a = self._pointer_polar()
                node_rout = node.radius + self.seg_depth / 2
                in_node_segment = (
                    r > self.r_neutral
                    and (node.radius - self.seg_depth / 2 - self.s <= r <= node_rout + self.s)
                    and _ang_dist(a, node.angle) <= node.half_deg
                )
                if in_node_segment:
                    if node.pinned:
                        state = "unpin" if self.hover_pin else "hint"
                    else:
                        state = "armed" if self.hover_pin else "hint"
                    self._paint_pin_badge(p, cx, cy, node, state)

    def _pin_geom(self, node):
        br = 6.5 * self.s
        rin = node.radius - self.seg_depth / 2
        # Tucked closer to the inner edge on the left side
        r_b = rin + br - 1.5 * self.s
        a_b = node.angle - node.half_deg * 0.45
        return r_b, a_b, br

    def _over_pin(self, node, r: float, a: float) -> bool:
        r_b, a_b, br = self._pin_geom(node)
        px, py = r * math.cos(math.radians(a)), r * math.sin(math.radians(a))
        bx, by = r_b * math.cos(math.radians(a_b)), r_b * math.sin(math.radians(a_b))
        # Trigger area slightly smaller than the visual badge
        return math.hypot(px - bx, py - by) <= br - 1.5 * self.s

    def _paint_pin_badge(self, p, cx, cy, node, state: str) -> None:
        r_b, a_b, br = self._pin_geom(node)
        bx = cx + r_b * math.cos(math.radians(a_b))
        by = cy + r_b * math.sin(math.radians(a_b))
        if state == "hint":
            opacity = 64
        elif state == "pinned":
            opacity = 255 if node is self.hover_node else 64
        else:
            opacity = 255
        
        if state == "hint":
            symbol_color = QColor(self._cyan().red(), self._cyan().green(), self._cyan().blue(), opacity)
        elif state == "armed":
            symbol_color = QColor(self._green().red(), self._green().green(), self._green().blue(), opacity)
        elif state == "pinned":
            symbol_color = QColor(self._cyan().red(), self._cyan().green(), self._cyan().blue(), opacity)
        else: # "unpin"
            symbol_color = QColor(self._red().red(), self._red().green(), self._red().blue(), opacity)
            
        # Draw simple pin symbol inside the badge
        s = br
        p.setBrush(Qt.BrushStyle.NoBrush)
        p.setPen(QPen(symbol_color, max(0.8, 0.8 * self.s), Qt.PenStyle.SolidLine, Qt.PenCapStyle.RoundCap))
        
        if state in ("pinned", "armed"):
            # "stuck" pin: shorter needle, head moved down, with a fold (Falte) hinted at the end
            head_cy = by + 0.05 * s
            insertion_y = by + 0.65 * s
            # shaft
            p.drawLine(QPointF(bx, head_cy + 0.15 * s), QPointF(bx, insertion_y))
            # circular head
            p.setPen(Qt.PenStyle.NoPen)
            p.setBrush(symbol_color)
            p.drawEllipse(QPointF(bx, head_cy), 0.25 * s, 0.25 * s)
            # fold / wrinkle (Falte)
            p.setBrush(Qt.BrushStyle.NoBrush)
            p.setPen(QPen(symbol_color, 0.6 * self.s, Qt.PenStyle.SolidLine, Qt.PenCapStyle.RoundCap))
            p.drawLine(QPointF(bx - 0.25 * s, insertion_y), QPointF(bx + 0.25 * s, insertion_y))
        else:
            # normal pin
            # shaft / needle point
            p.drawLine(QPointF(bx, by - 0.1 * s), QPointF(bx, by + 0.75 * s))
            # circular head (just a filled circle at the top)
            p.setPen(Qt.PenStyle.NoPen)
            p.setBrush(symbol_color)
            p.drawEllipse(QPointF(bx, by - 0.25 * s), 0.25 * s, 0.25 * s)

    def _paint_edge_glow(self, p, cx, cy, path, r_edge, node, a0=None, a1=None) -> None:
        """Cheap flat accent line on the outer arc (outward-facing edge)."""
        if r_edge <= 1 or a0 is None or a1 is None:
            return
            
        is_sys = node.kind in (IT_SHOW_DESKTOP, IT_MENU, IT_FAV_MENU, IT_TRAY_MENU, IT_SESSION)
        is_closed_pinned = (node.kind == IT_WINDOW_GROUP and node.pinned and getattr(node, "data", None) is None)
        
        if is_sys or is_closed_pinned:
            acc = self._cyan()
        elif node.kind in (IT_WINDOW, IT_WINDOW_GROUP):
            acc = self._green()
        elif node.kind == IT_FILE:
            acc = self._orange()
        else:
            acc = self._cyan()
            
        acc.setAlpha(180)
        g_out = math.degrees(1.0 / r_edge) if r_edge > 0 else 0
        arc = self._arc_path(cx, cy, r_edge, a0 + g_out, a1 - g_out)
        p.setBrush(Qt.BrushStyle.NoBrush)
        p.setPen(QPen(acc, max(1.0, 0.9 * self.s)))
        p.drawPath(arc)
    def _badge_geom(self, node):
        br = 5.5 * self.s
        rout = node.radius + self.seg_depth / 2
        # Tucked just below the LEFT end of the close bar -- never touching it.
        r_b = rout - self.bar_w - br - 3.0 * self.s
        a_b = node.angle - node.half_deg * 0.6
        return r_b, a_b, br

    def _over_badge(self, node, r: float, a: float) -> bool:
        r_b, a_b, br = self._badge_geom(node)
        px, py = r * math.cos(math.radians(a)), r * math.sin(math.radians(a))
        bx, by = r_b * math.cos(math.radians(a_b)), r_b * math.sin(math.radians(a_b))
        return math.hypot(px - bx, py - by) <= br + 4 * self.s

    def _speaker_geom(self, node):
        br = 5.5 * self.s
        rin = node.radius - self.seg_depth / 2
        # Tucked closer to the inner edge on the right side
        r_b = rin + br - 1.5 * self.s
        a_b = node.angle + node.half_deg * 0.45
        return r_b, a_b, br

    def _over_speaker(self, node, r: float, a: float) -> bool:
        r_b, a_b, br = self._speaker_geom(node)
        px, py = r * math.cos(math.radians(a)), r * math.sin(math.radians(a))
        bx, by = r_b * math.cos(math.radians(a_b)), r_b * math.sin(math.radians(a_b))
        # Trigger area slightly smaller than the visual badge
        return math.hypot(px - bx, py - by) <= br - 1.5 * self.s

    def _paint_speaker_badge(self, p, cx, cy, node, muted: bool, hovering: bool) -> None:
        # Same size/border as the count badge, with a black speaker symbol.  The
        # ONLY state cue is the fill: red = muted (or will mute), cyan = playing
        # (or will un-mute).  The symbol is never struck through or altered.
        r_b, a_b, br = self._speaker_geom(node)
        bx = cx + r_b * math.cos(math.radians(a_b))
        by = cy + r_b * math.sin(math.radians(a_b))
        
        opacity = 255 if node is self.hover_node else 153
        
        if muted:
            bg = self._green() if hovering else self._red()
        else:
            bg = self._red() if hovering else self._cyan()
            
        symbol_color = QColor(bg.red(), bg.green(), bg.blue(), opacity)
        
        s = br
        body = QPolygonF([
            QPointF(bx - 0.46 * s, by - 0.16 * s),
            QPointF(bx - 0.16 * s, by - 0.16 * s),
            QPointF(bx + 0.14 * s, by - 0.42 * s),
            QPointF(bx + 0.14 * s, by + 0.42 * s),
            QPointF(bx - 0.16 * s, by + 0.16 * s),
            QPointF(bx - 0.46 * s, by + 0.16 * s),
        ])
        
        # Draw colored speaker symbol inside the badge
        p.setPen(Qt.PenStyle.NoPen)
        p.setBrush(symbol_color)
        p.drawPolygon(body)
        
        p.setBrush(Qt.BrushStyle.NoBrush)
        p.setPen(QPen(symbol_color, max(0.8, 0.9 * self.s)))
        p.drawArc(QRectF(bx + 0.06 * s, by - 0.30 * s, 0.5 * s, 0.6 * s),
                  -55 * 16, 110 * 16)
        is_muted_visual = (not hovering) if muted else hovering
        if is_muted_visual:
            # white strikethrough line
            p.setPen(QPen(QColor(255, 255, 255, opacity), max(1.0, 1.5 * self.s - 1.0), Qt.PenStyle.SolidLine, Qt.PenCapStyle.RoundCap))
            p.drawLine(QPointF(bx - 0.45 * s, by - 0.45 * s), QPointF(bx + 0.45 * s, by + 0.45 * s))

    def _paint_count_badge(self, p, cx, cy, node, hot: bool) -> None:
        r_b, a_b, br = self._badge_geom(node)
        bx = cx + r_b * math.cos(math.radians(a_b))
        by = cy + r_b * math.sin(math.radians(a_b))
        border = max(0.8, 1.5 * self.s - 1.0)
        
        opacity = 255
        bg = self._red() if hot else self._cyan()
        bg_color = QColor(bg.red(), bg.green(), bg.blue(), opacity)
        border_color = self._seg_base()
        border_color.setAlpha(opacity)
        
        p.setPen(QPen(border_color, border))
        p.setBrush(bg_color)
        p.drawEllipse(QPointF(bx, by), br, br)
        if hot:
            cross_color = QColor(255, 255, 255, opacity)
            p.setPen(QPen(cross_color, 1.5 * self.s))
            d = br * 0.36
            p.drawLine(QPointF(bx - d, by - d), QPointF(bx + d, by + d))
            p.drawLine(QPointF(bx - d, by + d), QPointF(bx + d, by - d))
        else:
            text_color = QColor(255, 255, 255, opacity) if self._is_light() else QColor(8, 10, 14, opacity)
            p.setPen(QPen(text_color))
            f = QFont(); f.setPointSizeF(6.5 * self.s); f.setBold(True)
            p.setFont(f)
            n = min(99, node.count or len(node.children) + 1)
            p.drawText(QRectF(bx - br, by - br, 2 * br, 2 * br),
                       Qt.AlignmentFlag.AlignCenter, str(n))

    def _paint_bar(self, p, cx, cy, node, r_edge, color, faint=False,
                   framed=False) -> None:
        if color in (RED, self._red()):
            # Deeper/darker red at rest; lighten the bar when the pointer is on
            # it (no accent outline).
            col = QColor(255, 110, 122) if framed else QColor(176, 36, 48)
        else:
            col = QColor(color)
            
        if color in (CYAN, self._cyan()):
            # 27% transparency = 73% opacity
            col.setAlphaF(0.73)
        else:
            base = 70 if faint else 240
            # follow the transparency setting
            base = int(base * (1.0 - self.config.get("transparency", 10) / 100.0))
            col.setAlpha(max(20, base))
        # Bar fills its zone (same width for drill and close), drawn just inside
        # the segment edge.
        w = self.bar_w
        a0 = node.angle - node.half_deg
        a1 = node.angle + node.half_deg
        rc = r_edge - w / 2
        pen = QPen(col, w)
        pen.setCapStyle(Qt.PenCapStyle.FlatCap)
        p.setPen(pen)
        p.setBrush(Qt.BrushStyle.NoBrush)
        # Selective AA: bars are thin flat-capped arcs.  The outer (rim) edge is
        # already smoothed by the antialiased segment behind it, so aliasing them
        # is normally invisible and saves edge work.  Exception: in regular mode
        # the CYAN drill bar's INNER edge sits inside the segment with nothing to
        # smooth it -- so antialias the cyan bar there (never the red close bar,
        # never in Performance Mode).
        smooth = (color in (CYAN, self._cyan())) and getattr(self, "_aa", True)
        p.setRenderHint(QPainter.RenderHint.Antialiasing, smooth)
        p.drawArc(self._arc_rect(cx, cy, rc), int(-a0 * 16), int(-(a1 - a0) * 16))
        p.setRenderHint(QPainter.RenderHint.Antialiasing,
                        getattr(self, "_aa", True))

    def _paint_progress_bar(self, p, cx, cy, node, r_edge) -> None:
        if getattr(node, "kind", None) == IT_WINDOW_GROUP and getattr(self, "progress", None) is not None:
            frac = self.progress.get(*getattr(node, "progress_keys", ()))
            if frac is not None:
                frac = max(0.0, min(1.0, float(frac)))
                if frac > 0.001:
                    w = self.bar_w
                    rc = r_edge - w / 2
                    
                    # Use un-morphed angles so text doesn't move during segment growth
                    a0 = node.angle - node.half_deg
                    a1 = node.angle + node.half_deg
                    span = (a1 - a0) * frac
                    
                    # 1. Track: Lighter background across the ENTIRE segment (a0 to a1)
                    if self._is_light():
                        track_col = QColor(0, 0, 0, 50)
                        prog_col = QColor(0, 0, 0, 160)
                        text_col = QColor(255, 255, 255)
                    else:
                        track_col = QColor(61, 220, 132, 100)
                        prog_col = QColor(61, 220, 132, 240)
                        base = int(240 * (1.0 - self.config.get("transparency", 10) / 100.0))
                        text_col = QColor(8, 10, 14, max(20, base))

                    track_pen = QPen(track_col, w)
                    track_pen.setCapStyle(Qt.PenCapStyle.FlatCap)
                    p.setPen(track_pen)
                    p.setBrush(Qt.BrushStyle.NoBrush)
                    p.setRenderHint(QPainter.RenderHint.Antialiasing, getattr(self, "_aa", True))
                    p.drawArc(self._arc_rect(cx, cy, rc), int(-a0 * 16), int(-(a1 - a0) * 16))
                    
                    # 2. Progress fill
                    prog_pen = QPen(prog_col, w)
                    prog_pen.setCapStyle(Qt.PenCapStyle.FlatCap)
                    p.setPen(prog_pen)
                    p.drawArc(self._arc_rect(cx, cy, rc), int(-a0 * 16), int(-span * 16))
                    
                    # 3. Percentage Text (tangential inside the bar)
                    p.setPen(QPen(text_col))
                    f = QFont()
                    f.setPointSizeF(4.5 * self.s)
                    f.setBold(True)
                    f.setLetterSpacing(QFont.SpacingType.PercentageSpacing, 115.0)
                    p.setFont(f)
                    pct_text = f"{int(round(frac * 100))} %"
                    
                    text_angle = a0 + 2.5
                    rad = math.radians(text_angle)
                    tx = cx + rc * math.cos(rad)
                    ty = cy + rc * math.sin(rad)
                    
                    rot_deg = text_angle + 90
                    rot_deg = (rot_deg + 180) % 360 - 180
                    if rot_deg > 90 or rot_deg < -90:
                        rot_deg += 180
                    rot_deg += 4
                    
                    p.save()
                    p.translate(tx, ty)
                    p.rotate(rot_deg)
                    
                    tw = 30 * self.s
                    th = 12 * self.s
                    # Perfectly centered on rc
                    p.drawText(QRectF(0, -th / 2, tw, th),
                               Qt.AlignmentFlag.AlignLeft | Qt.AlignmentFlag.AlignVCenter, pct_text)
                    p.restore()

    def _paint_node_text(self, p, cx, cy, node, rin, rout) -> None:
        # Single-line radial label: fills the segment depth, ellipsised with
        # "..." toward the hub, never drawn outside the segment, flipped on the
        # left half so it is never upside-down.
        angle = node.angle
        flip = angle > 90 or angle < -90
        rot = angle - 180 if flip else angle
        depth = rout - rin
        rc = (rin + rout) / 2
        m = 3 * self.s
        p.save()
        p.translate(cx, cy)
        p.rotate(rot)
        f = QFont()
        f.setPointSizeF(6.2 * self.s)
        p.setFont(f)
        fm = p.fontMetrics()
        th = fm.height()
        avail = max(8, int(depth - 2 * m))
        elided = fm.elidedText(node.label, Qt.TextElideMode.ElideRight, avail)
        cxr = (-rc) if flip else rc          # band centre along the (rotated) x
        rect = QRectF(cxr - (depth / 2 - m), -th / 2, depth - 2 * m, th)
        p.setPen(QPen(self._glyph_color()))
        p.setClipRect(QRectF(cxr - (depth / 2 - m) - 1, -th, depth - 2 * m + 2, 2 * th))
        p.drawText(rect, int(Qt.AlignmentFlag.AlignCenter), elided)
        p.restore()

    def _paint_hub_label(self, p, cx, cy, text) -> None:
        if not text:
            return
        p.setPen(QPen(self._glyph_color()))
        f = QFont()
        f.setPointSizeF(8 * self.s)
        f.setBold(True)
        p.setFont(f)
        p.drawText(QRectF(cx - self.r_hub, cy - 12, 2 * self.r_hub, 24),
                   int(Qt.AlignmentFlag.AlignCenter | Qt.TextFlag.TextWordWrap),
                   text[:16])

    def _paint_drill_bar(self, p, cx, cy, node: Node) -> None:
        # cyan bar on the outer edge of the (possibly overflowed) parent band
        depth = self.seg3_depth if node.kind == IT_CTRL_BTN else self.seg_depth
        r_edge = node.radius + depth / 2
        self._paint_bar(p, cx, cy, node, r_edge, self._cyan())

    # -- glyphs -------------------------------------------------------------
    def _paint_glyph(self, p, x, y, name: str, scale: float = 1.0) -> None:
        from . import glyphs
        glyphs.set_color(self._glyph_color().name())
        glyphs.set_accent(self._cyan().name())
        size = 36 * self.s * scale
        p.save()
        p.translate(x - size / 2, y - size / 2)
        p.scale(size / 64.0, size / 64.0)
        glyphs.draw(p, name)
        p.restore()
