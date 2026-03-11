#ifndef REFIND_H
#define REFIND_H

#include <windows.h>
#include <stdio.h>

// rEFInd installation structure
typedef struct _REFIND_INSTALL_INFO {
    WCHAR espDrive[4];
    WCHAR installPath[MAX_PATH];
    BOOL backupExists;
    WCHAR backupPath[MAX_PATH];
} REFIND_INSTALL_INFO;

// Function declarations
BOOL RefindFindESP(WCHAR* driveLetter, DWORD size);
BOOL RefindMountESP(WCHAR* driveLetter, DWORD size);
BOOL RefindUnmountESP(const WCHAR* driveLetter);
BOOL RefindInstall(const WCHAR* sourcePath, const WCHAR* espDrive);
BOOL RefindUninstall(const WCHAR* espDrive);
BOOL RefindIsInstalled(const WCHAR* espDrive);
BOOL RefindAddNVRAMEntry(const WCHAR* description, const WCHAR* path);
BOOL RefindRemoveNVRAMEntry(const WCHAR* description);
BOOL RefindBackupBootx64(const WCHAR* espDrive, WCHAR* backupPath, DWORD size);
BOOL RefindRestoreBootx64(const WCHAR* espDrive, const WCHAR* backupPath);

#endif // REFIND_H
