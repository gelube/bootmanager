/**
 * limine.c - Limine Bootloader Management Implementation
 * 
 * 支持 BIOS/MBR 和 UEFI 两种安装模式
 */

#include "../../include/limine.h"
#include "../../include/esp.h"
#include "../../include/uefi_nvram.h"
#include <wchar.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// 最大磁盘和分区数量
#define MAX_DISKS 64
#define MAX_PARTITIONS 128
#define SECTOR_SIZE 512
#define MAX_BOOT_ENTRIES 32

// 启动项结构
typedef struct {
    WCHAR name[128];
    WCHAR path[MAX_PATH];
    WCHAR protocol[32];  // efi, linux, chain
    int priority;        // 排序优先级
} BOOT_ENTRY;

// ============================================
// 自动扫描 EFI 文件并生成配置
// ============================================

// 检查 EFI 文件是否存在
static BOOL EfiFileExists(const WCHAR* drive, const WCHAR* efiPath)
{
    WCHAR fullPath[MAX_PATH];
    swprintf(fullPath, MAX_PATH, L"%s%s", drive, efiPath);
    return (GetFileAttributesW(fullPath) != INVALID_FILE_ATTRIBUTES);
}

// 扫描 ESP 分区上的 EFI 文件
static int ScanEfiFiles(const WCHAR* espDrive, BOOT_ENTRY* entries, int maxEntries)
{
    int count = 0;
    
    // Windows Boot Manager
    if (count < maxEntries && EfiFileExists(espDrive, L"\\EFI\\Microsoft\\Boot\\bootmgfw.efi")) {
        wcscpy(entries[count].name, L"Windows Boot Manager");
        wcscpy(entries[count].path, L"boot():/EFI/Microsoft/Boot/bootmgfw.efi");
        wcscpy(entries[count].protocol, L"efi");
        entries[count].priority = 1;
        count++;
    }
    
    // Linux 发行版 (按优先级排序)
    struct {
        const WCHAR* path;
        const WCHAR* name;
        int priority;
    } linuxDistros[] = {
        { L"\\EFI\\ubuntu\\grubx64.efi", L"Ubuntu", 10 },
        { L"\\EFI\\ubuntu\\shimx64.efi", L"Ubuntu (Secure Boot)", 11 },
        { L"\\EFI\\fedora\\grubx64.efi", L"Fedora", 12 },
        { L"\\EFI\\fedora\\shimx64.efi", L"Fedora (Secure Boot)", 13 },
        { L"\\EFI\\debian\\grubx64.efi", L"Debian", 14 },
        { L"\\EFI\\debian\\shimx64.efi", L"Debian (Secure Boot)", 15 },
        { L"\\EFI\\arch\\grubx64.efi", L"Arch Linux", 16 },
        { L"\\EFI\\opensuse\\grubx64.efi", L"openSUSE", 17 },
        { L"\\EFI\\centos\\grubx64.efi", L"CentOS", 18 },
        { L"\\EFI\\grub\\grubx64.efi", L"GRUB", 20 },
        { L"\\EFI\\grub\\shimx64.efi", L"GRUB (Secure Boot)", 21 },
        { NULL, NULL, 0 }
    };
    
    for (int i = 0; linuxDistros[i].path && count < maxEntries; i++) {
        if (EfiFileExists(espDrive, linuxDistros[i].path)) {
            wcscpy(entries[count].name, linuxDistros[i].name);
            swprintf(entries[count].path, MAX_PATH, L"boot():%s", linuxDistros[i].path);
            wcscpy(entries[count].protocol, L"efi");
            entries[count].priority = linuxDistros[i].priority;
            count++;
        }
    }
    
    // rEFInd
    if (count < maxEntries && EfiFileExists(espDrive, L"\\EFI\\refind\\refind_x64.efi")) {
        wcscpy(entries[count].name, L"rEFInd Boot Manager");
        wcscpy(entries[count].path, L"boot():/EFI/refind/refind_x64.efi");
        wcscpy(entries[count].protocol, L"efi");
        entries[count].priority = 30;
        count++;
    }
    
    // systemd-boot
    if (count < maxEntries && EfiFileExists(espDrive, L"\\EFI\\systemd\\systemd-bootx64.efi")) {
        wcscpy(entries[count].name, L"systemd-boot");
        wcscpy(entries[count].path, L"boot():/EFI/systemd/systemd-bootx64.efi");
        wcscpy(entries[count].protocol, L"efi");
        entries[count].priority = 31;
        count++;
    }
    
    // 其他 EFI 文件 (扫描 EFI 目录下的其他目录)
    WCHAR searchPath[MAX_PATH];
    WIN32_FIND_DATAW findData;
    
    swprintf(searchPath, MAX_PATH, L"%s\\EFI\\*.*", espDrive);
    HANDLE hFind = FindFirstFileW(searchPath, &findData);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (!(findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
            if (wcscmp(findData.cFileName, L".") == 0 || wcscmp(findData.cFileName, L"..") == 0) continue;
            
            // 跳过已处理的目录
            if (_wcsicmp(findData.cFileName, L"Microsoft") == 0 ||
                _wcsicmp(findData.cFileName, L"Boot") == 0 ||
                _wcsicmp(findData.cFileName, L"boot") == 0 ||
                _wcsicmp(findData.cFileName, L"ubuntu") == 0 ||
                _wcsicmp(findData.cFileName, L"fedora") == 0 ||
                _wcsicmp(findData.cFileName, L"debian") == 0 ||
                _wcsicmp(findData.cFileName, L"arch") == 0 ||
                _wcsicmp(findData.cFileName, L"opensuse") == 0 ||
                _wcsicmp(findData.cFileName, L"centos") == 0 ||
                _wcsicmp(findData.cFileName, L"grub") == 0 ||
                _wcsicmp(findData.cFileName, L"refind") == 0 ||
                _wcsicmp(findData.cFileName, L"systemd") == 0 ||
                _wcsicmp(findData.cFileName, L"limine") == 0) {
                continue;
            }
            
            // 检查是否有 EFI 文件
            WCHAR efiPath[MAX_PATH];
            swprintf(efiPath, MAX_PATH, L"%s\\EFI\\%s\\%s", espDrive, findData.cFileName, findData.cFileName);
            
            // 尝试常见的 EFI 文件名
            const WCHAR* efiFiles[] = {
                L"bootx64.efi", L"BOOTX64.EFI",
                L"grubx64.efi", L"GRUBX64.EFI",
                L"shimx64.efi", L"SHIMX64.EFI",
                NULL
            };
            
            for (int i = 0; efiFiles[i] && count < maxEntries; i++) {
                swprintf(efiPath, MAX_PATH, L"\\EFI\\%s\\%s", findData.cFileName, efiFiles[i]);
                if (EfiFileExists(espDrive, efiPath)) {
                    swprintf(entries[count].name, 128, L"%s", findData.cFileName);
                    swprintf(entries[count].path, MAX_PATH, L"boot():%s", efiPath);
                    wcscpy(entries[count].protocol, L"efi");
                    entries[count].priority = 50;
                    count++;
                    break;
                }
            }
        } while (FindNextFileW(hFind, &findData) && count < maxEntries);
        FindClose(hFind);
    }
    
    return count;
}

// 比较函数，用于排序
static int CompareBootEntries(const void* a, const void* b)
{
    const BOOT_ENTRY* ea = (const BOOT_ENTRY*)a;
    const BOOT_ENTRY* eb = (const BOOT_ENTRY*)b;
    return ea->priority - eb->priority;
}

// 生成 limine.conf 内容
static char* GenerateLimineConf(const WCHAR* espDrive, DWORD* outSize)
{
    BOOT_ENTRY entries[MAX_BOOT_ENTRIES];
    int count = 0;
    
    // 扫描所有 EFI 文件（包括 Windows）
    // ScanEfiFiles 会自动添加 Windows Boot Manager
    count = ScanEfiFiles(espDrive, entries, MAX_BOOT_ENTRIES);
    
    // 如果没有扫描到任何条目，添加一个默认的 Windows 条目
    if (count == 0) {
        wcscpy(entries[count].name, L"Windows Boot Manager");
        wcscpy(entries[count].path, L"boot():/EFI/Microsoft/Boot/bootmgfw.efi");
        wcscpy(entries[count].protocol, L"efi");
        entries[count].priority = 1;
        count++;
    }
    
    // 按优先级排序
    if (count > 1) {
        qsort(entries, count, sizeof(BOOT_ENTRY), CompareBootEntries);
    }
    
    // 构建配置字符串
    char* buffer = (char*)malloc(16384);  // 增大缓冲区
    if (!buffer) {
        *outSize = 0;
        return NULL;
    }
    
    char* p = buffer;
    const char* bufEnd = buffer + 16384;
    p += sprintf(p, "# Limine Configuration\n");
    p += sprintf(p, "# Auto-generated by Boot Manager Pro v3.2.0\n\n");
    p += sprintf(p, "timeout: 5\n\n");

    for (int i = 0; i < count; i++) {
        // 转换为 UTF-8
        char nameUtf8[256];
        char pathUtf8[MAX_PATH];
        char protocolUtf8[32];
        WideCharToMultiByte(CP_UTF8, 0, entries[i].name, -1, nameUtf8, 256, NULL, NULL);
        WideCharToMultiByte(CP_UTF8, 0, entries[i].path, -1, pathUtf8, MAX_PATH, NULL, NULL);
        WideCharToMultiByte(CP_UTF8, 0, entries[i].protocol, -1, protocolUtf8, 32, NULL, NULL);

        // 边界检查：单条目最多约 1.5KB，剩余空间不足则停止追加
        if (p + 260 + MAX_PATH + 64 > bufEnd) {
            break;
        }

        p += sprintf(p, "/%s\n", nameUtf8);
        p += sprintf(p, "    protocol: %s\n", protocolUtf8);
        p += sprintf(p, "    path: %s\n\n", pathUtf8);
    }
    
    *outSize = (DWORD)(p - buffer);
    return buffer;
}

// 最后的错误信息
static WCHAR s_lastError[1024] = {0};

// ============================================
// 辅助函数：设置错误信息
// ============================================
static void SetError(const WCHAR* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    _vsnwprintf(s_lastError, sizeof(s_lastError) / sizeof(WCHAR), fmt, args);
    va_end(args);
}

const WCHAR* LimineGetLastErrorMessage(void)
{
    return s_lastError;
}

// ============================================
// 辅助函数：打开物理磁盘
// ============================================
static HANDLE OpenPhysicalDrive(DWORD diskIndex, DWORD access)
{
    WCHAR path[64];
    swprintf(path, 64, L"\\\\.\\PhysicalDrive%u", diskIndex);
    return CreateFileW(path, access, FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL, OPEN_EXISTING, 0, NULL);
}

// ============================================
// 辅助函数：打开逻辑驱动器
// ============================================
static HANDLE OpenLogicalDrive(WCHAR driveLetter, DWORD access)
{
    WCHAR path[8];
    swprintf(path, 8, L"\\\\.\\%c:", driveLetter);
    return CreateFileW(path, access, FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL, OPEN_EXISTING, 0, NULL);
}

// ============================================
// 格式化磁盘大小
// ============================================
void LimineFormatSize(DWORD64 bytes, WCHAR* buffer, DWORD size)
{
    if (bytes >= 1024ULL * 1024 * 1024 * 1024) {
        swprintf(buffer, size, L"%.1f TB", (double)bytes / (1024.0 * 1024 * 1024 * 1024));
    } else if (bytes >= 1024ULL * 1024 * 1024) {
        swprintf(buffer, size, L"%.1f GB", (double)bytes / (1024.0 * 1024 * 1024));
    } else if (bytes >= 1024ULL * 1024) {
        swprintf(buffer, size, L"%.1f MB", (double)bytes / (1024.0 * 1024));
    } else if (bytes >= 1024) {
        swprintf(buffer, size, L"%.1f KB", (double)bytes / 1024.0);
    } else {
        swprintf(buffer, size, L"%llu B", bytes);
    }
}

// ============================================
// 检查 Limine 是否已安装
// ============================================
LIMINE_STATUS LimineCheckInstalled(const WCHAR* drive)
{
    WCHAR path[MAX_PATH];
    
    if (!drive || wcslen(drive) < 1) return LIMINE_NOT_INSTALLED;
    
    // 检查 EFI\limine 目录 (UEFI)
    swprintf(path, MAX_PATH, L"%s\\EFI\\limine\\BOOTX64.EFI", drive);
    if (GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES) {
        return LIMINE_INSTALLED_UEFI;
    }
    
    // 检查 boot\limine 目录 (MBR/PBR)
    swprintf(path, MAX_PATH, L"%s\\boot\\limine\\limine.sys", drive);
    if (GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES) {
        return LIMINE_INSTALLED_MBR;
    }
    
    // 检查根目录的 limine.sys (PBR 模式)
    swprintf(path, MAX_PATH, L"%s\\limine.sys", drive);
    if (GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES) {
        return LIMINE_INSTALLED_PBR;
    }
    
    return LIMINE_NOT_INSTALLED;
}

// ============================================
// 智能检测 Limine 安装状态（自动挂载 ESP）
// ============================================
LIMINE_STATUS LimineCheckInstalledAuto(WCHAR* outEspDrive, DWORD outSize) {
    LIMINE_STATUS status = LIMINE_NOT_INSTALLED;
    WCHAR esp[4] = {0};
    BOOL mountedByUs = FALSE;
    
    // 1. 先检查已挂载的 ESP 分区（排除可移动介质）
    for (WCHAR d = L'C'; d <= L'Z'; d++) {
        WCHAR root[4] = {d, L':', L'\\', 0};
        
        // 排除可移动介质
        if (GetDriveTypeW(root) != DRIVE_FIXED && GetDriveTypeW(root) != DRIVE_RAMDISK) {
            continue;
        }
        
        WCHAR efiPath[MAX_PATH];
        swprintf(efiPath, MAX_PATH, L"%c:\\EFI\\limine\\BOOTX64.EFI", d);
        if (GetFileAttributesW(efiPath) != INVALID_FILE_ATTRIBUTES) {
            if (outEspDrive) swprintf(outEspDrive, outSize, L"%c:", d);
            return LIMINE_INSTALLED_UEFI;
        }
    }
    
    // 2. 检查 boot\limine（MBR 模式，不依赖 ESP）
    for (WCHAR d = L'C'; d <= L'Z'; d++) {
        WCHAR root[4] = {d, L':', L'\\', 0};
        
        // 排除可移动介质
        if (GetDriveTypeW(root) != DRIVE_FIXED) {
            continue;
        }
        
        WCHAR bootPath[MAX_PATH];
        swprintf(bootPath, MAX_PATH, L"%c:\\boot\\limine\\limine.sys", d);
        if (GetFileAttributesW(bootPath) != INVALID_FILE_ATTRIBUTES) {
            if (outEspDrive) swprintf(outEspDrive, outSize, L"%c:", d);
            return LIMINE_INSTALLED_MBR;
        }
    }
    
    // 3. 尝试挂载 ESP 检测
    if (EspMountEx(esp, 4, &mountedByUs)) {
        WCHAR liminePath[MAX_PATH];
        swprintf(liminePath, MAX_PATH, L"%s\\EFI\\limine\\BOOTX64.EFI", esp);
        
        if (GetFileAttributesW(liminePath) != INVALID_FILE_ATTRIBUTES) {
            status = LIMINE_INSTALLED_UEFI;
            if (outEspDrive) wcscpy(outEspDrive, esp);
        }
        
        // 卸载我们挂载的 ESP
        if (mountedByUs) {
            EspUnmountEx(esp, TRUE);
        }
    }
    
    return status;
}

// ============================================
// 查找 Limine 源文件
// ============================================
BOOL LimineFindSource(WCHAR* sourcePath, DWORD size)
{
    WCHAR exe[MAX_PATH];
    WCHAR* slash;
    
    if (!sourcePath || size == 0) return FALSE;
    
    GetModuleFileNameW(NULL, exe, MAX_PATH);
    slash = wcsrchr(exe, L'\\');
    if (slash) *slash = L'\0';
    
    // 尝试多个位置
    const WCHAR* subdirs[] = {
        L"\\limine",
        L"\\resources\\limine",
        L"\\..\\limine",
        NULL
    };
    
    for (int i = 0; subdirs[i]; i++) {
        swprintf(sourcePath, size, L"%s%s", exe, subdirs[i]);
        
        // 检查 UEFI 文件
        WCHAR testFile[MAX_PATH];
        swprintf(testFile, MAX_PATH, L"%s\\limine-efi\\BOOTX64.EFI", sourcePath);
        if (GetFileAttributesW(testFile) != INVALID_FILE_ATTRIBUTES) {
            return TRUE;
        }
        
        // 检查 BIOS 文件
        swprintf(testFile, MAX_PATH, L"%s\\limine-bios.sys", sourcePath);
        if (GetFileAttributesW(testFile) != INVALID_FILE_ATTRIBUTES) {
            return TRUE;
        }
    }
    
    // 尝试 Z: 盘
    if (GetFileAttributesW(L"Z:\\limine\\limine-bios.sys") != INVALID_FILE_ATTRIBUTES) {
        wcsncpy(sourcePath, L"Z:\\limine", size);
        return TRUE;
    }
    
    SetError(L"未找到 Limine 源文件");
    return FALSE;
}

// ============================================
// 获取系统磁盘索引
// ============================================
DWORD LimineGetSystemDiskIndex(void)
{
    WCHAR systemDir[MAX_PATH];
    GetWindowsDirectoryW(systemDir, MAX_PATH);
    
    HANDLE hVol = OpenLogicalDrive(systemDir[0], GENERIC_READ);
    if (hVol == INVALID_HANDLE_VALUE) return 0;
    
    VOLUME_DISK_EXTENTS extents = {0};
    DWORD bytesReturned;
    DWORD diskIndex = 0;
    
    if (DeviceIoControl(hVol, IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS,
        NULL, 0, &extents, sizeof(extents), &bytesReturned, NULL)) {
        diskIndex = extents.Extents[0].DiskNumber;
    }
    
    CloseHandle(hVol);
    return diskIndex;
}

// ============================================
// 获取磁盘列表
// ============================================
LIMINE_DISK_LIST* LimineGetDiskList(void)
{
    LIMINE_DISK_LIST* list = NULL;
    LIMINE_DISK_INFO* disks = NULL;
    DWORD count = 0;
    
    disks = (LIMINE_DISK_INFO*)calloc(MAX_DISKS, sizeof(LIMINE_DISK_INFO));
    if (!disks) return NULL;
    
    for (DWORD i = 0; i < MAX_DISKS; i++) {
        HANDLE hDisk = OpenPhysicalDrive(i, GENERIC_READ);
        if (hDisk == INVALID_HANDLE_VALUE) continue;
        
        DISK_GEOMETRY geo = {0};
        if (DeviceIoControl(hDisk, IOCTL_DISK_GET_DRIVE_GEOMETRY,
            NULL, 0, &geo, sizeof(geo), &(DWORD){0}, NULL)) {
            
            disks[count].index = i;
            disks[count].size = geo.Cylinders.QuadPart * 
                geo.TracksPerCylinder * geo.SectorsPerTrack * geo.BytesPerSector;
            
            // 尝试获取磁盘布局
            DRIVE_LAYOUT_INFORMATION_EX* layout = NULL;
            DWORD layoutSize = sizeof(DRIVE_LAYOUT_INFORMATION_EX) + 
                128 * sizeof(PARTITION_INFORMATION_EX);
            layout = (DRIVE_LAYOUT_INFORMATION_EX*)malloc(layoutSize);
            
            if (layout && DeviceIoControl(hDisk, IOCTL_DISK_GET_DRIVE_LAYOUT_EX,
                NULL, 0, layout, layoutSize, &(DWORD){0}, NULL)) {
                disks[count].partitionCount = layout->PartitionCount;
                disks[count].isGpt = (layout->PartitionStyle == PARTITION_STYLE_GPT);
                free(layout);
            }
            
            count++;
        }
        
        CloseHandle(hDisk);
    }
    
    if (count == 0) {
        free(disks);
        return NULL;
    }
    
    list = (LIMINE_DISK_LIST*)calloc(1, sizeof(LIMINE_DISK_LIST));
    if (!list) {
        free(disks);
        return NULL;
    }
    
    list->disks = disks;
    list->count = count;
    
    // 标记系统磁盘
    DWORD sysDisk = LimineGetSystemDiskIndex();
    for (DWORD i = 0; i < count; i++) {
        if (disks[i].index == sysDisk) {
            disks[i].isSystem = TRUE;
            break;
        }
    }
    
    return list;
}

// ============================================
// 释放磁盘列表
// ============================================
void LimineFreeDiskList(LIMINE_DISK_LIST* list)
{
    if (!list) return;
    if (list->disks) free(list->disks);
    free(list);
}

// ============================================
// 备份 PBR
// ============================================
BOOL LimineBackupPBR(const WCHAR* drive, const WCHAR* outputPath)
{
    HANDLE hVol;
    BYTE pbr[SECTOR_SIZE];
    DWORD bytesRead, bytesWritten;
    BOOL result = FALSE;
    
    if (!drive || !outputPath) return FALSE;
    
    hVol = OpenLogicalDrive(drive[0], GENERIC_READ);
    if (hVol == INVALID_HANDLE_VALUE) {
        SetError(L"无法打开驱动器 %c:", drive[0]);
        return FALSE;
    }
    
    if (ReadFile(hVol, pbr, SECTOR_SIZE, &bytesRead, NULL) && bytesRead == SECTOR_SIZE) {
        HANDLE hOut = CreateFileW(outputPath, GENERIC_WRITE, 0, NULL,
            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hOut != INVALID_HANDLE_VALUE) {
            if (WriteFile(hOut, pbr, SECTOR_SIZE, &bytesWritten, NULL) && 
                bytesWritten == SECTOR_SIZE) {
                result = TRUE;
            }
            CloseHandle(hOut);
        }
    }
    
    CloseHandle(hVol);
    if (!result) SetError(L"PBR 备份失败");
    return result;
}

// ============================================
// 恢复 PBR
// ============================================
BOOL LimineRestorePBR(const WCHAR* drive, const WCHAR* backupPath)
{
    HANDLE hVol, hIn;
    BYTE pbr[SECTOR_SIZE];
    DWORD bytesRead, bytesWritten;
    BOOL result = FALSE;
    
    if (!drive || !backupPath) return FALSE;
    
    hIn = CreateFileW(backupPath, GENERIC_READ, FILE_SHARE_READ, NULL,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hIn == INVALID_HANDLE_VALUE) {
        SetError(L"无法打开备份文件");
        return FALSE;
    }
    
    if (ReadFile(hIn, pbr, SECTOR_SIZE, &bytesRead, NULL) && bytesRead == SECTOR_SIZE) {
        hVol = OpenLogicalDrive(drive[0], GENERIC_WRITE);
        if (hVol != INVALID_HANDLE_VALUE) {
            DWORD bytesReturned;
            DeviceIoControl(hVol, FSCTL_LOCK_VOLUME, NULL, 0, NULL, 0, &bytesReturned, NULL);
            
            if (WriteFile(hVol, pbr, SECTOR_SIZE, &bytesWritten, NULL) && 
                bytesWritten == SECTOR_SIZE) {
                result = TRUE;
            }
            
            DeviceIoControl(hVol, FSCTL_UNLOCK_VOLUME, NULL, 0, NULL, 0, &bytesReturned, NULL);
            CloseHandle(hVol);
        }
    }
    
    CloseHandle(hIn);
    if (!result) SetError(L"PBR 恢复失败");
    return result;
}

// ============================================
// 备份 MBR（完整）
// ============================================
BOOL LimineBackupMBRFull(DWORD diskIndex, const WCHAR* outputPath)
{
    HANDLE hDisk, hOut;
    BYTE mbr[SECTOR_SIZE];
    DWORD bytesRead, bytesWritten;
    BOOL result = FALSE;
    
    hDisk = OpenPhysicalDrive(diskIndex, GENERIC_READ);
    if (hDisk == INVALID_HANDLE_VALUE) {
        SetError(L"无法打开磁盘 %u", diskIndex);
        return FALSE;
    }
    
    if (ReadFile(hDisk, mbr, SECTOR_SIZE, &bytesRead, NULL) && bytesRead == SECTOR_SIZE) {
        hOut = CreateFileW(outputPath, GENERIC_WRITE, 0, NULL,
            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hOut != INVALID_HANDLE_VALUE) {
            if (WriteFile(hOut, mbr, SECTOR_SIZE, &bytesWritten, NULL) && 
                bytesWritten == SECTOR_SIZE) {
                result = TRUE;
            }
            CloseHandle(hOut);
        }
    }
    
    CloseHandle(hDisk);
    if (!result) SetError(L"MBR 备份失败");
    return result;
}

// ============================================
// 复制目录内容
// ============================================
static BOOL CopyDirContents(const WCHAR* srcDir, const WCHAR* destDir)
{
    WCHAR findPath[MAX_PATH];
    WCHAR srcFile[MAX_PATH];
    WCHAR destFile[MAX_PATH];
    WIN32_FIND_DATAW fd;
    HANDLE hFind;
    BOOL success = TRUE;
    
    CreateDirectoryW(destDir, NULL);
    
    swprintf(findPath, MAX_PATH, L"%s\\*", srcDir);
    hFind = FindFirstFileW(findPath, &fd);
    if (hFind == INVALID_HANDLE_VALUE) return FALSE;
    
    do {
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0)
            continue;
        
        swprintf(srcFile, MAX_PATH, L"%s\\%s", srcDir, fd.cFileName);
        swprintf(destFile, MAX_PATH, L"%s\\%s", destDir, fd.cFileName);
        
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (!CopyDirContents(srcFile, destFile)) success = FALSE;
        } else {
            if (!CopyFileW(srcFile, destFile, FALSE)) {
                DeleteFileW(destFile);
                if (!CopyFileW(srcFile, destFile, FALSE)) success = FALSE;
            }
        }
    } while (FindNextFileW(hFind, &fd));
    
    FindClose(hFind);
    return success;
}

// ============================================
// 安装 Limine 到 MBR (BIOS 模式)
// ============================================
BOOL LimineInstallToMBR(DWORD diskIndex, const WCHAR* limineSource)
{
    WCHAR targetDrive[MAX_PATH] = {0};
    WCHAR bootDir[MAX_PATH];
    WCHAR limineDir[MAX_PATH];
    WCHAR srcFile[MAX_PATH];
    WCHAR destFile[MAX_PATH];
    
    if (!limineSource) {
        SetError(L"Limine 源路径为空");
        return FALSE;
    }
    
    // 获取磁盘的活动分区
    HANDLE hDisk = OpenPhysicalDrive(diskIndex, GENERIC_READ);
    if (hDisk == INVALID_HANDLE_VALUE) {
        SetError(L"无法打开磁盘 %u", diskIndex);
        return FALSE;
    }
    
    // 查找活动分区或第一个有盘符的分区
    WCHAR drives[512] = {0};
    if (GetLogicalDriveStringsW(511, drives)) {
        WCHAR* p = drives;
        while (*p) {
            HANDLE hVol = OpenLogicalDrive(p[0], GENERIC_READ);
            if (hVol != INVALID_HANDLE_VALUE) {
                VOLUME_DISK_EXTENTS extents = {0};
                DWORD bytesReturned;
                if (DeviceIoControl(hVol, IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS,
                    NULL, 0, &extents, sizeof(extents), &bytesReturned, NULL)) {
                    if (extents.Extents[0].DiskNumber == diskIndex) {
                        swprintf(targetDrive, MAX_PATH, L"%c:", p[0]);
                        CloseHandle(hVol);
                        break;
                    }
                }
                CloseHandle(hVol);
            }
            p += wcslen(p) + 1;
        }
    }
    CloseHandle(hDisk);
    
    if (targetDrive[0] == 0) {
        SetError(L"无法确定目标分区");
        return FALSE;
    }
    
    // 创建 boot/limine 目录
    swprintf(bootDir, MAX_PATH, L"%s\\boot", targetDrive);
    swprintf(limineDir, MAX_PATH, L"%s\\boot\\limine", targetDrive);
    CreateDirectoryW(bootDir, NULL);
    CreateDirectoryW(limineDir, NULL);
    
    // 复制 limine 文件
    if (!CopyDirContents(limineSource, limineDir)) {
        SetError(L"复制 Limine 文件失败");
        return FALSE;
    }
    
    // 复制 limine-bios.sys
    swprintf(srcFile, MAX_PATH, L"%s\\limine-bios.sys", limineSource);
    swprintf(destFile, MAX_PATH, L"%s\\limine-bios.sys", limineDir);
    if (GetFileAttributesW(srcFile) != INVALID_FILE_ATTRIBUTES) {
        if (!CopyFileW(srcFile, destFile, FALSE)) {
            SetError(L"复制 limine-bios.sys 失败");
            return FALSE;
        }
    }
    
    // 创建默认配置文件
    WCHAR configFile[MAX_PATH];
    swprintf(configFile, MAX_PATH, L"%s\\limine.conf", limineDir);
    if (GetFileAttributesW(configFile) == INVALID_FILE_ATTRIBUTES) {
        HANDLE hFile = CreateFileW(configFile, GENERIC_WRITE, 0, NULL,
            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile != INVALID_HANDLE_VALUE) {
            const char* defaultConf = 
                "# Limine Configuration\n"
                "# Auto-generated by Boot Manager Pro\n\n"
                "timeout: 5\n\n"
                "/Windows\n"
                "    protocol: chain\n"
                "    drive: boot\n"
                "    partition: boot\n"
                "    path: /bootmgr\n\n";
            DWORD written;
            WriteFile(hFile, defaultConf, (DWORD)strlen(defaultConf), &written, NULL);
            CloseHandle(hFile);
        }
    }
    
    return TRUE;
}

// ============================================
// 安装 Limine 到 ESP (UEFI 模式)
// ============================================
BOOL LimineInstallToUEFI(const WCHAR* espDrive, const WCHAR* limineSource)
{
    WCHAR efiDir[MAX_PATH];
    WCHAR limineDir[MAX_PATH];
    WCHAR srcFile[MAX_PATH];
    WCHAR destFile[MAX_PATH];
    
    if (!espDrive || !limineSource) {
        SetError(L"参数无效");
        return FALSE;
    }
    
    // 创建 EFI 目录结构
    swprintf(efiDir, MAX_PATH, L"%s\\EFI", espDrive);
    swprintf(limineDir, MAX_PATH, L"%s\\EFI\\limine", espDrive);
    CreateDirectoryW(efiDir, NULL);
    CreateDirectoryW(limineDir, NULL);
    
    // 复制 limine-efi 目录内容
    WCHAR srcEfiDir[MAX_PATH];
    swprintf(srcEfiDir, MAX_PATH, L"%s\\limine-efi", limineSource);
    if (GetFileAttributesW(srcEfiDir) != INVALID_FILE_ATTRIBUTES) {
        CopyDirContents(srcEfiDir, limineDir);
    }
    
    // 复制 BOOTX64.EFI 到 EFI\limine\
    swprintf(srcFile, MAX_PATH, L"%s\\BOOTX64.EFI", srcEfiDir);
    swprintf(destFile, MAX_PATH, L"%s\\BOOTX64.EFI", limineDir);
    if (GetFileAttributesW(srcFile) != INVALID_FILE_ATTRIBUTES) {
        if (!CopyFileW(srcFile, destFile, FALSE)) {
            SetError(L"复制 BOOTX64.EFI 失败");
            return FALSE;
        }
    }
    
    // 创建配置文件 (自动扫描系统)
    swprintf(destFile, MAX_PATH, L"%s\\limine.conf", limineDir);
    {
        DWORD confSize = 0;
        char* confContent = GenerateLimineConf(espDrive, &confSize);
        
        if (confContent && confSize > 0) {
            HANDLE hFile = CreateFileW(destFile, GENERIC_WRITE, 0, NULL,
                CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
            if (hFile != INVALID_HANDLE_VALUE) {
                DWORD written;
                WriteFile(hFile, confContent, confSize, &written, NULL);
                CloseHandle(hFile);
            }
            free(confContent);
        } else {
            // 如果扫描失败，创建基本配置
            HANDLE hFile = CreateFileW(destFile, GENERIC_WRITE, 0, NULL,
                CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
            if (hFile != INVALID_HANDLE_VALUE) {
                const char* defaultConf = 
                    "# Limine Configuration\n"
                    "# Auto-generated by Boot Manager Pro\n\n"
                    "timeout: 5\n\n";
                DWORD written;
                WriteFile(hFile, defaultConf, (DWORD)strlen(defaultConf), &written, NULL);
                CloseHandle(hFile);
            }
        }
    }
    
    // 注册到 NVRAM
    if (UefiNvramAcquirePrivilege()) {
        // 先检查是否已存在 Limine 启动项，如果存在则删除
        UEFI_BOOT_ORDER bo = {0};
        if (UefiNvramGetBootOrder(&bo) && bo.Count > 0) {
            // 找出所有 Limine 相关的启动项
            DWORD limineCount = 0;
            UINT16 limineBoots[16] = {0};
            
            for (DWORD i = 0; i < bo.Count && limineCount < 16; i++) {
                UEFI_BOOT_ENTRY* entry = UefiNvramGetBootEntry(bo.Order[i]);
                if (entry) {
                    if (wcsstr(entry->Description, L"Limine") != NULL) {
                        limineBoots[limineCount++] = bo.Order[i];
                    }
                    UefiNvramFreeEntry(entry);
                }
            }
            
            // 删除已存在的 Limine 启动项
            for (DWORD i = 0; i < limineCount; i++) {
                UefiNvramDeleteBootEntry(limineBoots[i]);
            }
            
            // 重新获取 BootOrder（删除后可能已变化）
            if (bo.Order) UefiNvramFreeBootOrder(&bo);
            UefiNvramGetBootOrder(&bo);
        }
        
        // 找空闲槽位
        UINT16 bootNum = 0x0001;
        if (bo.Count > 0) {
            BOOL used[256] = {FALSE};
            for (DWORD i = 0; i < bo.Count && i < 256; i++) {
                if (bo.Order[i] < 256) used[bo.Order[i]] = TRUE;
            }
            for (int n = 1; n < 256; n++) {
                if (!used[n]) { bootNum = (UINT16)n; break; }
            }
        }
        
        DWORD blobSize = 0;
        BYTE* blob = UefiNvramBuildLoadOption(L"Limine Boot Manager",
            L"\\EFI\\limine\\BOOTX64.EFI", LOAD_OPTION_ACTIVE, &blobSize);
        if (blob) {
            if (UefiNvramSetBootEntry(bootNum, blob, blobSize)) {
                // 添加到 BootOrder 第一位
                if (bo.Count > 0) {
                    UINT16* newOrder = (UINT16*)malloc((bo.Count + 1) * sizeof(UINT16));
                    if (newOrder) {
                        newOrder[0] = bootNum;
                        memcpy(newOrder + 1, bo.Order, bo.Count * sizeof(UINT16));
                        UefiNvramSetBootOrder(newOrder, bo.Count + 1);
                        free(newOrder);
                    }
                } else {
                    UefiNvramSetBootOrder(&bootNum, 1);
                }
            }
            free(blob);
        }
        if (bo.Order) UefiNvramFreeBootOrder(&bo);
    }
    
    return TRUE;
}

// ============================================
// 智能安装（自动检测引导模式）
// ============================================
BOOL LimineInstall(const WCHAR* limineSource)
{
    typedef BOOL (WINAPI *GetFirmwareTypeFn)(PFIRMWARE_TYPE);
    HMODULE hKernel = GetModuleHandleW(L"kernel32.dll");
    GetFirmwareTypeFn fn = NULL;
    FIRMWARE_TYPE fwType = FirmwareTypeUnknown;
    BOOL isUefi = FALSE;
    
    if (hKernel) {
        fn = (GetFirmwareTypeFn)GetProcAddress(hKernel, "GetFirmwareType");
        if (fn && fn(&fwType)) {
            isUefi = (fwType == FirmwareTypeUefi);
        }
    }
    
    if (isUefi) {
        // UEFI 模式：找到 ESP 分区
        WCHAR esp[4] = {0};
        
        // 查找已挂载的 ESP
        for (WCHAR d = L'C'; d <= L'Z'; d++) {
            WCHAR root[4] = {d, L':', L'\\', 0};
            WCHAR efiPath[MAX_PATH];
            
            if (GetDriveTypeW(root) != DRIVE_FIXED) continue;
            
            swprintf(efiPath, MAX_PATH, L"%s\\EFI", root);
            if (GetFileAttributesW(efiPath) != INVALID_FILE_ATTRIBUTES) {
                esp[0] = d;
                esp[1] = L':';
                esp[2] = 0;
                break;
            }
        }
        
        // 如果没找到，尝试使用 EspMount
        if (esp[0] == 0) {
            if (!EspMount(esp, 4)) {
                SetError(L"无法找到或挂载 ESP 分区");
                return FALSE;
            }
        }
        
        BOOL result = LimineInstallToUEFI(esp, limineSource);
        
        // 安装完成后，总是卸载 ESP（用户不应该看到 ESP 盘符）
        EspUnmount(esp);
        
        return result;
    } else {
        // BIOS/MBR 模式：安装到系统磁盘的 MBR
        DWORD diskIndex = LimineGetSystemDiskIndex();
        return LimineInstallToMBR(diskIndex, limineSource);
    }
}

// ============================================
// 卸载 Limine
// ============================================
BOOL LimineUninstall(const WCHAR* drive)
{
    WCHAR limineDir[MAX_PATH];
    WCHAR limineSys[MAX_PATH];
    
    if (!drive || wcslen(drive) < 1) return FALSE;
    
    // 删除 EFI\limine 目录 (UEFI)
    swprintf(limineDir, MAX_PATH, L"%s\\EFI\\limine", drive);
    if (GetFileAttributesW(limineDir) != INVALID_FILE_ATTRIBUTES) {
        STARTUPINFOW si = {0};
        PROCESS_INFORMATION pi = {0};
        WCHAR cmd[MAX_PATH];
        swprintf(cmd, MAX_PATH, L"cmd.exe /c rmdir /s /q \"%s\"", limineDir);
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;
        if (CreateProcessW(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
            WaitForSingleObject(pi.hProcess, 5000);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
        }
    }
    
    // 删除 boot\limine 目录 (MBR)
    swprintf(limineDir, MAX_PATH, L"%s\\boot\\limine", drive);
    if (GetFileAttributesW(limineDir) != INVALID_FILE_ATTRIBUTES) {
        STARTUPINFOW si = {0};
        PROCESS_INFORMATION pi = {0};
        WCHAR cmd[MAX_PATH];
        swprintf(cmd, MAX_PATH, L"cmd.exe /c rmdir /s /q \"%s\"", limineDir);
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;
        if (CreateProcessW(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
            WaitForSingleObject(pi.hProcess, 5000);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
        }
    }
    
    // 删除根目录的 limine.sys (PBR 模式)
    swprintf(limineSys, MAX_PATH, L"%s\\limine.sys", drive);
    if (GetFileAttributesW(limineSys) != INVALID_FILE_ATTRIBUTES) {
        DeleteFileW(limineSys);
    }
    
    // 从 NVRAM 删除 Limine 启动项
    if (UefiNvramAcquirePrivilege()) {
        UEFI_BOOT_ORDER bo = {0};
        if (UefiNvramGetBootOrder(&bo) && bo.Count > 0) {
            // 找出所有 Limine 相关的启动项编号
            DWORD limineCount = 0;
            UINT16 limineBoots[16] = {0};
            
            for (DWORD i = 0; i < bo.Count && limineCount < 16; i++) {
                UEFI_BOOT_ENTRY* entry = UefiNvramGetBootEntry(bo.Order[i]);
                if (entry) {
                    // 检查描述是否包含 "Limine"
                    if (wcsstr(entry->Description, L"Limine") != NULL) {
                        limineBoots[limineCount++] = bo.Order[i];
                    }
                    UefiNvramFreeEntry(entry);
                }
            }
            
            // 删除找到的 Limine 启动项
            for (DWORD i = 0; i < limineCount; i++) {
                UefiNvramDeleteBootEntry(limineBoots[i]);
            }
            
            // 重建 BootOrder（排除已删除的项）
            if (limineCount > 0) {
                UINT16* newOrder = (UINT16*)malloc((bo.Count - limineCount) * sizeof(UINT16));
                if (newOrder) {
                    DWORD newCount = 0;
                    for (DWORD i = 0; i < bo.Count; i++) {
                        BOOL isLimine = FALSE;
                        for (DWORD j = 0; j < limineCount; j++) {
                            if (bo.Order[i] == limineBoots[j]) {
                                isLimine = TRUE;
                                break;
                            }
                        }
                        if (!isLimine) {
                            newOrder[newCount++] = bo.Order[i];
                        }
                    }
                    if (newCount > 0) {
                        UefiNvramSetBootOrder(newOrder, newCount);
                    }
                    free(newOrder);
                }
            }
        }
        if (bo.Order) UefiNvramFreeBootOrder(&bo);
    }
    
    return TRUE;
}

// ============================================
// 获取分区列表（简化版）
// ============================================
LIMINE_PARTITION_LIST* LimineGetPartitionList(DWORD diskIndex)
{
    (void)diskIndex;
    // TODO: 实现完整的分区枚举
    return NULL;
}

LIMINE_PARTITION_LIST* LimineGetAllPartitions(void)
{
    // TODO: 实现完整的分区枚举
    return NULL;
}

void LimineFreePartitionList(LIMINE_PARTITION_LIST* list)
{
    if (!list) return;
    if (list->partitions) free(list->partitions);
    free(list);
}

// ============================================
// 设置活动分区
// ============================================
BOOL LimineSetActivePartition(DWORD diskIndex, DWORD partitionIndex)
{
    HANDLE hDisk;
    DRIVE_LAYOUT_INFORMATION_EX* layout = NULL;
    DWORD layoutSize;
    DWORD bytesReturned;
    BOOL result = FALSE;
    
    layoutSize = sizeof(DRIVE_LAYOUT_INFORMATION_EX) + 
        128 * sizeof(PARTITION_INFORMATION_EX);
    layout = (DRIVE_LAYOUT_INFORMATION_EX*)malloc(layoutSize);
    if (!layout) return FALSE;
    
    hDisk = OpenPhysicalDrive(diskIndex, GENERIC_READ | GENERIC_WRITE);
    if (hDisk == INVALID_HANDLE_VALUE) {
        free(layout);
        return FALSE;
    }
    
    if (DeviceIoControl(hDisk, IOCTL_DISK_GET_DRIVE_LAYOUT_EX,
        NULL, 0, layout, layoutSize, &bytesReturned, NULL)) {
        
        // MBR 模式下设置活动分区
        if (layout->PartitionStyle == PARTITION_STYLE_MBR) {
            // 清除所有分区的活动标志
            for (DWORD i = 0; i < layout->PartitionCount; i++) {
                layout->PartitionEntry[i].Mbr.BootIndicator = FALSE;
            }
            
            // 设置目标分区为活动
            if (partitionIndex < layout->PartitionCount) {
                layout->PartitionEntry[partitionIndex].Mbr.BootIndicator = TRUE;
                
                result = DeviceIoControl(hDisk, IOCTL_DISK_SET_DRIVE_LAYOUT_EX,
                    layout, layoutSize, NULL, 0, &bytesReturned, NULL);
            }
        }
    }
    
    free(layout);
    CloseHandle(hDisk);
    return result;
}

// ============================================
// 恢复 MBR
// ============================================
BOOL LimineRestoreMBRFull(DWORD diskIndex, const WCHAR* backupPath, BOOL restorePartitionTable)
{
    HANDLE hDisk, hIn;
    BYTE mbr[SECTOR_SIZE];
    BYTE currentMbr[SECTOR_SIZE];
    DWORD bytesRead, bytesWritten;
    BOOL result = FALSE;
    
    hIn = CreateFileW(backupPath, GENERIC_READ, FILE_SHARE_READ, NULL,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hIn == INVALID_HANDLE_VALUE) {
        SetError(L"无法打开备份文件");
        return FALSE;
    }
    
    if (ReadFile(hIn, mbr, SECTOR_SIZE, &bytesRead, NULL) && bytesRead == SECTOR_SIZE) {
        hDisk = OpenPhysicalDrive(diskIndex, GENERIC_READ | GENERIC_WRITE);
        if (hDisk != INVALID_HANDLE_VALUE) {
            if (!restorePartitionTable) {
                // 保留当前分区表
                if (ReadFile(hDisk, currentMbr, SECTOR_SIZE, &bytesRead, NULL)) {
                    memcpy(mbr + 446, currentMbr + 446, 64);  // 分区表
                    memcpy(mbr + 510, currentMbr + 510, 2);   // 签名
                }
            }
            
            if (WriteFile(hDisk, mbr, SECTOR_SIZE, &bytesWritten, NULL) && 
                bytesWritten == SECTOR_SIZE) {
                result = TRUE;
            }
            
            CloseHandle(hDisk);
        }
    }
    
    CloseHandle(hIn);
    return result;
}