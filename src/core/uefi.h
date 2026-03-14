/**
 * UEFI Boot Entry Management
 * 真正能用的 UEFI 启动项管理模块
 */

#ifndef UEFI_H
#define UEFI_H

#include <windows.h>
#include <stdio.h>

// UEFI 启动项结构
typedef struct _UEFI_BOOT_ENTRY {
    DWORD id;                      // BootXXXX 中的 XXXX
    WCHAR name[256];               // 启动项名称
    WCHAR devicePath[512];         // 设备路径
    WCHAR filePath[512];           // EFI 文件路径
    WCHAR guid[64];                // BCD identifier GUID
    BOOL active;                   // 是否激活
    BOOL isFirmwareRegistered;     // 是否注册到 firmware
    struct _UEFI_BOOT_ENTRY* next; // 链表指针
} UEFI_BOOT_ENTRY;

// 启动项链表
typedef struct _UEFI_BOOT_LIST {
    UEFI_BOOT_ENTRY* entries;
    DWORD count;
    DWORD* bootOrder;              // BootOrder 数组
    DWORD bootOrderCount;
} UEFI_BOOT_LIST;

// 权限检查
BOOL UefiIsAdmin(void);
BOOL UefiRequestAdmin(HWND hWnd);

// 启动项操作
UEFI_BOOT_LIST* UefiScanBootEntries(void);
void UefiFreeBootList(UEFI_BOOT_LIST* list);

// 启动项修改
BOOL UefiAddBootEntry(const WCHAR* name, const WCHAR* devicePath, const WCHAR* filePath, DWORD* newId);
BOOL UefiDeleteBootEntry(DWORD id);

// BootOrder 操作
BOOL UefiGetBootOrder(DWORD** bootOrder, DWORD* count);
BOOL UefiSetBootOrder(const DWORD* bootOrder, DWORD count);
BOOL UefiMoveBootEntryUp(UEFI_BOOT_LIST* list, DWORD id);
BOOL UefiMoveBootEntryDown(UEFI_BOOT_LIST* list, DWORD id);
BOOL UefiSetDefaultBootEntry(UEFI_BOOT_LIST* list, DWORD id);

// 导入导出
BOOL UefiExportNVRAM(const WCHAR* filePath);
BOOL UefiImportNVRAM(const WCHAR* filePath);

// 辅助函数
const WCHAR* UefiGetEntryName(UEFI_BOOT_ENTRY* entry);
const WCHAR* UefiGetEntryPath(UEFI_BOOT_ENTRY* entry);

#endif // UEFI_H