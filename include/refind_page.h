/**
 * refind_page.h - rEFInd 页面接口
 */

#ifndef REFIND_PAGE_H
#define REFIND_PAGE_H

#include <windows.h>

// 构建 rEFInd 管理页面
void RefindPageBuild(HWND hParent, HFONT fontTitle, HFONT fontBody, HFONT fontSmall);

// 刷新 menuentry 列表
void RefindPageRefresh(void);

// 设置安装状态（禁用/启用按钮）
void RefindPageSetInstalled(BOOL installed);

// 删除选中条目
void RefindPageDeleteSelected(HWND hWnd);

// 显示添加启动项菜单
void RefindPageShowAddMenu(HWND hWnd);

// 添加 EFI 菜单项
void RefindPageAddEfi(HWND hWnd);

// 添加 WIM 启动项
void RefindPageAddWim(HWND hWnd);

// 添加 VHD 启动项
void RefindPageAddVhd(HWND hWnd);

#endif // REFIND_PAGE_H