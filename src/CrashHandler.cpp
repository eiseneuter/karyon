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

const char *signalName(int sig)
{
    switch (sig) {
    case SIGSEGV: return "SIGSEGV (Speicherzugriffsfehler / Segmentation fault)";
    case SIGABRT: return "SIGABRT (Abbruch / Aborted)";
    case SIGBUS:  return "SIGBUS (Bus-Zugriffsfehler / Bus error)";
    case SIGFPE:  return "SIGFPE (Gleitkommafehler / Floating point exception)";
    case SIGILL:  return "SIGILL (Ungültige Maschineninstruktion / Illegal instruction)";
    case SIGTRAP: return "SIGTRAP (Trace/Breakpoint Trap)";
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

void writeFormatted(int fd, const char *prefix, unsigned long val, const char *suffix = "\n")
{
    if (fd < 0) return;
    char buf[64];
    snprintf(buf, sizeof(buf), "%s%lu%s", prefix, val, suffix);
    writeString(fd, buf);
}

void writeHex(int fd, const char *prefix, void *ptr, const char *suffix = "\n")
{
    if (fd < 0) return;
    char buf[64];
    snprintf(buf, sizeof(buf), "%s%p%s", prefix, ptr, suffix);
    writeString(fd, buf);
}

void posixSignalHandler(int sig, siginfo_t *info, void * /*ucontext*/)
{
    // Verhindere rekursive Signal-Behandlung
    if (s_crashOccurred.exchange(true)) {
        _exit(134);
    }

    // 1. Crash-Verzeichnis anlegen
    const char *home = getenv("HOME");
    char crashDir[512] = {0};
    if (home) {
        snprintf(crashDir, sizeof(crashDir), "%s/.config/karyon/crashes", home);
    } else {
        snprintf(crashDir, sizeof(crashDir), "/tmp/karyon-crashes");
    }
    mkdir(crashDir, 0755);

    // 2. Zeitstempel & Dateipfade generieren
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

    // Executable-Pfad
    char exePath[512] = {0};
    ssize_t exeLen = readlink("/proc/self/exe", exePath, sizeof(exePath) - 1);
    if (exeLen > 0) {
        exePath[exeLen] = '\0';
        writeToOutputs("Programm:         ");
        writeToOutputs(exePath);
        writeToOutputs("\n");
    }

    // System-Informationen
    struct utsname uts;
    if (uname(&uts) == 0) {
        char sysBuf[512];
        snprintf(sysBuf, sizeof(sysBuf), "System:           %s %s %s %s\n", uts.sysname, uts.nodename, uts.release, uts.machine);
        writeToOutputs(sysBuf);
    }

    // Prozess & Threads
    char pidBuf[64];
    snprintf(pidBuf, sizeof(pidBuf), "PID:              %d\n", getpid());
    writeToOutputs(pidBuf);

    if (s_lastFatalMsg[0] != '\0') {
        writeToOutputs("\n--- Letzte Fehlermeldung (Qt/Framework) ---\n");
        writeToOutputs(s_lastFatalMsg);
        writeToOutputs("\n");
    }

    // 3. Stack Trace sammeln und demanglen
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

        // Kopie/Symlink auf latest_crash.log
        unlink(latestFile);
        link(reportFile, latestFile);
    }

    // 4. GUI-Crash-Reporter in einem separaten sauberen Prozess starten
    pid_t child = fork();
    if (child == 0) {
        // Kind-Prozess: Signale zurücksetzen
        sigset_t set;
        sigemptyset(&set);
        sigprocmask(SIG_SETMASK, &set, nullptr);
        signal(SIGSEGV, SIG_DFL);
        signal(SIGABRT, SIG_DFL);
        signal(SIGBUS,  SIG_DFL);

        const char *appImageEnv = getenv("APPIMAGE");
        if (appImageEnv && appImageEnv[0] != '\0') {
            execl(appImageEnv, appImageEnv, "--crash-reporter", reportFile, (char *)nullptr);
        }

        if (exeLen > 0) {
            execl(exePath, exePath, "--crash-reporter", reportFile, (char *)nullptr);
        }
        // Fallback falls karyon binary nicht gestartet werden kann
        execlp("kdialog", "kdialog", "--title", "Karyon Absturz", "--textbox", reportFile, "850", "600", (char *)nullptr);
        execlp("zenity", "zenity", "--text-info", "--title=Karyon Absturz", (std::string("--filename=") + reportFile).c_str(), (char *)nullptr);
        _exit(1);
    } else if (child > 0) {
        // Warten bis der Crash-Reporter Dialog vom Nutzer bedient/geschlossen wird
        // Hält auch den AppImage-Mount (/tmp/.mount_Karyon...) währenddessen am Leben!
        int status = 0;
        waitpid(child, &status, 0);
    }

    // Hauptprozess beenden
    _exit(134);
}

} // namespace

void CrashHandler::install()
{
    // 1. Alternativer Signal-Stack für Stack-Overflow-Sicherheit
    static std::vector<char> altStack(SIGSTKSZ * 4);
    stack_t ss;
    ss.ss_sp = altStack.data();
    ss.ss_size = altStack.size();
    ss.ss_flags = 0;
    sigaltstack(&ss, nullptr);

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = posixSignalHandler;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&sa.sa_mask);

    sigaction(SIGSEGV, &sa, nullptr);
    sigaction(SIGABRT, &sa, nullptr);
    sigaction(SIGBUS,  &sa, nullptr);
    sigaction(SIGFPE,  &sa, nullptr);
    sigaction(SIGILL,  &sa, nullptr);
    sigaction(SIGTRAP, &sa, nullptr);

    // Unbehandelte C++ Exceptions abfangen
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
        content = QStringLiteral("Absturzbericht konnte nicht gelesen werden aus:\n") + crashReportPath;
    }

    QDialog dlg;
    dlg.setWindowTitle(QStringLiteral("Karyon - Absturzbericht"));
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
    QIcon errorIcon = dlg.style()->standardIcon(QStyle::SP_MessageBoxCritical);
    iconLabel->setPixmap(errorIcon.pixmap(36, 36));
    headerLayout->addWidget(iconLabel, 0, Qt::AlignTop);

    auto *titleLayout = new QVBoxLayout();
    titleLayout->setSpacing(3);

    auto *titleLabel = new QLabel(QStringLiteral("<b>Karyon ist unerwartet abgestürzt</b>"), &dlg);
    titleLabel->setStyleSheet(QStringLiteral("font-size: 16px; color: #f7768e; font-weight: bold;"));
    titleLayout->addWidget(titleLabel);

    auto *subLabel = new QLabel(QStringLiteral("Ein interner Fehler hat zum Beenden von Karyon geführt. Der folgende Absturzbericht wurde automatisch gespeichert:"), &dlg);
    subLabel->setStyleSheet(QStringLiteral("font-size: 12px; color: #9aa5ce;"));
    subLabel->setWordWrap(true);
    titleLayout->addWidget(subLabel);

    auto *pathLabel = new QLabel(QStringLiteral("<code>%1</code>").arg(crashReportPath), &dlg);
    pathLabel->setStyleSheet(QStringLiteral("font-size: 11px; color: #bb9af7;"));
    pathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    titleLayout->addWidget(pathLabel);

    headerLayout->addLayout(titleLayout, 1);
    mainLayout->addLayout(headerLayout);

    // Stack Trace Textfeld
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
