/**
 * bcd_page.h - BCD 启动菜单管理页面
 */

#ifndef BCD_PAGE_H
#define BCD_PAGE_H

#include <windows.h>

// 构建 BCD 管理页面
void BcdPageBuild(HWND hParent, HFONT fontTitle, HFONT fontBody, HFONT fontSmall);

// 刷新 BCD 列表
void BcdPageRefresh(void);

// 处理命令
void BcdPageCommand(HWND hWnd, WPARAM wParam);

#endif // BCD_PAGE_H