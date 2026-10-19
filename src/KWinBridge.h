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
    bool isActiveWindowGame() const { return m_activeWindowIsGame; }
    QPoint lastCursorPos() const;
    QList<WindowInfo> lastWindows() const;
    QString activeWindowId() const { return m_mruOrder.isEmpty() ? QString() : m_mruOrder.first(); }

public slots:
    Q_SCRIPTABLE void onDaemonInit(const QString &json);
    Q_SCRIPTABLE void onWindowActivated(const QString &id, const QString &rc, const QString &df, const QString &cap, bool min, int pid = 0);
    Q_SCRIPTABLE void onWindowAdded(const QString &id, const QString &rc, const QString &df, const QString &cap, bool min, int pid = 0);
    Q_SCRIPTABLE void onWindowRemoved(const QString &id);
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
    int m_counter = 0;
    int m_daemonSid = -1;
    QString m_daemonScriptPath;
    bool m_overlayOpen = false;
    bool m_desktopShowing = false;
    QStringList m_desktopHiddenWindowIds;
};
