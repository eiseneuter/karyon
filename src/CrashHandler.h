#pragma once

#include <QString>

class CrashHandler {
public:
    static void install();
    static void markCleanShutdownRequested();
    static bool isCleanShutdownRequested();
    static void logStartup(int argc, char *argv[]);
    static void logShutdown(const char *reason);
    static void setLastFatalMessage(const QString &msg);
    static int runCrashReporterDialog(const QString &crashReportPath);
    static QString crashesDirectory();
};
