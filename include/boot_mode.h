/**
 * boot_mode.h - 启动模式检测
 * 
 * 综合判断系统启动模式，不能只看 BIOS
 */

#ifndef BOOT_MODE_H
#define BOOT_MODE_H

#include <windows.h>
#include <stdbool.h>

// 启动模式
typedef enum {
    BOOT_MODE_UEFI,         // UEFI + GPT
    BOOT_MODE_MBR,          // Legacy + MBR
    BOOT_MODE_HYBRID,       // UEFI + MBR（混合模式）
    BOOT_MODE_UNKNOWN
} BOOT_MODE;

// 系统启动信息
typedef struct {
    bool isUEFIFirmware;        // BIOS 是否 UEFI
    bool isGPTDisk;             // 系统盘是否 GPT
    bool hasESP;                // 是否有 ESP 分区
    bool hasActiveMBR;          // 是否有活动 MBR 分区
    int systemDisk;             // 系统盘编号
    int activePartition;        // 活动分区编号
    WCHAR espPath[MAX_PATH];    // ESP 路径（如果有）
    BOOT_MODE bootMode;         // 综合判断的启动模式
} BOOT_INFO;

/**
 * 检测系统启动信息
 */
bool BootMode_Detect(BOOT_INFO* info);

/**
 * 获取启动模式显示名称
 */
const WCHAR* BootMode_GetName(BOOT_MODE mode);

/**
 * 检测 BIOS 固件类型
 */
bool BootMode_IsUEFIFirmware(void);

/**
 * 检测磁盘分区表类型
 * @return true = GPT, false = MBR
 */
bool BootMode_IsGPTDisk(int diskNumber);

/**
 * 查找系统盘
 */
int BootMode_FindSystemDisk(void);

/**
 * 查找活动分区
 */
int BootMode_FindActivePartition(int diskNumber);

/**
 * 查找 ESP 分区
 */
bool BootMode_FindESP(WCHAR* driveLetter);

/**
 * Check if running in WinPE environment
 * WinPE has HKLM\SYSTEM\CurrentControlSet\Control\MiniNT key
 */
bool BootMode_IsWinPE(void);

#endif // BOOT_MODE_H