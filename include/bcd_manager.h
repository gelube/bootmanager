/**
 * bcd_manager.h - BCD 启动菜单管理
 * 
 * 纯 Win32 API 实现，PE 兼容
 * 直接操作 BCD 注册表 Hive，不依赖 bcdedit.exe
 */

#ifndef BCD_MANAGER_H
#define BCD_MANAGER_H

#include <windows.h>
#include <stdbool.h>

// BCD 启动项类型
typedef enum {
    BCD_TYPE_OSLOADER = 0x101,      // Windows 启动项
    BCD_TYPE_BOOTMGR = 0x102,       // 启动管理器
    BCD_TYPE_MEMDIAG = 0x103,       // 内存诊断
    BCD_TYPE_NTLDR = 0x104,         // 旧版 NTLDR
} BCD_ENTRY_TYPE;

// BCD 启动项信息
typedef struct {
    GUID id;                        // 启动项 GUID
    WCHAR idStr[64];                // 原始标识符字符串 (如 {bootmgr} 或真正的 GUID)
    WCHAR name[256];                // 显示名称
    WCHAR path[MAX_PATH];           // 启动路径
    BCD_ENTRY_TYPE type;            // 类型
    bool isDefault;                 // 是否默认
    DWORD timeout;                  // 超时秒数
} BCD_ENTRY;

// BCD 列表
typedef struct {
    BCD_ENTRY* entries;
    int count;
    int capacity;
} BCD_LIST;

// ============================================
// BCD 操作函数
// ============================================

/**
 * 初始化 BCD 列表
 */
void BCD_InitList(BCD_LIST* list);

/**
 * 释放 BCD 列表
 */
void BCD_FreeList(BCD_LIST* list);

/**
 * 获取所有启动项
 * @param list 输出列表
 * @param error 错误信息缓冲区
 * @param errorSize 缓冲区大小
 * @return 成功返回 true
 */
bool BCD_GetEntries(BCD_LIST* list, WCHAR* error, DWORD errorSize);

/**
 * 创建新启动项
 * @param entry 启动项信息
 * @param error 错误信息缓冲区
 * @param errorSize 缓冲区大小
 * @return 成功返回 true
 */
bool BCD_CreateEntry(const BCD_ENTRY* entry, WCHAR* error, DWORD errorSize);

/**
 * 删除启动项
 * @param idStr 启动项标识符字符串 (如 {bootmgr} 或 GUID)
 * @param error 错误信息缓冲区
 * @param errorSize 缓冲区大小
 * @return 成功返回 true
 */
bool BCD_DeleteEntry(const WCHAR* idStr, WCHAR* error, DWORD errorSize);

/**
 * 设置默认启动项
 * @param idStr 启动项标识符字符串
 * @param error 错误信息缓冲区
 * @param errorSize 缓冲区大小
 * @return 成功返回 true
 */
bool BCD_SetDefault(const WCHAR* idStr, WCHAR* error, DWORD errorSize);

/**
 * 设置启动超时
 * @param seconds 超时秒数
 * @param error 错误信息缓冲区
 * @param errorSize 缓冲区大小
 * @return 成功返回 true
 */
bool BCD_SetTimeout(DWORD seconds, WCHAR* error, DWORD errorSize);

/**
 * 设置显示顺序
 * @param ids GUID 数组
 * @param count 数量
 * @param error 错误信息缓冲区
 * @param errorSize 缓冲区大小
 * @return 成功返回 true
 */
bool BCD_SetDisplayOrder(const GUID* ids, int count, WCHAR* error, DWORD errorSize);

/**
 * 重命名启动项
 * @param id 启动项 GUID
 * @param newName 新名称
 * @param error 错误信息缓冲区
 * @param errorSize 缓冲区大小
 * @return 成功返回 true
 */
bool BCD_RenameEntry(const GUID* id, const WCHAR* newName, WCHAR* error, DWORD errorSize);

/**
 * GUID 转字符串
 */
void GUID_ToString(const GUID* guid, WCHAR* str, size_t size);

/**
 * 字符串转 GUID
 */
bool GUID_FromString(const WCHAR* str, GUID* guid);

#endif // BCD_MANAGER_H