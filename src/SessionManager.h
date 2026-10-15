#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

class SessionManager : public QObject
{
    Q_OBJECT

public:
    static void logout();
    static void lock();
    static void switchUser();
    static void suspend();
    static void hibernate();
    static void reboot();
    static void shutdown();

private:
    static void callKdeShutdownMethod(const QString &methodName, const QStringList &fallbackCmd);
    static void callPowerManagementMethod(const QString &methodName, const QStringList &fallbackCmd);
};
