#ifndef BACKUP_H
#define BACKUP_H

#include <windows.h>
#include <stdio.h>

// Backup types
typedef enum _BACKUP_TYPE {
    BACKUP_MBR = 1,
    BACKUP_BCD = 2,
    BACKUP_NVRAM = 4,
    BACKUP_ALL = 7
} BACKUP_TYPE;

// Function declarations
BOOL BackupCreateDirectory(const WCHAR* path);
BOOL BackupGenerateTimestamp(WCHAR* timestamp, DWORD size);

// MBR operations
BOOL BackupMBR(const WCHAR* drive, const WCHAR* outputPath);
BOOL RestoreMBR(const WCHAR* drive, const WCHAR* backupPath);

// BCD operations
BOOL BackupBCD(const WCHAR* outputPath);
BOOL RestoreBCD(const WCHAR* backupPath);

// NVRAM operations
BOOL BackupNVRAM(const WCHAR* outputPath);
BOOL RestoreNVRAM(const WCHAR* backupPath);

// Full backup/restore
BOOL BackupAll(const WCHAR* backupDir, BACKUP_TYPE types);
BOOL RestoreAll(const WCHAR* backupDir, BACKUP_TYPE types);

// Repair operations
BOOL RepairBootRec(const WCHAR* targetDrive);
BOOL RepairBCDBoot(const WCHAR* windowsDir, const WCHAR* targetDrive);
BOOL RepairRebuildBCD(void);

#endif // BACKUP_H
