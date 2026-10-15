#include "SessionManager.h"
#include "ProcessUtils.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusReply>
#include <QProcess>
#include <QDebug>

void SessionManager::callKdeShutdownMethod(const QString &methodName, const QStringList &fallbackCmd)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(
        QStringLiteral("org.kde.Shutdown"),
        QStringLiteral("/Shutdown"),
        QStringLiteral("org.kde.Shutdown"),
        methodName
    );
    QDBusReply<void> reply = QDBusConnection::sessionBus().call(msg);
    if (!reply.isValid()) {
        qWarning() << "org.kde.Shutdown" << methodName << "failed:" << reply.error().message() << ". Trying fallback:" << fallbackCmd;
        if (!fallbackCmd.isEmpty()) {
            QString prog = fallbackCmd.first();
            QStringList args = fallbackCmd.mid(1);
            ProcessUtils::startDetachedHost(prog, args);
        }
    }
}

void SessionManager::callPowerManagementMethod(const QString &methodName, const QStringList &fallbackCmd)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(
        QStringLiteral("org.freedesktop.PowerManagement"),
        QStringLiteral("/org/freedesktop/PowerManagement"),
        QStringLiteral("org.freedesktop.PowerManagement"),
        methodName
    );
    QDBusReply<void> reply = QDBusConnection::sessionBus().call(msg);
    if (!reply.isValid()) {
        qWarning() << "org.freedesktop.PowerManagement" << methodName << "failed:" << reply.error().message() << ". Trying fallback:" << fallbackCmd;
        if (!fallbackCmd.isEmpty()) {
            QString prog = fallbackCmd.first();
            QStringList args = fallbackCmd.mid(1);
            ProcessUtils::startDetachedHost(prog, args);
        }
    }
}

void SessionManager::logout()
{
    QDBusMessage msg = QDBusMessage::createMethodCall(
        QStringLiteral("org.kde.Shutdown"),
        QStringLiteral("/Shutdown"),
        QStringLiteral("org.kde.Shutdown"),
        QStringLiteral("logout")
    );
    QDBusReply<void> reply = QDBusConnection::sessionBus().call(msg);
    if (!reply.isValid()) {
        // Fallback 1: KSMServer
        QDBusMessage msgKsm = QDBusMessage::createMethodCall(
            QStringLiteral("org.kde.ksmserver"),
            QStringLiteral("/KSMServer"),
            QStringLiteral("org.kde.KSMServerInterface"),
            QStringLiteral("closeSession")
        );
        QDBusReply<bool> repKsm = QDBusConnection::sessionBus().call(msgKsm);
        if (!repKsm.isValid()) {
            // Fallback 2: loginctl
            ProcessUtils::startDetachedHost(QStringLiteral("loginctl"), {QStringLiteral("terminate-session"), QStringLiteral("self")});
        }
    }
}

void SessionManager::lock()
{
    QDBusMessage msg = QDBusMessage::createMethodCall(
        QStringLiteral("org.freedesktop.ScreenSaver"),
        QStringLiteral("/ScreenSaver"),
        QStringLiteral("org.freedesktop.ScreenSaver"),
        QStringLiteral("Lock")
    );
    QDBusReply<void> reply = QDBusConnection::sessionBus().call(msg);
    if (!reply.isValid()) {
        ProcessUtils::startDetachedHost(QStringLiteral("loginctl"), {QStringLiteral("lock-session")});
    }
}

void SessionManager::switchUser()
{
    QDBusMessage msg = QDBusMessage::createMethodCall(
        QStringLiteral("org.freedesktop.DisplayManager"),
        QStringLiteral("/org/freedesktop/DisplayManager/Seat0"),
        QStringLiteral("org.freedesktop.DisplayManager.Seat"),
        QStringLiteral("SwitchToGreeter")
    );
    QDBusReply<void> reply = QDBusConnection::systemBus().call(msg);
    if (!reply.isValid()) {
        qWarning() << "org.freedesktop.DisplayManager SwitchToGreeter failed:" << reply.error().message() << ". Falling back to lock.";
        lock();
    }
}

void SessionManager::suspend()
{
    QDBusMessage msg = QDBusMessage::createMethodCall(
        QStringLiteral("org.freedesktop.login1"),
        QStringLiteral("/org/freedesktop/login1"),
        QStringLiteral("org.freedesktop.login1.Manager"),
        QStringLiteral("Suspend")
    );
    msg << true;
    QDBusReply<void> reply = QDBusConnection::systemBus().call(msg);
    if (!reply.isValid()) {
        callPowerManagementMethod(QStringLiteral("Suspend"), {QStringLiteral("systemctl"), QStringLiteral("suspend")});
    }
}

void SessionManager::hibernate()
{
    QDBusMessage msg = QDBusMessage::createMethodCall(
        QStringLiteral("org.freedesktop.login1"),
        QStringLiteral("/org/freedesktop/login1"),
        QStringLiteral("org.freedesktop.login1.Manager"),
        QStringLiteral("Hibernate")
    );
    msg << true;
    QDBusReply<void> reply = QDBusConnection::systemBus().call(msg);
    if (!reply.isValid()) {
        callPowerManagementMethod(QStringLiteral("Hibernate"), {QStringLiteral("systemctl"), QStringLiteral("hibernate")});
    }
}

void SessionManager::reboot()
{
    callKdeShutdownMethod(QStringLiteral("logoutAndReboot"), {QStringLiteral("systemctl"), QStringLiteral("reboot")});
}

void SessionManager::shutdown()
{
    callKdeShutdownMethod(QStringLiteral("logoutAndShutdown"), {QStringLiteral("systemctl"), QStringLiteral("poweroff")});
}
