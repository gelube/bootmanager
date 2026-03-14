#ifndef UEFI_H
#define UEFI_H

#include "../../include/boot.h"

typedef BOOTMGR_BOOT_ENTRY UEFI_BOOT_ENTRY;
typedef BOOTMGR_BOOT_LIST UEFI_BOOT_LIST;

#define UefiIsAdmin BootMgrIsAdmin
#define UefiRequestAdmin BootMgrRequestAdmin
#define UefiScanBootEntries BootMgrScanBootEntries
#define UefiFreeBootList BootMgrFreeBootList
#define UefiAddBootEntry BootMgrAddBootEntry
#define UefiDeleteBootEntry BootMgrDeleteBootEntry
#define UefiGetBootOrder BootMgrGetBootOrder
#define UefiSetBootOrder BootMgrSetBootOrder
#define UefiMoveBootEntryUp BootMgrMoveBootEntryUp
#define UefiMoveBootEntryDown BootMgrMoveBootEntryDown
#define UefiSetDefaultBootEntry BootMgrSetDefaultBootEntry
#define UefiExportNVRAM BootMgrExportNVRAM
#define UefiImportNVRAM BootMgrImportNVRAM
#define UefiGetEntryName BootMgrGetEntryName
#define UefiGetEntryPath BootMgrGetEntryPath

#endif
