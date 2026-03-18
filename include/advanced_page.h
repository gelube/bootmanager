#pragma once
#include <windows.h>

// MBR 修复按钮
#define ID_BTN_MBR_REPAIR    401
#define ID_BTN_UEFI_REPAIR   402

// Limine 引导管理器
#define ID_BTN_LIMINE_INSTALL    410
#define ID_BTN_LIMINE_UNINSTALL  411
#define ID_BTN_PBR_BACKUP        412
#define ID_BTN_PBR_RESTORE       413
#define ID_BTN_SET_ACTIVE        414

void AdvancedPageBuild(HWND hParent, HFONT fontTitle, HFONT fontBody, HFONT fontSmall);
void AdvancedPageCommand(HWND hWnd, WPARAM wParam);
