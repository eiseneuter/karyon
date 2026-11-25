#include "PinnedManager.h"
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QProcess>
#include <QSet>
#include <QRegularExpression>

PinnedManager::PinnedManager()
{
    load();
}

QString PinnedManager::configFilePath() const
{
    QString dir = QDir::homePath() + QStringLiteral("/.config/karyon");
    QDir().mkpath(dir);
    return dir + QStringLiteral("/pins.ini");
}

void PinnedManager::load()
{
    m_items.clear();
    QSettings settings(configFilePath(), QSettings::IniFormat);
    int size = settings.beginReadArray(QStringLiteral("PinnedItems"));
    for (int i = 0; i < size; ++i) {
        settings.setArrayIndex(i);
        PinnedItem item;
        int t = settings.value(QStringLiteral("type"), 0).toInt();
        item.type = (t == 1) ? PinnedItemType::File : PinnedItemType::App;
        item.id = settings.value(QStringLiteral("id")).toString();
        item.name = settings.value(QStringLiteral("name")).toString();
        item.execOrPath = settings.value(QStringLiteral("execOrPath")).toString();
        item.icon = settings.value(QStringLiteral("icon")).toString();
        item.desktopFile = settings.value(QStringLiteral("desktopFile")).toString();
        if (item.type == PinnedItemType::App &&
            (item.id == QLatin1String("plasmawindowed") || item.execOrPath.trimmed() == QLatin1String("plasmawindowed"))) {
            QString n = item.name.toLower();
            QString pid;
            if (n.contains(QStringLiteral("blue"))) {
                pid = QStringLiteral("org.kde.plasma.bluetooth");
                item.icon = QStringLiteral("preferences-system-bluetooth");
            } else if (n.contains(QStringLiteral("lautst")) || n.contains(QStringLiteral("volume")) || n.contains(QStringLiteral("audio"))) {
                pid = QStringLiteral("org.kde.plasma.volume");
                item.icon = QStringLiteral("audio-volume-high");
            } else if (n.contains(QStringLiteral("netz")) || n.contains(QStringLiteral("network")) || n.contains(QStringLiteral("wlan")) || n.contains(QStringLiteral("wifi"))) {
                pid = QStringLiteral("org.kde.plasma.networkmanagement");
                item.icon = QStringLiteral("network-wireless");
            } else if (n.contains(QStringLiteral("akku")) || n.contains(QStringLiteral("batt")) || n.contains(QStringLiteral("energie")) || n.contains(QStringLiteral("energy"))) {
                pid = QStringLiteral("org.kde.plasma.battery");
                item.icon = QStringLiteral("battery-charging");
            } else if (n.contains(QStringLiteral("hellig")) || n.contains(QStringLiteral("bright"))) {
                pid = QStringLiteral("org.kde.plasma.brightness");
                item.icon = QStringLiteral("display-brightness");
            } else if (n.contains(QStringLiteral("druck")) || n.contains(QStringLiteral("print"))) {
                pid = QStringLiteral("org.kde.plasma.printmanager");
                item.icon = QStringLiteral("printer");
            } else if (n.contains(QStringLiteral("wetter")) || n.contains(QStringLiteral("weather"))) {
                pid = QStringLiteral("org.kde.plasma.weather");
                item.icon = QStringLiteral("weather-few-clouds");
            } else if (n.contains(QStringLiteral("gerät")) || n.contains(QStringLiteral("device"))) {
                pid = QStringLiteral("org.kde.plasma.devicenotifier");
                item.icon = QStringLiteral("drive-removable-media");
            } else if (n.contains(QStringLiteral("kamera")) || n.contains(QStringLiteral("camera"))) {
                pid = QStringLiteral("org.kde.plasma.cameraindicator");
                item.icon = QStringLiteral("camera-web");
            } else if (n.contains(QStringLiteral("anzeige")) || n.contains(QStringLiteral("bildschirm")) || n.contains(QStringLiteral("display")) || n.contains(QStringLiteral("screen"))) {
                pid = QStringLiteral("org.kde.kscreen");
                item.icon = QStringLiteral("preferences-desktop-display");
            }
            if (!pid.isEmpty()) {
                item.id = QStringLiteral("plasmawindowed:") + pid;
                item.execOrPath = QStringLiteral("plasmawindowed ") + pid;
                item.desktopFile = QStringLiteral("plasmawindowed:") + pid;
            }
        }
        if (!item.id.isEmpty() || !item.execOrPath.isEmpty()) {
            m_items.append(item);
        }
    }
    settings.endArray();
}

void PinnedManager::save()
{
    QSettings settings(configFilePath(), QSettings::IniFormat);
    settings.remove(QStringLiteral("PinnedItems"));
    settings.beginWriteArray(QStringLiteral("PinnedItems"), m_items.size());
    for (int i = 0; i < m_items.size(); ++i) {
        settings.setArrayIndex(i);
        const auto &item = m_items[i];
        settings.setValue(QStringLiteral("type"), (item.type == PinnedItemType::File) ? 1 : 0);
        settings.setValue(QStringLiteral("id"), item.id);
        settings.setValue(QStringLiteral("name"), item.name);
        settings.setValue(QStringLiteral("execOrPath"), item.execOrPath);
        settings.setValue(QStringLiteral("icon"), item.icon);
        settings.setValue(QStringLiteral("desktopFile"), item.desktopFile);
    }
    settings.endArray();
    settings.sync();
}

bool PinnedManager::itemMatchesApp(const PinnedItem &item, const QString &desktopEntryName, const QString &resourceClass)
{
    if (item.type != PinnedItemType::App) return false;

    QString idLow = desktopEntryName.toLower().trimmed();
    QString rcLow = resourceClass.toLower().trimmed();
    if (idLow.startsWith(QLatin1String("app:"))) idLow = idLow.mid(4);
    if (idLow.startsWith(QLatin1String("window:"))) idLow = idLow.mid(7);
    if (rcLow.startsWith(QLatin1String("app:"))) rcLow = rcLow.mid(4);

    // Spezialfall: plasmawindowed Tray-Flyouts strikt über die Plasmoid-ID isolieren,
    // damit verschiedene Flyouts (Bluetooth, Volume, Network etc.) sich niemals gegenseitig matchen.
    auto extractPlasmoidId = [](const QString &str) -> QString {
        if (str.isEmpty()) return {};
        QString s = str.trimmed().toLower();
        if (s.startsWith(QLatin1String("plasmawindowed:"))) {
            return s.mid(15).trimmed();
        }
        if (s.startsWith(QLatin1String("app:plasmawindowed:"))) {
            return s.mid(19).trimmed();
        }
        if (s.contains(QLatin1String("plasmawindowed"))) {
            int idx = s.indexOf(QLatin1String("plasmawindowed"));
            QString rest = s.mid(idx + 14).trimmed();
            while (rest.startsWith(QLatin1Char(':')) || rest.startsWith(QLatin1Char(' '))) {
                rest = rest.mid(1).trimmed();
            }
            QStringList tokens = rest.split(QLatin1Char(' '), Qt::SkipEmptyParts);
            for (const QString &tok : tokens) {
                if (!tok.startsWith(QLatin1Char('-'))) {
                    return tok.trimmed();
                }
            }
        }
        if (s.startsWith(QLatin1String("org.kde.plasma.")) || s.startsWith(QLatin1String("org.kde.kscreen"))) {
            return s;
        }
        return {};
    };

    QString itemPlasmoid = extractPlasmoidId(item.id);
    if (itemPlasmoid.isEmpty()) itemPlasmoid = extractPlasmoidId(item.execOrPath);
    if (itemPlasmoid.isEmpty()) itemPlasmoid = extractPlasmoidId(item.desktopFile);

    bool itemIsPw = !itemPlasmoid.isEmpty() ||
                    item.id.contains(QLatin1String("plasmawindowed"), Qt::CaseInsensitive) ||
                    item.execOrPath.contains(QLatin1String("plasmawindowed"), Qt::CaseInsensitive) ||
                    item.desktopFile.contains(QLatin1String("plasmawindowed"), Qt::CaseInsensitive);

    bool targetIsPw = idLow.contains(QLatin1String("plasmawindowed")) ||
                      rcLow.contains(QLatin1String("plasmawindowed"));

    if (itemIsPw || targetIsPw) {
        if (!itemIsPw || !targetIsPw) {
            return false;
        }
        QString targetPlasmoid = extractPlasmoidId(desktopEntryName);
        if (targetPlasmoid.isEmpty()) targetPlasmoid = extractPlasmoidId(resourceClass);

        if (!itemPlasmoid.isEmpty() && !targetPlasmoid.isEmpty()) {
            return itemPlasmoid == targetPlasmoid;
        }
        return false;
    }

    // Vergleich über den "Stamm" einer Kennung: Pfad und .desktop weg, Reverse-DNS auf den
    // letzten Bestandteil gekürzt (org.kde.dolphin -> dolphin). Programm-Endungen bleiben
    // erhalten – sonst wären "a.exe" und "b.exe" beide nur noch "exe" und damit gleich.
    auto stemOf = [](QString v) {
        if (v.endsWith(QLatin1String(".desktop"))) v.chop(8);
        int slash = v.lastIndexOf(QLatin1Char('/'));
        if (slash >= 0) v = v.mid(slash + 1);
        static const QRegularExpression rdnsRe(QStringLiteral("^[a-z]{2,6}(\\.[a-z0-9_-]+){2,}$"));
        static const QRegularExpression binSuffixRe(QStringLiteral("\\.(exe|x86_64|x86|bin|appimage|sh|py|run)$"));
        if (rdnsRe.match(v).hasMatch() && !binSuffixRe.match(v).hasMatch()) {
            v = v.section(QLatin1Char('.'), -1);
        }
        return v;
    };
    auto matchStem = [&stemOf](const QString &a, const QString &b) {
        if (a.isEmpty() || b.isEmpty()) return false;
        if (a == b) return true;
        const QString aStem = stemOf(a);
        const QString bStem = stemOf(b);
        if (aStem.isEmpty() || bStem.isEmpty()) return false;
        return (aStem == bStem || aStem == b || a == bStem);
    };

    QString itemId = item.id.toLower().trimmed();
    if (itemId.startsWith(QLatin1String("app:"))) itemId = itemId.mid(4);
    if (itemId.startsWith(QLatin1String("window:"))) itemId = itemId.mid(7);
    QString itemDf = item.desktopFile.toLower().trimmed();
    QString itemName = item.name.toLower().trimmed();
    QString itemExec = item.execOrPath.toLower().trimmed();

    // Executable name extraction (e.g. "thunderbird %u" -> "thunderbird", "/usr/bin/dolphin" -> "dolphin")
    // Erstes echtes Programm der Exec-Zeile; "env", VAR=wert und allgemeine Starter
    // (wine, python, flatpak, steam steam://…) identifizieren keine App.
    QString execStem;
    {
        const QStringList toks = QProcess::splitCommand(itemExec);
        for (const QString &tok : toks) {
            if (tok == QLatin1String("env") || tok.endsWith(QLatin1String("/env"))) continue;
            if (tok.startsWith(QLatin1Char('-'))) continue;
            if (tok.contains(QLatin1Char('=')) && !tok.startsWith(QLatin1Char('/'))) continue;
            execStem = tok.section(QLatin1Char('/'), -1);
            break;
        }
        static const QSet<QString> genericLaunchers = {
            QStringLiteral("wine"), QStringLiteral("wine64"), QStringLiteral("python"), QStringLiteral("python3"),
            QStringLiteral("bash"), QStringLiteral("sh"), QStringLiteral("flatpak"), QStringLiteral("java"),
            QStringLiteral("electron"), QStringLiteral("gtk-launch"), QStringLiteral("xdg-open"),
            QStringLiteral("snap"), QStringLiteral("umu-run"), QStringLiteral("plasmawindowed")
        };
        if (genericLaunchers.contains(execStem) ||
            (execStem == QLatin1String("steam") && itemExec.contains(QLatin1String("steam://")))) {
            execStem.clear();
        }
    }

    if (matchStem(itemId, idLow) || matchStem(itemId, rcLow) ||
        matchStem(itemDf, idLow) || matchStem(itemDf, rcLow) ||
        (!execStem.isEmpty() && (matchStem(execStem, idLow) || matchStem(execStem, rcLow))) ||
        // Kein Teilwort-Vergleich: "steam" darf nicht "steam_app_123" treffen, "code" nicht "codeblocks".
        (!idLow.isEmpty() && itemName == idLow) ||
        (!rcLow.isEmpty() && itemName == rcLow) ||
        (!itemExec.isEmpty() && (matchStem(itemExec, idLow) || matchStem(itemExec, rcLow)))) {
        return true;
    }
    return false;
}

bool PinnedManager::isAppPinned(const QString &desktopEntryName, const QString &resourceClass) const
{
    for (const auto &item : m_items) {
        if (itemMatchesApp(item, desktopEntryName, resourceClass)) {
            return true;
        }
    }
    return false;
}

bool PinnedManager::isFilePinned(const QString &filePath) const
{
    const QString p = filePath.trimmed();
    for (const auto &item : m_items) {
        if (item.type == PinnedItemType::File && item.execOrPath.trimmed() == p) {
            return true;
        }
    }
    return false;
}

void PinnedManager::pinApp(const QString &id, const QString &name, const QString &exec, const QString &icon, const QString &desktopFile)
{
    // Ein Eintrag ohne Kennung würde auf JEDE App passen
    if (id.trimmed().isEmpty() || isAppPinned(id)) return;
    PinnedItem item;
    item.type = PinnedItemType::App;
    item.id = id;
    item.name = name;
    item.execOrPath = exec;
    item.icon = icon;
    item.desktopFile = desktopFile;
    m_items.append(item);
    save();
}

void PinnedManager::pinFile(const QString &filePath, const QString &name, const QString &icon)
{
    if (isFilePinned(filePath)) return;
    PinnedItem item;
    item.type = PinnedItemType::File;
    item.id = filePath;
    item.name = name;
    item.execOrPath = filePath;
    item.icon = icon;
    m_items.append(item);
    save();
}

void PinnedManager::unpinApp(const QString &id, const QString &resourceClass)
{
    for (int i = m_items.size() - 1; i >= 0; --i) {
        if (itemMatchesApp(m_items[i], id, resourceClass)) {
            m_items.removeAt(i);
        }
    }
    save();
}

void PinnedManager::unpinFile(const QString &filePath)
{
    const QString p = filePath.trimmed();
    for (int i = m_items.size() - 1; i >= 0; --i) {
        if (m_items[i].type == PinnedItemType::File && m_items[i].execOrPath.trimmed() == p) {
            m_items.removeAt(i);
        }
    }
    save();
}

void PinnedManager::toggleAppPin(const QString &id, const QString &resourceClass, const QString &name, const QString &exec, const QString &icon, const QString &desktopFile)
{
    if (isAppPinned(id, resourceClass)) {
        unpinApp(id, resourceClass);
    } else {
        pinApp(id.isEmpty() ? resourceClass : id, name, exec, icon, desktopFile);
    }
}

void PinnedManager::toggleFilePin(const QString &filePath, const QString &name, const QString &icon)
{
    if (isFilePinned(filePath)) {
        unpinFile(filePath);
    } else {
        pinFile(filePath, name, icon);
    }
}
