/**
 * esp.c - ESP 分区挂载/卸载
 */

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

    WaitForSingleObject(pi.hProcess, 15000);

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

    // 先检查 ESP 是否已经挂载（FAT32 且有 EFI 目录）
    for (WCHAR d = L'C'; d <= L'Z'; ++d) {
        WCHAR root[4] = {d, L':', L'\\', 0};
        WCHAR fsName[32] = {0};
        
        if (GetDriveTypeW(root) == DRIVE_NO_ROOT_DIR) continue;
        
        if (GetVolumeInformationW(root, NULL, 0, NULL, NULL, NULL, fsName, 32)) {
            if (_wcsicmp(fsName, L"FAT32") == 0 || _wcsicmp(fsName, L"FAT") == 0) {
                WCHAR efiDir[16];
                swprintf(efiDir, 16, L"%c:\\EFI", d);
                if (GetFileAttributesW(efiDir) != INVALID_FILE_ATTRIBUTES) {
                    swprintf(driveLetter, size, L"%c:", d);
                    return TRUE;
                }
            }
        }
    }

    // 没有已挂载的 ESP，用 mountvol /S 挂载
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

    // 尝试挂载
    if (!RunMountvol(command)) {
        return FALSE;
    }

    Sleep(300);

    // 验证挂载是否成功
    WCHAR root[4] = {letter, L':', L'\\', 0};
    if (GetFileAttributesW(root) != INVALID_FILE_ATTRIBUTES) {
        swprintf(driveLetter, size, L"%c:", letter);
        return TRUE;
    }

    return FALSE;
}

BOOL EspUnmount(const WCHAR* driveLetter) {
    WCHAR command[16];

    if (!driveLetter || wcslen(driveLetter) < 2) {
        return FALSE;
    }

    // 只卸载我们自动挂载的（不是原本就有盘符的）
    // 检查是否是通过 mountvol /S 挂载的
    WCHAR root[4] = {driveLetter[0], L':', L'\\', 0};
    WCHAR efiDir[16];
    swprintf(efiDir, 16, L"%s\\EFI", driveLetter);
    
    // 如果 EFI 目录存在，说明是 ESP，可以尝试卸载
    if (GetFileAttributesW(efiDir) != INVALID_FILE_ATTRIBUTES) {
        swprintf(command, 16, L"%c: /D", driveLetter[0]);
        return RunMountvol(command);
    }

    return TRUE;
}

BOOL EspFind(WCHAR* driveLetter, DWORD size) {
    return EspMount(driveLetter, size);
}