#include "AppManager.h"
#include "ProcessUtils.h"

#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QSet>
#include <QProcess>
#include <QRegularExpression>
#include <QDebug>
#include <QStandardPaths>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>
#include <QSettings>
#include <KServiceGroup>
#include <KService>
#include <sqlite3.h>

bool AppManager::isAppImage(const QString &path)
{
    if (path.isEmpty()) return false;
    return path.endsWith(QLatin1String(".AppImage"), Qt::CaseInsensitive) ||
           path.endsWith(QLatin1String(".appimage"), Qt::CaseInsensitive);
}

QString AppManager::findDesktopFilePath(const QString &name)
{
    if (name.isEmpty()) return {};
    if (name.endsWith(QLatin1String(".desktop")) && QFile::exists(name)) return name;

    QString clean = name;
    if (clean.endsWith(QLatin1String(".desktop"))) {
        clean.chop(8);
    }

    static const QStringList searchDirs = []() {
        QStringList dirs = QStandardPaths::standardLocations(QStandardPaths::ApplicationsLocation);
        dirs.prepend(QStringLiteral("/usr/share/applications"));
        dirs.append(QStringLiteral("/var/lib/flatpak/exports/share/applications"));
        dirs.append(QDir::homePath() + QStringLiteral("/.local/share/flatpak/exports/share/applications"));
        dirs.removeDuplicates();
        return dirs;
    }();

    for (const auto &dir : searchDirs) {
        QString path = dir + QLatin1Char('/') + clean + QStringLiteral(".desktop");
        if (QFile::exists(path)) return path;
        if (clean != name) {
            QString pathOrig = dir + QLatin1Char('/') + name;
            if (QFile::exists(pathOrig)) return pathOrig;
        }
    }
    return {};
}

namespace {

struct DesktopIndex {
    QHash<QString, QString> keyToPath;
    bool initialized = false;
};

static DesktopIndex s_desktopIndex;

void buildDesktopIndex(DesktopIndex &idx)
{
    if (idx.initialized) return;
    idx.initialized = true;

    QStringList searchDirs = QStandardPaths::standardLocations(QStandardPaths::ApplicationsLocation);
    searchDirs.prepend(QStringLiteral("/usr/share/applications"));
    searchDirs.append(QStringLiteral("/var/lib/flatpak/exports/share/applications"));
    searchDirs.append(QDir::homePath() + QStringLiteral("/.local/share/flatpak/exports/share/applications"));
    searchDirs.removeDuplicates();

    for (const auto &dirPath : searchDirs) {
        QDir dir(dirPath);
        if (!dir.exists()) continue;
        QFileInfoList list = dir.entryInfoList({QStringLiteral("*.desktop")}, QDir::Files, QDir::NoSort);
        for (const auto &fi : list) {
            QString path = fi.absoluteFilePath();
            QString fileName = fi.fileName();
            QString baseName = fi.completeBaseName();

            QString fnLow = fileName.toLower();
            QString bnLow = baseName.toLower();
            idx.keyToPath.insert(fnLow, path);
            idx.keyToPath.insert(bnLow, path);

            QString norm = bnLow;
            norm.remove(QLatin1Char('_')).remove(QLatin1Char('-')).remove(QLatin1Char(' '));
            idx.keyToPath.insert(norm, path);

            QSettings ini(path, QSettings::IniFormat);
            ini.beginGroup(QStringLiteral("Desktop Entry"));
            QString wmClass = ini.value(QStringLiteral("StartupWMClass")).toString().trimmed();
            if (!wmClass.isEmpty()) {
                idx.keyToPath.insert(wmClass.toLower(), path);
                QString wmNorm = wmClass.toLower().remove(QLatin1Char('_')).remove(QLatin1Char('-')).remove(QLatin1Char(' '));
                idx.keyToPath.insert(wmNorm, path);
            }
            QString nameVal = ini.value(QStringLiteral("Name")).toString().trimmed();
            if (!nameVal.isEmpty()) {
                idx.keyToPath.insert(nameVal.toLower(), path);
            }
            QString execVal = ini.value(QStringLiteral("Exec")).toString().trimmed();
            if (!execVal.isEmpty()) {
                static const QRegularExpression rungameRe(QStringLiteral("steam://rungameid/(\\d+)"), QRegularExpression::CaseInsensitiveOption);
                auto mRun = rungameRe.match(execVal);
                if (mRun.hasMatch()) {
                    QString sId = mRun.captured(1);
                    idx.keyToPath.insert(QStringLiteral("steam_app_") + sId, path);
                    idx.keyToPath.insert(QStringLiteral("steam_app_") + sId + QStringLiteral(".desktop"), path);
                    idx.keyToPath.insert(sId, path);
                }

                QString bin = execVal.section(QLatin1Char(' '), 0, 0);
                int slash = bin.lastIndexOf(QLatin1Char('/'));
                if (slash >= 0) bin = bin.mid(slash + 1);
                if (!bin.isEmpty()) {
                    idx.keyToPath.insert(bin.toLower(), path);
                    if (bin.endsWith(QLatin1String(".AppImage"), Qt::CaseInsensitive) ||
                        bin.endsWith(QLatin1String(".appimage"), Qt::CaseInsensitive)) {
                        QString binNoExt = bin.left(bin.length() - 9);
                        idx.keyToPath.insert(binNoExt.toLower(), path);
                    }
                }
            }
        }
    }
}

} // namespace

QString AppManager::findDesktopFilePathCaseInsensitive(const QString &name)
{
    if (name.isEmpty()) return {};
    if (name.endsWith(QLatin1String(".desktop"), Qt::CaseInsensitive) && QFile::exists(name)) return name;
    if (QFile::exists(name)) return name;

    QString direct = findDesktopFilePath(name);
    if (!direct.isEmpty()) return direct;

    if (!s_desktopIndex.initialized) {
        buildDesktopIndex(s_desktopIndex);
    }

    QString low = name.toLower();
    if (s_desktopIndex.keyToPath.contains(low)) {
        return s_desktopIndex.keyToPath.value(low);
    }

    if (low.endsWith(QLatin1String(".desktop"))) {
        QString stripped = low.left(low.length() - 8);
        if (s_desktopIndex.keyToPath.contains(stripped)) {
            return s_desktopIndex.keyToPath.value(stripped);
        }
    }

    QString norm = low;
    norm.remove(QLatin1String(".desktop"));
    norm.remove(QLatin1Char('_')).remove(QLatin1Char('-')).remove(QLatin1Char(' '));
    if (s_desktopIndex.keyToPath.contains(norm)) {
        return s_desktopIndex.keyToPath.value(norm);
    }

    return {};
}

static QString steamIdFromPid(int pid)
{
    if (pid <= 0) return {};
    QFile f(QStringLiteral("/proc/%1/environ").arg(pid));
    if (!f.open(QIODevice::ReadOnly)) return {};
    QByteArray data = f.readAll();
    const char *ptr = data.constData();
    int len = data.size();
    int i = 0;
    while (i < len) {
        const char *entry = ptr + i;
        int entryLen = qstrnlen(entry, len - i);
        if (entryLen > 11 && strncmp(entry, "SteamAppId=", 11) == 0) {
            return QString::fromUtf8(entry + 11, entryLen - 11).trimmed();
        }
        if (entryLen > 12 && strncmp(entry, "SteamGameId=", 12) == 0) {
            return QString::fromUtf8(entry + 12, entryLen - 12).trimmed();
        }
        i += entryLen + 1;
    }
    return {};
}

bool AppManager::isSteamApp(const QString &desktopFileName, const QString &resourceClass, QString *outSteamId, int pid)
{
    static const QRegularExpression steamRe(QStringLiteral("steam_app_(\\d+)"), QRegularExpression::CaseInsensitiveOption);
    auto matchDf = steamRe.match(desktopFileName);
    if (matchDf.hasMatch()) {
        if (outSteamId) *outSteamId = matchDf.captured(1);
        return true;
    }
    auto matchRc = steamRe.match(resourceClass);
    if (matchRc.hasMatch()) {
        if (outSteamId) *outSteamId = matchRc.captured(1);
        return true;
    }

    static const QRegularExpression rungameRe(QStringLiteral("steam://rungameid/(\\d+)"), QRegularExpression::CaseInsensitiveOption);
    auto matchRunDf = rungameRe.match(desktopFileName);
    if (matchRunDf.hasMatch()) {
        if (outSteamId) *outSteamId = matchRunDf.captured(1);
        return true;
    }
    auto matchRunRc = rungameRe.match(resourceClass);
    if (matchRunRc.hasMatch()) {
        if (outSteamId) *outSteamId = matchRunRc.captured(1);
        return true;
    }

    if (pid > 0) {
        QString pidSteamId = steamIdFromPid(pid);
        if (!pidSteamId.isEmpty() && pidSteamId != QLatin1String("0")) {
            if (outSteamId) *outSteamId = pidSteamId;
            return true;
        }
    }

    if (!desktopFileName.isEmpty()) {
        QString dfPath = findDesktopFilePathCaseInsensitive(desktopFileName);
        if (!dfPath.isEmpty() && QFile::exists(dfPath)) {
            QFile f(dfPath);
            if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
                while (!f.atEnd()) {
                    QString line = QString::fromUtf8(f.readLine());
                    if (line.startsWith(QLatin1String("Exec="), Qt::CaseInsensitive)) {
                        auto m = rungameRe.match(line);
                        if (m.hasMatch()) {
                            if (outSteamId) *outSteamId = m.captured(1);
                            return true;
                        }
                        break;
                    }
                }
            }
        }
    }

    return false;
}

AppEntry AppManager::resolveSteamGame(const QString &steamId, const QString &captionFallback)
{
    if (steamId.isEmpty() || steamId == QLatin1String("0")) return {};

    static QHash<QString, AppEntry> s_steamGameCache;
    if (s_steamGameCache.contains(steamId)) {
        AppEntry cached = s_steamGameCache.value(steamId);
        if (!captionFallback.isEmpty() && (cached.name.isEmpty() || cached.name.startsWith(QLatin1String("Steam App ")))) {
            QString cleanCap = captionFallback;
            cleanCap.remove(QStringLiteral(" — Steam"));
            cleanCap.remove(QStringLiteral(" - Steam"));
            if (!cleanCap.trimmed().isEmpty()) {
                cached.name = cleanCap.trimmed();
                s_steamGameCache.insert(steamId, cached);
            }
        }
        return cached;
    }

    QString foundName;
    QString foundIcon;

    // 1. Steam Library Paths durchsuchen
    QString home = QDir::homePath();
    QStringList libPaths = {
        home + QStringLiteral("/.local/share/Steam"),
        home + QStringLiteral("/.steam/steam"),
        home + QStringLiteral("/.steam/root")
    };

    // libraryfolders.vdf auslesen
    for (const QString &base : QStringList(libPaths)) {
        QString vdfPath = base + QStringLiteral("/steamapps/libraryfolders.vdf");
        if (QFile::exists(vdfPath)) {
            QFile f(vdfPath);
            if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
                static const QRegularExpression pathRe(QStringLiteral("\"path\"\\s+\"([^\"]+)\""));
                while (!f.atEnd()) {
                    QString line = QString::fromUtf8(f.readLine());
                    auto match = pathRe.match(line);
                    if (match.hasMatch()) {
                        QString p = match.captured(1);
                        if (QDir(p).exists() && !libPaths.contains(p)) {
                            libPaths.append(p);
                        }
                    }
                }
            }
        }
    }

    // appmanifest_<steamId>.acf auslesen für den offiziellen Spielnamen
    for (const QString &lib : libPaths) {
        QString acfPath = lib + QStringLiteral("/steamapps/appmanifest_") + steamId + QStringLiteral(".acf");
        if (QFile::exists(acfPath)) {
            QFile f(acfPath);
            if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
                static const QRegularExpression nameRe(QStringLiteral("\"name\"\\s+\"([^\"]+)\""));
                while (!f.atEnd()) {
                    QString line = QString::fromUtf8(f.readLine());
                    auto match = nameRe.match(line);
                    if (match.hasMatch()) {
                        foundName = match.captured(1).trimmed();
                        break;
                    }
                }
            }
        }
        if (!foundName.isEmpty()) break;
    }

    // Fallback: Desktop-Datei
    QString resolvedDf = findDesktopFilePathCaseInsensitive(QStringLiteral("steam_app_") + steamId);
    if (foundName.isEmpty()) {
        if (!resolvedDf.isEmpty() && QFile::exists(resolvedDf)) {
            QSettings ini(resolvedDf, QSettings::IniFormat);
            ini.beginGroup(QStringLiteral("Desktop Entry"));
            foundName = ini.value(QStringLiteral("Name")).toString().trimmed();
        }
    }

    // Fallback: Fenstertitel
    if (foundName.isEmpty() && !captionFallback.trimmed().isEmpty()) {
        QString cleanCap = captionFallback.trimmed();
        cleanCap.remove(QStringLiteral(" — Steam"));
        cleanCap.remove(QStringLiteral(" - Steam"));
        foundName = cleanCap.trimmed();
    }

    if (foundName.isEmpty()) {
        foundName = QStringLiteral("Steam App ") + steamId;
    }

    // 2. Icon auflösen
    const QStringList iconGlobs = {
        home + QStringLiteral("/.local/share/icons/hicolor/256x256/apps/steam_icon_") + steamId + QStringLiteral(".png"),
        home + QStringLiteral("/.local/share/icons/hicolor/128x128/apps/steam_icon_") + steamId + QStringLiteral(".png"),
        home + QStringLiteral("/.local/share/icons/hicolor/64x64/apps/steam_icon_") + steamId + QStringLiteral(".png"),
        home + QStringLiteral("/.local/share/icons/hicolor/48x48/apps/steam_icon_") + steamId + QStringLiteral(".png"),
        home + QStringLiteral("/.local/share/icons/hicolor/32x32/apps/steam_icon_") + steamId + QStringLiteral(".png")
    };
    for (const QString &ip : iconGlobs) {
        if (QFile::exists(ip)) {
            foundIcon = ip;
            break;
        }
    }

    if (foundIcon.isEmpty()) {
        if (!resolvedDf.isEmpty() && QFile::exists(resolvedDf)) {
            QSettings ini(resolvedDf, QSettings::IniFormat);
            ini.beginGroup(QStringLiteral("Desktop Entry"));
            foundIcon = ini.value(QStringLiteral("Icon")).toString().trimmed();
        }
    }

    if (foundIcon.isEmpty()) {
        foundIcon = QStringLiteral("steam_icon_") + steamId;
    }

    AppEntry entry;
    entry.id = QStringLiteral("steam_app_") + steamId;
    entry.name = foundName;
    entry.icon = foundIcon;
    entry.exec = QStringLiteral("steam steam://rungameid/") + steamId;
    entry.desktopFile = !resolvedDf.isEmpty() ? resolvedDf : (QStringLiteral("steam_app_") + steamId + QStringLiteral(".desktop"));
    entry.comment = QStringLiteral("Steam Game");

    s_steamGameCache.insert(steamId, entry);
    return entry;
}

namespace {

void collectServices(const KServiceGroup::Ptr &group, QList<AppEntry> &outApps, QSet<QString> &seenIds)
{
    if (!group || !group->isValid()) return;
    KServiceGroup::List list = group->entries(true, true);
    for (const auto &item : list) {
        if (item->isType(KST_KService)) {
            KService::Ptr s(static_cast<KService*>(item.data()));
            if (!s || !s->isValid() || s->noDisplay() || s->exec().trimmed().isEmpty()) continue;

            QString key = s->desktopEntryName();
            if (seenIds.contains(key)) continue;
            seenIds.insert(key);

            AppEntry entry;
            entry.id = key;
            entry.name = s->name();
            entry.icon = s->icon();
            entry.exec = s->exec();
            entry.comment = s->comment();
            entry.desktopFile = s->entryPath();
            outApps.append(entry);
        } else if (item->isType(KST_KServiceGroup)) {
            KServiceGroup::Ptr sub(static_cast<KServiceGroup*>(item.data()));
            collectServices(sub, outApps, seenIds);
        }
    }
}

} // namespace

AppManager::AppManager()
{
}

QString AppManager::recentAppsFilePath()
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/karyon");
    QDir().mkpath(dir);
    return dir + QStringLiteral("/recent_apps.json");
}

namespace {

QString readProcEnv(int pid, const QByteArray &varName)
{
    if (pid <= 0) return {};
    QFile f(QStringLiteral("/proc/%1/environ").arg(pid));
    if (!f.open(QIODevice::ReadOnly)) return {};
    QByteArray data = f.readAll();
    QList<QByteArray> raw = data.split('\0');
    QByteArray prefix = varName + '=';
    for (const auto &tok : raw) {
        if (tok.startsWith(prefix)) {
            return QString::fromUtf8(tok.mid(prefix.length()));
        }
    }
    return {};
}

int readProcPpid(int pid)
{
    if (pid <= 0) return 0;
    QFile f(QStringLiteral("/proc/%1/stat").arg(pid));
    if (!f.open(QIODevice::ReadOnly)) return 0;
    QByteArray data = f.readAll();
    int closeParen = data.lastIndexOf(')');
    if (closeParen < 0) return 0;
    QByteArray rest = data.mid(closeParen + 2);
    QList<QByteArray> parts = rest.split(' ');
    if (parts.size() >= 2) {
        return parts[1].toInt();
    }
    return 0;
}

} // namespace

AppEntry AppManager::findAppByExec(const QString &execPath)
{
    if (execPath.isEmpty()) return {};

    static QHash<QString, AppEntry> s_execCache;
    if (s_execCache.contains(execPath)) {
        return s_execCache.value(execPath);
    }

    QFileInfo fi(execPath);
    QString baseName = fi.completeBaseName();
    QString fileName = fi.fileName();

    // 1. Direkt über BaseName.desktop prüfen (z.B. JusticeViewer -> JusticeViewer.desktop)
    if (!baseName.isEmpty()) {
        QString directDf = findDesktopFilePath(baseName);
        if (!directDf.isEmpty() && QFile::exists(directDf)) {
            QSettings ini(directDf, QSettings::IniFormat);
            ini.beginGroup(QStringLiteral("Desktop Entry"));
            if (!ini.value(QStringLiteral("NoDisplay"), false).toBool()) {
                QString exec = ini.value(QStringLiteral("Exec")).toString().trimmed();
                if (!exec.isEmpty()) {
                    AppEntry entry;
                    entry.id = baseName;
                    entry.name = ini.value(QStringLiteral("Name")).toString();
                    if (entry.name.isEmpty()) entry.name = baseName;
                    entry.icon = ini.value(QStringLiteral("Icon")).toString();
                    if (entry.icon.isEmpty()) entry.icon = baseName;
                    entry.exec = exec;
                    entry.comment = ini.value(QStringLiteral("Comment")).toString();
                    entry.desktopFile = directDf;
                    s_execCache.insert(execPath, entry);
                    return entry;
                }
            }
        }
    }

    // 2. Durchsuche Desktop-Verzeichnisse nach Exec-Einträgen, die execPath enthalten
    static const QStringList searchDirs = []() {
        QStringList dirs = QStandardPaths::standardLocations(QStandardPaths::ApplicationsLocation);
        dirs.prepend(QStringLiteral("/usr/share/applications"));
        dirs.append(QStringLiteral("/var/lib/flatpak/exports/share/applications"));
        dirs.append(QDir::homePath() + QStringLiteral("/.local/share/flatpak/exports/share/applications"));
        dirs.removeDuplicates();
        return dirs;
    }();

    for (const auto &dirPath : searchDirs) {
        QDir dir(dirPath);
        QStringList dfFiles = dir.entryList({QStringLiteral("*.desktop")}, QDir::Files);
        for (const auto &df : dfFiles) {
            QString fullPath = dir.filePath(df);
            QSettings ini(fullPath, QSettings::IniFormat);
            ini.beginGroup(QStringLiteral("Desktop Entry"));
            if (ini.value(QStringLiteral("NoDisplay"), false).toBool()) continue;

            QString exec = ini.value(QStringLiteral("Exec")).toString().trimmed();
            if (exec.contains(execPath) || (!fileName.isEmpty() && exec.contains(fileName))) {
                AppEntry entry;
                QString id = df;
                if (id.endsWith(QLatin1String(".desktop"))) id.chop(8);
                entry.id = id;
                entry.name = ini.value(QStringLiteral("Name")).toString();
                if (entry.name.isEmpty()) entry.name = id;
                entry.icon = ini.value(QStringLiteral("Icon")).toString();
                if (entry.icon.isEmpty()) entry.icon = id;
                entry.exec = exec;
                entry.comment = ini.value(QStringLiteral("Comment")).toString();
                entry.desktopFile = fullPath;
                s_execCache.insert(execPath, entry);
                return entry;
            }
        }
    }

    s_execCache.insert(execPath, {});
    return {};
}

AppEntry AppManager::resolveAppImageFromPath(const QString &appImagePath, int pid)
{
    if (appImagePath.isEmpty() || !isAppImage(appImagePath)) return {};

    // 1. Zuerst prüfen, ob eine reguläre .desktop-Datei für diese Exec existiert
    AppEntry entry = findAppByExec(appImagePath);
    if (!entry.id.isEmpty() && !entry.exec.trimmed().isEmpty()) {
        return entry;
    }

    // 2. Keine System-.desktop-Datei vorhanden: Eigenständiges AppImage
    QFileInfo fi(appImagePath);
    QString baseId = fi.completeBaseName();
    entry.id = baseId;
    entry.name = baseId;
    entry.exec = appImagePath;
    entry.desktopFile = appImagePath;
    entry.icon = baseId;

    // Persistentes Icon-Verzeichnis vorbereiten (~/.local/share/karyon/icons/)
    QString iconCacheDir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/karyon/icons");
    QDir().mkpath(iconCacheDir);
    QString cachedIconPng = iconCacheDir + QLatin1Char('/') + baseId + QStringLiteral(".png");
    QString cachedIconSvg = iconCacheDir + QLatin1Char('/') + baseId + QStringLiteral(".svg");

    if (QFile::exists(cachedIconPng)) {
        entry.icon = cachedIconPng;
    } else if (QFile::exists(cachedIconSvg)) {
        entry.icon = cachedIconSvg;
    }

    // Falls Prozess-PID vorhanden ist, prüfen wir den gemounteten APPDIR (/tmp/.mount_...)
    if (pid > 0) {
        QString appDir = readProcEnv(pid, "APPDIR");
        if (appDir.isEmpty()) {
            int ppid = readProcPpid(pid);
            if (ppid > 0) appDir = readProcEnv(ppid, "APPDIR");
        }

        if (!appDir.isEmpty() && QDir(appDir).exists()) {
            QDir mountDir(appDir);

            // .desktop-Datei im Mount-Ordner suchen
            QStringList dfs = mountDir.entryList({QStringLiteral("*.desktop")}, QDir::Files);
            if (!dfs.isEmpty()) {
                QString mDfPath = mountDir.filePath(dfs.first());
                QSettings dfIni(mDfPath, QSettings::IniFormat);
                dfIni.beginGroup(QStringLiteral("Desktop Entry"));
                QString n = dfIni.value(QStringLiteral("Name")).toString();
                if (!n.isEmpty()) entry.name = n;
                entry.comment = dfIni.value(QStringLiteral("Comment")).toString();
                QString ic = dfIni.value(QStringLiteral("Icon")).toString();
                if (!ic.isEmpty() && entry.icon == baseId) {
                    entry.icon = ic;
                }
            }

            // Icon aus dem gemounteten Image kopieren, damit es nach dem Schließen erhalten bleibt!
            if (entry.icon == baseId || !QFile::exists(entry.icon)) {
                QString candidateIcon;
                if (QFile::exists(mountDir.filePath(QStringLiteral(".DirIcon")))) {
                    candidateIcon = mountDir.filePath(QStringLiteral(".DirIcon"));
                } else {
                    QStringList icons = mountDir.entryList({baseId + QStringLiteral("*.png"), baseId + QStringLiteral("*.svg"), QStringLiteral("*.png"), QStringLiteral("*.svg")}, QDir::Files);
                    if (!icons.isEmpty()) {
                        candidateIcon = mountDir.filePath(icons.first());
                    }
                }

                if (!candidateIcon.isEmpty()) {
                    QString target = candidateIcon.endsWith(QLatin1String(".svg"), Qt::CaseInsensitive) ? cachedIconSvg : cachedIconPng;
                    QFile::remove(target);
                    if (QFile::copy(candidateIcon, target)) {
                        entry.icon = target;
                    }
                }
            }
        }
    }

    return entry;
}

AppEntry AppManager::resolveAppImageFromPid(int pid)
{
    if (pid <= 0) return {};

    static QHash<int, AppEntry> s_pidCache;
    if (s_pidCache.contains(pid)) {
        return s_pidCache.value(pid);
    }

    QString appImagePath = readProcEnv(pid, "APPIMAGE");
    if (appImagePath.isEmpty()) {
        int ppid = readProcPpid(pid);
        if (ppid > 0) {
            appImagePath = readProcEnv(ppid, "APPIMAGE");
        }
    }

    // Niemals andere Prozesse als Karyon selbst auflösen!
    if (appImagePath.contains(QLatin1String("karyon"), Qt::CaseInsensitive)) {
        s_pidCache.insert(pid, {});
        return {};
    }

    QString exeTarget = QFile::symLinkTarget(QStringLiteral("/proc/%1/exe").arg(pid));

    // Validierung: Ein echter AppImage-Prozess läuft aus /tmp/.mount_* oder /tmp/appimage_extracted_*,
    // oder /proc/<pid>/exe ist die .AppImage Datei selbst.
    // Liegt /proc/<pid>/exe in /usr/ oder /bin/ (z.B. /usr/bin/steam), ist es KEIN AppImage!
    bool isRealAppImage = false;
    QString appDir = readProcEnv(pid, "APPDIR");
    if (appDir.isEmpty()) {
        int ppid = readProcPpid(pid);
        if (ppid > 0) appDir = readProcEnv(ppid, "APPDIR");
    }

    if (!appDir.isEmpty() && (exeTarget.startsWith(appDir) || exeTarget.contains(QLatin1String("/.mount_")) || exeTarget.contains(QLatin1String("/appimage_extracted_")))) {
        isRealAppImage = true;
    } else if (isAppImage(exeTarget) || (!appImagePath.isEmpty() && isAppImage(appImagePath))) {
        if (!exeTarget.startsWith(QLatin1String("/usr/")) && !exeTarget.startsWith(QLatin1String("/bin/"))) {
            isRealAppImage = true;
        }
    }

    if (!isRealAppImage) {
        s_pidCache.insert(pid, {});
        return {};
    }

    if (!appImagePath.isEmpty() && isAppImage(appImagePath)) {
        AppEntry entry = resolveAppImageFromPath(appImagePath, pid);
        if (!entry.id.isEmpty()) {
            s_pidCache.insert(pid, entry);
            return entry;
        }
    } else if (isAppImage(exeTarget)) {
        AppEntry entry = resolveAppImageFromPath(exeTarget, pid);
        if (!entry.id.isEmpty()) {
            s_pidCache.insert(pid, entry);
            return entry;
        }
    }

    s_pidCache.insert(pid, {});
    return {};
}

AppEntry AppManager::findApp(const QString &desktopFileName, const QString &resourceClass)
{
    static QHash<QPair<QString, QString>, AppEntry> s_findAppCache;
    QPair<QString, QString> cacheKey{desktopFileName, resourceClass};
    if (s_findAppCache.contains(cacheKey)) {
        return s_findAppCache.value(cacheKey);
    }

    auto cacheAndReturn = [&](const AppEntry &entry) -> AppEntry {
        s_findAppCache.insert(cacheKey, entry);
        return entry;
    };

    QString steamId;
    if (isSteamApp(desktopFileName, resourceClass, &steamId)) {
        AppEntry steamGame = resolveSteamGame(steamId);
        if (!steamGame.id.isEmpty()) return cacheAndReturn(steamGame);
    }

    if (isAppImage(desktopFileName)) {
        AppEntry ai = resolveAppImageFromPath(desktopFileName);
        if (!ai.id.isEmpty()) return cacheAndReturn(ai);
    }
    if (isAppImage(resourceClass)) {
        AppEntry ai = resolveAppImageFromPath(resourceClass);
        if (!ai.id.isEmpty()) return cacheAndReturn(ai);
    }

    auto serviceFrom = [](const QString &key) -> KService::Ptr {
        if (key.isEmpty()) return {};
        KService::Ptr s;
        if (key.endsWith(QLatin1String(".desktop"))) {
            s = KService::serviceByDesktopPath(key);
            if (!s) {
                QString stripped = key;
                stripped.chop(8);
                s = KService::serviceByDesktopName(stripped);
            }
        } else {
            s = KService::serviceByDesktopName(key);
        }
        if (!s) {
            s = KService::serviceByStorageId(key);
        }
        return s;
    };

    KService::Ptr s = serviceFrom(desktopFileName);
    if (!s && !resourceClass.isEmpty()) {
        s = serviceFrom(resourceClass);
    }

    if (s && s->isValid() && !s->noDisplay() && !s->exec().trimmed().isEmpty()) {
        QString nameLow = s->name().toLower();
        if (nameLow == QLatin1String("steam game 0") || s->desktopEntryName() == QLatin1String("steam_app_0")) {
            return cacheAndReturn({});
        }
        AppEntry entry;
        entry.id = s->desktopEntryName();
        entry.name = s->name();
        entry.icon = s->icon();
        entry.exec = s->exec();
        entry.comment = s->comment();
        entry.desktopFile = s->entryPath();
        return cacheAndReturn(entry);
    }

    // Fallback: direct desktop file search with case-insensitive / normalized index
    QString dfPath = findDesktopFilePathCaseInsensitive(desktopFileName);
    if (dfPath.isEmpty() && !resourceClass.isEmpty()) {
        dfPath = findDesktopFilePathCaseInsensitive(resourceClass);
    }

    if (!dfPath.isEmpty() && QFile::exists(dfPath)) {
        QSettings ini(dfPath, QSettings::IniFormat);
        ini.beginGroup(QStringLiteral("Desktop Entry"));
        if (ini.value(QStringLiteral("NoDisplay"), false).toBool()) {
            return cacheAndReturn({});
        }
        QString exec = ini.value(QStringLiteral("Exec")).toString().trimmed();
        if (exec.isEmpty()) return cacheAndReturn({});

        QString name = ini.value(QStringLiteral("Name")).toString();
        if (!name.isEmpty()) {
            QString nameLow = name.toLower();
            if (nameLow == QLatin1String("steam game 0") || dfPath.endsWith(QLatin1String("steam_app_0.desktop"))) {
                return cacheAndReturn({});
            }
        }

        AppEntry entry;
        QString base = dfPath.section(QLatin1Char('/'), -1);
        if (base.endsWith(QLatin1String(".desktop"))) base.chop(8);
        entry.id = base;
        entry.name = name.isEmpty() ? base : name;
        entry.icon = ini.value(QStringLiteral("Icon")).toString();
        if (entry.icon.isEmpty()) entry.icon = base;
        entry.exec = exec;
        entry.comment = ini.value(QStringLiteral("Comment")).toString();
        entry.desktopFile = dfPath;
        return cacheAndReturn(entry);
    }

    AppEntry byExec = findAppByExec(desktopFileName);
    if (!byExec.id.isEmpty()) return cacheAndReturn(byExec);
    byExec = findAppByExec(resourceClass);
    if (!byExec.id.isEmpty()) return cacheAndReturn(byExec);

    return cacheAndReturn({});
}

AppEntry AppManager::resolveWindowApp(const QString &desktopFileName, const QString &resourceClass, int pid, const QString &caption)
{
    static QHash<QString, AppEntry> s_windowAppCache;
    QString cacheKey = QStringLiteral("%1|%2|%3").arg(desktopFileName, resourceClass, QString::number(pid));
    if (s_windowAppCache.contains(cacheKey)) {
        AppEntry cached = s_windowAppCache.value(cacheKey);
        if (!caption.isEmpty() && cached.name.startsWith(QLatin1String("Steam App "))) {
            QString cleanCap = caption;
            cleanCap.remove(QStringLiteral(" — Steam")).remove(QStringLiteral(" - Steam"));
            if (!cleanCap.trimmed().isEmpty()) {
                cached.name = cleanCap.trimmed();
                s_windowAppCache.insert(cacheKey, cached);
            }
        }
        return cached;
    }

    auto cacheAndReturn = [&](const AppEntry &entry) -> AppEntry {
        s_windowAppCache.insert(cacheKey, entry);
        return entry;
    };

    // 1. Steam / Proton Game
    QString steamId;
    if (isSteamApp(desktopFileName, resourceClass, &steamId, pid)) {
        AppEntry steamGame = resolveSteamGame(steamId, caption);
        if (!steamGame.id.isEmpty()) return cacheAndReturn(steamGame);
    }

    // 2. Reguläre App-Suche (.desktop / sycoca / desktop index)
    AppEntry app = findApp(desktopFileName, resourceClass);
    if (!app.id.isEmpty() && !app.name.isEmpty()) {
        return cacheAndReturn(app);
    }

    // 3. Echter AppImage-Prozess via PID
    if (pid > 0) {
        AppEntry ai = resolveAppImageFromPid(pid);
        if (!ai.id.isEmpty()) {
            return cacheAndReturn(ai);
        }
    }

    // 4. AppImage via Pfad
    if (isAppImage(desktopFileName) || isAppImage(resourceClass)) {
        QString aiPath = isAppImage(desktopFileName) ? desktopFileName : resourceClass;
        AppEntry ai = resolveAppImageFromPath(aiPath, pid);
        if (!ai.id.isEmpty()) {
            return cacheAndReturn(ai);
        }
    }

    // 5. Fallback für Fenster ohne Treffer
    QString raw = desktopFileName.isEmpty() ? resourceClass : desktopFileName;
    if (raw.endsWith(QLatin1String(".desktop"), Qt::CaseInsensitive)) {
        raw.chop(8);
    }
    int slash = raw.lastIndexOf(QLatin1Char('/'));
    if (slash >= 0 && slash + 1 < raw.length()) {
        raw = raw.mid(slash + 1);
    }
    int dot = raw.lastIndexOf(QLatin1Char('.'));
    if (dot >= 0 && dot + 1 < raw.length()) {
        raw = raw.mid(dot + 1);
    }

    AppEntry fallback;
    fallback.id = raw.isEmpty() ? QStringLiteral("window") : raw;
    if (!caption.trimmed().isEmpty()) {
        QString cleanCap = caption.trimmed();
        cleanCap.remove(QStringLiteral(" — Steam")).remove(QStringLiteral(" - Steam"));
        fallback.name = cleanCap;
    } else if (!raw.isEmpty()) {
        QString n = raw;
        n[0] = n[0].toUpper();
        fallback.name = n;
    } else {
        fallback.name = QStringLiteral("Window");
    }
    fallback.icon = resourceClass.isEmpty() ? desktopFileName : resourceClass;
    return cacheAndReturn(fallback);
}

static QStringList readProcCmdline(int pid)
{
    if (pid <= 0) return {};
    QFile f(QStringLiteral("/proc/%1/cmdline").arg(pid));
    if (!f.open(QIODevice::ReadOnly)) return {};
    QByteArray data = f.readAll();
    QList<QByteArray> raw = data.split('\0');
    QStringList args;
    for (const auto &tok : raw) {
        if (!tok.isEmpty()) {
            args.append(QString::fromUtf8(tok));
        }
    }
    return args;
}

bool AppManager::isBlacklistedRecentApp(const AppEntry &entry, int pid, const QString &caption)
{
    QString id = entry.id.toLower();
    QString name = entry.name.toLower();
    QString exec = entry.exec.toLower();
    QString df = entry.desktopFile.toLower();

    // 0. Karyon selbst niemals als Recent App aufnehmen!
    if (id.contains(QLatin1String("karyon")) ||
        exec.contains(QLatin1String("karyon")) ||
        df.contains(QLatin1String("karyon")) ||
        name.contains(QLatin1String("karyon"))) {
        return true;
    }

    // 1. Reine Hilfsprozesse & Sicherheitsdialoge (immer ausschließen)
    if (id.contains(QLatin1String("polkit-")) || id.contains(QLatin1String("pinentry")) ||
        id.contains(QLatin1String("kwallet")) || id.contains(QLatin1String("gcr-prompter")) ||
        id.contains(QLatin1String("xdg-desktop-portal")) || id == QLatin1String("kdialog") ||
        id == QLatin1String("zenity") || id == QLatin1String("xmessage")) {
        return true;
    }
    if (exec.contains(QLatin1String("polkit-")) || exec.contains(QLatin1String("pinentry")) ||
        exec.contains(QLatin1String("kwallet")) || exec.contains(QLatin1String("gcr-prompter")) ||
        exec.contains(QLatin1String("xdg-desktop-portal")) || exec.startsWith(QLatin1String("kdialog")) ||
        exec.startsWith(QLatin1String("zenity")) || exec.startsWith(QLatin1String("xmessage"))) {
        return true;
    }

    // 2. Einstellungs-Unterprogramme / Sub-Module ausschließen (Hauptprogramm 'systemsettings' bleibt erlaubt!)
    if (id != QLatin1String("systemsettings") && !df.endsWith(QLatin1String("/systemsettings.desktop")) && df != QLatin1String("systemsettings")) {
        if (id.startsWith(QLatin1String("kcm_")) || id.startsWith(QLatin1String("kcmshell")) ||
            id == QLatin1String("print-manager") || id.contains(QLatin1String("cups")) ||
            id.contains(QLatin1String("printer")) || id.contains(QLatin1String("druck")) ||
            id == QLatin1String("system-config-printer") || id == QLatin1String("kinfocenter") ||
            id == QLatin1String("org.kde.kinfocenter")) {
            return true;
        }
        if (exec.contains(QLatin1String("kcm_")) || exec.contains(QLatin1String("kcmshell")) ||
            exec.contains(QLatin1String("system-config-printer")) || exec.contains(QLatin1String("print-manager")) ||
            exec.contains(QLatin1String("cups")) || exec.contains(QLatin1String("kinfocenter"))) {
            return true;
        }
        if (df.contains(QLatin1String("kcm_")) || df.contains(QLatin1String("system-config-printer")) ||
            df.contains(QLatin1String("print-manager")) || df.contains(QLatin1String("kinfocenter"))) {
            return true;
        }
        if (name.contains(QLatin1String("druckeinstellung")) ||
            name.contains(QLatin1String("drucker einrichten")) ||
            name.contains(QLatin1String("printer settings")) ||
            name.contains(QLatin1String("printer configuration")) ||
            name == QLatin1String("drucker") ||
            name == QLatin1String("printers") ||
            name == QLatin1String("infozentrum") ||
            name == QLatin1String("kinfocenter")) {
            return true;
        }
    }

    // 3. Screenshot-Tools temporäre Aufnahme-Flags
    bool isScreenshotTool = (id == QLatin1String("org.kde.spectacle") || id == QLatin1String("spectacle") ||
                             id == QLatin1String("flameshot") || id == QLatin1String("gnome-screenshot") ||
                             exec.contains(QLatin1String("spectacle")) || exec.contains(QLatin1String("flameshot")));
    if (isScreenshotTool && pid > 0) {
        QStringList args = readProcCmdline(pid);
        for (int i = 1; i < args.size(); ++i) {
            QString low = args[i].toLower();
            if (low == QLatin1String("-r") || low == QLatin1String("--region") ||
                low == QLatin1String("-m") || low == QLatin1String("--current") ||
                low == QLatin1String("-f") || low == QLatin1String("--fullscreen") ||
                low == QLatin1String("-a") || low == QLatin1String("--activewindow") ||
                low == QLatin1String("-u") || low == QLatin1String("--windowundercursor") ||
                low == QLatin1String("-t") || low == QLatin1String("--transientonly") ||
                low == QLatin1String("-b") || low == QLatin1String("--background") ||
                low == QLatin1String("-c") || low == QLatin1String("--clipboard") ||
                low == QLatin1String("gui")) {
                return true;
            }
        }
    }

    // 4. Systemeinstellungen Direktaufruf von Untermodulen (z.B. 'systemsettings kcm_...')
    if ((id == QLatin1String("systemsettings") || exec.contains(QLatin1String("systemsettings"))) && pid > 0) {
        QStringList args = readProcCmdline(pid);
        for (int i = 1; i < args.size(); ++i) {
            if (args[i].startsWith(QLatin1String("kcm_")) || args[i].contains(QLatin1String("kcm_"))) {
                return true;
            }
        }
    }

    // 5. Allgemeingültige Prüfung: Wurde das Programm über eine Datei aufgerufen (Datei-Handler)?
    // Apps, die nativ Verzeichnisse verwalten, Web-URLs ansteuern oder Steam-Spiele ausführen, sind ausgenommen.
    bool isExemptApp = false;
    if (id.contains(QLatin1String("brave")) || id.contains(QLatin1String("firefox")) ||
        id.contains(QLatin1String("chrome")) || id.contains(QLatin1String("chromium")) ||
        id.contains(QLatin1String("vivaldi")) || id.contains(QLatin1String("opera")) ||
        id.contains(QLatin1String("edge")) || id.contains(QLatin1String("tor-browser")) ||
        exec.contains(QLatin1String("brave")) || exec.contains(QLatin1String("firefox")) ||
        exec.contains(QLatin1String("chrome")) || exec.contains(QLatin1String("chromium"))) {
        isExemptApp = true;
    } else if (id == QLatin1String("org.kde.dolphin") || id == QLatin1String("dolphin") ||
               id.contains(QLatin1String("nemo")) || id.contains(QLatin1String("nautilus")) ||
               id.contains(QLatin1String("thunar")) || id.contains(QLatin1String("pcmanfm")) ||
               id.contains(QLatin1String("krusader")) || exec.contains(QLatin1String("dolphin"))) {
        isExemptApp = true;
    } else if (id == QLatin1String("org.kde.konsole") || id == QLatin1String("konsole") ||
               id.contains(QLatin1String("alacritty")) || id.contains(QLatin1String("kitty")) ||
               id.contains(QLatin1String("foot")) || id.contains(QLatin1String("wezterm")) ||
               id.contains(QLatin1String("gnome-terminal")) || id.contains(QLatin1String("xterm"))) {
        isExemptApp = true;
    } else if (id.startsWith(QLatin1String("steam_app_")) || id == QLatin1String("steam") ||
               exec.contains(QLatin1String("steam://rungameid/"))) {
        isExemptApp = true;
    }

    if (!isExemptApp) {
        static const QSet<QString> dataExtensions = {
            // Archive
            QStringLiteral("zip"), QStringLiteral("tar"), QStringLiteral("gz"), QStringLiteral("tgz"),
            QStringLiteral("bz2"), QStringLiteral("tbz2"), QStringLiteral("xz"), QStringLiteral("txz"),
            QStringLiteral("7z"), QStringLiteral("rar"), QStringLiteral("zst"), QStringLiteral("tzst"),
            QStringLiteral("iso"), QStringLiteral("dmg"), QStringLiteral("cab"),
            // Bilder & Grafik
            QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("jpeg"), QStringLiteral("jpe"),
            QStringLiteral("jfif"), QStringLiteral("gif"), QStringLiteral("bmp"), QStringLiteral("dib"),
            QStringLiteral("webp"), QStringLiteral("svg"), QStringLiteral("svgz"), QStringLiteral("ico"),
            QStringLiteral("tiff"), QStringLiteral("tif"), QStringLiteral("avif"), QStringLiteral("heic"),
            QStringLiteral("heif"), QStringLiteral("raw"), QStringLiteral("cr2"), QStringLiteral("nef"),
            QStringLiteral("arw"), QStringLiteral("dng"), QStringLiteral("xcf"), QStringLiteral("psd"),
            QStringLiteral("kra"),
            // Dokumente & Office
            QStringLiteral("pdf"), QStringLiteral("djvu"), QStringLiteral("epub"), QStringLiteral("mobi"),
            QStringLiteral("cbr"), QStringLiteral("cbz"), QStringLiteral("doc"), QStringLiteral("docx"),
            QStringLiteral("odt"), QStringLiteral("ott"), QStringLiteral("rtf"), QStringLiteral("txt"),
            QStringLiteral("md"), QStringLiteral("markdown"), QStringLiteral("xls"), QStringLiteral("xlsx"),
            QStringLiteral("ods"), QStringLiteral("ots"), QStringLiteral("csv"), QStringLiteral("tsv"),
            QStringLiteral("ppt"), QStringLiteral("pptx"), QStringLiteral("odp"), QStringLiteral("otp"),
            // Audio & Video
            QStringLiteral("mp3"), QStringLiteral("flac"), QStringLiteral("wav"), QStringLiteral("ogg"),
            QStringLiteral("oga"), QStringLiteral("m4a"), QStringLiteral("aac"), QStringLiteral("opus"),
            QStringLiteral("wma"), QStringLiteral("mp4"), QStringLiteral("mkv"), QStringLiteral("avi"),
            QStringLiteral("mov"), QStringLiteral("webm"), QStringLiteral("flv"), QStringLiteral("wmv"),
            QStringLiteral("m4v"), QStringLiteral("mpg"), QStringLiteral("mpeg")
        };

        // A. Start-Argumente prüfen (/proc/<pid>/cmdline)
        if (pid > 0) {
            QStringList args = readProcCmdline(pid);
            for (int i = 1; i < args.size(); ++i) {
                const QString &arg = args[i];
                if (arg.isEmpty()) continue;
                QString low = arg.toLower();

                // file:// URI
                if (low.startsWith(QLatin1String("file://"))) {
                    return true;
                }

                // Dateiendung als Argument prüfen
                int dot = low.lastIndexOf(QLatin1Char('.'));
                if (dot > 0 && dot + 1 < low.length()) {
                    QString ext = low.mid(dot + 1);
                    if (dataExtensions.contains(ext) && !arg.startsWith(QLatin1String("--"))) {
                        return true;
                    }
                }

                // Absoluter Pfad zu einer Datei im Benutzerbereich
                if (arg.startsWith(QLatin1Char('/')) &&
                    !arg.startsWith(QLatin1String("/usr")) &&
                    !arg.startsWith(QLatin1String("/opt")) &&
                    !arg.startsWith(QLatin1String("/bin")) &&
                    !arg.startsWith(QLatin1String("/sbin")) &&
                    !arg.startsWith(QLatin1String("/lib")) &&
                    !arg.startsWith(QLatin1String("/tmp/.mount_")) &&
                    !arg.startsWith(QLatin1String("/etc")) &&
                    !arg.startsWith(QLatin1String("/dev")) &&
                    !arg.startsWith(QLatin1String("/proc")) &&
                    !arg.startsWith(QLatin1String("/sys"))) {
                    QFileInfo fi(arg);
                    if (fi.exists() && fi.isFile()) {
                        return true;
                    }
                }
            }
        }

        // B. Ergänzende Fenstertitel-Prüfung (caption)
        if (!caption.isEmpty()) {
            static const QRegularExpression fileInTitleRe(QStringLiteral("\\.([a-zA-Z0-9]{2,5})(?![a-zA-Z0-9])"));
            auto it = fileInTitleRe.globalMatch(caption);
            while (it.hasNext()) {
                auto match = it.next();
                QString ext = match.captured(1).toLower();
                if (dataExtensions.contains(ext)) {
                    return true;
                }
            }
        }
    }

    return false;
}

void AppManager::recordAppUsage(const QString &desktopFileName, const QString &resourceClass, int pid, const QString &caption)
{
    if (resourceClass == QLatin1String("karyon") || 
        resourceClass.contains(QLatin1String("plasmawindowed")) ||
        resourceClass == QLatin1String("plasmashell") ||
        resourceClass == QLatin1String("org.kde.plasmashell") ||
        resourceClass == QLatin1String("krunner")) {
        return;
    }
    if (desktopFileName.isEmpty() && resourceClass.isEmpty() && pid <= 0) return;

    AppEntry app = resolveWindowApp(desktopFileName, resourceClass, pid, caption);
    if (app.id.isEmpty() || app.exec.trimmed().isEmpty() || isBlacklistedRecentApp(app, pid, caption)) {
        return;
    }

    static QString s_lastRecordedId;
    static qint64 s_lastRecordedTime = 0;
    qint64 now = QDateTime::currentSecsSinceEpoch();

    if (s_lastRecordedId == app.id && (now - s_lastRecordedTime) < 2) {
        return; // Throttle redundant writes
    }
    s_lastRecordedId = app.id;
    s_lastRecordedTime = now;

    QString path = recentAppsFilePath();
    QJsonArray arr;
    QFile f(path);
    if (f.open(QIODevice::ReadOnly)) {
        QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
        if (doc.isArray()) {
            arr = doc.array();
        }
        f.close();
    }

    QJsonArray newArr;
    QJsonObject currentItem;
    currentItem[QStringLiteral("id")] = app.id;
    currentItem[QStringLiteral("name")] = app.name;
    currentItem[QStringLiteral("icon")] = app.icon;
    currentItem[QStringLiteral("exec")] = app.exec;
    currentItem[QStringLiteral("comment")] = app.comment;
    currentItem[QStringLiteral("desktopFile")] = app.desktopFile;
    currentItem[QStringLiteral("lastUsed")] = now;

    static const QRegularExpression runRe(QStringLiteral("steam://rungameid/(\\d+)"), QRegularExpression::CaseInsensitiveOption);
    QString appSteamId;
    isSteamApp(app.desktopFile, app.id, &appSteamId, pid);
    if (appSteamId.isEmpty()) {
        auto mRun = runRe.match(app.exec);
        if (mRun.hasMatch()) appSteamId = mRun.captured(1);
    }

    int launchCount = 1;
    for (const auto &val : arr) {
        QJsonObject obj = val.toObject();
        QString existingId = obj[QStringLiteral("id")].toString();
        QString existingDf = obj[QStringLiteral("desktopFile")].toString();
        QString existingExec = obj[QStringLiteral("exec")].toString();
        QString existingName = obj[QStringLiteral("name")].toString();

        QString existingSteamId;
        if (!isSteamApp(existingDf, existingId, &existingSteamId)) {
            auto mRun = runRe.match(existingExec);
            if (mRun.hasMatch()) existingSteamId = mRun.captured(1);
        }

        bool match = (existingId == app.id) ||
                     (!app.desktopFile.isEmpty() && existingDf == app.desktopFile) ||
                     (!app.exec.isEmpty() && existingExec == app.exec) ||
                     (!app.name.trimmed().isEmpty() && existingName.compare(app.name, Qt::CaseInsensitive) == 0);
        if (!match && !appSteamId.isEmpty() && !existingSteamId.isEmpty()) {
            match = (appSteamId == existingSteamId);
        }

        if (match) {
            launchCount = qMax(launchCount, obj[QStringLiteral("launchCount")].toInt(1) + 1);
            continue; // Will be prepended at front
        }
        newArr.append(obj);
    }

    currentItem[QStringLiteral("launchCount")] = launchCount;
    newArr.prepend(currentItem);

    while (newArr.size() > 100) {
        newArr.removeLast();
    }

    QSaveFile sf(path);
    if (sf.open(QIODevice::WriteOnly)) {
        QJsonDocument doc(newArr);
        sf.write(doc.toJson(QJsonDocument::Compact));
        sf.commit();
    }
}

void AppManager::recordAppUsageById(const QString &appId)
{
    recordAppUsage(appId, appId);
}

static QString extractSteamId(const AppEntry &entry)
{
    static const QRegularExpression appRe(QStringLiteral("steam_app_(\\d+)"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression runRe(QStringLiteral("steam://rungameid/(\\d+)"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression iconRe(QStringLiteral("steam_icon_(\\d+)"), QRegularExpression::CaseInsensitiveOption);

    auto m = appRe.match(entry.id);
    if (m.hasMatch()) return m.captured(1);
    m = appRe.match(entry.desktopFile);
    if (m.hasMatch()) return m.captured(1);
    m = runRe.match(entry.exec);
    if (m.hasMatch()) return m.captured(1);
    m = iconRe.match(entry.icon);
    if (m.hasMatch()) return m.captured(1);
    return QString();
}

static QString cleanExecKey(const QString &exec)
{
    QString trimmed = exec.trimmed();
    if (trimmed.isEmpty()) return {};

    static const QRegularExpression runRe(QStringLiteral("steam://rungameid/(\\d+)"), QRegularExpression::CaseInsensitiveOption);
    auto m = runRe.match(trimmed);
    if (m.hasMatch()) {
        return QStringLiteral("steam_game_%1").arg(m.captured(1));
    }

    static const QRegularExpression fieldCodes(QStringLiteral("%[a-zA-Z]"));
    trimmed.remove(fieldCodes);
    return trimmed.trimmed().toLower();
}

QList<AppEntry> AppManager::loadRecentApps(int maxCount, const QSet<QString> &excludeAppKeys)
{
    QList<AppEntry> result;
    QSet<QString> seenIds;
    QSet<QString> seenNames;
    QSet<QString> seenExecs;
    QSet<QString> seenSteamIds;

    static const QSet<QString> genericNames = {
        QStringLiteral("window"), QStringLiteral("app"), QStringLiteral("application"),
        QStringLiteral("terminal"), QStringLiteral("settings"), QStringLiteral("desktop")
    };

    auto isDuplicate = [&](const AppEntry &entry, const QString &steamId) -> bool {
        if (!steamId.isEmpty() && seenSteamIds.contains(steamId)) {
            return true;
        }
        if (!entry.id.isEmpty() && seenIds.contains(entry.id.toLower())) {
            return true;
        }
        if (!entry.desktopFile.isEmpty()) {
            QString df = entry.desktopFile.toLower();
            if (seenIds.contains(df)) return true;
            int slash = df.lastIndexOf(QLatin1Char('/'));
            if (slash >= 0 && seenIds.contains(df.mid(slash + 1))) return true;
            if (df.endsWith(QLatin1String(".desktop"))) {
                QString stripped = df.left(df.length() - 8);
                if (seenIds.contains(stripped)) return true;
                int s2 = stripped.lastIndexOf(QLatin1Char('/'));
                if (s2 >= 0 && seenIds.contains(stripped.mid(s2 + 1))) return true;
            }
        }
        QString normName = entry.name.trimmed().toLower();
        if (!normName.isEmpty() && !genericNames.contains(normName) && seenNames.contains(normName)) {
            return true;
        }
        QString execKey = cleanExecKey(entry.exec);
        if (!execKey.isEmpty() && seenExecs.contains(execKey)) {
            return true;
        }
        return false;
    };

    auto markSeen = [&](const AppEntry &entry, const QString &steamId) {
        if (!steamId.isEmpty()) {
            seenSteamIds.insert(steamId);
            seenIds.insert(QStringLiteral("steam_app_%1").arg(steamId).toLower());
            seenIds.insert(QStringLiteral("steam_app_%1.desktop").arg(steamId).toLower());
        }
        if (!entry.id.isEmpty()) {
            seenIds.insert(entry.id.toLower());
            if (!entry.id.endsWith(QLatin1String(".desktop"), Qt::CaseInsensitive)) {
                seenIds.insert((entry.id + QLatin1String(".desktop")).toLower());
            }
        }
        if (!entry.desktopFile.isEmpty()) {
            QString df = entry.desktopFile.toLower();
            seenIds.insert(df);
            int slash = df.lastIndexOf(QLatin1Char('/'));
            if (slash >= 0) {
                QString base = df.mid(slash + 1);
                seenIds.insert(base);
                if (base.endsWith(QLatin1String(".desktop"))) {
                    seenIds.insert(base.left(base.length() - 8));
                }
            }
        }
        QString normName = entry.name.trimmed().toLower();
        if (!normName.isEmpty() && !genericNames.contains(normName)) {
            seenNames.insert(normName);
        }
        QString execKey = cleanExecKey(entry.exec);
        if (!execKey.isEmpty()) {
            seenExecs.insert(execKey);
        }
    };

    auto matchesExclude = [&](const QString &key) {
        if (key.isEmpty()) return false;
        QString k = key.toLower().trimmed();
        if (excludeAppKeys.contains(k)) return true;
        if (k.endsWith(QLatin1String(".desktop"))) {
            k.chop(8);
            if (excludeAppKeys.contains(k)) return true;
        }
        int dot = k.lastIndexOf(QLatin1Char('.'));
        if (dot >= 0 && dot + 1 < k.length()) {
            if (excludeAppKeys.contains(k.mid(dot + 1))) return true;
        }
        return false;
    };

    auto isExcluded = [&](const AppEntry &entry) {
        if (matchesExclude(entry.id) ||
            matchesExclude(entry.desktopFile) ||
            matchesExclude(entry.name)) {
            return true;
        }
        // Für Steam-Spiele: Niemals den Wrapper 'steam' gegen offene Fenster prüfen!
        if (entry.id.startsWith(QLatin1String("steam_app_"))) {
            return false;
        }
        QString sId = extractSteamId(entry);
        if (!sId.isEmpty() && matchesExclude(QStringLiteral("steam_app_") + sId)) {
            return true;
        }
        QString execCmd = entry.exec.trimmed();
        if (!execCmd.isEmpty()) {
            QString bin = execCmd.section(QLatin1Char(' '), 0, 0);
            int slash = bin.lastIndexOf(QLatin1Char('/'));
            if (slash >= 0) bin = bin.mid(slash + 1);
            static const QSet<QString> wrappers = {
                QStringLiteral("steam"), QStringLiteral("env"), QStringLiteral("flatpak"),
                QStringLiteral("prime-run"), QStringLiteral("gamemoderun"), QStringLiteral("wine"),
                QStringLiteral("wine64"), QStringLiteral("sh"), QStringLiteral("bash"),
                QStringLiteral("python"), QStringLiteral("python3")
            };
            if (!wrappers.contains(bin.toLower())) {
                if (matchesExclude(bin)) return true;
            }
        }
        return false;
    };

    // 1. First, load from our tracked recent_apps.json
    QString jsonPath = recentAppsFilePath();
    QFile jf(jsonPath);
    bool jsonNeedsCleanup = false;
    QJsonArray cleanedArr;

    if (jf.open(QIODevice::ReadOnly)) {
        QJsonDocument doc = QJsonDocument::fromJson(jf.readAll());
        if (doc.isArray()) {
            QJsonArray arr = doc.array();
            for (const auto &val : arr) {
                QJsonObject obj = val.toObject();
                AppEntry entry;
                entry.id = obj[QStringLiteral("id")].toString();
                entry.name = obj[QStringLiteral("name")].toString();
                entry.icon = obj[QStringLiteral("icon")].toString();
                entry.exec = obj[QStringLiteral("exec")].toString();
                entry.comment = obj[QStringLiteral("comment")].toString();
                entry.desktopFile = obj[QStringLiteral("desktopFile")].toString();

                if (isBlacklistedRecentApp(entry)) {
                    jsonNeedsCleanup = true;
                    continue;
                }

                QString sId = extractSteamId(entry);
                if (!sId.isEmpty()) {
                    AppEntry canonical = resolveSteamGame(sId, entry.name);
                    if (!canonical.name.isEmpty()) entry.name = canonical.name;
                    if (!canonical.icon.isEmpty()) entry.icon = canonical.icon;
                    if (!canonical.desktopFile.isEmpty() && entry.desktopFile.isEmpty()) {
                        entry.desktopFile = canonical.desktopFile;
                    }
                }

                if (isDuplicate(entry, sId)) {
                    jsonNeedsCleanup = true;
                    continue;
                }

                cleanedArr.append(obj);

                if (result.size() >= maxCount) continue;
                if (entry.id.isEmpty() || entry.exec.trimmed().isEmpty()) continue;

                // Validate that app still exists on system
                if (!entry.desktopFile.isEmpty() && !QFile::exists(entry.desktopFile)) {
                    if (sId.isEmpty() && !entry.id.startsWith(QLatin1String("steam_app_"))) {
                        KService::Ptr s = KService::serviceByDesktopName(entry.id);
                        if (!s || !s->isValid()) continue;
                    }
                }

                if (isExcluded(entry)) continue;

                markSeen(entry, sId);
                result.append(entry);
            }
        }
        jf.close();
    }

    if (jsonNeedsCleanup) {
        QSaveFile sf(jsonPath);
        if (sf.open(QIODevice::WriteOnly)) {
            QJsonDocument doc(cleanedArr);
            sf.write(doc.toJson(QJsonDocument::Compact));
            sf.commit();
        }
    }

    // 2. If result.size() < maxCount, supplement from kactivitymanagerd
    if (result.size() < maxCount) {
        QString dbPath = QDir::homePath() + QStringLiteral("/.local/share/kactivitymanagerd/resources/database");
        sqlite3 *db = nullptr;
        if (sqlite3_open_v2(dbPath.toUtf8().constData(), &db, SQLITE_OPEN_READONLY, nullptr) == SQLITE_OK) {
            const char *sql = "SELECT replace(targettedResource, 'applications:', ''), max(lastUpdate) as lu "
                              "FROM ResourceScoreCache "
                              "WHERE targettedResource LIKE 'applications:%' "
                              "GROUP BY targettedResource "
                              "ORDER BY lu DESC LIMIT 100;";

            sqlite3_stmt *stmt = nullptr;
            if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
                while (sqlite3_step(stmt) == SQLITE_ROW && result.size() < maxCount) {
                    const char *desktopFile = (const char *)sqlite3_column_text(stmt, 0);
                    if (!desktopFile) continue;
                    QString df = QString::fromUtf8(desktopFile);

                    // Filter irrelevant steam dummies or helpers
                    if (df == QLatin1String("steam_app_0.desktop") || df == QLatin1String("steam_app_0")) {
                        continue;
                    }

                    QString sid;
                    if (isSteamApp(df, QString(), &sid) || df.startsWith(QLatin1String("steam_app_"))) {
                        if (sid.isEmpty()) {
                            sid = df.mid(10);
                            if (sid.endsWith(QLatin1String(".desktop"))) sid.chop(8);
                        }
                        AppEntry steamEntry = resolveSteamGame(sid);
                        if (!steamEntry.id.isEmpty()) {
                            if (isDuplicate(steamEntry, sid)) continue;
                            if (isExcluded(steamEntry)) continue;
                            markSeen(steamEntry, sid);
                            seenIds.insert(df.toLower());
                            result.append(steamEntry);
                            continue;
                        }
                    }

                    KService::Ptr s = KService::serviceByDesktopName(df);
                    if (!s && df.endsWith(QLatin1String(".desktop"))) {
                        QString stripped = df;
                        stripped.chop(8);
                        s = KService::serviceByDesktopName(stripped);
                    }
                    if (!s || !s->isValid() || s->noDisplay() || s->exec().trimmed().isEmpty()) continue;

                    QString nameLow = s->name().toLower();
                    if (nameLow == QLatin1String("steam game 0")) {
                        continue;
                    }

                    AppEntry entry;
                    entry.id = s->desktopEntryName();
                    entry.name = s->name();
                    entry.icon = s->icon();
                    entry.exec = s->exec();
                    entry.comment = s->comment();
                    entry.desktopFile = s->entryPath();

                    QString entrySteamId = extractSteamId(entry);
                    if (!entrySteamId.isEmpty()) {
                        AppEntry steamEntry = resolveSteamGame(entrySteamId, entry.name);
                        if (!steamEntry.id.isEmpty()) {
                            entry = steamEntry;
                        }
                    }

                    if (isDuplicate(entry, entrySteamId)) continue;
                    if (isBlacklistedRecentApp(entry)) continue;
                    if (isExcluded(entry)) continue;

                    markSeen(entry, entrySteamId);
                    seenIds.insert(df.toLower());
                    result.append(entry);
                }
                sqlite3_finalize(stmt);
            }
            sqlite3_close(db);
        }
    }

    return result;
}

QList<CategoryEntry> AppManager::loadCategories()
{
    QList<CategoryEntry> categories;

    KServiceGroup::Ptr root = KServiceGroup::root();
    if (root && root->isValid()) {
        KServiceGroup::List list = root->entries(true, true);
        for (const auto &item : list) {
            if (!item->isType(KST_KServiceGroup)) continue;
            KServiceGroup::Ptr group(static_cast<KServiceGroup*>(item.data()));
            if (!group || group->childCount() == 0) continue;

            CategoryEntry cat;
            cat.id = group->name();
            cat.name = group->caption();
            cat.icon = group->icon();

            QSet<QString> seenIds;
            collectServices(group, cat.apps, seenIds);

            if (!cat.apps.isEmpty()) {
                categories.append(cat);
            }
        }
    }

    // Favorites in der Mitte einfügen (centerIndex = N/2 => exakt 12 Uhr / 90° über Hub-Zentrum)
    QList<AppEntry> favApps = loadFavorites();
    if (!favApps.isEmpty()) {
        CategoryEntry fav;
        fav.id = QStringLiteral("__FAVORITES__");
        fav.name = QStringLiteral("Favorites");
        fav.icon = QStringLiteral("favorites");
        fav.apps = favApps;
        int centerIdx = categories.size() / 2;
        categories.insert(centerIdx, fav);
    }

    return categories;
}

QList<AppEntry> AppManager::loadFavorites()
{
    QList<AppEntry> result;
    QSet<QString> seen;

    QString dbPath = QDir::homePath() + QStringLiteral("/.local/share/kactivitymanagerd/resources/database");
    sqlite3 *db = nullptr;
    if (sqlite3_open_v2(dbPath.toUtf8().constData(), &db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
        return result;
    }

    // KDE Plasma Kickoff Favorites: initiatingAgent enthält 'kickoff.favorites'
    const char *sql = "SELECT DISTINCT replace(targettedResource, 'applications:', '') "
                      "FROM ResourceLink "
                      "WHERE initiatingAgent LIKE '%kickoff.favorites%' "
                      "AND targettedResource LIKE 'applications:%';";

    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const char *desktopFile = (const char *)sqlite3_column_text(stmt, 0);
            if (!desktopFile) continue;
            QString df = QString::fromUtf8(desktopFile);
            if (seen.contains(df)) continue;

            KService::Ptr s = KService::serviceByDesktopName(df);
            if (!s && df.endsWith(QLatin1String(".desktop"))) {
                QString stripped = df;
                stripped.chop(8);
                s = KService::serviceByDesktopName(stripped);
            }
            if (!s || !s->isValid() || s->noDisplay() || s->exec().trimmed().isEmpty()) continue;

            seen.insert(df);

            AppEntry entry;
            entry.id = s->desktopEntryName();
            entry.name = s->name();
            entry.icon = s->icon();
            entry.exec = s->exec();
            entry.comment = s->comment();
            entry.desktopFile = s->entryPath();
            result.append(entry);
        }
        sqlite3_finalize(stmt);
    }
    sqlite3_close(db);

    return result;
}

bool AppManager::launchApp(const QString &execLine)
{
    if (execLine.trimmed().isEmpty()) return false;
    QString cmd = execLine.trimmed();

    // Check if cmd is a desktop file or service name
    KService::Ptr s;
    if (cmd.endsWith(QLatin1String(".desktop"))) {
        s = KService::serviceByDesktopPath(cmd);
        if (!s) {
            QString stripped = cmd.section(QLatin1Char('/'), -1);
            stripped.chop(8);
            s = KService::serviceByDesktopName(stripped);
        }
    } else if (!cmd.contains(QLatin1Char(' '))) {
        s = KService::serviceByDesktopName(cmd);
    }

    if (s && s->isValid() && !s->exec().trimmed().isEmpty()) {
        cmd = s->exec().trimmed();
    }

    static const QRegularExpression re(QStringLiteral("%[a-zA-Z]"));
    cmd.remove(re);
    cmd = cmd.trimmed();

    QStringList args = QProcess::splitCommand(cmd);
    if (args.isEmpty()) return false;

    QString prog = args.takeFirst();
    QProcess p;
    p.setProgram(prog);
    p.setArguments(args);
    p.setProcessEnvironment(ProcessUtils::cleanHostEnvironment());
    bool ok = p.startDetached();
    if (!ok && !s) {
        // Fallback: try KService with prog name if binary wasn't found directly
        s = KService::serviceByDesktopName(prog);
        if (s && s->isValid()) {
            QString sCmd = s->exec().remove(re).trimmed();
            QStringList sArgs = QProcess::splitCommand(sCmd);
            if (!sArgs.isEmpty()) {
                p.setProgram(sArgs.takeFirst());
                p.setArguments(sArgs);
                p.setProcessEnvironment(ProcessUtils::cleanHostEnvironment());
                ok = p.startDetached();
            }
        }
    }
    if (ok) {
        if (s && s->isValid()) {
            recordAppUsage(s->entryPath(), s->desktopEntryName());
        } else if (isAppImage(prog)) {
            AppEntry ai = resolveAppImageFromPath(prog);
            if (!ai.id.isEmpty()) {
                recordAppUsage(ai.desktopFile.isEmpty() ? ai.id : ai.desktopFile, ai.id);
            } else {
                recordAppUsage(prog, prog);
            }
        } else {
            recordAppUsage(prog, prog);
        }
    }
    return ok;
}
