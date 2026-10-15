#include "RecentFilesManager.h"
#include "ProcessUtils.h"

#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QUrl>
#include <QXmlStreamReader>
#include <QStandardPaths>
#include <QProcess>
#include <QHash>
#include <algorithm>

RecentFilesManager::RecentFilesManager()
{
}

QList<RecentFileItem> RecentFilesManager::loadRecentFiles(int maxCount)
{
    QList<RecentFileItem> all;
    all.append(loadFromXbel());
    all.append(loadFromRecentDocs());

    // Deduplicate by path (keep the one with latest mtime)
    QHash<QString, RecentFileItem> uniqueMap;
    for (const auto &item : all) {
        if (!uniqueMap.contains(item.path)) {
            uniqueMap.insert(item.path, item);
        } else if (item.mtime > uniqueMap.value(item.path).mtime) {
            uniqueMap.insert(item.path, item);
        }
    }

    QList<RecentFileItem> result = uniqueMap.values();
    std::sort(result.begin(), result.end(), [](const RecentFileItem &a, const RecentFileItem &b) {
        return a.mtime > b.mtime;
    });

    if (result.size() > maxCount) {
        result = result.mid(0, maxCount);
    }
    return result;
}

QList<RecentFileItem> RecentFilesManager::loadFromXbel()
{
    QList<RecentFileItem> items;
    QStringList candidatePaths = {
        QDir::homePath() + QStringLiteral("/.local/share/recently-used.xbel"),
        QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/recently-used.xbel")
    };

    QString xbelPath;
    for (const auto &p : candidatePaths) {
        if (QFile::exists(p)) {
            xbelPath = p;
            break;
        }
    }

    if (xbelPath.isEmpty()) return items;

    QFile file(xbelPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return items;

    QXmlStreamReader xml(&file);
    while (!xml.atEnd() && !xml.hasError()) {
        xml.readNext();
        if (xml.isStartElement() && xml.name() == QLatin1String("bookmark")) {
            QString href = xml.attributes().value(QStringLiteral("href")).toString();
            QString modified = xml.attributes().value(QStringLiteral("modified")).toString();
            if (modified.isEmpty()) {
                modified = xml.attributes().value(QStringLiteral("visited")).toString();
            }

            QString filePath;
            if (href.startsWith(QLatin1String("file://"))) {
                filePath = QUrl(href).toLocalFile();
            } else if (href.startsWith(QLatin1Char('/'))) {
                filePath = href;
            }

            if (filePath.isEmpty() || !QFile::exists(filePath)) continue;
            QFileInfo fi(filePath);
            if (fi.isDir()) continue;

            QDateTime dt = QDateTime::fromString(modified, Qt::ISODateWithMs);
            if (!dt.isValid()) {
                dt = QDateTime::fromString(modified, Qt::ISODate);
            }
            if (!dt.isValid()) {
                dt = fi.lastModified();
            }

            RecentFileItem item;
            item.name = fi.fileName();
            item.path = filePath;
            item.mtime = dt;
            items.append(item);
        }
    }
    return items;
}

QList<RecentFileItem> RecentFilesManager::loadFromRecentDocs()
{
    QList<RecentFileItem> items;
    QString recentDocsDir = QDir::homePath() + QStringLiteral("/.local/share/RecentDocuments");
    QDir dir(recentDocsDir);
    if (!dir.exists()) return items;

    QStringList desktopFiles = dir.entryList({QStringLiteral("*.desktop")}, QDir::Files);
    for (const QString &df : desktopFiles) {
        QFile f(dir.absoluteFilePath(df));
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) continue;

        QString name;
        QString url;
        QString icon;

        while (!f.atEnd()) {
            QString line = QString::fromUtf8(f.readLine()).trimmed();
            if (line.startsWith(QLatin1String("Name="))) {
                name = line.mid(5);
            } else if (line.startsWith(QLatin1String("URL="))) {
                url = line.mid(4);
            } else if (line.startsWith(QLatin1String("Icon="))) {
                icon = line.mid(5);
            }
        }

        QString filePath;
        if (url.startsWith(QLatin1String("file://"))) {
            filePath = QUrl(url).toLocalFile();
        } else if (url.startsWith(QLatin1Char('/'))) {
            filePath = url;
        }

        if (filePath.isEmpty() || !QFile::exists(filePath)) continue;
        QFileInfo fi(filePath);
        if (fi.isDir()) continue;

        RecentFileItem item;
        item.name = name.isEmpty() ? fi.fileName() : name;
        item.path = filePath;
        item.iconName = icon;
        item.mtime = fi.lastModified();
        items.append(item);
    }
    return items;
}

bool RecentFilesManager::openFile(const QString &filePath)
{
    if (filePath.isEmpty()) return false;
    return ProcessUtils::startDetachedHost(QStringLiteral("xdg-open"), {filePath});
}
