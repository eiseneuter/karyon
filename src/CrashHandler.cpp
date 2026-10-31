#include "CrashHandler.h"

#include <csignal>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/utsname.h>
#include <execinfo.h>
#include <cxxabi.h>
#include <dlfcn.h>

#include <atomic>
#include <string>
#include <vector>

#include <QApplication>
#include <QDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QClipboard>
#include <QDesktopServices>
#include <QUrl>
#include <QProcess>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QDateTime>
#include <QFontDatabase>
#include <QTimer>
#include <QStyle>

namespace {

static char s_lastFatalMsg[2048] = {0};
static std::atomic<bool> s_crashOccurred{false};
static std::atomic<bool> s_cleanShutdownRequested{false};

const char *signalName(int sig)
{
    switch (sig) {
    case SIGSEGV: return "SIGSEGV (Speicherzugriffsfehler / Segmentation fault)";
    case SIGABRT: return "SIGABRT (Abbruch / Aborted)";
    case SIGBUS:  return "SIGBUS (Bus-Zugriffsfehler / Bus error)";
    case SIGFPE:  return "SIGFPE (Gleitkommafehler / Floating point exception)";
    case SIGILL:  return "SIGILL (Ungültige Maschineninstruktion / Illegal instruction)";
    case SIGTRAP: return "SIGTRAP (Trace/Breakpoint Trap)";
    case SIGTERM: return "SIGTERM (Beendigungssignal / Termination request)";
    case SIGHUP:  return "SIGHUP (Hangup / Terminal geschlossen)";
    case SIGINT:  return "SIGINT (Interrupt / Strg+C)";
    case SIGQUIT: return "SIGQUIT (Quit-Signal mit Core Dump)";
    default:      return "Unbekanntes Signal";
    }
}

const char *segvCodeString(int code)
{
    switch (code) {
    case SEGV_MAPERR: return "SEGV_MAPERR (Adresse nicht zugeordnet / Address not mapped)";
    case SEGV_ACCERR: return "SEGV_ACCERR (Ungültige Zugriffsrechte / Invalid permissions)";
#ifdef SEGV_BNDERR
    case SEGV_BNDERR: return "SEGV_BNDERR (Grenzwertprüfung fehlgeschlagen / Bounds checking failed)";
#endif
#ifdef SEGV_PKUERR
    case SEGV_PKUERR: return "SEGV_PKUERR (Schutzschlüssel-Fehler / Protection key error)";
#endif
    default: return "SEGV Code";
    }
}

void writeString(int fd, const char *str)
{
    if (fd < 0 || !str) return;
    [[maybe_unused]] auto w = write(fd, str, strlen(str));
}

void getProcessInfo(pid_t pid, char *nameBuf, size_t nameBufSize)
{
    if (!nameBuf || nameBufSize == 0) return;
    nameBuf[0] = '\0';
    if (pid <= 0) {
        snprintf(nameBuf, nameBufSize, "Kernel / Unbekannt (PID <= 0)");
        return;
    }

    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/cmdline", pid);
    int pfd = open(path, O_RDONLY);
    if (pfd >= 0) {
        ssize_t n = read(pfd, nameBuf, nameBufSize - 1);
        close(pfd);
        if (n > 0) {
            nameBuf[n] = '\0';
            for (ssize_t i = 0; i < n - 1; ++i) {
                if (nameBuf[i] == '\0') nameBuf[i] = ' ';
            }
            return;
        }
    }

    snprintf(path, sizeof(path), "/proc/%d/comm", pid);
    pfd = open(path, O_RDONLY);
    if (pfd >= 0) {
        ssize_t n = read(pfd, nameBuf, nameBufSize - 1);
        close(pfd);
        if (n > 0) {
            if (nameBuf[n - 1] == '\n') n--;
            nameBuf[n] = '\0';
            return;
        }
    }

    snprintf(nameBuf, nameBufSize, "Prozess %d (bereits beendet)", pid);
}

void spawnReporterProcess(const char *reportFile, const char *title)
{
    char exePath[512] = {0};
    ssize_t exeLen = readlink("/proc/self/exe", exePath, sizeof(exePath) - 1);
    if (exeLen > 0) {
        exePath[exeLen] = '\0';
    }

    pid_t child = fork();
    if (child == 0) {
        sigset_t set;
        sigemptyset(&set);
        sigprocmask(SIG_SETMASK, &set, nullptr);
        signal(SIGSEGV, SIG_DFL);
        signal(SIGABRT, SIG_DFL);
        signal(SIGBUS,  SIG_DFL);
        signal(SIGTERM, SIG_DFL);
        signal(SIGHUP,  SIG_DFL);
        signal(SIGINT,  SIG_DFL);

        const char *appImageEnv = getenv("APPIMAGE");
        if (appImageEnv && appImageEnv[0] != '\0') {
            execl(appImageEnv, appImageEnv, "--crash-reporter", reportFile, (char *)nullptr);
        }

        if (exeLen > 0) {
            execl(exePath, exePath, "--crash-reporter", reportFile, (char *)nullptr);
        }

        // Fallbacks
        execlp("kdialog", "kdialog", "--title", title ? title : "Karyon", "--textbox", reportFile, "850", "600", (char *)nullptr);
        execlp("zenity", "zenity", "--text-info", (std::string("--title=") + (title ? title : "Karyon")).c_str(), (std::string("--filename=") + reportFile).c_str(), (char *)nullptr);
        _exit(1);
    } else if (child > 0) {
        int status = 0;
        waitpid(child, &status, 0);
    }
}

void posixCrashHandler(int sig, siginfo_t *info, void * /*ucontext*/)
{
    if (s_crashOccurred.exchange(true)) {
        _exit(134);
    }

    const char *home = getenv("HOME");
    char crashDir[512] = {0};
    if (home) {
        snprintf(crashDir, sizeof(crashDir), "%s/.config/karyon/crashes", home);
    } else {
        snprintf(crashDir, sizeof(crashDir), "/tmp/karyon-crashes");
    }
    mkdir(crashDir, 0755);

    time_t now = time(nullptr);
    struct tm tmNow;
    localtime_r(&now, &tmNow);

    char timeStr[64] = {0};
    strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &tmNow);

    char reportFile[512] = {0};
    char fileStamp[64] = {0};
    strftime(fileStamp, sizeof(fileStamp), "%Y-%m-%d_%H-%M-%S", &tmNow);
    snprintf(reportFile, sizeof(reportFile), "%s/crash_%s.log", crashDir, fileStamp);

    char latestFile[512] = {0};
    snprintf(latestFile, sizeof(latestFile), "%s/latest_crash.log", crashDir);

    int fd = open(reportFile, O_WRONLY | O_CREAT | O_TRUNC, 0644);

    auto writeToOutputs = [fd](const char *text) {
        writeString(STDERR_FILENO, text);
        if (fd >= 0) {
            writeString(fd, text);
        }
    };

    writeToOutputs("================================================================================\n");
    writeToOutputs("                           KARYON ABSTURZBERICHT                                \n");
    writeToOutputs("================================================================================\n\n");

    writeToOutputs("Zeitpunkt:        ");
    writeToOutputs(timeStr);
    writeToOutputs("\nSignal:           ");
    writeToOutputs(signalName(sig));
    writeToOutputs("\nSignal-Nummer:    ");
    char sigNumBuf[16];
    snprintf(sigNumBuf, sizeof(sigNumBuf), "%d\n", sig);
    writeToOutputs(sigNumBuf);

    if (info) {
        if (sig == SIGSEGV || sig == SIGBUS) {
            char addrBuf[128];
            snprintf(addrBuf, sizeof(addrBuf), "Fehleradresse:    %p\nFehler-Ursache:   %s (%d)\n",
                     info->si_addr, (sig == SIGSEGV ? segvCodeString(info->si_code) : "BUS"), info->si_code);
            writeToOutputs(addrBuf);
        }
    }

    char exePath[512] = {0};
    ssize_t exeLen = readlink("/proc/self/exe", exePath, sizeof(exePath) - 1);
    if (exeLen > 0) {
        exePath[exeLen] = '\0';
        writeToOutputs("Programm:         ");
        writeToOutputs(exePath);
        writeToOutputs("\n");
    }

    struct utsname uts;
    if (uname(&uts) == 0) {
        char sysBuf[512];
        snprintf(sysBuf, sizeof(sysBuf), "System:           %s %s %s %s\n", uts.sysname, uts.nodename, uts.release, uts.machine);
        writeToOutputs(sysBuf);
    }

    char pidBuf[64];
    snprintf(pidBuf, sizeof(pidBuf), "PID:              %d\n", getpid());
    writeToOutputs(pidBuf);

    if (s_lastFatalMsg[0] != '\0') {
        writeToOutputs("\n--- Letzte Fehlermeldung (Qt/Framework) ---\n");
        writeToOutputs(s_lastFatalMsg);
        writeToOutputs("\n");
    }

    writeToOutputs("\n--- Aufruf-Stapel (Call Stack) ---\n");
    void *callstack[128];
    int frames = backtrace(callstack, 128);
    char **symbols = backtrace_symbols(callstack, frames);

    if (symbols) {
        for (int i = 0; i < frames; ++i) {
            std::string line = symbols[i];
            size_t openParen = line.find('(');
            size_t plus = line.find('+', openParen);
            if (openParen != std::string::npos && plus != std::string::npos && plus > openParen + 1) {
                std::string sym = line.substr(openParen + 1, plus - openParen - 1);
                int status = -1;
                char *demangled = abi::__cxa_demangle(sym.c_str(), nullptr, nullptr, &status);
                if (status == 0 && demangled) {
                    line = line.substr(0, openParen + 1) + demangled + line.substr(plus);
                }
                free(demangled);
            }
            char frameIdx[16];
            snprintf(frameIdx, sizeof(frameIdx), "#%-2d ", i);
            writeToOutputs(frameIdx);
            writeToOutputs(line.c_str());
            writeToOutputs("\n");
        }
        free(symbols);
    } else {
        writeToOutputs("Stack-Trace konnte nicht generiert werden.\n");
    }

    writeToOutputs("\n================================================================================\n");

    if (fd >= 0) {
        fsync(fd);
        close(fd);
        unlink(latestFile);
        link(reportFile, latestFile);
    }

    char shutLog[512] = {0};
    snprintf(shutLog, sizeof(shutLog), "%s/shutdown.log", crashDir);
    int sfd = open(shutLog, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (sfd >= 0) {
        char entry[512];
        snprintf(entry, sizeof(entry), "[%s] CRASH: Signal %d (%s) bei PID %d\n", timeStr, sig, signalName(sig), getpid());
        writeString(sfd, entry);
        close(sfd);
    }

    spawnReporterProcess(reportFile, "Karyon Absturz");
    _exit(134);
}

void posixTerminationHandler(int sig, siginfo_t *info, void * /*ucontext*/)
{
    if (s_crashOccurred.exchange(true)) {
        _exit(128 + sig);
    }

    const char *home = getenv("HOME");
    char crashDir[512] = {0};
    if (home) {
        snprintf(crashDir, sizeof(crashDir), "%s/.config/karyon/crashes", home);
    } else {
        snprintf(crashDir, sizeof(crashDir), "/tmp/karyon-crashes");
    }
    mkdir(crashDir, 0755);

    time_t now = time(nullptr);
    struct tm tmNow;
    localtime_r(&now, &tmNow);

    char timeStr[64] = {0};
    strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &tmNow);

    pid_t senderPid = info ? info->si_pid : 0;
    uid_t senderUid = info ? info->si_uid : 0;
    int sigCode = info ? info->si_code : 0;

    char senderCmd[512] = {0};
    getProcessInfo(senderPid, senderCmd, sizeof(senderCmd));

    char shutLog[512] = {0};
    snprintf(shutLog, sizeof(shutLog), "%s/shutdown.log", crashDir);

    const char *sName = (sig == SIGTERM ? "SIGTERM" :
                        (sig == SIGHUP  ? "SIGHUP"  :
                        (sig == SIGINT  ? "SIGINT"  :
                        (sig == SIGQUIT ? "SIGQUIT" : "SIGNAL"))));

    // 1. Reguläres Beenden (vom Benutzer über Tray/Settings initiiert)
    if (s_cleanShutdownRequested.load()) {
        int sfd = open(shutLog, O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (sfd >= 0) {
            char entry[1024];
            snprintf(entry, sizeof(entry), "[%s] Reguläres Beenden: Signal %s (%d) empfangen (Sender PID: %d, UID: %d)\n",
                     timeStr, sName, sig, senderPid, senderUid);
            writeString(sfd, entry);
            close(sfd);
        }
        _exit(0);
    }

    // 2. Interaktiver Terminalabbruch (Strg+C im Vordergrund)
    if (sig == SIGINT && isatty(STDIN_FILENO) && (senderPid == getpid() || senderPid == 0)) {
        int sfd = open(shutLog, O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (sfd >= 0) {
            char entry[512];
            snprintf(entry, sizeof(entry), "[%s] Benutzerabbruch über Terminal (SIGINT / Strg+C)\n", timeStr);
            writeString(sfd, entry);
            close(sfd);
        }
        _exit(0);
    }

    // 3. UNERWARTETES Signal von außen (z.B. Moonlight, systemd, kill, KWin)
    char reportFile[512] = {0};
    char fileStamp[64] = {0};
    strftime(fileStamp, sizeof(fileStamp), "%Y-%m-%d_%H-%M-%S", &tmNow);
    snprintf(reportFile, sizeof(reportFile), "%s/exit_signal_%s.log", crashDir, fileStamp);

    char latestFile[512] = {0};
    snprintf(latestFile, sizeof(latestFile), "%s/latest_crash.log", crashDir);

    int fd = open(reportFile, O_WRONLY | O_CREAT | O_TRUNC, 0644);

    auto writeToOutputs = [fd](const char *text) {
        writeString(STDERR_FILENO, text);
        if (fd >= 0) {
            writeString(fd, text);
        }
    };

    writeToOutputs("================================================================================\n");
    writeToOutputs("      KARYON: UNERWARTETE BEENDIGUNG DURCH SIGNAL / UNEXPECTED TERMINATION      \n");
    writeToOutputs("================================================================================\n\n");

    writeToOutputs("Zeitpunkt:             ");
    writeToOutputs(timeStr);
    writeToOutputs("\nSignal:                ");
    writeToOutputs(sName);
    writeToOutputs("\nSignal-Code:           ");
    char numBuf[64];
    snprintf(numBuf, sizeof(numBuf), "%d (si_code=%d)\n", sig, sigCode);
    writeToOutputs(numBuf);

    writeToOutputs("Absender-PID:          ");
    snprintf(numBuf, sizeof(numBuf), "%d\n", senderPid);
    writeToOutputs(numBuf);

    writeToOutputs("Absender-UID:          ");
    snprintf(numBuf, sizeof(numBuf), "%d\n", senderUid);
    writeToOutputs(numBuf);

    writeToOutputs("Absender-Befehl:       ");
    writeToOutputs(senderCmd);
    writeToOutputs("\n\n");

    writeToOutputs("Karyon-PID:            ");
    snprintf(numBuf, sizeof(numBuf), "%d\n", getpid());
    writeToOutputs(numBuf);

    writeToOutputs("\nHinweis:\n");
    writeToOutputs("Karyon wurde durch ein externes Signal beendet (weder über das Tray-Menü\n");
    writeToOutputs("noch über das Einstellungsmenü veranlasst).\n");
    writeToOutputs("Mögliche Ursachen:\n");
    writeToOutputs("- Ein anderes Programm oder Spiel (z.B. Moonlight, Steam, KWin) hat Karyon beendet.\n");
    writeToOutputs("- Ein Systemdienst (systemd, session-manager) hat das Signal gesendet.\n");
    writeToOutputs("- Befehl wie 'kill', 'killall' oder 'pkill' wurde aufgerufen.\n");
    writeToOutputs("================================================================================\n");

    if (fd >= 0) {
        fsync(fd);
        close(fd);
        unlink(latestFile);
        link(reportFile, latestFile);
    }

    int sfd = open(shutLog, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (sfd >= 0) {
        char entry[1024];
        snprintf(entry, sizeof(entry), "[%s] UNERWARTETES SIGNAL: %s (%d) von PID %d (%s), UID %d\n",
                 timeStr, sName, sig, senderPid, senderCmd, senderUid);
        writeString(sfd, entry);
        close(sfd);
    }

    spawnReporterProcess(reportFile, "Karyon Unerwartet Beendet");
    _exit(128 + sig);
}

typedef int (*XIOErrorHandlerFn)(void *);
typedef XIOErrorHandlerFn (*XSetIOErrorHandlerFn)(XIOErrorHandlerFn);

static int karyonXIOErrorHandler(void * /*dpy*/)
{
    if (s_crashOccurred.exchange(true)) {
        _exit(1);
    }

    const char *home = getenv("HOME");
    char crashDir[512] = {0};
    if (home) {
        snprintf(crashDir, sizeof(crashDir), "%s/.config/karyon/crashes", home);
    } else {
        snprintf(crashDir, sizeof(crashDir), "/tmp/karyon-crashes");
    }
    mkdir(crashDir, 0755);

    time_t now = time(nullptr);
    struct tm tmNow;
    localtime_r(&now, &tmNow);

    char timeStr[64] = {0};
    strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &tmNow);

    char reportFile[512] = {0};
    char fileStamp[64] = {0};
    strftime(fileStamp, sizeof(fileStamp), "%Y-%m-%d_%H-%M-%S", &tmNow);
    snprintf(reportFile, sizeof(reportFile), "%s/crash_x11_io_%s.log", crashDir, fileStamp);

    char latestFile[512] = {0};
    snprintf(latestFile, sizeof(latestFile), "%s/latest_crash.log", crashDir);

    int fd = open(reportFile, O_WRONLY | O_CREAT | O_TRUNC, 0644);

    auto writeToOutputs = [fd](const char *text) {
        writeString(STDERR_FILENO, text);
        if (fd >= 0) {
            writeString(fd, text);
        }
    };

    writeToOutputs("================================================================================\n");
    writeToOutputs("                 KARYON: FATALER X11 I/O-FEHLER / X11 IO ERROR                  \n");
    writeToOutputs("================================================================================\n\n");
    writeToOutputs("Zeitpunkt:             ");
    writeToOutputs(timeStr);
    writeToOutputs("\nBeschreibung:          Die Verbindung zum X-Server / XWayland wurde unerwartet unterbrochen.\n");
    writeToOutputs("Fehlerursache:         Fatal IO error on X server (XSetIOErrorHandler ausgelöst)\n");
    char pidBuf[64];
    snprintf(pidBuf, sizeof(pidBuf), "Karyon-PID:            %d\n\n", getpid());
    writeToOutputs(pidBuf);
    writeToOutputs("Mögliche Ursachen:\n");
    writeToOutputs("- Der XWayland-Server wurde neu gestartet oder ist abgestürzt.\n");
    writeToOutputs("- Ein Vollbild-Spiel oder Streaming-Client (z.B. Moonlight) hat die Grafiksession übernommen.\n");
    writeToOutputs("- Die Wayland/XWayland-Verbindung wurde von KWin getrennt.\n");
    writeToOutputs("================================================================================\n");

    if (fd >= 0) {
        fsync(fd);
        close(fd);
        unlink(latestFile);
        link(reportFile, latestFile);
    }

    char shutLog[512] = {0};
    snprintf(shutLog, sizeof(shutLog), "%s/shutdown.log", crashDir);
    int sfd = open(shutLog, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (sfd >= 0) {
        char entry[512];
        snprintf(entry, sizeof(entry), "[%s] FATALER X11 I/O FEHLER: Verbindung zum X-Server/XWayland unterbrochen!\n", timeStr);
        writeString(sfd, entry);
        close(sfd);
    }

    spawnReporterProcess(reportFile, "Karyon X11 Fehler");
    _exit(1);
    return 0;
}

static void installX11ErrorHandler()
{
    void *libx11 = dlopen("libX11.so.6", RTLD_LAZY | RTLD_GLOBAL);
    if (!libx11) {
        libx11 = dlopen("libX11.so", RTLD_LAZY | RTLD_GLOBAL);
    }
    if (libx11) {
        auto xSetIO = reinterpret_cast<XSetIOErrorHandlerFn>(dlsym(libx11, "XSetIOErrorHandler"));
        if (xSetIO) {
            xSetIO(karyonXIOErrorHandler);
        }
    }
}

} // namespace

void CrashHandler::install()
{
    // 1. SIGPIPE ignorieren, damit abgebrochene Sockets/Pipes Karyon nie still beenden
    signal(SIGPIPE, SIG_IGN);

    // 2. Alternativer Signal-Stack für Stack-Overflow-Sicherheit
    static std::vector<char> altStack(SIGSTKSZ * 4);
    stack_t ss;
    ss.ss_sp = altStack.data();
    ss.ss_size = altStack.size();
    ss.ss_flags = 0;
    sigaltstack(&ss, nullptr);

    // 3. Fatal Crash Signale
    struct sigaction saCrash;
    memset(&saCrash, 0, sizeof(saCrash));
    saCrash.sa_sigaction = posixCrashHandler;
    saCrash.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&saCrash.sa_mask);

    sigaction(SIGSEGV, &saCrash, nullptr);
    sigaction(SIGABRT, &saCrash, nullptr);
    sigaction(SIGBUS,  &saCrash, nullptr);
    sigaction(SIGFPE,  &saCrash, nullptr);
    sigaction(SIGILL,  &saCrash, nullptr);
    sigaction(SIGTRAP, &saCrash, nullptr);

    // 4. Termination Signale (SIGTERM, SIGHUP, SIGINT, SIGQUIT)
    struct sigaction saTerm;
    memset(&saTerm, 0, sizeof(saTerm));
    saTerm.sa_sigaction = posixTerminationHandler;
    saTerm.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&saTerm.sa_mask);

    sigaction(SIGTERM, &saTerm, nullptr);
    sigaction(SIGHUP,  &saTerm, nullptr);
    sigaction(SIGINT,  &saTerm, nullptr);
    sigaction(SIGQUIT, &saTerm, nullptr);

    // 5. X11 I/O Error Handler
    installX11ErrorHandler();

    // 6. Unbehandelte C++ Exceptions abfangen
    std::set_terminate([]() {
        std::exception_ptr p = std::current_exception();
        if (p) {
            try {
                std::rethrow_exception(p);
            } catch (const std::exception &e) {
                CrashHandler::setLastFatalMessage(QStringLiteral("Unbehandelte C++ Ausnahme: ") + QString::fromUtf8(e.what()));
            } catch (...) {
                CrashHandler::setLastFatalMessage(QStringLiteral("Unbehandelte unbekannte C++ Ausnahme"));
            }
        } else {
            CrashHandler::setLastFatalMessage(QStringLiteral("std::terminate aufgerufen"));
        }
        abort();
    });

    // 7. atexit Hook für Shutdown-Logging
    std::atexit([]() {
        CrashHandler::logShutdown("atexit");
    });
}

void CrashHandler::markCleanShutdownRequested()
{
    s_cleanShutdownRequested.store(true);
}

bool CrashHandler::isCleanShutdownRequested()
{
    return s_cleanShutdownRequested.load();
}

void CrashHandler::logStartup(int argc, char *argv[])
{
    const char *home = getenv("HOME");
    char crashDir[512] = {0};
    if (home) {
        snprintf(crashDir, sizeof(crashDir), "%s/.config/karyon/crashes", home);
    } else {
        snprintf(crashDir, sizeof(crashDir), "/tmp/karyon-crashes");
    }
    mkdir(crashDir, 0755);

    time_t now = time(nullptr);
    struct tm tmNow;
    localtime_r(&now, &tmNow);
    char timeStr[64] = {0};
    strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &tmNow);

    char shutLog[512] = {0};
    snprintf(shutLog, sizeof(shutLog), "%s/shutdown.log", crashDir);

    int sfd = open(shutLog, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (sfd >= 0) {
        char entry[2048];
        int offset = snprintf(entry, sizeof(entry), "[%s] START: PID %d", timeStr, getpid());
        for (int i = 0; i < argc && offset < (int)sizeof(entry) - 64; ++i) {
            offset += snprintf(entry + offset, sizeof(entry) - offset, " %s", argv[i]);
        }
        snprintf(entry + offset, sizeof(entry) - offset, "\n");
        writeString(sfd, entry);
        close(sfd);
    }
}

void CrashHandler::logShutdown(const char *reason)
{
    const char *home = getenv("HOME");
    char crashDir[512] = {0};
    if (home) {
        snprintf(crashDir, sizeof(crashDir), "%s/.config/karyon/crashes", home);
    } else {
        snprintf(crashDir, sizeof(crashDir), "/tmp/karyon-crashes");
    }

    time_t now = time(nullptr);
    struct tm tmNow;
    localtime_r(&now, &tmNow);
    char timeStr[64] = {0};
    strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &tmNow);

    char shutLog[512] = {0};
    snprintf(shutLog, sizeof(shutLog), "%s/shutdown.log", crashDir);

    int sfd = open(shutLog, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (sfd >= 0) {
        char entry[512];
        snprintf(entry, sizeof(entry), "[%s] STOP: PID %d, Grund: %s\n", timeStr, getpid(), reason ? reason : "Normales Beenden");
        writeString(sfd, entry);
        close(sfd);
    }
}

void CrashHandler::setLastFatalMessage(const QString &msg)
{
    if (msg.isEmpty()) return;
    QByteArray utf = msg.toUtf8();
    strncpy(s_lastFatalMsg, utf.constData(), sizeof(s_lastFatalMsg) - 1);
    s_lastFatalMsg[sizeof(s_lastFatalMsg) - 1] = '\0';
}

QString CrashHandler::crashesDirectory()
{
    return QDir::homePath() + QStringLiteral("/.config/karyon/crashes");
}

int CrashHandler::runCrashReporterDialog(const QString &crashReportPath)
{
    QString content;
    QFile f(crashReportPath);
    if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        content = QString::fromUtf8(f.readAll());
        f.close();
    } else {
        content = QStringLiteral("Bericht konnte nicht gelesen werden aus:\n") + crashReportPath;
    }

    bool isSignalExit = content.contains(QStringLiteral("UNERWARTETE BEENDIGUNG DURCH SIGNAL"));
    bool isX11Exit = content.contains(QStringLiteral("FATALER X11 I/O-FEHLER"));

    QDialog dlg;
    dlg.resize(840, 600);
    dlg.setMinimumSize(600, 400);

    // Dunkles, professionelles Styling passend zu Karyon
    dlg.setStyleSheet(QStringLiteral(
        "QDialog { background-color: #1a1b26; color: #c0caf5; font-family: 'Segoe UI', 'Noto Sans', sans-serif; }"
        "QLabel { color: #c0caf5; }"
        "QPlainTextEdit { background-color: #16161e; color: #a9b1d6; border: 1px solid #292e42; border-radius: 6px; font-family: 'JetBrains Mono', 'Fira Code', 'DejaVu Sans Mono', 'Consolas', monospace; font-size: 11px; padding: 8px; selection-background-color: #3d59a1; }"
        "QPushButton { background-color: #24283b; color: #c0caf5; border: 1px solid #414868; border-radius: 6px; padding: 7px 16px; font-weight: bold; min-height: 24px; }"
        "QPushButton:hover { background-color: #414868; border-color: #7aa2f7; }"
        "QPushButton:pressed { background-color: #3b4261; }"
        "QPushButton#btnRestart { background-color: #7aa2f7; color: #1a1b26; border: none; }"
        "QPushButton#btnRestart:hover { background-color: #89b4fa; }"
        "QPushButton#btnCopy { background-color: #2ac3de; color: #1a1b26; border: none; }"
        "QPushButton#btnCopy:hover { background-color: #7dcfff; }"
    ));

    auto *mainLayout = new QVBoxLayout(&dlg);
    mainLayout->setContentsMargins(18, 18, 18, 18);
    mainLayout->setSpacing(12);

    // Header mit Icon & Text
    auto *headerLayout = new QHBoxLayout();
    headerLayout->setSpacing(12);

    auto *iconLabel = new QLabel(&dlg);
    QIcon iconObj = dlg.style()->standardIcon(isSignalExit ? QStyle::SP_MessageBoxWarning : QStyle::SP_MessageBoxCritical);
    iconLabel->setPixmap(iconObj.pixmap(36, 36));
    headerLayout->addWidget(iconLabel, 0, Qt::AlignTop);

    auto *titleLayout = new QVBoxLayout();
    titleLayout->setSpacing(3);

    auto *titleLabel = new QLabel(&dlg);
    auto *subLabel = new QLabel(&dlg);

    if (isSignalExit) {
        dlg.setWindowTitle(QStringLiteral("Karyon - Unerwartet Beendet"));
        titleLabel->setText(QStringLiteral("<b>Karyon wurde unerwartet durch ein externes Signal beendet</b>"));
        titleLabel->setStyleSheet(QStringLiteral("font-size: 16px; color: #e0af68; font-weight: bold;"));
        subLabel->setText(QStringLiteral("Ein anderes Programm oder das Betriebssystem hat ein Signal an Karyon gesendet. Details zum Absender:"));
    } else if (isX11Exit) {
        dlg.setWindowTitle(QStringLiteral("Karyon - Verbindung Unterbrochen"));
        titleLabel->setText(QStringLiteral("<b>Karyon: Verbindung zum X-Server verloren</b>"));
        titleLabel->setStyleSheet(QStringLiteral("font-size: 16px; color: #f7768e; font-weight: bold;"));
        subLabel->setText(QStringLiteral("Die Verbindung zum X-Server bzw. XWayland wurde unerwartet getrennt:"));
    } else {
        dlg.setWindowTitle(QStringLiteral("Karyon - Absturzbericht"));
        titleLabel->setText(QStringLiteral("<b>Karyon ist unerwartet abgestürzt</b>"));
        titleLabel->setStyleSheet(QStringLiteral("font-size: 16px; color: #f7768e; font-weight: bold;"));
        subLabel->setText(QStringLiteral("Ein interner Fehler hat zum Beenden von Karyon geführt. Der folgende Absturzbericht wurde automatisch gespeichert:"));
    }

    subLabel->setStyleSheet(QStringLiteral("font-size: 12px; color: #9aa5ce;"));
    subLabel->setWordWrap(true);
    titleLayout->addWidget(titleLabel);
    titleLayout->addWidget(subLabel);

    auto *pathLabel = new QLabel(QStringLiteral("<code>%1</code>").arg(crashReportPath), &dlg);
    pathLabel->setStyleSheet(QStringLiteral("font-size: 11px; color: #bb9af7;"));
    pathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    titleLayout->addWidget(pathLabel);

    headerLayout->addLayout(titleLayout, 1);
    mainLayout->addLayout(headerLayout);

    // Textfeld für Bericht
    auto *textEdit = new QPlainTextEdit(&dlg);
    textEdit->setPlainText(content);
    textEdit->setReadOnly(true);
    textEdit->setLineWrapMode(QPlainTextEdit::NoWrap);
    mainLayout->addWidget(textEdit, 1);

    // Aktions-Buttons
    auto *btnLayout = new QHBoxLayout();
    btnLayout->setSpacing(10);

    auto *btnCopy = new QPushButton(QStringLiteral("📋 In Zwischenablage kopieren"), &dlg);
    btnCopy->setObjectName(QStringLiteral("btnCopy"));
    btnLayout->addWidget(btnCopy);

    auto *btnOpenEditor = new QPushButton(QStringLiteral("📂 Im Editor öffnen"), &dlg);
    btnLayout->addWidget(btnOpenEditor);

    btnLayout->addStretch(1);

    auto *btnRestart = new QPushButton(QStringLiteral("🚀 Karyon neu starten"), &dlg);
    btnRestart->setObjectName(QStringLiteral("btnRestart"));
    btnLayout->addWidget(btnRestart);

    auto *btnClose = new QPushButton(QStringLiteral("Schließen"), &dlg);
    btnLayout->addWidget(btnClose);

    mainLayout->addLayout(btnLayout);

    // Button-Aktionen
    QObject::connect(btnCopy, &QPushButton::clicked, [&dlg, btnCopy, textEdit]() {
        QClipboard *clipboard = QGuiApplication::clipboard();
        if (clipboard) {
            clipboard->setText(textEdit->toPlainText());
            QString origText = btnCopy->text();
            btnCopy->setText(QStringLiteral("✓ In die Zwischenablage kopiert!"));
            QTimer::singleShot(2500, [btnCopy, origText]() {
                btnCopy->setText(origText);
            });
        }
    });

    QObject::connect(btnOpenEditor, &QPushButton::clicked, [crashReportPath]() {
        QDesktopServices::openUrl(QUrl::fromLocalFile(crashReportPath));
    });

    QObject::connect(btnRestart, &QPushButton::clicked, [&dlg]() {
        const char *appImageEnv = getenv("APPIMAGE");
        if (appImageEnv && appImageEnv[0] != '\0') {
            QProcess::startDetached(QString::fromUtf8(appImageEnv), QStringList());
        } else {
            char exePath[512] = {0};
            ssize_t len = readlink("/proc/self/exe", exePath, sizeof(exePath) - 1);
            if (len > 0) {
                exePath[len] = '\0';
                QProcess::startDetached(QString::fromUtf8(exePath), QStringList());
            }
        }
        dlg.accept();
    });

    QObject::connect(btnClose, &QPushButton::clicked, &dlg, &QDialog::reject);

    return dlg.exec();
}
