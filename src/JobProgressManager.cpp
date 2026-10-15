#include "JobProgressManager.h"
#include <QtDBus/QDBusMessage>
#include <QtDBus/QDBusReply>
#include <QDebug>

JobViewMonitorObject::JobViewMonitorObject(JobProgressManager *mgr)
    : m_mgr(mgr)
{
}

QString JobViewMonitorObject::introspect(const QString &) const
{
    return QString();
}

bool JobViewMonitorObject::handleMessage(const QDBusMessage &msg, const QDBusConnection &)
{
    if (m_mgr) {
        m_mgr->handleJobViewMessage(msg);
    }
    return true;
}

JobProgressManager::JobProgressManager(QObject *parent)
    : QObject(parent)
    , m_monBus(QStringLiteral("karyon_job_mon"))
    , m_virtualObj(this)
{
    // 1. Unity LauncherEntry signal on session bus (Firefox, Chrome, Electron, etc.)
    QDBusConnection::sessionBus().connect(
        QString(),
        QString(),
        QStringLiteral("com.canonical.Unity.LauncherEntry"),
        QStringLiteral("Update"),
        this,
        SLOT(onUnityUpdate(QString, QMap<QString, QVariant>))
    );

    // 2. Track DBus client disconnections
    QDBusConnection::sessionBus().connect(
        QStringLiteral("org.freedesktop.DBus"),
        QStringLiteral("/org/freedesktop/DBus"),
        QStringLiteral("org.freedesktop.DBus"),
        QStringLiteral("NameOwnerChanged"),
        this,
        SLOT(onNameOwnerChanged(QString, QString, QString))
    );

    // 3. Monitor KDE Kuiserver / JobView (Dolphin, Ark, KIO file operations)
    m_monBus = QDBusConnection::connectToBus(QDBusConnection::SessionBus, QStringLiteral("karyon_job_mon"));
    if (m_monBus.isConnected()) {
        m_monBus.registerVirtualObject(QStringLiteral("/"), &m_virtualObj, QDBusConnection::SubPath);

        QDBusMessage call = QDBusMessage::createMethodCall(
            QStringLiteral("org.freedesktop.DBus"),
            QStringLiteral("/org/freedesktop/DBus"),
            QStringLiteral("org.freedesktop.DBus.Monitoring"),
            QStringLiteral("BecomeMonitor")
        );
        QStringList rules;
        rules << QStringLiteral("type='method_call',interface='org.kde.JobViewServerV2'")
              << QStringLiteral("type='method_call',interface='org.kde.JobViewServer'")
              << QStringLiteral("type='method_call',interface='org.kde.JobViewV2'")
              << QStringLiteral("type='method_call',interface='org.kde.JobViewV3'");
        call << rules << static_cast<quint32>(0);
        m_monBus.call(call);
    }
}

JobProgressManager::~JobProgressManager()
{
    if (m_monBus.isConnected()) {
        m_monBus.unregisterObject(QStringLiteral("/"), QDBusConnection::UnregisterTree);
        QDBusConnection::disconnectFromBus(QStringLiteral("karyon_job_mon"));
    }
}

QString JobProgressManager::cleanAppKey(const QString &raw)
{
    QString s = raw.trimmed().toLower();
    if (s.startsWith(QLatin1String("application://"))) {
        s = s.mid(14);
    }
    if (s.endsWith(QLatin1String(".desktop"))) {
        s.chop(8);
    }
    return s;
}

void JobProgressManager::addAppKeysToSet(QSet<QString> &set, const QString &raw)
{
    if (raw.isEmpty()) return;
    QString clean = cleanAppKey(raw);
    if (!clean.isEmpty()) {
        set.insert(clean);
        QString base = clean.section(QLatin1Char('.'), -1);
        if (!base.isEmpty()) {
            set.insert(base);
        }
    }
}

bool JobProgressManager::isAppKeyInSet(const QSet<QString> &set, const QString &raw)
{
    if (raw.isEmpty() || set.isEmpty()) return false;
    QString clean = cleanAppKey(raw);
    if (set.contains(clean)) return true;
    QString base = clean.section(QLatin1Char('.'), -1);
    if (!base.isEmpty() && set.contains(base)) return true;
    return false;
}

void JobProgressManager::updateAppProgress(const QString &appKey, int percent)
{
    if (appKey.isEmpty()) return;
    QString clean = cleanAppKey(appKey);
    if (clean.isEmpty()) return;

    bool changed = false;
    if (m_appProgress.value(clean, -1) != percent) {
        m_appProgress[clean] = percent;
        changed = true;
    }
    QString base = clean.section(QLatin1Char('.'), -1);
    if (!base.isEmpty() && base != clean) {
        if (m_appProgress.value(base, -1) != percent) {
            m_appProgress[base] = percent;
            changed = true;
        }
    }
    if (changed) {
        emit progressUpdated();
    }
}

void JobProgressManager::removeAppProgress(const QString &appKey)
{
    if (appKey.isEmpty()) return;
    QString clean = cleanAppKey(appKey);
    if (clean.isEmpty()) return;

    bool changed = false;
    if (m_appProgress.contains(clean)) {
        m_appProgress.remove(clean);
        changed = true;
    }
    QString base = clean.section(QLatin1Char('.'), -1);
    if (!base.isEmpty() && base != clean) {
        if (m_appProgress.contains(base)) {
            m_appProgress.remove(base);
            changed = true;
        }
    }
    if (changed) {
        emit progressUpdated();
    }
}

void JobProgressManager::onUnityUpdate(const QString &appUri, const QMap<QString, QVariant> &properties)
{
    QString appKey = cleanAppKey(appUri);
    if (appKey.isEmpty()) return;

    bool visible = properties.value(QStringLiteral("progress-visible"), false).toBool();
    if (visible && properties.contains(QStringLiteral("progress"))) {
        double p = properties.value(QStringLiteral("progress")).toDouble();
        int percent = -1;
        if (p >= 0.0 && p <= 1.0) {
            percent = qRound(p * 100.0);
        } else if (p > 1.0 && p <= 100.0) {
            percent = qRound(p);
        }

        if (percent >= 0 && percent < 100) {
            updateAppProgress(appKey, percent);
        } else {
            removeAppProgress(appKey);
        }
    } else {
        removeAppProgress(appKey);
    }
}

void JobProgressManager::handleJobViewMessage(const QDBusMessage &msg)
{
    const QString member = msg.member();
    const QString sender = msg.service();
    const QList<QVariant> args = msg.arguments();

    if (member == QLatin1String("requestView")) {
        QString desktopEntry;
        if (msg.interface() == QLatin1String("org.kde.JobViewServerV2") && !args.isEmpty()) {
            desktopEntry = args.at(0).toString();
        } else if (msg.interface() == QLatin1String("org.kde.JobViewServer") && args.size() >= 2) {
            QString appIcon = args.at(1).toString();
            QString appName = args.at(0).toString();
            desktopEntry = !appIcon.isEmpty() ? appIcon : appName;
        }
        if (!desktopEntry.isEmpty()) {
            m_clientToApp[sender] = cleanAppKey(desktopEntry);
        }
    } else if (member == QLatin1String("setPercent")) {
        if (!args.isEmpty()) {
            uint p = args.at(0).toUInt();
            QString path = msg.path();
            if (m_clientToApp.contains(sender)) {
                m_pathToApp[path] = m_clientToApp.value(sender);
            }
            QString appKey = m_pathToApp.value(path, m_clientToApp.value(sender));
            if (!appKey.isEmpty()) {
                if (p > 0 && p < 100) {
                    updateAppProgress(appKey, static_cast<int>(p));
                } else {
                    removeAppProgress(appKey);
                }
            }
        }
    } else if (member == QLatin1String("terminate")) {
        QString path = msg.path();
        QString appKey = m_pathToApp.take(path);
        if (appKey.isEmpty() && m_clientToApp.contains(sender)) {
            appKey = m_clientToApp.value(sender);
        }
        if (!appKey.isEmpty()) {
            removeAppProgress(appKey);
        }
    }
}

void JobProgressManager::onNameOwnerChanged(const QString &name, const QString &, const QString &newOwner)
{
    if (newOwner.isEmpty()) {
        if (m_clientToApp.contains(name)) {
            QString appKey = m_clientToApp.take(name);
            removeAppProgress(appKey);
        }
    }
}

int JobProgressManager::progressForApp(const QString &desktopFileName, const QString &resourceClass, QString *matchedApp) const
{
    if (matchedApp) {
        matchedApp->clear();
    }
    if (m_appProgress.isEmpty()) {
        return -1;
    }

    auto checkKey = [this, matchedApp](const QString &raw) -> int {
        if (raw.isEmpty()) return -1;
        QString s = cleanAppKey(raw);
        if (m_appProgress.contains(s)) {
            if (matchedApp) *matchedApp = s;
            return m_appProgress.value(s);
        }
        QString base = s.section(QLatin1Char('.'), -1);
        if (!base.isEmpty() && m_appProgress.contains(base)) {
            if (matchedApp) *matchedApp = base;
            return m_appProgress.value(base);
        }
        return -1;
    };

    int p = checkKey(desktopFileName);
    if (p >= 0) return p;
    p = checkKey(resourceClass);
    if (p >= 0) return p;
    return -1;
}
