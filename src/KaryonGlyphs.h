#pragma once

#include <QIcon>
#include <QPixmap>
#include <QString>

class KaryonGlyphs {
public:
    static bool hasGlyph(const QString &name);
    static QIcon icon(const QString &name, int size = 64);
    static QPixmap pixmap(const QString &name, int size = 64);

    static QString ensureGlyphCached(const QString &name, const QString &cacheDir, int size = 64);
};
