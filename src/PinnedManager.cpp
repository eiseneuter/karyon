#include "PinnedManager.h"
#include <QDir>
#include <QFileInfo>
#include <QSettings>

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

    auto matchStem = [](const QString &a, const QString &b) {
        if (a.isEmpty() || b.isEmpty()) return false;
        if (a == b) return true;
        QString aStem = a;
        if (aStem.endsWith(QLatin1String(".desktop"))) aStem.chop(8);
        int slashA = aStem.lastIndexOf(QLatin1Char('/'));
        if (slashA >= 0) aStem = aStem.mid(slashA + 1);
        int dotA = aStem.lastIndexOf(QLatin1Char('.'));
        if (dotA >= 0) aStem = aStem.mid(dotA + 1);

        QString bStem = b;
        if (bStem.endsWith(QLatin1String(".desktop"))) bStem.chop(8);
        int slashB = bStem.lastIndexOf(QLatin1Char('/'));
        if (slashB >= 0) bStem = bStem.mid(slashB + 1);
        int dotB = bStem.lastIndexOf(QLatin1Char('.'));
        if (dotB >= 0) bStem = bStem.mid(dotB + 1);

        return (aStem == bStem || aStem == b || a == bStem);
    };

    QString itemId = item.id.toLower().trimmed();
    if (itemId.startsWith(QLatin1String("app:"))) itemId = itemId.mid(4);
    if (itemId.startsWith(QLatin1String("window:"))) itemId = itemId.mid(7);
    QString itemDf = item.desktopFile.toLower().trimmed();
    QString itemName = item.name.toLower().trimmed();
    QString itemExec = item.execOrPath.toLower().trimmed();

    // Executable name extraction (e.g. "thunderbird %u" -> "thunderbird", "/usr/bin/dolphin" -> "dolphin")
    QString execStem = itemExec;
    int firstSpace = execStem.indexOf(QLatin1Char(' '));
    if (firstSpace > 0) execStem = execStem.left(firstSpace);
    int lastSlash = execStem.lastIndexOf(QLatin1Char('/'));
    if (lastSlash >= 0) execStem = execStem.mid(lastSlash + 1);
    execStem = execStem.remove(QLatin1Char('"')).remove(QLatin1Char('\''));

    if (matchStem(itemId, idLow) || matchStem(itemId, rcLow) ||
        matchStem(itemDf, idLow) || matchStem(itemDf, rcLow) ||
        (!execStem.isEmpty() && (matchStem(execStem, idLow) || matchStem(execStem, rcLow))) ||
        (!idLow.isEmpty() && (itemName == idLow || (!itemName.isEmpty() && (itemId.contains(idLow) || idLow.contains(itemId))))) ||
        (!rcLow.isEmpty() && (itemName == rcLow || (!rcLow.isEmpty() && (itemId.contains(rcLow) || rcLow.contains(itemId))))) ||
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
    QString pLow = filePath.toLower().trimmed();
    for (const auto &item : m_items) {
        if (item.type == PinnedItemType::File && item.execOrPath.toLower().trimmed() == pLow) {
            return true;
        }
    }
    return false;
}

void PinnedManager::pinApp(const QString &id, const QString &name, const QString &exec, const QString &icon, const QString &desktopFile)
{
    if (isAppPinned(id)) return;
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
    QString pLow = filePath.toLower().trimmed();
    for (int i = m_items.size() - 1; i >= 0; --i) {
        if (m_items[i].type == PinnedItemType::File && m_items[i].execOrPath.toLower().trimmed() == pLow) {
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
