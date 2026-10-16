#include "TrayManager.h"
#include "ProcessUtils.h"
#include <QProcess>
#include <QCoreApplication>
#include <QFile>
#include <QSaveFile>
#include <QDir>
#include <QTextStream>
#include <QRegularExpression>
#include <QPair>
#include <QHash>
#include <QSettings>
#include <iostream>

namespace {
std::function<void(int)> s_karyonActionCb;
std::function<void()> s_karyonSettingsCb;
}

void TrayManager::setKaryonActionCallback(std::function<void(int)> cb)
{
    s_karyonActionCb = std::move(cb);
}

void TrayManager::setKaryonSettingsCallback(std::function<void()> cb)
{
    s_karyonSettingsCb = std::move(cb);
}

TrayManager::TrayManager(QObject *parent)
    : QObject(parent)
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (bus.isConnected()) {
        bus.connect(QString(), QString(), QStringLiteral("org.kde.StatusNotifierItem"),
                    QStringLiteral("NewIcon"), this, SLOT(onDbusTrayChanged()));
        bus.connect(QString(), QString(), QStringLiteral("org.kde.StatusNotifierItem"),
                    QStringLiteral("NewAttentionIcon"), this, SLOT(onDbusTrayChanged()));
        bus.connect(QString(), QString(), QStringLiteral("org.kde.StatusNotifierItem"),
                    QStringLiteral("NewOverlayIcon"), this, SLOT(onDbusTrayChanged()));
        bus.connect(QString(), QString(), QStringLiteral("org.kde.StatusNotifierItem"),
                    QStringLiteral("NewStatus"), this, SLOT(onDbusTrayChangedWithArg(QString)));
        bus.connect(QString(), QString(), QStringLiteral("org.kde.StatusNotifierItem"),
                    QStringLiteral("NewTitle"), this, SLOT(onDbusTrayChanged()));
        bus.connect(QStringLiteral("org.kde.StatusNotifierWatcher"),
                    QStringLiteral("/StatusNotifierWatcher"),
                    QStringLiteral("org.kde.StatusNotifierWatcher"),
                    QStringLiteral("StatusNotifierItemRegistered"),
                    this, SLOT(onDbusTrayChangedWithArg(QString)));
        bus.connect(QStringLiteral("org.kde.StatusNotifierWatcher"),
                    QStringLiteral("/StatusNotifierWatcher"),
                    QStringLiteral("org.kde.StatusNotifierWatcher"),
                    QStringLiteral("StatusNotifierItemUnregistered"),
                    this, SLOT(onDbusTrayChangedWithArg(QString)));
    }
}

void TrayManager::onDbusTrayChanged()
{
    emit trayIconsChanged();
}

void TrayManager::onDbusTrayChangedWithArg(const QString &)
{
    emit trayIconsChanged();
}

QList<TrayProgramItem> TrayManager::runningPrograms()
{
    QList<TrayProgramItem> items;
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) return items;

    QDBusInterface watcher(QStringLiteral("org.kde.StatusNotifierWatcher"),
                           QStringLiteral("/StatusNotifierWatcher"),
                           QStringLiteral("org.kde.StatusNotifierWatcher"),
                           bus);
    watcher.setTimeout(250);

    QStringList itemStrings;
    QDBusReply<QStringList> reply = watcher.call(QStringLiteral("RegisteredStatusNotifierItems"));
    if (reply.isValid()) {
        itemStrings = reply.value();
    } else {
        // Fallback: Property Get
        QDBusInterface props(QStringLiteral("org.kde.StatusNotifierWatcher"),
                              QStringLiteral("/StatusNotifierWatcher"),
                              QStringLiteral("org.freedesktop.DBus.Properties"),
                              bus);
        props.setTimeout(250);
        QDBusReply<QDBusVariant> pReply = props.call(QStringLiteral("Get"),
                                                     QStringLiteral("org.kde.StatusNotifierWatcher"),
                                                     QStringLiteral("RegisteredStatusNotifierItems"));
        if (pReply.isValid()) {
            itemStrings = pReply.value().variant().toStringList();
        }
    }

    if (itemStrings.isEmpty()) return items;
    for (const QString &itemStr : itemStrings) {
        if (itemStr.isEmpty()) continue;

        int slashIdx = itemStr.indexOf(QLatin1Char('/'));
        QString service = (slashIdx > 0) ? itemStr.left(slashIdx) : itemStr;
        QString path = (slashIdx > 0) ? itemStr.mid(slashIdx) : QStringLiteral("/StatusNotifierItem");
        if (!path.startsWith(QLatin1Char('/'))) {
            path.prepend(QLatin1Char('/'));
        }

        uint servicePid = 0;
        QDBusInterface dbusIface(QStringLiteral("org.freedesktop.DBus"),
                                 QStringLiteral("/org/freedesktop/DBus"),
                                 QStringLiteral("org.freedesktop.DBus"),
                                 bus);
        QDBusReply<uint> pidReply = dbusIface.call(QStringLiteral("GetConnectionUnixProcessID"), service);
        if (pidReply.isValid()) {
            servicePid = pidReply.value();
        }

        // Eigenes Karyon-Tray-Item sofort ohne blockierenden D-Bus-Self-Call übernehmen
        if ((servicePid > 0 && servicePid == static_cast<uint>(QCoreApplication::applicationPid())) ||
            service == bus.baseService() ||
            service == QLatin1String("org.kde.karyon") ||
            service.contains(QStringLiteral("karyon"), Qt::CaseInsensitive)) {
            TrayProgramItem prog;
            prog.service = QStringLiteral("org.kde.karyon");
            prog.path = path;
            prog.id = QStringLiteral("karyon");
            prog.title = QStringLiteral("Karyon");
            prog.iconName = QStringLiteral("karyon");
            prog.menuPath = QStringLiteral("/MenuBar");
            prog.icon = ProcessUtils::karyonIcon();
            prog.pid = servicePid;
            items.append(prog);
            continue;
        }

        QDBusInterface propsIface(service, path, QStringLiteral("org.freedesktop.DBus.Properties"), bus);
        propsIface.setTimeout(200);
        QDBusReply<QVariantMap> propReply = propsIface.call(QStringLiteral("GetAll"), QStringLiteral("org.kde.StatusNotifierItem"));
        if (!propReply.isValid()) {
            continue; // Tot / ungültig
        }

        const QVariantMap props = propReply.value();
        QString id = props.value(QStringLiteral("Id")).toString();
        if (id.isEmpty()) {
            continue; // Ungültiges / nicht existentes SNI-Objekt
        }

        QString title = props.value(QStringLiteral("Title")).toString();
        if (title.isEmpty()) {
            QString clean = id;
            int idx = clean.indexOf(QLatin1String("_status_icon"));
            if (idx > 0) clean = clean.left(idx);
            if (!clean.isEmpty()) {
                clean[0] = clean[0].toUpper();
                title = clean;
            } else {
                title = id;
            }
        }

        QString status = props.value(QStringLiteral("Status")).toString();
        bool isAttention = (status == QLatin1String("NeedsAttention"));
        QString iconName = props.value(QStringLiteral("IconName")).toString();
        if (isAttention) {
            QString attName = props.value(QStringLiteral("AttentionIconName")).toString();
            if (!attName.isEmpty()) {
                iconName = attName;
            }
        }

        QString iconThemePath = props.value(QStringLiteral("IconThemePath")).toString();
        if (!iconThemePath.isEmpty()) {
            if (!QIcon::themeSearchPaths().contains(iconThemePath)) {
                QIcon::setThemeSearchPaths(QIcon::themeSearchPaths() << iconThemePath);
            }
        }

        QString menuPath;
        QVariant menuVar = props.value(QStringLiteral("Menu"));
        if (menuVar.canConvert<QDBusObjectPath>()) {
            menuPath = menuVar.value<QDBusObjectPath>().path();
        } else {
            menuPath = menuVar.toString();
        }

        auto parsePixArg = [](const QVariant &pixVar) -> QImage {
            if (!pixVar.canConvert<QDBusArgument>()) return QImage();
            try {
                const QDBusArgument pixArg = pixVar.value<QDBusArgument>();
                pixArg.beginArray();
                QImage bestImg;
                while (!pixArg.atEnd()) {
                    pixArg.beginStructure();
                    int w, h;
                    QByteArray bytes;
                    pixArg >> w >> h >> bytes;
                    pixArg.endStructure();
                    if (w > 0 && h > 0 && bytes.size() == w * h * 4) {
                        QImage img(w, h, QImage::Format_ARGB32);
                        const quint32 *src = reinterpret_cast<const quint32 *>(bytes.constData());
                        quint32 *dst = reinterpret_cast<quint32 *>(img.bits());
                        for (int i = 0; i < w * h; ++i) {
                            dst[i] = qFromBigEndian(src[i]);
                        }
                        if (img.width() >= bestImg.width()) {
                            bestImg = img;
                        }
                    }
                }
                pixArg.endArray();
                return bestImg;
            } catch (...) {
                return QImage();
            }
        };

        QImage primaryPixImg;
        QIcon icon;

        // 1. AttentionIconPixmap (wenn Status == NeedsAttention)
        if (isAttention && props.contains(QStringLiteral("AttentionIconPixmap"))) {
            primaryPixImg = parsePixArg(props.value(QStringLiteral("AttentionIconPixmap")));
            if (!primaryPixImg.isNull()) {
                icon = QIcon(QPixmap::fromImage(primaryPixImg));
            }
        }

        // 2. IconPixmap (live vom Programm gerendert, z.B. Telegram Unread-Badge)
        if (icon.isNull() && props.contains(QStringLiteral("IconPixmap"))) {
            primaryPixImg = parsePixArg(props.value(QStringLiteral("IconPixmap")));
            if (!primaryPixImg.isNull()) {
                icon = QIcon(QPixmap::fromImage(primaryPixImg));
            }
        }

        // 3. Direct file check in IconThemePath (z.B. Steam)
        if (icon.isNull() && !iconThemePath.isEmpty() && !iconName.isEmpty()) {
            QStringList candidates = {
                iconThemePath + QLatin1Char('/') + iconName + QStringLiteral(".png"),
                iconThemePath + QLatin1Char('/') + iconName + QStringLiteral(".svg"),
                iconThemePath + QLatin1Char('/') + iconName,
                iconThemePath + QStringLiteral("/32x32/") + iconName + QStringLiteral(".png"),
                iconThemePath + QStringLiteral("/48x48/") + iconName + QStringLiteral(".png"),
                iconThemePath + QStringLiteral("/scalable/") + iconName + QStringLiteral(".svg")
            };
            for (const auto &cand : candidates) {
                if (QFile::exists(cand)) {
                    icon = QIcon(cand);
                    if (!icon.isNull() && !icon.availableSizes().isEmpty()) break;
                }
            }
        }

        // 4. IconName in system theme
        if (icon.isNull() && !iconName.isEmpty()) {
            icon = QIcon::fromTheme(iconName);
        }

        // 5. IconName in pixmaps or local icons
        if (icon.isNull() && !iconName.isEmpty()) {
            QStringList pixDirs = {
                QStringLiteral("/usr/share/pixmaps"),
                QDir::homePath() + QStringLiteral("/.local/share/pixmaps"),
                QStringLiteral("/usr/share/icons/hicolor/scalable/apps"),
                QStringLiteral("/usr/share/icons/hicolor/48x48/apps"),
                QStringLiteral("/usr/share/icons/hicolor/32x32/apps")
            };
            for (const auto &dir : pixDirs) {
                for (const auto &ext : {QStringLiteral(".png"), QStringLiteral(".svg"), QStringLiteral(".xpm"), QStringLiteral("")}) {
                    QString p = dir + QLatin1Char('/') + iconName + ext;
                    if (QFile::exists(p)) {
                        icon = QIcon(p);
                        if (!icon.isNull() && !icon.availableSizes().isEmpty()) break;
                    }
                }
                if (!icon.isNull() && !icon.availableSizes().isEmpty()) break;
            }
        }

        // 6. By ID / Desktop-File lookup (z.B. steam, cachy-update, telegram)
        if (icon.isNull()) {
            QString cleanId = id;
            int idx = cleanId.indexOf(QLatin1String("_status_icon"));
            if (idx > 0) cleanId = cleanId.left(idx);
            idx = cleanId.indexOf(QLatin1Char('.'));
            QString baseId = (idx > 0) ? cleanId.left(idx) : cleanId;

            for (const auto &tryKey : {cleanId, cleanId.toLower(), baseId, baseId.toLower()}) {
                icon = QIcon::fromTheme(tryKey);
                if (!icon.isNull() && !icon.availableSizes().isEmpty()) break;

                // Check .desktop file in standard locations
                for (const auto &appDir : {QDir::homePath() + QStringLiteral("/.local/share/applications"), QStringLiteral("/usr/share/applications")}) {
                    QString dfPath = appDir + QLatin1Char('/') + tryKey + QStringLiteral(".desktop");
                    if (QFile::exists(dfPath)) {
                        QSettings df(dfPath, QSettings::IniFormat);
                        df.beginGroup(QStringLiteral("Desktop Entry"));
                        QString dfIcon = df.value(QStringLiteral("Icon")).toString();
                        if (!dfIcon.isEmpty()) {
                            if (QFile::exists(dfIcon)) {
                                icon = QIcon(dfIcon);
                            } else {
                                icon = QIcon::fromTheme(dfIcon);
                            }
                            if (!icon.isNull() && !icon.availableSizes().isEmpty()) break;
                        }
                    }
                }
                if (!icon.isNull() && !icon.availableSizes().isEmpty()) break;
            }
        }
        if (icon.isNull()) {
            icon = QIcon::fromTheme(QStringLiteral("application-x-executable"));
        }

        // Falls derselbe Prozess mehrere identische SNI-Items registriert (z.B. xdg-desktop-portal-kde),
        // Duplikate zusammenfassen
        if (servicePid > 0) {
            bool isDuplicate = false;
            for (const auto &existing : items) {
                if (existing.pid == servicePid &&
                    existing.id.compare(id, Qt::CaseInsensitive) == 0 &&
                    existing.title.compare(title, Qt::CaseInsensitive) == 0) {
                    isDuplicate = true;
                    break;
                }
            }
            if (isDuplicate) {
                continue;
            }
        }

        TrayProgramItem prog;
        prog.service = (id.compare(QLatin1String("karyon"), Qt::CaseInsensitive) == 0)
            ? QStringLiteral("org.kde.karyon")
            : service;
        prog.path = path;
        prog.id = (id.compare(QLatin1String("karyon"), Qt::CaseInsensitive) == 0)
            ? QStringLiteral("karyon")
            : id;
        prog.title = title;
        prog.iconName = iconName;
        prog.menuPath = (prog.id == QLatin1String("karyon") && menuPath.isEmpty())
            ? QStringLiteral("/MenuBar")
            : menuPath;
        prog.icon = (prog.id == QLatin1String("karyon"))
            ? ProcessUtils::karyonIcon()
            : icon;
        prog.pid = servicePid;

        items.append(prog);
    }

    bool hasKaryon = false;
    for (const auto &it : items) {
        if (it.id.compare(QLatin1String("karyon"), Qt::CaseInsensitive) == 0) {
            hasKaryon = true;
            break;
        }
    }
    if (!hasKaryon) {
        TrayProgramItem prog;
        prog.service = QStringLiteral("org.kde.karyon");
        prog.path = QStringLiteral("/StatusNotifierItem");
        prog.id = QStringLiteral("karyon");
        prog.title = QStringLiteral("Karyon");
        prog.iconName = QStringLiteral("karyon");
        prog.menuPath = QStringLiteral("/MenuBar");
        prog.icon = ProcessUtils::karyonIcon();
        prog.pid = static_cast<uint>(QCoreApplication::applicationPid());
        items.append(prog);
    }

    return items;
}

QList<TrayMenuAction> TrayManager::fetchMenuActions(const QString &service, const QString &menuPath)
{
    QList<TrayMenuAction> actions;
    if (service.isEmpty() || menuPath.isEmpty()) return actions;

    QDBusConnection bus = QDBusConnection::sessionBus();
    if (service == bus.baseService() || service == QLatin1String("org.kde.karyon") || service.contains(QStringLiteral("karyon"))) {
        TrayMenuAction a1;
        a1.id = 1;
        a1.label = QStringLiteral("Info");
        a1.enabled = true;
        a1.visible = true;
        a1.service = service;
        a1.menuPath = menuPath;

        TrayMenuAction a2;
        a2.id = 2;
        a2.label = QStringLiteral("Settings");
        a2.enabled = true;
        a2.visible = true;
        a2.service = service;
        a2.menuPath = menuPath;

        TrayMenuAction a3;
        a3.id = 3;
        a3.label = QStringLiteral("Quit");
        a3.enabled = true;
        a3.visible = true;
        a3.service = service;
        a3.menuPath = menuPath;

        return {a1, a2, a3};
    }

    QDBusInterface iface(service, menuPath, QStringLiteral("com.canonical.dbusmenu"), bus);
    iface.setTimeout(250);

    const QStringList requestedProps{
        QStringLiteral("label"),
        QStringLiteral("enabled"),
        QStringLiteral("visible"),
        QStringLiteral("type")
    };
    QDBusMessage reply = iface.call(QStringLiteral("GetLayout"), 0, 1, requestedProps);
    if (reply.type() == QDBusMessage::ErrorMessage || reply.arguments().size() < 2) {
        return actions;
    }

    try {
        const QDBusArgument arg = reply.arguments().at(1).value<QDBusArgument>();
        arg.beginStructure();
        int rootId;
        arg >> rootId;
        QVariantMap rootProps;
        arg >> rootProps;

        arg.beginArray();
        while (!arg.atEnd()) {
            QDBusVariant var;
            arg >> var;
            const QDBusArgument childArg = var.variant().value<QDBusArgument>();
            childArg.beginStructure();
            int childId;
            childArg >> childId;
            QVariantMap childProps;
            childArg >> childProps;

            childArg.beginArray();
            while (!childArg.atEnd()) {
                QDBusVariant subVar;
                childArg >> subVar;
            }
            childArg.endArray();
            childArg.endStructure();

            bool visible = childProps.value(QStringLiteral("visible"), true).toBool();
            bool enabled = childProps.value(QStringLiteral("enabled"), true).toBool();
            QString type = childProps.value(QStringLiteral("type")).toString();
            QString label = childProps.value(QStringLiteral("label")).toString();

            if (!visible || type == QLatin1String("separator")) {
                continue;
            }

            label.remove(QLatin1Char('&'));
            label = label.trimmed();
            if (label.isEmpty()) continue;

            TrayMenuAction act;
            act.id = childId;
            act.label = label;
            act.enabled = enabled;
            act.visible = visible;
            act.service = service;
            act.menuPath = menuPath;

            actions.append(act);
        }
        arg.endArray();
        arg.endStructure();
    } catch (...) {
        // Robuster Schutz gegen Parsing-Ausnahmen
    }

    return actions;
}

QList<SystemTrayApplet> TrayManager::primaryApplets()
{
    return {
        {QStringLiteral("network"), QStringLiteral("Network"), QStringLiteral("org.kde.plasma.networkmanagement"), QStringLiteral("network"), true},
        {QStringLiteral("bluetooth"), QStringLiteral("Bluetooth"), QStringLiteral("org.kde.plasma.bluetooth"), QStringLiteral("bluetooth"), true},
        {QStringLiteral("volume"), QStringLiteral("Volume"), QStringLiteral("org.kde.plasma.volume"), QStringLiteral("volume"), true},
        {QStringLiteral("clipboard"), QStringLiteral("Clipboard"), QStringLiteral("org.kde.plasma.clipboard"), QStringLiteral("clipboard"), true},
        {QStringLiteral("energy"), QStringLiteral("Battery"), QStringLiteral("org.kde.plasma.battery"), QStringLiteral("energy"), true},
        {QStringLiteral("brightness"), QStringLiteral("Brightness"), QStringLiteral("org.kde.plasma.brightness"), QStringLiteral("brightness"), true}
    };
}

QList<SystemTrayApplet> TrayManager::secondaryApplets()
{
    return {
        {QStringLiteral("display"), QStringLiteral("Display"), QStringLiteral("org.kde.kscreen"), QStringLiteral("display"), false},
        {QStringLiteral("devices"), QStringLiteral("Devices"), QStringLiteral("org.kde.plasma.devicenotifier"), QStringLiteral("devices"), false},
        {QStringLiteral("printer"), QStringLiteral("Printers"), QStringLiteral("org.kde.plasma.printmanager"), QStringLiteral("printer"), false},
        {QStringLiteral("camera"), QStringLiteral("Camera"), QStringLiteral("org.kde.plasma.cameraindicator"), QStringLiteral("camera"), false},
        {QStringLiteral("weather"), QStringLiteral("Weather"), QStringLiteral("org.kde.plasma.weather"), QStringLiteral("weather"), false}
    };
}

void TrayManager::activateItem(const QString &service, const QString &path, int x, int y)
{
    if (service.isEmpty() || path.isEmpty()) return;
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (service == bus.baseService() || service == QLatin1String("org.kde.karyon") || service.contains(QStringLiteral("karyon"))) {
        if (s_karyonSettingsCb) {
            s_karyonSettingsCb();
        }
        return;
    }
    QDBusInterface iface(service, path, QStringLiteral("org.kde.StatusNotifierItem"), bus);
    iface.call(QDBus::NoBlock, QStringLiteral("Activate"), x, y);
}

void TrayManager::triggerMenuAction(const QString &service, const QString &menuPath, int actionId)
{
    if (service.isEmpty() || actionId < 0) return;
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (service == bus.baseService() || service == QLatin1String("org.kde.karyon") || service.contains(QStringLiteral("karyon"))) {
        if (s_karyonActionCb) {
            s_karyonActionCb(actionId);
        }
        return;
    }
    if (menuPath.isEmpty()) return;
    QDBusInterface iface(service, menuPath, QStringLiteral("com.canonical.dbusmenu"), bus);
    iface.call(QDBus::NoBlock, QStringLiteral("AboutToShow"), actionId);
    iface.call(QDBus::NoBlock, QStringLiteral("Event"), actionId, QStringLiteral("clicked"),
               QVariant::fromValue(QDBusVariant(0)), 0u);
}

void TrayManager::syncPlasmoidConfig(const QString &pluginId)
{
    if (pluginId.isEmpty()) return;

    QString home = QDir::homePath();
    QString desktopConfigPath = home + QStringLiteral("/.config/plasma-org.kde.plasma.desktop-appletsrc");
    QString windowedConfigPath = home + QStringLiteral("/.config/plasmawindowedrc");

    QFile desktopFile(desktopConfigPath);
    if (!desktopFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return;
    }

    struct Section {
        QString name;
        QList<QPair<QString, QString>> entries;
    };

    QList<Section> desktopSections;
    {
        QTextStream in(&desktopFile);
        Section currentSection;
        while (!in.atEnd()) {
            QString line = in.readLine().trimmed();
            if (line.isEmpty()) continue;
            if (line.startsWith(QLatin1Char('[')) && line.endsWith(QLatin1Char(']'))) {
                if (!currentSection.name.isEmpty()) {
                    desktopSections.append(currentSection);
                    currentSection.entries.clear();
                }
                currentSection.name = line;
            } else if (line.contains(QLatin1Char('='))) {
                int eq = line.indexOf(QLatin1Char('='));
                QString k = line.left(eq).trimmed();
                QString v = line.mid(eq + 1).trimmed();
                currentSection.entries.append({k, v});
            }
        }
        if (!currentSection.name.isEmpty()) {
            desktopSections.append(currentSection);
        }
        desktopFile.close();
    }

    QString matchedPrefix;
    for (const auto &sec : desktopSections) {
        for (const auto &entry : sec.entries) {
            if (entry.first == QLatin1String("plugin") && entry.second == pluginId) {
                matchedPrefix = sec.name;
                break;
            }
        }
        if (!matchedPrefix.isEmpty()) break;
    }

    if (matchedPrefix.isEmpty()) return;

    QList<Section> subSectionsToCopy;
    for (const auto &sec : desktopSections) {
        if (sec.name.startsWith(matchedPrefix) && sec.name.length() > matchedPrefix.length()) {
            if (sec.name.contains(QLatin1String("[ConfigDialog]"))) continue;
            Section s;
            s.name = sec.name.mid(matchedPrefix.length());
            s.entries = sec.entries;
            subSectionsToCopy.append(s);
        }
    }

    if (subSectionsToCopy.isEmpty()) return;

    QFile windowedFile(windowedConfigPath);
    QList<Section> windowedSections;
    int maxAppletNum = 0;
    QString targetAppletPrefix;

    if (windowedFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&windowedFile);
        Section currentSection;
        while (!in.atEnd()) {
            QString line = in.readLine().trimmed();
            if (line.isEmpty()) continue;
            if (line.startsWith(QLatin1Char('[')) && line.endsWith(QLatin1Char(']'))) {
                if (!currentSection.name.isEmpty()) {
                    windowedSections.append(currentSection);
                    currentSection.entries.clear();
                }
                currentSection.name = line;

                static const QRegularExpression appletRe(QStringLiteral("^\\[Applets\\]\\[(\\d+)\\]$"));
                auto match = appletRe.match(line);
                if (match.hasMatch()) {
                    int num = match.captured(1).toInt();
                    if (num > maxAppletNum) maxAppletNum = num;
                }
            } else if (line.contains(QLatin1Char('='))) {
                int eq = line.indexOf(QLatin1Char('='));
                QString k = line.left(eq).trimmed();
                QString v = line.mid(eq + 1).trimmed();
                currentSection.entries.append({k, v});
            }
        }
        if (!currentSection.name.isEmpty()) {
            windowedSections.append(currentSection);
        }
        windowedFile.close();
    }

    for (const auto &sec : windowedSections) {
        for (const auto &entry : sec.entries) {
            if (entry.first == QLatin1String("plugin") && entry.second == pluginId) {
                targetAppletPrefix = sec.name;
                break;
            }
        }
        if (!targetAppletPrefix.isEmpty()) break;
    }

    if (targetAppletPrefix.isEmpty()) {
        int newNum = maxAppletNum + 1;
        targetAppletPrefix = QStringLiteral("[Applets][%1]").arg(newNum);

        Section appSec;
        appSec.name = targetAppletPrefix;
        appSec.entries.append({QStringLiteral("immutability"), QStringLiteral("1")});
        appSec.entries.append({QStringLiteral("plugin"), pluginId});
        windowedSections.append(appSec);

        Section cfgSec;
        cfgSec.name = targetAppletPrefix + QStringLiteral("[Configuration]");
        cfgSec.entries.append({QStringLiteral("geometry"), QStringLiteral("0,0,500,500")});
        windowedSections.append(cfgSec);
    }

    for (const auto &subSec : subSectionsToCopy) {
        QString destSecName = targetAppletPrefix + subSec.name;
        bool found = false;
        for (auto &wSec : windowedSections) {
            if (wSec.name == destSecName) {
                wSec.entries = subSec.entries;
                found = true;
                break;
            }
        }
        if (!found) {
            Section newSec;
            newSec.name = destSecName;
            newSec.entries = subSec.entries;
            windowedSections.append(newSec);
        }
    }

    QSaveFile sf(windowedConfigPath);
    if (sf.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&sf);
        for (int i = 0; i < windowedSections.size(); ++i) {
            const auto &sec = windowedSections.at(i);
            out << sec.name << "\n";
            for (const auto &e : sec.entries) {
                out << e.first << "=" << e.second << "\n";
            }
            if (i < windowedSections.size() - 1) {
                out << "\n";
            }
        }
        sf.commit();
    }
}

void TrayManager::launchPlasmoid(const QString &pluginId)
{
    if (pluginId.isEmpty()) return;

    if (pluginId == QLatin1String("org.kde.plasma.clipboard")) {
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

    // Synchronize settings (e.g. weather location, etc.) from plasma-org.kde.plasma.desktop-appletsrc
    syncPlasmoidConfig(pluginId);

    // Weder kcmshell6 noch extra Buttons/Popups verwenden:
    // Alle übrigen Plasmoide (inkl. org.kde.kscreen und org.kde.plasma.printmanager) starten
    // via plasmawindowed in bereinigter Umgebung (ohne QT_QPA_PLATFORM), sodass sie nativ unter
    // Wayland ohne XWayland-Lags/Schwarzflächen und in Originalgröße öffnen.
    ProcessUtils::startDetachedHost(QStringLiteral("plasmawindowed"), {pluginId});
}
