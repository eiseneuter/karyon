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
        if (lowerName.find("touchpad") != std::string::npos ||
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

    return true;
}

bool hasPressedKeysOrButtons(int fd)
{
    unsigned long keyBits[(KEY_CNT + sizeof(long) * 8 - 1) / (sizeof(long) * 8)] = {0};
    if (ioctl(fd, EVIOCGKEY(sizeof(keyBits)), keyBits) < 0) {
        return false;
    }
    for (size_t i = 0; i < sizeof(keyBits) / sizeof(keyBits[0]); ++i) {
        if (keyBits[i] != 0) {
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
    m_overlayMouseSpeed.store(speed);
    std::cout << "[InputMonitor] Overlay mouse speed updated: " << speed << "\n";
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

    if (m_devices.empty() && changed) {
        m_lastReportedMouse.clear();
        emit mouseDisconnected();
    }

    // 2. /dev/input nach neu eingesteckten Mäusen durchsuchen
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

    if (m_uinputFd >= 0) {
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
        if (m_inotifyFd >= 0) {
            pfds.push_back({m_inotifyFd, POLLIN, 0});
        }
    };
    rebuildPfds();

    int pendingHotplugRetries = 0;
    auto lastHotplugCheck = std::chrono::steady_clock::now();
    auto lastIdleScan = std::chrono::steady_clock::now();

    while (m_running.load()) {
        if (m_rescanRequested.exchange(false)) {
            if (rescanDevices()) {
                rebuildPfds();
            }
        }

        tryGrabPendingDevices();

        int pollTimeout = (state == State::Pending) ? 5 : 25;
        int ret = poll(pfds.data(), pfds.size(), pollTimeout);

        auto now = std::chrono::steady_clock::now();

        if (state == State::Pending) {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - pressTime).count();
            if (dragDistance < 14.0f && elapsed >= m_holdTriggerMs.load()) {
                if (trailActive) {
                    trailActive = false;
                    emit gestureFinished();
                }
                state = State::Triggered;
                subpixelX = 0.0;
                subpixelY = 0.0;
                aimX = deltaX;
                aimY = -20 + deltaY;
                emit holdTriggered(deltaX, deltaY);
                emit cursorMoved(aimX, aimY);
            } else if (dragDistance >= 14.0f && elapsed > GESTURE_TIME_WINDOW_MS) {
                if (trailActive) {
                    trailActive = false;
                    emit gestureFinished();
                }
                state = State::Dragging;
                pendingGestureDir = -1;
                for (const auto &dev : m_devices) {
                    if (dev.grabbed && m_uinputFd >= 0) {
                        emitEvent(m_uinputFd, EV_KEY, BTN_RIGHT, 1);
                        emitSyn(m_uinputFd);
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
                std::cout << "[InputMonitor] Poll hangup on " << m_devices[i].path << "\n";
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

        if (needsRebuild) {
            rebuildPfds();
            continue;
        }

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

            // Solange das Gerät noch auf den sicheren EVIOCGRAB wartet (während der ersten 250ms nach
            // uinput-Start oder solange noch eine Maustaste vom Startklick gehalten wird), laufen alle
            // Klicks direkt über das physische Gerät an KWin/libinput. Wir verwerfen die gelesenen
            // Events hier nur aus dem Puffer und prüfen sofort, ob die Taste inzwischen losgelassen wurde.
            if (!dev.grabbed && m_uinputFd >= 0) {
                tryGrabPendingDevices();
                continue;
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

                // Linksklick während gedrückter rechter Maustaste -> Schließt Overlay sofort!
                if (ev.type == EV_KEY && ev.code == BTN_LEFT) {
                    m_leftDown.store(ev.value == 1);
                    if (state == State::Triggered) {
                        if (ev.value == 1) { // Left Pressed
                            state = State::Cancelled;
                            pendingGestureDir = -1;
                            if (trailActive) {
                                trailActive = false;
                                emit gestureFinished();
                            }
                            emit holdCancelled();
                            continue; // Klick verschlucken
                        }
                    } else if (state == State::Cancelled || state == State::Gesture) {
                        if (ev.value == 1) {
                            state = State::Cancelled;
                            pendingGestureDir = -1;
                            if (trailActive) {
                                trailActive = false;
                                emit gestureFinished();
                            }
                            emit holdCancelled();
                        }
                        continue; // Release/Click verschlucken
                    } else if (state == State::Pending) {
                        if (ev.value == 1) {
                            state = State::Idle;
                            pendingGestureDir = -1;
                            if (trailActive) {
                                trailActive = false;
                                emit gestureFinished();
                            }
                            // Replay beider Klicks als normaler Doppel-/Akkordklick
                            if (dev.grabbed && m_uinputFd >= 0) {
                                emitEvent(m_uinputFd, EV_KEY, BTN_RIGHT, 1);
                                emitSyn(m_uinputFd);
                                emitEvent(m_uinputFd, EV_KEY, BTN_LEFT, 1);
                                emitSyn(m_uinputFd);
                            }
                            continue;
                        }
                    }
                }

                // Mittelklick während gedrückter rechter Maustaste -> Fenstermanipulation
                if (ev.type == EV_KEY && ev.code == BTN_MIDDLE) {
                    if (state == State::Triggered) {
                        if (ev.value == 1) { // Middle Pressed
                            emit middleClicked();
                        }
                        continue; // Klick verschlucken
                    } else if (state == State::Cancelled || state == State::Gesture) {
                        continue;
                    }
                }

                if (ev.type == EV_KEY && ev.code == BTN_RIGHT) {
                    if (m_gameModeEnabled.load() && m_gameModeActive.load()) {
                        state = State::Idle;
                        if (dev.grabbed && m_uinputFd >= 0) {
                            emitEvent(m_uinputFd, EV_KEY, BTN_RIGHT, ev.value);
                            emitSyn(m_uinputFd);
                        }
                        continue;
                    }

                    if (ev.value == 1) { // Press
                        pressTime = std::chrono::steady_clock::now();
                        deltaX = 0;
                        deltaY = 0;
                        dragDistance = 0.0f;
                        trailActive = false;
                        gestureFired = false;
                        pendingGestureDir = -1;
                        state = State::Pending;
                        emit rightPressed(); // Sofortiger KWin-Snapshot-Trigger
                    } else if (ev.value == 0) { // Release
                        if (trailActive) {
                            trailActive = false;
                            emit gestureFinished();
                        }

                        if (state == State::Gesture) {
                            state = State::Idle;
                            pendingGestureDir = -1;
                            // Geste wurde bereits beim Schwellenwert direkt ausgelöst. Release verschlucken.
                            continue;
                        } else if (state == State::Cancelled) {
                            state = State::Idle;
                            pendingGestureDir = -1;
                            // Release verschlucken
                            continue;
                        } else if (state == State::Pending) {
                            state = State::Idle;
                            pendingGestureDir = -1;
                            if (dragDistance < 14.0f) {
                                if (dev.grabbed && m_uinputFd >= 0) {
                                    emitEvent(m_uinputFd, EV_KEY, BTN_RIGHT, 1);
                                    emitSyn(m_uinputFd);
                                    emitEvent(m_uinputFd, EV_KEY, BTN_RIGHT, 0);
                                    emitSyn(m_uinputFd);
                                }
                            }
                        } else if (state == State::Triggered) {
                            state = State::Idle;
                            pendingGestureDir = -1;
                            subpixelX = 0.0;
                            subpixelY = 0.0;
                            emit holdReleased();
                        } else if (state == State::Dragging) {
                            state = State::Idle;
                            pendingGestureDir = -1;
                            if (dev.grabbed && m_uinputFd >= 0) {
                                emitEvent(m_uinputFd, EV_KEY, BTN_RIGHT, 0);
                                emitSyn(m_uinputFd);
                            }
                        }
                    }
                } else {
                    if (state == State::Gesture && ev.type == EV_REL) {
                        // Geste ist erkannt: Trail folgt dem dynamischen Wisch-Schwung bis zum RMT-Release
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

                        if (!trailActive && !gestureFired && dragDistance >= 6.0f) {
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

                            if (dispSq >= GESTURE_MIN_DISPLACEMENT * GESTURE_MIN_DISPLACEMENT && elapsed <= GESTURE_TIME_WINDOW_MS) {
                                gestureFired = true;
                                state = State::Gesture;
                                int dir = GestureManager::determineDirection(deltaX, deltaY);
                                pendingGestureDir = -1;
                                emit gestureDetected(dir, deltaX, deltaY);
                                if (trailActive) {
                                    trailActive = false;
                                    emit gestureFinished();
                                }
                            } else if (dragDistance > 14.0f && elapsed > GESTURE_TIME_WINDOW_MS) {
                                if (trailActive) {
                                    trailActive = false;
                                    emit gestureFinished();
                                }
                                state = State::Dragging;
                                pendingGestureDir = -1;
                                flushPassthruRel();
                                if (dev.grabbed && m_uinputFd >= 0) {
                                    emitEvent(m_uinputFd, EV_KEY, BTN_RIGHT, 1);
                                    emitSyn(m_uinputFd);
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
                                continue;
                            } else if (ev.code == REL_WHEEL || ev.code == REL_WHEEL_HI_RES) {
                                if (ev.code == REL_WHEEL) {
                                    emit wheelScrolled(ev.value);
                                }
                                continue; // Wheel verschlucken
                            }
                        } else if (ev.type == EV_SYN && aimDirty) {
                            continue; // Am Ende des Read-Batches gebündelt emittieren
                        }
                    }

                    if (dev.grabbed && m_uinputFd >= 0 && state != State::Cancelled) {
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
                            [[maybe_unused]] auto w = write(m_uinputFd, &ev, sizeof(ev));
                            nonRelPassthruSinceSyn = true;
                        }
                    }
                }
            }

            flushPassthruRel();
            flushTriggeredRel();
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
