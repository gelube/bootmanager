/**
 * rEFInd Installation Module
 * 负责将 rEFInd 安装到 ESP 分区
 */

#include "refind.h"
#include "../../include/esp.h"
#include "../../include/error.h"
#include "../../include/logger.h"
#include "uefi.h"
#include <wchar.h>
#include <shlobj.h>
#include <fileapi.h>
#include <direct.h>
#include <stdarg.h>

#pragma comment(lib, "shell32.lib")

static WCHAR g_refindLastError[512] = L"";

// ============================================
// 调试输出
// ============================================
static void RefindDebugLog(const WCHAR* fmt, ...)
{
    WCHAR msg[1024];
    va_list args;
    va_start(args, fmt);
    _vsnwprintf(msg, 1024, fmt, args);
    va_end(args);
    msg[1023] = L'\0';

    OutputDebugStringW(L"[rEFInd] ");
    OutputDebugStringW(msg);
    OutputDebugStringW(L"\n");
}

static void RefindSetError(const WCHAR* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    _vsnwprintf(g_refindLastError, 512, fmt, args);
    va_end(args);
    g_refindLastError[511] = L'\0';
    RefindDebugLog(L"ERROR: %s", g_refindLastError);
}

static void RefindShowStep(const WCHAR* title, const WCHAR* text)
{
    RefindDebugLog(L"%s: %s", title, text);
    MessageBoxW(NULL, text, title, MB_OK | MB_ICONINFORMATION);
}

// ============================================
// 执行命令并等待完成
// ============================================
static BOOL RunCommand(const WCHAR* cmd, const WCHAR* params)
{
    WCHAR cmdLine[2048];
    STARTUPINFOW si = {0};
    PROCESS_INFORMATION pi = {0};

    if (!cmd || !params) {
        return FALSE;
    }

    swprintf(cmdLine, 2048, L"\"%s\" %s", cmd, params);

    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    if (!CreateProcessW(NULL, cmdLine, NULL, NULL, FALSE,
        CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        return FALSE;
    }

    WaitForSingleObject(pi.hProcess, 30000);

    DWORD exitCode = 1;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    return (exitCode == 0);
}

// ============================================
// 递归创建目录
// ============================================
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

// ============================================
// 复制目录内容
// ============================================
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
                DeleteFileW(destFile);
                if (!CopyFileW(srcFile, destFile, FALSE)) {
                    RefindDebugLog(L"Copy failed: %s -> %s", srcFile, destFile);
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
    BOOTMGR_ERROR error = {0};

    if (!driveLetter || size < 3) {
        return FALSE;
    }

    if (!EspMount(driveLetter, size)) {
        BootMgrSetError(&error, BOOTMGR_ERROR_ESP_MOUNT_FAILED, L"mountvol /S 挂载 ESP 失败", GetLastError());
        RefindSetError(BootMgrGetErrorMessage(&error));
        LOG_ERROR(L"RefindMountESP failed: %s", BootMgrGetErrorMessage(&error));
        return FALSE;
    }

    LOG_INFO(L"ESP mounted at %s", driveLetter);
    RefindDebugLog(L"ESP mounted at %s", driveLetter);
    return TRUE;
}

BOOL RefindUnmountESP(const WCHAR* driveLetter)
{
    if (!driveLetter || wcslen(driveLetter) < 2) {
        return FALSE;
    }

    if (!EspUnmount(driveLetter)) {
        RefindSetError(L"mountvol /D 卸载 ESP 失败: %c:", driveLetter[0]);
        LOG_ERROR(L"RefindUnmountESP failed for %c:", driveLetter[0]);
        return FALSE;
    }

    LOG_INFO(L"ESP unmounted from %c:", driveLetter[0]);
    RefindDebugLog(L"ESP unmounted from %c:", driveLetter[0]);
    return TRUE;
}

// ============================================
// 查找 ESP 分区（自动挂载）
// ============================================
BOOL RefindFindESP(WCHAR* driveLetter, DWORD size)
{
    return RefindMountESP(driveLetter, size);
}

// ============================================
// 添加 NVRAM 启动项（通过 bootmgr 路径）
// ============================================
BOOL RefindAddNVRAMEntry(const WCHAR* description, const WCHAR* path)
{
    WCHAR cmd[1024];

    if (!description || !path) return FALSE;

    RefindDebugLog(L"Setting boot manager path for %s => %s", description, path);
    swprintf(cmd, 1024, L"/c bcdedit /set {bootmgr} path %s", path);
    if (!RunCommand(L"cmd.exe", cmd)) {
        RefindSetError(L"bcdedit 设置 bootmgr path 失败");
        return FALSE;
    }

    return TRUE;
}

// ============================================
// 移除 NVRAM 启动项（恢复 Windows bootmgfw）
// ============================================
BOOL RefindRemoveNVRAMEntry(const WCHAR* description)
{
    WCHAR cmd[1024];
    (void)description;

    swprintf(cmd, 1024, L"/c bcdedit /set {bootmgr} path \\EFI\\Microsoft\\Boot\\bootmgfw.efi");
    if (!RunCommand(L"cmd.exe", cmd)) {
        RefindSetError(L"bcdedit 恢复 bootmgr path 失败");
        return FALSE;
    }

    return TRUE;
}

// ============================================
// 安装 rEFInd
// ============================================
BOOL RefindInstall(const WCHAR* sourcePath, const WCHAR* espDrive)
{
    WCHAR actualEsp[4] = {0};
    const WCHAR* targetEsp = actualEsp;
    WCHAR sourceRoot[MAX_PATH];
    WCHAR srcFile[MAX_PATH];

    g_refindLastError[0] = L'\0';

    (void)espDrive;

    if (!sourcePath || wcslen(sourcePath) == 0) {
        RefindSetError(L"sourcePath 为空");
        return FALSE;
    }

    if (!RefindMountESP(actualEsp, 4)) {
        RefindSetError(L"挂载 ESP 分区失败");
        return FALSE;
    }

    {
        WCHAR mountDrive[64];
        swprintf(mountDrive, 64, L"ESP 分区挂载成功:\n%s", targetEsp);
        RefindShowStep(L"挂载 ESP", mountDrive);
    }

    swprintf(sourceRoot, MAX_PATH, L"%s", sourcePath);
    swprintf(srcFile, MAX_PATH, L"%s\\refind_x64.efi", sourceRoot);

    if (GetFileAttributesW(srcFile) == INVALID_FILE_ATTRIBUTES) {
        swprintf(sourceRoot, MAX_PATH, L"%s\\refind", sourcePath);
        swprintf(srcFile, MAX_PATH, L"%s\\refind_x64.efi", sourceRoot);
    }

    {
        WCHAR srcCheck[MAX_PATH];
        swprintf(srcCheck, MAX_PATH, L"源文件路径:\n%s\\refind_x64.efi", sourceRoot);
        RefindShowStep(L"验证源文件", srcCheck);
    }

    if (GetFileAttributesW(srcFile) == INVALID_FILE_ATTRIBUTES) {
        RefindShowStep(L"错误", L"源文件不存在!");
        RefindSetError(L"未找到 refind_x64.efi，源路径: %s", sourcePath);
        return FALSE;
    }

    WCHAR destRefindDir[MAX_PATH];
    WCHAR destBootDir[MAX_PATH];
    WCHAR backupPath[MAX_PATH];
    WCHAR bootxEfi[MAX_PATH];
    WCHAR refindEfi[MAX_PATH];
    WCHAR destFile[MAX_PATH];

    swprintf(destRefindDir, MAX_PATH, L"%s\\EFI\\refind", targetEsp);
    swprintf(destBootDir, MAX_PATH, L"%s\\EFI\\Boot", targetEsp);
    swprintf(bootxEfi, MAX_PATH, L"%s\\bootx64.efi", destBootDir);
    swprintf(backupPath, MAX_PATH, L"%s\\bootx64.efi.bak", destBootDir);

    if (!CreateDirectoryRecursive(destBootDir) || !CreateDirectoryRecursive(destRefindDir)) {
        RefindSetError(L"创建目标目录失败");
        return FALSE;
    }

    if (GetFileAttributesW(bootxEfi) != INVALID_FILE_ATTRIBUTES) {
        if (GetFileAttributesW(backupPath) != INVALID_FILE_ATTRIBUTES) {
            DeleteFileW(backupPath);
        }
        if (!CopyFileW(bootxEfi, backupPath, FALSE)) {
            RefindSetError(L"备份 bootx64.efi 失败");
            return FALSE;
        }
        RefindShowStep(L"备份引导文件", L"已备份原始 EFI\\Boot\\bootx64.efi");
    }

    swprintf(destFile, MAX_PATH, L"%s\\refind_x64.efi", destRefindDir);
    {
        WCHAR copyMsg[MAX_PATH * 2];
        swprintf(copyMsg, MAX_PATH * 2, L"从:\n%s\n到:\n%s", srcFile, destFile);
        RefindShowStep(L"复制文件", copyMsg);
    }

    if (!CopyDirectoryContents(sourceRoot, destRefindDir)) {
        DWORD err = GetLastError();
        WCHAR errMsg[256];
        swprintf(errMsg, 256, L"复制失败!\n错误代码：%lu", err);
        RefindShowStep(L"错误", errMsg);
        RefindSetError(L"复制 rEFInd 文件失败，错误代码: %lu", err);
        return FALSE;
    }

    if (GetFileAttributesW(destFile) == INVALID_FILE_ATTRIBUTES) {
        RefindShowStep(L"错误", L"目标文件不存在!复制失败");
        RefindSetError(L"复制后目标文件缺失: %s", destFile);
        return FALSE;
    }
    RefindShowStep(L"成功", L"文件复制成功");

    swprintf(refindEfi, MAX_PATH, L"%s\\refind_x64.efi", destRefindDir);
    if (!CopyFileW(refindEfi, bootxEfi, FALSE)) {
        DeleteFileW(bootxEfi);
        if (!CopyFileW(refindEfi, bootxEfi, FALSE)) {
            RefindShowStep(L"错误", L"无法写入 EFI\\Boot\\bootx64.efi");
            RefindSetError(L"复制 refind_x64.efi 到 bootx64.efi 失败");
            return FALSE;
        }
    }

    if (!RefindAddNVRAMEntry(L"rEFInd Boot Manager", L"\\EFI\\refind\\refind_x64.efi")) {
        RefindShowStep(L"错误", L"NVRAM 启动项注册失败");
        return FALSE;
    }

    RefindShowStep(L"成功", L"NVRAM 启动项注册成功");
    return TRUE;
}

// ============================================
// 卸载 rEFInd
// ============================================
BOOL RefindUninstall(const WCHAR* espDrive)
{
    WCHAR actualEsp[4] = {0};
    const WCHAR* targetEsp = actualEsp;

    g_refindLastError[0] = L'\0';
    (void)espDrive;

    if (!RefindMountESP(actualEsp, 4)) {
        RefindSetError(L"挂载 ESP 分区失败");
        return FALSE;
    }

    RefindShowStep(L"rEFInd 调试", L"卸载步骤0: ESP 分区挂载成功");
    RefindDebugLog(L"Uninstall from ESP: %s", targetEsp);

    WCHAR destBootDir[MAX_PATH];
    WCHAR destRefindDir[MAX_PATH];
    WCHAR backupPath[MAX_PATH];
    WCHAR bootxEfi[MAX_PATH];
    WCHAR windowsEfi[MAX_PATH];

    swprintf(destBootDir, MAX_PATH, L"%s\\EFI\\Boot", targetEsp);
    swprintf(destRefindDir, MAX_PATH, L"%s\\EFI\\refind", targetEsp);
    swprintf(bootxEfi, MAX_PATH, L"%s\\bootx64.efi", destBootDir);
    swprintf(backupPath, MAX_PATH, L"%s\\bootx64.efi.bak", destBootDir);
    swprintf(windowsEfi, MAX_PATH, L"%s\\EFI\\Microsoft\\Boot\\bootmgfw.efi", targetEsp);

    if (GetFileAttributesW(backupPath) != INVALID_FILE_ATTRIBUTES) {
        DeleteFileW(bootxEfi);
        if (!CopyFileW(backupPath, bootxEfi, FALSE)) {
            RefindSetError(L"恢复 bootx64.efi 备份失败");
            return FALSE;
        }
        DeleteFileW(backupPath);
        RefindShowStep(L"rEFInd 调试", L"卸载步骤1: 已恢复 bootx64.efi 备份");
    } else {
        DeleteFileW(bootxEfi);
        if (GetFileAttributesW(windowsEfi) != INVALID_FILE_ATTRIBUTES) {
            if (!CopyFileW(windowsEfi, bootxEfi, FALSE)) {
                RefindSetError(L"恢复 Windows bootmgfw 失败");
                return FALSE;
            }
            RefindShowStep(L"rEFInd 调试", L"卸载步骤1: 无备份，已恢复 Windows bootmgfw.efi");
        } else {
            RefindSetError(L"未找到备份，也未找到 Microsoft bootmgfw.efi");
            return FALSE;
        }
    }

    if (GetFileAttributesW(destRefindDir) != INVALID_FILE_ATTRIBUTES) {
        if (!DeleteDirectoryRecursive(destRefindDir)) {
            RefindSetError(L"删除 rEFInd 目录失败");
            return FALSE;
        }
    }
    RefindShowStep(L"rEFInd 调试", L"卸载步骤2: 已删除 rEFInd 目录");

    if (!RefindRemoveNVRAMEntry(L"rEFInd Boot Manager")) {
        return FALSE;
    }
    RefindShowStep(L"rEFInd 调试", L"卸载步骤3: 已恢复 NVRAM 启动路径");

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
        backupPath[size - 1] = L'\0';
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

const WCHAR* RefindGetLastErrorMessage(void)
{
    return g_refindLastError;
}
