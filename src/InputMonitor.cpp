#include "InputMonitor.h"
#include "GestureManager.h"

#include <linux/input.h>
#include <linux/uinput.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/inotify.h>
#include <poll.h>
#include <cmath>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <cstring>
#include <algorithm>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusVariant>
#include <QDBusArgument>
#include <QMetaObject>
#include <QStringList>
#include <QVariantMap>
#include <QSettings>
#include <QFile>
#include <QDir>

namespace fs = std::filesystem;

namespace {

bool isExcludedNonMouse(const QString &devName)
{
    static const QStringList nonMouseKeywords = {
        QStringLiteral("keyboard"),
        QStringLiteral("touchpad"), QStringLiteral("trackpad"), QStringLiteral("glidepoint"),
        QStringLiteral("trackpoint"), QStringLiteral("trackstick"), QStringLiteral("pointstick"),
        QStringLiteral("dualpoint"), QStringLiteral("tpps/2"), QStringLiteral("stick"),
        QStringLiteral("synaptics"), QStringLiteral("alps"), QStringLiteral("focaltech"),
        QStringLiteral("goodix"), QStringLiteral("elan"),
        QStringLiteral("consumer control"), QStringLiteral("system control"),
        QStringLiteral("digitizer"), QStringLiteral("stylus"), QStringLiteral("touchscreen"),
        QStringLiteral("wacom"), QStringLiteral("tablet"), QStringLiteral("pen")
    };

    QString lower = devName.toLower();
    for (const QString &kw : nonMouseKeywords) {
        if (lower.contains(kw)) {
            return true;
        }
    }
    return false;
}

bool isMouseDevice(int fd)
{
    char name[256] = {0};
    if (ioctl(fd, EVIOCGNAME(sizeof(name)), name) >= 0) {
        std::string sName(name);
        if (sName.find("karyon-virtual") != std::string::npos) {
            return false;
        }
        std::string lowerName = sName;
        for (char &c : lowerName) c = std::tolower(c);
        if (lowerName.find("keyboard") != std::string::npos ||
            lowerName.find("touchpad") != std::string::npos ||
            lowerName.find("trackpad") != std::string::npos ||
            lowerName.find("consumer control") != std::string::npos ||
            lowerName.find("system control") != std::string::npos) {
            return false;
        }
    }

    unsigned long relBits[(REL_CNT + sizeof(long) * 8 - 1) / (sizeof(long) * 8)] = {0};
    unsigned long keyBits[(KEY_CNT + sizeof(long) * 8 - 1) / (sizeof(long) * 8)] = {0};

    if (ioctl(fd, EVIOCGBIT(EV_REL, sizeof(relBits)), relBits) < 0) return false;
    if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keyBits)), keyBits) < 0) return false;

    auto testBit = [](const unsigned long *arr, int bit) {
        return (arr[bit / (sizeof(long) * 8)] & (1UL << (bit % (sizeof(long) * 8)))) != 0;
    };

    if (!testBit(relBits, REL_X) || !testBit(relBits, REL_Y)) return false;
    if (!testBit(keyBits, BTN_LEFT) && !testBit(keyBits, BTN_RIGHT)) return false;

    // Geräte mit Schreibmaschinentasten (A, Leertaste, Enter) niemals als Maus einstufen!
    // Verbundgeräte (z. B. Dell KB216 Multimedia Keyboard) melden oft Pseudo-Mausachsen,
    // dürfen aber keinesfalls per EVIOCGRAB exklusiv als Maus übernommen werden.
    if (testBit(keyBits, KEY_A) && testBit(keyBits, KEY_SPACE) && testBit(keyBits, KEY_ENTER)) {
        return false;
    }

    return true;
}

bool isKeyboardDevice(int fd)
{
    char name[256] = {0};
    if (ioctl(fd, EVIOCGNAME(sizeof(name)), name) >= 0) {
        std::string sName(name);
        if (sName.find("karyon-virtual") != std::string::npos) {
            return false;
        }
        std::string lowerName = sName;
        for (char &c : lowerName) c = std::tolower(c);
        if (lowerName.find("power button") != std::string::npos ||
            lowerName.find("sleep button") != std::string::npos ||
            lowerName.find("video bus") != std::string::npos) {
            return false;
        }
    }

    unsigned long evBits[(EV_CNT + sizeof(long) * 8 - 1) / (sizeof(long) * 8)] = {0};
    if (ioctl(fd, EVIOCGBIT(0, sizeof(evBits)), evBits) < 0) return false;
    auto testBit = [](const unsigned long *arr, int bit) {
        return (arr[bit / (sizeof(long) * 8)] & (1UL << (bit % (sizeof(long) * 8)))) != 0;
    };
    if (!testBit(evBits, EV_KEY)) return false;

    unsigned long keyBits[(KEY_CNT + sizeof(long) * 8 - 1) / (sizeof(long) * 8)] = {0};
    if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keyBits)), keyBits) < 0) return false;

    // Keyboards must have standard typing keys
    if (!testBit(keyBits, KEY_A) || !testBit(keyBits, KEY_SPACE) || !testBit(keyBits, KEY_ENTER)) {
        return false;
    }

    return true;
}

QString mouseButtonNameFromEvdev(int code)
{
    switch (code) {
    case BTN_LEFT:    return QStringLiteral("Left Click");
    case BTN_RIGHT:   return QStringLiteral("Right Click");
    case BTN_MIDDLE:  return QStringLiteral("Middle Click");
    case BTN_SIDE:    return QStringLiteral("Mouse Back (Button 4)");
    case BTN_EXTRA:   return QStringLiteral("Mouse Forward (Button 5)");
    case BTN_FORWARD: return QStringLiteral("Mouse Button 6");
    case BTN_BACK:    return QStringLiteral("Mouse Button 7");
    case BTN_TASK:    return QStringLiteral("Mouse Task (Button 8)");
    default:
        if (code >= BTN_MOUSE && code < BTN_JOYSTICK) {
            return QStringLiteral("Mouse Button %1").arg(code - BTN_MOUSE + 1);
        }
        return QStringLiteral("Button %1").arg(code);
    }
}

QString keyboardKeyNameFromEvdev(int code)
{
    if (code >= KEY_A && code <= KEY_Z) {
        static const char *letters[] = {
            "A", "B", "C", "D", "E", "F", "G", "H", "I", "J",
            "K", "L", "M", "N", "O", "P", "Q", "R", "S", "T",
            "U", "V", "W", "X", "Y", "Z"
        };
        return QString::fromLatin1(letters[code - KEY_A]);
    }
    if (code >= KEY_1 && code <= KEY_9) {
        return QString::number(code - KEY_1 + 1);
    }
    if (code == KEY_0) return QStringLiteral("0");
    if (code >= KEY_F1 && code <= KEY_F12) {
        return QStringLiteral("F%1").arg(code - KEY_F1 + 1);
    }
    switch (code) {
    case KEY_SPACE: return QStringLiteral("Space");
    case KEY_ENTER: return QStringLiteral("Enter");
    case KEY_ESC: return QStringLiteral("Esc");
    case KEY_TAB: return QStringLiteral("Tab");
    case KEY_BACKSPACE: return QStringLiteral("Backspace");
    case KEY_DELETE: return QStringLiteral("Entf");
    case KEY_INSERT: return QStringLiteral("Einfg");
    case KEY_HOME: return QStringLiteral("Pos1");
    case KEY_END: return QStringLiteral("Ende");
    case KEY_PAGEUP: return QStringLiteral("BildAuf");
    case KEY_PAGEDOWN: return QStringLiteral("BildAb");
    case KEY_LEFT: return QStringLiteral("Links");
    case KEY_RIGHT: return QStringLiteral("Rechts");
    case KEY_UP: return QStringLiteral("Hoch");
    case KEY_DOWN: return QStringLiteral("Runter");
    case KEY_CAPSLOCK: return QStringLiteral("Caps Lock");
    case KEY_PRINT: return QStringLiteral("Druck");
    default: return QString();
    }
}

bool hasPressedKeysOrButtons(int fd)
{
    unsigned long keyBits[(KEY_CNT + sizeof(long) * 8 - 1) / (sizeof(long) * 8)] = {0};
    if (ioctl(fd, EVIOCGKEY(sizeof(keyBits)), keyBits) < 0) {
        return false;
    }
    auto testBit = [](const unsigned long *arr, int bit) {
        return (arr[bit / (sizeof(long) * 8)] & (1UL << (bit % (sizeof(long) * 8)))) != 0;
    };
    // Physische Maustasten prüfen (BTN_LEFT bis BTN_TASK / vor BTN_JOYSTICK)
    for (int b = BTN_MISC; b < BTN_JOYSTICK; ++b) {
        if (testBit(keyBits, b)) {
            return true;
        }
    }
    // Gängige Tasten/Modifier prüfen, falls Hybrid-/Kombigerät
    static const int modKeys[] = {
        KEY_LEFTCTRL, KEY_RIGHTCTRL, KEY_LEFTSHIFT, KEY_RIGHTSHIFT,
        KEY_LEFTALT, KEY_RIGHTALT, KEY_LEFTMETA, KEY_RIGHTMETA,
        KEY_SPACE, KEY_ENTER
    };
    for (int k : modKeys) {
        if (testBit(keyBits, k)) {
            return true;
        }
    }
    return false;
}

void emitEvent(int fd, uint16_t type, uint16_t code, int32_t value)
{
    if (fd < 0) return;
    struct input_event ev{};
    ev.type = type;
    ev.code = code;
    ev.value = value;
    [[maybe_unused]] auto res = write(fd, &ev, sizeof(ev));
}

void emitSyn(int fd)
{
    emitEvent(fd, EV_SYN, SYN_REPORT, 0);
}

} // namespace

InputMonitor::InputMonitor(QObject *parent)
    : QObject(parent)
{
}

InputMonitor::~InputMonitor()
{
    stop();
}

void InputMonitor::setOverlayMouseSpeed(double speed)
{
    double oldSpeed = m_overlayMouseSpeed.exchange(speed);
    if (std::abs(oldSpeed - speed) > 0.01) {
        std::cout << "[InputMonitor] Overlay mouse speed updated: " << speed << "\n";
    }
}

void InputMonitor::setTrigger(const QString &type, int button, const QString &keySeq)
{
    int newType = (type == QLatin1String("keyboard")) ? 1 : 0;
    int newBtn = (button > 0 ? button : 273);

    std::vector<int> newKeyCodes;
    if (!keySeq.isEmpty()) {
        const QStringList parts = keySeq.split(QLatin1Char(','), Qt::SkipEmptyParts);
        for (const QString &p : parts) {
            bool ok = false;
            int k = p.trimmed().toInt(&ok);
            if (ok && k > 0) {
                newKeyCodes.push_back(k);
            }
        }
    }

    bool changed = (!m_triggerInitialized ||
                    m_triggerType.load() != newType ||
                    m_triggerButton.load() != newBtn ||
                    m_triggerKeyCodes != newKeyCodes);

    m_triggerInitialized = true;
    m_triggerType.store(newType);
    m_triggerButton.store(newBtn);
    m_triggerKeyCodes = std::move(newKeyCodes);

    if (changed) {
        std::cout << "[InputMonitor] Trigger configured: type=" << (newType == 1 ? "keyboard" : "mouse")
                  << ", button=" << newBtn
                  << ", keySeq=" << keySeq.toStdString() << "\n";
    }
}

bool InputMonitor::start()
{
    if (m_running.load()) return true;

    std::cout << "[InputMonitor] Starting (overlayMouseSpeed: " << m_overlayMouseSpeed.load()
              << ", holdTrigger: " << m_holdTriggerMs.load() << "ms)\n";

    setupUInput();
    scanDevices();

    if (m_devices.empty()) {
        std::cerr << "[InputMonitor] No mouse input devices found yet under /dev/input/event*. Waiting for hotplug...\n";
    }

    m_running.store(true);
    m_thread = std::thread(&InputMonitor::workerLoop, this);
    return true;
}

void InputMonitor::stop()
{
    if (!m_running.load()) return;

    m_running.store(false);
    if (m_thread.joinable()) {
        m_thread.join();
    }

    cleanup();
}

void InputMonitor::setupUInput()
{
    // 1. Relatives virtuelles Gerät für Passthrough
    m_uinputFd = open("/dev/uinput", O_WRONLY | O_NONBLOCK | O_CLOEXEC);
    if (m_uinputFd >= 0) {
        ioctl(m_uinputFd, UI_SET_EVBIT, EV_KEY);
        ioctl(m_uinputFd, UI_SET_EVBIT, EV_REL);
        ioctl(m_uinputFd, UI_SET_EVBIT, EV_SYN);

        for (int rel : {REL_X, REL_Y, REL_WHEEL, REL_HWHEEL, REL_DIAL, REL_WHEEL_HI_RES, REL_HWHEEL_HI_RES}) {
            ioctl(m_uinputFd, UI_SET_RELBIT, rel);
        }

        for (int btn = BTN_MOUSE; btn < BTN_JOYSTICK; ++btn) {
            ioctl(m_uinputFd, UI_SET_KEYBIT, btn);
        }

        struct uinput_setup usetup{};
        std::strncpy(usetup.name, "karyon-virtual mouse", UINPUT_MAX_NAME_SIZE);
        usetup.id.bustype = BUS_USB;
        usetup.id.vendor = 0x1234;
        usetup.id.product = 0x5678;
        usetup.id.version = 1;

        ioctl(m_uinputFd, UI_DEV_SETUP, &usetup);
        ioctl(m_uinputFd, UI_DEV_CREATE);
    }

    // 2. Absolutes virtuelles Gerät für Cursor-Warping unter Wayland
    m_uinputAbsFd = open("/dev/uinput", O_WRONLY | O_NONBLOCK | O_CLOEXEC);
    if (m_uinputAbsFd >= 0) {
        ioctl(m_uinputAbsFd, UI_SET_EVBIT, EV_KEY);
        ioctl(m_uinputAbsFd, UI_SET_KEYBIT, BTN_LEFT);
        ioctl(m_uinputAbsFd, UI_SET_EVBIT, EV_ABS);
        ioctl(m_uinputAbsFd, UI_SET_ABSBIT, ABS_X);
        ioctl(m_uinputAbsFd, UI_SET_ABSBIT, ABS_Y);

        struct uinput_abs_setup abs_x{}, abs_y{};
        abs_x.code = ABS_X;
        abs_x.absinfo.minimum = 0;
        abs_x.absinfo.maximum = 65535;
        abs_y.code = ABS_Y;
        abs_y.absinfo.minimum = 0;
        abs_y.absinfo.maximum = 65535;

        ioctl(m_uinputAbsFd, UI_ABS_SETUP, &abs_x);
        ioctl(m_uinputAbsFd, UI_ABS_SETUP, &abs_y);

        struct uinput_setup usetup_abs{};
        std::strncpy(usetup_abs.name, "karyon-virtual abs", UINPUT_MAX_NAME_SIZE);
        usetup_abs.id.bustype = BUS_VIRTUAL;
        ioctl(m_uinputAbsFd, UI_DEV_SETUP, &usetup_abs);
        ioctl(m_uinputAbsFd, UI_DEV_CREATE);
    }

    // 3. Virtuelles Keyboard für Gesten-Tastenkombinationen und Passthrough von Maus-Makro-/Tastaturtasten
    m_uinputKbdFd = open("/dev/uinput", O_WRONLY | O_NONBLOCK | O_CLOEXEC);
    if (m_uinputKbdFd >= 0) {
        ioctl(m_uinputKbdFd, UI_SET_EVBIT, EV_KEY);
        ioctl(m_uinputKbdFd, UI_SET_EVBIT, EV_SYN);

        for (int key = 1; key < BTN_MISC; ++key) {
            ioctl(m_uinputKbdFd, UI_SET_KEYBIT, key);
        }

        struct uinput_setup usetup_kbd{};
        std::strncpy(usetup_kbd.name, "karyon-virtual keyboard", UINPUT_MAX_NAME_SIZE);
        usetup_kbd.id.bustype = BUS_VIRTUAL;
        usetup_kbd.id.vendor = 0x1234;
        usetup_kbd.id.product = 0x5679;
        usetup_kbd.id.version = 1;
        ioctl(m_uinputKbdFd, UI_DEV_SETUP, &usetup_kbd);
        ioctl(m_uinputKbdFd, UI_DEV_CREATE);
    }

    m_uinputCreatedTime = std::chrono::steady_clock::now();
    std::cout << "[InputMonitor] Virtual input devices initialized (mouse, abs, keyboard)\n";
}

void InputMonitor::warpCursor(int targetX, int targetY, int screenW, int screenH)
{
    if (m_uinputAbsFd < 0 || screenW <= 0 || screenH <= 0) return;

    struct input_event ev[3]{};
    ev[0].type = EV_ABS;
    ev[0].code = ABS_X;
    ev[0].value = std::clamp((int)((targetX / (float)(screenW > 1 ? screenW - 1 : 1)) * 65535.0f), 0, 65535);

    ev[1].type = EV_ABS;
    ev[1].code = ABS_Y;
    ev[1].value = std::clamp((int)((targetY / (float)(screenH > 1 ? screenH - 1 : 1)) * 65535.0f), 0, 65535);

    ev[2].type = EV_SYN;
    ev[2].code = SYN_REPORT;

    [[maybe_unused]] auto res = write(m_uinputAbsFd, ev, sizeof(ev));
}

void InputMonitor::sendKeyCombo(const std::vector<int> &keys)
{
    if (m_uinputKbdFd < 0 || keys.empty()) return;

    // Press all keys in order
    for (int key : keys) {
        emitEvent(m_uinputKbdFd, EV_KEY, static_cast<uint16_t>(key), 1);
    }
    emitSyn(m_uinputKbdFd);

    // 15ms Haltezeit für zuverlässige Erkennung durch GUI-Anwendungen
    usleep(15000);

    // Release all keys in reverse order
    for (auto it = keys.rbegin(); it != keys.rend(); ++it) {
        emitEvent(m_uinputKbdFd, EV_KEY, static_cast<uint16_t>(*it), 0);
    }
    emitSyn(m_uinputKbdFd);
}

void InputMonitor::tryGrabPendingDevices()
{
    if (m_uinputFd < 0) return;

    bool anyUngrabbed = false;
    for (const auto &dev : m_devices) {
        if (!dev.grabbed && dev.fd >= 0) {
            anyUngrabbed = true;
            break;
        }
    }
    if (!anyUngrabbed) return;

    // Nach dem Erstellen der virtuellen uinput-Geräte kurz (250ms) warten, damit udev und
    // KWin/libinput die virtuellen Geräte vollständig geöffnet haben, bevor die physische
    // Maus per EVIOCGRAB übernommen wird.
    auto now = std::chrono::steady_clock::now();
    auto sinceUinput = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_uinputCreatedTime).count();
    if (sinceUinput < 250) {
        return;
    }

    bool newlyGrabbed = false;
    for (auto &dev : m_devices) {
        if (dev.grabbed || dev.fd < 0) continue;

        auto sinceAttempt = std::chrono::duration_cast<std::chrono::milliseconds>(now - dev.lastGrabAttempt).count();
        if (sinceAttempt < 500) {
            continue;
        }
        dev.lastGrabAttempt = now;

        // NIEMALS EVIOCGRAB ausführen, solange eine Maustaste oder Taste physisch gedrückt ist!
        // Sonst hat libinput/KWin das Press-Event noch auf dem physischen Gerät registriert,
        // bekommt aber das Release-Event dort nie mehr, wodurch die Taste im Compositor
        // dauerhaft als "gedrückt" hängen bleibt.
        if (hasPressedKeysOrButtons(dev.fd)) {
            continue;
        }

        if (ioctl(dev.fd, EVIOCGRAB, 1) == 0) {
            if (hasPressedKeysOrButtons(dev.fd)) {
                ioctl(dev.fd, EVIOCGRAB, 0);
                continue;
            }
            dev.grabbed = true;
            newlyGrabbed = true;
            m_leftDown.store(false);
            std::cout << "[InputMonitor] Mouse grabbed safely (all buttons released): "
                      << dev.path << " [" << dev.name << "]\n";
        }
    }

    if (newlyGrabbed) {
        QMetaObject::invokeMethod(this, [this]() {
            syncKWinMouseSettings();
        }, Qt::QueuedConnection);
    }
}

void InputMonitor::syncKWinMouseSettings()
{
    QStringList grabbedSysNames;
    for (const auto &dev : m_devices) {
        if (dev.grabbed && !dev.path.empty()) {
            std::string fname = fs::path(dev.path).filename().string();
            if (!fname.empty()) {
                grabbedSysNames.append(QString::fromStdString(fname));
            }
        }
    }
    if (grabbedSysNames.isEmpty()) return;

    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) return;

    QDBusMessage listMsg = QDBusMessage::createMethodCall(
        QStringLiteral("org.kde.KWin"),
        QStringLiteral("/org/kde/KWin/InputDevice"),
        QStringLiteral("org.freedesktop.DBus.Properties"),
        QStringLiteral("Get"));
    listMsg << QStringLiteral("org.kde.KWin.InputDeviceManager")
            << QStringLiteral("devicesSysNames");

    QDBusMessage listReply = bus.call(listMsg, QDBus::Block, 500);
    if (listReply.type() != QDBusMessage::ReplyMessage || listReply.arguments().isEmpty()) {
        return;
    }

    QStringList allSysNames = listReply.arguments().first().value<QDBusVariant>().variant().toStringList();
    if (allSysNames.isEmpty()) return;

    auto getDeviceProps = [&](const QString &sysName) -> QVariantMap {
        QDBusMessage msg = QDBusMessage::createMethodCall(
            QStringLiteral("org.kde.KWin"),
            QStringLiteral("/org/kde/KWin/InputDevice/") + sysName,
            QStringLiteral("org.freedesktop.DBus.Properties"),
            QStringLiteral("GetAll"));
        msg << QStringLiteral("org.kde.KWin.InputDevice");
        QDBusMessage reply = bus.call(msg, QDBus::Block, 300);
        if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
            return {};
        }
        QVariant firstArg = reply.arguments().first();
        if (firstArg.canConvert<QDBusArgument>()) {
            QVariantMap map;
            const QDBusArgument dbusArg = firstArg.value<QDBusArgument>();
            dbusArg >> map;
            return map;
        }
        return firstArg.toMap();
    };

    QString virtualMouseSysName;
    for (const QString &sysName : allSysNames) {
        QVariantMap props = getDeviceProps(sysName);
        if (props.value(QStringLiteral("name")).toString() == QStringLiteral("karyon-virtual mouse")) {
            virtualMouseSysName = sysName;
            break;
        }
    }
    if (virtualMouseSysName.isEmpty()) return;

    // Beste physische Maus auswählen:
    // 1. Zuerst unter den gegrabbten Geräten suchen, dann Fallback auf alle echten KWin-Pointer
    // 2. Touchpads, Trackpoints, Consumer Control etc. streng ausschließen bzw. stark abwerten
    // 3. Echte externe Mäuse mit benutzerdefinierten Einstellungen (Flat-Profil, PointerAcceleration != 0) bevorzugen
    QVariantMap bestProps;
    int bestScore = -10000;

    auto evaluateDevice = [&](const QString &sysName) {
        if (!allSysNames.contains(sysName)) return;
        QVariantMap props = getDeviceProps(sysName);
        if (props.isEmpty()) return;
        if (!props.value(QStringLiteral("pointer")).toBool()) return;
        if (props.value(QStringLiteral("touchpad")).toBool()) return;
        if (props.value(QStringLiteral("touch")).toBool()) return;
        if (props.value(QStringLiteral("tabletTool")).toBool()) return;
        if (props.value(QStringLiteral("isVirtual")).toBool()) return;

        QString devName = props.value(QStringLiteral("name")).toString();
        if (devName.startsWith(QStringLiteral("karyon-virtual"))) return;

        bool excluded = isExcludedNonMouse(devName);
        int score = 0;
        if (excluded) {
            score -= 2000; // TrackPoints, Touchpad-Begleitknoten, Media Controls
        } else {
            score += 500;  // Echte physische Maus
        }

        if (grabbedSysNames.contains(sysName)) {
            score += 300;
        }

        int vendor = props.value(QStringLiteral("vendor"), 0).toInt();
        if (vendor != 0 && vendor != 2) {
            score += 400; // USB / Bluetooth / Wireless (kein PS/2)
        }

        double devAccel = props.value(QStringLiteral("pointerAcceleration"), 0.0).toDouble();
        bool devFlat = props.value(QStringLiteral("pointerAccelerationProfileFlat"), false).toBool();
        bool devLeftHanded = props.value(QStringLiteral("leftHanded"), false).toBool();
        bool devNatScroll = props.value(QStringLiteral("naturalScroll"), false).toBool();
        double devScrollFac = props.value(QStringLiteral("scrollFactor"), 1.0).toDouble();

        if (devFlat) {
            score += 2000; // Nutzer hat Zeigerbeschleunigung bewusst deaktiviert!
        }
        if (std::abs(devAccel) > 0.001) {
            score += 1000;
        }
        if (devLeftHanded || devNatScroll || std::abs(devScrollFac - 1.0) > 0.001) {
            score += 500;
        }

        if (score > bestScore) {
            bestScore = score;
            bestProps = props;
        }
    };

    for (const QString &sysName : grabbedSysNames) {
        evaluateDevice(sysName);
    }

    if (bestScore < 0) {
        for (const QString &sysName : allSysNames) {
            evaluateDevice(sysName);
        }
    }

    if (bestProps.isEmpty()) return;

    QString bestDevName = bestProps.value(QStringLiteral("name")).toString();
    double accel = bestProps.value(QStringLiteral("pointerAcceleration"), 0.0).toDouble();
    bool flat = bestProps.value(QStringLiteral("pointerAccelerationProfileFlat"), false).toBool();
    bool leftHanded = bestProps.value(QStringLiteral("leftHanded"), false).toBool();
    bool natScroll = bestProps.value(QStringLiteral("naturalScroll"), false).toBool();
    double scrollFac = bestProps.value(QStringLiteral("scrollFactor"), 1.0).toDouble();

    // ~/.config/kcminputrc prüfen, um Benutzer-Profile exakt zu matchen
    QString kcmPath = QDir::homePath() + QStringLiteral("/.config/kcminputrc");
    if (QFile::exists(kcmPath)) {
        QSettings kcm(kcmPath, QSettings::IniFormat);
        for (const QString &group : kcm.childGroups()) {
            if (!group.startsWith(QStringLiteral("Libinput/")) && !group.startsWith(QStringLiteral("Libinput]["))) continue;
            if (group.contains(QStringLiteral("Touchpad"), Qt::CaseInsensitive)) continue;
            if (group.contains(QStringLiteral("karyon-virtual"), Qt::CaseInsensitive)) continue;

            kcm.beginGroup(group);
            if (kcm.contains(QStringLiteral("PointerAccelerationProfile"))) {
                int profile = kcm.value(QStringLiteral("PointerAccelerationProfile")).toInt();
                if (profile == 1) {
                    flat = true;
                } else if (profile == 2) {
                    flat = false;
                }
            }
            if (kcm.contains(QStringLiteral("PointerAcceleration"))) {
                accel = kcm.value(QStringLiteral("PointerAcceleration")).toDouble();
            }
            if (kcm.contains(QStringLiteral("ScrollFactor"))) {
                scrollFac = kcm.value(QStringLiteral("ScrollFactor")).toDouble();
            }
            if (kcm.contains(QStringLiteral("NaturalScroll"))) {
                natScroll = kcm.value(QStringLiteral("NaturalScroll")).toBool();
            }
            if (kcm.contains(QStringLiteral("LeftHanded"))) {
                leftHanded = kcm.value(QStringLiteral("LeftHanded")).toBool();
            }
            kcm.endGroup();
            break;
        }
    }

    auto setVirtualProp = [&](const QString &propName, const QVariant &val) {
        QDBusMessage msg = QDBusMessage::createMethodCall(
            QStringLiteral("org.kde.KWin"),
            QStringLiteral("/org/kde/KWin/InputDevice/") + virtualMouseSysName,
            QStringLiteral("org.freedesktop.DBus.Properties"),
            QStringLiteral("Set"));
        msg << QStringLiteral("org.kde.KWin.InputDevice")
            << propName
            << QVariant::fromValue(QDBusVariant(val));
        bus.call(msg, QDBus::NoBlock);
    };

    setVirtualProp(QStringLiteral("pointerAcceleration"), accel);
    if (flat) {
        setVirtualProp(QStringLiteral("pointerAccelerationProfileFlat"), true);
        setVirtualProp(QStringLiteral("pointerAccelerationProfileAdaptive"), false);
    } else {
        setVirtualProp(QStringLiteral("pointerAccelerationProfileAdaptive"), true);
        setVirtualProp(QStringLiteral("pointerAccelerationProfileFlat"), false);
    }
    setVirtualProp(QStringLiteral("leftHanded"), leftHanded);
    setVirtualProp(QStringLiteral("naturalScroll"), natScroll);
    setVirtualProp(QStringLiteral("scrollFactor"), scrollFac);

    // In ~/.config/kcminputrc persistieren, damit KDE System Settings die virtuelle Maus synchron hält
    if (QFile::exists(kcmPath)) {
        QSettings kcm(kcmPath, QSettings::IniFormat);
        QString vGroup = QStringLiteral("Libinput][4660][22136][karyon-virtual mouse");
        kcm.beginGroup(vGroup);
        kcm.setValue(QStringLiteral("PointerAccelerationProfile"), flat ? 1 : 2);
        kcm.setValue(QStringLiteral("PointerAcceleration"), QString::number(accel, 'f', 3));
        kcm.setValue(QStringLiteral("ScrollFactor"), QString::number(scrollFac, 'f', 3));
        kcm.setValue(QStringLiteral("NaturalScroll"), natScroll);
        kcm.setValue(QStringLiteral("LeftHanded"), leftHanded);
        kcm.endGroup();
        kcm.sync();
    }

    std::cout << "[InputMonitor] Synced mouse settings from \""
              << bestDevName.toStdString()
              << "\" to karyon-virtual mouse (accel="
              << accel
              << ", profile=" << (flat ? "flat (disabled)" : "adaptive (enabled)")
              << ", scrollFactor=" << scrollFac
              << ")\n";
}

void InputMonitor::scanDevices()
{
    std::error_code ec;
    if (!fs::exists("/dev/input", ec)) return;

    for (const auto &entry : fs::directory_iterator("/dev/input", ec)) {
        const std::string filename = entry.path().filename().string();
        if (filename.rfind("event", 0) != 0) continue;

        int fd = open(entry.path().c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) continue;

        if (isMouseDevice(fd)) {
            char devName[256] = {0};
            ioctl(fd, EVIOCGNAME(sizeof(devName)), devName);
            std::string sName = (devName[0] != '\0') ? std::string(devName) : entry.path().string();

            // Zunächst ungrabbed registrieren; tryGrabPendingDevices() übernimmt den EVIOCGRAB,
            // sobald uinput bereit ist und garantiert keine Maustaste gedrückt gehalten wird.
            m_devices.push_back({fd, entry.path().string(), false, sName});
            m_lastReportedMouse = sName;
            m_lastReportedTime = std::chrono::steady_clock::now();

            std::cout << "[InputMonitor] Mouse registered: " << entry.path().string() 
                      << " [" << sName << "] (pending safe grab)\n";
        } else if (isKeyboardDevice(fd)) {
            char devName[256] = {0};
            ioctl(fd, EVIOCGNAME(sizeof(devName)), devName);
            std::string sName = (devName[0] != '\0') ? std::string(devName) : entry.path().string();

            m_keyboards.push_back({fd, entry.path().string(), false, sName});
            std::cout << "[InputMonitor] Keyboard registered: " << entry.path().string()
                      << " [" << sName << "] (passive monitoring)\n";
        } else {
            close(fd);
        }
    }
}

bool InputMonitor::rescanDevices()
{
    bool changed = false;

    // 1. Ungültige/getrennte FDs entfernen
    for (auto it = m_devices.begin(); it != m_devices.end();) {
        bool alive = true;
        char name[256];
        if (it->fd < 0 || !fs::exists(it->path) || fcntl(it->fd, F_GETFD) < 0 ||
            ioctl(it->fd, EVIOCGNAME(sizeof(name)), name) < 0) {
            alive = false;
        }
        if (!alive) {
            std::cout << "[InputMonitor] Mouse disconnected/invalidated: " << it->path << "\n";
            if (it->fd >= 0) {
                if (it->grabbed) ioctl(it->fd, EVIOCGRAB, 0);
                close(it->fd);
            }
            it = m_devices.erase(it);
            changed = true;
        } else {
            ++it;
        }
    }

    for (auto it = m_keyboards.begin(); it != m_keyboards.end();) {
        bool alive = true;
        char name[256];
        if (it->fd < 0 || !fs::exists(it->path) || fcntl(it->fd, F_GETFD) < 0 ||
            ioctl(it->fd, EVIOCGNAME(sizeof(name)), name) < 0) {
            alive = false;
        }
        if (!alive) {
            std::cout << "[InputMonitor] Keyboard disconnected/invalidated: " << it->path << "\n";
            if (it->fd >= 0) {
                close(it->fd);
            }
            it = m_keyboards.erase(it);
            changed = true;
        } else {
            ++it;
        }
    }

    if (m_devices.empty() && changed) {
        m_lastReportedMouse.clear();
        emit mouseDisconnected();
    }

    // 2. /dev/input nach neu eingesteckten Geräten durchsuchen
    std::error_code ec;
    std::string newlyConnectedName;
    if (fs::exists("/dev/input", ec)) {
        for (const auto &entry : fs::directory_iterator("/dev/input", ec)) {
            const std::string filename = entry.path().filename().string();
            if (filename.rfind("event", 0) != 0) continue;

            const std::string path = entry.path().string();
            bool alreadyOpened = false;
            for (const auto &dev : m_devices) {
                if (dev.path == path) {
                    alreadyOpened = true;
                    break;
                }
            }
            if (!alreadyOpened) {
                for (const auto &kbd : m_keyboards) {
                    if (kbd.path == path) {
                        alreadyOpened = true;
                        break;
                    }
                }
            }
            if (alreadyOpened) continue;

            int fd = open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
            if (fd < 0) continue;

            if (isMouseDevice(fd)) {
                char devName[256] = {0};
                ioctl(fd, EVIOCGNAME(sizeof(devName)), devName);
                std::string sName = (devName[0] != '\0') ? std::string(devName) : path;

                m_devices.push_back({fd, path, false, sName});
                changed = true;
                newlyConnectedName = sName;

                std::cout << "[InputMonitor] Mouse registered (hotplug): " << path 
                          << " [" << sName << "] (pending safe grab)\n";
            } else if (isKeyboardDevice(fd)) {
                char devName[256] = {0};
                ioctl(fd, EVIOCGNAME(sizeof(devName)), devName);
                std::string sName = (devName[0] != '\0') ? std::string(devName) : path;

                m_keyboards.push_back({fd, path, false, sName});
                changed = true;
                std::cout << "[InputMonitor] Keyboard registered (hotplug): " << path
                          << " [" << sName << "] (passive monitoring)\n";
            } else {
                close(fd);
            }
        }
    }

    tryGrabPendingDevices();

    if (!newlyConnectedName.empty()) {
        auto now = std::chrono::steady_clock::now();
        if (newlyConnectedName != m_lastReportedMouse ||
            std::chrono::duration_cast<std::chrono::seconds>(now - m_lastReportedTime).count() > 5) {
            m_lastReportedMouse = newlyConnectedName;
            m_lastReportedTime = now;
            emit mouseConnected(QString::fromStdString(newlyConnectedName));
        }
    }

    return changed;
}

void InputMonitor::cleanup()
{
    if (m_inotifyFd >= 0) {
        close(m_inotifyFd);
        m_inotifyFd = -1;
    }

    for (auto &dev : m_devices) {
        if (dev.fd >= 0) {
            if (dev.grabbed) {
                ioctl(dev.fd, EVIOCGRAB, 0);
            }
            close(dev.fd);
        }
    }
    m_devices.clear();
    m_lastReportedMouse.clear();

    for (auto &kbd : m_keyboards) {
        if (kbd.fd >= 0) {
            close(kbd.fd);
        }
    }
    m_keyboards.clear();

    if (m_uinputFd >= 0) {
        for (int b = BTN_MOUSE; b < BTN_JOYSTICK; ++b) {
            emitEvent(m_uinputFd, EV_KEY, static_cast<uint16_t>(b), 0);
        }
        emitSyn(m_uinputFd);
        ioctl(m_uinputFd, UI_DEV_DESTROY);
        close(m_uinputFd);
        m_uinputFd = -1;
    }

    if (m_uinputAbsFd >= 0) {
        ioctl(m_uinputAbsFd, UI_DEV_DESTROY);
        close(m_uinputAbsFd);
        m_uinputAbsFd = -1;
    }

    if (m_uinputKbdFd >= 0) {
        ioctl(m_uinputKbdFd, UI_DEV_DESTROY);
        close(m_uinputKbdFd);
        m_uinputKbdFd = -1;
    }
}

void InputMonitor::workerLoop()
{
    State state = State::Idle;
    auto pressTime = std::chrono::steady_clock::now();
    int deltaX = 0;
    int deltaY = 0;
    float dragDistance = 0.0f;
    bool trailActive = false;
    bool gestureFired = false;
    int pendingGestureDir = -1;

    // Aiming-Position relativ zum Hubzentrum:
    // Der Cursor steht beim Öffnen 20px ÜBER dem Hubzentrum -> (0, -20)
    int aimX = 0;
    int aimY = -20;
    bool aimDirty = false;
    double subpixelX = 0.0;
    double subpixelY = 0.0;
    int wheelHiResAccum = 0;

    m_inotifyFd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (m_inotifyFd >= 0) {
        inotify_add_watch(m_inotifyFd, "/dev/input", IN_CREATE | IN_DELETE | IN_ATTRIB);
    }

    std::vector<pollfd> pfds;
    auto rebuildPfds = [&pfds, this]() {
        pfds.clear();
        for (const auto &dev : m_devices) {
            pfds.push_back({dev.fd, POLLIN, 0});
        }
        for (const auto &kbd : m_keyboards) {
            pfds.push_back({kbd.fd, POLLIN, 0});
        }
        if (m_inotifyFd >= 0) {
            pfds.push_back({m_inotifyFd, POLLIN, 0});
        }
    };
    rebuildPfds();

    std::vector<bool> activeKeys(KEY_CNT, false);
    std::vector<bool> uinputBtnDown(KEY_MAX + 1, false);

    auto emitUInputButton = [&](uint16_t code, int32_t val) {
        if (m_uinputFd < 0) return;
        emitEvent(m_uinputFd, EV_KEY, code, val);
        emitSyn(m_uinputFd);
        if (code < uinputBtnDown.size()) {
            uinputBtnDown[code] = (val != 0);
        }
    };

    auto releaseAllUInputButtons = [&]() {
        if (m_uinputFd < 0) return;
        bool anyReleased = false;
        for (size_t b = 0; b < uinputBtnDown.size(); ++b) {
            if (uinputBtnDown[b]) {
                // Wenn LMT physisch gedrückt gehalten wird (aktives Drag & Drop),
                // darf BTN_LEFT keinesfalls zwangsweise losgelassen werden!
                if (b == BTN_LEFT && m_leftDown.load()) {
                    continue;
                }
                emitEvent(m_uinputFd, EV_KEY, static_cast<uint16_t>(b), 0);
                uinputBtnDown[b] = false;
                anyReleased = true;
            }
        }
        if (anyReleased) {
            emitSyn(m_uinputFd);
        }
    };

    int pendingHotplugRetries = 0;
    auto lastHotplugCheck = std::chrono::steady_clock::now();
    auto lastIdleScan = std::chrono::steady_clock::now();

    while (m_running.load()) {
        if (m_rescanRequested.exchange(false)) {
            if (rescanDevices()) {
                rebuildPfds();
            }
        }

        if (m_releaseButtonsRequested.exchange(false)) {
            releaseAllUInputButtons();
        }

        tryGrabPendingDevices();

        int pollTimeout = 250;
        if (state == State::Pending) {
            pollTimeout = 5;
        } else if (pendingHotplugRetries > 0) {
            pollTimeout = 150;
        } else if (m_devices.empty()) {
            pollTimeout = 1000;
        }
        int ret = poll(pfds.data(), pfds.size(), pollTimeout);

        auto now = std::chrono::steady_clock::now();
        int currentTriggerBtn = m_triggerButton.load();

        if (state == State::Pending) {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - pressTime).count();
            bool isLmbDrag = m_leftDown.load();
            if ((dragDistance < 14.0f || isLmbDrag) && elapsed >= m_holdTriggerMs.load()) {
                if (trailActive) {
                    trailActive = false;
                    emit gestureFinished();
                }
                state = State::Triggered;
                wheelHiResAccum = 0;
                subpixelX = 0.0;
                subpixelY = 0.0;
                aimX = deltaX;
                aimY = -20 + deltaY;
                emit holdTriggered(deltaX, deltaY);
                emit cursorMoved(aimX, aimY);
            } else if (!isLmbDrag && dragDistance >= 14.0f && elapsed > GESTURE_TIME_WINDOW_MS) {
                if (trailActive) {
                    trailActive = false;
                    emit gestureFinished();
                }
                state = State::Dragging;
                pendingGestureDir = -1;
                for (const auto &dev : m_devices) {
                    if (dev.grabbed && m_uinputFd >= 0) {
                        emitUInputButton(static_cast<uint16_t>(currentTriggerBtn), 1);
                        break;
                    }
                }
            }
        }

        bool needsRebuild = false;

        // Hotplug retry timer: allows udev to apply user ACLs to newly created devices
        if (pendingHotplugRetries > 0) {
            auto elapsedRetry = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastHotplugCheck).count();
            if (elapsedRetry >= 150) {
                lastHotplugCheck = now;
                pendingHotplugRetries--;
                if (rescanDevices()) {
                    needsRebuild = true;
                }
            }
        }

        // Periodic fallback scan when no mouse is connected at all
        if (m_devices.empty()) {
            auto elapsedIdle = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastIdleScan).count();
            if (elapsedIdle >= 1500) {
                lastIdleScan = now;
                if (rescanDevices()) {
                    needsRebuild = true;
                }
            }
        }

        if (ret <= 0) {
            if (needsRebuild) {
                rebuildPfds();
            }
            continue;
        }

        // 1. Hotplug-Erkennung via inotify
        if (m_inotifyFd >= 0) {
            for (const auto &pfd : pfds) {
                if (pfd.fd == m_inotifyFd && (pfd.revents & POLLIN)) {
                    char ibuf[1024];
                    while (read(m_inotifyFd, ibuf, sizeof(ibuf)) > 0) {}
                    if (rescanDevices()) {
                        needsRebuild = true;
                    }
                    // Trigger retries to allow udev ACL to settle
                    pendingHotplugRetries = 10;
                    lastHotplugCheck = now;
                    break;
                }
            }
        }

        // 2. Trennungs- und Fehlererkennung an FDs
        for (size_t i = 0; i < m_devices.size() && i < pfds.size(); ++i) {
            if (pfds[i].revents & (POLLHUP | POLLERR | POLLNVAL)) {
                std::cout << "[InputMonitor] Poll hangup on mouse " << m_devices[i].path << "\n";
                if (m_devices[i].grabbed) ioctl(m_devices[i].fd, EVIOCGRAB, 0);
                close(m_devices[i].fd);
                m_devices.erase(m_devices.begin() + i);
                needsRebuild = true;
                if (m_devices.empty()) {
                    m_lastReportedMouse.clear();
                    emit mouseDisconnected();
                }
                break;
            }
        }

        size_t kbdOffset = m_devices.size();
        for (size_t i = 0; i < m_keyboards.size() && (kbdOffset + i) < pfds.size(); ++i) {
            if (pfds[kbdOffset + i].revents & (POLLHUP | POLLERR | POLLNVAL)) {
                std::cout << "[InputMonitor] Poll hangup on keyboard " << m_keyboards[i].path << "\n";
                close(m_keyboards[i].fd);
                m_keyboards.erase(m_keyboards.begin() + i);
                needsRebuild = true;
                break;
            }
        }

        if (needsRebuild) {
            rebuildPfds();
            continue;
        }

        // Trigger-Aufnahme-Modus: Alle Mausklicks und Tastendrücke für Trigger erfassen
        if (m_isRecordingTrigger.load()) {
            for (size_t i = 0; i < m_devices.size() && i < pfds.size(); ++i) {
                if (!(pfds[i].revents & POLLIN)) continue;
                auto &dev = m_devices[i];
                struct input_event events[64];
                ssize_t bytes = read(dev.fd, events, sizeof(events));
                if (bytes <= 0) continue;
                size_t cnt = bytes / sizeof(struct input_event);
                for (size_t k = 0; k < cnt; ++k) {
                    const auto &ev = events[k];
                    // BTN_LEFT (272) ist für die UI-Bedienung reserviert - niemals als Trigger erfassen!
                    if (ev.type == EV_KEY && ev.value == 1 && ev.code > BTN_LEFT && ev.code < BTN_JOYSTICK) {
                        QString name = mouseButtonNameFromEvdev(ev.code);
                        emit inputCaptured(QStringLiteral("mouse"), ev.code, QString(), name);
                    }
                }
                if (dev.grabbed && m_uinputFd >= 0) {
                    [[maybe_unused]] auto w = write(m_uinputFd, events, bytes);
                }
            }
            for (size_t i = 0; i < m_keyboards.size() && (kbdOffset + i) < pfds.size(); ++i) {
                if (!(pfds[kbdOffset + i].revents & POLLIN)) continue;
                auto &kbd = m_keyboards[i];
                struct input_event events[64];
                ssize_t bytes = read(kbd.fd, events, sizeof(events));
                if (bytes <= 0) continue;
                size_t cnt = bytes / sizeof(struct input_event);
                for (size_t k = 0; k < cnt; ++k) {
                    const auto &ev = events[k];
                    if (ev.type == EV_KEY) {
                        if (ev.code < KEY_CNT) activeKeys[ev.code] = (ev.value != 0);
                        if (ev.value == 1) {
                            if (ev.code != KEY_LEFTCTRL && ev.code != KEY_RIGHTCTRL &&
                                ev.code != KEY_LEFTALT && ev.code != KEY_RIGHTALT &&
                                ev.code != KEY_LEFTSHIFT && ev.code != KEY_RIGHTSHIFT &&
                                ev.code != KEY_LEFTMETA && ev.code != KEY_RIGHTMETA) {
                                QStringList parts;
                                std::vector<int> kseq;
                                if (activeKeys[KEY_LEFTCTRL] || activeKeys[KEY_RIGHTCTRL]) {
                                    parts << QStringLiteral("Strg");
                                    kseq.push_back(KEY_LEFTCTRL);
                                }
                                if (activeKeys[KEY_LEFTALT] || activeKeys[KEY_RIGHTALT]) {
                                    parts << QStringLiteral("Alt");
                                    kseq.push_back(KEY_LEFTALT);
                                }
                                if (activeKeys[KEY_LEFTSHIFT] || activeKeys[KEY_RIGHTSHIFT]) {
                                    parts << QStringLiteral("Shift");
                                    kseq.push_back(KEY_LEFTSHIFT);
                                }
                                if (activeKeys[KEY_LEFTMETA] || activeKeys[KEY_RIGHTMETA]) {
                                    parts << QStringLiteral("Super");
                                    kseq.push_back(KEY_LEFTMETA);
                                }
                                QString kName = keyboardKeyNameFromEvdev(ev.code);
                                if (!kName.isEmpty()) {
                                    parts << kName;
                                    kseq.push_back(ev.code);
                                    QStringList seqStrs;
                                    for (int sk : kseq) seqStrs << QString::number(sk);
                                    emit inputCaptured(QStringLiteral("keyboard"), 0, seqStrs.join(QLatin1Char(',')), parts.join(QLatin1Char('+')));
                                }
                            }
                        }
                    }
                }
            }
            continue;
        }

        // Inhibited Modus: Einstellungen-Fenster ist offen -> volle Weiterleitung ohne Trigger-Interzeption
        if (m_inhibited.load()) {
            for (size_t i = 0; i < m_devices.size() && i < pfds.size(); ++i) {
                if (!(pfds[i].revents & POLLIN)) continue;
                auto &dev = m_devices[i];
                struct input_event events[64];
                ssize_t bytes = read(dev.fd, events, sizeof(events));
                if (bytes <= 0) continue;
                if (dev.grabbed && m_uinputFd >= 0) {
                    [[maybe_unused]] auto w = write(m_uinputFd, events, bytes);
                }
            }
            for (size_t i = 0; i < m_keyboards.size() && (kbdOffset + i) < pfds.size(); ++i) {
                if (!(pfds[kbdOffset + i].revents & POLLIN)) continue;
                struct input_event events[64];
                [[maybe_unused]] auto bytes = read(m_keyboards[i].fd, events, sizeof(events));
            }
            continue;
        }

        // 3. Physische Maus-Events verarbeiten
        for (size_t i = 0; i < m_devices.size() && i < pfds.size(); ++i) {
            if (!(pfds[i].revents & POLLIN)) continue;

            auto &dev = m_devices[i];
            struct input_event events[64];
            ssize_t bytes = read(dev.fd, events, sizeof(events));
            if (bytes <= 0) {
                if (bytes < 0 && (errno == ENODEV || errno == EBADF || errno == EIO || errno == ENOTTY)) {
                    std::cout << "[InputMonitor] Device disconnected: " << dev.path << " (errno " << errno << ")\n";
                    if (dev.grabbed) ioctl(dev.fd, EVIOCGRAB, 0);
                    close(dev.fd);
                    m_devices.erase(m_devices.begin() + i);
                    if (m_devices.empty()) {
                        m_lastReportedMouse.clear();
                        emit mouseDisconnected();
                    }
                    rebuildPfds();
                    break;
                }
                continue;
            }

            if (!dev.grabbed && m_uinputFd >= 0) {
                tryGrabPendingDevices();
                if (!dev.grabbed) {
                    continue;
                }
            }

            if (m_syncAimRequested.load(std::memory_order_acquire)) {
                m_syncAimRequested.store(false, std::memory_order_relaxed);
                aimX = m_syncAimX.load(std::memory_order_relaxed);
                aimY = m_syncAimY.load(std::memory_order_relaxed);
            }

            int32_t passthruRelX = 0;
            int32_t passthruRelY = 0;
            bool hasPassthruRel = false;
            bool nonRelPassthruSinceSyn = false;

            int32_t trigRelX = 0;
            int32_t trigRelY = 0;
            bool trigBatchDirty = false;

            auto flushPassthruRel = [&]() {
                if (!hasPassthruRel) return;
                hasPassthruRel = false;
                if (dev.grabbed && m_uinputFd >= 0) {
                    struct input_event outEvs[3]{};
                    int n = 0;
                    if (passthruRelX != 0) {
                        outEvs[n].type = EV_REL;
                        outEvs[n].code = REL_X;
                        outEvs[n].value = passthruRelX;
                        ++n;
                    }
                    if (passthruRelY != 0) {
                        outEvs[n].type = EV_REL;
                        outEvs[n].code = REL_Y;
                        outEvs[n].value = passthruRelY;
                        ++n;
                    }
                    if (n > 0) {
                        outEvs[n].type = EV_SYN;
                        outEvs[n].code = SYN_REPORT;
                        outEvs[n].value = 0;
                        ++n;
                        [[maybe_unused]] auto w = write(m_uinputFd, outEvs, n * sizeof(struct input_event));
                    }
                }
                passthruRelX = 0;
                passthruRelY = 0;
            };

            auto flushTriggeredRel = [&]() {
                if (!trigBatchDirty) return;
                trigBatchDirty = false;
                aimDirty = false;
                if (dev.grabbed && m_uinputFd >= 0) {
                    struct input_event outEvs[3]{};
                    int n = 0;
                    if (trigRelX != 0) {
                        outEvs[n].type = EV_REL;
                        outEvs[n].code = REL_X;
                        outEvs[n].value = trigRelX;
                        ++n;
                    }
                    if (trigRelY != 0) {
                        outEvs[n].type = EV_REL;
                        outEvs[n].code = REL_Y;
                        outEvs[n].value = trigRelY;
                        ++n;
                    }
                    if (n > 0) {
                        outEvs[n].type = EV_SYN;
                        outEvs[n].code = SYN_REPORT;
                        outEvs[n].value = 0;
                        ++n;
                        [[maybe_unused]] auto w = write(m_uinputFd, outEvs, n * sizeof(struct input_event));
                    }
                }
                trigRelX = 0;
                trigRelY = 0;
                emit cursorMoved(aimX, aimY);
            };

            size_t count = bytes / sizeof(struct input_event);
            for (size_t k = 0; k < count; ++k) {
                const auto &ev = events[k];

                // Tastatur-/Makro-Events von Mäusen (z.B. Logitech MX / Bolt / Unifying HID++)
                // direkt an das virtuelle Keyboard weiterleiten, damit sie durch EVIOCGRAB nicht verloren gehen
                if (ev.type == EV_KEY && ev.code < BTN_MISC) {
                    if (dev.grabbed && m_uinputKbdFd >= 0) {
                        emitEvent(m_uinputKbdFd, EV_KEY, ev.code, ev.value);
                        emitSyn(m_uinputKbdFd);
                    }
                    continue;
                }

                // Vor jedem Maustasten-Event etwaige aufgelaufene Bewegungen zuerst abschicken
                if (ev.type == EV_KEY) {
                    flushPassthruRel();
                    flushTriggeredRel();
                }

                if (ev.type == EV_KEY) {
                    bool isMouseTrigger = (m_triggerType.load() == 0);
                    int currentTriggerBtn = m_triggerButton.load();
                    bool isTrigger = isMouseTrigger && (ev.code == currentTriggerBtn);
                    int actionBtn = (isMouseTrigger && currentTriggerBtn == BTN_MIDDLE) ? BTN_RIGHT : BTN_MIDDLE;
                    bool isAction = (ev.code == actionBtn);

                    if (ev.code == BTN_LEFT) {
                        m_leftDown.store(ev.value == 1);
                    }

                    // 1. STATE: Triggered (Radial Overlay ist geöffnet und aktiv)
                    if (state == State::Triggered) {
                        if (isTrigger) {
                            if (ev.value == 0) { // Trigger losgelassen -> Segment auswählen!
                                state = State::Idle;
                                pendingGestureDir = -1;
                                subpixelX = 0.0;
                                subpixelY = 0.0;
                                emit holdReleased();
                                releaseAllUInputButtons();
                            }
                            continue;
                        } else if (isAction) {
                            if (ev.value == 1) { // Mittelklick-Aktion (Fenster schließen / Mute)
                                emit middleClicked();
                            }
                            continue;
                        } else {
                            // JEDER andere Mausklick (LMT, RMT falls anderer Trigger, Daumentasten etc.):
                            // Bricht das Overlay sauber ab, OHNE Klicks an Hintergrundfenster weiterzuleiten!
                            if (ev.value == 1) {
                                state = State::Cancelled;
                                pendingGestureDir = -1;
                                if (trailActive) {
                                    trailActive = false;
                                    emit gestureFinished();
                                }
                                emit holdCancelled();
                                releaseAllUInputButtons();
                            }
                            continue;
                        }
                    }

                    // 2. STATE: Pending (Trigger gedrückt, wartet auf Haltezeit / Geste)
                    else if (state == State::Pending) {
                        if (isTrigger) {
                            if (ev.value == 0) { // Vor Haltezeit losgelassen -> Kurzklick
                                state = State::Idle;
                                pendingGestureDir = -1;
                                if (trailActive) {
                                    trailActive = false;
                                    emit gestureFinished();
                                }
                                if (dragDistance < 14.0f) {
                                    if (dev.grabbed && m_uinputFd >= 0) {
                                        emitUInputButton(static_cast<uint16_t>(currentTriggerBtn), 1);
                                        emitUInputButton(static_cast<uint16_t>(currentTriggerBtn), 0);
                                    }
                                }
                            }
                            continue;
                        } else {
                            // JEDE andere Maustaste während Pending bricht die Auslösung sofort ab.
                            // Keinerlei Akkord-/Doppelklick-Weiterleitung, um Hänger und Fokusverlust zu verhindern!
                            if (ev.value == 1) {
                                state = State::Cancelled;
                                pendingGestureDir = -1;
                                if (trailActive) {
                                    trailActive = false;
                                    emit gestureFinished();
                                }
                                emit holdCancelled();
                                releaseAllUInputButtons();
                            }
                            continue;
                        }
                    }

                    // 3. STATE: Cancelled (Overlay geschlossen, wartet auf physischen Trigger-Release)
                    else if (state == State::Cancelled) {
                        if (isTrigger && ev.value == 0) {
                            state = State::Idle;
                            pendingGestureDir = -1;
                            releaseAllUInputButtons();
                        }
                        continue;
                    }

                    // 4. STATE: Gesture (Geste erkannt, wartet auf Trigger-Release)
                    else if (state == State::Gesture) {
                        if (isTrigger && ev.value == 0) {
                            state = State::Idle;
                            pendingGestureDir = -1;
                            releaseAllUInputButtons();
                        } else if (!isTrigger && ev.value == 1) {
                            state = State::Cancelled;
                            pendingGestureDir = -1;
                            if (trailActive) {
                                trailActive = false;
                                emit gestureFinished();
                            }
                            releaseAllUInputButtons();
                        }
                        continue;
                    }

                    // 5. STATE: Dragging (Desktop-Drag mit Triggertaste)
                    else if (state == State::Dragging) {
                        if (isTrigger && ev.value == 0) {
                            state = State::Idle;
                            pendingGestureDir = -1;
                            if (dev.grabbed && m_uinputFd >= 0) {
                                emitUInputButton(static_cast<uint16_t>(currentTriggerBtn), 0);
                            }
                            releaseAllUInputButtons();
                            continue;
                        } else if (!isTrigger && ev.value == 1) {
                            state = State::Idle;
                            pendingGestureDir = -1;
                            releaseAllUInputButtons();
                            continue;
                        }
                        if (!isTrigger) continue;
                    }

                    // 6. STATE: Idle (Normaler Desktop-Zustand)
                    else if (state == State::Idle) {
                        if (isTrigger) {
                            if (m_gameModeEnabled.load() && m_gameModeActive.load()) {
                                if (dev.grabbed && m_uinputFd >= 0) {
                                    emitUInputButton(static_cast<uint16_t>(currentTriggerBtn), ev.value);
                                }
                                continue;
                            }

                            if (ev.value == 1) {
                                pressTime = std::chrono::steady_clock::now();
                                deltaX = 0;
                                deltaY = 0;
                                dragDistance = 0.0f;
                                trailActive = false;
                                gestureFired = false;
                                pendingGestureDir = -1;
                                state = State::Pending;
                                emit rightPressed();
                                emit triggerPressed();
                                continue;
                            } else if (ev.value == 0) {
                                releaseAllUInputButtons();
                                continue;
                            }
                        }
                        // Wenn !isTrigger in Idle:
                        // Normaler Mausklick auf dem Desktop -> Weiterleitung nach unten zu Passthrough!
                    }
                }
                    if (state == State::Gesture && ev.type == EV_REL) {
                        // Geste ist erkannt: Trail folgt dem dynamischen Wisch-Schwung bis zum Release
                        if (ev.code == REL_X) deltaX += ev.value;
                        if (ev.code == REL_Y) deltaY += ev.value;
                        auto now = std::chrono::steady_clock::now();
                        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - pressTime).count();
                        if (trailActive) {
                            if (elapsed <= GESTURE_TIME_WINDOW_MS) {
                                emit gesturePointAdded(deltaX, deltaY);
                            } else {
                                trailActive = false;
                                emit gestureFinished();
                            }
                        }
                    } else if (state == State::Pending && ev.type == EV_REL) {
                        if (ev.code == REL_X) deltaX += ev.value;
                        if (ev.code == REL_Y) deltaY += ev.value;
                        dragDistance += std::abs(ev.value);

                        bool isLmbDrag = m_leftDown.load();
                        if (!isLmbDrag && !trailActive && !gestureFired && dragDistance >= 6.0f) {
                            trailActive = true;
                            emit gestureStarted();
                        }
                        if (trailActive) {
                            emit gesturePointAdded(deltaX, deltaY);
                        }

                        if (!gestureFired) {
                            auto now = std::chrono::steady_clock::now();
                            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - pressTime).count();
                            double dispSq = static_cast<double>(deltaX) * deltaX + static_cast<double>(deltaY) * deltaY;

                            if (!isLmbDrag && dispSq >= GESTURE_MIN_DISPLACEMENT * GESTURE_MIN_DISPLACEMENT && elapsed <= GESTURE_TIME_WINDOW_MS) {
                                gestureFired = true;
                                state = State::Gesture;
                                int dir = GestureManager::determineDirection(deltaX, deltaY);
                                pendingGestureDir = -1;
                                emit gestureDetected(dir, deltaX, deltaY);
                                if (trailActive) {
                                    trailActive = false;
                                    emit gestureFinished();
                                }
                            } else if (!isLmbDrag && dragDistance > 14.0f && elapsed > GESTURE_TIME_WINDOW_MS) {
                                if (trailActive) {
                                    trailActive = false;
                                    emit gestureFinished();
                                }
                                state = State::Dragging;
                                pendingGestureDir = -1;
                                flushPassthruRel();
                                if (dev.grabbed && m_uinputFd >= 0) {
                                    emitUInputButton(static_cast<uint16_t>(currentTriggerBtn), 1);
                                }
                            }
                        }
                    } else if (state == State::Triggered) {
                        if (ev.type == EV_REL) {
                            if (ev.code == REL_X || ev.code == REL_Y) {
                                int effDelta = ev.value;
                                if (dev.grabbed && m_uinputFd >= 0) {
                                    double speedVal = m_overlayMouseSpeed.load();
                                    double speedFactor = 1.0;
                                    if (speedVal >= 1.0) {
                                        speedFactor = 1.0;
                                    } else if (speedVal <= -1.0) {
                                        speedFactor = 0.35;
                                    } else if (speedVal <= 0.0) {
                                        speedFactor = 0.75 + speedVal * 0.40;
                                    } else {
                                        speedFactor = 0.75 + speedVal * 0.25;
                                    }

                                    double rawDelta = ev.value;
                                    double &subpixel = (ev.code == REL_X) ? subpixelX : subpixelY;
                                    double target = rawDelta * speedFactor + subpixel;
                                    int outVal = static_cast<int>(std::round(target));
                                    subpixel = target - outVal;
                                    effDelta = outVal;

                                    if (ev.code == REL_X) trigRelX += outVal;
                                    else if (ev.code == REL_Y) trigRelY += outVal;
                                }

                                if (ev.code == REL_X) { aimX += effDelta; aimDirty = true; trigBatchDirty = true; }
                                else if (ev.code == REL_Y) { aimY += effDelta; aimDirty = true; trigBatchDirty = true; }

                                const double maxAimDist = 4000.0;
                                double curDist = std::hypot(aimX, aimY);
                                if (curDist > maxAimDist) {
                                    aimX = static_cast<int>(std::round(aimX * maxAimDist / curDist));
                                    aimY = static_cast<int>(std::round(aimY * maxAimDist / curDist));
                                }
                                continue;
                            } else if (ev.code == REL_WHEEL || ev.code == REL_WHEEL_HI_RES) {
                                if (ev.code == REL_WHEEL) {
                                    emit wheelScrolled(ev.value);
                                    wheelHiResAccum = 0;
                                } else if (ev.code == REL_WHEEL_HI_RES) {
                                    wheelHiResAccum += ev.value;
                                    if (std::abs(wheelHiResAccum) >= 120) {
                                        int steps = wheelHiResAccum / 120;
                                        wheelHiResAccum -= steps * 120;
                                        emit wheelScrolled(steps);
                                    }
                                }
                                continue; // Wheel verschlucken
                            }
                        } else if (ev.type == EV_SYN && aimDirty) {
                            continue; // Am Ende des Read-Batches gebündelt emittieren
                        }
                    }

                    if (dev.grabbed && m_uinputFd >= 0) {
                        if (ev.type == EV_REL && (ev.code == REL_X || ev.code == REL_Y)) {
                            if (ev.code == REL_X) passthruRelX += ev.value;
                            else passthruRelY += ev.value;
                            hasPassthruRel = true;
                        } else if (ev.type == EV_MSC) {
                            // EV_MSC (MSC_SCAN) wird auf dem virtuellen uinput-Mausgerät nicht benötigt
                        } else if (ev.type == EV_SYN && ev.code == SYN_REPORT) {
                            if (nonRelPassthruSinceSyn) {
                                flushPassthruRel();
                                emitSyn(m_uinputFd);
                                nonRelPassthruSinceSyn = false;
                            }
                        } else {
                            flushPassthruRel();
                            if (ev.type == EV_KEY && ev.code < uinputBtnDown.size()) {
                                uinputBtnDown[ev.code] = (ev.value != 0);
                            }
                            [[maybe_unused]] auto w = write(m_uinputFd, &ev, sizeof(ev));
                            nonRelPassthruSinceSyn = true;
                        }
                    }
                }

                flushPassthruRel();
                flushTriggeredRel();
            }

        // 4. Physische Tastatur-Events verarbeiten
        for (size_t i = 0; i < m_keyboards.size() && (kbdOffset + i) < pfds.size(); ++i) {
            if (!(pfds[kbdOffset + i].revents & POLLIN)) continue;

            auto &kbd = m_keyboards[i];
            struct input_event events[64];
            ssize_t bytes = read(kbd.fd, events, sizeof(events));
            if (bytes <= 0) continue;

            size_t count = bytes / sizeof(struct input_event);
            for (size_t k = 0; k < count; ++k) {
                const auto &ev = events[k];
                if (ev.type == EV_KEY) {
                    if (ev.code < KEY_CNT) {
                        activeKeys[ev.code] = (ev.value != 0);
                    }

                    if (m_triggerType.load() == 1 && !m_triggerKeyCodes.empty()) {
                        int mainKey = m_triggerKeyCodes.back();
                        bool modifiersMatch = true;
                        for (size_t m = 0; m + 1 < m_triggerKeyCodes.size(); ++m) {
                            int mod = m_triggerKeyCodes[m];
                            if (mod == KEY_LEFTCTRL || mod == KEY_RIGHTCTRL) {
                                if (!activeKeys[KEY_LEFTCTRL] && !activeKeys[KEY_RIGHTCTRL]) modifiersMatch = false;
                            } else if (mod == KEY_LEFTALT || mod == KEY_RIGHTALT) {
                                if (!activeKeys[KEY_LEFTALT] && !activeKeys[KEY_RIGHTALT]) modifiersMatch = false;
                            } else if (mod == KEY_LEFTSHIFT || mod == KEY_RIGHTSHIFT) {
                                if (!activeKeys[KEY_LEFTSHIFT] && !activeKeys[KEY_RIGHTSHIFT]) modifiersMatch = false;
                            } else if (mod == KEY_LEFTMETA || mod == KEY_RIGHTMETA) {
                                if (!activeKeys[KEY_LEFTMETA] && !activeKeys[KEY_RIGHTMETA]) modifiersMatch = false;
                            } else {
                                if (mod < KEY_CNT && !activeKeys[mod]) modifiersMatch = false;
                            }
                        }

                        if (ev.code == mainKey) {
                            if (ev.value == 1 && modifiersMatch) { // Key pressed
                                pressTime = std::chrono::steady_clock::now();
                                deltaX = 0; deltaY = 0; dragDistance = 0.0f;
                                trailActive = false; gestureFired = false; pendingGestureDir = -1;
                                state = State::Pending;
                                emit rightPressed();
                                emit triggerPressed();
                            } else if (ev.value == 0) { // Key released
                                if (trailActive) {
                                    trailActive = false;
                                    emit gestureFinished();
                                }
                                if (state == State::Triggered) {
                                    state = State::Idle;
                                    subpixelX = 0.0; subpixelY = 0.0;
                                    emit holdReleased();
                                    releaseAllUInputButtons();
                                } else if (state == State::Pending || state == State::Cancelled || state == State::Gesture) {
                                    state = State::Idle;
                                    releaseAllUInputButtons();
                                }
                            }
                        }
                    }

                    if (ev.code == KEY_ESC && ev.value == 1) {
                        if (state == State::Triggered || state == State::Pending) {
                            state = State::Cancelled;
                            if (trailActive) {
                                trailActive = false;
                                emit gestureFinished();
                            }
                            emit holdCancelled();
                            releaseAllUInputButtons();
                        }
                    }
                }
            }
        }
    }
}

void InputMonitor::onPrepareForSleep(bool sleeping)
{
    if (!sleeping) {
        std::cout << "[InputMonitor] System resumed from sleep - requesting rescan\n";
        requestRescan();
        emit resumedFromSleep();
    }
}

void InputMonitor::syncAim(int offsetX, int offsetY)
{
    m_syncAimX.store(offsetX, std::memory_order_relaxed);
    m_syncAimY.store(offsetY, std::memory_order_relaxed);
    m_syncAimRequested.store(true, std::memory_order_release);
}
