"""All launcher-owned glyphs, drawn at 64x64 in light GLYPH color.

Theme-independent: never use theme icons for our own symbols (dark on light
themes).  Pen uses RoundCap/RoundJoin.
"""
from __future__ import annotations

import math

from PyQt6.QtCore import Qt, QRectF, QPointF
from PyQt6.QtGui import (QColor, QPainterPath, QPen, QPolygonF, QBrush,
                         QRadialGradient)

GLYPH = QColor(232, 238, 246)
_ACCENT = QColor(55, 208, 255)


def set_accent(color) -> None:
    """Accent colour used for the soft glow behind every self-drawn glyph."""
    global _ACCENT
    _ACCENT = QColor(color)


def _pen(width: float) -> QPen:
    pen = QPen(GLYPH, width)
    pen.setCapStyle(Qt.PenCapStyle.RoundCap)
    pen.setJoinStyle(Qt.PenJoinStyle.RoundJoin)
    return pen


def _draw_glow(p) -> None:
    g = QRadialGradient(32, 32, 30)
    c0 = QColor(_ACCENT); c0.setAlpha(80)
    c1 = QColor(_ACCENT); c1.setAlpha(0)
    g.setColorAt(0.0, c0)
    g.setColorAt(1.0, c1)
    p.setPen(Qt.PenStyle.NoPen)
    p.setBrush(g)
    p.drawEllipse(QPointF(32, 32), 30, 30)


def draw(p, name: str) -> None:
    # Soft accent glow behind every self-drawn glyph.
    _draw_glow(p)
    # Reset the brush so stroke-only glyphs don't inherit the (dark) segment
    # fill from the caller; filled glyphs set their own brush explicitly.
    p.setBrush(Qt.BrushStyle.NoBrush)
    fn = _GLYPHS.get(name)
    if fn is None:
        # tray plasmoid fallback handled by caller; draw a dot
        p.setBrush(QBrush(GLYPH))
        p.setPen(Qt.PenStyle.NoPen)
        p.drawEllipse(QPointF(32, 32), 6, 6)
        return
    fn(p)


# -- navigation / special ---------------------------------------------------
def _hamburger(p) -> None:
    p.setPen(_pen(6.0))
    for y in (20, 32, 44):
        p.drawLine(15, y, 49, y)


def _star(p) -> None:
    p.setPen(Qt.PenStyle.NoPen)
    p.setBrush(QBrush(GLYPH))
    cx, cy, ro, ri = 32, 33, 23, 9.4
    poly = QPolygonF()
    for i in range(10):
        ang = math.radians(-90 + i * 36)
        r = ro if i % 2 == 0 else ri
        poly.append(QPointF(cx + r * math.cos(ang), cy + r * math.sin(ang)))
    p.drawPolygon(poly)


def _gear(p) -> None:
    p.setPen(Qt.PenStyle.NoPen)
    p.setBrush(QBrush(GLYPH))
    cx, cy = 32, 32
    r_outer, r_body, r_hole = 23, 17, 7.5
    path = QPainterPath()
    teeth = 8
    for i in range(teeth * 2):
        ang = math.radians(i * (360 / (teeth * 2)))
        r = r_outer if i % 2 == 0 else r_body
        pt = QPointF(cx + r * math.cos(ang), cy + r * math.sin(ang))
        if i == 0:
            path.moveTo(pt)
        else:
            path.lineTo(pt)
    path.closeSubpath()
    hole = QPainterPath()
    hole.addEllipse(QPointF(cx, cy), r_hole, r_hole)
    p.drawPath(path.subtracted(hole))


def _tray(p) -> None:
    p.setPen(_pen(5.0))
    path = QPainterPath()
    path.moveTo(18, 25)
    path.lineTo(32, 41)
    path.lineTo(46, 25)
    p.drawPath(path)


def _wrench(p) -> None:
    p.setPen(_pen(5.0))
    p.drawLine(20, 44, 40, 24)
    path = QPainterPath()
    path.moveTo(44, 14)
    path.arcTo(QRectF(34, 14, 20, 20), 90, 240)
    p.drawPath(path)


def _show_desktop(p) -> None:
    p.setPen(_pen(4.0))
    p.drawRect(QRectF(16, 16, 32, 24))
    p.drawLine(16, 46, 48, 46)


# -- sector glyphs (ring 1) -------------------------------------------------
def _sec_windows(p) -> None:
    p.setPen(_pen(4.0))
    p.setBrush(Qt.BrushStyle.NoBrush)
    p.drawRoundedRect(QRectF(14, 18, 24, 20), 3, 3)
    p.drawRoundedRect(QRectF(26, 28, 24, 20), 3, 3)


def _sec_apps(p) -> None:
    p.setPen(Qt.PenStyle.NoPen)
    p.setBrush(QBrush(GLYPH))
    for r in range(3):
        for c in range(3):
            p.drawRoundedRect(QRectF(16 + c * 13, 16 + r * 13, 8, 8), 2, 2)


def _sec_files(p) -> None:
    p.setPen(_pen(4.0))
    p.setBrush(Qt.BrushStyle.NoBrush)
    path = QPainterPath()
    path.moveTo(20, 14)
    path.lineTo(38, 14)
    path.lineTo(46, 22)
    path.lineTo(46, 50)
    path.lineTo(20, 50)
    path.closeSubpath()
    p.drawPath(path)
    p.drawLine(38, 14, 38, 22)
    p.drawLine(38, 22, 46, 22)


# -- session ----------------------------------------------------------------
def _session(p) -> None:
    p.setPen(_pen(4.0))
    p.setBrush(QBrush(GLYPH))
    p.drawEllipse(QPointF(32, 24), 9, 9)
    path = QPainterPath()
    path.moveTo(16, 48)
    path.arcTo(QRectF(16, 34, 32, 28), 0, 180)
    p.setBrush(QBrush(GLYPH))
    p.drawPath(path)


def _session_logout(p) -> None:
    p.setPen(_pen(4.0))
    p.drawRect(QRectF(18, 16, 18, 32))
    p.drawLine(34, 32, 50, 32)
    p.drawLine(44, 26, 50, 32)
    p.drawLine(44, 38, 50, 32)


def _session_lock(p) -> None:
    p.setPen(_pen(4.0))
    path = QPainterPath()
    path.moveTo(24, 30)
    path.arcTo(QRectF(24, 18, 16, 16), 0, 180)
    p.drawPath(path)
    p.drawRoundedRect(QRectF(20, 30, 24, 20), 3, 3)


def _power(p) -> None:
    p.setPen(_pen(4.0))
    p.drawLine(32, 14, 32, 30)
    path = QPainterPath()
    path.moveTo(24, 20)
    path.arcTo(QRectF(16, 18, 32, 32), 120, 300)
    p.drawPath(path)


def _moon(p) -> None:
    p.setPen(Qt.PenStyle.NoPen)
    p.setBrush(QBrush(GLYPH))
    outer = QPainterPath()
    outer.addEllipse(QPointF(32, 32), 18, 18)
    cut = QPainterPath()
    cut.addEllipse(QPointF(40, 26), 16, 16)
    p.drawPath(outer.subtracted(cut))


def _snowflake(p) -> None:
    p.setPen(_pen(3.0))
    for i in range(6):
        ang = math.radians(i * 60)
        dx, dy = math.cos(ang), math.sin(ang)
        p.drawLine(QPointF(32, 32), QPointF(32 + 18 * dx, 32 + 18 * dy))
        # little branches
        bx, by = 32 + 12 * dx, 32 + 12 * dy
        for s in (-1, 1):
            a2 = ang + s * math.radians(35)
            p.drawLine(QPointF(bx, by),
                       QPointF(bx + 6 * math.cos(a2), by + 6 * math.sin(a2)))


def _zzz(p) -> None:
    p.setPen(_pen(3.0))

    def z(x, y, s):
        p.drawLine(QPointF(x, y), QPointF(x + s, y))
        p.drawLine(QPointF(x + s, y), QPointF(x, y + s))
        p.drawLine(QPointF(x, y + s), QPointF(x + s, y + s))

    z(14, 36, 16)   # big (lower-left)
    z(33, 24, 11)   # medium
    z(46, 14, 7)    # small (upper-right)


def _reboot(p) -> None:
    p.setPen(_pen(4.0))
    p.setBrush(Qt.BrushStyle.NoBrush)
    path = QPainterPath()
    path.arcMoveTo(QRectF(14, 14, 36, 36), 60)
    path.arcTo(QRectF(14, 14, 36, 36), 60, 280)
    p.drawPath(path)
    # arrow head at the gap
    p.setBrush(QBrush(GLYPH))
    p.setPen(Qt.PenStyle.NoPen)
    head = QPolygonF([QPointF(46, 12), QPointF(52, 24), QPointF(40, 24)])
    p.drawPolygon(head)


# -- control buttons --------------------------------------------------------
def _ctrl_minus(p) -> None:
    p.setPen(_pen(5.0))
    p.drawLine(19, 32, 45, 32)


def _ctrl_plus(p) -> None:
    p.setPen(_pen(5.0))
    p.drawLine(19, 32, 45, 32)
    p.drawLine(32, 19, 32, 45)


def _ctrl_on(p) -> None:
    p.setPen(_pen(5.0))
    p.drawLine(32, 16, 32, 40)


def _ctrl_off(p) -> None:
    p.setPen(_pen(5.0))
    p.setBrush(Qt.BrushStyle.NoBrush)
    p.drawEllipse(QPointF(32, 30), 12, 12)


# -- tray plasmoid glyphs (pen 4.0, no glow) --------------------------------
def _g_volume(p) -> None:
    p.setPen(_pen(4.0))
    p.setBrush(QBrush(GLYPH))
    spk = QPolygonF([QPointF(16, 26), QPointF(24, 26), QPointF(32, 18),
                     QPointF(32, 46), QPointF(24, 38), QPointF(16, 38)])
    p.drawPolygon(spk)
    p.setBrush(Qt.BrushStyle.NoBrush)
    for r in (9, 15):
        p.drawArc(QRectF(32 - r, 32 - r, 2 * r, 2 * r), -45 * 16, 90 * 16)


def _g_mute(p) -> None:
    # Speaker (no sound waves) with a red diagonal strike -> mute toggle.
    p.setPen(_pen(4.0))
    p.setBrush(QBrush(GLYPH))
    spk = QPolygonF([QPointF(16, 26), QPointF(24, 26), QPointF(32, 18),
                     QPointF(32, 46), QPointF(24, 38), QPointF(16, 38)])
    p.drawPolygon(spk)
    p.setBrush(Qt.BrushStyle.NoBrush)
    strike = QPen(QColor(228, 40, 52), 4.5)
    strike.setCapStyle(Qt.PenCapStyle.RoundCap)
    p.setPen(strike)
    p.drawLine(QPointF(16, 47), QPointF(48, 17))


def _g_network(p) -> None:
    p.setPen(_pen(4.0))
    p.setBrush(Qt.BrushStyle.NoBrush)
    for r in (9, 16, 23):
        p.drawArc(QRectF(32 - r, 45 - r, 2 * r, 2 * r), 55 * 16, 70 * 16)
    p.setBrush(QBrush(GLYPH))
    p.drawEllipse(QPointF(32, 45), 3, 3)


def _g_bluetooth(p) -> None:
    p.setPen(_pen(4.0))
    poly = QPolygonF([QPointF(23, 23), QPointF(41, 41), QPointF(32, 50),
                      QPointF(32, 14), QPointF(41, 23), QPointF(23, 41)])
    p.drawPolyline(poly)


def _g_display(p) -> None:
    p.setPen(_pen(4.0))
    p.drawRoundedRect(QRectF(13, 16, 38, 25), 3, 3)
    p.drawLine(32, 41, 32, 48)
    p.drawLine(22, 48, 42, 48)


def _g_clipboard(p) -> None:
    p.setPen(_pen(4.0))
    p.drawRoundedRect(QRectF(19, 17, 26, 33), 3, 3)
    p.setBrush(QBrush(GLYPH))
    p.drawRoundedRect(QRectF(26, 12, 12, 8), 2, 2)


def _g_notifications(p) -> None:
    p.setPen(_pen(4.0))
    path = QPainterPath()
    path.moveTo(20, 42)
    path.cubicTo(20, 20, 44, 20, 44, 42)
    p.drawPath(path)
    p.drawLine(16, 42, 48, 42)
    p.setBrush(QBrush(GLYPH))
    p.drawEllipse(QPointF(32, 48), 3, 3)


def _g_battery(p) -> None:
    p.setPen(_pen(4.0))
    p.drawRoundedRect(QRectF(15, 23, 30, 18), 2, 2)
    p.setBrush(QBrush(GLYPH))
    p.drawRect(QRectF(45, 28, 4, 8))
    p.drawRect(QRectF(18, 26, 12, 12))


def _g_brightness(p) -> None:
    p.setPen(_pen(4.0))
    p.setBrush(Qt.BrushStyle.NoBrush)
    p.drawEllipse(QPointF(32, 32), 7, 7)
    for i in range(8):
        ang = math.radians(i * 45)
        p.drawLine(QPointF(32 + 12 * math.cos(ang), 32 + 12 * math.sin(ang)),
                   QPointF(32 + 18 * math.cos(ang), 32 + 18 * math.sin(ang)))


def _g_devices(p) -> None:
    # USB trident
    p.setPen(_pen(3.2))
    p.drawLine(QPointF(32, 52), QPointF(32, 14))      # stem
    p.drawLine(QPointF(32, 14), QPointF(28, 21))      # arrow head
    p.drawLine(QPointF(32, 14), QPointF(36, 21))
    p.drawLine(QPointF(32, 30), QPointF(22, 24))      # branch to circle
    p.drawLine(QPointF(32, 38), QPointF(42, 31))      # branch to square
    p.setBrush(QBrush(GLYPH))
    p.drawEllipse(QPointF(32, 52), 4.5, 4.5)          # base plug
    p.drawEllipse(QPointF(22, 24), 3.2, 3.2)          # left prong: circle
    p.drawRect(QRectF(39, 28, 6, 6))                  # right prong: square


def _g_kdeconnect(p) -> None:
    p.setPen(_pen(4.0))
    p.drawRoundedRect(QRectF(25, 16, 14, 32), 3, 3)
    p.setBrush(QBrush(GLYPH))
    p.drawEllipse(QPointF(32, 44), 2, 2)


def _g_dots(p) -> None:
    # Three dots "..." -- the tray popup (overflow) symbol.
    p.setPen(Qt.PenStyle.NoPen)
    p.setBrush(QBrush(GLYPH))
    for dx in (-13, 0, 13):
        p.drawEllipse(QPointF(32 + dx, 33), 4.6, 4.6)


def _g_printer(p) -> None:
    p.setPen(_pen(4.0))
    p.drawRect(QRectF(20, 14, 24, 10))
    p.drawRoundedRect(QRectF(16, 24, 32, 16), 2, 2)
    p.drawRect(QRectF(20, 40, 24, 10))


def _g_keyboard(p) -> None:
    p.setPen(_pen(4.0))
    p.drawRoundedRect(QRectF(12, 22, 40, 20), 3, 3)
    p.drawLine(22, 38, 42, 38)


def _g_media(p) -> None:
    p.setPen(_pen(4.0))
    p.setBrush(QBrush(GLYPH))
    p.drawEllipse(QPointF(24, 44), 6, 5)
    p.setBrush(Qt.BrushStyle.NoBrush)
    p.drawLine(30, 44, 30, 18)
    path = QPainterPath()
    path.moveTo(30, 18)
    path.cubicTo(40, 18, 44, 24, 44, 30)
    p.drawPath(path)


def _g_camera(p) -> None:
    p.setPen(_pen(4.0))
    p.drawRoundedRect(QRectF(14, 22, 36, 22), 3, 3)
    p.drawRect(QRectF(24, 16, 12, 6))
    p.drawEllipse(QPointF(32, 33), 7, 7)


def _g_lockkeys(p) -> None:
    p.setPen(_pen(4.0))
    path = QPainterPath()
    path.moveTo(24, 30)
    path.arcTo(QRectF(24, 18, 16, 16), 0, 180)
    p.drawPath(path)
    body = QPainterPath()
    body.addRoundedRect(QRectF(20, 30, 24, 20), 3, 3)
    hole = QPainterPath()
    hole.addEllipse(QPointF(32, 40), 3, 3)
    p.setBrush(QBrush(GLYPH))
    p.drawPath(body.subtracted(hole))


def _g_inputmethod(p) -> None:
    p.setPen(_pen(4.0))
    p.setBrush(Qt.BrushStyle.NoBrush)
    p.drawEllipse(QPointF(32, 32), 18, 18)
    p.drawEllipse(QRectF(24, 14, 16, 36))
    p.drawLine(14, 32, 50, 32)


def _g_weather(p) -> None:
    p.setPen(_pen(3.0))
    p.setBrush(Qt.BrushStyle.NoBrush)
    p.drawEllipse(QPointF(24, 22), 6, 6)
    for i in range(8):
        ang = math.radians(i * 45)
        p.drawLine(QPointF(24 + 9 * math.cos(ang), 22 + 9 * math.sin(ang)),
                   QPointF(24 + 13 * math.cos(ang), 22 + 13 * math.sin(ang)))
    p.setBrush(QBrush(GLYPH))
    for cx, cy, r in ((32, 40, 9), (42, 42, 7), (24, 42, 6)):
        p.drawEllipse(QPointF(cx, cy), r, r)


_GLYPHS = {
    "hamburger": _hamburger,
    "star": _star,
    "gear": _gear,
    "tray": _tray,
    "wrench": _wrench,
    "show_desktop": _show_desktop,
    "sec_windows": _sec_windows,
    "sec_apps": _sec_apps,
    "sec_files": _sec_files,
    "session": _power,                 # same symbol as Shutdown
    "session_logout": _session_logout,
    "session_lock": _session_lock,
    "session_reboot": _reboot,
    "session_shutdown": _power,
    "session_suspend": _moon,
    "session_hibernate": _zzz,          # Zzz instead of a snowflake
    "ctrl_minus": _ctrl_minus,
    "ctrl_plus": _ctrl_plus,
    "mute": _g_mute,
    "ctrl_on": _ctrl_on,
    "ctrl_off": _ctrl_off,
    "volume": _g_volume,
    "network": _g_network,
    "bluetooth": _g_bluetooth,
    "display": _g_display,
    "clipboard": _g_clipboard,
    "notifications": _g_notifications,
    "battery": _g_battery,
    "brightness": _g_brightness,
    "devices": _g_devices,
    "dots": _g_dots,
    "kdeconnect": _g_kdeconnect,
    "printer": _g_printer,
    "keyboard": _g_keyboard,
    "media": _g_media,
    "camera": _g_camera,
    "lockkeys": _g_lockkeys,
    "inputmethod": _g_inputmethod,
    "weather": _g_weather,
}
