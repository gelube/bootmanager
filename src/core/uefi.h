/**
 * uefi.h - UEFI Boot Entry Management (Pure NVRAM API)
 * 不依赖 bcdedit，直接使用 UEFI NVRAM API
 */

#ifndef UEFI_H
#define UEFI_H

#include <windows.h>
#include "../../include/uefi_nvram.h"

// 启动项结构（兼容旧接口）
typedef struct _UEFI_BOOT_ENTRY_WRAPPER {
    DWORD id;                               // BootXXXX 中的 XXXX
    WCHAR name[256];                        // 启动项名称
    WCHAR devicePath[512];                  // 设备路径
    WCHAR filePath[512];                    // EFI 文件路径
    BOOL active;                            // 是否激活
    struct _UEFI_BOOT_ENTRY_WRAPPER* next;  // 链表指针
} UEFI_BOOT_ENTRY_WRAPPER;

// 启动项链表
typedef struct {
    UEFI_BOOT_ENTRY_WRAPPER* entries;
    DWORD count;
    UINT16* bootOrder;                      // BootOrder 数组
    DWORD bootOrderCount;
} UEFI_BOOT_LIST;

// 权限检查
BOOL UefiIsAdmin(void);
BOOL UefiRequestAdmin(HWND hWnd);

// 启动项操作（纯 NVRAM API）
UEFI_BOOT_LIST* UefiScanBootEntries(void);
void UefiFreeBootList(UEFI_BOOT_LIST* list);

// 添加/删除启动项
BOOL UefiAddBootEntry(const WCHAR* name, const WCHAR* devicePath, const WCHAR* filePath, DWORD* newId);
BOOL UefiDeleteBootEntry(DWORD id);

// BootOrder 操作
BOOL UefiGetBootOrder(UINT16** bootOrder, DWORD* count);
BOOL UefiSetBootOrder(const UINT16* bootOrder, DWORD count);
BOOL UefiMoveBootEntryUp(UEFI_BOOT_LIST* list, DWORD id);
BOOL UefiMoveBootEntryDown(UEFI_BOOT_LIST* list, DWORD id);
BOOL UefiSetDefaultBootEntry(UEFI_BOOT_LIST* list, DWORD id);

// 导入导出
BOOL UefiExportNVRAM(const WCHAR* filePath);
BOOL UefiImportNVRAM(const WCHAR* filePath);

// 辅助函数
const WCHAR* UefiGetEntryName(UEFI_BOOT_ENTRY_WRAPPER* entry);
const WCHAR* UefiGetEntryPath(UEFI_BOOT_ENTRY_WRAPPER* entry);

#endif // UEFI_H