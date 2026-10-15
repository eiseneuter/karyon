#pragma once

#include <QObject>
#include <QString>
#include <QHash>
#include <QSet>
#include <QMap>
#include <QVariant>
#include <QtDBus/QDBusConnection>
#include <QtDBus/QDBusMessage>
#include <QtDBus/QDBusVirtualObject>

class JobProgressManager;

class JobViewMonitorObject : public QDBusVirtualObject
{
public:
    explicit JobViewMonitorObject(JobProgressManager *mgr);
    QString introspect(const QString &path) const override;
    bool handleMessage(const QDBusMessage &msg, const QDBusConnection &connection) override;

private:
    JobProgressManager *m_mgr = nullptr;
};

class JobProgressManager : public QObject
{
    Q_OBJECT

public:
    explicit JobProgressManager(QObject *parent = nullptr);
    ~JobProgressManager() override;

    int progressForApp(const QString &desktopFileName, const QString &resourceClass, QString *matchedApp = nullptr) const;
    static QString cleanAppKey(const QString &raw);
    static void addAppKeysToSet(QSet<QString> &set, const QString &raw);
    static bool isAppKeyInSet(const QSet<QString> &set, const QString &raw);

signals:
    void progressUpdated();

public slots:
    void onUnityUpdate(const QString &appUri, const QMap<QString, QVariant> &properties);
    void onNameOwnerChanged(const QString &name, const QString &oldOwner, const QString &newOwner);

private:
    friend class JobViewMonitorObject;
    void handleJobViewMessage(const QDBusMessage &msg);
    void updateAppProgress(const QString &appKey, int percent);
    void removeAppProgress(const QString &appKey);

    QDBusConnection m_monBus;
    JobViewMonitorObject m_virtualObj;

    QHash<QString, int> m_appProgress;
    QHash<QString, QString> m_clientToApp;
    QHash<QString, QString> m_pathToApp;
};
