#pragma once

#include <QObject>
#include <QPoint>
#include <QIcon>
#include <QList>
#include <QHash>
#include <QDateTime>
#include <memory>
#include "AppManager.h"
#include "KaryonGlyphs.h"
#include "TrayManager.h"
#include "RecentFilesManager.h"
#include "PinnedManager.h"
#include "MediaManager.h"
#include "VolumeManager.h"
#include "JobProgressManager.h"

class QQuickView;
class InputMonitor;
struct WindowInfo;

struct OverlaySegment {
    QString windowId;
    QString appKey;
    QString resourceClass;
    QString desktopFileName;
    QString caption;
    QString tooltipText;
    double angle = 0.0; // Radians
    double span = 0.0;  // Radians
    int ring = 1;       // 1, 2, or 3
    bool isTextSegment = false;
    QString label;
    bool hasDrilldown = false;
    QString segmentType; // "window", "file", "app", or ""
    bool isRealWindow = false; // true if this is an actual open window
    bool canPin = false; // true for window, app, file
    bool isPinned = false; // true if currently pinned
    int progressPercent = -1; // 0 to 100, or -1 if no active job
    QString trayProgramKey;   // Non-empty ("service|path") if segment mirrors a running tray program
    int windowCount = 1;
    QStringList groupWindowIds;
};

enum class OverlayMode {
    Windows,
    RecentApps,
    RecentFiles
};

enum class RecentAppsStage {
    Recents,
    Categories,
    Favorites,
    Tray,
    Session
};

class CircleOverlay : public QObject
{
    Q_OBJECT

public:
    static constexpr int HUB_RADIUS = 75;           // Durchmesser 150 px
    static constexpr int HUB_DIAMETER = HUB_RADIUS * 2;
    static constexpr int DEADZONE_RADIUS = 10;      // Durchmesser 20 px Totzone
    static constexpr int CURSOR_OFFSET_Y = 20;      // Cursor 20px über Zentrum
    static constexpr int RING_GAP = 4;              // Abstand Hub -> Ring 1
    static constexpr int RING_DEPTH = 76;           // Erhöhte Tiefe/Höhe der Segmente (+20px)
    static constexpr int RING2_GAP = 4;             // Abstand Ring 1 -> Ring 2
    static constexpr int RING2_DEPTH = 76;          // Tiefe der Segmente auf Ring 2
    static constexpr int RING3_GAP = 4;             // Abstand Ring 2 -> Ring 3
    static constexpr int RING3_DEPTH = 76;          // Tiefe der Segmente auf Ring 3
    static constexpr int SEG_GAP = 2;               // Abstand zwischen Segmenten
    static constexpr double SEGMENT_WIDTH = 60.5;   // Verschlankte Segmentbreite (11+4 Architektur)
    static constexpr int ICON_SIZE = 28;            // Icongröße (verschlankt von 32px)
    static constexpr int MAX_RING1_SEGMENTS = 11;   // Ungerade Maximalanzahl für vertikale Symmetrie (11 Segmente, 11+4 Architektur)
    static constexpr int AIM_MARGIN = 95;           // Transparenter Rand für unterbrechungsfreies Aiming & Tooltip

    explicit CircleOverlay(QObject *parent = nullptr);
    ~CircleOverlay() override;

    void prewarm();
    void showAt(const QPoint &cursorPos, const QList<WindowInfo> &windows, InputMonitor *monitor = nullptr);
    void refresh(const QList<WindowInfo> &windows);
    void updateAim(int offsetX, int offsetY);
    QString selectedWindowId() const;
    bool isCursorOnSegment() const;
    bool isCursorInHub() const;
    QString manipulationTargetWindowId() const;
    bool isVisible() const;

    int centerX() const { return m_centerX; }
    int centerY() const { return m_centerY; }
    bool containsCursor(const QPoint &globalPos) const;

    void handleHubWheel(int delta);
    OverlayMode mode() const { return m_mode; }
    void resetToWindowsMode();

    void setShowDesktopEnabled(bool enabled) { m_showDesktopEnabled = enabled; }
    bool isShowDesktopEnabled() const { return m_showDesktopEnabled; }
    void setShowTrayEnabled(bool enabled) { m_showTrayEnabled = enabled; }
    bool isShowTrayEnabled() const { return m_showTrayEnabled; }
    void setShowSessionEnabled(bool enabled) { m_showSessionEnabled = enabled; }
    bool isShowSessionEnabled() const { return m_showSessionEnabled; }
    void setShowMenuEnabled(bool enabled) { m_showMenuEnabled = enabled; }
    bool isShowMenuEnabled() const { return m_showMenuEnabled; }
    void setShowMediaControlsEnabled(bool enabled) { m_showMediaControlsEnabled = enabled; }
    bool isShowMediaControlsEnabled() const { return m_showMediaControlsEnabled; }
    void setShowVolumeControlEnabled(bool enabled) { m_showVolumeControlEnabled = enabled; }
    bool isShowVolumeControlEnabled() const { return m_showVolumeControlEnabled; }
    void setDesktopShowing(bool showing) { m_desktopShowing = showing; }
    bool isDesktopShowing() const { return m_desktopShowing; }

    OverlayMode initialOverlayMode() const;

    bool isPinHovered() const { return m_isPinHovered; }
    void togglePinForCurrentSelection();
    void markFileLaunched(const QString &filePath);

    bool isCounterBadgeHovered() const { return m_isCounterBadgeHovered; }
    QStringList currentGroupWindowIds() const;

    bool isCursorInVolumeArea() const { return m_showVolumeControlEnabled && m_isVolumeHovered; }
    bool volumeScrolledThisSession() const { return m_volumeScrolledThisSession; }
    void stepVolume(int stepPercent);
    void toggleMute();
    void updateVolumeState(int vol, bool muted);

    void mediaPlayPause() { m_mediaManager.playPause(); }
    void mediaNext() { m_mediaManager.next(); }
    void mediaPrevious() { m_mediaManager.previous(); }
    MediaManager *mediaManager() { return &m_mediaManager; }
    VolumeManager *volumeManager() { return &m_volumeManager; }

public slots:
    void hideOverlay();
    void handleAimChanged(int offsetX, int offsetY, bool isDrilldown, const QString &curId, int selRing, int selIdx, bool isPinHovered = false, bool isVolumeHovered = false, bool isCounterBadgeHovered = false);

signals:
    void overlayShown();
    void overlayHidden();

private:
    std::unique_ptr<QQuickView> m_view;
    double currentScale() const;
    int hubRadius() const;
    int ringGap() const;
    int ringDepth() const;
    int ring2Gap() const;
    int ring2Depth() const;
    int ring3Gap() const;
    int ring3Depth() const;
    int segGap() const;
    double segmentWidth() const;
    int iconSize() const;
    int aimMargin() const;
    int cursorOffsetY() const;
    int deadzoneRadius() const;
    void updateScaleProperties();
    void applyWindowProperties();

    int visualRadius(bool hasRing2, bool hasRing3) const;
    int windowRadius(bool hasRing2, bool hasRing3) const;
    int totalRadius(bool hasRing2, bool hasRing3 = false) const { return visualRadius(hasRing2, hasRing3); }
    QIcon resolveWindowIcon(const QString &desktopFileName, const QString &resourceClass);
    QIcon resolveIcon(const QString &name);
    bool resolveSteamGame(const QString &steamId, QString &outName, QIcon &outIcon);
    QString findDesktopFile(const QString &name);
    QString iconNameFromDesktopFile(const QString &path);
    QString appNameFromDesktopFile(const QString &path);
    QString resolveAppName(const QString &desktopFileName, const QString &resourceClass);
    QString formatWindowTooltip(const QString &appName, const QString &caption, const QString &resourceClass);
    QSet<QString> openAppIdentifiers() const;

    void partitionWindows(const QList<WindowInfo> &mru,
                          QList<OverlaySegment> &outRing1,
                          int &outRing1CenterIndex,
                          QList<OverlaySegment> &outRing2,
                          QList<OverlaySegment> &outRing3);

    void partitionRecentApps(QList<OverlaySegment> &outRing1,
                            int &outRing1CenterIndex,
                            QList<OverlaySegment> &outRing2,
                            QList<OverlaySegment> &outRing3);

    void partitionCategories(QList<OverlaySegment> &outRing1,
                            int activeCatIndex,
                            QList<OverlaySegment> &outRing2,
                            QList<OverlaySegment> &outRing3);

    void partitionFavorites(QList<OverlaySegment> &outRing1,
                            QList<OverlaySegment> &outRing2,
                            QList<OverlaySegment> &outRing3);

    void partitionTray(QList<OverlaySegment> &outRing1,
                       QList<OverlaySegment> &outRing2,
                       QList<OverlaySegment> &outRing3);

    void partitionSession(QList<OverlaySegment> &outRing1,
                          QList<OverlaySegment> &outRing2);

    void partitionRecentFiles(QList<OverlaySegment> &outRing1,
                             int &outRing1CenterIndex,
                             QList<OverlaySegment> &outRing2,
                             QList<OverlaySegment> &outRing3);

    void populateSegments(const QList<WindowInfo> &windows, bool &outHasRing2, bool &outHasRing3);
    void populateRecentAppsSegments(bool &outHasRing2, bool &outHasRing3);
    void populateRecentFilesSegments(bool &outHasRing2, bool &outHasRing3);
    void refreshCurrentMode();

    QHash<QString, QString> m_iconPathCache;
    QHash<QString, QString> m_appNameCache;
    int m_lastAimX = 0;
    int m_lastAimY = -CURSOR_OFFSET_Y;
    int m_centerX = 0;
    int m_centerY = 0;
    bool m_showDesktopEnabled = true;
    bool m_showTrayEnabled = true;
    bool m_showSessionEnabled = true;
    bool m_showMenuEnabled = true;
    bool m_showFavoritesEnabled = true;
    bool m_showMediaControlsEnabled = true;
    bool m_showVolumeControlEnabled = true;
    bool m_desktopShowing = false;

    OverlayMode m_mode = OverlayMode::Windows;
    RecentAppsStage m_recentStage = RecentAppsStage::Recents;
    AppManager m_appManager;
    TrayManager m_trayManager;
    RecentFilesManager m_recentFilesManager;
    PinnedManager m_pinnedManager;
    bool m_isPinHovered = false;
    bool m_isCounterBadgeHovered = false;
    int m_selectedRing = -1;
    int m_selectedIndex = -1;
    QList<AppEntry> m_cachedRecentApps;
    QList<AppEntry> m_cachedFavorites;
    QList<CategoryEntry> m_cachedCategories;
    QList<RecentFileItem> m_cachedRecentFiles;
    QList<TrayProgramItem> m_cachedTrayPrograms;
    int m_trayIconRevision = 0;
    QList<TrayMenuAction> m_cachedMenuActions;
    QString m_activeTrayProgramKey;
    bool m_trayMoreHovered = false;
    int m_activeCategoryIndex = -1;
    int m_categoryPage = 0;
    QList<WindowInfo> m_cachedWindows;
    QList<OverlaySegment> m_currentRing1;
    QList<OverlaySegment> m_currentRing2;
    QList<OverlaySegment> m_currentRing3;
    void appendMediaSegments(QList<OverlaySegment> &outRing1);

    MediaManager m_mediaManager;
    VolumeManager m_volumeManager;
    bool m_isVolumeHovered = false;
    bool m_volumeScrolledThisSession = false;

    JobProgressManager m_jobProgressManager;
    QTimer m_progressThrottleTimer;
    void onProgressUpdated();
    void updateSegmentProgress();
    void assignSegmentProgress(QList<OverlaySegment> &ring1, QList<OverlaySegment> &ring2, QList<OverlaySegment> &ring3);

    QHash<QString, qint64> m_launchedFiles;
    QHash<QString, QString> m_steamNameCache;
    QHash<QString, QIcon> m_steamIconCache;
};
