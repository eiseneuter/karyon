"""Light-streak animation along a recorded gesture path."""
from __future__ import annotations

from PyQt6.QtCore import Qt, QTimer, QPointF
from PyQt6.QtGui import QColor, QPainter, QPen, QGuiApplication
from PyQt6.QtWidgets import QWidget


class GestureFlash(QWidget):
    def __init__(self) -> None:
        super().__init__(None)
        self.setWindowFlags(
            Qt.WindowType.Tool
            | Qt.WindowType.FramelessWindowHint
            | Qt.WindowType.WindowStaysOnTopHint
            | Qt.WindowType.WindowDoesNotAcceptFocus
        )
        self.setAttribute(Qt.WidgetAttribute.WA_TranslucentBackground)
        self.setAttribute(Qt.WidgetAttribute.WA_ShowWithoutActivating)
        self.setAttribute(Qt.WidgetAttribute.WA_TransparentForMouseEvents)
        self._path: list[QPointF] = []
        self._t = 0.0
        self._accent = QColor("#37d0ff")
        self._timer = QTimer(self)
        self._timer.timeout.connect(self._tick)

    def play(self, path: list[tuple], accent: str = "#37d0ff", geo=None) -> None:
        if not path or len(path) < 2:
            return
        self._accent = QColor(accent)
        if geo is None:
            geo = QGuiApplication.primaryScreen().geometry()
        self.setGeometry(geo)
        # path entries are (t, x, y) in coordinates local to ``geo``
        self._path = [QPointF(p[1], p[2]) for p in path]
        self._t = 0.0
        self.show()
        self.raise_()
        self._timer.start(16)

    def _tick(self) -> None:
        self._t += 0.16           # faster streak
        if self._t >= 1.4:
            self._timer.stop()
            self.hide()
            return
        self.update()

    def paintEvent(self, event) -> None:  # noqa: N802
        if not self._path:
            return
        p = QPainter(self)
        p.setRenderHint(QPainter.RenderHint.Antialiasing)
        n = len(self._path)
        head = min(1.0, self._t)
        fade = max(0.0, 1.0 - max(0.0, self._t - 1.0) / 0.4)
        for i in range(1, n):
            frac = i / (n - 1)
            if frac > head:
                break
            alpha = int(220 * fade * (frac))
            col = QColor(self._accent)
            col.setAlpha(max(0, min(255, alpha)))
            pen = QPen(col, 6.0)
            pen.setCapStyle(Qt.PenCapStyle.RoundCap)
            p.setPen(pen)
            p.drawLine(self._path[i - 1], self._path[i])
        p.end()
