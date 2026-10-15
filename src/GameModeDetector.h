#pragma once

#include <QString>

class GameModeDetector
{
public:
    static bool isGame(const QString &resourceClass, const QString &desktopFileName, const QString &caption, int pid);

    static bool isLauncherUI(const QString &resourceClass, const QString &desktopFileName, const QString &caption, int pid);
    static bool checkProtonAndLauncherEnv(int pid, const QString &resourceClass, const QString &desktopFileName);
private:
    static bool checkDesktopCategory(const QString &desktopFileName, const QString &resourceClass);
    static bool checkProcessPathAndWine(int pid, const QString &resourceClass);
    static bool checkEngines(int pid);
};
