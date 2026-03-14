#ifndef BOOTMGR_LOGGER_H
#define BOOTMGR_LOGGER_H

#include <windows.h>

typedef enum {
    LOG_LEVEL_DEBUG = 0,
    LOG_LEVEL_INFO = 1,
    LOG_LEVEL_WARNING = 2,
    LOG_LEVEL_ERROR = 3
} LOG_LEVEL;

void LoggerInit(void);
void LoggerLog(LOG_LEVEL level, const WCHAR* format, ...);

#define LOG_DEBUG(fmt, ...) LoggerLog(LOG_LEVEL_DEBUG, fmt, ##__VA_ARGS__)
#define LOG_INFO(fmt, ...) LoggerLog(LOG_LEVEL_INFO, fmt, ##__VA_ARGS__)
#define LOG_WARNING(fmt, ...) LoggerLog(LOG_LEVEL_WARNING, fmt, ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) LoggerLog(LOG_LEVEL_ERROR, fmt, ##__VA_ARGS__)

#endif
