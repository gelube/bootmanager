#include "backup.h"
#include "../../include/envinfo.h"
#include "../../include/mbr_manager.h"
#include <wchar.h>
#include <time.h>

// ============================================
// 获取程序所在目录
// ============================================
static BOOL GetAppDir(WCHAR* buffer, DWORD size)
{
    if (!buffer || size == 0) return FALSE;
    
    if (GetModuleFileNameW(NULL, buffer, size) == 0) {
        return FALSE;
    }
    
    WCHAR* lastSlash = wcsrchr(buffer, L'\\');
    if (lastSlash) {
        *lastSlash = L'\0';
    }
    
    return TRUE;
}

// ============================================
// 获取备份目录（程序目录下的 backups 文件夹）
// 返回绝对路径，如：C:\Tools\BootManager\backups\
// ============================================
BOOL BackupGetBackupDir(WCHAR* buffer, DWORD size)
{
    if (!buffer || size == 0) return FALSE;
    
    if (!GetAppDir(buffer, size)) {
        return FALSE;
    }
    
    size_t len = wcslen(buffer);
    if (len + 10 >= size) return FALSE;  // 确保空间足够
    
    wcscat(buffer, L"\\backups");
    
    // 确保目录存在
    CreateDirectoryW(buffer, NULL);
    
    return TRUE;
}

// ============================================
// 获取程序所在目录（公开函数）
// ============================================
BOOL BackupGetAppDir(WCHAR* buffer, DWORD size)
{
    return GetAppDir(buffer, size);
}

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
    BYTE backupMbr[512], currentMbr[512];
    DWORD bytesRead, bytesWritten;
    BOOL result = FALSE;
    
    if (!drive || !backupPath) return FALSE;
    
    // Read backup file
    hFile = CreateFileW(backupPath, GENERIC_READ, FILE_SHARE_READ, NULL,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    
    if (hFile == INVALID_HANDLE_VALUE) {
        return FALSE;
    }
    
    if (!ReadFile(hFile, backupMbr, 512, &bytesRead, NULL) || bytesRead != 512) {
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
    
    // Read current MBR to preserve partition table
    if (!ReadFile(hDrive, currentMbr, 512, &bytesRead, NULL) || bytesRead != 512) {
        CloseHandle(hDrive);
        return FALSE;
    }
    
    // Preserve current partition table (bytes 0x1BE-0x1FD) and disk signature
    // Only restore boot code (bytes 0x000-0x1BD) and boot signature (0x1FE-0x1FF)
    memcpy(backupMbr + 0x1BE, currentMbr + 0x1BE, 64);   // Partition table
    memcpy(backupMbr + 0x1B8, currentMbr + 0x1B8, 6);     // Disk signature + reserved
    
    // Write MBR with preserved partition table
    SetFilePointer(hDrive, 0, NULL, FILE_BEGIN);
    if (WriteFile(hDrive, backupMbr, 512, &bytesWritten, NULL) && bytesWritten == 512) {
        result = TRUE;
    }
    
    CloseHandle(hDrive);
    return result;
}

// Backup BCD using bcdedit
BOOL BackupBCD(const WCHAR* outputPath) {
    WCHAR cmd[1024];
    
    if (!outputPath) return FALSE;
    
    swprintf(cmd, 1024, L"cmd.exe /c bcdedit /export \"%s\"", outputPath);
    
    STARTUPINFOW si = {0}; si.cb = sizeof(si); si.dwFlags = STARTF_USESHOWWINDOW; si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi = {0};
    
    if (CreateProcessW(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, 60000);
        DWORD exitCode;
        GetExitCodeProcess(pi.hProcess, &exitCode);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        
        if (exitCode == 0) {
            // Clean up bcdedit log files (.LOG, .LOG1, .LOG2)
            WCHAR logPath[MAX_PATH];
            for (int i = 0; i <= 2; i++) {
                if (i == 0) {
                    swprintf(logPath, MAX_PATH, L"%s.LOG", outputPath);
                } else {
                    swprintf(logPath, MAX_PATH, L"%s.LOG%d", outputPath, i);
                }
                DeleteFileW(logPath);
            }
            return TRUE;
        }
    }
    
    return FALSE;
}

// Restore BCD
BOOL RestoreBCD(const WCHAR* backupPath) {
    WCHAR cmd[1024];
    
    if (!backupPath) return FALSE;
    
    swprintf(cmd, 1024, L"cmd.exe /c bcdedit /import \"%s\" /clean", backupPath);
    
    STARTUPINFOW si = {0}; si.cb = sizeof(si); si.dwFlags = STARTF_USESHOWWINDOW; si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi = {0};
    
    if (CreateProcessW(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, 60000);
        DWORD exitCode;
        GetExitCodeProcess(pi.hProcess, &exitCode);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        return (exitCode == 0);
    }
    
    return FALSE;
}

// Backup NVRAM (UEFI boot entries)
BOOL BackupNVRAM(const WCHAR* outputPath) {
    // NVRAM backup is essentially the same as BCD backup on Windows
    if (!outputPath) return FALSE;
    
    // 直接返回成功，不再生成额外的日志文件
    return BackupBCD(outputPath);
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
        WCHAR drive[40];
        int sysDisk = MBR_GetSystemDiskNumber();
        if (sysDisk < 0) sysDisk = 0;
        swprintf(drive, 40, L"PhysicalDrive%d", sysDisk);
        if (!BackupMBR(drive, path)) {
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
            WCHAR drive[40];
            int sysDisk = MBR_GetSystemDiskNumber();
            if (sysDisk < 0) sysDisk = 0;
            swprintf(drive, 40, L"PhysicalDrive%d", sysDisk);
            result = RestoreMBR(drive, path) && result;
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
    // Use CreateProcessW for WinPE compatibility (ShellExecuteExW unavailable in WinPE)
    STARTUPINFOW si = {0};
    PROCESS_INFORMATION pi = {0};
    DWORD exitCode = 1;

    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    WCHAR cmdLine[1024];
    swprintf(cmdLine, 1024, L"cmd.exe %s", parameters);

    if (!CreateProcessW(NULL, cmdLine, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        return FALSE;
    }

    WaitForSingleObject(pi.hProcess, 60000);
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
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

    if (EnvIsWinPE()) {
        /* PE 下无 UAC/Shell，直接 CreateProcess */
        return RunElevatedCommand(cmd);
    }
    
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
    if (EnvIsWinPE()) {
        return RunElevatedCommand(L"/c bootrec /rebuildbcd");
    }
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
