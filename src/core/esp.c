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
 * Check if a drive is an ESP partition.
 * Accepts FAT12, FAT16, FAT32 file systems.
 * Does NOT require an \EFI directory to exist (fresh ESP may be empty).
 * Does NOT exclude removable media (USB ESP is valid).
 */
static BOOL IsEspDriveEx(WCHAR driveLetter, BOOL excludeRemovable) {
    WCHAR root[4] = {driveLetter, L':', L'\\', 0};
    WCHAR fsName[32] = {0};
    
    UINT driveType = GetDriveTypeW(root);
    
    if (driveType == DRIVE_NO_ROOT_DIR) return FALSE;
    if (driveType == DRIVE_CDROM) return FALSE;  // Skip CD/DVD only
    
    // Exclude removable only if explicitly requested
    if (excludeRemovable && driveType == DRIVE_REMOVABLE) return FALSE;
    
    if (GetVolumeInformationW(root, NULL, 0, NULL, NULL, NULL, fsName, 32)) {
        // Accept FAT12, FAT16, FAT32, and generic "FAT" — all valid for ESP
        if (_wcsicmp(fsName, L"FAT32") == 0 ||
            _wcsicmp(fsName, L"FAT16") == 0 ||
            _wcsicmp(fsName, L"FAT12") == 0 ||
            _wcsicmp(fsName, L"FAT") == 0) {
            return TRUE;
        }
    }
    return FALSE;
}

static BOOL IsEspDrive(WCHAR driveLetter) {
    return IsEspDriveEx(driveLetter, FALSE);  // Allow removable ESP (USB)
}

BOOL EspMountEx(WCHAR* driveLetter, DWORD size, BOOL* mountedByUs) {
    WCHAR letter = 0;

    if (!driveLetter || size < 3) {
        return FALSE;
    }

    // Initialize output
    if (mountedByUs) *mountedByUs = FALSE;

    // First check if ESP is already mounted
    // In WinPE, ESP is often already mounted at a drive letter (e.g., C: or D:)
    for (WCHAR d = L'A'; d <= L'Z'; ++d) {
        if (IsEspDrive(d)) {
            swprintf(driveLetter, size, L"%c:", d);
            // Check if we mounted it before
            if (mountedByUs) {
                *mountedByUs = (s_mountedByUs == d);
            }
            return TRUE;
        }
    }

    // No mounted ESP found. Try mountvol to mount it.
    // In WinPE, mountvol /S may not work; try diskpart or manual GUID mount
    // Find a free drive letter (start from Z going down)
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

    // Try mounting
    if (!RunMountvol(command)) {
        return FALSE;
    }

    Sleep(300);

    // Verify mount succeeded
    WCHAR root[4] = {letter, L':', L'\\', 0};
    if (GetFileAttributesW(root) != INVALID_FILE_ATTRIBUTES) {
        swprintf(driveLetter, size, L"%c:", letter);
        s_mountedByUs = letter;
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