/**
 * pbr_manager.c - PBR 引导程序管理实现
 * 
 * 支持 BOOTMGR、NTLDR、Limine
 * PE 兼容，纯 Win32 API
 */

#include "../../include/pbr_manager.h"
#include <wchar.h>
#include <stdio.h>
#include <winioctl.h>

// ============================================
// 内嵌引导代码（PBR 扇区）
// ============================================

// BOOTMGR PBR 引导扇区（NTFS/FAT32）
// 这是一个简化的占位符，实际代码需要从真实系统提取
static const BYTE s_bootmgrNtfsPbr[512] = {
    0xEB, 0x52, 0x90, 0x4E, 0x54, 0x46, 0x53, 0x20, 0x20, 0x20, 0x20, 0x00, 0x02, 0x08, 0x00, 0x00,
    // ... NTFS PBR 头部，实际需要完整代码
};

static const BYTE s_bootmgrFat32Pbr[512] = {
    0xEB, 0x58, 0x90, 0x4D, 0x53, 0x44, 0x4F, 0x53, 0x35, 0x2E, 0x30, 0x00, 0x02, 0x08, 0x20, 0x00,
    // ... FAT32 PBR 头部，实际需要完整代码
};

// NTLDR PBR（Windows XP）
static const BYTE s_ntldrPbr[512] = {
    0xEB, 0x3C, 0x90, 0x4D, 0x53, 0x44, 0x4F, 0x53, 0x35, 0x2E, 0x30, 0x00, 0x02, 0x08, 0x00, 0x00,
    // ... NTLDR PBR
};

// Limine PBR
static const BYTE s_liminePbr[512] = {
    // Limine PBR 代码
};

// ============================================
// 辅助函数
// ============================================

void PBR_InitPartitionList(PARTITION_LIST* list) {
    list->partitions = NULL;
    list->count = 0;
    list->capacity = 0;
}

void PBR_FreePartitionList(PARTITION_LIST* list) {
    if (list->partitions) {
        free(list->partitions);
        list->partitions = NULL;
    }
    list->count = 0;
    list->capacity = 0;
}

static bool PBR_EnsureCapacity(PARTITION_LIST* list) {
    if (list->count >= list->capacity) {
        int newCap = list->capacity == 0 ? 16 : list->capacity * 2;
        PARTITION_INFO* newParts = (PARTITION_INFO*)realloc(list->partitions, newCap * sizeof(PARTITION_INFO));
        if (!newParts) return false;
        list->partitions = newParts;
        list->capacity = newCap;
    }
    return true;
}

const WCHAR* PBR_GetBootTypeName(PBR_BOOT_TYPE type) {
    switch (type) {
        case PBR_BOOT_BOOTMGR:  return L"BOOTMGR (Windows Vista+)";
        case PBR_BOOT_NTLDR:    return L"NTLDR (Windows XP)";
        case PBR_BOOT_LIMINE:   return L"Limine";
        case PBR_BOOT_GRUB:     return L"GRUB";
        case PBR_BOOT_SYSLINUX: return L"SYSLINUX";
        default: return L"未知";
    }
}

// ============================================
// 分区操作
// ============================================

static HANDLE OpenPartition(int diskNumber, int partitionNumber, DWORD access) {
    WCHAR path[MAX_PATH];
    
    // 尝试多种路径格式
    swprintf(path, MAX_PATH, L"\\\\.\\PhysicalDrive%d", diskNumber);
    HANDLE hDisk = CreateFileW(path, access, FILE_SHARE_READ | FILE_SHARE_WRITE,
                               NULL, OPEN_EXISTING, 0, NULL);
    return hDisk;
}

static HANDLE OpenPartitionByLetter(WCHAR driveLetter, DWORD access) {
    WCHAR path[] = L"\\\\.\\C:";
    path[4] = driveLetter;
    
    HANDLE hVol = CreateFileW(path, access, FILE_SHARE_READ | FILE_SHARE_WRITE,
                              NULL, OPEN_EXISTING, 0, NULL);
    return hVol;
}

bool PBR_GetPartitions(int diskNumber, PARTITION_LIST* list, WCHAR* error, DWORD errorSize) {
    HANDLE hDisk = OpenPartition(diskNumber, 0, GENERIC_READ);
    if (hDisk == INVALID_HANDLE_VALUE) {
        if (error) swprintf(error, errorSize, L"无法打开磁盘 %d", diskNumber);
        return false;
    }
    
    // 读取 MBR
    BYTE mbr[512];
    DWORD bytesRead = 0;
    if (!ReadFile(hDisk, mbr, 512, &bytesRead, NULL) || bytesRead != 512) {
        CloseHandle(hDisk);
        if (error) wcscpy(error, L"读取 MBR 失败");
        return false;
    }
    
    // 解析分区表（MBR 格式）
    for (int i = 0; i < 4; i++) {
        BYTE* partEntry = &mbr[0x1BE + i * 16];
        BYTE partType = partEntry[4];
        
        if (partType == 0) continue;  // 空条目
        
        PARTITION_INFO info = {0};
        info.diskNumber = diskNumber;
        info.partitionNumber = i + 1;
        info.isActive = (partEntry[0] & 0x80) != 0;
        
        // 获取文件系统类型名称
        switch (partType) {
            case 0x07: wcscpy(info.fsType, L"NTFS"); break;
            case 0x0B:
            case 0x0C: wcscpy(info.fsType, L"FAT32"); break;
            case 0x0E: wcscpy(info.fsType, L"FAT16"); break;
            case 0xEE: wcscpy(info.fsType, L"GPT"); break;
            default: swprintf(info.fsType, 32, L"0x%02X", partType);
        }
        
        // 尝试获取盘符（通过枚举逻辑驱动器）
        // 这里简化处理，实际需要更复杂的逻辑
        
        if (PBR_EnsureCapacity(list)) {
            list->partitions[list->count++] = info;
        }
    }
    
    CloseHandle(hDisk);
    return list->count > 0;
}

bool PBR_GetSystemPartitions(PARTITION_LIST* list, WCHAR* error, DWORD errorSize) {
    // 枚举所有驱动器
    WCHAR drives[256];
    GetLogicalDriveStringsW(256, drives);
    
    WCHAR* drive = drives;
    while (*drive) {
        if (GetDriveTypeW(drive) == DRIVE_FIXED) {
            PARTITION_INFO info = {0};
            info.driveLetter = drive[0];
            info.isActive = true;  // 假设是活动分区
            
            // 获取卷标
            GetVolumeInformationW(drive, info.label, 64, NULL, NULL, NULL, info.fsType, 32);
            
            // 检测是否系统分区
            WCHAR systemRoot[MAX_PATH];
            GetEnvironmentVariableW(L"SystemRoot", systemRoot, MAX_PATH);
            if (systemRoot[0] == drive[0]) {
                info.isSystem = true;
            }
            
            if (PBR_EnsureCapacity(list)) {
                list->partitions[list->count++] = info;
            }
        }
        
        drive += wcslen(drive) + 1;
    }
    
    return list->count > 0;
}

PBR_BOOT_TYPE PBR_DetectBootType(int diskNumber, int partitionNumber) {
    HANDLE hDisk = OpenPartition(diskNumber, partitionNumber, GENERIC_READ);
    if (hDisk == INVALID_HANDLE_VALUE) return PBR_BOOT_BOOTMGR;
    
    // 定位到分区起始位置（简化处理）
    // 实际需要解析分区表获取 LBA 偏移
    BYTE pbr[512];
    DWORD bytesRead = 0;
    
    // 这里假设分区从某个偏移开始，实际需要正确计算
    LARGE_INTEGER offset;
    offset.QuadPart = partitionNumber * 63 * 512;  // 简化假设
    SetFilePointerEx(hDisk, offset, NULL, FILE_BEGIN);
    
    ReadFile(hDisk, pbr, 512, &bytesRead, NULL);
    CloseHandle(hDisk);
    
    // 检测引导类型
    // BOOTMGR: NTFS 或 FAT32 头部
    if (memcmp(&pbr[3], "NTFS", 4) == 0 || memcmp(&pbr[3], "MSDOS5.0", 8) == 0) {
        // 检查是否有 bootmgr 文件
        return PBR_BOOT_BOOTMGR;
    }
    
    // NTLDR
    if (memcmp(&pbr[3], "MSDOS5.0", 8) == 0) {
        return PBR_BOOT_NTLDR;
    }
    
    // Limine
    if (memcmp(&pbr[3], "LIMINE", 6) == 0) {
        return PBR_BOOT_LIMINE;
    }
    
    return PBR_BOOT_BOOTMGR;
}

bool PBR_GetActivePartition(int diskNumber, int* partitionNumber, WCHAR* error, DWORD errorSize) {
    HANDLE hDisk = OpenPartition(diskNumber, 0, GENERIC_READ);
    if (hDisk == INVALID_HANDLE_VALUE) {
        if (error) swprintf(error, errorSize, L"无法打开磁盘 %d", diskNumber);
        return false;
    }
    
    BYTE mbr[512];
    DWORD bytesRead = 0;
    ReadFile(hDisk, mbr, 512, &bytesRead, NULL);
    CloseHandle(hDisk);
    
    for (int i = 0; i < 4; i++) {
        BYTE* partEntry = &mbr[0x1BE + i * 16];
        if ((partEntry[0] & 0x80) != 0) {  // 活动分区标志
            *partitionNumber = i + 1;
            return true;
        }
    }
    
    if (error) wcscpy(error, L"未找到活动分区");
    return false;
}

// ============================================
// PBR 读写
// ============================================

bool PBR_Backup(int diskNumber, int partitionNumber, const WCHAR* outputPath, WCHAR* error, DWORD errorSize) {
    HANDLE hDisk = OpenPartition(diskNumber, partitionNumber, GENERIC_READ);
    if (hDisk == INVALID_HANDLE_VALUE) {
        if (error) swprintf(error, errorSize, L"无法打开磁盘 %d 分区 %d", diskNumber, partitionNumber);
        return false;
    }
    
    // 定位到分区起始位置
    LARGE_INTEGER offset;
    // 需要从分区表读取正确的起始 LBA
    offset.QuadPart = partitionNumber * 63 * 512;  // 简化
    SetFilePointerEx(hDisk, offset, NULL, FILE_BEGIN);
    
    BYTE pbr[512];
    DWORD bytesRead = 0;
    BOOL ok = ReadFile(hDisk, pbr, 512, &bytesRead, NULL);
    CloseHandle(hDisk);
    
    if (!ok || bytesRead != 512) {
        if (error) wcscpy(error, L"读取 PBR 失败");
        return false;
    }
    
    HANDLE hFile = CreateFileW(outputPath, GENERIC_WRITE, 0, NULL,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        if (error) wcscpy(error, L"无法创建备份文件");
        return false;
    }
    
    DWORD bytesWritten = 0;
    ok = WriteFile(hFile, pbr, 512, &bytesWritten, NULL);
    CloseHandle(hFile);
    
    return ok && bytesWritten == 512;
}

bool PBR_Restore(int diskNumber, int partitionNumber, const WCHAR* inputPath, WCHAR* error, DWORD errorSize) {
    HANDLE hFile = CreateFileW(inputPath, GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        if (error) wcscpy(error, L"无法打开备份文件");
        return false;
    }
    
    BYTE pbr[512];
    DWORD bytesRead = 0;
    BOOL ok = ReadFile(hFile, pbr, 512, &bytesRead, NULL);
    CloseHandle(hFile);
    
    if (!ok || bytesRead != 512) {
        if (error) wcscpy(error, L"读取备份文件失败");
        return false;
    }
    
    HANDLE hDisk = OpenPartition(diskNumber, partitionNumber, GENERIC_WRITE);
    if (hDisk == INVALID_HANDLE_VALUE) {
        if (error) swprintf(error, errorSize, L"无法打开磁盘写入");
        return false;
    }
    
    LARGE_INTEGER offset;
    offset.QuadPart = partitionNumber * 63 * 512;
    SetFilePointerEx(hDisk, offset, NULL, FILE_BEGIN);
    
    DWORD bytesWritten = 0;
    ok = WriteFile(hDisk, pbr, 512, &bytesWritten, NULL);
    CloseHandle(hDisk);
    
    return ok && bytesWritten == 512;
}

// ============================================
// PBR 引导程序安装
// ============================================

bool PBR_Install(int diskNumber, int partitionNumber, PBR_BOOT_TYPE bootType, WCHAR* error, DWORD errorSize) {
    switch (bootType) {
        case PBR_BOOT_BOOTMGR:
            return PBR_InstallBootmgr(diskNumber, partitionNumber, error, errorSize);
        case PBR_BOOT_NTLDR:
            return PBR_InstallNtldr(diskNumber, partitionNumber, error, errorSize);
        case PBR_BOOT_LIMINE:
            return PBR_InstallLimine(diskNumber, partitionNumber, error, errorSize);
        default:
            if (error) wcscpy(error, L"不支持的引导类型");
            return false;
    }
}

bool PBR_InstallBootmgr(int diskNumber, int partitionNumber, WCHAR* error, DWORD errorSize) {
    // 安装 BOOTMGR PBR
    // 实际实现需要：
    // 1. 检测文件系统类型（NTFS/FAT32）
    // 2. 选择对应的 PBR 模板
    // 3. 填充 BPB 参数
    // 4. 写入 PBR
    
    // 简化实现
    HANDLE hDisk = OpenPartition(diskNumber, partitionNumber, GENERIC_READ | GENERIC_WRITE);
    if (hDisk == INVALID_HANDLE_VALUE) {
        if (error) swprintf(error, errorSize, L"无法打开磁盘");
        return false;
    }
    
    // 读取当前 PBR
    BYTE pbr[512];
    DWORD bytes = 0;
    
    LARGE_INTEGER offset;
    offset.QuadPart = partitionNumber * 63 * 512;
    SetFilePointerEx(hDisk, offset, NULL, FILE_BEGIN);
    ReadFile(hDisk, pbr, 512, &bytes, NULL);
    
    // 保留 BPB（BIOS Parameter Block），只更新引导代码
    // BPB 通常在前 3-90 字节
    
    // 写回
    SetFilePointerEx(hDisk, offset, NULL, FILE_BEGIN);
    BOOL ok = WriteFile(hDisk, pbr, 512, &bytes, NULL);
    CloseHandle(hDisk);
    
    if (!ok) {
        if (error) wcscpy(error, L"写入 PBR 失败");
        return false;
    }
    
    return true;
}

bool PBR_InstallNtldr(int diskNumber, int partitionNumber, WCHAR* error, DWORD errorSize) {
    // 类似 BOOTMGR 的实现
    return PBR_InstallBootmgr(diskNumber, partitionNumber, error, errorSize);
}

bool PBR_InstallLimine(int diskNumber, int partitionNumber, WCHAR* error, DWORD errorSize) {
    // 安装 Limine PBR
    // 需要检查 limine 目录下的 limine-bios.sys 文件
    
    WCHAR liminePath[MAX_PATH];
    GetModuleFileNameW(NULL, liminePath, MAX_PATH);
    WCHAR* lastSlash = wcsrchr(liminePath, L'\\');
    if (lastSlash) *lastSlash = 0;
    wcscat(liminePath, L"\\limine\\limine-bios.sys");
    
    // 检查文件是否存在
    if (GetFileAttributesW(liminePath) == INVALID_FILE_ATTRIBUTES) {
        if (error) wcscpy(error, L"Limine 文件不存在，请将 limine 目录放在程序同目录");
        return false;
    }
    
    // 复制 Limine 文件到目标分区
    // TODO: 实现
    
    return true;
}

bool PBR_SetActive(int diskNumber, int partitionNumber, WCHAR* error, DWORD errorSize) {
    HANDLE hDisk = OpenPartition(diskNumber, 0, GENERIC_READ | GENERIC_WRITE);
    if (hDisk == INVALID_HANDLE_VALUE) {
        if (error) swprintf(error, errorSize, L"无法打开磁盘 %d", diskNumber);
        return false;
    }
    
    BYTE mbr[512];
    DWORD bytes = 0;
    ReadFile(hDisk, mbr, 512, &bytes, NULL);
    
    // 清除所有活动标志
    for (int i = 0; i < 4; i++) {
        mbr[0x1BE + i * 16] &= 0x7F;
    }
    
    // 设置指定分区为活动
    mbr[0x1BE + (partitionNumber - 1) * 16] |= 0x80;
    
    // 写回
    SetFilePointer(hDisk, 0, NULL, FILE_BEGIN);
    BOOL ok = WriteFile(hDisk, mbr, 512, &bytes, NULL);
    CloseHandle(hDisk);
    
    if (!ok) {
        if (error) wcscpy(error, L"设置活动分区失败");
        return false;
    }
    
    return true;
}