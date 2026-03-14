#ifndef BOOTMGR_ERROR_H
#define BOOTMGR_ERROR_H

#include <windows.h>

typedef enum {
    BOOTMGR_SUCCESS = 0,
    BOOTMGR_ERROR_NOT_ADMIN = 1,
    BOOTMGR_ERROR_ESP_MOUNT_FAILED = 2,
    BOOTMGR_ERROR_FILE_NOT_FOUND = 3,
    BOOTMGR_ERROR_COPY_FAILED = 4,
    BOOTMGR_ERROR_BCD_EDIT_FAILED = 5,
    BOOTMGR_ERROR_NVRAM_FAILED = 6,
    BOOTMGR_ERROR_UNKNOWN = 99
} BOOTMGR_ERROR_CODE;

typedef struct {
    BOOTMGR_ERROR_CODE code;
    WCHAR message[256];
    DWORD win32LastError;
} BOOTMGR_ERROR;

void BootMgrSetError(BOOTMGR_ERROR* error, BOOTMGR_ERROR_CODE code, const WCHAR* message, DWORD win32LastError);
const WCHAR* BootMgrGetErrorMessage(const BOOTMGR_ERROR* error);
void BootMgrShowError(HWND hWnd, const BOOTMGR_ERROR* error);

#endif
