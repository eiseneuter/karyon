#pragma once

#include <QProcess>
#include <QProcessEnvironment>
#include <QStringList>
#include <QDir>
#include <QFile>
#include <QIcon>
#include <QCoreApplication>

namespace ProcessUtils {

inline QIcon karyonIcon()
{
    QIcon icon(QStringLiteral(":/qt/qml/karyon/resources/karyon.svg"));
    if (!icon.isNull()) return icon;

    QString userIcon = QDir::homePath() + QStringLiteral("/.local/share/icons/hicolor/scalable/apps/karyon.svg");
    if (QFile::exists(userIcon)) {
        icon = QIcon(userIcon);
        if (!icon.isNull()) return icon;
    }

    icon = QIcon::fromTheme(QStringLiteral("karyon"));
    if (!icon.isNull()) return icon;

    QString appDir = qEnvironmentVariable("APPDIR");
    if (!appDir.isEmpty()) {
        QString appDirIcon = appDir + QStringLiteral("/usr/share/icons/hicolor/scalable/apps/karyon.svg");
        if (QFile::exists(appDirIcon)) {
            icon = QIcon(appDirIcon);
            if (!icon.isNull()) return icon;
        }
    }

    QString relDevIcon = QCoreApplication::applicationDirPath() + QStringLiteral("/../resources/karyon.svg");
    if (QFile::exists(relDevIcon)) {
        icon = QIcon(relDevIcon);
        if (!icon.isNull()) return icon;
    }

    return QIcon(QStringLiteral("/home/eisen/Data/Programme/PythonProjects/Karyon C++/resources/karyon.svg"));
}

inline QProcessEnvironment cleanHostEnvironment()
{
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    QString appdir = qEnvironmentVariable("APPDIR");

    auto cleanPathList = [&env, &appdir](const QString &varName, const QString &origVarName) {
        if (env.contains(origVarName)) {
            QString orig = env.value(origVarName);
            if (orig.isEmpty()) {
                env.remove(varName);
            } else {
                env.insert(varName, orig);
            }
            env.remove(origVarName);
            return;
        }

        if (!env.contains(varName)) return;

        QStringList parts = env.value(varName).split(QLatin1Char(':'), Qt::SkipEmptyParts);
        QStringList cleanParts;
        for (const QString &p : parts) {
            if (!appdir.isEmpty() && p.startsWith(appdir)) continue;
            if (p.contains(QLatin1String("/.mount_"))) continue;
            cleanParts.append(p);
        }

        if (cleanParts.isEmpty()) {
            env.remove(varName);
        } else {
            env.insert(varName, cleanParts.join(QLatin1Char(':')));
        }
    };

    cleanPathList(QStringLiteral("LD_LIBRARY_PATH"), QStringLiteral("APPIMAGE_ORIGINAL_LD_LIBRARY_PATH"));
    cleanPathList(QStringLiteral("QT_PLUGIN_PATH"), QStringLiteral("APPIMAGE_ORIGINAL_QT_PLUGIN_PATH"));
    cleanPathList(QStringLiteral("QML_IMPORT_PATH"), QStringLiteral("APPIMAGE_ORIGINAL_QML_IMPORT_PATH"));
    cleanPathList(QStringLiteral("QML2_IMPORT_PATH"), QStringLiteral("APPIMAGE_ORIGINAL_QML2_IMPORT_PATH"));
    cleanPathList(QStringLiteral("XDG_DATA_DIRS"), QStringLiteral("APPIMAGE_ORIGINAL_XDG_DATA_DIRS"));
    cleanPathList(QStringLiteral("PATH"), QStringLiteral("APPIMAGE_ORIGINAL_PATH"));

    if (env.contains(QStringLiteral("APPIMAGE_ORIGINAL_QT_QPA_PLATFORM"))) {
        QString origPlatform = env.value(QStringLiteral("APPIMAGE_ORIGINAL_QT_QPA_PLATFORM"));
        if (origPlatform.isEmpty()) {
            env.remove(QStringLiteral("QT_QPA_PLATFORM"));
        } else {
            env.insert(QStringLiteral("QT_QPA_PLATFORM"), origPlatform);
        }
        env.remove(QStringLiteral("APPIMAGE_ORIGINAL_QT_QPA_PLATFORM"));
    } else {
        env.remove(QStringLiteral("QT_QPA_PLATFORM"));
    }

    if (env.contains(QStringLiteral("APPIMAGE_ORIGINAL_QT_XCB_GL_INTEGRATION"))) {
        QString origGl = env.value(QStringLiteral("APPIMAGE_ORIGINAL_QT_XCB_GL_INTEGRATION"));
        if (origGl.isEmpty()) {
            env.remove(QStringLiteral("QT_XCB_GL_INTEGRATION"));
        } else {
            env.insert(QStringLiteral("QT_XCB_GL_INTEGRATION"), origGl);
        }
        env.remove(QStringLiteral("APPIMAGE_ORIGINAL_QT_XCB_GL_INTEGRATION"));
    } else {
        env.remove(QStringLiteral("QT_XCB_GL_INTEGRATION"));
    }

    env.remove(QStringLiteral("APPDIR"));

    // AppImage-Identität darf NIE an gestartete Programme vererbt werden:
    // Sonst halten Karyon (und Electron-Apps beim Selbst-Neustart) z.B. Steam für die Karyon-AppImage.
    env.remove(QStringLiteral("APPIMAGE"));
    env.remove(QStringLiteral("ARGV0"));
    env.remove(QStringLiteral("OWD"));
    const QStringList keys = env.keys();
    for (const QString &k : keys) {
        if (k.startsWith(QLatin1String("APPIMAGE_"))) {
            env.remove(k);
            continue;
        }
        const QString v = env.value(k);
        if (v.contains(QLatin1String("/.mount_")) || (!appdir.isEmpty() && v.contains(appdir))) {
            env.remove(k);
        }
    }

    return env;
}

inline bool startDetachedHost(const QString &program, const QStringList &arguments = {})
{
    QProcess p;
    p.setProgram(program);
    p.setArguments(arguments);
    p.setProcessEnvironment(cleanHostEnvironment());
    return p.startDetached();
}

} // namespace ProcessUtils
