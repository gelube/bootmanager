#include "backup.h"
#include <wchar.h>
#include <time.h>

// Create backup directory with timestamp
BOOL BackupCreateDirectory(const WCHAR* path) {
    if (!path) return FALSE;
    
    // Create full path recursively
    WCHAR tempPath[MAX_PATH];
    wcsncpy(tempPath, path, MAX_PATH);
    
    for (WCHAR* p = tempPath + 3; *p; p++) {
        if (*p == L'\\' || *p == L'/') {
            WCHAR c = *p;
            *p = L'\0';
            CreateDirectoryW(tempPath, NULL);
            *p = c;
        }
    }
    
    return CreateDirectoryW(tempPath, NULL) || GetLastError() == ERROR_ALREADY_EXISTS;
}

// Generate timestamp string
BOOL BackupGenerateTimestamp(WCHAR* timestamp, DWORD size) {
    SYSTEMTIME st;
    GetLocalTime(&st);
    
    if (!timestamp || size < 20) return FALSE;
    
    swprintf(timestamp, size, L"%04d%02d%02d_%02d%02d%02d",
        st.wYear, st.wMonth, st.wDay,
        st.wHour, st.wMinute, st.wSecond);
    
    return TRUE;
}

// Backup MBR (512 bytes)
BOOL BackupMBR(const WCHAR* drive, const WCHAR* outputPath) {
    HANDLE hDrive, hFile;
    BYTE mbr[512];
    DWORD bytesRead, bytesWritten;
    BOOL result = FALSE;
    
    if (!drive || !outputPath) return FALSE;
    
    // Open physical drive
    WCHAR drivePath[32];
    swprintf(drivePath, 32, L"\\\\.\\%s", drive);
    
    hDrive = CreateFileW(drivePath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL, OPEN_EXISTING, 0, NULL);
    
    if (hDrive == INVALID_HANDLE_VALUE) {
        return FALSE;
    }
    
    // Read MBR
    if (!ReadFile(hDrive, mbr, 512, &bytesRead, NULL) || bytesRead != 512) {
        CloseHandle(hDrive);
        return FALSE;
    }
    
    CloseHandle(hDrive);
    
    // Write to backup file
    hFile = CreateFileW(outputPath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL, NULL);
    
    if (hFile == INVALID_HANDLE_VALUE) {
        return FALSE;
    }
    
    if (WriteFile(hFile, mbr, 512, &bytesWritten, NULL) && bytesWritten == 512) {
        result = TRUE;
    }
    
    CloseHandle(hFile);
    return result;
}

// Restore MBR
BOOL RestoreMBR(const WCHAR* drive, const WCHAR* backupPath) {
    HANDLE hDrive, hFile;
    BYTE mbr[512];
    DWORD bytesRead, bytesWritten;
    BOOL result = FALSE;
    
    if (!drive || !backupPath) return FALSE;
    
    // Read backup file
    hFile = CreateFileW(backupPath, GENERIC_READ, FILE_SHARE_READ, NULL,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    
    if (hFile == INVALID_HANDLE_VALUE) {
        return FALSE;
    }
    
    if (!ReadFile(hFile, mbr, 512, &bytesRead, NULL) || bytesRead != 512) {
        CloseHandle(hFile);
        return FALSE;
    }
    
    CloseHandle(hFile);
    
    // Open physical drive for writing
    WCHAR drivePath[32];
    swprintf(drivePath, 32, L"\\\\.\\%s", drive);
    
    hDrive = CreateFileW(drivePath, GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    
    if (hDrive == INVALID_HANDLE_VALUE) {
        return FALSE;
    }
    
    // Write MBR (preserve partition table if needed)
    if (WriteFile(hDrive, mbr, 512, &bytesWritten, NULL) && bytesWritten == 512) {
        result = TRUE;
    }
    
    CloseHandle(hDrive);
    return result;
}

// Backup BCD using bcdedit
BOOL BackupBCD(const WCHAR* outputPath) {
    WCHAR cmd[1024];
    
    if (!outputPath) return FALSE;
    
    swprintf(cmd, 1024, L"/c bcdedit /export \"%s\"", outputPath);
    
    SHELLEXECUTEINFOW sei = {0};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.lpVerb = L"runas";
    sei.lpFile = L"cmd.exe";
    sei.lpParameters = cmd;
    sei.nShow = SW_HIDE;
    
    if (ShellExecuteExW(&sei)) {
        WaitForSingleObject(sei.hProcess, INFINITE);
        DWORD exitCode;
        GetExitCodeProcess(sei.hProcess, &exitCode);
        CloseHandle(sei.hProcess);
        return (exitCode == 0);
    }
    
    return FALSE;
}

// Restore BCD
BOOL RestoreBCD(const WCHAR* backupPath) {
    WCHAR cmd[1024];
    
    if (!backupPath) return FALSE;
    
    swprintf(cmd, 1024, L"/c bcdedit /import \"%s\" /clean", backupPath);
    
    SHELLEXECUTEINFOW sei = {0};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.lpVerb = L"runas";
    sei.lpFile = L"cmd.exe";
    sei.lpParameters = cmd;
    sei.nShow = SW_HIDE;
    
    if (ShellExecuteExW(&sei)) {
        WaitForSingleObject(sei.hProcess, INFINITE);
        DWORD exitCode;
        GetExitCodeProcess(sei.hProcess, &exitCode);
        CloseHandle(sei.hProcess);
        return (exitCode == 0);
    }
    
    return FALSE;
}

// Backup NVRAM (UEFI boot entries)
BOOL BackupNVRAM(const WCHAR* outputPath) {
    // NVRAM backup is essentially the same as BCD backup on Windows
    // Plus we export firmware entries
    WCHAR cmd[1024];
    WCHAR txtPath[MAX_PATH];
    
    if (!outputPath) return FALSE;
    
    // First do BCD export
    if (!BackupBCD(outputPath)) {
        return FALSE;
    }
    
    // Export firmware entries to text file
    wcsncpy(txtPath, outputPath, MAX_PATH);
    WCHAR* ext = wcsrchr(txtPath, L'.');
    if (ext) {
        wcscpy(ext, L".txt");
    } else {
        wcscat(txtPath, L".txt");
    }
    
    swprintf(cmd, 1024, L"/c bcdedit /enum firmware > \"%s\"", txtPath);
    
    SHELLEXECUTEINFOW sei = {0};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.lpVerb = L"runas";
    sei.lpFile = L"cmd.exe";
    sei.lpParameters = cmd;
    sei.nShow = SW_HIDE;
    
    if (ShellExecuteExW(&sei)) {
        WaitForSingleObject(sei.hProcess, INFINITE);
        CloseHandle(sei.hProcess);
    }
    
    return TRUE;
}

// Restore NVRAM
BOOL RestoreNVRAM(const WCHAR* backupPath) {
    // Restore BCD which includes NVRAM entries
    return RestoreBCD(backupPath);
}

// Full backup
BOOL BackupAll(const WCHAR* backupDir, BACKUP_TYPE types) {
    WCHAR timestamp[32];
    WCHAR path[MAX_PATH];
    BOOL result = TRUE;
    
    if (!backupDir) return FALSE;
    
    BackupGenerateTimestamp(timestamp, 32);
    
    // Create backup directory
    swprintf(path, MAX_PATH, L"%s\\Backup_%s", backupDir, timestamp);
    if (!BackupCreateDirectory(path)) {
        return FALSE;
    }
    
    // Backup MBR
    if (types & BACKUP_MBR) {
        swprintf(path, MAX_PATH, L"%s\\Backup_%s\\mbr.bin", backupDir, timestamp);
        if (!BackupMBR(L"PhysicalDrive0", path)) {
            result = FALSE;
        }
    }
    
    // Backup BCD
    if (types & BACKUP_BCD) {
        swprintf(path, MAX_PATH, L"%s\\Backup_%s\\bcd.bak", backupDir, timestamp);
        if (!BackupBCD(path)) {
            result = FALSE;
        }
    }
    
    // Backup NVRAM
    if (types & BACKUP_NVRAM) {
        swprintf(path, MAX_PATH, L"%s\\Backup_%s\\nvram.bak", backupDir, timestamp);
        if (!BackupNVRAM(path)) {
            result = FALSE;
        }
    }
    
    return result;
}

// Full restore
BOOL RestoreAll(const WCHAR* backupDir, BACKUP_TYPE types) {
    BOOL result = TRUE;
    WCHAR path[MAX_PATH];

    if (!backupDir) return FALSE;

    if (types & BACKUP_MBR) {
        swprintf(path, MAX_PATH, L"%s\\mbr.bin", backupDir);
        if (GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES) {
            result = RestoreMBR(L"PhysicalDrive0", path) && result;
        } else {
            result = FALSE;
        }
    }

    if (types & BACKUP_BCD) {
        swprintf(path, MAX_PATH, L"%s\\bcd.bak", backupDir);
        if (GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES) {
            result = RestoreBCD(path) && result;
        } else {
            result = FALSE;
        }
    }

    if (types & BACKUP_NVRAM) {
        swprintf(path, MAX_PATH, L"%s\\nvram.bak", backupDir);
        if (GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES) {
            result = RestoreNVRAM(path) && result;
        } else {
            result = FALSE;
        }
    }

    return result;
}

static BOOL RunElevatedCommand(const WCHAR* parameters) {
    SHELLEXECUTEINFOW sei = {0};
    DWORD exitCode = 1;

    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.lpVerb = L"runas";
    sei.lpFile = L"cmd.exe";
    sei.lpParameters = parameters;
    sei.nShow = SW_HIDE;

    if (!ShellExecuteExW(&sei)) {
        return FALSE;
    }

    WaitForSingleObject(sei.hProcess, INFINITE);
    GetExitCodeProcess(sei.hProcess, &exitCode);
    CloseHandle(sei.hProcess);
    return exitCode == 0;
}

// Repair using bootrec
BOOL RepairBootRec(const WCHAR* targetDrive) {
    (void)targetDrive;  // Unused parameter

    if (!RunElevatedCommand(L"/c bootrec /fixmbr")) {
        return FALSE;
    }

    // /fixboot 在某些环境会返回拒绝访问，不阻断后续重建流程
    RunElevatedCommand(L"/c bootrec /fixboot");

    if (!RunElevatedCommand(L"/c bootrec /scanos")) {
        return FALSE;
    }

    return RunElevatedCommand(L"/c bootrec /rebuildbcd");
}

// Repair using bcdboot
BOOL RepairBCDBoot(const WCHAR* windowsDir, const WCHAR* targetDrive) {
    WCHAR cmd[1024];
    
    if (!windowsDir || !targetDrive) return FALSE;
    
    swprintf(cmd, 1024, L"/c bcdboot \"%s\" /s %s /f UEFI", windowsDir, targetDrive);
    
    SHELLEXECUTEINFOW sei = {0};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.lpVerb = L"runas";
    sei.lpFile = L"cmd.exe";
    sei.lpParameters = cmd;
    sei.nShow = SW_HIDE;
    
    if (ShellExecuteExW(&sei)) {
        WaitForSingleObject(sei.hProcess, INFINITE);
        DWORD exitCode;
        GetExitCodeProcess(sei.hProcess, &exitCode);
        CloseHandle(sei.hProcess);
        return (exitCode == 0);
    }
    
    return FALSE;
}

// Rebuild BCD
BOOL RepairRebuildBCD(void) {
    SHELLEXECUTEINFOW sei = {0};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.lpVerb = L"runas";
    sei.lpFile = L"cmd.exe";
    sei.lpParameters = L"/c bootrec /rebuildbcd";
    sei.nShow = SW_HIDE;
    
    if (ShellExecuteExW(&sei)) {
        WaitForSingleObject(sei.hProcess, INFINITE);
        DWORD exitCode;
        GetExitCodeProcess(sei.hProcess, &exitCode);
        CloseHandle(sei.hProcess);
        return (exitCode == 0);
    }
    
    return FALSE;
}
