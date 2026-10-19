#include "KaryonGlyphs.h"
#include "ThemeManager.h"
#include "ProcessUtils.h"

#include <QPainter>
#include <QPainterPath>
#include <QDir>
#include <QFile>
#include <QSet>
#include <cmath>

static QString normalizeGlyphName(const QString &name)
{
    QString n = name.toLower();
    if (n.startsWith(QLatin1String("__")) && n.endsWith(QLatin1String("__")) && n.length() > 4) {
        n = n.mid(2, n.length() - 4);
    }
    n.replace(QLatin1Char('_'), QLatin1Char('-'));
    return n;
}

bool KaryonGlyphs::hasGlyph(const QString &name)
{
    QString n = normalizeGlyphName(name);
    return (n == QLatin1String("show-desktop") ||
            n == QLatin1String("app-menu") ||
            n == QLatin1String("tray") ||
            n == QLatin1String("session") ||
            n == QLatin1String("session-logout") ||
            n == QLatin1String("session-lock") ||
            n == QLatin1String("session-suspend") ||
            n == QLatin1String("session-switch-user") ||
            n == QLatin1String("switch-user") ||
            n == QLatin1String("session-reboot") ||
            n == QLatin1String("session-shutdown") ||
            n == QLatin1String("more") ||
            n == QLatin1String("...") ||
            n == QLatin1String("media-previous") ||
            n == QLatin1String("media-play") ||
            n == QLatin1String("media-pause") ||
            n == QLatin1String("media-next") ||
            n == QLatin1String("network") ||
            n == QLatin1String("bluetooth") ||
            n == QLatin1String("volume") ||
            n == QLatin1String("brightness") ||
            n == QLatin1String("energy") ||
            n == QLatin1String("battery") ||
            n == QLatin1String("clipboard") ||
            n == QLatin1String("notifications") ||
            n == QLatin1String("display") ||
            n == QLatin1String("devices") ||
            n == QLatin1String("printer") ||
            n == QLatin1String("camera") ||
            n == QLatin1String("keyboard-layout") ||
            n == QLatin1String("weather") ||
            n == QLatin1String("favorites") ||
            n == QLatin1String("karyon"));
}

QPixmap KaryonGlyphs::pixmap(const QString &name, int size, const QColor &color)
{
    QPixmap pix(size, size);
    pix.fill(Qt::transparent);

    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);

    double s = static_cast<double>(size) / 64.0;
    p.scale(s, s);

    QColor glyphColor = color;
    if (!glyphColor.isValid()) {
        bool isLight = ThemeManager::instance().isLight();
        glyphColor = isLight ? QColor(44, 49, 58) : QColor(255, 255, 255);
    }
    const QColor white = glyphColor;
    QPen pen(white, 3.2);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);

    QString n = normalizeGlyphName(name);
    if (n == QLatin1String("show-desktop")) {
        p.drawRoundedRect(QRectF(14, 13, 36, 25), 2.5, 2.5);
        p.drawLine(QPointF(32, 38), QPointF(32, 47));
        p.drawLine(QPointF(21, 47), QPointF(43, 47));
    } else if (n == QLatin1String("app-menu")) {
        p.setPen(QPen(white, 3.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawLine(QPointF(17, 21), QPointF(47, 21));
        p.drawLine(QPointF(17, 32), QPointF(47, 32));
        p.drawLine(QPointF(17, 43), QPointF(47, 43));
    } else if (n == QLatin1String("tray")) {
        QPainterPath tray;
        tray.moveTo(15, 31);
        tray.lineTo(15, 45);
        tray.lineTo(49, 45);
        tray.lineTo(49, 31);
        p.drawPath(tray);

        QPainterPath arrow;
        arrow.moveTo(25, 27);
        arrow.lineTo(32, 20);
        arrow.lineTo(39, 27);
        p.drawPath(arrow);
    } else if (n == QLatin1String("session") || n == QLatin1String("session-shutdown")) {
        // IEC 5009 Standard Power/Standby Symbol:
        // Kreisbogen mit 60° Öffnung oben (zwischen 60° und 120°):
        // Start bei 120°, 300° gegen den Uhrzeigersinn bis 60°
        p.drawArc(QRectF(17, 17, 30, 30), 120 * 16, 300 * 16);
        p.drawLine(QPointF(32, 13), QPointF(32, 29));
    } else if (n == QLatin1String("session-lock")) {
        p.drawArc(QRectF(22, 14, 20, 24), 0, 180 * 16);
        p.drawLine(QPointF(22, 26), QPointF(22, 30));
        p.drawLine(QPointF(42, 26), QPointF(42, 30));
        p.drawRoundedRect(QRectF(18, 29, 28, 22), 3, 3);
        p.setPen(Qt::NoPen);
        p.setBrush(QBrush(white));
        p.drawEllipse(QPointF(32, 37), 2.5, 2.5);
        p.drawRect(QRectF(31, 37, 2, 6));
        p.setBrush(Qt::NoBrush);
        p.setPen(pen);
    } else if (n == QLatin1String("session-logout")) {
        p.drawLine(QPointF(34, 18), QPointF(20, 18));
        p.drawLine(QPointF(20, 18), QPointF(20, 46));
        p.drawLine(QPointF(20, 46), QPointF(34, 46));
        p.drawLine(QPointF(25, 32), QPointF(45, 32));
        QPainterPath arr;
        arr.moveTo(38, 25);
        arr.lineTo(45, 32);
        arr.lineTo(38, 39);
        p.drawPath(arr);
    } else if (n == QLatin1String("session-suspend")) {
        // Halbmond (Spitzen nach links)
        p.save();
        p.translate(32, 32);
        p.rotate(15);
        p.translate(-32, -32);
        QPainterPath moon;
        moon.arcMoveTo(QRectF(17, 17, 30, 30), 90);
        moon.arcTo(QRectF(17, 17, 30, 30), 90, -180);
        moon.arcTo(QRectF(10, 17, 23, 30), 270, 180);
        moon.closeSubpath();
        p.setPen(Qt::NoPen);
        p.setBrush(QBrush(white));
        p.drawPath(moon);
        p.restore();
    } else if (n == QLatin1String("session-switch-user") || n == QLatin1String("switch-user")) {
        // Personensymbol links + zwei horizontale Wechselpfeile rechts (⇄)
        p.drawEllipse(QPointF(24, 23), 5.5, 5.5);
        QPainterPath u;
        u.moveTo(12, 47);
        u.quadTo(13, 35, 24, 35);
        u.quadTo(35, 35, 36, 47);
        p.drawPath(u);

        // Oberer Pfeil nach rechts (->)
        p.drawLine(QPointF(41, 24), QPointF(53, 24));
        QPainterPath a1;
        a1.moveTo(49, 20);
        a1.lineTo(53, 24);
        a1.lineTo(49, 28);
        p.drawPath(a1);

        // Unterer Pfeil nach links (<-)
        p.drawLine(QPointF(53, 34), QPointF(41, 34));
        QPainterPath a2;
        a2.moveTo(45, 30);
        a2.lineTo(41, 34);
        a2.lineTo(45, 38);
        p.drawPath(a2);
    } else if (n == QLatin1String("session-reboot")) {
        p.drawArc(QRectF(17, 17, 30, 30), 30 * 16, 280 * 16);
        QPainterPath arr;
        arr.moveTo(36, 13);
        arr.lineTo(43, 20);
        arr.lineTo(35, 23);
        p.drawPath(arr);
    } else if (n == QLatin1String("more") || n == QLatin1String("...")) {
        p.setPen(Qt::NoPen);
        p.setBrush(QBrush(white));
        p.drawEllipse(QPointF(20, 32), 3.0, 3.0);
        p.drawEllipse(QPointF(32, 32), 3.0, 3.0);
        p.drawEllipse(QPointF(44, 32), 3.0, 3.0);
    } else if (n == QLatin1String("media-previous")) {
        p.setPen(Qt::NoPen);
        p.setBrush(QBrush(white));
        p.drawRoundedRect(QRectF(16, 20, 4, 24), 1.5, 1.5);
        QPainterPath triangle;
        triangle.moveTo(43, 20);
        triangle.lineTo(24, 32);
        triangle.lineTo(43, 44);
        triangle.closeSubpath();
        p.drawPath(triangle);
    } else if (n == QLatin1String("media-play")) {
        p.setPen(Qt::NoPen);
        p.setBrush(QBrush(white));
        QPainterPath play;
        play.moveTo(24, 19);
        play.lineTo(44, 32);
        play.lineTo(24, 45);
        play.closeSubpath();
        p.drawPath(play);
    } else if (n == QLatin1String("media-pause")) {
        p.setPen(Qt::NoPen);
        p.setBrush(QBrush(white));
        p.drawRoundedRect(QRectF(22, 19, 6, 26), 2, 2);
        p.drawRoundedRect(QRectF(36, 19, 6, 26), 2, 2);
    } else if (n == QLatin1String("media-next")) {
        p.setPen(Qt::NoPen);
        p.setBrush(QBrush(white));
        QPainterPath triangle;
        triangle.moveTo(21, 20);
        triangle.lineTo(40, 32);
        triangle.lineTo(21, 44);
        triangle.closeSubpath();
        p.drawPath(triangle);
        p.drawRoundedRect(QRectF(44, 20, 4, 24), 1.5, 1.5);
    } else if (n == QLatin1String("network")) {
        // Punkt unten
        p.setPen(Qt::NoPen);
        p.setBrush(QBrush(white));
        p.drawEllipse(QPointF(32, 47), 3.0, 3.0);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);

        // 3 konzentrische Funkbögen
        p.drawArc(QRectF(32 - 11, 47 - 11, 22, 22), 45 * 16, 90 * 16);
        p.drawArc(QRectF(32 - 20, 47 - 20, 40, 40), 45 * 16, 90 * 16);
        p.drawArc(QRectF(32 - 29, 47 - 29, 58, 58), 45 * 16, 90 * 16);
    } else if (n == QLatin1String("bluetooth")) {
        p.drawLine(QPointF(32, 14), QPointF(32, 50));
        QPainterPath bt;
        bt.moveTo(22, 23);
        bt.lineTo(42, 41);
        bt.lineTo(32, 50);
        bt.lineTo(32, 14);
        bt.lineTo(42, 23);
        bt.lineTo(22, 41);
        p.drawPath(bt);
    } else if (n == QLatin1String("volume")) {
        QPainterPath spk;
        spk.moveTo(17, 26);
        spk.lineTo(24, 26);
        spk.lineTo(33, 18);
        spk.lineTo(33, 46);
        spk.lineTo(24, 38);
        spk.lineTo(17, 38);
        spk.closeSubpath();
        p.setBrush(QBrush(white));
        p.drawPath(spk);
        p.setBrush(Qt::NoBrush);

        p.drawArc(QRectF(26, 24, 16, 16), -45 * 16, 90 * 16);
        p.drawArc(QRectF(23, 17, 30, 30), -45 * 16, 90 * 16);
    } else if (n == QLatin1String("brightness")) {
        p.drawEllipse(QPointF(32, 32), 8.5, 8.5);
        for (int i = 0; i < 8; ++i) {
            double ang = i * (M_PI / 4.0);
            double cosA = std::cos(ang);
            double sinA = std::sin(ang);
            p.drawLine(QPointF(32 + 13 * cosA, 32 + 13 * sinA),
                       QPointF(32 + 19 * cosA, 32 + 19 * sinA));
        }
    } else if (n == QLatin1String("energy") || n == QLatin1String("battery")) {
        p.drawRoundedRect(QRectF(15, 23, 31, 18), 3, 3);
        p.setBrush(QBrush(white));
        p.drawRoundedRect(QRectF(46, 28, 3.5, 8), 1.5, 1.5);
        p.setBrush(Qt::NoBrush);

        // Blitz im Inneren
        QPainterPath bolt;
        bolt.moveTo(31, 26);
        bolt.lineTo(27, 32);
        bolt.lineTo(33, 32);
        bolt.lineTo(29, 38);
        p.drawPath(bolt);
    } else if (n == QLatin1String("clipboard")) {
        p.drawRoundedRect(QRectF(17, 18, 30, 34), 3, 3);
        p.setBrush(QBrush(white));
        p.drawRoundedRect(QRectF(25, 13, 14, 7), 2, 2);
        p.setBrush(Qt::NoBrush);

        p.drawLine(QPointF(23, 28), QPointF(41, 28));
        p.drawLine(QPointF(23, 34), QPointF(41, 34));
        p.drawLine(QPointF(23, 40), QPointF(34, 40));
    } else if (n == QLatin1String("notifications")) {
        QPainterPath bell;
        bell.moveTo(19, 42);
        bell.lineTo(45, 42);
        bell.moveTo(21, 42);
        bell.cubicTo(21, 32, 25, 20, 32, 18);
        bell.cubicTo(39, 20, 43, 32, 43, 42);
        p.drawPath(bell);
        p.drawLine(QPointF(32, 18), QPointF(32, 14));

        p.setBrush(QBrush(white));
        p.drawChord(QRectF(29, 42, 6, 6), 0, -180 * 16);
        p.setBrush(Qt::NoBrush);
    } else if (n == QLatin1String("display")) {
        p.drawRoundedRect(QRectF(14, 16, 36, 25), 2.5, 2.5);
        p.drawLine(QPointF(32, 41), QPointF(32, 48));
        p.drawLine(QPointF(23, 48), QPointF(41, 48));
    } else if (n == QLatin1String("devices")) {
        p.drawRoundedRect(QRectF(21, 27, 22, 22), 3, 3);
        p.drawRect(QRectF(25, 15, 14, 12));
        p.drawLine(QPointF(29, 20), QPointF(29, 23));
        p.drawLine(QPointF(35, 20), QPointF(35, 23));
    } else if (n == QLatin1String("printer")) {
        p.drawRect(QRectF(22, 15, 20, 11));
        p.drawRoundedRect(QRectF(15, 26, 34, 18), 3, 3);
        p.drawLine(QPointF(20, 37), QPointF(44, 37));
        p.drawRect(QRectF(22, 37, 20, 11));
    } else if (n == QLatin1String("camera")) {
        p.drawRoundedRect(QRectF(14, 23, 36, 26), 4, 4);
        p.drawRoundedRect(QRectF(21, 18, 10, 5), 1.5, 1.5);
        p.drawEllipse(QPointF(32, 36), 7.5, 7.5);
        p.setBrush(QBrush(white));
        p.drawEllipse(QPointF(34, 34), 1.5, 1.5);
        p.setBrush(Qt::NoBrush);
    } else if (n == QLatin1String("keyboard-layout")) {
        p.drawRoundedRect(QRectF(13, 20, 38, 24), 3, 3);
        // Tastenreihen
        p.setPen(Qt::NoPen);
        p.setBrush(QBrush(white));
        for (int c = 0; c < 5; ++c) {
            p.drawRect(QRectF(18 + c * 6, 25, 3.5, 2.5));
            p.drawRect(QRectF(18 + c * 6, 31, 3.5, 2.5));
        }
        p.drawRoundedRect(QRectF(24, 37, 16, 2.5), 1, 1);
        p.setBrush(Qt::NoBrush);
        p.setPen(pen);
    } else if (n == QLatin1String("weather")) {
        // Wolke
        QPainterPath cloud;
        cloud.moveTo(20, 44);
        cloud.lineTo(44, 44);
        cloud.arcTo(QRectF(38, 34, 10, 10), -90, 120);
        cloud.arcTo(QRectF(27, 25, 16, 16), 0, 140);
        cloud.arcTo(QRectF(16, 34, 12, 12), 60, 140);
        cloud.closeSubpath();
        p.drawPath(cloud);
        // Sonnenstrahlen oben rechts
        p.drawLine(QPointF(42, 24), QPointF(46, 20));
        p.drawLine(QPointF(35, 19), QPointF(35, 14));
        p.drawLine(QPointF(46, 30), QPointF(51, 30));
    } else if (n == QLatin1String("favorites")) {
        // Fünfzackiger Stern (★)
        QPainterPath star;
        const double cx = 32.0, cy = 33.0;
        const double outerR = 16.0, innerR = 7.0;
        for (int i = 0; i < 5; ++i) {
            double outerAng = -M_PI / 2.0 + i * 2.0 * M_PI / 5.0;
            double innerAng = outerAng + M_PI / 5.0;
            double ox = cx + outerR * std::cos(outerAng);
            double oy = cy + outerR * std::sin(outerAng);
            double ix = cx + innerR * std::cos(innerAng);
            double iy = cy + innerR * std::sin(innerAng);
            if (i == 0) star.moveTo(ox, oy);
            else        star.lineTo(ox, oy);
            star.lineTo(ix, iy);
        }
        star.closeSubpath();
        p.setBrush(QBrush(white));
        p.drawPath(star);
        p.setBrush(Qt::NoBrush);
    } else if (n == QLatin1String("karyon")) {
        QIcon ico = ProcessUtils::karyonIcon();
        if (!ico.isNull()) {
            p.end();
            return ico.pixmap(size, size);
        }
    }

    p.end();
    return pix;
}

QIcon KaryonGlyphs::icon(const QString &name, int size, const QColor &color)
{
    return QIcon(pixmap(name, size, color));
}

QString KaryonGlyphs::ensureGlyphCached(const QString &name, const QString &cacheDir, int size)
{
    static QSet<QString> s_generated;
    QString norm = normalizeGlyphName(name);
    bool isLight = ThemeManager::instance().isLight();
    QString modeTag = isLight ? QStringLiteral("light_") : QStringLiteral("dark_");
    QDir().mkpath(cacheDir);
    QString path = cacheDir + QStringLiteral("/glyph_") + modeTag + norm + QStringLiteral(".png");
    if (!s_generated.contains(path) || !QFile::exists(path)) {
        QPixmap pix = pixmap(norm, size);
        pix.save(path, "PNG");
        s_generated.insert(path);
    }
    return path;
}
