#pragma once

#include <QIcon>
#include <QPixmap>
#include <QString>
#include <QColor>

class KaryonGlyphs {
public:
    static bool hasGlyph(const QString &name);
    static QIcon icon(const QString &name, int size = 64, const QColor &color = QColor());
    static QPixmap pixmap(const QString &name, int size = 64, const QColor &color = QColor());

    static QString ensureGlyphCached(const QString &name, const QString &cacheDir, int size = 64);
};
