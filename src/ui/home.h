/**
 * home.h - 卡片式首页（新版 UI 入口）
 */
#pragma once
#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 新版首页消息循环。--classic 模式下不要调用本函数 */
int HomeMain(HINSTANCE hInst, int nCmdShow);

/* 显示经典界面（旧版主窗口）。由 home.c 的卡片点击触发 */
HWND Classic_CreateAndShow(void);

#ifdef __cplusplus
}
#endif
