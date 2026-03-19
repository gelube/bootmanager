/**
 * mbr_manager.h - MBR 引导程序管理
 * 
 * 支持在 MBR 磁盘上安装/切换引导程序
 * 支持 Limine、Windows NT、GRUB4DOS
 * PE 兼容
 */

#ifndef MBR_MANAGER_H
#define MBR_MANAGER_H

#include <windows.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================
// 常量定义
// ============================================

#define MAX_DISKS           64
#define MAX_PARTITIONS      128
#define SECTOR_SIZE         512
#define MBR_BOOT_CODE_SIZE  446
#define MBR_PART_TABLE_SIZE 64
#define MBR_SIGNATURE_SIZE  2

// ============================================
// 枚举类型
// ============================================

// MBR 引导程序类型
typedef enum {
    MBR_BOOT_UNKNOWN = 0,
    MBR_BOOT_WINDOWS,       // Windows NT 6.x MBR
    MBR_BOOT_LIMINE,        // Limine 引导程序
    MBR_BOOT_GRUB4DOS,      // GRUB4DOS
    MBR_BOOT_SYSLINUX,      // SYSLINUX
    MBR_BOOT_GRUB2,         // GRUB2
} MBR_BOOT_TYPE;

// 分区类型
typedef enum {
    PART_TYPE_UNKNOWN = 0,
    PART_TYPE_NTFS,
    PART_TYPE_FAT32,
    PART_TYPE_FAT16,
    PART_TYPE_EXFAT,
    PART_TYPE_LINUX,
    PART_TYPE_SWAP,
    PART_TYPE_EFI,
} PARTITION_FILESYSTEM;

// ============================================
// 结构体定义
// ============================================

// 分区信息
typedef struct {
    int diskNumber;             // 所属磁盘编号
    int partitionNumber;        // 分区编号 (1-based)
    BYTE partitionType;         // 分区类型字节
    BOOL isActive;              // 是否活动分区
    BOOL isBootable;            // 是否可引导
    LONGLONG startLBA;          // 起始扇区
    LONGLONG totalSectors;      // 总扇区数
    LONGLONG sizeBytes;         // 大小（字节）
    WCHAR driveLetter;          // 盘符 (0 表示无盘符)
    WCHAR label[64];            // 卷标
    PARTITION_FILESYSTEM fs;    // 文件系统
} MBR_PARTITION_INFO;

// 磁盘信息
typedef struct {
    int diskNumber;             // 磁盘编号
    WCHAR model[128];           // 型号
    WCHAR serial[32];           // 序列号
    LONGLONG sizeBytes;         // 总大小 (字节)
    int partitionCount;         // 分区数量
    BOOL isGPT;                 // 是否 GPT
    BOOL isSystem;              // 是否系统盘
    BOOL isRemovable;           // 是否可移动介质
    int activePartition;        // 活动分区编号 (0 表示无)
    MBR_PARTITION_INFO* partitions;  // 分区列表
} MBR_DISK_INFO;

// 磁盘列表
typedef struct {
    MBR_DISK_INFO* disks;
    int count;
} MBR_DISK_LIST;

// ============================================
// 初始化和清理
// ============================================

/**
 * 初始化磁盘列表
 */
void MBR_InitDiskList(MBR_DISK_LIST* list);

/**
 * 释放磁盘列表
 */
void MBR_FreeDiskList(MBR_DISK_LIST* list);

// ============================================
// 磁盘信息查询
// ============================================

/**
 * 获取所有磁盘信息
 * @param list 输出磁盘列表
 * @param error 错误信息缓冲区
 * @param errorSize 缓冲区大小
 * @return 成功返回 true
 */
bool MBR_GetDisks(MBR_DISK_LIST* list, WCHAR* error, DWORD errorSize);

/**
 * 获取单个磁盘信息
 */
bool MBR_GetDiskInfo(int diskNumber, MBR_DISK_INFO* info, WCHAR* error, DWORD errorSize);

/**
 * 检测磁盘分区类型
 * @return true = GPT, false = MBR
 */
bool MBR_IsDiskGPT(int diskNumber);

/**
 * 检测当前 MBR 引导程序类型
 */
MBR_BOOT_TYPE MBR_DetectBootType(int diskNumber);

/**
 * 获取引导程序显示名称
 */
const WCHAR* MBR_GetBootTypeName(MBR_BOOT_TYPE type);

/**
 * 获取系统磁盘编号
 */
int MBR_GetSystemDiskNumber(void);

// ============================================
// 分区操作
// ============================================

/**
 * 获取磁盘的分区列表
 */
bool MBR_GetPartitions(int diskNumber, MBR_DISK_INFO* info, WCHAR* error, DWORD errorSize);

/**
 * 设置活动分区
 * @param diskNumber 磁盘编号
 * @param partitionNumber 分区编号 (1-based)
 */
bool MBR_SetActivePartition(int diskNumber, int partitionNumber, WCHAR* error, DWORD errorSize);

/**
 * 获取活动分区编号
 * @return 活动分区编号 (1-based)，0 表示无
 */
int MBR_GetActivePartition(int diskNumber);

// ============================================
// MBR 读写操作
// ============================================

/**
 * 备份 MBR（包含分区表）
 * @param diskNumber 磁盘编号
 * @param outputPath 输出文件路径
 */
bool MBR_Backup(int diskNumber, const WCHAR* outputPath, WCHAR* error, DWORD errorSize);

/**
 * 恢复 MBR
 * @param diskNumber 磁盘编号
 * @param inputPath 输入文件路径
 * @param preservePartTable 是否保留分区表（仅恢复引导代码）
 */
bool MBR_Restore(int diskNumber, const WCHAR* inputPath, bool preservePartTable, WCHAR* error, DWORD errorSize);

/**
 * 仅备份 MBR 引导代码（前 446 字节）
 */
bool MBR_BackupBootCode(int diskNumber, const WCHAR* outputPath, WCHAR* error, DWORD errorSize);

/**
 * 仅恢复 MBR 引导代码（保留分区表）
 */
bool MBR_RestoreBootCode(int diskNumber, const WCHAR* inputPath, WCHAR* error, DWORD errorSize);

// ============================================
// PBR（分区引导记录）操作
// ============================================

/**
 * 备份 PBR
 * @param diskNumber 磁盘编号
 * @param partitionNumber 分区编号
 * @param outputPath 输出文件路径
 */
bool MBR_BackupPBR(int diskNumber, int partitionNumber, const WCHAR* outputPath, WCHAR* error, DWORD errorSize);

/**
 * 备份 PBR（通过盘符）
 */
bool MBR_BackupPBRByDrive(WCHAR driveLetter, const WCHAR* outputPath, WCHAR* error, DWORD errorSize);

/**
 * 恢复 PBR
 */
bool MBR_RestorePBR(int diskNumber, int partitionNumber, const WCHAR* inputPath, WCHAR* error, DWORD errorSize);

/**
 * 恢复 PBR（通过盘符）
 */
bool MBR_RestorePBRByDrive(WCHAR driveLetter, const WCHAR* inputPath, WCHAR* error, DWORD errorSize);

// ============================================
// 引导程序安装
// ============================================

/**
 * 安装 MBR 引导程序
 * @param diskNumber 磁盘编号
 * @param bootType 引导程序类型
 */
bool MBR_Install(int diskNumber, MBR_BOOT_TYPE bootType, WCHAR* error, DWORD errorSize);

/**
 * 修复 Windows MBR（使用内置代码，不依赖 bootrec）
 */
bool MBR_RepairWindows(int diskNumber, WCHAR* error, DWORD errorSize);

/**
 * 安装 Limine 引导程序到 MBR
 * @param diskNumber 磁盘编号
 * @param limineSource Limine 源文件目录
 * @param installFiles 是否安装 Limine 文件到活动分区
 */
bool MBR_InstallLimine(int diskNumber, const WCHAR* limineSource, bool installFiles, WCHAR* error, DWORD errorSize);

/**
 * 安装 GRUB4DOS 到 MBR
 */
bool MBR_InstallGrub4Dos(int diskNumber, const WCHAR* grubSource, WCHAR* error, DWORD errorSize);

/**
 * 卸载 Limine（恢复 Windows MBR）
 */
bool MBR_UninstallLimine(int diskNumber, WCHAR* error, DWORD errorSize);

// ============================================
// 辅助函数
// ============================================

/**
 * 格式化磁盘大小
 */
void MBR_FormatSize(LONGLONG bytes, WCHAR* buffer, DWORD size);

/**
 * 获取分区文件系统名称
 */
const WCHAR* MBR_GetFilesystemName(PARTITION_FILESYSTEM fs);

#ifdef __cplusplus
}
#endif

#endif // MBR_MANAGER_H