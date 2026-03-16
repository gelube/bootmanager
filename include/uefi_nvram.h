/**
 * uefi_nvram.h - UEFI NVRAM operations via Windows API
 * Replaces bcdedit.exe dependency; compatible with Windows PE.
 */

#ifndef UEFI_NVRAM_H
#define UEFI_NVRAM_H

#include <windows.h>

// EFI Global Variable GUID string
#define EFI_GLOBAL_VARIABLE_GUID L"{8be4df61-93ca-11d2-aa0d-00e098032b8c}"

// EFI_LOAD_OPTION attributes
#define LOAD_OPTION_ACTIVE          0x00000001
#define LOAD_OPTION_FORCE_RECONNECT 0x00000002
#define LOAD_OPTION_HIDDEN          0x00000008

// EFI Device Path node header
#pragma pack(push, 1)
typedef struct {
    UINT8  Type;
    UINT8  SubType;
    UINT16 Length;
} EFI_DEVICE_PATH_PROTOCOL;
#pragma pack(pop)

// Parsed boot entry (caller-friendly)
typedef struct _UEFI_BOOT_ENTRY {
    UINT16  BootNum;            // 0x0000 - 0xFFFF
    UINT32  Attributes;
    WCHAR   Description[256];
    BYTE*   RawData;            // full EFI_LOAD_OPTION blob (heap-allocated)
    DWORD   RawDataSize;
    struct _UEFI_BOOT_ENTRY* Next;
} UEFI_BOOT_ENTRY;

// BootOrder list
typedef struct {
    UINT16* Order;   // heap-allocated array of BootXXXX numbers
    DWORD   Count;
} UEFI_BOOT_ORDER;

// --- Privilege ---
BOOL UefiNvramAcquirePrivilege(void);

// --- BootOrder ---
BOOL UefiNvramGetBootOrder(UEFI_BOOT_ORDER* out);
BOOL UefiNvramSetBootOrder(const UINT16* order, DWORD count);
void UefiNvramFreeBootOrder(UEFI_BOOT_ORDER* bo);

// --- Boot entries ---
// Returns heap-allocated UEFI_BOOT_ENTRY or NULL on failure. Caller frees with UefiNvramFreeEntry.
UEFI_BOOT_ENTRY* UefiNvramGetBootEntry(UINT16 bootNum);

// Creates or overwrites BootXXXX from a raw EFI_LOAD_OPTION blob.
BOOL UefiNvramSetBootEntry(UINT16 bootNum, const BYTE* data, DWORD size);

// Deletes BootXXXX variable.
BOOL UefiNvramDeleteBootEntry(UINT16 bootNum);

// Scans Boot0000-BootFFFF; returns linked list. Caller frees with UefiNvramFreeEntryList.
UEFI_BOOT_ENTRY* UefiNvramScanAllEntries(void);

void UefiNvramFreeEntry(UEFI_BOOT_ENTRY* entry);
void UefiNvramFreeEntryList(UEFI_BOOT_ENTRY* head);

// --- Helpers ---
// Build a minimal EFI_LOAD_OPTION blob for a simple file-path boot entry.
// filePath: EFI path like \EFI\refind\refind_x64.efi
// partitionGuid: GPT partition GUID string (may be NULL to use a HD() path stub)
// Returns heap-allocated blob; caller frees with free(). Sets *outSize.
BYTE* UefiNvramBuildLoadOption(const WCHAR* description, const WCHAR* filePath,
                                UINT32 attributes, DWORD* outSize);

#endif // UEFI_NVRAM_H
