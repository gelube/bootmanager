/**
 * rEFInd Installation Module
 * 负责将 rEFInd 安装到 ESP 分区
 */

#include "refind.h"
#include "uefi.h"
#include <wchar.h>
#include <shlobj.h>
#include <fileapi.h>
#include <direct.h>

#pragma comment(lib, "shell32.lib")

// ============================================
// 执行命令并等待完成
// ============================================
static BOOL RunCommand(const WCHAR* cmd, const WCHAR* params)
{
    SHELLEXECUTEINFOW sei = {0};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.lpVerb = L"runas";
    sei.lpFile = cmd;
    sei.lpParameters = params;
    sei.nShow = SW_HIDE;
    
    if (ShellExecuteExW(&sei)) {
        WaitForSingleObject(sei.hProcess, 30000);
        DWORD exitCode = 0;
        GetExitCodeProcess(sei.hProcess, &exitCode);
        CloseHandle(sei.hProcess);
        return (exitCode == 0);
    }
    return FALSE;
}

// ============================================
// 递归创建目录
// ============================================
static BOOL CreateDirectoryRecursive(const WCHAR* path)
{
    WCHAR temp[MAX_PATH];
    wcsncpy(temp, path, MAX_PATH);
    
    for (WCHAR* p = temp + 3; *p; p++) {
        if (*p == L'\\') {
            *p = L'\0';
            CreateDirectoryW(temp, NULL);
            *p = L'\\';
        }
    }
    
    return CreateDirectoryW(temp, NULL) || GetLastError() == ERROR_ALREADY_EXISTS;
}

// ============================================
// 复制目录内容
// ============================================
static BOOL CopyDirectoryContents(const WCHAR* srcDir, const WCHAR* destDir)
{
    WCHAR findPath[MAX_PATH];
    WIN32_FIND_DATAW findData;
    
    // 创建目标目录
    CreateDirectoryRecursive(destDir);
    
    // 构建搜索路径
    swprintf(findPath, MAX_PATH, L"%s\\*.*", srcDir);
    
    HANDLE hFind = FindFirstFileW(findPath, &findData);
    if (hFind == INVALID_HANDLE_VALUE) {
        return FALSE;
    }
    
    BOOL success = TRUE;
    
    do {
        if (wcscmp(findData.cFileName, L".") == 0 ||
            wcscmp(findData.cFileName, L"..") == 0) {
            continue;
        }
        
        WCHAR srcFile[MAX_PATH];
        WCHAR destFile[MAX_PATH];
        
        swprintf(srcFile, MAX_PATH, L"%s\\%s", srcDir, findData.cFileName);
        swprintf(destFile, MAX_PATH, L"%s\\%s", destDir, findData.cFileName);
        
        if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            // 递归复制子目录
            if (!CopyDirectoryContents(srcFile, destFile)) {
                success = FALSE;
            }
        } else {
            // 复制文件
            if (!CopyFileW(srcFile, destFile, FALSE)) {
                // 如果复制失败，尝试先删除目标文件
                DeleteFileW(destFile);
                if (!CopyFileW(srcFile, destFile, FALSE)) {
                    success = FALSE;
                }
            }
        }
    } while (FindNextFileW(hFind, &findData));
    
    FindClose(hFind);
    return success;
}

// ============================================
// 递归删除目录
// ============================================
static BOOL DeleteDirectoryRecursive(const WCHAR* path)
{
    WCHAR findPath[MAX_PATH];
    WIN32_FIND_DATAW findData;
    
    swprintf(findPath, MAX_PATH, L"%s\\*.*", path);
    
    HANDLE hFind = FindFirstFileW(findPath, &findData);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (wcscmp(findData.cFileName, L".") == 0 ||
                wcscmp(findData.cFileName, L"..") == 0) {
                continue;
            }
            
            WCHAR filePath[MAX_PATH];
            swprintf(filePath, MAX_PATH, L"%s\\%s", path, findData.cFileName);
            
            if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                DeleteDirectoryRecursive(filePath);
            } else {
                DeleteFileW(filePath);
            }
        } while (FindNextFileW(hFind, &findData));
        
        FindClose(hFind);
    }
    
    return RemoveDirectoryW(path);
}

// ============================================
// 检查 rEFInd 是否已安装
// ============================================
BOOL RefindIsInstalled(const WCHAR* espDrive)
{
    if (!espDrive) return FALSE;
    
    WCHAR refindPath[MAX_PATH];
    swprintf(refindPath, MAX_PATH, L"%s\\EFI\\refind\\refind_x64.efi", espDrive);
    
    return (GetFileAttributesW(refindPath) != INVALID_FILE_ATTRIBUTES);
}

// ============================================
// 挂载 ESP 分区（如果没有驱动器号，自动分配）
// ============================================
BOOL RefindMountESP(WCHAR* driveLetter, DWORD size)
{
    if (!driveLetter || size < 4) return FALSE;
    
    // 方法1: 尝试查找已挂载的 ESP
    for (WCHAR d = L'C'; d <= L'Z'; d++) {
        WCHAR root[4] = {d, L':', L'\\', 0};
        
        UINT type = GetDriveTypeW(root);
        if (type != DRIVE_FIXED && type != DRIVE_REMOVABLE) {
            continue;
        }
        
        WCHAR fsName[64] = {0};
        GetVolumeInformationW(root, NULL, 0, NULL, NULL, NULL, fsName, 64);
        
        if (_wcsicmp(fsName, L"FAT32") == 0 || _wcsicmp(fsName, L"FAT") == 0) {
            WCHAR efiPath[MAX_PATH];
            swprintf(efiPath, MAX_PATH, L"%s\\EFI", root);
            
            if (GetFileAttributesW(efiPath) != INVALID_FILE_ATTRIBUTES) {
                wcsncpy(driveLetter, root, size);
                driveLetter[2] = L'\0';
                return TRUE;
            }
        }
    }
    
    // 方法2: 使用 mountvol 挂载 ESP 分区
    // 先找一个可用的驱动器号
    WCHAR availableDrive = 0;
    for (WCHAR d = L'S'; d >= L'C'; d--) {
        WCHAR root[4] = {d, L':', L'\\', 0};
        if (GetDriveTypeW(root) == DRIVE_NO_ROOT_DIR) {
            availableDrive = d;
            break;
        }
    }
    
    if (availableDrive == 0) {
        return FALSE;
    }
    
    // 使用 mountvol 挂载 ESP
    // mountvol S: /S  (挂载 EFI 系统分区)
    WCHAR cmd[MAX_PATH];
    swprintf(cmd, MAX_PATH, L"/c mountvol %c: /S", availableDrive);
    
    SHELLEXECUTEINFOW sei = {0};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.lpVerb = L"runas";
    sei.lpFile = L"cmd.exe";
    sei.lpParameters = cmd;
    sei.nShow = SW_HIDE;
    
    if (ShellExecuteExW(&sei)) {
        WaitForSingleObject(sei.hProcess, 10000);
        CloseHandle(sei.hProcess);
        
        // 检查是否挂载成功
        WCHAR root[4] = {availableDrive, L':', L'\\', 0};
        if (GetDriveTypeW(root) != DRIVE_NO_ROOT_DIR) {
            swprintf(driveLetter, size, L"%c:", availableDrive);
            return TRUE;
        }
    }
    
    return FALSE;
}

// ============================================
// 查找 ESP 分区（自动挂载）
// ============================================
BOOL RefindFindESP(WCHAR* driveLetter, DWORD size)
{
    return RefindMountESP(driveLetter, size);
}

// ============================================
// 安装 rEFInd
// ============================================
BOOL RefindInstall(const WCHAR* sourcePath, const WCHAR* espDrive)
{
    WCHAR actualEsp[MAX_PATH] = {0};
    const WCHAR* targetEsp = espDrive;
    
    // 如果没有指定 ESP，自动查找并挂载
    if (!espDrive || wcslen(espDrive) == 0) {
        if (!RefindMountESP(actualEsp, MAX_PATH)) {
            return FALSE;
        }
        targetEsp = actualEsp;
    }
    
    if (!sourcePath || !targetEsp) return FALSE;
    
    // 检查源文件是否存在
    WCHAR srcEfi[MAX_PATH];
    swprintf(srcEfi, MAX_PATH, L"%s\\refind_x64.efi", sourcePath);
    
    if (GetFileAttributesW(srcEfi) == INVALID_FILE_ATTRIBUTES) {
        // 尝试其他可能的路径
        swprintf(srcEfi, MAX_PATH, L"%s\\refind\\refind_x64.efi", sourcePath);
        if (GetFileAttributesW(srcEfi) == INVALID_FILE_ATTRIBUTES) {
            return FALSE;
        }
    }
    
    // 创建目标目录
    WCHAR destRefindDir[MAX_PATH];
    WCHAR destBootDir[MAX_PATH];
    
    swprintf(destRefindDir, MAX_PATH, L"%s\\EFI\\refind", espDrive);
    swprintf(destBootDir, MAX_PATH, L"%s\\EFI\\Boot", espDrive);
    
    // 备份原始 bootx64.efi
    WCHAR backupPath[MAX_PATH];
    WCHAR bootxEfi[MAX_PATH];
    swprintf(bootxEfi, MAX_PATH, L"%s\\bootx64.efi", destBootDir);
    swprintf(backupPath, MAX_PATH, L"%s\\bootx64.efi.bak", destBootDir);
    
    if (GetFileAttributesW(bootxEfi) != INVALID_FILE_ATTRIBUTES) {
        // 备份已存在，先删除旧备份
        if (GetFileAttributesW(backupPath) != INVALID_FILE_ATTRIBUTES) {
            DeleteFileW(backupPath);
        }
        CopyFileW(bootxEfi, backupPath, FALSE);
    }
    
    // 复制 rEFInd 文件到 ESP
    if (!CopyDirectoryContents(sourcePath, destRefindDir)) {
        // 如果复制失败，尝试其他路径
        WCHAR altSrc[MAX_PATH];
        swprintf(altSrc, MAX_PATH, L"%s\\refind", sourcePath);
        if (!CopyDirectoryContents(altSrc, destRefindDir)) {
            return FALSE;
        }
    }
    
    // 确保 Boot 目录存在
    CreateDirectoryRecursive(destBootDir);
    
    // 将 refind_x64.efi 复制为 bootx64.efi
    WCHAR refindEfi[MAX_PATH];
    swprintf(refindEfi, MAX_PATH, L"%s\\refind_x64.efi", destRefindDir);
    
    if (!CopyFileW(refindEfi, bootxEfi, FALSE)) {
        DeleteFileW(bootxEfi);
        if (!CopyFileW(refindEfi, bootxEfi, FALSE)) {
            return FALSE;
        }
    }
    
    // 添加 NVRAM 启动项
    WCHAR nvramPath[MAX_PATH];
    swprintf(nvramPath, MAX_PATH, L"\\EFI\\refind\\refind_x64.efi");
    RefindAddNVRAMEntry(L"rEFInd Boot Manager", nvramPath);
    
    return TRUE;
}

// ============================================
// 卸载 rEFInd
// ============================================
BOOL RefindUninstall(const WCHAR* espDrive)
{
    WCHAR actualEsp[MAX_PATH] = {0};
    const WCHAR* targetEsp = espDrive;
    
    // 如果没有指定 ESP，自动查找并挂载
    if (!espDrive || wcslen(espDrive) == 0) {
        if (!RefindMountESP(actualEsp, MAX_PATH)) {
            return FALSE;
        }
        targetEsp = actualEsp;
    }
    
    if (!targetEsp) return FALSE;
    
    WCHAR destBootDir[MAX_PATH];
    WCHAR destRefindDir[MAX_PATH];
    
    swprintf(destBootDir, MAX_PATH, L"%s\\EFI\\Boot", targetEsp);
    swprintf(destRefindDir, MAX_PATH, L"%s\\EFI\\refind", targetEsp);
    
    // 恢复原始 bootx64.efi
    WCHAR backupPath[MAX_PATH];
    WCHAR bootxEfi[MAX_PATH];
    
    swprintf(bootxEfi, MAX_PATH, L"%s\\bootx64.efi", destBootDir);
    swprintf(backupPath, MAX_PATH, L"%s\\bootx64.efi.bak", destBootDir);
    
    if (GetFileAttributesW(backupPath) != INVALID_FILE_ATTRIBUTES) {
        // 删除 rEFInd 的 bootx64.efi
        DeleteFileW(bootxEfi);
        
        // 恢复备份
        CopyFileW(backupPath, bootxEfi, FALSE);
        
        // 删除备份文件
        DeleteFileW(backupPath);
    } else {
        // 没有备份，尝试恢复 Windows 引导
        // 删除 rEFInd 的 bootx64.efi
        DeleteFileW(bootxEfi);
        
        // 尝试从 Windows EFI 恢复
        WCHAR windowsEfi[MAX_PATH];
        swprintf(windowsEfi, MAX_PATH, L"%s\\EFI\\Microsoft\\Boot\\bootmgfw.efi", targetEsp);
        
        if (GetFileAttributesW(windowsEfi) != INVALID_FILE_ATTRIBUTES) {
            CopyFileW(windowsEfi, bootxEfi, FALSE);
        }
    }
    
    // 删除 rEFInd 目录
    if (GetFileAttributesW(destRefindDir) != INVALID_FILE_ATTRIBUTES) {
        DeleteDirectoryRecursive(destRefindDir);
    }
    
    // 移除 NVRAM 启动项
    RefindRemoveNVRAMEntry(L"rEFInd Boot Manager");
    
    return TRUE;
}

// ============================================
// 添加 NVRAM 启动项
// ============================================
BOOL RefindAddNVRAMEntry(const WCHAR* description, const WCHAR* path)
{
    if (!description || !path) return FALSE;
    
    // 使用 bcdedit 添加启动项
    // bcdedit /create /d "rEFInd" /application osloader
    WCHAR cmd[1024];
    
    // 创建启动项
    swprintf(cmd, 1024, L"/c bcdedit /create /d \"%s\" /application osloader", description);
    
    SHELLEXECUTEINFOW sei = {0};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.lpVerb = L"runas";
    sei.lpFile = L"cmd.exe";
    sei.lpParameters = cmd;
    sei.nShow = SW_HIDE;
    
    if (ShellExecuteExW(&sei)) {
        WaitForSingleObject(sei.hProcess, 10000);
        CloseHandle(sei.hProcess);
    }
    
    return TRUE;
}

// ============================================
// 移除 NVRAM 启动项
// ============================================
BOOL RefindRemoveNVRAMEntry(const WCHAR* description)
{
    if (!description) return FALSE;
    
    // 需要先查找 GUID，然后删除
    // 简化处理：使用 bcdedit /enum 找到后删除
    // 这里只返回 TRUE，实际删除逻辑需要解析 bcdedit 输出
    (void)description;
    return TRUE;
}

// ============================================
// 备份 bootx64.efi
// ============================================
BOOL RefindBackupBootx64(const WCHAR* espDrive, WCHAR* backupPath, DWORD size)
{
    if (!espDrive || !backupPath) return FALSE;
    
    WCHAR srcPath[MAX_PATH];
    WCHAR dstPath[MAX_PATH];
    
    swprintf(srcPath, MAX_PATH, L"%s\\EFI\\Boot\\bootx64.efi", espDrive);
    swprintf(dstPath, MAX_PATH, L"%s\\EFI\\Boot\\bootx64.efi.bak", espDrive);
    
    if (GetFileAttributesW(srcPath) == INVALID_FILE_ATTRIBUTES) {
        return FALSE;
    }
    
    if (CopyFileW(srcPath, dstPath, FALSE)) {
        wcsncpy(backupPath, dstPath, size);
        return TRUE;
    }
    
    return FALSE;
}

// ============================================
// 恢复 bootx64.efi
// ============================================
BOOL RefindRestoreBootx64(const WCHAR* espDrive, const WCHAR* backupPath)
{
    if (!espDrive || !backupPath) return FALSE;
    
    WCHAR dstPath[MAX_PATH];
    swprintf(dstPath, MAX_PATH, L"%s\\EFI\\Boot\\bootx64.efi", espDrive);
    
    if (GetFileAttributesW(backupPath) == INVALID_FILE_ATTRIBUTES) {
        return FALSE;
    }
    
    return CopyFileW(backupPath, dstPath, FALSE);
}