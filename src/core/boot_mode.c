/**
 * boot_mode.c - 启动模式检测实现
 * 
 * WinPE compatible: no Shell API, no bcdedit dependency
 */

#include "../../include/boot_mode.h"
#include <wchar.h>
#include <winioctl.h>
#include <stdio.h>

// ============================================
// WinPE Detection
// ============================================

bool BootMode_IsWinPE(void) {
    // WinPE sets HKLM\SYSTEM\CurrentControlSet\Control\MiniNT
    HKEY hKey = NULL;
    LONG result = RegOpenKeyExW(HKEY_LOCAL_MACHINE,
        L"SYSTEM\\CurrentControlSet\\Control\\MiniNT", 0, KEY_READ, &hKey);
    if (result == ERROR_SUCCESS) {
        RegCloseKey(hKey);
        return true;
    }
    return false;
}

// ============================================
// Helper: Enable SE_SYSTEM_ENVIRONMENT_NAME privilege
// Required for GetFirmwareEnvironmentVariable in WinPE
// ============================================

static bool EnableSystemEnvironmentPrivilege(void) {
    HANDLE hToken = NULL;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken)) {
        return false;
    }
    
    TOKEN_PRIVILEGES tp;
    tp.PrivilegeCount = 1;
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    
    BOOL ok = LookupPrivilegeValueW(NULL, SE_SYSTEM_ENVIRONMENT_NAME, &tp.Privileges[0].Luid);
    if (ok) {
        ok = AdjustTokenPrivileges(hToken, FALSE, &tp, 0, NULL, NULL);
    }
    
    CloseHandle(hToken);
    return ok && (GetLastError() == ERROR_SUCCESS);
}

// ============================================
// BIOS 固件检测
// ============================================

bool BootMode_IsUEFIFirmware(void) {
    // Method 0: Enable privilege first (critical for WinPE)
    EnableSystemEnvironmentPrivilege();
    
    // Method 1: Try opening Firmware device
    HANDLE hFirmware = CreateFileW(L"\\\\.\\Firmware", 
        GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (hFirmware != INVALID_HANDLE_VALUE) {
        CloseHandle(hFirmware);
        return true;
    }
    
    // Method 2: Check registry
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
        L"SYSTEM\\CurrentControlSet\\Control", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        
        DWORD value = 0, size = sizeof(DWORD);
        if (RegQueryValueExW(hKey, L"UEFI", NULL, NULL, (LPBYTE)&value, &size) == ERROR_SUCCESS) {
            RegCloseKey(hKey);
            return value != 0;
        }
        RegCloseKey(hKey);
    }
    
    // Method 3: Check if UEFI variables exist
    // After privilege enablement, this should work in WinPE too
    BYTE buffer[4];
    if (GetFirmwareEnvironmentVariableW(L"BootOrder",
        L"{8be4df61-93ca-11d2-aa0d-00e098032b8c}", buffer, 0) != 0 ||
        GetLastError() != ERROR_INVALID_FUNCTION) {
        return true;
    }
    
    // Method 4 (WinPE fallback): Check for EFI partition
    // In WinPE on UEFI, there's usually an EFI\System partition visible
    WCHAR drives[256];
    DWORD len = GetLogicalDriveStringsW(256, drives);
    if (len > 0) {
        WCHAR* d = drives;
        while (*d) {
            WCHAR root[4] = {d[0], L':', L'\\', 0};
            WCHAR fsName[32] = {0};
            if (GetVolumeInformationW(root, NULL, 0, NULL, NULL, NULL, fsName, 32)) {
                if (_wcsicmp(fsName, L"FAT32") == 0 || _wcsicmp(fsName, L"FAT16") == 0) {
                    WCHAR efiPath[MAX_PATH];
                    swprintf(efiPath, MAX_PATH, L"%c:\\EFI", d[0]);
                    if (GetFileAttributesW(efiPath) != INVALID_FILE_ATTRIBUTES) {
                        return true;  // EFI directory on FAT = UEFI system
                    }
                }
            }
            d += wcslen(d) + 1;
        }
    }
    
    return false;
}

// ============================================
// 磁盘分区表检测
// ============================================

bool BootMode_IsGPTDisk(int diskNumber) {
    WCHAR path[MAX_PATH];
    swprintf(path, MAX_PATH, L"\\\\.\\PhysicalDrive%d", diskNumber);
    
    HANDLE hDisk = CreateFileW(path, GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    
    if (hDisk == INVALID_HANDLE_VALUE) {
        return false;
    }
    
    // 读取 MBR
    BYTE mbr[512];
    DWORD bytesRead = 0;
    BOOL ok = ReadFile(hDisk, mbr, 512, &bytesRead, NULL);
    CloseHandle(hDisk);
    
    if (!ok || bytesRead != 512) {
        return false;
    }
    
    // 检查 MBR 签名
    if (mbr[0x1FE] != 0x55 || mbr[0x1FF] != 0xAA) {
        return true;  // 无效 MBR，可能是 GPT
    }
    
    // 检查保护性 MBR（GPT 磁盘的第一个分区类型是 0xEE）
    if (mbr[0x1BE + 4] == 0xEE) {
        return true;  // GPT 磁盘
    }
    
    return false;  // MBR 磁盘
}

// ============================================
// 查找系统盘
// ============================================

int BootMode_FindSystemDisk(void) {
    // 方法1: 从系统盘符获取
    WCHAR sysDrive[MAX_PATH] = {0};
    GetEnvironmentVariableW(L"SystemDrive", sysDrive, MAX_PATH);
    
    if (sysDrive[0] >= L'C' && sysDrive[0] <= L'Z') {
        // 打开卷获取磁盘号
        WCHAR volPath[] = L"\\\\.\\C:";
        volPath[4] = sysDrive[0];
        
        HANDLE hVol = CreateFileW(volPath, GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
        
        if (hVol != INVALID_HANDLE_VALUE) {
            VOLUME_DISK_EXTENTS extents;
            DWORD bytesReturned = 0;
            
            if (DeviceIoControl(hVol, IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS,
                NULL, 0, &extents, sizeof(extents), &bytesReturned, NULL)) {
                CloseHandle(hVol);
                return extents.Extents[0].DiskNumber;
            }
            CloseHandle(hVol);
        }
    }
    
    // 方法2: 遍历磁盘找系统分区
    for (int disk = 0; disk < 16; disk++) {
        if (BootMode_IsGPTDisk(disk)) {
            // GPT 磁盘，检查是否有 ESP
            // 简化处理，返回第一个 GPT 磁盘
            return disk;
        }
    }
    
    // 返回磁盘 0 作为默认
    return 0;
}

// ============================================
// 查找活动分区
// ============================================

int BootMode_FindActivePartition(int diskNumber) {
    WCHAR path[MAX_PATH];
    swprintf(path, MAX_PATH, L"\\\\.\\PhysicalDrive%d", diskNumber);
    
    HANDLE hDisk = CreateFileW(path, GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    
    if (hDisk == INVALID_HANDLE_VALUE) {
        return -1;
    }
    
    BYTE mbr[512];
    DWORD bytesRead = 0;
    if (!ReadFile(hDisk, mbr, 512, &bytesRead, NULL) || bytesRead != 512) {
        CloseHandle(hDisk);
        return -1;
    }
    CloseHandle(hDisk);
    
    // 遍历分区表
    for (int i = 0; i < 4; i++) {
        BYTE* entry = &mbr[0x1BE + i * 16];
        if ((entry[0] & 0x80) != 0) {  // 活动分区标志
            return i + 1;
        }
    }
    
    return -1;  // 未找到
}

// ============================================
// 查找 ESP 分区
// ============================================

bool BootMode_FindESP(WCHAR* driveLetter) {
    if (!driveLetter) return false;
    
    // 遍历所有驱动器
    WCHAR drives[256];
    GetLogicalDriveStringsW(256, drives);
    
    WCHAR* drive = drives;
    while (*drive) {
        // 检查是否是固定磁盘
        if (GetDriveTypeW(drive) == DRIVE_FIXED) {
            WCHAR bootPath[MAX_PATH];
            swprintf(bootPath, MAX_PATH, L"%sEFI\\Microsoft\\Boot\\bootmgfw.efi", drive);
            
            if (GetFileAttributesW(bootPath) != INVALID_FILE_ATTRIBUTES) {
                *driveLetter = drive[0];
                return true;
            }
        }
        
        drive += wcslen(drive) + 1;
    }
    
    return false;
}

// ============================================
// 综合检测
// ============================================

bool BootMode_Detect(BOOT_INFO* info) {
    if (!info) return false;
    
    memset(info, 0, sizeof(BOOT_INFO));
    
    // 1. 检测 BIOS 固件
    info->isUEFIFirmware = BootMode_IsUEFIFirmware();
    
    // 2. 查找系统盘
    info->systemDisk = BootMode_FindSystemDisk();
    
    // 3. 检测分区表类型
    info->isGPTDisk = BootMode_IsGPTDisk(info->systemDisk);
    
    // 4. 检测活动分区
    if (!info->isGPTDisk) {
        info->activePartition = BootMode_FindActivePartition(info->systemDisk);
        info->hasActiveMBR = (info->activePartition > 0);
    }
    
    // 5. 检测 ESP
    WCHAR espLetter = 0;
    info->hasESP = BootMode_FindESP(&espLetter);
    if (info->hasESP && espLetter) {
        swprintf(info->espPath, MAX_PATH, L"%c:", espLetter);
    }
    
    // 6. 综合判断启动模式
    if (info->isUEFIFirmware && info->isGPTDisk && info->hasESP) {
        // 标准 UEFI 模式
        info->bootMode = BOOT_MODE_UEFI;
    }
    else if (info->isUEFIFirmware && !info->isGPTDisk) {
        // UEFI + MBR 混合模式
        info->bootMode = BOOT_MODE_HYBRID;
    }
    else if (!info->isUEFIFirmware && !info->isGPTDisk) {
        // 标准 MBR 模式
        info->bootMode = BOOT_MODE_MBR;
    }
    else if (!info->isUEFIFirmware && info->isGPTDisk) {
        // 异常情况：Legacy BIOS + GPT（可能是转档）
        // 按 MBR 模式处理
        info->bootMode = BOOT_MODE_MBR;
    }
    else {
        info->bootMode = BOOT_MODE_UNKNOWN;
    }
    
    return true;
}

const WCHAR* BootMode_GetName(BOOT_MODE mode) {
    switch (mode) {
        case BOOT_MODE_UEFI:   return L"UEFI";
        case BOOT_MODE_MBR:    return L"MBR";
        case BOOT_MODE_HYBRID: return L"UEFI+MBR";
        default: return L"未知";
    }
}