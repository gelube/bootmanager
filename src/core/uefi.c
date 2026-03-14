/**
 * UEFI Boot Entry Management - Implementation
 * 通过 bcdedit 解析和管理 UEFI 启动项
 */

#include "uefi.h"
#include <wchar.h>
#include <stdlib.h>
#include <string.h>

#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "shell32.lib")

// 管理员权限检查
BOOL UefiIsAdmin(void) {
    BOOL isAdmin = FALSE;
    PSID administratorsGroup = NULL;
    
    SID_IDENTIFIER_AUTHORITY ntAuthority = SECURITY_NT_AUTHORITY;
    if (AllocateAndInitializeSid(&ntAuthority, 2,
        SECURITY_BUILTIN_DOMAIN_RID,
        DOMAIN_ALIAS_RID_ADMINS,
        0, 0, 0, 0, 0, 0,
        &administratorsGroup)) {
        CheckTokenMembership(NULL, administratorsGroup, &isAdmin);
        FreeSid(administratorsGroup);
    }
    
    return isAdmin;
}

// 请求管理员权限
BOOL UefiRequestAdmin(HWND hWnd) {
    WCHAR exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    
    SHELLEXECUTEINFOW sei = {0};
    sei.cbSize = sizeof(sei);
    sei.lpVerb = L"runas";
    sei.lpFile = exePath;
    sei.nShow = SW_SHOWNORMAL;
    
    if (ShellExecuteExW(&sei)) {
        PostQuitMessage(0);
        return TRUE;
    }
    
    return FALSE;
}

// 执行命令并捕获输出
static BOOL ExecuteCommand(const WCHAR* cmd, CHAR* output, DWORD outputSize) {
    SECURITY_ATTRIBUTES sa = {0};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE hReadPipe, hWritePipe;
    if (!CreatePipe(&hReadPipe, &hWritePipe, &sa, 0)) {
        return FALSE;
    }

    if (output && outputSize > 0) {
        output[0] = '\0';
    }

    SetHandleInformation(hReadPipe, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si = {0};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.hStdOutput = hWritePipe;
    si.hStdError = hWritePipe;
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi = {0};

    // 使用 cmd /c 执行命令
    WCHAR fullCmd[2048];
    swprintf(fullCmd, 2048, L"cmd.exe /c %s", cmd);

    if (!CreateProcessW(NULL, fullCmd, NULL, NULL, TRUE,
        CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        CloseHandle(hReadPipe);
        CloseHandle(hWritePipe);
        return FALSE;
    }

    CloseHandle(hWritePipe);

    // 读取输出
    DWORD bytesRead = 0;
    DWORD totalBytes = 0;
    CHAR buffer[4096];

    while (ReadFile(hReadPipe, buffer, sizeof(buffer) - 1, &bytesRead, NULL) && bytesRead > 0) {
        if (output && outputSize > 0 && totalBytes + bytesRead < outputSize) {
            memcpy(output + totalBytes, buffer, bytesRead);
            totalBytes += bytesRead;
        }
    }

    if (output && outputSize > 0) {
        if (totalBytes >= outputSize) {
            totalBytes = outputSize - 1;
        }
        output[totalBytes] = '\0';
    }

    CloseHandle(hReadPipe);

    DWORD waitResult = WaitForSingleObject(pi.hProcess, 10000);
    if (waitResult != WAIT_OBJECT_0) {
        TerminateProcess(pi.hProcess, 1);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        if (output && outputSize > 0) {
            _snprintf(output, outputSize, "WaitForSingleObject failed: %lu", waitResult);
            output[outputSize - 1] = '\0';
        }
        return FALSE;
    }

    DWORD exitCode = 0;
    if (!GetExitCodeProcess(pi.hProcess, &exitCode)) {
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        if (output && outputSize > 0) {
            _snprintf(output, outputSize, "GetExitCodeProcess failed: %lu", GetLastError());
            output[outputSize - 1] = '\0';
        }
        return FALSE;
    }

    {
        WCHAR debugMsg[1024];
        swprintf(debugMsg, 1024, L"[DEBUG] ExecuteCommand cmd=%s, exitCode=%lu\n", cmd, exitCode);
        OutputDebugStringW(debugMsg);
    }
    if (output && output[0] != '\0') {
        WCHAR outputW[2048] = {0};
        MultiByteToWideChar(CP_ACP, 0, output, -1, outputW, 2048);
        OutputDebugStringW(L"[DEBUG] ExecuteCommand output=\n");
        OutputDebugStringW(outputW);
        OutputDebugStringW(L"\n");
    }

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    return (exitCode == 0);
}

static void CleanupBootEntryByGuid(const WCHAR* guid) {
    if (!guid || wcslen(guid) == 0) return;

    WCHAR cmd[512];
    CHAR output[4096] = {0};
    swprintf(cmd, 512, L"bcdedit /delete %s /f", guid);
    ExecuteCommand(cmd, output, sizeof(output));
}

typedef enum _BCD_ENTRY_SOURCE {
    ENTRY_SOURCE_FIRMWARE = 1,
    ENTRY_SOURCE_BOOTAPP = 2
} BCD_ENTRY_SOURCE;

static void TrimInPlace(CHAR* str) {
    CHAR* start = str;
    CHAR* end;

    while (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n') {
        start++;
    }

    if (start != str) {
        memmove(str, start, strlen(start) + 1);
    }

    end = str + strlen(str);
    while (end > str && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' || end[-1] == '\n')) {
        end--;
    }
    *end = '\0';
}

static BOOL ExtractGuidFromOutput(const CHAR* output, WCHAR* guid, DWORD guidSize) {
    const CHAR* guidStart;
    const CHAR* guidEnd;
    CHAR guidA[64] = {0};
    int converted;

    if (!output || !guid || guidSize < 4) return FALSE;

    guid[0] = L'\0';
    guidStart = strchr(output, '{');
    if (!guidStart) return FALSE;

    guidEnd = strchr(guidStart, '}');
    if (!guidEnd || guidEnd <= guidStart) return FALSE;

    if ((size_t)(guidEnd - guidStart + 1) >= sizeof(guidA)) return FALSE;

    memcpy(guidA, guidStart, (size_t)(guidEnd - guidStart + 1));
    guidA[guidEnd - guidStart + 1] = '\0';

    converted = MultiByteToWideChar(CP_ACP, 0, guidA, -1, guid, (int)guidSize);
    return (converted > 0);
}

static DWORD ComputeEntryIdFromGuid(const WCHAR* guid) {
    DWORD hash = 0;
    const WCHAR* p = guid;

    while (p && *p) {
        hash = (hash * 131U) + (DWORD)(*p++);
    }

    if (hash == 0) {
        hash = 1;
    }

    return hash & 0xFFFFU;
}

// 解析单个 BCD 条目，按 key/value 字段读取，适配 firmware 和 BOOTAPP 输出格式。
static BOOL ParseBcdEntry(const CHAR* block, UEFI_BOOT_ENTRY* entry, BCD_ENTRY_SOURCE source) {
    CHAR copy[8192];
    CHAR* context = NULL;
    CHAR* line;
    CHAR description[256] = {0};
    CHAR identifier[128] = {0};
    CHAR device[512] = {0};
    CHAR path[512] = {0};

    if (!block || !entry) return FALSE;

    strncpy(copy, block, sizeof(copy) - 1);
    copy[sizeof(copy) - 1] = '\0';

    line = strtok_s(copy, "\n", &context);
    while (line) {
        CHAR current[1024] = {0};
        CHAR key[64] = {0};
        CHAR value[960] = {0};
        int parsed = 0;

        strncpy(current, line, sizeof(current) - 1);
        current[sizeof(current) - 1] = '\0';
        TrimInPlace(current);

        if (current[0] == '\0') {
            line = strtok_s(NULL, "\n", &context);
            continue;
        }

        if (strstr(current, "----") != NULL) {
            line = strtok_s(NULL, "\n", &context);
            continue;
        }

        parsed = sscanf(current, "%63s %959[^\n]", key, value);
        if (parsed >= 2) {
            TrimInPlace(value);
            if (_stricmp(key, "description") == 0) {
                strncpy(description, value, sizeof(description) - 1);
            } else if (_stricmp(key, "identifier") == 0) {
                strncpy(identifier, value, sizeof(identifier) - 1);
            } else if (_stricmp(key, "device") == 0) {
                strncpy(device, value, sizeof(device) - 1);
            } else if (_stricmp(key, "path") == 0) {
                strncpy(path, value, sizeof(path) - 1);
            }
        }

        line = strtok_s(NULL, "\n", &context);
    }

    if (identifier[0] == '\0') {
        return FALSE;
    }

    if (description[0] == '\0') {
        strncpy(description, identifier, sizeof(description) - 1);
    }

    MultiByteToWideChar(CP_ACP, 0, description, -1, entry->name, 256);
    MultiByteToWideChar(CP_ACP, 0, device, -1, entry->devicePath, 512);
    MultiByteToWideChar(CP_ACP, 0, path, -1, entry->filePath, 512);
    MultiByteToWideChar(CP_ACP, 0, identifier, -1, entry->guid, 64);

    entry->id = ComputeEntryIdFromGuid(entry->guid);
    entry->active = TRUE;
    entry->isFirmwareRegistered = (source == ENTRY_SOURCE_FIRMWARE);
    entry->next = NULL;

    return TRUE;
}

static BOOL AddBcdEntries(UEFI_BOOT_LIST* list, const CHAR* output, BCD_ENTRY_SOURCE source) {
    const CHAR* pos = output;
    UEFI_BOOT_ENTRY* tail;

    if (!list || !output) return FALSE;

    tail = list->entries;
    while (tail && tail->next) {
        tail = tail->next;
    }

    while (pos && *pos) {
        CHAR block[8192] = {0};
        int blockLen = 0;

        while (*pos) {
            if ((*pos == '\r' && *(pos + 1) == '\n' && *(pos + 2) == '\r' && *(pos + 3) == '\n') ||
                (*pos == '\n' && *(pos + 1) == '\n')) {
                while (*pos == '\r' || *pos == '\n') {
                    pos++;
                }
                break;
            }

            if (blockLen < (int)sizeof(block) - 1) {
                block[blockLen++] = *pos;
            }
            pos++;
        }

        if (blockLen > 0) {
            UEFI_BOOT_ENTRY* entry = (UEFI_BOOT_ENTRY*)calloc(1, sizeof(UEFI_BOOT_ENTRY));
            if (entry && ParseBcdEntry(block, entry, source)) {
                if (!list->entries) {
                    list->entries = entry;
                } else {
                    tail->next = entry;
                }
                tail = entry;
                list->count++;
            } else if (entry) {
                free(entry);
            }
        }
    }

    return TRUE;
}

static void DeduplicateEntriesByGuid(UEFI_BOOT_LIST* list) {
    UEFI_BOOT_ENTRY* current;

    if (!list) return;

    current = list->entries;
    while (current) {
        UEFI_BOOT_ENTRY* prev = current;
        UEFI_BOOT_ENTRY* check = current->next;

        while (check) {
            if (wcslen(current->guid) > 0 && _wcsicmp(current->guid, check->guid) == 0) {
                UEFI_BOOT_ENTRY* duplicate = check;
                prev->next = check->next;
                if (duplicate->isFirmwareRegistered) {
                    current->isFirmwareRegistered = TRUE;
                }
                check = prev->next;
                free(duplicate);
                if (list->count > 0) {
                    list->count--;
                }
                continue;
            }

            prev = check;
            check = check->next;
        }

        current = current->next;
    }
}

// 扫描所有启动项
UEFI_BOOT_LIST* UefiScanBootEntries(void) {
    CHAR output[65536] = {0};
    UEFI_BOOT_LIST* list = (UEFI_BOOT_LIST*)calloc(1, sizeof(UEFI_BOOT_LIST));

    if (!list) return NULL;

    if (ExecuteCommand(L"bcdedit /enum firmware", output, sizeof(output))) {
        AddBcdEntries(list, output, ENTRY_SOURCE_FIRMWARE);
    }

    ZeroMemory(output, sizeof(output));
    if (ExecuteCommand(L"bcdedit /enum BOOTAPP", output, sizeof(output))) {
        AddBcdEntries(list, output, ENTRY_SOURCE_BOOTAPP);
    }

    DeduplicateEntriesByGuid(list);

    return list;
}

// 释放启动项列表
void UefiFreeBootList(UEFI_BOOT_LIST* list) {
    if (!list) return;
    
    UEFI_BOOT_ENTRY* entry = list->entries;
    while (entry) {
        UEFI_BOOT_ENTRY* next = entry->next;
        free(entry);
        entry = next;
    }
    
    if (list->bootOrder) {
        free(list->bootOrder);
    }
    
    free(list);
}

// 获取 BootOrder
BOOL UefiGetBootOrder(DWORD** bootOrder, DWORD* count) {
    // Windows 不直接暴露 BootOrder，需要从 bcdedit 解析
    *bootOrder = NULL;
    *count = 0;
    return FALSE;
}

// 设置 BootOrder
BOOL UefiSetBootOrder(const DWORD* bootOrder, DWORD count) {
    (void)bootOrder;
    (void)count;
    return FALSE;
}

// 添加启动项
BOOL UefiAddBootEntry(const WCHAR* name, const WCHAR* devicePath, const WCHAR* filePath, DWORD* newId) {
    WCHAR cmd[2048];
    WCHAR normalizedDevice[256] = {0};
    WCHAR normalizedPath[512] = {0};

    OutputDebugStringW(L"[DEBUG] UefiAddBootEntry called\n");
    OutputDebugStringW(name ? name : L"(null)");
    OutputDebugStringW(L"\n");
    OutputDebugStringW(filePath ? filePath : L"(null)");
    OutputDebugStringW(L"\n");

    if (!name || wcslen(name) == 0) return FALSE;

    if (!UefiIsAdmin()) {
        OutputDebugStringW(L"[DEBUG] UefiAddBootEntry aborted: not running as admin\n");
        return FALSE;
    }

    if (devicePath && wcslen(devicePath) > 0) {
        wcsncpy(normalizedDevice, devicePath, 255);
        normalizedDevice[255] = L'\0';
    }

    if (filePath && wcslen(filePath) > 0) {
        // 支持传入 X:\EFI\xxx.efi，自动拆分成 device + path
        if (filePath[0] >= L'A' && filePath[0] <= L'Z' && filePath[1] == L':' && filePath[2] == L'\\') {
            if (wcslen(normalizedDevice) == 0) {
                swprintf(normalizedDevice, 256, L"partition=%c:", filePath[0]);
            }
            // 路径从第 3 个字符开始（跳过 X:\）
            swprintf(normalizedPath, 512, L"\\%s", filePath + 3);
            
            // 调试输出
            OutputDebugStringW(L"[DEBUG] Split path: device=");
            OutputDebugStringW(normalizedDevice);
            OutputDebugStringW(L", path=");
            OutputDebugStringW(normalizedPath);
            OutputDebugStringW(L"\n");
        } else {
            wcsncpy(normalizedPath, filePath, 511);
            normalizedPath[511] = L'\0';
        }
    }

    // EFI 程序应使用 BOOTAPP 类型
    swprintf(cmd, 2048, L"bcdedit /create /d \"%s\" /application BOOTAPP", name);

    CHAR output[4096] = {0};
    BOOL result = ExecuteCommand(cmd, output, sizeof(output));

    WCHAR debugMsg[1024];
    swprintf(debugMsg, 1024, L"[DEBUG] cmd=%s, result=%d, output=%S\n", cmd, result, output);
    OutputDebugStringW(debugMsg);

    if (!result) {
        return FALSE;
    }

    // 解析返回 GUID: {xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx}
    WCHAR guid[64] = {0};
    if (!ExtractGuidFromOutput(output, guid, 64)) {
        OutputDebugStringW(L"[DEBUG] failed to parse GUID from bcdedit output\n");
        return FALSE;
    }

    // device/path 需同时正确设置，否则条目不会生效
    if (wcslen(normalizedDevice) > 0) {
        swprintf(cmd, 2048, L"bcdedit /set %s device %s", guid, normalizedDevice);
        result = ExecuteCommand(cmd, output, sizeof(output));
        swprintf(debugMsg, 1024, L"[DEBUG] cmd=%s, result=%d, output=%S\n", cmd, result, output);
        OutputDebugStringW(debugMsg);
        if (!result) {
            CleanupBootEntryByGuid(guid);
            return FALSE;
        }
    }

    if (wcslen(normalizedPath) > 0) {
        swprintf(cmd, 2048, L"bcdedit /set %s path \"%s\"", guid, normalizedPath);
        result = ExecuteCommand(cmd, output, sizeof(output));
        swprintf(debugMsg, 1024, L"[DEBUG] cmd=%s, result=%d, output=%S\n", cmd, result, output);
        OutputDebugStringW(debugMsg);
        if (!result) {
            CleanupBootEntryByGuid(guid);
            return FALSE;
        }
    }

    // 将新条目加入 firmware displayorder，保证在列表中可见
    swprintf(cmd, 2048, L"bcdedit /set {fwbootmgr} displayorder %s /addlast", guid);
    result = ExecuteCommand(cmd, output, sizeof(output));
    swprintf(debugMsg, 1024, L"[DEBUG] cmd=%s, result=%d, output=%S\n", cmd, result, output);
    OutputDebugStringW(debugMsg);

    if (!result) {
        WCHAR fwError[1024];
        swprintf(fwError, 1024, L"[ERROR] Failed to register %s into {fwbootmgr} displayorder. bcdedit output: %S\n", guid, output);
        OutputDebugStringW(fwError);
        CleanupBootEntryByGuid(guid);
        return FALSE;
    }

    if (newId) *newId = ComputeEntryIdFromGuid(guid);
    return TRUE;
}

// 通过 ID 查找条目对应的 GUID
static BOOL FindEntryGuidById(UEFI_BOOT_LIST* list, DWORD id, WCHAR* guid, DWORD guidSize) {
    if (!list || !guid || guidSize < 64) return FALSE;
    
    UEFI_BOOT_ENTRY* entry = list->entries;
    while (entry) {
        if (entry->id == id) {
            if (wcslen(entry->guid) > 0 && wcslen(entry->guid) < guidSize) {
                wcsncpy(guid, entry->guid, guidSize - 1);
                guid[guidSize - 1] = L'\0';
                return TRUE;
            }

            // 兼容旧数据：从 devicePath 或 filePath 提取 GUID
            const WCHAR* start = wcsstr(entry->devicePath, L"{");
            if (!start) start = wcsstr(entry->filePath, L"{");
            if (start) {
                const WCHAR* end = wcsstr(start, L"}");
                if (end) {
                    size_t len = end - start + 1;
                    if (len < guidSize) {
                        wcsncpy(guid, start, len);
                        guid[len] = L'\0';
                        return TRUE;
                    }
                }
            }
        }
        entry = entry->next;
    }
    return FALSE;
}

// 删除启动项
BOOL UefiDeleteBootEntry(DWORD id) {
    if (id == 0) return FALSE;
    
    // 扫描当前启动项列表以获取 GUID
    UEFI_BOOT_LIST* list = UefiScanBootEntries();
    if (!list) return FALSE;
    
    WCHAR guid[64] = {0};
    BOOL found = FindEntryGuidById(list, id, guid, 64);
    UefiFreeBootList(list);
    
    if (!found) {
        // 如果找不到 GUID，尝试直接使用 ID 构造 bootXXXX 格式
        swprintf(guid, 64, L"{boot%04X}", id);
    }
    
    // 执行 bcdedit /delete {guid}
    WCHAR cmd[512];
    swprintf(cmd, 512, L"bcdedit /delete %s", guid);
    
    CHAR output[4096] = {0};
    return ExecuteCommand(cmd, output, sizeof(output));
}

// 上移启动项
BOOL UefiMoveBootEntryUp(UEFI_BOOT_LIST* list, DWORD id) {
    (void)list;
    (void)id;
    return FALSE;
}

// 下移启动项
BOOL UefiMoveBootEntryDown(UEFI_BOOT_LIST* list, DWORD id) {
    (void)list;
    (void)id;
    return FALSE;
}

// 设为默认启动项
BOOL UefiSetDefaultBootEntry(UEFI_BOOT_LIST* list, DWORD id) {
    if (id == 0) return FALSE;
    
    WCHAR guid[64] = {0};
    
    // 如果提供了列表，从列表中查找 GUID
    if (list) {
        if (!FindEntryGuidById(list, id, guid, 64)) {
            // 找不到 GUID，使用 bootXXXX 格式
            swprintf(guid, 64, L"{boot%04X}", id);
        }
    } else {
        // 没有列表，扫描获取
        UEFI_BOOT_LIST* scanList = UefiScanBootEntries();
        if (scanList) {
            FindEntryGuidById(scanList, id, guid, 64);
            UefiFreeBootList(scanList);
        }
        if (wcslen(guid) == 0) {
            swprintf(guid, 64, L"{boot%04X}", id);
        }
    }
    
    // 执行 bcdedit /default {guid}
    WCHAR cmd[512];
    swprintf(cmd, 512, L"bcdedit /default %s", guid);
    
    CHAR output[4096] = {0};
    return ExecuteCommand(cmd, output, sizeof(output));
}

// 导出 NVRAM
BOOL UefiExportNVRAM(const WCHAR* filePath) {
    if (!filePath) return FALSE;
    
    // 确保目录存在
    WCHAR dir[MAX_PATH];
    wcsncpy(dir, filePath, MAX_PATH);
    WCHAR* lastSlash = wcsrchr(dir, L'\\');
    if (lastSlash) {
        *lastSlash = L'\0';
        CreateDirectoryW(dir, NULL);
    }
    
    WCHAR cmd[1024];
    swprintf(cmd, 1024, L"bcdedit /export \"%s\"", filePath);
    
    SHELLEXECUTEINFOW sei = {0};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.lpVerb = L"runas";
    sei.lpFile = L"cmd.exe";
    sei.lpParameters = cmd;
    sei.nShow = SW_HIDE;
    
    if (ShellExecuteExW(&sei)) {
        WaitForSingleObject(sei.hProcess, 30000);
        DWORD exitCode;
        GetExitCodeProcess(sei.hProcess, &exitCode);
        CloseHandle(sei.hProcess);
        return (exitCode == 0);
    }
    
    return FALSE;
}

// 导入 NVRAM
BOOL UefiImportNVRAM(const WCHAR* filePath) {
    if (!filePath) return FALSE;
    
    WCHAR cmd[1024];
    swprintf(cmd, 1024, L"bcdedit /import \"%s\" /clean", filePath);
    
    SHELLEXECUTEINFOW sei = {0};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.lpVerb = L"runas";
    sei.lpFile = L"cmd.exe";
    sei.lpParameters = cmd;
    sei.nShow = SW_HIDE;
    
    if (ShellExecuteExW(&sei)) {
        WaitForSingleObject(sei.hProcess, 30000);
        DWORD exitCode;
        GetExitCodeProcess(sei.hProcess, &exitCode);
        CloseHandle(sei.hProcess);
        return (exitCode == 0);
    }
    
    return FALSE;
}

// 获取启动项名称
const WCHAR* UefiGetEntryName(UEFI_BOOT_ENTRY* entry) {
    return entry ? entry->name : L"";
}

// 获取启动项路径
const WCHAR* UefiGetEntryPath(UEFI_BOOT_ENTRY* entry) {
    return entry ? entry->filePath : L"";
}