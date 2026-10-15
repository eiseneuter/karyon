#include "MediaManager.h"
#include "KWinBridge.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusReply>
#include <QDBusArgument>
#include <QVariantMap>
#include <QStringList>
#include <QSet>
#include <QUrl>
#include <QFileInfo>
#include <QRegularExpression>
#include <QDateTime>

static QString stripSiteSuffix(const QString &title, const QString &siteHint = QString())
{
    QString t = title.trimmed();
    if (t.isEmpty()) {
        return t;
    }

    static const QStringList defaultSites = {
        QStringLiteral("YouTube"), QStringLiteral("Youtube"), QStringLiteral("YouTube Music"), QStringLiteral("Youtube Music"),
        QStringLiteral("SoundCloud"), QStringLiteral("Soundcloud"),
        QStringLiteral("Twitch"), QStringLiteral("Vimeo"), QStringLiteral("Spotify"), QStringLiteral("Netflix"),
        QStringLiteral("Dailymotion"), QStringLiteral("Bandcamp"), QStringLiteral("Mixcloud"),
        QStringLiteral("Deezer"), QStringLiteral("Apple Music"), QStringLiteral("Amazon Music"),
        QStringLiteral("Facebook"), QStringLiteral("Instagram"), QStringLiteral("TikTok"),
        QStringLiteral("Reddit"), QStringLiteral("Twitter"), QStringLiteral("X")
    };

    QStringList sites = defaultSites;
    if (!siteHint.isEmpty()) {
        QString s = siteHint.trimmed();
        if (s.startsWith(QLatin1String("http://"), Qt::CaseInsensitive) ||
            s.startsWith(QLatin1String("https://"), Qt::CaseInsensitive)) {
            QUrl u(s);
            QString host = u.host();
            if (host.startsWith(QLatin1String("www."))) {
                host = host.mid(4);
            }
            int dot = host.indexOf(QLatin1Char('.'));
            if (dot > 0) {
                host = host.left(dot);
            }
            s = host;
        }
        if (!s.isEmpty() && !sites.contains(s, Qt::CaseInsensitive)) {
            sites.append(s);
        }
    }

    QString pattern = QStringLiteral(R"(\s*[-–—|•·:]\s*(?:%1)\s*$)").arg(sites.join(QLatin1Char('|')));
    QRegularExpression re(pattern, QRegularExpression::CaseInsensitiveOption);
    QString stripped = t;
    stripped.replace(re, QString());
    stripped = stripped.trimmed();

    return stripped.isEmpty() ? t : stripped;
}

static QString sanitizeMediaTitle(const QString &rawTitle, const QString &rawUrl)
{
    QString t = rawTitle.trimmed();
    if (t.isEmpty()) {
        t = rawUrl.trimmed();
    }
    if (t.isEmpty()) {
        return QString();
    }

    // Check for file:// URI
    if (t.startsWith(QLatin1String("file://"), Qt::CaseInsensitive)) {
        QUrl u(t);
        QString local = u.toLocalFile();
        if (local.isEmpty()) {
            local = t.mid(7);
        }
        QString fn = QFileInfo(local).fileName();
        fn = QUrl::fromPercentEncoding(fn.toUtf8());
        return fn.isEmpty() ? local : fn;
    }

    // Check for absolute local path
    if (t.startsWith(QLatin1Char('/'))) {
        QString fn = QFileInfo(t).fileName();
        fn = QUrl::fromPercentEncoding(fn.toUtf8());
        return fn.isEmpty() ? t : fn;
    }

    // Check for web URL (http:// or https://)
    if (t.startsWith(QLatin1String("http://"), Qt::CaseInsensitive) ||
        t.startsWith(QLatin1String("https://"), Qt::CaseInsensitive)) {
        QUrl u(t);
        QString path = u.path();
        QString fn = QFileInfo(path).fileName();
        fn = QUrl::fromPercentEncoding(fn.toUtf8());
        if (!fn.isEmpty() && fn != QLatin1String("watch") && fn != QLatin1String("index.html") && fn != QLatin1String("index.php")) {
            return fn;
        }
        return u.host().isEmpty() ? t : u.host();
    }

    return t;
}

MediaManager::MediaManager(QObject *parent)
    : QObject(parent)
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (bus.isConnected()) {
        bus.connect(
            QString(),
            QStringLiteral("/org/mpris/MediaPlayer2"),
            QStringLiteral("org.freedesktop.DBus.Properties"),
            QStringLiteral("PropertiesChanged"),
            this,
            SLOT(onMprisPropertiesChanged(QString, QVariantMap, QStringList, QDBusMessage))
        );

        bus.connect(
            QStringLiteral("org.freedesktop.DBus"),
            QStringLiteral("/org/freedesktop/DBus"),
            QStringLiteral("org.freedesktop.DBus"),
            QStringLiteral("NameOwnerChanged"),
            this,
            SLOT(onNameOwnerChanged(QString, QString, QString))
        );
    }
}

void MediaManager::onMprisPropertiesChanged(const QString &interfaceName, const QVariantMap &changedProps,
                                            const QStringList &invalidatedProps, const QDBusMessage &msg)
{
    Q_UNUSED(invalidatedProps);
    if (interfaceName != QLatin1String("org.mpris.MediaPlayer2.Player")) {
        return;
    }

    QString sender = msg.service();
    QString wellKnown = m_uniqueToWellKnown.value(sender, sender);
    qint64 now = QDateTime::currentMSecsSinceEpoch();

    if (changedProps.contains(QLatin1String("PlaybackStatus"))) {
        QString status = changedProps.value(QLatin1String("PlaybackStatus")).toString();
        if (status == QLatin1String("Playing")) {
            m_history[wellKnown].lastPlayingTime = now;
            m_history[wellKnown].lastActionTime = now;
            m_history[wellKnown].isCurrentlyPlaying = true;
            m_history[wellKnown].serviceName = wellKnown;
            if (!m_history[wellKnown].appName.isEmpty()) {
                m_appHistory[m_history[wellKnown].appName.toLower()].lastPlayingTime = now;
                m_appHistory[m_history[wellKnown].appName.toLower()].lastActionTime = now;
                m_appHistory[m_history[wellKnown].appName.toLower()].isCurrentlyPlaying = true;
            }
        } else if (status == QLatin1String("Paused")) {
            m_history[wellKnown].lastPausedTime = now;
            m_history[wellKnown].lastActionTime = now;
            m_history[wellKnown].isCurrentlyPlaying = false;
            m_history[wellKnown].serviceName = wellKnown;
            if (!m_history[wellKnown].appName.isEmpty()) {
                m_appHistory[m_history[wellKnown].appName.toLower()].lastPausedTime = now;
                m_appHistory[m_history[wellKnown].appName.toLower()].lastActionTime = now;
                m_appHistory[m_history[wellKnown].appName.toLower()].isCurrentlyPlaying = false;
            }
        }
    }
}

void MediaManager::onNameOwnerChanged(const QString &serviceName, const QString &oldOwner, const QString &newOwner)
{
    if (!serviceName.startsWith(QLatin1String("org.mpris.MediaPlayer2."))) {
        return;
    }

    if (newOwner.isEmpty()) {
        m_uniqueToWellKnown.remove(oldOwner);
        m_wellKnownToUnique.remove(serviceName);
        m_history.remove(serviceName);
        m_history.remove(oldOwner);
        if (m_currentMedia.serviceName == serviceName) {
            m_currentMedia = MediaInfo();
            emit mediaChanged();
        }
    } else {
        m_uniqueToWellKnown[newOwner] = serviceName;
        m_wellKnownToUnique[serviceName] = newOwner;
    }
}

void MediaManager::refresh(const QList<WindowInfo> &windows)
{
    m_currentMedia = MediaInfo();

    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        return;
    }

    QDBusConnectionInterface *interface = bus.interface();
    if (!interface) {
        return;
    }

    const QStringList services = interface->registeredServiceNames().value();
    
    struct Candidate {
        MediaInfo info;
        int pid = 0;
        qint64 score = 0;
    };

    QList<Candidate> rawCandidates;
    QSet<int> plasmaPids;

    for (const QString &service : services) {
        if (!service.startsWith(QLatin1String("org.mpris.MediaPlayer2."))) {
            continue;
        }

        QString owner = interface->serviceOwner(service).value();
        if (!owner.isEmpty()) {
            m_uniqueToWellKnown[owner] = service;
            m_wellKnownToUnique[service] = owner;
        }

        // Query Player properties
        QDBusMessage msgPlayer = QDBusMessage::createMethodCall(
            service,
            QStringLiteral("/org/mpris/MediaPlayer2"),
            QStringLiteral("org.freedesktop.DBus.Properties"),
            QStringLiteral("GetAll")
        );
        msgPlayer << QStringLiteral("org.mpris.MediaPlayer2.Player");

        QDBusReply<QVariantMap> replyPlayer = bus.call(msgPlayer);
        if (!replyPlayer.isValid()) {
            continue;
        }

        QVariantMap playerProps = replyPlayer.value();
        QString status = playerProps.value(QStringLiteral("PlaybackStatus")).toString();
        if (status.isEmpty() || status == QLatin1String("Stopped")) {
            m_history[service].isCurrentlyPlaying = false;
            continue;
        }

        // Parse Metadata
        QVariant metaVal = playerProps.value(QStringLiteral("Metadata"));
        QVariantMap metaMap;
        if (metaVal.canConvert<QVariantMap>()) {
            metaMap = metaVal.toMap();
        } else if (metaVal.userType() == qMetaTypeId<QDBusArgument>()) {
            metaVal.value<QDBusArgument>() >> metaMap;
        }

        QString rawTitle = metaMap.value(QStringLiteral("xesam:title")).toString().trimmed();
        QString rawUrl = metaMap.value(QStringLiteral("xesam:url")).toString().trimmed();
        QString rawAlbum = metaMap.value(QStringLiteral("xesam:album")).toString().trimmed();
        QString title = sanitizeMediaTitle(rawTitle, rawUrl);
        title = stripSiteSuffix(title, rawAlbum.isEmpty() ? rawUrl : rawAlbum);

        QString artist;
        QVariant artistVal = metaMap.value(QStringLiteral("xesam:artist"));
        if (artistVal.typeId() == QMetaType::QStringList) {
            artist = artistVal.toStringList().join(QStringLiteral(", ")).trimmed();
        } else if (artistVal.typeId() == QMetaType::QString) {
            artist = artistVal.toString().trimmed();
        }

        // Clean artist: never allow a URL or file path/location as artist
        if (artist.startsWith(QLatin1String("http://"), Qt::CaseInsensitive) ||
            artist.startsWith(QLatin1String("https://"), Qt::CaseInsensitive) ||
            artist.startsWith(QLatin1String("file://"), Qt::CaseInsensitive) ||
            artist.startsWith(QLatin1Char('/'))) {
            artist.clear();
        }

        // STRIKTE REGEL: Niemals Verzeichnis- oder Ordnernamen aus dem Pfad als Künstler übernehmen!
        if (!artist.isEmpty()) {
            auto isFolderInPath = [](const QString &candFolder, const QString &pathOrUrl) -> bool {
                if (candFolder.isEmpty() || pathOrUrl.isEmpty()) return false;
                QUrl u(pathOrUrl);
                QString p = u.isLocalFile() ? u.toLocalFile() : (u.path().isEmpty() ? pathOrUrl : u.path());
                QStringList parts = p.split(QLatin1Char('/'), Qt::SkipEmptyParts);
                for (const QString &part : parts) {
                    QString decoded = QUrl::fromPercentEncoding(part.toUtf8()).trimmed();
                    if (!decoded.isEmpty() && decoded.compare(candFolder, Qt::CaseInsensitive) == 0) {
                        return true;
                    }
                }
                return false;
            };

            if (isFolderInPath(artist, rawUrl) || isFolderInPath(artist, rawTitle)) {
                artist.clear();
            }
        }

        // Bei Browser-Streams (YouTube, Twitch, etc.) ist xesam:artist der Kanalname, kein echter Künstler.
        if (!artist.isEmpty()) {
            bool isBrowserStream = service.contains(QLatin1String("plasma-browser-integration"), Qt::CaseInsensitive);
            if (!isBrowserStream && !rawUrl.isEmpty()) {
                static const QStringList streamingDomains = {
                    QStringLiteral("youtube.com"), QStringLiteral("youtu.be"),
                    QStringLiteral("twitch.tv"), QStringLiteral("soundcloud.com"),
                    QStringLiteral("vimeo.com"), QStringLiteral("dailymotion.com"),
                    QStringLiteral("bandcamp.com"), QStringLiteral("mixcloud.com"),
                    QStringLiteral("tiktok.com"), QStringLiteral("facebook.com"),
                    QStringLiteral("instagram.com"), QStringLiteral("reddit.com")
                };
                QUrl u(rawUrl);
                QString host = u.host().toLower();
                for (const auto &domain : streamingDomains) {
                    if (host.endsWith(domain)) {
                        isBrowserStream = true;
                        break;
                    }
                }
            }
            if (isBrowserStream) {
                artist.clear();
            }
        }

        // Falls ein echter Künstler vorliegt, kombinieren
        if (!artist.isEmpty() && !title.isEmpty() && !title.startsWith(artist, Qt::CaseInsensitive)) {
            title = artist + QStringLiteral(" - ") + title;
        }

        // If Paused, only consider if there is a non-empty track title
        if (status == QLatin1String("Paused") && title.isEmpty()) {
            continue;
        }

        // Track PID for plasma-browser-integration to deduplicate against chromium/brave internal mpris instances
        int pid = metaMap.value(QStringLiteral("kde:pid"), 0).toInt();
        if (service == QLatin1String("org.mpris.MediaPlayer2.plasma-browser-integration") && pid > 0) {
            plasmaPids.insert(pid);
        }

        // Query Root properties for App Name / Identity
        QDBusMessage msgRoot = QDBusMessage::createMethodCall(
            service,
            QStringLiteral("/org/mpris/MediaPlayer2"),
            QStringLiteral("org.freedesktop.DBus.Properties"),
            QStringLiteral("GetAll")
        );
        msgRoot << QStringLiteral("org.mpris.MediaPlayer2");

        QString appName;
        QString desktopEntry;
        QDBusReply<QVariantMap> replyRoot = bus.call(msgRoot);
        if (replyRoot.isValid()) {
            appName = replyRoot.value().value(QStringLiteral("Identity")).toString().trimmed();
            desktopEntry = replyRoot.value().value(QStringLiteral("DesktopEntry")).toString().trimmed();
        }

        // Fallback bzw. Spezifizierung bekannter Player anhand des DesktopEntry / Service-Namens
        if (appName.isEmpty() || appName == QLatin1String("Plasma Browser Integration")) {
            if (!desktopEntry.isEmpty()) {
                if (desktopEntry.contains(QLatin1String("brave"), Qt::CaseInsensitive)) {
                    appName = QStringLiteral("Brave");
                } else if (desktopEntry.contains(QLatin1String("chromium"), Qt::CaseInsensitive)) {
                    appName = QStringLiteral("Chromium");
                } else if (desktopEntry.contains(QLatin1String("firefox"), Qt::CaseInsensitive)) {
                    appName = QStringLiteral("Firefox");
                } else if (desktopEntry.contains(QLatin1String("chrome"), Qt::CaseInsensitive)) {
                    appName = QStringLiteral("Chrome");
                }
            }
            if (appName.isEmpty() || appName == QLatin1String("Plasma Browser Integration")) {
                if (service.contains(QLatin1String("brave"), Qt::CaseInsensitive)) {
                    appName = QStringLiteral("Brave");
                } else if (service.contains(QLatin1String("audacious"), Qt::CaseInsensitive)) {
                    appName = QStringLiteral("Audacious");
                } else if (service.contains(QLatin1String("chromium"), Qt::CaseInsensitive)) {
                    appName = QStringLiteral("Chromium");
                } else if (service.contains(QLatin1String("firefox"), Qt::CaseInsensitive)) {
                    appName = QStringLiteral("Firefox");
                } else if (service.contains(QLatin1String("chrome"), Qt::CaseInsensitive)) {
                    appName = QStringLiteral("Chrome");
                } else if (service.contains(QLatin1String("vlc"), Qt::CaseInsensitive)) {
                    appName = QStringLiteral("VLC");
                } else if (service.contains(QLatin1String("mpv"), Qt::CaseInsensitive)) {
                    appName = QStringLiteral("mpv");
                } else if (appName == QLatin1String("Plasma Browser Integration")) {
                    appName = QStringLiteral("Browser");
                }
            }
        }

        if (pid <= 0) {
            int instIdx = service.lastIndexOf(QLatin1String("instance"));
            if (instIdx > 0) {
                pid = service.mid(instIdx + 8).toInt();
            }
        }
        if (pid > 0 && (appName.isEmpty() || appName == QLatin1String("Chromium"))) {
            QFile commFile(QStringLiteral("/proc/%1/comm").arg(pid));
            if (commFile.open(QIODevice::ReadOnly)) {
                QString procName = QString::fromUtf8(commFile.readAll()).trimmed();
                if (!procName.isEmpty() && procName.compare(QLatin1String("chromium"), Qt::CaseInsensitive) != 0) {
                    procName[0] = procName[0].toUpper();
                    appName = procName;
                }
            }
        }

        if (appName.isEmpty()) {
            QString rest = service.mid(23); // strip "org.mpris.MediaPlayer2."
            int dot = rest.indexOf(QLatin1Char('.'));
            appName = (dot > 0) ? rest.left(dot) : rest;
            if (!appName.isEmpty()) {
                appName = appName.left(1).toUpper() + appName.mid(1);
            }
        }

        // Realtime History Aktualisierung
        qint64 now = QDateTime::currentMSecsSinceEpoch();
        bool isPlaying = (status.compare(QLatin1String("Playing"), Qt::CaseInsensitive) == 0);
        bool isPaused = (status.compare(QLatin1String("Paused"), Qt::CaseInsensitive) == 0);

        if (isPlaying) {
            if (!m_history[service].isCurrentlyPlaying || m_history[service].lastPlayingTime == 0) {
                m_history[service].lastPlayingTime = now;
                m_history[service].lastActionTime = now;
            }
            m_history[service].isCurrentlyPlaying = true;
            m_history[service].serviceName = service;
            m_history[service].appName = appName;
            m_appHistory[appName.toLower()] = m_history[service];
        } else if (isPaused) {
            if (m_history[service].isCurrentlyPlaying) {
                m_history[service].lastPausedTime = now;
                m_history[service].lastActionTime = now;
            } else if (m_history[service].lastActionTime <= 0) {
                m_history[service].lastPausedTime = 1;
                m_history[service].lastActionTime = 1;
            }
            m_history[service].isCurrentlyPlaying = false;
            m_history[service].serviceName = service;
            m_history[service].appName = appName;
            if (!m_appHistory.contains(appName.toLower())) {
                m_appHistory[appName.toLower()] = m_history[service];
            }
        }

        Candidate cand;
        cand.info.hasStream = true;
        cand.info.serviceName = service;
        cand.info.appName = appName;
        cand.info.trackTitle = title.isEmpty() ? QStringLiteral("Media Stream") : title;
        cand.info.artist = artist;
        cand.info.playbackStatus = status;
        cand.info.isPlaying = isPlaying;
        cand.info.canGoNext = playerProps.value(QStringLiteral("CanGoNext"), true).toBool();
        cand.info.canGoPrevious = playerProps.value(QStringLiteral("CanGoPrevious"), true).toBool();
        cand.pid = pid;

        rawCandidates.append(cand);
    }

    // Deduplizierung: Wenn plasma-browser-integration für die Browser-PID läuft,
    // redundanten internen MPRIS-Service (z. B. brave.instance<PID>) nur verwerfen,
    // wenn plasma-browser-integration ebenfalls läuft oder beide pausiert sind.
    // Ein LAUFENDER Stream darf NIEMALS verworfen werden!
    QList<Candidate> filteredCandidates;
    for (const auto &c : rawCandidates) {
        bool isDuplicateInstance = false;
        for (int p : plasmaPids) {
            QString pattern = QStringLiteral("instance%1").arg(p);
            if (c.info.serviceName.contains(pattern)) {
                bool plasmaExists = false;
                bool plasmaIsPlaying = false;
                for (const auto &other : rawCandidates) {
                    if (other.info.serviceName == QLatin1String("org.mpris.MediaPlayer2.plasma-browser-integration") && other.pid == p) {
                        plasmaExists = true;
                        plasmaIsPlaying = other.info.isPlaying;
                        break;
                    }
                }
                if (plasmaExists && (plasmaIsPlaying || !c.info.isPlaying)) {
                    isDuplicateInstance = true;
                    break;
                }
            }
        }
        if (!isDuplicateInstance) {
            filteredCandidates.append(c);
        }
    }

    // STREAM-PRIORITÄT:
    // 1. Priorität: DER LAUFENDE STREAM (Playing). Ein laufender Stream schlägt jeden pausierten Stream!
    // 2. Priorität (untergeordnet): DER, WELCHER ALS LETZTES PAUSIERT/GESPIELT WURDE.
    for (auto &cand : filteredCandidates) {
        qint64 actionTime = m_history.value(cand.info.serviceName).lastActionTime;
        if (actionTime <= 0) {
            actionTime = m_appHistory.value(cand.info.appName.toLower()).lastActionTime;
        }

        if (cand.info.isPlaying) {
            // Tier 1 (LAUFEND): Basis 10 Billionen + actionTime
            // Ein laufender Stream übertrifft JEDEN pausierten Stream garantiert!
            cand.score = 10000000000000LL + actionTime;
        } else if (!m_lastControlledService.isEmpty() && cand.info.serviceName == m_lastControlledService) {
            // Tier 2 (ÜBER KARYON PAUSIERT): Basis 5 Billionen + actionTime
            // Wenn kein Stream läuft, hat der zuletzt über Karyon pausierte Stream Vorrang zum Fortsetzen!
            cand.score = 5000000000000LL + actionTime;
        } else if (!m_lastControlledAppName.isEmpty() && cand.info.appName.compare(m_lastControlledAppName, Qt::CaseInsensitive) == 0) {
            // Tier 2b (Gleiche App wie über Karyon pausiert): Basis 4 Billionen + actionTime
            cand.score = 4000000000000LL + actionTime;
        } else {
            // Tier 3 (SONSTIGE PAUSIERTE STREAMS): Basis = actionTime (zuletzt manuell pausiert)
            cand.score = (actionTime > 0) ? actionTime : 1LL;
        }

        // Fein-Differenzierung / Tie-Breaker (+0 bis +2500 ms) bei gleichen Timestamps:
        for (int i = 0; i < windows.size(); ++i) {
            const WindowInfo &w = windows.at(i);
            QString rc = w.resourceClass.toLower();
            QString df = w.desktopFileName.toLower();
            QString cap = w.caption.toLower();
            QString app = cand.info.appName.toLower();
            QString srv = cand.info.serviceName.toLower();

            bool match = false;
            if (!app.isEmpty() && (rc.contains(app) || df.contains(app) || cap.contains(app))) {
                match = true;
            } else if (!cand.info.trackTitle.isEmpty() && cap.contains(cand.info.trackTitle.toLower())) {
                match = true;
            } else if (srv.contains(rc) || (!df.isEmpty() && srv.contains(df))) {
                match = true;
            }

            if (match) {
                if (i == 0) cand.score += 1500;      // Direkt aktives Fenster
                else if (i == 1) cand.score += 800;  // Zuletzt genutztes Fenster
                else if (i == 2) cand.score += 400;
                else cand.score += 200;
                break;
            }
        }

        if (!m_lastControlledService.isEmpty() && cand.info.serviceName == m_lastControlledService) {
            cand.score += 1000;
        } else if (!m_lastControlledAppName.isEmpty() && 
                   cand.info.appName.compare(m_lastControlledAppName, Qt::CaseInsensitive) == 0) {
            cand.score += 900;
        }

        if (!cand.info.artist.isEmpty()) cand.score += 150;
        if (cand.info.canGoNext || cand.info.canGoPrevious) cand.score += 100;
    }

    if (!filteredCandidates.isEmpty()) {
        std::sort(filteredCandidates.begin(), filteredCandidates.end(), [](const Candidate &a, const Candidate &b) {
            return a.score > b.score;
        });
        m_currentMedia = filteredCandidates.first().info;
    } else {
        m_currentMedia = MediaInfo();
    }

    emit mediaChanged();
}

void MediaManager::playPause()
{
    if (!m_currentMedia.hasStream || m_currentMedia.serviceName.isEmpty()) {
        return;
    }

    qint64 now = QDateTime::currentMSecsSinceEpoch();
    m_lastControlledService = m_currentMedia.serviceName;
    m_lastControlledAppName = m_currentMedia.appName;

    m_history[m_currentMedia.serviceName].lastActionTime = now;
    m_history[m_currentMedia.serviceName].serviceName = m_currentMedia.serviceName;
    m_history[m_currentMedia.serviceName].appName = m_currentMedia.appName;
    if (!m_currentMedia.appName.isEmpty()) {
        m_appHistory[m_currentMedia.appName.toLower()].lastActionTime = now;
    }

    if (m_currentMedia.isPlaying) {
        // Pausieren
        m_history[m_currentMedia.serviceName].lastPausedTime = now;
        m_history[m_currentMedia.serviceName].isCurrentlyPlaying = false;
        if (!m_currentMedia.appName.isEmpty()) {
            m_appHistory[m_currentMedia.appName.toLower()].lastPausedTime = now;
            m_appHistory[m_currentMedia.appName.toLower()].isCurrentlyPlaying = false;
        }

        QDBusMessage msg = QDBusMessage::createMethodCall(
            m_currentMedia.serviceName,
            QStringLiteral("/org/mpris/MediaPlayer2"),
            QStringLiteral("org.mpris.MediaPlayer2.Player"),
            QStringLiteral("PlayPause")
        );
        QDBusConnection::sessionBus().asyncCall(msg);

        m_currentMedia.isPlaying = false;
        m_currentMedia.playbackStatus = QStringLiteral("Paused");
    } else {
        // Fortsetzen
        m_history[m_currentMedia.serviceName].lastPlayingTime = now;
        m_history[m_currentMedia.serviceName].isCurrentlyPlaying = true;
        if (!m_currentMedia.appName.isEmpty()) {
            m_appHistory[m_currentMedia.appName.toLower()].lastPlayingTime = now;
            m_appHistory[m_currentMedia.appName.toLower()].isCurrentlyPlaying = true;
        }

        QDBusMessage msg = QDBusMessage::createMethodCall(
            m_currentMedia.serviceName,
            QStringLiteral("/org/mpris/MediaPlayer2"),
            QStringLiteral("org.mpris.MediaPlayer2.Player"),
            QStringLiteral("PlayPause")
        );
        QDBusConnection::sessionBus().asyncCall(msg);

        m_currentMedia.isPlaying = true;
        m_currentMedia.playbackStatus = QStringLiteral("Playing");
    }

    emit mediaChanged();
}

void MediaManager::next()
{
    if (!m_currentMedia.hasStream || m_currentMedia.serviceName.isEmpty()) {
        return;
    }

    qint64 now = QDateTime::currentMSecsSinceEpoch();
    m_lastControlledService = m_currentMedia.serviceName;
    m_lastControlledAppName = m_currentMedia.appName;
    m_history[m_currentMedia.serviceName].lastActionTime = now;
    m_history[m_currentMedia.serviceName].lastPlayingTime = now;
    if (!m_currentMedia.appName.isEmpty()) {
        m_appHistory[m_currentMedia.appName.toLower()].lastActionTime = now;
        m_appHistory[m_currentMedia.appName.toLower()].lastPlayingTime = now;
    }

    QDBusMessage msgNext = QDBusMessage::createMethodCall(
        m_currentMedia.serviceName,
        QStringLiteral("/org/mpris/MediaPlayer2"),
        QStringLiteral("org.mpris.MediaPlayer2.Player"),
        QStringLiteral("Next")
    );
    QDBusConnection::sessionBus().asyncCall(msgNext);
}

void MediaManager::previous()
{
    if (!m_currentMedia.hasStream || m_currentMedia.serviceName.isEmpty()) {
        return;
    }

    qint64 now = QDateTime::currentMSecsSinceEpoch();
    m_lastControlledService = m_currentMedia.serviceName;
    m_lastControlledAppName = m_currentMedia.appName;
    m_history[m_currentMedia.serviceName].lastActionTime = now;
    m_history[m_currentMedia.serviceName].lastPlayingTime = now;
    if (!m_currentMedia.appName.isEmpty()) {
        m_appHistory[m_currentMedia.appName.toLower()].lastActionTime = now;
        m_appHistory[m_currentMedia.appName.toLower()].lastPlayingTime = now;
    }

    QDBusMessage msgPrev = QDBusMessage::createMethodCall(
        m_currentMedia.serviceName,
        QStringLiteral("/org/mpris/MediaPlayer2"),
        QStringLiteral("org.mpris.MediaPlayer2.Player"),
        QStringLiteral("Previous")
    );
    QDBusConnection::sessionBus().asyncCall(msgPrev);
}
