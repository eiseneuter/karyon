#include "GestureManager.h"
#include "InputMonitor.h"
#include "SettingsManager.h"
#include "KWinBridge.h"
#include "MediaManager.h"
#include "VolumeManager.h"

#include <cmath>
#include <linux/input-event-codes.h>
#include <QFile>
#include <QDir>
#include <QSettings>
#include <QProcess>
#include "ProcessUtils.h"
#include <QDBusMessage>
#include <QSet>
#include <QDateTime>
#include <QDBusConnection>

// Liegt auf der aktiven Tastaturbelegung Z dort, wo bei QWERTY das Y ist (QWERTZ)?
// Maßgeblich ist die GERADE AKTIVE Belegung laut KDE, nicht die Systemsprache.
bool GestureManager::isGermanLayout()
{
    static qint64 lastCheckMs = 0;
    static bool isQwertz = false;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (lastCheckMs != 0 && (now - lastCheckMs) < 5000) {
        return isQwertz;
    }
    lastCheckMs = now;

    static const QSet<QString> qwertzLayouts = {
        QStringLiteral("de"), QStringLiteral("at"), QStringLiteral("ch"), QStringLiteral("cz"),
        QStringLiteral("sk"), QStringLiteral("hu"), QStringLiteral("si"), QStringLiteral("hr"),
        QStringLiteral("ba"), QStringLiteral("al"), QStringLiteral("lu")
    };

    // 1. Liste der eingerichteten Belegungen (kann "us,de" sein -> als Liste lesen)
    QStringList layouts;
    QString kxkbrc = QDir::homePath() + QStringLiteral("/.config/kxkbrc");
    if (QFile::exists(kxkbrc)) {
        QSettings s(kxkbrc, QSettings::IniFormat);
        const QVariant v = s.value(QStringLiteral("Layout/LayoutList"));
        layouts = v.toStringList();
        if (layouts.size() == 1 && layouts.first().contains(QLatin1Char(','))) {
            layouts = layouts.first().split(QLatin1Char(','), Qt::SkipEmptyParts);
        }
    }

    // 2. Welche davon ist gerade aktiv?
    int activeIdx = 0;
    QDBusMessage msg = QDBusMessage::createMethodCall(
        QStringLiteral("org.kde.keyboard"), QStringLiteral("/Layouts"),
        QStringLiteral("org.kde.KeyboardLayouts"), QStringLiteral("getLayout"));
    const QDBusMessage reply = QDBusConnection::sessionBus().call(msg, QDBus::Block, 200);
    if (reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty()) {
        activeIdx = reply.arguments().first().toInt();
    }

    if (!layouts.isEmpty()) {
        const QString active = layouts.value(activeIdx, layouts.first()).trimmed().toLower();
        isQwertz = qwertzLayouts.contains(active);
        return isQwertz;
    }

    // 3. Keine eigene Belegung eingerichtet (Systemstandard): Sprache des Systems als Näherung.
    //    Karyon setzt LANG für sich selbst auf Englisch – der ursprüngliche Wert steht in KARYON_HOST_LANG.
    QString lang = QString::fromLocal8Bit(qgetenv("KARYON_HOST_LANG"));
    if (lang.isEmpty() || lang.startsWith(QLatin1Char('\x01'))) lang = QString::fromLocal8Bit(qgetenv("LANG"));
    const QString country = lang.section(QLatin1Char('_'), 0, 0).toLower();
    isQwertz = (country == QLatin1String("de") || country == QLatin1String("cs") ||
                country == QLatin1String("sk") || country == QLatin1String("hu") ||
                country == QLatin1String("sl") || country == QLatin1String("hr"));
    return isQwertz;
}

int GestureManager::getZKey()
{
    // Auf einer deutschen Tastatur erzeugt die physikalische Taste KEY_Y den Buchstaben Z!
    return isGermanLayout() ? KEY_Y : KEY_Z;
}

int GestureManager::getYKey()
{
    return isGermanLayout() ? KEY_Z : KEY_Y;
}

namespace {
static const std::vector<int> s_emptyKeys;
} // namespace

const QList<GestureManager::ActionDef> &GestureManager::availableActions()
{
    int zKey = getZKey();

    static const QList<GestureManager::ActionDef> s_actions = {
        { QStringLiteral("none"),            QStringLiteral("None"),                      {} },
        { QStringLiteral("forward"),         QStringLiteral("Forward"),                   {KEY_LEFTALT, KEY_RIGHT} },
        { QStringLiteral("back"),            QStringLiteral("Back"),                      {KEY_LEFTALT, KEY_LEFT} },
        { QStringLiteral("reload"),          QStringLiteral("Reload"),                    {KEY_F5} },
        { QStringLiteral("minimize"),        QStringLiteral("Minimize"),                  {} },
        { QStringLiteral("maximize"),        QStringLiteral("Maximize/Restore"),          {} },
        { QStringLiteral("close_window"),    QStringLiteral("Close Window"),              {KEY_LEFTALT, KEY_F4} },
        { QStringLiteral("show_desktop"),    QStringLiteral("Show Desktop"),              {} },
        { QStringLiteral("next_desktop"),    QStringLiteral("Switch to Next Desktop"),     {} },
        { QStringLiteral("prev_desktop"),    QStringLiteral("Switch to Previous Desktop"), {} },
        { QStringLiteral("custom_app"),      QStringLiteral("Custom App..."),             {} },
        { QStringLiteral("krunner"),         QStringLiteral("KRunner"),                   {} },
        { QStringLiteral("print_screen"),    QStringLiteral("Print Screen"),              {KEY_PRINT} },
        { QStringLiteral("copy"),            QStringLiteral("Copy"),                      {KEY_LEFTCTRL, KEY_C} },
        { QStringLiteral("cut"),             QStringLiteral("Cut"),                       {KEY_LEFTCTRL, KEY_X} },
        { QStringLiteral("paste"),           QStringLiteral("Paste"),                     {KEY_LEFTCTRL, KEY_V} },
        { QStringLiteral("select_all"),      QStringLiteral("Select All"),                {KEY_LEFTCTRL, KEY_A} },
        { QStringLiteral("undo"),            QStringLiteral("Undo"),                      {KEY_LEFTCTRL, zKey} },
        { QStringLiteral("redo"),            QStringLiteral("Redo"),                      {KEY_LEFTCTRL, KEY_LEFTSHIFT, zKey} },
        { QStringLiteral("enter"),           QStringLiteral("Enter"),                     {KEY_ENTER} },
        { QStringLiteral("show_clipboard"),  QStringLiteral("Show Clipboard"),            {} },
        { QStringLiteral("play_pause"),      QStringLiteral("Play/Pause"),                {} },
        { QStringLiteral("next_track"),      QStringLiteral("Next Track"),                {} },
        { QStringLiteral("prev_track"),      QStringLiteral("Previous Track"),            {} },
        { QStringLiteral("mute"),            QStringLiteral("Mute"),                      {} },
        { QStringLiteral("custom_key"),      QStringLiteral("Custom Key..."),             {} }
    };
    return s_actions;
}

QStringList GestureManager::actionIdList()
{
    QStringList list;
    for (const auto &a : availableActions())
        list << a.id;
    return list;
}

QStringList GestureManager::actionDisplayList()
{
    QStringList list;
    for (const auto &a : availableActions())
        list << a.displayName;
    return list;
}

QString GestureManager::displayNameForId(const QString &id)
{
    for (const auto &a : availableActions()) {
        if (a.id == id) return a.displayName;
    }
    return QStringLiteral("None");
}

const std::vector<int> &GestureManager::keysForId(const QString &id)
{
    for (const auto &a : availableActions()) {
        if (a.id == id) return a.keys;
    }
    return s_emptyKeys;
}

int GestureManager::determineDirection(int deltaX, int deltaY)
{
    double angle = std::atan2(static_cast<double>(deltaY), static_cast<double>(deltaX));
    if (angle < 0.0) angle += 2.0 * M_PI;
    int sector = static_cast<int>((angle + M_PI / 8.0) / (M_PI / 4.0)) % 8;
    return sector;
}

double GestureManager::directionAngle(int direction)
{
    return direction * (M_PI / 4.0);
}

QString GestureManager::directionLabel(int direction)
{
    switch (direction) {
    case Right:     return QStringLiteral("Right");
    case DownRight: return QStringLiteral("Right-Down");
    case Down:      return QStringLiteral("Down");
    case DownLeft:  return QStringLiteral("Left-Down");
    case Left:      return QStringLiteral("Left");
    case UpLeft:    return QStringLiteral("Left-Up");
    case Up:        return QStringLiteral("Up");
    case UpRight:   return QStringLiteral("Right-Up");
    default:        return QStringLiteral("Unknown");
    }
}

GestureManager::GestureManager(QObject *parent)
    : QObject(parent)
{
}

void GestureManager::executeGesture(int direction, InputMonitor *monitor, KWinBridge *kwinBridge,
                                     MediaManager *mediaManager, VolumeManager *volumeManager)
{
    if (direction < 0 || direction > 7 || !monitor) return;

    QString actionId = SettingsManager::instance().gestureAction(direction);
    if (actionId.isEmpty() || actionId == QLatin1String("none")) return;

    // 1. KWin-Funktionen
    if (actionId == QLatin1String("minimize")) {
        if (kwinBridge) {
            QString winId = kwinBridge->activeWindowId();
            if (!winId.isEmpty()) kwinBridge->toggleMinimize(winId);
        }
        return;
    }
    if (actionId == QLatin1String("maximize")) {
        if (kwinBridge) {
            QString winId = kwinBridge->activeWindowId();
            if (!winId.isEmpty()) kwinBridge->toggleMaximize(winId);
        }
        return;
    }
    if (actionId == QLatin1String("close_window")) {
        if (kwinBridge) {
            QString winId = kwinBridge->activeWindowId();
            if (!winId.isEmpty()) {
                kwinBridge->closeWindow(winId);
                return;
            }
        }
        monitor->sendKeyCombo({KEY_LEFTALT, KEY_F4});
        return;
    }
    if (actionId == QLatin1String("show_desktop")) {
        if (kwinBridge) kwinBridge->toggleShowDesktop();
        return;
    }
    if (actionId == QLatin1String("next_desktop")) {
        QDBusMessage msg = QDBusMessage::createMethodCall(
            QStringLiteral("org.kde.KWin"), QStringLiteral("/KWin"),
            QStringLiteral("org.kde.KWin"), QStringLiteral("nextDesktop"));
        QDBusConnection::sessionBus().send(msg);
        return;
    }
    if (actionId == QLatin1String("prev_desktop")) {
        QDBusMessage msg = QDBusMessage::createMethodCall(
            QStringLiteral("org.kde.KWin"), QStringLiteral("/KWin"),
            QStringLiteral("org.kde.KWin"), QStringLiteral("previousDesktop"));
        QDBusConnection::sessionBus().send(msg);
        return;
    }
    if (actionId == QLatin1String("krunner")) {
        QDBusMessage msg = QDBusMessage::createMethodCall(
            QStringLiteral("org.kde.krunner"), QStringLiteral("/App"),
            QStringLiteral("org.kde.krunner.App"), QStringLiteral("display"));
        QDBusConnection::sessionBus().send(msg);
        return;
    }
    if (actionId == QLatin1String("show_clipboard")) {
        // Direkter Klipper Popup-Aufruf auf plasmashell
        QDBusMessage msg = QDBusMessage::createMethodCall(
            QStringLiteral("org.kde.plasmashell"),
            QStringLiteral("/klipper"),
            QStringLiteral("org.kde.klipper.klipper"),
            QStringLiteral("showKlipperPopupMenu"));
        if (!QDBusConnection::sessionBus().send(msg)) {
            QDBusConnection bus = QDBusConnection::sessionBus();
            QDBusInterface iface(QStringLiteral("org.kde.kglobalaccel"),
                                 QStringLiteral("/component/plasmashell"),
                                 QStringLiteral("org.kde.kglobalaccel.Component"),
                                 bus);
            iface.call(QDBus::NoBlock, QStringLiteral("invokeShortcut"), QStringLiteral("show-on-mouse-pos"));
        }
        return;
    }
    if (actionId == QLatin1String("custom_app")) {
        QString cmd = SettingsManager::instance().gestureCustomApp(direction);
        if (!cmd.isEmpty()) {
            ProcessUtils::startDetachedHost(QStringLiteral("/bin/sh"), {QStringLiteral("-c"), cmd});
        }
        return;
    }
    if (actionId == QLatin1String("custom_key")) {
        QString seq = SettingsManager::instance().gestureCustomKeySequence(direction);
        if (!seq.isEmpty()) {
            std::vector<int> keys;
            const auto parts = seq.split(QLatin1Char(','), Qt::SkipEmptyParts);
            for (const auto &p : parts) {
                bool ok = false;
                int k = p.trimmed().toInt(&ok);
                if (ok && k > 0) keys.push_back(k);
            }
            if (!keys.empty()) {
                monitor->sendKeyCombo(keys);
            }
        }
        return;
    }
    if (actionId == QLatin1String("print_screen")) {
        ProcessUtils::startDetachedHost(QStringLiteral("spectacle"), {});
        return;
    }

    // 2. Medien- & Audiosteuerung via KDE Globalaccel (Systemweit, nie Play/Pause-Verwechslung)
    if (actionId == QLatin1String("play_pause")) {
        QDBusConnection bus = QDBusConnection::sessionBus();
        QDBusInterface iface(QStringLiteral("org.kde.kglobalaccel"),
                             QStringLiteral("/component/mediacontrol"),
                             QStringLiteral("org.kde.kglobalaccel.Component"),
                             bus);
        iface.call(QDBus::NoBlock, QStringLiteral("invokeShortcut"), QStringLiteral("playpausemedia"));
        return;
    }
    if (actionId == QLatin1String("next_track")) {
        QDBusConnection bus = QDBusConnection::sessionBus();
        QDBusInterface iface(QStringLiteral("org.kde.kglobalaccel"),
                             QStringLiteral("/component/mediacontrol"),
                             QStringLiteral("org.kde.kglobalaccel.Component"),
                             bus);
        iface.call(QDBus::NoBlock, QStringLiteral("invokeShortcut"), QStringLiteral("nextmedia"));
        return;
    }
    if (actionId == QLatin1String("prev_track")) {
        QDBusConnection bus = QDBusConnection::sessionBus();
        QDBusInterface iface(QStringLiteral("org.kde.kglobalaccel"),
                             QStringLiteral("/component/mediacontrol"),
                             QStringLiteral("org.kde.kglobalaccel.Component"),
                             bus);
        iface.call(QDBus::NoBlock, QStringLiteral("invokeShortcut"), QStringLiteral("previousmedia"));
        return;
    }
    if (actionId == QLatin1String("mute")) {
        if (volumeManager) volumeManager->toggleMute();
        return;
    }

    // 3. Tastenkombinationen (Undo, Redo, Copy, Paste, etc.)
    const auto &keys = keysForId(actionId);
    if (!keys.empty()) {
        monitor->sendKeyCombo(keys);
    }
}
