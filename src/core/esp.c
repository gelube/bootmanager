#include "../../include/esp.h"

#include <wchar.h>

static BOOL RunMountvol(const WCHAR* command) {
    STARTUPINFOW si = {0};
    PROCESS_INFORMATION pi = {0};
    WCHAR cmdLine[128];

    swprintf(cmdLine, 128, L"cmd.exe /c mountvol %s", command);

    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    if (!CreateProcessW(NULL, cmdLine, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        return FALSE;
    }

    WaitForSingleObject(pi.hProcess, 10000);

    DWORD exitCode = 1;
    GetExitCodeProcess(pi.hProcess, &exitCode);

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return exitCode == 0;
}

BOOL EspMount(WCHAR* driveLetter, DWORD size) {
    WCHAR letter = 0;

    if (!driveLetter || size < 3) {
        return FALSE;
    }

    for (WCHAR d = L'Z'; d >= L'C'; --d) {
        WCHAR root[4] = {d, L':', L'\\', 0};
        if (GetDriveTypeW(root) == DRIVE_NO_ROOT_DIR) {
            letter = d;
            break;
        }
    }

    if (letter == 0) {
        return FALSE;
    }

    WCHAR command[16];
    swprintf(command, 16, L"%c: /S", letter);
    if (!RunMountvol(command)) {
        return FALSE;
    }

    Sleep(300);

    WCHAR efiPath[MAX_PATH];
    swprintf(efiPath, MAX_PATH, L"%c:\\EFI", letter);
    if (GetFileAttributesW(efiPath) == INVALID_FILE_ATTRIBUTES) {
        return FALSE;
    }

    swprintf(driveLetter, size, L"%c:", letter);
    return TRUE;
}

BOOL EspUnmount(const WCHAR* driveLetter) {
    WCHAR command[16];

    if (!driveLetter || wcslen(driveLetter) < 2) {
        return FALSE;
    }

    swprintf(command, 16, L"%c: /D", driveLetter[0]);
    return RunMountvol(command);
}

BOOL EspFind(WCHAR* driveLetter, DWORD size) {
    return EspMount(driveLetter, size);
}
