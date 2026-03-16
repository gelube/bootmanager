#include "boot_convert.h"
#include <stdio.h>

BOOL BootConvertIsUEFI(void) {
    BYTE buf[1];
    DWORD size = GetFirmwareEnvironmentVariableW(
        L"BootOrder",
        L"{8be4df61-93ca-11d2-aa0d-00e098032b8c}",
        buf, 0);
    return (size > 0 || GetLastError() != ERROR_INVALID_FUNCTION);
}

BOOL BootConvertIsGPT(int diskNumber) {
    WCHAR path[32];
    swprintf(path, 32, L"\\\\.\\PhysicalDrive%d", diskNumber);

    HANDLE hDisk = CreateFileW(path, GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (hDisk == INVALID_HANDLE_VALUE) return FALSE;

    BYTE mbr[512];
    DWORD read = 0;
    BOOL ok = ReadFile(hDisk, mbr, 512, &read, NULL);
    CloseHandle(hDisk);

    if (!ok || read < 512) return FALSE;

    // MBR signature at 0x1FE; if missing, assume GPT
    if (mbr[0x1FE] != 0x55 || mbr[0x1FF] != 0xAA) return TRUE;

    // Check protective MBR: partition type 0xEE at offset 0x1BE+4
    return (mbr[0x1BE + 4] == 0xEE);
}

static void SetError(WCHAR* error, DWORD errorSize, const WCHAR* msg) {
    if (error && errorSize > 0)
        wcsncpy_s(error, errorSize, msg, _TRUNCATE);
}

BOOL BootConvertMBRtoUEFI(int diskNumber, WCHAR* error, DWORD errorSize) {
    SetError(error, errorSize,
        L"请使用 Windows 安装盘或 mbr2gpt 工具完成 MBR 转 UEFI 转换。");
    return FALSE;
}

BOOL BootConvertUEFItoMBR(int diskNumber, WCHAR* error, DWORD errorSize) {
    SetError(error, errorSize,
        L"请使用 Windows 安装盘或 mbr2gpt 工具完成 UEFI 转 MBR 转换。");
    return FALSE;
}
