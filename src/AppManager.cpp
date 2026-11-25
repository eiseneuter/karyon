#include "AppManager.h"
#include "DebugLogger.h"
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
#include <KSycoca>
#include <KDesktopFile>
#include <KConfigGroup>
#include <QDirIterator>
#include <QFileInfo>
#include <sqlite3.h>
#include <QUrl>
#include <QDBusInterface>
#include <QDBusReply>

bool AppManager::isAppImage(const QString &path)
{
    if (path.isEmpty()) return false;
    return path.endsWith(QLatin1String(".AppImage"), Qt::CaseInsensitive) ||
           path.endsWith(QLatin1String(".appimage"), Qt::CaseInsensitive);
}

AppEntry AppManager::entryFromDesktopFile(const QString &path)
{
    AppEntry e;
    if (path.isEmpty() || !path.endsWith(QLatin1String(".desktop"), Qt::CaseInsensitive) || !QFile::exists(path)) {
        return e;
    }
    // KDesktopFile statt QSettings: QSettings zerlegt Werte mit Komma/Semikolon in Listen
    // und liefert dann leere Namen/Befehle.
    KDesktopFile df(path);
    const KConfigGroup g = df.desktopGroup();
    const QString base = QFileInfo(path).completeBaseName();
    e.id = base;
    e.name = df.readName().trimmed();
    if (e.name.isEmpty()) e.name = base;
    e.icon = df.readIcon().trimmed();
    if (e.icon.isEmpty()) e.icon = base;
    e.exec = g.readEntry("Exec", QString()).trimmed();
    e.comment = df.readComment();
    e.desktopFile = path;
    e.hidden = df.noDisplay() || g.readEntry("Hidden", false);
    return e;
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

// Auflösungs-Caches. Alle werden von AppManager::invalidateCaches() geleert, damit ein
// einmaliger Fehlschlag (App noch nicht installiert, Desktop-Datei fehlte) nicht bis zum
// Neustart hängen bleibt.
static QHash<QString, AppEntry> s_steamGameCache;
static QHash<QString, qint64> s_steamMissCache;      // steamId -> Zeitpunkt des letzten Fehlschlags
static QHash<QString, AppEntry> s_execCache;
static QHash<QString, AppEntry> s_pidCache;          // "pid:startzeit" -> AppImage
static QHash<QString, AppEntry> s_findAppCache;
static QHash<QString, AppEntry> s_windowAppCache;

inline void idxInsert(DesktopIndex &idx, const QString &key, const QString &path)
{
    if (key.isEmpty()) return;
    // Exakte Dateinamen-Treffer aus dem Hauptverzeichnis dürfen nicht von später gelesenen
    // Unterordner-Einträgen (z.B. "GoldWave Help.desktop") überschrieben werden.
    if (!idx.keyToPath.contains(key)) idx.keyToPath.insert(key, path);
}

void buildDesktopIndex(DesktopIndex &idx)
{
    if (idx.initialized) return;
    idx.initialized = true;

    // XDG-Reihenfolge: Benutzerverzeichnis zuerst (überschreibt Systemeinträge), erster Treffer gewinnt.
    QStringList searchDirs = QStandardPaths::standardLocations(QStandardPaths::ApplicationsLocation);
    searchDirs.append(QStringLiteral("/usr/share/applications"));
    searchDirs.append(QStringLiteral("/var/lib/flatpak/exports/share/applications"));
    searchDirs.append(QDir::homePath() + QStringLiteral("/.local/share/flatpak/exports/share/applications"));
    searchDirs.removeDuplicates();

    // Versteckte Einträge (NoDisplay/Hidden, z.B. "…UrlHandler.desktop") erst im zweiten Durchgang:
    // teilt sich ein versteckter Eintrag Fensterklasse oder Programmname mit dem sichtbaren
    // Haupteintrag, muss der sichtbare gewinnen.
    auto isHiddenEntry = [](const QString &path) {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return false;
        bool inDesktopEntry = false;
        while (!f.atEnd()) {
            const QByteArray line = f.readLine().trimmed();
            if (line.startsWith('[')) {
                if (inDesktopEntry) break;
                inDesktopEntry = (line == "[Desktop Entry]");
                continue;
            }
            if (!inDesktopEntry) continue;
            if (line == "NoDisplay=true" || line == "Hidden=true") return true;
        }
        return false;
    };

    for (int pass = 0; pass < 2; ++pass)
    for (const auto &dirPath : searchDirs) {
        if (!QDir(dirPath).exists()) continue;
        // Rekursiv: Wine/Proton und andere legen ihre Einträge in Unterordnern ab
        // (z.B. applications/wine/Programs/<App>/<App>.desktop).
        QDirIterator it(dirPath, {QStringLiteral("*.desktop")}, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            it.next();
            const QFileInfo fi = it.fileInfo();
            QString path = fi.absoluteFilePath();
            if (isHiddenEntry(path) != (pass == 1)) continue;
            QString fileName = fi.fileName();
            QString baseName = fi.completeBaseName();

            QString fnLow = fileName.toLower();
            QString bnLow = baseName.toLower();
            idxInsert(idx, fnLow, path);
            idxInsert(idx, bnLow, path);

            QString norm = bnLow;
            norm.remove(QLatin1Char('_')).remove(QLatin1Char('-')).remove(QLatin1Char(' '));
            idxInsert(idx, norm, path);

            QFile f(path);
            if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
                bool inDesktopEntry = false;
                QString wmClass;
                QString nameVal;
                QString execVal;
                while (!f.atEnd()) {
                    QByteArray rawLine = f.readLine();
                    QByteArray line = rawLine.trimmed();
                    if (line.isEmpty() || line.startsWith('#')) continue;
                    if (line.startsWith('[')) {
                        inDesktopEntry = (line == "[Desktop Entry]");
                        continue;
                    }
                    if (!inDesktopEntry) continue;

                    if (wmClass.isEmpty() && line.startsWith("StartupWMClass=")) {
                        wmClass = QString::fromUtf8(line.mid(15)).trimmed();
                    } else if (nameVal.isEmpty() && line.startsWith("Name=")) {
                        nameVal = QString::fromUtf8(line.mid(5)).trimmed();
                    } else if (execVal.isEmpty() && line.startsWith("Exec=")) {
                        execVal = QString::fromUtf8(line.mid(5)).trimmed();
                    }
                    if (!wmClass.isEmpty() && !nameVal.isEmpty() && !execVal.isEmpty()) {
                        break;
                    }
                }

                if (!wmClass.isEmpty()) {
                    // Teilen sich mehrere Einträge eine Fensterklasse (Programm, Hilfe, Setup …),
                    // gewinnt der, dessen Dateiname zur Klasse passt ("goldwave.exe" -> GoldWave.desktop).
                    QString stem = wmClass.toLower();
                    if (stem.endsWith(QLatin1String(".exe"))) stem.chop(4);
                    const QString existing = idx.keyToPath.value(wmClass.toLower());
                    if (!existing.isEmpty() && bnLow == stem &&
                        QFileInfo(existing).completeBaseName().toLower() != stem) {
                        idx.keyToPath.insert(wmClass.toLower(), path);
                    }
                    idxInsert(idx, wmClass.toLower(), path);
                    QString wmNorm = wmClass.toLower().remove(QLatin1Char('_')).remove(QLatin1Char('-')).remove(QLatin1Char(' '));
                    idxInsert(idx, wmNorm, path);
                }
                if (!nameVal.isEmpty()) {
                    idxInsert(idx, nameVal.toLower(), path);
                }
                if (!execVal.isEmpty()) {
                    static const QRegularExpression rungameRe(QStringLiteral("steam://rungameid/(\\d+)"), QRegularExpression::CaseInsensitiveOption);
                    auto mRun = rungameRe.match(execVal);
                    if (mRun.hasMatch()) {
                        QString sId = mRun.captured(1);
                        idxInsert(idx, QStringLiteral("steam_app_") + sId, path);
                        idxInsert(idx, QStringLiteral("steam_app_") + sId + QStringLiteral(".desktop"), path);
                        idxInsert(idx, sId, path);
                    }

                    // Erstes echtes Programm der Exec-Zeile ("env", VAR=wert und Wrapper überspringen)
                    QString bin;
                    const QStringList execToks = QProcess::splitCommand(execVal);
                    for (const QString &tok : execToks) {
                        if (tok == QLatin1String("env") || tok.endsWith(QLatin1String("/env"))) continue;
                        if (tok.startsWith(QLatin1Char('-'))) continue;
                        if (tok.contains(QLatin1Char('=')) && !tok.startsWith(QLatin1Char('/'))) continue;
                        bin = tok;
                        break;
                    }
                    int slash = bin.lastIndexOf(QLatin1Char('/'));
                    if (slash >= 0) bin = bin.mid(slash + 1);
                    // Allgemeine Starter/Interpreter identifizieren keine App
                    static const QSet<QString> genericLaunchers = {
                        QStringLiteral("wine"), QStringLiteral("wine64"), QStringLiteral("python"),
                        QStringLiteral("python3"), QStringLiteral("bash"), QStringLiteral("sh"),
                        QStringLiteral("flatpak"), QStringLiteral("java"), QStringLiteral("electron"),
                        QStringLiteral("gtk-launch"), QStringLiteral("xdg-open"), QStringLiteral("snap"),
                        QStringLiteral("umu-run"), QStringLiteral("proton"), QStringLiteral("kioclient")
                    };
                    if (genericLaunchers.contains(bin.toLower()) ||
                        (bin.compare(QLatin1String("steam"), Qt::CaseInsensitive) == 0 && execVal.contains(QLatin1String("steam://")))) {
                        bin.clear();
                    }
                    if (!bin.isEmpty()) {
                        idxInsert(idx, bin.toLower(), path);
                        if (bin.endsWith(QLatin1String(".AppImage"), Qt::CaseInsensitive) ||
                            bin.endsWith(QLatin1String(".appimage"), Qt::CaseInsensitive)) {
                            QString binNoExt = bin.left(bin.length() - 9);
                            idxInsert(idx, binNoExt.toLower(), path);
                        }
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
    if (!name.endsWith(QLatin1String(".desktop"), Qt::CaseInsensitive) && QFile::exists(name + QStringLiteral(".desktop"))) {
        return name + QStringLiteral(".desktop");
    }
    if (QFile::exists(name) && name.endsWith(QLatin1String(".desktop"), Qt::CaseInsensitive)) return name;

    QString direct = findDesktopFilePath(name);
    if (!direct.isEmpty()) return direct;

    // Falls ein vollständiger Pfad übergeben wurde (z.B. /usr/share/applications/brave-browser),
    // auch den Dateinamen-Teil extrahieren und prüfen
    QString basePart = name.contains(QLatin1Char('/')) ? name.section(QLatin1Char('/'), -1) : name;
    if (basePart != name) {
        QString directBase = findDesktopFilePath(basePart);
        if (!directBase.isEmpty()) return directBase;
    }

    if (!s_desktopIndex.initialized) {
        buildDesktopIndex(s_desktopIndex);
    }

    for (const QString &candidate : {name, basePart}) {
        if (candidate.isEmpty()) continue;
        QString low = candidate.toLower();
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

void AppManager::invalidateCaches()
{
    s_desktopIndex.keyToPath.clear();
    s_desktopIndex.initialized = false;
    s_steamGameCache.clear();
    s_steamMissCache.clear();
    s_execCache.clear();
    s_pidCache.clear();
    s_findAppCache.clear();
    s_windowAppCache.clear();
}

// Liefert ein Steam-Spiel NUR, wenn Steam es wirklich kennt (appmanifest in einer Bibliothek)
// oder eine Desktop-Datei mit steam://rungameid/<id> existiert. Für alles andere
// (Proton/Wine-Spiele aus Heroic/Lutris mit Klasse steam_app_0, in Steam eingetragene
// Fremdspiele ohne Manifest) wird ein leerer Eintrag zurückgegeben – der Aufrufer nimmt
// dann Fenstertitel und Fenster-Icon. Es gibt keinen erfundenen Titel "Steam App <id>" mehr.
AppEntry AppManager::resolveSteamGame(const QString &steamId, const QString &captionFallback)
{
    Q_UNUSED(captionFallback);
    if (steamId.isEmpty() || steamId == QLatin1String("0")) return {};
    static const QRegularExpression digitsRe(QStringLiteral("^\\d+$"));
    if (!digitsRe.match(steamId).hasMatch()) return {};

    if (s_steamGameCache.contains(steamId)) {
        return s_steamGameCache.value(steamId);
    }
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    if (s_steamMissCache.contains(steamId) && (nowMs - s_steamMissCache.value(steamId)) < 30000) {
        return {};
    }

    QString foundName;
    QString foundIcon;

    // 1. Steam Library Paths durchsuchen
    QString home = QDir::homePath();
    QStringList libPaths = {
        home + QStringLiteral("/.local/share/Steam"),
        home + QStringLiteral("/.steam/steam"),
        home + QStringLiteral("/.steam/root"),
        home + QStringLiteral("/.var/app/com.valvesoftware.Steam/.local/share/Steam")
    };

    // libraryfolders.vdf auslesen
    for (const QString &base : QStringList(libPaths)) {
        QString vdfPath = base + QStringLiteral("/steamapps/libraryfolders.vdf");
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

    // appmanifest_<steamId>.acf auslesen für den offiziellen Spielnamen
    for (const QString &lib : libPaths) {
        QFile f(lib + QStringLiteral("/steamapps/appmanifest_") + steamId + QStringLiteral(".acf"));
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
        if (!foundName.isEmpty()) break;
    }

    // Desktop-Datei (von Steam angelegte Verknüpfung)
    QString resolvedDf = findDesktopFilePathCaseInsensitive(QStringLiteral("steam_app_") + steamId);
    AppEntry dfEntry = entryFromDesktopFile(resolvedDf);
    if (!dfEntry.exec.contains(QLatin1String("steam://rungameid/") + steamId)) {
        dfEntry = {};
        resolvedDf.clear();
    }
    if (foundName.isEmpty() && !dfEntry.name.isEmpty()) {
        foundName = dfEntry.name;
    }

    if (foundName.isEmpty()) {
        s_steamMissCache.insert(steamId, nowMs);
        return {};
    }

    // 2. Icon auflösen
    for (const char *size : {"256x256", "128x128", "96x96", "64x64", "48x48", "32x32"}) {
        QString ip = home + QStringLiteral("/.local/share/icons/hicolor/") + QLatin1String(size) +
                     QStringLiteral("/apps/steam_icon_") + steamId + QStringLiteral(".png");
        if (QFile::exists(ip)) {
            foundIcon = ip;
            break;
        }
    }
    if (foundIcon.isEmpty() && !dfEntry.icon.isEmpty() && dfEntry.icon != dfEntry.id) {
        foundIcon = dfEntry.icon;
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

// Startzeit eines Prozesses (Feld 22 in /proc/<pid>/stat). Zusammen mit der PID eindeutig,
// auch wenn die PID später neu vergeben wird.
static QString procIdentity(int pid)
{
    if (pid <= 0) return {};
    QFile f(QStringLiteral("/proc/%1/stat").arg(pid));
    if (!f.open(QIODevice::ReadOnly)) return QString::number(pid);
    QByteArray data = f.readAll();
    int closeParen = data.lastIndexOf(')');
    QList<QByteArray> parts = data.mid(closeParen + 2).split(' ');
    QByteArray start = parts.size() > 19 ? parts[19] : QByteArray();
    return QString::number(pid) + QLatin1Char(':') + QString::fromLatin1(start);
}

AppEntry AppManager::findAppByExec(const QString &execPath)
{
    if (execPath.isEmpty()) return {};

    if (s_execCache.contains(execPath)) {
        return s_execCache.value(execPath);
    }

    QFileInfo fi(execPath);
    QString baseName = fi.completeBaseName();
    QString fileName = fi.fileName();

    // Allgemeine Starter/Interpreter stehen in vielen Exec-Zeilen ("python3 /opt/x/main.py",
    // "wine …") – sie identifizieren keine bestimmte App.
    static const QRegularExpression genericRe(
        QStringLiteral("^(python[0-9.]*|bash|sh|dash|zsh|perl|ruby|node|java|wine(64)?(-preloader)?|wineserver|"
                       "electron[0-9]*|flatpak|snap|env|gtk-launch|xdg-open|steam|proton|umu-run)$"),
        QRegularExpression::CaseInsensitiveOption);
    if (genericRe.match(fileName).hasMatch()) {
        s_execCache.insert(execPath, {});
        return {};
    }

    // 1. Direkt über BaseName.desktop prüfen (z.B. JusticeViewer -> JusticeViewer.desktop)
    if (!baseName.isEmpty()) {
        AppEntry entry = entryFromDesktopFile(findDesktopFilePath(baseName));
        if (!entry.id.isEmpty() && !entry.exec.isEmpty()) {
            s_execCache.insert(execPath, entry);
            return entry;
        }
    }

    // 2. Desktop-Einträge, deren Exec-Zeile genau dieses Programm startet.
    //    Verglichen wird pro Argument (voller Pfad oder Dateiname), nicht per Teilwort –
    //    sonst würde "code" auch "codeblocks" treffen.
    QStringList searchDirs = QStandardPaths::standardLocations(QStandardPaths::ApplicationsLocation);
    searchDirs.append(QStringLiteral("/usr/share/applications"));
    searchDirs.append(QStringLiteral("/var/lib/flatpak/exports/share/applications"));
    searchDirs.append(QDir::homePath() + QStringLiteral("/.local/share/flatpak/exports/share/applications"));
    searchDirs.removeDuplicates();

    const bool hasDir = execPath.contains(QLatin1Char('/'));
    for (const auto &dirPath : searchDirs) {
        if (!QDir(dirPath).exists()) continue;
        QDirIterator it(dirPath, {QStringLiteral("*.desktop")}, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString fullPath = it.next();
            AppEntry entry = entryFromDesktopFile(fullPath);
            if (entry.exec.isEmpty()) continue;

            bool match = false;
            const QStringList toks = QProcess::splitCommand(entry.exec);
            for (const QString &tok : toks) {
                if (tok.startsWith(QLatin1Char('%'))) continue;
                if (tok == execPath) { match = true; break; }
                if (!fileName.isEmpty() && tok.contains(QLatin1Char('/')) &&
                    tok.section(QLatin1Char('/'), -1) == fileName &&
                    (!hasDir || QFileInfo(tok).canonicalFilePath() == fi.canonicalFilePath())) {
                    match = true;
                    break;
                }
                if (!tok.contains(QLatin1Char('/')) && tok == fileName) {
                    // Exec nennt nur den Programmnamen: über PATH auflösen und mit dem echten Pfad vergleichen
                    if (!hasDir) { match = true; break; }
                    const QString resolved = QStandardPaths::findExecutable(tok);
                    if (!resolved.isEmpty() && QFileInfo(resolved).canonicalFilePath() == fi.canonicalFilePath()) {
                        match = true;
                        break;
                    }
                }
            }
            if (match) {
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
                const AppEntry mEntry = entryFromDesktopFile(mountDir.filePath(dfs.first()));
                if (!mEntry.name.isEmpty()) entry.name = mEntry.name;
                entry.comment = mEntry.comment;
                if (!mEntry.icon.isEmpty() && mEntry.icon != mEntry.id && entry.icon == baseId) {
                    entry.icon = mEntry.icon;
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

    const QString cacheKey = procIdentity(pid);
    if (s_pidCache.contains(cacheKey)) {
        return s_pidCache.value(cacheKey);
    }
    auto cacheAndReturn = [&](const AppEntry &e) {
        s_pidCache.insert(cacheKey, e);
        return e;
    };

    QString appImagePath = readProcEnv(pid, "APPIMAGE");
    QString appDir = readProcEnv(pid, "APPDIR");
    if (appImagePath.isEmpty() || appDir.isEmpty()) {
        int ppid = readProcPpid(pid);
        if (ppid > 0) {
            if (appImagePath.isEmpty()) appImagePath = readProcEnv(ppid, "APPIMAGE");
            if (appDir.isEmpty()) appDir = readProcEnv(ppid, "APPDIR");
        }
    }

    // Karyon selbst niemals als AppImage eines fremden Fensters auflösen
    if (appImagePath.contains(QLatin1String("karyon"), Qt::CaseInsensitive)) {
        return cacheAndReturn({});
    }

    const QString exeTarget = QFile::symLinkTarget(QStringLiteral("/proc/%1/exe").arg(pid));

    // Ein Prozess gehört nur dann zu einer AppImage, wenn er nachweislich AUS ihr läuft.
    // Die Umgebungsvariablen allein reichen nicht (sie werden an Kindprozesse vererbt,
    // z.B. an ein aus einer AppImage gestartetes /usr/bin/steam). Gültige Nachweise:
    //   a) das Programm selbst liegt im gemounteten AppDir bzw. ist die AppImage-Datei
    //   b) ein System-Interpreter (python3, bash, ...) führt ein Skript aus dem AppDir aus
    bool isRealAppImage = false;
    const bool exeInMount = exeTarget.contains(QLatin1String("/.mount_")) ||
                            exeTarget.contains(QLatin1String("/appimage_extracted_")) ||
                            (!appDir.isEmpty() && exeTarget.startsWith(appDir));
    if (exeInMount || isAppImage(exeTarget)) {
        isRealAppImage = true;
    } else if (!appDir.isEmpty() && isAppImage(appImagePath) && QDir(appDir).exists()) {
        const QStringList args = readProcCmdline(pid);
        for (int i = 0; i < args.size() && i < 4; ++i) {
            if (args[i].startsWith(appDir + QLatin1Char('/'))) {
                isRealAppImage = true;
                break;
            }
        }
    }

    if (!isRealAppImage) {
        return cacheAndReturn({});
    }

    const QString aiPath = isAppImage(appImagePath) ? appImagePath : (isAppImage(exeTarget) ? exeTarget : QString());
    if (!aiPath.isEmpty()) {
        AppEntry entry = resolveAppImageFromPath(aiPath, pid);
        if (!entry.id.isEmpty()) {
            return cacheAndReturn(entry);
        }
    }

    return cacheAndReturn({});
}

// Gemeinsame Suche nach dem Desktop-Eintrag einer App.
// allowHidden=true: auch NoDisplay-Einträge (für Fenster-Icon/-Titel, wie Taskleiste und Alt+Tab).
static AppEntry findAppImpl(const QString &desktopFileName, const QString &resourceClass, bool allowHidden)
{
    const QString cacheKey = desktopFileName + QLatin1Char('|') + resourceClass + (allowHidden ? QLatin1String("|h") : QLatin1String("|v"));
    if (s_findAppCache.contains(cacheKey)) {
        return s_findAppCache.value(cacheKey);
    }

    auto cacheAndReturn = [&](const AppEntry &entry) -> AppEntry {
        s_findAppCache.insert(cacheKey, entry);
        return entry;
    };
    auto usable = [&](const AppEntry &e) {
        return !e.id.isEmpty() && !e.exec.trimmed().isEmpty() && (allowHidden || !e.hidden);
    };

    QString steamId;
    if (AppManager::isSteamApp(desktopFileName, resourceClass, &steamId)) {
        AppEntry steamGame = AppManager::resolveSteamGame(steamId);
        if (!steamGame.id.isEmpty()) return cacheAndReturn(steamGame);
    }

    if (AppManager::isAppImage(desktopFileName)) {
        AppEntry ai = AppManager::resolveAppImageFromPath(desktopFileName);
        if (usable(ai)) return cacheAndReturn(ai);
    }
    if (AppManager::isAppImage(resourceClass)) {
        AppEntry ai = AppManager::resolveAppImageFromPath(resourceClass);
        if (usable(ai)) return cacheAndReturn(ai);
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

    if (s && s->isValid() && !s->exec().trimmed().isEmpty() &&
        s->desktopEntryName() != QLatin1String("steam_app_0")) {
        AppEntry entry;
        entry.id = s->desktopEntryName();
        entry.name = s->name();
        entry.icon = s->icon();
        entry.exec = s->exec();
        entry.comment = s->comment();
        entry.desktopFile = s->entryPath();
        entry.hidden = s->noDisplay();
        if (usable(entry)) return cacheAndReturn(entry);
    }

    // Index: Dateiname, StartupWMClass, Name, Programmname (ohne Rücksicht auf Groß/Klein, -, _)
    for (const QString &key : {desktopFileName, resourceClass}) {
        if (key.isEmpty()) continue;
        const QString dfPath = AppManager::findDesktopFilePathCaseInsensitive(key);
        if (dfPath.isEmpty() || dfPath.endsWith(QLatin1String("steam_app_0.desktop"))) continue;
        AppEntry entry = AppManager::entryFromDesktopFile(dfPath);
        if (usable(entry)) return cacheAndReturn(entry);
    }

    for (const QString &key : {desktopFileName, resourceClass}) {
        AppEntry byExec = AppManager::findAppByExec(key);
        if (usable(byExec)) return cacheAndReturn(byExec);
    }

    return cacheAndReturn({});
}

AppEntry AppManager::findApp(const QString &desktopFileName, const QString &resourceClass)
{
    return findAppImpl(desktopFileName, resourceClass, false);
}

AppEntry AppManager::resolveWindowApp(const QString &desktopFileName, const QString &resourceClass, int pid, const QString &caption)
{
    bool isPlasmaWindowed = resourceClass.contains(QLatin1String("plasmawindowed"), Qt::CaseInsensitive) ||
                            desktopFileName.contains(QLatin1String("plasmawindowed"), Qt::CaseInsensitive);

    // WICHTIG: plasmawindowed ist ein Single-Instance-Daemon. Alle Plasmoid-Fenster laufen im
    // SELBEN Prozess mit derselben PID, haben aber unterschiedliche Titel/Captions.
    // Daher darf der Cache-Key für plasmawindowed NIEMALS die PID nutzen, sondern muss die Caption nutzen!
    QString cacheKey = isPlasmaWindowed
        ? QStringLiteral("pw|%1").arg(caption.trimmed().toLower())
        : QStringLiteral("%1|%2|%3").arg(desktopFileName, resourceClass, procIdentity(pid));

    if (s_windowAppCache.contains(cacheKey)) {
        return s_windowAppCache.value(cacheKey);
    }

    auto cacheAndReturn = [&](const AppEntry &entry) -> AppEntry {
        s_windowAppCache.insert(cacheKey, entry);
        return entry;
    };

    // 0. Plasma Windowed (System Tray Flyouts wie Lautstärke, Netzwerke, Bluetooth, Akku etc.)
    if (isPlasmaWindowed) {
        AppEntry pwEntry;
        pwEntry.id = QStringLiteral("plasmawindowed");
        pwEntry.name = caption.trimmed().isEmpty() ? QStringLiteral("Plasma Windowed") : caption.trimmed();
        pwEntry.exec = QStringLiteral("plasmawindowed");
        pwEntry.icon = QStringLiteral("plasma");

        QString pluginId;
        // 1. Zuerst aus caption auflösen (da PID/cmdline von plasmawindowed mit allen anderen Fenstern geteilt wird!)
        if (!caption.isEmpty()) {
            QString c = caption.toLower();
            if (c.contains(QStringLiteral("wetter")) || c.contains(QStringLiteral("weather"))) {
                pluginId = QStringLiteral("org.kde.plasma.weather");
            } else if (c.contains(QStringLiteral("lautst")) || c.contains(QStringLiteral("volume")) || c.contains(QStringLiteral("audio"))) {
                pluginId = QStringLiteral("org.kde.plasma.volume");
            } else if (c.contains(QStringLiteral("netz")) || c.contains(QStringLiteral("network")) || c.contains(QStringLiteral("wlan")) || c.contains(QStringLiteral("wifi"))) {
                pluginId = QStringLiteral("org.kde.plasma.networkmanagement");
            } else if (c.contains(QStringLiteral("blue"))) {
                pluginId = QStringLiteral("org.kde.plasma.bluetooth");
            } else if (c.contains(QStringLiteral("akku")) || c.contains(QStringLiteral("batt")) || c.contains(QStringLiteral("energie")) || c.contains(QStringLiteral("energy")) || c.contains(QStringLiteral("power"))) {
                pluginId = QStringLiteral("org.kde.plasma.battery");
            } else if (c.contains(QStringLiteral("hellig")) || c.contains(QStringLiteral("bright"))) {
                pluginId = QStringLiteral("org.kde.plasma.brightness");
            } else if (c.contains(QStringLiteral("druck")) || c.contains(QStringLiteral("print"))) {
                pluginId = QStringLiteral("org.kde.plasma.printmanager");
            } else if (c.contains(QStringLiteral("gerät")) || c.contains(QStringLiteral("device")) || c.contains(QStringLiteral("platte")) || c.contains(QStringLiteral("disk"))) {
                pluginId = QStringLiteral("org.kde.plasma.devicenotifier");
            } else if (c.contains(QStringLiteral("kamera")) || c.contains(QStringLiteral("camera"))) {
                pluginId = QStringLiteral("org.kde.plasma.cameraindicator");
            } else if (c.contains(QStringLiteral("anzeige")) || c.contains(QStringLiteral("bildschirm")) || c.contains(QStringLiteral("display")) || c.contains(QStringLiteral("screen"))) {
                pluginId = QStringLiteral("org.kde.kscreen");
            } else if (c.contains(QStringLiteral("zwischenablage")) || c.contains(QStringLiteral("clipboard")) || c.contains(QStringLiteral("klipper"))) {
                pluginId = QStringLiteral("org.kde.plasma.clipboard");
            }
        }

        // 2. Fallback auf cmdline (nur wenn caption keinen Treffer ergab)
        if (pluginId.isEmpty() && pid > 0) {
            QStringList cmdline = readProcCmdline(pid);
            for (int i = 1; i < cmdline.size(); ++i) {
                const QString &arg = cmdline[i].trimmed();
                if (arg.startsWith(QLatin1Char('-'))) continue;
                if (i > 1 && (cmdline[i - 1] == QLatin1String("-p") || cmdline[i - 1] == QLatin1String("--shell-plugin"))) {
                    continue;
                }
                pluginId = arg;
                break;
            }
        }

        if (pluginId == QLatin1String("org.kde.plasma.volume")) {
            pwEntry.icon = QStringLiteral("audio-volume-high");
        } else if (pluginId == QLatin1String("org.kde.plasma.networkmanagement")) {
            pwEntry.icon = QStringLiteral("network-wireless");
        } else if (pluginId == QLatin1String("org.kde.plasma.bluetooth")) {
            pwEntry.icon = QStringLiteral("preferences-system-bluetooth");
        } else if (pluginId == QLatin1String("org.kde.plasma.battery")) {
            pwEntry.icon = QStringLiteral("battery-charging");
        } else if (pluginId == QLatin1String("org.kde.plasma.brightness")) {
            pwEntry.icon = QStringLiteral("display-brightness");
        } else if (pluginId == QLatin1String("org.kde.plasma.printmanager")) {
            pwEntry.icon = QStringLiteral("printer");
        } else if (pluginId == QLatin1String("org.kde.plasma.weather")) {
            pwEntry.icon = QStringLiteral("weather-few-clouds");
        } else if (pluginId == QLatin1String("org.kde.plasma.devicenotifier")) {
            pwEntry.icon = QStringLiteral("drive-removable-media");
        } else if (pluginId == QLatin1String("org.kde.plasma.cameraindicator")) {
            pwEntry.icon = QStringLiteral("camera-web");
        } else if (pluginId == QLatin1String("org.kde.kscreen")) {
            pwEntry.icon = QStringLiteral("preferences-desktop-display");
        } else if (pluginId == QLatin1String("org.kde.plasma.clipboard")) {
            pwEntry.icon = QStringLiteral("klipper");
        }
        if (!pluginId.isEmpty()) {
            pwEntry.id = QStringLiteral("plasmawindowed:") + pluginId;
            pwEntry.exec = QStringLiteral("plasmawindowed ") + pluginId;
            pwEntry.desktopFile = QStringLiteral("plasmawindowed:") + pluginId;
            if (pwEntry.name.isEmpty() || pwEntry.name == QLatin1String("Plasma Windowed")) {
                if (pluginId == QLatin1String("org.kde.plasma.volume")) {
                    pwEntry.name = QStringLiteral("Audio Volume");
                } else if (pluginId == QLatin1String("org.kde.plasma.networkmanagement")) {
                    pwEntry.name = QStringLiteral("Networks");
                } else if (pluginId == QLatin1String("org.kde.plasma.bluetooth")) {
                    pwEntry.name = QStringLiteral("Bluetooth");
                } else if (pluginId == QLatin1String("org.kde.plasma.battery")) {
                    pwEntry.name = QStringLiteral("Power and Battery");
                } else if (pluginId == QLatin1String("org.kde.plasma.brightness")) {
                    pwEntry.name = QStringLiteral("Brightness");
                } else if (pluginId == QLatin1String("org.kde.plasma.printmanager")) {
                    pwEntry.name = QStringLiteral("Printers");
                } else if (pluginId == QLatin1String("org.kde.plasma.weather")) {
                    pwEntry.name = QStringLiteral("Weather Report");
                } else if (pluginId == QLatin1String("org.kde.plasma.devicenotifier")) {
                    pwEntry.name = QStringLiteral("Disks & Devices");
                } else if (pluginId == QLatin1String("org.kde.plasma.cameraindicator")) {
                    pwEntry.name = QStringLiteral("Camera");
                } else if (pluginId == QLatin1String("org.kde.kscreen")) {
                    pwEntry.name = QStringLiteral("Display Configuration");
                } else if (pluginId == QLatin1String("org.kde.plasma.clipboard")) {
                    pwEntry.name = QStringLiteral("Clipboard");
                }
            }
        }

        return cacheAndReturn(pwEntry);
    }

    // Reihenfolge wie bei der Taskleiste: Desktop-Eintrag des Fensters -> StartupWMClass ->
    // Programm des Prozesses -> sonst Fenstertitel + Fenster-Icon (Fallback-Eintrag).

    // 1. Steam-Spiel – nur wenn Steam die ID wirklich kennt (sonst leer, siehe resolveSteamGame)
    QString steamId;
    if (isSteamApp(desktopFileName, resourceClass, &steamId, pid)) {
        AppEntry steamGame = resolveSteamGame(steamId);
        if (!steamGame.id.isEmpty()) return cacheAndReturn(steamGame);
    }

    // Wine/Proton vergibt für alles ohne Steam-ID dieselbe Sammelklasse – sie identifiziert keine App.
    static const QRegularExpression genericClassRe(
        QStringLiteral("^(steam_app_(0|default)|steam_proton|wine|explorer\\.exe|)$"),
        QRegularExpression::CaseInsensitiveOption);
    const bool genericClass = genericClassRe.match(resourceClass).hasMatch() &&
                              (desktopFileName.isEmpty() || genericClassRe.match(desktopFileName).hasMatch());

    // 2. Desktop-Eintrag (Dateiname, StartupWMClass, Name, Programmname) – auch NoDisplay-Einträge
    if (!genericClass) {
        AppEntry app = findAppImpl(desktopFileName, resourceClass, false);
        if (app.id.isEmpty()) {
            app = findAppImpl(desktopFileName, resourceClass, true);
        }
        if (!app.id.isEmpty() && !app.name.isEmpty()) {
            return cacheAndReturn(app);
        }
    }

    // 3. AppImage: Prozess läuft nachweislich aus einer AppImage
    if (pid > 0) {
        AppEntry ai = resolveAppImageFromPid(pid);
        if (!ai.id.isEmpty()) {
            return cacheAndReturn(ai);
        }
    }

    // 4. Programm des Prozesses -> Desktop-Eintrag, der genau dieses Programm startet
    QString exeBase;
    if (pid > 0) {
        const QString exeTarget = QFile::symLinkTarget(QStringLiteral("/proc/%1/exe").arg(pid));
        static const QRegularExpression interpreterRe(
            QStringLiteral("^(python[0-9.]*|bash|sh|dash|zsh|perl|ruby|node|java|wine(64)?(-preloader)?|wineserver|electron[0-9]*|bwrap|srt-bwrap|pressure-vessel.*)$"));
        const QString exeName = exeTarget.section(QLatin1Char('/'), -1);
        if (!exeTarget.isEmpty() && !interpreterRe.match(exeName).hasMatch()) {
            exeBase = exeName;
            AppEntry byExec = findAppByExec(exeTarget);
            if (!byExec.id.isEmpty() && !byExec.exec.isEmpty()) {
                return cacheAndReturn(byExec);
            }
        } else if (!exeTarget.isEmpty()) {
            // Wine/Proton: Programmname aus der Befehlszeile (…\Spiel.exe)
            const QStringList args = readProcCmdline(pid);
            for (const QString &a : args) {
                if (a.endsWith(QLatin1String(".exe"), Qt::CaseInsensitive)) {
                    QString n = a;
                    n.replace(QLatin1Char('\\'), QLatin1Char('/'));
                    exeBase = n.section(QLatin1Char('/'), -1);
                    break;
                }
            }
        }
    }

    // 5. Kein Desktop-Eintrag: Fallback-Eintrag. Name = Fenstertitel, Icon kommt vom Fenster selbst
    //    (icon bleibt ein reiner Themen-Kandidat; der Aufrufer fragt bei Fehlschlag das Fenster-Icon ab).
    auto prettify = [](QString raw) {
        if (raw.endsWith(QLatin1String(".desktop"), Qt::CaseInsensitive)) raw.chop(8);
        int slash = raw.lastIndexOf(QLatin1Char('/'));
        if (slash >= 0 && slash + 1 < raw.length()) raw = raw.mid(slash + 1);
        // Programm-Endungen entfernen (Spiel.x86_64, spiel.exe, tool.AppImage)
        static const QRegularExpression binSuffixRe(
            QStringLiteral("\\.(x86_64|x86|x64|amd64|exe|bin|sh|py|appimage|run)$"),
            QRegularExpression::CaseInsensitiveOption);
        raw.remove(binSuffixRe);
        // Reverse-DNS (org.kde.dolphin) -> letzter Bestandteil
        static const QRegularExpression rdnsRe(QStringLiteral("^[a-z]{2,6}(\\.[A-Za-z0-9_-]+){2,}$"));
        if (rdnsRe.match(raw).hasMatch()) {
            raw = raw.section(QLatin1Char('.'), -1);
        }
        return raw;
    };

    QString rawKey;
    if (!genericClass) {
        rawKey = prettify(desktopFileName.isEmpty() ? resourceClass : desktopFileName);
    }
    if (rawKey.isEmpty() && !exeBase.isEmpty()) {
        rawKey = prettify(exeBase);
    }

    QString cleanCap = caption.trimmed();
    cleanCap.remove(QStringLiteral(" — Steam")).remove(QStringLiteral(" - Steam"));
    cleanCap = cleanCap.trimmed();

    AppEntry fallback;
    if (!rawKey.isEmpty()) {
        fallback.id = rawKey;
    } else if (!cleanCap.isEmpty()) {
        // Sammelklasse ohne erkennbares Programm: der Titel hält verschiedene Spiele auseinander
        fallback.id = QStringLiteral("window:") + cleanCap.toLower();
    } else {
        fallback.id = QStringLiteral("window");
    }

    if (!cleanCap.isEmpty()) {
        fallback.name = cleanCap;
    } else if (!rawKey.isEmpty()) {
        QString n = rawKey;
        n[0] = n[0].toUpper();
        fallback.name = n;
    } else {
        fallback.name = QStringLiteral("Window");
    }
    fallback.icon = genericClass ? rawKey : (resourceClass.isEmpty() ? desktopFileName : resourceClass);
    return cacheAndReturn(fallback);
}

bool AppManager::isBlacklistedRecentApp(const AppEntry &entry, int pid, const QString &caption)
{
    QString id = entry.id.toLower();
    QString name = entry.name.toLower();
    QString exec = entry.exec.toLower();
    QString df = entry.desktopFile.toLower();

    // 0. Karyon selbst sowie Tray-Flyouts niemals als Recent App aufnehmen!
    if (id.contains(QLatin1String("karyon")) ||
        exec.contains(QLatin1String("karyon")) ||
        df.contains(QLatin1String("karyon")) ||
        name.contains(QLatin1String("karyon")) ||
        id.contains(QLatin1String("plasmawindowed")) ||
        exec.contains(QLatin1String("plasmawindowed")) ||
        df.contains(QLatin1String("plasmawindowed"))) {
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

    // 5. Über eine Datei oder einen Link geöffnet? Solche Starts gehören nicht in Recent Apps
    //    (die Datei steht in Recent Files). Maßgeblich ist allein der Startbefehl im Vergleich
    //    zur Exec-Zeile des Desktop-Eintrags:
    //      - Hat Exec keinen Datei-Platzhalter (%f %F %u %U), KANN die App nicht über eine Datei
    //        geöffnet worden sein -> zählt immer.
    //      - Sonst gilt jedes zusätzliche Argument, das eine vorhandene Datei oder eine URL ist,
    //        als Datei-Start. Argumente, die schon fest in Exec stehen, zählen nicht.
    //    Fenstertitel werden nicht mehr ausgewertet (sie ändern sich während der Benutzung).
    Q_UNUSED(caption);
    bool isExemptApp = false;
    if (id.contains(QLatin1String("brave")) || id.contains(QLatin1String("firefox")) ||
        id.contains(QLatin1String("chrome")) || id.contains(QLatin1String("chromium")) ||
        id.contains(QLatin1String("vivaldi")) || id.contains(QLatin1String("opera")) ||
        id.contains(QLatin1String("microsoft-edge")) || id.contains(QLatin1String("tor-browser")) ||
        id.contains(QLatin1String("zen-browser")) || id.contains(QLatin1String("librewolf"))) {
        isExemptApp = true; // Browser: eine URL als Argument ist der Normalfall
    } else if (id == QLatin1String("org.kde.dolphin") || id == QLatin1String("dolphin") ||
               id.contains(QLatin1String("nemo")) || id.contains(QLatin1String("nautilus")) ||
               id.contains(QLatin1String("thunar")) || id.contains(QLatin1String("pcmanfm")) ||
               id.contains(QLatin1String("krusader"))) {
        isExemptApp = true; // Dateimanager
    } else if (id.startsWith(QLatin1String("steam_app_")) || id == QLatin1String("steam") ||
               exec.contains(QLatin1String("steam://rungameid/"))) {
        isExemptApp = true;
    }

    static const QRegularExpression fileCodeRe(QStringLiteral("%[fFuU]"));
    if (!isExemptApp && pid > 0 && entry.exec.contains(fileCodeRe)) {
        QSet<QString> fixedArgs;
        const QStringList execToks = QProcess::splitCommand(entry.exec);
        for (const QString &tok : execToks) {
            if (!tok.startsWith(QLatin1Char('%'))) fixedArgs.insert(tok);
        }

        const QString cwd = QFile::symLinkTarget(QStringLiteral("/proc/%1/cwd").arg(pid));
        const QStringList args = readProcCmdline(pid);
        for (int i = 1; i < args.size(); ++i) {
            const QString &arg = args[i];
            if (arg.isEmpty() || arg.startsWith(QLatin1Char('-')) || fixedArgs.contains(arg)) continue;

            static const QRegularExpression urlRe(QStringLiteral("^[a-zA-Z][a-zA-Z0-9+.-]*://"));
            if (urlRe.match(arg).hasMatch()) {
                return true;
            }

            QString path = arg;
            if (!path.startsWith(QLatin1Char('/')) && !cwd.isEmpty()) {
                path = cwd + QLatin1Char('/') + path;
            }
            // Dateien aus der eigenen Installation (AppImage-Mount, /usr, /opt) sind Programmteile
            if (path.startsWith(QLatin1String("/tmp/.mount_")) || path.startsWith(QLatin1String("/usr/")) ||
                path.startsWith(QLatin1String("/opt/")) || path.startsWith(QLatin1String("/app/"))) {
                continue;
            }
            QFileInfo fi(path);
            if (fi.exists() && fi.isFile()) {
                return true;
            }
        }
    }

    return false;
}

struct RawRecentAppsCache {
    QList<AppEntry> entries;
    qint64 timestampMs = 0;
};
static RawRecentAppsCache s_rawRecentApps;

struct RawFavoritesCache {
    QList<AppEntry> entries;
    qint64 timestampMs = 0;
    QString sourceStamp; // mtimes der Quelldateien -> sofortige Invalidierung bei Änderungen
};

static QString kdeConfigPath(const QString &name)
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QLatin1Char('/') + name;
}

static QString favoritesSourceStamp()
{
    QString stamp;
    const QStringList files = {
        kdeConfigPath(QStringLiteral("kactivitymanagerd-statsrc")),
        kdeConfigPath(QStringLiteral("plasma-org.kde.plasma.desktop-appletsrc")),
        QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/kactivitymanagerd/resources/database"),
        QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/kactivitymanagerd/resources/database-wal")
    };
    for (const auto &f : files) {
        QFileInfo fi(f);
        stamp += QString::number(fi.exists() ? fi.lastModified().toMSecsSinceEpoch() : 0) + QLatin1Char(';');
    }
    return stamp;
}

// Liefert die Applet-IDs aller Startmenü-Applets (Kickoff / Kicker / Kickerdash),
// die aktuell wirklich in einem Plasma-Panel bzw. Desktop existieren.
// kactivitymanagerd-statsrc behält Sektionen gelöschter Applets für immer –
// ohne diesen Abgleich würden veraltete Favoritenlisten verwendet.
static QSet<QString> activeLauncherAppletIds()
{
    QSet<QString> ids;
    QFile f(kdeConfigPath(QStringLiteral("plasma-org.kde.plasma.desktop-appletsrc")));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return ids;

    static const QRegularExpression appletGroupRe(
        QStringLiteral(R"(^\[Containments\]\[\d+\]\[Applets\]\[(\d+)\]$)"));
    QString currentApplet;
    while (!f.atEnd()) {
        const QString line = QString::fromUtf8(f.readLine()).trimmed();
        if (line.startsWith(QLatin1Char('['))) {
            auto m = appletGroupRe.match(line);
            currentApplet = m.hasMatch() ? m.captured(1) : QString();
            continue;
        }
        if (currentApplet.isEmpty() || !line.startsWith(QLatin1String("plugin="))) continue;
        const QString plugin = line.mid(7).trimmed();
        if (plugin == QLatin1String("org.kde.plasma.kickoff") ||
            plugin == QLatin1String("org.kde.plasma.kicker") ||
            plugin == QLatin1String("org.kde.plasma.kickerdash")) {
            ids.insert(currentApplet);
        }
    }
    return ids;
}
static RawFavoritesCache s_rawFavorites;

void AppManager::recordAppUsage(const QString &desktopFileName, const QString &resourceClass, int pid, const QString &caption)
{
    s_rawRecentApps.timestampMs = 0;
    s_rawFavorites.timestampMs = 0;
    if (resourceClass == QLatin1String("karyon") || 
        resourceClass.contains(QLatin1String("plasmawindowed")) ||
        resourceClass == QLatin1String("plasmashell") ||
        resourceClass == QLatin1String("org.kde.plasmashell") ||
        resourceClass == QLatin1String("krunner")) {
        return;
    }
    if (desktopFileName.isEmpty() && resourceClass.isEmpty() && pid <= 0) return;

    AppEntry app = resolveWindowApp(desktopFileName, resourceClass, pid, caption);
    // Ohne Startbefehl (Fenster ohne Desktop-Eintrag) oder mit NoDisplay kann/soll nichts gestartet werden
    if (app.id.isEmpty() || app.hidden || app.exec.trimmed().isEmpty() || isBlacklistedRecentApp(app, pid, caption)) {
        return;
    }

    static QString s_lastRecordedId;
    static qint64 s_lastRecordedTime = 0;
    qint64 now = QDateTime::currentSecsSinceEpoch();

    if (s_lastRecordedId == app.id && (now - s_lastRecordedTime) < 20) {
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

static QList<AppEntry> getRawRecentApps()
{
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (!s_rawRecentApps.entries.isEmpty() && (now - s_rawRecentApps.timestampMs) < 15000) {
        return s_rawRecentApps.entries;
    }

    QList<AppEntry> rawList;
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

    // 1. First, load from our tracked recent_apps.json
    QString jsonPath = AppManager::recentAppsFilePath();
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

                if (AppManager::isBlacklistedRecentApp(entry)) {
                    jsonNeedsCleanup = true;
                    continue;
                }

                QString sId = extractSteamId(entry);
                if (!sId.isEmpty()) {
                    AppEntry canonical = AppManager::resolveSteamGame(sId, entry.name);
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

                if (entry.id.isEmpty() || entry.exec.trimmed().isEmpty()) continue;

                // Validate that app still exists on system
                if (!entry.desktopFile.isEmpty() && !QFile::exists(entry.desktopFile)) {
                    if (sId.isEmpty() && !entry.id.startsWith(QLatin1String("steam_app_"))) {
                        KService::Ptr s = KService::serviceByDesktopName(entry.id);
                        if (!s || !s->isValid()) continue;
                    }
                }

                markSeen(entry, sId);
                rawList.append(entry);
                if (rawList.size() >= 50) break;
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

    // 2. Nur beim allerersten Start (noch keine eigene Liste): aus der KDE-Statistik vorbelegen.
    //    Danach zählt ausschließlich, was Karyon selbst als Programmstart beobachtet hat –
    //    die KDE-Statistik unterscheidet nicht zwischen Programmstart und "Datei geöffnet mit".
    if (rawList.isEmpty()) {
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
                while (sqlite3_step(stmt) == SQLITE_ROW && rawList.size() < 50) {
                    const char *desktopFile = (const char *)sqlite3_column_text(stmt, 0);
                    if (!desktopFile) continue;
                    QString df = QString::fromUtf8(desktopFile);

                    // Filter irrelevant steam dummies or helpers
                    if (df == QLatin1String("steam_app_0.desktop") || df == QLatin1String("steam_app_0")) {
                        continue;
                    }

                    QString sid;
                    if (AppManager::isSteamApp(df, QString(), &sid) || df.startsWith(QLatin1String("steam_app_"))) {
                        if (sid.isEmpty()) {
                            sid = df.mid(10);
                            if (sid.endsWith(QLatin1String(".desktop"))) sid.chop(8);
                        }
                        AppEntry steamEntry = AppManager::resolveSteamGame(sid);
                        if (!steamEntry.id.isEmpty()) {
                            if (isDuplicate(steamEntry, sid)) continue;
                            markSeen(steamEntry, sid);
                            seenIds.insert(df.toLower());
                            rawList.append(steamEntry);
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
                        AppEntry steamEntry = AppManager::resolveSteamGame(entrySteamId, entry.name);
                        if (!steamEntry.id.isEmpty()) {
                            entry = steamEntry;
                        }
                    }

                    if (isDuplicate(entry, entrySteamId)) continue;
                    if (AppManager::isBlacklistedRecentApp(entry)) continue;

                    markSeen(entry, entrySteamId);
                    seenIds.insert(df.toLower());
                    rawList.append(entry);
                }
                sqlite3_finalize(stmt);
            }
            sqlite3_close(db);
        }
    }

    s_rawRecentApps.entries = rawList;
    s_rawRecentApps.timestampMs = now;
    return rawList;
}

QList<AppEntry> AppManager::loadRecentApps(int maxCount, const QSet<QString> &excludeAppKeys)
{
    QList<AppEntry> raw = getRawRecentApps();
    QList<AppEntry> result;

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

    for (const auto &entry : raw) {
        if (result.size() >= maxCount) break;
        if (isExcluded(entry)) continue;
        result.append(entry);
    }

    return result;
}

QList<CategoryEntry> AppManager::loadCategories()
{
    QList<CategoryEntry> categories;

    // Sicherstellen, dass die KDE-Menüdatenbank (sycoca) aktuell ist – sonst bleiben
    // neu installierte / entfernte Apps bis zum Neustart von Karyon unsichtbar.
    KSycoca::self()->ensureCacheValid();

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
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    const QString sourceStamp = favoritesSourceStamp();
    if (s_rawFavorites.timestampMs > 0 && (now - s_rawFavorites.timestampMs) < 15000 &&
        s_rawFavorites.sourceStamp == sourceStamp) {
        return s_rawFavorites.entries;
    }

    // Menü-Datenbank (sycoca) aktualisieren, damit neu installierte Apps auflösbar sind
    KSycoca::self()->ensureCacheValid();

    QList<AppEntry> result;
    QSet<QString> seen;
    QStringList orderedKeys;

    // 1. Current KDE Activity via D-Bus ermitteln (falls verfügbar)
    QString currentActivity;
    QDBusInterface actInterface(QStringLiteral("org.kde.ActivityManager"),
                                QStringLiteral("/ActivityManager/Activities"),
                                QStringLiteral("org.kde.ActivityManager.Activities"),
                                QDBusConnection::sessionBus());
    if (actInterface.isValid()) {
        QDBusReply<QString> reply = actInterface.call(QStringLiteral("CurrentActivity"));
        if (reply.isValid()) {
            currentActivity = reply.value().trimmed();
        }
    }

    // 2. kactivitymanagerd-statsrc auslesen (enthält die exakte, vom Nutzer festgelegte Reihenfolge!)
    QStringList orderingRaw;
    QString statsPath = kdeConfigPath(QStringLiteral("kactivitymanagerd-statsrc"));
    if (QFile::exists(statsPath)) {
        QSettings stats(statsPath, QSettings::IniFormat);
        QStringList bestOrdering;
        qint64 bestScore = -1;
        const QSet<QString> activeApplets = activeLauncherAppletIds();

        static const QRegularExpression instanceRegex(QStringLiteral(R"(instance-(\d+))"));

        for (const auto &group : stats.childGroups()) {
            if (!group.contains(QLatin1String("favorite"), Qt::CaseInsensitive)) {
                continue;
            }

            stats.beginGroup(group);
            QVariant val = stats.value(QStringLiteral("ordering"));
            QStringList items = val.toStringList();
            if (items.isEmpty()) {
                QString s = val.toString();
                if (!s.isEmpty()) items = s.split(QLatin1Char(','), Qt::SkipEmptyParts);
            }
            stats.endGroup();

            if (items.isEmpty()) {
                continue;
            }

            // Scoring:
            // 1. Aktivitäts-Übereinstimmung (aktuelle Aktivität > global > andere Aktivität)
            // 2. Höchste Instanz-ID (in Plasma erhalten neuere Launcher-Applets höhere IDs)
            qint64 score = 0;
            if (!currentActivity.isEmpty() && group.contains(currentActivity)) {
                score += 10000000;
            } else if (group.contains(QLatin1String("global"), Qt::CaseInsensitive)) {
                score += 1000000;
            } else if (!currentActivity.isEmpty() && group.contains(QLatin1Char('-'))) {
                // Sektion gehört explizit zu einer ANDEREN Aktivität -> überspringen
                continue;
            }

            auto match = instanceRegex.match(group);
            if (match.hasMatch()) {
                // Sektionen gelöschter Startmenü-Applets ignorieren (die höchste ID ist
                // oft ein längst entferntes Applet mit veralteter Liste)
                if (!activeApplets.isEmpty() && !activeApplets.contains(match.captured(1))) {
                    continue;
                }
                score += match.captured(1).toLongLong();
            }

            if (score > bestScore) {
                bestScore = score;
                bestOrdering = items;
            }
        }

        orderingRaw = bestOrdering;
    }

    auto normalizeKey = [](const QString &item) -> QString {
        QString clean = item.trimmed();
        if (clean.startsWith(QLatin1String("applications:"))) {
            clean = clean.mid(13);
        } else if (clean.startsWith(QLatin1String("file://"))) {
            clean = QUrl(clean).toLocalFile();
        } else if (clean.startsWith(QLatin1String("preferred://"))) {
            QString pref = clean.mid(12).toLower();
            if (pref == QLatin1String("browser")) {
                clean = QStringLiteral("brave-browser.desktop");
            }
        }
        return clean;
    };

    // 3. Die eigentliche Favoriten-MENGE steht in der kactivitymanagerd-Datenbank (ResourceLink).
    // Plasma 6 (Kickoff/Kicker/Kickerdash) verlinkt Favoriten über den gemeinsamen Agent
    // "org.kde.plasma.favorites.applications" – statsrc enthält nur die REIHENFOLGE und wird
    // beim Hinzufügen/Entfernen nicht zuverlässig nachgeführt. Daher:
    //   Menge = ResourceLink (global + aktuelle Aktivität), Reihenfolge = statsrc, Rest hinten an.
    QStringList linkedRaw;
    bool linkedQueryOk = false;
    {
        QString dbPath = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
                         + QStringLiteral("/kactivitymanagerd/resources/database");
        sqlite3 *db = nullptr;
        if (QFile::exists(dbPath) &&
            sqlite3_open_v2(dbPath.toUtf8().constData(), &db, SQLITE_OPEN_READONLY, nullptr) == SQLITE_OK) {
            sqlite3_busy_timeout(db, 200);
            const char *sql = "SELECT targettedResource FROM ResourceLink "
                              "WHERE initiatingAgent = 'org.kde.plasma.favorites.applications' "
                              "  AND (usedActivity = ':global' OR usedActivity = ':any' OR usedActivity = ?1) "
                              "ORDER BY rowid;";
            sqlite3_stmt *stmt = nullptr;
            if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
                const QByteArray act = currentActivity.toUtf8();
                sqlite3_bind_text(stmt, 1, act.constData(), act.size(), SQLITE_TRANSIENT);
                int rc;
                while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
                    const char *resText = (const char *)sqlite3_column_text(stmt, 0);
                    if (resText) linkedRaw.append(QString::fromUtf8(resText));
                }
                linkedQueryOk = (rc == SQLITE_DONE);
                sqlite3_finalize(stmt);
            }
        }
        if (db) sqlite3_close(db);
    }

    if (linkedQueryOk && !linkedRaw.isEmpty()) {
        QSet<QString> linkedLower;
        for (const auto &r : linkedRaw) linkedLower.insert(normalizeKey(r).toLower());
        QSet<QString> seenLower;
        // a) in der vom Nutzer festgelegten Reihenfolge – aber nur, was noch verlinkt ist
        for (const auto &item : orderingRaw) {
            QString key = normalizeKey(item);
            if (key.isEmpty() || !linkedLower.contains(key.toLower()) || seenLower.contains(key.toLower())) continue;
            seenLower.insert(key.toLower());
            orderedKeys.append(key);
        }
        // b) neu hinzugefügte Favoriten, die (noch) nicht in der Reihenfolge stehen
        for (const auto &r : linkedRaw) {
            QString key = normalizeKey(r);
            if (key.isEmpty() || seenLower.contains(key.toLower())) continue;
            seenLower.insert(key.toLower());
            orderedKeys.append(key);
        }
    } else {
        // Fallback (ältere Plasma-Versionen / DB nicht lesbar): nur statsrc-Reihenfolge
        for (const auto &item : orderingRaw) {
            QString key = normalizeKey(item);
            if (!key.isEmpty() && !seen.contains(key)) {
                seen.insert(key);
                orderedKeys.append(key);
            }
        }
    }

    // 3. Desktop-Dateien / Services auflösen
    for (const auto &df : orderedKeys) {
        KService::Ptr s = KService::serviceByDesktopName(df);
        if (!s && df.endsWith(QLatin1String(".desktop"))) {
            QString stripped = df;
            stripped.chop(8);
            s = KService::serviceByDesktopName(stripped);
        }
        if (!s) {
            s = KService::serviceByStorageId(df);
        }

        AppEntry entry;
        if (s && s->isValid() && !s->noDisplay() && !s->exec().trimmed().isEmpty()) {
            entry.id = s->desktopEntryName();
            entry.name = s->name();
            entry.icon = s->icon();
            entry.exec = s->exec();
            entry.comment = s->comment();
            entry.desktopFile = s->entryPath();
        } else {
            // Fallback auf findApp (unterstützt Flatpaks, AppImages, direkte Pfade)
            entry = findApp(df, df);
        }

        if (!entry.id.isEmpty() && !entry.name.isEmpty() && !entry.exec.trimmed().isEmpty()) {
            result.append(entry);
        }
    }

    s_rawFavorites.entries = result;
    s_rawFavorites.timestampMs = now;
    s_rawFavorites.sourceStamp = sourceStamp;
    return result;
}

bool AppManager::launchApp(const QString &execLine)
{
    if (execLine.trimmed().isEmpty()) return false;
    QString cmd = execLine.trimmed();

    if (cmd.startsWith(QLatin1String("app:"))) {
        cmd = cmd.mid(4).trimmed();
    }

    // Falls QSettings (IniFormat) oder eine .desktop-Datei den gesamten Befehl in äußere
    // Anführungszeichen gehüllt hat (z.B. "env LD_PRELOAD=... /usr/bin/dolphin"), diese entfernen,
    // sofern es kein einzelner existierender Dateipfad mit Leerzeichen ist.
    if (cmd.length() >= 2 &&
        ((cmd.startsWith(QLatin1Char('"')) && cmd.endsWith(QLatin1Char('"')) && cmd.count(QLatin1Char('"')) == 2) ||
         (cmd.startsWith(QLatin1Char('\'')) && cmd.endsWith(QLatin1Char('\'')) && cmd.count(QLatin1Char('\'')) == 2))) {
        QString unquoted = cmd.mid(1, cmd.length() - 2).trimmed();
        if (!QFile::exists(unquoted)) {
            cmd = unquoted;
        }
    }

    // Prüfen, ob cmd eine .desktop-Datei, ein Applications-Pfad (auch ohne .desktop-Endung) oder ein Service-Name ist
    KService::Ptr s;
    QString resolvedDfPath;
    if (cmd.endsWith(QLatin1String(".desktop"), Qt::CaseInsensitive) ||
        cmd.contains(QLatin1String("/applications/"), Qt::CaseInsensitive)) {
        resolvedDfPath = findDesktopFilePathCaseInsensitive(cmd);
        s = KService::serviceByDesktopPath(resolvedDfPath.isEmpty() ? cmd : resolvedDfPath);
        if (!s) {
            QString stripped = (resolvedDfPath.isEmpty() ? cmd : resolvedDfPath).section(QLatin1Char('/'), -1);
            if (stripped.endsWith(QLatin1String(".desktop"), Qt::CaseInsensitive)) {
                stripped.chop(8);
            }
            s = KService::serviceByDesktopName(stripped);
        }
    } else if (!cmd.contains(QLatin1Char(' '))) {
        s = KService::serviceByDesktopName(cmd);
        if (!s && (!cmd.startsWith(QLatin1Char('/')) || !QFile::exists(cmd))) {
            resolvedDfPath = findDesktopFilePathCaseInsensitive(cmd);
            if (!resolvedDfPath.isEmpty()) {
                s = KService::serviceByDesktopPath(resolvedDfPath);
            }
        }
    }

    if (s && s->isValid() && !s->exec().trimmed().isEmpty()) {
        cmd = s->exec().trimmed();
    } else if (!resolvedDfPath.isEmpty() && QFile::exists(resolvedDfPath)) {
        QSettings ini(resolvedDfPath, QSettings::IniFormat);
        ini.beginGroup(QStringLiteral("Desktop Entry"));
        QString dfExec = ini.value(QStringLiteral("Exec")).toString().trimmed();
        if (!dfExec.isEmpty()) {
            cmd = dfExec;
        }
    }

    static const QRegularExpression re(QStringLiteral("%[a-zA-Z]"));
    cmd.remove(re);
    cmd = cmd.trimmed();

    QStringList args = QProcess::splitCommand(cmd);
    if (args.isEmpty()) return false;

    // Falls der Befehl mit Umgebungsvariablen ohne vorangestelltes "env" beginnt (z.B. FOO=1 /usr/bin/app)
    if (args.first().contains(QLatin1Char('=')) && !QFile::exists(args.first())) {
        args.prepend(QStringLiteral("env"));
    }

    QString prog = args.takeFirst();
    QProcess p;
    p.setProgram(prog);
    p.setArguments(args);
    ProcessUtils::prepareDetached(p);
    bool ok = p.startDetached();
    if (!ok) {
        // Allgemeingültiger Fallback: Über AppManager::findApp nach Desktop-Eintrag / Binary suchen
        AppEntry fallbackApp = findApp(prog, prog.section(QLatin1Char('/'), -1));
        if (!fallbackApp.exec.trimmed().isEmpty()) {
            QString sCmd = fallbackApp.exec;
            sCmd.remove(re);
            sCmd = sCmd.trimmed();
            QStringList sArgs = QProcess::splitCommand(sCmd);
            if (!sArgs.isEmpty()) {
                if (sArgs.first().contains(QLatin1Char('=')) && !QFile::exists(sArgs.first())) {
                    sArgs.prepend(QStringLiteral("env"));
                }
                prog = sArgs.takeFirst();
                p.setProgram(prog);
                p.setArguments(sArgs);
                ProcessUtils::prepareDetached(p);
                ok = p.startDetached();
            }
        }
    }
    if (ok) {
        if (s && s->isValid()) {
            recordAppUsage(s->entryPath(), s->desktopEntryName());
        } else if (!resolvedDfPath.isEmpty()) {
            QString base = resolvedDfPath.section(QLatin1Char('/'), -1);
            if (base.endsWith(QLatin1String(".desktop"), Qt::CaseInsensitive)) base.chop(8);
            recordAppUsage(resolvedDfPath, base);
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
