#include "SettingsManager.h"
#include <algorithm>

SettingsManager &SettingsManager::instance()
{
    static SettingsManager s_instance;
    return s_instance;
}

SettingsManager::SettingsManager(QObject *parent)
    : QObject(parent)
    , m_settings(QStringLiteral("karyon"), QStringLiteral("karyon"))
{
    load();
}

void SettingsManager::load()
{
    m_triggerHoldMs = m_settings.value(QStringLiteral("trigger_hold_ms"), 120).toInt();
    m_triggerHoldMs = std::clamp(m_triggerHoldMs, 50, 800);

    m_gameModeEnabled = m_settings.value(QStringLiteral("game_mode_enabled"), true).toBool();

    m_overlayMouseSpeed = m_settings.value(QStringLiteral("overlay_mouse_speed"), -0.5).toDouble();
    m_overlayMouseSpeed = std::clamp(m_overlayMouseSpeed, -1.0, 1.0);

    m_overlayScale = m_settings.value(QStringLiteral("overlay_scale"), 0.9).toDouble();
    m_overlayScale = std::clamp(m_overlayScale, 0.8, 1.3);

    m_overlayTransparency = m_settings.value(QStringLiteral("overlay_transparency"), 4.0).toDouble();
    m_overlayTransparency = std::clamp(m_overlayTransparency, 0.0, 20.0);

    m_maxRecentApps = m_settings.value(QStringLiteral("max_recent_apps"), 12).toInt();
    m_maxRecentApps = std::clamp(m_maxRecentApps, 1, 30);

    m_maxRecentFiles = m_settings.value(QStringLiteral("max_recent_files"), 12).toInt();
    m_maxRecentFiles = std::clamp(m_maxRecentFiles, 1, 30);

    m_volumeStep = m_settings.value(QStringLiteral("volume_step"), 3).toInt();
    m_volumeStep = std::clamp(m_volumeStep, 1, 20);

    m_categoryWindowsEnabled = m_settings.value(QStringLiteral("category_windows_enabled"), true).toBool();
    m_categoryAppsEnabled = m_settings.value(QStringLiteral("category_apps_enabled"), true).toBool();
    m_categoryFilesEnabled = m_settings.value(QStringLiteral("category_files_enabled"), true).toBool();
    if (!m_categoryWindowsEnabled && !m_categoryAppsEnabled && !m_categoryFilesEnabled) {
        m_categoryWindowsEnabled = true;
    }

    m_showDesktop = m_settings.value(QStringLiteral("show_desktop"), true).toBool();
    m_showTray = m_settings.value(QStringLiteral("show_tray"), true).toBool();
    m_showSession = m_settings.value(QStringLiteral("show_session"), true).toBool();
    m_showMenu = m_settings.value(QStringLiteral("show_menu"), true).toBool();
    m_showFavorites = m_settings.value(QStringLiteral("show_favorites"), false).toBool();
    if (m_showMenu && m_showFavorites) {
        m_showFavorites = false;
    }
    m_showMediaControls = m_settings.value(QStringLiteral("show_media_controls"), true).toBool();
    m_showClock = m_settings.value(QStringLiteral("show_clock"), true).toBool();
    m_showDate = m_settings.value(QStringLiteral("show_date"), true).toBool();
    m_showVolumeControl = m_settings.value(QStringLiteral("show_volume_control"), true).toBool();
    m_dateFormatIndex = std::clamp(m_settings.value(QStringLiteral("date_format_index"), 0).toInt(), 0, 13);
    m_dateSeparator = m_settings.value(QStringLiteral("date_separator"), QStringLiteral(".")).toString();
    if (m_dateSeparator != QLatin1String(".") && m_dateSeparator != QLatin1String("/") && m_dateSeparator != QLatin1String("-")) {
        m_dateSeparator = QStringLiteral(".");
    }
    m_timeFormatIndex = std::clamp(m_settings.value(QStringLiteral("time_format_index"), 0).toInt(), 0, 1);

    m_gestureLeft = m_settings.value(QStringLiteral("gesture_left"), QStringLiteral("select_all")).toString();
    m_gestureRight = m_settings.value(QStringLiteral("gesture_right"), QStringLiteral("paste")).toString();
    m_gestureUp = m_settings.value(QStringLiteral("gesture_up"), QStringLiteral("reload")).toString();
    m_gestureDown = m_settings.value(QStringLiteral("gesture_down"), QStringLiteral("enter")).toString();
    m_gestureLeftUp = m_settings.value(QStringLiteral("gesture_left_up"), QStringLiteral("copy")).toString();
    m_gestureRightUp = m_settings.value(QStringLiteral("gesture_right_up"), QStringLiteral("redo")).toString();
    m_gestureLeftDown = m_settings.value(QStringLiteral("gesture_left_down"), QStringLiteral("cut")).toString();
    m_gestureRightDown = m_settings.value(QStringLiteral("gesture_right_down"), QStringLiteral("undo")).toString();

    m_gesturesEnabled = m_settings.value(QStringLiteral("gestures_enabled"), true).toBool();
    for (int i = 0; i < 8; ++i) {
        m_gestureCustomApp[i] = m_settings.value(QStringLiteral("gesture_custom_app_%1").arg(i)).toString();
        m_gestureCustomAppName[i] = m_settings.value(QStringLiteral("gesture_custom_app_name_%1").arg(i)).toString();
        m_gestureCustomKeyName[i] = m_settings.value(QStringLiteral("gesture_custom_key_name_%1").arg(i)).toString();
        m_gestureCustomKeySequence[i] = m_settings.value(QStringLiteral("gesture_custom_key_seq_%1").arg(i)).toString();
    }
}

void SettingsManager::save()
{
    m_settings.setValue(QStringLiteral("trigger_hold_ms"), m_triggerHoldMs);
    m_settings.setValue(QStringLiteral("game_mode_enabled"), m_gameModeEnabled);
    m_settings.setValue(QStringLiteral("overlay_mouse_speed"), m_overlayMouseSpeed);
    m_settings.setValue(QStringLiteral("overlay_scale"), m_overlayScale);
    m_settings.setValue(QStringLiteral("overlay_transparency"), m_overlayTransparency);
    m_settings.setValue(QStringLiteral("max_recent_apps"), m_maxRecentApps);
    m_settings.setValue(QStringLiteral("max_recent_files"), m_maxRecentFiles);
    m_settings.setValue(QStringLiteral("volume_step"), m_volumeStep);
    m_settings.setValue(QStringLiteral("category_windows_enabled"), m_categoryWindowsEnabled);
    m_settings.setValue(QStringLiteral("category_apps_enabled"), m_categoryAppsEnabled);
    m_settings.setValue(QStringLiteral("category_files_enabled"), m_categoryFilesEnabled);
    m_settings.setValue(QStringLiteral("show_desktop"), m_showDesktop);
    m_settings.setValue(QStringLiteral("show_tray"), m_showTray);
    m_settings.setValue(QStringLiteral("show_session"), m_showSession);
    m_settings.setValue(QStringLiteral("show_menu"), m_showMenu);
    m_settings.setValue(QStringLiteral("show_favorites"), m_showFavorites);
    m_settings.setValue(QStringLiteral("show_media_controls"), m_showMediaControls);
    m_settings.setValue(QStringLiteral("show_clock"), m_showClock);
    m_settings.setValue(QStringLiteral("show_date"), m_showDate);
    m_settings.setValue(QStringLiteral("show_volume_control"), m_showVolumeControl);
    m_settings.setValue(QStringLiteral("date_format_index"), m_dateFormatIndex);
    m_settings.setValue(QStringLiteral("date_separator"), m_dateSeparator);
    m_settings.setValue(QStringLiteral("time_format_index"), m_timeFormatIndex);
    m_settings.setValue(QStringLiteral("gestures_enabled"), m_gesturesEnabled);
    m_settings.setValue(QStringLiteral("gesture_left"), m_gestureLeft);
    m_settings.setValue(QStringLiteral("gesture_right"), m_gestureRight);
    m_settings.setValue(QStringLiteral("gesture_up"), m_gestureUp);
    m_settings.setValue(QStringLiteral("gesture_down"), m_gestureDown);
    m_settings.setValue(QStringLiteral("gesture_left_up"), m_gestureLeftUp);
    m_settings.setValue(QStringLiteral("gesture_right_up"), m_gestureRightUp);
    m_settings.setValue(QStringLiteral("gesture_left_down"), m_gestureLeftDown);
    m_settings.setValue(QStringLiteral("gesture_right_down"), m_gestureRightDown);
    for (int i = 0; i < 8; ++i) {
        m_settings.setValue(QStringLiteral("gesture_custom_app_%1").arg(i), m_gestureCustomApp[i]);
        m_settings.setValue(QStringLiteral("gesture_custom_app_name_%1").arg(i), m_gestureCustomAppName[i]);
        m_settings.setValue(QStringLiteral("gesture_custom_key_name_%1").arg(i), m_gestureCustomKeyName[i]);
        m_settings.setValue(QStringLiteral("gesture_custom_key_seq_%1").arg(i), m_gestureCustomKeySequence[i]);
    }
    m_settings.sync();
}

int SettingsManager::triggerHoldMs() const
{
    return m_triggerHoldMs;
}

void SettingsManager::setTriggerHoldMs(int ms)
{
    int clamped = std::clamp(ms, 50, 800);
    if (m_triggerHoldMs != clamped) {
        m_triggerHoldMs = clamped;
        save();
        emit settingsChanged();
    }
}

bool SettingsManager::gameModeEnabled() const
{
    return m_gameModeEnabled;
}

void SettingsManager::setGameModeEnabled(bool enabled)
{
    if (m_gameModeEnabled != enabled) {
        m_gameModeEnabled = enabled;
        save();
        emit settingsChanged();
        emit gameModeChanged(enabled);
    }
}

double SettingsManager::overlayMouseSpeed() const
{
    return m_overlayMouseSpeed;
}

void SettingsManager::setOverlayMouseSpeed(double speed)
{
    double clamped = std::clamp(speed, -1.0, 1.0);
    if (std::abs(m_overlayMouseSpeed - clamped) > 1e-4) {
        m_overlayMouseSpeed = clamped;
        save();
        emit settingsChanged();
    }
}

double SettingsManager::overlayScale() const
{
    return m_overlayScale;
}

void SettingsManager::setOverlayScale(double scale)
{
    double clamped = std::clamp(scale, 0.8, 1.3);
    clamped = std::round(clamped * 10.0) / 10.0;
    if (std::abs(m_overlayScale - clamped) > 1e-4) {
        m_overlayScale = clamped;
        save();
        emit settingsChanged();
    }
}

double SettingsManager::overlayTransparency() const
{
    return m_overlayTransparency;
}

void SettingsManager::setOverlayTransparency(double pct)
{
    double clamped = std::clamp(pct, 0.0, 20.0);
    clamped = std::round(clamped * 10.0) / 10.0;
    if (std::abs(m_overlayTransparency - clamped) > 1e-4) {
        m_overlayTransparency = clamped;
        save();
        emit settingsChanged();
    }
}

int SettingsManager::maxRecentApps() const
{
    return m_maxRecentApps;
}

void SettingsManager::setMaxRecentApps(int count)
{
    int clamped = std::clamp(count, 1, 30);
    if (m_maxRecentApps != clamped) {
        m_maxRecentApps = clamped;
        save();
        emit settingsChanged();
    }
}

int SettingsManager::maxRecentFiles() const
{
    return m_maxRecentFiles;
}

void SettingsManager::setMaxRecentFiles(int count)
{
    int clamped = std::clamp(count, 1, 30);
    if (m_maxRecentFiles != clamped) {
        m_maxRecentFiles = clamped;
        save();
        emit settingsChanged();
    }
}

int SettingsManager::volumeStep() const
{
    return m_volumeStep;
}

void SettingsManager::setVolumeStep(int step)
{
    int clamped = std::clamp(step, 1, 20);
    if (m_volumeStep != clamped) {
        m_volumeStep = clamped;
        save();
        emit settingsChanged();
    }
}

bool SettingsManager::categoryWindowsEnabled() const
{
    return m_categoryWindowsEnabled;
}

void SettingsManager::setCategoryWindowsEnabled(bool enabled)
{
    if (!enabled && !m_categoryAppsEnabled && !m_categoryFilesEnabled) {
        return; // Mindestens eine Kategorie muss aktiviert bleiben
    }
    if (m_categoryWindowsEnabled != enabled) {
        m_categoryWindowsEnabled = enabled;
        save();
        emit settingsChanged();
    }
}

bool SettingsManager::categoryAppsEnabled() const
{
    return m_categoryAppsEnabled;
}

void SettingsManager::setCategoryAppsEnabled(bool enabled)
{
    if (!enabled && !m_categoryWindowsEnabled && !m_categoryFilesEnabled) {
        return; // Mindestens eine Kategorie muss aktiviert bleiben
    }
    if (m_categoryAppsEnabled != enabled) {
        m_categoryAppsEnabled = enabled;
        save();
        emit settingsChanged();
    }
}

bool SettingsManager::categoryFilesEnabled() const
{
    return m_categoryFilesEnabled;
}

void SettingsManager::setCategoryFilesEnabled(bool enabled)
{
    if (!enabled && !m_categoryWindowsEnabled && !m_categoryAppsEnabled) {
        return; // Mindestens eine Kategorie muss aktiviert bleiben
    }
    if (m_categoryFilesEnabled != enabled) {
        m_categoryFilesEnabled = enabled;
        save();
        emit settingsChanged();
    }
}

bool SettingsManager::showDesktop() const
{
    return m_showDesktop;
}

void SettingsManager::setShowDesktop(bool enabled)
{
    if (m_showDesktop != enabled) {
        m_showDesktop = enabled;
        save();
        emit settingsChanged();
    }
}

bool SettingsManager::showTray() const
{
    return m_showTray;
}

void SettingsManager::setShowTray(bool enabled)
{
    if (m_showTray != enabled) {
        m_showTray = enabled;
        save();
        emit settingsChanged();
    }
}

bool SettingsManager::showSession() const
{
    return m_showSession;
}

void SettingsManager::setShowSession(bool enabled)
{
    if (m_showSession != enabled) {
        m_showSession = enabled;
        save();
        emit settingsChanged();
    }
}

bool SettingsManager::showMenu() const
{
    return m_showMenu;
}

void SettingsManager::setShowMenu(bool enabled)
{
    if (m_showMenu != enabled || (enabled && m_showFavorites)) {
        m_showMenu = enabled;
        if (enabled) {
            m_showFavorites = false;
        }
        save();
        emit settingsChanged();
    }
}

bool SettingsManager::showFavorites() const
{
    return m_showFavorites;
}

void SettingsManager::setShowFavorites(bool enabled)
{
    if (m_showFavorites != enabled || (enabled && m_showMenu)) {
        m_showFavorites = enabled;
        if (enabled) {
            m_showMenu = false;
        }
        save();
        emit settingsChanged();
    }
}

bool SettingsManager::showMediaControls() const
{
    return m_showMediaControls;
}

void SettingsManager::setShowMediaControls(bool enabled)
{
    if (m_showMediaControls != enabled) {
        m_showMediaControls = enabled;
        save();
        emit settingsChanged();
    }
}

bool SettingsManager::showClock() const
{
    return m_showClock;
}

void SettingsManager::setShowClock(bool enabled)
{
    if (m_showClock != enabled) {
        m_showClock = enabled;
        save();
        emit settingsChanged();
    }
}

bool SettingsManager::showDate() const
{
    return m_showDate;
}

void SettingsManager::setShowDate(bool enabled)
{
    if (m_showDate != enabled) {
        m_showDate = enabled;
        save();
        emit settingsChanged();
    }
}

bool SettingsManager::showVolumeControl() const
{
    return m_showVolumeControl;
}

void SettingsManager::setShowVolumeControl(bool enabled)
{
    if (m_showVolumeControl != enabled) {
        m_showVolumeControl = enabled;
        save();
        emit settingsChanged();
    }
}

int SettingsManager::dateFormatIndex() const
{
    return m_dateFormatIndex;
}

void SettingsManager::setDateFormatIndex(int index)
{
    int clamped = std::clamp(index, 0, 13);
    if (m_dateFormatIndex != clamped) {
        m_dateFormatIndex = clamped;
        save();
        emit settingsChanged();
    }
}

QString SettingsManager::dateSeparator() const
{
    return m_dateSeparator;
}

void SettingsManager::setDateSeparator(const QString &sep)
{
    QString validSep = sep;
    if (validSep != QLatin1String(".") && validSep != QLatin1String("/") && validSep != QLatin1String("-")) {
        validSep = QStringLiteral(".");
    }
    if (m_dateSeparator != validSep) {
        m_dateSeparator = validSep;
        save();
        emit settingsChanged();
    }
}

int SettingsManager::timeFormatIndex() const
{
    return m_timeFormatIndex;
}

void SettingsManager::setTimeFormatIndex(int index)
{
    int clamped = std::clamp(index, 0, 1);
    if (m_timeFormatIndex != clamped) {
        m_timeFormatIndex = clamped;
        save();
        emit settingsChanged();
    }
}

QString SettingsManager::gestureLeft() const { return m_gestureLeft; }
void SettingsManager::setGestureLeft(const QString &action)
{
    if (m_gestureLeft != action) {
        m_gestureLeft = action;
        save();
        emit settingsChanged();
    }
}

QString SettingsManager::gestureRight() const { return m_gestureRight; }
void SettingsManager::setGestureRight(const QString &action)
{
    if (m_gestureRight != action) {
        m_gestureRight = action;
        save();
        emit settingsChanged();
    }
}

QString SettingsManager::gestureUp() const { return m_gestureUp; }
void SettingsManager::setGestureUp(const QString &action)
{
    if (m_gestureUp != action) {
        m_gestureUp = action;
        save();
        emit settingsChanged();
    }
}

QString SettingsManager::gestureDown() const { return m_gestureDown; }
void SettingsManager::setGestureDown(const QString &action)
{
    if (m_gestureDown != action) {
        m_gestureDown = action;
        save();
        emit settingsChanged();
    }
}

QString SettingsManager::gestureLeftUp() const { return m_gestureLeftUp; }
void SettingsManager::setGestureLeftUp(const QString &action)
{
    if (m_gestureLeftUp != action) {
        m_gestureLeftUp = action;
        save();
        emit settingsChanged();
    }
}

QString SettingsManager::gestureRightUp() const { return m_gestureRightUp; }
void SettingsManager::setGestureRightUp(const QString &action)
{
    if (m_gestureRightUp != action) {
        m_gestureRightUp = action;
        save();
        emit settingsChanged();
    }
}

QString SettingsManager::gestureLeftDown() const { return m_gestureLeftDown; }
void SettingsManager::setGestureLeftDown(const QString &action)
{
    if (m_gestureLeftDown != action) {
        m_gestureLeftDown = action;
        save();
        emit settingsChanged();
    }
}

QString SettingsManager::gestureRightDown() const { return m_gestureRightDown; }
void SettingsManager::setGestureRightDown(const QString &action)
{
    if (m_gestureRightDown != action) {
        m_gestureRightDown = action;
        save();
        emit settingsChanged();
    }
}

QString SettingsManager::gestureAction(int direction) const
{
    switch (direction) {
    case 0: return m_gestureRight;
    case 1: return m_gestureRightDown;
    case 2: return m_gestureDown;
    case 3: return m_gestureLeftDown;
    case 4: return m_gestureLeft;
    case 5: return m_gestureLeftUp;
    case 6: return m_gestureUp;
    case 7: return m_gestureRightUp;
    default: return QStringLiteral("none");
    }
}

void SettingsManager::setGestureAction(int direction, const QString &action)
{
    switch (direction) {
    case 0: setGestureRight(action); break;
    case 1: setGestureRightDown(action); break;
    case 2: setGestureDown(action); break;
    case 3: setGestureLeftDown(action); break;
    case 4: setGestureLeft(action); break;
    case 5: setGestureLeftUp(action); break;
    case 6: setGestureUp(action); break;
    case 7: setGestureRightUp(action); break;
    default: break;
    }
}

bool SettingsManager::gesturesEnabled() const { return m_gesturesEnabled; }
void SettingsManager::setGesturesEnabled(bool enabled)
{
    if (m_gesturesEnabled != enabled) {
        m_gesturesEnabled = enabled;
        save();
        emit settingsChanged();
    }
}

QString SettingsManager::gestureCustomApp(int direction) const
{
    if (direction >= 0 && direction < 8) return m_gestureCustomApp[direction];
    return QString();
}

void SettingsManager::setGestureCustomApp(int direction, const QString &cmd)
{
    if (direction >= 0 && direction < 8 && m_gestureCustomApp[direction] != cmd) {
        m_gestureCustomApp[direction] = cmd;
        save();
        emit settingsChanged();
    }
}

QString SettingsManager::gestureCustomAppName(int direction) const
{
    if (direction >= 0 && direction < 8) return m_gestureCustomAppName[direction];
    return QString();
}

void SettingsManager::setGestureCustomAppName(int direction, const QString &name)
{
    if (direction >= 0 && direction < 8 && m_gestureCustomAppName[direction] != name) {
        m_gestureCustomAppName[direction] = name;
        save();
        emit settingsChanged();
    }
}

QString SettingsManager::gestureCustomKeyName(int direction) const
{
    if (direction >= 0 && direction < 8) return m_gestureCustomKeyName[direction];
    return QString();
}

void SettingsManager::setGestureCustomKeyName(int direction, const QString &name)
{
    if (direction >= 0 && direction < 8 && m_gestureCustomKeyName[direction] != name) {
        m_gestureCustomKeyName[direction] = name;
        save();
        emit settingsChanged();
    }
}

QString SettingsManager::gestureCustomKeySequence(int direction) const
{
    if (direction >= 0 && direction < 8) return m_gestureCustomKeySequence[direction];
    return QString();
}

void SettingsManager::setGestureCustomKeySequence(int direction, const QString &seq)
{
    if (direction >= 0 && direction < 8 && m_gestureCustomKeySequence[direction] != seq) {
        m_gestureCustomKeySequence[direction] = seq;
        save();
        emit settingsChanged();
    }
}

void SettingsManager::resetDefaults()
{
    m_triggerHoldMs = 120;
    m_gameModeEnabled = true;
    m_overlayMouseSpeed = -0.5;
    m_overlayScale = 1.0;
    m_overlayTransparency = 4.0;
    m_maxRecentApps = 12;
    m_maxRecentFiles = 12;
    m_volumeStep = 3;
    m_categoryWindowsEnabled = true;
    m_categoryAppsEnabled = true;
    m_categoryFilesEnabled = true;
    m_showDesktop = true;
    m_showTray = true;
    m_showSession = true;
    m_showMenu = true;
    m_showFavorites = false;
    m_showMediaControls = true;
    m_showClock = true;
    m_showDate = true;
    m_showVolumeControl = true;
    m_dateFormatIndex = 0;
    m_dateSeparator = QStringLiteral(".");
    m_timeFormatIndex = 0;

    m_gesturesEnabled = true;
    m_gestureLeft = QStringLiteral("select_all");
    m_gestureRight = QStringLiteral("paste");
    m_gestureUp = QStringLiteral("reload");
    m_gestureDown = QStringLiteral("enter");
    m_gestureLeftUp = QStringLiteral("copy");
    m_gestureRightUp = QStringLiteral("redo");
    m_gestureLeftDown = QStringLiteral("cut");
    m_gestureRightDown = QStringLiteral("undo");

    for (int i = 0; i < 8; ++i) {
        m_gestureCustomApp[i].clear();
        m_gestureCustomAppName[i].clear();
        m_gestureCustomKeyName[i].clear();
        m_gestureCustomKeySequence[i].clear();
    }

    save();
    emit settingsChanged();
}

