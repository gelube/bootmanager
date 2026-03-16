#pragma once
#include <windows.h>

#define ID_BTN_MBR_REPAIR    401
#define ID_BTN_UEFI_REPAIR   402

void AdvancedPageBuild(HWND hParent, HFONT fontTitle, HFONT fontBody, HFONT fontSmall);
void AdvancedPageCommand(HWND hWnd, WPARAM wParam);
