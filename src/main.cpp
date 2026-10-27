#include <QApplication>
#include <QCursor>
#include <QDateTime>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QAction>
#include <QDialog>
#include <QVBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QTimer>
#include <QFile>
#include <QLockFile>
#include <QStandardPaths>
#include <QProcess>
#include <QtDBus>
#include <QOpenGLContext>
#include <QSurfaceFormat>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <csignal>
#include <unistd.h>
#include <iostream>
#include "DebugLogger.h"
#include "CircleOverlay.h"
#include "InputMonitor.h"
#include "KWinBridge.h"
#include "AppManager.h"
#include "TrayManager.h"
#include "RecentFilesManager.h"
#include "SessionManager.h"
#include "SessionDialogController.h"
#include "SettingsManager.h"
#include "SettingsController.h"
#include "GestureManager.h"
#include "GestureTrail.h"
#include "PermissionManager.h"
#include "PermissionDialogController.h"
#include "InfoDialogController.h"
#include "ProcessUtils.h"

#include <thread>
#include <cstring>
#include <dlfcn.h>
#include <sys/wait.h>

namespace {
void handleSignal(int /*sig*/)
{
    _exit(0);
}

static int g_origStderr = -1;

void setupStderrFilter()
{
    int pipeFds[2];
    if (pipe(pipeFds) != 0) return;

    g_origStderr = dup(STDERR_FILENO);
    if (g_origStderr < 0) {
        close(pipeFds[0]);
        close(pipeFds[1]);
        return;
    }

    if (dup2(pipeFds[1], STDERR_FILENO) < 0) {
        close(pipeFds[0]);
        close(pipeFds[1]);
        close(g_origStderr);
        g_origStderr = -1;
        return;
    }
    close(pipeFds[1]);

    std::thread([readFd = pipeFds[0]]() {
        FILE *in = fdopen(readFd, "r");
        if (!in) return;
        char buffer[2048];
        while (fgets(buffer, sizeof(buffer), in)) {
            // Suppress external fontconfig XML schema warnings & sycoca layout warnings
            if (strstr(buffer, "Fontconfig warning:") != nullptr ||
                strstr(buffer, "Fontconfig error:") != nullptr ||
                strstr(buffer, "kf.service.sycoca:") != nullptr) {
                continue;
            }
            if (g_origStderr >= 0) {
                write(g_origStderr, buffer, strlen(buffer));
            }
        }
        fclose(in);
    }).detach();
}

void quietMessageHandler(QtMsgType type, const QMessageLogContext &/*context*/, const QString &msg)
{
    // Filter out known noisy framework warnings
    if (msg.contains(QLatin1String("The menu spec file")) ||
        msg.contains(QLatin1String("sycoca")) ||
        msg.contains(QLatin1String("Fontconfig")) ||
        msg.contains(QLatin1String("invalid attribute")) ||
        msg.contains(QLatin1String("invalid constant"))) {
        return;
    }
    if (type == QtFatalMsg) {
        std::string out = "Fatal: " + msg.toStdString() + "\n";
        int fd = (g_origStderr >= 0) ? g_origStderr : STDERR_FILENO;
        (void)write(fd, out.data(), out.size());
        abort();
    } else if (type == QtCriticalMsg || type == QtWarningMsg) {
        std::string out = (type == QtCriticalMsg ? "Error: " : "Warning: ") + msg.toStdString() + "\n";
        int fd = (g_origStderr >= 0) ? g_origStderr : STDERR_FILENO;
        (void)write(fd, out.data(), out.size());
    }
}

enum class GpuBackend {
    Glx,
    Egl,
    Software
};

GpuBackend probeHardwareGraphics()
{
    auto probeGlx = []() -> bool {
        void *libx11 = dlopen("libX11.so.6", RTLD_LAZY);
        if (!libx11) libx11 = dlopen("libX11.so", RTLD_LAZY);
        if (!libx11) return false;

        void *libglx = dlopen("libGLX.so.0", RTLD_LAZY);
        if (!libglx) libglx = dlopen("libGL.so.1", RTLD_LAZY);
        if (!libglx) libglx = dlopen("libGL.so", RTLD_LAZY);
        if (!libglx) { dlclose(libx11); return false; }

        typedef void* (*XOpenDisplayFn)(const char*);
        typedef int (*XCloseDisplayFn)(void*);
        typedef int (*glXQueryExtensionFn)(void*, int*, int*);
        typedef int (*glXQueryVersionFn)(void*, int*, int*);
        typedef void* (*glXGetFBConfigsFn)(void*, int, int*);
        typedef int (*XDefaultScreenFn)(void*);
        typedef int (*XFreeFn)(void*);

        auto xOpenDisplay = reinterpret_cast<XOpenDisplayFn>(dlsym(libx11, "XOpenDisplay"));
        auto xCloseDisplay = reinterpret_cast<XCloseDisplayFn>(dlsym(libx11, "XCloseDisplay"));
        auto glXQueryExt = reinterpret_cast<glXQueryExtensionFn>(dlsym(libglx, "glXQueryExtension"));
        auto glXQueryVer = reinterpret_cast<glXQueryVersionFn>(dlsym(libglx, "glXQueryVersion"));
        auto glXGetFB = reinterpret_cast<glXGetFBConfigsFn>(dlsym(libglx, "glXGetFBConfigs"));
        auto xDefaultScreen = reinterpret_cast<XDefaultScreenFn>(dlsym(libx11, "XDefaultScreen"));
        auto xFree = reinterpret_cast<XFreeFn>(dlsym(libx11, "XFree"));

        if (!xOpenDisplay || !xCloseDisplay || !glXQueryExt || !glXQueryVer || !glXGetFB || !xDefaultScreen) {
            dlclose(libglx); dlclose(libx11); return false;
        }

        void *dpy = xOpenDisplay(nullptr);
        if (!dpy) { dlclose(libglx); dlclose(libx11); return false; }

        int err = 0, ev = 0, maj = 0, min = 0;
        bool ok = glXQueryExt(dpy, &err, &ev) && glXQueryVer(dpy, &maj, &min) && (maj > 1 || (maj == 1 && min >= 3));
        if (ok) {
            int screen = xDefaultScreen(dpy);
            int nConfigs = 0;
            void *cfgs = glXGetFB(dpy, screen, &nConfigs);
            if (cfgs && nConfigs > 0) {
                if (xFree) xFree(cfgs);
            } else {
                ok = false;
            }
        }
        xCloseDisplay(dpy);
        dlclose(libglx);
        dlclose(libx11);
        return ok;
    };

    auto probeEgl = []() -> bool {
        void *libx11 = dlopen("libX11.so.6", RTLD_LAZY);
        if (!libx11) libx11 = dlopen("libX11.so", RTLD_LAZY);
        if (!libx11) return false;

        void *libegl = dlopen("libEGL.so.1", RTLD_LAZY);
        if (!libegl) libegl = dlopen("libEGL.so", RTLD_LAZY);
        if (!libegl) { dlclose(libx11); return false; }

        typedef void* (*XOpenDisplayFn)(const char*);
        typedef int (*XCloseDisplayFn)(void*);
        typedef void* (*eglGetPlatformDisplayFn)(unsigned int, void*, const void*);
        typedef void* (*eglGetDisplayFn)(void*);
        typedef int (*eglInitializeFn)(void*, int*, int*);
        typedef int (*eglTerminateFn)(void*);

        auto xOpenDisplay = reinterpret_cast<XOpenDisplayFn>(dlsym(libx11, "XOpenDisplay"));
        auto xCloseDisplay = reinterpret_cast<XCloseDisplayFn>(dlsym(libx11, "XCloseDisplay"));
        auto eglGetPlatformDisplay = reinterpret_cast<eglGetPlatformDisplayFn>(dlsym(libegl, "eglGetPlatformDisplay"));
        auto eglGetDisplay = reinterpret_cast<eglGetDisplayFn>(dlsym(libegl, "eglGetDisplay"));
        auto eglInitialize = reinterpret_cast<eglInitializeFn>(dlsym(libegl, "eglInitialize"));
        auto eglTerminate = reinterpret_cast<eglTerminateFn>(dlsym(libegl, "eglTerminate"));

        if (!xOpenDisplay || !xCloseDisplay || (!eglGetPlatformDisplay && !eglGetDisplay) || !eglInitialize) {
            dlclose(libegl); dlclose(libx11); return false;
        }

        void *dpy = xOpenDisplay(nullptr);
        if (!dpy) { dlclose(libegl); dlclose(libx11); return false; }

        void *edpy = nullptr;
        if (eglGetPlatformDisplay) {
            constexpr unsigned int EGL_PLATFORM_X11_KHR = 0x31D5;
            edpy = eglGetPlatformDisplay(EGL_PLATFORM_X11_KHR, dpy, nullptr);
        }
        if (!edpy && eglGetDisplay) {
            edpy = eglGetDisplay(dpy);
        }
        if (!edpy) { xCloseDisplay(dpy); dlclose(libegl); dlclose(libx11); return false; }

        int major = 0, minor = 0;
        int ok = eglInitialize(edpy, &major, &minor);
        if (ok && eglTerminate) eglTerminate(edpy);
        xCloseDisplay(dpy);
        dlclose(libegl);
        dlclose(libx11);
        return (ok != 0);
    };

    pid_t pid = fork();
    if (pid == 0) {
        // Preferred order: GLX first (mature X11/XWayland 32-bit ARGB visual support),
        // EGL second (hardware fallback for drivers without GLX),
        // Software third (fail-safe rasterizer).
        if (probeGlx()) {
            _exit(20);
        }
        if (probeEgl()) {
            _exit(10);
        }
        _exit(30);
    }

    if (pid < 0) {
        return GpuBackend::Software;
    }

    int status = 0;
    waitpid(pid, &status, 0);

    if (WIFSIGNALED(status)) {
        std::cout << "[Graphics] Hardware probe process terminated with signal " << WTERMSIG(status)
                  << " (driver crash caught and neutralized via sandbox isolation).\n";
        return GpuBackend::Software;
    }

    if (WIFEXITED(status)) {
        int code = WEXITSTATUS(status);
        if (code == 20) return GpuBackend::Glx;
        if (code == 10) return GpuBackend::Egl;
    }

    return GpuBackend::Software;
}
}

int main(int argc, char *argv[])
{
    DebugLogger::init();
    KLOG("Main", "Starting Karyon C++ Test Build (PID: %d)", getpid());
    setupStderrFilter();

    std::setlocale(LC_ALL, "");

    // Detect KDE Plasma / system locale with full fallbacks
    QString klang;
    QString plasmaLocaleRc = QDir::homePath() + QStringLiteral("/.config/plasma-localerc");
    if (QFile::exists(plasmaLocaleRc)) {
        QSettings plasmarc(plasmaLocaleRc, QSettings::IniFormat);
        klang = plasmarc.value(QStringLiteral("Formats/LC_TIME")).toString();
        if (klang.isEmpty()) {
            klang = plasmarc.value(QStringLiteral("Formats/LANG")).toString();
        }
        if (klang.isEmpty()) {
            klang = plasmarc.value(QStringLiteral("Translations/LANGUAGE")).toString().section(QLatin1Char(':'), 0, 0);
        }
    }
    if (klang.isEmpty()) {
        klang = QString::fromLocal8Bit(qgetenv("LC_ALL"));
    }
    if (klang.isEmpty()) {
        klang = QString::fromLocal8Bit(qgetenv("LC_TIME"));
    }
    if (klang.isEmpty()) {
        klang = QString::fromLocal8Bit(qgetenv("LANG"));
    }
    if (klang.isEmpty()) {
        klang = QString::fromLocal8Bit(qgetenv("LANGUAGE")).section(QLatin1Char(':'), 0, 0);
    }

    QString detectedLocaleStr = QStringLiteral("en_US");
    if (!klang.isEmpty() && klang.compare(QStringLiteral("C"), Qt::CaseInsensitive) != 0
        && !klang.startsWith(QStringLiteral("C."), Qt::CaseInsensitive)) {
        QString cleanLang = klang.section(QLatin1Char('.'), 0, 0).section(QLatin1Char('@'), 0, 0).trimmed();
        if (!cleanLang.isEmpty()) {
            QLocale candidate(cleanLang);
            if (candidate.language() != QLocale::C) {
                detectedLocaleStr = candidate.name();
            }
        }
    }
    SettingsManager::instance().setDetectedLocaleName(detectedLocaleStr);

    // Enforce English UI so all Qt dialogs (e.g. QColorDialog), buttons, and tooltips
    // remain strictly in English regardless of host system language.
    qputenv("LC_ALL", "C.UTF-8");
    qputenv("LANG", "C.UTF-8");
    qputenv("LANGUAGE", "en_US:en");
    std::setlocale(LC_ALL, "C.UTF-8");
    QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates));

    // Enforce QT_QPA_PLATFORM=xcb for direct X11/XWayland Override-Redirect (Rule 1)
    // Synchronous cursor positioning and frameless overlay window placement under cursor
    qputenv("QT_QPA_PLATFORM", "xcb");

    // -------------------------------------------------------------------------
    // Grafik-Backend & Hardware-Beschleunigung Absicherung (EGL -> GLX -> Software)
    // -------------------------------------------------------------------------
    // Ziel: Das QML-Overlay MUSS auf jedem System (auch CachyOS, VMs, NVIDIA-Wayland)
    // garantiert und ohne Absturz gezeichnet werden.
    // 1. EGL bevorzugen (modernster Standard für X11/XWayland und NVIDIA/Mesa).
    // 2. Fallback auf GLX für ältere X11-Server.
    // 3. Fallback auf Qt Quick Software-Rendering (CPU-Rasterizer, 100% ausfallsicher).
    // -------------------------------------------------------------------------
    if (qEnvironmentVariableIsSet("QT_XCB_GL_INTEGRATION")) {
        QString forced = QString::fromLocal8Bit(qgetenv("QT_XCB_GL_INTEGRATION"));
        std::cout << "[Graphics] User environment override detected: QT_XCB_GL_INTEGRATION="
                  << forced.toStdString() << "\n";
        if (forced == QLatin1String("none")) {
            std::cout << "[Graphics] Activating Qt Quick Software rendering fallback.\n";
            qputenv("QT_QUICK_BACKEND", "software");
            QQuickWindow::setSceneGraphBackend(QStringLiteral("software"));
        }
    } else {
        std::cout << "[Graphics] Probing graphics hardware acceleration on X11/XWayland display...\n";
        GpuBackend backend = probeHardwareGraphics();
        if (backend == GpuBackend::Glx) {
            std::cout << "[Graphics] Hardware GLX acceleration verified. Enabling xcb_glx.\n";
            qputenv("QT_XCB_GL_INTEGRATION", "xcb_glx");
        } else if (backend == GpuBackend::Egl) {
            std::cout << "[Graphics] GLX unavailable, Hardware EGL acceleration verified. Enabling xcb_egl.\n";
            qputenv("QT_XCB_GL_INTEGRATION", "xcb_egl");
        } else {
            std::cout << "[Graphics] Neither GLX nor EGL available. Activating Qt Quick Software rendering fallback.\n";
            std::cout << "[Graphics] Overlay is guaranteed to render reliably on any system.\n";
            qputenv("QT_XCB_GL_INTEGRATION", "none");
            qputenv("QT_QUICK_BACKEND", "software");
            QQuickWindow::setSceneGraphBackend(QStringLiteral("software"));
        }
    }

    // Configure default surface format with 8-bit alpha buffer to guarantee 32-bit ARGB visual allocation
    QSurfaceFormat defaultFormat;
    defaultFormat.setAlphaBufferSize(8);
    QSurfaceFormat::setDefaultFormat(defaultFormat);

    QApplication app(argc, argv);
    // Re-apply default English QLocale after QApplication constructor (which calls setlocale internally)
    QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates));
    app.setApplicationName(QStringLiteral("Karyon"));
    app.setApplicationDisplayName(QStringLiteral("Karyon"));
    app.setDesktopFileName(QStringLiteral("karyon"));
    app.setQuitOnLastWindowClosed(false);

    // Single-Instance Check (gilt für Binär-Build und AppImage gleichermaßen)
    QString lockPath = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    if (lockPath.isEmpty()) {
        lockPath = QDir::tempPath();
    }
    lockPath += QStringLiteral("/karyon.lock");

    QLockFile singleInstanceLock(lockPath);
    singleInstanceLock.setStaleLockTime(0);
    if (!singleInstanceLock.tryLock()) {
        qint64 runningPid = 0;
        QString hostname, appname;
        singleInstanceLock.getLockInfo(&runningPid, &hostname, &appname);

        QMessageBox msgBox;
        msgBox.setWindowTitle(QStringLiteral("Karyon"));
        msgBox.setWindowIcon(ProcessUtils::karyonIcon());
        msgBox.setIcon(QMessageBox::Information);
        msgBox.setText(QStringLiteral("Karyon is already running."));
        if (runningPid > 0) {
            msgBox.setInformativeText(QStringLiteral("An active instance of Karyon is already running (Process ID %1).\nRunning multiple instances simultaneously is not supported.").arg(runningPid));
        } else {
            msgBox.setInformativeText(QStringLiteral("An active instance of Karyon is already running.\nRunning multiple instances simultaneously is not supported."));
        }
        msgBox.setStandardButtons(QMessageBox::Ok);
        msgBox.setDefaultButton(QMessageBox::Ok);
        msgBox.exec();
        return 0;
    }

    // Ensure required Linux input subsystem permissions are present (/dev/uinput and /dev/input)
    // Run this BEFORE initializing any GUI components, overlay, tray icon, or background monitors.
    if (!PermissionManager::hasRequiredPermissions()) {
        PermissionDialogController permController;
        if (!permController.execDialog()) {
            return 0;
        }
    }

    // KWin-Regel für das Overlay absichern: Darf nur auf 'karyon-overlay' matchen,
    // damit SettingsDialog und der KDE App Chooser normalen Tastaturfokus erhalten.
    {
        QString kwinrulesrc = QDir::homePath() + QStringLiteral("/.config/kwinrulesrc");
        if (QFile::exists(kwinrulesrc)) {
            QSettings s(kwinrulesrc, QSettings::IniFormat);
            const QString ruleGroup = QStringLiteral("dab10000-0000-4000-8000-000000000001");
            if (s.childGroups().contains(ruleGroup)) {
                s.beginGroup(ruleGroup);
                QString title = s.value(QStringLiteral("title")).toString();
                int titlematch = s.value(QStringLiteral("titlematch"), 0).toInt();
                if (title != QLatin1String("karyon-overlay") || titlematch != 1) {
                    s.setValue(QStringLiteral("title"), QStringLiteral("karyon-overlay"));
                    s.setValue(QStringLiteral("titlematch"), 1);
                    s.setValue(QStringLiteral("titlerule"), 2);
                    s.setValue(QStringLiteral("description"), QStringLiteral("karyon overlay only"));
                    s.sync();
                    QDBusMessage msg = QDBusMessage::createMethodCall(
                        QStringLiteral("org.kde.KWin"), QStringLiteral("/KWin"),
                        QStringLiteral("org.kde.KWin"), QStringLiteral("reconfigure"));
                    QDBusConnection::sessionBus().send(msg);
                }
                s.endGroup();
            }
        }
    }



    CircleOverlay overlay;
    overlay.prewarm();

    GestureManager gestureManager;
    GestureTrail gestureTrail;

    SettingsController settingsController;
    InfoDialogController infoController;

    KWinBridge kwinBridge;
    InputMonitor monitor;
    std::signal(SIGINT, handleSignal);
    std::signal(SIGTERM, handleSignal);
    std::signal(SIGHUP, handleSignal);

    // Initialisiere dynamische Einstellungen für InputMonitor (Trigger Hold & Mouse Speed & Game Mode & Trigger Key/Button)
    monitor.setHoldTriggerMs(SettingsManager::instance().triggerHoldMs());
    monitor.setOverlayMouseSpeed(SettingsManager::instance().overlayMouseSpeed());
    monitor.setGameModeEnabled(SettingsManager::instance().gameModeEnabled());
    monitor.setPerformanceMode(SettingsManager::instance().performanceMode());
    monitor.setGameModeActive(kwinBridge.isActiveWindowGame());
    monitor.setTrigger(
        SettingsManager::instance().triggerType(),
        SettingsManager::instance().triggerButton(),
        SettingsManager::instance().triggerKeySeq()
    );

    QObject::connect(&SettingsManager::instance(), &SettingsManager::settingsChanged, [&monitor]() {
        monitor.setHoldTriggerMs(SettingsManager::instance().triggerHoldMs());
        monitor.setOverlayMouseSpeed(SettingsManager::instance().overlayMouseSpeed());
        monitor.setGameModeEnabled(SettingsManager::instance().gameModeEnabled());
        monitor.setPerformanceMode(SettingsManager::instance().performanceMode());
        monitor.setTrigger(
            SettingsManager::instance().triggerType(),
            SettingsManager::instance().triggerButton(),
            SettingsManager::instance().triggerKeySeq()
        );
    });

    QObject::connect(&settingsController, &SettingsController::settingsVisibilityChanged, &monitor, [&monitor](bool visible) {
        monitor.setInhibited(visible);
    });
    QObject::connect(&settingsController, &SettingsController::isRecordingTriggerChanged, &monitor, [&monitor, &settingsController]() {
        monitor.setRecordingTrigger(settingsController.isRecordingTrigger());
    });
    QObject::connect(&monitor, &InputMonitor::inputCaptured, &settingsController, &SettingsController::onInputCaptured);

    QObject::connect(&kwinBridge, &KWinBridge::activeWindowGameStatusChanged, &monitor, [&monitor](bool isGame) {
        monitor.setGameModeActive(isGame);
    });

    // Input Permissions Change: Stop/Start InputMonitor in real-time
    QObject::connect(&settingsController, &SettingsController::inputPermissionsChanged,
                     [&settingsController, &monitor]() {
        if (settingsController.hasInputPermissions()) {
            std::cout << "[Karyon] Input permissions active - starting InputMonitor\n";
            monitor.start();
        } else {
            std::cout << "[Karyon] Input permissions revoked - stopping InputMonitor\n";
            monitor.stop();
        }
    });

    // System-Standby (Suspend/Resume) Auto-Recovery via logind
    QDBusConnection::systemBus().connect(
        QStringLiteral("org.freedesktop.login1"),
        QStringLiteral("/org/freedesktop/login1"),
        QStringLiteral("org.freedesktop.login1.Manager"),
        QStringLiteral("PrepareForSleep"),
        &monitor,
        SLOT(onPrepareForSleep(bool))
    );

    QObject::connect(&monitor, &InputMonitor::resumedFromSleep, [&monitor, &kwinBridge]() {
        std::cout << "[Karyon] System resumed from sleep - re-syncing inputs and cursor\n";
        QTimer::singleShot(250, [&monitor, &kwinBridge]() {
            monitor.requestRescan();
            kwinBridge.queryCursor();
        });
        QTimer::singleShot(750, [&monitor, &kwinBridge]() {
            monitor.requestRescan();
            kwinBridge.queryCursor();
        });
    });

    // Settings Dialog Öffnen-Funktion
    auto showSettings = [&settingsController, &kwinBridge]() {
        settingsController.showSettings(kwinBridge.lastCursorPos());
    };

    auto showInfo = [&infoController]() {
        infoController.showInfo();
    };

    // TrayManager Callbacks (wenn Karyon-Tray-Icon oder Optionen im Overlay getriggert werden)
    TrayManager::setKaryonSettingsCallback([showSettings]() {
        showSettings();
    });
    TrayManager::setKaryonActionCallback([&app, showSettings, showInfo](int actionId) {
        if (actionId == 1) { // Info
            showInfo();
        } else if (actionId == 2) { // Settings
            showSettings();
        } else if (actionId == 3 || actionId == 4) { // Quit
            std::cout << "[Karyon] Quit requested from tray action.\n";
            app.quit();
            QTimer::singleShot(1500, []() {
                _exit(0);
            });
        }
    });

    // Ensure Karyon icon is deployed to user's icon directory for system-wide desktop notifications
    QString iconDir = QDir::homePath() + QStringLiteral("/.local/share/icons/hicolor/scalable/apps");
    QDir().mkpath(iconDir);
    QString userIconPath = iconDir + QStringLiteral("/karyon.svg");
    if (!QFile::exists(userIconPath) || QFile(userIconPath).size() == 0) {
        if (QFile::exists(QStringLiteral(":/qt/qml/karyon/resources/karyon.svg"))) {
            QFile::remove(userIconPath);
            QFile::copy(QStringLiteral(":/qt/qml/karyon/resources/karyon.svg"), userIconPath);
        } else {
            QString appDir = qEnvironmentVariable("APPDIR");
            QString appDirIcon = appDir + QStringLiteral("/usr/share/icons/hicolor/scalable/apps/karyon.svg");
            QString relDevIcon = QCoreApplication::applicationDirPath() + QStringLiteral("/../resources/karyon.svg");
            if (!appDir.isEmpty() && QFile::exists(appDirIcon)) {
                QFile::remove(userIconPath);
                QFile::copy(appDirIcon, userIconPath);
            } else if (QFile::exists(relDevIcon)) {
                QFile::remove(userIconPath);
                QFile::copy(relDevIcon, userIconPath);
            } else if (QFile::exists(QStringLiteral("/home/eisen/Data/Programme/PythonProjects/Karyon C++/resources/karyon.svg"))) {
                QFile::remove(userIconPath);
                QFile::copy(QStringLiteral("/home/eisen/Data/Programme/PythonProjects/Karyon C++/resources/karyon.svg"), userIconPath);
            }
        }
    }

    // System Tray Icon
    QIcon karyonAppIcon = ProcessUtils::karyonIcon();
    app.setWindowIcon(karyonAppIcon);

    QSystemTrayIcon trayIcon;
    trayIcon.setIcon(karyonAppIcon);
    trayIcon.setToolTip(QStringLiteral("Karyon"));

    QMenu trayMenu;
    QAction *infoAction = trayMenu.addAction(QStringLiteral("Info"));
    QAction *settingsAction = trayMenu.addAction(QStringLiteral("Settings"));
    trayMenu.addSeparator();
    QAction *quitAction = trayMenu.addAction(QStringLiteral("Quit"));

    QObject::connect(infoAction, &QAction::triggered, showInfo);
    QObject::connect(settingsAction, &QAction::triggered, showSettings);
    QObject::connect(quitAction, &QAction::triggered, [&app]() {
        std::cout << "[Karyon] Quit requested from tray menu.\n";
        app.quit();
        QTimer::singleShot(1500, []() {
            _exit(0);
        });
    });

    QObject::connect(&trayIcon, &QSystemTrayIcon::activated, [showSettings](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger) {
            showSettings();
        }
    });

    // Benachrichtigung bei Erkennung einer neu angeschlossenen Maus (Debounced)
    static qint64 lastNotifyMs = 0;
    QObject::connect(&monitor, &InputMonitor::mouseConnected, &trayIcon, [&trayIcon, userIconPath](const QString &deviceName) {
        qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
        if (nowMs - lastNotifyMs < 5000) return;
        lastNotifyMs = nowMs;

        bool sentViaDBus = false;
        QDBusInterface notify(
            QStringLiteral("org.freedesktop.Notifications"),
            QStringLiteral("/org/freedesktop/Notifications"),
            QStringLiteral("org.freedesktop.Notifications"),
            QDBusConnection::sessionBus()
        );

        if (notify.isValid()) {
            QVariantMap hints;
            hints.insert(QStringLiteral("desktop-entry"), QStringLiteral("karyon"));
            if (!userIconPath.isEmpty() && QFile::exists(userIconPath)) {
                hints.insert(QStringLiteral("image-path"), userIconPath);
            }

            QList<QVariant> args;
            args << QStringLiteral("Karyon"); // app_name (im Header angezeigt)
            args << quint32(0);               // replaces_id
            args << (QFile::exists(userIconPath) ? userIconPath : QStringLiteral("karyon")); // app_icon (vor Karyon im Header)
            args << QStringLiteral("Karyon"); // summary
            args << QStringLiteral("Mouse detected: %1\nOverlay is ready to use.").arg(deviceName); // body
            args << QStringList();            // actions
            args << hints;                    // hints
            args << qint32(3500);             // timeout

            QDBusMessage reply = notify.callWithArgumentList(QDBus::NoBlock, QStringLiteral("Notify"), args);
            if (reply.type() != QDBusMessage::ErrorMessage) {
                sentViaDBus = true;
            }
        }

        if (!sentViaDBus) {
            trayIcon.showMessage(
                QStringLiteral("Karyon"),
                QStringLiteral("Mouse detected: %1\nOverlay is ready to use.").arg(deviceName),
                trayIcon.icon(),
                3500
            );
        }
    });

    trayIcon.setContextMenu(&trayMenu);
    trayIcon.show();

    if (app.arguments().contains(QStringLiteral("--settings"))) {
        QTimer::singleShot(150, showSettings);
    }
    if (app.arguments().contains(QStringLiteral("--info"))) {
        QTimer::singleShot(150, showInfo);
    }

    SessionDialogController sessionDialog;

    QPoint gestureStartPos;
    bool inGesture = false;

    // Sobald die Trigger-Taste gedrückt wird: sofort echte globale Cursorposition via KWin abfragen
    auto onTriggerPress = [&kwinBridge, &gestureStartPos, &inGesture]() {
        gestureStartPos = kwinBridge.lastCursorPos();
        inGesture = false;
        kwinBridge.queryCursor();
    };
    QObject::connect(&monitor, &InputMonitor::rightPressed, &kwinBridge, onTriggerPress, Qt::QueuedConnection);
    QObject::connect(&monitor, &InputMonitor::triggerPressed, &kwinBridge, onTriggerPress, Qt::QueuedConnection);

    QObject::connect(&kwinBridge, &KWinBridge::cursorPosUpdated, [&gestureStartPos, &inGesture](const QPoint &pos) {
        if (!inGesture) {
            gestureStartPos = pos;
        }
    });

    // 1. Trail-Start: Sobald bei gedrücktem Trigger Bewegung beginnt
    QObject::connect(&monitor, &InputMonitor::gestureStarted, &gestureTrail,
                     [&gestureTrail, &gestureStartPos, &inGesture]() {
        if (!SettingsManager::instance().gesturesEnabled()) return;
        inGesture = true;
        if (SettingsManager::instance().showGestureTrail()) {
            gestureTrail.startTrail(gestureStartPos);
        }
    }, Qt::QueuedConnection);

    // 2. Trail-Aktualisierung: Verfolgt die Zugrichtung flüssig ohne Haken
    QObject::connect(&monitor, &InputMonitor::gesturePointAdded, &gestureTrail,
                     [&gestureTrail, &gestureStartPos](int deltaX, int deltaY) {
        if (!SettingsManager::instance().gesturesEnabled()) return;
        if (SettingsManager::instance().showGestureTrail()) {
            gestureTrail.updateCurrentPos(gestureStartPos.x() + deltaX, gestureStartPos.y() + deltaY);
        }
    }, Qt::QueuedConnection);

    // 3. Trail-Ende: Sanftes Ausfaden beim Loslassen der RMT
    QObject::connect(&monitor, &InputMonitor::gestureFinished, &gestureTrail,
                     [&gestureTrail, &inGesture]() {
        inGesture = false;
        gestureTrail.finishTrail();
    }, Qt::QueuedConnection);

    // 4. Gesten-Auslösung: Direkt beim Erreichen des Gesten-Schwellenwerts ausführen
    QObject::connect(&monitor, &InputMonitor::gestureDetected, &gestureManager,
                     [&gestureManager, &monitor, &kwinBridge, &overlay, &gestureTrail, &inGesture](int direction, int deltaX, int deltaY) {
        if (!SettingsManager::instance().gesturesEnabled()) return;
        Q_UNUSED(deltaX);
        Q_UNUSED(deltaY);
        inGesture = false;
        gestureTrail.finishTrail();
        gestureManager.executeGesture(direction, &monitor, &kwinBridge, overlay.mediaManager(), overlay.volumeManager());
    }, Qt::QueuedConnection);

    // Sobald 120ms erreicht sind: Overlay unter der aktuellen Position spawnen
    QObject::connect(&monitor, &InputMonitor::holdTriggered, &overlay,
                     [&overlay, &kwinBridge, &monitor, &gestureTrail](int deltaX, int deltaY) {
        gestureTrail.cancelTrail();
        Q_UNUSED(deltaX);
        Q_UNUSED(deltaY);
        QPoint pos = kwinBridge.lastCursorPos();
        if (pos.isNull() || (pos.x() == 0 && pos.y() == 0)) {
            QScreen *primary = QGuiApplication::primaryScreen();
            if (primary) {
                pos = primary->geometry().center();
            }
        }

        KLOG("Main", "holdTriggered -> showAt(%d, %d), %d windows", pos.x(), pos.y(), kwinBridge.lastWindows().size());
        overlay.showAt(pos, kwinBridge.lastWindows(), &monitor, kwinBridge.isActiveWindowGame());
    }, Qt::QueuedConnection);

    // Aiming im Normalbetrieb erfolgt flackerfrei über MouseArea/DropArea in QML anhand der Compositor-Cursorposition.
    // Verlässt der Cursor bei gedrückter RMT das Overlay-Fenster (oder bei aktivem Drag & Drop),
    // steuert cursorMoved das Segment-Aiming direkt und übergangslos über die globale Cursorposition:
    QObject::connect(&monitor, &InputMonitor::cursorMoved, &overlay, [&overlay, &kwinBridge, &monitor](int aimX, int aimY) {
        if (!overlay.isVisible()) return;

        if (monitor.isLeftDown()) {
            overlay.updateAim(aimX, aimY);
        } else {
            overlay.onHardwareCursorMoved(aimX, aimY, kwinBridge.lastCursorPos());
        }
    }, Qt::QueuedConnection);

    // Synchronisation des Overlay-Zustands mit der KWinBridge (schützt MRU vor Hintergrund-Fokuswechseln)
    QObject::connect(&overlay, &CircleOverlay::overlayShown, &kwinBridge, [&kwinBridge]() {
        KLOG("Main", "overlayShown signal");
        kwinBridge.setOverlayOpen(true);
    });
    QObject::connect(&overlay, &CircleOverlay::overlayHidden, &kwinBridge, [&kwinBridge]() {
        KLOG("Main", "overlayHidden signal");
        kwinBridge.setOverlayOpen(false);
    });
    // Synchronisation des Show-Desktop-Zustands
    QObject::connect(&kwinBridge, &KWinBridge::showDesktopChanged, &overlay, &CircleOverlay::setDesktopShowing);

    // Abbruch-Aktion (z.B. Linksklick bei RMB-Trigger, RMT bei Custom-Trigger, ESC) -> Schließt Overlay sofort ohne Aktion
    QObject::connect(&monitor, &InputMonitor::holdCancelled, &overlay, [&overlay, &monitor]() {
        KLOG("Main", "holdCancelled signal");
        overlay.hideOverlay();
        monitor.requestReleaseButtons();
    }, Qt::QueuedConnection);

    // Mittelklick bei gedrückter rechter Maustaste -> System-Mute IM GANZEN HUB, darüber hinaus aber nirgends
    QObject::connect(&monitor, &InputMonitor::middleClicked, &overlay, [&overlay, &kwinBridge]() {
        if (overlay.isCursorInHub()) {
            if (SettingsManager::instance().showVolumeControl()) {
                overlay.toggleMute();
            }
            return;
        }
        QString winId = overlay.manipulationTargetWindowId();
        if (!winId.isEmpty()) {
            kwinBridge.closeWindow(winId);
        }
    }, Qt::QueuedConnection);

    // Scrollrad bei geöffnetem Overlay:
    // - Im Lautstärkebereich: Lautstärkeregelung
    // - Auf einem Fenster-Segment: Maximieren (UP) / Minimieren (DOWN)
    // - Im Hub oder bei fehlendem Fenstertarget: Kategoriewechsel (Windows, Apps, Files) mit sauberem 150ms-Debounce
    static qint64 lastScrollUpTime = 0;
    static qint64 lastScrollDownTime = 0;
    static qint64 lastHubWheelTime = 0;

    QObject::connect(&monitor, &InputMonitor::wheelScrolled, &overlay, [&overlay, &kwinBridge](int delta) {
        if (!overlay.isVisible()) return;

        // 1. Im Lautstärkebereich des Hubs: Lautstärkeregelung
        if (overlay.isCursorInVolumeArea()) {
            int vStep = SettingsManager::instance().volumeStep();
            int step = (delta > 0) ? vStep : -vStep;
            overlay.stepVolume(step);
            return;
        }

        // 2. Im Hub (außerhalb des Lautstärkebereichs): Kategoriewechsel (Windows, Apps, Files) mit sauberem 150ms-Debounce
        if (overlay.isCursorInHub()) {
            qint64 now = QDateTime::currentMSecsSinceEpoch();
            if (now - lastHubWheelTime >= 150) {
                lastHubWheelTime = now;
                overlay.handleHubWheel(delta);
            }
            return;
        }

        // 3. Auf einem Fenster-Segment/Icon im Ring: Maximieren (UP) / Minimieren (DOWN)
        QString winId = overlay.manipulationTargetWindowId();
        if (winId.isEmpty()) {
            return;
        }

        qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (delta > 0) {
            if (now - lastScrollUpTime >= 200) {
                lastScrollUpTime = now;
                kwinBridge.toggleMaximize(winId);
            }
        } else if (delta < 0) {
            if (now - lastScrollDownTime >= 200) {
                lastScrollDownTime = now;
                kwinBridge.toggleMinimize(winId);
            }
        }
    }, Qt::QueuedConnection);

    // Live-Update des Overlays, wenn sich Fenster ändern (z.B. geschlossenes Fenster verschwindet sofort)
    QObject::connect(&kwinBridge, &KWinBridge::windowsUpdated, &overlay, [&overlay, &kwinBridge]() {
        KLOG("Main", "windowsUpdated signal: isVisible=%d count=%d", overlay.isVisible(), kwinBridge.lastWindows().size());
        if (overlay.isVisible()) {
            overlay.refresh(kwinBridge.lastWindows());
        }
    }, Qt::QueuedConnection);

    // Loslassen der rechten Maustaste -> Gewähltes Fenster aktivieren, App starten, Datei öffnen, Tray ausführen, Desktop toggeln, Medien steuern und Overlay schließen
    QObject::connect(&monitor, &InputMonitor::holdReleased, &overlay, [&overlay, &kwinBridge, &sessionDialog]() {
        KLOG("Main", "holdReleased signal: selectedWinId='%s' pinHovered=%d counterHovered=%d",
             qPrintable(overlay.selectedWindowId()), overlay.isPinHovered(), overlay.isCounterBadgeHovered());
        if (overlay.isPinHovered()) {
            overlay.togglePinForCurrentSelection();
            overlay.hideOverlay();
            return;
        }
        if (overlay.isCounterBadgeHovered()) {
            QStringList groupIds = overlay.currentGroupWindowIds();
            for (const QString &wid : groupIds) {
                kwinBridge.closeWindow(wid);
            }
            overlay.hideOverlay();
            return;
        }
        QString selectedWinId = overlay.selectedWindowId();
        overlay.hideOverlay();
        if (selectedWinId == QLatin1String("__SHOW_DESKTOP__")) {
            kwinBridge.toggleShowDesktop();
        } else if (selectedWinId == QLatin1String("__MEDIA_PLAYPAUSE__")) {
            overlay.mediaPlayPause();
        } else if (selectedWinId == QLatin1String("__MEDIA_PREV__")) {
            overlay.mediaPrevious();
        } else if (selectedWinId == QLatin1String("__MEDIA_NEXT__")) {
            overlay.mediaNext();
        } else if (selectedWinId.startsWith(QLatin1String("app:"))) {
            QString execLine = selectedWinId.mid(4);
            AppManager::launchApp(execLine);
        } else if (selectedWinId.startsWith(QLatin1String("file:"))) {
            QString filePath = selectedWinId.mid(5);
            overlay.markFileLaunched(filePath);
            if (AppManager::isAppImage(filePath)) {
                AppManager::launchApp(filePath);
            } else {
                RecentFilesManager::openFile(filePath);
            }
        } else if (selectedWinId.startsWith(QLatin1String("tray_sys:"))) {
            QString pluginId = selectedWinId.mid(9);
            TrayManager::launchPlasmoid(pluginId);
        } else if (selectedWinId.startsWith(QLatin1String("tray_prog|")) || selectedWinId.startsWith(QLatin1String("tray_prog:"))) {
            QStringList parts = selectedWinId.contains(QLatin1Char('|'))
                ? selectedWinId.split(QLatin1Char('|'))
                : selectedWinId.split(QLatin1Char(':'));
            if (parts.size() >= 3) {
                QString service = parts[1];
                QString path = parts[2];
                QPoint cur = kwinBridge.lastCursorPos();
                TrayManager::activateItem(service, path, cur.x(), cur.y());
            }
        } else if (selectedWinId.startsWith(QLatin1String("tray_opt|")) || selectedWinId.startsWith(QLatin1String("tray_opt:"))) {
            QStringList parts = selectedWinId.contains(QLatin1Char('|'))
                ? selectedWinId.split(QLatin1Char('|'))
                : selectedWinId.split(QLatin1Char(':'));
            if (parts.size() >= 4) {
                QString service = parts[1];
                QString menuPath = parts[2];
                int actionId = parts[3].toInt();
                TrayManager::triggerMenuAction(service, menuPath, actionId);
            }
        } else if (selectedWinId.startsWith(QLatin1String("session:"))) {
            QString action = selectedWinId.mid(8);
            if (action == QLatin1String("lock")) {
                SessionManager::lock();
            } else if (action == QLatin1String("suspend")) {
                SessionManager::suspend();
            } else if (action == QLatin1String("hibernate")) {
                SessionManager::hibernate();
            } else {
                sessionDialog.showPrompt(action, kwinBridge.lastCursorPos());
            }
        } else if (!selectedWinId.isEmpty() &&
                   !selectedWinId.startsWith(QLatin1String("__")) &&
                   !selectedWinId.startsWith(QLatin1String("cat:"))) {
            kwinBridge.activateWindow(selectedWinId);
        }
    }, Qt::QueuedConnection);

    QObject::connect(&app, &QCoreApplication::aboutToQuit, [&]() {
        std::cout << "[Karyon] Application shutting down...\n";
        monitor.stop();
        kwinBridge.stopDaemon();
        settingsController.hideSettings();
        infoController.closeDialog();
        overlay.hideOverlay();
    });

    if (!monitor.start()) {
        std::cerr << "Error starting input monitor.\n";
        return 1;
    }

    std::cout << "Karyon C++ active. Hold trigger button for 120 ms.\n";
    int ret = app.exec();
    std::cout << "[Karyon] Application terminated cleanly.\n";
    _exit(ret);
}
