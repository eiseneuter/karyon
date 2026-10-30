#include "RecentFilesManager.h"
#include "DebugLogger.h"
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

namespace {

bool isExcludedApplication(const QString &appName, const QString &appExec)
{
    static const QStringList excludedKeywords = {
        QStringLiteral("portal"),
        QStringLiteral("kdialog"),
        QStringLiteral("browser"),
        QStringLiteral("firefox"),
        QStringLiteral("chrome"),
        QStringLiteral("chromium"),
        QStringLiteral("brave"),
        QStringLiteral("opera"),
        QStringLiteral("vivaldi"),
        QStringLiteral("edge"),
        QStringLiteral("librewolf"),
        QStringLiteral("waterfox"),
        QStringLiteral("zen"),
        QStringLiteral("epiphany"),
        QStringLiteral("konqueror"),
        QStringLiteral("tor"),
        QStringLiteral("systemsettings"),
        QStringLiteral("kfilebox"),
        QStringLiteral("gtkfilechooser")
    };

    QString lowerName = appName.toLower();
    QString lowerExec = appExec.toLower();

    if (lowerName.isEmpty() && lowerExec.isEmpty()) {
        return true;
    }

    for (const QString &kw : excludedKeywords) {
        if (lowerName.contains(kw) || lowerExec.contains(kw)) {
            return true;
        }
    }
    return false;
}

} // namespace

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
    KLOG("FileMgr", "loadRecentFiles(%d) returned %d files", maxCount, result.size());
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
            QString bookmarkMod = xml.attributes().value(QStringLiteral("modified")).toString();
            if (bookmarkMod.isEmpty()) {
                bookmarkMod = xml.attributes().value(QStringLiteral("visited")).toString();
            }

            bool hasValidApp = false;
            QDateTime latestValidAppTime;

            // Lese innere Elemente bis zum schließenden </bookmark>
            while (!xml.atEnd() && !(xml.isEndElement() && xml.name() == QLatin1String("bookmark"))) {
                xml.readNext();
                if (xml.isStartElement() && xml.name() == QLatin1String("application")) {
                    QString appName = xml.attributes().value(QStringLiteral("name")).toString();
                    QString appExec = xml.attributes().value(QStringLiteral("exec")).toString();
                    QString appMod = xml.attributes().value(QStringLiteral("modified")).toString();

                    if (!isExcludedApplication(appName, appExec)) {
                        hasValidApp = true;
                        QDateTime dt = QDateTime::fromString(appMod, Qt::ISODateWithMs);
                        if (!dt.isValid()) dt = QDateTime::fromString(appMod, Qt::ISODate);
                        if (dt.isValid() && (!latestValidAppTime.isValid() || dt > latestValidAppTime)) {
                            latestValidAppTime = dt;
                        }
                    }
                }
            }

            // Dateien überspringen, die ausschließlich durch Upload-Dialoge / Portale / Browser angefasst wurden
            if (!hasValidApp) {
                continue;
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

            QDateTime dt = latestValidAppTime;
            if (!dt.isValid()) {
                dt = QDateTime::fromString(bookmarkMod, Qt::ISODateWithMs);
                if (!dt.isValid()) {
                    dt = QDateTime::fromString(bookmarkMod, Qt::ISODate);
                }
                if (!dt.isValid()) {
                    dt = fi.lastModified();
                }
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
