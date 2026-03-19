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

// 颜色
#define COLOR_BG_MAIN       RGB(250, 250, 252)
#define COLOR_TEXT_PRIMARY  RGB(17, 24, 39)

// 尺寸
#define WINDOW_WIDTH        1000
#define WINDOW_HEIGHT       720
#define SIDEBAR_WIDTH       220
#define NAV_ITEM_HEIGHT     48
#define BTN_HEIGHT          38
#define TAB_HEIGHT          28

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

// 全局变量
static HWND g_hMainWnd = NULL, g_hContent = NULL, g_hListView = NULL, g_hStatusText = NULL;
static int g_currentPage = 0;
static int g_currentThirdPartyTab = 0;
static HFONT g_fontTitle = NULL, g_fontBody = NULL, g_fontSmall = NULL;
static HWND g_navItems[4] = {0};
static UEFI_BOOT_LIST* g_bootList = NULL;

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
    wc.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
    RegisterClassW(&wc);
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

static void InitFonts(void)
{
    g_fontTitle = CreateFontW(20, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
    g_fontBody = CreateFontW(14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
    g_fontSmall = CreateFontW(12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
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
    // 枚举分区
    ESP_PARTITION_INFO* partitions = NULL;
    int count = EnumEspPartitions(&partitions);
    
    if (count == 0) {
        MessageBoxW(hParent, L"未找到 FAT 格式分区\n\nESP 分区通常是 FAT16 或 FAT32 格式。\n如果 ESP 已挂载，请检查是否有 EFI 目录。", L"提示", MB_OK | MB_ICONWARNING);
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
        SwitchPage(0);
        return 0;
        
    case WM_CTLCOLORSTATIC: {
        HDC hdc = (HDC)wParam;
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, COLOR_TEXT_PRIMARY);
        return (LRESULT)CreateSolidBrush(COLOR_BG_MAIN);
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
                if (UefiDeleteBootEntry(id)) { RefreshBootList(); SetStatus(L"✓ 已删除"); }
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
            break;
        }
        
        case ID_BTN_SET_DEFAULT: {
            if (!g_hListView) break;
            int sel = ListView_GetNextItem(g_hListView, -1, LVNI_SELECTED);
            if (sel == -1) { MessageBoxW(hWnd, L"请先选择", L"提示", MB_OK); break; }
            
            WCHAR idText[16] = {0};
            ListView_GetItemText(g_hListView, sel, 0, idText, 16);
            DWORD id = wcstoul(idText, NULL, 16);
            
            if (UefiSetDefaultBootEntry(g_bootList, id)) MessageBoxW(hWnd, L"已设为默认", L"完成", MB_OK);
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
            break;
        }
        
        case ID_BTN_UNINSTALL_LIMINE: {
            if (MessageBoxW(hWnd, L"确定卸载 Limine？", L"确认", MB_YESNO) != IDYES) break;
            
            // 让用户选择 ESP 分区
            WCHAR espDrive = ShowEspSelectDialog(hWnd);
            if (espDrive == 0) break;
            
            WCHAR esp[4] = {espDrive, L':', 0};
            
            LimineUninstall(esp);
            
            MessageBoxW(hWnd, L"已卸载", L"完成", MB_OK);
            if (s_limineBtnInstall) EnableWindow(s_limineBtnInstall, TRUE);
            if (s_limineBtnUninstall) EnableWindow(s_limineBtnUninstall, FALSE);
            if (s_limineBtnAddEntry) EnableWindow(s_limineBtnAddEntry, FALSE);
            if (s_limineBtnEditEntry) EnableWindow(s_limineBtnEditEntry, FALSE);
            if (s_limineBtnDelEntry) EnableWindow(s_limineBtnDelEntry, FALSE);
            if (s_limineStatus) SetWindowTextW(s_limineStatus, L"Limine 未安装");
            if (s_limineList) ListView_DeleteAllItems(s_limineList);
            s_limineEntryCount = 0;
            s_limineConfDir[0] = L'\0';
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
            if (RestoreMBR(L"PhysicalDrive0", file)) {
                MessageBoxW(hWnd, L"恢复成功", L"完成", MB_OK);
            } else {
                MessageBoxW(hWnd, L"恢复失败", L"错误", MB_OK | MB_ICONERROR);
            }
            break;
        }
        
        case ID_BTN_MBR_REPAIR: {
            if (MessageBoxW(hWnd, L"确定修复 MBR？", L"确认", MB_YESNO) != IDYES) break;
            SHELLEXECUTEINFOW sei = {0};
            sei.cbSize = sizeof(sei);
            sei.lpVerb = L"runas";
            sei.lpFile = L"cmd.exe";
            sei.lpParameters = L"/c bootrec /fixmbr";
            sei.nShow = SW_HIDE;
            if (ShellExecuteExW(&sei)) {
                WaitForSingleObject(sei.hProcess, 30000);
                DWORD code;
                GetExitCodeProcess(sei.hProcess, &code);
                CloseHandle(sei.hProcess);
                MessageBoxW(hWnd, code == 0 ? L"修复完成" : L"修复失败", code == 0 ? L"完成" : L"错误", MB_OK);
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
            if (!BootMode_IsUEFIFirmware()) {
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
            
            // 使用 bcdboot 修复
            SHELLEXECUTEINFOW sei = {0};
            sei.cbSize = sizeof(sei);
            sei.lpVerb = L"runas";
            sei.lpFile = L"cmd.exe";
            sei.lpParameters = L"/c bcdboot C:\\Windows /l zh-cn /s S: /f UEFI";
            sei.nShow = SW_HIDE;
            
            if (ShellExecuteExW(&sei)) {
                WaitForSingleObject(sei.hProcess, 30000);
                DWORD code;
                GetExitCodeProcess(sei.hProcess, &code);
                CloseHandle(sei.hProcess);
                
                if (code == 0) {
                    MessageBoxW(hWnd, L"UEFI 引导修复成功\n\n如果启动项仍未显示，请重启后检查 BIOS 设置", L"完成", MB_OK);
                } else {
                    MessageBoxW(hWnd, L"UEFI 引导修复失败\n\n可能需要手动检查 ESP 分区", L"错误", MB_OK | MB_ICONERROR);
                }
            }
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
                SwitchPage(1);
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
    CreateWindowExW(0, L"STATIC", NULL, WS_CHILD | WS_VISIBLE, 0, 0, SIDEBAR_WIDTH, WINDOW_HEIGHT, hWnd, NULL, NULL, NULL);
    
    HWND hLogo = CreateWindowExW(0, L"STATIC", L"⚙ Boot Manager", WS_CHILD | WS_VISIBLE, 24, 24, SIDEBAR_WIDTH - 48, 32, hWnd, NULL, NULL, NULL);
    SendMessageW(hLogo, WM_SETFONT, (WPARAM)g_fontTitle, TRUE);
    
    struct { int id; const WCHAR* text; } items[] = {
        { ID_NAV_BOOT_MGR, L"引导管理" },
        { ID_NAV_THIRD_PARTY, L"第三方引导管理器" },
        { ID_NAV_BACKUP_RESTORE, L"备份恢复" },
        { ID_NAV_ABOUT, L"关于" }
    };
    
    int y = 100;
    for (int i = 0; i < 4; i++) {
        g_navItems[i] = CreateWindowExW(0, L"BUTTON", items[i].text, WS_CHILD | WS_VISIBLE, 16, y, SIDEBAR_WIDTH - 32, NAV_ITEM_HEIGHT, hWnd, (HMENU)(UINT_PTR)items[i].id, NULL, NULL);
        SendMessageW(g_navItems[i], WM_SETFONT, (WPARAM)g_fontBody, TRUE);
        y += NAV_ITEM_HEIGHT + 8;
    }
}

static void CreateContentArea(HWND hWnd)
{
    // 使用自定义窗口类，能转发 WM_COMMAND 和 WM_NOTIFY 消息
    g_hContent = CreateWindowExW(0, L"BootManagerContentClass", NULL, 
        WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN, 
        SIDEBAR_WIDTH, 0, WINDOW_WIDTH - SIDEBAR_WIDTH, WINDOW_HEIGHT, 
        hWnd, NULL, NULL, NULL);
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
    g_hListView = NULL;
    g_hStatusText = NULL;
    s_refindBtnInstall = s_refindBtnUninstall = s_refindStatus = NULL;
    s_limineBtnInstall = s_limineBtnUninstall = s_limineStatus = NULL;
    
    // 多次枚举确保所有子窗口被销毁
    if (g_hContent) {
        for (int i = 0; i < 3; i++) {
            EnumChildWindows(g_hContent, DestroyChildWindow, 0);
        }
    }
    
    switch (page) {
        case 0: BuildBootMgrPage(g_hContent); break;
        case 1: BuildThirdPartyPage(g_hContent); break;
        case 2: BuildBackupRestorePage(g_hContent); break;
        case 3: BuildAboutPage(g_hContent); break;
    }
}

// 引导管理页面 (仅 UEFI 管理)
static void BuildBootMgrPage(HWND hParent)
{
    RECT rc; GetClientRect(hParent, &rc);
    
    // 检查是否为 UEFI 模式
    BOOL isUEFI = BootMode_IsUEFIFirmware();
    
    int y = 10;
    
    // 标题
    HWND hTitle = CreateWindowExW(0, L"STATIC", L"UEFI 启动项管理", WS_CHILD | WS_VISIBLE, 10, y, rc.right - 20, 28, hParent, NULL, NULL, NULL);
    SendMessageW(hTitle, WM_SETFONT, (WPARAM)g_fontTitle, TRUE);
    y += 35;
    
    if (!isUEFI) {
        // 非 UEFI 模式提示
        HWND hInfo = CreateWindowExW(0, L"STATIC",
            L"⚠ 当前系统未运行在 UEFI 模式下\n\n"
            L"UEFI 启动项管理功能仅在 UEFI 固件模式下可用。\n"
            L"请在 BIOS 设置中启用 UEFI 启动模式。",
            WS_CHILD | WS_VISIBLE, 10, y, rc.right - 20, 100, hParent, NULL, NULL, NULL);
        SendMessageW(hInfo, WM_SETFONT, (WPARAM)g_fontBody, TRUE);
        return;
    }
    
    // UEFI 启动项列表
    g_hListView = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEW, NULL,
        WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL, 10, y, rc.right - 20, rc.bottom - y - 60, hParent, (HMENU)ID_LIST_BOOT, NULL, NULL);
    ListView_SetExtendedListViewStyle(g_hListView, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
    
    LVCOLUMN lvc = {0}; lvc.mask = LVCF_TEXT | LVCF_WIDTH;
    lvc.pszText = L"ID"; lvc.cx = 70; ListView_InsertColumn(g_hListView, 0, &lvc);
    lvc.pszText = L"名称"; lvc.cx = 200; ListView_InsertColumn(g_hListView, 1, &lvc);
    lvc.pszText = L"路径"; lvc.cx = rc.right - 290; ListView_InsertColumn(g_hListView, 2, &lvc);
    
    RefreshBootList();
    
    int bx = 10, by = rc.bottom - 50;
    #define B(id, t, w) CreateWindowExW(0, L"BUTTON", t, WS_CHILD | WS_VISIBLE, bx, by, w, BTN_HEIGHT, hParent, (HMENU)id, NULL, NULL); SendMessageW(GetDlgItem(hParent, id), WM_SETFONT, (WPARAM)g_fontBody, TRUE); bx += w + 8;
    B(ID_BTN_REFRESH, L"刷新", 70);
    B(ID_BTN_ADD_ENTRY, L"添加", 70);
    B(ID_BTN_DELETE_ENTRY, L"删除", 70);
    B(ID_BTN_MOVE_UP, L"↑", 50);
    B(ID_BTN_MOVE_DOWN, L"↓", 50);
    B(ID_BTN_SET_DEFAULT, L"默认", 70);
    #undef B
    
    g_hStatusText = CreateWindowExW(0, L"STATIC", L"就绪", WS_CHILD | WS_VISIBLE, 10, rc.bottom - 20, rc.right - 20, 20, hParent, (HMENU)ID_STATUS_TEXT, NULL, NULL);
    SendMessageW(g_hStatusText, WM_SETFONT, (WPARAM)g_fontSmall, TRUE);
}

// 第三方引导管理器页面
static void BuildThirdPartyPage(HWND hParent)
{
    RECT rc; GetClientRect(hParent, &rc);
    BOOL isUEFI = BootMode_IsUEFIFirmware();
    
    // MBR 模式下显示提示
    if (!isUEFI) {
        int y = 50;
        
        HWND hTitle = CreateWindowExW(0, L"STATIC", L"第三方引导管理器", WS_CHILD | WS_VISIBLE, 10, y, rc.right - 20, 28, hParent, NULL, NULL, NULL);
        SendMessageW(hTitle, WM_SETFONT, (WPARAM)g_fontTitle, TRUE);
        y += 50;
        
        HWND hInfo = CreateWindowExW(0, L"STATIC",
            L"⚠ 当前为 Legacy BIOS (MBR) 模式\n\n"
            L"rEFInd 仅支持 UEFI 模式。\n"
            L"Limine MBR 版本请在「引导管理 → MBR 管理」页面安装。",
            WS_CHILD | WS_VISIBLE, 10, y, rc.right - 20, 100, hParent, NULL, NULL, NULL);
        SendMessageW(hInfo, WM_SETFONT, (WPARAM)g_fontBody, TRUE);
        return;
    }
    
    // UEFI 模式：显示 rEFInd 和 Limine (UEFI版)
    HWND hTab = CreateWindowExW(0, WC_TABCONTROL, NULL, WS_CHILD | WS_VISIBLE, 10, 10, rc.right - 20, TAB_HEIGHT, hParent, (HMENU)ID_TAB_THIRD_PARTY, NULL, NULL);
    SendMessageW(hTab, WM_SETFONT, (WPARAM)g_fontBody, TRUE);
    
    TCITEMW tci = {0}; tci.mask = TCIF_TEXT;
    tci.pszText = L"rEFInd"; TabCtrl_InsertItem(hTab, 0, &tci);
    tci.pszText = L"Limine (UEFI)"; TabCtrl_InsertItem(hTab, 1, &tci);
    
    TabCtrl_SetCurSel(hTab, g_currentThirdPartyTab);
    
    if (g_currentThirdPartyTab == 0) {
        BuildRefindControls(hParent);
        RefindRefreshStatus();
    } else {
        BuildLimineControls(hParent);
        LimineRefreshStatus();
    }
}

// rEFInd 控件
static void BuildRefindControls(HWND hParent)
{
    RECT rc; GetClientRect(hParent, &rc);
    int w = rc.right - 20, y = 50;
    
    HWND hTitle = CreateWindowExW(0, L"STATIC", L"rEFInd 引导管理器", WS_CHILD | WS_VISIBLE, 10, y, w, 28, hParent, NULL, NULL, NULL);
    SendMessageW(hTitle, WM_SETFONT, (WPARAM)g_fontTitle, TRUE);
    y += 40;
    
    HWND hDesc = CreateWindowExW(0, L"STATIC", L"rEFInd 是现代化的 UEFI 引导管理器，安装后会自动检测并列出所有操作系统。", WS_CHILD | WS_VISIBLE, 10, y, w, 24, hParent, NULL, NULL, NULL);
    SendMessageW(hDesc, WM_SETFONT, (WPARAM)g_fontBody, TRUE);
    y += 35;
    
    HWND hInfo = CreateWindowExW(0, L"STATIC", L"安装位置: ESP 分区 \\EFI\\refind\\", WS_CHILD | WS_VISIBLE, 10, y, w, 20, hParent, NULL, NULL, NULL);
    SendMessageW(hInfo, WM_SETFONT, (WPARAM)g_fontSmall, TRUE);
    y += 30;
    
    HWND hRes = CreateWindowExW(0, L"STATIC", L"资源文件: 程序目录下的 refind\\ 文件夹", WS_CHILD | WS_VISIBLE, 10, y, w, 20, hParent, NULL, NULL, NULL);
    SendMessageW(hRes, WM_SETFONT, (WPARAM)g_fontSmall, TRUE);
    y += 45;
    
    s_refindBtnInstall = CreateWindowExW(0, L"BUTTON", L"安装 rEFInd", WS_CHILD | WS_VISIBLE, 10, y, 130, BTN_HEIGHT, hParent, (HMENU)ID_BTN_INSTALL, NULL, NULL);
    SendMessageW(s_refindBtnInstall, WM_SETFONT, (WPARAM)g_fontBody, TRUE);
    
    s_refindBtnUninstall = CreateWindowExW(0, L"BUTTON", L"卸载 rEFInd", WS_CHILD | WS_VISIBLE, 150, y, 130, BTN_HEIGHT, hParent, (HMENU)ID_BTN_UNINSTALL, NULL, NULL);
    SendMessageW(s_refindBtnUninstall, WM_SETFONT, (WPARAM)g_fontBody, TRUE);
    y += 60;
    
    HWND hNote = CreateWindowExW(0, L"STATIC", L"说明：\n• rEFInd 安装后会自动扫描系统\n• 支持检测 Windows、Linux、macOS\n• 需要 refind_x64.efi 文件", WS_CHILD | WS_VISIBLE, 10, y, w, 80, hParent, NULL, NULL, NULL);
    SendMessageW(hNote, WM_SETFONT, (WPARAM)g_fontSmall, TRUE);
    
    s_refindStatus = CreateWindowExW(0, L"STATIC", L"检测中...", WS_CHILD | WS_VISIBLE, 10, rc.bottom - 20, w, 20, hParent, NULL, NULL, NULL);
    SendMessageW(s_refindStatus, WM_SETFONT, (WPARAM)g_fontSmall, TRUE);
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
    if (s_refindStatus) SetWindowTextW(s_refindStatus, installed ? L"rEFInd 已安装" : L"rEFInd 未安装");
}

// 解析 limine.conf
static BOOL LoadLimineConfEntries(void)
{
    s_limineEntryCount = 0;
    if (s_limineConfDir[0] == L'\0') {
        return FALSE;
    }
    
    WCHAR confPath[MAX_PATH];
    swprintf(confPath, MAX_PATH, L"%s\\limine.conf", s_limineConfDir);
    
    HANDLE hFile = CreateFileW(confPath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        return FALSE;
    }
    
    DWORD fileSize = GetFileSize(hFile, NULL);
    if (fileSize == 0 || fileSize > 64 * 1024) {
        CloseHandle(hFile);
        return FALSE;
    }
    
    char* buffer = (char*)malloc(fileSize + 1);
    if (!buffer) {
        CloseHandle(hFile);
        return FALSE;
    }
    DWORD bytesRead = 0;
    ReadFile(hFile, buffer, fileSize, &bytesRead, NULL);
    buffer[bytesRead] = '\0';
    CloseHandle(hFile);
    
    // 计算宽字符缓冲区大小
    int wideSize = MultiByteToWideChar(CP_UTF8, 0, buffer, -1, NULL, 0);
    if (wideSize <= 0) {
        free(buffer);
        return FALSE;
    }
    
    WCHAR* wbuffer = (WCHAR*)malloc(wideSize * sizeof(WCHAR));
    if (!wbuffer) {
        free(buffer);
        return FALSE;
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
    return s_limineEntryCount > 0;
}

// 保存 limine.conf
static BOOL SaveLimineConfEntries(void)
{
    if (s_limineConfDir[0] == L'\0') return FALSE;
    
    WCHAR confPath[MAX_PATH];
    swprintf(confPath, MAX_PATH, L"%s\\limine.conf", s_limineConfDir);
    
    HANDLE hFile = CreateFileW(confPath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return FALSE;
    
    WCHAR wbuffer[32 * 1024] = {0};
    wcscat(wbuffer, L"# Limine Configuration\n# Generated by Boot Manager Pro\n\ntimeout: 5\n\n");
    
    for (int i = 0; i < s_limineEntryCount; i++) {
        LIMINE_CONF_ENTRY* e = &s_limineEntries[i];
        WCHAR entry[1024];
        swprintf(entry, 1024, L"/%s\n    protocol: %s\n    path: %s\n\n", e->name, e->protocol, e->path);
        wcscat(wbuffer, entry);
    }
    
    int utf8Len = WideCharToMultiByte(CP_UTF8, 0, wbuffer, -1, NULL, 0, NULL, NULL);
    char* utf8 = (char*)malloc(utf8Len);
    WideCharToMultiByte(CP_UTF8, 0, wbuffer, -1, utf8, utf8Len, NULL, NULL);
    
    DWORD written;
    WriteFile(hFile, utf8, utf8Len - 1, &written, NULL);
    free(utf8);
    CloseHandle(hFile);
    return TRUE;
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
                    WCHAR name[128] = {0}, path[MAX_PATH] = {0};
                    GetDlgItemTextW(hDlg, 3300, name, 128);
                    GetDlgItemTextW(hDlg, 3301, path, MAX_PATH);
                    
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
        0, 0, 380, 200,
        hParent, NULL, GetModuleHandleW(NULL), NULL);
    
    if (!hDlg) return FALSE;
    
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
    while (g_editHwnd != NULL || IsWindow(hDlg)) {
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
    
    return TRUE;
}

static void BuildLimineControls(HWND hParent)
{
    RECT rc; GetClientRect(hParent, &rc);
    int w = rc.right - 20, y = 50;
    
    // 标题
    HWND hTitle = CreateWindowExW(0, L"STATIC", L"Limine 引导管理器", WS_CHILD | WS_VISIBLE, 10, y, w, 28, hParent, NULL, NULL, NULL);
    SendMessageW(hTitle, WM_SETFONT, (WPARAM)g_fontTitle, TRUE);
    y += 35;
    
    // 安装按钮行
    s_limineBtnInstall = CreateWindowExW(0, L"BUTTON", L"安装 Limine", WS_CHILD | WS_VISIBLE, 10, y, 110, BTN_HEIGHT, hParent, (HMENU)ID_BTN_INSTALL_LIMINE, NULL, NULL);
    SendMessageW(s_limineBtnInstall, WM_SETFONT, (WPARAM)g_fontBody, TRUE);
    
    s_limineBtnUninstall = CreateWindowExW(0, L"BUTTON", L"卸载", WS_CHILD | WS_VISIBLE, 125, y, 70, BTN_HEIGHT, hParent, (HMENU)ID_BTN_UNINSTALL_LIMINE, NULL, NULL);
    SendMessageW(s_limineBtnUninstall, WM_SETFONT, (WPARAM)g_fontBody, TRUE);
    
    y += 45;
    
    // 配置区域标题
    HWND hConfigTitle = CreateWindowExW(0, L"STATIC", L"启动项配置 (limine.conf)：", WS_CHILD | WS_VISIBLE, 10, y, w, 20, hParent, NULL, NULL, NULL);
    SendMessageW(hConfigTitle, WM_SETFONT, (WPARAM)g_fontBody, TRUE);
    y += 25;
    
    // 列表视图
    s_limineList = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEW, NULL,
        WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL,
        10, y, w, 150, hParent, (HMENU)3200, NULL, NULL);
    ListView_SetExtendedListViewStyle(s_limineList, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
    SendMessageW(s_limineList, WM_SETFONT, (WPARAM)g_fontSmall, TRUE);
    
    LVCOLUMN lvc = {0};
    lvc.mask = LVCF_TEXT | LVCF_WIDTH;
    lvc.pszText = L"#"; lvc.cx = 35;
    ListView_InsertColumn(s_limineList, 0, &lvc);
    lvc.pszText = L"名称"; lvc.cx = 100;
    ListView_InsertColumn(s_limineList, 1, &lvc);
    lvc.pszText = L"协议"; lvc.cx = 70;
    ListView_InsertColumn(s_limineList, 2, &lvc);
    lvc.pszText = L"路径"; lvc.cx = w - 215;
    ListView_InsertColumn(s_limineList, 3, &lvc);
    
    y += 160;
    
    // 操作按钮行
    s_limineBtnAddEntry = CreateWindowExW(0, L"BUTTON", L"添加", WS_CHILD | WS_VISIBLE, 10, y, 60, BTN_HEIGHT, hParent, (HMENU)ID_BTN_LIMINE_ADD, NULL, NULL);
    SendMessageW(s_limineBtnAddEntry, WM_SETFONT, (WPARAM)g_fontBody, TRUE);
    
    s_limineBtnEditEntry = CreateWindowExW(0, L"BUTTON", L"编辑", WS_CHILD | WS_VISIBLE, 75, y, 60, BTN_HEIGHT, hParent, (HMENU)ID_BTN_LIMINE_EDIT, NULL, NULL);
    SendMessageW(s_limineBtnEditEntry, WM_SETFONT, (WPARAM)g_fontBody, TRUE);
    
    s_limineBtnDelEntry = CreateWindowExW(0, L"BUTTON", L"删除", WS_CHILD | WS_VISIBLE, 140, y, 60, BTN_HEIGHT, hParent, (HMENU)ID_BTN_LIMINE_DELETE, NULL, NULL);
    SendMessageW(s_limineBtnDelEntry, WM_SETFONT, (WPARAM)g_fontBody, TRUE);
    
    s_limineBtnRefresh = CreateWindowExW(0, L"BUTTON", L"刷新", WS_CHILD | WS_VISIBLE, 205, y, 60, BTN_HEIGHT, hParent, (HMENU)ID_BTN_LIMINE_REFRESH, NULL, NULL);
    SendMessageW(s_limineBtnRefresh, WM_SETFONT, (WPARAM)g_fontBody, TRUE);
    
    y += 35;
    
    // 提示
    HWND hHint = CreateWindowExW(0, L"STATIC",
        L"提示：双击条目可编辑 | 协议：limine(.iso镜像), efi(.efi引导), linux(.elf内核)",
        WS_CHILD | WS_VISIBLE, 10, y, w, 20, hParent, NULL, NULL, NULL);
    SendMessageW(hHint, WM_SETFONT, (WPARAM)g_fontSmall, TRUE);
    
    // 状态栏
    s_limineStatus = CreateWindowExW(0, L"STATIC", L"检测中...", WS_CHILD | WS_VISIBLE, 10, rc.bottom - 20, w, 20, hParent, NULL, NULL, NULL);
    SendMessageW(s_limineStatus, WM_SETFONT, (WPARAM)g_fontSmall, TRUE);
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
    
    // 2. 如果没找到，尝试挂载 ESP 检测
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
            
            // 如果是我们挂载的，需要保持挂载状态以便读取配置
            // 不在这里卸载
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
    int w = rc.right - 20, y = 10;
    
    // 标题
    HWND hTitle = CreateWindowExW(0, L"STATIC", L"备份与恢复", WS_CHILD | WS_VISIBLE, 10, y, w, 28, hParent, NULL, NULL, NULL);
    SendMessageW(hTitle, WM_SETFONT, (WPARAM)g_fontTitle, TRUE);
    y += 40;
    
    // ===== MBR 备份恢复区域 =====
    HWND hMbrTitle = CreateWindowExW(0, L"STATIC", L"MBR 备份与修复", WS_CHILD | WS_VISIBLE, 10, y, w, 20, hParent, NULL, NULL, NULL);
    SendMessageW(hMbrTitle, WM_SETFONT, (WPARAM)g_fontBody, TRUE);
    y += 28;
    
    // MBR 按钮行
    int bx = 10;
    CreateWindowExW(0, L"BUTTON", L"备份 MBR", WS_CHILD | WS_VISIBLE, bx, y, 100, BTN_HEIGHT, hParent, (HMENU)ID_BTN_BACKUP_MBR, NULL, NULL);
    SendMessageW(GetDlgItem(hParent, ID_BTN_BACKUP_MBR), WM_SETFONT, (WPARAM)g_fontBody, TRUE);
    bx += 108;
    
    CreateWindowExW(0, L"BUTTON", L"恢复 MBR", WS_CHILD | WS_VISIBLE, bx, y, 100, BTN_HEIGHT, hParent, (HMENU)ID_BTN_RESTORE_MBR, NULL, NULL);
    SendMessageW(GetDlgItem(hParent, ID_BTN_RESTORE_MBR), WM_SETFONT, (WPARAM)g_fontBody, TRUE);
    bx += 108;
    
    CreateWindowExW(0, L"BUTTON", L"修复 Windows MBR", WS_CHILD | WS_VISIBLE, bx, y, 130, BTN_HEIGHT, hParent, (HMENU)ID_BTN_MBR_REPAIR, NULL, NULL);
    SendMessageW(GetDlgItem(hParent, ID_BTN_MBR_REPAIR), WM_SETFONT, (WPARAM)g_fontBody, TRUE);
    y += 45;
    
    // MBR 说明
    HWND hMbrNote = CreateWindowExW(0, L"STATIC",
        L"说明：MBR（主引导记录）位于磁盘第一个扇区，包含引导代码和分区表。\n"
        L"• 备份 MBR：保存完整的 MBR 到文件\n"
        L"• 恢复 MBR：从备份文件恢复 MBR\n"
        L"• 修复 Windows MBR：将 MBR 引导代码重置为 Windows 标准",
        WS_CHILD | WS_VISIBLE, 10, y, w, 60, hParent, NULL, NULL, NULL);
    SendMessageW(hMbrNote, WM_SETFONT, (WPARAM)g_fontSmall, TRUE);
    y += 75;
    
    // ===== UEFI 修复区域 =====
    BOOL isUEFI = BootMode_IsUEFIFirmware();
    
    HWND hUefiTitle = CreateWindowExW(0, L"STATIC", L"UEFI 引导修复", WS_CHILD | WS_VISIBLE, 10, y, w, 20, hParent, NULL, NULL, NULL);
    SendMessageW(hUefiTitle, WM_SETFONT, (WPARAM)g_fontBody, TRUE);
    y += 28;
    
    if (!isUEFI) {
        // 非 UEFI 模式提示
        HWND hUefiNote = CreateWindowExW(0, L"STATIC",
            L"⚠ 当前系统未运行在 UEFI 模式下，UEFI 修复功能不可用",
            WS_CHILD | WS_VISIBLE, 10, y, w, 20, hParent, NULL, NULL, NULL);
        SendMessageW(hUefiNote, WM_SETFONT, (WPARAM)g_fontSmall, TRUE);
        y += 25;
    } else {
        // UEFI 修复按钮
        bx = 10;
        CreateWindowExW(0, L"BUTTON", L"修复 UEFI 引导", WS_CHILD | WS_VISIBLE, bx, y, 120, BTN_HEIGHT, hParent, (HMENU)ID_BTN_UEFI_REPAIR, NULL, NULL);
        SendMessageW(GetDlgItem(hParent, ID_BTN_UEFI_REPAIR), WM_SETFONT, (WPARAM)g_fontBody, TRUE);
        y += 45;
        
        // UEFI 说明
        HWND hUefiNote = CreateWindowExW(0, L"STATIC",
            L"说明：修复 UEFI 引导可以解决以下问题：\n"
            L"• Windows Boot Manager 丢失或损坏\n"
            L"• ESP 分区引导文件缺失\n"
            L"• NVRAM 启动项丢失",
            WS_CHILD | WS_VISIBLE, 10, y, w, 60, hParent, NULL, NULL, NULL);
        SendMessageW(hUefiNote, WM_SETFONT, (WPARAM)g_fontSmall, TRUE);
        y += 70;
    }
    
    // 状态栏
    HWND hStatus = CreateWindowExW(0, L"STATIC", L"就绪", WS_CHILD | WS_VISIBLE, 10, rc.bottom - 30, w, 20, hParent, (HMENU)ID_STATUS_TEXT, NULL, NULL);
    SendMessageW(hStatus, WM_SETFONT, (WPARAM)g_fontSmall, TRUE);
}

// 关于页面
static void BuildAboutPage(HWND hParent)
{
    RECT rc; GetClientRect(hParent, &rc);
    int w = rc.right - 32, y = 32;
    
    HWND hTitle = CreateWindowExW(0, L"STATIC", L"关于", WS_CHILD | WS_VISIBLE, 32, y, w, 32, hParent, NULL, NULL, NULL);
    SendMessageW(hTitle, WM_SETFONT, (WPARAM)g_fontTitle, TRUE);
    
    y += 50;
    HWND hName = CreateWindowExW(0, L"STATIC", L"Boot Manager Pro v3.2.0", WS_CHILD | WS_VISIBLE, 32, y, w, 28, hParent, NULL, NULL, NULL);
    SendMessageW(hName, WM_SETFONT, (WPARAM)g_fontTitle, TRUE);
    
    y += 40;
    HWND hDesc = CreateWindowExW(0, L"STATIC", L"UEFI/MBR 启动项管理工具", WS_CHILD | WS_VISIBLE, 32, y, w, 150, hParent, NULL, NULL, NULL);
    SendMessageW(hDesc, WM_SETFONT, (WPARAM)g_fontBody, TRUE);
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
    
    // 2. Z:\refind (开发环境)
    if (GetFileAttributesW(L"Z:\\refind\\refind_x64.efi") != INVALID_FILE_ATTRIBUTES) {
        wcsncpy(path, L"Z:\\refind", size);
        return TRUE;
    }
    
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
    
    // 4. Z:\limine (开发环境)
    if (GetFileAttributesW(L"Z:\\limine\\limine-bios.sys") != INVALID_FILE_ATTRIBUTES ||
        GetFileAttributesW(L"Z:\\limine\\limine-efi\\BOOTX64.EFI") != INVALID_FILE_ATTRIBUTES) {
        wcsncpy(path, L"Z:\\limine", size);
        return TRUE;
    }
    
    return FALSE;
}

// ============================================
// MBR 管理页面实现
// ============================================

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE hPrevInst, LPWSTR lpCmdLine, int nCmdShow)
{
    InitCommonControls();
    InitFonts();
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
    
    g_hMainWnd = CreateWindowExW(0, L"BootManagerProClass", L"Boot Manager Pro v3.2.0",
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