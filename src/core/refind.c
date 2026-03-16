/**
 * refind.c - rEFInd 安装/卸载模块
 */

#include "refind.h"
#include "../../include/esp.h"
#include "../../include/error.h"
#include "../../include/logger.h"
#include "../../include/uefi_nvram.h"
#include <wchar.h>
#include <shlobj.h>
#include <fileapi.h>
#include <direct.h>
#include <stdarg.h>
#include <string.h>

#pragma comment(lib, "shell32.lib")

static WCHAR g_refindLastError[512] = L"";

static void RefindSetError(const WCHAR* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    _vsnwprintf(g_refindLastError, 512, fmt, args);
    va_end(args);
    g_refindLastError[511] = L'\0';
}

// 递归创建目录
static BOOL CreateDirectoryRecursive(const WCHAR* path)
{
    WCHAR temp[MAX_PATH];
    wcsncpy(temp, path, MAX_PATH);
    temp[MAX_PATH - 1] = L'\0';

    for (WCHAR* p = temp + 3; *p; p++) {
        if (*p == L'\\') {
            *p = L'\0';
            CreateDirectoryW(temp, NULL);
            *p = L'\\';
        }
    }

    return CreateDirectoryW(temp, NULL) || GetLastError() == ERROR_ALREADY_EXISTS;
}

// 复制目录内容
static BOOL CopyDirectoryContents(const WCHAR* srcDir, const WCHAR* destDir)
{
    WCHAR findPath[MAX_PATH];
    WIN32_FIND_DATAW findData;

    if (!CreateDirectoryRecursive(destDir)) {
        return FALSE;
    }

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
            if (!CopyDirectoryContents(srcFile, destFile)) {
                success = FALSE;
            }
        } else {
            if (!CopyFileW(srcFile, destFile, FALSE)) {
                // 重试一次
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

// 递归删除目录
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
// 挂载/卸载 ESP
// ============================================
BOOL RefindMountESP(WCHAR* driveLetter, DWORD size)
{
    return EspMount(driveLetter, size);
}

BOOL RefindUnmountESP(const WCHAR* driveLetter)
{
    return EspUnmount(driveLetter);
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
// 安装 rEFInd
// ============================================
BOOL RefindInstall(const WCHAR* sourcePath, const WCHAR* espDrive)
{
    WCHAR autoEsp[4] = {0};
    const WCHAR* targetEsp = NULL;
    WCHAR srcFile[MAX_PATH];
    WCHAR destRefindDir[MAX_PATH];
    WCHAR destBootDir[MAX_PATH];
    WCHAR bootxEfi[MAX_PATH];
    WCHAR backupPath[MAX_PATH];
    WCHAR refindEfi[MAX_PATH];

    g_refindLastError[0] = L'\0';

    if (!sourcePath || wcslen(sourcePath) == 0) {
        RefindSetError(L"源路径为空");
        return FALSE;
    }

    // 检查源文件
    swprintf(srcFile, MAX_PATH, L"%s\\refind_x64.efi", sourcePath);
    if (GetFileAttributesW(srcFile) == INVALID_FILE_ATTRIBUTES) {
        // 尝试 sourcePath\refind 子目录
        WCHAR altPath[MAX_PATH];
        swprintf(altPath, MAX_PATH, L"%s\\refind", sourcePath);
        swprintf(srcFile, MAX_PATH, L"%s\\refind_x64.efi", altPath);
        if (GetFileAttributesW(srcFile) == INVALID_FILE_ATTRIBUTES) {
            RefindSetError(L"未找到 refind_x64.efi");
            return FALSE;
        }
        sourcePath = altPath;
    }

    // 挂载 ESP
    if (espDrive && wcslen(espDrive) >= 2 && espDrive[1] == L':') {
        targetEsp = espDrive;
    } else {
        if (!RefindMountESP(autoEsp, 4)) {
            RefindSetError(L"挂载 ESP 分区失败");
            return FALSE;
        }
        targetEsp = autoEsp;
    }

    // 目标路径
    swprintf(destRefindDir, MAX_PATH, L"%s\\EFI\\refind", targetEsp);
    swprintf(destBootDir, MAX_PATH, L"%s\\EFI\\Boot", targetEsp);
    swprintf(bootxEfi, MAX_PATH, L"%s\\bootx64.efi", destBootDir);
    swprintf(backupPath, MAX_PATH, L"%s\\bootx64.efi.bak", destBootDir);

    // 创建目录
    if (!CreateDirectoryRecursive(destBootDir) || !CreateDirectoryRecursive(destRefindDir)) {
        RefindSetError(L"创建目标目录失败");
        if (targetEsp == autoEsp) RefindUnmountESP(autoEsp);
        return FALSE;
    }

    // 备份原有 bootx64.efi
    if (GetFileAttributesW(bootxEfi) != INVALID_FILE_ATTRIBUTES) {
        if (GetFileAttributesW(backupPath) != INVALID_FILE_ATTRIBUTES) {
            DeleteFileW(backupPath);
        }
        CopyFileW(bootxEfi, backupPath, FALSE);
    }

    // 复制 rEFInd 文件
    if (!CopyDirectoryContents(sourcePath, destRefindDir)) {
        RefindSetError(L"复制 rEFInd 文件失败");
        if (targetEsp == autoEsp) RefindUnmountESP(autoEsp);
        return FALSE;
    }

    // 复制 refind_x64.efi 到 bootx64.efi
    swprintf(refindEfi, MAX_PATH, L"%s\\refind_x64.efi", destRefindDir);
    if (!CopyFileW(refindEfi, bootxEfi, FALSE)) {
        DeleteFileW(bootxEfi);
        if (!CopyFileW(refindEfi, bootxEfi, FALSE)) {
            RefindSetError(L"设置 bootx64.efi 失败");
            if (targetEsp == autoEsp) RefindUnmountESP(autoEsp);
            return FALSE;
        }
    }

    // 注册到 NVRAM
    if (UefiNvramAcquirePrivilege()) {
        // 找空闲槽位
        UINT16 bootNum = 0x0001;
        UEFI_BOOT_ORDER bo = {0};
        if (UefiNvramGetBootOrder(&bo) && bo.Count > 0) {
            BOOL used[256] = {FALSE};
            for (DWORD i = 0; i < bo.Count && i < 256; i++) {
                if (bo.Order[i] < 256) used[bo.Order[i]] = TRUE;
            }
            for (int n = 1; n < 256; n++) {
                if (!used[n]) { bootNum = (UINT16)n; break; }
            }
        }

        DWORD blobSize = 0;
        BYTE* blob = UefiNvramBuildLoadOption(L"rEFInd Boot Manager",
            L"\\EFI\\refind\\refind_x64.efi", LOAD_OPTION_ACTIVE, &blobSize);
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

    if (targetEsp == autoEsp) RefindUnmountESP(autoEsp);
    return TRUE;
}

// ============================================
// 卸载 rEFInd
// ============================================
BOOL RefindUninstall(const WCHAR* espDrive)
{
    WCHAR autoEsp[4] = {0};
    const WCHAR* targetEsp = NULL;

    g_refindLastError[0] = L'\0';

    // 挂载 ESP
    if (espDrive && wcslen(espDrive) >= 2 && espDrive[1] == L':') {
        targetEsp = espDrive;
    } else {
        if (!RefindMountESP(autoEsp, 4)) {
            RefindSetError(L"挂载 ESP 分区失败");
            return FALSE;
        }
        targetEsp = autoEsp;
    }

    WCHAR destRefindDir[MAX_PATH];
    WCHAR destBootDir[MAX_PATH];
    WCHAR bootxEfi[MAX_PATH];
    WCHAR backupPath[MAX_PATH];
    WCHAR windowsEfi[MAX_PATH];

    swprintf(destRefindDir, MAX_PATH, L"%s\\EFI\\refind", targetEsp);
    swprintf(destBootDir, MAX_PATH, L"%s\\EFI\\Boot", targetEsp);
    swprintf(bootxEfi, MAX_PATH, L"%s\\bootx64.efi", destBootDir);
    swprintf(backupPath, MAX_PATH, L"%s\\bootx64.efi.bak", destBootDir);
    swprintf(windowsEfi, MAX_PATH, L"%s\\EFI\\Microsoft\\Boot\\bootmgfw.efi", targetEsp);

    // 1. 恢复 bootx64.efi
    BOOL restored = FALSE;
    if (GetFileAttributesW(backupPath) != INVALID_FILE_ATTRIBUTES) {
        DeleteFileW(bootxEfi);
        if (CopyFileW(backupPath, bootxEfi, FALSE)) {
            DeleteFileW(backupPath);
            restored = TRUE;
        }
    }
    if (!restored && GetFileAttributesW(windowsEfi) != INVALID_FILE_ATTRIBUTES) {
        DeleteFileW(bootxEfi);
        CopyFileW(windowsEfi, bootxEfi, FALSE);
    }

    // 2. 删除 rEFInd 目录
    if (GetFileAttributesW(destRefindDir) != INVALID_FILE_ATTRIBUTES) {
        DeleteDirectoryRecursive(destRefindDir);
    }

    // 3. 从 NVRAM 删除 rEFInd 启动项
    if (UefiNvramAcquirePrivilege()) {
        UEFI_BOOT_ORDER bo = {0};
        if (UefiNvramGetBootOrder(&bo) && bo.Count > 0) {
            // 找出所有 rEFInd 相关的启动项编号
            DWORD refindCount = 0;
            UINT16 refindBoots[16] = {0};
            
            for (DWORD i = 0; i < bo.Count && refindCount < 16; i++) {
                UEFI_BOOT_ENTRY* entry = UefiNvramGetBootEntry(bo.Order[i]);
                if (entry) {
                    if (wcsstr(entry->Description, L"rEFInd") != NULL ||
                        wcsstr(entry->Description, L"refind") != NULL) {
                        refindBoots[refindCount++] = bo.Order[i];
                    }
                    UefiNvramFreeEntry(entry);
                }
            }
            
            // 构建新的 BootOrder（排除 rEFInd）
            UINT16* newOrder = (UINT16*)malloc(bo.Count * sizeof(UINT16));
            DWORD newCount = 0;
            
            for (DWORD i = 0; i < bo.Count; i++) {
                BOOL isRefind = FALSE;
                for (DWORD j = 0; j < refindCount; j++) {
                    if (bo.Order[i] == refindBoots[j]) {
                        isRefind = TRUE;
                        break;
                    }
                }
                if (!isRefind) {
                    newOrder[newCount++] = bo.Order[i];
                }
            }
            
            // 更新 BootOrder
            if (newCount > 0) {
                UefiNvramSetBootOrder(newOrder, newCount);
            }
            free(newOrder);
            UefiNvramFreeBootOrder(&bo);
            
            // 删除 rEFInd 的 BootXXXX 变量
            for (DWORD j = 0; j < refindCount; j++) {
                UefiNvramDeleteBootEntry(refindBoots[j]);
            }
        }
    }

    if (targetEsp == autoEsp) RefindUnmountESP(autoEsp);
    return TRUE;
}

const WCHAR* RefindGetLastErrorMessage(void)
{
    return g_refindLastError;
}