#pragma once

#include <QObject>
#include <QString>
#include <QList>
#include <QIcon>
#include <QtDBus>
#include <functional>

struct TrayProgramItem {
    QString service;
    QString path;
    QString id;
    QString title;
    QString iconName;
    QString menuPath;
    QIcon icon;
    uint pid = 0;
};

struct TrayMenuAction {
    int id = -1;
    QString label;
    bool enabled = true;
    bool visible = true;
    QString service;
    QString menuPath;
};

struct SystemTrayApplet {
    QString id;
    QString name;
    QString pluginId;
    QString glyphName;
    bool isPrimary = true;
};

class TrayManager : public QObject
{
    Q_OBJECT

public:
    explicit TrayManager(QObject *parent = nullptr);
    ~TrayManager() override = default;

    QList<TrayProgramItem> runningPrograms();
    QList<TrayMenuAction> fetchMenuActions(const QString &service, const QString &menuPath);

    static QList<SystemTrayApplet> primaryApplets();
    static QList<SystemTrayApplet> secondaryApplets();

    static void activateItem(const QString &service, const QString &path, int x, int y);
    static void triggerMenuAction(const QString &service, const QString &menuPath, int actionId);
    static void launchPlasmoid(const QString &pluginId);
    static void syncPlasmoidConfig(const QString &pluginId);

    static void setKaryonActionCallback(std::function<void(int)> cb);
    static void setKaryonSettingsCallback(std::function<void()> cb);

signals:
    void trayIconsChanged();

private slots:
    void onDbusTrayChanged();
    void onDbusTrayChangedWithArg(const QString &arg);
};
