#pragma once

#include <QString>
#include <QList>
#include <QDateTime>

struct RecentFileItem {
    QString name;
    QString path;
    QString iconName;
    QDateTime mtime;
};

class RecentFilesManager {
public:
    RecentFilesManager();

    QList<RecentFileItem> loadRecentFiles(int maxCount = 15);
    static bool openFile(const QString &filePath);

private:
    QList<RecentFileItem> loadFromXbel();
    QList<RecentFileItem> loadFromRecentDocs();
};
