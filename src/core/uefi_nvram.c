/**
 * uefi_nvram.c - UEFI NVRAM operations via Windows API
 * Uses GetFirmwareEnvironmentVariableExW / SetFirmwareEnvironmentVariableExW.
 * Requires SE_SYSTEM_ENVIRONMENT_NAME privilege (admin).
 */

#include "../../include/uefi_nvram.h"
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#pragma comment(lib, "advapi32.lib")

// Dynamically resolve the Ex variants (available Win8+/WinPE; fall back to non-Ex on older)
typedef DWORD (WINAPI *PFN_GetFirmwareEnvEx)(LPCWSTR, LPCWSTR, PVOID, DWORD, PDWORD);
typedef BOOL  (WINAPI *PFN_SetFirmwareEnvEx)(LPCWSTR, LPCWSTR, PVOID, DWORD, DWORD);

static PFN_GetFirmwareEnvEx s_getFwEx = NULL;
static PFN_SetFirmwareEnvEx s_setFwEx = NULL;

static void LoadFirmwareAPIs(void) {
    if (s_getFwEx) return;
    HMODULE hKernel = GetModuleHandleW(L"kernel32.dll");
    if (!hKernel) return;
    s_getFwEx = (PFN_GetFirmwareEnvEx)GetProcAddress(hKernel, "GetFirmwareEnvironmentVariableExW");
    s_setFwEx = (PFN_SetFirmwareEnvEx)GetProcAddress(hKernel, "SetFirmwareEnvironmentVariableExW");
    // Fall back to non-Ex variants if Ex not available
    if (!s_getFwEx)
        s_getFwEx = (PFN_GetFirmwareEnvEx)GetProcAddress(hKernel, "GetFirmwareEnvironmentVariableW");
    if (!s_setFwEx)
        s_setFwEx = (PFN_SetFirmwareEnvEx)GetProcAddress(hKernel, "SetFirmwareEnvironmentVariableW");
}

// ---------------------------------------------------------------------------
// Privilege
// ---------------------------------------------------------------------------

BOOL UefiNvramAcquirePrivilege(void) {
    HANDLE hToken;
    TOKEN_PRIVILEGES tp;
    LUID luid;

    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken))
        return FALSE;

    if (!LookupPrivilegeValueW(NULL, SE_SYSTEM_ENVIRONMENT_NAME, &luid)) {
        CloseHandle(hToken);
        return FALSE;
    }

    tp.PrivilegeCount = 1;
    tp.Privileges[0].Luid = luid;
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

    SetLastError(ERROR_SUCCESS);
    BOOL ok = AdjustTokenPrivileges(hToken, FALSE, &tp, sizeof(tp), NULL, NULL);
    DWORD err = GetLastError();
    CloseHandle(hToken);
    
    return ok && (err == ERROR_SUCCESS);
}

// ---------------------------------------------------------------------------
// Internal read/write helpers
// ---------------------------------------------------------------------------

static DWORD NvramRead(LPCWSTR name, BYTE* buf, DWORD bufSize, DWORD* attribs) {
    LoadFirmwareAPIs();
    if (!s_getFwEx) { SetLastError(ERROR_NOT_SUPPORTED); return 0; }
    DWORD attr = 0;
    DWORD ret = s_getFwEx(name, EFI_GLOBAL_VARIABLE_GUID, buf, bufSize, &attr);
    if (attribs) *attribs = attr;
    return ret;
}

static BOOL NvramWrite(LPCWSTR name, const BYTE* data, DWORD size, DWORD attribs) {
    LoadFirmwareAPIs();
    if (!s_setFwEx) { SetLastError(ERROR_NOT_SUPPORTED); return FALSE; }
    return s_setFwEx(name, EFI_GLOBAL_VARIABLE_GUID, (PVOID)data, size, attribs);
}

static BOOL NvramDelete(LPCWSTR name) {
    LoadFirmwareAPIs();
    if (!s_setFwEx) { SetLastError(ERROR_NOT_SUPPORTED); return FALSE; }
    return s_setFwEx(name, EFI_GLOBAL_VARIABLE_GUID, NULL, 0, 0);
}

// ---------------------------------------------------------------------------
// BootOrder
// ---------------------------------------------------------------------------

BOOL UefiNvramGetBootOrder(UEFI_BOOT_ORDER* out) {
    if (!out) return FALSE;
    out->Order = NULL;
    out->Count = 0;

    BYTE buf[1024];
    DWORD bytes = NvramRead(L"BootOrder", buf, sizeof(buf), NULL);
    if (bytes == 0 || bytes % 2 != 0) return FALSE;

    DWORD count = bytes / 2;
    UINT16* arr = (UINT16*)malloc(bytes);
    if (!arr) return FALSE;
    memcpy(arr, buf, bytes);
    out->Order = arr;
    out->Count = count;
    return TRUE;
}

BOOL UefiNvramSetBootOrder(const UINT16* order, DWORD count) {
    if (!order || count == 0) return FALSE;
    // NV | BS | RT attributes for BootOrder
    return NvramWrite(L"BootOrder", (const BYTE*)order, count * sizeof(UINT16), 7);
}

void UefiNvramFreeBootOrder(UEFI_BOOT_ORDER* bo) {
    if (bo) { free(bo->Order); bo->Order = NULL; bo->Count = 0; }
}

// ---------------------------------------------------------------------------
// Boot entry name helper: Boot0001, Boot0002, ...
// ---------------------------------------------------------------------------

static void MakeBootName(UINT16 num, WCHAR* out) {
    swprintf(out, 16, L"Boot%04X", (unsigned)num);
}

// ---------------------------------------------------------------------------
// Parse raw EFI_LOAD_OPTION blob into UEFI_BOOT_ENTRY
// ---------------------------------------------------------------------------

static UEFI_BOOT_ENTRY* ParseLoadOption(UINT16 bootNum, const BYTE* data, DWORD size) {
    if (size < 6) return NULL; // Attributes(4) + FilePathListLength(2) minimum

    UEFI_BOOT_ENTRY* e = (UEFI_BOOT_ENTRY*)calloc(1, sizeof(UEFI_BOOT_ENTRY));
    if (!e) return NULL;

    e->BootNum   = bootNum;
    e->Attributes = *(UINT32*)data;

    // Description starts at offset 6, null-terminated CHAR16
    const WCHAR* desc = (const WCHAR*)(data + 6);
    DWORD maxDescChars = (size - 6) / 2;
    DWORD i;
    for (i = 0; i < maxDescChars && i < 255 && desc[i]; i++)
        e->Description[i] = desc[i];
    e->Description[i] = L'\0';

    e->RawData = (BYTE*)malloc(size);
    if (e->RawData) {
        memcpy(e->RawData, data, size);
        e->RawDataSize = size;
    }

    return e;
}

// ---------------------------------------------------------------------------
// Public entry API
// ---------------------------------------------------------------------------

UEFI_BOOT_ENTRY* UefiNvramGetBootEntry(UINT16 bootNum) {
    WCHAR name[16];
    MakeBootName(bootNum, name);

    BYTE buf[4096];
    DWORD bytes = NvramRead(name, buf, sizeof(buf), NULL);
    if (bytes == 0) return NULL;

    return ParseLoadOption(bootNum, buf, bytes);
}

BOOL UefiNvramSetBootEntry(UINT16 bootNum, const BYTE* data, DWORD size) {
    WCHAR name[16];
    MakeBootName(bootNum, name);
    // NV | BS | RT
    return NvramWrite(name, data, size, 7);
}

BOOL UefiNvramDeleteBootEntry(UINT16 bootNum) {
    WCHAR name[16];
    MakeBootName(bootNum, name);
    return NvramDelete(name);
}

UEFI_BOOT_ENTRY* UefiNvramScanAllEntries(void) {
    UEFI_BOOT_ENTRY* head = NULL;
    UEFI_BOOT_ENTRY* tail = NULL;

    // 只扫描 BootOrder 里的项，不遍历全部 0-0xFFFF
    UEFI_BOOT_ORDER bo = {0};
    if (UefiNvramGetBootOrder(&bo) && bo.Count > 0) {
        for (DWORD i = 0; i < bo.Count; i++) {
            UEFI_BOOT_ENTRY* e = UefiNvramGetBootEntry(bo.Order[i]);
            if (!e) continue;
            if (!head) head = e; else tail->Next = e;
            tail = e;
        }
        UefiNvramFreeBootOrder(&bo);
    }

    return head;
}

void UefiNvramFreeEntry(UEFI_BOOT_ENTRY* entry) {
    if (!entry) return;
    free(entry->RawData);
    free(entry);
}

void UefiNvramFreeEntryList(UEFI_BOOT_ENTRY* head) {
    while (head) {
        UEFI_BOOT_ENTRY* next = head->Next;
        UefiNvramFreeEntry(head);
        head = next;
    }
}

// ---------------------------------------------------------------------------
// Build minimal EFI_LOAD_OPTION blob
// Layout: Attributes(4) | FilePathListLength(2) | Description(variable, CHAR16, null-term)
//         | FilePathList (End-of-Hardware-Device-Path node)
// ---------------------------------------------------------------------------

// Minimal End-of-Hardware-Device-Path node (type=0x7F, subtype=0xFF, length=4)
static const BYTE kEndNode[] = { 0x7F, 0xFF, 0x04, 0x00 };

// File Path Media Device Path node: type=4, subtype=4
// Layout: type(1) | subtype(1) | length(2) | PathString(variable CHAR16)
static BYTE* BuildFilePathNode(const WCHAR* path, DWORD* outSize) {
    DWORD pathBytes = (DWORD)((wcslen(path) + 1) * sizeof(WCHAR));
    DWORD nodeSize  = 4 + pathBytes;
    BYTE* node = (BYTE*)malloc(nodeSize);
    if (!node) return NULL;
    node[0] = 0x04; // Media Device Path
    node[1] = 0x04; // File Path
    node[2] = (BYTE)(nodeSize & 0xFF);
    node[3] = (BYTE)((nodeSize >> 8) & 0xFF);
    memcpy(node + 4, path, pathBytes);
    *outSize = nodeSize;
    return node;
}

BYTE* UefiNvramBuildLoadOption(const WCHAR* description, const WCHAR* filePath,
                                UINT32 attributes, DWORD* outSize) {
    if (!description || !filePath || !outSize) return NULL;

    DWORD descBytes     = (DWORD)((wcslen(description) + 1) * sizeof(WCHAR));
    DWORD fpNodeSize    = 0;
    BYTE* fpNode        = BuildFilePathNode(filePath, &fpNodeSize);
    if (!fpNode) return NULL;

    DWORD filePathListLen = fpNodeSize + sizeof(kEndNode);
    DWORD totalSize = 4 + 2 + descBytes + filePathListLen;

    BYTE* blob = (BYTE*)malloc(totalSize);
    if (!blob) { free(fpNode); return NULL; }

    BYTE* p = blob;
    memcpy(p, &attributes,      4); p += 4;
    UINT16 fpLen = (UINT16)filePathListLen;
    memcpy(p, &fpLen,           2); p += 2;
    memcpy(p, description, descBytes); p += descBytes;
    memcpy(p, fpNode,      fpNodeSize); p += fpNodeSize;
    memcpy(p, kEndNode,    sizeof(kEndNode));

    free(fpNode);
    *outSize = totalSize;
    return blob;
}
