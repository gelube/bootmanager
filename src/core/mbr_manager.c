/**
 * mbr_manager.c - MBR 引导程序管理实现
 * 
 * 支持 Limine、Windows NT、GRUB4DOS
 * PE 兼容，纯 Win32 API
 */

#include "../../include/mbr_manager.h"
#include <wchar.h>
#include <stdio.h>
#include <winioctl.h>
#include <initguid.h>
#include <ntddstor.h>
#include <ntdddisk.h>

// 如果 ntddstor.h 没有定义这些，手动定义
#ifndef IOCTL_STORAGE_QUERY_PROPERTY
#define IOCTL_STORAGE_QUERY_PROPERTY CTL_CODE(IOCTL_STORAGE_BASE, 0x0500, METHOD_BUFFERED, FILE_ANY_ACCESS)

typedef enum _STORAGE_PROPERTY_ID {
    StorageDeviceProperty = 0,
    StorageAdapterProperty,
    StorageDeviceIdProperty,
    StorageDeviceUniqueIdProperty,
    StorageDeviceWriteCacheProperty,
    StorageMiniportProperty,
    StorageAccessAlignmentProperty
} STORAGE_PROPERTY_ID;

typedef enum _STORAGE_QUERY_TYPE {
    PropertyStandardQuery = 0,
    PropertyExistsQuery,
    PropertyMaskQuery,
    PropertyQueryMaxDefined
} STORAGE_QUERY_TYPE;

typedef struct _STORAGE_PROPERTY_QUERY {
    STORAGE_PROPERTY_ID PropertyId;
    STORAGE_QUERY_TYPE QueryType;
    BYTE AdditionalParameters[1];
} STORAGE_PROPERTY_QUERY, *PSTORAGE_PROPERTY_QUERY;

typedef struct _STORAGE_DEVICE_DESCRIPTOR {
    DWORD Version;
    DWORD Size;
    BYTE DeviceType;
    BYTE DeviceTypeModifier;
    BOOLEAN RemovableMedia;
    BOOLEAN CommandQueueing;
    DWORD VendorIdOffset;
    DWORD ProductIdOffset;
    DWORD ProductRevisionOffset;
    DWORD SerialNumberOffset;
    STORAGE_BUS_TYPE BusType;
    DWORD RawPropertiesLength;
    BYTE RawDeviceProperties[1];
} STORAGE_DEVICE_DESCRIPTOR, *PSTORAGE_DEVICE_DESCRIPTOR;

#endif

// ============================================
// 内嵌引导代码
// ============================================

// Windows NT 6.x MBR 引导代码（从 Windows 10 提取）
static const BYTE s_winMbr[MBR_BOOT_CODE_SIZE] = {
    0x33,0xC0,0x8E,0xD0,0xBC,0x00,0x7C,0x8E,0xC0,0x8E,0xD8,0xBE,0x00,0x7C,0xBF,0x00,
    0x06,0xB9,0x00,0x02,0xFC,0xF3,0xA4,0x50,0x68,0x1C,0x06,0xCB,0xFB,0xB9,0x04,0x00,
    0xBD,0xBE,0x07,0x80,0x7E,0x00,0x00,0x7C,0x0B,0x0F,0x85,0x0E,0x01,0x83,0xC5,0x10,
    0xE2,0xF1,0xCD,0x18,0x88,0x56,0x00,0x55,0xC6,0x46,0x11,0x05,0xC6,0x46,0x10,0x00,
    0xB4,0x41,0xBB,0xAA,0x55,0xCD,0x13,0x5D,0x72,0x0F,0x81,0xFB,0x55,0xAA,0x75,0x09,
    0xF7,0xC1,0x01,0x00,0x74,0x03,0xFE,0x46,0x10,0x66,0x60,0x80,0x7E,0x10,0x00,0x74,
    0x26,0x66,0x68,0x00,0x00,0x00,0x00,0x66,0xFF,0x76,0x08,0x68,0x00,0x00,0x68,0x00,
    0x7C,0x68,0x01,0x00,0x68,0x10,0x00,0xB4,0x42,0x8A,0x56,0x00,0x8B,0xF4,0xCD,0x13,
    0x9F,0x83,0xC4,0x10,0x9E,0xEB,0x14,0xB8,0x01,0x02,0xBB,0x00,0x7C,0x8A,0x56,0x00,
    0x8A,0x76,0x01,0x8A,0x4E,0x02,0x8A,0x6E,0x03,0xCD,0x13,0x66,0x61,0x73,0x1C,0xFE,
    0x4E,0x11,0x75,0x0C,0x80,0x7E,0x00,0x80,0x0F,0x84,0x8A,0x00,0xB2,0x80,0xEB,0x84,
    0x55,0x32,0xE4,0x8A,0x56,0x00,0xCD,0x13,0x5D,0xEB,0x9E,0x81,0x3E,0xFE,0x7D,0x55,
    0xAA,0x75,0x6E,0xFF,0x76,0x00,0xE8,0x8D,0x00,0x75,0x17,0xFA,0xB0,0xD1,0xE6,0x64,
    0xE8,0x83,0x00,0xB0,0xDF,0xE6,0x60,0xE8,0x7C,0x00,0xB0,0xFF,0xE6,0x64,0xE8,0x75,
    0x00,0xFB,0xB8,0x00,0xBB,0xCD,0x1A,0x66,0x23,0xC0,0x75,0x3B,0x66,0x81,0xFB,0x54,
    0x43,0x50,0x41,0x75,0x32,0x81,0xF9,0x02,0x01,0x72,0x2C,0x66,0x68,0x07,0xBB,0x00,
    0x00,0x66,0x68,0x82,0x00,0x00,0x00,0x66,0x68,0x13,0x00,0x00,0x00,0x66,0x53,0x66,
    0x53,0x66,0x55,0x66,0x68,0x00,0x00,0x00,0x00,0x66,0x68,0x00,0x7C,0x00,0x00,0x66,
    0x61,0x68,0x00,0x00,0x07,0xCD,0x1A,0x5A,0x32,0xF6,0x74,0x0B,0x40,0x75,0x01,0x42,
    0x80,0xC7,0x02,0x75,0xF4,0x7B,0x49,0x8B,0xF0,0xAC,0x3C,0x00,0x74,0x09,0xBB,0x07,
    0x00,0xB4,0x0E,0xCD,0x10,0xEB,0xF2,0xF4,0xEB,0xFD,0x2B,0xC9,0xE4,0x64,0xEB,0x00,
    0x24,0x02,0xE0,0xF8,0x24,0x02,0xC3,0x49,0x6E,0x76,0x61,0x6C,0x69,0x64,0x20,0x70,
    0x61,0x72,0x74,0x69,0x74,0x69,0x6F,0x6E,0x20,0x74,0x61,0x62,0x6C,0x65,0x00,0x45,
    0x72,0x72,0x6F,0x72,0x20,0x6C,0x6F,0x61,0x64,0x69,0x6E,0x67,0x20,0x6F,0x70,0x65,
    0x72,0x61,0x74,0x69,0x6E,0x67,0x20,0x73,0x79,0x73,0x74,0x65,0x6D,0x00,0x4D,0x69,
    0x73,0x73,0x69,0x6E,0x67,0x20,0x6F,0x70,0x65,0x72,0x61,0x74,0x69,0x6E,0x67,0x20,
    0x73,0x79,0x73,0x74,0x65,0x6D,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00
};

// 最后的错误信息
static WCHAR s_lastError[512] = {0};

// ============================================
// 辅助函数
// ============================================

static void SetError(const WCHAR* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    _vsnwprintf(s_lastError, 512, fmt, args);
    va_end(args);
}

const WCHAR* MBR_GetLastErrorMessage(void) {
    return s_lastError;
}

void MBR_FormatSize(LONGLONG bytes, WCHAR* buffer, DWORD size) {
    if (bytes >= 1024LL * 1024 * 1024 * 1024) {
        swprintf(buffer, size, L"%.1f TB", (double)bytes / (1024.0 * 1024 * 1024 * 1024));
    } else if (bytes >= 1024LL * 1024 * 1024) {
        swprintf(buffer, size, L"%.1f GB", (double)bytes / (1024.0 * 1024 * 1024));
    } else if (bytes >= 1024LL * 1024) {
        swprintf(buffer, size, L"%.1f MB", (double)bytes / (1024.0 * 1024));
    } else if (bytes >= 1024) {
        swprintf(buffer, size, L"%.1f KB", (double)bytes / 1024.0);
    } else {
        swprintf(buffer, size, L"%lld B", bytes);
    }
}

const WCHAR* MBR_GetBootTypeName(MBR_BOOT_TYPE type) {
    switch (type) {
        case MBR_BOOT_WINDOWS:  return L"Windows NT";
        case MBR_BOOT_LIMINE:   return L"Limine";
        case MBR_BOOT_GRUB4DOS: return L"GRUB4DOS";
        case MBR_BOOT_SYSLINUX: return L"SYSLINUX";
        case MBR_BOOT_GRUB2:    return L"GRUB2";
        default: return L"未知";
    }
}

const WCHAR* MBR_GetFilesystemName(PARTITION_FILESYSTEM fs) {
    switch (fs) {
        case PART_TYPE_NTFS:   return L"NTFS";
        case PART_TYPE_FAT32:  return L"FAT32";
        case PART_TYPE_FAT16:  return L"FAT16";
        case PART_TYPE_EXFAT:  return L"exFAT";
        case PART_TYPE_LINUX:  return L"ext";
        case PART_TYPE_SWAP:   return L"swap";
        case PART_TYPE_EFI:    return L"EFI";
        default: return L"未知";
    }
}

// ============================================
// 磁盘操作
// ============================================

static HANDLE OpenDisk(int diskNumber, DWORD access) {
    WCHAR path[MAX_PATH];
    swprintf(path, MAX_PATH, L"\\\\.\\PhysicalDrive%d", diskNumber);
    
    HANDLE hDisk = CreateFileW(path, access, FILE_SHARE_READ | FILE_SHARE_WRITE,
                               NULL, OPEN_EXISTING, 0, NULL);
    return hDisk;
}

static HANDLE OpenVolume(WCHAR driveLetter, DWORD access) {
    WCHAR path[8];
    swprintf(path, 8, L"\\\\.\\%c:", driveLetter);
    
    HANDLE hVol = CreateFileW(path, access, FILE_SHARE_READ | FILE_SHARE_WRITE,
                              NULL, OPEN_EXISTING, 0, NULL);
    return hVol;
}

/* PE 兜底：X: 内存盘没有物理磁盘映射，此时按"有活动分区的 MBR 盘"推断系统盘 */
static int FallbackSystemDiskByBootFlag(void) {
    for (int i = 0; i < 16; i++) {
        HANDLE hDisk = OpenDisk(i, GENERIC_READ);
        if (hDisk == INVALID_HANDLE_VALUE) continue;

        BYTE mbr[SECTOR_SIZE];
        DWORD bytesRead = 0;
        BOOL ok = ReadFile(hDisk, mbr, SECTOR_SIZE, &bytesRead, NULL) && bytesRead == SECTOR_SIZE;
        CloseHandle(hDisk);
        if (!ok) continue;
        if (mbr[0x1FE] != 0x55 || mbr[0x1FF] != 0xAA) continue;

        for (int p = 0; p < 4; p++) {
            BYTE* entry = &mbr[0x1BE + p * 16];
            if (entry[0] == 0x80 && entry[4] != 0) return i;   /* 活动分区所在盘 */
        }
    }
    return -1;
}

int MBR_GetSystemDiskNumber(void) {
    WCHAR systemDir[MAX_PATH];
    GetWindowsDirectoryW(systemDir, MAX_PATH);

    HANDLE hVol = OpenVolume(systemDir[0], GENERIC_READ);
    if (hVol != INVALID_HANDLE_VALUE) {
        VOLUME_DISK_EXTENTS extents = {0};
        DWORD bytesReturned;
        int diskNumber = -1;

        if (DeviceIoControl(hVol, IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS,
            NULL, 0, &extents, sizeof(extents), &bytesReturned, NULL)
            && extents.NumberOfDiskExtents > 0) {
            diskNumber = (int)extents.Extents[0].DiskNumber;
        }
        CloseHandle(hVol);
        if (diskNumber >= 0) return diskNumber;
    }

    /* 正常 Windows 打开卷失败极少见；走到这里大概率是 PE（Windows 目录在内存盘上） */
    return FallbackSystemDiskByBootFlag();
}

// ============================================
// 初始化和清理
// ============================================

void MBR_InitDiskList(MBR_DISK_LIST* list) {
    list->disks = NULL;
    list->count = 0;
}

void MBR_FreeDiskList(MBR_DISK_LIST* list) {
    if (!list) return;
    
    for (int i = 0; i < list->count; i++) {
        if (list->disks[i].partitions) {
            free(list->disks[i].partitions);
        }
    }
    
    if (list->disks) {
        free(list->disks);
        list->disks = NULL;
    }
    list->count = 0;
}

// ============================================
// 磁盘信息查询
// ============================================

bool MBR_IsDiskGPT(int diskNumber) {
    HANDLE hDisk = OpenDisk(diskNumber, GENERIC_READ);
    if (hDisk == INVALID_HANDLE_VALUE) return false;
    
    BYTE mbr[SECTOR_SIZE];
    DWORD bytesRead = 0;
    BOOL ok = ReadFile(hDisk, mbr, SECTOR_SIZE, &bytesRead, NULL);
    CloseHandle(hDisk);
    
    if (!ok || bytesRead != SECTOR_SIZE) return false;
    
    // 检查保护性 MBR (分区类型 0xEE)
    // GPT 磁盘的第一个分区项类型为 0xEE
    if (mbr[0x1BE + 4] == 0xEE) return true;
    
    // 检查 MBR 签名
    if (mbr[0x1FE] != 0x55 || mbr[0x1FF] != 0xAA) return false;
    
    return false;
}

MBR_BOOT_TYPE MBR_DetectBootType(int diskNumber) {
    HANDLE hDisk = OpenDisk(diskNumber, GENERIC_READ);
    if (hDisk == INVALID_HANDLE_VALUE) return MBR_BOOT_UNKNOWN;
    
    BYTE mbr[SECTOR_SIZE];
    DWORD bytesRead = 0;
    ReadFile(hDisk, mbr, SECTOR_SIZE, &bytesRead, NULL);
    CloseHandle(hDisk);
    
    if (bytesRead != SECTOR_SIZE) return MBR_BOOT_UNKNOWN;
    
    // 检测引导程序签名
    // Limine: "LIMINE" 签名在偏移 3
    if (memcmp(&mbr[3], "LIMINE", 6) == 0) return MBR_BOOT_LIMINE;
    
    // GRUB4DOS: "GRUB" 签名
    if (memcmp(&mbr[3], "GRUB", 4) == 0) return MBR_BOOT_GRUB4DOS;
    
    // GRUB2: 特征码
    if (memcmp(&mbr[0x80], "\xEB\x63\x90\x00\x00\x00\x00\x00", 8) == 0) return MBR_BOOT_GRUB2;
    
    // Windows NT: 0x33 0xC0 (XOR AX, AX)
    if (mbr[0] == 0x33 && mbr[1] == 0xC0) return MBR_BOOT_WINDOWS;
    
    // SYSLINUX: 检测特征
    if (memcmp(&mbr[0x40], "SYSLINUX", 8) == 0) return MBR_BOOT_SYSLINUX;
    
    return MBR_BOOT_UNKNOWN;
}

// 获取分区文件系统类型
static PARTITION_FILESYSTEM DetectFilesystem(BYTE partitionType) {
    switch (partitionType) {
        case 0x07: return PART_TYPE_NTFS;      // NTFS/exFAT
        case 0x0B: 
        case 0x0C: return PART_TYPE_FAT32;     // FAT32
        case 0x04:
        case 0x06: return PART_TYPE_FAT16;     // FAT16
        case 0x83: return PART_TYPE_LINUX;     // Linux
        case 0x82: return PART_TYPE_SWAP;      // Linux swap
        case 0xEF: return PART_TYPE_EFI;       // EFI System Partition
        default: return PART_TYPE_UNKNOWN;
    }
}

bool MBR_GetPartitions(int diskNumber, MBR_DISK_INFO* info, WCHAR* error, DWORD errorSize) {
    HANDLE hDisk = OpenDisk(diskNumber, GENERIC_READ);
    if (hDisk == INVALID_HANDLE_VALUE) {
        if (error) swprintf(error, errorSize, L"无法打开磁盘 %d", diskNumber);
        return false;
    }
    
    // 读取 MBR
    BYTE mbr[SECTOR_SIZE];
    DWORD bytesRead = 0;
    if (!ReadFile(hDisk, mbr, SECTOR_SIZE, &bytesRead, NULL) || bytesRead != SECTOR_SIZE) {
        CloseHandle(hDisk);
        if (error) swprintf(error, errorSize, L"读取 MBR 失败");
        return false;
    }
    
    CloseHandle(hDisk);
    
    // 解析分区表（MBR 最多 4 个主分区）
    info->partitionCount = 0;
    info->activePartition = 0;
    
    // 分配分区数组
    info->partitions = (MBR_PARTITION_INFO*)calloc(4, sizeof(MBR_PARTITION_INFO));
    if (!info->partitions) {
        if (error) wcscpy(error, L"内存分配失败");
        return false;
    }
    
    // 解析 4 个分区表项
    for (int i = 0; i < 4; i++) {
        BYTE* entry = &mbr[0x1BE + i * 16];
        BYTE status = entry[0];
        BYTE type = entry[4];
        
        if (type == 0) continue;  // 空分区
        
        MBR_PARTITION_INFO* part = &info->partitions[info->partitionCount];
        part->diskNumber = diskNumber;
        part->partitionNumber = i + 1;
        part->isActive = (status == 0x80);
        part->partitionType = type;
        part->fs = DetectFilesystem(type);
        
        // 获取起始 LBA 和大小
        part->startLBA = *(DWORD*)(entry + 8);
        part->totalSectors = *(DWORD*)( entry + 12);
        part->sizeBytes = (LONGLONG)part->totalSectors * SECTOR_SIZE;
        
        if (part->isActive) {
            info->activePartition = i + 1;
        }
        
        info->partitionCount++;
    }
    
    // 获取盘符映射
    WCHAR drives[512] = {0};
    if (GetLogicalDriveStringsW(511, drives)) {
        WCHAR* p = drives;
        while (*p) {
            HANDLE hVol = OpenVolume(p[0], GENERIC_READ);
            if (hVol != INVALID_HANDLE_VALUE) {
                VOLUME_DISK_EXTENTS extents = {0};
                DWORD bytesReturned;
                
                if (DeviceIoControl(hVol, IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS,
                    NULL, 0, &extents, sizeof(extents), &bytesReturned, NULL)) {
                    
                    if (extents.Extents[0].DiskNumber == (DWORD)diskNumber) {
                        // 找到匹配的分区
                        LONGLONG partStart = extents.Extents[0].StartingOffset.QuadPart / SECTOR_SIZE;
                        
                        for (int i = 0; i < info->partitionCount; i++) {
                            if (info->partitions[i].startLBA == partStart) {
                                info->partitions[i].driveLetter = p[0];
                                
                                // 获取卷标
                                WCHAR root[4] = {p[0], L':', L'\\', 0};
                                GetVolumeInformationW(root, info->partitions[i].label, 64,
                                    NULL, NULL, NULL, NULL, 0);
                                break;
                            }
                        }
                    }
                }
                CloseHandle(hVol);
            }
            p += wcslen(p) + 1;
        }
    }
    
    return true;
}

bool MBR_GetDiskInfo(int diskNumber, MBR_DISK_INFO* info, WCHAR* error, DWORD errorSize) {
    if (!info) {
        if (error) wcscpy(error, L"参数无效");
        return false;
    }
    
    memset(info, 0, sizeof(MBR_DISK_INFO));
    info->diskNumber = diskNumber;
    
    HANDLE hDisk = OpenDisk(diskNumber, GENERIC_READ);
    if (hDisk == INVALID_HANDLE_VALUE) {
        if (error) swprintf(error, errorSize, L"无法打开磁盘 %d", diskNumber);
        return false;
    }
    
    // 获取磁盘大小
    GET_LENGTH_INFORMATION lenInfo;
    DWORD bytesReturned;
    if (DeviceIoControl(hDisk, IOCTL_DISK_GET_LENGTH_INFO, NULL, 0,
        &lenInfo, sizeof(lenInfo), &bytesReturned, NULL)) {
        info->sizeBytes = lenInfo.Length.QuadPart;
    }
    
    // 获取磁盘几何信息
    DISK_GEOMETRY geo;
    if (DeviceIoControl(hDisk, IOCTL_DISK_GET_DRIVE_GEOMETRY, NULL, 0,
        &geo, sizeof(geo), &bytesReturned, NULL)) {
        // 检查是否可移动介质
        info->isRemovable = (geo.MediaType == RemovableMedia);
    }
    
    // 额外检查：使用 STORAGE_PROPERTY_QUERY 获取更准确的可移动介质信息
    STORAGE_PROPERTY_QUERY query = {0};
    query.PropertyId = StorageDeviceProperty;
    query.QueryType = PropertyStandardQuery;
    
    BYTE buffer[1024] = {0};
    PSTORAGE_DEVICE_DESCRIPTOR pDesc = (PSTORAGE_DEVICE_DESCRIPTOR)buffer;
    
    if (DeviceIoControl(hDisk, IOCTL_STORAGE_QUERY_PROPERTY,
        &query, sizeof(query), buffer, sizeof(buffer), &bytesReturned, NULL)) {
        if (pDesc->RemovableMedia) {
            info->isRemovable = TRUE;
        }
    }
    
    // 检查是否 GPT
    info->isGPT = MBR_IsDiskGPT(diskNumber);
    
    CloseHandle(hDisk);
    
    // 获取分区信息
    if (!info->isGPT) {
        MBR_GetPartitions(diskNumber, info, error, errorSize);
    }
    
    // 检查是否系统磁盘
    int sysDisk = MBR_GetSystemDiskNumber();
    info->isSystem = (diskNumber == sysDisk);
    
    return true;
}

bool MBR_GetDisks(MBR_DISK_LIST* list, WCHAR* error, DWORD errorSize) {
    if (!list) {
        if (error) wcscpy(error, L"参数无效");
        return false;
    }
    
    MBR_InitDiskList(list);
    
    // 先计算符合条件的磁盘数量（只统计 MBR 磁盘）
    int diskCount = 0;
    for (int i = 0; i < MAX_DISKS; i++) {
        HANDLE hDisk = OpenDisk(i, GENERIC_READ);
        if (hDisk == INVALID_HANDLE_VALUE) continue;
        
        // 检查是否可移动介质
        STORAGE_PROPERTY_QUERY query = {0};
        query.PropertyId = StorageDeviceProperty;
        query.QueryType = PropertyStandardQuery;
        
        BYTE buffer[1024] = {0};
        PSTORAGE_DEVICE_DESCRIPTOR pDesc = (PSTORAGE_DEVICE_DESCRIPTOR)buffer;
        DWORD bytesReturned;
        
        BOOL isRemovable = FALSE;
        if (DeviceIoControl(hDisk, IOCTL_STORAGE_QUERY_PROPERTY,
            &query, sizeof(query), buffer, sizeof(buffer), &bytesReturned, NULL)) {
            isRemovable = pDesc->RemovableMedia;
        }
        
        // 检查是否 GPT
        BOOL isGPT = MBR_IsDiskGPT(i);
        
        CloseHandle(hDisk);
        
        // 只统计 MBR 格式的固定磁盘
        if (!isRemovable && !isGPT) {
            diskCount++;
        }
    }
    
    if (diskCount == 0) {
        if (error) wcscpy(error, L"未找到 MBR 磁盘\n\n仅显示 MBR 格式的固定磁盘");
        return false;
    }
    
    list->disks = (MBR_DISK_INFO*)calloc(diskCount, sizeof(MBR_DISK_INFO));
    if (!list->disks) {
        if (error) wcscpy(error, L"内存分配失败");
        return false;
    }
    
    int sysDisk = MBR_GetSystemDiskNumber();
    
    for (int i = 0, idx = 0; i < MAX_DISKS && idx < diskCount; i++) {
        HANDLE hDisk = OpenDisk(i, GENERIC_READ);
        if (hDisk == INVALID_HANDLE_VALUE) continue;
        
        // 检查是否可移动介质
        STORAGE_PROPERTY_QUERY query = {0};
        query.PropertyId = StorageDeviceProperty;
        query.QueryType = PropertyStandardQuery;
        
        BYTE buffer[1024] = {0};
        PSTORAGE_DEVICE_DESCRIPTOR pDesc = (PSTORAGE_DEVICE_DESCRIPTOR)buffer;
        DWORD bytesReturned;
        
        BOOL isRemovable = FALSE;
        if (DeviceIoControl(hDisk, IOCTL_STORAGE_QUERY_PROPERTY,
            &query, sizeof(query), buffer, sizeof(buffer), &bytesReturned, NULL)) {
            isRemovable = pDesc->RemovableMedia;
        }
        
        // 检查是否 GPT
        BOOL isGPT = MBR_IsDiskGPT(i);
        
        // 跳过可移动介质和 GPT 磁盘
        if (isRemovable || isGPT) {
            CloseHandle(hDisk);
            continue;
        }
        
        list->disks[idx].diskNumber = i;
        list->disks[idx].isSystem = (i == sysDisk);
        list->disks[idx].isGPT = FALSE;  // 已经过滤了 GPT
        list->disks[idx].isRemovable = FALSE;  // 已经过滤了可移动介质
        
        // 获取磁盘大小
        GET_LENGTH_INFORMATION lenInfo;
        if (DeviceIoControl(hDisk, IOCTL_DISK_GET_LENGTH_INFO, NULL, 0,
            &lenInfo, sizeof(lenInfo), &bytesReturned, NULL)) {
            list->disks[idx].sizeBytes = lenInfo.Length.QuadPart;
        }
        
        CloseHandle(hDisk);
        
        // 获取分区信息
        MBR_GetPartitions(i, &list->disks[idx], NULL, 0);
        
        list->count = ++idx;
    }
    
    return true;
}

// ============================================
// 分区操作
// ============================================

int MBR_GetActivePartition(int diskNumber) {
    HANDLE hDisk = OpenDisk(diskNumber, GENERIC_READ);
    if (hDisk == INVALID_HANDLE_VALUE) return 0;
    
    BYTE mbr[SECTOR_SIZE];
    DWORD bytesRead = 0;
    ReadFile(hDisk, mbr, SECTOR_SIZE, &bytesRead, NULL);
    CloseHandle(hDisk);
    
    if (bytesRead != SECTOR_SIZE) return 0;
    
    for (int i = 0; i < 4; i++) {
        BYTE* entry = &mbr[0x1BE + i * 16];
        if (entry[0] == 0x80 && entry[4] != 0) {
            return i + 1;
        }
    }
    
    return 0;
}

bool MBR_SetActivePartition(int diskNumber, int partitionNumber, WCHAR* error, DWORD errorSize) {
    if (partitionNumber < 1 || partitionNumber > 4) {
        if (error) swprintf(error, errorSize, L"无效的分区编号: %d", partitionNumber);
        return false;
    }
    
    HANDLE hDisk = OpenDisk(diskNumber, GENERIC_READ | GENERIC_WRITE);
    if (hDisk == INVALID_HANDLE_VALUE) {
        if (error) swprintf(error, errorSize, L"无法打开磁盘 %d", diskNumber);
        return false;
    }
    
    BYTE mbr[SECTOR_SIZE];
    DWORD bytes = 0;
    
    if (!ReadFile(hDisk, mbr, SECTOR_SIZE, &bytes, NULL) || bytes != SECTOR_SIZE) {
        CloseHandle(hDisk);
        if (error) wcscpy(error, L"读取 MBR 失败");
        return false;
    }
    
    // 检查分区是否存在
    BYTE* targetEntry = &mbr[0x1BE + (partitionNumber - 1) * 16];
    if (targetEntry[4] == 0) {
        CloseHandle(hDisk);
        if (error) swprintf(error, errorSize, L"分区 %d 不存在", partitionNumber);
        return false;
    }
    
    // 清除所有分区的活动标志
    for (int i = 0; i < 4; i++) {
        mbr[0x1BE + i * 16] &= 0x7F;
    }
    
    // 设置目标分区为活动
    mbr[0x1BE + (partitionNumber - 1) * 16] = 0x80;
    
    // 写回 MBR
    SetFilePointer(hDisk, 0, NULL, FILE_BEGIN);
    BOOL ok = WriteFile(hDisk, mbr, SECTOR_SIZE, &bytes, NULL) && bytes == SECTOR_SIZE;
    CloseHandle(hDisk);
    
    if (!ok) {
        if (error) wcscpy(error, L"写入 MBR 失败");
        return false;
    }
    
    return true;
}

// ============================================
// MBR 读写操作
// ============================================

bool MBR_Backup(int diskNumber, const WCHAR* outputPath, WCHAR* error, DWORD errorSize) {
    HANDLE hDisk = OpenDisk(diskNumber, GENERIC_READ);
    if (hDisk == INVALID_HANDLE_VALUE) {
        if (error) swprintf(error, errorSize, L"无法打开磁盘 %d", diskNumber);
        return false;
    }
    
    BYTE mbr[SECTOR_SIZE];
    DWORD bytesRead = 0;
    BOOL ok = ReadFile(hDisk, mbr, SECTOR_SIZE, &bytesRead, NULL);
    CloseHandle(hDisk);
    
    if (!ok || bytesRead != SECTOR_SIZE) {
        if (error) wcscpy(error, L"读取 MBR 失败");
        return false;
    }
    
    HANDLE hFile = CreateFileW(outputPath, GENERIC_WRITE, 0, NULL,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        if (error) wcscpy(error, L"无法创建备份文件");
        return false;
    }
    
    DWORD bytesWritten = 0;
    ok = WriteFile(hFile, mbr, SECTOR_SIZE, &bytesWritten, NULL);
    CloseHandle(hFile);
    
    if (!ok || bytesWritten != SECTOR_SIZE) {
        if (error) wcscpy(error, L"写入备份文件失败");
        return false;
    }
    
    return true;
}

bool MBR_Restore(int diskNumber, const WCHAR* inputPath, bool preservePartTable, WCHAR* error, DWORD errorSize) {
    HANDLE hFile = CreateFileW(inputPath, GENERIC_READ, 0, NULL,
                               OPEN_EXISTING, 0, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        if (error) wcscpy(error, L"无法打开备份文件");
        return false;
    }
    
    BYTE mbr[SECTOR_SIZE];
    DWORD bytesRead = 0;
    BOOL ok = ReadFile(hFile, mbr, SECTOR_SIZE, &bytesRead, NULL);
    CloseHandle(hFile);
    
    if (!ok || bytesRead != SECTOR_SIZE) {
        if (error) wcscpy(error, L"读取备份文件失败");
        return false;
    }
    
    if (preservePartTable) {
        // 读取当前分区表
        HANDLE hDisk = OpenDisk(diskNumber, GENERIC_READ);
        if (hDisk != INVALID_HANDLE_VALUE) {
            BYTE currentMbr[SECTOR_SIZE];
            DWORD curRead = 0;
            BOOL curOk = ReadFile(hDisk, currentMbr, SECTOR_SIZE, &curRead, NULL) && curRead == SECTOR_SIZE;
            CloseHandle(hDisk);

            if (!curOk) {
                if (error) wcscpy(error, L"读取当前分区表失败，已取消恢复");
                return false;
            }

            // 保留分区表 (偏移 446-511，共 66 字节)
            memcpy(&mbr[446], &currentMbr[446], 66);
        }
    }
    
    // 写入 MBR
    HANDLE hDisk = OpenDisk(diskNumber, GENERIC_WRITE);
    if (hDisk == INVALID_HANDLE_VALUE) {
        if (error) wcscpy(error, L"无法打开磁盘进行写入");
        return false;
    }
    
    DWORD bytesWritten = 0;
    ok = WriteFile(hDisk, mbr, SECTOR_SIZE, &bytesWritten, NULL);
    CloseHandle(hDisk);
    
    if (!ok || bytesWritten != SECTOR_SIZE) {
        if (error) wcscpy(error, L"写入 MBR 失败");
        return false;
    }
    
    return true;
}

bool MBR_BackupBootCode(int diskNumber, const WCHAR* outputPath, WCHAR* error, DWORD errorSize) {
    HANDLE hDisk = OpenDisk(diskNumber, GENERIC_READ);
    if (hDisk == INVALID_HANDLE_VALUE) {
        if (error) swprintf(error, errorSize, L"无法打开磁盘 %d", diskNumber);
        return false;
    }
    
    BYTE mbr[SECTOR_SIZE];
    DWORD bytesRead = 0;
    BOOL ok = ReadFile(hDisk, mbr, SECTOR_SIZE, &bytesRead, NULL);
    CloseHandle(hDisk);
    
    if (!ok || bytesRead != SECTOR_SIZE) {
        if (error) wcscpy(error, L"读取 MBR 失败");
        return false;
    }
    
    HANDLE hFile = CreateFileW(outputPath, GENERIC_WRITE, 0, NULL,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        if (error) wcscpy(error, L"无法创建备份文件");
        return false;
    }
    
    DWORD bytesWritten = 0;
    ok = WriteFile(hFile, mbr, MBR_BOOT_CODE_SIZE, &bytesWritten, NULL);
    CloseHandle(hFile);
    
    if (!ok || bytesWritten != MBR_BOOT_CODE_SIZE) {
        if (error) wcscpy(error, L"写入备份文件失败");
        return false;
    }
    
    return true;
}

bool MBR_RestoreBootCode(int diskNumber, const WCHAR* inputPath, WCHAR* error, DWORD errorSize) {
    HANDLE hFile = CreateFileW(inputPath, GENERIC_READ, 0, NULL,
                               OPEN_EXISTING, 0, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        if (error) wcscpy(error, L"无法打开备份文件");
        return false;
    }
    
    BYTE bootCode[MBR_BOOT_CODE_SIZE];
    DWORD bytesRead = 0;
    BOOL ok = ReadFile(hFile, bootCode, MBR_BOOT_CODE_SIZE, &bytesRead, NULL);
    CloseHandle(hFile);
    
    if (!ok || bytesRead != MBR_BOOT_CODE_SIZE) {
        if (error) wcscpy(error, L"读取备份文件失败");
        return false;
    }
    
    // 读取当前 MBR
    HANDLE hDisk = OpenDisk(diskNumber, GENERIC_READ | GENERIC_WRITE);
    if (hDisk == INVALID_HANDLE_VALUE) {
        if (error) swprintf(error, errorSize, L"无法打开磁盘 %d", diskNumber);
        return false;
    }
    
    BYTE mbr[SECTOR_SIZE];
    DWORD readOk = ReadFile(hDisk, mbr, SECTOR_SIZE, &bytesRead, NULL) && bytesRead == SECTOR_SIZE;
    if (!readOk) {
        CloseHandle(hDisk);
        if (error) wcscpy(error, L"读取当前 MBR 失败，已取消恢复");
        return false;
    }

    // 仅覆盖引导代码
    memcpy(mbr, bootCode, MBR_BOOT_CODE_SIZE);
    
    // 写回
    SetFilePointer(hDisk, 0, NULL, FILE_BEGIN);
    DWORD bytesWritten = 0;
    ok = WriteFile(hDisk, mbr, SECTOR_SIZE, &bytesWritten, NULL);
    CloseHandle(hDisk);
    
    if (!ok || bytesWritten != SECTOR_SIZE) {
        if (error) wcscpy(error, L"写入 MBR 失败");
        return false;
    }
    
    return true;
}

// ============================================
// PBR 操作
// ============================================

bool MBR_BackupPBRByDrive(WCHAR driveLetter, const WCHAR* outputPath, WCHAR* error, DWORD errorSize) {
    HANDLE hVol = OpenVolume(driveLetter, GENERIC_READ);
    if (hVol == INVALID_HANDLE_VALUE) {
        if (error) swprintf(error, errorSize, L"无法打开驱动器 %c:", driveLetter);
        return false;
    }
    
    BYTE pbr[SECTOR_SIZE];
    DWORD bytesRead = 0;
    BOOL ok = ReadFile(hVol, pbr, SECTOR_SIZE, &bytesRead, NULL);
    CloseHandle(hVol);
    
    if (!ok || bytesRead != SECTOR_SIZE) {
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
    ok = WriteFile(hFile, pbr, SECTOR_SIZE, &bytesWritten, NULL);
    CloseHandle(hFile);
    
    if (!ok || bytesWritten != SECTOR_SIZE) {
        if (error) wcscpy(error, L"写入备份文件失败");
        return false;
    }
    
    return true;
}

bool MBR_RestorePBRByDrive(WCHAR driveLetter, const WCHAR* inputPath, WCHAR* error, DWORD errorSize) {
    HANDLE hFile = CreateFileW(inputPath, GENERIC_READ, 0, NULL,
                               OPEN_EXISTING, 0, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        if (error) wcscpy(error, L"无法打开备份文件");
        return false;
    }
    
    BYTE pbr[SECTOR_SIZE];
    DWORD bytesRead = 0;
    BOOL ok = ReadFile(hFile, pbr, SECTOR_SIZE, &bytesRead, NULL);
    CloseHandle(hFile);
    
    if (!ok || bytesRead != SECTOR_SIZE) {
        if (error) wcscpy(error, L"读取备份文件失败");
        return false;
    }
    
    HANDLE hVol = OpenVolume(driveLetter, GENERIC_WRITE);
    if (hVol == INVALID_HANDLE_VALUE) {
        if (error) swprintf(error, errorSize, L"无法打开驱动器 %c:", driveLetter);
        return false;
    }
    
    // 锁定卷
    DWORD bytesReturned;
    DeviceIoControl(hVol, FSCTL_LOCK_VOLUME, NULL, 0, NULL, 0, &bytesReturned, NULL);
    
    DWORD bytesWritten = 0;
    ok = WriteFile(hVol, pbr, SECTOR_SIZE, &bytesWritten, NULL);
    
    // 解锁卷
    DeviceIoControl(hVol, FSCTL_UNLOCK_VOLUME, NULL, 0, NULL, 0, &bytesReturned, NULL);
    CloseHandle(hVol);
    
    if (!ok || bytesWritten != SECTOR_SIZE) {
        if (error) wcscpy(error, L"写入 PBR 失败");
        return false;
    }
    
    return true;
}

bool MBR_BackupPBR(int diskNumber, int partitionNumber, const WCHAR* outputPath, WCHAR* error, DWORD errorSize) {
    // 获取分区信息以找到盘符
    MBR_DISK_INFO info = {0};
    if (!MBR_GetDiskInfo(diskNumber, &info, error, errorSize)) {
        return false;
    }
    
    WCHAR driveLetter = 0;
    for (int i = 0; i < info.partitionCount; i++) {
        if (info.partitions[i].partitionNumber == partitionNumber) {
            driveLetter = info.partitions[i].driveLetter;
            break;
        }
    }
    
    if (info.partitions) free(info.partitions);
    
    if (driveLetter == 0) {
        if (error) swprintf(error, errorSize, L"分区 %d 没有盘符", partitionNumber);
        return false;
    }
    
    return MBR_BackupPBRByDrive(driveLetter, outputPath, error, errorSize);
}

bool MBR_RestorePBR(int diskNumber, int partitionNumber, const WCHAR* inputPath, WCHAR* error, DWORD errorSize) {
    MBR_DISK_INFO info = {0};
    if (!MBR_GetDiskInfo(diskNumber, &info, error, errorSize)) {
        return false;
    }
    
    WCHAR driveLetter = 0;
    for (int i = 0; i < info.partitionCount; i++) {
        if (info.partitions[i].partitionNumber == partitionNumber) {
            driveLetter = info.partitions[i].driveLetter;
            break;
        }
    }
    
    if (info.partitions) free(info.partitions);
    
    if (driveLetter == 0) {
        if (error) swprintf(error, errorSize, L"分区 %d 没有盘符", partitionNumber);
        return false;
    }
    
    return MBR_RestorePBRByDrive(driveLetter, inputPath, error, errorSize);
}

// ============================================
// 引导程序安装
// ============================================

bool MBR_RepairWindows(int diskNumber, WCHAR* error, DWORD errorSize) {
    HANDLE hDisk = OpenDisk(diskNumber, GENERIC_READ | GENERIC_WRITE);
    if (hDisk == INVALID_HANDLE_VALUE) {
        if (error) swprintf(error, errorSize, L"无法打开磁盘 %d", diskNumber);
        return false;
    }
    
    // 读取当前 MBR
    BYTE mbr[SECTOR_SIZE];
    DWORD bytes = 0;
    if (!ReadFile(hDisk, mbr, SECTOR_SIZE, &bytes, NULL) || bytes != SECTOR_SIZE) {
        CloseHandle(hDisk);
        if (error) wcscpy(error, L"读取 MBR 失败");
        return false;
    }
    
    // 覆盖引导代码，保留分区表
    memcpy(mbr, s_winMbr, MBR_BOOT_CODE_SIZE);
    
    // 确保签名正确
    mbr[0x1FE] = 0x55;
    mbr[0x1FF] = 0xAA;
    
    // 写回
    SetFilePointer(hDisk, 0, NULL, FILE_BEGIN);
    BOOL ok = WriteFile(hDisk, mbr, SECTOR_SIZE, &bytes, NULL) && bytes == SECTOR_SIZE;
    CloseHandle(hDisk);
    
    if (!ok) {
        if (error) wcscpy(error, L"写入 MBR 失败");
        return false;
    }
    
    return true;
}

bool MBR_Install(int diskNumber, MBR_BOOT_TYPE bootType, WCHAR* error, DWORD errorSize) {
    switch (bootType) {
        case MBR_BOOT_WINDOWS:
            return MBR_RepairWindows(diskNumber, error, errorSize);
        case MBR_BOOT_LIMINE:
            return MBR_InstallLimine(diskNumber, NULL, false, error, errorSize);
        case MBR_BOOT_GRUB4DOS:
            return MBR_InstallGrub4Dos(diskNumber, NULL, error, errorSize);
        default:
            if (error) wcscpy(error, L"不支持的引导程序类型");
            return false;
    }
}

// 从文件加载引导代码
static BYTE* LoadBootCodeFromFile(const WCHAR* filePath, DWORD* outSize, WCHAR* error, DWORD errorSize) {
    HANDLE hFile = CreateFileW(filePath, GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        if (error) swprintf(error, errorSize, L"无法打开文件: %s", filePath);
        return NULL;
    }
    
    DWORD fileSize = GetFileSize(hFile, NULL);
    if (fileSize == 0 || fileSize > SECTOR_SIZE) {
        CloseHandle(hFile);
        if (error) wcscpy(error, L"文件大小无效");
        return NULL;
    }
    
    BYTE* buffer = (BYTE*)malloc(fileSize);
    if (!buffer) {
        CloseHandle(hFile);
        if (error) wcscpy(error, L"内存分配失败");
        return NULL;
    }
    
    DWORD bytesRead = 0;
    if (!ReadFile(hFile, buffer, fileSize, &bytesRead, NULL) || bytesRead != fileSize) {
        free(buffer);
        CloseHandle(hFile);
        if (error) wcscpy(error, L"读取文件失败");
        return NULL;
    }
    
    CloseHandle(hFile);
    *outSize = fileSize;
    return buffer;
}

// 查找 Limine 源目录
static bool FindLimineSource(WCHAR* sourcePath, DWORD size) {
    WCHAR exeDir[MAX_PATH];
    GetModuleFileNameW(NULL, exeDir, MAX_PATH);
    WCHAR* lastSlash = wcsrchr(exeDir, L'\\');
    if (lastSlash) *lastSlash = L'\0';
    
    // 尝试多个位置
    const WCHAR* subdirs[] = {
        L"\\limine",
        L"\\resources\\limine",
        NULL
    };
    
    for (int i = 0; subdirs[i]; i++) {
        swprintf(sourcePath, size, L"%s%s", exeDir, subdirs[i]);
        
        // 检查 limine.exe 或 limine-bios.sys
        WCHAR testFile[MAX_PATH];
        swprintf(testFile, MAX_PATH, L"%s\\limine.exe", sourcePath);
        if (GetFileAttributesW(testFile) != INVALID_FILE_ATTRIBUTES) {
            return true;
        }
        
        swprintf(testFile, MAX_PATH, L"%s\\limine-bios.sys", sourcePath);
        if (GetFileAttributesW(testFile) != INVALID_FILE_ATTRIBUTES) {
            return true;
        }
    }
    
    return false;
}

// 使用 limine.exe bios-install 安装 Limine 到 MBR
bool MBR_InstallLimine(int diskNumber, const WCHAR* limineSource, bool installFiles, WCHAR* error, DWORD errorSize) {
    WCHAR sourcePath[MAX_PATH] = {0};
    
    // 如果没有指定源路径，自动查找
    if (!limineSource || wcslen(limineSource) == 0) {
        if (!FindLimineSource(sourcePath, MAX_PATH)) {
            if (error) wcscpy(error, L"未找到 Limine 源文件，请在程序目录下创建 limine 文件夹");
            return false;
        }
        limineSource = sourcePath;
    }
    
    // 检查 limine.exe 是否存在
    WCHAR limineExe[MAX_PATH];
    swprintf(limineExe, MAX_PATH, L"%s\\limine.exe", limineSource);
    
    if (GetFileAttributesW(limineExe) == INVALID_FILE_ATTRIBUTES) {
        // 没有 limine.exe，检查是否有 limine-mbr.bin（旧版兼容）
        WCHAR mbrPath[MAX_PATH];
        swprintf(mbrPath, MAX_PATH, L"%s\\limine-mbr.bin", limineSource);
        
        if (GetFileAttributesW(mbrPath) != INVALID_FILE_ATTRIBUTES) {
            // 使用旧的 MBR bin 文件方式
            DWORD bootCodeSize = 0;
            BYTE* bootCode = LoadBootCodeFromFile(mbrPath, &bootCodeSize, error, errorSize);
            if (!bootCode) {
                return false;
            }
            
            HANDLE hDisk = OpenDisk(diskNumber, GENERIC_READ | GENERIC_WRITE);
            if (hDisk == INVALID_HANDLE_VALUE) {
                free(bootCode);
                if (error) swprintf(error, errorSize, L"无法打开磁盘 %d", diskNumber);
                return false;
            }
            
            BYTE mbr[SECTOR_SIZE];
            DWORD bytes = 0;
            if (!ReadFile(hDisk, mbr, SECTOR_SIZE, &bytes, NULL) || bytes != SECTOR_SIZE) {
                CloseHandle(hDisk);
                free(bootCode);
                if (error) wcscpy(error, L"读取 MBR 失败");
                return false;
            }
            
            DWORD copySize = (bootCodeSize < MBR_BOOT_CODE_SIZE) ? bootCodeSize : MBR_BOOT_CODE_SIZE;
            memcpy(mbr, bootCode, copySize);
            free(bootCode);
            
            mbr[0x1FE] = 0x55;
            mbr[0x1FF] = 0xAA;
            
            SetFilePointer(hDisk, 0, NULL, FILE_BEGIN);
            BOOL ok = WriteFile(hDisk, mbr, SECTOR_SIZE, &bytes, NULL) && bytes == SECTOR_SIZE;
            CloseHandle(hDisk);
            
            if (!ok) {
                if (error) wcscpy(error, L"写入 MBR 失败");
                return false;
            }
        } else {
            if (error) swprintf(error, errorSize, L"未找到 limine.exe 或 limine-mbr.bin");
            return false;
        }
    } else {
        // 使用 limine.exe bios-install 命令（推荐方式）
        // 构建命令行: limine.exe bios-install --force \\.\PhysicalDriveN
        WCHAR cmdLine[MAX_PATH * 2];
        swprintf(cmdLine, MAX_PATH * 2, L"\"%s\" bios-install --force \\\\.\\PhysicalDrive%d", 
                 limineExe, diskNumber);
        
        // 创建管道捕获输出
        SECURITY_ATTRIBUTES sa = {sizeof(SECURITY_ATTRIBUTES), NULL, TRUE};
        HANDLE hReadPipe = NULL, hWritePipe = NULL;
        CreatePipe(&hReadPipe, &hWritePipe, &sa, 4096);
        
        STARTUPINFOW si = {sizeof(si)};
        si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;
        si.hStdOutput = hWritePipe;
        si.hStdError = hWritePipe;
        PROCESS_INFORMATION pi = {0};
        
        WCHAR cmdLineCopy[MAX_PATH * 2];
        wcscpy(cmdLineCopy, cmdLine);
        
        BOOL createOk = CreateProcessW(NULL, cmdLineCopy, NULL, NULL, TRUE, 
                                       CREATE_NO_WINDOW, NULL, limineSource, &si, &pi);
        
        // 关闭写入端，避免死锁
        CloseHandle(hWritePipe);
        
        if (!createOk) {
            CloseHandle(hReadPipe);
            if (error) swprintf(error, errorSize, L"无法启动 limine.exe: 错误码 %lu", GetLastError());
            return false;
        }
        
        // 读取输出
        char outputBuf[4096] = {0};
        DWORD bytesRead = 0;
        DWORD totalRead = 0;
        char* p = outputBuf;
        while (totalRead < sizeof(outputBuf) - 1) {
            if (!ReadFile(hReadPipe, p, sizeof(outputBuf) - 1 - totalRead, &bytesRead, NULL) || bytesRead == 0) {
                break;
            }
            totalRead += bytesRead;
            p += bytesRead;
        }
        outputBuf[totalRead] = '\0';
        CloseHandle(hReadPipe);
        
        // 等待进程完成
        WaitForSingleObject(pi.hProcess, 30000);  // 30秒超时
        
        DWORD exitCode = 0;
        GetExitCodeProcess(pi.hProcess, &exitCode);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        
        if (exitCode != 0) {
            // 转换输出到宽字符并返回错误
            WCHAR outputW[2048] = {0};
            MultiByteToWideChar(CP_UTF8, 0, outputBuf, -1, outputW, 2048);
            
            // 截取最后一行作为错误信息
            WCHAR* lastLine = wcsrchr(outputW, L'\n');
            if (lastLine && lastLine[1]) {
                wcscpy(outputW, lastLine + 1);
            }
            // 移除换行符
            WCHAR* nl = wcschr(outputW, L'\n');
            if (nl) *nl = L'\0';
            WCHAR* cr = wcschr(outputW, L'\r');
            if (cr) *cr = L'\0';
            
            if (outputW[0]) {
                swprintf(error, errorSize, L"%s", outputW);
            } else {
                swprintf(error, errorSize, L"Limine 安装失败，退出码: %lu", exitCode);
            }
            return false;
        }
    }
    
    // 如果需要安装文件到活动分区
    if (installFiles) {
        // 获取活动分区
        int activePart = MBR_GetActivePartition(diskNumber);
        if (activePart == 0) {
            if (error) wcscpy(error, L"没有活动分区，无法安装 Limine 文件");
            return false;
        }
        
        // 获取活动分区的盘符
        MBR_DISK_INFO info = {0};
        if (MBR_GetDiskInfo(diskNumber, &info, NULL, 0)) {
            for (int i = 0; i < info.partitionCount; i++) {
                if (info.partitions[i].partitionNumber == activePart && info.partitions[i].driveLetter != 0) {
                    WCHAR targetDir[MAX_PATH];
                    WCHAR srcFile[MAX_PATH];
                    WCHAR destFile[MAX_PATH];
                    
                    // 创建 boot\limine 目录
                    swprintf(targetDir, MAX_PATH, L"%c:\\boot\\limine", info.partitions[i].driveLetter);
                    CreateDirectoryW(targetDir, NULL);
                    
                    // 复制 limine-bios.sys
                    swprintf(srcFile, MAX_PATH, L"%s\\limine-bios.sys", limineSource);
                    swprintf(destFile, MAX_PATH, L"%c:\\boot\\limine\\limine.sys", info.partitions[i].driveLetter);
                    if (GetFileAttributesW(srcFile) != INVALID_FILE_ATTRIBUTES) {
                        CopyFileW(srcFile, destFile, FALSE);
                    }
                    
                    // 创建或覆盖配置文件
                    swprintf(destFile, MAX_PATH, L"%c:\\boot\\limine\\limine.conf", info.partitions[i].driveLetter);
                    {
                        // 检查文件是否已存在且有内容
                        DWORD existingSize = 0;
                        HANDLE hExisting = CreateFileW(destFile, GENERIC_READ, FILE_SHARE_READ, NULL,
                            OPEN_EXISTING, 0, NULL);
                        if (hExisting != INVALID_HANDLE_VALUE) {
                            existingSize = GetFileSize(hExisting, NULL);
                            CloseHandle(hExisting);
                        }
                        
                        // 如果文件不存在或为空，创建配置
                        if (existingSize == 0) {
                            HANDLE hConf = CreateFileW(destFile, GENERIC_WRITE, 0, NULL,
                                CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
                            if (hConf != INVALID_HANDLE_VALUE) {
                                // MBR 模式使用 bios 协议
                                const char* defaultConf = 
                                    "# Limine Configuration\n"
                                    "# Auto-generated by Boot Manager Pro\n\n"
                                    "timeout: 5\n\n"
                                    "/Windows\n"
                                    "    protocol: bios\n"
                                    "    path: /bootmgr\n\n";
                                DWORD written;
                                WriteFile(hConf, defaultConf, (DWORD)strlen(defaultConf), &written, NULL);
                                CloseHandle(hConf);
                            }
                        }
                    }
                    
                    break;
                }
            }
            if (info.partitions) free(info.partitions);
        }
    }
    
    return true;
}

bool MBR_InstallGrub4Dos(int diskNumber, const WCHAR* grubSource, WCHAR* error, DWORD errorSize) {
    // GRUB4DOS 需要单独的 g4d_mbr.bin 文件
    if (!grubSource || wcslen(grubSource) == 0) {
        if (error) wcscpy(error, L"需要指定 GRUB4DOS 源文件路径");
        return false;
    }
    
    WCHAR mbrPath[MAX_PATH];
    swprintf(mbrPath, MAX_PATH, L"%s\\g4d_mbr.bin", grubSource);
    
    DWORD bootCodeSize = 0;
    BYTE* bootCode = LoadBootCodeFromFile(mbrPath, &bootCodeSize, error, errorSize);
    
    if (!bootCode) {
        return false;
    }
    
    HANDLE hDisk = OpenDisk(diskNumber, GENERIC_READ | GENERIC_WRITE);
    if (hDisk == INVALID_HANDLE_VALUE) {
        free(bootCode);
        if (error) swprintf(error, errorSize, L"无法打开磁盘 %d", diskNumber);
        return false;
    }
    
    BYTE mbr[SECTOR_SIZE];
    DWORD bytes = 0;
    if (!ReadFile(hDisk, mbr, SECTOR_SIZE, &bytes, NULL) || bytes != SECTOR_SIZE) {
        CloseHandle(hDisk);
        free(bootCode);
        if (error) wcscpy(error, L"读取 MBR 失败");
        return false;
    }

    DWORD copySize = (bootCodeSize < MBR_BOOT_CODE_SIZE) ? bootCodeSize : MBR_BOOT_CODE_SIZE;
    memcpy(mbr, bootCode, copySize);
    free(bootCode);
    
    mbr[0x1FE] = 0x55;
    mbr[0x1FF] = 0xAA;
    
    SetFilePointer(hDisk, 0, NULL, FILE_BEGIN);
    BOOL ok = WriteFile(hDisk, mbr, SECTOR_SIZE, &bytes, NULL) && bytes == SECTOR_SIZE;
    CloseHandle(hDisk);
    
    if (!ok) {
        if (error) wcscpy(error, L"写入 MBR 失败");
        return false;
    }
    
    return true;
}

bool MBR_UninstallLimine(int diskNumber, WCHAR* error, DWORD errorSize) {
    // 恢复 Windows MBR
    return MBR_RepairWindows(diskNumber, error, errorSize);
}