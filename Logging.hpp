#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <TimeProv.h>

enum LogTimeProvEventType : WORD {
    LogTimeProvEventTypeError = 1,
    LogTimeProvEventTypeWarning = 2,
    LogTimeProvEventTypeInformation = 3,
};

void TimeProvVLog(LogTimeProvEventFunc *logger, LogTimeProvEventType level, PCWSTR format, va_list args);
void TimeProvLog(LogTimeProvEventFunc *logger, LogTimeProvEventType level, PCWSTR format, ...);

void VDebugLog(PCSTR format, va_list args);
void DebugLog(PCSTR format, ...);
