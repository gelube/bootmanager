/**
 * bcd_page.c - BCD 启动菜单管理页面
 * 
 * 显示详细的启动项信息，类似 BOOTICE 风格
 */

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <wchar.h>
#include <stdio.h>
#include "../../../include/bcd_page.h"
#include "../../../include/bcd_manager.h"
#include "../../../include/boot_mode.h"
#include "../../core/wimboot.h"

enum {
    ID_LIST_BCD = 2700,
    ID_BTN_REFRESH,
    ID_BTN_DELETE,
    ID_BTN_SET_DEFAULT,
    ID_BTN_ADD,
    ID_BTN_MBR_MODE,
    ID_STATUS_TEXT
};

#define ID_MENU_WIM     2801
#define ID_MENU_VHD     2802
#define ID_MENU_RAM     2803
#define ID_MENU_WINPE   2804
#define ID_MENU_ESD     2805
#define ID_MENU_ISO     2806

static HWND s_hList = NULL;
static HWND s_hStatus = NULL;
static HWND s_hBtnMBR = NULL;
static BCD_LIST s_bcdList = {0};
static BOOL s_isUEFI = TRUE;

static void SetStatus(const WCHAR* text) {
    if (s_hStatus) SetWindowTextW(s_hStatus, text);
}

// 从路径提取盘符
static WCHAR GetDriveFromPath(const WCHAR* path) {
    if (!path) return 0;
    if (path[0] >= L'A' && path[0] <= L'Z' && path[1] == L':') {
        return path[0];
    }
    if (path[0] >= L'a' && path[0] <= L'z' && path[1] == L':') {
        return path[0] - 32;  // 转大写
    }
    return 0;
}

// 识别操作系统类型
static const WCHAR* DetectOSType(const WCHAR* name, const WCHAR* path) {
    if (!path) path = L"";
    if (!name) name = L"";
    
    // 从名称判断
    if (wcsstr(name, L"Windows")) return L"Windows";
    if (wcsstr(name, L"ubuntu") || wcsstr(name, L"Ubuntu")) return L"Ubuntu";
    if (wcsstr(name, L"Fedora")) return L"Fedora";
    if (wcsstr(name, L"Debian")) return L"Debian";
    if (wcsstr(name, L"Arch")) return L"Arch Linux";
    if (wcsstr(name, L"Manjaro")) return L"Manjaro";
    if (wcsstr(name, L"openSUSE")) return L"openSUSE";
    if (wcsstr(name, L"CentOS")) return L"CentOS";
    if (wcsstr(name, L"Red Hat")) return L"RHEL";
    if (wcsstr(name, L"macOS") || wcsstr(name, L"OS X")) return L"macOS";
    if (wcsstr(name, L"WinPE") || wcsstr(name, L"PE")) return L"WinPE";
    
    // 从路径判断
    if (wcsstr(path, L"\\Windows\\") || wcsstr(path, L"winload")) return L"Windows";
    if (wcsstr(path, L"\\EFI\\Microsoft\\")) return L"Windows Boot Manager";
    if (wcsstr(path, L"\\EFI\\refind\\")) return L"rEFInd";
    if (wcsstr(path, L"\\EFI\\BOOT\\BOOTX64")) return L"UEFI 默认引导";
    if (wcsstr(path, L"\\EFI\\ubuntu")) return L"Ubuntu";
    if (wcsstr(path, L"\\EFI\\fedora")) return L"Fedora";
    if (wcsstr(path, L"\\EFI\\debian")) return L"Debian";
    if (wcsstr(path, L"\\EFI\\arch")) return L"Arch Linux";
    if (wcsstr(path, L"\\EFI\\manjaro")) return L"Manjaro";
    if (wcsstr(path, L"\\EFI\\opensuse")) return L"openSUSE";
    if (wcsstr(path, L"\\EFI\\centos")) return L"CentOS";
    if (wcsstr(path, L"\\EFI\\grub") || wcsstr(path, L"grubx64")) return L"GRUB/Linux";
    if (wcsstr(path, L"\\EFI\\clover")) return L"Clover (macOS)";
    if (wcsstr(path, L"\\EFI\\opencore")) return L"OpenCore (macOS)";
    if (wcsstr(path, L"memtest") || wcsstr(path, L"memdiag")) return L"内存测试";
    if (wcsstr(path, L".wim") || wcsstr(path, L"WIM")) return L"WIM 映像";
    if (wcsstr(path, L".vhd") || wcsstr(path, L".vhdx")) return L"VHD 系统";
    
    return L"未知";
}

// 是否为系统保留条目（不过滤，全部显示）
static BOOL IsSystemEntry(const WCHAR* guidStr) {
    // 不过滤任何条目，全部显示给用户
    return FALSE;
}

void BcdPageRefresh(void) {
    if (!s_hList) return;
    
    ListView_DeleteAllItems(s_hList);
    BCD_FreeList(&s_bcdList);
    
    WCHAR error[256] = {0};
    if (!BCD_GetEntries(&s_bcdList, error, 256)) {
        LVITEM lvi = {0};
        lvi.mask = LVIF_TEXT;
        // 显示具体错误原因
        WCHAR errMsg[300];
        if (error[0]) {
            swprintf(errMsg, 300, L"错误: %s", error);
        } else {
            wcscpy(errMsg, L"加载失败，请以管理员身份运行");
        }
        lvi.pszText = errMsg;
        lvi.iItem = 0;
        ListView_InsertItem(s_hList, &lvi);
        SetStatus(L"✗ 加载失败");
        return;
    }
    
    int idx = 0;
    for (int i = 0; i < s_bcdList.count; i++) {
        BCD_ENTRY* entry = &s_bcdList.entries[i];
        
        // 使用原始标识符字符串
        WCHAR* guidStr = entry->idStr[0] ? entry->idStr : L"{unknown}";
        
        // 过滤系统保留条目
        if (IsSystemEntry(guidStr)) continue;
        
        // 名称
        WCHAR displayName[256];
        if (entry->name[0]) {
            wcsncpy(displayName, entry->name, 255);
        } else {
            wcscpy(displayName, L"(无名称)");
        }
        
        // 系统类型
        const WCHAR* osType = DetectOSType(entry->name, entry->path);
        
        // 盘符
        WCHAR driveLetter[4] = L"-";
        WCHAR drv = GetDriveFromPath(entry->path);
        if (drv) {
            swprintf(driveLetter, 4, L"%c:", drv);
        }
        
        // 路径（简化）
        WCHAR displayPath[MAX_PATH] = L"-";
        if (entry->path[0]) {
            // 提取关键路径
            WCHAR* efi = wcsstr(entry->path, L"\\EFI\\");
            if (efi) {
                wcscpy(displayPath, efi);
            } else if (wcsstr(entry->path, L"\\Windows\\")) {
                WCHAR* win = wcsstr(entry->path, L"\\Windows\\");
                wcscpy(displayPath, win);
            } else if (wcslen(entry->path) > 60) {
                wcscpy(displayPath, L"...");
                wcscat(displayPath, entry->path + wcslen(entry->path) - 56);
            } else {
                wcscpy(displayPath, entry->path);
            }
        }
        
        // GUID（简短显示）
        WCHAR shortGuid[20];
        wcsncpy(shortGuid, guidStr, 19);
        shortGuid[19] = 0;
        
        LVITEM lvi = {0};
        lvi.mask = LVIF_TEXT;
        lvi.iItem = idx;
        
        WCHAR num[8];
        swprintf(num, 8, L"%d", idx + 1);
        lvi.pszText = num;
        ListView_InsertItem(s_hList, &lvi);
        ListView_SetItemText(s_hList, idx, 1, displayName);
        ListView_SetItemText(s_hList, idx, 2, (LPWSTR)osType);
        ListView_SetItemText(s_hList, idx, 3, driveLetter);
        ListView_SetItemText(s_hList, idx, 4, displayPath);
        ListView_SetItemText(s_hList, idx, 5, shortGuid);
        
        idx++;
    }
    
    if (idx == 0) {
        LVITEM lvi = {0};
        lvi.mask = LVIF_TEXT;
        lvi.pszText = L"-";
        lvi.iItem = 0;
        ListView_InsertItem(s_hList, &lvi);
        ListView_SetItemText(s_hList, 0, 1, L"(无启动项)");
        SetStatus(L"无启动项");
    } else {
        WCHAR status[64];
        swprintf(status, 64, L"共 %d 个启动项", idx);
        SetStatus(status);
    }
}

void BcdPageBuild(HWND hParent, HFONT fontTitle, HFONT fontBody, HFONT fontSmall) {
    s_hList = NULL;
    s_hStatus = NULL;
    s_hBtnMBR = NULL;
    BCD_InitList(&s_bcdList);
    s_isUEFI = BootMode_IsUEFIFirmware();
    
    RECT rc;
    GetClientRect(hParent, &rc);
    int w = rc.right - 32;
    int h = rc.bottom;
    int y = 32;
    
    // 标题
    WCHAR title[64];
    swprintf(title, 64, L"BCD 启动菜单 (%s)", s_isUEFI ? L"UEFI" : L"MBR");
    
    HWND hTitle = CreateWindowExW(0, L"STATIC", title,
        WS_CHILD | WS_VISIBLE, 32, y, w, 28, hParent, NULL, NULL, NULL);
    SendMessageW(hTitle, WM_SETFONT, (WPARAM)fontTitle, TRUE);
    y += 38;
    
    // 列表 - 减少高度，给按钮留空间
    s_hList = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEW, NULL,
        WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL,
        32, y, w, h - y - 100, hParent, (HMENU)ID_LIST_BCD, NULL, NULL);
    ListView_SetExtendedListViewStyle(s_hList, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
    
    LVCOLUMN lvc = {0};
    lvc.mask = LVCF_TEXT | LVCF_WIDTH;
    
    lvc.pszText = L"#";
    lvc.cx = 35;
    ListView_InsertColumn(s_hList, 0, &lvc);
    
    lvc.pszText = L"名称";
    lvc.cx = 180;
    ListView_InsertColumn(s_hList, 1, &lvc);
    
    lvc.pszText = L"系统";
    lvc.cx = 80;
    ListView_InsertColumn(s_hList, 2, &lvc);
    
    lvc.pszText = L"盘符";
    lvc.cx = 45;
    ListView_InsertColumn(s_hList, 3, &lvc);
    
    lvc.pszText = L"路径";
    lvc.cx = w - 380;
    ListView_InsertColumn(s_hList, 4, &lvc);
    
    lvc.pszText = L"GUID";
    lvc.cx = 140;
    ListView_InsertColumn(s_hList, 5, &lvc);
    
    // 按钮行 - 固定在底部
    int bx = 32;
    int by = h - 58;
    
    #define BTN(id, text, wd) \
        CreateWindowExW(0, L"BUTTON", text, WS_CHILD | WS_VISIBLE, bx, by, wd, 26, hParent, (HMENU)id, NULL, NULL); \
        SendMessageW(GetDlgItem(hParent, id), WM_SETFONT, (WPARAM)fontSmall, TRUE); \
        bx += wd + 6;
    
    BTN(ID_BTN_REFRESH, L"刷新", 50);
    BTN(ID_BTN_DELETE, L"删除", 50);
    BTN(ID_BTN_SET_DEFAULT, L"默认", 50);
    BTN(ID_BTN_ADD, L"▼添加", 60);
    
    #undef BTN
    
    // 状态栏
    s_hStatus = CreateWindowExW(0, L"STATIC", L"加载中...",
        WS_CHILD | WS_VISIBLE, 32, h - 28, w, 24, hParent, (HMENU)ID_STATUS_TEXT, NULL, NULL);
    SendMessageW(s_hStatus, WM_SETFONT, (WPARAM)fontSmall, TRUE);
    
    BcdPageRefresh();
}

static int GetSelectedIndex(void) {
    if (!s_hList) return -1;
    return ListView_GetNextItem(s_hList, -1, LVNI_SELECTED);
}

static BCD_ENTRY* GetActualEntry(int displayIdx) {
    int actual = 0;
    for (int i = 0; i < s_bcdList.count; i++) {
        WCHAR guidStr[64];
        GUID_ToString(&s_bcdList.entries[i].id, guidStr, 64);
        if (IsSystemEntry(guidStr)) continue;
        
        if (actual == displayIdx) return &s_bcdList.entries[i];
        actual++;
    }
    return NULL;
}

void BcdPageCommand(HWND hWnd, WPARAM wParam) {
    switch (LOWORD(wParam)) {
        case ID_BTN_REFRESH:
            BcdPageRefresh();
            break;
        
        case ID_BTN_ADD: {
            POINT pt;
            GetCursorPos(&pt);
            
            HMENU hMenu = CreatePopupMenu();
            AppendMenuW(hMenu, MF_STRING, ID_MENU_WIM, L"WIM 映像");
            AppendMenuW(hMenu, MF_STRING, ID_MENU_VHD, L"VHD 虚拟盘");
            AppendMenuW(hMenu, MF_STRING, ID_MENU_RAM, L"RAM 启动");
            AppendMenuW(hMenu, MF_STRING, ID_MENU_WINPE, L"WinPE");
            AppendMenuW(hMenu, MF_STRING, ID_MENU_ESD, L"eSD");
            AppendMenuW(hMenu, MF_STRING, ID_MENU_ISO, L"ISO 镜像");
            
            TrackPopupMenu(hMenu, TPM_LEFTALIGN | TPM_TOPALIGN, pt.x, pt.y, 0, hWnd, NULL);
            DestroyMenu(hMenu);
            break;
        }
        
        case ID_BTN_DELETE: {
            int sel = GetSelectedIndex();
            if (sel < 0) { MessageBoxW(hWnd, L"请先选择", L"提示", MB_OK); break; }
            
            BCD_ENTRY* entry = GetActualEntry(sel);
            if (!entry) break;
            
            WCHAR msg[512];
            swprintf(msg, 512, L"确定删除？\n\n%s", entry->name);
            if (MessageBoxW(hWnd, msg, L"确认", MB_YESNO) != IDYES) break;
            
            WCHAR error[256] = {0};
            if (BCD_DeleteEntry(entry->idStr, error, 256)) {
                MessageBoxW(hWnd, L"已删除", L"完成", MB_OK);
                BcdPageRefresh();
            } else {
                MessageBoxW(hWnd, error[0] ? error : L"删除失败", L"错误", MB_OK | MB_ICONERROR);
            }
            break;
        }
        
        case ID_BTN_SET_DEFAULT: {
            int sel = GetSelectedIndex();
            if (sel < 0) { MessageBoxW(hWnd, L"请先选择", L"提示", MB_OK); break; }
            
            BCD_ENTRY* entry = GetActualEntry(sel);
            if (!entry) break;
            
            WCHAR error[256] = {0};
            if (BCD_SetDefault(entry->idStr, error, 256)) {
                MessageBoxW(hWnd, L"已设为默认", L"完成", MB_OK);
            } else {
                MessageBoxW(hWnd, L"设置失败", L"错误", MB_OK | MB_ICONERROR);
            }
            break;
        }
        
        case ID_BTN_MBR_MODE:
            MessageBoxW(hWnd, 
                L"MBR 引导模式功能\n\n"
                L"当前系统为 UEFI 模式，此功能不可用。\n\n"
                L"在 MBR/BIOS 系统上，此功能用于管理传统启动项。",
                L"提示", MB_OK | MB_ICONINFORMATION);
            break;
        
        case ID_MENU_WIM: {
            WCHAR path[MAX_PATH] = {0};
            if (!WimSelectFileDialog(hWnd, path, MAX_PATH)) break;
            WCHAR name[128]; wcscpy(name, L"WIM");
            WCHAR* fn = wcsrchr(path, L'\\');
            if (fn) { wcsncpy(name, fn + 1, 127); WCHAR* dot = wcsrchr(name, L'.'); if (dot) *dot = 0; }
            SetStatus(L"添加中...");
            if (WimAddBootEntry(name, path, L"1")) {
                MessageBoxW(hWnd, L"添加成功", L"完成", MB_OK);
                BcdPageRefresh();
            } else {
                MessageBoxW(hWnd, L"添加失败\n\n请以管理员身份运行", L"错误", MB_OK | MB_ICONERROR);
            }
            break;
        }
        
        case ID_MENU_VHD: {
            WCHAR path[MAX_PATH] = {0};
            if (!VhdSelectFileDialog(hWnd, path, MAX_PATH)) break;
            WCHAR name[128]; wcscpy(name, L"VHD");
            WCHAR* fn = wcsrchr(path, L'\\');
            if (fn) { wcsncpy(name, fn + 1, 127); WCHAR* dot = wcsrchr(name, L'.'); if (dot) *dot = 0; }
            SetStatus(L"添加中...");
            if (VhdAddBootEntry(name, path)) {
                MessageBoxW(hWnd, L"添加成功", L"完成", MB_OK);
                BcdPageRefresh();
            } else {
                MessageBoxW(hWnd, L"添加失败", L"错误", MB_OK | MB_ICONERROR);
            }
            break;
        }
        
        case ID_MENU_RAM: {
            WCHAR path[MAX_PATH] = {0};
            if (!RamSelectFileDialog(hWnd, path, MAX_PATH)) break;
            WCHAR name[128]; wcscpy(name, L"RAM");
            WCHAR* fn = wcsrchr(path, L'\\');
            if (fn) { wcsncpy(name, fn + 1, 127); WCHAR* dot = wcsrchr(name, L'.'); if (dot) *dot = 0; }
            SetStatus(L"添加中...");
            if (RamAddBootEntry(name, path, NULL)) {
                MessageBoxW(hWnd, L"添加成功", L"完成", MB_OK);
                BcdPageRefresh();
            } else {
                MessageBoxW(hWnd, L"添加失败\n可能缺少 boot.sdi 文件", L"错误", MB_OK | MB_ICONERROR);
            }
            break;
        }
        
        case ID_MENU_WINPE: {
            WCHAR path[MAX_PATH] = {0};
            if (!WinPeSelectFileDialog(hWnd, path, MAX_PATH)) break;
            WCHAR name[128]; wcscpy(name, L"WinPE");
            WCHAR* fn = wcsrchr(path, L'\\');
            if (fn) { wcsncpy(name, fn + 1, 127); WCHAR* dot = wcsrchr(name, L'.'); if (dot) *dot = 0; }
            SetStatus(L"添加中...");
            if (WinPeAddBootEntry(name, path)) {
                MessageBoxW(hWnd, L"添加成功", L"完成", MB_OK);
                BcdPageRefresh();
            } else {
                MessageBoxW(hWnd, L"添加失败", L"错误", MB_OK | MB_ICONERROR);
            }
            break;
        }
        
        case ID_MENU_ESD: {
            WCHAR path[MAX_PATH] = {0};
            if (!EsdSelectFileDialog(hWnd, path, MAX_PATH)) break;
            WCHAR name[128]; wcscpy(name, L"eSD");
            WCHAR* fn = wcsrchr(path, L'\\');
            if (fn) { wcsncpy(name, fn + 1, 127); WCHAR* dot = wcsrchr(name, L'.'); if (dot) *dot = 0; }
            SetStatus(L"添加中...");
            if (EsdAddBootEntry(name, path, L"1")) {
                MessageBoxW(hWnd, L"添加成功", L"完成", MB_OK);
                BcdPageRefresh();
            } else {
                MessageBoxW(hWnd, L"添加失败", L"错误", MB_OK | MB_ICONERROR);
            }
            break;
        }
        
        case ID_MENU_ISO: {
            WCHAR path[MAX_PATH] = {0};
            if (!IsoSelectFileDialog(hWnd, path, MAX_PATH)) break;
            WCHAR name[128]; wcscpy(name, L"ISO");
            WCHAR* fn = wcsrchr(path, L'\\');
            if (fn) { wcsncpy(name, fn + 1, 127); WCHAR* dot = wcsrchr(name, L'.'); if (dot) *dot = 0; }
            SetStatus(L"添加中...");
            if (IsoAddBootEntry(name, path)) {
                MessageBoxW(hWnd, L"添加成功", L"完成", MB_OK);
                BcdPageRefresh();
            } else {
                MessageBoxW(hWnd, L"添加失败\nISO 启动需要 Limine 支持", L"错误", MB_OK | MB_ICONERROR);
            }
            break;
        }
    }
}