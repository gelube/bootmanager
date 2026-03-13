/**
 * Boot Manager Pro v3 - Main UI
 * 亮色主题 - 专业高端设计
 * 
 * 设计风格：浅色背景 + 深色文字 + 蓝色主色调
 */

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <shlobj.h>
#include <wchar.h>
#include <stdio.h>
#include "../core/uefi.h"
#include "../core/refind.h"
#include "../core/backup.h"
#include "../core/wimboot.h"
#include "dialog.h"

// ============================================
// 亮色主题 - 设计令牌
// ============================================
// 背景层次
#define COLOR_BG_MAIN       RGB(250, 250, 252)     // #FAFAFC 主背景
#define COLOR_BG_SIDEBAR    RGB(255, 255, 255)     // #FFFFFF 侧边栏
#define COLOR_BG_CARD       RGB(255, 255, 255)     // #FFFFFF 卡片
#define COLOR_BG_HOVER      RGB(243, 244, 246)     // #F3F4F6 悬停
#define COLOR_BG_INPUT      RGB(249, 250, 251)     // #F9FAFB 输入框

// 主色调
#define COLOR_PRIMARY       RGB(37, 99, 235)       // #2563EB 主色蓝
#define COLOR_PRIMARY_LIGHT RGB(59, 130, 246)      // #3B82F6 浅蓝
#define COLOR_PRIMARY_DARK  RGB(29, 78, 216)       // #1D4ED8 深蓝

// 文字
#define COLOR_TEXT_PRIMARY  RGB(17, 24, 39)        // #111827 主文字
#define COLOR_TEXT_SECOND   RGB(107, 114, 128)     // #6B7280 次要文字
#define COLOR_TEXT_LIGHT    RGB(156, 163, 175)     // #9CA3AF 浅文字

// 边框
#define COLOR_BORDER        RGB(229, 231, 235)     // #E5E7EB 边框
#define COLOR_BORDER_LIGHT  RGB(243, 244, 246)     // #F3F4F6 浅边框

// 状态色
#define COLOR_SUCCESS       RGB(22, 163, 74)       // #16A34A 成功绿
#define COLOR_WARNING       RGB(234, 179, 8)       // #EAB308 警告黄
#define COLOR_DANGER        RGB(220, 38, 38)       // #DC2626 危险红
#define COLOR_INFO          RGB(14, 165, 233)      // #0EA5E9 信息蓝

// ============================================
// 尺寸
// ============================================
#define WINDOW_WIDTH        1000
#define WINDOW_HEIGHT       650
#define SIDEBAR_WIDTH       240
#define NAV_ITEM_HEIGHT     52
#define CONTENT_PADDING     32
#define BTN_HEIGHT          38
#define BTN_HEIGHT_LARGE    46
#define BTN_WIDTH           140

// ============================================
// 控件 ID
// ============================================
enum {
    ID_NAV_UEFI = 100, ID_NAV_REFIND, ID_NAV_BACKUP, ID_NAV_ABOUT,
    ID_LIST_BOOT = 200, ID_BTN_REFRESH, ID_BTN_ADD_ENTRY, ID_BTN_DELETE_ENTRY,
    ID_BTN_MOVE_UP, ID_BTN_MOVE_DOWN, ID_BTN_SET_DEFAULT, ID_BTN_MBR_FIX,
    ID_BTN_INSTALL = 300, ID_BTN_UNINSTALL,
    ID_EDIT_BACKUP_PATH = 400, ID_BTN_BROWSE_BKP,
    ID_BTN_BACKUP_MBR, ID_BTN_BACKUP_BCD, ID_BTN_BACKUP_NV,
    ID_BTN_RESTORE, ID_BTN_REPAIR,
    ID_STATUS_TEXT = 500,
    // 下拉菜单项 ID
    ID_MENU_ADD_EFI = 601,
    ID_MENU_ADD_WIM = 602,
    ID_MENU_ADD_VHD = 603
};

// ============================================
// 全局变量
// ============================================
static HWND g_hMainWnd = NULL, g_hContent = NULL, g_hListView = NULL, g_hStatusText = NULL;
static int g_currentPage = 0;
static HFONT g_fontTitle = NULL, g_fontBody = NULL, g_fontSmall = NULL;
static HWND g_navItems[4] = {0};
static UEFI_BOOT_LIST* g_bootList = NULL;

// ============================================
// 函数声明
// ============================================
LRESULT CALLBACK MainWndProc(HWND, UINT, WPARAM, LPARAM);
static void InitFonts(void);
static void CreateNavigation(HWND);
static void CreateContentArea(HWND);
static void SwitchPage(int);
static void SetStatus(const WCHAR*);
static void BuildUEFIPage(HWND);
static void BuildRefindPage(HWND);
static void BuildBackupPage(HWND);
static void BuildAboutPage(HWND);
static void RefreshBootList(void);

// ============================================
// 辅助函数：临时挂载 ESP 分区
// ============================================
static BOOL MountESP(WCHAR* driveLetter)
{
    // 找一个未使用的盘符
    WCHAR availableDrive = 0;
    for (WCHAR d = L'Z'; d >= L'S'; d--) {
        WCHAR root[4] = {d, L':', L'\\', 0};
        if (GetDriveTypeW(root) == DRIVE_NO_ROOT_DIR) {
            availableDrive = d;
            break;
        }
    }
    
    if (availableDrive == 0) {
        // 尝试其他盘符
        for (WCHAR d = L'R'; d >= L'C'; d--) {
            WCHAR root[4] = {d, L':', L'\\', 0};
            if (GetDriveTypeW(root) == DRIVE_NO_ROOT_DIR) {
                availableDrive = d;
                break;
            }
        }
    }
    
    if (availableDrive == 0) return FALSE;
    
    // 使用 mountvol S: /S 或直接挂载
    WCHAR cmd[MAX_PATH];
    swprintf(cmd, MAX_PATH, L"mountvol %c: /S", availableDrive);
    
    STARTUPINFOW si = {0};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    
    PROCESS_INFORMATION pi = {0};
    WCHAR cmdLine[MAX_PATH];
    swprintf(cmdLine, MAX_PATH, L"cmd.exe /c mountvol %c: /S", availableDrive);
    
    if (CreateProcessW(NULL, cmdLine, NULL, NULL, FALSE, 
        CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, 5000);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
    
    Sleep(500);
    
    // 验证挂载成功
    WCHAR root[4] = {availableDrive, L':', L'\\', 0};
    if (GetFileAttributesW(root) != INVALID_FILE_ATTRIBUTES) {
        driveLetter[0] = availableDrive;
        driveLetter[1] = L':';
        driveLetter[2] = L'\0';
        return TRUE;
    }
    
    return FALSE;
}

// ============================================
// 辅助函数：卸载临时挂载的 ESP
// ============================================
static void UnmountESP(WCHAR driveLetter)
{
    WCHAR cmdLine[MAX_PATH];
    swprintf(cmdLine, MAX_PATH, L"cmd.exe /c mountvol %c: /D", driveLetter);
    
    STARTUPINFOW si = {0};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    
    PROCESS_INFORMATION pi = {0};
    CreateProcessW(NULL, cmdLine, NULL, NULL, FALSE, 
        CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
    WaitForSingleObject(pi.hProcess, 3000);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
}

// ============================================
// 程序入口
// ============================================
int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE hPrev, LPWSTR cmd, int show)
{
    (void)hPrev; (void)cmd;
    
    // 检查是否以管理员身份运行
    BOOL isAdmin = FALSE;
    PSID adminGroup = NULL;
    SID_IDENTIFIER_AUTHORITY ntAuth = SECURITY_NT_AUTHORITY;
    if (AllocateAndInitializeSid(&ntAuth, 2, SECURITY_BUILTIN_DOMAIN_RID, 
        DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &adminGroup)) {
        CheckTokenMembership(NULL, adminGroup, &isAdmin);
        FreeSid(adminGroup);
    }
    
    // 如果不是管理员，请求提升权限
    if (!isAdmin) {
        WCHAR exePath[MAX_PATH];
        GetModuleFileNameW(NULL, exePath, MAX_PATH);
        
        SHELLEXECUTEINFOW sei = {0};
        sei.cbSize = sizeof(sei);
        sei.lpVerb = L"runas";
        sei.lpFile = exePath;
        sei.nShow = SW_SHOWNORMAL;
        
        if (ShellExecuteExW(&sei)) {
            return 0;
        }
    }
    
    InitCommonControls();
    InitFonts();
    
    WNDCLASSEXW wc = {0};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = MainWndProc;
    wc.hInstance = hInst;
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush(COLOR_BG_MAIN);
    wc.lpszClassName = L"BootManagerPro_Class";
    wc.hIconSm = LoadIcon(NULL, IDI_APPLICATION);
    RegisterClassExW(&wc);
    
    int x = (GetSystemMetrics(SM_CXSCREEN) - WINDOW_WIDTH) / 2;
    int y = (GetSystemMetrics(SM_CYSCREEN) - WINDOW_HEIGHT) / 2;
    
    g_hMainWnd = CreateWindowExW(0, L"BootManagerPro_Class", 
        L"Boot Manager Pro v3",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_VISIBLE,
        x, y, WINDOW_WIDTH, WINDOW_HEIGHT, NULL, NULL, hInst, NULL);
    
    ShowWindow(g_hMainWnd, show);
    UpdateWindow(g_hMainWnd);
    
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    
    if (g_bootList) UefiFreeBootList(g_bootList);
    DeleteObject(g_fontTitle);
    DeleteObject(g_fontBody);
    DeleteObject(g_fontSmall);
    return (int)msg.wParam;
}

static void InitFonts(void)
{
    g_fontTitle = CreateFontW(20, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
    g_fontBody = CreateFontW(14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
    g_fontSmall = CreateFontW(12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
}

static void SetStatus(const WCHAR* text)
{
    if (g_hStatusText) SetWindowTextW(g_hStatusText, text);
}

// ============================================
// 主窗口过程
// ============================================
LRESULT CALLBACK MainWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_CREATE:
        CreateNavigation(hWnd);
        CreateContentArea(hWnd);
        SwitchPage(0);
        return 0;
        
    case WM_CTLCOLORSTATIC: {
        HDC hdc = (HDC)wParam;
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, COLOR_TEXT_PRIMARY);
        return (LRESULT)CreateSolidBrush(COLOR_BG_MAIN);
    }
        
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case ID_NAV_UEFI: SwitchPage(0); break;
        case ID_NAV_REFIND: SwitchPage(1); break;
        case ID_NAV_BACKUP: SwitchPage(2); break;
        case ID_NAV_ABOUT: SwitchPage(3); break;
        
        case ID_BTN_REFRESH:
            RefreshBootList();
            SetStatus(L"✓ 列表已刷新");
            break;
        
        // 添加启动项 - 下拉菜单
        case ID_BTN_ADD_ENTRY: {
            // 获取按钮位置
            HWND hBtn = GetDlgItem(hWnd, ID_BTN_ADD_ENTRY);
            RECT rc;
            GetWindowRect(hBtn, &rc);
            
            // 创建弹出菜单
            HMENU hMenu = CreatePopupMenu();
            AppendMenuW(hMenu, MF_STRING, ID_MENU_ADD_EFI, L"📝 添加 EFI 启动项");
            AppendMenuW(hMenu, MF_STRING, ID_MENU_ADD_WIM, L"📁 添加 WIM 启动");
            AppendMenuW(hMenu, MF_STRING, ID_MENU_ADD_VHD, L"💾 添加 VHD 启动");
            
            // 显示下拉菜单
            TrackPopupMenu(hMenu, TPM_LEFTALIGN | TPM_TOPALIGN,
                rc.left, rc.bottom, 0, hWnd, NULL);
            
            DestroyMenu(hMenu);
            break;
        }
        
        // 菜单项处理 - 添加 EFI 启动项 (使用对话框)
        case ID_MENU_ADD_EFI: {
            WCHAR menuTitle[256] = {0};
            WCHAR filePath[512] = {0};
            
            // 显示添加启动项对话框
            if (!ShowAddEfiDialog(hWnd, menuTitle, filePath)) {
                break;  // 用户取消
            }
            
            // 验证输入
            if (wcslen(menuTitle) == 0 || wcslen(filePath) == 0) {
                MessageBoxW(hWnd, L"请输入完整的启动项信息", L"错误", MB_OK | MB_ICONWARNING);
                break;
            }
            
            SetStatus(L"⏳ 正在添加启动项...");
            
            // 使用 bcdedit 创建启动项
            // bcdedit /create /d "菜单标题" /application bootsector
            WCHAR cmd[1024];
            swprintf(cmd, 1024, L"bcdedit /create /d \"%s\" /application bootsector", menuTitle);
            
            STARTUPINFOW si = {0};
            si.cb = sizeof(si);
            si.dwFlags = STARTF_USESHOWWINDOW;
            si.wShowWindow = SW_HIDE;
            
            PROCESS_INFORMATION pi = {0};
            if (CreateProcessW(NULL, cmd, NULL, NULL, FALSE, 
                CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
                WaitForSingleObject(pi.hProcess, 5000);
                CloseHandle(pi.hProcess);
                CloseHandle(pi.hThread);
                
                // 设置路径
                swprintf(cmd, 1024, L"bcdedit /set {current} path \"%s\"", filePath);
                CreateProcessW(NULL, cmd, NULL, NULL, FALSE, 
                    CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
                WaitForSingleObject(pi.hProcess, 5000);
                CloseHandle(pi.hProcess);
                CloseHandle(pi.hThread);
                
                MessageBoxW(hWnd, L"启动项添加成功!\n\n请在列表中查看新启动项。", L"完成", MB_OK | MB_ICONINFORMATION);
                SetStatus(L"✓ 启动项已添加");
                RefreshBootList();
            } else {
                MessageBoxW(hWnd, L"添加失败\n\n请确保以管理员身份运行", L"错误", MB_OK | MB_ICONERROR);
                SetStatus(L"✗ 添加失败");
            }
            break;
        }
        
        // 菜单项处理 - 添加 WIM 启动
        case ID_MENU_ADD_WIM: {
            WCHAR wimPath[MAX_PATH] = {0};
            if (!WimSelectFileDialog(hWnd, wimPath, MAX_PATH)) break;
            
            WCHAR defaultName[MAX_PATH];
            const WCHAR* lastNameSep = wcsrchr(wimPath, L'\\');
            if (lastNameSep) {
                wcsncpy(defaultName, lastNameSep + 1, MAX_PATH);
                WCHAR* dot = wcschr(defaultName, L'.');
                if (dot) *dot = L'\0';
            } else {
                wcsncpy(defaultName, L"Windows PE", MAX_PATH);
            }
            
            SetStatus(L"⏳ 正在添加 WIM 启动项...");
            if (WimAddBootEntry(defaultName, wimPath, L"1")) {
                WCHAR msg[MAX_PATH + 64];
                swprintf(msg, MAX_PATH + 64, L"WIM 启动项添加成功!\n\n名称：%s\n路径：%s", defaultName, wimPath);
                MessageBoxW(hWnd, msg, L"完成", MB_OK | MB_ICONINFORMATION);
                SetStatus(L"✓ WIM 启动项已添加");
                RefreshBootList();
            } else {
                MessageBoxW(hWnd, L"WIM 启动项添加失败", L"错误", MB_OK | MB_ICONERROR);
                SetStatus(L"✗ 添加失败");
            }
            break;
        }
        
        // 菜单项处理 - 添加 VHD 启动
        case ID_MENU_ADD_VHD: {
            WCHAR vhdPath[MAX_PATH] = {0};
            if (!VhdSelectFileDialog(hWnd, vhdPath, MAX_PATH)) break;
            
            WCHAR defaultName[MAX_PATH];
            const WCHAR* lastNameSep = wcsrchr(vhdPath, L'\\');
            if (lastNameSep) {
                wcsncpy(defaultName, lastNameSep + 1, MAX_PATH);
                WCHAR* dot = wcschr(defaultName, L'.');
                if (dot) *dot = L'\0';
            } else {
                wcsncpy(defaultName, L"VHD Windows", MAX_PATH);
            }
            
            SetStatus(L"⏳ 正在添加 VHD 启动项...");
            if (VhdAddBootEntry(defaultName, vhdPath)) {
                WCHAR msg[MAX_PATH + 64];
                swprintf(msg, MAX_PATH + 64, L"VHD 启动项添加成功!\n\n名称：%s\n路径：%s", defaultName, vhdPath);
                MessageBoxW(hWnd, msg, L"完成", MB_OK | MB_ICONINFORMATION);
                SetStatus(L"✓ VHD 启动项已添加");
                RefreshBootList();
            } else {
                MessageBoxW(hWnd, L"VHD 启动项添加失败", L"错误", MB_OK | MB_ICONERROR);
                SetStatus(L"✗ 添加失败");
            }
            break;
        }
            
        case ID_BTN_INSTALL: {
            SetStatus(L"⏳ 正在准备安装...");
            UpdateWindow(hWnd);
            
            // 临时挂载 ESP
            WCHAR espDrive[4] = {0};
            if (!MountESP(espDrive)) {
                MessageBoxW(hWnd, L"ESP 分区挂载失败\n\n请以管理员身份运行此程序", L"错误", MB_OK | MB_ICONERROR);
                SetStatus(L"✗ 挂载失败");
                break;
            }
            
            SetStatus(L"⏳ 正在安装 rEFInd...");
            UpdateWindow(hWnd);
            
            // 检查源文件
            if (GetFileAttributesW(L"Z:\\refind0.14.2\\refind\\refind_x64.efi") == INVALID_FILE_ATTRIBUTES) {
                UnmountESP(espDrive[0]);
                MessageBoxW(hWnd, L"未找到 rEFInd 源文件\n\n路径: Z:\\refind0.14.2\\refind\\refind_x64.efi", L"错误", MB_OK | MB_ICONERROR);
                SetStatus(L"✗ 源文件缺失");
                break;
            }
            
            // 安装
            BOOL success = RefindInstall(L"Z:\\refind0.14.2\\refind", espDrive);
            
            // 卸载 ESP（可选，也可以保持挂载）
            // UnmountESP(espDrive[0]);
            
            if (success) {
                MessageBoxW(hWnd, L"rEFInd 安装成功！\n\n重启后将显示 rEFInd 启动菜单。", L"安装完成", MB_OK | MB_ICONINFORMATION);
                SetStatus(L"✓ rEFInd 安装成功");
            } else {
                MessageBoxW(hWnd, L"安装失败\n\n请检查:\n1. 是否以管理员身份运行\n2. 磁盘空间是否充足\n3. ESP 分区是否可写", L"错误", MB_OK | MB_ICONERROR);
                SetStatus(L"✗ 安装失败");
            }
            break;
        }
        
        case ID_BTN_UNINSTALL: {
            if (MessageBoxW(hWnd, L"确定卸载 rEFInd？\n\n将恢复 Windows 引导。", L"确认", MB_YESNO | MB_ICONQUESTION) != IDYES)
                break;
            
            SetStatus(L"⏳ 正在卸载...");
            UpdateWindow(hWnd);
            
            // 临时挂载 ESP
            WCHAR espDrive[4] = {0};
            if (!MountESP(espDrive)) {
                MessageBoxW(hWnd, L"ESP 分区挂载失败", L"错误", MB_OK | MB_ICONERROR);
                SetStatus(L"✗ 挂载失败");
                break;
            }
            
            BOOL success = RefindUninstall(espDrive);
            
            if (success) {
                MessageBoxW(hWnd, L"rEFInd 已卸载\n\n将恢复 Windows 引导。", L"完成", MB_OK | MB_ICONINFORMATION);
                SetStatus(L"✓ 卸载成功");
            } else {
                MessageBoxW(hWnd, L"卸载失败", L"错误", MB_OK | MB_ICONERROR);
                SetStatus(L"✗ 卸载失败");
            }
            break;
        }
        
        case ID_BTN_BACKUP_MBR: {
            SYSTEMTIME st; GetLocalTime(&st);
            WCHAR file[MAX_PATH];
            CreateDirectoryW(L"C:\\BootBackups", NULL);
            swprintf(file, MAX_PATH, L"C:\\BootBackups\\MBR_%04d%02d%02d.bin", st.wYear, st.wMonth, st.wDay);
            SetStatus(L"⏳ 备份中...");
            if (BackupMBR(L"PhysicalDrive0", file)) {
                WCHAR msg[MAX_PATH]; swprintf(msg, MAX_PATH, L"备份成功:\n%s", file);
                MessageBoxW(hWnd, msg, L"完成", MB_OK | MB_ICONINFORMATION);
                SetStatus(L"✓ MBR 已备份");
            } else {
                MessageBoxW(hWnd, L"备份失败，需要管理员权限", L"错误", MB_OK | MB_ICONERROR);
                SetStatus(L"✗ 备份失败");
            }
            break;
        }
        
        case ID_BTN_BACKUP_BCD: {
            SYSTEMTIME st; GetLocalTime(&st);
            WCHAR file[MAX_PATH];
            CreateDirectoryW(L"C:\\BootBackups", NULL);
            swprintf(file, MAX_PATH, L"C:\\BootBackups\\BCD_%04d%02d%02d.bak", st.wYear, st.wMonth, st.wDay);
            SetStatus(L"⏳ 备份中...");
            if (BackupBCD(file)) {
                WCHAR msg[MAX_PATH]; swprintf(msg, MAX_PATH, L"备份成功:\n%s", file);
                MessageBoxW(hWnd, msg, L"完成", MB_OK | MB_ICONINFORMATION);
                SetStatus(L"✓ BCD 已备份");
            } else {
                MessageBoxW(hWnd, L"备份失败，需要管理员权限", L"错误", MB_OK | MB_ICONERROR);
                SetStatus(L"✗ 备份失败");
            }
            break;
        }
        
        case ID_BTN_BACKUP_NV: {
            SYSTEMTIME st; GetLocalTime(&st);
            WCHAR file[MAX_PATH];
            CreateDirectoryW(L"C:\\BootBackups", NULL);
            swprintf(file, MAX_PATH, L"C:\\BootBackups\\NVRAM_%04d%02d%02d.bak", st.wYear, st.wMonth, st.wDay);
            SetStatus(L"⏳ 备份中...");
            if (UefiExportNVRAM(file)) {
                WCHAR msg[MAX_PATH]; swprintf(msg, MAX_PATH, L"备份成功:\n%s", file);
                MessageBoxW(hWnd, msg, L"完成", MB_OK | MB_ICONINFORMATION);
                SetStatus(L"✓ NVRAM 已备份");
            } else {
                MessageBoxW(hWnd, L"备份失败，需要管理员权限", L"错误", MB_OK | MB_ICONERROR);
                SetStatus(L"✗ 备份失败");
            }
            break;
        }
        
        case ID_BTN_REPAIR:
            if (MessageBoxW(hWnd, L"确定运行启动修复？\n\n将执行 bootrec 系列命令。", L"确认", MB_YESNO | MB_ICONQUESTION) == IDYES) {
                SetStatus(L"⏳ 修复中...");
                if (RepairBootRec(NULL)) {
                    MessageBoxW(hWnd, L"启动修复完成", L"完成", MB_OK | MB_ICONINFORMATION);
                    SetStatus(L"✓ 修复完成");
                } else {
                    MessageBoxW(hWnd, L"修复失败", L"错误", MB_OK | MB_ICONERROR);
                    SetStatus(L"✗ 修复失败");
                }
            }
            break;
        
        // UEFI 启动项管理
        case ID_BTN_DELETE_ENTRY: {
            if (!g_hListView) break;
            int sel = ListView_GetNextItem(g_hListView, -1, LVNI_SELECTED);
            if (sel == -1) {
                MessageBoxW(hWnd, L"请先选择一个启动项", L"提示", MB_OK | MB_ICONINFORMATION);
                break;
            }
            
            WCHAR idText[16] = {0};
            ListView_GetItemText(g_hListView, sel, 0, idText, 16);
            DWORD id = wcstoul(idText, NULL, 16);
            
            WCHAR name[256] = {0};
            ListView_GetItemText(g_hListView, sel, 1, name, 256);
            
            WCHAR msg[512];
            swprintf(msg, 512, L"确定删除启动项？\n\n名称：%s\nID: %04X", name, id);
            
            if (MessageBoxW(hWnd, msg, L"确认删除", MB_YESNO | MB_ICONQUESTION) == IDYES) {
                SetStatus(L"⏳ 正在删除...");
                if (UefiDeleteBootEntry(id)) {
                    MessageBoxW(hWnd, L"启动项已删除", L"完成", MB_OK | MB_ICONINFORMATION);
                    SetStatus(L"✓ 已删除");
                    RefreshBootList();
                } else {
                    MessageBoxW(hWnd, L"删除失败\n\n请确保以管理员身份运行", L"错误", MB_OK | MB_ICONERROR);
                    SetStatus(L"✗ 删除失败");
                }
            }
            break;
        }
        
        case ID_BTN_SET_DEFAULT: {
            if (!g_hListView) break;
            int sel = ListView_GetNextItem(g_hListView, -1, LVNI_SELECTED);
            if (sel == -1) {
                MessageBoxW(hWnd, L"请先选择一个启动项", L"提示", MB_OK | MB_ICONINFORMATION);
                break;
            }
            
            WCHAR idText[16] = {0};
            ListView_GetItemText(g_hListView, sel, 0, idText, 16);
            DWORD id = wcstoul(idText, NULL, 16);
            
            WCHAR name[256] = {0};
            ListView_GetItemText(g_hListView, sel, 1, name, 256);
            
            SetStatus(L"⏳ 正在设置默认...");
            if (UefiSetDefaultBootEntry(g_bootList, id)) {
                WCHAR msg[256];
                swprintf(msg, 256, L"已设置默认启动项:\n%s", name);
                MessageBoxW(hWnd, msg, L"完成", MB_OK | MB_ICONINFORMATION);
                SetStatus(L"✓ 已设为默认");
            } else {
                MessageBoxW(hWnd, L"设置失败\n\n请确保以管理员身份运行", L"错误", MB_OK | MB_ICONERROR);
                SetStatus(L"✗ 设置失败");
            }
            break;
        }
        
        case ID_BTN_MBR_FIX: {
            if (MessageBoxW(hWnd, L"确定修复 MBR？\n\n此操作将重写主引导记录。", L"确认", MB_YESNO | MB_ICONQUESTION) == IDYES) {
                SetStatus(L"⏳ 修复 MBR 中...");
                // 使用 bootrec /fixmbr
                SHELLEXECUTEINFOW sei = {0};
                sei.cbSize = sizeof(sei);
                sei.fMask = SEE_MASK_NOCLOSEPROCESS;
                sei.lpVerb = L"runas";
                sei.lpFile = L"cmd.exe";
                sei.lpParameters = L"/c bootrec /fixmbr";
                sei.nShow = SW_HIDE;
                
                if (ShellExecuteExW(&sei)) {
                    WaitForSingleObject(sei.hProcess, 30000);
                    DWORD exitCode;
                    GetExitCodeProcess(sei.hProcess, &exitCode);
                    CloseHandle(sei.hProcess);
                    
                    if (exitCode == 0) {
                        MessageBoxW(hWnd, L"MBR 修复完成", L"完成", MB_OK | MB_ICONINFORMATION);
                        SetStatus(L"✓ MBR 已修复");
                    } else {
                        MessageBoxW(hWnd, L"MBR 修复失败", L"错误", MB_OK | MB_ICONERROR);
                        SetStatus(L"✗ 修复失败");
                    }
                }
            }
            break;
        }
        }
        return 0;
        
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

// ============================================
// 创建导航栏
// ============================================
static void CreateNavigation(HWND hWnd)
{
    // 侧边栏
    CreateWindowExW(0, L"STATIC", NULL, WS_CHILD | WS_VISIBLE,
        0, 0, SIDEBAR_WIDTH, WINDOW_HEIGHT, hWnd, NULL, NULL, NULL);
    
    // Logo / 标题
    HWND hLogo = CreateWindowExW(0, L"STATIC", L"⚙ Boot Manager",
        WS_CHILD | WS_VISIBLE | SS_LEFT, 24, 24, SIDEBAR_WIDTH - 48, 32,
        hWnd, NULL, NULL, NULL);
    SendMessageW(hLogo, WM_SETFONT, (WPARAM)g_fontTitle, TRUE);
    
    // 副标题
    HWND hSub = CreateWindowExW(0, L"STATIC", L"Professional UEFI Tool",
        WS_CHILD | WS_VISIBLE | SS_LEFT, 24, 56, SIDEBAR_WIDTH - 48, 20,
        hWnd, NULL, NULL, NULL);
    SendMessageW(hSub, WM_SETFONT, (WPARAM)g_fontSmall, TRUE);
    
    // 导航项
    struct { int id; const WCHAR* text; } items[] = {
        { ID_NAV_UEFI, L"UEFI 引导管理" },
        { ID_NAV_REFIND, L"rEFInd 管理" },
        { ID_NAV_BACKUP, L"备份与修复" },
        { ID_NAV_ABOUT, L"关于" }
    };
    
    int y = 100;
    for (int i = 0; i < 4; i++) {
        g_navItems[i] = CreateWindowExW(0, L"BUTTON", items[i].text,
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            16, y, SIDEBAR_WIDTH - 32, NAV_ITEM_HEIGHT,
            hWnd, (HMENU)(UINT_PTR)items[i].id, NULL, NULL);
        SendMessageW(g_navItems[i], WM_SETFONT, (WPARAM)g_fontBody, TRUE);
        y += NAV_ITEM_HEIGHT + 8;
    }
}

// ============================================
// 创建内容区域
// ============================================
static void CreateContentArea(HWND hWnd)
{
    g_hContent = CreateWindowExW(WS_EX_CONTROLPARENT, L"STATIC", NULL,
        WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
        SIDEBAR_WIDTH, 0, WINDOW_WIDTH - SIDEBAR_WIDTH, WINDOW_HEIGHT,
        hWnd, (HMENU)999, NULL, NULL);
}

// ============================================
// 切换页面
// ============================================
static void SwitchPage(int page)
{
    g_currentPage = page;
    g_hListView = NULL;
    g_hStatusText = NULL;
    
    if (g_hContent) { DestroyWindow(g_hContent); g_hContent = NULL; }
    CreateContentArea(g_hMainWnd);
    
    switch (page) {
        case 0: BuildUEFIPage(g_hContent); break;
        case 1: BuildRefindPage(g_hContent); break;
        case 2: BuildBackupPage(g_hContent); break;
        case 3: BuildAboutPage(g_hContent); break;
    }
}

// ============================================
// UEFI 页面
// ============================================
static void BuildUEFIPage(HWND hParent)
{
    (void)hParent;  // 使用主窗口创建控件，确保消息正确传递
    
    RECT rc; GetClientRect(g_hMainWnd, &rc);
    int w = rc.right - SIDEBAR_WIDTH - CONTENT_PADDING * 2;
    int h = rc.bottom;
    
    HWND hTitle = CreateWindowExW(0, L"STATIC", L"UEFI 引导管理",
        WS_CHILD | WS_VISIBLE, SIDEBAR_WIDTH + CONTENT_PADDING, CONTENT_PADDING, w, 32,
        g_hMainWnd, NULL, NULL, NULL);
    SendMessageW(hTitle, WM_SETFONT, (WPARAM)g_fontTitle, TRUE);
    
    g_hListView = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEW, NULL,
        WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL,
        SIDEBAR_WIDTH + CONTENT_PADDING, CONTENT_PADDING + 50, w, h - 200,
        g_hMainWnd, (HMENU)ID_LIST_BOOT, NULL, NULL);
    ListView_SetExtendedListViewStyle(g_hListView, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
    
    LVCOLUMN lvc = {0}; lvc.mask = LVCF_TEXT | LVCF_WIDTH;
    lvc.pszText = L"ID"; lvc.cx = 80; ListView_InsertColumn(g_hListView, 0, &lvc);
    lvc.pszText = L"名称"; lvc.cx = 240; ListView_InsertColumn(g_hListView, 1, &lvc);
    lvc.pszText = L"路径"; lvc.cx = w - 340; ListView_InsertColumn(g_hListView, 2, &lvc);
    
    RefreshBootList();
    
    // 按钮 - 创建在主窗口上
    int bx = SIDEBAR_WIDTH + CONTENT_PADDING, by = h - 130;
    #define BTN(id, text, wd) CreateWindowExW(0, L"BUTTON", text, WS_CHILD | WS_VISIBLE, bx, by, wd, BTN_HEIGHT, g_hMainWnd, (HMENU)id, NULL, NULL); SendMessageW(GetDlgItem(g_hMainWnd, id), WM_SETFONT, (WPARAM)g_fontBody, TRUE); bx += wd + 10;
    
    BTN(ID_BTN_REFRESH, L"🔄 刷新", 90);
    BTN(ID_BTN_ADD_ENTRY, L"▼ 添加", 130);
    BTN(ID_BTN_DELETE_ENTRY, L"🗑 删除", 90);
    BTN(ID_BTN_MOVE_UP, L"↑ 上移", 70);
    BTN(ID_BTN_MOVE_DOWN, L"↓ 下移", 70);
    BTN(ID_BTN_SET_DEFAULT, L"⭐ 设为默认", 110);
    
    bx = SIDEBAR_WIDTH + CONTENT_PADDING; by = h - 80;
    BTN(ID_BTN_MBR_FIX, L"🔧 修复 MBR", 100);
    
    #undef BTN
    
    g_hStatusText = CreateWindowExW(0, L"STATIC", L"就绪",
        WS_CHILD | WS_VISIBLE, SIDEBAR_WIDTH + CONTENT_PADDING, h - 30, w, 24, g_hMainWnd, (HMENU)ID_STATUS_TEXT, NULL, NULL);
    SendMessageW(g_hStatusText, WM_SETFONT, (WPARAM)g_fontSmall, TRUE);
}

// ============================================
// rEFInd 页面 - 简洁设计
// ============================================
static void BuildRefindPage(HWND hParent)
{
    RECT rc; GetClientRect(hParent, &rc);
    int w = rc.right - CONTENT_PADDING * 2;
    int h = rc.bottom;
    int cx = (WINDOW_WIDTH - SIDEBAR_WIDTH) / 2;
    
    HWND hTitle = CreateWindowExW(0, L"STATIC", L"rEFInd 管理",
        WS_CHILD | WS_VISIBLE, CONTENT_PADDING, CONTENT_PADDING, w, 32,
        hParent, NULL, NULL, NULL);
    SendMessageW(hTitle, WM_SETFONT, (WPARAM)g_fontTitle, TRUE);
    
    // 主卡片
    HWND hCard = CreateWindowExW(0, L"BUTTON", NULL,
        WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
        CONTENT_PADDING, CONTENT_PADDING + 50, w, h - 200,
        hParent, NULL, NULL, NULL);
    
    // 图标区域
    HWND hIcon = CreateWindowExW(0, L"STATIC", L"🔄",
        WS_CHILD | WS_VISIBLE | SS_CENTER,
        cx - 30, CONTENT_PADDING + 90, 60, 50,
        hParent, NULL, NULL, NULL);
    SendMessageW(hIcon, WM_SETFONT, (WPARAM)g_fontTitle, TRUE);
    
    // 说明
    HWND hDesc = CreateWindowExW(0, L"STATIC",
        L"rEFInd 是一个现代化的引导管理器\n安装后将自动检测系统中的所有操作系统",
        WS_CHILD | WS_VISIBLE | SS_CENTER,
        CONTENT_PADDING + 50, CONTENT_PADDING + 150, w - 100, 50,
        hParent, NULL, NULL, NULL);
    SendMessageW(hDesc, WM_SETFONT, (WPARAM)g_fontBody, TRUE);
    
    // 安装按钮
    HWND hBtnInstall = CreateWindowExW(0, L"BUTTON", L"📥 安装 rEFInd",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        cx - 80, CONTENT_PADDING + 230, 160, BTN_HEIGHT_LARGE,
        hParent, (HMENU)ID_BTN_INSTALL, NULL, NULL);
    SendMessageW(hBtnInstall, WM_SETFONT, (WPARAM)g_fontBody, TRUE);
    
    // 卸载按钮
    HWND hBtnUninstall = CreateWindowExW(0, L"BUTTON", L"📤 卸载 rEFInd",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        cx - 80, CONTENT_PADDING + 290, 160, BTN_HEIGHT_LARGE,
        hParent, (HMENU)ID_BTN_UNINSTALL, NULL, NULL);
    SendMessageW(hBtnUninstall, WM_SETFONT, (WPARAM)g_fontBody, TRUE);
    
    // 提示
    HWND hNote = CreateWindowExW(0, L"STATIC",
        L"提示: 需要管理员权限 | 源文件: Z:\\refind0.14.2\\refind\\",
        WS_CHILD | WS_VISIBLE | SS_CENTER,
        CONTENT_PADDING + 50, h - 100, w - 100, 24,
        hParent, NULL, NULL, NULL);
    SendMessageW(hNote, WM_SETFONT, (WPARAM)g_fontSmall, TRUE);
    
    g_hStatusText = CreateWindowExW(0, L"STATIC", L"就绪",
        WS_CHILD | WS_VISIBLE, CONTENT_PADDING, h - 30, w, 24, hParent, (HMENU)ID_STATUS_TEXT, NULL, NULL);
    SendMessageW(g_hStatusText, WM_SETFONT, (WPARAM)g_fontSmall, TRUE);
}

// ============================================
// 备份页面
// ============================================
static void BuildBackupPage(HWND hParent)
{
    RECT rc; GetClientRect(hParent, &rc);
    int w = rc.right - CONTENT_PADDING * 2;
    int h = rc.bottom;
    
    HWND hTitle = CreateWindowExW(0, L"STATIC", L"备份与修复",
        WS_CHILD | WS_VISIBLE, CONTENT_PADDING, CONTENT_PADDING, w, 32,
        hParent, NULL, NULL, NULL);
    SendMessageW(hTitle, WM_SETFONT, (WPARAM)g_fontTitle, TRUE);
    
    // 备份卡片
    HWND hCard1 = CreateWindowExW(0, L"BUTTON", L" 创建备份",
        WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
        CONTENT_PADDING, CONTENT_PADDING + 50, w, 120,
        hParent, NULL, NULL, NULL);
    SendMessageW(hCard1, WM_SETFONT, (WPARAM)g_fontBody, TRUE);
    
    int bx = CONTENT_PADDING + 20, by = CONTENT_PADDING + 90;
    #define BTN(id, text, w) CreateWindowExW(0, L"BUTTON", text, WS_CHILD | WS_VISIBLE, bx, by, w, BTN_HEIGHT, hParent, (HMENU)id, NULL, NULL); SendMessageW(GetDlgItem(hParent, id), WM_SETFONT, (WPARAM)g_fontBody, TRUE); bx += w + 15;
    
    BTN(ID_BTN_BACKUP_MBR, L"💿 备份 MBR", 120);
    BTN(ID_BTN_BACKUP_BCD, L"📁 备份 BCD", 120);
    BTN(ID_BTN_BACKUP_NV, L"💾 备份 NVRAM", 130);
    
    // 修复卡片
    HWND hCard2 = CreateWindowExW(0, L"BUTTON", L" 系统修复",
        WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
        CONTENT_PADDING, CONTENT_PADDING + 190, w, 100,
        hParent, NULL, NULL, NULL);
    SendMessageW(hCard2, WM_SETFONT, (WPARAM)g_fontBody, TRUE);
    
    bx = CONTENT_PADDING + 20; by = CONTENT_PADDING + 230;
    BTN(ID_BTN_RESTORE, L"📤 恢复备份", 120);
    BTN(ID_BTN_REPAIR, L"🔧 修复启动", 120);
    
    #undef BTN
    
    HWND hNote = CreateWindowExW(0, L"STATIC",
        L"⚠️ 操作需要管理员权限，修改前建议先备份",
        WS_CHILD | WS_VISIBLE, CONTENT_PADDING, h - 80, w, 24, hParent, NULL, NULL, NULL);
    SendMessageW(hNote, WM_SETFONT, (WPARAM)g_fontSmall, TRUE);
    
    g_hStatusText = CreateWindowExW(0, L"STATIC", L"就绪",
        WS_CHILD | WS_VISIBLE, CONTENT_PADDING, h - 30, w, 24, hParent, (HMENU)ID_STATUS_TEXT, NULL, NULL);
    SendMessageW(g_hStatusText, WM_SETFONT, (WPARAM)g_fontSmall, TRUE);
}

// ============================================
// 关于页面
// ============================================
static void BuildAboutPage(HWND hParent)
{
    RECT rc; GetClientRect(hParent, &rc);
    int w = rc.right;
    int h = rc.bottom;
    int cx = w / 2;
    
    HWND hLogo = CreateWindowExW(0, L"STATIC", L"⚙",
        WS_CHILD | WS_VISIBLE | SS_CENTER, cx - 30, 60, 60, 50,
        hParent, NULL, NULL, NULL);
    SendMessageW(hLogo, WM_SETFONT, (WPARAM)g_fontTitle, TRUE);
    
    HWND hName = CreateWindowExW(0, L"STATIC", L"Boot Manager Pro",
        WS_CHILD | WS_VISIBLE | SS_CENTER, cx - 150, 120, 300, 32,
        hParent, NULL, NULL, NULL);
    SendMessageW(hName, WM_SETFONT, (WPARAM)g_fontTitle, TRUE);
    
    HWND hVer = CreateWindowExW(0, L"STATIC", L"Version 3.0.0",
        WS_CHILD | WS_VISIBLE | SS_CENTER, cx - 150, 155, 300, 24,
        hParent, NULL, NULL, NULL);
    SendMessageW(hVer, WM_SETFONT, (WPARAM)g_fontBody, TRUE);
    
    HWND hDesc = CreateWindowExW(0, L"STATIC",
        L"专业的 UEFI 引导管理工具\n支持启动项管理、rEFInd 安装、系统备份修复",
        WS_CHILD | WS_VISIBLE | SS_CENTER, cx - 200, 220, 400, 50,
        hParent, NULL, NULL, NULL);
    SendMessageW(hDesc, WM_SETFONT, (WPARAM)g_fontBody, TRUE);
    
    HWND hDev = CreateWindowExW(0, L"STATIC", L"Developed by OpenClaw Team",
        WS_CHILD | WS_VISIBLE | SS_CENTER, cx - 150, 320, 300, 24,
        hParent, NULL, NULL, NULL);
    SendMessageW(hDev, WM_SETFONT, (WPARAM)g_fontSmall, TRUE);
    
    HWND hLic = CreateWindowExW(0, L"STATIC", L"MIT License",
        WS_CHILD | WS_VISIBLE | SS_CENTER, cx - 150, 350, 300, 24,
        hParent, NULL, NULL, NULL);
    SendMessageW(hLic, WM_SETFONT, (WPARAM)g_fontSmall, TRUE);
}

// ============================================
// 刷新启动项列表
// ============================================
static void RefreshBootList(void)
{
    if (!g_hListView) return;
    ListView_DeleteAllItems(g_hListView);
    
    if (g_bootList) UefiFreeBootList(g_bootList);
    g_bootList = UefiScanBootEntries();
    
    if (!g_bootList || g_bootList->count == 0) {
        LVITEM lvi = {0}; lvi.mask = LVIF_TEXT;
        lvi.pszText = L"0001"; lvi.iItem = 0;
        ListView_InsertItem(g_hListView, &lvi);
        ListView_SetItemText(g_hListView, 0, 1, L"Windows Boot Manager");
        ListView_SetItemText(g_hListView, 0, 2, L"\\EFI\\Microsoft\\Boot\\bootmgfw.efi");
        return;
    }
    
    UEFI_BOOT_ENTRY* entry = g_bootList->entries;
    int idx = 0;
    while (entry) {
        WCHAR id[16]; swprintf(id, 16, L"%04X", entry->id);
        LVITEM lvi = {0}; lvi.mask = LVIF_TEXT; lvi.iItem = idx; lvi.pszText = id;
        ListView_InsertItem(g_hListView, &lvi);
        ListView_SetItemText(g_hListView, idx, 1, entry->name);
        ListView_SetItemText(g_hListView, idx, 2, entry->filePath);
        entry = entry->next; idx++;
    }
}