/**
 * home.c - 卡片式首页（状态仪表盘）
 * 设计规范见 docs/REDESIGN.md 第二节
 */
#include "home.h"
#include "cards.h"
#include "../../include/boot.h"
#include "../core/backup.h"
#include "../include/mbr_manager.h"
#include <windows.h>
#include <wchar.h>

/* ---- 色板 ---- */
static const COLORREF HOME_BG = RGB(246, 248, 250);   /* #F6F8FA */

#define HOME_W  920
#define HOME_H  560
#define CARD_W  264
#define CARD_H  168
#define CARD_GAP  24
#define HOME_MARGIN 40

/* 卡片 ID */
#define CARD_BOOTMGR   1
#define CARD_LOADER    2
#define CARD_BACKUP    3

static HWND g_hHome = NULL;
static HWND g_hCardBootMgr, g_hCardLoader, g_hCardBackup;

/* ---------- 状态摘要填充（惰性，一次性） ---------- */

static void FillBootMgrLine(void) {
    WCHAR l1[128], l2[128];
    BOOTMGR_BOOT_LIST* list = BootMgrScanBootEntries();
    int count = list ? list->count : 0;
    swprintf(l1, 128, L"%d 个启动项", count);
    l2[0] = L'\0';
    if (list && list->entries) {
        swprintf(l2, 128, L"默认: %s", BootMgrGetEntryName(list->entries));
    }
    if (list) BootMgrFreeBootList(list);
    BMCard_SetLine(g_hCardBootMgr, 1, l1);
    BMCard_SetLine(g_hCardBootMgr, 2, l2[0] ? l2 : L"点击查看详情");
}

static void FillLoaderLine(void) {
    WCHAR l1[128];
    int sysDisk = MBR_GetSystemDiskNumber();
    MBR_BOOT_TYPE t = (sysDisk >= 0) ? MBR_DetectBootType(sysDisk) : MBR_BOOT_UNKNOWN;
    switch (t) {
        case MBR_BOOT_LIMINE:   swprintf(l1, 128, L"当前 MBR: Limine"); break;
        case MBR_BOOT_GRUB4DOS: swprintf(l1, 128, L"当前 MBR: GRUB4DOS"); break;
        case MBR_BOOT_WINDOWS:  swprintf(l1, 128, L"当前 MBR: Windows"); break;
        default:                swprintf(l1, 128, L"当前 MBR: 未知/未装"); break;
    }
    BMCard_SetLine(g_hCardLoader, 1, l1);
    BMCard_SetLine(g_hCardLoader, 2, L"rEFInd / Limine 安装见详情");
}

static void FillBackupLine(void) {
    WCHAR dir[MAX_PATH];
    WCHAR pattern[MAX_PATH];
    WIN32_FIND_DATAW fd;
    HANDLE h;
    int count = 0;
    WCHAR l1[128];

    if (!BackupGetBackupDir(dir, MAX_PATH)) {
        BMCard_SetLine(g_hCardBackup, 1, L"尚无备份");
        return;
    }
    swprintf(pattern, MAX_PATH, L"%s\\*.*", dir);
    h = FindFirstFileW(pattern, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) count++;
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    if (count == 0) {
        swprintf(l1, 128, L"尚无备份");
        BMCard_SetLine(g_hCardBackup, 1, l1);
        BMCard_SetLine(g_hCardBackup, 2, L"建议尽快做一次全量备份");
    } else {
        swprintf(l1, 128, L"%d 个备份文件", count);
        BMCard_SetLine(g_hCardBackup, 1, l1);
        BMCard_SetLine(g_hCardBackup, 2, L"点此管理备份与恢复");
    }
}

/* ---------- 主窗口 ---------- */

static LRESULT CALLBACK HomeProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        CREATESTRUCTW* cs = (CREATESTRUCTW*)lParam;
        int x = HOME_MARGIN;

        /* 三张导航卡片 */
        g_hCardBootMgr = BMCard_Create(hWnd, CARD_BOOTMGR, x, 110, CARD_W, CARD_H,
            L"引导管理", L"正在读取…", L"", L"管理", FALSE);
        g_hCardLoader  = BMCard_Create(hWnd, CARD_LOADER, x + CARD_W + CARD_GAP, 110,
            CARD_W, CARD_H, L"引导器安装", L"检测中…", L"", L"安装", FALSE);
        g_hCardBackup  = BMCard_Create(hWnd, CARD_BACKUP, x + (CARD_W + CARD_GAP) * 2, 110,
            CARD_W, CARD_H, L"备份恢复", L"检测中…", L"", L"查看", TRUE);

        /* 系统概览卡（占满一行，摘要型） */
        BMCard_Create(hWnd, 4, HOME_MARGIN, 110 + CARD_H + CARD_GAP,
                      CARD_W * 3 + CARD_GAP * 2, 110,
                      L"系统概览", L"检测中…", L"", NULL, FALSE);

        /* 惰性填充状态（避免阻塞窗口显示） */
        SetTimer(hWnd, 1, 400, NULL);
        (void)cs;
        return 0;
    }

    case WM_TIMER:
        if (wParam == 1) {
            KillTimer(hWnd, 1);
            FillBootMgrLine();
            FillLoaderLine();
            FillBackupLine();
        }
        return 0;

    case WM_COMMAND: {
        int id = LOWORD(wParam);
        int code = HIWORD(wParam);
        if (code == BMN_OPEN || code == BMN_BUTTON) {
            /* P1 阶段：卡片导航到经典界面；P2 逐页替换为卡片工作页 */
            Classic_CreateAndShow();
            ShowWindow(g_hHome, SW_HIDE);
            (void)id;
        }
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        RECT rc;
        GetClientRect(hWnd, &rc);
        {
            HBRUSH bg = CreateSolidBrush(HOME_BG);
            FillRect(hdc, &rc, bg);
            DeleteObject(bg);
        }
        {
            static HFONT hTitle = NULL, hSub = NULL;
            if (!hTitle) {
                hTitle = CreateFontW(-22, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0,
                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                    CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
                hSub = CreateFontW(-13, 0, 0, 0, FW_NORMAL, 0, 0, 0,
                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                    CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
            }
            RECT r1 = { HOME_MARGIN, 34, rc.right, 66 };
            RECT r2 = { HOME_MARGIN, 68, rc.right, 92 };
            SelectObject(hdc, hTitle);
            SetTextColor(hdc, RGB(15, 23, 42));
            DrawTextW(hdc, L"Boot Manager Pro", -1, &r1, DT_LEFT | DT_SINGLELINE);
            SelectObject(hdc, hSub);
            SetTextColor(hdc, RGB(100, 116, 139));
            DrawTextW(hdc, L"UEFI 引导管理 · 备份恢复 · 第三方引导器", -1, &r2,
                      DT_LEFT | DT_SINGLELINE);
        }
        EndPaint(hWnd, &ps);
        return 0;
    }

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

int HomeMain(HINSTANCE hInst, int nCmdShow) {
    WNDCLASSEXW wc = {0};

    BMCard_RegisterClass(hInst);

    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = HomeProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush(HOME_BG);
    wc.lpszClassName = L"BMHomeClass";
    RegisterClassExW(&wc);

    g_hHome = CreateWindowExW(0, L"BMHomeClass", L"Boot Manager Pro",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, HOME_W, HOME_H, NULL, NULL, hInst, NULL);
    if (!g_hHome) return 1;

    ShowWindow(g_hHome, nCmdShow);
    UpdateWindow(g_hHome);

    {
        MSG msg;
        while (GetMessageW(&msg, NULL, 0, 0)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        return (int)msg.wParam;
    }
}
