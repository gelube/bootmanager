/**
 * limine_page.h - Limine 页面接口
 */

#ifndef LIMINE_PAGE_H
#define LIMINE_PAGE_H

#include <windows.h>

// 构建 Limine 管理页面
void LiminePageBuild(HWND hParent, HFONT fontTitle, HFONT fontBody, HFONT fontSmall);

// 刷新页面状态
void LiminePageRefresh(void);

// 设置安装状态
void LiminePageSetInstalled(BOOL installed);

// 处理按钮命令
void LiminePageCommand(HWND hWnd, WPARAM wParam);

#endif // LIMINE_PAGE_H