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
// 只复制文件到 \EFI\refind\，通过 NVRAM 启动项引导
// 不覆盖 \EFI\Boot\bootx64.efi
// ============================================
BOOL RefindInstall(const WCHAR* sourcePath, const WCHAR* espDrive)
{
    WCHAR autoEsp[4] = {0};
    const WCHAR* targetEsp = NULL;
    WCHAR srcFile[MAX_PATH];
    WCHAR destRefindDir[MAX_PATH];

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

    // 创建目录
    if (!CreateDirectoryRecursive(destRefindDir)) {
        RefindSetError(L"创建目标目录失败");
        if (targetEsp == autoEsp) RefindUnmountESP(autoEsp);
        return FALSE;
    }

    // 复制 rEFInd 文件
    if (!CopyDirectoryContents(sourcePath, destRefindDir)) {
        RefindSetError(L"复制 rEFInd 文件失败");
        if (targetEsp == autoEsp) RefindUnmountESP(autoEsp);
        return FALSE;
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
// 只删除 \EFI\refind\ 目录和 NVRAM 启动项
// 不影响 \EFI\Boot\bootx64.efi
// ============================================
BOOL RefindUninstall(const WCHAR* espDrive)
{
    WCHAR refindDir[MAX_PATH];
    
    if (!espDrive || wcslen(espDrive) < 1) return FALSE;
    
    g_refindLastError[0] = L'\0';
    
    // 删除 EFI\refind 目录
    swprintf(refindDir, MAX_PATH, L"%s\\EFI\\refind", espDrive);
    if (GetFileAttributesW(refindDir) != INVALID_FILE_ATTRIBUTES) {
        // 使用 cmd 删除目录（更可靠）
        STARTUPINFOW si = {0};
        PROCESS_INFORMATION pi = {0};
        WCHAR cmd[MAX_PATH];
        swprintf(cmd, MAX_PATH, L"cmd.exe /c rmdir /s /q \"%s\"", refindDir);
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;
        if (CreateProcessW(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
            WaitForSingleObject(pi.hProcess, 5000);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
        }
    }

    // 从 NVRAM 删除 rEFInd 启动项
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
            
            // 删除找到的 rEFInd 启动项
            for (DWORD i = 0; i < refindCount; i++) {
                UefiNvramDeleteBootEntry(refindBoots[i]);
            }
            
            // 重建 BootOrder（排除已删除的项）
            if (refindCount > 0) {
                UINT16* newOrder = (UINT16*)malloc((bo.Count - refindCount) * sizeof(UINT16));
                if (newOrder) {
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

const WCHAR* RefindGetLastErrorMessage(void)
{
    return g_refindLastError;
}