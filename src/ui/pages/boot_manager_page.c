/**
 * boot_manager_page.c - 启动管理页面实现
 * 
 * 根据 BIOS 模式自动切换 UI 内容
 */

#include "boot_manager_page.h"
#include "../../../include/boot_mode.h"
#include "../../../include/uefi_nvram.h"
#include "../../../include/limine.h"
#include "../../../include/bcd_manager.h"
#include "../../../include/mbr_manager.h"
#include "../../../include/pbr_manager.h"
#include "../../core/uefi.h"
#include "../../core/refind.h"
#include "../../core/wimboot.h"
#include <windowsx.h>
#include <commctrl.h>
#include <wchar.h>
#include <stdio.h>

// ============================================
// 内部变量
// ============================================
static HWND g_hTabCtrl = NULL;
static HWND g_hListView = NULL;
static HWND g_hStatusText = NULL;
static HWND g_hTabContent[TAB_COUNT] = {0};
static int g_currentTab = 0;
static BOOL g_isUEFI = TRUE;
static HFONT g_fontTitle = NULL;
static HFONT g_fontBody = NULL;
static HFONT g_fontSmall = NULL;
static UEFI_BOOT_LIST* g_bootList = NULL;

// ============================================
// 控件 ID
// ============================================
enum {
    ID_TAB_BOOT = 1000,
    ID_LIST_BOOT_ENTRIES = 1001,
    ID_BTN_REFRESH = 1010,
    ID_BTN_ADD = 1011,
    ID_BTN_DELETE = 1012,
    ID_BTN_UP = 1013,
    ID_BTN_DOWN = 1014,
    ID_BTN_SET_DEFAULT = 1015,
    ID_BTN_INSTALL = 1020,
    ID_BTN_UNINSTALL = 1021,
    ID_BTN_ADD_WIM = 1022,
    ID_BTN_ADD_VHD = 1023,
    ID_BTN_ADD_EFI = 1024,
    ID_BTN_PBR_BACKUP = 1030,
    ID_BTN_PBR_RESTORE = 1031,
    ID_BTN_SET_ACTIVE = 1032,
    // BCD 启动项类型
    ID_BTN_ADD_RAM = 1040,      // RAM 启动
    ID_BTN_ADD_WINPE = 1041,    // WinPE 启动
    ID_BTN_ADD_ISO = 1042,      // ISO 启动 (通过 Limine)
    ID_BTN_ADD_ESD = 1043,      // eSD 启动
    ID_STATUS = 1099
};

// ============================================
// 内部函数声明
// ============================================
static void BuildUEFITab(HWND hParent);
static void BuildRefindTab(HWND hParent);
static void BuildBCDTab(HWND hParent);
static void BuildMBRTab(HWND hParent);
static void BuildPBRTab(HWND hParent);
static void RefreshUEFIList(void);
static void RefreshRefindList(void);
static void RefreshBCDList(void);
static void RefreshMBRList(void);
static void RefreshPBRList(void);
static void SetStatusText(const WCHAR* text);

// ============================================
// 初始化
// ============================================
void BootManagerPage_Init(HWND hParent, HFONT fontTitle, HFONT fontBody, HFONT fontSmall)
{
    g_fontTitle = fontTitle;
    g_fontBody = fontBody;
    g_fontSmall = fontSmall;
    g_isUEFI = BootMode_IsUEFIFirmware();
}

// ============================================
// 构建主页面
// ============================================
void BootManagerPage_Build(HWND hParent)
{
    RECT rc;
    GetClientRect(hParent, &rc);
    int w = rc.right - 64;
    int h = rc.bottom;
    int padding = 32;

    // 模式指示器
    WCHAR modeText[64];
    if (g_isUEFI) {
        wcscpy(modeText, L"UEFI 模式");
    } else {
        wcscpy(modeText, L"MBR/BIOS 模式");
    }
    
    HWND hModeLabel = CreateWindowExW(0, L"STATIC", modeText,
        WS_CHILD | WS_VISIBLE, padding, padding, 200, 24,
        hParent, NULL, NULL, NULL);
    SendMessageW(hModeLabel, WM_SETFONT, (WPARAM)g_fontSmall, TRUE);

    // Tab 控件
    g_hTabCtrl = CreateWindowExW(0, WC_TABCONTROL, NULL,
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
        padding, padding + 30, w, h - 120,
        hParent, (HMENU)ID_TAB_BOOT, NULL, NULL);
    SendMessageW(g_hTabCtrl, WM_SETFONT, (WPARAM)g_fontBody, TRUE);

    // 根据 BIOS 模式添加不同的 Tab
    TCITEM tie = {0};
    tie.mask = TCIF_TEXT;
    
    if (g_isUEFI) {
        // UEFI 模式: UEFI启动项 | rEFInd | BCD菜单
        tie.pszText = L"UEFI 启动项";
        TabCtrl_InsertItem(g_hTabCtrl, TAB_UEFI_BOOT, &tie);
        tie.pszText = L"rEFInd";
        TabCtrl_InsertItem(g_hTabCtrl, TAB_REFIND, &tie);
        tie.pszText = L"BCD 菜单";
        TabCtrl_InsertItem(g_hTabCtrl, TAB_BCD_MENU, &tie);
    } else {
        // MBR 模式: MBR引导 | PBR管理 | BCD菜单
        tie.pszText = L"MBR 引导 (Limine)";
        TabCtrl_InsertItem(g_hTabCtrl, TAB_MBR_BOOT, &tie);
        tie.pszText = L"PBR 管理";
        TabCtrl_InsertItem(g_hTabCtrl, TAB_PBR_MANAGER, &tie);
        tie.pszText = L"BCD 菜单";
        TabCtrl_InsertItem(g_hTabCtrl, TAB_BCD_MENU, &tie);
    }

    // 创建 Tab 内容区域（所有 Tab 共用一个区域，切换时显示/隐藏）
    for (int i = 0; i < TAB_COUNT; i++) {
        g_hTabContent[i] = CreateWindowExW(WS_EX_CONTROLPARENT, L"STATIC", NULL,
            WS_CHILD | WS_CLIPCHILDREN,
            padding + 4, padding + 58, w - 8, h - 180,
            hParent, NULL, NULL, NULL);
    }

    // 构建各 Tab 页面内容
    if (g_isUEFI) {
        BuildUEFITab(g_hTabContent[TAB_UEFI_BOOT]);
        BuildRefindTab(g_hTabContent[TAB_REFIND]);
        BuildBCDTab(g_hTabContent[TAB_BCD_MENU]);
    } else {
        BuildMBRTab(g_hTabContent[TAB_MBR_BOOT]);
        BuildPBRTab(g_hTabContent[TAB_PBR_MANAGER]);
        BuildBCDTab(g_hTabContent[TAB_BCD_MENU]);
    }

    // 默认显示第一个 Tab
    if (g_isUEFI) {
        g_currentTab = TAB_UEFI_BOOT;
        ShowWindow(g_hTabContent[TAB_UEFI_BOOT], SW_SHOW);
    } else {
        g_currentTab = TAB_MBR_BOOT;
        ShowWindow(g_hTabContent[TAB_MBR_BOOT], SW_SHOW);
    }

    // 状态栏
    g_hStatusText = CreateWindowExW(0, L"STATIC", L"就绪",
        WS_CHILD | WS_VISIBLE, padding, h - 40, w, 24,
        hParent, (HMENU)ID_STATUS, NULL, NULL);
    SendMessageW(g_hStatusText, WM_SETFONT, (WPARAM)g_fontSmall, TRUE);
}

// ============================================
// 构建各个 Tab 页面
// ============================================

// UEFI 启动项页面
static void BuildUEFITab(HWND hParent)
{
    RECT rc;
    GetClientRect(hParent, &rc);
    int w = rc.right;
    int h = rc.bottom;

    // 列表视图
    g_hListView = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEW, NULL,
        WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL,
        0, 0, w, h - 60, hParent, (HMENU)ID_LIST_BOOT_ENTRIES, NULL, NULL);
    ListView_SetExtendedListViewStyle(g_hListView, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);

    // 列
    LVCOLUMN lvc = {0};
    lvc.mask = LVCF_TEXT | LVCF_WIDTH;
    lvc.pszText = L"序号"; lvc.cx = 50;
    ListView_InsertColumn(g_hListView, 0, &lvc);
    lvc.pszText = L"ID"; lvc.cx = 70;
    ListView_InsertColumn(g_hListView, 1, &lvc);
    lvc.pszText = L"名称"; lvc.cx = 200;
    ListView_InsertColumn(g_hListView, 2, &lvc);
    lvc.pszText = L"路径"; lvc.cx = w - 340;
    ListView_InsertColumn(g_hListView, 3, &lvc);

    // 按钮
    int bx = 0, by = h - 50;
    #define BTN(id, text, wd) \
        CreateWindowExW(0, L"BUTTON", text, WS_CHILD | WS_VISIBLE, bx, by, wd, 32, hParent, (HMENU)id, NULL, NULL); \
        SendMessageW(GetDlgItem(hParent, id), WM_SETFONT, (WPARAM)g_fontBody, TRUE); \
        bx += wd + 10;

    BTN(ID_BTN_REFRESH, L"刷新", 70);
    BTN(ID_BTN_ADD_EFI, L"添加EFI", 80);
    BTN(ID_BTN_DELETE, L"删除", 60);
    BTN(ID_BTN_UP, L"上移", 60);
    BTN(ID_BTN_DOWN, L"下移", 60);
    BTN(ID_BTN_SET_DEFAULT, L"设为默认", 80);

    #undef BTN

    // 加载数据
    RefreshUEFIList();
}

// rEFInd 页面
static void BuildRefindTab(HWND hParent)
{
    RECT rc;
    GetClientRect(hParent, &rc);
    int w = rc.right;
    int h = rc.bottom;

    // 标题
    HWND hTitle = CreateWindowExW(0, L"STATIC", L"rEFInd 引导管理器",
        WS_CHILD | WS_VISIBLE, 0, 10, w, 28, hParent, NULL, NULL, NULL);
    SendMessageW(hTitle, WM_SETFONT, (WPARAM)g_fontTitle, TRUE);

    // 描述
    HWND hDesc = CreateWindowExW(0, L"STATIC",
        L"rEFInd 是现代化的 UEFI 引导管理器。\n"
        L"安装后会自动扫描并列出所有可启动的操作系统，无需手动配置。",
        WS_CHILD | WS_VISIBLE, 0, 50, w, 48, hParent, NULL, NULL, NULL);
    SendMessageW(hDesc, WM_SETFONT, (WPARAM)g_fontSmall, TRUE);

    // 提示信息
    HWND hTip = CreateWindowExW(0, L"STATIC",
        L"安装位置: ESP 分区 \\EFI\\refind\\\n"
        L"配置文件: \\EFI\\refind\\refind.conf",
        WS_CHILD | WS_VISIBLE, 0, 110, w, 48, hParent, NULL, NULL, NULL);
    SendMessageW(hTip, WM_SETFONT, (WPARAM)g_fontSmall, TRUE);

    // 按钮 - 只有安装和卸载
    int bx = 0, by = h - 60;
    #define BTN(id, text, wd) \
        CreateWindowExW(0, L"BUTTON", text, WS_CHILD | WS_VISIBLE, bx, by, wd, 36, hParent, (HMENU)id, NULL, NULL); \
        SendMessageW(GetDlgItem(hParent, id), WM_SETFONT, (WPARAM)g_fontBody, TRUE); \
        bx += wd + 15;

    BTN(ID_BTN_INSTALL, L"安装 rEFInd", 120);
    BTN(ID_BTN_UNINSTALL, L"卸载 rEFInd", 120);

    #undef BTN

    // 加载数据
    RefreshRefindList();
}

// BCD 菜单页面
static void BuildBCDTab(HWND hParent)
{
    RECT rc;
    GetClientRect(hParent, &rc);
    int w = rc.right;
    int h = rc.bottom;

    // 标题
    HWND hTitle = CreateWindowExW(0, L"STATIC", L"BCD 启动菜单管理",
        WS_CHILD | WS_VISIBLE, 0, 10, w, 28, hParent, NULL, NULL, NULL);
    SendMessageW(hTitle, WM_SETFONT, (WPARAM)g_fontTitle, TRUE);

    // 描述
    HWND hDesc = CreateWindowExW(0, L"STATIC",
        L"管理 Windows 启动菜单（BCD），支持多种启动方式。",
        WS_CHILD | WS_VISIBLE, 0, 45, w, 24, hParent, NULL, NULL, NULL);
    SendMessageW(hDesc, WM_SETFONT, (WPARAM)g_fontSmall, TRUE);

    // 启动方式说明
    HWND hTypes = CreateWindowExW(0, L"STATIC",
        L"WIM: 映像文件  |  VHD: 虚拟磁盘  |  RAM: 内存启动  |  WinPE: 预安装环境  |  eSD: 压缩映像  |  ISO: 光盘镜像",
        WS_CHILD | WS_VISIBLE, 0, 72, w, 20, hParent, NULL, NULL, NULL);
    SendMessageW(hTypes, WM_SETFONT, (WPARAM)g_fontSmall, TRUE);

    // 列表视图
    HWND hList = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEW, NULL,
        WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL,
        0, 100, w, h - 190, hParent, (HMENU)ID_LIST_BOOT_ENTRIES, NULL, NULL);
    ListView_SetExtendedListViewStyle(hList, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);

    LVCOLUMN lvc = {0};
    lvc.mask = LVCF_TEXT | LVCF_WIDTH;
    lvc.pszText = L"序号"; lvc.cx = 50;
    ListView_InsertColumn(hList, 0, &lvc);
    lvc.pszText = L"名称"; lvc.cx = 180;
    ListView_InsertColumn(hList, 1, &lvc);
    lvc.pszText = L"类型"; lvc.cx = 70;
    ListView_InsertColumn(hList, 2, &lvc);
    lvc.pszText = L"标识符"; lvc.cx = 250;
    ListView_InsertColumn(hList, 3, &lvc);
    lvc.pszText = L"路径"; lvc.cx = w - 570;
    ListView_InsertColumn(hList, 4, &lvc);

    // 第一行按钮：刷新、删除、排序、设默认
    int bx = 0, by = h - 80;
    #define BTN(id, text, wd) \
        CreateWindowExW(0, L"BUTTON", text, WS_CHILD | WS_VISIBLE, bx, by, wd, 28, hParent, (HMENU)id, NULL, NULL); \
        SendMessageW(GetDlgItem(hParent, id), WM_SETFONT, (WPARAM)g_fontSmall, TRUE); \
        bx += wd + 8;

    BTN(ID_BTN_REFRESH, L"刷新", 55);
    BTN(ID_BTN_DELETE, L"删除", 55);
    BTN(ID_BTN_UP, L"上移", 55);
    BTN(ID_BTN_DOWN, L"下移", 55);
    BTN(ID_BTN_SET_DEFAULT, L"设默认", 65);

    #undef BTN

    // 第二行按钮：添加各种启动项
    bx = 0; by = h - 48;
    #define BTN(id, text, wd) \
        CreateWindowExW(0, L"BUTTON", text, WS_CHILD | WS_VISIBLE, bx, by, wd, 28, hParent, (HMENU)id, NULL, NULL); \
        SendMessageW(GetDlgItem(hParent, id), WM_SETFONT, (WPARAM)g_fontSmall, TRUE); \
        bx += wd + 6;

    BTN(ID_BTN_ADD_WIM, L"+WIM", 50);
    BTN(ID_BTN_ADD_VHD, L"+VHD", 50);
    BTN(ID_BTN_ADD_RAM, L"+RAM", 50);
    BTN(ID_BTN_ADD_WINPE, L"+WinPE", 55);
    BTN(ID_BTN_ADD_ESD, L"+eSD", 50);
    BTN(ID_BTN_ADD_ISO, L"+ISO", 50);

    #undef BTN
}

// MBR 引导页面 (Limine)
static void BuildMBRTab(HWND hParent)
{
    RECT rc;
    GetClientRect(hParent, &rc);
    int w = rc.right;
    int h = rc.bottom;

    // 标题
    HWND hTitle = CreateWindowExW(0, L"STATIC", L"MBR 引导管理 (Limine)",
        WS_CHILD | WS_VISIBLE, 0, 10, w, 28, hParent, NULL, NULL, NULL);
    SendMessageW(hTitle, WM_SETFONT, (WPARAM)g_fontTitle, TRUE);

    // 描述
    HWND hDesc = CreateWindowExW(0, L"STATIC",
        L"Limine 是现代化的引导加载器，支持 MBR 和 GPT 磁盘。\n"
        L"安装后会自动扫描并列出所有可启动的操作系统。",
        WS_CHILD | WS_VISIBLE, 0, 50, w, 48, hParent, NULL, NULL, NULL);
    SendMessageW(hDesc, WM_SETFONT, (WPARAM)g_fontSmall, TRUE);

    // 提示信息
    HWND hTip = CreateWindowExW(0, L"STATIC",
        L"安装位置: 启动分区 \\boot\\limine\\\n"
        L"配置文件: \\boot\\limine\\limine.conf",
        WS_CHILD | WS_VISIBLE, 0, 110, w, 48, hParent, NULL, NULL, NULL);
    SendMessageW(hTip, WM_SETFONT, (WPARAM)g_fontSmall, TRUE);

    // 按钮 - 只有安装和卸载
    int bx = 0, by = h - 60;
    #define BTN(id, text, wd) \
        CreateWindowExW(0, L"BUTTON", text, WS_CHILD | WS_VISIBLE, bx, by, wd, 36, hParent, (HMENU)id, NULL, NULL); \
        SendMessageW(GetDlgItem(hParent, id), WM_SETFONT, (WPARAM)g_fontBody, TRUE); \
        bx += wd + 15;

    BTN(ID_BTN_INSTALL, L"安装 Limine", 120);
    BTN(ID_BTN_UNINSTALL, L"卸载 Limine", 120);

    #undef BTN
}

// PBR 管理页面
static void BuildPBRTab(HWND hParent)
{
    RECT rc;
    GetClientRect(hParent, &rc);
    int w = rc.right;
    int h = rc.bottom;

    // 标题
    HWND hTitle = CreateWindowExW(0, L"STATIC", L"分区引导记录 (PBR) 管理",
        WS_CHILD | WS_VISIBLE, 0, 10, w, 28, hParent, NULL, NULL, NULL);
    SendMessageW(hTitle, WM_SETFONT, (WPARAM)g_fontTitle, TRUE);

    // 描述
    HWND hDesc = CreateWindowExW(0, L"STATIC",
        L"PBR 是分区引导记录，控制分区如何引导系统。\n"
        L"支持备份、恢复和设置活动分区。",
        WS_CHILD | WS_VISIBLE, 0, 45, w, 48, hParent, NULL, NULL, NULL);
    SendMessageW(hDesc, WM_SETFONT, (WPARAM)g_fontSmall, TRUE);

    // 列表视图（显示分区列表）
    HWND hList = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEW, NULL,
        WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL,
        0, 100, w, h - 170, hParent, (HMENU)ID_LIST_BOOT_ENTRIES, NULL, NULL);
    ListView_SetExtendedListViewStyle(hList, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);

    LVCOLUMN lvc = {0};
    lvc.mask = LVCF_TEXT | LVCF_WIDTH;
    lvc.pszText = L"分区"; lvc.cx = 80;
    ListView_InsertColumn(hList, 0, &lvc);
    lvc.pszText = L"文件系统"; lvc.cx = 100;
    ListView_InsertColumn(hList, 1, &lvc);
    lvc.pszText = L"引导类型"; lvc.cx = 150;
    ListView_InsertColumn(hList, 2, &lvc);
    lvc.pszText = L"状态"; lvc.cx = 80;
    ListView_InsertColumn(hList, 3, &lvc);
    lvc.pszText = L"备注"; lvc.cx = w - 430;
    ListView_InsertColumn(hList, 4, &lvc);

    // 按钮 - 备份/恢复/设置活动分区
    int bx = 0, by = h - 55;
    #define BTN(id, text, wd) \
        CreateWindowExW(0, L"BUTTON", text, WS_CHILD | WS_VISIBLE, bx, by, wd, 32, hParent, (HMENU)id, NULL, NULL); \
        SendMessageW(GetDlgItem(hParent, id), WM_SETFONT, (WPARAM)g_fontBody, TRUE); \
        bx += wd + 10;

    BTN(ID_BTN_REFRESH, L"刷新", 70);
    BTN(ID_BTN_PBR_BACKUP, L"备份 PBR", 90);
    BTN(ID_BTN_PBR_RESTORE, L"恢复 PBR", 90);
    BTN(ID_BTN_SET_ACTIVE, L"设活动分区", 100);

    #undef BTN
}

// ============================================
// 刷新列表
// ============================================

static void RefreshUEFIList(void)
{
    HWND hList = GetDlgItem(g_hTabContent[TAB_UEFI_BOOT], ID_LIST_BOOT_ENTRIES);
    if (!hList) return;
    ListView_DeleteAllItems(hList);

    if (g_bootList) UefiFreeBootList(g_bootList);
    g_bootList = UefiScanBootEntries();

    if (!g_bootList || g_bootList->count == 0) {
        LVITEM lvi = {0};
        lvi.mask = LVIF_TEXT;
        lvi.pszText = L"-";
        lvi.iItem = 0;
        ListView_InsertItem(hList, &lvi);
        ListView_SetItemText(hList, 0, 1, L"-");
        ListView_SetItemText(hList, 0, 2, L"未检测到 UEFI 启动项");
        ListView_SetItemText(hList, 0, 3, L"请确保以管理员身份运行");
        return;
    }

    UEFI_BOOT_ENTRY_WRAPPER* entry = g_bootList->entries;
    int idx = 1;
    while (entry) {
        WCHAR num[8];
        swprintf(num, 8, L"%d", idx);
        LVITEM lvi = {0};
        lvi.mask = LVIF_TEXT;
        lvi.iItem = idx - 1;
        lvi.pszText = num;
        ListView_InsertItem(hList, &lvi);

        WCHAR id[16];
        swprintf(id, 16, L"%04X", entry->id);
        ListView_SetItemText(hList, idx - 1, 1, id);
        ListView_SetItemText(hList, idx - 1, 2, entry->name);
        ListView_SetItemText(hList, idx - 1, 3, entry->filePath);

        entry = entry->next;
        idx++;
    }

    SetStatusText(L"UEFI 启动项列表已加载");
}

static void RefreshRefindList(void)
{
    HWND hList = GetDlgItem(g_hTabContent[TAB_REFIND], ID_LIST_BOOT_ENTRIES);
    if (!hList) return;
    ListView_DeleteAllItems(hList);

    // TODO: 从 refind.conf 加载菜单项
    // 暂时显示提示
    LVITEM lvi = {0};
    lvi.mask = LVIF_TEXT;
    lvi.pszText = L"(自动检测)";
    lvi.iItem = 0;
    ListView_InsertItem(hList, &lvi);
    ListView_SetItemText(hList, 0, 1, L"rEFInd 会自动检测系统");

    SetStatusText(L"rEFInd 菜单列表已加载");
}

static void RefreshBCDList(void)
{
    HWND hList = GetDlgItem(g_hTabContent[TAB_BCD_MENU], ID_LIST_BOOT_ENTRIES);
    if (!hList) return;
    ListView_DeleteAllItems(hList);

    // 加载真实的 BCD 启动项
    BCD_LIST bcdList;
    BCD_InitList(&bcdList);
    
    WCHAR error[256] = {0};
    if (!BCD_GetEntries(&bcdList, error, 256)) {
        // 加载失败，显示错误
        LVITEM lvi = {0};
        lvi.mask = LVIF_TEXT;
        lvi.pszText = L"-";
        lvi.iItem = 0;
        ListView_InsertItem(hList, &lvi);
        ListView_SetItemText(hList, 0, 1, error[0] ? error : L"无法加载 BCD");
        ListView_SetItemText(hList, 0, 2, L"-");
        ListView_SetItemText(hList, 0, 3, L"-");
        ListView_SetItemText(hList, 0, 4, L"-");
        BCD_FreeList(&bcdList);
        SetStatusText(L"BCD 加载失败");
        return;
    }
    
    // 没有启动项
    if (bcdList.count == 0) {
        LVITEM lvi = {0};
        lvi.mask = LVIF_TEXT;
        lvi.pszText = L"-";
        lvi.iItem = 0;
        ListView_InsertItem(hList, &lvi);
        ListView_SetItemText(hList, 0, 1, L"(无启动项)");
        ListView_SetItemText(hList, 0, 2, L"-");
        ListView_SetItemText(hList, 0, 3, L"-");
        ListView_SetItemText(hList, 0, 4, L"-");
        BCD_FreeList(&bcdList);
        SetStatusText(L"BCD 无启动项");
        return;
    }
    
    // 填充列表
    for (int i = 0; i < bcdList.count; i++) {
        BCD_ENTRY* entry = &bcdList.entries[i];
        
        WCHAR num[8];
        swprintf(num, 8, L"%d", i + 1);
        
        WCHAR guidStr[64];
        GUID_ToString(&entry->id, guidStr, 64);
        
        // 类型名称
        WCHAR typeName[16];
        switch (entry->type) {
            case BCD_TYPE_OSLOADER: wcscpy(typeName, L"OSLoader"); break;
            case BCD_TYPE_BOOTMGR: wcscpy(typeName, L"BootMgr"); break;
            case BCD_TYPE_MEMDIAG: wcscpy(typeName, L"MemDiag"); break;
            case BCD_TYPE_NTLDR: wcscpy(typeName, L"NTLDR"); break;
            default: wcscpy(typeName, L"其他"); break;
        }
        
        LVITEM lvi = {0};
        lvi.mask = LVIF_TEXT;
        lvi.iItem = i;
        lvi.pszText = num;
        ListView_InsertItem(hList, &lvi);
        ListView_SetItemText(hList, i, 1, entry->name[0] ? entry->name : L"(未命名)");
        ListView_SetItemText(hList, i, 2, typeName);
        ListView_SetItemText(hList, i, 3, guidStr);
        ListView_SetItemText(hList, i, 4, entry->path[0] ? entry->path : L"-");
    }
    
    WCHAR status[64];
    swprintf(status, 64, L"已加载 %d 个 BCD 启动项", bcdList.count);
    SetStatusText(status);
    
    BCD_FreeList(&bcdList);
}

static void RefreshMBRList(void)
{
    HWND hList = GetDlgItem(g_hTabContent[TAB_MBR_BOOT], ID_LIST_BOOT_ENTRIES);
    if (!hList) return;
    ListView_DeleteAllItems(hList);

    // TODO: 从 limine.conf 加载菜单项
    LVITEM lvi = {0};
    lvi.mask = LVIF_TEXT;
    lvi.pszText = L"1";
    lvi.iItem = 0;
    ListView_InsertItem(hList, &lvi);
    ListView_SetItemText(hList, 0, 1, L"Windows");
    ListView_SetItemText(hList, 0, 2, L"hd0,msdos1");

    SetStatusText(L"MBR 引导列表已加载");
}

static void RefreshPBRList(void)
{
    HWND hList = GetDlgItem(g_hTabContent[TAB_PBR_MANAGER], ID_LIST_BOOT_ENTRIES);
    if (!hList) return;
    ListView_DeleteAllItems(hList);

    // TODO: 扫描分区
    LVITEM lvi = {0};
    lvi.mask = LVIF_TEXT;
    lvi.pszText = L"C:";
    lvi.iItem = 0;
    ListView_InsertItem(hList, &lvi);
    ListView_SetItemText(hList, 0, 1, L"NTFS");
    ListView_SetItemText(hList, 0, 2, L"BOOTMGR");
    ListView_SetItemText(hList, 0, 3, L"正常");

    SetStatusText(L"PBR 分区列表已加载");
}

// ============================================
// 公共接口
// ============================================

void BootManagerPage_Refresh(void)
{
    switch (g_currentTab) {
        case TAB_UEFI_BOOT:
            RefreshUEFIList();
            break;
        case TAB_REFIND:
            RefreshRefindList();
            break;
        case TAB_BCD_MENU:
            RefreshBCDList();
            break;
        case TAB_MBR_BOOT:
            RefreshMBRList();
            break;
        case TAB_PBR_MANAGER:
            RefreshPBRList();
            break;
    }
}

void BootManagerPage_SwitchTab(int tabIndex)
{
    if (tabIndex == g_currentTab) return;
    if (tabIndex < 0 || tabIndex >= TAB_COUNT) return;

    // 隐藏当前 Tab
    ShowWindow(g_hTabContent[g_currentTab], SW_HIDE);

    // 显示新 Tab
    g_currentTab = tabIndex;
    ShowWindow(g_hTabContent[tabIndex], SW_SHOW);

    // 刷新内容
    BootManagerPage_Refresh();
}

int BootManagerPage_GetCurrentTab(void)
{
    return g_currentTab;
}

HWND BootManagerPage_GetListView(void)
{
    return g_hListView;
}

void BootManagerPage_RefreshUEFIList(void)
{
    RefreshUEFIList();
}

void BootManagerPage_RefreshMBRList(void)
{
    RefreshMBRList();
}

void BootManagerPage_RefreshBCDList(void)
{
    RefreshBCDList();
}

static void SetStatusText(const WCHAR* text)
{
    if (g_hStatusText) {
        SetWindowTextW(g_hStatusText, text);
    }
}

void BootManagerPage_Cleanup(void)
{
    if (g_bootList) {
        UefiFreeBootList(g_bootList);
        g_bootList = NULL;
    }
}

BOOL BootManagerPage_OnCommand(HWND hWnd, WPARAM wParam, LPARAM lParam)
{
    WORD cmd = LOWORD(wParam);
    
    switch (cmd) {
        case ID_BTN_REFRESH:
            BootManagerPage_Refresh();
            return TRUE;

        case ID_BTN_INSTALL:
            if (g_currentTab == TAB_REFIND) {
                SetStatusText(L"正在安装 rEFInd...");
                // TODO: 调用 RefindInstall
                MessageBoxW(hWnd, L"rEFInd 安装功能待实现", L"提示", MB_OK | MB_ICONINFORMATION);
            } else if (g_currentTab == TAB_MBR_BOOT) {
                SetStatusText(L"正在安装 Limine...");
                // TODO: 调用 Limine_Install
                MessageBoxW(hWnd, L"Limine 安装功能待实现", L"提示", MB_OK | MB_ICONINFORMATION);
            }
            return TRUE;

        case ID_BTN_UNINSTALL:
            if (g_currentTab == TAB_REFIND) {
                SetStatusText(L"正在卸载 rEFInd...");
                // TODO: 调用 RefindUninstall
                MessageBoxW(hWnd, L"rEFInd 卸载功能待实现", L"提示", MB_OK | MB_ICONINFORMATION);
            } else if (g_currentTab == TAB_MBR_BOOT) {
                SetStatusText(L"正在卸载 Limine...");
                // TODO: 调用 Limine_Uninstall
                MessageBoxW(hWnd, L"Limine 卸载功能待实现", L"提示", MB_OK | MB_ICONINFORMATION);
            }
            return TRUE;

        case ID_BTN_ADD_EFI:
            SetStatusText(L"添加 EFI 启动项...");
            // TODO: 显示添加 EFI 对话框
            MessageBoxW(hWnd, L"添加 EFI 功能待集成", L"提示", MB_OK | MB_ICONINFORMATION);
            return TRUE;

        case ID_BTN_ADD_WIM: {
            SetStatusText(L"添加 WIM 启动项...");
            WCHAR wimPath[MAX_PATH] = {0};
            if (WimSelectFileDialog(hWnd, wimPath, MAX_PATH)) {
                // 简单输入名称对话框
                WCHAR name[128] = L"WIM 启动项";
                // 从文件名提取名称
                WCHAR* fileName = wcsrchr(wimPath, L'\\');
                if (fileName) {
                    fileName++;
                    wcsncpy(name, fileName, 127);
                    WCHAR* dot = wcsrchr(name, L'.');
                    if (dot) *dot = L'\0';
                }
                if (WimAddBootEntry(name, wimPath, L"1")) {
                    WCHAR msg[MAX_PATH + 64];
                    swprintf(msg, MAX_PATH + 64, L"已添加 WIM 启动项: %s", name);
                    SetStatusText(msg);
                    RefreshBCDList();
                } else {
                    MessageBoxW(hWnd, L"添加 WIM 启动项失败", L"错误", MB_OK | MB_ICONERROR);
                }
            }
            return TRUE;
        }

        case ID_BTN_ADD_VHD: {
            SetStatusText(L"添加 VHD 启动项...");
            WCHAR vhdPath[MAX_PATH] = {0};
            if (VhdSelectFileDialog(hWnd, vhdPath, MAX_PATH)) {
                WCHAR name[128] = L"VHD 启动项";
                WCHAR* fileName = wcsrchr(vhdPath, L'\\');
                if (fileName) {
                    fileName++;
                    wcsncpy(name, fileName, 127);
                    WCHAR* dot = wcsrchr(name, L'.');
                    if (dot) *dot = L'\0';
                }
                if (VhdAddBootEntry(name, vhdPath)) {
                    WCHAR msg[MAX_PATH + 64];
                    swprintf(msg, MAX_PATH + 64, L"已添加 VHD 启动项: %s", name);
                    SetStatusText(msg);
                    RefreshBCDList();
                } else {
                    MessageBoxW(hWnd, L"添加 VHD 启动项失败", L"错误", MB_OK | MB_ICONERROR);
                }
            }
            return TRUE;
        }

        case ID_BTN_ADD_RAM: {
            SetStatusText(L"添加 RAM 启动项...");
            WCHAR wimPath[MAX_PATH] = {0};
            if (RamSelectFileDialog(hWnd, wimPath, MAX_PATH)) {
                WCHAR name[128] = L"RAM 启动项";
                WCHAR* fileName = wcsrchr(wimPath, L'\\');
                if (fileName) {
                    fileName++;
                    wcsncpy(name, fileName, 127);
                    WCHAR* dot = wcsrchr(name, L'.');
                    if (dot) *dot = L'\0';
                }
                // RAM 启动需要 boot.sdi
                if (RamAddBootEntry(name, wimPath, NULL)) {
                    WCHAR msg[MAX_PATH + 64];
                    swprintf(msg, MAX_PATH + 64, L"已添加 RAM 启动项: %s", name);
                    SetStatusText(msg);
                    RefreshBCDList();
                } else {
                    MessageBoxW(hWnd, 
                        L"添加 RAM 启动项失败\n\n"
                        L"可能原因:\n"
                        L"1. 未找到 boot.sdi 文件\n"
                        L"2. 未检测到 ESP 分区\n"
                        L"3. rEFInd 未安装", 
                        L"错误", MB_OK | MB_ICONERROR);
                }
            }
            return TRUE;
        }

        case ID_BTN_ADD_WINPE: {
            SetStatusText(L"添加 WinPE 启动项...");
            WCHAR wimPath[MAX_PATH] = {0};
            if (WinPeSelectFileDialog(hWnd, wimPath, MAX_PATH)) {
                WCHAR name[128] = L"WinPE";
                WCHAR* fileName = wcsrchr(wimPath, L'\\');
                if (fileName) {
                    fileName++;
                    wcsncpy(name, fileName, 127);
                    WCHAR* dot = wcsrchr(name, L'.');
                    if (dot) *dot = L'\0';
                }
                if (WinPeAddBootEntry(name, wimPath)) {
                    WCHAR msg[MAX_PATH + 64];
                    swprintf(msg, MAX_PATH + 64, L"已添加 WinPE 启动项: %s", name);
                    SetStatusText(msg);
                    RefreshBCDList();
                } else {
                    MessageBoxW(hWnd, L"添加 WinPE 启动项失败", L"错误", MB_OK | MB_ICONERROR);
                }
            }
            return TRUE;
        }

        case ID_BTN_ADD_ESD: {
            SetStatusText(L"添加 eSD 启动项...");
            WCHAR esdPath[MAX_PATH] = {0};
            if (EsdSelectFileDialog(hWnd, esdPath, MAX_PATH)) {
                WCHAR name[128] = L"eSD 启动项";
                WCHAR* fileName = wcsrchr(esdPath, L'\\');
                if (fileName) {
                    fileName++;
                    wcsncpy(name, fileName, 127);
                    WCHAR* dot = wcsrchr(name, L'.');
                    if (dot) *dot = L'\0';
                }
                if (EsdAddBootEntry(name, esdPath, L"1")) {
                    WCHAR msg[MAX_PATH + 64];
                    swprintf(msg, MAX_PATH + 64, L"已添加 eSD 启动项: %s", name);
                    SetStatusText(msg);
                    RefreshBCDList();
                } else {
                    MessageBoxW(hWnd, L"添加 eSD 启动项失败", L"错误", MB_OK | MB_ICONERROR);
                }
            }
            return TRUE;
        }

        case ID_BTN_ADD_ISO: {
            SetStatusText(L"添加 ISO 启动项...");
            WCHAR isoPath[MAX_PATH] = {0};
            if (IsoSelectFileDialog(hWnd, isoPath, MAX_PATH)) {
                WCHAR name[128] = L"ISO 启动项";
                WCHAR* fileName = wcsrchr(isoPath, L'\\');
                if (fileName) {
                    fileName++;
                    wcsncpy(name, fileName, 127);
                    WCHAR* dot = wcsrchr(name, L'.');
                    if (dot) *dot = L'\0';
                }
                if (IsoAddBootEntry(name, isoPath)) {
                    WCHAR msg[MAX_PATH + 64];
                    swprintf(msg, MAX_PATH + 64, L"已添加 ISO 启动项: %s", name);
                    SetStatusText(msg);
                    RefreshBCDList();
                } else {
                    MessageBoxW(hWnd, 
                        L"添加 ISO 启动项失败\n\n"
                        L"ISO 启动需要:\n"
                        L"1. 已安装 Limine 引导器\n"
                        L"2. ESP 分区可访问", 
                        L"错误", MB_OK | MB_ICONERROR);
                }
            }
            return TRUE;
        }

        case ID_BTN_DELETE:
            SetStatusText(L"删除启动项...");
            // TODO: 删除功能
            return TRUE;

        case ID_BTN_UP:
        case ID_BTN_DOWN:
            SetStatusText(L"调整顺序...");
            // TODO: 上移下移
            return TRUE;

        case ID_BTN_SET_DEFAULT:
            SetStatusText(L"设置默认启动项...");
            // TODO: 设为默认
            return TRUE;

        case ID_BTN_PBR_BACKUP:
            SetStatusText(L"备份 PBR...");
            // TODO: PBR 备份
            MessageBoxW(hWnd, L"PBR 备份功能待集成", L"提示", MB_OK | MB_ICONINFORMATION);
            return TRUE;

        case ID_BTN_PBR_RESTORE:
            SetStatusText(L"恢复 PBR...");
            // TODO: PBR 恢复
            MessageBoxW(hWnd, L"PBR 恢复功能待集成", L"提示", MB_OK | MB_ICONINFORMATION);
            return TRUE;

        case ID_BTN_SET_ACTIVE:
            SetStatusText(L"设置活动分区...");
            // TODO: 设置活动分区
            MessageBoxW(hWnd, L"设置活动分区功能待集成", L"提示", MB_OK | MB_ICONINFORMATION);
            return TRUE;
    }

    return FALSE;
}