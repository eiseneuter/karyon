#include "SettingsController.h"
#include "SettingsManager.h"
#include "GestureManager.h"
#include "PermissionManager.h"

#include <QQmlContext>
#include <QQuickItem>
#include <QGuiApplication>
#include <QScreen>
#include <QColor>
#include <QUrl>
#include <QEvent>
#include <QKeyEvent>
#include <QRegularExpression>

#include <KWindowSystem>
#include <KX11Extras>
#include <KOpenWithDialog>
#include <KService>
#include <linux/input-event-codes.h>

SettingsController::SettingsController(QObject *parent)
    : QObject(parent)
{
    m_view = new QQuickView();
    m_view->setColor(QColor(Qt::transparent));
    m_view->setFlags(Qt::FramelessWindowHint | Qt::Dialog);
    m_view->setResizeMode(QQuickView::SizeRootObjectToView);
    m_view->setTitle(QStringLiteral("Karyon Settings"));

    m_view->rootContext()->setContextProperty(QStringLiteral("settingsController"), this);
    m_view->setSource(QUrl(QStringLiteral("qrc:/qt/qml/karyon/qml/SettingsDialog.qml")));
    m_view->create();

    if (KWindowSystem::isPlatformX11()) {
        KX11Extras::setOnAllDesktops(m_view->winId(), true);
        KX11Extras::setType(m_view->winId(), NET::Dialog);
    }

    m_view->installEventFilter(this);

    connect(&SettingsManager::instance(), &SettingsManager::settingsChanged, this, [this]() {
        emit triggerHoldMsChanged();
        emit gameModeChanged();
        emit overlayMouseSpeedChanged();
        emit overlayScaleChanged();
        emit overlayTransparencyChanged();
        emit maxRecentAppsChanged();
        emit maxRecentFilesChanged();
        emit volumeStepChanged();
        emit categoryWindowsEnabledChanged();
        emit categoryAppsEnabledChanged();
        emit categoryFilesEnabledChanged();
        emit showDesktopChanged();
        emit showTrayChanged();
        emit showSessionChanged();
        emit showMenuChanged();
        emit showFavoritesChanged();
        emit showMediaControlsChanged();
        emit showClockChanged();
        emit showDateChanged();
        emit showVolumeControlChanged();
        emit gesturesEnabledChanged();
        emit gestureLeftChanged();
        emit gestureRightChanged();
        emit gestureUpChanged();
        emit gestureDownChanged();
        emit gestureLeftUpChanged();
        emit gestureRightUpChanged();
        emit gestureLeftDownChanged();
        emit gestureRightDownChanged();
    });
}

SettingsController::~SettingsController()
{
    if (m_view) {
        m_view->close();
        delete m_view;
        m_view = nullptr;
    }
}

int SettingsController::triggerHoldMs() const
{
    return SettingsManager::instance().triggerHoldMs();
}

void SettingsController::setTriggerHoldMs(int ms)
{
    SettingsManager::instance().setTriggerHoldMs(ms);
}

bool SettingsController::gameMode() const
{
    return SettingsManager::instance().gameModeEnabled();
}

void SettingsController::setGameMode(bool enabled)
{
    SettingsManager::instance().setGameModeEnabled(enabled);
}

double SettingsController::overlayMouseSpeed() const
{
    return SettingsManager::instance().overlayMouseSpeed();
}

void SettingsController::setOverlayMouseSpeed(double speed)
{
    SettingsManager::instance().setOverlayMouseSpeed(speed);
}

double SettingsController::overlayScale() const
{
    return SettingsManager::instance().overlayScale();
}

void SettingsController::setOverlayScale(double scale)
{
    SettingsManager::instance().setOverlayScale(scale);
}

double SettingsController::overlayTransparency() const
{
    return SettingsManager::instance().overlayTransparency();
}

void SettingsController::setOverlayTransparency(double pct)
{
    SettingsManager::instance().setOverlayTransparency(pct);
}

int SettingsController::maxRecentApps() const
{
    return SettingsManager::instance().maxRecentApps();
}

void SettingsController::setMaxRecentApps(int count)
{
    SettingsManager::instance().setMaxRecentApps(count);
}

int SettingsController::maxRecentFiles() const
{
    return SettingsManager::instance().maxRecentFiles();
}

void SettingsController::setMaxRecentFiles(int count)
{
    SettingsManager::instance().setMaxRecentFiles(count);
}

int SettingsController::volumeStep() const
{
    return SettingsManager::instance().volumeStep();
}

void SettingsController::setVolumeStep(int step)
{
    SettingsManager::instance().setVolumeStep(step);
}

bool SettingsController::categoryWindowsEnabled() const
{
    return SettingsManager::instance().categoryWindowsEnabled();
}

void SettingsController::setCategoryWindowsEnabled(bool enabled)
{
    SettingsManager::instance().setCategoryWindowsEnabled(enabled);
}

bool SettingsController::categoryAppsEnabled() const
{
    return SettingsManager::instance().categoryAppsEnabled();
}

void SettingsController::setCategoryAppsEnabled(bool enabled)
{
    SettingsManager::instance().setCategoryAppsEnabled(enabled);
}

bool SettingsController::categoryFilesEnabled() const
{
    return SettingsManager::instance().categoryFilesEnabled();
}

void SettingsController::setCategoryFilesEnabled(bool enabled)
{
    SettingsManager::instance().setCategoryFilesEnabled(enabled);
}

bool SettingsController::showDesktop() const
{
    return SettingsManager::instance().showDesktop();
}

void SettingsController::setShowDesktop(bool enabled)
{
    SettingsManager::instance().setShowDesktop(enabled);
}

bool SettingsController::showTray() const
{
    return SettingsManager::instance().showTray();
}

void SettingsController::setShowTray(bool enabled)
{
    SettingsManager::instance().setShowTray(enabled);
}

bool SettingsController::showSession() const
{
    return SettingsManager::instance().showSession();
}

void SettingsController::setShowSession(bool enabled)
{
    SettingsManager::instance().setShowSession(enabled);
}

bool SettingsController::showMenu() const
{
    return SettingsManager::instance().showMenu();
}

void SettingsController::setShowMenu(bool enabled)
{
    SettingsManager::instance().setShowMenu(enabled);
}

bool SettingsController::showFavorites() const
{
    return SettingsManager::instance().showFavorites();
}

void SettingsController::setShowFavorites(bool enabled)
{
    SettingsManager::instance().setShowFavorites(enabled);
}

bool SettingsController::showMediaControls() const
{
    return SettingsManager::instance().showMediaControls();
}

void SettingsController::setShowMediaControls(bool enabled)
{
    SettingsManager::instance().setShowMediaControls(enabled);
}

bool SettingsController::showClock() const
{
    return SettingsManager::instance().showClock();
}

void SettingsController::setShowClock(bool enabled)
{
    SettingsManager::instance().setShowClock(enabled);
}

bool SettingsController::showDate() const
{
    return SettingsManager::instance().showDate();
}

void SettingsController::setShowDate(bool enabled)
{
    SettingsManager::instance().setShowDate(enabled);
}

bool SettingsController::showVolumeControl() const
{
    return SettingsManager::instance().showVolumeControl();
}

void SettingsController::setShowVolumeControl(bool enabled)
{
    SettingsManager::instance().setShowVolumeControl(enabled);
}

QString SettingsController::gestureLeft() const { return SettingsManager::instance().gestureLeft(); }
void SettingsController::setGestureLeft(const QString &action) { SettingsManager::instance().setGestureLeft(action); }

QString SettingsController::gestureRight() const { return SettingsManager::instance().gestureRight(); }
void SettingsController::setGestureRight(const QString &action) { SettingsManager::instance().setGestureRight(action); }

QString SettingsController::gestureUp() const { return SettingsManager::instance().gestureUp(); }
void SettingsController::setGestureUp(const QString &action) { SettingsManager::instance().setGestureUp(action); }

QString SettingsController::gestureDown() const { return SettingsManager::instance().gestureDown(); }
void SettingsController::setGestureDown(const QString &action) { SettingsManager::instance().setGestureDown(action); }

QString SettingsController::gestureLeftUp() const { return SettingsManager::instance().gestureLeftUp(); }
void SettingsController::setGestureLeftUp(const QString &action) { SettingsManager::instance().setGestureLeftUp(action); }

QString SettingsController::gestureRightUp() const { return SettingsManager::instance().gestureRightUp(); }
void SettingsController::setGestureRightUp(const QString &action) { SettingsManager::instance().setGestureRightUp(action); }

QString SettingsController::gestureLeftDown() const { return SettingsManager::instance().gestureLeftDown(); }
void SettingsController::setGestureLeftDown(const QString &action) { SettingsManager::instance().setGestureLeftDown(action); }

QString SettingsController::gestureRightDown() const { return SettingsManager::instance().gestureRightDown(); }
void SettingsController::setGestureRightDown(const QString &action) { SettingsManager::instance().setGestureRightDown(action); }

static int directionIndex(const QString &dir) {
    if (dir == QLatin1String("right") || dir == QLatin1String("Right")) return 0;
    if (dir == QLatin1String("right_down") || dir == QLatin1String("down_right") || dir == QLatin1String("Right-Down")) return 1;
    if (dir == QLatin1String("down") || dir == QLatin1String("Down")) return 2;
    if (dir == QLatin1String("left_down") || dir == QLatin1String("down_left") || dir == QLatin1String("Left-Down")) return 3;
    if (dir == QLatin1String("left") || dir == QLatin1String("Left")) return 4;
    if (dir == QLatin1String("left_up") || dir == QLatin1String("up_left") || dir == QLatin1String("Left-Up")) return 5;
    if (dir == QLatin1String("up") || dir == QLatin1String("Up")) return 6;
    if (dir == QLatin1String("right_up") || dir == QLatin1String("up_right") || dir == QLatin1String("Right-Up")) return 7;
    return -1;
}

void SettingsController::emitGesturesChanged(int dir)
{
    switch (dir) {
    case 0: emit gestureRightChanged(); break;
    case 1: emit gestureRightDownChanged(); break;
    case 2: emit gestureDownChanged(); break;
    case 3: emit gestureLeftDownChanged(); break;
    case 4: emit gestureLeftChanged(); break;
    case 5: emit gestureLeftUpChanged(); break;
    case 6: emit gestureUpChanged(); break;
    case 7: emit gestureRightUpChanged(); break;
    default: break;
    }
}

bool SettingsController::gesturesEnabled() const
{
    return SettingsManager::instance().gesturesEnabled();
}

void SettingsController::setGesturesEnabled(bool enabled)
{
    SettingsManager::instance().setGesturesEnabled(enabled);
    emit gesturesEnabledChanged();
}

void SettingsController::selectCustomApp(const QString &direction)
{
    int dir = directionIndex(direction);
    if (dir < 0) return;

    KOpenWithDialog dlg;
    dlg.setWindowTitle(QStringLiteral("Wählen Sie eine Anwendung für die Geste"));
    if (dlg.exec() == QDialog::Accepted) {
        KService::Ptr service = dlg.service();
        if (service) {
            QString name = service->name();
            QString exec = service->exec();
            exec = exec.remove(QRegularExpression(QStringLiteral("%[a-zA-Z]"))).trimmed();
            SettingsManager::instance().setGestureCustomApp(dir, exec);
            SettingsManager::instance().setGestureCustomAppName(dir, name);
        } else if (!dlg.text().trimmed().isEmpty()) {
            QString cmd = dlg.text().trimmed();
            SettingsManager::instance().setGestureCustomApp(dir, cmd);
            SettingsManager::instance().setGestureCustomAppName(dir, cmd);
        }
        SettingsManager::instance().setGestureAction(dir, QStringLiteral("custom_app"));
        emitGesturesChanged(dir);
    }
}

void SettingsController::setCustomKey(const QString &direction, const QString &name, const QString &sequence)
{
    int dir = directionIndex(direction);
    if (dir < 0) return;

    SettingsManager::instance().setGestureCustomKeyName(dir, name);
    SettingsManager::instance().setGestureCustomKeySequence(dir, sequence);
    SettingsManager::instance().setGestureAction(dir, QStringLiteral("custom_key"));
    emitGesturesChanged(dir);
}

QString SettingsController::gestureActionDisplay(const QString &direction, const QString &hint) const
{
    Q_UNUSED(hint)
    int dir = directionIndex(direction);
    if (dir < 0) return QString();

    QString action = SettingsManager::instance().gestureAction(dir);
    return actionDisplayForId(action, direction);
}

QString SettingsController::actionDisplayForId(const QString &id, const QString &direction) const
{
    int dir = directionIndex(direction);
    if (dir >= 0) {
        if (id == QLatin1String("custom_app")) {
            QString appName = SettingsManager::instance().gestureCustomAppName(dir);
            if (!appName.isEmpty()) {
                if (appName.length() > 14) {
                    appName = appName.left(12) + QStringLiteral("…");
                }
                return QStringLiteral("Custom App [%1]").arg(appName);
            }
            return QStringLiteral("Custom App...");
        } else if (id == QLatin1String("custom_key")) {
            QString keyName = SettingsManager::instance().gestureCustomKeyName(dir);
            if (!keyName.isEmpty()) {
                return QStringLiteral("Custom Key [%1]").arg(keyName);
            }
            return QStringLiteral("Custom Key...");
        }
    }
    return GestureManager::displayNameForId(id);
}

QString SettingsController::gestureCustomKeySequence(const QString &direction) const
{
    int dir = directionIndex(direction);
    if (dir < 0) return QString();
    return SettingsManager::instance().gestureCustomKeySequence(dir);
}

QStringList SettingsController::gestureActionIds() const
{
    return GestureManager::actionIdList();
}

QStringList SettingsController::gestureActionNames() const
{
    return GestureManager::actionDisplayList();
}

QString SettingsController::gestureActionName(const QString &id) const
{
    return GestureManager::displayNameForId(id);
}

QVariantMap SettingsController::processKeyEvent(int key, int modifiers, const QString &text) const
{
    QVariantMap res;
    res[QStringLiteral("valid")] = false;
    res[QStringLiteral("name")] = QString();
    res[QStringLiteral("sequence")] = QString();

    QStringList parts;
    std::vector<int> keys;

    bool ctrl = (modifiers & Qt::ControlModifier);
    bool alt = (modifiers & Qt::AltModifier);
    bool shift = (modifiers & Qt::ShiftModifier);
    bool meta = (modifiers & Qt::MetaModifier);

    if (ctrl) {
        parts << QStringLiteral("Strg");
        keys.push_back(KEY_LEFTCTRL);
    }
    if (alt) {
        parts << QStringLiteral("Alt");
        keys.push_back(KEY_LEFTALT);
    }
    if (shift) {
        parts << QStringLiteral("Shift");
        keys.push_back(KEY_LEFTSHIFT);
    }
    if (meta) {
        parts << QStringLiteral("Super");
        keys.push_back(KEY_LEFTMETA);
    }

    if (key == Qt::Key_Control || key == Qt::Key_Alt || key == Qt::Key_Shift || key == Qt::Key_Meta || key == Qt::Key_AltGr) {
        if (!parts.isEmpty()) {
            res[QStringLiteral("name")] = parts.join(QLatin1Char('+')) + QStringLiteral("+...");
        }
        return res;
    }

    int linuxKey = 0;
    QString keyName;

    if (key >= Qt::Key_A && key <= Qt::Key_Z) {
        int idx = key - Qt::Key_A;
        keyName = QString(QChar('A' + idx));
        if (key == Qt::Key_Y) {
            linuxKey = GestureManager::getYKey();
        } else if (key == Qt::Key_Z) {
            linuxKey = GestureManager::getZKey();
        } else {
            static const int letterKeys[] = {
                KEY_A, KEY_B, KEY_C, KEY_D, KEY_E, KEY_F, KEY_G, KEY_H, KEY_I, KEY_J,
                KEY_K, KEY_L, KEY_M, KEY_N, KEY_O, KEY_P, KEY_Q, KEY_R, KEY_S, KEY_T,
                KEY_U, KEY_V, KEY_W, KEY_X, KEY_Y, KEY_Z
            };
            linuxKey = letterKeys[idx];
        }
    } else if (key >= Qt::Key_0 && key <= Qt::Key_9) {
        keyName = QString(QChar('0' + (key - Qt::Key_0)));
        static const int numKeys[] = {
            KEY_0, KEY_1, KEY_2, KEY_3, KEY_4, KEY_5, KEY_6, KEY_7, KEY_8, KEY_9
        };
        linuxKey = numKeys[key - Qt::Key_0];
    } else if (key >= Qt::Key_F1 && key <= Qt::Key_F12) {
        keyName = QStringLiteral("F%1").arg(key - Qt::Key_F1 + 1);
        static const int fKeys[] = {
            KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F6,
            KEY_F7, KEY_F8, KEY_F9, KEY_F10, KEY_F11, KEY_F12
        };
        linuxKey = fKeys[key - Qt::Key_F1];
    } else {
        switch (key) {
        case Qt::Key_Return:
        case Qt::Key_Enter:     keyName = QStringLiteral("Enter"); linuxKey = KEY_ENTER; break;
        case Qt::Key_Escape:    keyName = QStringLiteral("Esc"); linuxKey = KEY_ESC; break;
        case Qt::Key_Tab:       keyName = QStringLiteral("Tab"); linuxKey = KEY_TAB; break;
        case Qt::Key_Backspace: keyName = QStringLiteral("Backspace"); linuxKey = KEY_BACKSPACE; break;
        case Qt::Key_Space:     keyName = QStringLiteral("Leertaste"); linuxKey = KEY_SPACE; break;
        case Qt::Key_Delete:    keyName = QStringLiteral("Entf"); linuxKey = KEY_DELETE; break;
        case Qt::Key_Insert:    keyName = QStringLiteral("Einfg"); linuxKey = KEY_INSERT; break;
        case Qt::Key_Home:      keyName = QStringLiteral("Pos1"); linuxKey = KEY_HOME; break;
        case Qt::Key_End:       keyName = QStringLiteral("Ende"); linuxKey = KEY_END; break;
        case Qt::Key_PageUp:    keyName = QStringLiteral("BildAuf"); linuxKey = KEY_PAGEUP; break;
        case Qt::Key_PageDown:  keyName = QStringLiteral("BildAb"); linuxKey = KEY_PAGEDOWN; break;
        case Qt::Key_Left:      keyName = QStringLiteral("Links"); linuxKey = KEY_LEFT; break;
        case Qt::Key_Right:     keyName = QStringLiteral("Rechts"); linuxKey = KEY_RIGHT; break;
        case Qt::Key_Up:        keyName = QStringLiteral("Hoch"); linuxKey = KEY_UP; break;
        case Qt::Key_Down:      keyName = QStringLiteral("Runter"); linuxKey = KEY_DOWN; break;
        case Qt::Key_Print:     keyName = QStringLiteral("Druck"); linuxKey = KEY_PRINT; break;
        case Qt::Key_Minus:     keyName = QStringLiteral("-"); linuxKey = KEY_MINUS; break;
        case Qt::Key_Plus:      keyName = QStringLiteral("+"); linuxKey = KEY_EQUAL; break;
        case Qt::Key_Equal:     keyName = QStringLiteral("="); linuxKey = KEY_EQUAL; break;
        case Qt::Key_Comma:     keyName = QStringLiteral(","); linuxKey = KEY_COMMA; break;
        case Qt::Key_Period:    keyName = QStringLiteral("."); linuxKey = KEY_DOT; break;
        case Qt::Key_Slash:     keyName = QStringLiteral("/"); linuxKey = KEY_SLASH; break;
        default:
            if (!text.isEmpty() && text.at(0).isPrint()) {
                keyName = text.toUpper();
            }
            break;
        }
    }

    if (linuxKey > 0) {
        keys.push_back(linuxKey);
    }

    if (!keyName.isEmpty()) {
        parts << keyName;
    }

    if (!parts.isEmpty() && linuxKey > 0) {
        QStringList keyStrings;
        for (int k : keys) {
            keyStrings << QString::number(k);
        }
        res[QStringLiteral("valid")] = true;
        res[QStringLiteral("name")] = parts.join(QLatin1Char('+'));
        res[QStringLiteral("sequence")] = keyStrings.join(QLatin1Char(','));
    }

    return res;
}

void SettingsController::resetDefaults()
{
    SettingsManager::instance().resetDefaults();
}

void SettingsController::hideSettings()
{
    if (m_view) {
        m_view->hide();
    }
}

void SettingsController::quitApp()
{
    hideSettings();
    qApp->quit();
}

void SettingsController::startWindowDrag()
{
    if (m_view) {
        m_view->startSystemMove();
    }
}

void SettingsController::showSettings(const QPoint &cursorPos)
{
    if (!m_view) return;

    const int dlgWidth = 580;
    const int dlgHeight = 600;
    m_view->resize(dlgWidth, dlgHeight);

    QScreen *screen = nullptr;
    if (!cursorPos.isNull()) {
        screen = QGuiApplication::screenAt(cursorPos);
    }
    if (!screen) {
        screen = QGuiApplication::primaryScreen();
    }

    int x = 0;
    int y = 0;
    if (screen) {
        QRect sg = screen->geometry();
        x = sg.x() + (sg.width() - dlgWidth) / 2;
        y = sg.y() + (sg.height() - dlgHeight) / 2;
    }

    m_view->setPosition(x, y);
    m_view->show();
    m_view->requestActivate();
    m_view->raise();

    QQuickItem *root = m_view->rootObject();
    if (root) {
        root->forceActiveFocus();
    }
}

void SettingsController::setIsRecordingKey(bool rec)
{
    if (m_isRecordingKey != rec) {
        m_isRecordingKey = rec;
        emit isRecordingKeyChanged();
    }
}

bool SettingsController::eventFilter(QObject *obj, QEvent *event)
{
    if (obj == m_view && event->type() == QEvent::KeyPress) {
        if (m_isRecordingKey) {
            // Wenn der Key-Recorder aktiv ist, kein Schließen über Escape
            return false;
        }
        auto *ke = static_cast<QKeyEvent *>(event);
        if (ke->key() == Qt::Key_Escape) {
            hideSettings();
            return true;
        }
    }
    return QObject::eventFilter(obj, event);
}

bool SettingsController::hasInputPermissions() const
{
    return PermissionManager::hasRequiredPermissions();
}

bool SettingsController::requestInputPermissions()
{
    QString err;
    bool ok = PermissionManager::grantPermissions(err);
    emit inputPermissionsChanged();
    return ok;
}

bool SettingsController::revokeInputPermissions()
{
    QString err;
    bool ok = PermissionManager::revokePermissions(err);
    emit inputPermissionsChanged();
    return ok;
}

