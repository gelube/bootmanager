/**
 * BootMgr Boot Entry Management
 * 鐪熸鑳界敤鐨?BootMgr 鍚姩椤圭鐞嗘ā鍧?
 */

#ifndef BOOT_H
#define BOOT_H

#include <windows.h>
#include <stdio.h>

// BootMgr 鍚姩椤圭粨鏋?
typedef struct _BOOTMGR_BOOT_ENTRY {
    DWORD id;                      // BootXXXX 涓殑 XXXX
    WCHAR name[256];               // 鍚姩椤瑰悕绉?
    WCHAR devicePath[512];         // 璁惧璺緞
    WCHAR filePath[512];           // EFI 鏂囦欢璺緞
    WCHAR guid[64];                // BCD identifier GUID
    BOOL active;                   // 鏄惁婵€娲?
    BOOL isFirmwareRegistered;     // 鏄惁娉ㄥ唽鍒?firmware
    struct _BOOTMGR_BOOT_ENTRY* next; // 閾捐〃鎸囬拡
} BOOTMGR_BOOT_ENTRY;

// 鍚姩椤归摼琛?
typedef struct _BOOTMGR_BOOT_LIST {
    BOOTMGR_BOOT_ENTRY* entries;
    DWORD count;
    DWORD* bootOrder;              // BootOrder 鏁扮粍
    DWORD bootOrderCount;
} BOOTMGR_BOOT_LIST;

// 鏉冮檺妫€鏌?
BOOL BootMgrIsAdmin(void);
BOOL BootMgrRequestAdmin(HWND hWnd);

// 鍚姩椤规搷浣?
BOOTMGR_BOOT_LIST* BootMgrScanBootEntries(void);
void BootMgrFreeBootList(BOOTMGR_BOOT_LIST* list);

// 鍚姩椤逛慨鏀?
BOOL BootMgrAddBootEntry(const WCHAR* name, const WCHAR* devicePath, const WCHAR* filePath, DWORD* newId);
BOOL BootMgrDeleteBootEntry(DWORD id);

// BootOrder 鎿嶄綔
BOOL BootMgrGetBootOrder(DWORD** bootOrder, DWORD* count);
BOOL BootMgrSetBootOrder(const DWORD* bootOrder, DWORD count);
BOOL BootMgrMoveBootEntryUp(BOOTMGR_BOOT_LIST* list, DWORD id);
BOOL BootMgrMoveBootEntryDown(BOOTMGR_BOOT_LIST* list, DWORD id);
BOOL BootMgrSetDefaultBootEntry(BOOTMGR_BOOT_LIST* list, DWORD id);

// 瀵煎叆瀵煎嚭
BOOL BootMgrExportNVRAM(const WCHAR* filePath);
BOOL BootMgrImportNVRAM(const WCHAR* filePath);

// 杈呭姪鍑芥暟
const WCHAR* BootMgrGetEntryName(BOOTMGR_BOOT_ENTRY* entry);
const WCHAR* BootMgrGetEntryPath(BOOTMGR_BOOT_ENTRY* entry);

#endif // BootMgr_H
