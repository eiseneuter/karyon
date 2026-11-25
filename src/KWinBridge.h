#pragma once

#include <QObject>
#include <QPoint>
#include <QtDBus>
#include <QHash>
#include <QStringList>

struct WindowInfo {
    QString id;
    QString resourceClass;
    QString desktopFileName;
    QString caption;
    bool minimized = false;
    int pid = 0;
};

class KWinBridge : public QObject
{
    Q_OBJECT

public:
    explicit KWinBridge(QObject *parent = nullptr);
    ~KWinBridge() override;

    void stopDaemon();
    void queryCursor();
    void activateWindow(const QString &winId);
    void closeWindow(const QString &winId);
    void toggleMaximize(const QString &winId);
    void toggleMinimize(const QString &winId);
    void toggleShowDesktop();
    bool isDesktopShowing() const { return m_desktopShowing; }
    void setOverlayOpen(bool open) { m_overlayOpen = open; }
    bool isOverlayOpen() const { return m_overlayOpen; }
    bool isActiveWindowGame() const;
    QPoint lastCursorPos() const;
    QList<WindowInfo> lastWindows() const;
    // Das Fenster, das gerade wirklich den Fokus hat (auch Dialoge). Nur wenn KWin keines
    // gemeldet hat, das zuletzt aktive normale Fenster.
    QString activeWindowId() const
    {
        if (!m_focusedId.isEmpty()) return m_focusedId;
        return m_mruOrder.isEmpty() ? QString() : m_mruOrder.first();
    }

public slots:
    Q_SCRIPTABLE void onDaemonInit(const QString &json);
    Q_SCRIPTABLE void onWindowActivated(const QString &id, const QString &rc, const QString &df, const QString &cap, bool min, int pid = 0);
    Q_SCRIPTABLE void onWindowAdded(const QString &id, const QString &rc, const QString &df, const QString &cap, bool min, int pid = 0);
    Q_SCRIPTABLE void onWindowRemoved(const QString &id);
    Q_SCRIPTABLE void onOtherActivated(const QString &id);
    Q_SCRIPTABLE void reportCursorPos(int x, int y);
    Q_SCRIPTABLE void reportCursorPos(double x, double y);
    Q_SCRIPTABLE void onShowDesktopDone(bool showing, const QString &json);

signals:
    void cursorPosUpdated(const QPoint &pos);
    void windowsUpdated();
    void showDesktopChanged(bool showing);
    void activeWindowGameStatusChanged(bool isGame);

private:
    void setupDaemon();
    void runScriptAsync(const QString &js, const QString &prefix);

    bool m_activeWindowIsGame = false;

    QPoint m_lastPos{0, 0};
    QHash<QString, WindowInfo> m_knownWindows;
    QStringList m_mruOrder;
    bool m_focusOnDesktop = false;
    QString m_focusedId; // tatsächlich fokussiertes Fenster laut KWin (leer = Desktop/keines)
    QDBusServiceWatcher *m_kwinWatcher = nullptr;
    int m_counter = 0;
    int m_daemonSid = -1;
    QString m_daemonScriptPath;
    bool m_overlayOpen = false;
    bool m_desktopShowing = false;
    QStringList m_desktopHiddenWindowIds;
};
