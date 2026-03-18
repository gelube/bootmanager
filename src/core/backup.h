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

// ============================================
// 备份目录工具函数
// ============================================

/**
 * 获取备份目录（程序目录下的 backups 文件夹）
 * 自动创建目录，返回绝对路径
 */
BOOL BackupGetBackupDir(WCHAR* buffer, DWORD size);

/**
 * 获取程序所在目录
 */
BOOL BackupGetAppDir(WCHAR* buffer, DWORD size);

// ============================================
// 目录和时间工具
// ============================================
BOOL BackupCreateDirectory(const WCHAR* path);
BOOL BackupGenerateTimestamp(WCHAR* timestamp, DWORD size);

// ============================================
// MBR 操作
// ============================================
BOOL BackupMBR(const WCHAR* drive, const WCHAR* outputPath);
BOOL RestoreMBR(const WCHAR* drive, const WCHAR* backupPath);

// ============================================
// BCD 操作
// ============================================
BOOL BackupBCD(const WCHAR* outputPath);
BOOL RestoreBCD(const WCHAR* backupPath);

// ============================================
// NVRAM 操作
// ============================================
BOOL BackupNVRAM(const WCHAR* outputPath);
BOOL RestoreNVRAM(const WCHAR* backupPath);

// ============================================
// 完整备份/恢复
// ============================================
BOOL BackupAll(const WCHAR* backupDir, BACKUP_TYPE types);
BOOL RestoreAll(const WCHAR* backupDir, BACKUP_TYPE types);

// ============================================
// 修复操作
// ============================================
BOOL RepairBootRec(const WCHAR* targetDrive);
BOOL RepairBCDBoot(const WCHAR* windowsDir, const WCHAR* targetDrive);
BOOL RepairRebuildBCD(void);

#endif // BACKUP_H
