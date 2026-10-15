#pragma once

#include <QString>
#include <QList>
#include <QSet>

struct AppEntry {
    QString id;
    QString name;
    QString icon;
    QString exec;
    QString comment;
    QString desktopFile;
};

struct CategoryEntry {
    QString id;
    QString name;
    QString icon;
    QList<AppEntry> apps;
};

class AppManager {
public:
    AppManager();

    QList<AppEntry> loadRecentApps(int maxCount = 13, const QSet<QString> &excludeAppKeys = {});
    QList<CategoryEntry> loadCategories();
    QList<AppEntry> loadFavorites();

    static bool launchApp(const QString &execLine);
    static bool isBlacklistedRecentApp(const AppEntry &entry, int pid = 0, const QString &caption = QString());

    static AppEntry findApp(const QString &desktopFileName, const QString &resourceClass);
    static void recordAppUsage(const QString &desktopFileName, const QString &resourceClass, int pid = 0, const QString &caption = QString());
    static void recordAppUsageById(const QString &appId);
    static QString recentAppsFilePath();

    static bool isAppImage(const QString &path);
    static AppEntry resolveAppImageFromPid(int pid);
    static AppEntry resolveAppImageFromPath(const QString &appImagePath, int pid = 0);
    static AppEntry findAppByExec(const QString &execPath);
    static QString findDesktopFilePath(const QString &name);
    static QString findDesktopFilePathCaseInsensitive(const QString &name);

    static AppEntry resolveWindowApp(const QString &desktopFileName, const QString &resourceClass, int pid = 0, const QString &caption = QString());
    static bool isSteamApp(const QString &desktopFileName, const QString &resourceClass, QString *outSteamId = nullptr, int pid = 0);
    static AppEntry resolveSteamGame(const QString &steamId, const QString &captionFallback = QString());
};
