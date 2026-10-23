#include "CircleOverlay.h"
#include "InputMonitor.h"
#include "KWinBridge.h"
#include "SettingsManager.h"
#include "ThemeManager.h"

#include <QQuickView>
#include <QQuickItem>
#include <QQmlContext>
#include <QGuiApplication>
#include <QScreen>
#include <QCursor>
#include <QUrl>
#include <QColor>
#include <QPixmap>
#include <QDir>
#include <QFile>
#include <QSettings>
#include <QStandardPaths>
#include <QVariantList>
#include <QVariantMap>
#include <QSet>
#include <QRegularExpression>
#include <QFileInfo>
#include <algorithm>
#include <KWindowSystem>
#include <KX11Extras>
#include <xcb/xcb.h>
#include <QMimeDatabase>
#include <QMimeType>
#include <KService>

CircleOverlay::CircleOverlay(QObject *parent)
    : QObject(parent)
    , m_view(std::make_unique<QQuickView>())
{
    // Stale cached placeholder icons bereinigen
    QDir(QDir::tempPath() + QStringLiteral("/karyon_icons")).removeRecursively();
    applyWindowProperties();

    connect(&m_volumeManager, &VolumeManager::volumeChanged, this, &CircleOverlay::updateVolumeState);
    connect(&m_mediaManager, &MediaManager::mediaChanged, this, [this]() {
        if (isVisible()) {
            refreshCurrentMode();
        }
    });
    connect(&m_trayManager, &TrayManager::trayIconsChanged, this, [this]() {
        m_trayIconRevision++;
        m_cachedTrayPrograms = m_trayManager.runningPrograms();
        if (isVisible()) {
            refreshCurrentMode();
        }
    });

    m_progressThrottleTimer.setSingleShot(true);
    m_progressThrottleTimer.setInterval(100);
    connect(&m_progressThrottleTimer, &QTimer::timeout, this, &CircleOverlay::updateSegmentProgress);
    connect(&m_jobProgressManager, &JobProgressManager::progressUpdated, this, &CircleOverlay::onProgressUpdated);
    connect(&SettingsManager::instance(), &SettingsManager::settingsChanged, this, [this]() {
        m_showDesktopEnabled = SettingsManager::instance().showDesktop();
        m_showTrayEnabled = SettingsManager::instance().showTray();
        m_showSessionEnabled = SettingsManager::instance().showSession();
        m_showMenuEnabled = SettingsManager::instance().showMenu();
        m_showFavoritesEnabled = SettingsManager::instance().showFavorites();
        m_showMediaControlsEnabled = SettingsManager::instance().showMediaControls();
        m_showVolumeControlEnabled = SettingsManager::instance().showVolumeControl();
        m_cachedRecentApps.clear();
        m_cachedFavorites.clear();
        m_cachedRecentFiles.clear();

        bool winEn = SettingsManager::instance().categoryWindowsEnabled();
        bool appsEn = SettingsManager::instance().categoryAppsEnabled();
        bool filesEn = SettingsManager::instance().categoryFilesEnabled();

        if (m_mode == OverlayMode::Windows && !winEn) {
            if (appsEn) m_mode = OverlayMode::RecentApps;
            else if (filesEn) m_mode = OverlayMode::RecentFiles;
        } else if (m_mode == OverlayMode::RecentApps && !appsEn) {
            if (winEn) m_mode = OverlayMode::Windows;
            else if (filesEn) m_mode = OverlayMode::RecentFiles;
        } else if (m_mode == OverlayMode::RecentFiles && !filesEn) {
            if (winEn) m_mode = OverlayMode::Windows;
            else if (appsEn) m_mode = OverlayMode::RecentApps;
        }

        updateScaleProperties();
        if (isVisible()) {
            refreshCurrentMode();
        }
    });
}


CircleOverlay::~CircleOverlay() = default;

OverlayMode CircleOverlay::initialOverlayMode() const
{
    if (SettingsManager::instance().categoryWindowsEnabled()) {
        return OverlayMode::Windows;
    }
    if (SettingsManager::instance().categoryAppsEnabled()) {
        return OverlayMode::RecentApps;
    }
    if (SettingsManager::instance().categoryFilesEnabled()) {
        return OverlayMode::RecentFiles;
    }
    return OverlayMode::Windows;
}

void CircleOverlay::applyWindowProperties()
{
    m_view->setFlags(
        Qt::FramelessWindowHint |
        Qt::WindowStaysOnTopHint |
        Qt::BypassWindowManagerHint |
        Qt::Tool |
        Qt::WindowDoesNotAcceptFocus
    );
    m_view->setCursor(Qt::ArrowCursor);

    m_view->setColor(QColor(Qt::transparent));
    m_view->setResizeMode(QQuickView::SizeRootObjectToView);
    m_view->setTitle(QStringLiteral("karyon-overlay"));
}

void CircleOverlay::prewarm()
{
    m_cachedTrayPrograms = m_trayManager.runningPrograms();
    int wr = windowRadius(true, true);
    int size = wr * 2;

    m_view->resize(size, size);

    // Context-Properties vor dem QML-Laden initialisieren
    updateScaleProperties();
    m_view->rootContext()->setContextProperty(QStringLiteral("centerIndex"), 0);
    m_view->rootContext()->setContextProperty(QStringLiteral("segments"), QVariantList());
    m_view->rootContext()->setContextProperty(QStringLiteral("segments2"), QVariantList());
    m_view->rootContext()->setContextProperty(QStringLiteral("segments3"), QVariantList());
    m_view->rootContext()->setContextProperty(QStringLiteral("hasRing2"), false);
    m_view->rootContext()->setContextProperty(QStringLiteral("hasRing3"), false);
    m_view->rootContext()->setContextProperty(QStringLiteral("volumeLevel"), m_volumeManager.volume());
    m_view->rootContext()->setContextProperty(QStringLiteral("isMuted"), m_volumeManager.isMuted());
    m_view->rootContext()->setContextProperty(QStringLiteral("categoryMode"), QStringLiteral("windows"));
    m_view->rootContext()->setContextProperty(QStringLiteral("showClock"), SettingsManager::instance().showClock());
    m_view->rootContext()->setContextProperty(QStringLiteral("showDate"), SettingsManager::instance().showDate());
    m_view->rootContext()->setContextProperty(QStringLiteral("showVolumeControl"), SettingsManager::instance().showVolumeControl());
    m_view->rootContext()->setContextProperty(QStringLiteral("dateFormatIndex"), SettingsManager::instance().dateFormatIndex());
    m_view->rootContext()->setContextProperty(QStringLiteral("dateSeparator"), SettingsManager::instance().dateSeparator());
    m_view->rootContext()->setContextProperty(QStringLiteral("timeFormatIndex"), SettingsManager::instance().timeFormatIndex());
    m_view->rootContext()->setContextProperty(QStringLiteral("detectedLocaleName"), QLocale().name());
    m_view->rootContext()->setContextProperty(QStringLiteral("overlayController"), this);
    m_view->rootContext()->setContextProperty(QStringLiteral("karyonTheme"), &ThemeManager::instance());

    m_view->setSource(QUrl(QStringLiteral("qrc:/qt/qml/karyon/qml/Circle.qml")));
    m_view->create();

    if (KWindowSystem::isPlatformX11()) {
        KX11Extras::setOnAllDesktops(m_view->winId(), true);
        // NET::Utility signalisiert KWin (fadingpopups), das Fenster nicht zu animieren!
        KX11Extras::setType(m_view->winId(), NET::Utility);

        auto *x11App = qGuiApp->nativeInterface<QNativeInterface::QX11Application>();
        if (x11App) {
            xcb_connection_t *conn = x11App->connection();

            // 1. _KDE_NET_WM_SKIP_CLOSE_ANIMATION = 1 (unterbindet Schließanimation in KWin)
            xcb_intern_atom_cookie_t skipCookie = xcb_intern_atom(conn, 0, 32, "_KDE_NET_WM_SKIP_CLOSE_ANIMATION");
            xcb_intern_atom_reply_t *skipReply = xcb_intern_atom_reply(conn, skipCookie, nullptr);
            if (skipReply) {
                uint32_t val = 1;
                xcb_change_property(conn, XCB_PROP_MODE_REPLACE, m_view->winId(), skipReply->atom, XCB_ATOM_CARDINAL, 32, 1, &val);
                free(skipReply);
            }

            // 2. WM_CLASS auf "karyon\0karyon\0" setzen
            const char wmClass[] = "karyon\0karyon\0";
            xcb_change_property(conn, XCB_PROP_MODE_REPLACE, m_view->winId(), XCB_ATOM_WM_CLASS, XCB_ATOM_STRING, 8, sizeof(wmClass), wmClass);

            // 3. XdndAware = 5 (Signalisiert X11 Drag & Drop Unterstuetzung)
            xcb_intern_atom_cookie_t xdndCookie = xcb_intern_atom(conn, 0, 9, "XdndAware");
            xcb_intern_atom_reply_t *xdndReply = xcb_intern_atom_reply(conn, xdndCookie, nullptr);
            if (xdndReply) {
                uint32_t xdndVersion = 5;
                xcb_change_property(conn, XCB_PROP_MODE_REPLACE, m_view->winId(), xdndReply->atom, XCB_ATOM_ATOM, 32, 1, &xdndVersion);
                free(xdndReply);
            }

            xcb_flush(conn);
        }
    }

    m_view->setVisible(false);

    m_cachedRecentApps = m_appManager.loadRecentApps(13, openAppIdentifiers());
    m_cachedCategories = m_appManager.loadCategories();
}

double CircleOverlay::currentScale() const
{
    return SettingsManager::instance().overlayScale();
}

int CircleOverlay::hubRadius() const
{
    return qRound(HUB_RADIUS * currentScale());
}

int CircleOverlay::ringGap() const
{
    return qRound(RING_GAP * currentScale());
}

int CircleOverlay::ringDepth() const
{
    return qRound(RING_DEPTH * currentScale());
}

int CircleOverlay::ring2Gap() const
{
    return qRound(RING2_GAP * currentScale());
}

int CircleOverlay::ring2Depth() const
{
    return qRound(RING2_DEPTH * currentScale());
}

int CircleOverlay::ring3Gap() const
{
    return qRound(RING3_GAP * currentScale());
}

int CircleOverlay::ring3Depth() const
{
    return qRound(RING3_DEPTH * currentScale());
}

int CircleOverlay::segGap() const
{
    return qRound(SEG_GAP * currentScale());
}

double CircleOverlay::segmentWidth() const
{
    return SEGMENT_WIDTH * currentScale();
}

int CircleOverlay::iconSize() const
{
    return qRound(ICON_SIZE * currentScale());
}

int CircleOverlay::aimMargin() const
{
    return qRound(AIM_MARGIN * currentScale());
}

int CircleOverlay::cursorOffsetY() const
{
    return qRound(CURSOR_OFFSET_Y * currentScale());
}

int CircleOverlay::deadzoneRadius() const
{
    return qRound(DEADZONE_RADIUS * currentScale());
}

void CircleOverlay::updateScaleProperties()
{
    if (!m_view) return;
    auto *ctx = m_view->rootContext();
    if (!ctx) return;
    double s = currentScale();
    ctx->setContextProperty(QStringLiteral("overlayScale"), s);
    ctx->setContextProperty(QStringLiteral("circleRadius"), hubRadius());
    ctx->setContextProperty(QStringLiteral("deadzoneRadius"), deadzoneRadius());
    ctx->setContextProperty(QStringLiteral("ringGap"), ringGap());
    ctx->setContextProperty(QStringLiteral("ringDepth"), ringDepth());
    ctx->setContextProperty(QStringLiteral("ring2Gap"), ring2Gap());
    ctx->setContextProperty(QStringLiteral("ring2Depth"), ring2Depth());
    ctx->setContextProperty(QStringLiteral("ring3Gap"), ring3Gap());
    ctx->setContextProperty(QStringLiteral("ring3Depth"), ring3Depth());
    ctx->setContextProperty(QStringLiteral("segGap"), segGap());
    ctx->setContextProperty(QStringLiteral("iconSize"), iconSize());
    ctx->setContextProperty(QStringLiteral("aimMargin"), aimMargin());
    ctx->setContextProperty(QStringLiteral("cursorOffsetY"), cursorOffsetY());
    ctx->setContextProperty(QStringLiteral("overlayTransparency"), SettingsManager::instance().overlayTransparency());
    ctx->setContextProperty(QStringLiteral("volumeStep"), SettingsManager::instance().volumeStep());
    ctx->setContextProperty(QStringLiteral("showClock"), SettingsManager::instance().showClock());
    ctx->setContextProperty(QStringLiteral("showDate"), SettingsManager::instance().showDate());
    ctx->setContextProperty(QStringLiteral("showVolumeControl"), SettingsManager::instance().showVolumeControl());
    ctx->setContextProperty(QStringLiteral("dateFormatIndex"), SettingsManager::instance().dateFormatIndex());
    ctx->setContextProperty(QStringLiteral("dateSeparator"), SettingsManager::instance().dateSeparator());
    ctx->setContextProperty(QStringLiteral("timeFormatIndex"), SettingsManager::instance().timeFormatIndex());
    ctx->setContextProperty(QStringLiteral("detectedLocaleName"), QLocale().name());

    if (m_view->rootObject()) {
        m_view->rootObject()->setProperty("overlayScale", s);
        m_view->rootObject()->setProperty("aimMarginVal", aimMargin());
        m_view->rootObject()->setProperty("currentVolumeStep", SettingsManager::instance().volumeStep());
        m_view->rootObject()->setProperty("showClock", SettingsManager::instance().showClock());
        m_view->rootObject()->setProperty("showDate", SettingsManager::instance().showDate());
        m_view->rootObject()->setProperty("showVolumeControl", SettingsManager::instance().showVolumeControl());
        m_view->rootObject()->setProperty("dateFormatIdx", SettingsManager::instance().dateFormatIndex());
        m_view->rootObject()->setProperty("dateSep", SettingsManager::instance().dateSeparator());
        m_view->rootObject()->setProperty("timeFormatIdx", SettingsManager::instance().timeFormatIndex());
        m_view->rootObject()->setProperty("overlayTransparencyVal", SettingsManager::instance().overlayTransparency());
    }
}


int CircleOverlay::visualRadius(bool hasRing2, bool hasRing3) const
{
    int r = hubRadius() + ringGap() + ringDepth();
    if (hasRing2) {
        r += ring2Gap() + ring2Depth();
    }
    if (hasRing3) {
        r += ring3Gap() + ring3Depth();
    }
    return r;
}

int CircleOverlay::windowRadius(bool hasRing2, bool hasRing3) const
{
    return visualRadius(hasRing2, hasRing3) + aimMargin();
}

QString CircleOverlay::findDesktopFile(const QString &name)
{
    return AppManager::findDesktopFilePathCaseInsensitive(name);
}

QString CircleOverlay::iconNameFromDesktopFile(const QString &path)
{
    if (path.isEmpty()) return {};
    QSettings desktop(path, QSettings::IniFormat);
    desktop.beginGroup(QStringLiteral("Desktop Entry"));
    return desktop.value(QStringLiteral("Icon")).toString();
}

QIcon CircleOverlay::resolveIcon(const QString &name)
{
    if (name.isEmpty()) return {};

    // 1. Direkter Dateipfad (nur echte Bilddateien oder .desktop)
    if (QFile::exists(name)) {
        static const QStringList imgExts = {
            QStringLiteral(".png"), QStringLiteral(".svg"), QStringLiteral(".svgz"),
            QStringLiteral(".xpm"), QStringLiteral(".jpg"), QStringLiteral(".jpeg"),
            QStringLiteral(".ico"), QStringLiteral(".webp")
        };
        for (const auto &ext : imgExts) {
            if (name.endsWith(ext, Qt::CaseInsensitive)) {
                QIcon imgIcon(name);
                if (!imgIcon.isNull() && !imgIcon.availableSizes().isEmpty()) {
                    return imgIcon;
                }
                break;
            }
        }
        if (name.endsWith(QLatin1String(".desktop"), Qt::CaseInsensitive)) {
            QString ic = iconNameFromDesktopFile(name);
            if (!ic.isEmpty()) {
                QIcon dIcon = resolveIcon(ic);
                if (!dIcon.isNull()) return dIcon;
            }
        }
    }

    // 2. Steam App Icon Check
    if (name.startsWith(QLatin1String("steam_app_"))) {
        QString sid = name.mid(10);
        if (sid.endsWith(QLatin1String(".desktop"))) sid.chop(8);
        QString sName;
        QIcon sIcon;
        if (resolveSteamGame(sid, sName, sIcon) && !sIcon.isNull()) {
            return sIcon;
        }
    }

    // 3. System Theme Icon
    QIcon icon = QIcon::fromTheme(name);
    if (!icon.isNull() && !icon.availableSizes().isEmpty()) {
        return icon;
    }

    // 4. Pixmaps-Verzeichnisse (/usr/share/pixmaps, ~/.local/share/pixmaps)
    static const QStringList pixmapDirs = {
        QStringLiteral("/usr/share/pixmaps"),
        QDir::homePath() + QStringLiteral("/.local/share/pixmaps"),
        QStringLiteral("/usr/local/share/pixmaps")
    };
    static const QStringList extensions = {
        QStringLiteral(".png"),
        QStringLiteral(".svg"),
        QStringLiteral(".xpm"),
        QStringLiteral("")
    };

    for (const QString &dir : pixmapDirs) {
        for (const QString &ext : extensions) {
            QString full = dir + QLatin1Char('/') + name + ext;
            if (QFile::exists(full)) {
                QIcon pIcon(full);
                if (!pIcon.isNull()) return pIcon;
            }
        }
    }

    // 5. Lowercase Theme & Pixmaps
    QString lower = name.toLower();
    if (lower != name) {
        icon = QIcon::fromTheme(lower);
        if (!icon.isNull() && !icon.availableSizes().isEmpty()) {
            return icon;
        }
        for (const QString &dir : pixmapDirs) {
            for (const QString &ext : extensions) {
                QString full = dir + QLatin1Char('/') + lower + ext;
                if (QFile::exists(full)) {
                    QIcon pIcon(full);
                    if (!pIcon.isNull()) return pIcon;
                }
            }
        }
    }

    return {};
}

bool CircleOverlay::resolveSteamGame(const QString &steamId, QString &outName, QIcon &outIcon)
{
    if (steamId.isEmpty()) return false;

    if (m_steamNameCache.contains(steamId) && m_steamIconCache.contains(steamId)) {
        outName = m_steamNameCache.value(steamId);
        outIcon = m_steamIconCache.value(steamId);
        return true;
    }

    AppEntry entry = AppManager::resolveSteamGame(steamId);
    if (!entry.id.isEmpty()) {
        outName = entry.name;
        m_steamNameCache.insert(steamId, outName);

        QIcon icon;
        // 1. System icon theme check (z.B. Papirus SVG für scharfe Vektor-Icons)
        QIcon themeIcon = QIcon::fromTheme(QStringLiteral("steam_icon_") + steamId);
        if (!themeIcon.isNull() && !themeIcon.availableSizes().isEmpty()) {
            icon = themeIcon;
        } else if (!entry.icon.isEmpty()) {
            if (QFile::exists(entry.icon)) {
                icon = QIcon(entry.icon);
            } else {
                icon = resolveIcon(entry.icon);
            }
        }
        if (icon.isNull()) {
            icon = QIcon::fromTheme(QStringLiteral("steam_icon_") + steamId);
        }
        if (icon.isNull()) {
            icon = QIcon::fromTheme(QStringLiteral("steam"));
        }
        outIcon = icon;
        m_steamIconCache.insert(steamId, outIcon);
        return true;
    }

    outName = QStringLiteral("Steam App ") + steamId;
    outIcon = QIcon::fromTheme(QStringLiteral("steam"));
    return false;
}

QIcon CircleOverlay::resolveWindowIcon(const QString &desktopFileName, const QString &resourceClass)
{
    if (KaryonGlyphs::hasGlyph(desktopFileName)) {
        return KaryonGlyphs::icon(desktopFileName);
    }
    if (KaryonGlyphs::hasGlyph(resourceClass)) {
        return KaryonGlyphs::icon(resourceClass);
    }

    // 1. Steam Game Erkennung
    static const QRegularExpression steamRe(QStringLiteral("steam_app_(\\d+)"), QRegularExpression::CaseInsensitiveOption);
    auto matchDf = steamRe.match(desktopFileName);
    auto matchRc = steamRe.match(resourceClass);
    QString steamId = matchDf.hasMatch() ? matchDf.captured(1) : (matchRc.hasMatch() ? matchRc.captured(1) : QString());
    if (steamId.isEmpty()) {
        AppManager::isSteamApp(desktopFileName, resourceClass, &steamId);
    }
    if (!steamId.isEmpty()) {
        QString sName;
        QIcon sIcon;
        if (resolveSteamGame(steamId, sName, sIcon) && !sIcon.isNull()) {
            return sIcon;
        }
    }

    // 2. desktopFileName direkt über resolveIcon
    if (!desktopFileName.isEmpty()) {
        QIcon icon = resolveIcon(desktopFileName);
        if (!icon.isNull()) return icon;
    }

    // 3. .desktop-Datei suchen und Icon= auslesen
    QString dfName = desktopFileName.isEmpty() ? resourceClass : desktopFileName;
    QString dfPath = findDesktopFile(dfName);
    if (!dfPath.isEmpty()) {
        QString iconName = iconNameFromDesktopFile(dfPath);
        if (!iconName.isEmpty()) {
            QIcon icon = resolveIcon(iconName);
            if (!icon.isNull()) return icon;
        }
    }

    // 4. resourceClass direkt
    if (!resourceClass.isEmpty()) {
        QIcon icon = resolveIcon(resourceClass);
        if (!icon.isNull()) return icon;
        icon = resolveIcon(resourceClass.toLower());
        if (!icon.isNull()) return icon;

        if (dfName != resourceClass) {
            dfPath = findDesktopFile(resourceClass);
            if (!dfPath.isEmpty()) {
                QString iconName = iconNameFromDesktopFile(dfPath);
                if (!iconName.isEmpty()) {
                    QIcon icon2 = resolveIcon(iconName);
                    if (!icon2.isNull()) return icon2;
                }
            }
        }
    }

    // 5. Fallback
    return QIcon::fromTheme(QStringLiteral("application-x-executable"));
}

QString CircleOverlay::appNameFromDesktopFile(const QString &path)
{
    if (path.isEmpty()) return {};
    QSettings desktop(path, QSettings::IniFormat);
    desktop.beginGroup(QStringLiteral("Desktop Entry"));
    return desktop.value(QStringLiteral("Name")).toString();
}

QString CircleOverlay::resolveAppName(const QString &desktopFileName, const QString &resourceClass)
{
    QString cacheKey = desktopFileName.isEmpty() ? resourceClass : desktopFileName;
    if (cacheKey.isEmpty()) return QStringLiteral("Window");

    if (m_appNameCache.contains(cacheKey)) {
        return m_appNameCache.value(cacheKey);
    }

    // 1. Steam Game Erkennung
    static const QRegularExpression steamRe(QStringLiteral("steam_app_(\\d+)"), QRegularExpression::CaseInsensitiveOption);
    auto matchDf = steamRe.match(desktopFileName);
    auto matchRc = steamRe.match(resourceClass);
    QString steamId = matchDf.hasMatch() ? matchDf.captured(1) : (matchRc.hasMatch() ? matchRc.captured(1) : QString());
    if (steamId.isEmpty()) {
        AppManager::isSteamApp(desktopFileName, resourceClass, &steamId);
    }
    if (!steamId.isEmpty()) {
        QString sName;
        QIcon sIcon;
        if (resolveSteamGame(steamId, sName, sIcon) && !sName.isEmpty()) {
            m_appNameCache.insert(cacheKey, sName);
            return sName;
        }
    }

    // 2. Suche über desktopFileName
    QString dfPath;
    if (!desktopFileName.isEmpty()) {
        dfPath = findDesktopFile(desktopFileName);
    }
    if (dfPath.isEmpty() && !resourceClass.isEmpty()) {
        dfPath = findDesktopFile(resourceClass);
    }

    if (!dfPath.isEmpty()) {
        QString name = appNameFromDesktopFile(dfPath);
        if (!name.isEmpty()) {
            m_appNameCache.insert(cacheKey, name);
            return name;
        }
    }

    // 3. Fallback aus Name oder resourceClass
    QString raw = desktopFileName.isEmpty() ? resourceClass : desktopFileName;
    if (raw.endsWith(QLatin1String(".desktop"), Qt::CaseInsensitive)) {
        raw.chop(8);
    }
    int slash = raw.lastIndexOf(QLatin1Char('/'));
    if (slash >= 0 && slash + 1 < raw.length()) {
        raw = raw.mid(slash + 1);
    }
    int lastDot = raw.lastIndexOf(QLatin1Char('.'));
    if (lastDot >= 0 && lastDot + 1 < raw.length()) {
        raw = raw.mid(lastDot + 1);
    }
    if (!raw.isEmpty()) {
        raw[0] = raw[0].toUpper();
    } else {
        raw = QStringLiteral("Window");
    }

    m_appNameCache.insert(cacheKey, raw);
    return raw;
}

QString CircleOverlay::formatWindowTooltip(const QString &appName, const QString &caption, const QString &resourceClass)
{
    // Whitespace und Steuerzeichen bereinigen
    QString cleanCap;
    cleanCap.reserve(caption.size());
    bool inSpace = false;
    for (const QChar &ch : caption) {
        if (ch.isSpace()) {
            if (!inSpace) {
                cleanCap.append(QLatin1Char(' '));
                inSpace = true;
            }
        } else if (ch.category() != QChar::Other_Control &&
                   ch.category() != QChar::Other_Format &&
                   ch.category() != QChar::Other_Surrogate &&
                   ch.unicode() != 0xfffd) {
            cleanCap.append(ch);
            inSpace = false;
        }
    }
    cleanCap = cleanCap.trimmed();

    if (cleanCap.isEmpty()) {
        return appName;
    }

    // Trennung an Gedankenstrichen / Bindestrichen (em-dash, en-dash, hyphen mit Leerzeichen)
    static const QRegularExpression sepRegex(QStringLiteral(R"(\s+[—–\-]\s+)"));
    QStringList parts = cleanCap.split(sepRegex, Qt::SkipEmptyParts);

    QString detail;
    if (parts.size() >= 2) {
        QString last = parts.last().trimmed();
        bool isAppSuffix = false;
        if (last.compare(appName, Qt::CaseInsensitive) == 0 ||
            (!resourceClass.isEmpty() && last.compare(resourceClass, Qt::CaseInsensitive) == 0) ||
            last.contains(appName, Qt::CaseInsensitive) ||
            appName.contains(last, Qt::CaseInsensitive)) {
            isAppSuffix = true;
        }

        if (isAppSuffix) {
            parts.removeLast();
            detail = parts.join(QStringLiteral(" - ")).trimmed();
        } else {
            detail = cleanCap;
        }
    } else {
        if (cleanCap.compare(appName, Qt::CaseInsensitive) == 0 ||
            (!resourceClass.isEmpty() && cleanCap.compare(resourceClass, Qt::CaseInsensitive) == 0)) {
            detail.clear();
        } else {
            detail = cleanCap;
        }
    }

    if (detail.isEmpty()) {
        return appName;
    }

    // Detail abkürzen, falls zu lang (max. 32 Zeichen)
    constexpr int MAX_DETAIL_LEN = 32;
    if (detail.length() > MAX_DETAIL_LEN) {
        detail = detail.left(MAX_DETAIL_LEN - 3).trimmed() + QStringLiteral("...");
    }

    if (appName.isEmpty()) {
        return detail;
    }

    return appName + QStringLiteral(" - ") + detail;
}

static uint getProcessParentPid(uint pid)
{
    if (pid == 0) return 0;
    QFile f(QStringLiteral("/proc/%1/stat").arg(pid));
    if (!f.open(QIODevice::ReadOnly)) return 0;
    QByteArray data = f.readAll();
    int rparen = data.lastIndexOf(')');
    if (rparen < 0) return 0;
    QByteArray rest = data.mid(rparen + 2);
    int space = rest.indexOf(' ');
    if (space < 0) return 0;
    int space2 = rest.indexOf(' ', space + 1);
    if (space2 < 0) return 0;
    return rest.mid(space + 1, space2 - space - 1).toUInt();
}

static QString getProcessComm(uint pid)
{
    if (pid == 0) return {};
    QFile f(QStringLiteral("/proc/%1/comm").arg(pid));
    if (!f.open(QIODevice::ReadOnly)) return {};
    return QString::fromUtf8(f.readLine()).trimmed();
}

QSet<QString> CircleOverlay::openAppIdentifiers() const
{
    QSet<QString> set;
    static const QRegularExpression steamRe(QStringLiteral("steam_app_(\\d+)"), QRegularExpression::CaseInsensitiveOption);

    for (const auto &w : m_cachedWindows) {
        if (!w.desktopFileName.isEmpty()) {
            QString df = w.desktopFileName.toLower();
            set.insert(df);
            if (df.endsWith(QLatin1String(".desktop"))) {
                df.chop(8);
                set.insert(df);
            }
            int dot = df.lastIndexOf(QLatin1Char('.'));
            if (dot >= 0 && dot + 1 < df.length()) {
                set.insert(df.mid(dot + 1));
            }
        }
        if (!w.resourceClass.isEmpty()) {
            QString rc = w.resourceClass.toLower();
            set.insert(rc);
            int dot = rc.lastIndexOf(QLatin1Char('.'));
            if (dot >= 0 && dot + 1 < rc.length()) {
                set.insert(rc.mid(dot + 1));
            }
        }

        // Steam-Game Identifiers
        QString steamId;
        if (!AppManager::isSteamApp(w.desktopFileName, w.resourceClass, &steamId, w.pid)) {
            auto matchDf = steamRe.match(w.desktopFileName);
            auto matchRc = steamRe.match(w.resourceClass);
            steamId = matchDf.hasMatch() ? matchDf.captured(1) : (matchRc.hasMatch() ? matchRc.captured(1) : QString());
        }
        if (!steamId.isEmpty()) {
            set.insert(QStringLiteral("steam_app_") + steamId);
            set.insert(QStringLiteral("steam_app_") + steamId + QStringLiteral(".desktop"));
            set.insert(QStringLiteral("steam_icon_") + steamId);
            set.insert(steamId);
            QString sName;
            QIcon sIcon;
            if (const_cast<CircleOverlay*>(this)->resolveSteamGame(steamId, sName, sIcon) && !sName.isEmpty()) {
                set.insert(sName.toLower());
            }
        }

        QString name = const_cast<CircleOverlay*>(this)->resolveAppName(w.desktopFileName, w.resourceClass).toLower();
        if (!name.isEmpty() && name != QLatin1String("window")) {
            set.insert(name);
        }
    }

    // Laufende Tray-Programme ebenfalls ausschließen, selbst wenn ihr Fenster geschlossen ist
    QList<TrayProgramItem> trayProgs = m_cachedTrayPrograms;
    if (trayProgs.isEmpty()) {
        trayProgs = const_cast<CircleOverlay*>(this)->m_trayManager.runningPrograms();
    }
    for (const auto &prog : trayProgs) {
        if (prog.id.isEmpty() || prog.id == QLatin1String("karyon")) continue;
        QString pId = prog.id.toLower();
        set.insert(pId);
        int sIdx = pId.indexOf(QLatin1String("_status_icon"));
        if (sIdx > 0) {
            set.insert(pId.left(sIdx));
        }
        if (pId.endsWith(QLatin1String(".desktop"))) {
            set.insert(pId.left(pId.length() - 8));
        }
        int dot = pId.lastIndexOf(QLatin1Char('.'));
        if (dot >= 0 && dot + 1 < pId.length()) {
            set.insert(pId.mid(dot + 1));
        }
        if (!prog.title.isEmpty()) {
            set.insert(prog.title.toLower());
        }
        if (prog.pid > 0) {
            QFile commFile(QStringLiteral("/proc/%1/comm").arg(prog.pid));
            if (commFile.open(QIODevice::ReadOnly)) {
                QString comm = QString::fromUtf8(commFile.readAll()).trimmed().toLower();
                if (!comm.isEmpty()) {
                    set.insert(comm);
                }
            }
        }
    }

    return set;
}

void CircleOverlay::partitionWindows(const QList<WindowInfo> &mru,
                                     QList<OverlaySegment> &outRing1,
                                     int &outRing1CenterIndex,
                                     QList<OverlaySegment> &outRing2,
                                     QList<OverlaySegment> &outRing3)
{
    outRing1.clear();
    outRing2.clear();
    outRing3.clear();

    const double availArc = 1.5 * M_PI; // 270° Bogen (-225° bis +45°, zentriert um -90°)
    const double arcMin = -M_PI / 2.0 - availArc / 2.0;
    const double arcMax = -M_PI / 2.0 + availArc / 2.0;

    const int r1Outer = hubRadius() + ringGap() + ringDepth();
    const double gapAng1 = (double)segGap() / r1Outer;
    const double maxSegAng1 = segmentWidth() / r1Outer;

    auto getAppKey = [](const WindowInfo &w) {
        if (w.resourceClass.contains(QLatin1String("plasmawindowed"), Qt::CaseInsensitive) ||
            w.desktopFileName.contains(QLatin1String("plasmawindowed"), Qt::CaseInsensitive)) {
            AppEntry app = AppManager::resolveWindowApp(w.desktopFileName, w.resourceClass, w.pid, w.caption);
            if (!app.id.isEmpty()) return app.id;
            return QStringLiteral("plasmawindowed:") + w.caption.trimmed().toLower();
        }
        return w.desktopFileName.isEmpty() ? w.resourceClass : w.desktopFileName;
    };

    QHash<QString, QStringList> appGroupWindowIds;
    for (const auto &w : mru) {
        QString key = getAppKey(w);
        appGroupWindowIds[key].append(w.id);
    }

    auto getMruIdx = [&](const WindowInfo &w) {
        for (int i = 0; i < mru.size(); ++i) {
            if (mru[i].id == w.id) return i;
        }
        return 999;
    };

    auto matchesFileWindow = [&](const WindowInfo &w, const PinnedItem &item) -> bool {
        if (item.type != PinnedItemType::File) return false;
        QFileInfo fi(item.execOrPath);
        QString fileName = fi.fileName();
        if (!fileName.isEmpty() && w.caption.contains(fileName, Qt::CaseInsensitive)) {
            return true;
        }
        QString baseName = fi.completeBaseName();
        if (baseName.length() >= 2) {
            QRegularExpression re(QStringLiteral("(?:^|[\\s\\-_—\\[\\]\\(\\)•*])%1(?:$|[\\s\\-_—\\[\\]\\(\\)•*])")
                                  .arg(QRegularExpression::escape(baseName)),
                                  QRegularExpression::CaseInsensitiveOption);
            if (re.match(w.caption).hasMatch()) {
                return true;
            }
        }
        return false;
    };

    QSet<QString> appsWithPinAssigned;
    QString preferredPinWindowId;
    QString preferredPinAppKey;

    auto configurePinStatus = [&](OverlaySegment &seg, const WindowInfo &w) {
        bool isPw = w.resourceClass.contains(QLatin1String("plasmawindowed"), Qt::CaseInsensitive) ||
                    w.desktopFileName.contains(QLatin1String("plasmawindowed"), Qt::CaseInsensitive);

        auto checkAppPinned = [&]() -> bool {
            if (!seg.appKey.isEmpty() && m_pinnedManager.isAppPinned(seg.appKey, seg.resourceClass)) return true;
            if (!seg.desktopFileName.isEmpty() && m_pinnedManager.isAppPinned(seg.desktopFileName, seg.resourceClass)) return true;
            if (m_pinnedManager.isAppPinned(w.desktopFileName, w.resourceClass)) return true;
            return false;
        };

        if (isPw) {
            // Jedes Plasmoid-Fenster ist ein eigenständiges Fenstersegment und immer einzeln pinnbar
            seg.canPin = true;
            seg.isPinned = checkAppPinned();
            return;
        }

        QString appKey = getAppKey(w);
        bool isFilePin = false;
        for (const auto &item : m_pinnedManager.pinnedItems()) {
            if (item.type == PinnedItemType::File && matchesFileWindow(w, item)) {
                isFilePin = true;
                break;
            }
        }

        if (isFilePin) {
            seg.canPin = true;
            seg.isPinned = true;
        } else if (!preferredPinWindowId.isEmpty() && appKey == preferredPinAppKey) {
            if (w.id == preferredPinWindowId) {
                seg.canPin = true;
                seg.isPinned = checkAppPinned();
                appsWithPinAssigned.insert(appKey);
            } else {
                seg.canPin = false;
                seg.isPinned = false;
            }
        } else if (!appsWithPinAssigned.contains(appKey)) {
            // Erstinstanz dieser Anwendung im Overlay erhält die Pinnadel
            seg.canPin = true;
            seg.isPinned = checkAppPinned();
            appsWithPinAssigned.insert(appKey);
        } else {
            // Alle weiteren Instanzen einer App erhalten keine Pinnadel
            seg.canPin = false;
            seg.isPinned = false;
        }
    };

    auto makeWinSeg = [&](const WindowInfo &w) {
        OverlaySegment seg;
        seg.windowId = w.id;
        seg.caption = w.caption;
        seg.ring = 1;

        bool isPw = w.resourceClass.contains(QLatin1String("plasmawindowed"), Qt::CaseInsensitive) ||
                    w.desktopFileName.contains(QLatin1String("plasmawindowed"), Qt::CaseInsensitive);

        AppEntry app = AppManager::resolveWindowApp(w.desktopFileName, w.resourceClass, w.pid, w.caption);
        seg.appKey = !app.id.isEmpty() ? app.id : getAppKey(w);
        seg.desktopFileName = !app.desktopFile.isEmpty() ? app.desktopFile : w.desktopFileName;
        seg.resourceClass = !app.icon.isEmpty() ? app.icon : w.resourceClass;

        if (isPw) {
            seg.caption = !w.caption.isEmpty() ? w.caption : (!app.name.isEmpty() ? app.name : QStringLiteral("Plasma Windowed"));
            seg.tooltipText = seg.caption;
            seg.groupWindowIds = {w.id};
            seg.windowCount = 1;
        } else {
            QString displayName = !app.name.isEmpty() ? app.name : resolveAppName(seg.desktopFileName, seg.resourceClass);
            seg.tooltipText = formatWindowTooltip(displayName, w.caption, w.resourceClass);
            QString appGroupKey = getAppKey(w);
            seg.groupWindowIds = appGroupWindowIds.value(appGroupKey);
            seg.windowCount = seg.groupWindowIds.size();
        }

        seg.segmentType = QStringLiteral("window");
        seg.isRealWindow = true;
        configurePinStatus(seg, w);

        // Wenn das Fenster zu einer im Tray residierenden App gehört, Live-Tray-Icon verknüpfen
        const TrayProgramItem *trayMatch = nullptr;
        bool isGameWindow = AppManager::isSteamApp(w.desktopFileName, w.resourceClass, nullptr, w.pid) ||
                            w.resourceClass.startsWith(QLatin1String("steam_app_"), Qt::CaseInsensitive) ||
                            w.desktopFileName.startsWith(QLatin1String("steam_app_"), Qt::CaseInsensitive) ||
                            seg.appKey.startsWith(QLatin1String("steam_app_"), Qt::CaseInsensitive);

        if (!isGameWindow) {
            uint windowPpid = (w.pid > 0) ? getProcessParentPid((uint)w.pid) : 0;
            QString windowComm = (w.pid > 0) ? getProcessComm((uint)w.pid).toLower() : QString();

            for (const auto &prog : m_cachedTrayPrograms) {
                if (prog.id == QLatin1String("karyon")) continue;
                // 1. Exakter PID- oder PPID-Match (nur bei gleichem Prozessnamen für Multi-Process Electron/Chromium-Apps)
                if (prog.pid > 0 && w.pid > 0) {
                    if (prog.pid == (uint)w.pid) {
                        trayMatch = &prog;
                        break;
                    }
                    if (prog.pid == windowPpid && !windowComm.isEmpty()) {
                        QString progComm = getProcessComm(prog.pid).toLower();
                        if (!progComm.isEmpty() && progComm == windowComm) {
                            trayMatch = &prog;
                            break;
                        }
                    }
                }
                // 2. Namens- und Stem-Match (nur exakte Übereinstimmung)
                QString cleanProgId = prog.id.toLower();
                int idx = cleanProgId.indexOf(QLatin1String("_status_icon"));
                if (idx > 0) cleanProgId = cleanProgId.left(idx);
                int dot = cleanProgId.lastIndexOf(QLatin1Char('.'));
                QString stemProgId = (dot >= 0) ? cleanProgId.mid(dot + 1) : cleanProgId;

                QString wRcLow = w.resourceClass.toLower();
                QString wDfLow = w.desktopFileName.toLower();
                if (wDfLow.endsWith(QLatin1String(".desktop"))) {
                    wDfLow.chop(8);
                }

                if ((!wRcLow.isEmpty() && (wRcLow == cleanProgId || wRcLow == stemProgId)) ||
                    (!wDfLow.isEmpty() && (wDfLow == cleanProgId || wDfLow == stemProgId)) ||
                    (!prog.title.isEmpty() && (wRcLow == prog.title.toLower() || wDfLow == prog.title.toLower()))) {
                    trayMatch = &prog;
                    break;
                }
            }
        }

        if (trayMatch) {
            seg.trayProgramKey = QStringLiteral("%1|%2").arg(trayMatch->service, trayMatch->path);
        }

        return seg;
    };

    auto makePinnedSeg = [&](const PinnedItem &item) {
        OverlaySegment seg;
        if (item.type == PinnedItemType::App) {
            const TrayProgramItem *trayMatch = nullptr;
            for (const auto &prog : m_cachedTrayPrograms) {
                if (PinnedManager::itemMatchesApp(item, prog.id, prog.title)) {
                    trayMatch = &prog;
                    break;
                }
            }

            if (trayMatch) {
                seg.windowId = QStringLiteral("tray_prog|%1|%2").arg(trayMatch->service, trayMatch->path);
                seg.appKey = QStringLiteral("tray_prog_%1").arg(trayMatch->id);
                seg.resourceClass = trayMatch->id;
                seg.desktopFileName = QStringLiteral("tray_prog|%1|%2").arg(trayMatch->service, trayMatch->path);
                seg.caption = trayMatch->title.isEmpty() ? item.name : trayMatch->title;
                seg.tooltipText = seg.caption;
                seg.segmentType = QStringLiteral("tray");
                seg.trayProgramKey = QStringLiteral("%1|%2").arg(trayMatch->service, trayMatch->path);
            } else {
                QString exec = item.execOrPath.trimmed();
                QString df = item.desktopFile;
                QString icon = item.icon;
                if (exec.isEmpty() ||
                    exec.endsWith(QLatin1String(".desktop"), Qt::CaseInsensitive) ||
                    exec.contains(QLatin1String("/applications/"), Qt::CaseInsensitive)) {
                    AppEntry resolved = AppManager::findApp(
                        !df.isEmpty() ? df : (!item.id.isEmpty() ? item.id : exec),
                        item.name);
                    if (!resolved.exec.trimmed().isEmpty()) {
                        exec = resolved.exec.trimmed();
                    }
                    if (df.isEmpty() && !resolved.desktopFile.isEmpty()) {
                        df = resolved.desktopFile;
                    }
                    if (icon.isEmpty() && !resolved.icon.isEmpty()) {
                        icon = resolved.icon;
                    }
                }
                if (exec.isEmpty()) {
                    exec = df.isEmpty() ? item.id : df;
                }
                seg.windowId = QStringLiteral("app:") + exec;
                seg.appKey = item.id;
                seg.resourceClass = icon.isEmpty() ? item.id : icon;
                seg.desktopFileName = df.isEmpty() ? item.id : df;
                seg.caption = item.name;
                seg.tooltipText = item.name;
                seg.segmentType = QStringLiteral("app");
            }
        } else {
            seg.windowId = QStringLiteral("file:") + item.execOrPath;
            seg.appKey = QStringLiteral("file:") + item.execOrPath;
            seg.resourceClass = QStringLiteral("document");
            seg.desktopFileName = QStringLiteral("file:") + item.execOrPath;
            seg.caption = item.name;
            seg.tooltipText = item.name;
            seg.segmentType = QStringLiteral("file");
        }
        seg.ring = 1;
        seg.isRealWindow = false;
        seg.canPin = true;
        seg.isPinned = true;
        appsWithPinAssigned.insert(item.id);
        return seg;
    };

    // Ungeöffnete Pinned Items sammeln (geöffnete Apps/Dateien werden zu normalen Fenstern)
    auto isAppOpenInMru = [&](const PinnedItem &item) -> bool {
        for (const auto &w : mru) {
            AppEntry app = AppManager::resolveWindowApp(w.desktopFileName, w.resourceClass, w.pid, w.caption);
            if (!app.id.isEmpty() && PinnedManager::itemMatchesApp(item, app.id, app.icon)) {
                return true;
            }
            if (!app.desktopFile.isEmpty() && PinnedManager::itemMatchesApp(item, app.desktopFile, app.icon)) {
                return true;
            }
            if (PinnedManager::itemMatchesApp(item, w.desktopFileName, w.resourceClass)) {
                return true;
            }
            if (!item.name.isEmpty()) {
                QString wName = const_cast<CircleOverlay*>(this)->resolveAppName(w.desktopFileName, w.resourceClass);
                if (!wName.isEmpty() && wName.compare(item.name, Qt::CaseInsensitive) == 0) {
                    return true;
                }
            }
        }
        return false;
    };

    QList<PinnedItem> unopenPins;
    for (const auto &item : m_pinnedManager.pinnedItems()) {
        if (item.type == PinnedItemType::App) {
            if (isAppOpenInMru(item)) {
                continue; // Bereits als echtes Fenster geöffnet und in mru vorhanden!
            }
            unopenPins.append(item);
        } else if (item.type == PinnedItemType::File) {
            bool open = false;
            // 1. Hat Karyon diesen Dateipfad kürzlich gestartet (innerhalb der letzten 6 Sekunden)?
            const qint64 now = QDateTime::currentMSecsSinceEpoch();
            auto checkRecent = [&](const QString &k) {
                if (m_launchedFiles.contains(k)) {
                    if (now - m_launchedFiles.value(k) < 6000) {
                        return true;
                    } else {
                        m_launchedFiles.remove(k);
                    }
                }
                return false;
            };
            if (checkRecent(item.execOrPath) || checkRecent(item.name)) {
                open = true;
            }
            // 2. Existiert ein offenes Fenster für diese Datei?
            for (const auto &w : mru) {
                if (matchesFileWindow(w, item)) {
                    open = true;
                    // Fenster ist jetzt real vorhanden, Pending-Flag ablösen
                    m_launchedFiles.remove(item.execOrPath);
                    m_launchedFiles.remove(item.name);
                    break;
                }
            }
            if (!open) {
                unopenPins.append(item);
            }
        }
    }

    const int leadBase = m_showDesktopEnabled ? 1 : 0;
    // Wie viele Pins können maximal auf Ring 1 untergebracht werden?
    // Bei leerer Fensterliste: bis zu 10 (bzw. 11 ohne Show Desktop)
    // Bei offenen Fenstern: mindestens W1 und W2 (bzw. W1 bei nur 1 Fenster) für stabile 12-Uhr-Vorauswahl reservieren
    const int minWinSlots = std::min((int)mru.size(), 2);
    const int maxR1Pins = MAX_RING1_SEGMENTS - leadBase - minWinSlots;
    int numPins = std::min((int)unopenPins.size(), maxR1Pins);

    if (mru.isEmpty()) {
        if (m_showDesktopEnabled) {
            OverlaySegment sdSeg;
            sdSeg.windowId = QStringLiteral("__SHOW_DESKTOP__");
            sdSeg.appKey = QStringLiteral("__SHOW_DESKTOP__");
            sdSeg.resourceClass = QStringLiteral("show-desktop");
            sdSeg.desktopFileName = QStringLiteral("__SHOW_DESKTOP__");
            sdSeg.caption = QStringLiteral("Show Desktop");
            sdSeg.ring = 1;
            sdSeg.tooltipText = m_desktopShowing ? QStringLiteral("Show Desktop - Restore")
                                                : QStringLiteral("Show Desktop");
            sdSeg.segmentType = QStringLiteral("action");
            sdSeg.isRealWindow = false;
            sdSeg.canPin = false;
            sdSeg.isPinned = false;
            outRing1.append(sdSeg);
        }
        for (int i = 0; i < numPins; ++i) {
            outRing1.append(makePinnedSeg(unopenPins[i]));
        }
        int totalR1 = outRing1.size();
        if (totalR1 > 0) {
            double segAng1 = (availArc - (totalR1 - 1) * gapAng1) / totalR1;
            if (segAng1 > maxSegAng1) segAng1 = maxSegAng1;
            double step1 = segAng1 + gapAng1;
            outRing1CenterIndex = (totalR1 - 1) / 2;
            for (int i = 0; i < totalR1; ++i) {
                outRing1[i].angle = -M_PI / 2.0 + (i - outRing1CenterIndex) * step1;
                outRing1[i].span = segAng1;
            }
        } else {
            outRing1CenterIndex = 0;
        }

        // Überzählige Pins auf Ring 2 (falls > 10 Pins vorhanden sind)
        if (unopenPins.size() > numPins) {
            int r2Pins = unopenPins.size() - numPins;
            const int r2Outer = r1Outer + ring2Gap() + ring2Depth();
            const double gapAng2 = (double)segGap() / r2Outer;
            const double segAng2 = segmentWidth() / r2Outer;
            const double step2 = segAng2 + gapAng2;
            double r2Start = -M_PI / 2.0 - ((r2Pins - 1) * step2) / 2.0;
            for (int i = 0; i < r2Pins; ++i) {
                OverlaySegment seg = makePinnedSeg(unopenPins[numPins + i]);
                seg.ring = 2;
                seg.angle = r2Start + i * step2;
                seg.span = segAng2;
                outRing2.append(seg);
            }
        }
        return;
    }

    // App-MRU-Rang ermitteln: Index des frühesten Fensters einer App in mru (0 = aktivste App)
    QHash<QString, int> appMruRank;
    for (int i = 0; i < mru.size(); ++i) {
        QString key = getAppKey(mru[i]);
        if (!appMruRank.contains(key)) {
            appMruRank[key] = i;
        }
    }

    // 1. Fenster für Ring 1 auswählen:
    // W1 (mru[0], aktiv) und W2 (mru[1], zuletzt aktiv) MÜSSEN ZWINGEND auf Ring 1 sein,
    // selbst wenn sie Instanzen derselben App sind (wichtig für 100% stabile A-B-A-B Vorauswahl)!
    QList<WindowInfo> r1Windows;
    if (mru.size() <= 1) {
        r1Windows = mru;
    } else {
        r1Windows.append(mru[0]); // W1: aktuell aktiv
        r1Windows.append(mru[1]); // W2: zuletzt aktiv (12-Uhr-Vorauswahl)

        QSet<QString> appsOnRing1;
        appsOnRing1.insert(getAppKey(mru[0]));
        appsOnRing1.insert(getAppKey(mru[1]));

        int maxR1Wins = MAX_RING1_SEGMENTS - leadBase - numPins;
        for (int i = 2; i < mru.size() && r1Windows.size() < maxR1Wins; ++i) {
            QString key = getAppKey(mru[i]);
            if (!appsOnRing1.contains(key)) {
                appsOnRing1.insert(key);
                r1Windows.append(mru[i]);
            }
        }
    }

    if (r1Windows.size() >= 2 && getAppKey(r1Windows[0]) == getAppKey(r1Windows[1])) {
        preferredPinWindowId = r1Windows[1].id;
        preferredPinAppKey = getAppKey(r1Windows[1]);
    }

    // 2. Ring 1 Segmente zusammenstellen
    QList<OverlaySegment> r1Display;

    if (m_showDesktopEnabled) {
        // Show Desktop Segment kommt ZWINGEND an Index 0 (äußerst links auf Ring 1)
        OverlaySegment sdSeg;
        sdSeg.windowId = QStringLiteral("__SHOW_DESKTOP__");
        sdSeg.appKey = QStringLiteral("__SHOW_DESKTOP__");
        sdSeg.resourceClass = QStringLiteral("show-desktop");
        sdSeg.desktopFileName = QStringLiteral("__SHOW_DESKTOP__");
        sdSeg.caption = QStringLiteral("Show Desktop");
        sdSeg.ring = 1;
        sdSeg.tooltipText = m_desktopShowing ? QStringLiteral("Show Desktop - Restore")
                                            : QStringLiteral("Show Desktop");
        sdSeg.segmentType = QStringLiteral("action");
        sdSeg.isRealWindow = false;
        sdSeg.canPin = false;
        sdSeg.isPinned = false;
        r1Display.append(sdSeg);

        // Direkt nach Show Desktop: Ungeöffnete Pinned Items (Index 1..numPins)
        for (int i = 0; i < numPins; ++i) {
            r1Display.append(makePinnedSeg(unopenPins[i]));
        }

        if (r1Windows.size() == 1) {
            r1Display.append(makeWinSeg(r1Windows[0]));
            outRing1CenterIndex = r1Display.size() - 1;
        } else {
            // Mindestens 2 Fenster (W1 und W2) vorhanden:
            // Ring 1 wird bis zu seiner Maximalkapazität (11 Segmente) VOLL aufgefüllt.
            // W2 verbleibt strikt bei 12 Uhr (-90°). Links und rechts werden die Segmente
            // so symmetrisch wie möglich austariert (|S_left - S_right| <= 1).
            int candCount = r1Windows.size();
            int leadCount = 1 + numPins; // Show Desktop (1) + numPins
            int R = std::clamp((leadCount + candCount) / 2, 0, candCount - 1);
            int L = candCount - 1 - R;

            QList<WindowInfo> leftList;
            QList<WindowInfo> rightList;

            if (L >= 1) {
                leftList.append(r1Windows[0]); // W1 unmittelbar links von W2
                for (int i = 2; i < candCount; ++i) {
                    if (rightList.size() < R) {
                        rightList.append(r1Windows[i]);
                    } else if (leftList.size() < L) {
                        leftList.prepend(r1Windows[i]);
                    }
                }
            } else {
                rightList.append(r1Windows[0]); // W1 unmittelbar rechts von W2
                for (int i = 2; i < candCount && rightList.size() < R; ++i) {
                    rightList.append(r1Windows[i]);
                }
            }

            for (const auto &w : leftList) {
                r1Display.append(makeWinSeg(w));
            }

            // W2 bei 12 Uhr
            outRing1CenterIndex = r1Display.size();
            r1Display.append(makeWinSeg(r1Windows[1]));

            for (const auto &w : rightList) {
                r1Display.append(makeWinSeg(w));
            }
        }
    } else {
        // Ohne Show Desktop
        for (int i = 0; i < numPins; ++i) {
            r1Display.append(makePinnedSeg(unopenPins[i]));
        }

        if (r1Windows.size() <= 1) {
            if (!r1Windows.isEmpty()) {
                r1Display.append(makeWinSeg(r1Windows[0]));
            }
            outRing1CenterIndex = std::max<int>(0, r1Display.size() - 1);
        } else {
            int candCount = r1Windows.size();
            int leadCount = numPins;
            int R = std::clamp((leadCount + candCount) / 2, 0, candCount - 1);
            int L = candCount - 1 - R;

            QList<WindowInfo> leftList;
            QList<WindowInfo> rightList;

            if (L >= 1) {
                leftList.append(r1Windows[0]); // W1 unmittelbar links von W2
                for (int i = 2; i < candCount; ++i) {
                    if (rightList.size() < R) {
                        rightList.append(r1Windows[i]);
                    } else if (leftList.size() < L) {
                        leftList.prepend(r1Windows[i]);
                    }
                }
            } else {
                rightList.append(r1Windows[0]); // W1 unmittelbar rechts von W2
                for (int i = 2; i < candCount && rightList.size() < R; ++i) {
                    rightList.append(r1Windows[i]);
                }
            }

            for (const auto &w : leftList) {
                r1Display.append(makeWinSeg(w));
            }

            // W2 bei 12 Uhr
            outRing1CenterIndex = r1Display.size();
            r1Display.append(makeWinSeg(r1Windows[1]));

            for (const auto &w : rightList) {
                r1Display.append(makeWinSeg(w));
            }
        }
    }

    int totalR1Segs = r1Display.size();
    double segAng1 = (availArc - (totalR1Segs - 1) * gapAng1) / std::max(1, totalR1Segs);
    if (segAng1 > maxSegAng1) segAng1 = maxSegAng1;
    const double step1 = segAng1 + gapAng1;

    QHash<QString, double> r1AppAngles;
    for (int i = 0; i < totalR1Segs; ++i) {
        double mid = -M_PI / 2.0 + (i - outRing1CenterIndex) * step1;
        r1Display[i].angle = mid;
        r1Display[i].span = segAng1;

        QString app = r1Display[i].appKey;
        if (app != QLatin1String("__SHOW_DESKTOP__")) {
            if (!r1AppAngles.contains(app) || i == outRing1CenterIndex) {
                r1AppAngles[app] = mid;
            }
        }
    }

    outRing1 = r1Display;

    QSet<QString> r1WindowIds;
    for (const auto &s : r1Display) {
        if (s.isRealWindow) {
            r1WindowIds.insert(s.windowId);
        }
    }

    // 3. Restliche Fenster (die nicht auf Ring 1 liegen) nach Apps gruppieren:
    QHash<QString, QList<WindowInfo>> remainingAppWindows;
    for (const auto &win : mru) {
        if (r1WindowIds.contains(win.id)) continue;
        QString key = getAppKey(win);
        remainingAppWindows[key].append(win);
    }

    // Apps strikt nach App-MRU-Rang ordnen (0 = aktivste App)
    QStringList remainingAppOrder = remainingAppWindows.keys();
    std::sort(remainingAppOrder.begin(), remainingAppOrder.end(), [&](const QString &a, const QString &b) {
        return appMruRank.value(a, 999) < appMruRank.value(b, 999);
    });

    // 4. Ring 2 Vorbereitung:
    const int r2Outer = r1Outer + ring2Gap() + ring2Depth();
    const double gapAng2 = (double)segGap() / r2Outer;
    // Bogenbreite in Pixeln bleibt starr bei segmentWidth() am Außenrand:
    const double segAng2 = segmentWidth() / r2Outer;
    const double step2 = segAng2 + gapAng2;

    // Fester Minimalabstand von Clustern zu Nachbar-Apps: exakt 8 px
    const double clusterGapAng2 = (8.0 * currentScale()) / r2Outer;

    struct Exclusion {
        double minAngle;
        double maxAngle;
    };
    QList<Exclusion> r2Exclusions;

    struct CascadedCluster {
        QString app;
        double anchorAngle;
        QList<WindowInfo> windows;
    };
    QList<CascadedCluster> r3Clusters;

    // A) Sekundäre Instanzen der Ring-1-Apps auf Ring 2 platzieren
    // 1. Alle Ring-1-Apps mit Sekundärinstanzen sammeln
    QStringList r1AppsWithSec;
    for (const QString &app : remainingAppOrder) {
        if (r1AppAngles.contains(app) && !remainingAppWindows[app].isEmpty()) {
            r1AppsWithSec.append(app);
        }
    }
    // Nach Ankerwinkel sortieren (von links nach rechts) für geometrische Nachbarschaftsbeziehungen
    std::sort(r1AppsWithSec.begin(), r1AppsWithSec.end(), [&](const QString &a, const QString &b) {
        return r1AppAngles[a] < r1AppAngles[b];
    });

    struct ClusterPlan {
        QString app;
        double anchor = 0.0;
        QList<WindowInfo> secWins;
        int count = 0;
        bool expandRight = false;
        bool expandLeft = false;
    };
    QList<ClusterPlan> plans;
    const int numSecApps = r1AppsWithSec.size();

    for (int i = 0; i < numSecApps; ++i) {
        ClusterPlan plan;
        plan.app = r1AppsWithSec[i];
        plan.anchor = r1AppAngles.value(plan.app, -M_PI / 2.0);
        plan.secWins = remainingAppWindows[plan.app];
        // Innerhalb der App nach MRU sortieren: Index 0 = jüngste Sekundärinstanz
        std::sort(plan.secWins.begin(), plan.secWins.end(), [&](const WindowInfo &a, const WindowInfo &b) {
            return getMruIdx(a) < getMruIdx(b);
        });
        plan.count = std::min((int)plan.secWins.size(), 3);
        plans.append(plan);
    }

    // 2. Gesamtkapazität auf Ring 2 prüfen:
    auto calcTotalRequiredSpan = [&](const QList<ClusterPlan> &plist) {
        double total = 0.0;
        int activeCount = 0;
        for (const auto &p : plist) {
            if (p.count > 0) {
                total += (p.count - 1) * step2 + segAng2;
                activeCount++;
            }
        }
        if (activeCount > 1) {
            total += (activeCount - 1) * clusterGapAng2;
        }
        return total;
    };

    // Sollte Ring 2 physikalisch voll sein (selten), reduzieren Apps mit schlechterem (höherem) appMruRank:
    while (calcTotalRequiredSpan(plans) > (arcMax - arcMin)) {
        int worstIdx = -1;
        int worstRank = -1;
        for (int i = 0; i < plans.size(); ++i) {
            if (plans[i].count > 0) {
                int rank = appMruRank.value(plans[i].app, 999);
                if (rank > worstRank) {
                    worstRank = rank;
                    worstIdx = i;
                }
            }
        }
        if (worstIdx == -1) break;
        plans[worstIdx].count--;
    }

    QList<ClusterPlan> activePlans;
    for (const auto &p : plans) {
        if (p.count > 0) {
            activePlans.append(p);
        } else if (!p.secWins.isEmpty()) {
            r3Clusters.append({p.app, p.anchor, p.secWins});
        }
    }

    // 3. Direktionales Branching bei benachbarten kollidierenden Apps
    const int M = activePlans.size();
    for (int i = 0; i + 1 < M; ++i) {
        double halfI = ((activePlans[i].count - 1) * step2 + segAng2) / 2.0;
        double halfNext = ((activePlans[i+1].count - 1) * step2 + segAng2) / 2.0;
        double dist = activePlans[i+1].anchor - activePlans[i].anchor;
        if (dist - (halfI + halfNext) < clusterGapAng2) {
            activePlans[i].expandLeft = true;
            activePlans[i+1].expandRight = true;
        }
    }

    // 4. Ideale Zentren berechnen
    QList<double> centers(M);
    QList<double> halfClusters(M);
    for (int i = 0; i < M; ++i) {
        halfClusters[i] = ((activePlans[i].count - 1) * step2 + segAng2) / 2.0;
        bool expLeft = activePlans[i].expandLeft && !activePlans[i].expandRight;
        bool expRight = activePlans[i].expandRight && !activePlans[i].expandLeft;
        if (expRight) {
            // Rechtes Branching: Linkestes Segment sitzt direkt über dem Anker
            centers[i] = activePlans[i].anchor + (activePlans[i].count - 1) * step2 / 2.0;
        } else if (expLeft) {
            // Linkes Branching: Rechtestes Segment sitzt direkt über dem Anker
            centers[i] = activePlans[i].anchor - (activePlans[i].count - 1) * step2 / 2.0;
        } else {
            // Zentriert
            centers[i] = activePlans[i].anchor;
        }
    }

    // 5. Globale konfliktfreie Relaxation (symmetrisches Ausweichen mit Clusterabstand & Priorisierung)
    if (M > 0) {
        for (int iter = 0; iter < 10; ++iter) {
            for (int i = 0; i + 1 < M; ++i) {
                double reqDist = halfClusters[i] + clusterGapAng2 + halfClusters[i+1];
                double curDist = centers[i+1] - centers[i];
                if (curDist < reqDist) {
                    double overlap = reqDist - curDist;
                    int rankI = appMruRank.value(activePlans[i].app, 999);
                    int rankNext = appMruRank.value(activePlans[i+1].app, 999);
                    double wI = 0.5;
                    double wNext = 0.5;
                    if (rankI < rankNext) {
                        wI = 0.2;
                        wNext = 0.8;
                    } else if (rankI > rankNext) {
                        wI = 0.8;
                        wNext = 0.2;
                    }
                    centers[i] -= overlap * wI;
                    centers[i+1] += overlap * wNext;
                }
            }
            if (centers[0] - halfClusters[0] < arcMin) {
                centers[0] = arcMin + halfClusters[0];
            }
            if (centers[M-1] + halfClusters[M-1] > arcMax) {
                centers[M-1] = arcMax - halfClusters[M-1];
            }
        }

        // Vorwärts-Pass (von links nach rechts)
        for (int i = 1; i < M; ++i) {
            double minPos = centers[i-1] + halfClusters[i-1] + clusterGapAng2 + halfClusters[i];
            if (centers[i] < minPos) {
                centers[i] = minPos;
            }
        }

        // Rückwärts-Pass (von rechts nach links)
        if (centers[M-1] + halfClusters[M-1] > arcMax) {
            centers[M-1] = arcMax - halfClusters[M-1];
        }
        for (int i = M - 2; i >= 0; --i) {
            double maxPos = centers[i+1] - halfClusters[i+1] - clusterGapAng2 - halfClusters[i];
            if (centers[i] > maxPos) {
                centers[i] = maxPos;
            }
        }

        // Sicherheits-Clamp links
        if (centers[0] - halfClusters[0] < arcMin) {
            centers[0] = arcMin + halfClusters[0];
            for (int i = 1; i < M; ++i) {
                double minPos = centers[i-1] + halfClusters[i-1] + clusterGapAng2 + halfClusters[i];
                if (centers[i] < minPos) {
                    centers[i] = minPos;
                }
            }
        }
    }

    // 6. Segmente auf Ring 2 platzieren
    for (int i = 0; i < M; ++i) {
        const auto &plan = activePlans[i];
        int K = plan.count;
        double center = centers[i];
        bool expLeft = plan.expandLeft && !plan.expandRight;
        bool expRight = plan.expandRight && !plan.expandLeft;

        QList<WindowInfo> winsToPlace;
        if (expRight) {
            // k = 0 sitzt am Anker (links) -> jüngste Sekundärinstanz secWins[0]
            winsToPlace = plan.secWins.mid(0, K);
        } else if (expLeft) {
            // k = K-1 sitzt am Anker (rechts) -> jüngste Sekundärinstanz secWins[0]
            for (int k = 0; k < K; ++k) {
                winsToPlace.append(plan.secWins[K - 1 - k]);
            }
        } else {
            // Zentriert: chronologisch von links nach rechts (älteste links, neueste rechts)
            winsToPlace = plan.secWins.mid(0, K);
            std::sort(winsToPlace.begin(), winsToPlace.end(), [&](const WindowInfo &a, const WindowInfo &b) {
                return getMruIdx(a) > getMruIdx(b);
            });
        }

        double cMin = 999.0;
        double cMax = -999.0;
        for (int k = 0; k < K; ++k) {
            double ang = center - (K - 1) * step2 / 2.0 + k * step2;
            OverlaySegment seg;
            seg.windowId = winsToPlace[k].id;
            seg.appKey = plan.app;
            seg.resourceClass = winsToPlace[k].resourceClass;
            seg.desktopFileName = winsToPlace[k].desktopFileName;
            seg.caption = winsToPlace[k].caption;
            seg.angle = ang;
            seg.span = segAng2;
            seg.ring = 2;
            seg.tooltipText = formatWindowTooltip(resolveAppName(winsToPlace[k].desktopFileName, winsToPlace[k].resourceClass), winsToPlace[k].caption, winsToPlace[k].resourceClass);
            seg.segmentType = QStringLiteral("window");
            seg.isRealWindow = true;
            configurePinStatus(seg, winsToPlace[k]);
            outRing2.append(seg);

            if (ang < cMin) cMin = ang;
            if (ang > cMax) cMax = ang;
        }

        Exclusion excl;
        excl.minAngle = cMin - segAng2 / 2.0 - clusterGapAng2;
        excl.maxAngle = cMax + segAng2 / 2.0 + clusterGapAng2;
        r2Exclusions.append(excl);

        // Überzählige Instanzen (ab Index K) kaskadieren auf Ring 3
        if (plan.secWins.size() > K) {
            r3Clusters.append({plan.app, center, plan.secWins.mid(K)});
        }
    }

    // B) Überlauf-Apps auf Ring 2 platzieren ("Herumfließen", zentriert um 12 Uhr)
    QStringList overflowApps;
    for (const QString &app : remainingAppOrder) {
        if (!r1AppAngles.contains(app)) {
            overflowApps.append(app);
        }
    }

    // Exklusionszonen sortieren und zusammenführen
    std::sort(r2Exclusions.begin(), r2Exclusions.end(), [](const Exclusion &a, const Exclusion &b) {
        return a.minAngle < b.minAngle;
    });

    QList<Exclusion> mergedExcl;
    for (const auto &excl : r2Exclusions) {
        if (mergedExcl.isEmpty()) {
            mergedExcl.append(excl);
        } else {
            if (excl.minAngle <= mergedExcl.last().maxAngle) {
                mergedExcl.last().maxAngle = std::max(mergedExcl.last().maxAngle, excl.maxAngle);
            } else {
                mergedExcl.append(excl);
            }
        }
    }

    // Freie Intervalle auf Ring 2 ermitteln
    struct FreeInterval {
        double start;
        double end;
        int capacity = 0;
        double distTo12 = 0.0;
        bool contains12 = false;
    };
    QList<FreeInterval> freeIntervals;
    double curAng = arcMin;
    for (const auto &excl : mergedExcl) {
        if (excl.minAngle > curAng) {
            double s = curAng;
            double e = std::min(arcMax, excl.minAngle);
            if (e - s >= segAng2) {
                freeIntervals.append({s, e});
            }
        }
        curAng = std::max(curAng, excl.maxAngle);
    }
    if (curAng < arcMax && arcMax - curAng >= segAng2) {
        freeIntervals.append({curAng, arcMax});
    }

    const double angle12 = -M_PI / 2.0;

    for (auto &iv : freeIntervals) {
        double span = iv.end - iv.start;
        iv.capacity = std::max(0, (int)std::floor((span + gapAng2) / step2));
        iv.contains12 = (iv.start <= angle12 && angle12 <= iv.end);
        if (iv.contains12) {
            iv.distTo12 = 0.0;
        } else if (iv.end < angle12) {
            iv.distTo12 = angle12 - iv.end;
        } else {
            iv.distTo12 = iv.start - angle12;
        }
    }

    // Intervalle ohne Kapazität entfernen
    for (int i = freeIntervals.size() - 1; i >= 0; --i) {
        if (freeIntervals[i].capacity <= 0) {
            freeIntervals.removeAt(i);
        }
    }

    // Überzählige unopen Pins (falls unopenPins.size() > numPins)
    QList<PinnedItem> excessPins;
    if (unopenPins.size() > numPins) {
        excessPins = unopenPins.mid(numPins);
    }

    // Überlauf-Apps nach MRU ordnen (aktivste zuerst)
    std::sort(overflowApps.begin(), overflowApps.end(), [&](const QString &a, const QString &b) {
        return appMruRank.value(a, 999) < appMruRank.value(b, 999);
    });

    QVector<int> assignedCount(freeIntervals.size(), 0);
    int remainingToPlace = excessPins.size() + overflowApps.size();

    while (remainingToPlace > 0) {
        int bestIdx = -1;
        double bestDist = 1e9;
        for (int i = 0; i < freeIntervals.size(); ++i) {
            if (assignedCount[i] < freeIntervals[i].capacity) {
                double effDist = freeIntervals[i].distTo12 + assignedCount[i] * step2;
                if (effDist < bestDist) {
                    bestDist = effDist;
                    bestIdx = i;
                }
            }
        }
        if (bestIdx == -1) break; // Ring 2 voll
        assignedCount[bestIdx]++;
        remainingToPlace--;
    }

    struct AssignedSlot {
        double angle;
        double distTo12;
    };
    QList<AssignedSlot> assignedSlots;

    for (int i = 0; i < freeIntervals.size(); ++i) {
        int m = assignedCount[i];
        if (m <= 0) continue;
        const auto &iv = freeIntervals[i];

        if (iv.contains12) {
            // Symmetrisch um 12 Uhr zentrieren!
            double startAng = angle12 - (m - 1) * step2 / 2.0;
            if (startAng - segAng2 / 2.0 < iv.start) {
                startAng = iv.start + segAng2 / 2.0;
            }
            if (startAng + (m - 1) * step2 + segAng2 / 2.0 > iv.end) {
                startAng = iv.end - segAng2 / 2.0 - (m - 1) * step2;
            }
            for (int k = 0; k < m; ++k) {
                double a = startAng + k * step2;
                assignedSlots.append({a, std::abs(a - angle12)});
            }
        } else if (iv.end <= angle12) {
            // Links von 12 Uhr: Schmiegt sich an den rechten Rand (nahe 12 Uhr)
            double rightEdge = iv.end - segAng2 / 2.0;
            double startAng = rightEdge - (m - 1) * step2;
            for (int k = 0; k < m; ++k) {
                double a = startAng + k * step2;
                assignedSlots.append({a, std::abs(a - angle12)});
            }
        } else {
            // Rechts von 12 Uhr: Schmiegt sich an den linken Rand (nahe 12 Uhr)
            double leftEdge = iv.start + segAng2 / 2.0;
            for (int k = 0; k < m; ++k) {
                double a = leftEdge + k * step2;
                assignedSlots.append({a, std::abs(a - angle12)});
            }
        }
    }

    // Slots nach Nähe zu 12 Uhr sortieren
    std::sort(assignedSlots.begin(), assignedSlots.end(), [](const AssignedSlot &a, const AssignedSlot &b) {
        return a.distTo12 < b.distTo12;
    });

    int placedExcessPins = std::min((int)assignedSlots.size(), (int)excessPins.size());
    QList<OverlaySegment> r2OverflowSegs;

    for (int k = 0; k < placedExcessPins; ++k) {
        OverlaySegment seg = makePinnedSeg(excessPins[k]);
        seg.ring = 2;
        seg.angle = assignedSlots[k].angle;
        seg.span = segAng2;
        r2OverflowSegs.append(seg);
    }

    int remainingSlots = assignedSlots.size() - placedExcessPins;
    int placedCount = std::min(remainingSlots, (int)overflowApps.size());
    QStringList r3OverflowApps;

    for (int k = 0; k < placedCount; ++k) {
        const QString &app = overflowApps[k];
        double ang = assignedSlots[placedExcessPins + k].angle;
        WindowInfo win = remainingAppWindows[app].first();
        OverlaySegment seg;
        seg.windowId = win.id;
        seg.appKey = app;
        seg.resourceClass = win.resourceClass;
        seg.desktopFileName = win.desktopFileName;
        seg.caption = win.caption;
        seg.angle = ang;
        seg.span = segAng2;
        seg.ring = 2;
        seg.tooltipText = formatWindowTooltip(resolveAppName(win.desktopFileName, win.resourceClass), win.caption, win.resourceClass);
        seg.segmentType = QStringLiteral("window");
        seg.isRealWindow = true;
        configurePinStatus(seg, win);
        r2OverflowSegs.append(seg);

        if (remainingAppWindows[app].size() > 1) {
            r3Clusters.append({app, ang, remainingAppWindows[app].mid(1)});
        }
    }

    for (int k = placedCount; k < overflowApps.size(); ++k) {
        r3OverflowApps.append(overflowApps[k]);
    }

    // Segmente nach Winkel aufsteigend sortieren für gleichmäßige Zeigerführung
    std::sort(r2OverflowSegs.begin(), r2OverflowSegs.end(), [](const OverlaySegment &a, const OverlaySegment &b) {
        return a.angle < b.angle;
    });
    outRing2.append(r2OverflowSegs);

    // 5. Ring 3: Kaskadierte Instanzen und weiterer Überlauf
    const int r3Outer = r2Outer + ring3Gap() + ring3Depth();
    const double gapAng3 = (double)segGap() / r3Outer;
    // Bogenbreite in Pixeln bleibt starr bei segmentWidth() am Außenrand:
    const double segAng3 = segmentWidth() / r3Outer;
    const double step3 = segAng3 + gapAng3;
    const double clusterGapAng3 = (8.0 * currentScale()) / r3Outer;

    QList<Exclusion> r3Exclusions;

    // Ring-3-Cluster nach Ankerwinkel von links nach rechts sortieren
    std::sort(r3Clusters.begin(), r3Clusters.end(), [](const CascadedCluster &a, const CascadedCluster &b) {
        return a.anchorAngle < b.anchorAngle;
    });

    const int M3 = r3Clusters.size();
    QList<double> centers3(M3);
    QList<double> halfClusters3(M3);
    for (int i = 0; i < M3; ++i) {
        int C = r3Clusters[i].windows.size();
        halfClusters3[i] = ((C - 1) * step3 + segAng3) / 2.0;
        centers3[i] = r3Clusters[i].anchorAngle;
    }

    if (M3 > 0) {
        for (int iter = 0; iter < 10; ++iter) {
            for (int i = 0; i + 1 < M3; ++i) {
                double reqDist = halfClusters3[i] + clusterGapAng3 + halfClusters3[i+1];
                double curDist = centers3[i+1] - centers3[i];
                if (curDist < reqDist) {
                    double overlap = reqDist - curDist;
                    centers3[i] -= overlap * 0.5;
                    centers3[i+1] += overlap * 0.5;
                }
            }
            if (centers3[0] - halfClusters3[0] < arcMin) {
                centers3[0] = arcMin + halfClusters3[0];
            }
            if (centers3[M3-1] + halfClusters3[M3-1] > arcMax) {
                centers3[M3-1] = arcMax - halfClusters3[M3-1];
            }
        }
        for (int i = 1; i < M3; ++i) {
            double minPos = centers3[i-1] + halfClusters3[i-1] + clusterGapAng3 + halfClusters3[i];
            if (centers3[i] < minPos) centers3[i] = minPos;
        }
        if (centers3[M3-1] + halfClusters3[M3-1] > arcMax) {
            centers3[M3-1] = arcMax - halfClusters3[M3-1];
        }
        for (int i = M3 - 2; i >= 0; --i) {
            double maxPos = centers3[i+1] - halfClusters3[i+1] - clusterGapAng3 - halfClusters3[i];
            if (centers3[i] > maxPos) centers3[i] = maxPos;
        }
        if (centers3[0] - halfClusters3[0] < arcMin) {
            centers3[0] = arcMin + halfClusters3[0];
            for (int i = 1; i < M3; ++i) {
                double minPos = centers3[i-1] + halfClusters3[i-1] + clusterGapAng3 + halfClusters3[i];
                if (centers3[i] < minPos) centers3[i] = minPos;
            }
        }
    }

    for (int i = 0; i < M3; ++i) {
        const auto &cluster = r3Clusters[i];
        QList<WindowInfo> wins = cluster.windows;
        std::sort(wins.begin(), wins.end(), [&](const WindowInfo &a, const WindowInfo &b) {
            return getMruIdx(a) > getMruIdx(b);
        });
        int C = wins.size();
        if (C == 0) continue;

        double center3 = centers3[i];
        double cMin = 999.0;
        double cMax = -999.0;
        for (int k = 0; k < C; ++k) {
            double ang = center3 - (C - 1) * step3 / 2.0 + k * step3;
            OverlaySegment seg;
            seg.windowId = wins[k].id;
            seg.appKey = cluster.app;
            seg.resourceClass = wins[k].resourceClass;
            seg.desktopFileName = wins[k].desktopFileName;
            seg.caption = wins[k].caption;
            seg.angle = ang;
            seg.span = segAng3;
            seg.ring = 3;
            seg.tooltipText = formatWindowTooltip(resolveAppName(wins[k].desktopFileName, wins[k].resourceClass), wins[k].caption, wins[k].resourceClass);
            seg.segmentType = QStringLiteral("window");
            seg.isRealWindow = true;
            configurePinStatus(seg, wins[k]);
            outRing3.append(seg);

            if (ang < cMin) cMin = ang;
            if (ang > cMax) cMax = ang;
        }

        Exclusion excl;
        excl.minAngle = cMin - segAng3 / 2.0 - clusterGapAng3;
        excl.maxAngle = cMax + segAng3 / 2.0 + clusterGapAng3;
        r3Exclusions.append(excl);
    }

    // Falls noch Apps auf Ring 3 überlaufen müssen: Herumfließen um Ring-3-Cluster
    if (!r3OverflowApps.isEmpty()) {
        std::sort(r3Exclusions.begin(), r3Exclusions.end(), [](const Exclusion &a, const Exclusion &b) {
            return a.minAngle < b.minAngle;
        });

        QList<Exclusion> mergedExcl3;
        for (const auto &excl : r3Exclusions) {
            if (mergedExcl3.isEmpty()) {
                mergedExcl3.append(excl);
            } else {
                if (excl.minAngle <= mergedExcl3.last().maxAngle) {
                    mergedExcl3.last().maxAngle = std::max(mergedExcl3.last().maxAngle, excl.maxAngle);
                } else {
                    mergedExcl3.append(excl);
                }
            }
        }

        struct FreeInterval3 {
            double start;
            double end;
            int capacity = 0;
            double distTo12 = 0.0;
            bool contains12 = false;
        };
        QList<FreeInterval3> freeIntervals3;
        double curAng3 = arcMin;
        for (const auto &excl : mergedExcl3) {
            if (excl.minAngle > curAng3) {
                double s = curAng3;
                double e = std::min(arcMax, excl.minAngle);
                if (e - s >= segAng3) {
                    freeIntervals3.append({s, e});
                }
            }
            curAng3 = std::max(curAng3, excl.maxAngle);
        }
        if (curAng3 < arcMax && arcMax - curAng3 >= segAng3) {
            freeIntervals3.append({curAng3, arcMax});
        }

        const double angle12 = -M_PI / 2.0;
        for (auto &iv : freeIntervals3) {
            double span = iv.end - iv.start;
            iv.capacity = std::max(0, (int)std::floor((span + gapAng3) / step3));
            iv.contains12 = (iv.start <= angle12 && angle12 <= iv.end);
            if (iv.contains12) {
                iv.distTo12 = 0.0;
            } else if (iv.end < angle12) {
                iv.distTo12 = angle12 - iv.end;
            } else {
                iv.distTo12 = iv.start - angle12;
            }
        }

        for (int i = freeIntervals3.size() - 1; i >= 0; --i) {
            if (freeIntervals3[i].capacity <= 0) {
                freeIntervals3.removeAt(i);
            }
        }

        QVector<int> assignedCount3(freeIntervals3.size(), 0);
        int remainingToPlace3 = r3OverflowApps.size();
        while (remainingToPlace3 > 0) {
            int bestIdx = -1;
            double bestDist = 1e9;
            for (int i = 0; i < freeIntervals3.size(); ++i) {
                if (assignedCount3[i] < freeIntervals3[i].capacity) {
                    double effDist = freeIntervals3[i].distTo12 + assignedCount3[i] * step3;
                    if (effDist < bestDist) {
                        bestDist = effDist;
                        bestIdx = i;
                    }
                }
            }
            if (bestIdx == -1) break;
            assignedCount3[bestIdx]++;
            remainingToPlace3--;
        }

        struct AssignedSlot3 {
            double angle;
            double distTo12;
        };
        QList<AssignedSlot3> assignedSlots3;
        for (int i = 0; i < freeIntervals3.size(); ++i) {
            int m = assignedCount3[i];
            if (m <= 0) continue;
            const auto &iv = freeIntervals3[i];

            if (iv.contains12) {
                double startAng = angle12 - (m - 1) * step3 / 2.0;
                if (startAng - segAng3 / 2.0 < iv.start) startAng = iv.start + segAng3 / 2.0;
                if (startAng + (m - 1) * step3 + segAng3 / 2.0 > iv.end) startAng = iv.end - segAng3 / 2.0 - (m - 1) * step3;
                for (int k = 0; k < m; ++k) {
                    double a = startAng + k * step3;
                    assignedSlots3.append({a, std::abs(a - angle12)});
                }
            } else if (iv.end <= angle12) {
                double rightEdge = iv.end - segAng3 / 2.0;
                double startAng = rightEdge - (m - 1) * step3;
                for (int k = 0; k < m; ++k) {
                    double a = startAng + k * step3;
                    assignedSlots3.append({a, std::abs(a - angle12)});
                }
            } else {
                double leftEdge = iv.start + segAng3 / 2.0;
                for (int k = 0; k < m; ++k) {
                    double a = leftEdge + k * step3;
                    assignedSlots3.append({a, std::abs(a - angle12)});
                }
            }
        }

        std::sort(assignedSlots3.begin(), assignedSlots3.end(), [](const AssignedSlot3 &a, const AssignedSlot3 &b) {
            return a.distTo12 < b.distTo12;
        });

        int placedCount3 = std::min((int)assignedSlots3.size(), (int)r3OverflowApps.size());
        QList<OverlaySegment> r3OverflowSegs;
        for (int k = 0; k < placedCount3; ++k) {
            const QString &app = r3OverflowApps[k];
            double ang = assignedSlots3[k].angle;
            WindowInfo win = remainingAppWindows[app].first();
            OverlaySegment seg;
            seg.windowId = win.id;
            seg.appKey = app;
            seg.resourceClass = win.resourceClass;
            seg.desktopFileName = win.desktopFileName;
            seg.caption = win.caption;
            seg.angle = ang;
            seg.span = segAng3;
            seg.ring = 3;
            seg.tooltipText = formatWindowTooltip(resolveAppName(win.desktopFileName, win.resourceClass), win.caption, win.resourceClass);
            seg.segmentType = QStringLiteral("window");
            seg.isRealWindow = true;
            configurePinStatus(seg, win);
            r3OverflowSegs.append(seg);
        }

        std::sort(r3OverflowSegs.begin(), r3OverflowSegs.end(), [](const OverlaySegment &a, const OverlaySegment &b) {
            return a.angle < b.angle;
        });
        outRing3.append(r3OverflowSegs);
    }
}

void CircleOverlay::appendMediaSegments(QList<OverlaySegment> &outRing1)
{
    if (!m_showMediaControlsEnabled || !m_mediaManager.hasActiveStream()) {
        return;
    }

    const MediaInfo &media = m_mediaManager.currentMedia();

    // Total arc: 90 degrees = pi / 2 radians, centered at 6 o'clock (+pi / 2)
    // Range: pi / 4 (45 deg) to 3 * pi / 4 (135 deg)
    const double totalArc = M_PI / 2.0;
    const double rMid = (hubRadius() + ringGap()) + ringDepth() / 2.0;
    const double gapAng = static_cast<double>(segGap()) / rMid;
    const double segSpan = (totalArc - 2.0 * gapAng) / 3.0;
    const double step = segSpan + gapAng;
    const double centerAngle = M_PI / 2.0;

    // 1. Previous Track (left, x < 0, angle = centerAngle + step)
    OverlaySegment prevSeg;
    prevSeg.windowId = QStringLiteral("__MEDIA_PREV__");
    prevSeg.appKey = QStringLiteral("__MEDIA_PREV__");
    prevSeg.resourceClass = QStringLiteral("media-previous");
    prevSeg.desktopFileName = QStringLiteral("__MEDIA_PREV__");
    prevSeg.caption = QStringLiteral("Previous");
    prevSeg.angle = centerAngle + step;
    prevSeg.span = segSpan;
    prevSeg.ring = 1;
    prevSeg.segmentType = QStringLiteral("media");
    prevSeg.isRealWindow = false;
    prevSeg.canPin = false;
    prevSeg.isPinned = false;
    prevSeg.tooltipText = media.appName.isEmpty() 
        ? QStringLiteral("Previous Track")
        : QStringLiteral("Previous Track (%1)").arg(media.appName);

    // 2. Play / Pause (center at 6 o'clock, angle = centerAngle)
    OverlaySegment playPauseSeg;
    playPauseSeg.windowId = QStringLiteral("__MEDIA_PLAYPAUSE__");
    playPauseSeg.appKey = QStringLiteral("__MEDIA_PLAYPAUSE__");
    playPauseSeg.resourceClass = media.isPlaying ? QStringLiteral("media-pause") : QStringLiteral("media-play");
    playPauseSeg.desktopFileName = QStringLiteral("__MEDIA_PLAYPAUSE__");
    playPauseSeg.caption = media.isPlaying ? QStringLiteral("Pause") : QStringLiteral("Play");
    playPauseSeg.angle = centerAngle;
    playPauseSeg.span = segSpan;
    playPauseSeg.ring = 1;
    playPauseSeg.segmentType = QStringLiteral("media");
    playPauseSeg.isRealWindow = false;
    playPauseSeg.canPin = false;
    playPauseSeg.isPinned = false;
    QString displayTitle = media.trackTitle;
    if (!media.appName.isEmpty() && !displayTitle.isEmpty()) {
        displayTitle = QStringLiteral("%1 - %2").arg(media.appName, displayTitle);
    } else if (displayTitle.isEmpty()) {
        displayTitle = media.appName.isEmpty() 
            ? (media.isPlaying ? QStringLiteral("Pause") : QStringLiteral("Play"))
            : media.appName;
    }
    playPauseSeg.tooltipText = displayTitle;

    // 3. Next Track (right, x > 0, angle = centerAngle - step)
    OverlaySegment nextSeg;
    nextSeg.windowId = QStringLiteral("__MEDIA_NEXT__");
    nextSeg.appKey = QStringLiteral("__MEDIA_NEXT__");
    nextSeg.resourceClass = QStringLiteral("media-next");
    nextSeg.desktopFileName = QStringLiteral("__MEDIA_NEXT__");
    nextSeg.caption = QStringLiteral("Next");
    nextSeg.angle = centerAngle - step;
    nextSeg.span = segSpan;
    nextSeg.ring = 1;
    nextSeg.segmentType = QStringLiteral("media");
    nextSeg.isRealWindow = false;
    nextSeg.canPin = false;
    nextSeg.isPinned = false;
    nextSeg.tooltipText = media.appName.isEmpty() 
        ? QStringLiteral("Next Track")
        : QStringLiteral("Next Track (%1)").arg(media.appName);

    outRing1.append(prevSeg);
    outRing1.append(playPauseSeg);
    outRing1.append(nextSeg);
}

void CircleOverlay::populateSegments(const QList<WindowInfo> &windows, bool &outHasRing2, bool &outHasRing3)
{
    QList<OverlaySegment> ring1Segs;
    QList<OverlaySegment> ring2Segs;
    QList<OverlaySegment> ring3Segs;
    int centerIndex = 0;

    partitionWindows(windows, ring1Segs, centerIndex, ring2Segs, ring3Segs);
    appendMediaSegments(ring1Segs);

    outHasRing2 = !ring2Segs.isEmpty();
    outHasRing3 = !ring3Segs.isEmpty();

    QString tmpDir = QDir::tempPath() + QStringLiteral("/karyon_icons");
    QDir().mkpath(tmpDir);

    auto prepareList = [&](const QList<OverlaySegment> &list) {
        QVariantList result;
        for (const auto &seg : list) {
            QString iconPath;
            if (seg.isTextSegment) {
                // Textsegment besitzt kein Icon
            } else if (seg.appKey == QLatin1String("__SHOW_DESKTOP__")) {
                iconPath = KaryonGlyphs::ensureGlyphCached(QStringLiteral("show-desktop"), tmpDir, ICON_SIZE);
            } else if (seg.appKey == QLatin1String("__APP_MENU__")) {
                iconPath = KaryonGlyphs::ensureGlyphCached(QStringLiteral("app-menu"), tmpDir, ICON_SIZE);
            } else if (seg.appKey == QLatin1String("__FAVORITES__")) {
                iconPath = KaryonGlyphs::ensureGlyphCached(QStringLiteral("favorites"), tmpDir, ICON_SIZE);
            } else if (seg.appKey == QLatin1String("__TRAY__")) {
                iconPath = KaryonGlyphs::ensureGlyphCached(QStringLiteral("tray"), tmpDir, ICON_SIZE);
            } else if (seg.appKey == QLatin1String("__MEDIA_PREV__")) {
                iconPath = KaryonGlyphs::ensureGlyphCached(QStringLiteral("media-previous"), tmpDir, ICON_SIZE);
            } else if (seg.appKey == QLatin1String("__MEDIA_PLAYPAUSE__")) {
                iconPath = KaryonGlyphs::ensureGlyphCached(seg.resourceClass, tmpDir, ICON_SIZE);
            } else if (seg.appKey == QLatin1String("__MEDIA_NEXT__")) {
                iconPath = KaryonGlyphs::ensureGlyphCached(QStringLiteral("media-next"), tmpDir, ICON_SIZE);
            } else if (seg.appKey.startsWith(QLatin1String("file:"))) {
                QString key = seg.appKey;
                iconPath = m_iconPathCache.value(key);
                if (iconPath.isEmpty() || !QFile::exists(iconPath)) {
                    QString filePath = key.mid(5);
                    QMimeDatabase mimeDb;
                    QMimeType mimeType = mimeDb.mimeTypeForFile(filePath);
                    QIcon icon = QIcon::fromTheme(mimeType.iconName());
                    if (icon.isNull()) icon = QIcon::fromTheme(mimeType.genericIconName());
                    if (icon.isNull()) icon = QIcon::fromTheme(QStringLiteral("text-x-generic"));

                    QString cleanKey = key;
                    cleanKey.replace(QLatin1Char('/'), QLatin1Char('_')).replace(QLatin1Char(':'), QLatin1Char('_')).replace(QLatin1Char('.'), QLatin1Char('_'));
                    iconPath = tmpDir + QStringLiteral("/") + cleanKey + QStringLiteral(".png");
                    if (!QFile::exists(iconPath)) {
                        QPixmap pix = icon.pixmap(ICON_SIZE, ICON_SIZE);
                        pix.save(iconPath, "PNG");
                    }
                    m_iconPathCache.insert(key, iconPath);
                }
            } else if (!seg.trayProgramKey.isEmpty() || seg.windowId.startsWith(QLatin1String("tray_prog|")) || seg.windowId.startsWith(QLatin1String("tray_prog:"))) {
                QString progKey = !seg.trayProgramKey.isEmpty() ? seg.trayProgramKey : seg.windowId.mid(10);
                for (const auto &prog : m_cachedTrayPrograms) {
                    if (QStringLiteral("%1|%2").arg(prog.service, prog.path) == progKey ||
                        QStringLiteral("%1:%2").arg(prog.service, prog.path) == progKey) {
                        QString cleanKey = QStringLiteral("tray_prog_") + prog.id;
                        cleanKey.replace(QLatin1Char('/'), QLatin1Char('_')).replace(QLatin1Char(':'), QLatin1Char('_'));
                        QString revKey = QStringLiteral("%1_v%2").arg(cleanKey, QString::number(m_trayIconRevision));
                        iconPath = tmpDir + QStringLiteral("/") + revKey + QStringLiteral(".png");
                        if (!QFile::exists(iconPath)) {
                            if (m_trayIconRevision > 0) {
                                QString oldRevKey = QStringLiteral("%1_v%2.png").arg(cleanKey, QString::number(m_trayIconRevision - 1));
                                QFile::remove(tmpDir + QStringLiteral("/") + oldRevKey);
                            }
                            QPixmap pix = prog.icon.pixmap(iconSize(), iconSize());
                            pix.save(iconPath, "PNG");
                        }
                        break;
                    }
                }
            } else {
                QString key = seg.appKey;
                iconPath = m_iconPathCache.value(key);
                if (iconPath.isEmpty() || !QFile::exists(iconPath)) {
                    QIcon icon = resolveWindowIcon(seg.desktopFileName, seg.resourceClass);
                    QString cleanKey = key;
                    cleanKey.replace(QLatin1Char('/'), QLatin1Char('_')).replace(QLatin1Char(':'), QLatin1Char('_'));
                    iconPath = tmpDir + QStringLiteral("/") + cleanKey + QStringLiteral(".png");
                    if (!QFile::exists(iconPath)) {
                        QPixmap pix = icon.pixmap(iconSize(), iconSize());
                        pix.save(iconPath, "PNG");
                    }
                    m_iconPathCache.insert(key, iconPath);
                }
            }

            QVariantMap m;
            m[QStringLiteral("isTextSegment")] = seg.isTextSegment;
            m[QStringLiteral("label")] = seg.label;
            m[QStringLiteral("iconSource")] = iconPath.isEmpty() ? QString() : (QStringLiteral("file://") + iconPath);
            m[QStringLiteral("windowId")] = seg.windowId;
            m[QStringLiteral("resourceClass")] = seg.resourceClass;
            m[QStringLiteral("appKey")] = seg.appKey;
            m[QStringLiteral("angle")] = seg.angle;
            m[QStringLiteral("span")] = seg.span;
            m[QStringLiteral("ring")] = seg.ring;
            m[QStringLiteral("tooltipText")] = seg.tooltipText;
            m[QStringLiteral("hasDrilldown")] = seg.hasDrilldown;
            m[QStringLiteral("segmentType")] = seg.segmentType;
            m[QStringLiteral("isRealWindow")] = seg.isRealWindow;
            m[QStringLiteral("canPin")] = seg.canPin;
            m[QStringLiteral("isPinned")] = seg.isPinned;
            m[QStringLiteral("progressPercent")] = seg.progressPercent;
            m[QStringLiteral("windowCount")] = seg.windowCount;
            m[QStringLiteral("groupWindowIds")] = seg.groupWindowIds;
            result.append(m);
        }
        return result;
    };

    assignSegmentProgress(ring1Segs, ring2Segs, ring3Segs);

    m_currentRing1 = ring1Segs;
    m_currentRing2 = ring2Segs;
    m_currentRing3 = ring3Segs;

    QVariantList segmentList1 = prepareList(ring1Segs);
    QVariantList segmentList2 = prepareList(ring2Segs);
    QVariantList segmentList3 = prepareList(ring3Segs);

    m_view->rootContext()->setContextProperty(QStringLiteral("segments"), segmentList1);
    m_view->rootContext()->setContextProperty(QStringLiteral("segments2"), segmentList2);
    m_view->rootContext()->setContextProperty(QStringLiteral("segments3"), segmentList3);
    m_view->rootContext()->setContextProperty(QStringLiteral("hasRing2"), outHasRing2);
    m_view->rootContext()->setContextProperty(QStringLiteral("hasRing3"), outHasRing3);
    m_view->rootContext()->setContextProperty(QStringLiteral("centerIndex"), centerIndex);
    updateScaleProperties();
    m_view->rootContext()->setContextProperty(QStringLiteral("categoryMode"), QStringLiteral("windows"));

    if (m_view->rootObject()) {
        m_view->rootObject()->setProperty("categoryMode", QStringLiteral("windows"));
        QMetaObject::invokeMethod(m_view->rootObject(), "setSegmentLists",
                                  Q_ARG(QVariant, QVariant::fromValue(segmentList1)),
                                  Q_ARG(QVariant, QVariant::fromValue(segmentList2)),
                                  Q_ARG(QVariant, QVariant::fromValue(segmentList3)),
                                  Q_ARG(QVariant, outHasRing2),
                                  Q_ARG(QVariant, outHasRing3));
        // Auswahl nach dem Neuaufbau aus QML zurücklesen, damit C++ nie mit veralteten Indizes arbeitet
        m_selectedRing = m_view->rootObject()->property("selectedRing").toInt();
        m_selectedIndex = m_view->rootObject()->property("selectedIndex").toInt();
    }
}

void CircleOverlay::partitionRecentApps(QList<OverlaySegment> &outRing1,
                                       int &outRing1CenterIndex,
                                       QList<OverlaySegment> &outRing2,
                                       QList<OverlaySegment> &outRing3)
{
    outRing1.clear();
    outRing2.clear();
    outRing3.clear();

    int maxApps = SettingsManager::instance().maxRecentApps();
    m_cachedRecentApps = m_appManager.loadRecentApps(maxApps, openAppIdentifiers());
    if (m_cachedRecentApps.size() > maxApps) {
        m_cachedRecentApps = m_cachedRecentApps.mid(0, maxApps);
    }

    const double availArc = 1.5 * M_PI; // 270° Bogen (-225° bis +45°, zentriert um -90°)
    const int r1Outer = hubRadius() + ringGap() + ringDepth();
    const double gapAng1 = (double)segGap() / r1Outer;
    const double maxSegAng1 = segmentWidth() / r1Outer;

    const int r1Count = 11;
    double segAng1 = (availArc - (r1Count - 1) * gapAng1) / r1Count;
    if (segAng1 > maxSegAng1) segAng1 = maxSegAng1;
    const double step1 = segAng1 + gapAng1;

    // 11 Segmente auf Ring 1:
    // Index 5: App-Menu (12 Uhr / -90°)
    // Index 4: Tray (unmittelbar links von App-Menu)
    // Index 6: Recent App 0
    // Index 7: Recent App 1
    // Index 8: Recent App 2
    // Index 9: Recent App 3
    // Index 10: Recent App 4
    // Index 3: Recent App 5
    // Index 2: Recent App 6
    // Index 1: Recent App 7
    // Index 0: Recent App 8
    outRing1CenterIndex = 5;

    auto makeAppSeg = [&](const AppEntry &app, double angle, double span, int ring) {
        OverlaySegment seg;
        seg.windowId = QStringLiteral("app:") + app.exec;
        seg.appKey = QStringLiteral("app:") + app.id;
        seg.resourceClass = app.icon;
        seg.desktopFileName = app.desktopFile;
        seg.caption = app.name;
        seg.angle = angle;
        seg.span = span;
        seg.ring = ring;
        seg.tooltipText = app.name;
        seg.segmentType = QStringLiteral("app");
        seg.isRealWindow = false;
        seg.canPin = true;
        seg.isPinned = m_pinnedManager.isAppPinned(app.id, app.desktopFile);
        return seg;
    };

    // Welche Slots sind durch Spezialsegmente belegt?
    bool slotHasSpecial[11] = {false};
    if (m_showMenuEnabled) slotHasSpecial[5] = true;
    else if (m_showFavoritesEnabled) slotHasSpecial[5] = true;
    if (m_showTrayEnabled) slotHasSpecial[4] = true;
    if (m_showSessionEnabled) slotHasSpecial[10] = true;

    // Reihenfolge freier Slots für Recent Apps:
    // Wenn weder Menu noch Favorites aktiv sind, startet App 0 direkt bei 12 Uhr (Slot 5).
    // Ansonsten breiten sich Apps von 12 Uhr nach rechts (6, 7, 8, 9, 10)
    // und dann nach links (4, 3, 2, 1, 0) aus.
    QList<int> appSlotOrder;
    if (!slotHasSpecial[5]) appSlotOrder.append(5);
    if (!slotHasSpecial[6]) appSlotOrder.append(6);
    if (!slotHasSpecial[7]) appSlotOrder.append(7);
    if (!slotHasSpecial[8]) appSlotOrder.append(8);
    if (!slotHasSpecial[9]) appSlotOrder.append(9);
    if (!slotHasSpecial[10]) appSlotOrder.append(10);
    if (!slotHasSpecial[4]) appSlotOrder.append(4);
    if (!slotHasSpecial[3]) appSlotOrder.append(3);
    if (!slotHasSpecial[2]) appSlotOrder.append(2);
    if (!slotHasSpecial[1]) appSlotOrder.append(1);
    if (!slotHasSpecial[0]) appSlotOrder.append(0);

    QHash<int, OverlaySegment> slotSegments;
    int r1AppCount = std::min((int)appSlotOrder.size(), (int)m_cachedRecentApps.size());
    for (int a = 0; a < r1AppCount; ++a) {
        int slot = appSlotOrder[a];
        double angle = -M_PI / 2.0 + (slot - outRing1CenterIndex) * step1;
        slotSegments[slot] = makeAppSeg(m_cachedRecentApps[a], angle, segAng1, 1);
    }

    if (m_showMenuEnabled) {
        double angle = -M_PI / 2.0 + (5 - outRing1CenterIndex) * step1;
        OverlaySegment seg;
        seg.windowId = QStringLiteral("__APP_MENU__");
        seg.appKey = QStringLiteral("__APP_MENU__");
        seg.resourceClass = QStringLiteral("app-menu");
        seg.caption = QStringLiteral("App Menu");
        seg.tooltipText = QStringLiteral("App Menu");
        seg.angle = angle;
        seg.span = segAng1;
        seg.ring = 1;
        seg.hasDrilldown = true;
        slotSegments[5] = seg;
    } else if (m_showFavoritesEnabled) {
        double angle = -M_PI / 2.0 + (5 - outRing1CenterIndex) * step1;
        OverlaySegment seg;
        seg.windowId = QStringLiteral("__FAVORITES__");
        seg.appKey = QStringLiteral("__FAVORITES__");
        seg.resourceClass = QStringLiteral("favorites");
        seg.caption = QStringLiteral("Favorites");
        seg.tooltipText = QStringLiteral("Favorites");
        seg.angle = angle;
        seg.span = segAng1;
        seg.ring = 1;
        seg.hasDrilldown = true;
        slotSegments[5] = seg;
    }

    if (m_showTrayEnabled) {
        double angle = -M_PI / 2.0 + (4 - outRing1CenterIndex) * step1;
        OverlaySegment seg;
        seg.windowId = QStringLiteral("__TRAY__");
        seg.appKey = QStringLiteral("__TRAY__");
        seg.resourceClass = QStringLiteral("tray");
        seg.caption = QStringLiteral("Tray");
        seg.tooltipText = QStringLiteral("Tray");
        seg.angle = angle;
        seg.span = segAng1;
        seg.ring = 1;
        seg.hasDrilldown = true;
        slotSegments[4] = seg;
    }

    if (m_showSessionEnabled) {
        double angle = -M_PI / 2.0 + (10 - outRing1CenterIndex) * step1;
        OverlaySegment seg;
        seg.windowId = QStringLiteral("__SESSION__");
        seg.appKey = QStringLiteral("session");
        seg.resourceClass = QStringLiteral("session");
        seg.desktopFileName = QStringLiteral("__SESSION__");
        seg.caption = QStringLiteral("Session");
        seg.tooltipText = QStringLiteral("Session");
        seg.angle = angle;
        seg.span = segAng1;
        seg.ring = 1;
        seg.hasDrilldown = true;
        slotSegments[10] = seg;
    }

    for (int i = 0; i < r1Count; ++i) {
        if (slotSegments.contains(i)) {
            outRing1.append(slotSegments[i]);
        }
    }

    // Verbleibende Recent Apps auf Ring 2 (bis zu 10 Apps)
    int r1MaxApps = r1AppCount;
    int r2MaxApps = r1MaxApps + 10;
    QList<AppEntry> r2Apps;
    for (int idx = r1MaxApps; idx < m_cachedRecentApps.size() && idx < r2MaxApps; ++idx) {
        r2Apps.append(m_cachedRecentApps[idx]);
    }

    if (!r2Apps.isEmpty() && !outRing1.isEmpty()) {
        const int r2Outer = r1Outer + ring2Gap() + ring2Depth();
        const double gapAng2 = (double)segGap() / r2Outer;
        const double segAng2 = segmentWidth() / r2Outer;

        // Erfasse alle Drilldown-Winkelbereiche in Ring 1 (z. B. Menu, Favorites, Tray, Session)
        QList<QPair<double, double>> drilldownIntervals;
        const OverlaySegment *topDrillLeft = nullptr;
        const OverlaySegment *topDrillRight = nullptr;

        for (const auto &s : outRing1) {
            if (s.hasDrilldown) {
                double aMin = s.angle - s.span / 2.0;
                double aMax = s.angle + s.span / 2.0;
                drilldownIntervals.append({aMin, aMax});

                // Oberer Drilldown-Bereich am Scheitelpunkt (zwischen -180° und 0°)
                if (s.angle >= -M_PI && s.angle <= 0.0) {
                    if (!topDrillLeft || s.angle < topDrillLeft->angle) {
                        topDrillLeft = &s;
                    }
                    if (!topDrillRight || s.angle > topDrillRight->angle) {
                        topDrillRight = &s;
                    }
                }
            }
        }

        auto overlapsDrilldown = [&](double angle, double span) {
            double segMin = angle - span / 2.0;
            double segMax = angle + span / 2.0;
            for (const auto &iv : drilldownIntervals) {
                if (segMax > iv.first + 1e-4 && segMin < iv.second - 1e-4) {
                    return true;
                }
            }
            return false;
        };

        auto generateSlots = [&](int ringOuter, double segAng, double gapAng, int countNeeded) -> QList<double> {
            const double step = segAng + gapAng;
            QList<double> resultSlots;
            if (countNeeded <= 0) return resultSlots;

            // Bogen-Winkelgrenzen anhand von Ring 1
            double minArcAngle = outRing1.first().angle - outRing1.first().span / 2.0 - segAng;
            double maxArcAngle = outRing1.last().angle + outRing1.last().span / 2.0 + segAng;

            if (!topDrillLeft && !topDrillRight) {
                // Kein oberer Drilldown vorhanden: klassisch symmetrisch um 12 Uhr zentrieren
                const double angle12 = -M_PI / 2.0;
                double startAng = angle12 - (countNeeded - 1) * step / 2.0;
                for (int k = 0; k < countNeeded; ++k) {
                    double a = startAng + k * step;
                    if (!overlapsDrilldown(a, segAng)) {
                        resultSlots.append(a);
                    }
                }
                return resultSlots;
            }

            // Oberer Drilldown vorhanden: Beidseitig von den Außenkanten der oberen Drilldowns
            // nach außen/unten wachsen, Drilldowns vollständig aussparen
            double rightStart = (topDrillRight ? (topDrillRight->angle + topDrillRight->span / 2.0) : (-M_PI / 2.0)) + gapAng + segAng / 2.0;
            double leftStart = (topDrillLeft ? (topDrillLeft->angle - topDrillLeft->span / 2.0) : (-M_PI / 2.0)) - gapAng - segAng / 2.0;

            QList<double> rightWing;
            for (int k = 0; k < 15; ++k) {
                double a = rightStart + k * step;
                if (a + segAng / 2.0 > maxArcAngle) break;
                if (!overlapsDrilldown(a, segAng)) {
                    rightWing.append(a);
                }
            }

            QList<double> leftWing;
            for (int k = 0; k < 15; ++k) {
                double a = leftStart - k * step;
                if (a - segAng / 2.0 < minArcAngle) break;
                if (!overlapsDrilldown(a, segAng)) {
                    leftWing.append(a);
                }
            }

            // Gleichmäßig alternierend von der Mitte aus nach rechts und links zuteilen
            int rIdx = 0, lIdx = 0;
            while (resultSlots.size() < countNeeded && (rIdx < rightWing.size() || lIdx < leftWing.size())) {
                if (rIdx < rightWing.size()) {
                    resultSlots.append(rightWing[rIdx++]);
                    if (resultSlots.size() >= countNeeded) break;
                }
                if (lIdx < leftWing.size()) {
                    resultSlots.append(leftWing[lIdx++]);
                }
            }

            return resultSlots;
        };

        QList<double> r2Slots = generateSlots(r2Outer, segAng2, gapAng2, r2Apps.size());
        int r2PlaceCount = std::min(r2Slots.size(), r2Apps.size());
        for (int k = 0; k < r2PlaceCount; ++k) {
            outRing2.append(makeAppSeg(r2Apps[k], r2Slots[k], segAng2, 2));
        }

        // Segmente auf Ring 2 von links nach rechts sortieren
        std::sort(outRing2.begin(), outRing2.end(), [](const OverlaySegment &a, const OverlaySegment &b) {
            return a.angle < b.angle;
        });

        // Verbleibende Apps kaskadieren auf Ring 3
        QList<AppEntry> r3Apps;
        for (int idx = r1MaxApps + r2PlaceCount; idx < m_cachedRecentApps.size(); ++idx) {
            r3Apps.append(m_cachedRecentApps[idx]);
        }
        if (!r3Apps.isEmpty()) {
            const int r3Outer = r2Outer + ring3Gap() + ring3Depth();
            const double gapAng3 = (double)segGap() / r3Outer;
            const double segAng3 = segmentWidth() / r3Outer;

            QList<double> r3Slots = generateSlots(r3Outer, segAng3, gapAng3, r3Apps.size());
            int r3PlaceCount = std::min(r3Slots.size(), r3Apps.size());
            for (int k = 0; k < r3PlaceCount; ++k) {
                outRing3.append(makeAppSeg(r3Apps[k], r3Slots[k], segAng3, 3));
            }

            std::sort(outRing3.begin(), outRing3.end(), [](const OverlaySegment &a, const OverlaySegment &b) {
                return a.angle < b.angle;
            });
        }
    }
}

void CircleOverlay::partitionSession(QList<OverlaySegment> &outRing1, QList<OverlaySegment> &outRing2)
{
    int dummyCenter = 0;
    QList<OverlaySegment> dummyRing2;
    QList<OverlaySegment> dummyRing3;
    partitionRecentApps(outRing1, dummyCenter, dummyRing2, dummyRing3);


    outRing2.clear();

    double sessionAngle = 0.0;
    bool foundSession = false;
    for (const auto &seg : outRing1) {
        if (seg.windowId == QLatin1String("__SESSION__")) {
            sessionAngle = seg.angle;
            foundSession = true;
            break;
        }
    }
    if (!foundSession) return;

    const int r1Outer = hubRadius() + ringGap() + ringDepth();
    const int r2Outer = r1Outer + ring2Gap() + ring2Depth();
    const double gapAng2 = (double)segGap() / r2Outer;
    const double segAng2 = segmentWidth() / r2Outer;
    const double step2 = segAng2 + gapAng2;

    struct SessionActionDef {
        QString actionId;
        QString glyph;
        QString caption;
        QString tooltip;
    };

    QList<SessionActionDef> actions = {
        { QStringLiteral("session:lock"), QStringLiteral("session-lock"), QStringLiteral("Lock"), QStringLiteral("Lock") },
        { QStringLiteral("session:switch_user"), QStringLiteral("session-switch-user"), QStringLiteral("Switch User"), QStringLiteral("Switch User") },
        { QStringLiteral("session:logout"), QStringLiteral("session-logout"), QStringLiteral("Log Out"), QStringLiteral("Log Out") },
        { QStringLiteral("session:suspend"), QStringLiteral("session-suspend"), QStringLiteral("Sleep"), QStringLiteral("Sleep") },
        { QStringLiteral("session:reboot"), QStringLiteral("session-reboot"), QStringLiteral("Restart"), QStringLiteral("Restart") },
        { QStringLiteral("session:shutdown"), QStringLiteral("session-shutdown"), QStringLiteral("Shut Down"), QStringLiteral("Shut Down") }
    };

    for (int k = 0; k < actions.size(); ++k) {
        double angle = sessionAngle - (actions.size() - 1 - k) * step2;
        OverlaySegment seg;
        seg.windowId = actions[k].actionId;
        seg.appKey = actions[k].glyph;
        seg.resourceClass = actions[k].glyph;
        seg.desktopFileName = actions[k].actionId;
        seg.caption = actions[k].caption;
        seg.tooltipText = actions[k].tooltip;
        seg.angle = angle;
        seg.span = segAng2;
        seg.ring = 2;
        seg.hasDrilldown = false;
        seg.segmentType = QStringLiteral("session");
        outRing2.append(seg);
    }
}

void CircleOverlay::partitionRecentFiles(QList<OverlaySegment> &outRing1,
                                         int &outRing1CenterIndex,
                                         QList<OverlaySegment> &outRing2,
                                         QList<OverlaySegment> &outRing3)
{
    outRing1.clear();
    outRing2.clear();
    outRing3.clear();

    int maxFiles = SettingsManager::instance().maxRecentFiles();
    if (m_cachedRecentFiles.isEmpty() || m_cachedRecentFiles.size() > maxFiles) {
        m_cachedRecentFiles = m_recentFilesManager.loadRecentFiles(maxFiles);
    }
    if (m_cachedRecentFiles.isEmpty()) return;

    int totalFiles = std::min(static_cast<int>(m_cachedRecentFiles.size()), maxFiles);
    int r1Count = std::min(totalFiles, 11);
    if (r1Count % 2 == 0 && r1Count > 1) {
        r1Count--;
    }
    if (r1Count == 0) r1Count = 1;

    const double angle12 = -M_PI / 2.0;
    const double availArc = 1.5 * M_PI; // 270° Bogen (-225° bis +45°, zentriert um -90°)
    const int r1Outer = hubRadius() + ringGap() + ringDepth();
    const double gapAng1 = (double)segGap() / r1Outer;
    const double maxSegAng1 = segmentWidth() / r1Outer;

    double segAng1 = (availArc - (r1Count - 1) * gapAng1) / r1Count;
    if (segAng1 > maxSegAng1) segAng1 = maxSegAng1;
    const double step1 = segAng1 + gapAng1;

    outRing1CenterIndex = r1Count / 2;

    auto makeFileSeg = [&](const RecentFileItem &file, double angle, double span, int ring) {
        OverlaySegment seg;
        seg.windowId = QStringLiteral("file:") + file.path;
        seg.appKey = QStringLiteral("file:") + file.path;
        seg.resourceClass = QStringLiteral("file");
        seg.desktopFileName = QString();
        seg.caption = file.name;
        seg.angle = angle;
        seg.span = span;
        seg.ring = ring;
        seg.isTextSegment = false;
        seg.label = file.name;
        seg.hasDrilldown = false;
        seg.tooltipText = file.name;
        seg.segmentType = QStringLiteral("file");
        seg.isRealWindow = false;
        seg.canPin = true;
        seg.isPinned = m_pinnedManager.isFilePinned(file.path);
        return seg;
    };

    QVector<RecentFileItem> r1Slots(r1Count);
    r1Slots[outRing1CenterIndex] = m_cachedRecentFiles[0];
    int fileIdx = 1;
    for (int offset = 1; offset <= outRing1CenterIndex; ++offset) {
        if (outRing1CenterIndex + offset < r1Count && fileIdx < totalFiles) {
            r1Slots[outRing1CenterIndex + offset] = m_cachedRecentFiles[fileIdx++];
        }
        if (outRing1CenterIndex - offset >= 0 && fileIdx < totalFiles) {
            r1Slots[outRing1CenterIndex - offset] = m_cachedRecentFiles[fileIdx++];
        }
    }

    for (int i = 0; i < r1Count; ++i) {
        double angle = angle12 + (i - outRing1CenterIndex) * step1;
        outRing1.append(makeFileSeg(r1Slots[i], angle, segAng1, 1));
    }

    // Verbleibende Dateien auf Ring 2 (bis zu 10 Dateien)
    int r2Limit = std::min(totalFiles, fileIdx + 10);
    QList<RecentFileItem> r2Files;
    for (int idx = fileIdx; idx < r2Limit; ++idx) {
        r2Files.append(m_cachedRecentFiles[idx]);
    }

    // Dateien ab r2Limit kaskadieren auf Ring 3
    QList<RecentFileItem> r3Files;
    for (int idx = r2Limit; idx < totalFiles; ++idx) {
        r3Files.append(m_cachedRecentFiles[idx]);
    }

    if (!r2Files.isEmpty() && !outRing1.isEmpty()) {
        const int r2Outer = r1Outer + ring2Gap() + ring2Depth();
        const double gapAng2 = (double)segGap() / r2Outer;
        const double segAng2 = segmentWidth() / r2Outer;
        const double step2 = segAng2 + gapAng2;

        // Zentrierte Anordnung um 12 Uhr (-90°) analog zur Fensterkategorie
        int m2 = r2Files.size();
        double startAng2 = angle12 - (m2 - 1) * step2 / 2.0;

        struct AssignedSlot {
            double angle;
            double distTo12;
        };
        QList<AssignedSlot> r2Slots;
        for (int k = 0; k < m2; ++k) {
            double a = startAng2 + k * step2;
            r2Slots.append({a, std::abs(a - angle12)});
        }

        // Slots nach Nähe zu 12 Uhr sortieren (MRU: 12-Uhr-nah vor 12-Uhr-fern, rechts vor links)
        std::sort(r2Slots.begin(), r2Slots.end(), [](const AssignedSlot &a, const AssignedSlot &b) {
            if (std::abs(a.distTo12 - b.distTo12) > 1e-6) {
                return a.distTo12 < b.distTo12;
            }
            return a.angle > b.angle;
        });

        for (int k = 0; k < m2; ++k) {
            outRing2.append(makeFileSeg(r2Files[k], r2Slots[k].angle, segAng2, 2));
        }

        // Segmente auf Ring 2 von links nach rechts anordnen
        std::sort(outRing2.begin(), outRing2.end(), [](const OverlaySegment &a, const OverlaySegment &b) {
            return a.angle < b.angle;
        });
    }

    if (!r3Files.isEmpty() && !outRing1.isEmpty()) {
        const int r2Outer = r1Outer + ring2Gap() + ring2Depth();
        const int r3Outer = r2Outer + ring3Gap() + ring3Depth();
        const double gapAng3 = (double)segGap() / r3Outer;
        const double segAng3 = segmentWidth() / r3Outer;
        const double step3 = segAng3 + gapAng3;

        int m3 = r3Files.size();
        double startAng3 = angle12 - (m3 - 1) * step3 / 2.0;

        struct AssignedSlot {
            double angle;
            double distTo12;
        };
        QList<AssignedSlot> r3Slots;
        for (int k = 0; k < m3; ++k) {
            double a = startAng3 + k * step3;
            r3Slots.append({a, std::abs(a - angle12)});
        }

        std::sort(r3Slots.begin(), r3Slots.end(), [](const AssignedSlot &a, const AssignedSlot &b) {
            if (std::abs(a.distTo12 - b.distTo12) > 1e-6) {
                return a.distTo12 < b.distTo12;
            }
            return a.angle > b.angle;
        });

        for (int k = 0; k < m3; ++k) {
            outRing3.append(makeFileSeg(r3Files[k], r3Slots[k].angle, segAng3, 3));
        }

        std::sort(outRing3.begin(), outRing3.end(), [](const OverlaySegment &a, const OverlaySegment &b) {
            return a.angle < b.angle;
        });
    }
}

void CircleOverlay::partitionCategories(QList<OverlaySegment> &outRing1,
                                       int activeCatIndex,
                                       QList<OverlaySegment> &outRing2,
                                       QList<OverlaySegment> &outRing3)
{
    outRing1.clear();
    outRing2.clear();
    outRing3.clear();

    if (m_cachedCategories.isEmpty()) {
        m_cachedCategories = m_appManager.loadCategories();
    }
    if (m_cachedCategories.isEmpty()) return;

    // 230° Bogen (-205° bis +25°, zentriert um -90° / 12 Uhr)
    // Lässt links und rechts jeweils ca. 20° klaren Sicherheitsabstand zu den Medienbuttons bei 6 Uhr!
    const double availArc = (230.0 / 180.0) * M_PI;
    const int r1Outer = hubRadius() + ringGap() + ringDepth();
    const double gapAng1 = (double)segGap() / r1Outer;
    const double maxSegAng1 = segmentWidth() / r1Outer;

    const int totalCats = m_cachedCategories.size();
    const int MAX_CATS_SINGLE_PAGE = 13;
    const int MAX_CATS_WITH_MORE = 12;

    int startIndex = m_categoryPage * MAX_CATS_WITH_MORE;
    if (startIndex >= totalCats) {
        m_categoryPage = 0;
        startIndex = 0;
    }

    int remaining = totalCats - startIndex;
    bool hasMore = false;
    int numCatsOnPage = 0;

    if (remaining <= MAX_CATS_SINGLE_PAGE) {
        numCatsOnPage = remaining;
        hasMore = false;
    } else {
        numCatsOnPage = MAX_CATS_WITH_MORE; // 12
        hasMore = true;
    }

    int itemsOnPage = numCatsOnPage + (hasMore ? 1 : 0);

    // Segmentbreite und Schrittweite berechnen
    double segAng1 = (availArc - (itemsOnPage - 1) * gapAng1) / itemsOnPage;
    if (segAng1 > maxSegAng1) segAng1 = maxSegAng1;
    double step1 = segAng1 + gapAng1;

    // Segment bei 12 Uhr (-90° / 90° über Hub-Zentrum) zentrieren
    int centerIndex = (itemsOnPage - 1) / 2;

    for (int k = 0; k < numCatsOnPage; ++k) {
        int catIdx = startIndex + k;
        const auto &cat = m_cachedCategories[catIdx];
        double ang = -M_PI / 2.0 + (k - centerIndex) * step1;

        OverlaySegment seg;
        seg.windowId = QStringLiteral("cat:") + QString::number(catIdx);
        seg.appKey = QStringLiteral("cat:") + QString::number(catIdx);
        seg.resourceClass = cat.icon;
        seg.desktopFileName = QString();
        seg.caption = cat.name;
        seg.tooltipText = cat.name;
        seg.angle = ang;
        seg.span = segAng1;
        seg.ring = 1;
        seg.isTextSegment = true;
        seg.label = cat.name;
        outRing1.append(seg);
    }

    if (hasMore) {
        double ang = -M_PI / 2.0 + (numCatsOnPage - centerIndex) * step1;
        OverlaySegment moreSeg;
        moreSeg.windowId = QStringLiteral("__CAT_MORE__");
        moreSeg.appKey = QStringLiteral("more");
        moreSeg.resourceClass = QStringLiteral("more");
        moreSeg.desktopFileName = QStringLiteral("__CAT_MORE__");
        moreSeg.caption = QStringLiteral("More");
        moreSeg.tooltipText = QStringLiteral("Weitere Kategorien");
        moreSeg.angle = ang;
        moreSeg.span = segAng1;
        moreSeg.ring = 1;
        moreSeg.isTextSegment = false;
        moreSeg.hasDrilldown = true;
        outRing1.append(moreSeg);
    }

    // Aktive Kategorie für Ring 2 & 3 validieren
    if (activeCatIndex < startIndex || activeCatIndex >= startIndex + numCatsOnPage) {
        activeCatIndex = startIndex + (numCatsOnPage / 2);
    }
    m_activeCategoryIndex = activeCatIndex;

    const auto &apps = m_cachedCategories[activeCatIndex].apps;
    if (apps.isEmpty()) return;

    auto makeAppSeg = [&](const AppEntry &app, double angle, double span, int ring) {
        OverlaySegment seg;
        seg.windowId = QStringLiteral("app:") + app.exec;
        seg.appKey = QStringLiteral("app:") + app.id;
        seg.resourceClass = app.icon;
        seg.desktopFileName = app.desktopFile;
        seg.caption = app.name;
        seg.angle = angle;
        seg.span = span;
        seg.ring = ring;
        seg.tooltipText = app.name;
        seg.segmentType = QStringLiteral("app");
        seg.isRealWindow = false;
        seg.canPin = true;
        seg.isPinned = m_pinnedManager.isAppPinned(app.id, app.desktopFile);
        return seg;
    };

    double activeCatAngle = -M_PI / 2.0;
    const QString targetCatId = QStringLiteral("cat:") + QString::number(activeCatIndex);
    for (const auto &seg : outRing1) {
        if (seg.windowId == targetCatId) {
            activeCatAngle = seg.angle;
            break;
        }
    }

    int r2Count = std::min((int)apps.size(), 15);
    const int r2Outer = r1Outer + ring2Gap() + ring2Depth();
    const double gapAng2 = (double)segGap() / r2Outer;
    const double maxSegAng2 = segmentWidth() / r2Outer;

    double segAng2 = (availArc - (r2Count - 1) * gapAng2) / r2Count;
    if (segAng2 > maxSegAng2) segAng2 = maxSegAng2;
    double step2 = segAng2 + gapAng2;

    double r2Start = activeCatAngle - ((r2Count - 1) * step2) / 2.0;
    for (int k = 0; k < r2Count; ++k) {
        double angle = r2Start + k * step2;
        outRing2.append(makeAppSeg(apps[k], angle, segAng2, 2));
    }

    if (apps.size() > 15) {
        int r3Count = std::min((int)apps.size() - 15, 20);
        const int r3Outer = r2Outer + ring3Gap() + ring3Depth();
        const double gapAng3 = (double)segGap() / r3Outer;
        const double maxSegAng3 = segmentWidth() / r3Outer;

        double segAng3 = (availArc - (r3Count - 1) * gapAng3) / r3Count;
        if (segAng3 > maxSegAng3) segAng3 = maxSegAng3;
        double step3 = segAng3 + gapAng3;

        double r3Start = activeCatAngle - ((r3Count - 1) * step3) / 2.0;
        for (int k = 0; k < r3Count; ++k) {
            double angle = r3Start + k * step3;
            outRing3.append(makeAppSeg(apps[15 + k], angle, segAng3, 3));
        }
    }
}

void CircleOverlay::partitionFavorites(QList<OverlaySegment> &outRing1,
                                       QList<OverlaySegment> &outRing2,
                                       QList<OverlaySegment> &outRing3)
{
    int dummyCenter = 0;
    QList<OverlaySegment> dummyRing2;
    QList<OverlaySegment> dummyRing3;
    partitionRecentApps(outRing1, dummyCenter, dummyRing2, dummyRing3);

    outRing2.clear();
    outRing3.clear();

    if (m_cachedFavorites.isEmpty()) {
        m_cachedFavorites = m_appManager.loadFavorites();
    }
    if (m_cachedFavorites.isEmpty()) return;

    auto makeAppSeg = [&](const AppEntry &app, double angle, double span, int ring) {
        OverlaySegment seg;
        seg.windowId = QStringLiteral("app:") + app.exec;
        seg.appKey = QStringLiteral("app:") + app.id;
        seg.resourceClass = app.icon;
        seg.desktopFileName = app.desktopFile;
        seg.caption = app.name;
        seg.angle = angle;
        seg.span = span;
        seg.ring = ring;
        seg.tooltipText = app.name;
        seg.segmentType = QStringLiteral("app");
        seg.isRealWindow = false;
        seg.canPin = true;
        seg.isPinned = m_pinnedManager.isAppPinned(app.id, app.desktopFile);
        return seg;
    };

    const double availArc = (230.0 / 180.0) * M_PI;
    const int r1Outer = hubRadius() + ringGap() + ringDepth();
    const int r2Outer = r1Outer + ring2Gap() + ring2Depth();
    const double gapAng2 = (double)segGap() / r2Outer;
    const double maxSegAng2 = segmentWidth() / r2Outer;

    int r2Count = std::min((int)m_cachedFavorites.size(), 15);
    double segAng2 = (availArc - (r2Count - 1) * gapAng2) / r2Count;
    if (segAng2 > maxSegAng2) segAng2 = maxSegAng2;
    double step2 = segAng2 + gapAng2;

    double r2Start = -M_PI / 2.0 - ((r2Count - 1) * step2) / 2.0;
    for (int k = 0; k < r2Count; ++k) {
        double angle = r2Start + k * step2;
        outRing2.append(makeAppSeg(m_cachedFavorites[k], angle, segAng2, 2));
    }

    if (m_cachedFavorites.size() > 15) {
        int r3Count = std::min((int)m_cachedFavorites.size() - 15, 20);
        const int r3Outer = r2Outer + ring3Gap() + ring3Depth();
        const double gapAng3 = (double)segGap() / r3Outer;
        const double maxSegAng3 = segmentWidth() / r3Outer;

        double segAng3 = (availArc - (r3Count - 1) * gapAng3) / r3Count;
        if (segAng3 > maxSegAng3) segAng3 = maxSegAng3;
        double step3 = segAng3 + gapAng3;

        double r3Start = -M_PI / 2.0 - ((r3Count - 1) * step3) / 2.0;
        for (int k = 0; k < r3Count; ++k) {
            double angle = r3Start + k * step3;
            outRing3.append(makeAppSeg(m_cachedFavorites[15 + k], angle, segAng3, 3));
        }
    }
}

void CircleOverlay::partitionTray(QList<OverlaySegment> &outRing1, QList<OverlaySegment> &outRing2, QList<OverlaySegment> &outRing3)
{
    outRing1.clear();
    outRing2.clear();
    outRing3.clear();

    const double availArc = 1.5 * M_PI; // 270° Bogen (-225° bis +45°)
    const int r1Outer = hubRadius() + ringGap() + ringDepth();
    const double gapAng1 = (double)segGap() / r1Outer;
    const double maxSegAng1 = segmentWidth() / r1Outer;

    // 1. Programme (SNI) und System-Applets
    QList<TrayProgramItem> progs = m_cachedTrayPrograms;
    QList<SystemTrayApplet> primApplets = TrayManager::primaryApplets();
    QList<SystemTrayApplet> secApplets = TrayManager::secondaryApplets();

    // 2. Ring 1 zusammenstellen:
    // - Maximal 11 Segmente auf Ring 1:
    //   - Index 0: "..." Segment ganz links
    //   - Programme: Auf der rechten Flanke (bis zu 10 Plätze; verdrängte System-Applets wandern nach links in "...")
    //   - Traysymbole: Füllen alle verbleibenden Plätze zwischen "..." und den Programmen auf der linken Flanke auf.
    //   - REIHENFOLGE: Von "..." nach rechts laufen die Traysymbole zur 12-Uhr-Mitte hin (Network steht direkt vor den Programmicons,
    //     weitere Tray-Symbole wie Clipboard stehen direkt nach "...").
    //   - HINTER DEN PROGRAMMEN STEHT NIEMALS EIN TRAY-SYMBOL!
    int numProgs = std::min((int)progs.size(), 10);
    int numTray = std::min((int)primApplets.size(), 10 - numProgs);

    QList<OverlaySegment> r1Items;

    // Index 0: "..." Segment ganz links
    OverlaySegment moreSeg;
    moreSeg.windowId = QStringLiteral("__TRAY_MORE__");
    moreSeg.appKey = QStringLiteral("more");
    moreSeg.resourceClass = QStringLiteral("more");
    moreSeg.desktopFileName = QStringLiteral("__TRAY_MORE__");
    moreSeg.caption = QStringLiteral("More");
    moreSeg.tooltipText = QStringLiteral("More Symbols");
    moreSeg.ring = 1;
    moreSeg.hasDrilldown = true;
    moreSeg.segmentType = QStringLiteral("tray_more");
    moreSeg.canPin = false;
    moreSeg.isPinned = false;
    r1Items.append(moreSeg);

    // Traysymbole zwischen "..." und Programmen:
    // primApplets[0] (network) steht direkt vor den Programmen.
    // Die weiteren Symbole (bluetooth, volume, clipboard, ...) schließen nach links bis "..." an.
    for (int i = numTray - 1; i >= 0; --i) {
        const auto &applet = primApplets[i];
        OverlaySegment seg;
        seg.windowId = QStringLiteral("tray_sys:%1").arg(applet.pluginId);
        seg.appKey = applet.glyphName;
        seg.resourceClass = applet.glyphName;
        seg.desktopFileName = applet.glyphName;
        seg.caption = applet.name;
        seg.tooltipText = applet.name;
        seg.ring = 1;
        seg.hasDrilldown = false;
        seg.segmentType = QStringLiteral("tray_sys");
        seg.canPin = false;
        seg.isPinned = false;
        r1Items.append(seg);
    }

    // Anschließend die laufenden Programme (nach den Tray-Symbolen auf der rechten Flanke)
    for (int i = 0; i < numProgs; ++i) {
        const auto &p = progs[i];
        OverlaySegment seg;
        seg.windowId = QStringLiteral("tray_prog|%1|%2").arg(p.service, p.path);
        seg.appKey = QStringLiteral("tray_prog_%1").arg(p.id);
        seg.resourceClass = p.id;
        seg.desktopFileName = QStringLiteral("tray_prog|") + QStringLiteral("%1|%2").arg(p.service, p.path);
        seg.caption = p.title;
        seg.tooltipText = p.title;
        seg.ring = 1;
        seg.hasDrilldown = true;
        seg.segmentType = QStringLiteral("tray");
        seg.canPin = false;
        seg.isPinned = false;
        r1Items.append(seg);
    }

    outRing1 = r1Items;

    int r1Count = outRing1.size();
    double segAng1 = (availArc - (r1Count - 1) * gapAng1) / std::max(1, r1Count);
    if (segAng1 > maxSegAng1) segAng1 = maxSegAng1;
    double step1 = segAng1 + gapAng1;
    int centerIndex1 = (r1Count - 1) / 2;

    for (int i = 0; i < r1Count; ++i) {
        outRing1[i].angle = -M_PI / 2.0 + (i - centerIndex1) * step1;
        outRing1[i].span = segAng1;
    }

    // 3. Ring 2 (und ggf. Ring 3 bei Drilldown eines Überlauf-Tray-Programms in "..."):
    const int r2Outer = r1Outer + ring2Gap() + ring2Depth();
    const double gapAng2 = (double)segGap() / r2Outer;

    if (m_trayMoreHovered) {
        // Alles, was sich aus "..." öffnet, öffnet sich von links nach rechts,
        // anfangend direkt über dem "..."-Symbol (outRing1[0].angle)
        QList<OverlaySegment> moreSegs;
        for (const auto &applet : secApplets) {
            OverlaySegment seg;
            seg.windowId = QStringLiteral("tray_sys:%1").arg(applet.pluginId);
            seg.appKey = applet.glyphName;
            seg.resourceClass = applet.glyphName;
            seg.desktopFileName = applet.glyphName;
            seg.caption = applet.name;
            seg.tooltipText = applet.name;
            seg.ring = 2;
            seg.hasDrilldown = false;
            seg.segmentType = QStringLiteral("tray_sys");
            seg.canPin = false;
            seg.isPinned = false;
            moreSegs.append(seg);
        }
        for (int i = numTray; i < primApplets.size(); ++i) {
            const auto &applet = primApplets[i];
            OverlaySegment seg;
            seg.windowId = QStringLiteral("tray_sys:%1").arg(applet.pluginId);
            seg.appKey = applet.glyphName;
            seg.resourceClass = applet.glyphName;
            seg.desktopFileName = applet.glyphName;
            seg.caption = applet.name;
            seg.tooltipText = applet.name;
            seg.ring = 2;
            seg.hasDrilldown = false;
            seg.segmentType = QStringLiteral("tray_sys");
            seg.canPin = false;
            seg.isPinned = false;
            moreSegs.append(seg);
        }
        for (int i = numProgs; i < progs.size(); ++i) {
            const auto &p = progs[i];
            OverlaySegment seg;
            seg.windowId = QStringLiteral("tray_prog|%1|%2").arg(p.service, p.path);
            seg.appKey = QStringLiteral("tray_prog_%1").arg(p.id);
            seg.resourceClass = p.id;
            seg.desktopFileName = QStringLiteral("tray_prog|") + QStringLiteral("%1|%2").arg(p.service, p.path);
            seg.caption = p.title;
            seg.tooltipText = p.title;
            seg.ring = 2;
            seg.hasDrilldown = true;
            seg.segmentType = QStringLiteral("tray");
            seg.canPin = false;
            seg.isPinned = false;
            moreSegs.append(seg);
        }

        int r2Count = moreSegs.size();
        if (r2Count > 0 && !outRing1.isEmpty()) {
            double segAng2 = (availArc - (r2Count - 1) * gapAng2) / std::max(1, r2Count);
            if (segAng2 > segmentWidth() / r2Outer) segAng2 = segmentWidth() / r2Outer;
            double step2 = segAng2 + gapAng2;
            double r2Start = outRing1[0].angle; // Anfangend direkt über dem "..."-Symbol
            const double arcMax = -M_PI / 2.0 + availArc / 2.0;
            if (r2Start + (r2Count - 1) * step2 + segAng2 / 2.0 > arcMax) {
                r2Start = arcMax - segAng2 / 2.0 - (r2Count - 1) * step2;
            }

            for (int j = 0; j < r2Count; ++j) {
                moreSegs[j].angle = r2Start + j * step2; // Von links nach rechts
                moreSegs[j].span = segAng2;
                outRing2.append(moreSegs[j]);
            }
        }

        // Falls ein Überlauf-Programm in "..." auf Ring 2 gehovert wird -> dessen Menü auf Ring 3 öffnen
        if (!m_activeTrayProgramKey.isEmpty() && !m_cachedMenuActions.isEmpty()) {
            const int r3Outer = r2Outer + ring3Gap() + ring3Depth();
            const double gapAng3 = (double)segGap() / r3Outer;
            int optCount = m_cachedMenuActions.size();
            double segAng3 = (availArc - (optCount - 1) * gapAng3) / std::max(1, optCount);
            if (segAng3 > 0.20) segAng3 = 0.20;
            double step3 = segAng3 + gapAng3;
            int optCenter = (optCount - 1) / 2;

            double centerProgAngle = -M_PI / 2.0;
            for (const auto &s : outRing2) {
                if ((s.windowId.startsWith(QLatin1String("tray_prog|")) || s.windowId.startsWith(QLatin1String("tray_prog:"))) &&
                    s.windowId.mid(10) == m_activeTrayProgramKey) {
                    centerProgAngle = s.angle;
                    break;
                }
            }

            for (int k = 0; k < optCount; ++k) {
                const auto &act = m_cachedMenuActions[k];
                OverlaySegment seg;
                seg.windowId = QStringLiteral("tray_opt|%1|%2|%3").arg(act.service, act.menuPath, QString::number(act.id));
                seg.appKey = QStringLiteral("tray_opt_%1").arg(act.id);
                seg.resourceClass = QStringLiteral("tray-option");
                seg.desktopFileName = QStringLiteral("tray-option");
                seg.caption = act.label;
                seg.label = act.label;
                seg.tooltipText = act.label;
                seg.isTextSegment = true;
                seg.ring = 3;
                seg.segmentType = QStringLiteral("tray_opt");
                seg.canPin = false;
                seg.isPinned = false;
                seg.angle = centerProgAngle + (k - optCenter) * step3;
                seg.span = segAng3;
                outRing3.append(seg);
            }
        }
    } else if (!m_activeTrayProgramKey.isEmpty() && !m_cachedMenuActions.isEmpty()) {
        int optCount = m_cachedMenuActions.size();
        double segAng2 = (availArc - (optCount - 1) * gapAng2) / std::max(1, optCount);
        if (segAng2 > 0.20) segAng2 = 0.20; // Schmal wie Menü-Kategorien
        double step2 = segAng2 + gapAng2;
        int optCenter = (optCount - 1) / 2;

        double centerProgAngle = -M_PI / 2.0;
        for (const auto &s : outRing1) {
            if ((s.windowId.startsWith(QLatin1String("tray_prog|")) || s.windowId.startsWith(QLatin1String("tray_prog:"))) &&
                s.windowId.mid(10) == m_activeTrayProgramKey) {
                centerProgAngle = s.angle;
                break;
            }
        }

        for (int k = 0; k < optCount; ++k) {
            const auto &act = m_cachedMenuActions[k];
            OverlaySegment seg;
            seg.windowId = QStringLiteral("tray_opt|%1|%2|%3").arg(act.service, act.menuPath, QString::number(act.id));
            seg.appKey = QStringLiteral("tray_opt_%1").arg(act.id);
            seg.resourceClass = QStringLiteral("tray-option");
            seg.desktopFileName = QStringLiteral("tray-option");
            seg.caption = act.label;
            seg.label = act.label;
            seg.tooltipText = act.label;
            seg.isTextSegment = true;
            seg.ring = 2;
            seg.segmentType = QStringLiteral("tray_opt");
            seg.canPin = false;
            seg.isPinned = false;
            seg.angle = centerProgAngle + (k - optCenter) * step2;
            seg.span = segAng2;
            outRing2.append(seg);
        }
    }
}

void CircleOverlay::populateRecentAppsSegments(bool &outHasRing2, bool &outHasRing3)
{
    QList<OverlaySegment> ring1Segs;
    QList<OverlaySegment> ring2Segs;
    QList<OverlaySegment> ring3Segs;
    int centerIndex = 0;

    if (m_recentStage == RecentAppsStage::Recents) {
        partitionRecentApps(ring1Segs, centerIndex, ring2Segs, ring3Segs);
    } else if (m_recentStage == RecentAppsStage::Favorites) {
        partitionFavorites(ring1Segs, ring2Segs, ring3Segs);
        centerIndex = 5;
    } else if (m_recentStage == RecentAppsStage::Categories) {
        partitionCategories(ring1Segs, m_activeCategoryIndex, ring2Segs, ring3Segs);
        centerIndex = ring1Segs.size() / 2;
    } else if (m_recentStage == RecentAppsStage::Tray) {
        partitionTray(ring1Segs, ring2Segs, ring3Segs);
        centerIndex = ring1Segs.size() / 2;
    } else if (m_recentStage == RecentAppsStage::Session) {
        partitionSession(ring1Segs, ring2Segs);
        centerIndex = ring1Segs.size() / 2;
    }

    appendMediaSegments(ring1Segs);

    outHasRing2 = !ring2Segs.isEmpty();
    outHasRing3 = !ring3Segs.isEmpty();

    QString tmpDir = QDir::tempPath() + QStringLiteral("/karyon_icons");
    QDir().mkpath(tmpDir);

    auto prepareList = [&](const QList<OverlaySegment> &list) {
        QVariantList result;
        for (const auto &seg : list) {
            QString iconPath;
            if (seg.isTextSegment) {
                // Textsegment besitzt kein Icon
            } else if (KaryonGlyphs::hasGlyph(seg.appKey)) {
                iconPath = KaryonGlyphs::ensureGlyphCached(seg.appKey, tmpDir, iconSize());
            } else if (KaryonGlyphs::hasGlyph(seg.resourceClass)) {
                iconPath = KaryonGlyphs::ensureGlyphCached(seg.resourceClass, tmpDir, iconSize());
            } else if (!seg.trayProgramKey.isEmpty() || seg.windowId.startsWith(QLatin1String("tray_prog|")) || seg.windowId.startsWith(QLatin1String("tray_prog:"))) {
                QString progKey = !seg.trayProgramKey.isEmpty() ? seg.trayProgramKey : seg.windowId.mid(10);
                for (const auto &prog : m_cachedTrayPrograms) {
                    if (QStringLiteral("%1|%2").arg(prog.service, prog.path) == progKey ||
                        QStringLiteral("%1:%2").arg(prog.service, prog.path) == progKey) {
                        QString cleanKey = QStringLiteral("tray_prog_") + prog.id;
                        cleanKey.replace(QLatin1Char('/'), QLatin1Char('_')).replace(QLatin1Char(':'), QLatin1Char('_'));
                        QString revKey = QStringLiteral("%1_v%2").arg(cleanKey, QString::number(m_trayIconRevision));
                        iconPath = tmpDir + QStringLiteral("/") + revKey + QStringLiteral(".png");
                        if (!QFile::exists(iconPath)) {
                            if (m_trayIconRevision > 0) {
                                QString oldRevKey = QStringLiteral("%1_v%2.png").arg(cleanKey, QString::number(m_trayIconRevision - 1));
                                QFile::remove(tmpDir + QStringLiteral("/") + oldRevKey);
                            }
                            QPixmap pix = prog.icon.pixmap(iconSize(), iconSize());
                            pix.save(iconPath, "PNG");
                        }
                        break;
                    }
                }
            } else {
                QString key = seg.appKey;
                iconPath = m_iconPathCache.value(key);
                if (iconPath.isEmpty() || !QFile::exists(iconPath)) {
                    QIcon icon = resolveWindowIcon(seg.desktopFileName, seg.resourceClass);
                    QString cleanKey = key;
                    cleanKey.replace(QLatin1Char('/'), QLatin1Char('_')).replace(QLatin1Char(':'), QLatin1Char('_'));
                    iconPath = tmpDir + QStringLiteral("/") + cleanKey + QStringLiteral(".png");
                    if (!QFile::exists(iconPath)) {
                        QPixmap pix = icon.pixmap(iconSize(), iconSize());
                        pix.save(iconPath, "PNG");
                    }
                    m_iconPathCache.insert(key, iconPath);
                }
            }

            QVariantMap m;
            m[QStringLiteral("isTextSegment")] = seg.isTextSegment;
            m[QStringLiteral("label")] = seg.label;
            m[QStringLiteral("iconSource")] = iconPath.isEmpty() ? QString() : (QStringLiteral("file://") + iconPath);
            m[QStringLiteral("windowId")] = seg.windowId;
            m[QStringLiteral("resourceClass")] = seg.resourceClass;
            m[QStringLiteral("appKey")] = seg.appKey;
            m[QStringLiteral("angle")] = seg.angle;
            m[QStringLiteral("span")] = seg.span;
            m[QStringLiteral("ring")] = seg.ring;
            m[QStringLiteral("tooltipText")] = seg.tooltipText;
            m[QStringLiteral("hasDrilldown")] = seg.hasDrilldown;
            m[QStringLiteral("segmentType")] = seg.segmentType;
            m[QStringLiteral("isRealWindow")] = seg.isRealWindow;
            m[QStringLiteral("canPin")] = seg.canPin;
            m[QStringLiteral("isPinned")] = seg.isPinned;
            m[QStringLiteral("progressPercent")] = seg.progressPercent;
            result.append(m);
        }
        return result;
    };

    assignSegmentProgress(ring1Segs, ring2Segs, ring3Segs);

    m_currentRing1 = ring1Segs;
    m_currentRing2 = ring2Segs;
    m_currentRing3 = ring3Segs;

    QVariantList segmentList1 = prepareList(ring1Segs);
    QVariantList segmentList2 = prepareList(ring2Segs);
    QVariantList segmentList3 = prepareList(ring3Segs);

    if (m_view && m_view->rootContext()) {
        m_view->rootContext()->setContextProperty(QStringLiteral("segments"), segmentList1);
        m_view->rootContext()->setContextProperty(QStringLiteral("segments2"), segmentList2);
        m_view->rootContext()->setContextProperty(QStringLiteral("segments3"), segmentList3);
        m_view->rootContext()->setContextProperty(QStringLiteral("hasRing2"), outHasRing2);
        m_view->rootContext()->setContextProperty(QStringLiteral("hasRing3"), outHasRing3);
        m_view->rootContext()->setContextProperty(QStringLiteral("centerIndex"), centerIndex);
        m_view->rootContext()->setContextProperty(QStringLiteral("categoryMode"), QStringLiteral("apps"));
    }

    if (m_view && m_view->rootObject()) {
        m_view->rootObject()->setProperty("categoryMode", QStringLiteral("apps"));
        QMetaObject::invokeMethod(m_view->rootObject(), "setSegmentLists",
                                  Q_ARG(QVariant, QVariant::fromValue(segmentList1)),
                                  Q_ARG(QVariant, QVariant::fromValue(segmentList2)),
                                  Q_ARG(QVariant, QVariant::fromValue(segmentList3)),
                                  Q_ARG(QVariant, outHasRing2),
                                  Q_ARG(QVariant, outHasRing3));
        // Auswahl nach dem Neuaufbau aus QML zurücklesen, damit C++ nie mit veralteten Indizes arbeitet
        m_selectedRing = m_view->rootObject()->property("selectedRing").toInt();
        m_selectedIndex = m_view->rootObject()->property("selectedIndex").toInt();
    }
}

void CircleOverlay::populateRecentFilesSegments(bool &outHasRing2, bool &outHasRing3)
{
    outHasRing2 = false;
    outHasRing3 = false;

    QList<OverlaySegment> ring1Segs;
    QList<OverlaySegment> ring2Segs;
    QList<OverlaySegment> ring3Segs;
    int centerIndex = 0;

    partitionRecentFiles(ring1Segs, centerIndex, ring2Segs, ring3Segs);
    appendMediaSegments(ring1Segs);

    outHasRing2 = !ring2Segs.isEmpty();
    outHasRing3 = !ring3Segs.isEmpty();

    QString tmpDir = QDir::tempPath() + QStringLiteral("/karyon_icons");
    QDir().mkpath(tmpDir);

    auto prepareList = [&](const QList<OverlaySegment> &list) {
        QVariantList result;
        for (const auto &seg : list) {
            QString iconPath;
            if (KaryonGlyphs::hasGlyph(seg.appKey)) {
                iconPath = KaryonGlyphs::ensureGlyphCached(seg.appKey, tmpDir, ICON_SIZE);
            } else if (KaryonGlyphs::hasGlyph(seg.resourceClass)) {
                iconPath = KaryonGlyphs::ensureGlyphCached(seg.resourceClass, tmpDir, ICON_SIZE);
            } else {
                QString key = seg.appKey;
                iconPath = m_iconPathCache.value(key);
                if (iconPath.isEmpty() || !QFile::exists(iconPath)) {
                QString filePath = key.startsWith(QLatin1String("file:")) ? key.mid(5) : key;
                QMimeDatabase mimeDb;
                QMimeType mimeType = mimeDb.mimeTypeForFile(filePath);
                QIcon icon = QIcon::fromTheme(mimeType.iconName());
                if (icon.isNull()) icon = QIcon::fromTheme(mimeType.genericIconName());
                if (icon.isNull()) icon = QIcon::fromTheme(QStringLiteral("text-x-generic"));

                QString cleanKey = key;
                cleanKey.replace(QLatin1Char('/'), QLatin1Char('_')).replace(QLatin1Char(':'), QLatin1Char('_')).replace(QLatin1Char('.'), QLatin1Char('_'));
                iconPath = tmpDir + QStringLiteral("/") + cleanKey + QStringLiteral(".png");
                if (!QFile::exists(iconPath)) {
                    QPixmap pix = icon.pixmap(ICON_SIZE, ICON_SIZE);
                    pix.save(iconPath, "PNG");
                }
                m_iconPathCache.insert(key, iconPath);
            }
        }

        QVariantMap m;
            m[QStringLiteral("isTextSegment")] = seg.isTextSegment;
            m[QStringLiteral("label")] = seg.label;
            m[QStringLiteral("iconSource")] = iconPath.isEmpty() ? QString() : (QStringLiteral("file://") + iconPath);
            m[QStringLiteral("windowId")] = seg.windowId;
            m[QStringLiteral("resourceClass")] = seg.resourceClass;
            m[QStringLiteral("appKey")] = seg.appKey;
            m[QStringLiteral("angle")] = seg.angle;
            m[QStringLiteral("span")] = seg.span;
            m[QStringLiteral("ring")] = seg.ring;
            m[QStringLiteral("tooltipText")] = seg.tooltipText;
            m[QStringLiteral("hasDrilldown")] = seg.hasDrilldown;
            m[QStringLiteral("segmentType")] = seg.segmentType;
            m[QStringLiteral("isRealWindow")] = seg.isRealWindow;
            m[QStringLiteral("canPin")] = seg.canPin;
            m[QStringLiteral("isPinned")] = seg.isPinned;
            m[QStringLiteral("progressPercent")] = seg.progressPercent;
            result.append(m);
        }
        return result;
    };

    assignSegmentProgress(ring1Segs, ring2Segs, ring3Segs);

    m_currentRing1 = ring1Segs;
    m_currentRing2 = ring2Segs;
    m_currentRing3 = ring3Segs;

    QVariantList segmentList1 = prepareList(ring1Segs);
    QVariantList segmentList2 = prepareList(ring2Segs);
    QVariantList segmentList3 = prepareList(ring3Segs);

    if (m_view && m_view->rootContext()) {
        m_view->rootContext()->setContextProperty(QStringLiteral("segments"), segmentList1);
        m_view->rootContext()->setContextProperty(QStringLiteral("segments2"), segmentList2);
        m_view->rootContext()->setContextProperty(QStringLiteral("segments3"), segmentList3);
        m_view->rootContext()->setContextProperty(QStringLiteral("hasRing2"), outHasRing2);
        m_view->rootContext()->setContextProperty(QStringLiteral("hasRing3"), outHasRing3);
        m_view->rootContext()->setContextProperty(QStringLiteral("centerIndex"), centerIndex);
        m_view->rootContext()->setContextProperty(QStringLiteral("categoryMode"), QStringLiteral("files"));
    }

    if (m_view && m_view->rootObject()) {
        m_view->rootObject()->setProperty("categoryMode", QStringLiteral("files"));
        QMetaObject::invokeMethod(m_view->rootObject(), "setSegmentLists",
                                  Q_ARG(QVariant, QVariant::fromValue(segmentList1)),
                                  Q_ARG(QVariant, QVariant::fromValue(segmentList2)),
                                  Q_ARG(QVariant, QVariant::fromValue(segmentList3)),
                                  Q_ARG(QVariant, outHasRing2),
                                  Q_ARG(QVariant, outHasRing3));
        // Auswahl nach dem Neuaufbau aus QML zurücklesen, damit C++ nie mit veralteten Indizes arbeitet
        m_selectedRing = m_view->rootObject()->property("selectedRing").toInt();
        m_selectedIndex = m_view->rootObject()->property("selectedIndex").toInt();
    }
}

void CircleOverlay::refreshCurrentMode()
{
    bool hasRing2 = false;
    bool hasRing3 = false;
    if (m_mode == OverlayMode::Windows) {
        populateSegments(m_cachedWindows, hasRing2, hasRing3);
    } else if (m_mode == OverlayMode::RecentApps) {
        populateRecentAppsSegments(hasRing2, hasRing3);
    } else if (m_mode == OverlayMode::RecentFiles) {
        populateRecentFilesSegments(hasRing2, hasRing3);
    }

    if (m_view && m_view->rootObject()) {
        QMetaObject::invokeMethod(m_view->rootObject(), "requestPaintCanvas");
    }
}

void CircleOverlay::showAt(const QPoint &cursorPos, const QList<WindowInfo> &windows, InputMonitor *monitor, bool isGame)
{
    m_cachedWindows = windows;
    if (m_cachedTrayPrograms.isEmpty()) {
        m_cachedTrayPrograms = m_trayManager.runningPrograms();
    }
    m_showDesktopEnabled = SettingsManager::instance().showDesktop();
    m_showTrayEnabled = SettingsManager::instance().showTray();
    m_showSessionEnabled = SettingsManager::instance().showSession();
    m_showMenuEnabled = SettingsManager::instance().showMenu();
    m_showFavoritesEnabled = SettingsManager::instance().showFavorites();
    m_showMediaControlsEnabled = SettingsManager::instance().showMediaControls();
    m_showVolumeControlEnabled = SettingsManager::instance().showVolumeControl();
    m_mode = initialOverlayMode();
    m_recentStage = RecentAppsStage::Recents;
    m_activeCategoryIndex = -1;
    m_categoryPage = 0;
    m_isVolumeHovered = false;
    m_volumeScrolledThisSession = false;
    if (m_view && m_view->rootObject()) {
        m_view->rootObject()->setProperty("volumeScrolledThisSession", false);
    }

    m_volumeManager.refresh();
    m_mediaManager.refresh(windows);
    updateVolumeState(m_volumeManager.volume(), m_volumeManager.isMuted());

    updateScaleProperties();

    bool hasRing2 = false;
    bool hasRing3 = false;
    if (m_mode == OverlayMode::Windows) {
        m_cachedRecentApps.clear();
        populateSegments(windows, hasRing2, hasRing3);
    } else if (m_mode == OverlayMode::RecentApps) {
        m_cachedRecentApps = m_appManager.loadRecentApps(SettingsManager::instance().maxRecentApps(), openAppIdentifiers());
        if (m_cachedCategories.isEmpty()) {
            m_cachedCategories = m_appManager.loadCategories();
        }
        if (m_cachedFavorites.isEmpty()) {
            m_cachedFavorites = m_appManager.loadFavorites();
        }
        populateRecentAppsSegments(hasRing2, hasRing3);
    } else if (m_mode == OverlayMode::RecentFiles) {
        m_cachedRecentFiles = m_recentFilesManager.loadRecentFiles(SettingsManager::instance().maxRecentFiles());
        populateRecentFilesSegments(hasRing2, hasRing3);
    }

    int vr = visualRadius(hasRing2, hasRing3);
    int wr = windowRadius(true, true);
    int size = wr * 2;
    m_view->resize(size, size);

    // Bildschirm-Clamping für gesamtes Overlay (Hub + Ringe dürfen nie abgeschnitten werden)
    QScreen *screen = QGuiApplication::screenAt(cursorPos);
    if (!screen) {
        screen = QGuiApplication::primaryScreen();
    }
    if (!screen) {
        return;
    }

    const QRect screenGeo = screen->geometry();
    const int screenLeft = screenGeo.x();
    const int screenRight = screenGeo.x() + screenGeo.width();
    const int screenTop = screenGeo.y();
    const int screenBottom = screenGeo.y() + screenGeo.height();

    int curOffsetY = cursorOffsetY();
    const int minCursorX = screenLeft + vr;
    const int maxCursorX = screenRight - vr;
    const int minCursorY = screenTop + vr - curOffsetY;
    const int maxCursorY = screenBottom - vr - curOffsetY;

    QPoint targetCursor = cursorPos;
    bool needsWarp = false;

    if (targetCursor.x() < minCursorX) {
        targetCursor.setX(minCursorX);
        needsWarp = true;
    } else if (targetCursor.x() > maxCursorX) {
        targetCursor.setX(maxCursorX);
        needsWarp = true;
    }

    if (targetCursor.y() < minCursorY) {
        targetCursor.setY(minCursorY);
        needsWarp = true;
    } else if (targetCursor.y() > maxCursorY) {
        targetCursor.setY(maxCursorY);
        needsWarp = true;
    }

    if (needsWarp) {
        QRect vGeo = screen->virtualGeometry();
        if (vGeo.isEmpty()) vGeo = screenGeo;
        QCursor::setPos(targetCursor);
        if (monitor) {
            monitor->warpCursor(targetCursor.x() - vGeo.x(), targetCursor.y() - vGeo.y(), vGeo.width(), vGeo.height());
        }
    }

    const int winX = targetCursor.x() - wr;
    const int winY = targetCursor.y() + curOffsetY - wr;

    m_centerX = targetCursor.x();
    m_centerY = targetCursor.y() + curOffsetY;

    m_view->setPosition(winX, winY);
    m_view->setVisible(true);
    m_view->raise();

    emit overlayShown();

    m_monitor = monitor;
    m_isPinHovered = false;
    m_isCounterBadgeHovered = false;
    m_isGameActive = isGame;
    m_qmlPointerEverMoved = false;
    m_hardwareMoveCountWithoutQml = 0;
    m_overlayShownTime = std::chrono::steady_clock::now();
    bool shouldShowFallback = isGame;
    if (m_fallbackCursorActive != shouldShowFallback) {
        m_fallbackCursorActive = shouldShowFallback;
        emit fallbackCursorActiveChanged(m_fallbackCursorActive);
    }

    // Initiale Mausposition: cursorOffsetY über dem Zentrum -> Vorauswahl von Ring 1
    m_lastAimX = 0;
    m_lastAimY = -curOffsetY;
    updateAim(0, -curOffsetY);
}

void CircleOverlay::refresh(const QList<WindowInfo> &windows)
{
    m_cachedWindows = windows;
    if (!m_view || !m_view->isVisible()) return;

    if (m_mode == OverlayMode::Windows) {
        bool hasRing2 = false;
        bool hasRing3 = false;
        populateSegments(windows, hasRing2, hasRing3);
    } else if (m_mode == OverlayMode::RecentApps) {
        refreshCurrentMode();
    }

    // Fenstergröße und -position bleiben während des geöffneten Overlays unverändert fixiert,
    // um jedes Zucken/Verrutschen des Overlays auszuschließen!

    updateAim(m_lastAimX, m_lastAimY);
}

void CircleOverlay::updateAim(int offsetX, int offsetY)
{
    m_lastAimX = offsetX;
    m_lastAimY = offsetY;
    if (!m_view || !m_view->rootObject()) return;
    QMetaObject::invokeMethod(m_view->rootObject(), "setAimOffset",
                              Q_ARG(QVariant, offsetX),
                              Q_ARG(QVariant, offsetY));
}

void CircleOverlay::onHardwareCursorMoved(int aimX, int aimY, const QPoint &kwinCurPos)
{
    Q_UNUSED(kwinCurPos);

    // 1. Wenn QML in dieser Overlay-Sitzung bereits Compositor-Events empfangen hat (Desktop-Betrieb):
    if (m_qmlPointerEverMoved) {
        // Desktop-Aiming läuft flackerfrei über QML MouseArea, solange der Cursor im Fenster liegt.
        // Verlässt der Cursor das Overlay-Fenster, führt InputMonitor das globale Aiming
        // über den gesamten Bildschirm nahtlos weiter:
        int halfW = m_view ? (m_view->width() / 2) : 300;
        int halfH = m_view ? (m_view->height() / 2) : halfW;
        if (std::abs(aimX) >= halfW || std::abs(aimY) >= halfH) {
            updateAim(aimX, aimY);
        }
        return;
    }

    // 2. QML hat noch kein Event empfangen:
    m_hardwareMoveCountWithoutQml++;

    auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - m_overlayShownTime).count();

    // Wenn als Spiel erkannt, ODER nach mindestens 20 Hardware-Paketen UND mindestens 120ms ohne QML-Event (Pointer-Lock / versteckter Cursor):
    if (m_isGameActive || (m_hardwareMoveCountWithoutQml >= 20 && elapsedMs >= 120)) {
        if (!m_fallbackCursorActive) {
            m_fallbackCursorActive = true;
            emit fallbackCursorActiveChanged(true);
        }
        updateAim(aimX, aimY);
    }
}

void CircleOverlay::reportQmlPointerMoved()
{
    m_qmlPointerEverMoved = true;
    m_hardwareMoveCountWithoutQml = 0;
    if (m_fallbackCursorActive) {
        m_fallbackCursorActive = false;
        emit fallbackCursorActiveChanged(false);
    }
}

void CircleOverlay::handleAimChanged(int offsetX, int offsetY, bool isDrilldown, const QString &curId, int selRing, int selIdx, bool isPinHovered, bool isVolumeHovered, bool isCounterBadgeHovered)
{
    m_lastAimX = offsetX;
    m_lastAimY = offsetY;
    if (m_monitor) {
        m_monitor->syncAim(offsetX, offsetY);
    }
    m_isPinHovered = isPinHovered;
    m_isVolumeHovered = isVolumeHovered;
    m_isCounterBadgeHovered = isCounterBadgeHovered;
    m_selectedRing = selRing;
    m_selectedIndex = selIdx;

    if (m_mode == OverlayMode::RecentApps) {
        if (m_recentStage == RecentAppsStage::Recents) {
            if (isDrilldown && curId == QLatin1String("__APP_MENU__")) {
                m_recentStage = RecentAppsStage::Categories;
                m_categoryPage = 0;
                int p0Count = (m_cachedCategories.size() <= 13) ? m_cachedCategories.size() : 12;
                m_activeCategoryIndex = p0Count / 2;
                refreshCurrentMode();
            } else if (isDrilldown && curId == QLatin1String("__FAVORITES__")) {
                m_recentStage = RecentAppsStage::Favorites;
                refreshCurrentMode();
            } else if (isDrilldown && curId == QLatin1String("__TRAY__")) {
                m_recentStage = RecentAppsStage::Tray;
                m_cachedTrayPrograms = m_trayManager.runningPrograms();
                m_trayMoreHovered = false;
                m_activeTrayProgramKey.clear();
                m_cachedMenuActions.clear();
                refreshCurrentMode();
            } else if (isDrilldown && curId == QLatin1String("__SESSION__")) {
                m_recentStage = RecentAppsStage::Session;
                refreshCurrentMode();
            }
        } else if (m_recentStage == RecentAppsStage::Favorites) {
            if (isCursorInHub()) {
                m_recentStage = RecentAppsStage::Recents;
                refreshCurrentMode();
            } else if (isDrilldown && curId == QLatin1String("__TRAY__")) {
                m_recentStage = RecentAppsStage::Tray;
                m_cachedTrayPrograms = m_trayManager.runningPrograms();
                m_trayMoreHovered = false;
                m_activeTrayProgramKey.clear();
                m_cachedMenuActions.clear();
                refreshCurrentMode();
            } else if (isDrilldown && curId == QLatin1String("__SESSION__")) {
                m_recentStage = RecentAppsStage::Session;
                refreshCurrentMode();
            }
        } else if (m_recentStage == RecentAppsStage::Categories) {
            if (isCursorInHub()) {
                m_recentStage = RecentAppsStage::Recents;
                m_categoryPage = 0;
                m_activeCategoryIndex = -1;
                refreshCurrentMode();
            } else {
                if (selRing == 1) {
                    if (curId == QLatin1String("__CAT_MORE__") && isDrilldown) {
                        m_categoryPage++;
                        int newStart = m_categoryPage * 12;
                        if (newStart >= m_cachedCategories.size()) {
                            m_categoryPage = 0;
                            newStart = 0;
                        }
                        int rem = m_cachedCategories.size() - newStart;
                        int countOnNewPage = (rem <= 13) ? rem : 12;
                        m_activeCategoryIndex = newStart + countOnNewPage / 2;
                        refreshCurrentMode();
                    } else if (curId.startsWith(QLatin1String("cat:"))) {
                        int catIdx = curId.mid(4).toInt();
                        if (catIdx >= 0 && catIdx < m_cachedCategories.size() && catIdx != m_activeCategoryIndex) {
                            m_activeCategoryIndex = catIdx;
                            refreshCurrentMode();
                        }
                    }
                }
            }
        } else if (m_recentStage == RecentAppsStage::Tray) {
            if (isCursorInHub()) {
                m_recentStage = RecentAppsStage::Recents;
                m_trayMoreHovered = false;
                m_activeTrayProgramKey.clear();
                m_cachedMenuActions.clear();
                refreshCurrentMode();
            } else {
                if (selRing == 1 && curId == QLatin1String("__TRAY_MORE__") && isDrilldown) {
                    if (!m_trayMoreHovered || !m_activeTrayProgramKey.isEmpty()) {
                        m_trayMoreHovered = true;
                        m_activeTrayProgramKey.clear();
                        m_cachedMenuActions.clear();
                        refreshCurrentMode();
                    }
                } else if (selRing == 1 && (curId.startsWith(QLatin1String("tray_prog|")) || curId.startsWith(QLatin1String("tray_prog:"))) && isDrilldown) {
                    QString progKey = curId.mid(10);
                    if (m_activeTrayProgramKey != progKey || m_trayMoreHovered) {
                        m_activeTrayProgramKey = progKey;
                        m_trayMoreHovered = false;
                        for (const auto &prog : m_cachedTrayPrograms) {
                            if (QStringLiteral("%1|%2").arg(prog.service, prog.path) == progKey ||
                                QStringLiteral("%1:%2").arg(prog.service, prog.path) == progKey) {
                                m_cachedMenuActions = m_trayManager.fetchMenuActions(prog.service, prog.menuPath);
                                break;
                            }
                        }
                        refreshCurrentMode();
                    }
                } else if (selRing == 2 && m_trayMoreHovered && (curId.startsWith(QLatin1String("tray_prog|")) || curId.startsWith(QLatin1String("tray_prog:"))) && isDrilldown) {
                    QString progKey = curId.mid(10);
                    if (m_activeTrayProgramKey != progKey) {
                        m_activeTrayProgramKey = progKey;
                        for (const auto &prog : m_cachedTrayPrograms) {
                            if (QStringLiteral("%1|%2").arg(prog.service, prog.path) == progKey ||
                                QStringLiteral("%1:%2").arg(prog.service, prog.path) == progKey) {
                                m_cachedMenuActions = m_trayManager.fetchMenuActions(prog.service, prog.menuPath);
                                break;
                            }
                        }
                        refreshCurrentMode();
                    }
                } else if (isDrilldown && curId == QLatin1String("__FAVORITES__")) {
                    m_recentStage = RecentAppsStage::Favorites;
                    m_trayMoreHovered = false;
                    m_activeTrayProgramKey.clear();
                    m_cachedMenuActions.clear();
                    refreshCurrentMode();
                }
                // REGEL 2: Segmente, welche sich aus einem Drilldown ausklappen, bleiben bestehen,
                // selbst wenn man andere Segmente highlightet. Sie verschwinden nur, wenn man einen
                // anderen Drilldown berührt oder in den Hub zurückkehrt.
            }
        } else if (m_recentStage == RecentAppsStage::Session) {
            if (isCursorInHub()) {
                m_recentStage = RecentAppsStage::Recents;
                refreshCurrentMode();
            } else if (isDrilldown && curId == QLatin1String("__APP_MENU__")) {
                m_recentStage = RecentAppsStage::Categories;
                m_categoryPage = 0;
                int p0Count = (m_cachedCategories.size() <= 13) ? m_cachedCategories.size() : 12;
                m_activeCategoryIndex = p0Count / 2;
                refreshCurrentMode();
            } else if (isDrilldown && curId == QLatin1String("__FAVORITES__")) {
                m_recentStage = RecentAppsStage::Favorites;
                refreshCurrentMode();
            } else if (isDrilldown && curId == QLatin1String("__TRAY__")) {
                m_recentStage = RecentAppsStage::Tray;
                m_cachedTrayPrograms = m_trayManager.runningPrograms();
                m_trayMoreHovered = false;
                m_activeTrayProgramKey.clear();
                m_cachedMenuActions.clear();
                refreshCurrentMode();
            }
        }
    }
}

void CircleOverlay::togglePinForCurrentSelection()
{
    const OverlaySegment *seg = nullptr;
    if (m_selectedRing == 1 && m_selectedIndex >= 0 && m_selectedIndex < m_currentRing1.size()) {
        seg = &m_currentRing1[m_selectedIndex];
    } else if (m_selectedRing == 2 && m_selectedIndex >= 0 && m_selectedIndex < m_currentRing2.size()) {
        seg = &m_currentRing2[m_selectedIndex];
    } else if (m_selectedRing == 3 && m_selectedIndex >= 0 && m_selectedIndex < m_currentRing3.size()) {
        seg = &m_currentRing3[m_selectedIndex];
    }

    if (!seg || !seg->canPin) return;

    if (seg->segmentType == QLatin1String("window")) {
        // Zuerst prüfen, ob dieses Fenster zu einer angepinnten Datei gehört
        bool unpinnedFile = false;
        for (const auto &item : m_pinnedManager.pinnedItems()) {
            if (item.type == PinnedItemType::File) {
                QFileInfo fi(item.execOrPath);
                QString fileName = fi.fileName();
                QString baseName = fi.completeBaseName();
                bool matches = false;
                if (!fileName.isEmpty() && seg->caption.contains(fileName, Qt::CaseInsensitive)) {
                    matches = true;
                } else if (baseName.length() >= 2) {
                    QRegularExpression re(QStringLiteral("(?:^|[\\s\\-_—\\[\\]\\(\\)•*])%1(?:$|[\\s\\-_—\\[\\]\\(\\)•*])")
                                          .arg(QRegularExpression::escape(baseName)),
                                          QRegularExpression::CaseInsensitiveOption);
                    if (re.match(seg->caption).hasMatch()) {
                        matches = true;
                    }
                }
                if (matches) {
                    m_pinnedManager.unpinFile(item.execOrPath);
                    m_launchedFiles.remove(item.execOrPath);
                    m_launchedFiles.remove(fileName);
                    m_launchedFiles.remove(baseName);
                    unpinnedFile = true;
                    break;
                }
            }
        }

        if (!unpinnedFile) {
            // Fenster-Metadaten (inkl. PID für AppImages/Steam) über AppManager::resolveWindowApp auflösen,
            // damit Pins aus der Windows-Kategorie exakt dieselben validen Startbefehle erhalten wie aus der Apps-Kategorie.
            AppEntry app;
            for (const auto &w : m_cachedWindows) {
                if (w.id == seg->windowId) {
                    app = AppManager::resolveWindowApp(w.desktopFileName, w.resourceClass, w.pid, w.caption);
                    break;
                }
            }
            if (app.exec.trimmed().isEmpty()) {
                app = AppManager::findApp(seg->desktopFileName, seg->resourceClass);
            }
            if (app.exec.trimmed().isEmpty() && !seg->appKey.isEmpty()) {
                app = AppManager::findApp(seg->appKey, seg->caption);
            }

            QString desktopFile = app.desktopFile;
            QString exec = app.exec;
            QString name = app.name;
            QString icon = app.icon;
            QString pinId = !app.id.isEmpty() ? app.id : seg->appKey;

            if (desktopFile.isEmpty()) {
                desktopFile = findDesktopFile(seg->desktopFileName.isEmpty() ? seg->resourceClass : seg->desktopFileName);
            }
            if (exec.isEmpty() && !desktopFile.isEmpty()) {
                QSettings desktop(desktopFile, QSettings::IniFormat);
                desktop.beginGroup(QStringLiteral("Desktop Entry"));
                exec = desktop.value(QStringLiteral("Exec")).toString();
                if (name.isEmpty()) name = desktop.value(QStringLiteral("Name")).toString();
                if (icon.isEmpty()) icon = desktop.value(QStringLiteral("Icon")).toString();
            }

            if (pinId.isEmpty()) {
                pinId = seg->desktopFileName.isEmpty() ? seg->resourceClass : seg->desktopFileName;
            }
            if (pinId.contains(QLatin1Char('/')) && !AppManager::isAppImage(pinId)) {
                pinId = pinId.section(QLatin1Char('/'), -1);
            }
            if (pinId.endsWith(QLatin1String(".desktop"), Qt::CaseInsensitive)) {
                pinId.chop(8);
            }

            if (exec.isEmpty()) {
                exec = !desktopFile.isEmpty() ? desktopFile : pinId;
            }
            static const QRegularExpression re(QStringLiteral("%[a-zA-Z]"));
            exec.remove(re);
            exec = exec.trimmed();

            if (name.isEmpty()) {
                name = resolveAppName(seg->desktopFileName, seg->resourceClass);
            }
            if (icon.isEmpty()) {
                icon = seg->resourceClass.isEmpty() ? pinId : seg->resourceClass;
            }

            // Falls bereits (auch unter altem Pfad-Key) angepinnt, zuerst sauber entpinnen
            if (m_pinnedManager.isAppPinned(pinId, icon) ||
                (!seg->appKey.isEmpty() && m_pinnedManager.isAppPinned(seg->appKey, seg->resourceClass)) ||
                (!seg->desktopFileName.isEmpty() && m_pinnedManager.isAppPinned(seg->desktopFileName, seg->resourceClass))) {
                m_pinnedManager.unpinApp(pinId, icon);
                if (!seg->appKey.isEmpty()) m_pinnedManager.unpinApp(seg->appKey, seg->resourceClass);
                if (!seg->desktopFileName.isEmpty()) m_pinnedManager.unpinApp(seg->desktopFileName, seg->resourceClass);
            } else {
                m_pinnedManager.pinApp(pinId, name, exec, icon, desktopFile);
            }
        }
    } else if (seg->segmentType == QLatin1String("app")) {
        QString exec = seg->windowId.startsWith(QLatin1String("app:")) ? seg->windowId.mid(4) : seg->appKey;
        QString name = seg->caption;
        QString desktopFile = seg->desktopFileName;
        QString icon = seg->resourceClass;
        m_pinnedManager.toggleAppPin(seg->appKey, seg->resourceClass, name, exec, icon, desktopFile);
    } else if (seg->segmentType == QLatin1String("file")) {
        QString filePath = seg->windowId.startsWith(QLatin1String("file:")) ? seg->windowId.mid(5) : seg->appKey;
        m_pinnedManager.toggleFilePin(filePath, seg->caption, QStringLiteral("document"));
    } else if (seg->segmentType == QLatin1String("tray") ||
               seg->windowId.startsWith(QLatin1String("tray_prog|")) ||
               seg->windowId.startsWith(QLatin1String("tray_prog:"))) {
        QString progKey = seg->windowId.mid(10);
        QString trayId = seg->resourceClass;
        QString trayTitle = seg->caption;
        QString trayIcon = seg->appKey;
        for (const auto &p : m_cachedTrayPrograms) {
            if (QStringLiteral("%1|%2").arg(p.service, p.path) == progKey ||
                QStringLiteral("%1:%2").arg(p.service, p.path) == progKey ||
                p.id == trayId) {
                trayId = p.id;
                trayTitle = p.title;
                trayIcon = p.iconName;
                break;
            }
        }
        AppEntry app = AppManager::findApp(trayId, trayTitle);
        QString id = !app.id.isEmpty() ? app.id : trayId;
        QString name = !app.name.isEmpty() ? app.name : trayTitle;
        QString exec = !app.exec.isEmpty() ? app.exec : trayId;
        QString icon = !app.icon.isEmpty() ? app.icon : (!trayIcon.isEmpty() ? trayIcon : trayId);
        QString desktopFile = app.desktopFile;
        m_pinnedManager.toggleAppPin(id, trayTitle, name, exec, icon, desktopFile);
    }

    refreshCurrentMode();
}

void CircleOverlay::markFileLaunched(const QString &filePath)
{
    if (filePath.isEmpty()) return;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    m_launchedFiles.insert(filePath, now);
    QFileInfo fi(filePath);
    if (!fi.fileName().isEmpty()) {
        m_launchedFiles.insert(fi.fileName(), now);
    }
    if (!fi.completeBaseName().isEmpty()) {
        m_launchedFiles.insert(fi.completeBaseName(), now);
    }
}

QString CircleOverlay::selectedWindowId() const
{
    if (!m_view || !m_view->rootObject()) return {};
    QString id = m_view->rootObject()->property("selectedWindowId").toString();
    if (m_volumeScrolledThisSession && id.startsWith(QLatin1String("__MEDIA_"))) {
        return QString();
    }
    return id;
}

bool CircleOverlay::isCursorOnSegment() const
{
    if (isCursorInHub()) return false;

    if (m_selectedRing < 1 || m_selectedIndex < 0) return false;

    double dist = std::hypot(m_lastAimX, m_lastAimY);
    int innerR1 = hubRadius() + ringGap();
    int outerR1 = innerR1 + ringDepth();
    int innerR2 = outerR1 + ring2Gap();
    int outerR2 = innerR2 + ring2Depth();
    int innerR3 = outerR2 + ring3Gap();
    int outerR3 = innerR3 + ring3Depth();

    int innerR = 0;
    int outerR = 0;
    if (m_selectedRing == 1) {
        innerR = innerR1;
        outerR = outerR1;
    } else if (m_selectedRing == 2) {
        innerR = innerR2;
        outerR = outerR2;
    } else if (m_selectedRing == 3) {
        innerR = innerR3;
        outerR = outerR3;
    } else {
        return false;
    }

    if (dist < innerR || dist > outerR) {
        return false;
    }

    if (m_view && m_view->rootObject()) {
        return m_view->rootObject()->property("isCursorOnSegment").toBool();
    }
    return true;
}

bool CircleOverlay::isCursorInHub() const
{
    double dist = std::hypot(m_lastAimX, m_lastAimY);
    if (dist < (hubRadius() + ringGap() / 2.0)) {
        return true;
    }
    if (m_view && m_view->rootObject()) {
        return m_view->rootObject()->property("isCursorInHub").toBool();
    }
    return false;
}

void CircleOverlay::handleHubWheel(int delta)
{
    QList<OverlayMode> activeModes;
    if (SettingsManager::instance().categoryWindowsEnabled()) activeModes.append(OverlayMode::Windows);
    if (SettingsManager::instance().categoryAppsEnabled()) activeModes.append(OverlayMode::RecentApps);
    if (SettingsManager::instance().categoryFilesEnabled()) activeModes.append(OverlayMode::RecentFiles);
    if (activeModes.isEmpty()) activeModes.append(OverlayMode::Windows);

    if (activeModes.size() <= 1) {
        return;
    }

    int curIdx = activeModes.indexOf(m_mode);
    if (curIdx < 0) curIdx = 0;

    int newIdx = curIdx;
    if (delta < 0) {
        // Scroll DOWN: Nächste aktivierte Kategorie
        newIdx = (curIdx + 1) % activeModes.size();
    } else if (delta > 0) {
        // Scroll UP: Vorherige aktivierte Kategorie
        newIdx = (curIdx - 1 + activeModes.size()) % activeModes.size();
    }

    if (newIdx == curIdx) return;

    OverlayMode newMode = activeModes[newIdx];
    m_mode = newMode;
    m_recentStage = RecentAppsStage::Recents;
    m_activeCategoryIndex = -1;
    m_categoryPage = 0;

    if (m_mode == OverlayMode::RecentApps) {
        m_cachedRecentApps = m_appManager.loadRecentApps(SettingsManager::instance().maxRecentApps(), openAppIdentifiers());
        if (m_cachedCategories.isEmpty()) {
            m_cachedCategories = m_appManager.loadCategories();
        }
    } else if (m_mode == OverlayMode::RecentFiles) {
        m_cachedRecentFiles = m_recentFilesManager.loadRecentFiles(SettingsManager::instance().maxRecentFiles());
    }

    refreshCurrentMode();
    updateAim(m_lastAimX, m_lastAimY);
}

void CircleOverlay::resetToWindowsMode()
{
    m_mode = initialOverlayMode();
    m_recentStage = RecentAppsStage::Recents;
    m_activeCategoryIndex = -1;
    m_categoryPage = 0;
    refreshCurrentMode();
}

QString CircleOverlay::manipulationTargetWindowId() const
{
    if (!isCursorOnSegment()) return {};
    QString id = selectedWindowId();
    if (id.startsWith(QLatin1String("__")) ||
        id.startsWith(QLatin1String("app:")) ||
        id.startsWith(QLatin1String("file:")) ||
        id.startsWith(QLatin1String("cat:")) ||
        id.startsWith(QLatin1String("tray_")) ||
        id.startsWith(QLatin1String("session:"))) {
        return {};
    }
    return id;
}

bool CircleOverlay::isVisible() const
{
    return m_view && m_view->isVisible();
}

bool CircleOverlay::containsCursor(const QPoint &globalPos) const
{
    if (!m_view || !m_view->isVisible()) return false;
    return m_view->geometry().contains(globalPos);
}

void CircleOverlay::hideOverlay()
{
    m_volumeScrolledThisSession = false;
    m_isPinHovered = false;
    m_isCounterBadgeHovered = false;
    m_isGameActive = false;
    m_qmlPointerEverMoved = false;
    m_hardwareMoveCountWithoutQml = 0;
    m_monitor = nullptr;
    if (m_fallbackCursorActive) {
        m_fallbackCursorActive = false;
        emit fallbackCursorActiveChanged(false);
    }
    if (m_view && m_view->rootObject()) {
        m_view->rootObject()->setProperty("volumeScrolledThisSession", false);
    }

    // Sofort aus dem sichtbaren Bereich schieben, um jegliches Nachleuchten/Faden zu verhindern
    m_view->setPosition(-9999, -9999);
    m_view->setVisible(false);

    emit overlayHidden();
}

QStringList CircleOverlay::currentGroupWindowIds() const
{
    if (m_selectedRing == 1 && m_selectedIndex >= 0 && m_selectedIndex < m_currentRing1.size()) {
        const auto &seg = m_currentRing1[m_selectedIndex];
        if (!seg.groupWindowIds.isEmpty()) {
            return seg.groupWindowIds;
        }
        if (seg.isRealWindow && !seg.windowId.isEmpty()) {
            return { seg.windowId };
        }
    }
    return {};
}

void CircleOverlay::stepVolume(int stepPercent)
{
    m_volumeScrolledThisSession = true;
    if (m_view && m_view->rootObject()) {
        QMetaObject::invokeMethod(m_view->rootObject(), "onVolumeScrolled");
    }
    m_volumeManager.stepVolume(stepPercent);
}

void CircleOverlay::toggleMute()
{
    m_volumeManager.toggleMute();
}

void CircleOverlay::updateVolumeState(int vol, bool muted)
{
    if (m_view && m_view->rootObject()) {
        m_view->rootObject()->setProperty("systemVolume", vol);
        m_view->rootObject()->setProperty("systemMuted", muted);
    }
}

void CircleOverlay::onProgressUpdated()
{
    if (isVisible() && !m_progressThrottleTimer.isActive()) {
        m_progressThrottleTimer.start();
    }
}

void CircleOverlay::assignSegmentProgress(QList<OverlaySegment> &ring1, QList<OverlaySegment> &ring2, QList<OverlaySegment> &ring3)
{
    QSet<QString> seenAppsWithProgress;
    auto assignList = [this, &seenAppsWithProgress](QList<OverlaySegment> &segs) {
        for (auto &seg : segs) {
            if (seg.isRealWindow) {
                QString matchedApp;
                int p = m_jobProgressManager.progressForApp(seg.desktopFileName, seg.resourceClass, &matchedApp);
                if (p >= 0) {
                    bool alreadySeen = JobProgressManager::isAppKeyInSet(seenAppsWithProgress, matchedApp) ||
                                       JobProgressManager::isAppKeyInSet(seenAppsWithProgress, seg.desktopFileName) ||
                                       JobProgressManager::isAppKeyInSet(seenAppsWithProgress, seg.resourceClass);
                    if (!alreadySeen) {
                        seg.progressPercent = p;
                        JobProgressManager::addAppKeysToSet(seenAppsWithProgress, matchedApp);
                        JobProgressManager::addAppKeysToSet(seenAppsWithProgress, seg.desktopFileName);
                        JobProgressManager::addAppKeysToSet(seenAppsWithProgress, seg.resourceClass);
                    } else {
                        seg.progressPercent = -1;
                    }
                } else {
                    seg.progressPercent = -1;
                }
            } else {
                seg.progressPercent = -1;
            }
        }
    };
    assignList(ring1);
    assignList(ring2);
    assignList(ring3);
}

void CircleOverlay::updateSegmentProgress()
{
    if (!isVisible() || !m_view || !m_view->rootObject()) {
        return;
    }

    QSet<QString> seenAppsWithProgress;
    auto updateList = [this, &seenAppsWithProgress](QList<OverlaySegment> &segments, const char *propName) {
        if (segments.isEmpty()) return;
        bool anyChanged = false;
        QVariantList qmlList = m_view->rootObject()->property(propName).toList();
        for (int i = 0; i < segments.size(); ++i) {
            OverlaySegment &seg = segments[i];
            if (seg.isRealWindow) {
                QString matchedApp;
                int p = m_jobProgressManager.progressForApp(seg.desktopFileName, seg.resourceClass, &matchedApp);
                if (p >= 0) {
                    bool alreadySeen = JobProgressManager::isAppKeyInSet(seenAppsWithProgress, matchedApp) ||
                                       JobProgressManager::isAppKeyInSet(seenAppsWithProgress, seg.desktopFileName) ||
                                       JobProgressManager::isAppKeyInSet(seenAppsWithProgress, seg.resourceClass);
                    if (!alreadySeen) {
                        JobProgressManager::addAppKeysToSet(seenAppsWithProgress, matchedApp);
                        JobProgressManager::addAppKeysToSet(seenAppsWithProgress, seg.desktopFileName);
                        JobProgressManager::addAppKeysToSet(seenAppsWithProgress, seg.resourceClass);
                    } else {
                        p = -1;
                    }
                }
                if (seg.progressPercent != p) {
                    seg.progressPercent = p;
                    anyChanged = true;
                    if (i < qmlList.size()) {
                        QVariantMap m = qmlList[i].toMap();
                        m[QStringLiteral("progressPercent")] = p;
                        qmlList[i] = m;
                    }
                }
            }
        }
        if (anyChanged) {
            m_view->rootObject()->setProperty(propName, qmlList);
        }
    };

    updateList(m_currentRing1, "segmentList1");
    updateList(m_currentRing2, "segmentList2");
    updateList(m_currentRing3, "segmentList3");
}
