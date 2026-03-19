/**
 * esp.h - ESP 分区挂载/卸载
 */

#ifndef BOOTMGR_ESP_H
#define BOOTMGR_ESP_H

#include <windows.h>

/**
 * 挂载 ESP 分区
 * @param driveLetter 输出盘符 (如 "S:")
 * @param size 缓冲区大小
 * @param mountedByUs 输出：是否由本次调用挂载（NULL 表示不关心）
 * @return 成功返回 TRUE
 */
BOOL EspMountEx(WCHAR* driveLetter, DWORD size, BOOL* mountedByUs);

/**
 * 挂载 ESP 分区（简化版，不追踪挂载状态）
 */
BOOL EspMount(WCHAR* driveLetter, DWORD size);

/**
 * 卸载 ESP 分区
 * @param driveLetter 盘符
 * @param onlyIfMountedByUs 仅当是我们挂载的才卸载
 * @return 成功返回 TRUE
 */
BOOL EspUnmountEx(const WCHAR* driveLetter, BOOL onlyIfMountedByUs);

/**
 * 卸载 ESP 分区（兼容旧接口）
 */
BOOL EspUnmount(const WCHAR* driveLetter);

/**
 * 查找 ESP 分区
 */
BOOL EspFind(WCHAR* driveLetter, DWORD size);

#endif // BOOTMGR_ESP_H