"""Standalone settings window (forced dark palette, theme-independent)."""
from __future__ import annotations

import logging

from PyQt6.QtCore import Qt, pyqtSignal
from PyQt6.QtGui import QColor, QPalette
from PyQt6.QtWidgets import (QCheckBox, QComboBox, QFrame, QGridLayout,
                             QHBoxLayout, QLabel, QPushButton, QScrollArea,
                             QSlider, QSpinBox, QVBoxLayout, QWidget)

from .gestures import ACTIONS, DIRECTIONS
from .config import DEFAULTS

log = logging.getLogger(__name__)





PANEL_STYLE = """
QFrame#panel { background: rgb(20,24,32); }
QScrollArea, QWidget#panelInner { background: rgb(20,24,32); border: none; }
QLabel { color: #e8ecf4; }
QLabel#title { font-size: 18px; font-weight: bold; }
QLabel#section { font-size: 13px; font-weight: bold; color: #aeb6c6; }
QLabel#footer { color: #9aa3b2; font-size: 11px; }
QLabel#value { color: #b9c2d0; min-width: 60px; }
QCheckBox, QRadioButton { color: #e8ecf4; spacing: 8px; }
QCheckBox::indicator, QRadioButton::indicator {
    width: 16px; height: 16px; border-radius: 4px;
    border: 1px solid rgba(255,255,255,70); background: rgb(20,24,32);
}
QCheckBox::indicator:hover { border: 1px solid #37d0ff; }
QCheckBox::indicator:checked { background: #37d0ff; border: 1px solid #37d0ff; }
QCheckBox::indicator:disabled { border: 1px solid rgba(255,255,255,25); }
QCheckBox::indicator:checked:disabled {
    background: rgba(255,255,255,45); border: 1px solid rgba(255,255,255,45);
}
QCheckBox:disabled { color: rgb(107,114,128); }
QPushButton {
    background: rgba(255,255,255,18); color: #f0f4fa; border: none;
    padding: 7px 14px; border-radius: 6px;
}
QPushButton:hover { background: #37d0ff; color: rgb(12,16,22); }
QComboBox, QSpinBox {
    background: rgb(58,63,71); color: #f0f4fa;
    border: 1px solid rgba(255,255,255,55);
    padding: 4px 8px; border-radius: 5px;
}
QComboBox:hover, QSpinBox:hover { border: 1px solid rgba(255,255,255,95); }
QComboBox QAbstractItemView {
    background: rgb(58,63,71); color: #f0f4fa; border: 1px solid rgba(255,255,255,30);
    selection-background-color: rgb(104,111,122); selection-color: #f4f7fb;
    outline: none;
}
QComboBox QAbstractItemView::item { min-height: 22px; padding: 2px 6px; }
QComboBox QAbstractItemView::item:selected,
QComboBox QAbstractItemView::item:hover {
    background: rgb(104,111,122); color: #f4f7fb;
}
QComboBox::drop-down { border: none; width: 20px; }
QComboBox::down-arrow {
    width: 0; height: 0; margin-right: 7px;
    border-left: 5px solid transparent;
    border-right: 5px solid transparent;
    border-top: 6px solid #b9c2d0;
}
QSlider::groove:horizontal { height: 4px; background: rgba(255,255,255,25); }
QSlider::sub-page:horizontal { background: #37d0ff; }
QSlider::handle:horizontal {
    background: #37d0ff; width: 14px; margin: -6px 0; border-radius: 7px;
}
"""


def _dark_palette() -> QPalette:
    p = QPalette()
    C = QColor
    p.setColor(QPalette.ColorRole.Window, C(20, 24, 32))
    p.setColor(QPalette.ColorRole.WindowText, C("#e8ecf4"))
    p.setColor(QPalette.ColorRole.Text, C("#e8ecf4"))
    p.setColor(QPalette.ColorRole.ButtonText, C("#e8ecf4"))
    # Combos/spinboxes get their colour from the palette under Fusion (a
    # stylesheet background is ignored on some Qt builds), so the dropdown grey
    # and its lighter-grey hover live here.
    p.setColor(QPalette.ColorRole.Base, C(58, 63, 71))         # popup / field grey
    p.setColor(QPalette.ColorRole.AlternateBase, C(82, 88, 98))
    p.setColor(QPalette.ColorRole.Button, C(58, 63, 71))       # closed combo grey
    p.setColor(QPalette.ColorRole.ToolTipBase, C(28, 32, 42))
    p.setColor(QPalette.ColorRole.ToolTipText, C("#e8ecf4"))
    p.setColor(QPalette.ColorRole.PlaceholderText, C("#9aa3b2"))
    p.setColor(QPalette.ColorRole.Highlight, C(104, 111, 122))  # hover = lighter grey
    p.setColor(QPalette.ColorRole.HighlightedText, C("#f4f7fb"))
    for role in (QPalette.ColorRole.WindowText, QPalette.ColorRole.Text,
                 QPalette.ColorRole.ButtonText):
        p.setColor(QPalette.ColorGroup.Disabled, role, C(107, 114, 128))
    return p


class _NoWheelCombo(QComboBox):
    def wheelEvent(self, event):  # noqa: N802
        event.ignore()


class _NoWheelSlider(QSlider):
    def wheelEvent(self, event):  # noqa: N802
        event.ignore()


class SettingsPanel(QFrame):
    closed = pyqtSignal()

    def __init__(self, config) -> None:
        super().__init__(None)
        self.config = config
        self.proxy = None
        self._captured_key = config.get("trigger_key", 0)
        # Staged per-direction custom gesture keys (committed on Save, so Cancel
        # reverts).  _capture_target routes the next captured key: None = trigger,
        # else the gesture direction whose "Custom Key" item is capturing.
        self._gesture_keys = dict(config.get("gesture_custom_keys", {}) or {})
        self._capture_target = None
        self.setObjectName("panel")
        self.setWindowFlags(Qt.WindowType.Tool)
        self.setWindowTitle("Karyon Settings")
        self.setFixedSize(680, 720)
        self.setPalette(_dark_palette())
        self._apply_style()
        self._build()

    def _apply_style(self) -> None:
        self.setStyleSheet(PANEL_STYLE)

    # -- build --------------------------------------------------------------
    def _build(self) -> None:
        outer = QVBoxLayout(self)
        outer.setContentsMargins(0, 0, 0, 0)
        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        scroll.setFrameShape(QFrame.Shape.NoFrame)
        # Force a dark viewport/content background so a LIGHT system theme can't
        # leave the settings window white (the QScrollArea viewport + content
        # widget otherwise fall back to the system Base colour).
        scroll.viewport().setStyleSheet("background: rgb(20,24,32);")
        inner = QWidget()
        inner.setObjectName("panelInner")
        self._lay = QVBoxLayout(inner)
        self._lay.setContentsMargins(20, 20, 20, 20)
        self._lay.setSpacing(10)
        scroll.setWidget(inner)
        outer.addWidget(scroll)

        title = QLabel("Karyon 2.1")
        title.setObjectName("title")
        self._lay.addWidget(title)

        info_text = QLabel("Hold Right mouse to show overlay. Let right mouse go to select. Left mouse click to cancel. Middle mouse click to close window. Mouse wheel up on window segments to maximize/restore. Mouse wheel down to minimize.")
        info_text.setWordWrap(True)
        info_text.setStyleSheet("color: #888; font-style: italic; margin-bottom: 10px;")
        self._lay.addWidget(info_text)

        self.sliders = {}
        self.combos = {}
        self.spins = {}
        self.checks = {}

        # One aligned grid for every title/control/value row so titles, dropdowns
        # and sliders all line up and end flush (col 1 limited width).
        self._grid = QGridLayout()
        self._grid.setHorizontalSpacing(12)
        self._grid.setVerticalSpacing(10)
        self._grid.setColumnMinimumWidth(0, 190)
        self._grid.setColumnMinimumWidth(1, 300)
        self._grid.setColumnMinimumWidth(2, 54)
        self._grid.setColumnStretch(3, 1)    # keeps controls at a limited width
        self._lay.addLayout(self._grid)
        self._grow = 0



        self._add_combo("overlay_mode", "Overlay Mode", [("Pie Mode", "pie"), ("Switch Mode (Mouse wheel)", "switch")])
        self.combos["overlay_mode"].currentIndexChanged.connect(self._update_volume_label)
        self.combos["overlay_mode"].currentIndexChanged.connect(self._deps)
        self._add_combo("theme", "Theme", [("Dark", "dark"), ("Light", "light")])

        # Directly under Theme, spanning the grid so it isn't pushed to the bottom.
        self._grid.addWidget(self._make_check("game_mode", "Game Mode (auto-disable overlay in games)"),
                             self._grow, 0, 1, 4)
        self._grow += 1
        self._grid.addWidget(self._make_check(
            "adjust_volume_with_trigger_wheel",
            "Volume Control"),
            self._grow, 0, 1, 4)
        self._grow += 1

        self.volume_info_label = QLabel()
        self.volume_info_label.setStyleSheet("color: #888; font-size: 11px;")
        self.volume_info_label.setContentsMargins(24, 0, 0, 10)
        self._grid.addWidget(self.volume_info_label, self._grow, 0, 1, 4)
        self._update_volume_label()
        self._grow += 1

        # All sliders except the gesture window.  Units live on the value, not
        # the title.
        self._add_slider("hold_ms", "Trigger Hold", 50, 800, 1, unit=" ms")
        self._add_slider("mouse_speed", "Overlay mouse speed", -100, 100, 0.01)
        self._add_slider("scale", "Overlay scale", 10, 20, 0.1)   # menu scale 1.0 - 2.0
        self._add_slider("transparency", "Overlay transparency", 0, 50, 1, unit=" %")
        self._add_slider("max_recent_apps", "Max recent apps", 1, 30, 1)
        self._add_slider("max_recent_files", "Max recent files", 1, 30, 1)
        self._add_slider("volume_steps", "Volume steps", 1, 10, 1, unit=" %")

        # Checkboxes, two columns.  Left: the Show toggles + Dim area + Focus.
        # Right: the drawn-icon toggles.  (Ring transformation is always animated.)
        grid = QGridLayout()
        self._lay.addLayout(grid)
        left = [
            ("show_windows", "Show Windows"),
            ("show_apps", "Show Apps"),
            ("show_recent_files", "Show Recent Files"),
            ("mail_notification", "Icon Letter Notification"),
            ("show_tray", "Icon Tray"),
        ]
        right = [
            ("show_desktop", "Icon Desktop"),
            ("show_session", "Icon Session"),
            ("show_all_apps", "Icon All Applications"),
            ("show_favorites", "Icon Favorites"),
        ]
        for r, (k, lab) in enumerate(left):
            grid.addWidget(self._make_check(k, lab), r, 0)
        for r, (k, lab) in enumerate(right):
            grid.addWidget(self._make_check(k, lab), r, 1)
        # Dependencies: >=2 main categories; Apps needs >=1 sub-option; the apps
        # sub-options grey out while Apps is off; recents caps track residents.
        for k in ("show_windows", "show_apps", "show_recent_files",
                  "show_favorites", "show_all_apps", "show_session"):
            self.checks[k].toggled.connect(self._deps)
        self._deps()

        # Hub displays: a divider, then the four info-card toggles (no title).
        # All independent -- any combination, or none.
        self._lay.addWidget(self._separator())
        hgrid = QGridLayout()
        self._lay.addLayout(hgrid)
        for i, (k, lab) in enumerate([
                ("hub_show_clock", "Clock"),
                ("hub_show_date", "Date"),
                ("hub_show_charge", "Charge")]):
            hgrid.addWidget(self._make_check(k, lab), i // 2, i % 2)

        # Gestures: a divider, then Enable, the activation-window slider, then the
        # direction combos (no section title).
        self._lay.addWidget(self._separator())
        self._lay.addWidget(self._make_check("gestures_enabled", "Enable gestures"))
        # The activation-window slider sits directly under "Enable gestures" in its
        # own grid (the main grid is far above), aligned to the same columns.
        gw_grid = QGridLayout()
        gw_grid.setHorizontalSpacing(12)
        gw_grid.setColumnMinimumWidth(0, 190)
        gw_grid.setColumnMinimumWidth(1, 300)
        gw_grid.setColumnMinimumWidth(2, 54)
        gw_grid.setColumnStretch(3, 1)
        self._lay.addLayout(gw_grid)
        self._add_slider("gesture_time_window", "Gesture activation window",
                         100, 1000, 1, unit=" ms", grid=gw_grid)
        gg = QGridLayout()
        gg.setColumnStretch(0, 1)
        gg.setColumnStretch(1, 1)
        self._lay.addLayout(gg)
        self.gesture_combos = {}
        gpairs = [("left", "Left", "right", "Right"),
                  ("up", "Up", "down", "Down"),
                  ("left_up", "Left-Up", "right_up", "Right-Up"),
                  ("left_down", "Left-Down", "right_down", "Right-Down")]
        for r, (da, la, db, lb) in enumerate(gpairs):
            gg.addWidget(self._make_gesture_combo(da, la), r, 0)
            gg.addWidget(self._make_gesture_combo(db, lb), r, 1)

        footer = QLabel(
            "Vibe-coded by Eisen 2026 | https://eisenvibe.vercel.app | "
            "rostrausch@gmail.com")
        footer.setObjectName("footer")
        footer.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self._lay.addWidget(footer)

        btns = QHBoxLayout()
        btns.setContentsMargins(12, 8, 12, 14)   # a little room below the buttons
        quit_btn = QPushButton("Quit launcher")
        quit_btn.clicked.connect(self._quit_launcher)
        save_btn = QPushButton("Save")
        save_btn.clicked.connect(self._save)
        cancel_btn = QPushButton("Cancel")
        cancel_btn.clicked.connect(self._cancel)
        btns.addWidget(quit_btn)
        btns.addStretch(1)
        btns.addWidget(save_btn)
        btns.addWidget(cancel_btn)
        outer.addLayout(btns)

    def _set_silent(self, cb, val) -> None:
        cb.blockSignals(True)
        cb.setChecked(val)
        cb.blockSignals(False)

    def _deps(self, *_) -> None:
        wins = self.checks.get("show_windows")
        apps = self.checks["show_apps"]
        rfiles = self.checks["show_recent_files"]
        subkeys = ("show_favorites", "show_all_apps")
        subs = [self.checks[k] for k in subkeys]
        sender = self.sender()
        any_sub = any(s.isChecked() for s in subs)
        # Apps needs at least one of Favorites / All Applications.
        if apps.isChecked() and not any_sub:
            if sender is apps:
                self._set_silent(subs[0], True)
            else:
                self._set_silent(apps, False)
                
        # Minimum main categories logic
        mode = self.combos.get("overlay_mode")
        is_pie = mode is None or mode.currentData() == "pie"
        
        num_checked = sum(1 for cb in (wins, apps, rfiles) if cb and cb.isChecked())
        min_required = 2 if is_pie else 1
        
        if num_checked < min_required:
            if sender is rfiles:
                if not apps.isChecked(): self._set_silent(apps, True)
                elif wins and not wins.isChecked(): self._set_silent(wins, True)
            elif sender is apps:
                if wins and not wins.isChecked(): self._set_silent(wins, True)
                elif not rfiles.isChecked(): self._set_silent(rfiles, True)
            else:
                if not apps.isChecked(): self._set_silent(apps, True)
                elif not rfiles.isChecked(): self._set_silent(rfiles, True)
                
        # Re-check subkeys if Apps was forced on
        if apps.isChecked() and not any(s.isChecked() for s in subs):
            self._set_silent(subs[0], True)

        # Grey the apps sub-options while Apps is off
        on = apps.isChecked()
        for s in subs:
            s.setEnabled(on)
            
        # Grey the Windows dependents while Windows is off
        if wins:
            wins_on = wins.isChecked()
            for k in ("show_desktop", "mail_notification"):
                cb = self.checks.get(k)
                if cb:
                    cb.setEnabled(wins_on)



    def _update_volume_label(self) -> None:
        mode = self.combos.get("overlay_mode")
        if not mode or not hasattr(self, "volume_info_label"): return
        if mode.currentData() == "switch":
            self.volume_info_label.setText("Volume: Mouse Back/Forward | Mute: Middle click")
        else:
            self.volume_info_label.setText("Volume: Mouse wheel up/down | Mute: Middle click")





    def _key_text(self) -> str:
        return self._key_name(getattr(self, "_captured_key", 0))

    @staticmethod
    def _key_name(code) -> str:
        code = int(code or 0)
        if not code:
            return "(none)"
        try:
            from evdev import ecodes
            names = ecodes.KEY.get(code) or ecodes.BTN.get(code)
            if isinstance(names, (list, tuple)):
                names = names[0]
            s = str(names or code)
            for pre in ("KEY_", "BTN_"):
                if s.startswith(pre):
                    s = s[len(pre):]
            return s.capitalize()
        except Exception:  # noqa: BLE001
            return str(code)

    def _section(self, name: str) -> None:
        lbl = QLabel(name)
        lbl.setObjectName("section")
        self._lay.addWidget(lbl)

    @staticmethod
    def _fmt(value, step, unit) -> str:
        # %g drops trailing zeros and float noise (-0.700000001 -> -0.7).
        text = str(int(round(value))) if step >= 1 else ("%g" % round(value, 4))
        return f"{text}{unit}"

    def _add_slider(self, key, label, lo, hi, step, unit="", grid=None) -> None:
        # grid=None -> the main aligned grid (uses the shared row counter); pass a
        # grid to place the slider elsewhere (e.g. inside the gestures section).
        g = grid if grid is not None else self._grid
        r = self._grow if grid is None else 0
        g.addWidget(QLabel(label), r, 0)
        s = _NoWheelSlider(Qt.Orientation.Horizontal)
        s.setMinimum(lo)
        s.setMaximum(hi)
        s.setValue(int(round(self.config.get(key, DEFAULTS[key]) / step)))
        val = QLabel(self._fmt(self.config.get(key, DEFAULTS[key]), step, unit))
        val.setObjectName("value")
        s.valueChanged.connect(lambda v, st=step, u=unit, vl=val:
                               vl.setText(self._fmt(round(v * st, 4), st, u)))
        g.addWidget(s, r, 1)
        g.addWidget(val, r, 2)
        if grid is None:
            self._grow += 1
        self.sliders[key] = (s, step)

    def _add_combo(self, key, label, options, by_value=False) -> None:
        r = self._grow
        self._grid.addWidget(QLabel(label), r, 0)
        c = _NoWheelCombo()
        for name, value in options:
            c.addItem(name, value)
        cur = self.config.get(key)
        for i in range(c.count()):
            if c.itemData(i) == cur:
                c.setCurrentIndex(i)
                break
        self._style_combo(c)
        self._grid.addWidget(c, r, 1)
        self._grow += 1
        self.combos[key] = c

    def _add_spin(self, key, label, lo, hi) -> None:
        row = QHBoxLayout()
        row.addWidget(QLabel(label))
        sp = QSpinBox()
        sp.setRange(lo, hi)
        sp.setValue(int(self.config.get(key, DEFAULTS[key])))
        row.addWidget(sp, 1)
        self._lay.addLayout(row)
        self.spins[key] = sp

    def _style_combo(self, c) -> None:
        """Force dropdown colours per-widget (strongest specificity) so neither the
        style nor KDE's platform-theme palette can override them to black."""
        sheet = (
            "QComboBox { background-color: rgb(58,63,71); color: #f0f4fa;"
            " border: 1px solid rgba(255,255,255,55); border-radius: 5px;"
            " padding: 4px 8px; }"
            "QComboBox:hover { border: 1px solid rgba(255,255,255,95); }"
            "QComboBox::drop-down { border: none; width: 20px; }"
            "QComboBox QAbstractItemView { background-color: rgb(58,63,71);"
            " color: #f0f4fa; outline: none;"
            " selection-background-color: rgb(104,111,122); selection-color: #f4f7fb; }"
            "QComboBox QAbstractItemView::item { min-height: 22px; padding: 2px 6px; }"
            "QComboBox QAbstractItemView::item:hover,"
            " QComboBox QAbstractItemView::item:selected {"
            " background-color: rgb(104,111,122); color: #f4f7fb; }")
        c.setStyleSheet(sheet)
        # The popup view is a separate top-level; style it directly too.
        c.view().setStyleSheet(
            "QAbstractItemView { background-color: rgb(58,63,71); color: #f0f4fa; }"
            "QAbstractItemView::item:hover, QAbstractItemView::item:selected {"
            " background-color: rgb(104,111,122); color: #f4f7fb; }")

    def _separator(self) -> QFrame:
        """A 1px divider with an EXPLICIT colour -- a QFrame HLine takes its colour
        from the palette, so it vanishes on a dark theme."""
        line = QFrame()
        line.setFixedHeight(1)
        line.setStyleSheet("background-color: rgba(255,255,255,42); border: none;")
        return line

    # -- Touchpad Mode (preset + restore) -----------------------------------
    def _collect_values(self) -> dict:
        """Snapshot every editable widget value as a config dict."""
        d = {}
        for k, c in self.combos.items():
            d[k] = c.currentData()
        for k, (s, step) in self.sliders.items():
            d[k] = round(s.value() * step, 4)
        for k, sp in self.spins.items():
            d[k] = sp.value()
        for k, c in self.checks.items():
            d[k] = c.isChecked()
        return d

    def _apply_values(self, d: dict) -> None:
        """Push values from a config dict back into the widgets (editable)."""
        for k, v in d.items():
            if k in self.combos:
                c = self.combos[k]
                c.blockSignals(True)
                for i in range(c.count()):
                    if c.itemData(i) == v:
                        c.setCurrentIndex(i)
                        break
                c.blockSignals(False)
            elif k in self.sliders:
                s, step = self.sliders[k]
                s.setValue(int(round(float(v) / step)))
            elif k in self.spins:
                self.spins[k].setValue(int(v))
            elif k in self.checks:
                self.checks[k].blockSignals(True)
                self.checks[k].setChecked(bool(v))
                self.checks[k].blockSignals(False)
        self._deps()

    def _make_check(self, key, label) -> QCheckBox:
        cb = QCheckBox(label)
        cb.setChecked(bool(self.config.get(key, DEFAULTS.get(key, False))))
        self.checks[key] = cb
        return cb

    def _make_gesture_combo(self, direction, label) -> QWidget:
        w = QWidget()
        row = QHBoxLayout(w)
        row.setContentsMargins(0, 0, 0, 0)
        lbl = QLabel(label)
        lbl.setFixedWidth(78)          # fixed -> all gesture dropdowns line up
        row.addWidget(lbl)
        c = _NoWheelCombo()
        for key, text in ACTIONS:
            c.addItem(text, key)
        cur = self.config.get(f"gesture_{direction}", "none")
        for i in range(c.count()):
            if c.itemData(i) == cur:
                c.setCurrentIndex(i)
                break
        self._style_combo(c)
        row.addWidget(c, 1)
        self.gesture_combos[direction] = c
        self._refresh_gesture_custom(direction)
        c.currentIndexChanged.connect(
            lambda _i, d=direction: self._on_gesture_changed(d))
        return w

    def _gesture_custom_index(self, combo) -> int:
        for i in range(combo.count()):
            if combo.itemData(i) == "custom_key":
                return i
        return -1

    def _refresh_gesture_custom(self, direction) -> None:
        """Show the captured key inside the 'Custom Key' item for this direction
        (e.g. 'Custom key: F8'), or 'Custom Key…' when none is set."""
        c = self.gesture_combos.get(direction)
        if c is None:
            return
        i = self._gesture_custom_index(c)
        if i < 0:
            return
        code = self._gesture_keys.get(direction)
        c.setItemText(i, f"Custom key: {self._key_name(code)}" if code
                      else "Custom Key…")

    def _on_gesture_changed(self, direction) -> None:
        # Selecting the 'Custom Key' item starts a key capture for THIS gesture.
        c = self.gesture_combos.get(direction)
        if c is None or c.currentData() != "custom_key":
            return
        c.setItemText(self._gesture_custom_index(c), "Custom key: press a key…")
        self._capture_target = direction
        if self.proxy is not None:
            self.proxy.begin_key_capture()

    # -- external hooks -----------------------------------------------------
    def bind_proxy(self, proxy) -> None:
        self.proxy = proxy

    def set_captured_key(self, code: int) -> None:
        tgt = self._capture_target
        self._capture_target = None
        if tgt is not None:
            self._gesture_keys[tgt] = int(code)
            self._refresh_gesture_custom(tgt)
            return

    # -- save/cancel/load ---------------------------------------------------
    def _load(self) -> None:
        for key, (s, step) in self.sliders.items():
            s.setValue(int(round(self.config.get(key, DEFAULTS[key]) / step)))
            # The value label is updated by the valueChanged signal.
        for key, c in self.combos.items():
            cur = self.config.get(key)
            for i in range(c.count()):
                if c.itemData(i) == cur:
                    c.setCurrentIndex(i)
                    break
        for key, sp in self.spins.items():
            sp.setValue(int(self.config.get(key, DEFAULTS[key])))
        for key, cb in self.checks.items():
            cb.setChecked(bool(self.config.get(key, DEFAULTS.get(key, False))))
        for direction, c in self.gesture_combos.items():
            cur = self.config.get(f"gesture_{direction}", "none")
            for i in range(c.count()):
                if c.itemData(i) == cur:
                    c.setCurrentIndex(i)
                    break
        self._gesture_keys = dict(self.config.get("gesture_custom_keys", {}))
        for direction in self.gesture_combos:
            self._refresh_gesture_custom(direction)

    def showEvent(self, event):  # noqa: N802
        super().showEvent(event)
        self._load()

    def _save(self) -> None:
        for key, (s, step) in self.sliders.items():
            val = round(s.value() * step, 4)   # kill float noise (-0.70000001)
            self.config[key] = type(DEFAULTS[key])(val) if not isinstance(
                DEFAULTS[key], float) else float(val)
        for key, c in self.combos.items():
            self.config[key] = c.currentData()
        for key, sp in self.spins.items():
            self.config[key] = sp.value()
        for key, cb in self.checks.items():
            self.config[key] = cb.isChecked()
        for direction, c in self.gesture_combos.items():
            self.config[f"gesture_{direction}"] = c.currentData()
        self.config["gesture_custom_keys"] = dict(self._gesture_keys)
        # enforce minimum selected categories
        is_pie = self.config.get("overlay_mode", "pie") == "pie"
        min_req = 2 if is_pie else 1
        checked = sum(1 for k in ("show_windows", "show_apps", "show_recent_files") if self.config.get(k, True))
        if checked < min_req:
            if not self.config.get("show_apps", True):
                self.config["show_apps"] = True
                checked += 1
            if checked < min_req and not self.config.get("show_recent_files", True):
                self.config["show_recent_files"] = True
        self.config.save()
        self._apply_style()
        self.hide()
        self.closed.emit()

    def _cancel(self) -> None:
        self.hide()
        self.closed.emit()

    def _quit_launcher(self) -> None:
        from PyQt6.QtWidgets import QApplication
        QApplication.instance().quit()

    def closeEvent(self, event):  # noqa: N802
        event.ignore()
        self.hide()
        self.closed.emit()
