/**
 * uefi_repair.c - Native UEFI boot repair without bcdboot.exe
 *
 * Mounts the ESP, creates the EFI\Microsoft\Boot directory tree,
 * and copies bootmgfw.efi / bootmgr.efi from Windows\Boot\EFI.
 */

#include "../../include/uefi_repair.h"
#include "../../include/esp.h"
#include <stdio.h>
#include <string.h>

/* Create all intermediate directories (like mkdir -p) */
static void MkdirRecursive(const WCHAR* path)
{
    WCHAR tmp[MAX_PATH];
    wcsncpy(tmp, path, MAX_PATH - 1);
    tmp[MAX_PATH - 1] = L'\0';

    for (WCHAR* p = tmp + 3; *p; p++) {
        if (*p == L'\\') {
            *p = L'\0';
            CreateDirectoryW(tmp, NULL);
            *p = L'\\';
        }
    }
    CreateDirectoryW(tmp, NULL);
}

/* Search common Windows\Boot\EFI locations (normal + PE) */
static BOOL FindBootEFIDir(WCHAR* out, DWORD size)
{
    static const WCHAR* candidates[] = {
        L"C:\\Windows\\Boot\\EFI",
        L"X:\\Windows\\Boot\\EFI",   /* WinPE ramdisk */
        L"D:\\Windows\\Boot\\EFI",
        L"W:\\Windows\\Boot\\EFI",
        NULL
    };
    for (int i = 0; candidates[i]; i++) {
        if (GetFileAttributesW(candidates[i]) != INVALID_FILE_ATTRIBUTES) {
            wcsncpy(out, candidates[i], size - 1);
            out[size - 1] = L'\0';
            return TRUE;
        }
    }
    return FALSE;
}

BOOL RepairUEFI_Native(void)
{
    /* 1. Find Windows\Boot\EFI source */
    WCHAR srcDir[MAX_PATH];
    if (!FindBootEFIDir(srcDir, MAX_PATH))
        return FALSE;   /* caller should show "use install media" */

    /* 2. Mount ESP */
    WCHAR esp[8];
    if (!EspMount(esp, 8))
        return FALSE;

    /* 3. Create directory structure */
    WCHAR bootDir[MAX_PATH];
    swprintf(bootDir, MAX_PATH, L"%s\\EFI\\Microsoft\\Boot", esp);
    MkdirRecursive(bootDir);

    /* 4. Copy EFI boot files */
    static const WCHAR* files[] = {
        L"bootmgfw.efi",
        L"bootmgr.efi",
        NULL
    };

    BOOL anyOk = FALSE;
    for (int i = 0; files[i]; i++) {
        WCHAR src[MAX_PATH], dst[MAX_PATH];
        swprintf(src, MAX_PATH, L"%s\\%s", srcDir, files[i]);
        swprintf(dst, MAX_PATH, L"%s\\%s", bootDir, files[i]);

        if (GetFileAttributesW(src) != INVALID_FILE_ATTRIBUTES) {
            if (CopyFileW(src, dst, FALSE))
                anyOk = TRUE;
        }
    }

    /* 5. Unmount ESP */
    EspUnmount(esp);

    return anyOk;
}
