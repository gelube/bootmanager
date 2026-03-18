/**
 * boot_manager_page.h - 启动管理页面
 * 
 * 根据 BIOS 模式自动切换 UI 内容：
 * - UEFI 模式: [UEFI启动项] [rEFInd] [BCD菜单]
 * - MBR 模式: [MBR引导(Limine)] [PBR管理] [BCD菜单]
 */

#ifndef BOOT_MANAGER_PAGE_H
#define BOOT_MANAGER_PAGE_H

#include <windows.h>

// Tab 页索引
enum {
    TAB_UEFI_BOOT = 0,
    TAB_REFIND,
    TAB_BCD_MENU,
    TAB_MBR_BOOT,
    TAB_PBR_MANAGER,
    TAB_COUNT
};

// 初始化启动管理页面
void BootManagerPage_Init(HWND hParent, HFONT fontTitle, HFONT fontBody, HFONT fontSmall);

// 构建 UI
void BootManagerPage_Build(HWND hParent);

// 刷新当前 Tab 页
void BootManagerPage_Refresh(void);

// 切换 Tab 页
void BootManagerPage_SwitchTab(int tabIndex);

// 获取当前 Tab 索引
int BootManagerPage_GetCurrentTab(void);

// 处理 WM_COMMAND
BOOL BootManagerPage_OnCommand(HWND hWnd, WPARAM wParam, LPARAM lParam);

// 清理资源
void BootManagerPage_Cleanup(void);

// 获取列表视图句柄（用于主窗口消息处理）
HWND BootManagerPage_GetListView(void);

// 刷新 UEFI 启动项列表
void BootManagerPage_RefreshUEFIList(void);

// 刷新 Limine/MBR 启动项列表
void BootManagerPage_RefreshMBRList(void);

// 刷新 BCD 菜单列表
void BootManagerPage_RefreshBCDList(void);

#endif // BOOT_MANAGER_PAGE_H