/**
 * refind_page.c - rEFInd 管理页面
 */

#include <windows.h>
#include <commctrl.h>
#include <stdio.h>
#include "../../../include/refind_page.h"

#define ID_BTN_INSTALL        300
#define ID_BTN_UNINSTALL      301
#define ID_STATUS_TEXT        500
#define CONTENT_PADDING       32
#define BTN_HEIGHT            38

static HWND s_hStatus = NULL;
static HWND s_hBtnInstall = NULL;
static HWND s_hBtnUninstall = NULL;
static BOOL s_isInstalled = FALSE;

static void SetStatus(const WCHAR* t){ if (s_hStatus) SetWindowTextW(s_hStatus, t); }

// 检测 rEFInd 是否已安装
static BOOL CheckRefindInstalled(void)
{
    // 检查 NVRAM 中是否有 rEFInd 启动项
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, 
        L"SYSTEM\\CurrentControlSet\\Control\\FirmwareResources\\BootOptions", 
        0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        
        WCHAR valueName[64];
        DWORD index = 0;
        DWORD nameLen;
        
        while (TRUE) {
            nameLen = 64;
            if (RegEnumKeyExW(hKey, index, valueName, &nameLen, 
                NULL, NULL, NULL, NULL) != ERROR_SUCCESS) break;
            
            // 检查是否有 rEFInd 相关的启动项
            // 简化：直接返回 TRUE 表示可能已安装
            RegCloseKey(hKey);
            return FALSE;  // 暂不检测，避免误判
        }
        RegCloseKey(hKey);
    }
    return FALSE;
}

void RefindPageRefresh(void)
{
    if (s_isInstalled) {
        SetStatus(L"rEFInd 已安装");
    } else {
        SetStatus(L"点击按钮安装或卸载 rEFInd");
    }
}

void RefindPageBuild(HWND hParent, HFONT fontTitle, HFONT fontBody, HFONT fontSmall)
{
    s_hStatus = NULL;
    s_hBtnInstall = NULL;
    s_hBtnUninstall = NULL;
    s_isInstalled = FALSE;
    
    RECT rc;
    GetClientRect(hParent, &rc);
    int w = rc.right - CONTENT_PADDING * 2;
    int h = rc.bottom;
    int y = CONTENT_PADDING;
    
    // 标题
    HWND hTitle = CreateWindowExW(0, L"STATIC", L"rEFInd 引导管理器",
        WS_CHILD | WS_VISIBLE, CONTENT_PADDING, y, w, 28, hParent, NULL, NULL, NULL);
    SendMessageW(hTitle, WM_SETFONT, (WPARAM)fontTitle, TRUE);
    y += 40;
    
    // 描述
    HWND hDesc = CreateWindowExW(0, L"STATIC",
        L"rEFInd 是现代化的 UEFI 引导管理器，安装后会自动检测并列出所有操作系统。",
        WS_CHILD | WS_VISIBLE, CONTENT_PADDING, y, w, 24, hParent, NULL, NULL, NULL);
    SendMessageW(hDesc, WM_SETFONT, (WPARAM)fontBody, TRUE);
    y += 35;
    
    // 安装信息
    HWND hInfo = CreateWindowExW(0, L"STATIC",
        L"安装位置: ESP 分区 \\EFI\\refind\\",
        WS_CHILD | WS_VISIBLE, CONTENT_PADDING, y, w, 20, hParent, NULL, NULL, NULL);
    SendMessageW(hInfo, WM_SETFONT, (WPARAM)fontSmall, TRUE);
    y += 30;
    
    // 资源说明
    HWND hRes = CreateWindowExW(0, L"STATIC",
        L"资源文件: 程序目录下的 refind\\ 文件夹（需包含 refind_x64.efi）",
        WS_CHILD | WS_VISIBLE, CONTENT_PADDING, y, w, 20, hParent, NULL, NULL, NULL);
    SendMessageW(hRes, WM_SETFONT, (WPARAM)fontSmall, TRUE);
    y += 40;
    
    // 安装按钮
    s_hBtnInstall = CreateWindowExW(0, L"BUTTON", L"安装 rEFInd",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        CONTENT_PADDING, y, 130, BTN_HEIGHT, hParent, (HMENU)ID_BTN_INSTALL, NULL, NULL);
    SendMessageW(s_hBtnInstall, WM_SETFONT, (WPARAM)fontBody, TRUE);
    
    // 卸载按钮
    s_hBtnUninstall = CreateWindowExW(0, L"BUTTON", L"卸载 rEFInd",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        CONTENT_PADDING + 150, y, 130, BTN_HEIGHT, hParent, (HMENU)ID_BTN_UNINSTALL, NULL, NULL);
    SendMessageW(s_hBtnUninstall, WM_SETFONT, (WPARAM)fontBody, TRUE);
    
    y += 60;
    
    // 说明
    HWND hNote = CreateWindowExW(0, L"STATIC",
        L"说明：\n"
        L"• rEFInd 安装后会自动扫描系统，无需手动配置启动项\n"
        L"• 支持自动检测 Windows、Linux、macOS 等系统\n"
        L"• 安装前请确保 refind\\ 文件夹中有 refind_x64.efi 文件",
        WS_CHILD | WS_VISIBLE, CONTENT_PADDING, y, w, 80, hParent, NULL, NULL, NULL);
    SendMessageW(hNote, WM_SETFONT, (WPARAM)fontSmall, TRUE);
    
    // 状态栏
    s_hStatus = CreateWindowExW(0, L"STATIC", L"就绪",
        WS_CHILD | WS_VISIBLE, CONTENT_PADDING, h - 30, w, 20, hParent, (HMENU)ID_STATUS_TEXT, NULL, NULL);
    SendMessageW(s_hStatus, WM_SETFONT, (WPARAM)fontSmall, TRUE);
}

// 设置安装状态（供 main.c 调用）
void RefindPageSetInstalled(BOOL installed)
{
    s_isInstalled = installed;
    if (s_hBtnInstall) {
        EnableWindow(s_hBtnInstall, !installed);
    }
    if (s_hBtnUninstall) {
        EnableWindow(s_hBtnUninstall, installed);
    }
}

void RefindPageDeleteSelected(HWND hWnd) { }
void RefindPageShowAddMenu(HWND hWnd) { }
void RefindPageAddEfi(HWND hWnd) { }
void RefindPageAddWim(HWND hWnd) { }
void RefindPageAddVhd(HWND hWnd) { }