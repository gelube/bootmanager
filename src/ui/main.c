/**
 * Boot Manager Pro v3 - Main UI
 * 亮色主题 - 专业高端设计
 * 
 * 设计风格：浅色背景 + 深色文字 + 蓝色主色调
 */

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shlobj.h>
#include <wchar.h>
#include <stdio.h>
#include "dialogs/add_efi_dialog.h"
#include "../core/uefi.h"
#include "../core/refind.h"
#include "../core/refind_config.h"
#include "../core/backup.h"
#include "../../include/esp.h"
#include "../core/wimboot.h"
#include "dialog.h"
#include "../../include/refind_page.h"

#define REFIND_SOURCE_PATH L".\\refind"

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
    ID_LIST_REFIND = 260, ID_BTN_REFIND_REFRESH, ID_BTN_REFIND_DELETE, ID_BTN_ADD_MENU,
    ID_BTN_INSTALL = 300, ID_BTN_UNINSTALL,
    ID_EDIT_BACKUP_PATH = 400, ID_BTN_BROWSE_BKP,
    ID_BTN_BACKUP_MBR, ID_BTN_BACKUP_BCD, ID_BTN_BACKUP_NV,
    ID_BTN_RESTORE, ID_BTN_REPAIR,
    ID_BTN_MBR_REPAIR = 450, ID_BTN_UEFI_REPAIR,
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
static void BuildAdvancedPage(HWND);
static void BuildAboutPage(HWND);
static void RefreshBootList(void);
static void RefreshRefindMenuList(void);
static BOOL ResolveRefindSourcePath(WCHAR* sourcePath, DWORD size);
static BOOL BrowseBackupFile(HWND hWnd, WCHAR* filePath, DWORD size);
static BOOL BrowseBackupFolder(HWND hWnd, WCHAR* folderPath, DWORD size);
static BOOL RestoreFromBackupPath(HWND hWnd, const WCHAR* selectedPath);

// ============================================
// 辅助函数：临时挂载 ESP 分区
// ============================================
static BOOL MountESP(WCHAR* driveLetter)
{
    if (!driveLetter) {
        return FALSE;
    }

    // 统一走 core 层逻辑，避免 UI 层与 core 层挂载行为不一致
    return RefindMountESP(driveLetter, 4);
}

// ============================================
// 辅助函数：卸载临时挂载的 ESP
// ============================================
static void UnmountESP(WCHAR driveLetter)
{
    WCHAR mountedDrive[4] = {driveLetter, L':', L'\0', L'\0'};
    RefindUnmountESP(mountedDrive);
}

static BOOL ResolveRefindSourcePath(WCHAR* sourcePath, DWORD size)
{
    WCHAR exePath[MAX_PATH];
    WCHAR exeDir[MAX_PATH];
    WCHAR cwd[MAX_PATH];

    if (!sourcePath || size == 0) return FALSE;
    if (GetModuleFileNameW(NULL, exePath, MAX_PATH) == 0) return FALSE;

    // 获取程序所在目录
    wcsncpy(exeDir, exePath, MAX_PATH);
    exeDir[MAX_PATH - 1] = L'\0';
    WCHAR* lastSlash = wcsrchr(exeDir, L'\\');
    if (lastSlash) *lastSlash = L'\0';

    // 获取当前工作目录
    GetCurrentDirectoryW(MAX_PATH, cwd);

    // 按优先级搜索 rEFInd 源文件
    // 1. <程序目录>\refind
    // 2. <程序目录>\dist\refind
    // 3. <当前工作目录>\refind
    // 4. <当前工作目录>\dist\refind
    // 5. <程序目录>\..\refind
    // 6. <程序目录>\..\..\refind
    // 7. .\refind (相对于工作目录)
    WCHAR c0[MAX_PATH], c1[MAX_PATH], c2[MAX_PATH], c3[MAX_PATH], c4[MAX_PATH], c5[MAX_PATH], c6[MAX_PATH];
    swprintf(c0, MAX_PATH, L"%s\\refind", exeDir);
    swprintf(c1, MAX_PATH, L"%s\\dist\\refind", exeDir);
    swprintf(c2, MAX_PATH, L"%s\\refind", cwd);
    swprintf(c3, MAX_PATH, L"%s\\dist\\refind", cwd);
    swprintf(c4, MAX_PATH, L"%s\\..\\refind", exeDir);
    swprintf(c5, MAX_PATH, L"%s\\..\\..\\refind", exeDir);
    swprintf(c6, MAX_PATH, L".\\refind");

    const WCHAR* candidates[7] = { c0, c1, c2, c3, c4, c5, c6 };

    {
        WCHAR dbg[MAX_PATH + 64];
        swprintf(dbg, MAX_PATH + 64, L"[rEFInd] exeDir=%s cwd=%s\n", exeDir, cwd);
        OutputDebugStringW(dbg);
    }

    for (int i = 0; i < 7; ++i) {
        WCHAR probe[MAX_PATH];
        swprintf(probe, MAX_PATH, L"%s\\refind_x64.efi", candidates[i]);
        {
            WCHAR dbg[MAX_PATH + 64];
            swprintf(dbg, MAX_PATH + 64, L"[rEFInd] probe[%d]=%s exists=%d\n",
                i, probe, GetFileAttributesW(probe) != INVALID_FILE_ATTRIBUTES);
            OutputDebugStringW(dbg);
        }
        if (GetFileAttributesW(probe) != INVALID_FILE_ATTRIBUTES) {
            wcsncpy(sourcePath, candidates[i], size);
            sourcePath[size - 1] = L'\0';
            {
                WCHAR dbg[MAX_PATH + 64];
                swprintf(dbg, MAX_PATH + 64, L"[rEFInd] Found source at: %s\n", sourcePath);
                OutputDebugStringW(dbg);
            }
            return TRUE;
        }
    }

    OutputDebugStringW(L"[rEFInd] ResolveRefindSourcePath: not found in any candidate\n");
    return FALSE;
}

static BOOL BrowseBackupFile(HWND hWnd, WCHAR* filePath, DWORD size)
{
    OPENFILENAMEW ofn = {0};

    if (!filePath || size == 0) {
        return FALSE;
    }

    filePath[0] = L'\0';
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hWnd;
    ofn.lpstrFile = filePath;
    ofn.nMaxFile = size;
    ofn.lpstrFilter = L"Backup Files (*.bak;*.bin)\0*.bak;*.bin\0All Files (*.*)\0*.*\0";
    ofn.lpstrTitle = L"选择备份文件";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;

    return GetOpenFileNameW(&ofn);
}

static BOOL BrowseBackupFolder(HWND hWnd, WCHAR* folderPath, DWORD size)
{
    BROWSEINFOW bi = {0};
    PIDLIST_ABSOLUTE pidl;

    if (!folderPath || size == 0) {
        return FALSE;
    }

    folderPath[0] = L'\0';
    bi.hwndOwner = hWnd;
    bi.lpszTitle = L"选择备份目录";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    pidl = SHBrowseForFolderW(&bi);
    if (!pidl) {
        return FALSE;
    }

    if (!SHGetPathFromIDListW(pidl, folderPath)) {
        CoTaskMemFree(pidl);
        return FALSE;
    }

    CoTaskMemFree(pidl);
    return TRUE;
}

static BOOL RestoreFromBackupPath(HWND hWnd, const WCHAR* selectedPath)
{
    const WCHAR* ext;
    DWORD attrs;

    if (!selectedPath || selectedPath[0] == L'\0') {
        return FALSE;
    }

    attrs = GetFileAttributesW(selectedPath);
    if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY)) {
        return RestoreAll(selectedPath, BACKUP_ALL);
    }

    ext = wcsrchr(selectedPath, L'.');
    if (ext && _wcsicmp(ext, L".bin") == 0) {
        return RestoreMBR(L"PhysicalDrive0", selectedPath);
    }

    if (wcsstr(selectedPath, L"NVRAM_") != NULL || wcsstr(selectedPath, L"nvram") != NULL) {
        return RestoreNVRAM(selectedPath);
    }

    if (wcsstr(selectedPath, L"BCD_") != NULL || wcsstr(selectedPath, L"bcd") != NULL || (ext && _wcsicmp(ext, L".bak") == 0)) {
        return RestoreBCD(selectedPath);
    }

    MessageBoxW(hWnd, L"无法判断备份类型，请选择 .bin(MBR) 或 .bak(BCD/NVRAM) 文件，或包含 mbr.bin/bcd.bak/nvram.bak 的备份目录。", L"提示", MB_OK | MB_ICONWARNING);
    return FALSE;
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
            // 新进程已启动，退出当前进程
            return 0;
        }
        
        // UAC 提升失败（用户拒绝或其他原因）
        // 继续运行但会提示用户操作受限
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

        case ID_BTN_REFIND_REFRESH:
            RefindPageRefresh();
            SetStatus(L"✓ rEFInd 条目已刷新");
            break;
        
        // 添加启动项 - 下拉菜单（仅 EFI）
        case ID_BTN_ADD_ENTRY: {
            // 直接显示添加 EFI 对话框
            WCHAR title[256] = {0};
            WCHAR path[512] = {0};
            WCHAR driveLetter[4] = {0};

            if (!ShowAddEfiDialog(hWnd, title, path, driveLetter)) {
                break;
            }

            if (wcslen(title) == 0 || wcslen(path) == 0) {
                MessageBoxW(hWnd, L"请填写完整信息", L"提示", MB_OK | MB_ICONWARNING);
                break;
            }

            SetStatus(L"正在添加 EFI 启动项...");

            WCHAR fullPath[512] = {0};
            WCHAR mountedEsp[4] = {0};
            BOOL espMountedForAdd = FALSE;
            WCHAR devicePath[32] = {0};

            if (path[0] == L'\\' && path[1] == L'\\') {
                MessageBoxW(hWnd, L"不支持 UNC 网络路径\n\n请选择本地 EFI 文件", L"错误", MB_OK | MB_ICONERROR);
                SetStatus(L"✗ 添加失败：路径格式错误");
                break;
            } else if (path[1] == L':') {
                wcsncpy(fullPath, path, 511);
                swprintf(devicePath, 32, L"partition=%c:", path[0]);
            } else if (path[0] == L'\\') {
                if (wcslen(driveLetter) >= 2) {
                    swprintf(fullPath, 512, L"%s%s", driveLetter, path);
                    swprintf(devicePath, 32, L"partition=%c:", driveLetter[0]);
                } else {
                    if (!MountESP(mountedEsp)) {
                        MessageBoxW(hWnd,
                            L"ESP 分区挂载失败\n\n"
                            L"可能原因：\n"
                            L"1. 程序未以管理员身份运行\n"
                            L"2. 系统未使用 UEFI 启动模式\n\n"
                            L"请右键程序选择「以管理员身份运行」",
                            L"错误", MB_OK | MB_ICONERROR);
                        SetStatus(L"✗ 添加失败：挂载失败");
                        break;
                    }
                    espMountedForAdd = TRUE;
                    swprintf(fullPath, 512, L"%s%s", mountedEsp, path);
                    swprintf(devicePath, 32, L"partition=%c:", mountedEsp[0]);
                }
            } else {
                wcsncpy(fullPath, path, 511);
            }

            fullPath[511] = L'\0';

            if (GetFileAttributesW(fullPath) == INVALID_FILE_ATTRIBUTES) {
                DWORD err = GetLastError();
                WCHAR errMsg[640];
                swprintf(errMsg, 640, L"EFI 文件不存在\n\n路径：%s\n错误码：%lu\n\n请确认文件路径正确。", fullPath, err);
                MessageBoxW(hWnd, errMsg, L"错误", MB_OK | MB_ICONERROR);
                SetStatus(L"✗ 添加失败：文件不存在");
                if (espMountedForAdd) UnmountESP(mountedEsp[0]);
                break;
            }

            DWORD newId = 0;
            BOOL addOk = UefiAddBootEntry(title, devicePath, fullPath, &newId);

            if (espMountedForAdd) UnmountESP(mountedEsp[0]);

            if (addOk) {
                WCHAR msg[768];
                swprintf(msg, 768,
                    L"✓ EFI 启动项添加成功！\n\n"
                    L"名称：%s\n"
                    L"路径：%s\n\n"
                    L"启动项已添加到 UEFI 启动列表，\n"
                    L"可在 BIOS/UEFI 设置中调整启动顺序。",
                    title, fullPath);
                MessageBoxW(hWnd, msg, L"添加成功", MB_OK | MB_ICONINFORMATION);
                SetStatus(L"✓ 启动项已添加到 UEFI 列表");
                RefreshBootList();
            } else {
                DWORD err = GetLastError();
                WCHAR errMsg[512];
                swprintf(errMsg, 512,
                    L"添加 EFI 启动项失败\n\n"
                    L"错误代码：%lu\n\n"
                    L"可能原因：\n"
                    L"1. 程序未以管理员身份运行\n"
                    L"2. BCD 存储损坏\n"
                    L"3. 路径格式不正确\n\n"
                    L"请右键程序选择「以管理员身份运行」",
                    err);
                MessageBoxW(hWnd, errMsg, L"错误", MB_OK | MB_ICONERROR);
                SetStatus(L"✗ 添加失败");
            }
            break;
        }
        
        // 第二个模块的添加菜单
        case ID_BTN_ADD_MENU:
            RefindPageShowAddMenu(hWnd);
            break;
        
        case ID_MENU_ADD_WIM:
            RefindPageAddWim(hWnd);
            break;
        
        case ID_MENU_ADD_VHD:
            RefindPageAddVhd(hWnd);
            break;
            
        case ID_BTN_INSTALL: {
            SetStatus(L"⏳ 正在安装 rEFInd...");
            UpdateWindow(hWnd);

            // 检查源文件
            WCHAR resolvedSourcePath[MAX_PATH];
            if (!ResolveRefindSourcePath(resolvedSourcePath, MAX_PATH)) {
                MessageBoxW(hWnd, 
                    L"未找到 rEFInd 源文件\n\n"
                    L"请将 rEFInd 文件夹放入程序目录，\n"
                    L"确保包含 refind_x64.efi 文件。",
                    L"错误", MB_OK | MB_ICONERROR);
                SetStatus(L"✗ 源文件缺失");
                break;
            }

            // 安装（内部自动处理 ESP 挂载）
            BOOL success = RefindInstall(resolvedSourcePath, NULL);

            if (success) {
                MessageBoxW(hWnd, 
                    L"rEFInd 安装成功！\n\n"
                    L"重启后将显示 rEFInd 启动菜单。",
                    L"安装完成", MB_OK | MB_ICONINFORMATION);
                SetStatus(L"✓ rEFInd 安装成功");
            } else {
                const WCHAR* err = RefindGetLastErrorMessage();
                WCHAR failMsg[1024];
                swprintf(failMsg, 1024, 
                    L"安装失败\n\n"
                    L"错误: %s\n\n"
                    L"请确保以管理员身份运行本程序。",
                    (err && wcslen(err) > 0) ? err : L"未知错误");
                MessageBoxW(hWnd, failMsg, L"错误", MB_OK | MB_ICONERROR);
                SetStatus(L"✗ 安装失败");
            }
            break;
        }
        
        case ID_BTN_UNINSTALL: {
            if (MessageBoxW(hWnd, L"确定卸载 rEFInd？\n\n将恢复 Windows 引导。", L"确认", MB_YESNO | MB_ICONQUESTION) != IDYES)
                break;
            
            SetStatus(L"⏳ 正在卸载...");
            UpdateWindow(hWnd);

            // 卸载（内部自动处理 ESP 挂载）
            BOOL success = RefindUninstall(NULL);
            
            if (success) {
                MessageBoxW(hWnd, L"rEFInd 已卸载\n\n将恢复 Windows 引导。", L"完成", MB_OK | MB_ICONINFORMATION);
                SetStatus(L"✓ 卸载成功");
            } else {
                const WCHAR* err = RefindGetLastErrorMessage();
                WCHAR failMsg[1024];
                swprintf(failMsg, 1024, L"卸载失败\n\n错误: %s", (err && wcslen(err) > 0) ? err : L"未知错误");
                MessageBoxW(hWnd, failMsg, L"错误", MB_OK | MB_ICONERROR);
                SetStatus(L"✗ 卸载失败");
            }
            break;
        }

        case ID_BTN_REFIND_DELETE:
            RefindPageDeleteSelected(hWnd);
            break;
        
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
        
        case ID_BTN_RESTORE: {
            WCHAR selectedPath[MAX_PATH] = {0};
            int pickMode = MessageBoxW(hWnd, L"选择要恢复的备份来源：\n\n是：选择单个备份文件\n否：选择完整备份目录", L"恢复备份", MB_YESNOCANCEL | MB_ICONQUESTION);

            if (pickMode == IDCANCEL) {
                break;
            }

            if (pickMode == IDYES) {
                if (!BrowseBackupFile(hWnd, selectedPath, MAX_PATH)) {
                    break;
                }
            } else {
                if (!BrowseBackupFolder(hWnd, selectedPath, MAX_PATH)) {
                    break;
                }
            }

            if (MessageBoxW(hWnd, L"恢复引导备份可能直接影响系统启动，确定继续？", L"确认恢复", MB_YESNO | MB_ICONWARNING) != IDYES) {
                break;
            }

            SetStatus(L"⏳ 正在恢复备份...");
            if (RestoreFromBackupPath(hWnd, selectedPath)) {
                WCHAR msg[MAX_PATH + 64];
                swprintf(msg, MAX_PATH + 64, L"恢复成功:\n%s", selectedPath);
                MessageBoxW(hWnd, msg, L"完成", MB_OK | MB_ICONINFORMATION);
                SetStatus(L"✓ 恢复完成");
            } else {
                MessageBoxW(hWnd, L"恢复失败，请确认备份文件存在且已使用管理员权限运行。", L"错误", MB_OK | MB_ICONERROR);
                SetStatus(L"✗ 恢复失败");
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

        case ID_BTN_MBR_REPAIR: {
            if (MessageBoxW(hWnd,
                L"⚠️ 高风险操作：MBR 修复\n\n"
                L"将使用 bootrec /fixmbr 恢复 Windows 默认 MBR 引导代码。\n"
                L"分区表不会被修改，但当前 MBR 引导代码将被覆盖。\n\n"
                L"建议操作前先备份 MBR。确定继续？",
                L"确认 MBR 修复", MB_YESNO | MB_ICONWARNING) != IDYES) break;

            SetStatus(L"⏳ 正在修复 MBR...");
            UpdateWindow(hWnd);

            SHELLEXECUTEINFOW sei = {0};
            sei.cbSize = sizeof(sei);
            sei.fMask = SEE_MASK_NOCLOSEPROCESS;
            sei.lpVerb = L"runas";
            sei.lpFile = L"cmd.exe";
            sei.lpParameters = L"/c bootrec /fixmbr";
            sei.nShow = SW_HIDE;

            if (ShellExecuteExW(&sei)) {
                WaitForSingleObject(sei.hProcess, 30000);
                DWORD exitCode = 1;
                GetExitCodeProcess(sei.hProcess, &exitCode);
                CloseHandle(sei.hProcess);
                if (exitCode == 0) {
                    MessageBoxW(hWnd, L"MBR 修复完成\n\n重启后生效。", L"完成", MB_OK | MB_ICONINFORMATION);
                    SetStatus(L"✓ MBR 已修复");
                } else {
                    MessageBoxW(hWnd, L"MBR 修复失败\n\n请确保以管理员身份运行，或在 Windows PE 环境下执行。", L"错误", MB_OK | MB_ICONERROR);
                    SetStatus(L"✗ MBR 修复失败");
                }
            } else {
                MessageBoxW(hWnd, L"无法启动修复进程，请确保以管理员身份运行。", L"错误", MB_OK | MB_ICONERROR);
                SetStatus(L"✗ 启动失败");
            }
            break;
        }

        case ID_BTN_UEFI_REPAIR: {
            if (MessageBoxW(hWnd,
                L"UEFI 引导修复\n\n"
                L"将检查 ESP 分区并尝试恢复 Windows 默认 UEFI 引导链：\n"
                L"• 执行 bootrec /fixboot\n"
                L"• 执行 bcdboot C:\\Windows /s <ESP> /f UEFI\n\n"
                L"如果 Windows 安装在非 C: 盘，修复可能不完整。\n"
                L"确定继续？",
                L"确认 UEFI 修复", MB_YESNO | MB_ICONQUESTION) != IDYES) break;

            SetStatus(L"⏳ 正在修复 UEFI 引导...");
            UpdateWindow(hWnd);

            /* Step 1: mount ESP */
            WCHAR espDrive[4] = {0};
            BOOL espMounted = EspMount(espDrive, 4);

            /* Step 2: bootrec /fixboot */
            BOOL fixbootOk = FALSE;
            {
                SHELLEXECUTEINFOW sei = {0};
                sei.cbSize = sizeof(sei);
                sei.fMask = SEE_MASK_NOCLOSEPROCESS;
                sei.lpVerb = L"runas";
                sei.lpFile = L"cmd.exe";
                sei.lpParameters = L"/c bootrec /fixboot";
                sei.nShow = SW_HIDE;
                if (ShellExecuteExW(&sei)) {
                    WaitForSingleObject(sei.hProcess, 30000);
                    DWORD ec = 1; GetExitCodeProcess(sei.hProcess, &ec);
                    CloseHandle(sei.hProcess);
                    fixbootOk = (ec == 0);
                }
            }

            /* Step 3: bcdboot to restore EFI\Microsoft\Boot */
            BOOL bcdbootOk = FALSE;
            if (espMounted && espDrive[0]) {
                WCHAR params[128];
                swprintf(params, 128, L"/c bcdboot C:\\Windows /s %c: /f UEFI", espDrive[0]);
                SHELLEXECUTEINFOW sei = {0};
                sei.cbSize = sizeof(sei);
                sei.fMask = SEE_MASK_NOCLOSEPROCESS;
                sei.lpVerb = L"runas";
                sei.lpFile = L"cmd.exe";
                sei.lpParameters = params;
                sei.nShow = SW_HIDE;
                if (ShellExecuteExW(&sei)) {
                    WaitForSingleObject(sei.hProcess, 30000);
                    DWORD ec = 1; GetExitCodeProcess(sei.hProcess, &ec);
                    CloseHandle(sei.hProcess);
                    bcdbootOk = (ec == 0);
                }
                EspUnmount(espDrive);
            }

            if (fixbootOk || bcdbootOk) {
                MessageBoxW(hWnd,
                    L"UEFI 引导修复完成\n\n"
                    L"如果问题仍然存在，请使用 Windows 安装盘进入修复模式，\n"
                    L"选择「修复计算机」→「疑难解答」→「启动修复」。",
                    L"完成", MB_OK | MB_ICONINFORMATION);
                SetStatus(L"✓ UEFI 引导已修复");
            } else {
                MessageBoxW(hWnd,
                    L"UEFI 修复未能完全完成\n\n"
                    L"手动修复方法：\n"
                    L"1. 使用 Windows 安装盘启动\n"
                    L"2. 选择「修复计算机」→「疑难解答」→「命令提示符」\n"
                    L"3. 执行：bootrec /fixboot\n"
                    L"4. 执行：bcdboot C:\\Windows /s <ESP盘符>: /f UEFI",
                    L"提示", MB_OK | MB_ICONWARNING);
                SetStatus(L"⚠ 请参考手动修复指引");
            }
            break;
        }
        
        // UEFI 启动项管理
        case ID_BTN_DELETE_ENTRY: {
            if (!g_hListView) break;
            int sel = ListView_GetNextItem(g_hListView, -1, LVNI_SELECTED);
            if (sel == -1) {
                MessageBoxW(hWnd, L"请先选择一个启动项", L"提示", MB_OK | MB_ICONINFORMATION);
                break;
            }
            
            // 从第 1 列读取 ID（第 0 列是序号）
            WCHAR idText[16] = {0};
            ListView_GetItemText(g_hListView, sel, 1, idText, 16);
            DWORD id = wcstoul(idText, NULL, 16);
            
            // 从第 2 列读取名称
            WCHAR name[256] = {0};
            ListView_GetItemText(g_hListView, sel, 2, name, 256);
            
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
        
        case ID_BTN_MOVE_UP:
        case ID_BTN_MOVE_DOWN: {
            if (!g_hListView || !g_bootList) break;
            int sel = ListView_GetNextItem(g_hListView, -1, LVNI_SELECTED);
            if (sel == -1) {
                MessageBoxW(hWnd, L"请先选择一个启动项", L"提示", MB_OK | MB_ICONINFORMATION);
                break;
            }

            // 从第 1 列读取 ID（第 0 列是序号）
            WCHAR idText[16] = {0};
            ListView_GetItemText(g_hListView, sel, 1, idText, 16);
            DWORD id = wcstoul(idText, NULL, 16);

            SetStatus((LOWORD(wParam) == ID_BTN_MOVE_UP) ? L"⏳ 正在上移..." : L"⏳ 正在下移...");
            BOOL ok = (LOWORD(wParam) == ID_BTN_MOVE_UP)
                ? UefiMoveBootEntryUp(g_bootList, id)
                : UefiMoveBootEntryDown(g_bootList, id);

            if (ok) {
                SetStatus(L"✓ BootOrder 已更新");
                RefreshBootList();
                // 选中移动后的项
                int newSel = (LOWORD(wParam) == ID_BTN_MOVE_UP) ? sel - 1 : sel + 1;
                if (newSel >= 0 && newSel < ListView_GetItemCount(g_hListView)) {
                    ListView_SetItemState(g_hListView, newSel, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
                    ListView_EnsureVisible(g_hListView, newSel, FALSE);
                }
            } else {
                MessageBoxW(hWnd, L"调整顺序失败\n\n请确保以管理员身份运行", L"错误", MB_OK | MB_ICONERROR);
                SetStatus(L"✗ 调整失败");
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
            
            // 从第 1 列读取 ID（第 0 列是序号）
            WCHAR idText[16] = {0};
            ListView_GetItemText(g_hListView, sel, 1, idText, 16);
            DWORD id = wcstoul(idText, NULL, 16);
            
            // 从第 2 列读取名称
            WCHAR name[256] = {0};
            ListView_GetItemText(g_hListView, sel, 2, name, 256);
            
            SetStatus(L"⏳ 正在设置默认...");
            if (UefiSetDefaultBootEntry(g_bootList, id)) {
                WCHAR msg[256];
                swprintf(msg, 256, L"已设置默认启动项:\n%s", name);
                MessageBoxW(hWnd, msg, L"完成", MB_OK | MB_ICONINFORMATION);
                SetStatus(L"✓ 已设为默认");
                RefreshBootList();
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
        { ID_NAV_UEFI, L"启动项管理" },
        { ID_NAV_REFIND, L"rEFInd 管理" },
        { ID_NAV_BACKUP, L"高级功能" },
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
// 内容面板子类化窗口过程 - 转发 WM_COMMAND 到主窗口
static LRESULT CALLBACK ContentWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_COMMAND) {
        // 转发到主窗口
        SendMessageW(g_hMainWnd, WM_COMMAND, wParam, lParam);
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

static WNDPROC g_oldContentProc = NULL;

static void CreateContentArea(HWND hWnd)
{
    g_hContent = CreateWindowExW(WS_EX_CONTROLPARENT, L"STATIC", NULL,
        WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
        SIDEBAR_WIDTH, 0, WINDOW_WIDTH - SIDEBAR_WIDTH, WINDOW_HEIGHT,
        hWnd, (HMENU)999, NULL, NULL);
    
    // 子类化内容面板，转发 WM_COMMAND 到主窗口
    g_oldContentProc = (WNDPROC)SetWindowLongPtrW(g_hContent, GWLP_WNDPROC, (LONG_PTR)ContentWndProc);
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
        case 2: BuildAdvancedPage(g_hContent); break;
        case 3: BuildAboutPage(g_hContent); break;
    }
}

// ============================================
// UEFI 页面
// ============================================
static void BuildUEFIPage(HWND hParent)
{
    RECT rc; GetClientRect(hParent, &rc);
    int w = rc.right - CONTENT_PADDING * 2;
    int h = rc.bottom;

    HWND hTitle = CreateWindowExW(0, L"STATIC", L"启动项管理",
        WS_CHILD | WS_VISIBLE, CONTENT_PADDING, CONTENT_PADDING, w, 32,
        hParent, NULL, NULL, NULL);
    SendMessageW(hTitle, WM_SETFONT, (WPARAM)g_fontTitle, TRUE);

    g_hListView = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEW, NULL,
        WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL,
        CONTENT_PADDING, CONTENT_PADDING + 50, w, h - 200,
        hParent, (HMENU)ID_LIST_BOOT, NULL, NULL);
    ListView_SetExtendedListViewStyle(g_hListView, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);

    LVCOLUMN lvc = {0}; lvc.mask = LVCF_TEXT | LVCF_WIDTH;
    lvc.pszText = L"序号"; lvc.cx = 50; ListView_InsertColumn(g_hListView, 0, &lvc);
    lvc.pszText = L"ID"; lvc.cx = 70; ListView_InsertColumn(g_hListView, 1, &lvc);
    lvc.pszText = L"名称"; lvc.cx = 220; ListView_InsertColumn(g_hListView, 2, &lvc);
    lvc.pszText = L"路径"; lvc.cx = w - 360; ListView_InsertColumn(g_hListView, 3, &lvc);

    RefreshBootList();

    int bx = CONTENT_PADDING, by = h - 130;
    #define BTN(id, text, wd) CreateWindowExW(0, L"BUTTON", text, WS_CHILD | WS_VISIBLE, bx, by, wd, BTN_HEIGHT, hParent, (HMENU)id, NULL, NULL); SendMessageW(GetDlgItem(hParent, id), WM_SETFONT, (WPARAM)g_fontBody, TRUE); bx += wd + 10;

    BTN(ID_BTN_REFRESH, L"🔄 刷新", 90);
    BTN(ID_BTN_ADD_ENTRY, L"➕ 添加 EFI", 120);
    BTN(ID_BTN_DELETE_ENTRY, L"🗑 删除", 90);
    BTN(ID_BTN_MOVE_UP, L"↑ 上移", 70);
    BTN(ID_BTN_MOVE_DOWN, L"↓ 下移", 70);
    BTN(ID_BTN_SET_DEFAULT, L"⭐ 设为默认", 110);

    #undef BTN

    g_hStatusText = CreateWindowExW(0, L"STATIC", L"就绪",
        WS_CHILD | WS_VISIBLE, CONTENT_PADDING, h - 30, w, 24, hParent, (HMENU)ID_STATUS_TEXT, NULL, NULL);
    SendMessageW(g_hStatusText, WM_SETFONT, (WPARAM)g_fontSmall, TRUE);
}

// ============================================
// rEFInd 页面 - 委托给 refind_page 模块
// ============================================
static void BuildRefindPage(HWND hParent)
{
    RefindPageBuild(hParent, g_fontTitle, g_fontBody, g_fontSmall);
}

static void RefreshRefindMenuList(void)
{
    RefindPageRefresh();
}
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
// 高级功能页面
// ============================================
static void BuildAdvancedPage(HWND hParent)
{
    RECT rc; GetClientRect(hParent, &rc);
    int w = rc.right - CONTENT_PADDING * 2;
    int h = rc.bottom;

    HWND hTitle = CreateWindowExW(0, L"STATIC", L"高级功能",
        WS_CHILD | WS_VISIBLE, CONTENT_PADDING, CONTENT_PADDING, w, 32,
        hParent, NULL, NULL, NULL);
    SendMessageW(hTitle, WM_SETFONT, (WPARAM)g_fontTitle, TRUE);

    /* MBR 修复卡片 */
    HWND hCard1 = CreateWindowExW(0, L"BUTTON", L" MBR 修复",
        WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
        CONTENT_PADDING, CONTENT_PADDING + 50, w, 130,
        hParent, NULL, NULL, NULL);
    SendMessageW(hCard1, WM_SETFONT, (WPARAM)g_fontBody, TRUE);

    HWND hDesc1 = CreateWindowExW(0, L"STATIC",
        L"恢复 Windows 默认 MBR 引导代码（前 446 字节），保留分区表不变。\n"
        L"适用于 MBR 被第三方引导程序覆盖的情况。",
        WS_CHILD | WS_VISIBLE,
        CONTENT_PADDING + 16, CONTENT_PADDING + 78, w - 32, 36,
        hParent, NULL, NULL, NULL);
    SendMessageW(hDesc1, WM_SETFONT, (WPARAM)g_fontSmall, TRUE);

    HWND hBtnMbr = CreateWindowExW(0, L"BUTTON", L"🔧 修复 MBR",
        WS_CHILD | WS_VISIBLE,
        CONTENT_PADDING + 16, CONTENT_PADDING + 122, 130, BTN_HEIGHT,
        hParent, (HMENU)ID_BTN_MBR_REPAIR, NULL, NULL);
    SendMessageW(hBtnMbr, WM_SETFONT, (WPARAM)g_fontBody, TRUE);

    /* UEFI 修复卡片 */
    HWND hCard2 = CreateWindowExW(0, L"BUTTON", L" UEFI 引导修复",
        WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
        CONTENT_PADDING, CONTENT_PADDING + 200, w, 150,
        hParent, NULL, NULL, NULL);
    SendMessageW(hCard2, WM_SETFONT, (WPARAM)g_fontBody, TRUE);

    HWND hDesc2 = CreateWindowExW(0, L"STATIC",
        L"恢复 Windows 默认 UEFI 引导链：检查 ESP 分区，重建 \\EFI\\Microsoft\\Boot\\ 目录，\n"
        L"并通过 bcdboot 恢复 bootmgfw.efi。如无法自动修复，将提供手动修复指引。",
        WS_CHILD | WS_VISIBLE,
        CONTENT_PADDING + 16, CONTENT_PADDING + 228, w - 32, 40,
        hParent, NULL, NULL, NULL);
    SendMessageW(hDesc2, WM_SETFONT, (WPARAM)g_fontSmall, TRUE);

    HWND hBtnUefi = CreateWindowExW(0, L"BUTTON", L"🔧 修复 UEFI 引导",
        WS_CHILD | WS_VISIBLE,
        CONTENT_PADDING + 16, CONTENT_PADDING + 276, 150, BTN_HEIGHT,
        hParent, (HMENU)ID_BTN_UEFI_REPAIR, NULL, NULL);
    SendMessageW(hBtnUefi, WM_SETFONT, (WPARAM)g_fontBody, TRUE);

    HWND hWarn = CreateWindowExW(0, L"STATIC",
        L"⚠️ 以上操作均为高风险操作，执行前请确保已备份重要数据，并以管理员身份运行本程序。",
        WS_CHILD | WS_VISIBLE, CONTENT_PADDING, h - 80, w, 24, hParent, NULL, NULL, NULL);
    SendMessageW(hWarn, WM_SETFONT, (WPARAM)g_fontSmall, TRUE);

    g_hStatusText = CreateWindowExW(0, L"STATIC", L"就绪",
        WS_CHILD | WS_VISIBLE, CONTENT_PADDING, h - 30, w, 24,
        hParent, (HMENU)ID_STATUS_TEXT, NULL, NULL);
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
    
    HWND hVer = CreateWindowExW(0, L"STATIC", L"Version 3.2.0",
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
    g_bootList = NULL;

    // 扫描 UEFI 启动项（纯 NVRAM API）
    g_bootList = UefiScanBootEntries();
    if (!g_bootList || g_bootList->count == 0) {
        if (g_bootList) {
            UefiFreeBootList(g_bootList);
            g_bootList = NULL;
        }
        Sleep(120);
        g_bootList = UefiScanBootEntries();
    }

    if (!g_bootList || g_bootList->count == 0) {
        // 显示提示信息
        LVITEM lvi = {0}; lvi.mask = LVIF_TEXT;
        lvi.pszText = L"-"; lvi.iItem = 0;
        ListView_InsertItem(g_hListView, &lvi);
        ListView_SetItemText(g_hListView, 0, 1, L"未检测到 UEFI 启动项");
        ListView_SetItemText(g_hListView, 0, 2, L"请确保以管理员身份运行");
        return;
    }
    
    UEFI_BOOT_ENTRY_WRAPPER* entry = g_bootList->entries;
    int idx = 0;
    while (entry) {
        // 序号列（启动顺序）
        WCHAR order[8]; swprintf(order, 8, L"%d", idx + 1);
        LVITEM lvi = {0}; lvi.mask = LVIF_TEXT; lvi.iItem = idx; lvi.pszText = order;
        ListView_InsertItem(g_hListView, &lvi);
        
        // ID 列
        WCHAR id[16]; swprintf(id, 16, L"%04X", entry->id);
        ListView_SetItemText(g_hListView, idx, 1, id);
        
        // 名称列
        ListView_SetItemText(g_hListView, idx, 2, entry->name);
        
        // 路径列
        ListView_SetItemText(g_hListView, idx, 3, entry->filePath);
        
        entry = entry->next; idx++;
    }
}