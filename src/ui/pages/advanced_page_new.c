/**
 * advanced_page_new.c - 高级功能页面
 * 
 * 包含：模式转换、备份恢复、引导修复
 */

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <shlobj.h>
#include <wchar.h>
#include <stdio.h>
#include "../../../include/boot_mode.h"
#include "../../../include/mbr_io.h"
#include "../../../include/uefi_repair.h"
#include "../../core/backup.h"
#include "../../core/uefi.h"

#define CONTENT_PADDING  32
#define BTN_HEIGHT       38
#define CARD_PADDING     20

// 控件 ID
enum {
    ID_BTN_CONVERT_TO_UEFI = 600,
    ID_BTN_CONVERT_TO_MBR,
    ID_BTN_BACKUP_MBR,
    ID_BTN_BACKUP_NVRAM,
    ID_BTN_RESTORE,
    ID_BTN_REPAIR_UEFI,
    ID_BTN_REPAIR_PBR,
    ID_STATUS_TEXT = 500
};

static HWND s_hStatus = NULL;
static HFONT s_fontTitle = NULL;
static HFONT s_fontBody = NULL;
static HFONT s_fontSmall = NULL;
static BOOT_INFO s_bootInfo = {0};

static void SetStatus(const WCHAR* text)
{
    if (s_hStatus) SetWindowTextW(s_hStatus, text);
}

// ============================================
// 备份恢复
// ============================================

static void DoBackupMBR(HWND hWnd)
{
    SYSTEMTIME st;
    GetLocalTime(&st);
    WCHAR file[MAX_PATH];
    CreateDirectoryW(L"C:\\BootBackups", NULL);
    swprintf(file, MAX_PATH, L"C:\\BootBackups\\MBR_%04d%02d%02d.bin",
        st.wYear, st.wMonth, st.wDay);

    SetStatus(L"正在备份 MBR...");
    if (BackupMBR(L"PhysicalDrive0", file)) {
        WCHAR msg[MAX_PATH];
        swprintf(msg, MAX_PATH, L"MBR 备份成功：\n%s", file);
        MessageBoxW(hWnd, msg, L"备份完成", MB_OK | MB_ICONINFORMATION);
        SetStatus(L"MBR 已备份");
    } else {
        MessageBoxW(hWnd, L"备份失败，需要管理员权限。", L"错误", MB_OK | MB_ICONERROR);
        SetStatus(L"备份失败");
    }
}

static void DoBackupNVRAM(HWND hWnd)
{
    SYSTEMTIME st;
    GetLocalTime(&st);
    WCHAR file[MAX_PATH];
    CreateDirectoryW(L"C:\\BootBackups", NULL);
    swprintf(file, MAX_PATH, L"C:\\BootBackups\\NVRAM_%04d%02d%02d.bin",
        st.wYear, st.wMonth, st.wDay);

    SetStatus(L"正在备份 NVRAM...");
    if (UefiExportNVRAM(file)) {
        WCHAR msg[MAX_PATH];
        swprintf(msg, MAX_PATH, L"NVRAM 备份成功：\n%s", file);
        MessageBoxW(hWnd, msg, L"备份完成", MB_OK | MB_ICONINFORMATION);
        SetStatus(L"NVRAM 已备份");
    } else {
        MessageBoxW(hWnd, L"备份失败。PE 环境可能不支持此功能。", L"提示", MB_OK | MB_ICONWARNING);
        SetStatus(L"备份失败");
    }
}

static BOOL BrowseBackupFile(HWND hWnd, WCHAR* filePath, DWORD size)
{
    OPENFILENAMEW ofn = {0};
    filePath[0] = L'\0';
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hWnd;
    ofn.lpstrFile = filePath;
    ofn.nMaxFile = size;
    ofn.lpstrFilter = L"备份文件 (*.bak;*.bin)\0*.bak;*.bin\0所有文件 (*.*)\0*.*\0";
    ofn.lpstrTitle = L"选择备份文件";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    return GetOpenFileNameW(&ofn);
}

static void DoRestore(HWND hWnd)
{
    WCHAR selectedPath[MAX_PATH] = {0};
    if (!BrowseBackupFile(hWnd, selectedPath, MAX_PATH)) return;

    if (MessageBoxW(hWnd, L"恢复引导备份可能影响系统启动，确定继续？",
        L"确认恢复", MB_YESNO | MB_ICONWARNING) != IDYES) return;

    SetStatus(L"正在恢复...");
    MessageBoxW(hWnd, L"恢复功能正在完善中。", L"提示", MB_OK | MB_ICONINFORMATION);
}

// ============================================
// 引导修复
// ============================================

static void DoRepairUEFI(HWND hWnd)
{
    if (MessageBoxW(hWnd,
        L"此操作将复制 Windows EFI 引导文件到 ESP 分区。\n\n确定继续？",
        L"UEFI 修复确认", MB_YESNO | MB_ICONQUESTION) != IDYES) return;

    SetStatus(L"正在修复 UEFI 引导链...");

    if (RepairUEFI_Native()) {
        MessageBoxW(hWnd,
            L"UEFI 引导文件已成功复制到 ESP 分区。\n\n请重启计算机验证修复结果。",
            L"UEFI 修复成功", MB_OK | MB_ICONINFORMATION);
        SetStatus(L"UEFI 修复完成");
    } else {
        MessageBoxW(hWnd,
            L"UEFI 修复失败。可能原因：\n\n"
            L"• 未找到 Windows EFI 引导文件\n"
            L"• ESP 分区挂载失败\n"
            L"• 权限不足\n\n"
            L"请以管理员身份运行。",
            L"UEFI 修复失败", MB_OK | MB_ICONWARNING);
        SetStatus(L"UEFI 修复失败");
    }
}

// ============================================
// 页面构建
// ============================================

void AdvancedPageNew_Build(HWND hParent, HFONT fontTitle, HFONT fontBody, HFONT fontSmall)
{
    s_fontTitle = fontTitle;
    s_fontBody = fontBody;
    s_fontSmall = fontSmall;
    s_hStatus = NULL;

    // 检测启动模式
    BootMode_Detect(&s_bootInfo);

    RECT rc;
    GetClientRect(hParent, &rc);
    int w = rc.right - CONTENT_PADDING * 2;
    int h = rc.bottom;
    int cardY = CONTENT_PADDING + 40;
    int cardH = 110;

    // 标题
    HWND hTitle = CreateWindowExW(0, L"STATIC", L"高级功能",
        WS_CHILD | WS_VISIBLE, CONTENT_PADDING, CONTENT_PADDING, w, 32,
        hParent, NULL, NULL, NULL);
    SendMessageW(hTitle, WM_SETFONT, (WPARAM)fontTitle, TRUE);

    // ===== 卡片 1: 备份恢复 =====
    HWND hCard1 = CreateWindowExW(0, L"BUTTON", L" 备份与恢复",
        WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
        CONTENT_PADDING, cardY, w, cardH,
        hParent, NULL, NULL, NULL);
    SendMessageW(hCard1, WM_SETFONT, (WPARAM)fontBody, TRUE);

    int btnY1 = cardY + 25;
    HWND hBtnBackupMBR = CreateWindowExW(0, L"BUTTON", L"备份 MBR",
        WS_CHILD | WS_VISIBLE,
        CONTENT_PADDING + CARD_PADDING, btnY1, 90, BTN_HEIGHT,
        hParent, (HMENU)ID_BTN_BACKUP_MBR, NULL, NULL);
    SendMessageW(hBtnBackupMBR, WM_SETFONT, (WPARAM)fontBody, TRUE);

    if (s_bootInfo.isUEFIFirmware) {
        HWND hBtnBackupNVRAM = CreateWindowExW(0, L"BUTTON", L"备份 NVRAM",
            WS_CHILD | WS_VISIBLE,
            CONTENT_PADDING + CARD_PADDING + 100, btnY1, 100, BTN_HEIGHT,
            hParent, (HMENU)ID_BTN_BACKUP_NVRAM, NULL, NULL);
        SendMessageW(hBtnBackupNVRAM, WM_SETFONT, (WPARAM)fontBody, TRUE);
    }

    HWND hBtnRestore = CreateWindowExW(0, L"BUTTON", L"恢复备份",
        WS_CHILD | WS_VISIBLE,
        CONTENT_PADDING + CARD_PADDING, btnY1 + 45, 100, BTN_HEIGHT,
        hParent, (HMENU)ID_BTN_RESTORE, NULL, NULL);
    SendMessageW(hBtnRestore, WM_SETFONT, (WPARAM)fontBody, TRUE);

    cardY += cardH + 20;

    // ===== 卡片 2: 引导修复 =====
    HWND hCard2 = CreateWindowExW(0, L"BUTTON", L" 引导修复",
        WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
        CONTENT_PADDING, cardY, w, cardH,
        hParent, NULL, NULL, NULL);
    SendMessageW(hCard2, WM_SETFONT, (WPARAM)fontBody, TRUE);

    int btnY2 = cardY + 25;
    if (s_bootInfo.isUEFIFirmware) {
        HWND hBtnRepairUEFI = CreateWindowExW(0, L"BUTTON", L"UEFI 修复",
            WS_CHILD | WS_VISIBLE,
            CONTENT_PADDING + CARD_PADDING, btnY2, 100, BTN_HEIGHT,
            hParent, (HMENU)ID_BTN_REPAIR_UEFI, NULL, NULL);
        SendMessageW(hBtnRepairUEFI, WM_SETFONT, (WPARAM)fontBody, TRUE);
        
        // UEFI 模式下不显示 MBR 修复（MBR 管理页面有）
    }
    
    // PBR 修复（所有模式都可用）
    HWND hBtnRepairPBR = CreateWindowExW(0, L"BUTTON", L"PBR 修复",
        WS_CHILD | WS_VISIBLE,
        CONTENT_PADDING + CARD_PADDING + (s_bootInfo.isUEFIFirmware ? 110 : 0), btnY2, 100, BTN_HEIGHT,
        hParent, (HMENU)ID_BTN_REPAIR_PBR, NULL, NULL);
    SendMessageW(hBtnRepairPBR, WM_SETFONT, (WPARAM)fontBody, TRUE);

    // 模式信息
    WCHAR modeInfo[256];
    swprintf(modeInfo, 256, L"当前模式: %s | 磁盘: %s",
        BootMode_GetName(s_bootInfo.bootMode),
        s_bootInfo.isGPTDisk ? L"GPT" : L"MBR");

    HWND hModeInfo = CreateWindowExW(0, L"STATIC", modeInfo,
        WS_CHILD | WS_VISIBLE,
        CONTENT_PADDING + CARD_PADDING, btnY2 + 45, w - CARD_PADDING * 2, 24,
        hParent, NULL, NULL, NULL);
    SendMessageW(hModeInfo, WM_SETFONT, (WPARAM)fontSmall, TRUE);

    // 状态栏
    s_hStatus = CreateWindowExW(0, L"STATIC", L"就绪",
        WS_CHILD | WS_VISIBLE,
        CONTENT_PADDING, h - 30, w, 24,
        hParent, (HMENU)ID_STATUS_TEXT, NULL, NULL);
    SendMessageW(s_hStatus, WM_SETFONT, (WPARAM)fontSmall, TRUE);
}

// ============================================
// 命令处理
// ============================================

BOOL AdvancedPageNew_OnCommand(HWND hWnd, WPARAM wParam, LPARAM lParam)
{
    switch (LOWORD(wParam)) {
        case ID_BTN_BACKUP_MBR:
            DoBackupMBR(hWnd);
            return TRUE;
        case ID_BTN_BACKUP_NVRAM:
            DoBackupNVRAM(hWnd);
            return TRUE;
        case ID_BTN_RESTORE:
            DoRestore(hWnd);
            return TRUE;
        case ID_BTN_REPAIR_UEFI:
            DoRepairUEFI(hWnd);
            return TRUE;
        case ID_BTN_REPAIR_PBR:
            MessageBoxW(hWnd, L"PBR 修复功能正在完善中。", L"提示", MB_OK | MB_ICONINFORMATION);
            return TRUE;
    }
    return FALSE;
}