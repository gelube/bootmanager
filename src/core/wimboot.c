/**
 * WIM/VHD Boot Entry Management - Implementation
 * 
 * 使用 dist\bcdedit.exe（系统默认 BCD）
 */

#include "wimboot.h"
#include <wchar.h>
#include <stdlib.h>
#include <string.h>
#include <commctrl.h>
#include <shlobj.h>

#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "shell32.lib")

static WCHAR s_lastError[1024] = {0};

static void SetError(const WCHAR* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    _vsnwprintf(s_lastError, sizeof(s_lastError) / sizeof(WCHAR), fmt, args);
    va_end(args);
}

const WCHAR* WimbootGetLastErrorMessage(void) { return s_lastError; }

// 获取 bcdedit.exe 路径
static void GetBcdEditPath(WCHAR* path, DWORD size) {
    WCHAR exeDir[MAX_PATH];
    GetModuleFileNameW(NULL, exeDir, MAX_PATH);
    WCHAR* lastSlash = wcsrchr(exeDir, L'\\');
    if (lastSlash) *lastSlash = 0;
    
    swprintf(path, size, L"%s\\dist\\bcdedit.exe", exeDir);
    if (GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES) return;
    
    swprintf(path, size, L"%s\\bcdedit.exe", exeDir);
    if (GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES) return;
    
    wcscpy(path, L"bcdedit.exe");
}

// 执行 bcdedit（使用系统默认 BCD）
static BOOL RunBcdEdit(const WCHAR* args, WCHAR* output, DWORD outputSize) {
    WCHAR bcdeditPath[MAX_PATH];
    GetBcdEditPath(bcdeditPath, MAX_PATH);
    
    WCHAR cmdLine[2048] = {0};
    // 不指定 /store，使用系统默认 BCD
    swprintf(cmdLine, 2048, L"\"%s\" %s", bcdeditPath, args);
    
    SECURITY_ATTRIBUTES sa = {0};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    
    HANDLE hRead, hWrite;
    if (!CreatePipe(&hRead, &hWrite, &sa, 0)) return FALSE;
    SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0);
    
    STARTUPINFOW si = {0};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = hWrite;
    si.hStdError = hWrite;
    
    PROCESS_INFORMATION pi = {0};
    
    if (!CreateProcessW(NULL, cmdLine, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        CloseHandle(hRead);
        CloseHandle(hWrite);
        return FALSE;
    }
    
    CloseHandle(hWrite);
    
    char* buffer = (char*)malloc(65536);
    if (!buffer) {
        CloseHandle(hRead);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        return FALSE;
    }
    
    DWORD totalSize = 0, bytesRead;
    while (ReadFile(hRead, buffer + totalSize, 65535 - totalSize, &bytesRead, NULL) && bytesRead > 0) {
        totalSize += bytesRead;
    }
    
    CloseHandle(hRead);
    WaitForSingleObject(pi.hProcess, 10000);
    
    DWORD exitCode = 1;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    
    buffer[totalSize] = 0;
    
    if (output && outputSize > 0) {
        int wideSize = MultiByteToWideChar(CP_ACP, 0, buffer, totalSize, NULL, 0);
        if (wideSize > 0) {
            MultiByteToWideChar(CP_ACP, 0, buffer, totalSize, output, outputSize / sizeof(WCHAR) - 1);
            output[wideSize] = 0;
        }
    }
    
    free(buffer);
    return exitCode == 0;
}

static BOOL ExtractGuid(const WCHAR* output, WCHAR* guid, DWORD size) {
    const WCHAR* start = wcschr(output, L'{');
    if (!start) return FALSE;
    const WCHAR* end = wcschr(start, L'}');
    if (!end) return FALSE;
    size_t len = end - start + 1;
    if (len >= size) return FALSE;
    wcsncpy(guid, start, len);
    guid[len] = 0;
    return TRUE;
}

// 查找 boot.sdi
static BOOL FindBootSdi(const WCHAR* wimPath, WCHAR* sdiPath, DWORD sdiSize) {
    WCHAR p[MAX_PATH];
    
    // 1. 程序所在目录\boot.sdi（优先，exe 已经在 dist 目录下）
    WCHAR exeDir[MAX_PATH];
    GetModuleFileNameW(NULL, exeDir, MAX_PATH);
    WCHAR* lastSlash = wcsrchr(exeDir, L'\\');
    if (lastSlash) {
        *lastSlash = L'\0';
        swprintf(p, MAX_PATH, L"%s\\boot.sdi", exeDir);
        if (GetFileAttributesW(p) != INVALID_FILE_ATTRIBUTES) {
            wcsncpy(sdiPath, p, sdiSize);
            return TRUE;
        }
    }
    
    // 2. WIM 同目录
    wcsncpy(p, wimPath, MAX_PATH);
    lastSlash = wcsrchr(p, L'\\');
    if (lastSlash) {
        wcscpy(lastSlash + 1, L"boot.sdi");
        if (GetFileAttributesW(p) != INVALID_FILE_ATTRIBUTES) {
            wcsncpy(sdiPath, p, sdiSize);
            return TRUE;
        }
    }
    
    // 3. WIM 所在盘的 \boot\boot.sdi
    if (wimPath[1] == L':') {
        swprintf(p, MAX_PATH, L"%c:\\boot\\boot.sdi", wimPath[0]);
        if (GetFileAttributesW(p) != INVALID_FILE_ATTRIBUTES) {
            wcsncpy(sdiPath, p, sdiSize);
            return TRUE;
        }
    }
    
    // 4. 系统盘 \boot\boot.sdi
    WCHAR sysDir[MAX_PATH];
    GetWindowsDirectoryW(sysDir, MAX_PATH);
    swprintf(p, MAX_PATH, L"%c:\\boot\\boot.sdi", sysDir[0]);
    if (GetFileAttributesW(p) != INVALID_FILE_ATTRIBUTES) {
        wcsncpy(sdiPath, p, sdiSize);
        return TRUE;
    }
    
    SetError(L"未找到 boot.sdi 文件\n\n请将 boot.sdi 放到程序所在目录下");
    return FALSE;
}

// ============================================
// WIM 启动项
// ============================================
BOOL WimAddBootEntry(const WCHAR* name, const WCHAR* wimPath, const WCHAR* imageIndex) {
    if (!name || !wimPath) {
        SetError(L"参数无效");
        return FALSE;
    }
    
    if (GetFileAttributesW(wimPath) == INVALID_FILE_ATTRIBUTES) {
        SetError(L"WIM 文件不存在");
        return FALSE;
    }
    
    WCHAR sdiPath[MAX_PATH] = {0};
    if (!FindBootSdi(wimPath, sdiPath, MAX_PATH)) {
        return FALSE;
    }
    
    WCHAR output[2048] = {0}, guid[64] = {0}, args[2048] = {0};
    
    // 1. 创建/更新 ramdiskoptions（忽略已存在的错误）
    swprintf(args, 2048, L"/create {ramdiskoptions} /d \"Ramdisk Options\"");
    RunBcdEdit(args, output, 2048);  // 忽略错误，可能已存在
    
    WCHAR sdiDevice[8] = {0}, sdiPathOnly[MAX_PATH] = {0};
    if (sdiPath[1] == L':') {
        sdiDevice[0] = sdiPath[0]; sdiDevice[1] = L':';
        wcscpy(sdiPathOnly, sdiPath + 2);
    } else {
        wcscpy(sdiPathOnly, sdiPath);
    }
    
    swprintf(args, 2048, L"/set {ramdiskoptions} ramdisksdidevice partition=%s", sdiDevice);
    if (!RunBcdEdit(args, output, 2048)) {
        SetError(L"设置 ramdisksdidevice 失败: %s", output);
        return FALSE;
    }
    
    swprintf(args, 2048, L"/set {ramdiskoptions} ramdisksdipath %s", sdiPathOnly);
    if (!RunBcdEdit(args, output, 2048)) {
        SetError(L"设置 ramdisksdipath 失败: %s", output);
        return FALSE;
    }
    
    // 2. 创建 osloader
    swprintf(args, 2048, L"/create /d \"%s\" /application osloader", name);
    output[0] = 0;
    if (!RunBcdEdit(args, output, 2048)) {
        SetError(L"创建启动项失败: %s", output[0] ? output : L"未知错误");
        return FALSE;
    }
    if (!ExtractGuid(output, guid, 64)) {
        SetError(L"无法获取新启动项 GUID，输出: %s", output);
        return FALSE;
    }
    
    // 3. 设置属性
    swprintf(args, 2048, L"/set %s path \\windows\\system32\\winload.exe", guid);
    RunBcdEdit(args, NULL, 0);
    
    swprintf(args, 2048, L"/set %s systemroot \\windows", guid);
    RunBcdEdit(args, NULL, 0);
    
    swprintf(args, 2048, L"/set %s winpe yes", guid);
    RunBcdEdit(args, NULL, 0);
    
    swprintf(args, 2048, L"/set %s detecthal yes", guid);
    RunBcdEdit(args, NULL, 0);
    
    // 4. 设置 ramdisk 设备
    WCHAR winPath[MAX_PATH] = {0};
    if (wimPath[1] == L':') {
        swprintf(winPath, MAX_PATH, L"[%c:]%s", wimPath[0], wimPath + 2);
    } else {
        wcsncpy(winPath, wimPath, MAX_PATH);
    }
    
    swprintf(args, 2048, L"/set %s osdevice ramdisk=%s,{ramdiskoptions}", guid, winPath);
    RunBcdEdit(args, NULL, 0);
    
    swprintf(args, 2048, L"/set %s device ramdisk=%s,{ramdiskoptions}", guid, winPath);
    RunBcdEdit(args, NULL, 0);
    
    // 5. 添加到显示顺序
    swprintf(args, 2048, L"/displayorder %s /addlast", guid);
    RunBcdEdit(args, NULL, 0);
    
    return TRUE;
}

// ============================================
// VHD 启动项
// ============================================
BOOL VhdAddBootEntry(const WCHAR* name, const WCHAR* vhdPath) {
    if (!name || !vhdPath) {
        SetError(L"参数无效");
        return FALSE;
    }
    
    if (GetFileAttributesW(vhdPath) == INVALID_FILE_ATTRIBUTES) {
        SetError(L"VHD 文件不存在");
        return FALSE;
    }
    
    WCHAR output[2048] = {0}, guid[64] = {0}, args[2048] = {0};
    
    swprintf(args, 2048, L"/create /d \"%s\" /application osloader", name);
    if (!RunBcdEdit(args, output, 2048)) {
        SetError(L"创建启动项失败\n请确保以管理员身份运行");
        return FALSE;
    }
    if (!ExtractGuid(output, guid, 64)) return FALSE;
    
    swprintf(args, 2048, L"/set %s path \\windows\\system32\\winload.exe", guid);
    RunBcdEdit(args, NULL, 0);
    
    swprintf(args, 2048, L"/set %s systemroot \\windows", guid);
    RunBcdEdit(args, NULL, 0);
    
    WCHAR vhdParam[MAX_PATH + 16] = {0};
    if (vhdPath[1] == L':') {
        swprintf(vhdParam, MAX_PATH + 16, L"vhd=[%c:]%s", vhdPath[0], vhdPath + 2);
    } else {
        swprintf(vhdParam, MAX_PATH + 16, L"vhd=%s", vhdPath);
    }
    
    swprintf(args, 2048, L"/set %s osdevice %s", guid, vhdParam);
    RunBcdEdit(args, NULL, 0);
    
    swprintf(args, 2048, L"/set %s device %s", guid, vhdParam);
    RunBcdEdit(args, NULL, 0);
    
    swprintf(args, 2048, L"/displayorder %s /addlast", guid);
    RunBcdEdit(args, NULL, 0);
    
    return TRUE;
}

// ============================================
// RAM 启动
// ============================================
BOOL RamAddBootEntry(const WCHAR* name, const WCHAR* wimPath, const WCHAR* sdiPath) {
    if (!name || !wimPath) {
        SetError(L"参数无效");
        return FALSE;
    }
    
    WCHAR actualSdi[MAX_PATH] = {0};
    if (sdiPath && sdiPath[0]) {
        wcsncpy(actualSdi, sdiPath, MAX_PATH);
    } else {
        if (!FindBootSdi(wimPath, actualSdi, MAX_PATH)) {
            return FALSE;
        }
    }
    
    return WimAddBootEntry(name, wimPath, L"1");
}

// ============================================
// WinPE 启动项
// ============================================
BOOL WinPeAddBootEntry(const WCHAR* name, const WCHAR* wimPath) {
    return WimAddBootEntry(name, wimPath, L"1");
}

// ============================================
// eSD 启动项
// ============================================
BOOL EsdAddBootEntry(const WCHAR* name, const WCHAR* esdPath, const WCHAR* imageIndex) {
    if (!name || !esdPath) {
        SetError(L"参数无效");
        return FALSE;
    }
    
    if (GetFileAttributesW(esdPath) == INVALID_FILE_ATTRIBUTES) {
        SetError(L"eSD 文件不存在");
        return FALSE;
    }
    
    // eSD 与 WIM 类似，使用相同的流程
    return WimAddBootEntry(name, esdPath, imageIndex ? imageIndex : L"1");
}

// ============================================
// 文件选择对话框
// ============================================
static BOOL CommonFileDialog(HWND hWnd, const WCHAR* filter, const WCHAR* title, WCHAR* path, DWORD size) {
    OPENFILENAMEW ofn = {0};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hWnd;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = path;
    ofn.nMaxFile = size;
    ofn.lpstrTitle = title;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;
    return GetOpenFileNameW(&ofn);
}

BOOL WimSelectFileDialog(HWND hWnd, WCHAR* path, DWORD size) {
    return CommonFileDialog(hWnd, L"WIM 文件 (*.wim)\0*.wim\0", L"选择 WIM 文件", path, size);
}

BOOL VhdSelectFileDialog(HWND hWnd, WCHAR* path, DWORD size) {
    return CommonFileDialog(hWnd, L"VHD 文件 (*.vhd;*.vhdx)\0*.vhd;*.vhdx\0", L"选择 VHD 文件", path, size);
}

BOOL RamSelectFileDialog(HWND hWnd, WCHAR* path, DWORD size) {
    return WimSelectFileDialog(hWnd, path, size);
}

BOOL WinPeSelectFileDialog(HWND hWnd, WCHAR* path, DWORD size) {
    return CommonFileDialog(hWnd, L"WinPE 文件 (*.wim;*.iso)\0*.wim;*.iso\0", L"选择 WinPE 文件", path, size);
}

BOOL EsdSelectFileDialog(HWND hWnd, WCHAR* path, DWORD size) {
    return CommonFileDialog(hWnd, L"eSD 文件 (*.esd)\0*.esd\0", L"选择 eSD 文件", path, size);
}

// ============================================
// ISO 启动项（暂不支持）
// ============================================
BOOL IsoSelectFileDialog(HWND hWnd, WCHAR* path, DWORD size) {
    MessageBoxW(hWnd, L"ISO 启动功能暂不支持\n\n请使用 Limine 引导器配置 ISO 启动", L"提示", MB_OK | MB_ICONINFORMATION);
    return FALSE;
}

BOOL IsoAddBootEntry(const WCHAR* name, const WCHAR* isoPath) {
    SetError(L"ISO 启动功能暂不支持");
    return FALSE;
}