#include "DebugLogger.h"

#include <QDir>
#include <QDateTime>
#include <QThread>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <unistd.h>
#include <fcntl.h>
#include <csignal>
#include <execinfo.h>
#include <sys/types.h>
#include <sys/stat.h>

namespace DebugLogger {

static bool s_fileLoggingEnabled = false; // File logging switched OFF by default per user request
static int s_logFd = -1;
static std::mutex s_logMutex;
static QString s_logPath;

void setFileLoggingEnabled(bool enabled)
{
    std::lock_guard<std::mutex> lock(s_logMutex);
    s_fileLoggingEnabled = enabled;
    if (!enabled && s_logFd >= 0) {
        fsync(s_logFd);
        close(s_logFd);
        s_logFd = -1;
    }
}

bool isFileLoggingEnabled()
{
    return s_fileLoggingEnabled;
}

static const char* signalName(int sig)
{
    switch (sig) {
        case SIGSEGV: return "SIGSEGV (Segmentation Fault)";
        case SIGABRT: return "SIGABRT (Abort)";
        case SIGBUS:  return "SIGBUS (Bus Error)";
        case SIGFPE:  return "SIGFPE (Floating Point Exception)";
        case SIGILL:  return "SIGILL (Illegal Instruction)";
        case SIGTERM: return "SIGTERM (Termination Request)";
        default:      return "Unknown Signal";
    }
}

static void crashSignalHandler(int sig, siginfo_t *info, void * /*ucontext*/)
{
    // Async-signal-safe writing
    const char header[] = "\n\n"
        "================================================================================\n"
        "                     !!! CRITICAL CRASH DETECTED !!!                            \n"
        "================================================================================\n";
    if (s_logFd >= 0) {
        (void)write(s_logFd, header, sizeof(header) - 1);
    }
    (void)write(STDERR_FILENO, header, sizeof(header) - 1);

    char sigInfoBuf[256];
    int len = snprintf(sigInfoBuf, sizeof(sigInfoBuf),
                       "Signal: %d [%s]\nFaulty Address: %p\nPID: %d\n\nStack trace:\n",
                       sig, signalName(sig), info ? info->si_addr : nullptr, getpid());
    if (len > 0) {
        if (s_logFd >= 0) (void)write(s_logFd, sigInfoBuf, len);
        (void)write(STDERR_FILENO, sigInfoBuf, len);
    }

    void *traceArray[64];
    int traceSize = backtrace(traceArray, 64);
    if (s_logFd >= 0) {
        backtrace_symbols_fd(traceArray, traceSize, s_logFd);
    }
    backtrace_symbols_fd(traceArray, traceSize, STDERR_FILENO);

    const char footer[] =
        "================================================================================\n"
        "End of crash report.\n\n";
    if (s_logFd >= 0) {
        (void)write(s_logFd, footer, sizeof(footer) - 1);
        fsync(s_logFd);
    }
    (void)write(STDERR_FILENO, footer, sizeof(footer) - 1);

    // Default-Handler wiederherstellen und Signal erneut auslösen
    struct sigaction sa{};
    sa.sa_handler = SIG_DFL;
    sigemptyset(&sa.sa_mask);
    sigaction(sig, &sa, nullptr);
    raise(sig);
}

void installCrashHandler()
{
    struct sigaction sa{};
    sa.sa_sigaction = crashSignalHandler;
    sa.sa_flags = SA_SIGINFO | SA_RESETHAND;
    sigemptyset(&sa.sa_mask);

    sigaction(SIGSEGV, &sa, nullptr);
    sigaction(SIGABRT, &sa, nullptr);
    sigaction(SIGBUS,  &sa, nullptr);
    sigaction(SIGFPE,  &sa, nullptr);
    sigaction(SIGILL,  &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);
}

QString logFilePath()
{
    return s_logPath;
}

static void qtDebugMessageHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
    // Filter out known noisy framework warnings
    if (msg.contains(QLatin1String("The menu spec file")) ||
        msg.contains(QLatin1String("sycoca")) ||
        msg.contains(QLatin1String("Fontconfig")) ||
        msg.contains(QLatin1String("invalid attribute")) ||
        msg.contains(QLatin1String("invalid constant"))) {
        return;
    }

    const char *levelStr = "DEBUG";
    switch (type) {
        case QtDebugMsg:    levelStr = "DEBUG"; break;
        case QtInfoMsg:     levelStr = "INFO"; break;
        case QtWarningMsg:  levelStr = "WARN"; break;
        case QtCriticalMsg: levelStr = "CRITICAL"; break;
        case QtFatalMsg:    levelStr = "FATAL"; break;
    }

    QString formatted;
    if (context.file && context.line > 0) {
        formatted = QStringLiteral("[%1:%2] %3").arg(QString::fromUtf8(context.file)).arg(context.line).arg(msg);
    } else {
        formatted = msg;
    }

    log(levelStr, formatted);

    if (type == QtFatalMsg) {
        abort();
    }
}

void init()
{
    std::lock_guard<std::mutex> lock(s_logMutex);

    // Logging to file is switched OFF by default (can be re-enabled on demand via KARYON_DEBUG_LOG=1)
    if (qEnvironmentVariableIntValue("KARYON_DEBUG_LOG") == 1) {
        s_fileLoggingEnabled = true;
    }

    if (!s_fileLoggingEnabled) {
        return;
    }

    QString downloadsDir = QDir::homePath() + QStringLiteral("/Downloads");
    QDir().mkpath(downloadsDir);
    s_logPath = downloadsDir + QStringLiteral("/karyon-debug.log");

    // Im Append-Modus öffnen, damit vorherige Logs nicht zwingend sofort gelöscht werden,
    // aber mit klarem Session-Start-Banner versehen
    s_logFd = open(s_logPath.toUtf8().constData(), O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);

    qInstallMessageHandler(qtDebugMessageHandler);
    installCrashHandler();

    QString banner = QStringLiteral(
        "\n"
        "################################################################################\n"
        "#                     KARYON DEBUG SESSION STARTED                             #\n"
        "# Timestamp: %1                                        #\n"
        "# PID: %2                                                                  #\n"
        "# Logfile: %3                             #\n"
        "################################################################################\n\n"
    ).arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")))
     .arg(getpid())
     .arg(s_logPath);

    QByteArray bytes = banner.toUtf8();
    if (s_logFd >= 0) {
        (void)write(s_logFd, bytes.constData(), bytes.size());
        fsync(s_logFd);
    }
}

void log(const char *category, const QString &message)
{
    log(category, message.toUtf8().constData());
}

void log(const char *category, const char *message)
{
    if (!s_fileLoggingEnabled || s_logFd < 0) {
        return;
    }

    std::lock_guard<std::mutex> lock(s_logMutex);

    QString ts = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"));
    quintptr threadId = reinterpret_cast<quintptr>(QThread::currentThreadId());

    char line[4096];
    int len = snprintf(line, sizeof(line), "[%s] [Th:0x%lx] [%-10s] %s\n",
                       ts.toUtf8().constData(),
                       static_cast<unsigned long>(threadId),
                       category ? category : "LOG",
                       message ? message : "");

    if (len > 0) {
        if (s_logFd >= 0) {
            (void)write(s_logFd, line, len);
            fsync(s_logFd);
        }
    }
}

void flush()
{
    if (!s_fileLoggingEnabled || s_logFd < 0) return;
    std::lock_guard<std::mutex> lock(s_logMutex);
    if (s_logFd >= 0) {
        fsync(s_logFd);
    }
}

} // namespace DebugLogger
