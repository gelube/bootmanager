/**
 * boot_config_page.h - 引导配置页面头文件
 */

#ifndef BOOT_CONFIG_PAGE_H
#define BOOT_CONFIG_PAGE_H

#include <windows.h>

/**
 * 创建引导配置页面
 */
HWND CreateBootConfigPage(HWND hParent, HINSTANCE hInst);

#endif // BOOT_CONFIG_PAGE_H