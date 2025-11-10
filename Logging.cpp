#include <cstdio>
#include <cstdarg>

#include "Globals.hpp"
#include "Logging.hpp"

void TimeProvVLog(LogTimeProvEventFunc *logger, LogTimeProvEventType level, PCWSTR format, va_list args) {
    WCHAR buf[512];

    vswprintf_s(buf, format, args);
    logger(level, const_cast<PWSTR>(XenTimeProviderName), buf);
}

void TimeProvLog(LogTimeProvEventFunc *logger, LogTimeProvEventType level, PCWSTR format, ...) {
    va_list args;

    va_start(args, format);
    TimeProvVLog(logger, level, format, args);
    va_end(args);
}

void VDebugLog(PCSTR format, va_list args) {
    CHAR buf[512];

    vsprintf_s(buf, format, args);
    OutputDebugStringA(buf);
}

void DebugLog(PCSTR format, ...) {
    va_list args;

    va_start(args, format);
    VDebugLog(format, args);
    va_end(args);
}
