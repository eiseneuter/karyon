#pragma once

#include <QString>
#include <QList>

enum class PinnedItemType {
    App,
    File
};

struct PinnedItem {
    PinnedItemType type = PinnedItemType::App;
    QString id;          // desktopEntryName for App, filePath for File
    QString name;        // Display name
    QString execOrPath;  // Exec for App, filePath for File
    QString icon;        // Icon name or path
    QString desktopFile; // Path to desktop file (for App)
};

class PinnedManager {
public:
    PinnedManager();

    QList<PinnedItem> pinnedItems() const { return m_items; }

    bool isAppPinned(const QString &desktopEntryName, const QString &resourceClass = QString()) const;
    bool isFilePinned(const QString &filePath) const;
    static bool itemMatchesApp(const PinnedItem &item, const QString &desktopEntryName, const QString &resourceClass = QString());

    void pinApp(const QString &id, const QString &name, const QString &exec, const QString &icon, const QString &desktopFile);
    void pinFile(const QString &filePath, const QString &name, const QString &icon);

    void unpinApp(const QString &id, const QString &resourceClass = QString());
    void unpinFile(const QString &filePath);

    void toggleAppPin(const QString &id, const QString &resourceClass, const QString &name, const QString &exec, const QString &icon, const QString &desktopFile);
    void toggleFilePin(const QString &filePath, const QString &name = QString(), const QString &icon = QString());

    void load();
    void save();

private:
    QList<PinnedItem> m_items;
    QString configFilePath() const;
};
