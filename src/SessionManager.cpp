#include "SessionManager.h"
#include "ProcessUtils.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusReply>
#include <QProcess>
#include <QDebug>

namespace {
// Blockierende Aufrufe höchstens so lange warten lassen (Standard wären 25 s Stillstand).
constexpr int kSessionCallTimeoutMs = 4000;

// Auf den Ersatzweg nur ausweichen, wenn es den Dienst/die Methode nicht gibt. Bei einer
// Zeitüberschreitung läuft die erste Anfrage evtl. noch (z.B. wartet sie auf eine
// Passwortabfrage) – ein zweiter Auslöser würde den Rechner dann zweimal schlafen legen.
bool serviceUnavailable(const QDBusError &e)
{
    switch (e.type()) {
    case QDBusError::ServiceUnknown:
    case QDBusError::UnknownMethod:
    case QDBusError::UnknownObject:
    case QDBusError::UnknownInterface:
    case QDBusError::NotSupported:
    case QDBusError::Disconnected:
        return true;
    default:
        return false;
    }
}
} // namespace

void SessionManager::callKdeShutdownMethod(const QString &methodName, const QStringList &fallbackCmd)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(
        QStringLiteral("org.kde.Shutdown"),
        QStringLiteral("/Shutdown"),
        QStringLiteral("org.kde.Shutdown"),
        methodName
    );
    QDBusReply<void> reply = QDBusConnection::sessionBus().call(msg, QDBus::Block, kSessionCallTimeoutMs);
    if (!reply.isValid() && serviceUnavailable(reply.error())) {
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
    QDBusReply<void> reply = QDBusConnection::sessionBus().call(msg, QDBus::Block, kSessionCallTimeoutMs);
    if (!reply.isValid() && serviceUnavailable(reply.error())) {
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
    QDBusReply<void> reply = QDBusConnection::sessionBus().call(msg, QDBus::Block, kSessionCallTimeoutMs);
    if (!reply.isValid() && serviceUnavailable(reply.error())) {
        // Fallback 1: KSMServer
        QDBusMessage msgKsm = QDBusMessage::createMethodCall(
            QStringLiteral("org.kde.ksmserver"),
            QStringLiteral("/KSMServer"),
            QStringLiteral("org.kde.KSMServerInterface"),
            QStringLiteral("closeSession")
        );
        QDBusReply<bool> repKsm = QDBusConnection::sessionBus().call(msgKsm, QDBus::Block, kSessionCallTimeoutMs);
        if (!repKsm.isValid() && serviceUnavailable(repKsm.error())) {
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
    QDBusReply<void> reply = QDBusConnection::sessionBus().call(msg, QDBus::Block, kSessionCallTimeoutMs);
    if (!reply.isValid() && serviceUnavailable(reply.error())) {
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
    QDBusReply<void> reply = QDBusConnection::systemBus().call(msg, QDBus::Block, kSessionCallTimeoutMs);
    if (!reply.isValid() && serviceUnavailable(reply.error())) {
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
    QDBusReply<void> reply = QDBusConnection::systemBus().call(msg, QDBus::Block, kSessionCallTimeoutMs);
    if (!reply.isValid() && serviceUnavailable(reply.error())) {
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
    QDBusReply<void> reply = QDBusConnection::systemBus().call(msg, QDBus::Block, kSessionCallTimeoutMs);
    if (!reply.isValid() && serviceUnavailable(reply.error())) {
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
