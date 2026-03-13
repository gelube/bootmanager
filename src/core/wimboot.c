/**
 * WIM/VHD Boot Entry Management - Implementation
 * 通过 bcdedit 添加 WIM 和 VHD 启动项
 */

#include "wimboot.h"
#include <wchar.h>
#include <stdlib.h>
#include <string.h>
#include <commctrl.h>
#include <shlobj.h>

#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "shell32.lib")

// 执行命令并捕获输出 (复用 uefi.c 的逻辑)
static BOOL ExecuteCommand(const WCHAR* cmd, CHAR* output, DWORD outputSize) {
    SECURITY_ATTRIBUTES sa = {0};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    
    HANDLE hReadPipe, hWritePipe;
    if (!CreatePipe(&hReadPipe, &hWritePipe, &sa, 0)) {
        return FALSE;
    }
    
    SetHandleInformation(hReadPipe, HANDLE_FLAG_INHERIT, 0);
    
    STARTUPINFOW si = {0};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.hStdOutput = hWritePipe;
    si.hStdError = hWritePipe;
    si.wShowWindow = SW_HIDE;
    
    PROCESS_INFORMATION pi = {0};
    
    WCHAR fullCmd[2048];
    swprintf(fullCmd, 2048, L"cmd.exe /c %s", cmd);
    
    if (!CreateProcessW(NULL, fullCmd, NULL, NULL, TRUE,
        CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        CloseHandle(hReadPipe);
        CloseHandle(hWritePipe);
        return FALSE;
    }
    
    CloseHandle(hWritePipe);
    
    DWORD bytesRead = 0;
    DWORD totalBytes = 0;
    CHAR buffer[4096];
    
    while (ReadFile(hReadPipe, buffer, sizeof(buffer) - 1, &bytesRead, NULL) && bytesRead > 0) {
        if (totalBytes + bytesRead < outputSize) {
            memcpy(output + totalBytes, buffer, bytesRead);
            totalBytes += bytesRead;
        }
    }
    
    output[totalBytes] = '\0';
    
    CloseHandle(hReadPipe);
    WaitForSingleObject(pi.hProcess, 5000);
    
    DWORD exitCode = 0;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    
    return (exitCode == 0);
}

// 添加 WIM 启动项
BOOL WimAddBootEntry(const WCHAR* name, const WCHAR* wimPath, const WCHAR* imageIndex) {
    if (!name || !wimPath) return FALSE;
    
    WCHAR guid[64] = {0};
    CHAR output[4096] = {0};
    WCHAR cmd[2048];
    
    // 步骤 1: 创建新的启动项
    // bcdedit /create /d "名称" /application osloader
    swprintf(cmd, 2048, L"bcdedit /create /d \"%s\" /application osloader", name);
    
    if (!ExecuteCommand(cmd, output, sizeof(output))) {
        return FALSE;
    }
    
    // 解析返回的 GUID
    CHAR* guidStart = strchr(output, '{');
    if (guidStart) {
        CHAR* guidEnd = strchr(guidStart, '}');
        if (guidEnd) {
            char guidA[64] = {0};
            size_t len = guidEnd - guidStart + 1;
            if (len < sizeof(guidA)) {
                memcpy(guidA, guidStart, len);
                MultiByteToWideChar(CP_UTF8, 0, guidA, -1, guid, 64);
            }
        }
    }
    
    if (wcslen(guid) == 0) {
        return FALSE;
    }
    
    // 步骤 2: 设置 device 为 ramdisk 指向 WIM 文件
    // bcdedit /set {guid} device ramdisk=[boot]\\path\\to\\wim,{ramdiskoptions}
    swprintf(cmd, 2048, L"bcdedit /set %s device ramdisk=[boot]\\%s,{ramdiskoptions}", guid, wimPath);
    ExecuteCommand(cmd, output, sizeof(output));
    
    // 步骤 3: 设置 osdevice
    swprintf(cmd, 2048, L"bcdedit /set %s osdevice ramdisk=[boot]\\%s,{ramdiskoptions}", guid, wimPath);
    ExecuteCommand(cmd, output, sizeof(output));
    
    // 步骤 4: 设置 systemroot
    swprintf(cmd, 2048, L"bcdedit /set %s systemroot \\Windows", guid);
    ExecuteCommand(cmd, output, sizeof(output));
    
    // 步骤 5: 如果有 imageIndex，设置 itype 或其他参数
    if (imageIndex && wcslen(imageIndex) > 0) {
        swprintf(cmd, 2048, L"bcdedit /set %s imageindex %s", guid, imageIndex);
        ExecuteCommand(cmd, output, sizeof(output));
    }
    
    // 步骤 6: 添加到显示顺序
    swprintf(cmd, 2048, L"bcdedit /displayorder %s /addlast", guid);
    ExecuteCommand(cmd, output, sizeof(output));
    
    return TRUE;
}

// 添加 VHD 启动项
BOOL VhdAddBootEntry(const WCHAR* name, const WCHAR* vhdPath) {
    if (!name || !vhdPath) return FALSE;
    
    WCHAR guid[64] = {0};
    CHAR output[4096] = {0};
    WCHAR cmd[2048];
    
    // 步骤 1: 创建新的启动项
    swprintf(cmd, 2048, L"bcdedit /create /d \"%s\" /application osloader", name);
    
    if (!ExecuteCommand(cmd, output, sizeof(output))) {
        return FALSE;
    }
    
    // 解析返回的 GUID
    CHAR* guidStart = strchr(output, '{');
    if (guidStart) {
        CHAR* guidEnd = strchr(guidStart, '}');
        if (guidEnd) {
            char guidA[64] = {0};
            size_t len = guidEnd - guidStart + 1;
            if (len < sizeof(guidA)) {
                memcpy(guidA, guidStart, len);
                MultiByteToWideChar(CP_UTF8, 0, guidA, -1, guid, 64);
            }
        }
    }
    
    if (wcslen(guid) == 0) {
        return FALSE;
    }
    
    // 步骤 2: 设置 device 为 vhd 文件
    // bcdedit /set {guid} device vhd=[boot]\\path\\to\\vhd
    swprintf(cmd, 2048, L"bcdedit /set %s device vhd=[boot]\\%s", guid, vhdPath);
    if (!ExecuteCommand(cmd, output, sizeof(output))) {
        return FALSE;
    }
    
    // 步骤 3: 设置 osdevice
    swprintf(cmd, 2048, L"bcdedit /set %s osdevice vhd=[boot]\\%s", guid, vhdPath);
    ExecuteCommand(cmd, output, sizeof(output));
    
    // 步骤 4: 设置 systemroot
    swprintf(cmd, 2048, L"bcdedit /set %s systemroot \\Windows", guid);
    ExecuteCommand(cmd, output, sizeof(output));
    
    // 步骤 5: 设置 detecthal (VHD 启动需要)
    swprintf(cmd, 2048, L"bcdedit /set %s detecthal Yes", guid);
    ExecuteCommand(cmd, output, sizeof(output));
    
    // 步骤 6: 添加到显示顺序
    swprintf(cmd, 2048, L"bcdedit /displayorder %s /addlast", guid);
    ExecuteCommand(cmd, output, sizeof(output));
    
    return TRUE;
}

// 文件对话框辅助函数 - 选择 WIM 文件
BOOL WimSelectFileDialog(HWND hWnd, WCHAR* outPath, DWORD outPathSize) {
    if (!outPath || outPathSize < MAX_PATH) return FALSE;
    
    WCHAR szFile[MAX_PATH] = L"";
    
    OPENFILENAMEW ofn = {0};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hWnd;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = L"WIM 文件 (*.wim;*.swm)\\0*.wim;*.swm\\0所有文件 (*.*)\\0*.*\\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrFileTitle = NULL;
    ofn.nMaxFileTitle = 0;
    ofn.lpstrInitialDir = NULL;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
    
    if (GetOpenFileNameW(&ofn)) {
        wcsncpy(outPath, szFile, outPathSize);
        return TRUE;
    }
    
    return FALSE;
}

// 文件对话框辅助函数 - 选择 VHD 文件
BOOL VhdSelectFileDialog(HWND hWnd, WCHAR* outPath, DWORD outPathSize) {
    if (!outPath || outPathSize < MAX_PATH) return FALSE;
    
    WCHAR szFile[MAX_PATH] = L"";
    
    OPENFILENAMEW ofn = {0};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hWnd;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = L"VHD 文件 (*.vhd;*.vhdx)\\0*.vhd;*.vhdx\\0所有文件 (*.*)\\0*.*\\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrFileTitle = NULL;
    ofn.nMaxFileTitle = 0;
    ofn.lpstrInitialDir = NULL;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
    
    if (GetOpenFileNameW(&ofn)) {
        wcsncpy(outPath, szFile, outPathSize);
        return TRUE;
    }
    
    return FALSE;
}
