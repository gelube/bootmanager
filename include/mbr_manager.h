/**
 * mbr_manager.h - MBR 引导程序管理
 * 
 * 支持在 MBR 磁盘上安装/切换引导程序
 * 支持 Limine 引导程序
 * PE 兼容
 */

#ifndef MBR_MANAGER_H
#define MBR_MANAGER_H

#include <windows.h>
#include <stdbool.h>

// MBR 引导程序类型
typedef enum {
    MBR_BOOT_WINDOWS,       // Windows NT 6.x MBR
    MBR_BOOT_LIMINE,        // Limine 引导程序
    MBR_BOOT_GRUB4DOS,      // GRUB4DOS
    MBR_BOOT_SYSLINUX,      // SYSLINUX
} MBR_BOOT_TYPE;

// 磁盘信息
typedef struct {
    int diskNumber;             // 磁盘编号
    WCHAR model[128];           // 型号
    DWORD totalSize;            // 总大小 (MB)
    bool isGPT;                 // 是否 GPT
    bool isSystem;              // 是否系统盘
    bool hasActivePartition;    // 是否有活动分区
    int activePartition;        // 活动分区编号
} DISK_INFO;

// 磁盘列表
typedef struct {
    DISK_INFO* disks;
    int count;
    int capacity;
} DISK_LIST;

// ============================================
// 磁盘信息函数
// ============================================

/**
 * 初始化磁盘列表
 */
void MBR_InitDiskList(DISK_LIST* list);

/**
 * 释放磁盘列表
 */
void MBR_FreeDiskList(DISK_LIST* list);

/**
 * 获取所有磁盘信息
 */
bool MBR_GetDisks(DISK_LIST* list, WCHAR* error, DWORD errorSize);

/**
 * 检测磁盘分区类型
 * @return true = GPT, false = MBR
 */
bool MBR_IsDiskGPT(int diskNumber);

/**
 * 检测当前 MBR 引导程序类型
 */
MBR_BOOT_TYPE MBR_DetectBootType(int diskNumber);

// ============================================
// MBR 操作函数
// ============================================

/**
 * 备份 MBR
 * @param diskNumber 磁盘编号
 * @param outputPath 输出文件路径
 * @param error 错误信息
 * @param errorSize 缓冲区大小
 */
bool MBR_Backup(int diskNumber, const WCHAR* outputPath, WCHAR* error, DWORD errorSize);

/**
 * 恢复 MBR
 * @param diskNumber 磁盘编号
 * @param inputPath 输入文件路径
 * @param preservePartTable 是否保留分区表
 * @param error 错误信息
 * @param errorSize 缓冲区大小
 */
bool MBR_Restore(int diskNumber, const WCHAR* inputPath, bool preservePartTable, WCHAR* error, DWORD errorSize);

/**
 * 安装 MBR 引导程序
 * @param diskNumber 磁盘编号
 * @param bootType 引导程序类型
 * @param error 错误信息
 * @param errorSize 缓冲区大小
 */
bool MBR_Install(int diskNumber, MBR_BOOT_TYPE bootType, WCHAR* error, DWORD errorSize);

/**
 * 修复 Windows MBR（使用内置代码，不依赖 bootrec）
 */
bool MBR_RepairWindows(int diskNumber, WCHAR* error, DWORD errorSize);

/**
 * 安装 Limine 引导程序到 MBR
 * @param diskNumber 磁盘编号
 * @param installFiles 是否同时安装 Limine 文件到 ESP/活动分区
 * @param error 错误信息
 * @param errorSize 缓冲区大小
 */
bool MBR_InstallLimine(int diskNumber, bool installFiles, WCHAR* error, DWORD errorSize);

/**
 * 安装 GRUB4DOS 到 MBR
 */
bool MBR_InstallGrub4Dos(int diskNumber, WCHAR* error, DWORD errorSize);

// ============================================
// 引导程序名称
// ============================================

/**
 * 获取引导程序显示名称
 */
const WCHAR* MBR_GetBootTypeName(MBR_BOOT_TYPE type);

#endif // MBR_MANAGER_H