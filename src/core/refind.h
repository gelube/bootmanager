#ifndef REFIND_H
#define REFIND_H

#include <windows.h>

BOOL RefindMountESP(WCHAR* driveLetter, DWORD size);
BOOL RefindUnmountESP(const WCHAR* driveLetter);
BOOL RefindInstall(const WCHAR* sourcePath, const WCHAR* espDrive);
BOOL RefindUninstall(const WCHAR* espDrive);
BOOL RefindIsInstalled(const WCHAR* espDrive);
const WCHAR* RefindGetLastErrorMessage(void);

#endif // REFIND_H