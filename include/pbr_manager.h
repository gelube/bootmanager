/**
 * pbr_manager.h - PBR 引导程序管理
 * 
 * 支持在分区上安装/切换引导程序
 * 支持 BOOTMGR、NTLDR、Limine
 * PE 兼容
 */

#ifndef PBR_MANAGER_H
#define PBR_MANAGER_H

#include <windows.h>
#include <stdbool.h>

// PBR 引导程序类型
typedef enum {
    PBR_BOOT_BOOTMGR,       // Windows Vista+ BOOTMGR
    PBR_BOOT_NTLDR,         // Windows XP NTLDR
    PBR_BOOT_LIMINE,        // Limine
    PBR_BOOT_GRUB,          // GRUB
    PBR_BOOT_SYSLINUX,      // SYSLINUX
} PBR_BOOT_TYPE;

// 分区信息
typedef struct {
    int diskNumber;             // 磁盘编号
    int partitionNumber;        // 分区编号
    WCHAR driveLetter;          // 盘符 (如 'C')
    WCHAR label[64];            // 卷标
    WCHAR fsType[32];           // 文件系统 (NTFS/FAT32/exFAT)
    DWORD64 totalSize;          // 总大小 (字节)
    bool isActive;              // 是否活动分区
    bool isSystem;              // 是否系统分区
    PBR_BOOT_TYPE bootType;     // 当前引导类型
} PARTITION_INFO;

// 分区列表
typedef struct {
    PARTITION_INFO* partitions;
    int count;
    int capacity;
} PARTITION_LIST;

// ============================================
// 分区信息函数
// ============================================

/**
 * 初始化分区列表
 */
void PBR_InitPartitionList(PARTITION_LIST* list);

/**
 * 释放分区列表
 */
void PBR_FreePartitionList(PARTITION_LIST* list);

/**
 * 获取磁盘的所有分区
 */
bool PBR_GetPartitions(int diskNumber, PARTITION_LIST* list, WCHAR* error, DWORD errorSize);

/**
 * 获取所有系统分区
 */
bool PBR_GetSystemPartitions(PARTITION_LIST* list, WCHAR* error, DWORD errorSize);

/**
 * 检测分区引导类型
 */
PBR_BOOT_TYPE PBR_DetectBootType(int diskNumber, int partitionNumber);

/**
 * 获取活动分区
 */
bool PBR_GetActivePartition(int diskNumber, int* partitionNumber, WCHAR* error, DWORD errorSize);

// ============================================
// PBR 操作函数
// ============================================

/**
 * 备份 PBR
 */
bool PBR_Backup(int diskNumber, int partitionNumber, const WCHAR* outputPath, WCHAR* error, DWORD errorSize);

/**
 * 恢复 PBR
 */
bool PBR_Restore(int diskNumber, int partitionNumber, const WCHAR* inputPath, WCHAR* error, DWORD errorSize);

/**
 * 安装 PBR 引导程序
 */
bool PBR_Install(int diskNumber, int partitionNumber, PBR_BOOT_TYPE bootType, WCHAR* error, DWORD errorSize);

/**
 * 安装 BOOTMGR 到分区
 */
bool PBR_InstallBootmgr(int diskNumber, int partitionNumber, WCHAR* error, DWORD errorSize);

/**
 * 安装 NTLDR 到分区
 */
bool PBR_InstallNtldr(int diskNumber, int partitionNumber, WCHAR* error, DWORD errorSize);

/**
 * 安装 Limine 到分区
 */
bool PBR_InstallLimine(int diskNumber, int partitionNumber, WCHAR* error, DWORD errorSize);

/**
 * 设置活动分区
 */
bool PBR_SetActive(int diskNumber, int partitionNumber, WCHAR* error, DWORD errorSize);

/**
 * 获取引导类型名称
 */
const WCHAR* PBR_GetBootTypeName(PBR_BOOT_TYPE type);

#endif // PBR_MANAGER_H