/**
 * refind_page.c - rEFInd 管理页面
 */

#include <windows.h>
#include <commctrl.h>
#include <stdio.h>
#include "refind_page.h"
#include "refind.h"
#include "esp.h"

#define ID_BTN_INSTALL        300
#define ID_BTN_UNINSTALL      301
#define BTN_HEIGHT            38

static HWND s_hStatus = NULL;
static HWND s_hBtnInstall = NULL;
static HWND s_hBtnUninstall = NULL;
static BOOL s_isInstalled = FALSE;

static void SetStatus(const WCHAR* t){ if (s_hStatus) SetWindowTextW(s_hStatus, t); }

// 检测 rEFInd 是否已安装
static BOOL CheckRefindInstalled(void)
{
    WCHAR espDrive[4] = {0};
    if (!EspMount(espDrive, 4)) return FALSE;
    BOOL installed = RefindIsInstalled(espDrive);
    EspUnmount(espDrive);
    return installed;
}

void RefindPageRefresh(void)
{
    s_isInstalled = CheckRefindInstalled();
    
    if (s_hBtnInstall) EnableWindow(s_hBtnInstall, !s_isInstalled);
    if (s_hBtnUninstall) EnableWindow(s_hBtnUninstall, s_isInstalled);
    
    SetStatus(s_isInstalled ? L"rEFInd 已安装" : L"rEFInd 未安装");
}

void RefindPageBuild(HWND hParent, HFONT fontTitle, HFONT fontBody, HFONT fontSmall)
{
    s_hStatus = NULL;
    s_hBtnInstall = NULL;
    s_hBtnUninstall = NULL;
    s_isInstalled = FALSE;
    
    RECT rc;
    GetClientRect(hParent, &rc);
    int w = rc.right - 20;
    int h = rc.bottom;
    int y = 50;  // Tab 高度 + 边距
    
    // 标题
    HWND hTitle = CreateWindowExW(0, L"STATIC", L"rEFInd 引导管理器",
        WS_CHILD | WS_VISIBLE, 10, y, w, 28, hParent, NULL, NULL, NULL);
    SendMessageW(hTitle, WM_SETFONT, (WPARAM)fontTitle, TRUE);
    y += 40;
    
    // 描述
    HWND hDesc = CreateWindowExW(0, L"STATIC",
        L"rEFInd 是现代化的 UEFI 引导管理器，安装后会自动检测并列出所有操作系统。",
        WS_CHILD | WS_VISIBLE, 10, y, w, 24, hParent, NULL, NULL, NULL);
    SendMessageW(hDesc, WM_SETFONT, (WPARAM)fontBody, TRUE);
    y += 35;
    
    // 安装信息
    HWND hInfo = CreateWindowExW(0, L"STATIC",
        L"安装位置: ESP 分区 \\EFI\\refind\\",
        WS_CHILD | WS_VISIBLE, 10, y, w, 20, hParent, NULL, NULL, NULL);
    SendMessageW(hInfo, WM_SETFONT, (WPARAM)fontSmall, TRUE);
    y += 30;
    
    // 资源说明
    HWND hRes = CreateWindowExW(0, L"STATIC",
        L"资源文件: 程序目录下的 refind\\ 文件夹（需包含 refind_x64.efi）",
        WS_CHILD | WS_VISIBLE, 10, y, w, 20, hParent, NULL, NULL, NULL);
    SendMessageW(hRes, WM_SETFONT, (WPARAM)fontSmall, TRUE);
    y += 45;
    
    // 安装按钮
    s_hBtnInstall = CreateWindowExW(0, L"BUTTON", L"安装 rEFInd",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        10, y, 130, BTN_HEIGHT, hParent, (HMENU)ID_BTN_INSTALL, NULL, NULL);
    SendMessageW(s_hBtnInstall, WM_SETFONT, (WPARAM)fontBody, TRUE);
    
    // 卸载按钮
    s_hBtnUninstall = CreateWindowExW(0, L"BUTTON", L"卸载 rEFInd",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        150, y, 130, BTN_HEIGHT, hParent, (HMENU)ID_BTN_UNINSTALL, NULL, NULL);
    SendMessageW(s_hBtnUninstall, WM_SETFONT, (WPARAM)fontBody, TRUE);
    
    y += 60;
    
    // 说明
    HWND hNote = CreateWindowExW(0, L"STATIC",
        L"说明：\n"
        L"• rEFInd 安装后会自动扫描系统，无需手动配置启动项\n"
        L"• 支持自动检测 Windows、Linux、macOS 等系统\n"
        L"• 安装前请确保 refind\\ 文件夹中有 refind_x64.efi 文件",
        WS_CHILD | WS_VISIBLE, 10, y, w, 80, hParent, NULL, NULL, NULL);
    SendMessageW(hNote, WM_SETFONT, (WPARAM)fontSmall, TRUE);
    
    // 状态栏
    s_hStatus = CreateWindowExW(0, L"STATIC", L"检测中...",
        WS_CHILD | WS_VISIBLE, 10, h - 25, w, 20, hParent, NULL, NULL, NULL);
    SendMessageW(s_hStatus, WM_SETFONT, (WPARAM)fontSmall, TRUE);
}

void RefindPageSetInstalled(BOOL installed)
{
    s_isInstalled = installed;
    if (s_hBtnInstall) EnableWindow(s_hBtnInstall, !installed);
    if (s_hBtnUninstall) EnableWindow(s_hBtnUninstall, installed);
    if (s_hStatus) SetStatus(installed ? L"rEFInd 已安装" : L"rEFInd 未安装");
}

void RefindPageDeleteSelected(HWND hWnd) { }
void RefindPageShowAddMenu(HWND hWnd) { }
void RefindPageAddEfi(HWND hWnd) { }
void RefindPageAddWim(HWND hWnd) { }
void RefindPageAddVhd(HWND hWnd) { }