#pragma once

#include <QString>

class CrashHandler {
public:
    static void install();
    static void setLastFatalMessage(const QString &msg);
    static int runCrashReporterDialog(const QString &crashReportPath);
    static QString crashesDirectory();
};
