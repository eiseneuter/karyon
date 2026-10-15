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
#include <csignal>
#include <unistd.h>
#include <iostream>
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

namespace {
void handleSignal(int /*sig*/)
{
    _exit(0);
}

void setupStderrFilter()
{
    int pipeFds[2];
    if (pipe(pipeFds) != 0) return;

    int origStderr = dup(STDERR_FILENO);
    if (origStderr < 0) {
        close(pipeFds[0]);
        close(pipeFds[1]);
        return;
    }

    if (dup2(pipeFds[1], STDERR_FILENO) < 0) {
        close(pipeFds[0]);
        close(pipeFds[1]);
        close(origStderr);
        return;
    }
    close(pipeFds[1]);

    std::thread([readFd = pipeFds[0], origStderr]() {
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
            write(origStderr, buffer, strlen(buffer));
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
        std::cerr << "Fatal: " << msg.toStdString() << "\n";
        abort();
    } else if (type == QtCriticalMsg) {
        std::cerr << "Error: " << msg.toStdString() << "\n";
    }
}
}

int main(int argc, char *argv[])
{
    setupStderrFilter();
    qInstallMessageHandler(quietMessageHandler);

    QString lockPath = QDir::tempPath() + QStringLiteral("/karyon_instance.lock");
    QLockFile lockFile(lockPath);
    if (!lockFile.tryLock(100)) {
        std::cerr << "Another instance of Karyon is already running. Exiting.\n";
        return 0;
    }

    std::setlocale(LC_ALL, "");

    // Detect KDE Plasma system locale if configured, with system fallback
    QString klang;
    QString plasmaLocaleRc = QDir::homePath() + QStringLiteral("/.config/plasma-localerc");
    if (QFile::exists(plasmaLocaleRc)) {
        QSettings plasmarc(plasmaLocaleRc, QSettings::IniFormat);
        klang = plasmarc.value(QStringLiteral("Formats/LC_TIME")).toString();
        if (klang.isEmpty()) {
            klang = plasmarc.value(QStringLiteral("Formats/LANG")).toString();
        }
    }
    if (klang.isEmpty()) {
        klang = QString::fromLocal8Bit(qgetenv("LC_TIME"));
    }
    if (klang.isEmpty()) {
        klang = QString::fromLocal8Bit(qgetenv("LANG"));
    }

    if (!klang.isEmpty()) {
        std::setlocale(LC_ALL, klang.toUtf8().constData());
        std::setlocale(LC_TIME, klang.toUtf8().constData());
        QLocale sysLocale(klang);
        QLocale::setDefault(sysLocale);
    } else {
        QLocale::setDefault(QLocale::system());
    }

    // Enforce QT_QPA_PLATFORM=xcb for direct X11/XWayland Override-Redirect (Rule 1)
    // Synchronous cursor positioning and frameless overlay window placement under cursor
    qputenv("QT_QPA_PLATFORM", "xcb");

    QApplication app(argc, argv);
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

    // Initialisiere dynamische Einstellungen für InputMonitor (Trigger Hold & Mouse Speed & Game Mode)
    monitor.setHoldTriggerMs(SettingsManager::instance().triggerHoldMs());
    monitor.setOverlayMouseSpeed(SettingsManager::instance().overlayMouseSpeed());
    monitor.setGameModeEnabled(SettingsManager::instance().gameModeEnabled());
    monitor.setGameModeActive(kwinBridge.isActiveWindowGame());

    QObject::connect(&SettingsManager::instance(), &SettingsManager::settingsChanged, [&monitor]() {
        monitor.setHoldTriggerMs(SettingsManager::instance().triggerHoldMs());
        monitor.setOverlayMouseSpeed(SettingsManager::instance().overlayMouseSpeed());
        monitor.setGameModeEnabled(SettingsManager::instance().gameModeEnabled());
    });

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
            app.quit();
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
    QObject::connect(quitAction, &QAction::triggered, &app, &QApplication::quit);

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

    // Sobald die rechte Taste gedrückt wird: sofort echte globale Cursorposition via KWin abfragen
    QObject::connect(&monitor, &InputMonitor::rightPressed, &kwinBridge, [&kwinBridge, &gestureStartPos, &inGesture]() {
        gestureStartPos = kwinBridge.lastCursorPos();
        inGesture = false;
        kwinBridge.queryCursor();
    }, Qt::QueuedConnection);

    QObject::connect(&kwinBridge, &KWinBridge::cursorPosUpdated, [&gestureStartPos, &inGesture](const QPoint &pos) {
        if (!inGesture) {
            gestureStartPos = pos;
        }
    });

    // 1. Trail-Start: Sobald bei gedrückter RMT Bewegung beginnt
    QObject::connect(&monitor, &InputMonitor::gestureStarted, &gestureTrail,
                     [&gestureTrail, &gestureStartPos, &inGesture]() {
        if (!SettingsManager::instance().gesturesEnabled()) return;
        inGesture = true;
        gestureTrail.startTrail(gestureStartPos);
    }, Qt::QueuedConnection);

    // 2. Trail-Aktualisierung: Verfolgt die Zugrichtung flüssig ohne Haken
    QObject::connect(&monitor, &InputMonitor::gesturePointAdded, &gestureTrail,
                     [&gestureTrail, &gestureStartPos](int deltaX, int deltaY) {
        if (!SettingsManager::instance().gesturesEnabled()) return;
        gestureTrail.updateCurrentPos(gestureStartPos.x() + deltaX, gestureStartPos.y() + deltaY);
    }, Qt::QueuedConnection);

    // 3. Trail-Ende: Sanftes Ausfaden beim Loslassen der RMT
    QObject::connect(&monitor, &InputMonitor::gestureFinished, &gestureTrail,
                     [&gestureTrail, &inGesture]() {
        inGesture = false;
        gestureTrail.finishTrail();
    }, Qt::QueuedConnection);

    // 4. Gesten-Auslösung: Direkt beim Loslassen der rechten Maustaste ausführen
    QObject::connect(&monitor, &InputMonitor::gestureDetected, &gestureManager,
                     [&gestureManager, &monitor, &kwinBridge, &overlay](int direction, int deltaX, int deltaY) {
        if (!SettingsManager::instance().gesturesEnabled()) return;
        Q_UNUSED(deltaX);
        Q_UNUSED(deltaY);
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

        overlay.showAt(pos, kwinBridge.lastWindows(), &monitor);
    }, Qt::QueuedConnection);

    // Aiming im Normalbetrieb erfolgt flackerfrei über MouseArea/DropArea in QML anhand der Compositor-Cursorposition.
    // Bei aktivem Drag & Drop (gehaltene LMT beim Ziehen einer Datei) liefert das Fenstersystem unter XWayland
    // oft keine Events an Override-Redirect-Fenster. cursorMoved steuert das Segment-Aiming direkt über evdev-Mausdaten:
    QObject::connect(&monitor, &InputMonitor::cursorMoved, &overlay, [&overlay, &monitor](int aimX, int aimY) {
        if (monitor.isLeftDown()) {
            overlay.updateAim(aimX, aimY);
        }
    }, Qt::QueuedConnection);

    // Synchronisation des Overlay-Zustands mit der KWinBridge (schützt MRU vor Hintergrund-Fokuswechseln)
    QObject::connect(&overlay, &CircleOverlay::overlayShown, &kwinBridge, [&kwinBridge]() {
        kwinBridge.setOverlayOpen(true);
    });
    QObject::connect(&overlay, &CircleOverlay::overlayHidden, &kwinBridge, [&kwinBridge]() {
        kwinBridge.setOverlayOpen(false);
    });
    // Synchronisation des Show-Desktop-Zustands
    QObject::connect(&kwinBridge, &KWinBridge::showDesktopChanged, &overlay, &CircleOverlay::setDesktopShowing);

    // Linksklick während gedrückter rechter Maustaste -> Schließt Overlay sofort ohne Aktion
    QObject::connect(&monitor, &InputMonitor::holdCancelled, &overlay, &CircleOverlay::hideOverlay, Qt::QueuedConnection);

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

    // Scrollrad bei gedrückter rechter Maustaste -> Maximieren (UP) / Minimieren (DOWN) auf Segmenten, Lautstärke im Lautstärkebereich, Kategorie-Wechsel im Hub
    static qint64 lastScrollUpTime = 0;
    static qint64 lastScrollDownTime = 0;

    QObject::connect(&monitor, &InputMonitor::wheelScrolled, &overlay, [&overlay, &kwinBridge](int delta) {
        if (overlay.isCursorInVolumeArea()) {
            int vStep = SettingsManager::instance().volumeStep();
            int step = (delta > 0) ? vStep : -vStep;
            overlay.stepVolume(step);
            return;
        }

        QString winId = overlay.manipulationTargetWindowId();
        if (winId.isEmpty()) {
            if (overlay.isCursorInHub() || overlay.mode() != OverlayMode::Windows) {
                overlay.handleHubWheel(delta);
            }
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
        if (overlay.isVisible()) {
            overlay.refresh(kwinBridge.lastWindows());
        }
    }, Qt::QueuedConnection);

    // Loslassen der rechten Maustaste -> Gewähltes Fenster aktivieren, App starten, Datei öffnen, Tray ausführen, Desktop toggeln, Medien steuern und Overlay schließen
    QObject::connect(&monitor, &InputMonitor::holdReleased, &overlay, [&overlay, &kwinBridge, &sessionDialog]() {
        if (overlay.isPinHovered()) {
            overlay.togglePinForCurrentSelection();
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

    if (!monitor.start()) {
        std::cerr << "Error starting input monitor.\n";
        return 1;
    }

    std::cout << "Karyon C++ active. Hold right mouse button for 120 ms.\n";
    return app.exec();
}
