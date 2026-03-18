/**
 * limine.h - Limine Bootloader Management
 * 
 * Limine v8.x - 现代化跨平台引导管理器
 * 支持 BIOS/MBR 和 UEFI 两种模式
 */

#pragma once

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================
// 安装状态
// ============================================
typedef enum _LIMINE_STATUS {
    LIMINE_NOT_INSTALLED = 0,
    LIMINE_INSTALLED_MBR,       // 安装到 MBR (BIOS 模式)
    LIMINE_INSTALLED_PBR,       // 安装到 PBR（分区引导记录，BIOS 模式）
    LIMINE_INSTALLED_UEFI,      // 安装到 ESP (UEFI 模式)
    LIMINE_INSTALLED_UNKNOWN
} LIMINE_STATUS;

// ============================================
// 磁盘信息结构
// ============================================
typedef struct _LIMINE_DISK_INFO {
    DWORD index;               // 磁盘索引 (0, 1, 2, ...)
    WCHAR model[128];          // 磁盘型号
    DWORD64 size;              // 磁盘大小 (字节)
    DWORD partitionCount;      // 分区数量
    BOOL isGpt;                // 是否 GPT 磁盘
    BOOL isSystem;             // 是否系统磁盘
} LIMINE_DISK_INFO;

typedef struct _LIMINE_DISK_LIST {
    LIMINE_DISK_INFO* disks;
    DWORD count;
} LIMINE_DISK_LIST;

// ============================================
// 分区信息结构
// ============================================
typedef struct _LIMINE_PARTITION_INFO {
    DWORD diskIndex;           // 所属磁盘索引
    DWORD partitionIndex;      // 分区索引
    WCHAR driveLetter;         // 盘符 (如 'C')
    WCHAR label[64];           // 卷标
    WCHAR fileSystem[32];      // 文件系统 (NTFS, FAT32, etc.)
    DWORD64 size;              // 分区大小
    DWORD64 freeSpace;         // 可用空间
    BOOL isBoot;               // 是否引导分区
    BOOL isSystem;             // 是否系统分区
    BOOL isActive;             // 是否活动分区
} LIMINE_PARTITION_INFO;

typedef struct _LIMINE_PARTITION_LIST {
    LIMINE_PARTITION_INFO* partitions;
    DWORD count;
} LIMINE_PARTITION_LIST;

// ============================================
// 核心安装/卸载功能
// ============================================

/**
 * 检查 Limine 是否已安装
 * @param drive 驱动器路径 (如 "C:")
 * @return 安装状态
 */
LIMINE_STATUS LimineCheckInstalled(const WCHAR* drive);

/**
 * 安装 Limine 到 MBR (BIOS 模式)
 * @param diskIndex 磁盘索引
 * @param limineSource Limine 源文件目录
 * @return 成功返回 TRUE
 */
BOOL LimineInstallToMBR(DWORD diskIndex, const WCHAR* limineSource);

/**
 * 安装 Limine 到 PBR（分区引导记录，BIOS 模式）
 * @param driveLetter 盘符
 * @param limineSource Limine 源文件目录
 * @return 成功返回 TRUE
 */
BOOL LimineInstallToPBR(const WCHAR* driveLetter, const WCHAR* limineSource);

/**
 * 安装 Limine 到 ESP (UEFI 模式)
 * @param espDrive ESP 分区盘符 (如 "S:")
 * @param limineSource Limine 源文件目录
 * @return 成功返回 TRUE
 */
BOOL LimineInstallToUEFI(const WCHAR* espDrive, const WCHAR* limineSource);

/**
 * 智能安装（自动检测当前引导模式）
 * @param limineSource Limine 源文件目录
 * @return 成功返回 TRUE
 */
BOOL LimineInstall(const WCHAR* limineSource);

/**
 * 卸载 Limine
 * @param drive 驱动器路径
 * @return 成功返回 TRUE
 */
BOOL LimineUninstall(const WCHAR* drive);

/**
 * 查找 Limine 源文件
 * @param sourcePath 输出路径
 * @param size 缓冲区大小
 * @return 找到返回 TRUE
 */
BOOL LimineFindSource(WCHAR* sourcePath, DWORD size);

// ============================================
// 磁盘和分区管理
// ============================================

/**
 * 获取磁盘列表
 * @return 磁盘列表，使用后需调用 LimineFreeDiskList 释放
 */
LIMINE_DISK_LIST* LimineGetDiskList(void);

/**
 * 释放磁盘列表
 * @param list 磁盘列表
 */
void LimineFreeDiskList(LIMINE_DISK_LIST* list);

/**
 * 获取分区列表
 * @param diskIndex 磁盘索引
 * @return 分区列表
 */
LIMINE_PARTITION_LIST* LimineGetPartitionList(DWORD diskIndex);

/**
 * 获取所有分区
 * @return 分区列表
 */
LIMINE_PARTITION_LIST* LimineGetAllPartitions(void);

/**
 * 释放分区列表
 * @param list 分区列表
 */
void LimineFreePartitionList(LIMINE_PARTITION_LIST* list);

/**
 * 设置活动分区
 * @param diskIndex 磁盘索引
 * @param partitionIndex 分区索引
 * @return 成功返回 TRUE
 */
BOOL LimineSetActivePartition(DWORD diskIndex, DWORD partitionIndex);

/**
 * 获取系统磁盘索引
 * @return 磁盘索引
 */
DWORD LimineGetSystemDiskIndex(void);

// ============================================
// PBR 备份恢复
// ============================================

/**
 * 备份 PBR
 * @param drive 驱动器路径 (如 "C:")
 * @param outputPath 输出文件路径
 * @return 成功返回 TRUE
 */
BOOL LimineBackupPBR(const WCHAR* drive, const WCHAR* outputPath);

/**
 * 恢复 PBR
 * @param drive 驱动器路径
 * @param backupPath 备份文件路径
 * @return 成功返回 TRUE
 */
BOOL LimineRestorePBR(const WCHAR* drive, const WCHAR* backupPath);

/**
 * 备份 MBR（完整，包含分区表）
 * @param diskIndex 磁盘索引
 * @param outputPath 输出文件路径
 * @return 成功返回 TRUE
 */
BOOL LimineBackupMBRFull(DWORD diskIndex, const WCHAR* outputPath);

/**
 * 恢复 MBR
 * @param diskIndex 磁盘索引
 * @param backupPath 备份文件路径
 * @param restorePartitionTable 是否恢复分区表
 * @return 成功返回 TRUE
 */
BOOL LimineRestoreMBRFull(DWORD diskIndex, const WCHAR* backupPath, BOOL restorePartitionTable);

// ============================================
// 辅助函数
// ============================================

/**
 * 格式化磁盘大小
 * @param bytes 字节数
 * @param buffer 输出缓冲区
 * @param size 缓冲区大小
 */
void LimineFormatSize(DWORD64 bytes, WCHAR* buffer, DWORD size);

/**
 * 获取最后的错误信息
 * @return 错误信息字符串
 */
const WCHAR* LimineGetLastErrorMessage(void);

#ifdef __cplusplus
}
#endif