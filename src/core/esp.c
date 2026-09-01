/**
 * esp.c - ESP 分区挂载/卸载
 */

#include "../../include/esp.h"
#include <wchar.h>
#include <winioctl.h>
#include <stdio.h>

/* GPT EFI System Partition GUID: c12a7328-f81f-11d2-ba4b-00a0c93ec93b */
static GUID kEspGuid = {0xc12a7328, 0xf81f, 0x11d2,
    {0xba, 0x4b, 0x00, 0xa0, 0xc9, 0x3e, 0xc9, 0x3b}};

/*
 * 纯 API 的 ESP 挂载兜底（PE 下 mountvol /S 经常不可用）：
 * 1) 枚举磁盘 GPT 布局，找到 ESP 所在的 (磁盘, 起始偏移)
 * 2) 枚举所有卷（含无盘符卷），按 磁盘+偏移 匹配出 ESP 卷
 * 3) 用 DefineDosDevice 分配空闲盘符
 */
static BOOL MountEspViaPartition(WCHAR* driveLetter, DWORD size) {
    for (int d = 0; d < 16; d++) {
        WCHAR diskPath[MAX_PATH];
        HANDLE hDisk;
        BYTE layout[16 * 1024];
        DWORD bytes = 0;
        DWORD partCount, i;
        DRIVE_LAYOUT_INFORMATION_EX* dl;

        swprintf(diskPath, MAX_PATH, L"\\\\.\\PhysicalDrive%d", d);
        hDisk = CreateFileW(diskPath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                            NULL, OPEN_EXISTING, 0, NULL);
        if (hDisk == INVALID_HANDLE_VALUE) continue;

        if (!DeviceIoControl(hDisk, IOCTL_DISK_GET_DRIVE_LAYOUT_EX,
                             NULL, 0, layout, sizeof(layout), &bytes, NULL)) {
            CloseHandle(hDisk);
            continue;
        }
        CloseHandle(hDisk);

        dl = (DRIVE_LAYOUT_INFORMATION_EX*)layout;
        if (dl->PartitionStyle != PARTITION_STYLE_GPT) continue;
        partCount = dl->PartitionCount;

        for (i = 0; i < partCount; i++) {
            PARTITION_INFORMATION_EX* pi = &dl->PartitionEntry[i];
            WCHAR volName[MAX_PATH];
            HANDLE hFind;

            if (pi->PartitionStyle != PARTITION_STYLE_GPT) continue;
            if (memcmp(&pi->Gpt.PartitionType, &kEspGuid, sizeof(GUID)) != 0) continue;

            /* 找到 ESP：枚举卷，按 磁盘号+起始偏移 匹配 */
            hFind = FindFirstVolumeW(volName, MAX_PATH);
            if (hFind == INVALID_HANDLE_VALUE) continue;

            do {
                HANDLE hVol = CreateFileW(volName, GENERIC_READ,
                                          FILE_SHARE_READ | FILE_SHARE_WRITE,
                                          NULL, OPEN_EXISTING, 0, NULL);
                VOLUME_DISK_EXTENTS ext = {0};
                DWORD ret = 0;

                if (hVol == INVALID_HANDLE_VALUE) continue;
                if (!DeviceIoControl(hVol, IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS,
                                     NULL, 0, &ext, sizeof(ext), &ret, NULL) ||
                    ext.NumberOfDiskExtents <= 0 ||
                    ext.Extents[0].DiskNumber != (DWORD)d ||
                    ext.Extents[0].StartingOffset.QuadPart != pi->StartingOffset.QuadPart) {
                    CloseHandle(hVol);
                    continue;
                }
                CloseHandle(hVol);
                FindClose(hFind);

                /* 分配空闲盘符：raw DefineDosDevice 指向 \\?\Volume{guid} */
                for (WCHAR c = L'Z'; c >= L'C'; --c) {
                    WCHAR root[4] = {c, L':', L'\\', 0};
                    WCHAR dosDev[3] = {c, L':', 0};
                    WCHAR devName[MAX_PATH];
                    size_t len;

                    if (GetDriveTypeW(root) != DRIVE_NO_ROOT_DIR) continue;
                    lstrcpynW(devName, volName, MAX_PATH);
                    len = wcslen(devName);
                    if (len > 0 && devName[len - 1] == L'\\') devName[len - 1] = 0;

                    if (DefineDosDeviceW(DDD_RAW_TARGET_PATH, dosDev, devName)) {
                        Sleep(300);
                        if (GetFileAttributesW(root) != INVALID_FILE_ATTRIBUTES) {
                            swprintf(driveLetter, size, L"%c:", c);
                            return TRUE;
                        }
                    }
                }
                return FALSE;
            } while (FindNextVolumeW(hFind, volName, MAX_PATH));
            FindClose(hFind);
        }
    }
    return FALSE;
}

// 追踪我们挂载的 ESP 盘符（最多一个）
static WCHAR s_mountedByUs = 0;

static BOOL RunMountvol(const WCHAR* command) {
    STARTUPINFOW si = {0};
    PROCESS_INFORMATION pi = {0};
    WCHAR cmdLine[128];

    swprintf(cmdLine, 128, L"cmd.exe /c mountvol %ls", command);

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

    /* mountvol 失败兜底：纯 API 挂载（PE 下 mountvol /S 常不可用） */
    if (MountEspViaPartition(driveLetter, size)) {
        s_mountedByUs = driveLetter[0];
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