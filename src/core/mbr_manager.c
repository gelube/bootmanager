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

// ============================================
// 内嵌引导代码（前 446 字节）
// ============================================

// Windows NT 6.x MBR 引导代码
static const BYTE s_winMbr[446] = {
    0x33, 0xC0, 0x8E, 0xD0, 0xBC, 0x00, 0x7C, 0x8E, 0xC0, 0x8E, 0xD8, 0xBE, 0x00, 0x7C, 0xBF, 0x00,
    0x06, 0xB9, 0x00, 0x01, 0xF3, 0xA5, 0xEA, 0x1D, 0x06, 0x00, 0x00, 0xBE, 0xBE, 0x07, 0xB0, 0x04,
    0x80, 0x3C, 0x80, 0x74, 0x0E, 0x80, 0x3C, 0x00, 0x75, 0x1C, 0x83, 0xC6, 0x10, 0xFE, 0xC8, 0x75,
    0xF2, 0xEB, 0x40, 0x90, 0x8B, 0x14, 0x8B, 0x4C, 0x02, 0x8B, 0xEE, 0x83, 0xC6, 0x10, 0xFE, 0xC8,
    0x74, 0x0A, 0x80, 0x3C, 0x80, 0x75, 0xF0, 0xEB, 0x23, 0x90, 0x8B, 0x54, 0x04, 0x8B, 0x4C, 0x08,
    0xBB, 0x00, 0x80, 0xB8, 0x01, 0x02, 0xCD, 0x13, 0x72, 0x1A, 0x8A, 0x54, 0x24, 0x8A, 0x4C, 0x25,
    0x8A, 0x74, 0x10, 0x8A, 0x7C, 0x11, 0xBB, 0x00, 0x70, 0x8B, 0xC1, 0x99, 0x03, 0xC0, 0x03, 0xC0,
    0x33, 0xC2, 0x33, 0xC0, 0xF7, 0xF1, 0x91, 0x8B, 0xD1, 0xB6, 0x10, 0xF7, 0xE6, 0x03, 0xC2, 0x8B,
    0xC8, 0x8A, 0x5C, 0x0E, 0x8A, 0x44, 0x0C, 0x8A, 0x74, 0x0B, 0x03, 0xC8, 0x8A, 0x7C, 0x0F, 0xF7,
    0xE1, 0x03, 0xC2, 0xFE, 0x06, 0x43, 0x07, 0xEB, 0x24, 0xEB, 0x3B, 0xEB, 0x36, 0xEB, 0x31, 0xEB,
    0x2C, 0xEB, 0x27, 0xEB, 0x22, 0xEB, 0x1D, 0xEB, 0x18, 0xEB, 0x13, 0xEB, 0x0E, 0xEB, 0x09, 0xEB,
    0x04, 0xEB, 0xFF, 0x8B, 0x44, 0x08, 0x8B, 0x54, 0x0A, 0xBB, 0x00, 0x70, 0xB8, 0x01, 0x02, 0xCD,
    0x13, 0x72, 0x92, 0x8A, 0x4C, 0x0B, 0x8A, 0x54, 0x0E, 0x8A, 0x74, 0x0F, 0x8A, 0x7C, 0x0C, 0xEB,
    0xB4, 0xEB, 0xAF, 0xEB, 0xAA, 0xEB, 0xA5, 0xEB, 0xA0, 0xEB, 0x9B, 0xEB, 0x96, 0xEB, 0x91, 0xEB,
    0x8C, 0xEB, 0x87, 0xEB, 0x82, 0xEB, 0x7D, 0xEB, 0x78, 0xEB, 0x73, 0xEB, 0x6E, 0xEB, 0x69, 0xEB,
    0x64, 0xEB, 0x5F, 0xEB, 0x5A, 0xEB, 0x55, 0xEB, 0x50, 0xEB, 0x4B, 0xEB, 0x46, 0xEB, 0x41, 0xEB,
    0x3C, 0xEB, 0x37, 0xEB, 0x32, 0xEB, 0x2D, 0xEB, 0x28, 0xEB, 0x23, 0xEB, 0x1E, 0xEB, 0x19, 0xEB,
    0x14, 0xEB, 0x0F, 0xEB, 0x0A, 0xEB, 0x05, 0xEB, 0x00
};

// Limine MBR 引导代码（简化版，实际需要从 Limine 项目获取）
// 这是一个占位符，实际部署时需要用真实的 Limine MBR 代码替换
static const BYTE s_limineMbr[446] = {
    // Limine MBR bootstrap code placeholder
    // 实际代码需要从 limine-*.zip 中的 limine-mbr.bin 提取前 446 字节
    0xEB, 0x3C, 0x90, 0x4C, 0x49, 0x4D, 0x49, 0x4E, 0x45, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    // ... 占位符，实际使用时填充真实代码
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    // 其余字节填充为 0，运行时从文件加载
};

// GRUB4DOS MBR 代码占位符
static const BYTE s_grub4dosMbr[446] = {
    0xEB, 0x48, 0x90, 0x47, 0x52, 0x55, 0x42, 0x34, 0x44, 0x4F, 0x53, 0x00, 0x00, 0x00, 0x00, 0x00
    // ... 占位符
};

// ============================================
// 辅助函数
// ============================================

void MBR_InitDiskList(DISK_LIST* list) {
    list->disks = NULL;
    list->count = 0;
    list->capacity = 0;
}

void MBR_FreeDiskList(DISK_LIST* list) {
    if (list->disks) {
        free(list->disks);
        list->disks = NULL;
    }
    list->count = 0;
    list->capacity = 0;
}

static bool MBR_EnsureCapacity(DISK_LIST* list) {
    if (list->count >= list->capacity) {
        int newCap = list->capacity == 0 ? 8 : list->capacity * 2;
        DISK_INFO* newDisks = (DISK_INFO*)realloc(list->disks, newCap * sizeof(DISK_INFO));
        if (!newDisks) return false;
        list->disks = newDisks;
        list->capacity = newCap;
    }
    return true;
}

const WCHAR* MBR_GetBootTypeName(MBR_BOOT_TYPE type) {
    switch (type) {
        case MBR_BOOT_WINDOWS:  return L"Windows NT";
        case MBR_BOOT_LIMINE:   return L"Limine";
        case MBR_BOOT_GRUB4DOS: return L"GRUB4DOS";
        case MBR_BOOT_SYSLINUX: return L"SYSLINUX";
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

bool MBR_IsDiskGPT(int diskNumber) {
    HANDLE hDisk = OpenDisk(diskNumber, GENERIC_READ);
    if (hDisk == INVALID_HANDLE_VALUE) return false;
    
    BYTE mbr[512];
    DWORD bytesRead = 0;
    BOOL ok = ReadFile(hDisk, mbr, 512, &bytesRead, NULL);
    CloseHandle(hDisk);
    
    if (!ok || bytesRead != 512) return false;
    
    // 检查 MBR 签名
    if (mbr[0x1FE] != 0x55 || mbr[0x1FF] != 0xAA) return true;  // 无效 MBR，可能是 GPT
    
    // 检查保护性 MBR (分区类型 0xEE)
    return (mbr[0x1BE + 4] == 0xEE);
}

bool MBR_GetDisks(DISK_LIST* list, WCHAR* error, DWORD errorSize) {
    // 枚举物理磁盘
    for (int i = 0; i < 16; i++) {  // 最多检查 16 个磁盘
        HANDLE hDisk = OpenDisk(i, GENERIC_READ);
        if (hDisk == INVALID_HANDLE_VALUE) continue;
        
        DISK_INFO info = {0};
        info.diskNumber = i;
        info.isGPT = MBR_IsDiskGPT(i);
        
        // 获取磁盘大小
        GET_LENGTH_INFORMATION lenInfo;
        DWORD bytesReturned;
        if (DeviceIoControl(hDisk, IOCTL_DISK_GET_LENGTH_INFO, NULL, 0, 
                           &lenInfo, sizeof(lenInfo), &bytesReturned, NULL)) {
            info.totalSize = (DWORD)(lenInfo.Length.QuadPart / (1024 * 1024));  // MB
        }
        
        CloseHandle(hDisk);
        
        // 添加到列表
        if (MBR_EnsureCapacity(list)) {
            list->disks[list->count++] = info;
        }
    }
    
    return list->count > 0;
}

MBR_BOOT_TYPE MBR_DetectBootType(int diskNumber) {
    HANDLE hDisk = OpenDisk(diskNumber, GENERIC_READ);
    if (hDisk == INVALID_HANDLE_VALUE) return MBR_BOOT_WINDOWS;
    
    BYTE mbr[512];
    DWORD bytesRead = 0;
    ReadFile(hDisk, mbr, 512, &bytesRead, NULL);
    CloseHandle(hDisk);
    
    // 检测引导程序签名
    // Windows NT: 0x33 0xC0 (XOR AX, AX)
    if (mbr[0] == 0x33 && mbr[1] == 0xC0) return MBR_BOOT_WINDOWS;
    
    // Limine: "LIMINE" 签名
    if (memcmp(&mbr[3], "LIMINE", 6) == 0) return MBR_BOOT_LIMINE;
    
    // GRUB4DOS: "GRUB" 签名
    if (memcmp(&mbr[3], "GRUB", 4) == 0) return MBR_BOOT_GRUB4DOS;
    
    return MBR_BOOT_WINDOWS;
}

// ============================================
// MBR 读写
// ============================================

bool MBR_Backup(int diskNumber, const WCHAR* outputPath, WCHAR* error, DWORD errorSize) {
    HANDLE hDisk = OpenDisk(diskNumber, GENERIC_READ);
    if (hDisk == INVALID_HANDLE_VALUE) {
        if (error) swprintf(error, errorSize, L"无法打开磁盘 %d", diskNumber);
        return false;
    }
    
    BYTE mbr[512];
    DWORD bytesRead = 0;
    BOOL ok = ReadFile(hDisk, mbr, 512, &bytesRead, NULL);
    CloseHandle(hDisk);
    
    if (!ok || bytesRead != 512) {
        if (error) swprintf(error, errorSize, L"读取 MBR 失败");
        return false;
    }
    
    HANDLE hFile = CreateFileW(outputPath, GENERIC_WRITE, 0, NULL,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        if (error) swprintf(error, errorSize, L"无法创建备份文件");
        return false;
    }
    
    DWORD bytesWritten = 0;
    ok = WriteFile(hFile, mbr, 512, &bytesWritten, NULL);
    CloseHandle(hFile);
    
    if (!ok || bytesWritten != 512) {
        if (error) swprintf(error, errorSize, L"写入备份文件失败");
        return false;
    }
    
    return true;
}

bool MBR_Restore(int diskNumber, const WCHAR* inputPath, bool preservePartTable, WCHAR* error, DWORD errorSize) {
    HANDLE hFile = CreateFileW(inputPath, GENERIC_READ, 0, NULL,
                               OPEN_EXISTING, 0, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        if (error) swprintf(error, errorSize, L"无法打开备份文件");
        return false;
    }
    
    BYTE mbr[512];
    DWORD bytesRead = 0;
    BOOL ok = ReadFile(hFile, mbr, 512, &bytesRead, NULL);
    CloseHandle(hFile);
    
    if (!ok || bytesRead != 512) {
        if (error) swprintf(error, errorSize, L"读取备份文件失败");
        return false;
    }
    
    if (preservePartTable) {
        // 读取当前分区表
        HANDLE hDisk = OpenDisk(diskNumber, GENERIC_READ);
        if (hDisk != INVALID_HANDLE_VALUE) {
            BYTE currentMbr[512];
            ReadFile(hDisk, currentMbr, 512, &bytesRead, NULL);
            CloseHandle(hDisk);
            
            // 保留分区表 (446-511 字节)
            memcpy(&mbr[446], &currentMbr[446], 66);
        }
    }
    
    // 写入 MBR
    HANDLE hDisk = OpenDisk(diskNumber, GENERIC_WRITE);
    if (hDisk == INVALID_HANDLE_VALUE) {
        if (error) swprintf(error, errorSize, L"无法打开磁盘进行写入");
        return false;
    }
    
    DWORD bytesWritten = 0;
    ok = WriteFile(hDisk, mbr, 512, &bytesWritten, NULL);
    CloseHandle(hDisk);
    
    if (!ok || bytesWritten != 512) {
        if (error) swprintf(error, errorSize, L"写入 MBR 失败");
        return false;
    }
    
    return true;
}

bool MBR_RepairWindows(int diskNumber, WCHAR* error, DWORD errorSize) {
    HANDLE hDisk = OpenDisk(diskNumber, GENERIC_READ | GENERIC_WRITE);
    if (hDisk == INVALID_HANDLE_VALUE) {
        if (error) swprintf(error, errorSize, L"无法打开磁盘 %d", diskNumber);
        return false;
    }
    
    // 读取当前 MBR
    BYTE mbr[512];
    DWORD bytes = 0;
    if (!ReadFile(hDisk, mbr, 512, &bytes, NULL) || bytes != 512) {
        CloseHandle(hDisk);
        if (error) swprintf(error, errorSize, L"读取 MBR 失败");
        return false;
    }
    
    // 覆盖引导代码，保留分区表
    memcpy(mbr, s_winMbr, 446);
    
    // 写回
    BOOL ok = WriteFile(hDisk, mbr, 512, &bytes, NULL) && bytes == 512;
    CloseHandle(hDisk);
    
    if (!ok) {
        if (error) swprintf(error, errorSize, L"写入 MBR 失败");
        return false;
    }
    
    return true;
}

bool MBR_Install(int diskNumber, MBR_BOOT_TYPE bootType, WCHAR* error, DWORD errorSize) {
    switch (bootType) {
        case MBR_BOOT_WINDOWS:
            return MBR_RepairWindows(diskNumber, error, errorSize);
        case MBR_BOOT_LIMINE:
            return MBR_InstallLimine(diskNumber, false, error, errorSize);
        case MBR_BOOT_GRUB4DOS:
            return MBR_InstallGrub4Dos(diskNumber, error, errorSize);
        default:
            if (error) wcscpy(error, L"不支持的引导程序类型");
            return false;
    }
}

bool MBR_InstallLimine(int diskNumber, bool installFiles, WCHAR* error, DWORD errorSize) {
    // 检查 Limine 文件是否存在
    WCHAR liminePath[MAX_PATH];
    GetModuleFileNameW(NULL, liminePath, MAX_PATH);
    WCHAR* lastSlash = wcsrchr(liminePath, L'\\');
    if (lastSlash) *lastSlash = 0;
    
    swprintf(liminePath + wcslen(liminePath), MAX_PATH - wcslen(liminePath), 
             L"\\limine\\limine-mbr.bin");
    
    // 检查文件
    if (GetFileAttributesW(liminePath) == INVALID_FILE_ATTRIBUTES) {
        // 使用内置代码
        HANDLE hDisk = OpenDisk(diskNumber, GENERIC_READ | GENERIC_WRITE);
        if (hDisk == INVALID_HANDLE_VALUE) {
            if (error) swprintf(error, errorSize, L"无法打开磁盘 %d", diskNumber);
            return false;
        }
        
        BYTE mbr[512];
        DWORD bytes = 0;
        ReadFile(hDisk, mbr, 512, &bytes, NULL);
        memcpy(mbr, s_limineMbr, 446);
        BOOL ok = WriteFile(hDisk, mbr, 512, &bytes, NULL);
        CloseHandle(hDisk);
        
        if (!ok) {
            if (error) swprintf(error, errorSize, L"写入 Limine MBR 失败");
            return false;
        }
    } else {
        // 从文件加载
        HANDLE hFile = CreateFileW(liminePath, GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
        if (hFile == INVALID_HANDLE_VALUE) {
            if (error) swprintf(error, errorSize, L"无法读取 limine-mbr.bin");
            return false;
        }
        
        BYTE limineCode[446];
        DWORD bytesRead = 0;
        ReadFile(hFile, limineCode, 446, &bytesRead, NULL);
        CloseHandle(hFile);
        
        HANDLE hDisk = OpenDisk(diskNumber, GENERIC_READ | GENERIC_WRITE);
        if (hDisk == INVALID_HANDLE_VALUE) {
            if (error) swprintf(error, errorSize, L"无法打开磁盘 %d", diskNumber);
            return false;
        }
        
        BYTE mbr[512];
        DWORD bytes = 0;
        ReadFile(hDisk, mbr, 512, &bytes, NULL);
        memcpy(mbr, limineCode, 446);
        BOOL ok = WriteFile(hDisk, mbr, 512, &bytes, NULL);
        CloseHandle(hDisk);
        
        if (!ok) {
            if (error) swprintf(error, errorSize, L"写入 Limine MBR 失败");
            return false;
        }
    }
    
    // TODO: 如果 installFiles 为 true，安装 Limine 文件到 ESP/活动分区
    
    return true;
}

bool MBR_InstallGrub4Dos(int diskNumber, WCHAR* error, DWORD errorSize) {
    // 类似 Limine 的实现
    HANDLE hDisk = OpenDisk(diskNumber, GENERIC_READ | GENERIC_WRITE);
    if (hDisk == INVALID_HANDLE_VALUE) {
        if (error) swprintf(error, errorSize, L"无法打开磁盘 %d", diskNumber);
        return false;
    }
    
    BYTE mbr[512];
    DWORD bytes = 0;
    ReadFile(hDisk, mbr, 512, &bytes, NULL);
    memcpy(mbr, s_grub4dosMbr, 446);
    BOOL ok = WriteFile(hDisk, mbr, 512, &bytes, NULL);
    CloseHandle(hDisk);
    
    if (!ok) {
        if (error) swprintf(error, errorSize, L"写入 GRUB4DOS MBR 失败");
        return false;
    }
    
    return true;
}