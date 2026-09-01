/**
 * uefi.c - UEFI Boot Entry Management (纯 NVRAM API)
 * 不依赖 bcdedit，兼容 Windows PE
 */

#include "uefi.h"
#include "../../include/uefi_nvram.h"
#include "../../include/esp.h"
#include <wchar.h>
#include <stdlib.h>
#include <string.h>

#pragma comment(lib, "advapi32.lib")

// ============================================
// 权限检查
// ============================================
BOOL UefiIsAdmin(void) {
    BOOL isAdmin = FALSE;
    PSID adminGroup = NULL;
    
    SID_IDENTIFIER_AUTHORITY ntAuth = SECURITY_NT_AUTHORITY;
    if (AllocateAndInitializeSid(&ntAuth, 2, SECURITY_BUILTIN_DOMAIN_RID, 
        DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &adminGroup)) {
        CheckTokenMembership(NULL, adminGroup, &isAdmin);
        FreeSid(adminGroup);
    }
    
    return isAdmin;
}

BOOL UefiRequestAdmin(HWND hWnd) {
    WCHAR exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    
    // Try ShellExecuteExW for UAC (works on Windows, fails gracefully on WinPE)
    SHELLEXECUTEINFOW sei = {0};
    sei.cbSize = sizeof(sei);
    sei.lpVerb = L"runas";
    sei.lpFile = exePath;
    sei.nShow = SW_SHOWNORMAL;
    
    if (ShellExecuteExW(&sei)) {
        PostQuitMessage(0);
        return TRUE;
    }
    
    // WinPE fallback: no UAC, must be run as admin manually
    return FALSE;
}

// ============================================
// 扫描启动项
// ============================================
UEFI_BOOT_LIST* UefiScanBootEntries(void) {
    UEFI_BOOT_LIST* list = NULL;
    UEFI_BOOT_ORDER bo = {0};
    
    // 获取权限
    if (!UefiNvramAcquirePrivilege()) {
        return NULL;
    }
    
    list = (UEFI_BOOT_LIST*)calloc(1, sizeof(UEFI_BOOT_LIST));
    if (!list) return NULL;
    
    // 获取 BootOrder
    if (!UefiNvramGetBootOrder(&bo)) {
        // 无法获取 BootOrder，返回空列表（不是 NULL）
        return list;
    }
    
    if (bo.Count == 0) {
        // BootOrder 为空，释放并返回空列表
        UefiNvramFreeBootOrder(&bo);
        return list;
    }
    
    // 保存 BootOrder
    list->bootOrder = (UINT16*)malloc(bo.Count * sizeof(UINT16));
    if (list->bootOrder) {
        memcpy(list->bootOrder, bo.Order, bo.Count * sizeof(UINT16));
        list->bootOrderCount = bo.Count;
    }
    
    // 遍历 BootOrder，获取每个启动项的信息
    UEFI_BOOT_ENTRY_WRAPPER* tail = NULL;
    
    for (DWORD i = 0; i < bo.Count; i++) {
        UEFI_BOOT_ENTRY* entry = UefiNvramGetBootEntry(bo.Order[i]);
        if (!entry) continue;
        
        UEFI_BOOT_ENTRY_WRAPPER* wrapper = (UEFI_BOOT_ENTRY_WRAPPER*)calloc(1, sizeof(UEFI_BOOT_ENTRY_WRAPPER));
        if (!wrapper) {
            UefiNvramFreeEntry(entry);
            continue;
        }
        
        wrapper->id = entry->BootNum;
        wrapper->active = (entry->Attributes & LOAD_OPTION_ACTIVE) != 0;
        wcsncpy(wrapper->name, entry->Description, 255);
        wrapper->name[255] = L'\0';
        
        // 从 RawData 提取真实 EFI 路径
        // EFI_LOAD_OPTION 结构：
        //   Attributes (4 bytes)
        //   FilePathListLength (2 bytes)
        //   Description (CHAR16 null-terminated)
        //   FilePathList (Device Path)
        if (entry->RawData && entry->RawDataSize > 6) {
            DWORD offset = 6; // 跳过 Attributes + FilePathListLength
            // 跳过 Description（找到 null 终止符）
            const WCHAR* desc = (const WCHAR*)(entry->RawData + offset);
            while (offset + 2 <= entry->RawDataSize && desc[0] != L'\0') {
                offset += 2;
                desc++;
            }
            offset += 2; // 跳过 null 终止符
            
            // 现在 offset 指向 FilePathList
            // 解析 Device Path 节点，寻找 File Path Media Device Path (Type=4, SubType=4)
            wrapper->filePath[0] = L'\0';
            while (offset + 4 <= entry->RawDataSize) {
                UINT8 nodeType = entry->RawData[offset];
                UINT8 nodeSubType = entry->RawData[offset + 1];
                UINT16 nodeLen = entry->RawData[offset + 2] | (entry->RawData[offset + 3] << 8);
                
                if (nodeLen < 4 || offset + nodeLen > entry->RawDataSize) break;
                
                // End of Hardware Device Path (Type=0x7F, SubType=0xFF)
                if (nodeType == 0x7F && nodeSubType == 0xFF) break;
                
                // Media Device Path - File Path (Type=4, SubType=4)
                if (nodeType == 0x04 && nodeSubType == 0x04 && nodeLen > 4) {
                    // File path string starts at offset + 4, CHAR16 format
                    const WCHAR* pathStr = (const WCHAR*)(entry->RawData + offset + 4);
                    DWORD pathChars = (nodeLen - 4) / 2;
                    if (pathChars > 0 && pathStr[0] != L'\0') {
                        DWORD copyLen = (pathChars < 511) ? pathChars : 511;
                        wcsncpy(wrapper->filePath, pathStr, copyLen);
                        wrapper->filePath[copyLen] = L'\0';
                        // 去掉末尾可能的 null
                        while (copyLen > 0 && wrapper->filePath[copyLen - 1] == L'\0') {
                            copyLen--;
                        }
                        wrapper->filePath[copyLen] = L'\0';
                        break; // 找到第一个有效路径就退出
                    }
                }
                
                offset += nodeLen;
            }
            
            // 如果没解析到路径，显示占位符
            if (wrapper->filePath[0] == L'\0') {
                wcsncpy(wrapper->filePath, L"(未知路径)", 511);
            }
        }
        
        if (!list->entries) {
            list->entries = wrapper;
        } else {
            tail->next = wrapper;
        }
        tail = wrapper;
        list->count++;
        
        UefiNvramFreeEntry(entry);
    }
    
    UefiNvramFreeBootOrder(&bo);
    return list;
}

// ============================================
// 释放列表
// ============================================
void UefiFreeBootList(UEFI_BOOT_LIST* list) {
    if (!list) return;
    
    UEFI_BOOT_ENTRY_WRAPPER* entry = list->entries;
    while (entry) {
        UEFI_BOOT_ENTRY_WRAPPER* next = entry->next;
        free(entry);
        entry = next;
    }
    
    if (list->bootOrder) free(list->bootOrder);
    free(list);
}

// ============================================
// 添加启动项
// ============================================
BOOL UefiAddBootEntry(const WCHAR* name, const WCHAR* devicePath, const WCHAR* filePath, DWORD* newId) {
    if (!name || wcslen(name) == 0) return FALSE;
    
    // 获取权限
    if (!UefiNvramAcquirePrivilege()) return FALSE;
    
    // 规范化 EFI 路径
    WCHAR efiPath[512] = {0};
    if (filePath && filePath[1] == L':' && filePath[2] == L'\\') {
        // 去掉盘符前缀
        swprintf(efiPath, 512, L"\\%ls", filePath + 3);
    } else if (filePath) {
        wcsncpy(efiPath, filePath, 511);
    }
    
    // 获取当前 BootOrder
    UEFI_BOOT_ORDER bo = {0};
    UefiNvramGetBootOrder(&bo);
    
    // 找一个空闲的 BootXXXX 槽位
    // 策略：先找不在 BootOrder 中的编号，再找真正不存在的编号
    UINT16 bootNum = 0xFFFF;
    
    // 标记 BootOrder 中已使用的编号
    BOOL usedInOrder[256] = {FALSE};
    for (DWORD i = 0; i < bo.Count && i < 256; i++) {
        if (bo.Order[i] > 0 && bo.Order[i] < 256) {
            usedInOrder[bo.Order[i]] = TRUE;
        }
    }
    
    // 优先找不在 BootOrder 中但变量存在的（可能是之前删除后残留的）
    for (UINT32 n = 1; n <= 0x00FF; n++) {
        if (!usedInOrder[n]) {
            UEFI_BOOT_ENTRY* existing = UefiNvramGetBootEntry((UINT16)n);
            if (existing) {
                // 变量存在但不在 BootOrder 中，可以复用
                UefiNvramFreeEntry(existing);
                bootNum = (UINT16)n;
                break;
            }
        }
    }
    
    // 如果没找到可复用的，找完全不存在的
    if (bootNum == 0xFFFF) {
        for (UINT32 n = 1; n <= 0x00FF; n++) {
            UEFI_BOOT_ENTRY* existing = UefiNvramGetBootEntry((UINT16)n);
            if (!existing) {
                bootNum = (UINT16)n;
                break;
            }
            UefiNvramFreeEntry(existing);
        }
    }
    
    // 如果还没找到，找一个不在 BootOrder 中的强制使用
    if (bootNum == 0xFFFF) {
        for (UINT32 n = 1; n <= 0x00FF; n++) {
            if (!usedInOrder[n]) {
                bootNum = (UINT16)n;
                // 先删除可能存在的旧变量
                UefiNvramDeleteBootEntry(bootNum);
                break;
            }
        }
    }
    
    if (bootNum == 0xFFFF) {
        // 没有空闲槽位
        if (bo.Order) UefiNvramFreeBootOrder(&bo);
        return FALSE;
    }
    
    // 构建 EFI_LOAD_OPTION
    DWORD blobSize = 0;
    BYTE* blob = UefiNvramBuildLoadOption(name, efiPath, LOAD_OPTION_ACTIVE, &blobSize);
    if (!blob) {
        if (bo.Order) UefiNvramFreeBootOrder(&bo);
        return FALSE;
    }
    
    // 写入 NVRAM
    BOOL ok = UefiNvramSetBootEntry(bootNum, blob, blobSize);
    free(blob);
    
    if (!ok) {
        if (bo.Order) UefiNvramFreeBootOrder(&bo);
        return FALSE;
    }
    
    // 更新 BootOrder，新项放第一位
    if (bo.Count > 0) {
        UINT16* newOrder = (UINT16*)malloc((bo.Count + 1) * sizeof(UINT16));
        if (newOrder) {
            newOrder[0] = bootNum;
            for (DWORD i = 0; i < bo.Count; i++) {
                newOrder[i + 1] = bo.Order[i];
            }
            UefiNvramSetBootOrder(newOrder, bo.Count + 1);
            free(newOrder);
        }
        UefiNvramFreeBootOrder(&bo);
    } else {
        // 没有现有 BootOrder
        UefiNvramSetBootOrder(&bootNum, 1);
    }
    
    if (newId) *newId = (DWORD)bootNum;
    return TRUE;
}

// ============================================
// 递归删除目录
// ============================================
static BOOL DeleteDirectoryRecursive(const WCHAR* path)
{
    WCHAR findPath[MAX_PATH];
    WIN32_FIND_DATAW findData;

    swprintf(findPath, MAX_PATH, L"%ls\\*.*", path);

    HANDLE hFind = FindFirstFileW(findPath, &findData);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (wcscmp(findData.cFileName, L".") == 0 ||
                wcscmp(findData.cFileName, L"..") == 0) {
                continue;
            }

            WCHAR filePath[MAX_PATH];
            swprintf(filePath, MAX_PATH, L"%ls\\%ls", path, findData.cFileName);

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
// 删除 ESP 上的 EFI 目录
// 排除系统目录：Microsoft, BOOT, refind, limine
// ============================================
static void DeleteEfiDirectoryFromEsp(const WCHAR* efiPath)
{
    if (!efiPath || wcslen(efiPath) == 0) return;
    
    // 解析路径，提取目录名
    // efiPath 格式: \EFI\DirectoryName\file.efi
    if (wcsncmp(efiPath, L"\\EFI\\", 5) != 0) return;
    
    // 提取 \EFI\ 后面的目录名
    const WCHAR* dirStart = efiPath + 5;
    const WCHAR* dirEnd = wcschr(dirStart, L'\\');
    if (!dirEnd) return;  // 没有子目录，不删除
    
    // 检查目录名长度
    size_t dirLen = dirEnd - dirStart;
    if (dirLen == 0 || dirLen > 64) return;
    
    // 提取目录名
    WCHAR dirName[64] = {0};
    wcsncpy(dirName, dirStart, dirLen);
    
    // 排除系统目录（不区分大小写）
    const WCHAR* protectedDirs[] = {
        L"Microsoft", L"BOOT", L"refind", L"limine", L"Apple", L"ubuntu", L"debian", L"fedora", L"opensuse"
    };
    for (int i = 0; i < sizeof(protectedDirs) / sizeof(protectedDirs[0]); i++) {
        if (_wcsicmp(dirName, protectedDirs[i]) == 0) {
            return;  // 受保护的目录，不删除
        }
    }
    
    // 挂载 ESP
    WCHAR esp[4] = {0};
    if (!EspMount(esp, 4)) {
        return;  // 无法挂载 ESP
    }
    
    // 构建完整目录路径
    WCHAR fullPath[MAX_PATH];
    swprintf(fullPath, MAX_PATH, L"%ls\\EFI\\%ls", esp, dirName);
    
    // 检查目录是否存在
    if (GetFileAttributesW(fullPath) != INVALID_FILE_ATTRIBUTES) {
        DeleteDirectoryRecursive(fullPath);
    }
    
    // 卸载 ESP
    EspUnmount(esp);
}

// ============================================
// 删除启动项
// 同时删除 ESP 上对应的目录
// ============================================
BOOL UefiDeleteBootEntry(DWORD id) {
    if (id == 0) return FALSE;
    
    if (!UefiNvramAcquirePrivilege()) return FALSE;
    
    UINT16 bootNum = (UINT16)id;
    BOOL success = FALSE;
    WCHAR efiPath[512] = {0};
    
    // 先获取启动项的 EFI 路径
    UEFI_BOOT_ENTRY* entry = UefiNvramGetBootEntry(bootNum);
    if (entry && entry->RawData && entry->RawDataSize > 6) {
        // 从 RawData 提取 EFI 路径
        DWORD offset = 6;
        const WCHAR* desc = (const WCHAR*)(entry->RawData + offset);
        while (offset + 2 <= entry->RawDataSize && desc[0] != L'\0') {
            offset += 2;
            desc++;
        }
        offset += 2;
        
        // 解析 Device Path
        while (offset + 4 <= entry->RawDataSize) {
            UINT8 nodeType = entry->RawData[offset];
            UINT8 nodeSubType = entry->RawData[offset + 1];
            UINT16 nodeLen = entry->RawData[offset + 2] | (entry->RawData[offset + 3] << 8);
            
            if (nodeLen < 4 || offset + nodeLen > entry->RawDataSize) break;
            if (nodeType == 0x7F && nodeSubType == 0xFF) break;
            
            if (nodeType == 0x04 && nodeSubType == 0x04 && nodeLen > 4) {
                const WCHAR* pathStr = (const WCHAR*)(entry->RawData + offset + 4);
                DWORD pathChars = (nodeLen - 4) / 2;
                if (pathChars > 0 && pathStr[0] != L'\0') {
                    DWORD copyLen = (pathChars < 511) ? pathChars : 511;
                    wcsncpy(efiPath, pathStr, copyLen);
                    efiPath[copyLen] = L'\0';
                    break;
                }
            }
            offset += nodeLen;
        }
    }
    if (entry) UefiNvramFreeEntry(entry);
    
    // 先从 BootOrder 移除
    UEFI_BOOT_ORDER bo = {0};
    if (UefiNvramGetBootOrder(&bo) && bo.Count > 0) {
        UINT16* newOrder = (UINT16*)malloc(bo.Count * sizeof(UINT16));
        if (newOrder) {
            DWORD newCount = 0;
            for (DWORD i = 0; i < bo.Count; i++) {
                if (bo.Order[i] != bootNum) {
                    newOrder[newCount++] = bo.Order[i];
                }
            }
            // 从 BootOrder 中找到了并移除了
            if (newCount < bo.Count) {
                success = TRUE;
            }
            if (newCount > 0) {
                UefiNvramSetBootOrder(newOrder, newCount);
            }
            free(newOrder);
        }
        UefiNvramFreeBootOrder(&bo);
    }
    
    // 删除 BootXXXX 变量
    if (UefiNvramDeleteBootEntry(bootNum)) {
        success = TRUE;  // NVRAM deletion succeeded
    } else {
        // Variable deletion failed, but if we removed from BootOrder that's still OK
        // Variable residue will be reused on next add
    }
    
    // 删除 ESP 上的 EFI 目录
    if (success && efiPath[0] != L'\0') {
        DeleteEfiDirectoryFromEsp(efiPath);
    }
    
    return success;
}

// ============================================
// BootOrder 操作
// ============================================
BOOL UefiGetBootOrder(UINT16** bootOrder, DWORD* count) {
    if (!bootOrder || !count) return FALSE;
    
    UEFI_BOOT_ORDER bo = {0};
    if (!UefiNvramGetBootOrder(&bo)) return FALSE;
    
    *bootOrder = (UINT16*)malloc(bo.Count * sizeof(UINT16));
    if (!*bootOrder) {
        UefiNvramFreeBootOrder(&bo);
        return FALSE;
    }
    
    memcpy(*bootOrder, bo.Order, bo.Count * sizeof(UINT16));
    *count = bo.Count;
    UefiNvramFreeBootOrder(&bo);
    return TRUE;
}

BOOL UefiSetBootOrder(const UINT16* bootOrder, DWORD count) {
    if (!bootOrder || count == 0) return FALSE;
    return UefiNvramSetBootOrder(bootOrder, count);
}

BOOL UefiMoveBootEntryUp(UEFI_BOOT_LIST* list, DWORD id) {
    if (!list || !list->bootOrder || list->bootOrderCount < 2) return FALSE;
    
    // 找到 id 的位置
    for (DWORD i = 1; i < list->bootOrderCount; i++) {
        if (list->bootOrder[i] == id) {
            // 交换
            UINT16 tmp = list->bootOrder[i - 1];
            list->bootOrder[i - 1] = list->bootOrder[i];
            list->bootOrder[i] = tmp;
            return UefiNvramSetBootOrder(list->bootOrder, list->bootOrderCount);
        }
    }
    
    return FALSE;
}

BOOL UefiMoveBootEntryDown(UEFI_BOOT_LIST* list, DWORD id) {
    if (!list || !list->bootOrder || list->bootOrderCount < 2) return FALSE;
    
    for (DWORD i = 0; i + 1 < list->bootOrderCount; i++) {
        if (list->bootOrder[i] == id) {
            UINT16 tmp = list->bootOrder[i + 1];
            list->bootOrder[i + 1] = list->bootOrder[i];
            list->bootOrder[i] = tmp;
            return UefiNvramSetBootOrder(list->bootOrder, list->bootOrderCount);
        }
    }
    
    return FALSE;
}

BOOL UefiSetDefaultBootEntry(UEFI_BOOT_LIST* list, DWORD id) {
    if (!list || !list->bootOrder || list->bootOrderCount == 0) return FALSE;
    
    // 找到 id 的位置
    DWORD foundIndex = 0xFFFFFFFF;
    for (DWORD i = 0; i < list->bootOrderCount; i++) {
        if (list->bootOrder[i] == id) {
            foundIndex = i;
            break;
        }
    }
    
    if (foundIndex == 0xFFFFFFFF) return FALSE;
    if (foundIndex == 0) return TRUE;  // 已经是第一个
    
    // 移到第一位
    UINT16 tmp = list->bootOrder[foundIndex];
    for (DWORD i = foundIndex; i > 0; i--) {
        list->bootOrder[i] = list->bootOrder[i - 1];
    }
    list->bootOrder[0] = tmp;
    
    return UefiNvramSetBootOrder(list->bootOrder, list->bootOrderCount);
}

// ============================================
// 导入导出（简化版，PE 下可能不可用）
// ============================================
BOOL UefiExportNVRAM(const WCHAR* filePath) {
    // PE 下可能没有 bcdedit，直接返回失败
    return FALSE;
}

BOOL UefiImportNVRAM(const WCHAR* filePath) {
    return FALSE;
}

// ============================================
// 辅助函数
// ============================================
const WCHAR* UefiGetEntryName(UEFI_BOOT_ENTRY_WRAPPER* entry) {
    return entry ? entry->name : L"";
}

const WCHAR* UefiGetEntryPath(UEFI_BOOT_ENTRY_WRAPPER* entry) {
    return entry ? entry->filePath : L"";
}