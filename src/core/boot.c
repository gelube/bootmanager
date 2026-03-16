/**
 * BootMgr Boot Entry Management - Implementation
 * 閫氳繃 bcdedit 瑙ｆ瀽鍜岀鐞?BootMgr 鍚姩椤?
 */

#include "../../include/boot.h"
#include "../../include/bcdedit.h"
#include "../../include/uefi_nvram.h"
#include <wchar.h>
#include <stdlib.h>
#include <string.h>

#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "shell32.lib")

// 绠＄悊鍛樻潈闄愭鏌?
BOOL BootMgrIsAdmin(void) {
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

// 璇锋眰绠＄悊鍛樻潈闄?
BOOL BootMgrRequestAdmin(HWND hWnd) {
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

// 鎵ц鍛戒护骞舵崟鑾疯緭鍑?
static BOOL FindEntryGuidById(BOOTMGR_BOOT_LIST* list, DWORD id, WCHAR* guid, DWORD guidSize);

static BOOL ExecuteCommand(const WCHAR* cmd, CHAR* output, DWORD outputSize) {
    if (cmd && wcsncmp(cmd, L"bcdedit ", 8) == 0) {
        WCHAR wideOutput[8192] = {0};
        BOOL ok = BcdEditExecute(cmd + 8, wideOutput, 8192);
        if (output && outputSize > 0) {
            if (WideCharToMultiByte(CP_ACP, 0, wideOutput, -1, output, (int)outputSize, NULL, NULL) == 0) {
                output[0] = '\0';
            }
        }
        return ok;
    }

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

    // 浣跨敤 cmd /c 鎵ц鍛戒护
    WCHAR fullCmd[2048];
    swprintf(fullCmd, 2048, L"cmd.exe /c %s", cmd);

    if (!CreateProcessW(NULL, fullCmd, NULL, NULL, TRUE,
        CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        CloseHandle(hReadPipe);
        CloseHandle(hWritePipe);
        return FALSE;
    }

    CloseHandle(hWritePipe);

    // 璇诲彇杈撳嚭
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

// 瑙ｆ瀽鍗曚釜 BCD 鏉＄洰锛屾寜 key/value 瀛楁璇诲彇锛岄€傞厤 firmware 鍜?BOOTAPP 杈撳嚭鏍煎紡銆?
static BOOL ParseBcdEntry(const CHAR* block, BOOTMGR_BOOT_ENTRY* entry, BCD_ENTRY_SOURCE source) {
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

static BOOL AddBcdEntries(BOOTMGR_BOOT_LIST* list, const CHAR* output, BCD_ENTRY_SOURCE source) {
    const CHAR* pos = output;
    BOOTMGR_BOOT_ENTRY* tail;

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
            BOOTMGR_BOOT_ENTRY* entry = (BOOTMGR_BOOT_ENTRY*)calloc(1, sizeof(BOOTMGR_BOOT_ENTRY));
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

static void DeduplicateEntriesByGuid(BOOTMGR_BOOT_LIST* list) {
    BOOTMGR_BOOT_ENTRY* current;

    if (!list) return;

    current = list->entries;
    while (current) {
        BOOTMGR_BOOT_ENTRY* prev = current;
        BOOTMGR_BOOT_ENTRY* check = current->next;

        while (check) {
            if (wcslen(current->guid) > 0 && _wcsicmp(current->guid, check->guid) == 0) {
                BOOTMGR_BOOT_ENTRY* duplicate = check;
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

// 鎵弿鎵€鏈夊惎鍔ㄩ」
BOOTMGR_BOOT_LIST* BootMgrScanBootEntries(void) {
    CHAR output[65536] = {0};
    BOOTMGR_BOOT_LIST* list = (BOOTMGR_BOOT_LIST*)calloc(1, sizeof(BOOTMGR_BOOT_LIST));

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

// 閲婃斁鍚姩椤瑰垪琛?
void BootMgrFreeBootList(BOOTMGR_BOOT_LIST* list) {
    if (!list) return;
    
    BOOTMGR_BOOT_ENTRY* entry = list->entries;
    while (entry) {
        BOOTMGR_BOOT_ENTRY* next = entry->next;
        free(entry);
        entry = next;
    }
    
    if (list->bootOrder) {
        free(list->bootOrder);
    }
    
    free(list);
}

// 鑾峰彇 BootOrder
BOOL BootMgrGetBootOrder(DWORD** bootOrder, DWORD* count) {
    WCHAR output[8192] = {0};
    WCHAR* displayOrder = NULL;
    WCHAR* p = NULL;
    DWORD values[256];
    DWORD valueCount = 0;
    DWORD* result = NULL;

    if (!bootOrder || !count) {
        return FALSE;
    }

    *bootOrder = NULL;
    *count = 0;

    if (!BcdEditExecute(L"/enum {fwbootmgr}", output, 8192)) {
        return FALSE;
    }

    displayOrder = wcsstr(output, L"displayorder");
    if (!displayOrder) {
        return FALSE;
    }

    p = displayOrder;
    while ((p = wcschr(p, L'{')) != NULL) {
        WCHAR* end = wcschr(p, L'}');
        WCHAR guid[64] = {0};
        if (!end) {
            break;
        }

        size_t guidLen = (size_t)(end - p + 1);
        if (guidLen >= 8 && guidLen < 64) {
            wcsncpy(guid, p, guidLen);
            guid[guidLen] = L'\0';
            values[valueCount++] = ComputeEntryIdFromGuid(guid);
            if (valueCount >= 256) {
                break;
            }
        }

        p = end + 1;
        if (*p != L' ' && *p != L'\t' && *p != L'\r' && *p != L'\n') {
            break;
        }
    }

    if (valueCount == 0) {
        return FALSE;
    }

    result = (DWORD*)calloc(valueCount, sizeof(DWORD));
    if (!result) {
        return FALSE;
    }

    memcpy(result, values, valueCount * sizeof(DWORD));
    *bootOrder = result;
    *count = valueCount;
    return TRUE;
}

// 璁剧疆 BootOrder
BOOL BootMgrSetBootOrder(const DWORD* bootOrder, DWORD count) {
    BOOTMGR_BOOT_LIST* list = NULL;
    WCHAR cmd[4096] = {0};
    size_t offset = 0;
    DWORD i;
    BOOL foundAny = FALSE;
    CHAR output[4096] = {0};

    if (!bootOrder || count == 0) {
        return FALSE;
    }

    list = BootMgrScanBootEntries();
    if (!list) {
        return FALSE;
    }

    offset = swprintf(cmd, 4096, L"bcdedit /set {fwbootmgr} displayorder");
    for (i = 0; i < count && offset < 4000; ++i) {
        WCHAR guid[64] = {0};
        if (!FindEntryGuidById(list, bootOrder[i], guid, 64)) {
            continue;
        }

        offset += swprintf(cmd + offset, 4096 - offset, L" %s", guid);
        foundAny = TRUE;
    }

    BootMgrFreeBootList(list);

    if (!foundAny) {
        return FALSE;
    }

    return ExecuteCommand(cmd, output, sizeof(output));
}

// 娣诲姞鍚姩椤?
BOOL BootMgrAddBootEntry(const WCHAR* name, const WCHAR* devicePath, const WCHAR* filePath, DWORD* newId) {
    if (!name || wcslen(name) == 0) return FALSE;
    if (!BootMgrIsAdmin()) return FALSE;

    // Normalize EFI path: strip drive letter prefix (X:\EFI\... -> \EFI\...)
    WCHAR efiPath[512] = {0};
    if (filePath && filePath[0] >= L'A' && filePath[0] <= L'Z' && filePath[1] == L':' && filePath[2] == L'\\') {
        swprintf(efiPath, 512, L"\\%s", filePath + 3);
    } else if (filePath) {
        wcsncpy(efiPath, filePath, 511);
    }

    if (!UefiNvramAcquirePrivilege()) return FALSE;

    // Find a free BootXXXX slot
    UINT16 bootNum = 0;
    for (UINT32 n = 0; n <= 0xFFFF; n++) {
        UEFI_BOOT_ENTRY* existing = UefiNvramGetBootEntry((UINT16)n);
        if (!existing) { bootNum = (UINT16)n; break; }
        UefiNvramFreeEntry(existing);
    }

    DWORD blobSize = 0;
    BYTE* blob = UefiNvramBuildLoadOption(name, efiPath, LOAD_OPTION_ACTIVE, &blobSize);
    if (!blob) return FALSE;

    BOOL ok = UefiNvramSetBootEntry(bootNum, blob, blobSize);
    free(blob);
    if (!ok) return FALSE;

    // Append to BootOrder
    UEFI_BOOT_ORDER bo = {0};
    if (UefiNvramGetBootOrder(&bo)) {
        UINT16* newOrder = (UINT16*)malloc((bo.Count + 1) * sizeof(UINT16));
        if (newOrder) {
            memcpy(newOrder, bo.Order, bo.Count * sizeof(UINT16));
            newOrder[bo.Count] = bootNum;
            UefiNvramSetBootOrder(newOrder, bo.Count + 1);
            free(newOrder);
        }
        UefiNvramFreeBootOrder(&bo);
    } else {
        UefiNvramSetBootOrder(&bootNum, 1);
    }

    if (newId) *newId = (DWORD)bootNum;
    return TRUE;
}

// 閫氳繃 ID 鏌ユ壘鏉＄洰瀵瑰簲鐨?GUID
static BOOL FindEntryGuidById(BOOTMGR_BOOT_LIST* list, DWORD id, WCHAR* guid, DWORD guidSize) {
    if (!list || !guid || guidSize < 64) return FALSE;
    
    BOOTMGR_BOOT_ENTRY* entry = list->entries;
    while (entry) {
        if (entry->id == id) {
            if (wcslen(entry->guid) > 0 && wcslen(entry->guid) < guidSize) {
                wcsncpy(guid, entry->guid, guidSize - 1);
                guid[guidSize - 1] = L'\0';
                return TRUE;
            }

            // 鍏煎鏃ф暟鎹細浠?devicePath 鎴?filePath 鎻愬彇 GUID
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

// 鍒犻櫎鍚姩椤?
BOOL BootMgrDeleteBootEntry(DWORD id) {
    if (id == 0) return FALSE;
    
    // 鎵弿褰撳墠鍚姩椤瑰垪琛ㄤ互鑾峰彇 GUID
    BOOTMGR_BOOT_LIST* list = BootMgrScanBootEntries();
    if (!list) return FALSE;
    
    WCHAR guid[64] = {0};
    BOOL found = FindEntryGuidById(list, id, guid, 64);
    BootMgrFreeBootList(list);
    
    if (!found) {
        // 濡傛灉鎵句笉鍒?GUID锛屽皾璇曠洿鎺ヤ娇鐢?ID 鏋勯€?bootXXXX 鏍煎紡
        swprintf(guid, 64, L"{boot%04X}", id);
    }
    
    // 鎵ц bcdedit /delete {guid}
    WCHAR cmd[512];
    swprintf(cmd, 512, L"bcdedit /delete %s", guid);
    
    CHAR output[4096] = {0};
    return ExecuteCommand(cmd, output, sizeof(output));
}

// 涓婄Щ鍚姩椤?
BOOL BootMgrMoveBootEntryUp(BOOTMGR_BOOT_LIST* list, DWORD id) {
    DWORD i;

    if (!list || !list->entries || id == 0) return FALSE;

    if ((!list->bootOrder || list->bootOrderCount == 0) && !BootMgrGetBootOrder(&list->bootOrder, &list->bootOrderCount)) {
        return FALSE;
    }

    for (i = 1; i < list->bootOrderCount; ++i) {
        if (list->bootOrder[i] == id) {
            DWORD tmp = list->bootOrder[i - 1];
            list->bootOrder[i - 1] = list->bootOrder[i];
            list->bootOrder[i] = tmp;
            return BootMgrSetBootOrder(list->bootOrder, list->bootOrderCount);
        }
    }

    return FALSE;
}

// 涓嬬Щ鍚姩椤?
BOOL BootMgrMoveBootEntryDown(BOOTMGR_BOOT_LIST* list, DWORD id) {
    DWORD i;

    if (!list || !list->entries || id == 0) return FALSE;

    if ((!list->bootOrder || list->bootOrderCount == 0) && !BootMgrGetBootOrder(&list->bootOrder, &list->bootOrderCount)) {
        return FALSE;
    }

    for (i = 0; i + 1 < list->bootOrderCount; ++i) {
        if (list->bootOrder[i] == id) {
            DWORD tmp = list->bootOrder[i + 1];
            list->bootOrder[i + 1] = list->bootOrder[i];
            list->bootOrder[i] = tmp;
            return BootMgrSetBootOrder(list->bootOrder, list->bootOrderCount);
        }
    }

    return FALSE;
}

// 璁句负榛樿鍚姩椤?
BOOL BootMgrSetDefaultBootEntry(BOOTMGR_BOOT_LIST* list, DWORD id) {
    if (id == 0) return FALSE;
    
    WCHAR guid[64] = {0};
    
    // 濡傛灉鎻愪緵浜嗗垪琛紝浠庡垪琛ㄤ腑鏌ユ壘 GUID
    if (list) {
        if (!FindEntryGuidById(list, id, guid, 64)) {
            // 鎵句笉鍒?GUID锛屼娇鐢?bootXXXX 鏍煎紡
            swprintf(guid, 64, L"{boot%04X}", id);
        }
    } else {
        // 娌℃湁鍒楄〃锛屾壂鎻忚幏鍙?
        BOOTMGR_BOOT_LIST* scanList = BootMgrScanBootEntries();
        if (scanList) {
            FindEntryGuidById(scanList, id, guid, 64);
            BootMgrFreeBootList(scanList);
        }
        if (wcslen(guid) == 0) {
            swprintf(guid, 64, L"{boot%04X}", id);
        }
    }
    
    // 鎵ц bcdedit /default {guid}
    WCHAR cmd[512];
    swprintf(cmd, 512, L"bcdedit /default %s", guid);
    
    CHAR output[4096] = {0};
    return ExecuteCommand(cmd, output, sizeof(output));
}

// 瀵煎嚭 NVRAM
BOOL BootMgrExportNVRAM(const WCHAR* filePath) {
    if (!filePath) return FALSE;
    
    // 纭繚鐩綍瀛樺湪
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

// 瀵煎叆 NVRAM
BOOL BootMgrImportNVRAM(const WCHAR* filePath) {
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

// 鑾峰彇鍚姩椤瑰悕绉?
const WCHAR* BootMgrGetEntryName(BOOTMGR_BOOT_ENTRY* entry) {
    return entry ? entry->name : L"";
}

// 鑾峰彇鍚姩椤硅矾寰?
const WCHAR* BootMgrGetEntryPath(BOOTMGR_BOOT_ENTRY* entry) {
    return entry ? entry->filePath : L"";
}
