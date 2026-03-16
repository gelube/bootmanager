/**
 * refind_page.c - rEFInd menuentry management page
 * 包含：安装/卸载 rEFInd、管理 menuentry、添加 WIM/VHD 启动项
 */

#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <stdio.h>
#include "../../../include/refind_page.h"
#include "../../core/refind_config.h"
#include "../../core/refind.h"
#include "../../core/wimboot.h"

#define ID_LIST_REFIND        260
#define ID_BTN_REFIND_REFRESH 261
#define ID_BTN_REFIND_DELETE  262
#define ID_BTN_ADD_MENU       263
#define ID_BTN_INSTALL        300
#define ID_BTN_UNINSTALL      301
#define ID_STATUS_TEXT        500
#define CONTENT_PADDING       32
#define BTN_HEIGHT            38

// 下拉菜单项 ID
#define ID_MENU_ADD_EFI       601
#define ID_MENU_ADD_WIM       602
#define ID_MENU_ADD_VHD       603

static HWND s_hList   = NULL;
static HWND s_hStatus = NULL;
static REFIND_MENU_ENTRY* s_entries = NULL;

static BOOL MountESP(WCHAR* drive)   { return RefindMountESP(drive, 4); }
static void UnmountESP(WCHAR letter) { WCHAR d[4]={letter,L':',L'\0',L'\0'}; RefindUnmountESP(d); }
static void SetStatus(const WCHAR* t){ if (s_hStatus) SetWindowTextW(s_hStatus, t); }

void RefindPageRefresh(void)
{
    if (!s_hList) return;
    ListView_DeleteAllItems(s_hList);
    if (s_entries) { RefindConfigFreeEntries(s_entries); s_entries = NULL; }

    // 先显示加载提示
    LVITEM lvi = {0}; lvi.mask = LVIF_TEXT; lvi.iItem = 0; lvi.pszText = L"...";
    ListView_InsertItem(s_hList, &lvi);
    ListView_SetItemText(s_hList, 0, 1, L"正在加载...");
    UpdateWindow(s_hList);

    WCHAR espDrive[4] = {0};
    if (!MountESP(espDrive)) {
        ListView_DeleteAllItems(s_hList);
        lvi.iItem = 0; lvi.pszText = L"-";
        ListView_InsertItem(s_hList, &lvi);
        ListView_SetItemText(s_hList, 0, 1, L"无法挂载 ESP");
        return;
    }

    s_entries = RefindConfigLoad(espDrive);
    UnmountESP(espDrive[0]);

    ListView_DeleteAllItems(s_hList);

    if (!s_entries) {
        lvi.iItem = 0; lvi.pszText = L"-";
        ListView_InsertItem(s_hList, &lvi);
        ListView_SetItemText(s_hList, 0, 1, L"未发现 menuentry");
        return;
    }

    REFIND_MENU_ENTRY* e = s_entries;
    int idx = 0;
    while (e) {
        lvi.mask = LVIF_TEXT; lvi.iItem = idx;
        lvi.pszText = e->isManaged ? L"是" : L"否";
        ListView_InsertItem(s_hList, &lvi);
        ListView_SetItemText(s_hList, idx, 1, e->title[0] ? e->title : L"(未命名)");
        ListView_SetItemText(s_hList, idx, 2, e->loader);
        ListView_SetItemText(s_hList, idx, 3, e->options);
        e = e->next; idx++;
    }
}

void RefindPageBuild(HWND hParent, HFONT fontTitle, HFONT fontBody, HFONT fontSmall)
{
    s_hList = NULL; s_hStatus = NULL;
    if (s_entries) { RefindConfigFreeEntries(s_entries); s_entries = NULL; }

    RECT rc; GetClientRect(hParent, &rc);
    int w = rc.right - CONTENT_PADDING * 2;
    int h = rc.bottom;

    HWND hTitle = CreateWindowExW(0, L"STATIC", L"rEFInd 管理",
        WS_CHILD | WS_VISIBLE, CONTENT_PADDING, CONTENT_PADDING, w, 32,
        hParent, NULL, NULL, NULL);
    SendMessageW(hTitle, WM_SETFONT, (WPARAM)fontTitle, TRUE);

    HWND hDesc = CreateWindowExW(0, L"STATIC",
        L"管理 rEFInd 安装状态，以及 Boot Manager Pro 写入的 menuentry 条目。",
        WS_CHILD | WS_VISIBLE,
        CONTENT_PADDING, CONTENT_PADDING + 40, w, 24,
        hParent, NULL, NULL, NULL);
    SendMessageW(hDesc, WM_SETFONT, (WPARAM)fontBody, TRUE);

    // 安装/卸载按钮
    HWND hBtnInstall = CreateWindowExW(0, L"BUTTON", L"安装 rEFInd",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        CONTENT_PADDING, CONTENT_PADDING + 80, 140, BTN_HEIGHT,
        hParent, (HMENU)ID_BTN_INSTALL, NULL, NULL);
    SendMessageW(hBtnInstall, WM_SETFONT, (WPARAM)fontBody, TRUE);

    HWND hBtnUninstall = CreateWindowExW(0, L"BUTTON", L"卸载 rEFInd",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        CONTENT_PADDING + 150, CONTENT_PADDING + 80, 140, BTN_HEIGHT,
        hParent, (HMENU)ID_BTN_UNINSTALL, NULL, NULL);
    SendMessageW(hBtnUninstall, WM_SETFONT, (WPARAM)fontBody, TRUE);

    // 添加菜单按钮
    HWND hBtnAddMenu = CreateWindowExW(0, L"BUTTON", L"▼ 添加启动项",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        CONTENT_PADDING + 300, CONTENT_PADDING + 80, 140, BTN_HEIGHT,
        hParent, (HMENU)ID_BTN_ADD_MENU, NULL, NULL);
    SendMessageW(hBtnAddMenu, WM_SETFONT, (WPARAM)fontBody, TRUE);

    HWND hListTitle = CreateWindowExW(0, L"STATIC", L"menuentry 列表",
        WS_CHILD | WS_VISIBLE,
        CONTENT_PADDING, CONTENT_PADDING + 140, w, 24,
        hParent, NULL, NULL, NULL);
    SendMessageW(hListTitle, WM_SETFONT, (WPARAM)fontBody, TRUE);

    s_hList = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEW, NULL,
        WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL,
        CONTENT_PADDING, CONTENT_PADDING + 170, w, h - 300,
        hParent, (HMENU)ID_LIST_REFIND, NULL, NULL);
    ListView_SetExtendedListViewStyle(s_hList, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);

    LVCOLUMN lvc = {0}; lvc.mask = LVCF_TEXT | LVCF_WIDTH;
    lvc.pszText = L"托管";    lvc.cx = 80;        ListView_InsertColumn(s_hList, 0, &lvc);
    lvc.pszText = L"标题";    lvc.cx = 220;       ListView_InsertColumn(s_hList, 1, &lvc);
    lvc.pszText = L"Loader";  lvc.cx = 260;       ListView_InsertColumn(s_hList, 2, &lvc);
    lvc.pszText = L"Options"; lvc.cx = w - 560;   ListView_InsertColumn(s_hList, 3, &lvc);

    HWND hBtnRefresh = CreateWindowExW(0, L"BUTTON", L"刷新条目",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        CONTENT_PADDING, h - 110, 120, BTN_HEIGHT,
        hParent, (HMENU)ID_BTN_REFIND_REFRESH, NULL, NULL);
    SendMessageW(hBtnRefresh, WM_SETFONT, (WPARAM)fontBody, TRUE);

    HWND hBtnDelete = CreateWindowExW(0, L"BUTTON", L"删除选中条目",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        CONTENT_PADDING + 130, h - 110, 140, BTN_HEIGHT,
        hParent, (HMENU)ID_BTN_REFIND_DELETE, NULL, NULL);
    SendMessageW(hBtnDelete, WM_SETFONT, (WPARAM)fontBody, TRUE);

    HWND hNote = CreateWindowExW(0, L"STATIC",
        L"仅允许删除 Boot Manager Pro 托管条目；普通用户自定义条目只读显示。",
        WS_CHILD | WS_VISIBLE,
        CONTENT_PADDING, h - 70, w, 24,
        hParent, NULL, NULL, NULL);
    SendMessageW(hNote, WM_SETFONT, (WPARAM)fontSmall, TRUE);

    s_hStatus = CreateWindowExW(0, L"STATIC", L"点击「刷新条目」加载列表",
        WS_CHILD | WS_VISIBLE,
        CONTENT_PADDING, h - 30, w, 24,
        hParent, (HMENU)ID_STATUS_TEXT, NULL, NULL);
    SendMessageW(s_hStatus, WM_SETFONT, (WPARAM)fontSmall, TRUE);

    // 不自动刷新，避免卡顿
}

// 删除选中条目
void RefindPageDeleteSelected(HWND hWnd)
{
    if (!s_hList) return;
    int sel = ListView_GetNextItem(s_hList, -1, LVNI_SELECTED);
    if (sel == -1) {
        MessageBoxW(hWnd, L"请先选择一个 rEFInd 条目", L"提示", MB_OK | MB_ICONINFORMATION);
        return;
    }

    WCHAR managed[32] = {0};
    WCHAR title[256]  = {0};
    ListView_GetItemText(s_hList, sel, 0, managed, 32);
    ListView_GetItemText(s_hList, sel, 1, title, 256);

    if (_wcsicmp(managed, L"是") != 0) {
        MessageBoxW(hWnd,
            L"该条目不是 Boot Manager Pro 创建的托管项，暂不允许直接删除，避免误删用户自定义配置。",
            L"提示", MB_OK | MB_ICONWARNING);
        return;
    }

    WCHAR prompt[512];
    swprintf(prompt, 512, L"确定删除 rEFInd 条目？\n\n标题：%s", title);
    if (MessageBoxW(hWnd, prompt, L"确认删除", MB_YESNO | MB_ICONQUESTION) != IDYES) return;

    WCHAR espDrive[4] = {0};
    SetStatus(L"⏳ 正在删除 rEFInd 条目...");
    if (!MountESP(espDrive)) {
        MessageBoxW(hWnd, L"ESP 分区挂载失败", L"错误", MB_OK | MB_ICONERROR);
        SetStatus(L"✗ 挂载失败");
        return;
    }

    if (RefindConfigRemoveMenuEntry(espDrive, title)) {
        MessageBoxW(hWnd, L"rEFInd 条目已删除", L"完成", MB_OK | MB_ICONINFORMATION);
        SetStatus(L"✓ 条目已删除");
        RefindPageRefresh();
    } else {
        MessageBoxW(hWnd, L"删除失败，可能是 refind.conf 不存在或条目未找到。", L"错误", MB_OK | MB_ICONERROR);
        SetStatus(L"✗ 删除失败");
    }
    UnmountESP(espDrive[0]);
}

// 处理添加启动项下拉菜单
void RefindPageShowAddMenu(HWND hWnd)
{
    POINT pt;
    GetCursorPos(&pt);
    
    HMENU hMenu = CreatePopupMenu();
    AppendMenuW(hMenu, MF_STRING, ID_MENU_ADD_EFI, L"添加 EFI 菜单项");
    AppendMenuW(hMenu, MF_STRING, ID_MENU_ADD_WIM, L"添加 WIM 菜单项");
    AppendMenuW(hMenu, MF_STRING, ID_MENU_ADD_VHD, L"添加 VHD 菜单项");
    
    TrackPopupMenu(hMenu, TPM_LEFTALIGN | TPM_TOPALIGN,
        pt.x, pt.y, 0, hWnd, NULL);
    
    DestroyMenu(hMenu);
}

// 处理添加 WIM 启动项
void RefindPageAddWim(HWND hWnd)
{
    WCHAR wimPath[MAX_PATH] = {0};
    if (!WimSelectFileDialog(hWnd, wimPath, MAX_PATH)) return;
    
    WCHAR defaultName[MAX_PATH];
    const WCHAR* lastNameSep = wcsrchr(wimPath, L'\\');
    if (lastNameSep) {
        wcsncpy(defaultName, lastNameSep + 1, MAX_PATH);
        WCHAR* dot = wcschr(defaultName, L'.');
        if (dot) *dot = L'\0';
    } else {
        wcsncpy(defaultName, L"Windows PE", MAX_PATH);
    }
    
    SetStatus(L"⏳ 正在添加 WIM 菜单项...");
    if (WimAddBootEntry(defaultName, wimPath, L"1")) {
        WCHAR msg[MAX_PATH + 64];
        swprintf(msg, MAX_PATH + 64, L"WIM 菜单项添加成功!\n\n名称：%s\n路径：%s", defaultName, wimPath);
        MessageBoxW(hWnd, msg, L"完成", MB_OK | MB_ICONINFORMATION);
        SetStatus(L"✓ WIM 菜单项已添加");
        RefindPageRefresh();
    } else {
        MessageBoxW(hWnd, L"WIM 菜单项添加失败\n\n请确保 rEFInd 已安装，且有管理员权限。", L"错误", MB_OK | MB_ICONERROR);
        SetStatus(L"✗ 添加失败");
    }
}

// 处理添加 VHD 启动项
void RefindPageAddVhd(HWND hWnd)
{
    WCHAR vhdPath[MAX_PATH] = {0};
    if (!VhdSelectFileDialog(hWnd, vhdPath, MAX_PATH)) return;
    
    WCHAR defaultName[MAX_PATH];
    const WCHAR* lastNameSep = wcsrchr(vhdPath, L'\\');
    if (lastNameSep) {
        wcsncpy(defaultName, lastNameSep + 1, MAX_PATH);
        WCHAR* dot = wcschr(defaultName, L'.');
        if (dot) *dot = L'\0';
    } else {
        wcsncpy(defaultName, L"VHD Windows", MAX_PATH);
    }
    
    SetStatus(L"⏳ 正在添加 VHD 菜单项...");
    if (VhdAddBootEntry(defaultName, vhdPath)) {
        WCHAR msg[MAX_PATH + 64];
        swprintf(msg, MAX_PATH + 64, L"VHD 菜单项添加成功!\n\n名称：%s\n路径：%s", defaultName, vhdPath);
        MessageBoxW(hWnd, msg, L"完成", MB_OK | MB_ICONINFORMATION);
        SetStatus(L"✓ VHD 菜单项已添加");
        RefindPageRefresh();
    } else {
        MessageBoxW(hWnd, L"VHD 菜单项添加失败\n\n请确保 rEFInd 已安装，且有管理员权限。", L"错误", MB_OK | MB_ICONERROR);
        SetStatus(L"✗ 添加失败");
    }
}