/**
 * Boot Manager Pro v3 - Dialog Header
 * 添加 EFI 菜单项对话框
 */

#ifndef DIALOG_H
#define DIALOG_H

#include <windows.h>

// 对话框 ID
#define IDD_DIALOG_ADD_EFI  2001

// 控件 ID (2000+ 避免与主窗口冲突)
#define IDC_EDIT_TITLE      2101
#define IDC_COMBO_DISK      2102
#define IDC_COMBO_PARTITION 2103
#define IDC_EDIT_PATH       2104
#define IDC_BTN_BROWSE      2105
#define IDC_STATIC_HINT     2106

// 磁盘信息结构
typedef struct {
    INT diskNumber;         // 磁盘编号
    WCHAR diskName[64];     // 磁盘名称 (显示用)
    BOOL hasESP;            // 是否有 ESP 分区
    WCHAR espDrive[4];      // ESP 盘符
} DISK_INFO;

// 分区信息结构
typedef struct {
    WCHAR driveLetter;      // 盘符
    WCHAR label[128];       // 分区标签
    BOOL isESP;             // 是否为 ESP 分区
    WCHAR fileSystem[32];   // 文件系统
} PARTITION_INFO;

// 对话框数据
typedef struct {
    HWND hDlg;              // 对话框窗口
    HWND hComboDisk;        // 磁盘下拉框句柄
    HWND hComboPart;        // 分区下拉框句柄
    WCHAR menuTitle[256];   // rEFInd 菜单标题
    INT selectedDisk;       // 选中的磁盘索引
    INT selectedPartition;  // 选中的分区索引
    WCHAR filePath[512];    // 启动文件路径
    INT_PTR result;         // 对话框结果 (IDOK/IDCANCEL)
    
    DISK_INFO* disks;       // 磁盘列表
    INT diskCount;          // 磁盘数量
    
    PARTITION_INFO* partitions;  // 分区列表
    INT partitionCount;          // 分区数量
} ADD_EFI_DIALOG_DATA;

// 函数声明
BOOL ShowAddEfiDialog(HWND hParent, WCHAR* outTitle, WCHAR* outPath, WCHAR* outDriveLetter);

// 辅助函数
INT EnumPhysicalDisks(DISK_INFO** disks);
INT EnumEspPartitionsForDisk(INT diskNumber, PARTITION_INFO** partitions);
VOID FreeDiskList(DISK_INFO* disks, INT count);
VOID FreePartitionList(PARTITION_INFO* partitions, INT count);

#endif // DIALOG_H
