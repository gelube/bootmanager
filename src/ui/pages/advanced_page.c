/**
 * advanced_page.c - MBR repair and UEFI repair page
 */

#include <windows.h>
#include <stdio.h>
#include "../../../include/advanced_page.h"
#include "../../../include/mbr_io.h"
#include "../../../include/uefi_repair.h"

#define ID_STATUS_TEXT       500
#define CONTENT_PADDING      32
#define BTN_HEIGHT           38

static HWND s_hStatus = NULL;

static void SetStatus(const WCHAR* t) { if (s_hStatus) SetWindowTextW(s_hStatus, t); }

static void DoMBRRepair(HWND hWnd)
{
    if (MessageBoxW(hWnd,
        L"此操作将恢复 Windows 默认 MBR 引导代码，不会影响分区表。\n\n"
        L"⚠ 高风险操作，请确保已备份重要数据！\n\n确定继续？",
        L"MBR 修复确认", MB_YESNO | MB_ICONWARNING) != IDYES) return;

    SetStatus(L"⏳ 正在修复 MBR...");

    if (RepairMBR_Native(0)) {
        MessageBoxW(hWnd, L"MBR 引导代码已成功写入。\n\n请重启计算机验证修复结果。",
            L"MBR 修复成功", MB_OK | MB_ICONINFORMATION);
        SetStatus(L"✓ MBR 修复完成");
    } else {
        DWORD err = GetLastError();
        WCHAR msg[256];
        swprintf(msg, 256,
            L"MBR 修复失败（错误码：%lu）。\n\n请确保以管理员身份运行。", err);
        MessageBoxW(hWnd, msg, L"MBR 修复失败", MB_OK | MB_ICONERROR);
        SetStatus(L"✗ MBR 修复失败");
    }
}

static void DoUEFIRepair(HWND hWnd)
{
    if (MessageBoxW(hWnd,
        L"此操作将复制 Windows EFI 引导文件到 ESP 分区。\n\n确定继续？",
        L"UEFI 修复确认", MB_YESNO | MB_ICONQUESTION) != IDYES) return;

    SetStatus(L"⏳ 正在修复 UEFI 引导链...");

    if (RepairUEFI_Native()) {
        MessageBoxW(hWnd,
            L"UEFI 引导文件已成功复制到 ESP 分区。\n\n请重启计算机验证修复结果。",
            L"UEFI 修复成功", MB_OK | MB_ICONINFORMATION);
        SetStatus(L"✓ UEFI 修复完成");
    } else {
        MessageBoxW(hWnd,
            L"UEFI 修复失败。可能原因：\n\n"
            L"• 未找到 Windows\\Boot\\EFI\\bootmgfw.efi\n"
            L"• ESP 分区挂载失败\n"
            L"• 权限不足（请以管理员身份运行）\n\n"
            L"如在 Windows PE 中，请使用 Windows 安装盘修复。",
            L"UEFI 修复失败", MB_OK | MB_ICONWARNING);
        SetStatus(L"✗ UEFI 修复失败");
    }
}

void AdvancedPageCommand(HWND hWnd, WPARAM wParam)
{
    switch (LOWORD(wParam)) {
        case ID_BTN_MBR_REPAIR:  DoMBRRepair(hWnd);  break;
        case ID_BTN_UEFI_REPAIR: DoUEFIRepair(hWnd); break;
    }
}

void AdvancedPageBuild(HWND hParent, HFONT fontTitle, HFONT fontBody, HFONT fontSmall)
{
    s_hStatus = NULL;

    RECT rc; GetClientRect(hParent, &rc);
    int w = rc.right - CONTENT_PADDING * 2;
    int h = rc.bottom;

    HWND hTitle = CreateWindowExW(0, L"STATIC", L"高级功能",
        WS_CHILD | WS_VISIBLE, CONTENT_PADDING, CONTENT_PADDING, w, 32,
        hParent, NULL, NULL, NULL);
    SendMessageW(hTitle, WM_SETFONT, (WPARAM)fontTitle, TRUE);

    HWND hDesc = CreateWindowExW(0, L"STATIC",
        L"MBR 修复与 UEFI 引导链修复工具。操作前请确保已备份重要数据。",
        WS_CHILD | WS_VISIBLE,
        CONTENT_PADDING, CONTENT_PADDING + 40, w, 24,
        hParent, NULL, NULL, NULL);
    SendMessageW(hDesc, WM_SETFONT, (WPARAM)fontBody, TRUE);

    /* MBR 修复 */
    HWND hMBRLabel = CreateWindowExW(0, L"STATIC", L"MBR 修复",
        WS_CHILD | WS_VISIBLE,
        CONTENT_PADDING, CONTENT_PADDING + 90, w, 22,
        hParent, NULL, NULL, NULL);
    SendMessageW(hMBRLabel, WM_SETFONT, (WPARAM)fontBody, TRUE);

    HWND hMBRDesc = CreateWindowExW(0, L"STATIC",
        L"恢复 Windows 默认 MBR 引导代码，不会影响分区表。",
        WS_CHILD | WS_VISIBLE,
        CONTENT_PADDING, CONTENT_PADDING + 116, w, 20,
        hParent, NULL, NULL, NULL);
    SendMessageW(hMBRDesc, WM_SETFONT, (WPARAM)fontSmall, TRUE);

    HWND hBtnMBR = CreateWindowExW(0, L"BUTTON", L"MBR 修复",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        CONTENT_PADDING, CONTENT_PADDING + 142, 140, BTN_HEIGHT,
        hParent, (HMENU)ID_BTN_MBR_REPAIR, NULL, NULL);
    SendMessageW(hBtnMBR, WM_SETFONT, (WPARAM)fontBody, TRUE);

    /* UEFI 修复 */
    HWND hUEFILabel = CreateWindowExW(0, L"STATIC", L"UEFI 修复",
        WS_CHILD | WS_VISIBLE,
        CONTENT_PADDING, CONTENT_PADDING + 210, w, 22,
        hParent, NULL, NULL, NULL);
    SendMessageW(hUEFILabel, WM_SETFONT, (WPARAM)fontBody, TRUE);

    HWND hUEFIDesc = CreateWindowExW(0, L"STATIC",
        L"检查 ESP 分区的 Windows UEFI 引导链完整性，缺失时提示修复方案。",
        WS_CHILD | WS_VISIBLE,
        CONTENT_PADDING, CONTENT_PADDING + 236, w, 20,
        hParent, NULL, NULL, NULL);
    SendMessageW(hUEFIDesc, WM_SETFONT, (WPARAM)fontSmall, TRUE);

    HWND hBtnUEFI = CreateWindowExW(0, L"BUTTON", L"UEFI 修复",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        CONTENT_PADDING, CONTENT_PADDING + 262, 140, BTN_HEIGHT,
        hParent, (HMENU)ID_BTN_UEFI_REPAIR, NULL, NULL);
    SendMessageW(hBtnUEFI, WM_SETFONT, (WPARAM)fontBody, TRUE);

    s_hStatus = CreateWindowExW(0, L"STATIC", L"就绪",
        WS_CHILD | WS_VISIBLE,
        CONTENT_PADDING, h - 30, w, 24,
        hParent, (HMENU)ID_STATUS_TEXT, NULL, NULL);
    SendMessageW(s_hStatus, WM_SETFONT, (WPARAM)fontSmall, TRUE);
}
