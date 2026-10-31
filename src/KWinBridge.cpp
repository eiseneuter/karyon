#include "KWinBridge.h"
#include "DebugLogger.h"
#include "AppManager.h"
#include "GameModeDetector.h"

#include <QFile>
#include <QDir>
#include <QTimer>
#include <QCursor>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <cmath>
#include <unistd.h>
#include <iostream>

static const QString SERVICE_NAME = QStringLiteral("org.karyon.Bridge");
static const QString OBJECT_PATH = QStringLiteral("/bridge");
static const QString INTERFACE_NAME = QStringLiteral("org.karyon.Bridge");

KWinBridge::KWinBridge(QObject *parent)
    : QObject(parent)
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    bus.registerService(SERVICE_NAME);
    bus.registerObject(OBJECT_PATH, INTERFACE_NAME, this, QDBusConnection::ExportAllSlots);

    // Prune any stale / orphaned shortcuts from KGlobalAccel
    QDBusInterface kglobalaccel(QStringLiteral("org.kde.kglobalaccel"), QStringLiteral("/component/kwin"),
                                QStringLiteral("org.kde.kglobalaccel.Component"), bus);
    kglobalaccel.call(QStringLiteral("cleanUp"));

    setupDaemon();
    queryCursor();
}

void KWinBridge::stopDaemon()
{
    if (m_daemonSid >= 0) {
        QDBusConnection bus = QDBusConnection::sessionBus();
        QDBusMessage msg = QDBusMessage::createMethodCall(
            QStringLiteral("org.kde.KWin"),
            QStringLiteral("/Scripting/Script%1").arg(m_daemonSid),
            QStringLiteral("org.kde.kwin.Script"),
            QStringLiteral("stop"));
        bus.send(msg);
        m_daemonSid = -1;
    }
    if (!m_daemonScriptPath.isEmpty()) {
        QFile::remove(m_daemonScriptPath);
        m_daemonScriptPath.clear();
    }
}

KWinBridge::~KWinBridge()
{
    stopDaemon();
}

void KWinBridge::setupDaemon()
{
    int pid = getpid();
    m_daemonScriptPath = QDir::tempPath() + QStringLiteral("/karyon_daemon_%1.js").arg(pid);

    // KWin-Daemon-Script:
    // 1. Initialer Snapshot aller normalen Fenster mit Markierung des aktiven Fensters
    // 2. Echtzeit-Listener auf windowActivated, windowAdded, windowRemoved
    QString js = QStringLiteral(R"JS(
var wins = workspace.windowList ? workspace.windowList() : workspace.stackingOrder;
var act = workspace.activeWindow;
var actId = act ? act.internalId.toString() : "";
var initList = [];

for (var i = 0; i < wins.length; i++) {
    var w = wins[i];
    if (!w || (!w.normalWindow && !w.fullScreen) || (w.skipTaskbar && !w.fullScreen) || w.deleted || w.resourceClass === "karyon") continue;
    initList.push({
        id: w.internalId.toString(),
        rc: w.resourceClass,
        df: w.desktopFileName,
        cap: w.caption,
        min: (w.minimized === true),
        act: (w.internalId.toString() === actId),
        pid: (w.pid ? w.pid : 0)
    });
}

var lastCurX = -1, lastCurY = -1;
var curTimer = new QTimer(workspace);
curTimer.interval = 10;
curTimer.timeout.connect(function() {
    var c = workspace.cursorPos;
    if (c) {
        var rx = Math.round(c.x);
        var ry = Math.round(c.y);
        if (rx !== lastCurX || ry !== lastCurY) {
            lastCurX = rx;
            lastCurY = ry;
            callDBus("%1", "%2", "%3", "reportCursorPos", rx, ry);
        }
    }
});
curTimer.start();

var initC = workspace.cursorPos;
if (initC) {
    lastCurX = Math.round(initC.x);
    lastCurY = Math.round(initC.y);
    callDBus("%1", "%2", "%3", "reportCursorPos", lastCurX, lastCurY);
}

callDBus("%1", "%2", "%3", "onDaemonInit", JSON.stringify(initList));

workspace.windowActivated.connect(function(w) {
    if (w && (w.normalWindow || w.fullScreen) && !w.deleted && w.resourceClass !== "karyon") {
        callDBus("%1", "%2", "%3", "onWindowActivated",
                 w.internalId.toString(), w.resourceClass, w.desktopFileName, w.caption, (w.minimized === true), (w.pid ? w.pid : 0));
    }
});

workspace.windowAdded.connect(function(w) {
    if (w && (w.normalWindow || w.fullScreen) && (w.fullScreen || !w.skipTaskbar) && !w.deleted && w.resourceClass !== "karyon") {
        callDBus("%1", "%2", "%3", "onWindowAdded",
                 w.internalId.toString(), w.resourceClass, w.desktopFileName, w.caption, (w.minimized === true), (w.pid ? w.pid : 0));
    }
});

workspace.windowRemoved.connect(function(w) {
    if (w) {
        callDBus("%1", "%2", "%3", "onWindowRemoved",
                 w.internalId.toString());
    }
});
)JS").arg(SERVICE_NAME, OBJECT_PATH, INTERFACE_NAME);

    QFile f(m_daemonScriptPath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return;
    }
    f.write(js.toUtf8());
    f.close();

    QDBusConnection bus = QDBusConnection::sessionBus();
    QDBusInterface scripting(QStringLiteral("org.kde.KWin"), QStringLiteral("/Scripting"),
                             QStringLiteral("org.kde.kwin.Scripting"), bus);

    QDBusReply<int> reply = scripting.call(QStringLiteral("loadScript"), m_daemonScriptPath);
    if (reply.isValid()) {
        m_daemonSid = reply.value();
        if (m_daemonSid >= 0) {
            QDBusInterface script(QStringLiteral("org.kde.KWin"),
                                  QStringLiteral("/Scripting/Script%1").arg(m_daemonSid),
                                  QStringLiteral("org.kde.kwin.Script"), bus);
            script.call(QStringLiteral("run"));
            scripting.call(QStringLiteral("start"));
        }
    } else {
        std::cerr << "[KWinBridge] Error loading KWin daemon: " << reply.error().message().toStdString() << "\n";
    }
}

void KWinBridge::onDaemonInit(const QString &json)
{
    QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
    if (!doc.isArray()) return;

    QJsonArray arr = doc.array();
    m_knownWindows.clear();
    m_mruOrder.clear();

    QString activeId;
    QStringList otherIds;

    for (const auto &val : arr) {
        QJsonObject obj = val.toObject();
        WindowInfo info;
        info.id = obj[QStringLiteral("id")].toString();
        info.resourceClass = obj[QStringLiteral("rc")].toString();
        info.desktopFileName = obj[QStringLiteral("df")].toString();
        info.caption = obj[QStringLiteral("cap")].toString();
        info.minimized = obj[QStringLiteral("min")].toBool();
        info.pid = obj[QStringLiteral("pid")].toInt();
        bool isActive = obj[QStringLiteral("act")].toBool();

        m_knownWindows[info.id] = info;

        if (isActive) {
            activeId = info.id;
        } else {
            otherIds.append(info.id);
        }
    }

    if (!activeId.isEmpty()) {
        m_mruOrder.append(activeId);
        if (m_knownWindows.contains(activeId)) {
            AppManager::recordAppUsage(m_knownWindows[activeId].desktopFileName,
                                       m_knownWindows[activeId].resourceClass,
                                       m_knownWindows[activeId].pid,
                                       m_knownWindows[activeId].caption);
            const WindowInfo &actInfo = m_knownWindows[activeId];
            bool isGame = GameModeDetector::isGame(actInfo.resourceClass, actInfo.desktopFileName, actInfo.caption, actInfo.pid);
            m_activeWindowIsGame = isGame;
            emit activeWindowGameStatusChanged(isGame);
        }
    }
    m_mruOrder.append(otherIds);

    emit windowsUpdated();
}

void KWinBridge::onWindowActivated(const QString &id, const QString &rc, const QString &df, const QString &cap, bool min, int pid)
{
    if (id.isEmpty() || rc == QLatin1String("karyon")) return;

    AppManager::recordAppUsage(df, rc, pid, cap);

    WindowInfo info;
    info.id = id;
    info.resourceClass = rc;
    info.desktopFileName = df;
    info.caption = cap;
    info.minimized = min;
    info.pid = pid;

    m_knownWindows[id] = info;

    bool isGame = GameModeDetector::isGame(rc, df, cap, pid);
    if (m_activeWindowIsGame != isGame) {
        m_activeWindowIsGame = isGame;
        emit activeWindowGameStatusChanged(isGame);
    }

    // Wenn der Desktop aktiv war und außerhalb des Overlays ein Fenster aktiviert wird:
    if (m_desktopShowing && !m_overlayOpen) {
        m_desktopShowing = false;
        m_desktopHiddenWindowIds.clear();
        emit showDesktopChanged(false);
    }

    // Wenn das Overlay geöffnet ist, darf ein Hintergrund-Fokuswechsel von KWin
    // (z.B. durch Minimieren oder Schließen) die Segment-Reihenfolge NICHT verändern!
    if (m_overlayOpen) {
        return;
    }

    KLOG("KWin", "Window activated: id=%s", qPrintable(id));

    // Streng chronologisches MRU-Update im regulären Betrieb
    m_mruOrder.removeAll(id);
    m_mruOrder.prepend(id);

    emit windowsUpdated();
}

void KWinBridge::onWindowAdded(const QString &id, const QString &rc, const QString &df, const QString &cap, bool min, int pid)
{
    if (id.isEmpty() || rc == QLatin1String("karyon")) return;

    KLOG("KWin", "Window added: id=%s caption='%s' df='%s' pid=%d", qPrintable(id), qPrintable(cap), qPrintable(df), pid);

    AppManager::recordAppUsage(df, rc, pid, cap);

    WindowInfo info;
    info.id = id;
    info.resourceClass = rc;
    info.desktopFileName = df;
    info.caption = cap;
    info.minimized = min;
    info.pid = pid;

    m_knownWindows[id] = info;
    if (!m_mruOrder.contains(id)) {
        m_mruOrder.append(id);
    }
}

void KWinBridge::onWindowRemoved(const QString &id)
{
    KLOG("KWin", "Window removed: id=%s", qPrintable(id));
    bool changed = false;
    if (m_knownWindows.contains(id)) {
        m_knownWindows.remove(id);
        changed = true;
    }
    if (m_mruOrder.contains(id)) {
        m_mruOrder.removeAll(id);
        changed = true;
    }
    if (changed) {
        if (!m_mruOrder.isEmpty()) {
            QString topId = m_mruOrder.first();
            if (m_knownWindows.contains(topId)) {
                const WindowInfo &topInfo = m_knownWindows[topId];
                bool isGame = GameModeDetector::isGame(topInfo.resourceClass, topInfo.desktopFileName, topInfo.caption, topInfo.pid);
                if (m_activeWindowIsGame != isGame) {
                    m_activeWindowIsGame = isGame;
                    emit activeWindowGameStatusChanged(isGame);
                }
            }
        } else {
            if (m_activeWindowIsGame) {
                m_activeWindowIsGame = false;
                emit activeWindowGameStatusChanged(false);
            }
        }
        emit windowsUpdated();
    }
}

QList<WindowInfo> KWinBridge::lastWindows() const
{
    QList<WindowInfo> result;
    for (const QString &id : m_mruOrder) {
        auto it = m_knownWindows.find(id);
        if (it != m_knownWindows.end()) {
            result.append(it.value());
        }
    }
    return result;
}

QPoint KWinBridge::lastCursorPos() const
{
    if (m_lastPos.x() != 0 || m_lastPos.y() != 0) {
        return m_lastPos;
    }
    QPoint qpos = QCursor::pos();
    if (!qpos.isNull() && (qpos.x() != 0 || qpos.y() != 0)) {
        return qpos;
    }
    return m_lastPos;
}

bool KWinBridge::isActiveWindowGame() const
{
    if (!m_mruOrder.isEmpty() && m_knownWindows.contains(m_mruOrder.first())) {
        const WindowInfo &top = m_knownWindows[m_mruOrder.first()];
        return GameModeDetector::isGame(top.resourceClass, top.desktopFileName, top.caption, top.pid);
    }
    return m_activeWindowIsGame;
}

void KWinBridge::queryCursor()
{
    QString js = QStringLiteral(
        "var c = workspace.cursorPos;\n"
        "if (c) {\n"
        "    callDBus(\"%1\", \"%2\", \"%3\", \"reportCursorPos\", Math.round(c.x), Math.round(c.y));\n"
        "}\n"
    ).arg(SERVICE_NAME, OBJECT_PATH, INTERFACE_NAME);

    runScriptAsync(js, QStringLiteral("karyon_cur"));
}

void KWinBridge::reportCursorPos(int x, int y)
{
    m_lastPos = QPoint(x, y);
    emit cursorPosUpdated(m_lastPos);
}

void KWinBridge::reportCursorPos(double x, double y)
{
    reportCursorPos(static_cast<int>(std::round(x)), static_cast<int>(std::round(y)));
}

void KWinBridge::activateWindow(const QString &winId)
{
    if (winId.isEmpty() || winId.startsWith(QLatin1String("__"))) return;

    if (m_desktopShowing) {
        m_desktopShowing = false;
        m_desktopHiddenWindowIds.clear();
        emit showDesktopChanged(false);
    }

    // 1. Sofortiges synchrones lokales MRU-Update in C++
    m_mruOrder.removeAll(winId);
    m_mruOrder.prepend(winId);

    // 2. KWin-Script zur Fensteraktivierung asynchron feuern
    QString js = QStringLiteral(
        "var wins = workspace.windowList ? workspace.windowList() : workspace.stackingOrder;\n"
        "for (var i = 0; i < wins.length; i++) {\n"
        "    if (wins[i].internalId.toString() === \"%1\") {\n"
        "        wins[i].minimized = false;\n"
        "        if (workspace.raiseWindow) workspace.raiseWindow(wins[i]);\n"
        "        workspace.activeWindow = wins[i];\n"
        "        break;\n"
        "    }\n"
        "}\n"
    ).arg(winId);

    // Shot 1: Sofort
    runScriptAsync(js, QStringLiteral("karyon_act"));

    // Shot 2: Nach 60ms bekräftigen
    QTimer::singleShot(60, this, [this, js]() {
        runScriptAsync(js, QStringLiteral("karyon_act2"));
    });

    // Shot 3: Nach 160ms nochmals bekräftigen (gewinnt garantiert gegen asynchronen Focus-Restore beim Window-Hide)
    QTimer::singleShot(160, this, [this, js]() {
        runScriptAsync(js, QStringLiteral("karyon_act3"));
    });
}

void KWinBridge::closeWindow(const QString &winId)
{
    if (winId.isEmpty() || winId.startsWith(QLatin1String("__"))) return;

    QString js = QStringLiteral(
        "var wins = workspace.windowList ? workspace.windowList() : workspace.stackingOrder;\n"
        "for (var i = 0; i < wins.length; i++) {\n"
        "    if (wins[i].internalId.toString() === \"%1\") {\n"
        "        wins[i].closeWindow();\n"
        "        break;\n"
        "    }\n"
        "}\n"
    ).arg(winId);

    runScriptAsync(js, QStringLiteral("karyon_close"));

    // WICHTIG: Fenster NICHT voreilig lokal entfernen!
    // Erst wenn KWin das Fenster tatsächlich zerstört (z.B. nach Bestätigung von Speichern-Dialogen),
    // meldet der KWin-Daemon workspace.windowRemoved -> onWindowRemoved().
}

void KWinBridge::toggleMaximize(const QString &winId)
{
    if (winId.isEmpty() || winId.startsWith(QLatin1String("__"))) return;

    QString js = QStringLiteral(
        "var wins = workspace.windowList ? workspace.windowList() : workspace.stackingOrder;\n"
        "for (var i = 0; i < wins.length; i++) {\n"
        "    if (wins[i].internalId.toString() === \"%1\") {\n"
        "        if (wins[i].maximizeMode !== 0) {\n"
        "            wins[i].setMaximize(false, false);\n"
        "        } else {\n"
        "            wins[i].setMaximize(true, true);\n"
        "        }\n"
        "        break;\n"
        "    }\n"
        "}\n"
    ).arg(winId);

    runScriptAsync(js, QStringLiteral("karyon_max"));
}

void KWinBridge::toggleMinimize(const QString &winId)
{
    if (winId.isEmpty() || winId.startsWith(QLatin1String("__"))) return;

    QString js = QStringLiteral(
        "var wins = workspace.windowList ? workspace.windowList() : workspace.stackingOrder;\n"
        "for (var i = 0; i < wins.length; i++) {\n"
        "    if (wins[i].internalId.toString() === \"%1\") {\n"
        "        wins[i].minimized = !wins[i].minimized;\n"
        "        break;\n"
        "    }\n"
        "}\n"
    ).arg(winId);

    runScriptAsync(js, QStringLiteral("karyon_min"));
}

void KWinBridge::toggleShowDesktop()
{
    // Zustand 1: desktopShowing == false -> Freiräumen: alle sichtbaren Fenster minimieren
    // Zustand 2: desktopShowing == true -> Wiederherstellen: zuvor minimierte Fenster wieder einblenden
    bool restore = m_desktopShowing;

    QString idsJson = QStringLiteral("[]");
    if (restore && !m_desktopHiddenWindowIds.isEmpty()) {
        QJsonArray arr;
        for (const QString &id : m_desktopHiddenWindowIds) {
            arr.append(id);
        }
        idsJson = QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact));
    }

    QString js = QStringLiteral(R"JS(
var wins = workspace.windowList ? workspace.windowList() : workspace.stackingOrder;
var ignore = ["karyon", "python3", "plasmashell", "org.kde.plasmashell", "kwin_wayland", "kwin_x11", "kwin", "org.kde.kwin_wayland", "org.kde.kwin", "xembedsniproxy"];
var isRestore = %4;
var toRestore = %5;

if (isRestore) {
    for (var i = 0; i < wins.length; i++) {
        var w = wins[i];
        if (!w) continue;
        var id = w.internalId.toString();
        if (toRestore.length > 0) {
            if (toRestore.indexOf(id) >= 0) {
                w.minimized = false;
            }
        } else {
            if (w.normalWindow && !w.skipTaskbar && !w.deleted && ignore.indexOf(w.resourceClass) < 0) {
                w.minimized = false;
            }
        }
    }
    callDBus("%1", "%2", "%3", "onShowDesktopDone", false, "[]");
} else {
    var hiddenIds = [];
    for (var i = 0; i < wins.length; i++) {
        var w = wins[i];
        if (!w || !w.normalWindow || w.skipTaskbar || w.deleted) continue;
        if (ignore.indexOf(w.resourceClass) >= 0) continue;
        if (w.minimized === false) {
            hiddenIds.push(w.internalId.toString());
            w.minimized = true;
        }
    }
    callDBus("%1", "%2", "%3", "onShowDesktopDone", true, JSON.stringify(hiddenIds));
}
)JS").arg(SERVICE_NAME, OBJECT_PATH, INTERFACE_NAME,
          restore ? QStringLiteral("true") : QStringLiteral("false"),
          idsJson);

    runScriptAsync(js, QStringLiteral("karyon_show_desktop"));
}

void KWinBridge::onShowDesktopDone(bool showing, const QString &json)
{
    m_desktopShowing = showing;
    if (showing) {
        if (m_activeWindowIsGame) {
            m_activeWindowIsGame = false;
            emit activeWindowGameStatusChanged(false);
        }
        QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
        m_desktopHiddenWindowIds.clear();
        if (doc.isArray()) {
            for (const auto &v : doc.array()) {
                QString id = v.toString();
                m_desktopHiddenWindowIds.append(id);
                if (m_knownWindows.contains(id)) {
                    m_knownWindows[id].minimized = true;
                }
            }
        }
    } else {
        for (const QString &id : m_desktopHiddenWindowIds) {
            if (m_knownWindows.contains(id)) {
                m_knownWindows[id].minimized = false;
            }
        }
        m_desktopHiddenWindowIds.clear();
        if (!m_mruOrder.isEmpty() && m_knownWindows.contains(m_mruOrder.first())) {
            const WindowInfo &topInfo = m_knownWindows[m_mruOrder.first()];
            bool isGame = GameModeDetector::isGame(topInfo.resourceClass, topInfo.desktopFileName, topInfo.caption, topInfo.pid);
            if (m_activeWindowIsGame != isGame) {
                m_activeWindowIsGame = isGame;
                emit activeWindowGameStatusChanged(isGame);
            }
        }
    }
    emit showDesktopChanged(m_desktopShowing);
    emit windowsUpdated();
}

void KWinBridge::runScriptAsync(const QString &js, const QString &prefix)
{
    int pid = getpid();
    int count = ++m_counter;
    QString scriptPath = QDir::tempPath() + QStringLiteral("/%1_%2_%3.js").arg(prefix).arg(pid).arg(count);

    QFile f(scriptPath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return;
    }
    f.write(js.toUtf8());
    f.close();

    QDBusConnection bus = QDBusConnection::sessionBus();
    QDBusInterface scripting(QStringLiteral("org.kde.KWin"), QStringLiteral("/Scripting"),
                             QStringLiteral("org.kde.kwin.Scripting"), bus);

    QDBusPendingCall pcall = scripting.asyncCall(QStringLiteral("loadScript"), scriptPath);
    auto *watcher = new QDBusPendingCallWatcher(pcall, this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, scriptPath](QDBusPendingCallWatcher *callWatcher) {
        QDBusPendingReply<int> reply = *callWatcher;
        callWatcher->deleteLater();
        if (reply.isValid()) {
            int sid = reply.value();
            if (sid >= 0) {
                QDBusConnection bus = QDBusConnection::sessionBus();
                QDBusInterface script(QStringLiteral("org.kde.KWin"),
                                      QStringLiteral("/Scripting/Script%1").arg(sid),
                                      QStringLiteral("org.kde.kwin.Script"), bus);
                script.asyncCall(QStringLiteral("run"));
                QDBusInterface scripting(QStringLiteral("org.kde.KWin"), QStringLiteral("/Scripting"),
                                         QStringLiteral("org.kde.kwin.Scripting"), bus);
                scripting.asyncCall(QStringLiteral("start"));

                // Nach 300ms Script stoppen und JS-Datei aufräumen
                QTimer::singleShot(300, this, [sid, scriptPath]() {
                    QDBusConnection bus = QDBusConnection::sessionBus();
                    QDBusInterface script(QStringLiteral("org.kde.KWin"),
                                          QStringLiteral("/Scripting/Script%1").arg(sid),
                                          QStringLiteral("org.kde.kwin.Script"), bus);
                    script.asyncCall(QStringLiteral("stop"));
                    QFile::remove(scriptPath);
                });
                return;
            }
        }
        QFile::remove(scriptPath);
    });
}
