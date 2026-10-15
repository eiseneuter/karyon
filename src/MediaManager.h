#pragma once

#include <QObject>
#include <QString>
#include <QVariantMap>

#include <QList>
#include <QMap>
#include <QDBusMessage>

struct WindowInfo;

struct MediaInfo {
    bool hasStream = false;
    QString serviceName;
    QString appName;
    QString trackTitle;
    QString artist;
    QString playbackStatus; // "Playing", "Paused", "Stopped"
    bool isPlaying = false;
    bool canGoNext = false;
    bool canGoPrevious = false;
};

struct PlayerHistory {
    QString serviceName;
    QString appName;
    qint64 lastPlayingTime = 0;  // ms epoch
    qint64 lastPausedTime = 0;   // ms epoch
    qint64 lastActionTime = 0;   // max(lastPlayingTime, lastPausedTime)
    bool isCurrentlyPlaying = false;
};

class MediaManager : public QObject
{
    Q_OBJECT

public:
    explicit MediaManager(QObject *parent = nullptr);
    ~MediaManager() override = default;

    void refresh(const QList<WindowInfo> &windows = {});
    bool hasActiveStream() const { return m_currentMedia.hasStream; }
    const MediaInfo &currentMedia() const { return m_currentMedia; }

    void playPause();
    void next();
    void previous();

signals:
    void mediaChanged();

private slots:
    void onMprisPropertiesChanged(const QString &interfaceName, const QVariantMap &changedProps,
                                  const QStringList &invalidatedProps, const QDBusMessage &msg);
    void onNameOwnerChanged(const QString &serviceName, const QString &oldOwner, const QString &newOwner);

private:
    MediaInfo m_currentMedia;
    QString m_lastControlledService;
    QString m_lastControlledAppName;

    QMap<QString, PlayerHistory> m_history;
    QMap<QString, PlayerHistory> m_appHistory;
    QMap<QString, QString> m_uniqueToWellKnown;
    QMap<QString, QString> m_wellKnownToUnique;
};
