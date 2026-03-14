#ifndef BOOTMGR_ESP_H
#define BOOTMGR_ESP_H

#include <windows.h>

BOOL EspMount(WCHAR* driveLetter, DWORD size);
BOOL EspUnmount(const WCHAR* driveLetter);
BOOL EspFind(WCHAR* driveLetter, DWORD size);

#endif
