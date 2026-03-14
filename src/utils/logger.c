#include "../../include/logger.h"

#include <direct.h>
#include <stdarg.h>
#include <stdio.h>
#include <wchar.h>

static CRITICAL_SECTION g_loggerLock;
static BOOL g_loggerInitialized = FALSE;
static WCHAR g_logFilePath[MAX_PATH];

static const WCHAR* LoggerLevelName(LOG_LEVEL level) {
    switch (level) {
        case LOG_LEVEL_DEBUG: return L"DEBUG";
        case LOG_LEVEL_INFO: return L"INFO";
        case LOG_LEVEL_WARNING: return L"WARN";
        case LOG_LEVEL_ERROR: return L"ERROR";
        default: return L"UNKNOWN";
    }
}

void LoggerInit(void) {
    if (g_loggerInitialized) {
        return;
    }

    InitializeCriticalSection(&g_loggerLock);

    WCHAR tempPath[MAX_PATH];
    GetTempPathW(MAX_PATH, tempPath);

    WCHAR logDir[MAX_PATH];
    swprintf(logDir, MAX_PATH, L"%sBootManager", tempPath);
    _wmkdir(logDir);

    swprintf(g_logFilePath, MAX_PATH, L"%s\\log.txt", logDir);
    g_loggerInitialized = TRUE;
}

void LoggerLog(LOG_LEVEL level, const WCHAR* format, ...) {
    if (!g_loggerInitialized) {
        LoggerInit();
    }

    EnterCriticalSection(&g_loggerLock);

    FILE* fp = _wfopen(g_logFilePath, L"a+, ccs=UTF-8");
    if (fp) {
        SYSTEMTIME st;
        WCHAR message[1024];
        va_list args;

        GetLocalTime(&st);

        va_start(args, format);
        _vsnwprintf(message, 1024, format, args);
        va_end(args);
        message[1023] = L'\0';

        fwprintf(fp, L"[%04d-%02d-%02d %02d:%02d:%02d] [%s] %s\n",
            st.wYear, st.wMonth, st.wDay,
            st.wHour, st.wMinute, st.wSecond,
            LoggerLevelName(level), message);
        fclose(fp);
    }

    LeaveCriticalSection(&g_loggerLock);
}
