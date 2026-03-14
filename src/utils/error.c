#include "../../include/error.h"

#include <wchar.h>

void BootMgrSetError(BOOTMGR_ERROR* error, BOOTMGR_ERROR_CODE code, const WCHAR* message, DWORD win32LastError) {
    if (!error) {
        return;
    }

    error->code = code;
    error->win32LastError = win32LastError;

    if (message) {
        wcsncpy(error->message, message, 255);
        error->message[255] = L'\0';
    } else {
        error->message[0] = L'\0';
    }
}

const WCHAR* BootMgrGetErrorMessage(const BOOTMGR_ERROR* error) {
    if (!error) {
        return L"Unknown error";
    }

    if (error->message[0] != L'\0') {
        return error->message;
    }

    switch (error->code) {
        case BOOTMGR_SUCCESS: return L"Success";
        case BOOTMGR_ERROR_NOT_ADMIN: return L"Administrator privileges are required";
        case BOOTMGR_ERROR_ESP_MOUNT_FAILED: return L"Failed to mount ESP partition";
        case BOOTMGR_ERROR_FILE_NOT_FOUND: return L"File not found";
        case BOOTMGR_ERROR_COPY_FAILED: return L"File copy failed";
        case BOOTMGR_ERROR_BCD_EDIT_FAILED: return L"bcdedit command failed";
        case BOOTMGR_ERROR_NVRAM_FAILED: return L"NVRAM operation failed";
        default: return L"Unknown error";
    }
}

void BootMgrShowError(HWND hWnd, const BOOTMGR_ERROR* error) {
    WCHAR fullMessage[512];
    const WCHAR* msg = BootMgrGetErrorMessage(error);

    if (error && error->win32LastError != 0) {
        swprintf(fullMessage, 512, L"%s\n\nWin32 Error: %lu", msg, error->win32LastError);
    } else {
        swprintf(fullMessage, 512, L"%s", msg);
    }

    MessageBoxW(hWnd, fullMessage, L"Boot Manager Error", MB_OK | MB_ICONERROR);
}
