#pragma once

#include <QString>
#include <string>

namespace DebugLogger {

void init();
void setFileLoggingEnabled(bool enabled);
bool isFileLoggingEnabled();
void log(const char *category, const QString &message);
void log(const char *category, const char *message);
void flush();
void installCrashHandler();
QString logFilePath();

} // namespace DebugLogger

#define KLOG(category, ...) DebugLogger::log(category, QString::asprintf(__VA_ARGS__))
#define KLOG_STR(category, str) DebugLogger::log(category, str)
