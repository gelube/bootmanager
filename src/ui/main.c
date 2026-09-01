/**
 * Boot Manager Pro v3 - Main UI
 */

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shlobj.h>
#include <wchar.h>
#include <stdio.h>
#include "../core/uefi.h"
#include "../core/refind.h"
#include "limine.h"
#include "../core/backup.h"
#include "../../include/esp.h"
#include "../../include/boot_mode.h"
#include "../../include/mbr_manager.h"
#include "home.h"

// 颜色 - 现代浅色主题 (统一风格)
#define COLOR_BG_MAIN           RGB(240, 253, 250)   // #F0FDFA 浅青背景
#define COLOR_SIDEBAR           RGB(255, 255, 255)   // 纯白侧边栏
#define COLOR_CONTENT           RGB(248, 250, 252)   // 内容区浅灰
#define COLOR_CARD              RGB(255, 255, 255)   // 卡片白色
#define COLOR_CARD_BORDER       RGB(226, 232, 240)   // 卡片边框
#define COLOR_TEXT_PRIMARY      RGB(15, 23, 42)      // #0F172A 深色文字
#define COLOR_TEXT_SECONDARY    RGB(100, 116, 139)   // #64748B 次要文字
#define COLOR_ACCENT            RGB(6, 182, 212)     // #06B6D4 主色调青色
#define COLOR_ACCENT_HOVER      RGB(14, 165, 233)    // #0EA5E9 悬停色
#define COLOR_ACCENT_LIGHT      RGB(236, 254, 255)   // #ECFEFF 浅青背景
#define COLOR_CTA               RGB(6, 182, 212)     // CTA 按钮
#define COLOR_CTA_HOVER         RGB(2, 132, 199)     // CTA 悬停
#define COLOR_SUCCESS           RGB(34, 197, 94)     // 成功绿
#define COLOR_WARNING           RGB(251, 146, 60)    // 警告橙
#define COLOR_ERROR             RGB(239, 68, 68)     // 错误红
#define COLOR_BORDER            RGB(226, 232, 240)   // 边框颜色
#define COLOR_LIST_HEADER       RGB(241, 245, 249)   // 列表头背景
#define COLOR_LIST_ALT          RGB(248, 250, 252)   // 列表交替行

// 尺寸
#define WINDOW_WIDTH        1100
#define WINDOW_HEIGHT       780
#define SIDEBAR_WIDTH       240
#define NAV_ITEM_HEIGHT     52
#define BTN_HEIGHT          40
#define BTN_WIDTH           100
#define TAB_HEIGHT          32
#define CONTENT_PADDING     24
// Unified page layout constants
#define PAGE_TITLE_H        28
#define PAGE_TITLE_GAP      8
#define PAGE_SECTION_GAP    35
#define PAGE_DESC_H         20
#define PAGE_BTN_H          36
#define PAGE_BTN_GAP        8
#define PAGE_STATUS_H       18
#define PAGE_FOOTER_H       62  // btn(36) + gap(8) + status(18)

// 控件 ID
enum {
    ID_NAV_BOOT_MGR = 100,
    ID_NAV_THIRD_PARTY,
    ID_NAV_BACKUP_RESTORE,
    ID_NAV_ABOUT,
    
    ID_TAB_THIRD_PARTY = 151,
    
    ID_LIST_BOOT = 200,
    ID_BTN_REFRESH, ID_BTN_ADD_ENTRY, ID_BTN_DELETE_ENTRY,
    ID_BTN_MOVE_UP, ID_BTN_MOVE_DOWN, ID_BTN_SET_DEFAULT,
    
    ID_BTN_INSTALL = 300, ID_BTN_UNINSTALL,
    ID_BTN_INSTALL_LIMINE = 310, ID_BTN_UNINSTALL_LIMINE,
    
    ID_BTN_BACKUP_MBR = 400, ID_BTN_RESTORE,
    ID_BTN_MBR_REPAIR = 450,
    ID_BTN_RESTORE_MBR = 460,
    ID_BTN_UEFI_REPAIR,
    ID_STATUS_TEXT = 500,
    
    // Limine 启动项管理按钮
    ID_BTN_LIMINE_ADD = 600,
    ID_BTN_LIMINE_EDIT,
    ID_BTN_LIMINE_DELETE,
    ID_BTN_LIMINE_REFRESH,
};

// Async refresh messages (WM_APP-based, posted after page controls are created)
#define WM_APP_REFRESH_REFIND  (WM_APP + 1)
#define WM_APP_REFRESH_LIMINE  (WM_APP + 2)

// 全局变量
static HWND g_hMainWnd = NULL, g_hContent = NULL, g_hListView = NULL, g_hStatusText = NULL;
static HWND g_hPages[4] = {NULL, NULL, NULL, NULL};  // Page container windows
static HWND g_hRefindPanel = NULL, g_hLiminePanel = NULL;  // Tab panels in third-party page
static int g_currentPage = 0;
static int g_currentThirdPartyTab = 0;
static HFONT g_fontTitle = NULL, g_fontBody = NULL, g_fontSmall = NULL;
static HWND g_navItems[4] = {0};
static int g_navHoverIndex = -1;  // 悬停的导航项索引
static int g_navPressedIndex = -1;  // 按下的导航项索引
static UEFI_BOOT_LIST* g_bootList = NULL;
static BOOL g_isUEFI = -1;  // -1=unchecked, 0=Legacy, 1=UEFI (cached)

// Cached UEFI check - avoids repeated NVRAM reads on every page switch
static BOOL IsUEFICached(void) {
    if (g_isUEFI == -1) {
        g_isUEFI = BootMode_IsUEFIFirmware();
    }
    return g_isUEFI;
}

// 导航按钮窗口过程
static LRESULT CALLBACK NavButtonProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    static HBRUSH hBrushNormal = NULL;
    static HBRUSH hBrushHover = NULL;
    static HBRUSH hBrushSelected = NULL;
    
    switch (msg) {
        case WM_CREATE: {
            // 创建画刷
            if (!hBrushNormal) hBrushNormal = CreateSolidBrush(COLOR_SIDEBAR);
            if (!hBrushHover) hBrushHover = CreateSolidBrush(COLOR_ACCENT_LIGHT);
            if (!hBrushSelected) hBrushSelected = CreateSolidBrush(COLOR_ACCENT_LIGHT);
            return 0;
        }
        
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hWnd, &ps);
            RECT rc;
            GetClientRect(hWnd, &rc);
            
            // 获取按钮状态
            int index = -1;
            for (int i = 0; i < 4; i++) {
                if (g_navItems[i] == hWnd) { index = i; break; }
            }
            
            BOOL isSelected = (index == g_currentPage);
            BOOL isHover = (index == g_navHoverIndex);
            
            // 绘制背景
            if (isSelected) {
                FillRect(hdc, &rc, hBrushSelected);
            } else if (isHover) {
                FillRect(hdc, &rc, hBrushHover);
            } else {
                FillRect(hdc, &rc, hBrushNormal);
            }
            
            // 绘制左边框（选中时显示主题色）
            if (isSelected) {
                HPEN hPen = CreatePen(PS_SOLID, 3, COLOR_ACCENT);
                HPEN hOldPen = SelectObject(hdc, hPen);
                MoveToEx(hdc, 0, 10, NULL);
                LineTo(hdc, 0, rc.bottom - 10);
                SelectObject(hdc, hOldPen);
                DeleteObject(hPen);
            }
            
            // 绘制文字
            WCHAR text[64];
            GetWindowTextW(hWnd, text, 64);
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, isSelected ? COLOR_ACCENT : COLOR_TEXT_PRIMARY);
            HFONT hFont = (HFONT)SendMessageW(hWnd, WM_GETFONT, 0, 0);
            HFONT hOldFont = SelectObject(hdc, hFont);
            
            // 居中绘制文字
            RECT textRc = rc;
            textRc.left += 16;  // 左边距
            DrawTextW(hdc, text, -1, &textRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            
            SelectObject(hdc, hOldFont);
            EndPaint(hWnd, &ps);
            return 0;
        }
        
        case WM_MOUSEMOVE: {
            int index = -1;
            for (int i = 0; i < 4; i++) {
                if (g_navItems[i] == hWnd) { index = i; break; }
            }
            if (index != g_navHoverIndex) {
                int oldHover = g_navHoverIndex;
                g_navHoverIndex = index;
                if (oldHover >= 0 && oldHover < 4 && g_navItems[oldHover]) {
                    InvalidateRect(g_navItems[oldHover], NULL, FALSE);
                }
                InvalidateRect(hWnd, NULL, FALSE);
            }
            
            // 追踪鼠标离开
            TRACKMOUSEEVENT tme = {0};
            tme.cbSize = sizeof(tme);
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = hWnd;
            TrackMouseEvent(&tme);
            return 0;
        }
        
        case WM_MOUSELEAVE: {
            int index = -1;
            for (int i = 0; i < 4; i++) {
                if (g_navItems[i] == hWnd) { index = i; break; }
            }
            if (index == g_navHoverIndex) {
                g_navHoverIndex = -1;
                InvalidateRect(hWnd, NULL, FALSE);
            }
            return 0;
        }
        
        case WM_LBUTTONDOWN: {
            g_navPressedIndex = -1;
            for (int i = 0; i < 4; i++) {
                if (g_navItems[i] == hWnd) { g_navPressedIndex = i; break; }
            }
            return 0;
        }
        
        case WM_LBUTTONUP: {
            // 触发父窗口的导航切换
            int index = -1;
            for (int i = 0; i < 4; i++) {
                if (g_navItems[i] == hWnd) { index = i; break; }
            }
            if (index >= 0 && index == g_navPressedIndex) {
                SendMessageW(GetParent(hWnd), WM_COMMAND, ID_NAV_BOOT_MGR + index, 0);
            }
            g_navPressedIndex = -1;
            return 0;
        }
    }
    
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

// 注册导航按钮类
static void RegisterNavButtonClass(void)
{
    static BOOL registered = FALSE;
    if (registered) return;
    
    WNDCLASSW wc = {0};
    wc.lpfnWndProc = NavButtonProc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.hCursor = LoadCursorW(NULL, IDC_HAND);  // 手型光标
    wc.hbrBackground = (HBRUSH)COLOR_WINDOW;
    wc.lpszClassName = L"NavButtonClass";
    RegisterClassW(&wc);
    registered = TRUE;
}

// ============================================
// 扁平按钮类 - 内容区域使用，风格与导航一致
// ============================================

static LRESULT CALLBACK FlatBtnProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
        case WM_CREATE: {
            // 初始化状态
            SetPropW(hWnd, L"Hover", NULL);
            SetPropW(hWnd, L"Pressed", NULL);
            return 0;
        }
        
        case WM_DESTROY: {
            RemovePropW(hWnd, L"Hover");
            RemovePropW(hWnd, L"Pressed");
            RemovePropW(hWnd, L"IsPrimary");
            return 0;
        }
        
        case WM_ENABLE: {
            InvalidateRect(hWnd, NULL, FALSE);
            return 0;
        }
        
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hWnd, &ps);
            RECT rc;
            GetClientRect(hWnd, &rc);
            
            BOOL isDisabled = !IsWindowEnabled(hWnd);
            BOOL isHover = (GetPropW(hWnd, L"Hover") != NULL);
            BOOL isPressed = (GetPropW(hWnd, L"Pressed") != NULL);
            BOOL isPrimary = (GetPropW(hWnd, L"IsPrimary") != NULL);
            
            // 确定背景颜色 - 与左侧导航风格一致
            HBRUSH hBrush;
            COLORREF textColor;
            
            if (isDisabled) {
                hBrush = CreateSolidBrush(COLOR_CARD);
                textColor = COLOR_TEXT_SECONDARY;
            } else if (isPrimary) {
                // 主按钮：青色背景
                if (isPressed) {
                    hBrush = CreateSolidBrush(COLOR_ACCENT_HOVER);
                } else if (isHover) {
                    hBrush = CreateSolidBrush(COLOR_ACCENT_HOVER);
                } else {
                    hBrush = CreateSolidBrush(COLOR_ACCENT);
                }
                textColor = RGB(255, 255, 255);
            } else {
                // 次要按钮：与导航项风格一致
                if (isPressed) {
                    hBrush = CreateSolidBrush(COLOR_BORDER);
                    textColor = COLOR_TEXT_PRIMARY;
                } else if (isHover) {
                    hBrush = CreateSolidBrush(COLOR_ACCENT_LIGHT);
                    textColor = COLOR_ACCENT;
                } else {
                    hBrush = CreateSolidBrush(COLOR_CARD);
                    textColor = COLOR_TEXT_PRIMARY;
                }
            }
            
            // 绘制背景（无边框，扁平风格）
            FillRect(hdc, &rc, hBrush);
            DeleteObject(hBrush);  // Prevent GDI leak — CreateSolidBrush must be freed
            
            // 绘制文字
            WCHAR text[128];
            GetWindowTextW(hWnd, text, 128);
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, textColor);
            HFONT hFont = (HFONT)SendMessageW(hWnd, WM_GETFONT, 0, 0);
            HFONT hOldFont = SelectObject(hdc, hFont);
            DrawTextW(hdc, text, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            SelectObject(hdc, hOldFont);
            
            EndPaint(hWnd, &ps);
            return 0;
        }
        
        case WM_MOUSEMOVE: {
            if (!IsWindowEnabled(hWnd)) return 0;
            
            if (!GetPropW(hWnd, L"Hover")) {
                SetPropW(hWnd, L"Hover", (HANDLE)1);
                InvalidateRect(hWnd, NULL, FALSE);
            }
            
            TRACKMOUSEEVENT tme = {0};
            tme.cbSize = sizeof(tme);
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = hWnd;
            TrackMouseEvent(&tme);
            return 0;
        }
        
        case WM_MOUSELEAVE: {
            RemovePropW(hWnd, L"Hover");
            RemovePropW(hWnd, L"Pressed");
            InvalidateRect(hWnd, NULL, FALSE);
            return 0;
        }
        
        case WM_LBUTTONDOWN: {
            if (!IsWindowEnabled(hWnd)) return 0;
            SetPropW(hWnd, L"Pressed", (HANDLE)1);
            InvalidateRect(hWnd, NULL, FALSE);
            SetCapture(hWnd);
            return 0;
        }
        
        case WM_LBUTTONUP: {
            BOOL wasPressed = GetPropW(hWnd, L"Pressed") != NULL;
            RemovePropW(hWnd, L"Pressed");
            ReleaseCapture();
            
            POINT pt;
            pt.x = LOWORD(lParam);
            pt.y = HIWORD(lParam);
            RECT rc;
            GetClientRect(hWnd, &rc);
            
            if (wasPressed && PtInRect(&rc, pt)) {
                SendMessageW(GetParent(hWnd), WM_COMMAND, GetDlgCtrlID(hWnd), 0);
            }
            InvalidateRect(hWnd, NULL, FALSE);
            return 0;
        }
        
        case WM_KEYDOWN: {
            if (wParam == VK_SPACE || wParam == VK_RETURN) {
                if (IsWindowEnabled(hWnd)) {
                    SendMessageW(GetParent(hWnd), WM_COMMAND, GetDlgCtrlID(hWnd), 0);
                }
                return 0;
            }
            break;
        }
    }
    
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

// 注册扁平按钮类
static void RegisterFlatButtonClass(void)
{
    static BOOL registered = FALSE;
    if (registered) return;
    
    WNDCLASSW wc = {0};
    wc.lpfnWndProc = FlatBtnProc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.hCursor = LoadCursorW(NULL, IDC_HAND);
    wc.hbrBackground = (HBRUSH)COLOR_WINDOW;
    wc.lpszClassName = L"FlatBtnClass";
    RegisterClassW(&wc);
    registered = TRUE;
}

// 创建扁平按钮的辅助函数
static HWND CreateFlatButton(HWND hParent, int x, int y, int w, int h, const WCHAR* text, int id, BOOL isPrimary)
{
    HWND hWnd = CreateWindowExW(0, L"FlatBtnClass", text,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        x, y, w, h, hParent, (HMENU)id, NULL, NULL);
    
    if (hWnd) {
        SendMessageW(hWnd, WM_SETFONT, (WPARAM)g_fontBody, TRUE);
        // 设置主要按钮样式
        if (isPrimary) {
            SetPropW(hWnd, L"IsPrimary", (HANDLE)1);
        }
    }
    return hWnd;
}

// 内容容器窗口过程 - 转发消息到主窗口
static LRESULT CALLBACK ContentWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_COMMAND:
        // 转发到主窗口
        SendMessageW(g_hMainWnd, msg, wParam, lParam);
        return 0;
        
    case WM_NOTIFY:
        // 转发到主窗口
        SendMessageW(g_hMainWnd, msg, wParam, lParam);
        return 0;
        
    case WM_CTLCOLORSTATIC:
        return (LRESULT)GetStockObject(WHITE_BRUSH);
    }
    
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

// 注册内容容器窗口类
static void RegisterContentClass(void)
{
    WNDCLASSW wc = {0};
    wc.lpfnWndProc = ContentWndProc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = L"BootManagerContentClass";
    wc.hbrBackground = CreateSolidBrush(COLOR_CONTENT);  // 统一背景色
    RegisterClassW(&wc);
}

// 绘制卡片边框（圆角矩形效果）
static void DrawCard(HDC hdc, RECT* rc, const WCHAR* title)
{
    // 绘制背景
    FillRect(hdc, rc, CreateSolidBrush(COLOR_CARD));
    
    // 绘制边框
    HPEN hPen = CreatePen(PS_SOLID, 1, COLOR_CARD_BORDER);
    HPEN hOldPen = SelectObject(hdc, hPen);
    HBRUSH hOldBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
    Rectangle(hdc, rc->left, rc->top, rc->right, rc->bottom);
    SelectObject(hdc, hOldBrush);
    SelectObject(hdc, hOldPen);
    DeleteObject(hPen);
    
    // 绘制标题背景条
    if (title && title[0]) {
        RECT titleRc = {rc->left + 1, rc->top + 1, rc->right - 1, rc->top + 36};
        FillRect(hdc, &titleRc, CreateSolidBrush(COLOR_ACCENT_LIGHT));
        
        // 绘制标题文字
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, COLOR_TEXT_PRIMARY);
        HFONT hFont = CreateFontW(14, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
        HFONT hOldFont = SelectObject(hdc, hFont);
        titleRc.left += 12;
        titleRc.top += 8;
        DrawTextW(hdc, title, -1, &titleRc, DT_LEFT | DT_TOP | DT_SINGLELINE);
        SelectObject(hdc, hOldFont);
        DeleteObject(hFont);
    }
}

// 函数声明
LRESULT CALLBACK MainWndProc(HWND, UINT, WPARAM, LPARAM);
static void InitFonts(void);
static void CreateNavigation(HWND);
static void CreateContentArea(HWND);
static void SwitchPage(int);
static void SetStatus(const WCHAR*);
static void BuildBootMgrPage(HWND);
static void BuildThirdPartyPage(HWND);
static void BuildBackupRestorePage(HWND);
static void BuildAboutPage(HWND);
static void RefreshBootList(void);
static BOOL ResolveRefindSourcePath(WCHAR*, DWORD);
static BOOL ResolveLimineSourcePath(WCHAR*, DWORD);
static BOOL MountESP(WCHAR*);
static BOOL UnmountESP(WCHAR);
static void ApplyDarkTitleBar(HWND hWnd);  // 应用深色标题栏

// 应用深色标题栏 (Windows 10 1809+)
static void ApplyDarkTitleBar(HWND hWnd)
{
    // DWMWA_USE_IMMERSIVE_DARK_MODE = 20 (Windows 11)
    // DWMWA_USE_IMMERSIVE_DARK_MODE = 19 (Windows 10 2004+)
    typedef HRESULT(WINAPI* DwmSetWindowAttributeFunc)(HWND, DWORD, LPCVOID, DWORD);
    
    HMODULE hDwm = LoadLibraryW(L"dwmapi.dll");
    if (hDwm) {
        DwmSetWindowAttributeFunc pDwmSetWindowAttribute = 
            (DwmSetWindowAttributeFunc)GetProcAddress(hDwm, "DwmSetWindowAttribute");
        if (pDwmSetWindowAttribute) {
            // 尝试 Windows 11 的值
            BOOL value = TRUE;
            HRESULT hr = pDwmSetWindowAttribute(hWnd, 20, &value, sizeof(value));
            if (FAILED(hr)) {
                // 尝试 Windows 10 的值
                pDwmSetWindowAttribute(hWnd, 19, &value, sizeof(value));
            }
        }
        FreeLibrary(hDwm);
    }
}

static void InitFonts(void)
{
    // DPI-aware font sizing
    HDC hdc = GetDC(NULL);
    int dpi = GetDeviceCaps(hdc, LOGPIXELSY);
    ReleaseDC(NULL, hdc);
    int scale = (dpi * 100 + 96 / 2) / 96;  // percentage of 96dpi
    
    g_fontTitle = CreateFontW(MulDiv(22, scale, 100), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
    g_fontBody = CreateFontW(MulDiv(16, scale, 100), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
    g_fontSmall = CreateFontW(MulDiv(13, scale, 100), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
}

static void SetStatus(const WCHAR* text) { if (g_hStatusText) SetWindowTextW(g_hStatusText, text); }

// ESP 挂载追踪
static BOOL s_espMountedByUs = FALSE;
static WCHAR s_espDriveLetter = 0;

static BOOL MountESP(WCHAR* d) { 
    BOOL mountedByUs = FALSE;
    BOOL result = EspMountEx(d, 4, &mountedByUs);
    if (result) {
        s_espMountedByUs = mountedByUs;
        s_espDriveLetter = d[0];
    }
    return result;
}
static BOOL UnmountESP(WCHAR d) {
    WCHAR drive[4] = {d, L':', L'\0', 0};
    // 仅当是我们挂载的才卸载
    BOOL result = EspUnmountEx(drive, TRUE);
    if (result) {
        s_espMountedByUs = FALSE;
        s_espDriveLetter = 0;
    }
    return result;
}

// rEFInd/Limine 页面静态变量和函数
static HWND s_refindBtnInstall = NULL, s_refindBtnUninstall = NULL, s_refindStatus = NULL;
static HWND s_limineBtnInstall = NULL, s_limineBtnUninstall = NULL, s_limineStatus = NULL;

// ============================================
// ESP 分区选择对话框
// 让用户选择要安装到的 ESP 分区
// ============================================
typedef struct {
    WCHAR driveLetter;      // 盘符 (如 'C')
    WCHAR displayName[128]; // 显示名称
    WCHAR volumeLabel[64];  // 卷标
    WCHAR fsName[32];       // 文件系统
    ULONGLONG totalSize;    // 总大小
    DWORD diskNumber;       // 磁盘号
    BOOL isESP;             // 是否是 ESP
} ESP_PARTITION_INFO;

static ESP_PARTITION_INFO* s_espPartitions = NULL;
static int s_espPartitionCount = 0;
static HWND s_espSelDialog = NULL;
static HWND s_espSelCombo = NULL;
static int s_espSelResult = -1;

// 枚举所有 FAT32 分区（可能是 ESP）
static int EnumEspPartitions(ESP_PARTITION_INFO** partitions)
{
    int count = 0;
    int maxPartitions = 32;
    
    *partitions = (ESP_PARTITION_INFO*)calloc(maxPartitions, sizeof(ESP_PARTITION_INFO));
    if (!*partitions) return 0;
    
    // 枚举所有盘符
    WCHAR drives[512] = {0};
    if (GetLogicalDriveStringsW(511, drives)) {
        WCHAR* p = drives;
        while (*p && count < maxPartitions) {
            WCHAR root[4] = {p[0], L':', L'\\', 0};
            
            // 只处理固定磁盘和可移动磁盘
            UINT type = GetDriveTypeW(root);
            if (type != DRIVE_FIXED && type != DRIVE_REMOVABLE) {
                p += wcslen(p) + 1;
                continue;
            }
            
            // 获取文件系统
            WCHAR fsName[32] = {0};
            WCHAR volumeLabel[64] = {0};
            ULONGLONG totalBytes = 0;
            
            GetVolumeInformationW(root, volumeLabel, 64, NULL, NULL, NULL, fsName, 32);
            GetDiskFreeSpaceExW(root, NULL, (PULARGE_INTEGER)&totalBytes, NULL);
            
            // 处理 FAT 系列（FAT12/FAT16/FAT32/exFAT 都可能是 ESP）
            BOOL isFat = (_wcsicmp(fsName, L"FAT32") == 0 || 
                          _wcsicmp(fsName, L"FAT") == 0 ||
                          _wcsicmp(fsName, L"FAT16") == 0 ||
                          _wcsicmp(fsName, L"FAT12") == 0);
            
            if (isFat) {
                ESP_PARTITION_INFO* info = &(*partitions)[count];
                info->driveLetter = p[0];
                info->totalSize = totalBytes;
                wcsncpy(info->fsName, fsName, 31);
                wcsncpy(info->volumeLabel, volumeLabel, 63);
                
                // 检查是否是 ESP（有 EFI 目录）
                WCHAR efiPath[MAX_PATH];
                swprintf(efiPath, MAX_PATH, L"%s\\EFI", root);
                info->isESP = (GetFileAttributesW(efiPath) != INVALID_FILE_ATTRIBUTES);
                
                // 获取磁盘号
                WCHAR volPath[8] = {L'\\', L'\\', L'.', L'\\', p[0], L':', 0};
                HANDLE hVol = CreateFileW(volPath, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
                if (hVol != INVALID_HANDLE_VALUE) {
                    VOLUME_DISK_EXTENTS extents = {0};
                    DWORD bytesReturned = 0;
                    if (DeviceIoControl(hVol, IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS, NULL, 0, &extents, sizeof(extents), &bytesReturned, NULL)) {
                        info->diskNumber = extents.Extents[0].DiskNumber;
                    }
                    CloseHandle(hVol);
                }
                
                // 构建显示名称
                WCHAR sizeStr[32] = {0};
                if (totalBytes >= 1024ULL * 1024 * 1024) {
                    swprintf(sizeStr, 32, L"%.0fGB", (double)totalBytes / (1024.0 * 1024 * 1024));
                } else {
                    swprintf(sizeStr, 32, L"%.0fMB", (double)totalBytes / (1024.0 * 1024));
                }
                
                if (volumeLabel[0]) {
                    swprintf(info->displayName, 128, L"%c: %s (%s, %s)%s", 
                        p[0], volumeLabel, sizeStr, fsName, 
                        info->isESP ? L" [ESP]" : L"");
                } else {
                    swprintf(info->displayName, 128, L"%c: (%s, %s)%s", 
                        p[0], sizeStr, fsName, 
                        info->isESP ? L" [ESP]" : L"");
                }
                
                count++;
            }
            
            p += wcslen(p) + 1;
        }
    }
    
    s_espPartitions = *partitions;
    s_espPartitionCount = count;
    return count;
}

// ESP 选择对话框过程
static LRESULT CALLBACK EspSelDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
        case WM_CREATE: {
            RECT rc;
            GetClientRect(hDlg, &rc);
            int w = rc.right;
            int y = 20;
            HFONT hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
            
            // 提示文字
            CreateWindowExW(0, L"STATIC", L"选择要安装到的 ESP 分区：", 
                WS_CHILD | WS_VISIBLE, 20, y, w - 40, 24, hDlg, NULL, NULL, NULL);
            y += 30;
            
            // 分区列表
            s_espSelCombo = CreateWindowExW(0, L"COMBOBOX", NULL, 
                WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL | CBS_SORT,
                20, y, w - 40, 200, hDlg, NULL, NULL, NULL);
            SendMessageW(s_espSelCombo, WM_SETFONT, (WPARAM)hFont, TRUE);
            
            // 填充分区列表，优先显示 ESP 分区
            int espIdx = -1;
            for (int i = 0; i < s_espPartitionCount; i++) {
                int idx = (int)SendMessageW(s_espSelCombo, CB_ADDSTRING, 0, (LPARAM)s_espPartitions[i].displayName);
                SendMessageW(s_espSelCombo, CB_SETITEMDATA, idx, i);
                if (s_espPartitions[i].isESP && espIdx < 0) {
                    espIdx = idx;
                }
            }
            
            // 默认选中第一个 ESP 分区
            if (espIdx >= 0) {
                SendMessageW(s_espSelCombo, CB_SETCURSEL, espIdx, 0);
            } else if (s_espPartitionCount > 0) {
                SendMessageW(s_espSelCombo, CB_SETCURSEL, 0, 0);
            }
            
            y += 40;
            
            // 提示信息
            CreateWindowExW(0, L"STATIC", 
                L"提示：ESP 分区通常标记为 [ESP]，是 UEFI 启动分区。\n"
                L"如果没有 ESP 分区，请选择一个 FAT32 分区。",
                WS_CHILD | WS_VISIBLE, 20, y, w - 40, 48, hDlg, NULL, NULL, NULL);
            y += 60;
            
            // 按钮
            CreateWindowExW(0, L"BUTTON", L"确定", 
                WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                (w - 180) / 2, y, 80, 28, hDlg, (HMENU)IDOK, NULL, NULL);
            CreateWindowExW(0, L"BUTTON", L"取消", 
                WS_CHILD | WS_VISIBLE,
                (w - 180) / 2 + 100, y, 80, 28, hDlg, (HMENU)IDCANCEL, NULL, NULL);
            
            return 0;
        }
        
        case WM_COMMAND: {
            switch (LOWORD(wParam)) {
                case IDOK: {
                    int sel = (int)SendMessageW(s_espSelCombo, CB_GETCURSEL, 0, 0);
                    if (sel >= 0) {
                        s_espSelResult = (int)SendMessageW(s_espSelCombo, CB_GETITEMDATA, sel, 0);
                    }
                    DestroyWindow(hDlg);
                    return 0;
                }
                case IDCANCEL:
                    s_espSelResult = -1;
                    DestroyWindow(hDlg);
                    return 0;
            }
            break;
        }
        
        case WM_CLOSE:
            s_espSelResult = -1;
            DestroyWindow(hDlg);
            return 0;
    }
    
    return DefWindowProcW(hDlg, msg, wParam, lParam);
}

// 显示 ESP 选择对话框，返回选中的盘符
// 返回 0 表示取消
static WCHAR ShowEspSelectDialog(HWND hParent)
{
    // 先尝试挂载 ESP（可能没有盘符）
    WCHAR espDrive[4] = {0};
    BOOL espMountedNow = FALSE;
    if (!EspFind(espDrive, 4)) {
        BOOL mountedByUs = FALSE;
        espMountedNow = EspMountEx(espDrive, 4, &mountedByUs);
        // If we mounted it just for selection, track it
        // (the install function will mount it again if needed)
    }
    
    // 枚举分区
    ESP_PARTITION_INFO* partitions = NULL;
    int count = EnumEspPartitions(&partitions);
    
    if (count == 0) {
        MessageBoxW(hParent, 
            L"未找到 FAT 格式分区\n\n"
            L"ESP 分区通常是 FAT16 或 FAT32 格式。\n"
            L"请确认 ESP 分区存在且已挂载盘符。\n\n"
            L"可以尝试在磁盘管理中给 ESP 分区分配盘符。", 
            L"提示", MB_OK | MB_ICONWARNING);
        return 0;
    }
    
    if (count == 1) {
        // 只有一个，直接返回
        WCHAR result = partitions[0].driveLetter;
        free(partitions);
        s_espPartitions = NULL;
        s_espPartitionCount = 0;
        return result;
    }
    
    // 注册窗口类
    static BOOL registered = FALSE;
    if (!registered) {
        WNDCLASSEXW wc = {0};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = EspSelDlgProc;
        wc.hInstance = GetModuleHandleW(NULL);
        wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = L"EspSelectDlgClass";
        RegisterClassExW(&wc);
        registered = TRUE;
    }
    
    // 创建对话框
    s_espSelResult = -1;
    HWND hDlg = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_WINDOWEDGE,
        L"EspSelectDlgClass", L"选择 ESP 分区",
        WS_POPUP | WS_CAPTION | WS_SYSMENU,
        0, 0, 400, 220, hParent, NULL, GetModuleHandleW(NULL), NULL);
    
    if (!hDlg) {
        free(partitions);
        return 0;
    }
    
    // 居中
    RECT rcParent, rcDlg;
    GetWindowRect(hParent, &rcParent);
    GetWindowRect(hDlg, &rcDlg);
    int x = rcParent.left + (rcParent.right - rcParent.left - (rcDlg.right - rcDlg.left)) / 2;
    int y = rcParent.top + (rcParent.bottom - rcParent.top - (rcDlg.bottom - rcDlg.top)) / 2;
    SetWindowPos(hDlg, NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
    
    EnableWindow(hParent, FALSE);
    ShowWindow(hDlg, SW_SHOW);
    
    // 消息循环
    MSG msg;
    while (IsWindow(hDlg) && GetMessageW(&msg, NULL, 0, 0)) {
        if (!IsDialogMessageW(hDlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    
    EnableWindow(hParent, TRUE);
    SetForegroundWindow(hParent);
    
    WCHAR result = 0;
    if (s_espSelResult >= 0 && s_espSelResult < s_espPartitionCount) {
        result = s_espPartitions[s_espSelResult].driveLetter;
    }
    
    free(partitions);
    s_espPartitions = NULL;
    s_espPartitionCount = 0;
    
    return result;
}
static HWND s_limineList = NULL;
static HWND s_limineBtnRefresh = NULL;
static HWND s_limineBtnAddEntry = NULL;
static HWND s_limineBtnEditEntry = NULL;
static HWND s_limineBtnDelEntry = NULL;

// Limine 启动项结构
typedef struct {
    WCHAR name[128];
    WCHAR protocol[32];
    WCHAR path[MAX_PATH];
    WCHAR cmdline[512];
} LIMINE_CONF_ENTRY;

static LIMINE_CONF_ENTRY s_limineEntries[64];
static int s_limineEntryCount = 0;
static WCHAR s_limineConfDir[MAX_PATH] = {0};

static void BuildRefindControls(HWND hParent);
static void BuildLimineControls(HWND hParent);
static void RefindRefreshStatus(void);
static void LimineRefreshStatus(void);
static BOOL LoadLimineConfEntries(void);
static BOOL SaveLimineConfEntries(void);
static void RefreshLimineEntryList(void);
static BOOL ShowLimineEditDialog(HWND hParent, LIMINE_CONF_ENTRY* entry, BOOL isAdd);

// 主窗口过程
LRESULT CALLBACK MainWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_CREATE:
        CreateNavigation(hWnd);
        CreateContentArea(hWnd);
        ApplyDarkTitleBar(hWnd);  // 应用深色标题栏
        SwitchPage(0);
        return 0;
        
    case WM_CTLCOLORSTATIC: {
        HDC hdc = (HDC)wParam;
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, COLOR_TEXT_PRIMARY);
        // Use cached brush instead of creating new one every call
        static HBRUSH s_contentBrush = NULL;
        if (!s_contentBrush) s_contentBrush = CreateSolidBrush(COLOR_CONTENT);
        return (LRESULT)s_contentBrush;
    }
    
    case WM_CTLCOLORBTN: {
        // Button background
        HDC hdc = (HDC)wParam;
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, COLOR_TEXT_PRIMARY);
        static HBRUSH s_btnBrush = NULL;
        if (!s_btnBrush) s_btnBrush = CreateSolidBrush(COLOR_CONTENT);
        return (LRESULT)s_btnBrush;
    }
    
    case WM_MOUSEMOVE: {
        // 导航按钮悬停效果
        POINT pt;
        pt.x = LOWORD(lParam);
        pt.y = HIWORD(lParam);
        
        int newHover = -1;
        for (int i = 0; i < 4; i++) {
            RECT rc;
            GetWindowRect(g_navItems[i], &rc);
            ScreenToClient(hWnd, (POINT*)&rc.left);
            ScreenToClient(hWnd, (POINT*)&rc.right);
            if (PtInRect(&rc, pt)) {
                newHover = i;
                break;
            }
        }
        
        if (newHover != g_navHoverIndex) {
            g_navHoverIndex = newHover;
            InvalidateRect(hWnd, NULL, FALSE);
        }
        break;
    }
    
    case WM_MOUSELEAVE: {
        g_navHoverIndex = -1;
        InvalidateRect(hWnd, NULL, FALSE);
        break;
    }
        
    case WM_COMMAND: {
        WORD cmd = LOWORD(wParam);
        
        // 导航
        if (cmd >= ID_NAV_BOOT_MGR && cmd <= ID_NAV_ABOUT) {
            SwitchPage(cmd - ID_NAV_BOOT_MGR);
            return 0;
        }
        
        switch (cmd) {
        // UEFI 按钮
        case ID_BTN_REFRESH: RefreshBootList(); SetStatus(L"✓ 已刷新"); break;
        
        case ID_BTN_ADD_ENTRY: {
            WCHAR title[256] = {0}, path[512] = {0}, driveLetter[4] = {0};
            BOOL hasEsp = FALSE;
            extern BOOL ShowAddEfiDialog(HWND, WCHAR*, WCHAR*, WCHAR*, BOOL*);
            if (!ShowAddEfiDialog(hWnd, title, path, driveLetter, &hasEsp)) break;
            if (wcslen(title) == 0 || wcslen(path) == 0) { MessageBoxW(hWnd, L"请填写完整信息", L"提示", MB_OK); break; }
            
            WCHAR fullPath[512] = {0}, esp[4] = {0}, efiPath[512] = {0};
            BOOL mounted = FALSE;
            BOOL needCopy = FALSE;  // 是否需要复制文件到 ESP
            
            if (path[1] == L':') {
                // 绝对路径 - 检查是否在 ESP 上
                wcscpy(fullPath, path);
                
                // 检查是否在已知 ESP 盘符上
                if (driveLetter[0] && path[0] == driveLetter[0]) {
                    // 在选中的 ESP 分区上
                    swprintf(efiPath, 512, L"\\%s", path + 3);  // 去掉盘符
                } else {
                    // 不在 ESP 上，需要复制
                    needCopy = TRUE;
                }
            } else if (path[0] == L'\\') {
                // 相对路径
                if (driveLetter[0]) {
                    swprintf(fullPath, 512, L"%s%s", driveLetter, path);
                    wcscpy(efiPath, path);
                } else {
                    // 没有选择 ESP 分区，尝试挂载
                    if (!MountESP(esp)) {
                        MessageBoxW(hWnd, L"未检测到 ESP 分区\n\nUEFI 启动项必须位于 ESP 分区上", L"错误", MB_OK | MB_ICONERROR);
                        break;
                    }
                    mounted = TRUE;
                    swprintf(fullPath, 512, L"%s%s", esp, path);
                    wcscpy(efiPath, path);
                }
            } else {
                // 无效路径格式
                MessageBoxW(hWnd, L"路径格式无效\n\n请使用绝对路径（如 C:\\path\\file.efi）或 EFI 相对路径（如 \\EFI\\Boot\\bootx64.efi）", L"错误", MB_OK | MB_ICONERROR);
                break;
            }
            
            if (GetFileAttributesW(fullPath) == INVALID_FILE_ATTRIBUTES) {
                MessageBoxW(hWnd, L"EFI 文件不存在", L"错误", MB_OK | MB_ICONERROR);
                if (mounted) UnmountESP(esp[0]);
                break;
            }
            
            // 如果需要复制到 ESP
            if (needCopy) {
                if (!MountESP(esp)) {
                    MessageBoxW(hWnd, L"未检测到 ESP 分区\n\nUEFI 启动项必须位于 ESP 分区上", L"错误", MB_OK | MB_ICONERROR);
                    break;
                }
                mounted = TRUE;
                
                // 提取文件名（不含扩展名）作为目录名
                const WCHAR* fileName = wcsrchr(fullPath, L'\\');
                if (!fileName) fileName = fullPath;
                else fileName++;
                
                WCHAR dirName[64] = {0};
                wcsncpy(dirName, fileName, 63);
                WCHAR* dot = wcsrchr(dirName, L'.');
                if (dot) *dot = L'\0';
                
                // 构建目标目录和路径
                WCHAR destDir[MAX_PATH], destPath[MAX_PATH];
                swprintf(destDir, MAX_PATH, L"%s\\EFI\\%s", esp, dirName);
                swprintf(destPath, MAX_PATH, L"%s\\%s", destDir, fileName);
                swprintf(efiPath, 512, L"\\EFI\\%s\\%s", dirName, fileName);
                
                // 创建目录（递归创建父目录）
                WCHAR parentDir[MAX_PATH];
                wcscpy(parentDir, destDir);
                WCHAR* p = parentDir + 3;
                while (*p) {
                    if (*p == L'\\') {
                        *p = L'\0';
                        CreateDirectoryW(parentDir, NULL);
                        *p = L'\\';
                    }
                    p++;
                }
                CreateDirectoryW(destDir, NULL);
                
                // 复制文件
                if (!CopyFileW(fullPath, destPath, FALSE)) {
                    MessageBoxW(hWnd, L"复制 EFI 文件到 ESP 失败", L"错误", MB_OK | MB_ICONERROR);
                    if (mounted) UnmountESP(esp[0]);
                    break;
                }
            }
            
            // 最终检查：确保有有效的 EFI 路径
            if (efiPath[0] == L'\0') {
                MessageBoxW(hWnd, L"无法确定 EFI 路径\n\n请确保选择了有效的 ESP 分区", L"错误", MB_OK | MB_ICONERROR);
                if (mounted) UnmountESP(esp[0]);
                break;
            }
            
            WCHAR device[32] = L"";
            // device 参数暂时不用，UefiAddBootEntry 会自动处理
            
            DWORD newId = 0;
            if (UefiAddBootEntry(title, device, efiPath, &newId)) {
                WCHAR msg[128];
                swprintf(msg, 128, L"添加成功\nBootID: %04X", newId);
                MessageBoxW(hWnd, msg, L"完成", MB_OK);
                RefreshBootList();
            } else {
                MessageBoxW(hWnd, L"添加失败\n请以管理员身份运行", L"错误", MB_OK | MB_ICONERROR);
            }
            if (mounted) UnmountESP(esp[0]);
            break;
        }
        
        case ID_BTN_DELETE_ENTRY: {
            if (!g_hListView) break;
            int sel = ListView_GetNextItem(g_hListView, -1, LVNI_SELECTED);
            if (sel == -1) { MessageBoxW(hWnd, L"请先选择", L"提示", MB_OK); break; }
            
            WCHAR idText[16] = {0};
            ListView_GetItemText(g_hListView, sel, 0, idText, 16);
            DWORD id = wcstoul(idText, NULL, 16);
            
            if (MessageBoxW(hWnd, L"确定删除？", L"确认", MB_YESNO) == IDYES) {
                if (UefiDeleteBootEntry(id)) {
                    RefreshBootList(); SetStatus(L"✓ 已删除");
                } else {
                    MessageBoxW(hWnd, L"删除启动项失败\n请以管理员身份运行", L"错误", MB_OK | MB_ICONERROR);
                }
            }
            break;
        }
        
        case ID_BTN_MOVE_UP:
        case ID_BTN_MOVE_DOWN: {
            if (!g_hListView || !g_bootList) break;
            int sel = ListView_GetNextItem(g_hListView, -1, LVNI_SELECTED);
            if (sel == -1) break;
            
            WCHAR idText[16] = {0};
            ListView_GetItemText(g_hListView, sel, 0, idText, 16);
            DWORD id = wcstoul(idText, NULL, 16);
            
            BOOL ok = (cmd == ID_BTN_MOVE_UP) ? UefiMoveBootEntryUp(g_bootList, id) : UefiMoveBootEntryDown(g_bootList, id);
            if (ok) { RefreshBootList(); SetStatus(L"✓ 已调整"); }
            else { SetStatus(L"✗ 调整失败"); }
            break;
        }
        
        case ID_BTN_SET_DEFAULT: {
            if (!g_hListView) break;
            int sel = ListView_GetNextItem(g_hListView, -1, LVNI_SELECTED);
            if (sel == -1) { MessageBoxW(hWnd, L"请先选择", L"提示", MB_OK); break; }
            
            WCHAR idText[16] = {0};
            ListView_GetItemText(g_hListView, sel, 0, idText, 16);
            DWORD id = wcstoul(idText, NULL, 16);
            
            if (UefiSetDefaultBootEntry(g_bootList, id)) {
                MessageBoxW(hWnd, L"已设为默认", L"完成", MB_OK);
            } else {
                MessageBoxW(hWnd, L"设置默认启动项失败\n请以管理员身份运行", L"错误", MB_OK | MB_ICONERROR);
            }
            break;
        }
        
        // rEFInd
        case ID_BTN_INSTALL: {
            // 让用户选择 ESP 分区
            WCHAR espDrive = ShowEspSelectDialog(hWnd);
            if (espDrive == 0) break;
            
            WCHAR esp[4] = {espDrive, L':', 0};
            
            WCHAR src[MAX_PATH];
            if (!ResolveRefindSourcePath(src, MAX_PATH)) {
                MessageBoxW(hWnd, L"未找到 refind 源文件", L"错误", MB_OK | MB_ICONERROR);
                EspUnmountEx(esp, FALSE);  // Clean up ESP mount
                break;
            }
            
            if (RefindInstall(src, esp)) {
                MessageBoxW(hWnd, L"rEFInd 安装成功！", L"完成", MB_OK);
                if (s_refindBtnInstall) EnableWindow(s_refindBtnInstall, FALSE);
                if (s_refindBtnUninstall) EnableWindow(s_refindBtnUninstall, TRUE);
                if (s_refindStatus) SetWindowTextW(s_refindStatus, L"rEFInd 已安装");
            } else {
                MessageBoxW(hWnd, L"安装失败", L"错误", MB_OK | MB_ICONERROR);
            }
            EspUnmountEx(esp, FALSE);  // Always unmount ESP after operation
            break;
        }
        
        case ID_BTN_UNINSTALL: {
            if (MessageBoxW(hWnd, L"确定卸载 rEFInd？", L"确认", MB_YESNO) != IDYES) break;
            
            // 让用户选择 ESP 分区
            WCHAR espDrive = ShowEspSelectDialog(hWnd);
            if (espDrive == 0) break;
            
            WCHAR esp[4] = {espDrive, L':', 0};
            
            if (RefindUninstall(esp)) {
                MessageBoxW(hWnd, L"已卸载", L"完成", MB_OK);
                if (s_refindBtnInstall) EnableWindow(s_refindBtnInstall, TRUE);
                if (s_refindBtnUninstall) EnableWindow(s_refindBtnUninstall, FALSE);
                if (s_refindStatus) SetWindowTextW(s_refindStatus, L"rEFInd 未安装");
            }
            EspUnmountEx(esp, FALSE);  // Always unmount ESP after operation
            break;
        }
        
        // Limine
        case ID_BTN_INSTALL_LIMINE: {
            WCHAR src[MAX_PATH];
            if (!ResolveLimineSourcePath(src, MAX_PATH)) {
                MessageBoxW(hWnd, 
                    L"未找到 Limine 源文件\n\n"
                    L"请在程序目录下创建 limine 文件夹：\n"
                    L"  程序目录\\limine\\limine-bios.sys  (BIOS 模式)\n"
                    L"  程序目录\\limine\\limine-efi\\BOOTX64.EFI  (UEFI 模式)\n\n"
                    L"可从 https://github.com/limine-bootloader/limine/releases 下载",
                    L"缺少文件", MB_OK | MB_ICONINFORMATION);
                break;
            }
            
            // 让用户选择 ESP 分区
            WCHAR espDrive = ShowEspSelectDialog(hWnd);
            if (espDrive == 0) break;
            
            WCHAR esp[4] = {espDrive, L':', 0};
            
            // 安装 Limine 到选中的 ESP（LimineInstallToUEFI 内部会注册 NVRAM）
            if (LimineInstallToUEFI(esp, src)) {
                MessageBoxW(hWnd, L"Limine 安装成功！", L"完成", MB_OK);
                if (s_limineBtnInstall) EnableWindow(s_limineBtnInstall, FALSE);
                if (s_limineBtnUninstall) EnableWindow(s_limineBtnUninstall, TRUE);
                if (s_limineBtnAddEntry) EnableWindow(s_limineBtnAddEntry, TRUE);
                if (s_limineBtnEditEntry) EnableWindow(s_limineBtnEditEntry, TRUE);
                if (s_limineBtnDelEntry) EnableWindow(s_limineBtnDelEntry, TRUE);
                if (s_limineStatus) SetWindowTextW(s_limineStatus, L"Limine 已安装");
                
                // 设置配置目录
                swprintf(s_limineConfDir, MAX_PATH, L"%s\\EFI\\limine", esp);
                RefreshLimineEntryList();
            } else {
                WCHAR msg[512];
                swprintf(msg, 512, L"安装失败\n%s", LimineGetLastErrorMessage());
                MessageBoxW(hWnd, msg, L"错误", MB_OK | MB_ICONERROR);
            }
            EspUnmountEx(esp, FALSE);  // Always unmount ESP after operation
            break;
        }
        
        case ID_BTN_UNINSTALL_LIMINE: {
            if (MessageBoxW(hWnd, L"确定卸载 Limine？", L"确认", MB_YESNO) != IDYES) break;
            
            // 让用户选择 ESP 分区
            WCHAR espDrive = ShowEspSelectDialog(hWnd);
            if (espDrive == 0) break;
            
            WCHAR esp[4] = {espDrive, L':', 0};
            
            if (LimineUninstall(esp)) {
                MessageBoxW(hWnd, L"已卸载", L"完成", MB_OK);
                // Only update UI state on successful uninstall
                if (s_limineBtnInstall) EnableWindow(s_limineBtnInstall, TRUE);
                if (s_limineBtnUninstall) EnableWindow(s_limineBtnUninstall, FALSE);
                if (s_limineBtnAddEntry) EnableWindow(s_limineBtnAddEntry, FALSE);
                if (s_limineBtnEditEntry) EnableWindow(s_limineBtnEditEntry, FALSE);
                if (s_limineBtnDelEntry) EnableWindow(s_limineBtnDelEntry, FALSE);
                if (s_limineStatus) SetWindowTextW(s_limineStatus, L"Limine 未安装");
                if (s_limineList) ListView_DeleteAllItems(s_limineList);
                s_limineEntryCount = 0;
                s_limineConfDir[0] = L'\0';
            } else {
                MessageBoxW(hWnd, L"卸载失败", L"错误", MB_OK | MB_ICONERROR);
            }
            EspUnmountEx(esp, FALSE);  // Always unmount ESP after operation
            break;
        }
        
        // Limine 启动项管理
        case ID_BTN_LIMINE_ADD: {  // 添加
            if (s_limineEntryCount >= 64) break;
            LIMINE_CONF_ENTRY* e = &s_limineEntries[s_limineEntryCount];
            memset(e, 0, sizeof(LIMINE_CONF_ENTRY));
            
            if (ShowLimineEditDialog(hWnd, e, TRUE)) {
                s_limineEntryCount++;
                SaveLimineConfEntries();
                RefreshLimineEntryList();
                // 选中新条目
                ListView_SetItemState(s_limineList, s_limineEntryCount - 1, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
            }
            break;
        }
        
        case ID_BTN_LIMINE_EDIT: {  // 编辑
            int sel = ListView_GetNextItem(s_limineList, -1, LVNI_SELECTED);
            if (sel < 0 || sel >= s_limineEntryCount) {
                MessageBoxW(hWnd, L"请先选择一个启动项", L"提示", MB_OK);
                break;
            }
            
            if (ShowLimineEditDialog(hWnd, &s_limineEntries[sel], FALSE)) {
                SaveLimineConfEntries();
                RefreshLimineEntryList();
            }
            break;
        }
        
        case ID_BTN_LIMINE_DELETE: {  // 删除
            int sel = ListView_GetNextItem(s_limineList, -1, LVNI_SELECTED);
            if (sel < 0 || sel >= s_limineEntryCount) {
                MessageBoxW(hWnd, L"请先选择一个启动项", L"提示", MB_OK);
                break;
            }
            WCHAR msg[256];
            swprintf(msg, 256, L"确定删除 \"%s\" 吗？", s_limineEntries[sel].name);
            if (MessageBoxW(hWnd, msg, L"确认", MB_YESNO) == IDYES) {
                for (int i = sel; i < s_limineEntryCount - 1; i++) {
                    s_limineEntries[i] = s_limineEntries[i + 1];
                }
                s_limineEntryCount--;
                SaveLimineConfEntries();
                RefreshLimineEntryList();
            }
            break;
        }
        
        case ID_BTN_LIMINE_REFRESH:  // 刷新
            RefreshLimineEntryList();
            break;
        
        // 备份恢复
        case ID_BTN_BACKUP_MBR: {
            WCHAR dir[MAX_PATH] = {0}, file[MAX_PATH];
            BackupGetBackupDir(dir, MAX_PATH);
            SYSTEMTIME st; GetLocalTime(&st);
            swprintf(file, MAX_PATH, L"%s\\MBR_%04d%02d%02d.bin", dir, st.wYear, st.wMonth, st.wDay);
            if (BackupMBR(L"PhysicalDrive0", file)) {
                MessageBoxW(hWnd, L"备份成功", L"完成", MB_OK);
            } else {
                MessageBoxW(hWnd, L"备份失败", L"错误", MB_OK | MB_ICONERROR);
            }
            break;
        }
        
        case ID_BTN_RESTORE: {
            OPENFILENAMEW ofn = {0};
            WCHAR file[MAX_PATH] = {0}, dir[MAX_PATH];
            BackupGetBackupDir(dir, MAX_PATH);
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner = hWnd;
            ofn.lpstrFile = file;
            ofn.nMaxFile = MAX_PATH;
            ofn.lpstrFilter = L"MBR 备份 (*.bin)\0*.bin\0";
            ofn.lpstrInitialDir = dir;
            ofn.Flags = OFN_FILEMUSTEXIST;
            
            if (!GetOpenFileNameW(&ofn)) break;
            
            // 询问是否保留分区表
            int result = MessageBoxW(hWnd, 
                L"是否保留当前分区表？\n\n"
                L"选择「是」：仅恢复引导代码，保留分区表（推荐）\n"
                L"选择「否」：完整恢复（包含分区表，危险操作）\n"
                L"选择「取消」：放弃操作",
                L"恢复选项", MB_YESNOCANCEL | MB_ICONQUESTION);
            
            if (result == IDCANCEL) break;
            
            BOOL preservePartTable = (result == IDYES);
            WCHAR error[256] = {0};

            // Use MBR_Restore with preservePartTable (disk 0 = PhysicalDrive0)
            if (MBR_Restore(0, file, preservePartTable, error, 256)) {
                MessageBoxW(hWnd, L"恢复成功", L"完成", MB_OK);
            } else {
                MessageBoxW(hWnd, error[0] ? error : L"恢复失败", L"错误", MB_OK | MB_ICONERROR);
            }
            break;
        }
        
        case ID_BTN_MBR_REPAIR: {
            if (MessageBoxW(hWnd, L"确定修复 MBR？", L"确认", MB_YESNO) != IDYES) break;
            // Use CreateProcessW instead of ShellExecuteExW for WinPE compatibility
            STARTUPINFOW si = {0}; si.cb = sizeof(si); si.dwFlags = STARTF_USESHOWWINDOW; si.wShowWindow = SW_HIDE;
            PROCESS_INFORMATION pi = {0};
            WCHAR cmdLine[256] = L"cmd.exe /c bootrec /fixmbr";
            if (CreateProcessW(NULL, cmdLine, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
                WaitForSingleObject(pi.hProcess, 30000);
                DWORD code;
                GetExitCodeProcess(pi.hProcess, &code);
                CloseHandle(pi.hProcess);
                CloseHandle(pi.hThread);
                MessageBoxW(hWnd, code == 0 ? L"修复完成" : L"修复失败", code == 0 ? L"完成" : L"错误", MB_OK);
            } else {
                MessageBoxW(hWnd, L"无法启动 bootrec\n在 WinPE 中请确保 bootrec.exe 可用", L"错误", MB_OK | MB_ICONERROR);
            }
            break;
        }
        
        // 备份恢复页面的新增按钮
        case ID_BTN_RESTORE_MBR: {
            OPENFILENAMEW ofn = {0};
            WCHAR file[MAX_PATH] = {0}, dir[MAX_PATH];
            BackupGetBackupDir(dir, MAX_PATH);
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner = hWnd;
            ofn.lpstrFile = file;
            ofn.nMaxFile = MAX_PATH;
            ofn.lpstrFilter = L"MBR 备份 (*.bin)\0*.bin\0";
            ofn.lpstrInitialDir = dir;
            ofn.Flags = OFN_FILEMUSTEXIST;
            
            if (!GetOpenFileNameW(&ofn)) break;
            
            // 询问是否保留分区表
            int result = MessageBoxW(hWnd, 
                L"是否保留当前分区表？\n\n"
                L"选择「是」：仅恢复引导代码，保留分区表（推荐）\n"
                L"选择「否」：完整恢复（包含分区表，危险操作）\n"
                L"选择「取消」：放弃操作",
                L"恢复选项", MB_YESNOCANCEL | MB_ICONQUESTION);
            
            if (result == IDCANCEL) break;
            
            BOOL preservePartTable = (result == IDYES);
            WCHAR error[256] = {0};
            
            // 默认恢复到磁盘 0
            if (MBR_Restore(0, file, preservePartTable, error, 256)) {
                MessageBoxW(hWnd, L"MBR 恢复成功", L"完成", MB_OK);
            } else {
                MessageBoxW(hWnd, error[0] ? error : L"恢复失败", L"错误", MB_OK | MB_ICONERROR);
            }
            break;
        }
        
        case ID_BTN_UEFI_REPAIR: {
            if (!IsUEFICached()) {
                MessageBoxW(hWnd, L"当前不是 UEFI 模式，无法修复 UEFI 引导", L"错误", MB_OK | MB_ICONERROR);
                break;
            }
            
            if (MessageBoxW(hWnd, 
                L"确定修复 UEFI 引导？\n\n"
                L"此操作将：\n"
                L"• 检查并修复 ESP 分区\n"
                L"• 重新创建 Windows Boot Manager 启动项\n"
                L"• 修复 UEFI NVRAM 启动顺序",
                L"确认", MB_YESNO | MB_ICONQUESTION) != IDYES) break;
            
            // Detect ESP drive letter dynamically instead of hardcoding S:
            WCHAR espDrive[4] = {0};
            BOOL espMounted = FALSE;
            if (!EspFind(espDrive, 4)) {
                // Try to mount ESP
                BOOL mountedByUs = FALSE;
                espMounted = EspMountEx(espDrive, 4, &mountedByUs);
                if (!espMounted) {
                    MessageBoxW(hWnd, L"未找到 ESP 分区，无法修复 UEFI 引导", L"错误", MB_OK | MB_ICONERROR);
                    break;
                }
            }

            // Build bcdboot command with detected ESP drive
            // Use SystemRoot environment variable instead of hardcoding C:\Windows
            WCHAR sysRoot[MAX_PATH] = L"C:\\Windows";
            GetEnvironmentVariableW(L"SystemRoot", sysRoot, MAX_PATH);

            // Get system locale for bcdboot /l parameter
            WCHAR locale[16] = L"zh-cn";
            GetLocaleInfoW(LOCALE_SYSTEM_DEFAULT, LOCALE_SNAME, locale, 16);

            WCHAR params[512];
            swprintf(params, 512, L"cmd.exe /c bcdboot %s /l %s /s %c: /f UEFI", sysRoot, locale, espDrive[0]);

            // Use CreateProcessW instead of ShellExecuteExW for WinPE compatibility
            STARTUPINFOW si = {0}; si.cb = sizeof(si); si.dwFlags = STARTF_USESHOWWINDOW; si.wShowWindow = SW_HIDE;
            PROCESS_INFORMATION pi = {0};
            if (CreateProcessW(NULL, params, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
                WaitForSingleObject(pi.hProcess, 30000);
                DWORD code;
                GetExitCodeProcess(pi.hProcess, &code);
                CloseHandle(pi.hProcess);
                CloseHandle(pi.hThread);
                
                if (code == 0) {
                    MessageBoxW(hWnd, L"UEFI 引导修复成功\n\n如果启动项仍未显示，请重启后检查 BIOS 设置", L"完成", MB_OK);
                } else {
                    MessageBoxW(hWnd, L"UEFI 引导修复失败\n\n可能需要手动检查 ESP 分区", L"错误", MB_OK | MB_ICONERROR);
                }
            } else {
                MessageBoxW(hWnd, L"无法启动 bcdboot\n在 WinPE 中请确保 bcdboot.exe 可用", L"错误", MB_OK | MB_ICONERROR);
            }
            // Always unmount ESP after repair operation
            EspUnmountEx(espDrive, FALSE);
            break;
        }
    }
    return 0;
}
        
    case WM_NOTIFY: {
        LPNMHDR pnmhdr = (LPNMHDR)lParam;
        
        // Tab 切换
        if (pnmhdr->code == TCN_SELCHANGE) {
            if (pnmhdr->idFrom == ID_TAB_THIRD_PARTY) {
                g_currentThirdPartyTab = TabCtrl_GetCurSel(pnmhdr->hwndFrom);
                // Show/hide tab panels — instant, no rebuild
                ShowWindow(g_hRefindPanel, g_currentThirdPartyTab == 0 ? SW_SHOW : SW_HIDE);
                ShowWindow(g_hLiminePanel, g_currentThirdPartyTab == 1 ? SW_SHOW : SW_HIDE);
            }
        }
        return 0;
    }
        
    case WM_APP_REFRESH_REFIND:
        RefindRefreshStatus();
        return 0;
        
    case WM_APP_REFRESH_LIMINE:
        LimineRefreshStatus();
        return 0;
    
    case WM_SIZE: {
        // Resize content area to match client area
        if (g_hContent) {
            MoveWindow(g_hContent, SIDEBAR_WIDTH, 0, 
                LOWORD(lParam) - SIDEBAR_WIDTH, HIWORD(lParam), TRUE);
            // Resize all page containers
            for (int i = 0; i < 4; i++) {
                if (g_hPages[i]) {
                    MoveWindow(g_hPages[i], 0, 0,
                        LOWORD(lParam) - SIDEBAR_WIDTH, HIWORD(lParam), TRUE);
                }
            }
        }
        return 0;
    }
        
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

static void CreateNavigation(HWND hWnd)
{
    // 侧边栏背景 - 白色
    HWND hSidebar = CreateWindowExW(0, L"STATIC", NULL, 
        WS_CHILD | WS_VISIBLE | SS_WHITERECT, 
        0, 0, SIDEBAR_WIDTH, WINDOW_HEIGHT, hWnd, NULL, NULL, NULL);
    
    // Logo 和标题 - 使用主色调
    HWND hLogo = CreateWindowExW(0, L"STATIC", L"Boot Manager Pro", 
        WS_CHILD | WS_VISIBLE, 24, 28, SIDEBAR_WIDTH - 48, 32, hWnd, NULL, NULL, NULL);
    SendMessageW(hLogo, WM_SETFONT, (WPARAM)g_fontTitle, TRUE);
    
    // 版本号 - 次要文字色
    HWND hVer = CreateWindowExW(0, L"STATIC", L"v1.0", 
        WS_CHILD | WS_VISIBLE, 24, 56, SIDEBAR_WIDTH - 48, 20, hWnd, NULL, NULL, NULL);
    SendMessageW(hVer, WM_SETFONT, (WPARAM)g_fontSmall, TRUE);
    
    // 分隔线
    HWND hSeparator = CreateWindowExW(0, L"STATIC", NULL, 
        WS_CHILD | WS_VISIBLE | SS_ETCHEDHORZ, 
        24, 90, SIDEBAR_WIDTH - 48, 2, hWnd, NULL, NULL, NULL);
    
    // 导航项 - 中英双语
    struct { int id; const WCHAR* text; } items[] = {
        { ID_NAV_BOOT_MGR, L"  ⚙ 引导管理 / Boot" },
        { ID_NAV_THIRD_PARTY, L"  📦 第三方引导 / 3rd Party" },
        { ID_NAV_BACKUP_RESTORE, L"  💾 备份恢复 / Backup" },
        { ID_NAV_ABOUT, L"  ℹ 关于 / About" }
    };
    
    int y = 110;
    for (int i = 0; i < 4; i++) {
        g_navItems[i] = CreateWindowExW(0, L"BUTTON", items[i].text, 
            WS_CHILD | WS_VISIBLE | BS_LEFT | BS_FLAT, 
            12, y, SIDEBAR_WIDTH - 24, NAV_ITEM_HEIGHT, 
            hWnd, (HMENU)(UINT_PTR)items[i].id, NULL, NULL);
        SendMessageW(g_navItems[i], WM_SETFONT, (WPARAM)g_fontBody, TRUE);
        y += NAV_ITEM_HEIGHT + 8;
    }
}

static void CreateContentArea(HWND hWnd)
{
    RECT rc;
    GetClientRect(hWnd, &rc);
    // Use client area height, not window height — window height includes title bar
    g_hContent = CreateWindowExW(0, L"BootManagerContentClass", NULL, 
        WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN, 
        SIDEBAR_WIDTH, 0, WINDOW_WIDTH - SIDEBAR_WIDTH, rc.bottom, 
        hWnd, NULL, NULL, NULL);
    
    // Create 4 page container windows (invisible initially, shown on switch)
    // Use BootManagerContentClass so WM_COMMAND/WM_NOTIFY are forwarded to main wnd
    for (int i = 0; i < 4; i++) {
        g_hPages[i] = CreateWindowExW(0, L"BootManagerContentClass", NULL,
            WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
            0, 0, rc.right, rc.bottom,
            g_hContent, NULL, NULL, NULL);
    }
    
    // Build all pages once
    BuildBootMgrPage(g_hPages[0]);
    BuildThirdPartyPage(g_hPages[1]);
    BuildBackupRestorePage(g_hPages[2]);
    BuildAboutPage(g_hPages[3]);
    
    // Show first page
    SwitchPage(0);
}

// 销毁子窗口的回调
static BOOL CALLBACK DestroyChildWindow(HWND hWnd, LPARAM lParam)
{
    DestroyWindow(hWnd);
    return TRUE;
}

static void SwitchPage(int page)
{
    g_currentPage = page;
    
    // Show/hide page containers — no destroy/recreate, instant switch
    for (int i = 0; i < 4; i++) {
        if (g_hPages[i]) {
            ShowWindow(g_hPages[i], (i == page) ? SW_SHOW : SW_HIDE);
        }
    }
    
    // Refresh data for the visible page
    if (page == 0 && g_hListView) {
        RefreshBootList();
    }
}

// 引导管理页面 (仅 UEFI 管理)
static void BuildBootMgrPage(HWND hParent)
{
    RECT rc; GetClientRect(hParent, &rc);
    int w = rc.right - CONTENT_PADDING * 2;
    BOOL isUEFI = IsUEFICached();
    int y = CONTENT_PADDING;
    
    // 标题区 (unified height)
    HWND hTitle = CreateWindowExW(0, L"STATIC", L"UEFI 启动项管理 / Boot Entries", 
        WS_CHILD | WS_VISIBLE, CONTENT_PADDING, y, w, PAGE_TITLE_H, hParent, NULL, NULL, NULL);
    SendMessageW(hTitle, WM_SETFONT, (WPARAM)g_fontTitle, TRUE);
    y += PAGE_TITLE_H + PAGE_TITLE_GAP;
    
    // 说明文字
    HWND hDesc = CreateWindowExW(0, L"STATIC", 
        L"管理 UEFI 启动项：添加、删除、调整顺序、设置默认", 
        WS_CHILD | WS_VISIBLE, CONTENT_PADDING, y, w, PAGE_DESC_H, hParent, NULL, NULL, NULL);
    SendMessageW(hDesc, WM_SETFONT, (WPARAM)g_fontBody, TRUE);
    y += PAGE_DESC_H + PAGE_SECTION_GAP;
    
    if (!isUEFI) {
        HWND hInfo = CreateWindowExW(0, L"STATIC",
            L"⚠ 当前系统未运行在 UEFI 模式下\n"
            L"请在 BIOS 设置中启用 UEFI 启动模式",
            WS_CHILD | WS_VISIBLE, CONTENT_PADDING, y, w, 80, hParent, NULL, NULL, NULL);
        SendMessageW(hInfo, WM_SETFONT, (WPARAM)g_fontBody, TRUE);
        return;
    }
    
    // UEFI 启动项列表
    g_hListView = CreateWindowExW(0, WC_LISTVIEW, NULL,
        WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL, 
        CONTENT_PADDING, y, w, rc.bottom - y - PAGE_FOOTER_H, hParent, (HMENU)ID_LIST_BOOT, NULL, NULL);
    ListView_SetExtendedListViewStyle(g_hListView, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
    
    LVCOLUMN lvc = {0}; lvc.mask = LVCF_TEXT | LVCF_WIDTH;
    lvc.pszText = L"ID"; lvc.cx = 70; ListView_InsertColumn(g_hListView, 0, &lvc);
    lvc.pszText = L"名称"; lvc.cx = 200; ListView_InsertColumn(g_hListView, 1, &lvc);
    lvc.pszText = L"路径"; lvc.cx = w - 280; ListView_InsertColumn(g_hListView, 2, &lvc);
    
    RefreshBootList();
    
    // 状态栏 (above button row)
    g_hStatusText = CreateWindowExW(0, L"STATIC", L"就绪", 
        WS_CHILD | WS_VISIBLE, CONTENT_PADDING, rc.bottom - PAGE_FOOTER_H, w, PAGE_STATUS_H, hParent, (HMENU)ID_STATUS_TEXT, NULL, NULL);
    SendMessageW(g_hStatusText, WM_SETFONT, (WPARAM)g_fontSmall, TRUE);
    
    // 按钮行 (below status bar)
    int bx = CONTENT_PADDING, by = rc.bottom - PAGE_FOOTER_H + PAGE_STATUS_H + PAGE_BTN_GAP;
    CreateFlatButton(hParent, bx, by, 80, PAGE_BTN_H, L"刷新", ID_BTN_REFRESH, FALSE); bx += 80 + PAGE_BTN_GAP;
    CreateFlatButton(hParent, bx, by, 80, PAGE_BTN_H, L"添加", ID_BTN_ADD_ENTRY, TRUE); bx += 80 + PAGE_BTN_GAP;
    CreateFlatButton(hParent, bx, by, 80, PAGE_BTN_H, L"删除", ID_BTN_DELETE_ENTRY, FALSE); bx += 80 + PAGE_BTN_GAP;
    CreateFlatButton(hParent, bx, by, 40, PAGE_BTN_H, L"↑", ID_BTN_MOVE_UP, FALSE); bx += 40 + PAGE_BTN_GAP;
    CreateFlatButton(hParent, bx, by, 40, PAGE_BTN_H, L"↓", ID_BTN_MOVE_DOWN, FALSE); bx += 40 + PAGE_BTN_GAP;
    CreateFlatButton(hParent, bx, by, 100, PAGE_BTN_H, L"设为默认", ID_BTN_SET_DEFAULT, FALSE);
}

// 第三方引导管理器页面
static void BuildThirdPartyPage(HWND hParent)
{
    RECT rc; GetClientRect(hParent, &rc);
    int w = rc.right - CONTENT_PADDING * 2;
    BOOL isUEFI = IsUEFICached();
    
    // MBR 模式下显示提示
    if (!isUEFI) {
        int y = CONTENT_PADDING;
        
        HWND hTitle = CreateWindowExW(0, L"STATIC", L"第三方引导管理器 / 3rd Party Bootloader", 
            WS_CHILD | WS_VISIBLE, CONTENT_PADDING, y, w, PAGE_TITLE_H, hParent, NULL, NULL, NULL);
        SendMessageW(hTitle, WM_SETFONT, (WPARAM)g_fontTitle, TRUE);
        y += PAGE_TITLE_H + PAGE_SECTION_GAP;
        
        HWND hInfo = CreateWindowExW(0, L"STATIC",
            L"⚠ 当前为 Legacy BIOS (MBR) 模式\n"
            L"rEFInd 和 Limine UEFI 版本仅支持 UEFI 模式",
            WS_CHILD | WS_VISIBLE, CONTENT_PADDING, y, w, 60, hParent, NULL, NULL, NULL);
        SendMessageW(hInfo, WM_SETFONT, (WPARAM)g_fontBody, TRUE);
        return;
    }
    
    // UEFI 模式：显示 Tab
    HWND hTab = CreateWindowExW(0, WC_TABCONTROL, NULL, 
        WS_CHILD | WS_VISIBLE, CONTENT_PADDING, CONTENT_PADDING, w, TAB_HEIGHT, 
        hParent, (HMENU)ID_TAB_THIRD_PARTY, NULL, NULL);
    SendMessageW(hTab, WM_SETFONT, (WPARAM)g_fontBody, TRUE);
    
    TCITEMW tci = {0}; tci.mask = TCIF_TEXT;
    tci.pszText = L"rEFInd"; TabCtrl_InsertItem(hTab, 0, &tci);
    tci.pszText = L"Limine"; TabCtrl_InsertItem(hTab, 1, &tci);
    
    TabCtrl_SetCurSel(hTab, g_currentThirdPartyTab);
    
    // Create tab panels as child containers (both always exist, show/hide to switch)
    int panelY = CONTENT_PADDING + TAB_HEIGHT + PAGE_TITLE_GAP;
    g_hRefindPanel = CreateWindowExW(0, L"BootManagerContentClass", NULL,
        WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
        0, panelY, rc.right, rc.bottom - panelY, hParent, NULL, NULL, NULL);
    g_hLiminePanel = CreateWindowExW(0, L"BootManagerContentClass", NULL,
        WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
        0, panelY, rc.right, rc.bottom - panelY, hParent, NULL, NULL, NULL);
    
    // Build both tab panels once
    BuildRefindControls(g_hRefindPanel);
    BuildLimineControls(g_hLiminePanel);
    
    // Show the active tab panel
    ShowWindow(g_hRefindPanel, g_currentThirdPartyTab == 0 ? SW_SHOW : SW_HIDE);
    ShowWindow(g_hLiminePanel, g_currentThirdPartyTab == 1 ? SW_SHOW : SW_HIDE);
}

// rEFInd 控件
static void BuildRefindControls(HWND hParent)
{
    RECT rc; GetClientRect(hParent, &rc);
    int w = rc.right - CONTENT_PADDING * 2;
    int y = CONTENT_PADDING;
    
    HWND hTitle = CreateWindowExW(0, L"STATIC", L"rEFInd 引导管理器", 
        WS_CHILD | WS_VISIBLE, CONTENT_PADDING, y, w, PAGE_TITLE_H, hParent, NULL, NULL, NULL);
    SendMessageW(hTitle, WM_SETFONT, (WPARAM)g_fontTitle, TRUE);
    y += PAGE_TITLE_H + PAGE_TITLE_GAP;
    
    HWND hDesc = CreateWindowExW(0, L"STATIC", 
        L"自动检测并列出所有操作系统的 UEFI 引导管理器", 
        WS_CHILD | WS_VISIBLE, CONTENT_PADDING, y, w, PAGE_DESC_H, hParent, NULL, NULL, NULL);
    SendMessageW(hDesc, WM_SETFONT, (WPARAM)g_fontBody, TRUE);
    y += PAGE_DESC_H + PAGE_TITLE_GAP;
    
    HWND hInfo = CreateWindowExW(0, L"STATIC", 
        L"安装位置: ESP\\EFI\\refind\\", 
        WS_CHILD | WS_VISIBLE, CONTENT_PADDING, y, w, PAGE_DESC_H, hParent, NULL, NULL, NULL);
    SendMessageW(hInfo, WM_SETFONT, (WPARAM)g_fontSmall, TRUE);
    y += PAGE_DESC_H + PAGE_SECTION_GAP;
    
    // 按钮行
    s_refindBtnInstall = CreateFlatButton(hParent, CONTENT_PADDING, y, 100, PAGE_BTN_H, L"安装", ID_BTN_INSTALL, TRUE);
    s_refindBtnUninstall = CreateFlatButton(hParent, CONTENT_PADDING + 100 + PAGE_BTN_GAP, y, 100, PAGE_BTN_H, L"卸载", ID_BTN_UNINSTALL, FALSE);
    y += PAGE_BTN_H + PAGE_SECTION_GAP;
    
    HWND hNote = CreateWindowExW(0, L"STATIC", 
        L"• 安装后自动扫描系统\n"
        L"• 支持 Windows、Linux、macOS\n"
        L"• 需要 refind\\ 文件夹", 
        WS_CHILD | WS_VISIBLE, CONTENT_PADDING, y, w, 60, hParent, NULL, NULL, NULL);
    SendMessageW(hNote, WM_SETFONT, (WPARAM)g_fontSmall, TRUE);
    
    // 状态栏 (unified position)
    s_refindStatus = CreateWindowExW(0, L"STATIC", L"检测中...", 
        WS_CHILD | WS_VISIBLE, CONTENT_PADDING, rc.bottom - PAGE_STATUS_H, w, PAGE_STATUS_H, hParent, NULL, NULL, NULL);
    SendMessageW(s_refindStatus, WM_SETFONT, (WPARAM)g_fontSmall, TRUE);
    
    // Defer status refresh — MountESP is slow, don't block page creation
    PostMessageW(g_hMainWnd, WM_APP_REFRESH_REFIND, 0, 0);
}

static void RefindRefreshStatus(void)
{
    WCHAR esp[4] = {0};
    BOOL installed = FALSE;
    if (MountESP(esp)) {
        installed = RefindIsInstalled(esp);
        UnmountESP(esp[0]);
    }
    
    if (s_refindBtnInstall) EnableWindow(s_refindBtnInstall, !installed);
    if (s_refindBtnUninstall) EnableWindow(s_refindBtnUninstall, installed);
    if (s_refindStatus) SetWindowTextW(s_refindStatus, installed ? L"✓ rEFInd 已安装 / Installed" : L"rEFInd 未安装 / Not installed");
}

// 解析 limine.conf
static BOOL LoadLimineConfEntries(void)
{
    s_limineEntryCount = 0;
    if (s_limineConfDir[0] == L'\0') {
        return FALSE;
    }
    
    // Ensure ESP is mounted — it may have been unmounted after status detection
    BOOL espMountedByUs = FALSE;
    WCHAR espRoot[4] = {s_limineConfDir[0], L':', L'\\', 0};
    if (GetDriveTypeW(espRoot) == DRIVE_NO_ROOT_DIR) {
        // ESP not accessible, remount it
        WCHAR esp[4] = {0};
        if (!EspMountEx(esp, 4, &espMountedByUs)) {
            return FALSE;
        }
        // Update conf dir with new drive letter
        swprintf(s_limineConfDir, MAX_PATH, L"%s\\EFI\\limine", esp);
    }
    
    WCHAR confPath[MAX_PATH];
    swprintf(confPath, MAX_PATH, L"%s\\limine.conf", s_limineConfDir);
    
    HANDLE hFile = CreateFileW(confPath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        goto load_cleanup;
    }
    
    DWORD fileSize = GetFileSize(hFile, NULL);
    if (fileSize == 0 || fileSize > 64 * 1024) {
        CloseHandle(hFile);
        goto load_cleanup;
    }
    
    char* buffer = (char*)malloc(fileSize + 1);
    if (!buffer) {
        CloseHandle(hFile);
        goto load_cleanup;
    }
    DWORD bytesRead = 0;
    if (!ReadFile(hFile, buffer, fileSize, &bytesRead, NULL) || bytesRead == 0) {
        free(buffer);
        CloseHandle(hFile);
        goto load_cleanup;
    }
    buffer[bytesRead] = '\0';
    CloseHandle(hFile);
    
    // 计算宽字符缓冲区大小
    int wideSize = MultiByteToWideChar(CP_UTF8, 0, buffer, -1, NULL, 0);
    if (wideSize <= 0) {
        free(buffer);
        goto load_cleanup;
    }
    
    WCHAR* wbuffer = (WCHAR*)malloc(wideSize * sizeof(WCHAR));
    if (!wbuffer) {
        free(buffer);
        goto load_cleanup;
    }
    MultiByteToWideChar(CP_UTF8, 0, buffer, -1, wbuffer, wideSize);
    free(buffer);
    
    // 解析行
    WCHAR* line = wbuffer;
    WCHAR* lineEnd = NULL;
    LIMINE_CONF_ENTRY* current = NULL;
    
    while (*line && s_limineEntryCount < 64) {
        // 找到行尾
        lineEnd = line;
        while (*lineEnd && *lineEnd != L'\r' && *lineEnd != L'\n') {
            lineEnd++;
        }
        
        // 截断行
        WCHAR saved = *lineEnd;
        *lineEnd = L'\0';
        
        // 跳过空行和注释
        if (line[0] != L'\0' && line[0] != L'#') {
            if (line[0] == L'/') {
                // 新条目
                current = &s_limineEntries[s_limineEntryCount];
                memset(current, 0, sizeof(LIMINE_CONF_ENTRY));
                wcsncpy(current->name, line + 1, 127);
                wcscpy(current->protocol, L"limine");
                s_limineEntryCount++;
            } else if (current) {
                // 解析键值对
                WCHAR* colon = wcschr(line, L':');
                if (colon) {
                    *colon = L'\0';
                    WCHAR* key = line;
                    while (*key == L' ' || *key == L'\t') key++;
                    WCHAR* value = colon + 1;
                    while (*value == L' ' || *value == L'\t') value++;
                    
                    if (_wcsicmp(key, L"protocol") == 0) {
                        wcsncpy(current->protocol, value, 31);
                    } else if (_wcsicmp(key, L"path") == 0) {
                        wcsncpy(current->path, value, MAX_PATH - 1);
                    } else if (_wcsicmp(key, L"kernel_cmdline") == 0) {
                        wcsncpy(current->cmdline, value, 511);
                    }
                }
            }
        }
        
        // 恢复并移到下一行
        *lineEnd = saved;
        line = lineEnd;
        while (*line == L'\r' || *line == L'\n') {
            line++;
        }
    }
    
    free(wbuffer);
    
    // Unmount ESP after loading config
    {
        WCHAR espDrive[4] = {s_limineConfDir[0], L':', 0};
        EspUnmountEx(espDrive, FALSE);
    }
    
    return s_limineEntryCount > 0;

load_cleanup:
    {
        WCHAR espDrive[4] = {s_limineConfDir[0], L':', 0};
        EspUnmountEx(espDrive, FALSE);
    }
    return FALSE;
}

// 保存 limine.conf
static BOOL SaveLimineConfEntries(void)
{
    if (s_limineConfDir[0] == L'\0') return FALSE;
    
    // Ensure ESP is mounted — it may have been unmounted after status detection
    BOOL espMountedByUs = FALSE;
    WCHAR espRoot[4] = {s_limineConfDir[0], L':', L'\\', 0};
    if (GetDriveTypeW(espRoot) == DRIVE_NO_ROOT_DIR) {
        WCHAR esp[4] = {0};
        if (!EspMountEx(esp, 4, &espMountedByUs)) {
            return FALSE;
        }
        swprintf(s_limineConfDir, MAX_PATH, L"%s\\EFI\\limine", esp);
    }
    
    WCHAR confPath[MAX_PATH];
    swprintf(confPath, MAX_PATH, L"%s\\limine.conf", s_limineConfDir);
    
    HANDLE hFile = CreateFileW(confPath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) goto save_cleanup;
    
    // Use heap allocation with bounds checking instead of 32KB stack buffer
    DWORD bufCap = 8192;  // Start with 8K, grow if needed
    DWORD bufLen = 0;
    WCHAR* wbuffer = (WCHAR*)malloc(bufCap * sizeof(WCHAR));
    if (!wbuffer) {
        CloseHandle(hFile);
        goto save_cleanup;
    }

    const WCHAR* header = L"# Limine Configuration\n# Generated by Boot Manager Pro\n\ntimeout: 5\n\n";
    DWORD headerLen = (DWORD)wcslen(header);
    if (headerLen + 1 > bufCap) {
        free(wbuffer);
        CloseHandle(hFile);
        goto save_cleanup;
    }
    wcscpy(wbuffer, header);
    bufLen = headerLen;
    
    for (int i = 0; i < s_limineEntryCount; i++) {
        LIMINE_CONF_ENTRY* e = &s_limineEntries[i];
        WCHAR entry[2048];
        int entryLen = swprintf(entry, 2048, L"/%s\n    protocol: %s\n    path: %s\n", e->name, e->protocol, e->path);

        // Write cmdline if present (for linux protocol)
        if (wcslen(e->cmdline) > 0) {
            entryLen += swprintf(entry + entryLen, 2048 - entryLen, L"    kernel_cmdline: %s\n", e->cmdline);
        }
        entryLen += swprintf(entry + entryLen, 2048 - entryLen, L"\n");

        // Grow buffer if needed
        if (bufLen + entryLen + 1 > bufCap) {
            DWORD newCap = bufCap * 2;
            WCHAR* newBuf = (WCHAR*)realloc(wbuffer, newCap * sizeof(WCHAR));
            if (!newBuf) {
                free(wbuffer);
                CloseHandle(hFile);
                goto save_cleanup;
            }
            wbuffer = newBuf;
            bufCap = newCap;
        }

        wcscat(wbuffer, entry);
        bufLen += entryLen;
    }
    
    int utf8Len = WideCharToMultiByte(CP_UTF8, 0, wbuffer, -1, NULL, 0, NULL, NULL);
    char* utf8 = (char*)malloc(utf8Len);
    if (!utf8) {
        free(wbuffer);
        CloseHandle(hFile);
        goto save_cleanup;
    }
    WideCharToMultiByte(CP_UTF8, 0, wbuffer, -1, utf8, utf8Len, NULL, NULL);
    
    DWORD written;
    BOOL writeOk = WriteFile(hFile, utf8, utf8Len - 1, &written, NULL);
    free(utf8);
    free(wbuffer);
    CloseHandle(hFile);
    
    // Unmount ESP after saving config
    {
        WCHAR espDrive[4] = {s_limineConfDir[0], L':', 0};
        EspUnmountEx(espDrive, FALSE);
    }
    
    return writeOk && (written == (DWORD)(utf8Len - 1));

save_cleanup:
    {
        WCHAR espDrive[4] = {s_limineConfDir[0], L':', 0};
        EspUnmountEx(espDrive, FALSE);
    }
    return FALSE;
}

// 刷新 Limine 启动项列表
static void RefreshLimineEntryList(void)
{
    if (!s_limineList) return;
    ListView_DeleteAllItems(s_limineList);
    
    // 检查配置目录
    if (s_limineConfDir[0] == L'\0') {
        if (s_limineStatus) SetWindowTextW(s_limineStatus, L"未找到配置目录");
        return;
    }
    
    // 加载配置
    LoadLimineConfEntries();
    
    // 添加到列表
    for (int i = 0; i < s_limineEntryCount; i++) {
        LVITEMW lvi = {0};
        lvi.mask = LVIF_TEXT;
        WCHAR num[8]; 
        swprintf(num, 8, L"%d", i + 1);
        lvi.pszText = num;
        lvi.iItem = i;
        ListView_InsertItem(s_limineList, &lvi);
        ListView_SetItemText(s_limineList, i, 1, s_limineEntries[i].name);
        ListView_SetItemText(s_limineList, i, 2, s_limineEntries[i].protocol);
        ListView_SetItemText(s_limineList, i, 3, s_limineEntries[i].path);
    }
    
    // 更新状态
    if (s_limineStatus) {
        WCHAR status[512];
        if (s_limineEntryCount > 0) {
            swprintf(status, 512, L"共 %d 个启动项 [%s]", s_limineEntryCount, s_limineConfDir);
        } else {
            swprintf(status, 512, L"配置文件为空或无效 [%s\\limine.conf]", s_limineConfDir);
        }
        SetWindowTextW(s_limineStatus, status);
    }
}

// 根据扩展名自动选择协议
// Limine 协议说明：
// - limine: 原生协议，支持 ISO 镜像的 CD-ROM 模拟引导
// - efi: UEFI 链式加载，用于加载 EFI 引导程序（如 Windows、GRUB）
// - bios_chain: BIOS 链式加载
// - linux: Linux 内核直接启动
static const WCHAR* DetectProtocolByExtension(const WCHAR* path)
{
    if (!path || !path[0]) return L"limine";
    
    const WCHAR* ext = wcsrchr(path, L'.');
    if (!ext) return L"limine";
    
    if (_wcsicmp(ext, L".iso") == 0 || _wcsicmp(ext, L".img") == 0) {
        return L"limine";  // ISO/IMG 镜像 - 使用 CD-ROM 模拟
    }
    if (_wcsicmp(ext, L".efi") == 0) {
        return L"efi";  // EFI 文件 - UEFI 链式加载
    }
    if (_wcsicmp(ext, L".bin") == 0) {
        return L"bios_chain";  // BIOS 引导
    }
    if (_wcsicmp(ext, L".elf") == 0) {
        return L"linux";  // Linux 内核
    }
    
    return L"limine";  // 默认
}

// 编辑对话框数据
static LIMINE_CONF_ENTRY* g_editEntry = NULL;
static BOOL g_editResult = FALSE;  // TRUE if user clicked OK
static BOOL g_editIsAdd = FALSE;
static HWND g_editHwnd = NULL;

static LRESULT CALLBACK LimineEditDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
        case WM_CREATE: {
            RECT rc;
            GetClientRect(hDlg, &rc);
            int w = rc.right, y = 15;
            HFONT hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
            
            // 名称
            CreateWindowExW(0, L"STATIC", L"启动项名称：", WS_CHILD | WS_VISIBLE, 15, y, 80, 20, hDlg, NULL, NULL, NULL);
            CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", g_editEntry->name, WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 100, y, w - 180, 24, hDlg, (HMENU)3300, NULL, NULL);
            y += 35;
            
            // 协议
            CreateWindowExW(0, L"STATIC", L"启动协议：", WS_CHILD | WS_VISIBLE, 15, y, 80, 20, hDlg, NULL, NULL, NULL);
            HWND hCombo = CreateWindowExW(0, L"COMBOBOX", NULL, WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST, 100, y, w - 180, 150, hDlg, (HMENU)3302, NULL, NULL);
            SendMessageW(hCombo, WM_SETFONT, (WPARAM)hFont, TRUE);
            // Limine 协议说明
            // limine: ISO/IMG 镜像的 CD-ROM 模拟引导
            // efi: UEFI 链式加载 EFI 文件（grubx64.efi, bootmgfw.efi, shimx64.efi 等）
            // linux: 直接加载 Linux 内核（.elf 文件）
            ComboBox_AddString(hCombo, L"limine (.iso/.img 镜像)");
            ComboBox_AddString(hCombo, L"efi (.efi 引导程序)");
            ComboBox_AddString(hCombo, L"linux (.elf 内核文件)");
            int sel = 0;
            if (_wcsicmp(g_editEntry->protocol, L"efi") == 0) sel = 1;
            else if (_wcsicmp(g_editEntry->protocol, L"linux") == 0) sel = 2;
            ComboBox_SetCurSel(hCombo, sel);
            y += 35;
            
            // 路径
            CreateWindowExW(0, L"STATIC", L"启动文件：", WS_CHILD | WS_VISIBLE, 15, y, 80, 20, hDlg, NULL, NULL, NULL);
            CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", g_editEntry->path, WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 100, y, w - 200, 24, hDlg, (HMENU)3301, NULL, NULL);
            CreateWindowExW(0, L"BUTTON", L"浏览...", WS_CHILD | WS_VISIBLE, w - 90, y, 75, 24, hDlg, (HMENU)3305, NULL, NULL);
            y += 35;
            
            // 内核命令行 (kernel_cmdline, linux 协议时使用)
            CreateWindowExW(0, L"STATIC", L"内核参数：", WS_CHILD | WS_VISIBLE, 15, y, 80, 20, hDlg, NULL, NULL, NULL);
            CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", g_editEntry->cmdline, WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 100, y, w - 120, 24, hDlg, (HMENU)3306, NULL, NULL);
            y += 45;
            
            // 按钮
            CreateWindowExW(0, L"BUTTON", L"确定", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, (w - 180) / 2, y, 80, 28, hDlg, (HMENU)IDOK, NULL, NULL);
            CreateWindowExW(0, L"BUTTON", L"取消", WS_CHILD | WS_VISIBLE, (w - 180) / 2 + 100, y, 80, 28, hDlg, (HMENU)IDCANCEL, NULL, NULL);
            
            // 设置字体
            HWND hChild = GetWindow(hDlg, GW_CHILD);
            while (hChild) {
                SendMessageW(hChild, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
                hChild = GetWindow(hChild, GW_HWNDNEXT);
            }
            
            return 0;
        }
        
        case WM_COMMAND: {
            switch (LOWORD(wParam)) {
                case 3305: {  // 浏览
                    OPENFILENAMEW ofn = {0};
                    WCHAR szFile[MAX_PATH] = {0};
                    GetDlgItemTextW(hDlg, 3301, szFile, MAX_PATH);
                    
                    ofn.lStructSize = sizeof(ofn);
                    ofn.hwndOwner = hDlg;
                    ofn.lpstrFile = szFile;
                    ofn.nMaxFile = MAX_PATH;
                    ofn.lpstrFilter = L"支持的文件 (*.iso;*.img;*.efi;*.elf)\0*.iso;*.img;*.efi;*.elf\0ISO 镜像 (*.iso)\0*.iso\0EFI 文件 (*.efi)\0*.efi\0所有文件 (*.*)\0*.*\0";
                    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;
                    
                    if (GetOpenFileNameW(&ofn)) {
                        SetDlgItemTextW(hDlg, 3301, szFile);
                        
                        // 自动设置名称
                        WCHAR name[128];
                        const WCHAR* fn = wcsrchr(szFile, L'\\');
                        if (fn) fn++; else fn = szFile;
                        wcsncpy(name, fn, 127);
                        WCHAR* dot = wcsrchr(name, L'.');
                        if (dot) *dot = L'\0';
                        SetDlgItemTextW(hDlg, 3300, name);
                        
                        // 自动选择协议
                        const WCHAR* protocol = DetectProtocolByExtension(szFile);
                        int sel = 0;
                        if (_wcsicmp(protocol, L"efi") == 0) sel = 1;
                        else if (_wcsicmp(protocol, L"linux") == 0) sel = 2;
                        SendDlgItemMessageW(hDlg, 3302, CB_SETCURSEL, sel, 0);
                    }
                    return 0;
                }
                
                case IDOK: {
                    WCHAR name[128] = {0}, path[MAX_PATH] = {0}, cmdline[512] = {0};
                    GetDlgItemTextW(hDlg, 3300, name, 128);
                    GetDlgItemTextW(hDlg, 3301, path, MAX_PATH);
                    GetDlgItemTextW(hDlg, 3306, cmdline, 512);
                    
                    if (wcslen(name) == 0) {
                        MessageBoxW(hDlg, L"请输入启动项名称", L"提示", MB_OK | MB_ICONWARNING);
                        return 0;
                    }
                    if (wcslen(path) == 0) {
                        MessageBoxW(hDlg, L"请选择启动文件", L"提示", MB_OK | MB_ICONWARNING);
                        return 0;
                    }
                    
                    // 获取协议
                    int sel = (int)SendDlgItemMessageW(hDlg, 3302, CB_GETCURSEL, 0, 0);
                    wcscpy(g_editEntry->protocol, L"limine");
                    if (sel == 1) wcscpy(g_editEntry->protocol, L"efi");
                    else if (sel == 2) wcscpy(g_editEntry->protocol, L"linux");
                    
                    wcsncpy(g_editEntry->name, name, 127);
                    wcsncpy(g_editEntry->cmdline, cmdline, 511);
                    
                    // 转换路径格式
                    // Windows: C:\EFI\Microsoft\Boot\bootmgfw.efi
                    // Limine 格式取决于协议类型：
                    // - efi_chain: boot():/path (当前启动设备)
                    // - limine (ISO): boot:///path (所有设备搜索) 或 boot(disk,part):/path
                    // - linux: boot():/path
                    WCHAR liminePath[MAX_PATH] = {0};
                    
                    // 找到路径中的 \EFI 或 \boot 等启动相关部分
                    const WCHAR* efiStart = wcsstr(path, L"\\EFI\\");
                    const WCHAR* bootStart = wcsstr(path, L"\\boot\\");
                    const WCHAR* pathStart = NULL;
                    
                    if (efiStart) {
                        pathStart = efiStart + 1;  // 跳过第一个反斜杠
                    } else if (bootStart) {
                        pathStart = bootStart + 1;
                    } else {
                        // 没有找到标准路径，使用完整路径（去掉盘符）
                        if (path[1] == L':') {
                            pathStart = path + 2;  // 跳过 "C:"
                        } else {
                            pathStart = path;
                        }
                    }
                    
                    // 先复制路径并转换反斜杠为正斜杠
                    WCHAR normalizedPath[MAX_PATH] = {0};
                    wcsncpy(normalizedPath, pathStart, MAX_PATH - 1);
                    for (WCHAR* p = normalizedPath; *p; p++) {
                        if (*p == L'\\') *p = L'/';
                    }
                    // normalizedPath 已以 "/" 开头
                    
                    // boot:// + normalizedPath = boot:///path （三个斜杠，在所有设备搜索）
                    swprintf(liminePath, MAX_PATH, L"boot://%s", normalizedPath);
                    
                    wcsncpy(g_editEntry->path, liminePath, MAX_PATH - 1);
                    
                    g_editResult = TRUE;  // Mark as user confirmed
                    DestroyWindow(hDlg);
                    g_editHwnd = NULL;
                    return 0;
                }
                
                case IDCANCEL:
                    DestroyWindow(hDlg);
                    g_editHwnd = NULL;
                    return 0;
            }
            break;
        }
        
        case WM_CLOSE:
            DestroyWindow(hDlg);
            g_editHwnd = NULL;
            return 0;
    }
    
    return DefWindowProcW(hDlg, msg, wParam, lParam);
}

// 显示编辑对话框
static BOOL ShowLimineEditDialog(HWND hParent, LIMINE_CONF_ENTRY* entry, BOOL isAdd)
{
    g_editEntry = entry;
    g_editIsAdd = isAdd;
    g_editResult = FALSE;  // Reset result before showing dialog
    
    static BOOL registered = FALSE;
    if (!registered) {
        WNDCLASSEXW wc = {0};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = LimineEditDlgProc;
        wc.hInstance = GetModuleHandleW(NULL);
        wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = L"LimineEditDlgClass";
        RegisterClassExW(&wc);
        registered = TRUE;
    }
    
    HWND hDlg = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_WINDOWEDGE,
        L"LimineEditDlgClass",
        isAdd ? L"添加启动项" : L"编辑启动项",
        WS_POPUP | WS_CAPTION | WS_SYSMENU,
        0, 0, 380, 260,
        hParent, NULL, GetModuleHandleW(NULL), NULL);
    
    if (!hDlg) return FALSE;
    
    // 居中
    RECT rcParent, rcDlg;
    GetWindowRect(hParent, &rcParent);
    GetWindowRect(hDlg, &rcDlg);
    int x = rcParent.left + (rcParent.right - rcParent.left - (rcDlg.right - rcDlg.left)) / 2;
    int y = rcParent.top + (rcParent.bottom - rcParent.top - (rcDlg.bottom - rcDlg.top)) / 2;
    SetWindowPos(hDlg, NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
    
    g_editHwnd = hDlg;  // Track dialog window for message loop
    EnableWindow(hParent, FALSE);
    ShowWindow(hDlg, SW_SHOW);
    
    // 消息循环
    MSG msg;
    while (IsWindow(hDlg)) {
        if (GetMessageW(&msg, NULL, 0, 0)) {
            if (!IsDialogMessageW(hDlg, &msg)) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        } else {
            break;
        }
    }
    
    EnableWindow(hParent, TRUE);
    SetForegroundWindow(hParent);
    
    return g_editResult;
}

static void BuildLimineControls(HWND hParent)
{
    RECT rc; GetClientRect(hParent, &rc);
    int w = rc.right - CONTENT_PADDING * 2;
    int y = CONTENT_PADDING;
    
    // 标题
    HWND hTitle = CreateWindowExW(0, L"STATIC", L"Limine 引导管理器", 
        WS_CHILD | WS_VISIBLE, CONTENT_PADDING, y, w, PAGE_TITLE_H, hParent, NULL, NULL, NULL);
    SendMessageW(hTitle, WM_SETFONT, (WPARAM)g_fontTitle, TRUE);
    y += PAGE_TITLE_H + PAGE_SECTION_GAP;
    
    // 安装按钮行
    s_limineBtnInstall = CreateFlatButton(hParent, CONTENT_PADDING, y, 100, PAGE_BTN_H, L"安装", ID_BTN_INSTALL_LIMINE, TRUE);
    s_limineBtnUninstall = CreateFlatButton(hParent, CONTENT_PADDING + 100 + PAGE_BTN_GAP, y, 100, PAGE_BTN_H, L"卸载", ID_BTN_UNINSTALL_LIMINE, FALSE);
    y += PAGE_BTN_H + PAGE_SECTION_GAP;
    
    // 配置区域标题
    HWND hConfigTitle = CreateWindowExW(0, L"STATIC", L"启动项配置", 
        WS_CHILD | WS_VISIBLE, CONTENT_PADDING, y, w, PAGE_DESC_H, hParent, NULL, NULL, NULL);
    SendMessageW(hConfigTitle, WM_SETFONT, (WPARAM)g_fontBody, TRUE);
    y += PAGE_DESC_H + PAGE_TITLE_GAP;
    
    // 列表视图
    s_limineList = CreateWindowExW(0, WC_LISTVIEW, NULL,
        WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL,
        CONTENT_PADDING, y, w, 160, hParent, (HMENU)3200, NULL, NULL);
    ListView_SetExtendedListViewStyle(s_limineList, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
    SendMessageW(s_limineList, WM_SETFONT, (WPARAM)g_fontSmall, TRUE);
    
    LVCOLUMN lvc = {0};
    lvc.mask = LVCF_TEXT | LVCF_WIDTH;
    lvc.pszText = L"#"; lvc.cx = 35;
    ListView_InsertColumn(s_limineList, 0, &lvc);
    lvc.pszText = L"名称"; lvc.cx = 120;
    ListView_InsertColumn(s_limineList, 1, &lvc);
    lvc.pszText = L"协议"; lvc.cx = 70;
    ListView_InsertColumn(s_limineList, 2, &lvc);
    lvc.pszText = L"路径"; lvc.cx = w - 235;
    ListView_InsertColumn(s_limineList, 3, &lvc);
    
    y += 170;
    
    // 操作按钮行
    int bx = CONTENT_PADDING;
    s_limineBtnAddEntry = CreateFlatButton(hParent, bx, y, 80, PAGE_BTN_H, L"添加", ID_BTN_LIMINE_ADD, TRUE); bx += 80 + PAGE_BTN_GAP;
    s_limineBtnEditEntry = CreateFlatButton(hParent, bx, y, 80, PAGE_BTN_H, L"编辑", ID_BTN_LIMINE_EDIT, FALSE); bx += 80 + PAGE_BTN_GAP;
    s_limineBtnDelEntry = CreateFlatButton(hParent, bx, y, 80, PAGE_BTN_H, L"删除", ID_BTN_LIMINE_DELETE, FALSE); bx += 80 + PAGE_BTN_GAP;
    s_limineBtnRefresh = CreateFlatButton(hParent, bx, y, 80, PAGE_BTN_H, L"刷新", ID_BTN_LIMINE_REFRESH, FALSE);
    y += PAGE_BTN_H + PAGE_TITLE_GAP;
    
    // 提示
    HWND hHint = CreateWindowExW(0, L"STATIC",
        L"双击编辑 | limine=镜像/ISO, efi=引导程序, linux=内核",
        WS_CHILD | WS_VISIBLE, CONTENT_PADDING, y, w, PAGE_DESC_H, hParent, NULL, NULL, NULL);
    SendMessageW(hHint, WM_SETFONT, (WPARAM)g_fontSmall, TRUE);
    
    // 状态栏 (unified position)
    s_limineStatus = CreateWindowExW(0, L"STATIC", L"检测中...", 
        WS_CHILD | WS_VISIBLE, CONTENT_PADDING, rc.bottom - PAGE_STATUS_H, w, PAGE_STATUS_H, hParent, NULL, NULL, NULL);
    SendMessageW(s_limineStatus, WM_SETFONT, (WPARAM)g_fontSmall, TRUE);
    
    // Defer status refresh — ESP scan is slow, don't block page creation
    PostMessageW(g_hMainWnd, WM_APP_REFRESH_LIMINE, 0, 0);
}

static void LimineRefreshStatus(void)
{
    LIMINE_STATUS status = LIMINE_NOT_INSTALLED;
    s_limineConfDir[0] = L'\0';
    
    // 1. 首先检查已挂载的驱动器
    for (WCHAR d = L'A'; d <= L'Z'; d++) {
        WCHAR root[4] = {d, L':', L'\\', 0};
        UINT type = GetDriveTypeW(root);
        
        // 跳过无效驱动器和可移动介质
        if (type == DRIVE_UNKNOWN || type == DRIVE_NO_ROOT_DIR || type == DRIVE_REMOVABLE) {
            continue;
        }
        
        // 检查 EFI\limine\BOOTX64.EFI
        WCHAR efiPath[MAX_PATH];
        swprintf(efiPath, MAX_PATH, L"%c:\\EFI\\limine\\BOOTX64.EFI", d);
        if (GetFileAttributesW(efiPath) != INVALID_FILE_ATTRIBUTES) {
            status = LIMINE_INSTALLED_UEFI;
            swprintf(s_limineConfDir, MAX_PATH, L"%c:\\EFI\\limine", d);
            break;
        }
        
        // 检查 boot\limine\limine.sys
        WCHAR bootPath[MAX_PATH];
        swprintf(bootPath, MAX_PATH, L"%c:\\boot\\limine\\limine.sys", d);
        if (GetFileAttributesW(bootPath) != INVALID_FILE_ATTRIBUTES) {
            status = LIMINE_INSTALLED_MBR;
            swprintf(s_limineConfDir, MAX_PATH, L"%c:\\boot\\limine", d);
            break;
        }
    }
    
    // 2. If not found, try mounting ESP to detect
    if (status == LIMINE_NOT_INSTALLED) {
        WCHAR esp[4] = {0};
        BOOL mountedByUs = FALSE;
        
        if (EspMountEx(esp, 4, &mountedByUs)) {
            WCHAR efiPath[MAX_PATH];
            swprintf(efiPath, MAX_PATH, L"%s\\EFI\\limine\\BOOTX64.EFI", esp);
            
            if (GetFileAttributesW(efiPath) != INVALID_FILE_ATTRIBUTES) {
                status = LIMINE_INSTALLED_UEFI;
                swprintf(s_limineConfDir, MAX_PATH, L"%s\\EFI\\limine", esp);
            }
            // Always unmount ESP after detection — don't leak mounted partitions to user
            EspUnmountEx(esp, FALSE);
        }
    }
    
    BOOL installed = (status != LIMINE_NOT_INSTALLED);
    
    if (s_limineBtnInstall) EnableWindow(s_limineBtnInstall, !installed);
    if (s_limineBtnUninstall) EnableWindow(s_limineBtnUninstall, installed);
    if (s_limineBtnAddEntry) EnableWindow(s_limineBtnAddEntry, installed);
    if (s_limineBtnEditEntry) EnableWindow(s_limineBtnEditEntry, installed);
    if (s_limineBtnDelEntry) EnableWindow(s_limineBtnDelEntry, installed);
    
    if (s_limineStatus) {
        WCHAR txt[512];
        if (installed) {
            const WCHAR* mode = (status == LIMINE_INSTALLED_UEFI) ? L"UEFI" : L"MBR";
            swprintf(txt, 512, L"已安装 (%s) [%s]", mode, s_limineConfDir);
            SetWindowTextW(s_limineStatus, txt);
            RefreshLimineEntryList();
        } else {
            SetWindowTextW(s_limineStatus, L"Limine 未安装 - 点击「安装 Limine」按钮");
            if (s_limineList) ListView_DeleteAllItems(s_limineList);
        }
    }
}

// 备份恢复页面
static void BuildBackupRestorePage(HWND hParent)
{
    RECT rc; GetClientRect(hParent, &rc);
    int w = rc.right - CONTENT_PADDING * 2;
    int y = CONTENT_PADDING;
    
    // 标题
    HWND hTitle = CreateWindowExW(0, L"STATIC", L"备份与恢复", 
        WS_CHILD | WS_VISIBLE, CONTENT_PADDING, y, w, PAGE_TITLE_H, hParent, NULL, NULL, NULL);
    SendMessageW(hTitle, WM_SETFONT, (WPARAM)g_fontTitle, TRUE);
    y += PAGE_TITLE_H + PAGE_SECTION_GAP;
    
    // ===== MBR 备份恢复区域 =====
    HWND hMbrTitle = CreateWindowExW(0, L"STATIC", L"MBR 备份与修复", 
        WS_CHILD | WS_VISIBLE, CONTENT_PADDING, y, w, PAGE_DESC_H, hParent, NULL, NULL, NULL);
    SendMessageW(hMbrTitle, WM_SETFONT, (WPARAM)g_fontBody, TRUE);
    y += PAGE_DESC_H + PAGE_TITLE_GAP;
    
    // MBR 按钮行
    int bx = CONTENT_PADDING;
    CreateFlatButton(hParent, bx, y, 100, PAGE_BTN_H, L"备份 MBR", ID_BTN_BACKUP_MBR, FALSE); bx += 100 + PAGE_BTN_GAP;
    CreateFlatButton(hParent, bx, y, 100, PAGE_BTN_H, L"恢复 MBR", ID_BTN_RESTORE_MBR, FALSE); bx += 100 + PAGE_BTN_GAP;
    CreateFlatButton(hParent, bx, y, 120, PAGE_BTN_H, L"修复 Win MBR", ID_BTN_MBR_REPAIR, TRUE);
    y += PAGE_BTN_H + PAGE_TITLE_GAP;
    
    // MBR 说明
    HWND hMbrNote = CreateWindowExW(0, L"STATIC",
        L"• 备份：保存完整 MBR\n"
        L"• 恢复：从备份恢复（保留分区表）\n"
        L"• 修复：重置为 Windows 标准",
        WS_CHILD | WS_VISIBLE, CONTENT_PADDING, y, w, 55, hParent, NULL, NULL, NULL);
    SendMessageW(hMbrNote, WM_SETFONT, (WPARAM)g_fontSmall, TRUE);
    y += 60 + PAGE_SECTION_GAP;
    
    // ===== UEFI 修复区域 =====
    BOOL isUEFI = IsUEFICached();
    
    HWND hUefiTitle = CreateWindowExW(0, L"STATIC", L"UEFI 引导修复", 
        WS_CHILD | WS_VISIBLE, CONTENT_PADDING, y, w, PAGE_DESC_H, hParent, NULL, NULL, NULL);
    SendMessageW(hUefiTitle, WM_SETFONT, (WPARAM)g_fontBody, TRUE);
    y += PAGE_DESC_H + PAGE_TITLE_GAP;
    
    if (!isUEFI) {
        HWND hUefiNote = CreateWindowExW(0, L"STATIC",
            L"⚠ 当前非 UEFI 模式，此功能不可用",
            WS_CHILD | WS_VISIBLE, CONTENT_PADDING, y, w, PAGE_DESC_H, hParent, NULL, NULL, NULL);
        SendMessageW(hUefiNote, WM_SETFONT, (WPARAM)g_fontSmall, TRUE);
        y += PAGE_DESC_H + PAGE_TITLE_GAP;
    } else {
        CreateFlatButton(hParent, CONTENT_PADDING, y, 140, PAGE_BTN_H, L"修复 UEFI 引导", ID_BTN_UEFI_REPAIR, TRUE);
        y += PAGE_BTN_H + PAGE_TITLE_GAP;
        
        HWND hUefiNote = CreateWindowExW(0, L"STATIC",
            L"• 修复 Windows Boot Manager\n"
            L"• 重建 ESP 引导文件\n"
            L"• 恢复 NVRAM 启动项",
            WS_CHILD | WS_VISIBLE, CONTENT_PADDING, y, w, 55, hParent, NULL, NULL, NULL);
        SendMessageW(hUefiNote, WM_SETFONT, (WPARAM)g_fontSmall, TRUE);
        y += 60 + PAGE_SECTION_GAP;
    }
    
    // 状态栏 (unified position)
    HWND hStatus = CreateWindowExW(0, L"STATIC", L"就绪", 
        WS_CHILD | WS_VISIBLE, CONTENT_PADDING, rc.bottom - PAGE_STATUS_H, w, PAGE_STATUS_H, hParent, (HMENU)ID_STATUS_TEXT, NULL, NULL);
    SendMessageW(hStatus, WM_SETFONT, (WPARAM)g_fontSmall, TRUE);
}

// 关于页面
static void BuildAboutPage(HWND hParent)
{
    RECT rc; GetClientRect(hParent, &rc);
    int w = rc.right - CONTENT_PADDING * 2;
    int y = CONTENT_PADDING;
    
    HWND hTitle = CreateWindowExW(0, L"STATIC", L"关于", 
        WS_CHILD | WS_VISIBLE, CONTENT_PADDING, y, w, PAGE_TITLE_H, hParent, NULL, NULL, NULL);
    SendMessageW(hTitle, WM_SETFONT, (WPARAM)g_fontTitle, TRUE);
    y += PAGE_TITLE_H + PAGE_SECTION_GAP;
    
    HWND hName = CreateWindowExW(0, L"STATIC", L"Boot Manager Pro v1.0", 
        WS_CHILD | WS_VISIBLE, CONTENT_PADDING, y, w, PAGE_TITLE_H, hParent, NULL, NULL, NULL);
    SendMessageW(hName, WM_SETFONT, (WPARAM)g_fontTitle, TRUE);
    y += PAGE_TITLE_H + PAGE_TITLE_GAP;
    
    HWND hDesc = CreateWindowExW(0, L"STATIC", L"UEFI 引导管理工具", 
        WS_CHILD | WS_VISIBLE, CONTENT_PADDING, y, w, PAGE_DESC_H, hParent, NULL, NULL, NULL);
    SendMessageW(hDesc, WM_SETFONT, (WPARAM)g_fontBody, TRUE);
    y += PAGE_DESC_H + PAGE_SECTION_GAP;
    
    HWND hInfo = CreateWindowExW(0, L"STATIC", 
        L"功能\n"
        L"• UEFI 启动项管理\n"
        L"• rEFInd / Limine 安装\n"
        L"• MBR 备份与恢复\n"
        L"• UEFI 引导修复",
        WS_CHILD | WS_VISIBLE, CONTENT_PADDING, y, w, 100, hParent, NULL, NULL, NULL);
    SendMessageW(hInfo, WM_SETFONT, (WPARAM)g_fontBody, TRUE);
}

static void RefreshBootList(void)
{
    if (!g_hListView) return;
    ListView_DeleteAllItems(g_hListView);
    if (g_bootList) UefiFreeBootList(g_bootList);
    g_bootList = UefiScanBootEntries();
    if (!g_bootList) return;
    
    UEFI_BOOT_ENTRY_WRAPPER* e = g_bootList->entries;
    int i = 0;
    while (e) {
        LVITEMW lvi = {0};
        lvi.mask = LVIF_TEXT;
        lvi.iItem = i;
        WCHAR id[16]; swprintf(id, 16, L"%04X", e->id);
        lvi.pszText = id;
        ListView_InsertItem(g_hListView, &lvi);
        ListView_SetItemText(g_hListView, i, 1, e->name);
        ListView_SetItemText(g_hListView, i, 2, e->filePath);
        e = e->next; i++;
    }
}

static BOOL ResolveRefindSourcePath(WCHAR* path, DWORD size)
{
    // 获取程序所在目录
    WCHAR exeDir[MAX_PATH];
    GetModuleFileNameW(NULL, exeDir, MAX_PATH);
    WCHAR* slash = wcsrchr(exeDir, L'\\');
    if (slash) *slash = L'\0';
    
    // 查找 refind 目录
    WCHAR testPath[MAX_PATH];
    
    // 1. 程序目录\refind
    swprintf(testPath, MAX_PATH, L"%s\\refind\\refind_x64.efi", exeDir);
    if (GetFileAttributesW(testPath) != INVALID_FILE_ATTRIBUTES) {
        swprintf(path, size, L"%s\\refind", exeDir);
        return TRUE;
    }
    
    // 2. (Development path removed - production should not hardcode drive letters)
    
    return FALSE;
}

static BOOL ResolveLimineSourcePath(WCHAR* path, DWORD size)
{
    // 获取程序所在目录
    WCHAR exeDir[MAX_PATH];
    GetModuleFileNameW(NULL, exeDir, MAX_PATH);
    WCHAR* slash = wcsrchr(exeDir, L'\\');
    if (slash) *slash = L'\0';
    
    WCHAR testPath[MAX_PATH];
    
    // 1. 程序目录\limine\limine-bios.sys (BIOS)
    swprintf(testPath, MAX_PATH, L"%s\\limine\\limine-bios.sys", exeDir);
    if (GetFileAttributesW(testPath) != INVALID_FILE_ATTRIBUTES) {
        swprintf(path, size, L"%s\\limine", exeDir);
        return TRUE;
    }
    
    // 2. 程序目录\limine\limine-efi\BOOTX64.EFI (UEFI)
    swprintf(testPath, MAX_PATH, L"%s\\limine\\limine-efi\\BOOTX64.EFI", exeDir);
    if (GetFileAttributesW(testPath) != INVALID_FILE_ATTRIBUTES) {
        swprintf(path, size, L"%s\\limine", exeDir);
        return TRUE;
    }
    
    // 3. 程序目录\limine\BOOTX64.EFI (简化结构)
    swprintf(testPath, MAX_PATH, L"%s\\limine\\BOOTX64.EFI", exeDir);
    if (GetFileAttributesW(testPath) != INVALID_FILE_ATTRIBUTES) {
        swprintf(path, size, L"%s\\limine", exeDir);
        return TRUE;
    }
    
    // 4. (Development path removed - production should not hardcode drive letters)
    
    return FALSE;
}

// ============================================
// MBR 管理页面实现
// ============================================

HWND Classic_CreateAndShow(void)
{
    static BOOL s_classRegistered = FALSE;
    if (!s_classRegistered) {
        WNDCLASSEXW wc = {0};
        wc.cbSize = sizeof(wc);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = MainWndProc;
        wc.hInstance = GetModuleHandleW(NULL);
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = CreateSolidBrush(COLOR_BG_MAIN);
        wc.lpszClassName = L"BootManagerProClass";
        RegisterClassExW(&wc);
        s_classRegistered = TRUE;
    }
    if (!g_hMainWnd) {
        InitFonts();
        RegisterFlatButtonClass();
        RegisterContentClass();
        g_hMainWnd = CreateWindowExW(0, L"BootManagerProClass", L"Boot Manager Pro v1.0",
            WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
            CW_USEDEFAULT, CW_USEDEFAULT, WINDOW_WIDTH, WINDOW_HEIGHT, NULL, NULL,
            GetModuleHandleW(NULL), NULL);
    }
    if (g_hMainWnd) {
        ShowWindow(g_hMainWnd, SW_SHOW);
        SetForegroundWindow(g_hMainWnd);
    }
    return g_hMainWnd;
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE hPrevInst, LPWSTR lpCmdLine, int nCmdShow)
{
    // Per-monitor DPI awareness (Windows 8.1+)
    typedef BOOL (WINAPI *SetProcessDpiAwarenessContext_t)(HANDLE);
    HMODULE hUser32 = GetModuleHandleW(L"user32.dll");
    if (hUser32) {
        SetProcessDpiAwarenessContext_t fn = (SetProcessDpiAwarenessContext_t)
            GetProcAddress(hUser32, "SetProcessDpiAwarenessContext");
        if (fn) fn((HANDLE)-4);  // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
    }

    InitCommonControls();

    /* 新版卡片首页为默认入口；--classic 回到旧版界面 */
    if (!(lpCmdLine && wcsstr(lpCmdLine, L"--classic"))) {
        RegisterFlatButtonClass();
        RegisterContentClass();
        return HomeMain(hInst, nCmdShow);
    }

    InitFonts();
    RegisterFlatButtonClass();  // 注册扁平按钮类
    RegisterContentClass();  // 注册内容容器窗口类

    WNDCLASSEXW wc = {0};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = MainWndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush(COLOR_BG_MAIN);
    wc.lpszClassName = L"BootManagerProClass";
    RegisterClassExW(&wc);

    g_hMainWnd = CreateWindowExW(0, L"BootManagerProClass", L"Boot Manager Pro v1.0",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, WINDOW_WIDTH, WINDOW_HEIGHT, NULL, NULL, hInst, NULL);

    ShowWindow(g_hMainWnd, nCmdShow);
    UpdateWindow(g_hMainWnd);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return (int)msg.wParam;
}
