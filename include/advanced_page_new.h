/**
 * advanced_page_new.h - 高级功能页面
 */

#ifndef ADVANCED_PAGE_NEW_H
#define ADVANCED_PAGE_NEW_H

#include <windows.h>

// 构建页面
void AdvancedPageNew_Build(HWND hParent, HFONT fontTitle, HFONT fontBody, HFONT fontSmall);

// 命令处理
BOOL AdvancedPageNew_OnCommand(HWND hWnd, WPARAM wParam, LPARAM lParam);

#endif // ADVANCED_PAGE_NEW_H