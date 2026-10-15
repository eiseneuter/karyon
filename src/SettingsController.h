#pragma once

#include <QObject>
#include <QQuickView>
#include <QPoint>

class SettingsController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int triggerHoldMs READ triggerHoldMs WRITE setTriggerHoldMs NOTIFY triggerHoldMsChanged)
    Q_PROPERTY(bool gameMode READ gameMode WRITE setGameMode NOTIFY gameModeChanged)
    Q_PROPERTY(double overlayMouseSpeed READ overlayMouseSpeed WRITE setOverlayMouseSpeed NOTIFY overlayMouseSpeedChanged)
    Q_PROPERTY(double overlayScale READ overlayScale WRITE setOverlayScale NOTIFY overlayScaleChanged)
    Q_PROPERTY(double overlayTransparency READ overlayTransparency WRITE setOverlayTransparency NOTIFY overlayTransparencyChanged)
    Q_PROPERTY(int maxRecentApps READ maxRecentApps WRITE setMaxRecentApps NOTIFY maxRecentAppsChanged)
    Q_PROPERTY(int maxRecentFiles READ maxRecentFiles WRITE setMaxRecentFiles NOTIFY maxRecentFilesChanged)
    Q_PROPERTY(int volumeStep READ volumeStep WRITE setVolumeStep NOTIFY volumeStepChanged)
    Q_PROPERTY(bool categoryWindowsEnabled READ categoryWindowsEnabled WRITE setCategoryWindowsEnabled NOTIFY categoryWindowsEnabledChanged)
    Q_PROPERTY(bool categoryAppsEnabled READ categoryAppsEnabled WRITE setCategoryAppsEnabled NOTIFY categoryAppsEnabledChanged)
    Q_PROPERTY(bool categoryFilesEnabled READ categoryFilesEnabled WRITE setCategoryFilesEnabled NOTIFY categoryFilesEnabledChanged)
    Q_PROPERTY(bool showDesktop READ showDesktop WRITE setShowDesktop NOTIFY showDesktopChanged)
    Q_PROPERTY(bool showTray READ showTray WRITE setShowTray NOTIFY showTrayChanged)
    Q_PROPERTY(bool showSession READ showSession WRITE setShowSession NOTIFY showSessionChanged)
    Q_PROPERTY(bool showMenu READ showMenu WRITE setShowMenu NOTIFY showMenuChanged)
    Q_PROPERTY(bool showFavorites READ showFavorites WRITE setShowFavorites NOTIFY showFavoritesChanged)
    Q_PROPERTY(bool showMediaControls READ showMediaControls WRITE setShowMediaControls NOTIFY showMediaControlsChanged)
    Q_PROPERTY(bool showClock READ showClock WRITE setShowClock NOTIFY showClockChanged)
    Q_PROPERTY(bool showDate READ showDate WRITE setShowDate NOTIFY showDateChanged)
    Q_PROPERTY(bool showVolumeControl READ showVolumeControl WRITE setShowVolumeControl NOTIFY showVolumeControlChanged)

    Q_PROPERTY(bool gesturesEnabled READ gesturesEnabled WRITE setGesturesEnabled NOTIFY gesturesEnabledChanged)
    Q_PROPERTY(bool isRecordingKey READ isRecordingKey WRITE setIsRecordingKey NOTIFY isRecordingKeyChanged)
    Q_PROPERTY(QString gestureLeft READ gestureLeft WRITE setGestureLeft NOTIFY gestureLeftChanged)
    Q_PROPERTY(QString gestureRight READ gestureRight WRITE setGestureRight NOTIFY gestureRightChanged)
    Q_PROPERTY(QString gestureUp READ gestureUp WRITE setGestureUp NOTIFY gestureUpChanged)
    Q_PROPERTY(QString gestureDown READ gestureDown WRITE setGestureDown NOTIFY gestureDownChanged)
    Q_PROPERTY(QString gestureLeftUp READ gestureLeftUp WRITE setGestureLeftUp NOTIFY gestureLeftUpChanged)
    Q_PROPERTY(QString gestureRightUp READ gestureRightUp WRITE setGestureRightUp NOTIFY gestureRightUpChanged)
    Q_PROPERTY(QString gestureLeftDown READ gestureLeftDown WRITE setGestureLeftDown NOTIFY gestureLeftDownChanged)
    Q_PROPERTY(QString gestureRightDown READ gestureRightDown WRITE setGestureRightDown NOTIFY gestureRightDownChanged)
    Q_PROPERTY(bool hasInputPermissions READ hasInputPermissions NOTIFY inputPermissionsChanged)

public:
    explicit SettingsController(QObject *parent = nullptr);
    ~SettingsController() override;

    int triggerHoldMs() const;
    void setTriggerHoldMs(int ms);

    bool gameMode() const;
    void setGameMode(bool enabled);

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

    Q_INVOKABLE QStringList gestureActionIds() const;
    Q_INVOKABLE QStringList gestureActionNames() const;
    Q_INVOKABLE QString gestureActionName(const QString &id) const;

    Q_INVOKABLE void selectCustomApp(const QString &direction);
    Q_INVOKABLE void setCustomKey(const QString &direction, const QString &name, const QString &sequence);
    Q_INVOKABLE QString gestureActionDisplay(const QString &direction, const QString &hint = QString()) const;
    Q_INVOKABLE QString actionDisplayForId(const QString &id, const QString &direction) const;
    Q_INVOKABLE QString gestureCustomKeySequence(const QString &direction) const;
    Q_INVOKABLE QVariantMap processKeyEvent(int key, int modifiers, const QString &text) const;

    bool isRecordingKey() const { return m_isRecordingKey; }
    void setIsRecordingKey(bool rec);

    Q_INVOKABLE void resetDefaults();
    Q_INVOKABLE void hideSettings();
    Q_INVOKABLE void quitApp();
    Q_INVOKABLE void startWindowDrag();

    bool hasInputPermissions() const;
    Q_INVOKABLE bool requestInputPermissions();
    Q_INVOKABLE bool revokeInputPermissions();

    void showSettings(const QPoint &cursorPos = QPoint());

    bool eventFilter(QObject *obj, QEvent *event) override;

signals:
    void inputPermissionsChanged();
    void triggerHoldMsChanged();
    void gameModeChanged();
    void overlayMouseSpeedChanged();
    void overlayScaleChanged();
    void overlayTransparencyChanged();
    void maxRecentAppsChanged();
    void maxRecentFilesChanged();
    void volumeStepChanged();
    void categoryWindowsEnabledChanged();
    void categoryAppsEnabledChanged();
    void categoryFilesEnabledChanged();
    void showDesktopChanged();
    void showTrayChanged();
    void showSessionChanged();
    void showMenuChanged();
    void showFavoritesChanged();
    void showMediaControlsChanged();
    void showClockChanged();
    void showDateChanged();
    void showVolumeControlChanged();
    void gesturesEnabledChanged();
    void isRecordingKeyChanged();
    void gestureLeftChanged();
    void gestureRightChanged();
    void gestureUpChanged();
    void gestureDownChanged();
    void gestureLeftUpChanged();
    void gestureRightUpChanged();
    void gestureLeftDownChanged();
    void gestureRightDownChanged();

private:
    void emitGesturesChanged(int dir);

    QQuickView *m_view = nullptr;
    bool m_isRecordingKey = false;
};

