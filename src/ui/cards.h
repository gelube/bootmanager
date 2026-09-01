/**
 * cards.h - 卡片控件引擎（简约风格）
 * 自绘卡片：标题 + 两行摘要 + 右下角动作按钮。
 * 点击卡片本体 → WM_COMMAND (MAKEWPARAM(id, BMN_OPEN), 0)
 * 点击按钮   → WM_COMMAND (MAKEWPARAM(id, BMN_BUTTON), 0)
 */
#pragma once
#include <windows.h>

#define BMN_OPEN    1
#define BMN_BUTTON  2

#ifdef __cplusplus
extern "C" {
#endif

BOOL BMCard_RegisterClass(HINSTANCE hInst);

/* 创建卡片，返回句柄。btnText 为 NULL 时不显示按钮 */
HWND BMCard_Create(HWND parent, int id, int x, int y, int w, int h,
                   const WCHAR* title,
                   const WCHAR* line1, const WCHAR* line2,
                   const WCHAR* btnText, BOOL danger);

/* 运行时更新摘要行（idx: 1 或 2），自动重绘 */
void BMCard_SetLine(HWND card, int idx, const WCHAR* text);

#ifdef __cplusplus
}
#endif
