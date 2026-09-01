/**
 * envinfo.c - 运行环境探测实现
 */
#include "../../include/envinfo.h"

BOOL EnvIsWinPE(void) {
    HKEY hKey;
    /* WinPE 特征键：HKLM\SYSTEM\CurrentControlSet\Control\MiniNT */
    LONG r = RegOpenKeyExW(HKEY_LOCAL_MACHINE,
        L"SYSTEM\\CurrentControlSet\\Control\\MiniNT", 0, KEY_READ, &hKey);
    if (r == ERROR_SUCCESS) {
        RegCloseKey(hKey);
        return TRUE;
    }
    return FALSE;
}

BOOL EnvHasBcdEdit(void) {
    WCHAR exeDir[MAX_PATH];
    WCHAR* slash;
    WCHAR path[MAX_PATH];

    /* 程序目录（dist 里自带 bcdedit.exe） */
    if (GetModuleFileNameW(NULL, exeDir, MAX_PATH) > 0) {
        slash = wcsrchr(exeDir, L'\\');
        if (slash) {
            lstrcpynW(slash + 1, L"bcdedit.exe", MAX_PATH - (int)(slash - exeDir) - 1);
            if (GetFileAttributesW(exeDir) != INVALID_FILE_ATTRIBUTES) return TRUE;
        }
    }
    /* PATH */
    if (SearchPathW(NULL, L"bcdedit.exe", NULL, MAX_PATH, path, NULL) > 0) return TRUE;
    return FALSE;
}
