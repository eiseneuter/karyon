#include "GameModeDetector.h"

#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QByteArray>
#include <QStringList>
#include <QSet>
#include <KService>
#include <iostream>

bool GameModeDetector::isLauncherUI(const QString &resourceClass, const QString &desktopFileName, const QString &caption, int pid)
{
    // Echte Steam-Spiele unter Proton/Wine haben 'steam_app_<id>' – diese sind definitiv SPIELE, keine Launcher!
    if (resourceClass.contains(QLatin1String("steam_app_"), Qt::CaseInsensitive) ||
        desktopFileName.contains(QLatin1String("steam_app_"), Qt::CaseInsensitive)) {
        return false;
    }

    QString rc = resourceClass.trimmed().toLower();
    QString df = desktopFileName.trimmed().toLower();

    // Sonderfall Steam: Spiele, die unter Steam gestartet werden (nativ oder Wine/Proton),
    // erben in KWin oft den Desktop-Dateinamen 'steam'/'steam.desktop' oder den Flatpak-ID.
    // Wenn die Fensterklasse (resourceClass) weder 'steam' noch 'steamwebhelper' ist,
    // handelt es sich um das gestartete Spiel, keinesfalls um das Steam-Client-UI!
    if ((df == QLatin1String("steam") || df == QLatin1String("steam.desktop") ||
         df == QLatin1String("com.valvesoftware.steam") || df == QLatin1String("com.valvesoftware.steam.desktop")) &&
        !rc.isEmpty() && rc != QLatin1String("steam") && rc != QLatin1String("steamwebhelper") &&
        !rc.contains(QLatin1String("steamwebhelper"))) {
        return false;
    }

    static const QSet<QString> launcherClasses = {
        QStringLiteral("steam"),
        QStringLiteral("steamwebhelper"),
        QStringLiteral("heroic"),
        QStringLiteral("com.heroicgameslauncher.hgl"),
        QStringLiteral("lutris"),
        QStringLiteral("net.lutris.lutris"),
        QStringLiteral("net.lutris.lutris1"),
        QStringLiteral("bottles"),
        QStringLiteral("com.usebottles.bottles"),
        QStringLiteral("prismlauncher"),
        QStringLiteral("org.prismlauncher.prismlauncher"),
        QStringLiteral("polymc"),
        QStringLiteral("org.polymc.polymc"),
        QStringLiteral("multimc"),
        QStringLiteral("org.multimc.multimc"),
        QStringLiteral("minecraft-launcher"),
        QStringLiteral("itch"),
        QStringLiteral("io.itch.itch"),
        QStringLiteral("minigalaxy"),
        QStringLiteral("io.github.sharkwouter.minigalaxy"),
        QStringLiteral("modrinth-app"),
        QStringLiteral("com.modrinth.theseus"),
        QStringLiteral("curseforge"),
        QStringLiteral("page.kramo.cartridges"),
        QStringLiteral("cartridges"),
        QStringLiteral("pegasus-frontend"),
        QStringLiteral("org.pegasus_frontend.pegasus"),
        QStringLiteral("playonlinux"),
        // Wine / Windows Launcher Frontends
        QStringLiteral("battle.net.exe"),
        QStringLiteral("battle.net"),
        QStringLiteral("epicgameslauncher.exe"),
        QStringLiteral("epicgameslauncher"),
        QStringLiteral("galaxyclient.exe"),
        QStringLiteral("galaxyclient"),
        QStringLiteral("eadesktop.exe"),
        QStringLiteral("eaconnect.exe"),
        QStringLiteral("origin.exe"),
        QStringLiteral("ubisoftconnect.exe"),
        QStringLiteral("upc.exe"),
        QStringLiteral("riotclientux.exe")
    };

    if (!rc.isEmpty() && launcherClasses.contains(rc)) {
        return true;
    }

    static const QSet<QString> launcherDesktopNames = {
        QStringLiteral("steam"),
        QStringLiteral("steam.desktop"),
        QStringLiteral("com.heroicgameslauncher.hgl"),
        QStringLiteral("com.heroicgameslauncher.hgl.desktop"),
        QStringLiteral("heroic"),
        QStringLiteral("heroic.desktop"),
        QStringLiteral("net.lutris.lutris"),
        QStringLiteral("net.lutris.lutris.desktop"),
        QStringLiteral("net.lutris.lutris1"),
        QStringLiteral("net.lutris.lutris1.desktop"),
        QStringLiteral("lutris"),
        QStringLiteral("lutris.desktop"),
        QStringLiteral("com.usebottles.bottles"),
        QStringLiteral("com.usebottles.bottles.desktop"),
        QStringLiteral("bottles"),
        QStringLiteral("bottles.desktop"),
        QStringLiteral("org.prismlauncher.prismlauncher"),
        QStringLiteral("org.prismlauncher.prismlauncher.desktop"),
        QStringLiteral("prismlauncher"),
        QStringLiteral("prismlauncher.desktop"),
        QStringLiteral("org.polymc.polymc"),
        QStringLiteral("org.polymc.polymc.desktop"),
        QStringLiteral("polymc"),
        QStringLiteral("polymc.desktop"),
        QStringLiteral("org.multimc.multimc"),
        QStringLiteral("org.multimc.multimc.desktop"),
        QStringLiteral("multimc"),
        QStringLiteral("multimc.desktop"),
        QStringLiteral("minecraft-launcher"),
        QStringLiteral("minecraft-launcher.desktop"),
        QStringLiteral("io.itch.itch"),
        QStringLiteral("io.itch.itch.desktop"),
        QStringLiteral("itch"),
        QStringLiteral("itch.desktop"),
        QStringLiteral("io.github.sharkwouter.minigalaxy"),
        QStringLiteral("io.github.sharkwouter.minigalaxy.desktop"),
        QStringLiteral("minigalaxy"),
        QStringLiteral("minigalaxy.desktop"),
        QStringLiteral("com.modrinth.theseus"),
        QStringLiteral("com.modrinth.theseus.desktop"),
        QStringLiteral("modrinth-app"),
        QStringLiteral("modrinth-app.desktop"),
        QStringLiteral("curseforge"),
        QStringLiteral("curseforge.desktop"),
        QStringLiteral("page.kramo.cartridges"),
        QStringLiteral("page.kramo.cartridges.desktop"),
        QStringLiteral("cartridges"),
        QStringLiteral("cartridges.desktop"),
        QStringLiteral("org.pegasus_frontend.pegasus"),
        QStringLiteral("org.pegasus_frontend.pegasus.desktop"),
        QStringLiteral("pegasus-frontend"),
        QStringLiteral("pegasus-frontend.desktop"),
        QStringLiteral("playonlinux"),
        QStringLiteral("playonlinux.desktop")
    };

    if (!df.isEmpty() && launcherDesktopNames.contains(df)) {
        return true;
    }

    if (pid > 0) {
        QString exePath = QFile::symLinkTarget(QStringLiteral("/proc/%1/exe").arg(pid));
        if (!exePath.isEmpty()) {
            QString exeName = QFileInfo(exePath).fileName().toLower();
            static const QSet<QString> launcherBinaries = {
                QStringLiteral("steam"),
                QStringLiteral("steamwebhelper"),
                QStringLiteral("heroic"),
                QStringLiteral("lutris"),
                QStringLiteral("bottles"),
                QStringLiteral("prismlauncher"),
                QStringLiteral("polymc"),
                QStringLiteral("multimc"),
                QStringLiteral("minecraft-launcher"),
                QStringLiteral("itch"),
                QStringLiteral("minigalaxy"),
                QStringLiteral("theseus"),
                QStringLiteral("cartridges"),
                QStringLiteral("pegasus-fe"),
                QStringLiteral("pegasus"),
                QStringLiteral("playonlinux"),
                QStringLiteral("battle.net.exe"),
                QStringLiteral("epicgameslauncher.exe"),
                QStringLiteral("galaxyclient.exe"),
                QStringLiteral("eadesktop.exe"),
                QStringLiteral("eaconnect.exe"),
                QStringLiteral("origin.exe"),
                QStringLiteral("ubisoftconnect.exe"),
                QStringLiteral("upc.exe"),
                QStringLiteral("riotclientux.exe")
            };
            if (launcherBinaries.contains(exeName)) {
                return true;
            }
        }
    }

    if (!caption.isEmpty()) {
        QString cap = caption.trimmed().toLower();
        if (cap == QLatin1String("steam") ||
            cap.startsWith(QLatin1String("steam - ")) ||
            cap.startsWith(QLatin1String("steam-")) ||
            cap.startsWith(QLatin1String("steam ")) ||
            cap == QLatin1String("heroic games launcher") ||
            cap.startsWith(QLatin1String("heroic games launcher")) ||
            cap == QLatin1String("lutris") ||
            cap.startsWith(QLatin1String("lutris - ")) ||
            cap == QLatin1String("bottles") ||
            cap.startsWith(QLatin1String("prism launcher")) ||
            cap == QLatin1String("battle.net") ||
            cap == QLatin1String("epic games launcher") ||
            cap == QLatin1String("ubisoft connect") ||
            cap == QLatin1String("ea app") ||
            cap == QLatin1String("gog galaxy") ||
            cap == QLatin1String("riot client")) {
            return true;
        }
    }

    return false;
}

bool GameModeDetector::isGame(const QString &resourceClass, const QString &desktopFileName, const QString &caption, int pid)
{
    // Stufe 0: Game Launcher UIs ausschließen (Steam-Client, Heroic, Lutris, Bottles etc. sind keine Spiele!)
    if (isLauncherUI(resourceClass, desktopFileName, caption, pid)) {
        return false;
    }

    // Stufe 1: Proton, Launcher- & Gaming-Wine-Signaturen
    if (checkProtonAndLauncherEnv(pid, resourceClass, desktopFileName)) {
        return true;
    }

    // Stufe 2: XDG-Desktop-Kategorie (Native Linux-Spiele & Emulatoren)
    if (checkDesktopCategory(desktopFileName, resourceClass)) {
        return true;
    }

    // Stufe 3: Gaming-Pfade & Wine mit DXVK/VKD3D
    if (checkProcessPathAndWine(pid, resourceClass)) {
        return true;
    }

    // Stufe 4: Dedizierte Game-Engines (Unity, Unreal, Godot, Pygame, LOVE)
    if (checkEngines(pid)) {
        return true;
    }

    return false;
}

bool GameModeDetector::checkProtonAndLauncherEnv(int pid, const QString &resourceClass, const QString &desktopFileName)
{
    // Schnelle Fensterklassen-Prüfung
    if (resourceClass.contains(QLatin1String("steam_app_"), Qt::CaseInsensitive) ||
        desktopFileName.contains(QLatin1String("steam_app_"), Qt::CaseInsensitive)) {
        return true;
    }
    if (resourceClass.startsWith(QLatin1String("heroic-"), Qt::CaseInsensitive)) {
        return true;
    }

    QString rc = resourceClass.trimmed().toLower();
    QString df = desktopFileName.trimmed().toLower();
    if ((df == QLatin1String("steam") || df == QLatin1String("steam.desktop") ||
         df == QLatin1String("com.valvesoftware.steam") || df == QLatin1String("com.valvesoftware.steam.desktop")) &&
        !rc.isEmpty() && rc != QLatin1String("steam") && rc != QLatin1String("steamwebhelper") &&
        !rc.contains(QLatin1String("steamwebhelper"))) {
        return true;
    }

    if (pid <= 0) return false;

    // /proc/<pid>/environ prüfen (Nullbyte-separiert)
    QFile envFile(QStringLiteral("/proc/%1/environ").arg(pid));
    if (envFile.open(QIODevice::ReadOnly)) {
        // Erste 16 KB genügen vollständig für Standard-Gaming-Variablen
        QByteArray data = envFile.read(16384);
        envFile.close();

        static const QList<QByteArray> markers = {
            "STEAM_COMPAT_DATA_PATH=",
            "UMU_ID=",
            "GAMEID=",
            "SteamAppId=",
            "SteamGameId=",
            "LUTRIS_GAME_UUID=",
            "HEROIC_APP_NAME=",
            "WINEFSYNC=",
            "WINEESYNC=",
            "DXVK_STATE_CACHE_PATH="
        };

        for (const auto &marker : markers) {
            if (data.contains(marker)) {
                return true;
            }
        }
    }

    return false;
}

bool GameModeDetector::checkDesktopCategory(const QString &desktopFileName, const QString &resourceClass)
{
    auto checkService = [](const QString &key) -> bool {
        if (key.isEmpty()) return false;
        KService::Ptr s;
        if (key.endsWith(QLatin1String(".desktop"))) {
            s = KService::serviceByDesktopPath(key);
            if (!s) {
                QString stripped = key;
                stripped.chop(8);
                s = KService::serviceByDesktopName(stripped);
            }
        } else {
            s = KService::serviceByDesktopName(key);
        }
        if (!s) {
            s = KService::serviceByStorageId(key);
        }
        if (s && s->isValid()) {
            const QStringList cats = s->categories();
            for (const QString &c : cats) {
                if (c.compare(QLatin1String("Game"), Qt::CaseInsensitive) == 0 ||
                    c.compare(QLatin1String("Emulator"), Qt::CaseInsensitive) == 0 ||
                    c.compare(QLatin1String("ActionGame"), Qt::CaseInsensitive) == 0 ||
                    c.compare(QLatin1String("AdventureGame"), Qt::CaseInsensitive) == 0 ||
                    c.compare(QLatin1String("ArcadeGame"), Qt::CaseInsensitive) == 0 ||
                    c.compare(QLatin1String("StrategyGame"), Qt::CaseInsensitive) == 0 ||
                    c.compare(QLatin1String("Simulation"), Qt::CaseInsensitive) == 0 ||
                    c.compare(QLatin1String("RolePlaying"), Qt::CaseInsensitive) == 0) {
                    return true;
                }
            }
        }
        return false;
    };

    return checkService(desktopFileName) || checkService(resourceClass);
}

bool GameModeDetector::checkProcessPathAndWine(int pid, const QString &resourceClass)
{
    if (pid <= 0) return false;

    QString exePath = QFile::symLinkTarget(QStringLiteral("/proc/%1/exe").arg(pid));
    QString cmdline;
    QFile cmdFile(QStringLiteral("/proc/%1/cmdline").arg(pid));
    if (cmdFile.open(QIODevice::ReadOnly)) {
        cmdline = QString::fromUtf8(cmdFile.read(4096));
        cmdFile.close();
    }

    auto pathMatches = [](const QString &p) {
        if (p.isEmpty()) return false;
        return p.contains(QLatin1String("/steamapps/"), Qt::CaseInsensitive) ||
               p.contains(QLatin1String("pressure-vessel"), Qt::CaseInsensitive) ||
               p.contains(QLatin1String("/PortWINE/"), Qt::CaseInsensitive) ||
               p.contains(QLatin1String("PortProton"), Qt::CaseInsensitive) ||
               p.contains(QLatin1String("/bottles/bottles/"), Qt::CaseInsensitive) ||
               p.contains(QLatin1String("/Games/"), Qt::CaseInsensitive) ||
               p.contains(QLatin1String("/Heroic/"), Qt::CaseInsensitive) ||
               p.contains(QLatin1String("/PrismLauncher/instances/"), Qt::CaseInsensitive) ||
               p.contains(QLatin1String("/.minecraft/"), Qt::CaseInsensitive);
    };

    // Bekannte Gaming-Verzeichnisse
    if (pathMatches(exePath) || pathMatches(cmdline)) {
        return true;
    }

    // Bekannte Emulatoren
    QString exeName = QFileInfo(exePath).fileName().toLower();
    static const QStringList emulators = {
        QStringLiteral("rpcs3"), QStringLiteral("dolphin-emu"), QStringLiteral("retroarch"),
        QStringLiteral("pcsx2"), QStringLiteral("pcsx2-qt"), QStringLiteral("ryujinx"),
        QStringLiteral("cemu"), QStringLiteral("duckstation"), QStringLiteral("ppsspp"),
        QStringLiteral("yuzu"), QStringLiteral("suyu"), QStringLiteral("shadps4")
    };
    if (emulators.contains(exeName)) {
        return true;
    }

    // Wine mit DXVK / VKD3D: Echte Windows-Spiele
    bool isWine = exeName.contains(QLatin1String("wine")) ||
                  exeName.endsWith(QLatin1String(".exe"), Qt::CaseInsensitive) ||
                  resourceClass.endsWith(QLatin1String(".exe"), Qt::CaseInsensitive);

    if (isWine) {
        QFile mapsFile(QStringLiteral("/proc/%1/maps").arg(pid));
        if (mapsFile.open(QIODevice::ReadOnly)) {
            QByteArray maps = mapsFile.read(32768);
            mapsFile.close();
            if (maps.contains("dxgi") || maps.contains("d3d11") || maps.contains("d3d12") ||
                maps.contains("dxvk") || maps.contains("vkd3d")) {
                return true;
            }
        }
    }

    return false;
}

bool GameModeDetector::checkEngines(int pid)
{
    if (pid <= 0) return false;

    QFile mapsFile(QStringLiteral("/proc/%1/maps").arg(pid));
    if (mapsFile.open(QIODevice::ReadOnly)) {
        QByteArray maps = mapsFile.read(32768);
        mapsFile.close();

        if (maps.contains("libUnityPlayer.so") ||
            maps.contains("libUE4") ||
            maps.contains("libUnrealEngine") ||
            maps.contains("libgodot") ||
            maps.contains("liblove") ||
            maps.contains("/pygame/")) {
            return true;
        }
    }

    return false;
}
