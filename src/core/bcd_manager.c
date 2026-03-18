/**
 * bcd_manager.c - BCD 启动菜单管理实现
 * 
 * 使用 bcdedit 命令行工具读取和操作 BCD
 * 使用 CreateProcess + 管道读取输出，兼容 GUI 程序
 */

#include "../../include/bcd_manager.h"
#include "../../include/boot_mode.h"
#include <wchar.h>
#include <stdio.h>
#include <stdlib.h>
#include <windows.h>

void GUID_ToString(const GUID* guid, WCHAR* str, size_t size) {
    swprintf(str, size,
        L"{%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}",
        guid->Data1, guid->Data2, guid->Data3,
        guid->Data4[0], guid->Data4[1], guid->Data4[2], guid->Data4[3],
        guid->Data4[4], guid->Data4[5], guid->Data4[6], guid->Data4[7]);
}

bool GUID_FromString(const WCHAR* str, GUID* guid) {
    if (!str || !guid) return false;
    memset(guid, 0, sizeof(GUID));
    
    if (str[0] == L'{') str++;
    
    // 特殊标识符处理 - {bootmgr}, {current}, {memdiag}, {ntldr} 等
    if (wcsncmp(str, L"bootmgr}", 8) == 0) {
        guid->Data1 = 0x101;
        return true;
    }
    if (wcsncmp(str, L"current}", 8) == 0) {
        guid->Data1 = 0x101;
        return true;
    }
    if (wcsncmp(str, L"memdiag}", 8) == 0) {
        guid->Data1 = 0x103;
        return true;
    }
    if (wcsncmp(str, L"ntldr}", 6) == 0) {
        guid->Data1 = 0x104;
        return true;
    }
    
    // 真正的 GUID 格式
    int data4[8];
    int count = swscanf(str,
        L"%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X",
        &guid->Data1, &guid->Data2, &guid->Data3,
        &data4[0], &data4[1], &data4[2], &data4[3],
        &data4[4], &data4[5], &data4[6], &data4[7]);
    
    if (count != 11) return false;
    for (int i = 0; i < 8; i++) {
        guid->Data4[i] = (BYTE)data4[i];
    }
    return true;
}

void BCD_InitList(BCD_LIST* list) {
    list->entries = NULL;
    list->count = 0;
    list->capacity = 0;
}

void BCD_FreeList(BCD_LIST* list) {
    if (list->entries) {
        free(list->entries);
        list->entries = NULL;
    }
    list->count = 0;
    list->capacity = 0;
}

static bool BCD_EnsureCapacity(BCD_LIST* list) {
    if (list->count >= list->capacity) {
        int newCap = list->capacity == 0 ? 16 : list->capacity * 2;
        BCD_ENTRY* newEntries = (BCD_ENTRY*)realloc(list->entries, newCap * sizeof(BCD_ENTRY));
        if (!newEntries) return false;
        list->entries = newEntries;
        list->capacity = newCap;
    }
    return true;
}

static void TrimString(WCHAR* str) {
    if (!str || !str[0]) return;
    int len = wcslen(str);
    while (len > 0 && (str[len-1] == L' ' || str[len-1] == L'\r' || str[len-1] == L'\n' || str[len-1] == L'\t')) {
        str[--len] = 0;
    }
    WCHAR* p = str;
    while (*p == L' ' || *p == L'\t') p++;
    if (p != str) {
        memmove(str, p, (wcslen(p) + 1) * sizeof(WCHAR));
    }
}

// 使用 CreateProcess 执行命令并读取输出
static bool RunCommand(const WCHAR* cmd, char** output, DWORD* size) {
    SECURITY_ATTRIBUTES sa = {0};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = NULL;
    
    HANDLE hRead, hWrite;
    if (!CreatePipe(&hRead, &hWrite, &sa, 0)) {
        return false;
    }
    SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0);
    
    STARTUPINFOW si = {0};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = hWrite;
    si.hStdError = hWrite;
    
    PROCESS_INFORMATION pi = {0};
    WCHAR cmdLine[512];
    wcscpy(cmdLine, cmd);
    
    if (!CreateProcessW(NULL, cmdLine, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        CloseHandle(hRead);
        CloseHandle(hWrite);
        return false;
    }
    
    CloseHandle(hWrite);
    
    DWORD totalSize = 0;
    DWORD bufferSize = 65536;
    char* buffer = (char*)malloc(bufferSize);
    if (!buffer) {
        CloseHandle(hRead);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        return false;
    }
    
    DWORD bytesRead;
    while (ReadFile(hRead, buffer + totalSize, bufferSize - totalSize - 1, &bytesRead, NULL) && bytesRead > 0) {
        totalSize += bytesRead;
        if (totalSize >= bufferSize - 1) {
            bufferSize *= 2;
            char* newBuf = (char*)realloc(buffer, bufferSize);
            if (!newBuf) break;
            buffer = newBuf;
        }
    }
    
    CloseHandle(hRead);
    WaitForSingleObject(pi.hProcess, 5000);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    
    buffer[totalSize] = 0;
    *output = buffer;
    *size = totalSize;
    return true;
}

// 检测并转换输出（可能是 ANSI 或 UTF-16）
static WCHAR* ConvertOutputToWide(const char* output, DWORD size) {
    if (!output || size == 0) return NULL;
    
    // 检查 UTF-16 LE BOM
    if (size >= 2 && (unsigned char)output[0] == 0xFF && (unsigned char)output[1] == 0xFE) {
        DWORD wcharCount = (size - 2) / 2;
        WCHAR* result = (WCHAR*)malloc((wcharCount + 1) * sizeof(WCHAR));
        if (!result) return NULL;
        memcpy(result, output + 2, size - 2);
        result[wcharCount] = 0;
        return result;
    }
    
    // 检查 UTF-8 BOM
    if (size >= 3 && (unsigned char)output[0] == 0xEF && (unsigned char)output[1] == 0xBB && (unsigned char)output[2] == 0xBF) {
        int wideSize = MultiByteToWideChar(CP_UTF8, 0, output + 3, size - 3, NULL, 0);
        WCHAR* result = (WCHAR*)malloc((wideSize + 1) * sizeof(WCHAR));
        if (!result) return NULL;
        MultiByteToWideChar(CP_UTF8, 0, output + 3, size - 3, result, wideSize);
        result[wideSize] = 0;
        return result;
    }
    
    // 检测无 BOM 的 UTF-16（ASCII 字符的高字节为 0）
    if (size >= 4 && output[1] == 0 && output[3] == 0) {
        DWORD wcharCount = size / 2;
        WCHAR* result = (WCHAR*)malloc((wcharCount + 1) * sizeof(WCHAR));
        if (!result) return NULL;
        memcpy(result, output, size);
        result[wcharCount] = 0;
        return result;
    }
    
    // 默认按 ANSI 转换
    int wideSize = MultiByteToWideChar(CP_ACP, 0, output, size, NULL, 0);
    if (wideSize == 0) return NULL;
    
    WCHAR* result = (WCHAR*)malloc((wideSize + 1) * sizeof(WCHAR));
    if (!result) return NULL;
    MultiByteToWideChar(CP_ACP, 0, output, size, result, wideSize);
    result[wideSize] = 0;
    return result;
}

bool BCD_GetEntries(BCD_LIST* list, WCHAR* error, DWORD errorSize) {
    if (!list) return false;
    
    // 使用程序目录下的临时文件
    WCHAR exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    WCHAR* lastSlash = wcsrchr(exePath, L'\\');
    if (lastSlash) *lastSlash = 0;
    
    WCHAR tempPath[MAX_PATH];
    swprintf(tempPath, MAX_PATH, L"%s\\bcd_temp.txt", exePath);
    
    // 删除可能存在的旧文件
    DeleteFileW(tempPath);
    
    // 执行 bcdedit 输出到文件 - 使用 UTF-8 编码
    WCHAR cmd[512];
    swprintf(cmd, 512, L"chcp 65001 >nul && bcdedit /enum > \"%s\"", tempPath);
    
    int result = _wsystem(cmd);
    if (result != 0) {
        if (error) swprintf(error, errorSize, L"bcdedit 执行失败 (code %d)", result);
        return false;
    }
    
    // 检查文件是否存在
    DWORD attrs = GetFileAttributesW(tempPath);
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        if (error) wcscpy(error, L"bcdedit 未生成输出文件");
        return false;
    }
    
    // 读取文件 - UTF-8 模式
    FILE* fp = _wfopen(tempPath, L"rt, ccs=UTF-8");
    if (!fp) {
        fp = _wfopen(tempPath, L"rt");
    }
    if (!fp) {
        if (error) wcscpy(error, L"无法打开输出文件");
        DeleteFileW(tempPath);
        return false;
    }
    
    // 按行解析
    WCHAR line[1024];
    BCD_ENTRY* current = NULL;
    int identifierCount = 0;
    
    while (fgetws(line, 1024, fp)) {
        WCHAR* cr = wcschr(line, L'\r');
        if (cr) *cr = 0;
        WCHAR* nl = wcschr(line, L'\n');
        if (nl) *nl = 0;
        
        if (line[0] == 0 || line[0] == L'-') continue;
        
        // identifier 行
        if (wcsncmp(line, L"identifier", 10) == 0) {
            WCHAR* value = line + 10;
            while (*value == L' ' || *value == L'\t') value++;
            TrimString(value);
            
            identifierCount++;
            
            if (*value && BCD_EnsureCapacity(list)) {
                current = &list->entries[list->count++];
                memset(current, 0, sizeof(BCD_ENTRY));
                wcsncpy(current->idStr, value, 63);
                GUID_FromString(value, &current->id);
                
                if (wcsstr(value, L"{bootmgr}")) {
                    current->type = BCD_TYPE_BOOTMGR;
                } else if (wcsstr(value, L"{memdiag}")) {
                    current->type = BCD_TYPE_MEMDIAG;
                } else if (wcsstr(value, L"{ntldr}")) {
                    current->type = BCD_TYPE_NTLDR;
                } else {
                    current->type = BCD_TYPE_OSLOADER;
                }
            }
        }
        // description 行
        else if (current && wcsncmp(line, L"description", 11) == 0) {
            WCHAR* value = line + 11;
            while (*value == L' ' || *value == L'\t') value++;
            TrimString(value);
            wcsncpy(current->name, value, 255);
        }
        // path 行
        else if (current && wcsncmp(line, L"path", 4) == 0 && line[4] == L' ') {
            WCHAR* value = line + 4;
            while (*value == L' ' || *value == L'\t') value++;
            TrimString(value);
            wcsncpy(current->path, value, MAX_PATH - 1);
        }
    }
    
    fclose(fp);
    DeleteFileW(tempPath);
    
    if (list->count == 0) {
        if (error) swprintf(error, errorSize, L"未找到启动项 (解析 %d 个 identifier)", identifierCount);
        return false;
    }
    
    return true;
}

bool BCD_DeleteEntry(const WCHAR* idStr, WCHAR* error, DWORD errorSize) {
    if (!idStr) return false;
    
    WCHAR cmd[256];
    swprintf(cmd, 256, L"cmd.exe /c bcdedit /delete %s /cleanup", idStr);
    
    char* output = NULL;
    DWORD size = 0;
    if (!RunCommand(cmd, &output, &size)) {
        if (error) wcscpy(error, L"删除失败");
        return false;
    }
    free(output);
    return true;
}

bool BCD_SetDefault(const WCHAR* idStr, WCHAR* error, DWORD errorSize) {
    if (!idStr) return false;
    
    WCHAR cmd[256];
    swprintf(cmd, 256, L"cmd.exe /c bcdedit /default %s", idStr);
    
    char* output = NULL;
    DWORD size = 0;
    if (!RunCommand(cmd, &output, &size)) {
        if (error) wcscpy(error, L"设置失败");
        return false;
    }
    free(output);
    return true;
}

bool BCD_SetTimeout(DWORD seconds, WCHAR* error, DWORD errorSize) {
    WCHAR cmd[256];
    swprintf(cmd, 256, L"cmd.exe /c bcdedit /timeout %lu", seconds);
    
    char* output = NULL;
    DWORD size = 0;
    if (!RunCommand(cmd, &output, &size)) {
        if (error) wcscpy(error, L"设置失败");
        return false;
    }
    free(output);
    return true;
}

bool BCD_SetDisplayOrder(const GUID* ids, int count, WCHAR* error, DWORD errorSize) {
    if (error) wcscpy(error, L"此功能暂未实现");
    return false;
}

bool BCD_RenameEntry(const GUID* id, const WCHAR* newName, WCHAR* error, DWORD errorSize) {
    if (!id || !newName) return false;
    
    WCHAR guidStr[64];
    GUID_ToString(id, guidStr, 64);
    
    WCHAR cmd[512];
    swprintf(cmd, 512, L"cmd.exe /c bcdedit /set %s description \"%s\"", guidStr, newName);
    
    char* output = NULL;
    DWORD size = 0;
    if (!RunCommand(cmd, &output, &size)) {
        if (error) wcscpy(error, L"重命名失败");
        return false;
    }
    free(output);
    return true;
}