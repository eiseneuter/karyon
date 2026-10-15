#include "PermissionManager.h"
#include "ProcessUtils.h"

#include <fcntl.h>
#include <unistd.h>
#include <filesystem>
#include <QFile>
#include <QProcess>
#include <QDebug>

namespace fs = std::filesystem;

bool PermissionManager::checkUInputAccess()
{
    int fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd >= 0) {
        close(fd);
        return true;
    }
    return false;
}

bool PermissionManager::checkEventDeviceAccess()
{
    if (!fs::exists("/dev/input")) {
        return false;
    }

    int foundCount = 0;
    int readableCount = 0;

    for (const auto &entry : fs::directory_iterator("/dev/input")) {
        const std::string fn = entry.path().filename().string();
        if (fn.rfind("event", 0) == 0) {
            foundCount++;
            int fd = open(entry.path().c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
            if (fd >= 0) {
                readableCount++;
                close(fd);
            }
        }
    }

    if (foundCount == 0) {
        return false;
    }

    // Require modern 70-karyon.rules (runs before 73-seat-late.rules)
    if (QFile::exists(QStringLiteral("/etc/udev/rules.d/70-karyon.rules"))) {
        return readableCount > 0;
    }

    // Without 70-karyon.rules, a single incidental device (e.g. OpenRGB matching one vendor)
    // is NOT general input access. We require all event devices to be readable (e.g. input group).
    return readableCount == foundCount;
}

bool PermissionManager::hasRequiredPermissions()
{
    return checkUInputAccess() && checkEventDeviceAccess();
}

bool PermissionManager::grantPermissions(QString &errorOut)
{
    QString user = qEnvironmentVariable("USER");
    if (user.isEmpty()) {
        user = QString::fromLocal8Bit(qgetenv("LOGNAME"));
    }

    QString script = QStringLiteral(
        "modprobe uinput 2>/dev/null || true; "
        "rm -f /etc/udev/rules.d/*dumb-launcher*.rules /etc/udev/rules.d/99-karyon.rules 2>/dev/null || true; "
        "mkdir -p /etc/udev/rules.d && "
        "cat << 'EOF' > /etc/udev/rules.d/70-karyon.rules\n"
        "# Karyon hardware input subsystem permissions\n"
        "KERNEL==\"uinput\", SUBSYSTEM==\"misc\", TAG+=\"uaccess\", OPTIONS+=\"static_node=uinput\"\n"
        "KERNEL==\"event*\", SUBSYSTEM==\"input\", TAG+=\"uaccess\"\n"
        "TAG==\"uaccess\", ENV{MAJOR}!=\"\", RUN{builtin}+=\"uaccess\"\n"
        "EOF\n"
        "udevadm control --reload-rules && "
        "udevadm trigger --action=change --subsystem-match=misc --subsystem-match=input && "
        "if [ -n \"%1\" ]; then "
        "  setfacl -m u:%1:rw /dev/uinput 2>/dev/null || true; "
        "  for d in /dev/input/event*; do setfacl -m u:%1:rw \"$d\" 2>/dev/null || true; done; "
        "fi"
    ).arg(user);

    QProcess proc;
    proc.setProcessEnvironment(ProcessUtils::cleanHostEnvironment());
    proc.start(QStringLiteral("pkexec"), {QStringLiteral("sh"), QStringLiteral("-c"), script});
    if (!proc.waitForStarted(5000)) {
        errorOut = QStringLiteral("Failed to start pkexec.");
        return false;
    }

    proc.waitForFinished(-1);

    if (proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0) {
        errorOut = QString::fromUtf8(proc.readAllStandardError());
        if (errorOut.trimmed().isEmpty()) {
            errorOut = QStringLiteral("Authentication failed or cancelled.");
        }
        return false;
    }

    return hasRequiredPermissions();
}

bool PermissionManager::revokePermissions(QString &errorOut)
{
    QString script = QStringLiteral(
        "rm -f /etc/udev/rules.d/70-karyon.rules /etc/udev/rules.d/99-karyon.rules /etc/udev/rules.d/*dumb-launcher*.rules /etc/udev/rules.d/*karyon*.rules 2>/dev/null || true; "
        "udevadm control --reload-rules && "
        "udevadm trigger --action=change --subsystem-match=misc --subsystem-match=input && "
        "setfacl -b /dev/uinput 2>/dev/null || true; "
        "for d in /dev/input/event*; do setfacl -b \"$d\" 2>/dev/null || true; done"
    );

    QProcess proc;
    proc.setProcessEnvironment(ProcessUtils::cleanHostEnvironment());
    proc.start(QStringLiteral("pkexec"), {QStringLiteral("sh"), QStringLiteral("-c"), script});
    if (!proc.waitForStarted(5000)) {
        errorOut = QStringLiteral("Failed to start pkexec.");
        return false;
    }

    proc.waitForFinished(-1);

    if (proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0) {
        errorOut = QString::fromUtf8(proc.readAllStandardError());
        if (errorOut.trimmed().isEmpty()) {
            errorOut = QStringLiteral("Authentication failed or cancelled.");
        }
        return false;
    }

    return true;
}
