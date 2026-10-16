#pragma once

#include <QObject>
#include <QSettings>

class SettingsManager : public QObject
{
    Q_OBJECT

public:
    static SettingsManager &instance();

    int triggerHoldMs() const;
    void setTriggerHoldMs(int ms);

    bool gameModeEnabled() const;
    void setGameModeEnabled(bool enabled);

    double overlayMouseSpeed() const;
    void setOverlayMouseSpeed(double speed);

    double overlayScale() const;
    void setOverlayScale(double scale);

    double overlayTransparency() const;
    void setOverlayTransparency(double pct);

    int maxRecentApps() const;
    void setMaxRecentApps(int count);

    int maxRecentFiles() const;
    void setMaxRecentFiles(int count);

    int volumeStep() const;
    void setVolumeStep(int step);

    bool categoryWindowsEnabled() const;
    void setCategoryWindowsEnabled(bool enabled);

    bool categoryAppsEnabled() const;
    void setCategoryAppsEnabled(bool enabled);

    bool categoryFilesEnabled() const;
    void setCategoryFilesEnabled(bool enabled);

    bool showDesktop() const;
    void setShowDesktop(bool enabled);

    bool showTray() const;
    void setShowTray(bool enabled);

    bool showSession() const;
    void setShowSession(bool enabled);

    bool showMenu() const;
    void setShowMenu(bool enabled);

    bool showFavorites() const;
    void setShowFavorites(bool enabled);

    bool showMediaControls() const;
    void setShowMediaControls(bool enabled);

    bool showClock() const;
    void setShowClock(bool enabled);

    bool showDate() const;
    void setShowDate(bool enabled);

    bool showVolumeControl() const;
    void setShowVolumeControl(bool enabled);

    int dateFormatIndex() const;
    void setDateFormatIndex(int index);

    QString dateSeparator() const;
    void setDateSeparator(const QString &sep);

    int timeFormatIndex() const;
    void setTimeFormatIndex(int index);

    bool gesturesEnabled() const;
    void setGesturesEnabled(bool enabled);

    QString gestureLeft() const;
    void setGestureLeft(const QString &action);

    QString gestureRight() const;
    void setGestureRight(const QString &action);

    QString gestureUp() const;
    void setGestureUp(const QString &action);

    QString gestureDown() const;
    void setGestureDown(const QString &action);

    QString gestureLeftUp() const;
    void setGestureLeftUp(const QString &action);

    QString gestureRightUp() const;
    void setGestureRightUp(const QString &action);

    QString gestureLeftDown() const;
    void setGestureLeftDown(const QString &action);

    QString gestureRightDown() const;
    void setGestureRightDown(const QString &action);

    QString gestureAction(int direction) const;
    void setGestureAction(int direction, const QString &action);

    QString gestureCustomApp(int direction) const;
    void setGestureCustomApp(int direction, const QString &cmd);
    QString gestureCustomAppName(int direction) const;
    void setGestureCustomAppName(int direction, const QString &name);

    QString gestureCustomKeyName(int direction) const;
    void setGestureCustomKeyName(int direction, const QString &name);
    QString gestureCustomKeySequence(int direction) const;
    void setGestureCustomKeySequence(int direction, const QString &seq);

    void resetDefaults();

signals:
    void settingsChanged();
    void gameModeChanged(bool enabled);

private:
    explicit SettingsManager(QObject *parent = nullptr);
    void load();
    void save();

    int m_triggerHoldMs = 120;
    bool m_gameModeEnabled = true;
    double m_overlayMouseSpeed = -0.5;
    double m_overlayScale = 0.9;
    double m_overlayTransparency = 4.0;
    int m_maxRecentApps = 12;
    int m_maxRecentFiles = 12;
    int m_volumeStep = 3;
    bool m_categoryWindowsEnabled = true;
    bool m_categoryAppsEnabled = true;
    bool m_categoryFilesEnabled = true;
    bool m_showDesktop = true;
    bool m_showTray = true;
    bool m_showSession = true;
    bool m_showMenu = true;
    bool m_showFavorites = true;
    bool m_showMediaControls = true;
    bool m_showClock = true;
    bool m_showDate = true;
    bool m_showVolumeControl = true;
    int m_dateFormatIndex = 0;
    QString m_dateSeparator = QStringLiteral(".");
    int m_timeFormatIndex = 0;

    bool m_gesturesEnabled = true;
    QString m_gestureLeft = QStringLiteral("select_all");
    QString m_gestureRight = QStringLiteral("paste");
    QString m_gestureUp = QStringLiteral("reload");
    QString m_gestureDown = QStringLiteral("enter");
    QString m_gestureLeftUp = QStringLiteral("copy");
    QString m_gestureRightUp = QStringLiteral("redo");
    QString m_gestureLeftDown = QStringLiteral("cut");
    QString m_gestureRightDown = QStringLiteral("undo");

    QString m_gestureCustomApp[8];
    QString m_gestureCustomAppName[8];
    QString m_gestureCustomKeyName[8];
    QString m_gestureCustomKeySequence[8];

    QSettings m_settings;
};
