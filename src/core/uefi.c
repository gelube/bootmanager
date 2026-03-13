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
    
    return TRUE;
}

// 解析单个 BCD 条目
static void ParseBcdEntry(const CHAR* block, UEFI_BOOT_ENTRY* entry) {
    // 示例输出:
    // Windows Boot Manager
    // --------------------
    // identifier              {fwbootmgr}
    // displayorder            {bootmgr} {ntldr}
    // timeout                 30
    
    const CHAR* line = block;
    CHAR name[256] = {0};
    CHAR identifier[64] = {0};
    CHAR device[512] = {0};
    CHAR path[512] = {0};
    
    // 解析名称 (第一行)
    const CHAR* endLine = strchr(line, '\n');
    if (endLine) {
        size_t nameLen = endLine - line;
        if (nameLen > 0 && nameLen < sizeof(name)) {
            // 去除分隔线
            const CHAR* dash = strchr(line, '-');
            if (dash && (dash - line) < 5) {
                // 这是分隔线，跳过
            } else {
                memcpy(name, line, nameLen);
                // 去除尾部的回车
                char* cr = strchr(name, '\r');
                if (cr) *cr = '\0';
            }
        }
    }
    
    // 解析各个字段
    const CHAR* pos = block;
    while (pos && *pos) {
        // 查找 identifier
        const CHAR* idPos = strstr(pos, "identifier");
        if (idPos) {
            idPos += 10; // 跳过 "identifier"
            while (*idPos == ' ' || *idPos == '\t') idPos++;
            
            const CHAR* idEnd = strchr(idPos, '\n');
            if (idEnd) {
                size_t len = idEnd - idPos;
                if (len > 0 && len < sizeof(identifier)) {
                    memcpy(identifier, idPos, len);
                    identifier[len] = '\0';
                    char* cr = strchr(identifier, '\r');
                    if (cr) *cr = '\0';
                }
            }
        }
        
        // 查找 device
        const CHAR* devPos = strstr(pos, "device");
        if (devPos && devPos < block + strlen(block)) {
            devPos += 6;
            while (*devPos == ' ' || *devPos == '\t') devPos++;
            
            const CHAR* devEnd = strchr(devPos, '\n');
            if (devEnd) {
                size_t len = devEnd - devPos;
                if (len > 0 && len < sizeof(device)) {
                    memcpy(device, devPos, len);
                    device[len] = '\0';
                    char* cr = strchr(device, '\r');
                    if (cr) *cr = '\0';
                }
            }
        }
        
        // 查找 path
        const CHAR* pathPos = strstr(pos, "path");
        if (pathPos) {
            pathPos += 4;
            while (*pathPos == ' ' || *pathPos == '\t') pathPos++;
            
            const CHAR* pathEnd = strchr(pathPos, '\n');
            if (pathEnd) {
                size_t len = pathEnd - pathPos;
                if (len > 0 && len < sizeof(path)) {
                    memcpy(path, pathPos, len);
                    path[len] = '\0';
                    char* cr = strchr(path, '\r');
                    if (cr) *cr = '\0';
                }
            }
        }
        
        pos = strchr(pos + 1, '\n');
        if (pos) pos++;
    }
    
    // 填充 entry
    if (strlen(name) > 0 && strlen(identifier) > 0) {
        MultiByteToWideChar(CP_UTF8, 0, name, -1, entry->name, 256);
        MultiByteToWideChar(CP_UTF8, 0, device, -1, entry->devicePath, 512);
        MultiByteToWideChar(CP_UTF8, 0, path, -1, entry->filePath, 512);
        
        // 从 identifier 提取 ID (如果是 BootXXXX 格式)
        if (strncmp(identifier, "{bootmgr}", 9) == 0) {
            entry->id = 0x0000;
        } else if (strncmp(identifier, "{fwbootmgr}", 10) == 0) {
            entry->id = 0xFFFF;
        } else if (strncmp(identifier, "{memdiag}", 9) == 0) {
            entry->id = 0xFFFE;
        } else if (strncmp(identifier, "{ntldr}", 7) == 0) {
            entry->id = 0xFFFD;
        } else {
            // 尝试解析 GUID 或其他格式
            entry->id = 0x0001;
        }
        
        entry->active = TRUE;
        entry->next = NULL;
    }
}

// 扫描所有启动项
UEFI_BOOT_LIST* UefiScanBootEntries(void) {
    CHAR output[65536] = {0};  // 64KB 缓冲区
    
    // 执行 bcdedit /enum firmware
    if (!ExecuteCommand(L"bcdedit /enum firmware", output, sizeof(output))) {
        return NULL;
    }
    
    // 创建列表
    UEFI_BOOT_LIST* list = (UEFI_BOOT_LIST*)malloc(sizeof(UEFI_BOOT_LIST));
    if (!list) return NULL;
    
    list->entries = NULL;
    list->count = 0;
    list->bootOrder = NULL;
    list->bootOrderCount = 0;
    
    // 解析输出 - 按空行分割条目
    UEFI_BOOT_ENTRY* tail = NULL;
    CHAR* pos = output;
    CHAR block[8192];
    int blockLen = 0;
    
    while (*pos) {
        // 收集一个条目 (到空行或文件结尾)
        blockLen = 0;
        while (*pos) {
            if (*pos == '\r' && *(pos + 1) == '\n' && *(pos + 2) == '\r') {
                // 空行
                pos += 2;
                break;
            }
            if (*pos == '\n' && *(pos + 1) == '\n') {
                pos++;
                break;
            }
            if (blockLen < (int)sizeof(block) - 1) {
                block[blockLen++] = *pos;
            }
            pos++;
        }
        
        if (blockLen > 10) {
            block[blockLen] = '\0';
            
            // 创建新条目
            UEFI_BOOT_ENTRY* entry = (UEFI_BOOT_ENTRY*)calloc(1, sizeof(UEFI_BOOT_ENTRY));
            if (entry) {
                ParseBcdEntry(block, entry);
                
                if (wcslen(entry->name) > 0) {
                    // 添加到链表
                    if (!list->entries) {
                        list->entries = entry;
                    } else {
                        tail->next = entry;
                    }
                    tail = entry;
                    list->count++;
                } else {
                    free(entry);
                }
            }
        }
    }
    
    // 如果没有找到条目，创建一些示例数据
    if (list->count == 0) {
        // 尝试 bcdedit /enum (不带 firmware)
        ZeroMemory(output, sizeof(output));
        if (ExecuteCommand(L"bcdedit /enum", output, sizeof(output))) {
            pos = output;
            while (*pos) {
                blockLen = 0;
                while (*pos) {
                    if (*pos == '\r' && *(pos + 1) == '\n' && *(pos + 2) == '\r') {
                        pos += 2;
                        break;
                    }
                    if (blockLen < (int)sizeof(block) - 1) {
                        block[blockLen++] = *pos;
                    }
                    pos++;
                }
                
                if (blockLen > 10) {
                    block[blockLen] = '\0';
                    
                    UEFI_BOOT_ENTRY* entry = (UEFI_BOOT_ENTRY*)calloc(1, sizeof(UEFI_BOOT_ENTRY));
                    if (entry) {
                        ParseBcdEntry(block, entry);
                        
                        if (wcslen(entry->name) > 0) {
                            if (!list->entries) {
                                list->entries = entry;
                            } else {
                                tail->next = entry;
                            }
                            tail = entry;
                            list->count++;
                        } else {
                            free(entry);
                        }
                    }
                }
            }
        }
    }
    
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
    
    if (!name) return FALSE;
    
    // 使用 bcdedit 创建新条目
    // bcdedit /create /d "名称" /application bootsector
    swprintf(cmd, 2048, L"bcdedit /create /d \"%s\" /application bootsector", name);
    
    CHAR output[4096] = {0};
    if (!ExecuteCommand(cmd, output, sizeof(output))) {
        return FALSE;
    }
    
    // 解析返回的 GUID
    // {xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx}
    WCHAR guid[64] = {0};
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
    
    // 设置 device 和 path
    if (devicePath && wcslen(devicePath) > 0) {
        swprintf(cmd, 2048, L"bcdedit /set %s device %s", guid, devicePath);
        ExecuteCommand(cmd, output, sizeof(output));
    }
    
    if (filePath && wcslen(filePath) > 0) {
        swprintf(cmd, 2048, L"bcdedit /set %s path %s", guid, filePath);
        ExecuteCommand(cmd, output, sizeof(output));
    }
    
    if (newId) *newId = 1;
    return TRUE;
}

// 通过 ID 查找条目对应的 GUID
static BOOL FindEntryGuidById(UEFI_BOOT_LIST* list, DWORD id, WCHAR* guid, DWORD guidSize) {
    if (!list || !guid || guidSize < 64) return FALSE;
    
    UEFI_BOOT_ENTRY* entry = list->entries;
    while (entry) {
        if (entry->id == id) {
            // 从 devicePath 或 filePath 提取 GUID
            // 格式通常是 {xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx}
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