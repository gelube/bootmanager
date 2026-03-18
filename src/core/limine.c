/**
 * limine.c - Limine Bootloader Management Implementation
 * 
 * 支持 BIOS/MBR 和 UEFI 两种安装模式
 */

#include "../../include/limine.h"
#include "../../include/esp.h"
#include <wchar.h>
#include <stdlib.h>
#include <string.h>

// 最大磁盘和分区数量
#define MAX_DISKS 64
#define MAX_PARTITIONS 128
#define SECTOR_SIZE 512

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
        CopyFileW(srcFile, destFile, FALSE);
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
    WCHAR bootDir[MAX_PATH];
    WCHAR srcFile[MAX_PATH];
    WCHAR destFile[MAX_PATH];
    
    if (!espDrive || !limineSource) {
        SetError(L"参数无效");
        return FALSE;
    }
    
    // 创建 EFI 目录结构
    swprintf(efiDir, MAX_PATH, L"%s\\EFI", espDrive);
    swprintf(limineDir, MAX_PATH, L"%s\\EFI\\limine", espDrive);
    swprintf(bootDir, MAX_PATH, L"%s\\EFI\\Boot", espDrive);
    
    CreateDirectoryW(efiDir, NULL);
    CreateDirectoryW(limineDir, NULL);
    CreateDirectoryW(bootDir, NULL);
    
    // 复制 limine-efi 目录
    WCHAR srcEfiDir[MAX_PATH];
    swprintf(srcEfiDir, MAX_PATH, L"%s\\limine-efi", limineSource);
    if (GetFileAttributesW(srcEfiDir) != INVALID_FILE_ATTRIBUTES) {
        CopyDirContents(srcEfiDir, limineDir);
    }
    
    // 复制 BOOTX64.EFI
    swprintf(srcFile, MAX_PATH, L"%s\\BOOTX64.EFI", srcEfiDir);
    swprintf(destFile, MAX_PATH, L"%s\\BOOTX64.EFI", limineDir);
    if (GetFileAttributesW(srcFile) != INVALID_FILE_ATTRIBUTES) {
        CopyFileW(srcFile, destFile, FALSE);
    }
    
    // 备份并替换 EFI\Boot\BOOTX64.EFI
    swprintf(destFile, MAX_PATH, L"%s\\BOOTX64.EFI", bootDir);
    WCHAR backupPath[MAX_PATH];
    swprintf(backupPath, MAX_PATH, L"%s\\BOOTX64.EFI.bak", bootDir);
    
    if (GetFileAttributesW(destFile) != INVALID_FILE_ATTRIBUTES) {
        if (GetFileAttributesW(backupPath) == INVALID_FILE_ATTRIBUTES) {
            CopyFileW(destFile, backupPath, FALSE);
        }
    }
    
    if (GetFileAttributesW(srcFile) != INVALID_FILE_ATTRIBUTES) {
        CopyFileW(srcFile, destFile, FALSE);
    }
    
    // 创建配置文件
    swprintf(destFile, MAX_PATH, L"%s\\limine.conf", limineDir);
    if (GetFileAttributesW(destFile) == INVALID_FILE_ATTRIBUTES) {
        HANDLE hFile = CreateFileW(destFile, GENERIC_WRITE, 0, NULL,
            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile != INVALID_HANDLE_VALUE) {
            const char* defaultConf = 
                "# Limine Configuration\n"
                "# Auto-generated by Boot Manager Pro\n\n"
                "timeout: 5\n\n"
                "/Windows Boot Manager\n"
                "    protocol: efi_chain\n"
                "    path: /EFI/Microsoft/Boot/bootmgfw.efi\n\n";
            DWORD written;
            WriteFile(hFile, defaultConf, (DWORD)strlen(defaultConf), &written, NULL);
            CloseHandle(hFile);
        }
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
    
    // 尝试恢复 EFI\Boot\BOOTX64.EFI 备份
    WCHAR bootxEfi[MAX_PATH];
    WCHAR backupPath[MAX_PATH];
    swprintf(bootxEfi, MAX_PATH, L"%s\\EFI\\Boot\\BOOTX64.EFI", drive);
    swprintf(backupPath, MAX_PATH, L"%s\\EFI\\Boot\\BOOTX64.EFI.bak", drive);
    if (GetFileAttributesW(backupPath) != INVALID_FILE_ATTRIBUTES) {
        DeleteFileW(bootxEfi);
        CopyFileW(backupPath, bootxEfi, FALSE);
        DeleteFileW(backupPath);
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