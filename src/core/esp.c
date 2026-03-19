/**
 * esp.c - ESP 分区挂载/卸载
 */

#include "../../include/esp.h"
#include <wchar.h>

// 追踪我们挂载的 ESP 盘符（最多一个）
static WCHAR s_mountedByUs = 0;

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

/**
 * 检查驱动器是否是 ESP（FAT32 + EFI 目录）
 * @param driveLetter 盘符
 * @param excludeRemovable 是否排除可移动介质（U盘）
 */
static BOOL IsEspDriveEx(WCHAR driveLetter, BOOL excludeRemovable) {
    WCHAR root[4] = {driveLetter, L':', L'\\', 0};
    WCHAR fsName[32] = {0};
    
    UINT driveType = GetDriveTypeW(root);
    
    if (driveType == DRIVE_NO_ROOT_DIR) return FALSE;
    
    // 排除可移动介质（U盘、光驱等）
    if (excludeRemovable && driveType == DRIVE_REMOVABLE) return FALSE;
    
    // 只接受固定磁盘或 RAM 磁盘
    if (driveType != DRIVE_FIXED && driveType != DRIVE_RAMDISK) return FALSE;
    
    if (GetVolumeInformationW(root, NULL, 0, NULL, NULL, NULL, fsName, 32)) {
        if (_wcsicmp(fsName, L"FAT32") == 0 || _wcsicmp(fsName, L"FAT") == 0) {
            WCHAR efiDir[16];
            swprintf(efiDir, 16, L"%c:\\EFI", driveLetter);
            if (GetFileAttributesW(efiDir) != INVALID_FILE_ATTRIBUTES) {
                return TRUE;
            }
        }
    }
    return FALSE;
}

static BOOL IsEspDrive(WCHAR driveLetter) {
    return IsEspDriveEx(driveLetter, TRUE);  // 默认排除可移动介质
}

BOOL EspMountEx(WCHAR* driveLetter, DWORD size, BOOL* mountedByUs) {
    WCHAR letter = 0;

    if (!driveLetter || size < 3) {
        return FALSE;
    }

    // 初始化输出参数
    if (mountedByUs) *mountedByUs = FALSE;

    // 先检查 ESP 是否已经挂载
    for (WCHAR d = L'C'; d <= L'Z'; ++d) {
        if (IsEspDrive(d)) {
            swprintf(driveLetter, size, L"%c:", d);
            // 检查是否是我们之前挂载的
            if (mountedByUs) {
                *mountedByUs = (s_mountedByUs == d);
            }
            return TRUE;
        }
    }

    // 没有已挂载的 ESP，找一个空闲盘符
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
        s_mountedByUs = letter;  // 记录我们挂载的盘符
        if (mountedByUs) *mountedByUs = TRUE;
        return TRUE;
    }

    return FALSE;
}

BOOL EspMount(WCHAR* driveLetter, DWORD size) {
    return EspMountEx(driveLetter, size, NULL);
}

BOOL EspUnmountEx(const WCHAR* driveLetter, BOOL onlyIfMountedByUs) {
    WCHAR cmdLine[256];
    STARTUPINFOW si = {0};
    PROCESS_INFORMATION pi = {0};
    DWORD exitCode;

    if (!driveLetter || wcslen(driveLetter) < 2) {
        return FALSE;
    }

    WCHAR letter = driveLetter[0];
    WCHAR root[4] = {letter, L':', L'\\', 0};
    
    // 检查盘符是否还存在
    if (GetFileAttributesW(root) == INVALID_FILE_ATTRIBUTES) {
        s_mountedByUs = 0;
        return TRUE;  // 已经不存在了
    }
    
    // 如果要求仅卸载我们挂载的
    if (onlyIfMountedByUs && s_mountedByUs != letter) {
        return TRUE;  // 不是我们挂载的，跳过
    }
    
    // 方法1: mountvol /D 删除挂载点
    swprintf(cmdLine, 256, L"mountvol %c: /D", letter);
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    
    if (CreateProcessW(NULL, cmdLine, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, 5000);
        GetExitCodeProcess(pi.hProcess, &exitCode);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
    
    Sleep(200);
    if (GetFileAttributesW(root) == INVALID_FILE_ATTRIBUTES) {
        s_mountedByUs = 0;
        return TRUE;
    }
    
    // 方法2: mountvol /P 完全移除
    swprintf(cmdLine, 256, L"mountvol %c: /P", letter);
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    
    if (CreateProcessW(NULL, cmdLine, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, 5000);
        GetExitCodeProcess(pi.hProcess, &exitCode);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
    
    Sleep(200);
    if (GetFileAttributesW(root) == INVALID_FILE_ATTRIBUTES) {
        s_mountedByUs = 0;
        return TRUE;
    }
    
    // 方法3: 再次尝试 /D
    swprintf(cmdLine, 256, L"mountvol %c: /D", letter);
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    
    if (CreateProcessW(NULL, cmdLine, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, 5000);
        GetExitCodeProcess(pi.hProcess, &exitCode);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
    
    Sleep(200);
    BOOL success = (GetFileAttributesW(root) == INVALID_FILE_ATTRIBUTES);
    if (success) {
        s_mountedByUs = 0;
    }
    return success;
}

BOOL EspUnmount(const WCHAR* driveLetter) {
    return EspUnmountEx(driveLetter, FALSE);
}

BOOL EspFind(WCHAR* driveLetter, DWORD size) {
    return EspMountEx(driveLetter, size, NULL);
}