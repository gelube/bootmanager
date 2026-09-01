/**
 * home.c - 卡片式界面 v2
 * 卡片页是真正的功能页，取代旧界面（--classic 仍可回退）：
 *   首页仪表盘 → 引导管理 / 引导器安装 / 备份恢复 三张卡片工作页
 */
#include "home.h"
#include "cards.h"
#include "../../include/boot.h"
#include "../../include/envinfo.h"
#include "../../include/uefi_nvram.h"
#include "../../include/ops.h"
#include "../../include/esp.h"
#include "../core/backup.h"
#include "../core/refind.h"
#include "../include/limine.h"
#include "../include/mbr_manager.h"
#include <windows.h>
#include <windowsx.h>
#include <wchar.h>
#include <stdio.h>

static const COLORREF HOME_BG = RGB(246, 248, 250);

#define HOME_W   960
#define HOME_H   680
#define CARD_W   264
#define CARD_H   168
#define CARD_GAP  24
#define MARGIN    40

/* 页面 */
enum { PAGE_DASH = 0, PAGE_BOOTMGR, PAGE_LOADER, PAGE_BACKUP };

/* 控件 ID（卡片 1000+，按钮 2000+，见 cards.c） */
#define CID_BOOTMGR   1
#define CID_LOADER    2
#define CID_BACKUP    3
#define CID_OVERVIEW  4
/* 引导项行卡片从 100 起 */

#define BTN_BACK        20
#define BTN_REFRESH     21
#define BTN_UP          22
#define BTN_DOWN        23
#define BTN_SETDEFAULT  24
#define BTN_DELETE      25
#define BTN_INSTALL     26
#define BTN_UNINSTALL   27
#define BTN_BACKUPNOW   28
#define BTN_RESTOREMBR  29
#define BTN_OPENDIR     30

static HWND g_hHome = NULL;
static int  g_page = PAGE_DASH;
static HBRUSH g_bgBrush = NULL;

/* 引导管理页数据 */
static BOOTMGR_BOOT_LIST* g_bootList = NULL;
static DWORD g_selectedId = 0;
#define MAX_ROWS 24
static HWND g_rowCards[MAX_ROWS];
static int  g_rowCount = 0;

/* 引导器页数据 */
static WCHAR g_espDrive[4] = {0};
static HWND g_hCardRefind = NULL, g_hCardLimine = NULL;

static void ShowPage(int page);
static void ClearPage(void);
static LRESULT CALLBACK HomeProc(HWND, UINT, WPARAM, LPARAM);

/* ---------------- 提权 ---------------- */

static BOOL RelaunchAsAdmin(void) {
    WCHAR exePath[MAX_PATH];
    SHELLEXECUTEINFOW sei = {0};
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    sei.cbSize = sizeof(sei);
    sei.lpVerb = L"runas";
    sei.lpFile = exePath;
    sei.nShow = SW_SHOWNORMAL;
    return ShellExecuteExW(&sei);
}

/* ---------------- 首页仪表盘 ---------------- */

static void FillOverview(HWND card) {
    WCHAR l1[128], l2[128];
    int sysDisk = MBR_GetSystemDiskNumber();
    BOOL uefi = FALSE, admin = FALSE;
    UEFI_BOOT_ORDER bo = {0};

    if (UefiNvramGetBootOrder(&bo)) {
        uefi = TRUE;
        UefiNvramFreeBootOrder(&bo);
    }
    /* BootMgrIsAdmin 在 boot.h 声明 */
    admin = BootMgrIsAdmin() ? TRUE : FALSE;

    swprintf(l1, 128, L"固件: %s · 权限: %s", uefi ? L"UEFI" : L"Legacy/未知",
             admin ? L"管理员 ✓" : L"普通(部分功能不可用) ⚠");
    swprintf(l2, 128, L"系统盘: PhysicalDrive%d · 环境: %s",
             sysDisk < 0 ? 0 : sysDisk, EnvIsWinPE() ? L"WinPE" : L"Windows");
    BMCard_SetLine(card, 1, l1);
    BMCard_SetLine(card, 2, l2);
}

static void FillDashStatus(HWND hBoot, HWND hLoader, HWND hBackup) {
    WCHAR l1[128], l2[128];

    /* 引导管理：需要管理员 + bcdedit */
    if (!BootMgrIsAdmin()) {
        BMCard_SetLine(hBoot, 1, L"需要管理员权限");
        BMCard_SetLine(hBoot, 2, L"无法读取启动项");
    } else if (!EnvHasBcdEdit()) {
        BMCard_SetLine(hBoot, 1, L"bcdedit 不可用");
        BMCard_SetLine(hBoot, 2, L"请确认程序目录或 PATH");
    } else {
        BOOTMGR_BOOT_LIST* list = BootMgrScanBootEntries();
        int count = list ? list->count : 0;
        swprintf(l1, 128, L"%d 个启动项", count);
        BMCard_SetLine(hBoot, 1, l1);
        if (list && list->entries && BootMgrGetEntryName(list->entries)[0])
            swprintf(l2, 128, L"首项: %s", BootMgrGetEntryName(list->entries));
        else
            swprintf(l2, 128, L"点击查看详情");
        if (list) BootMgrFreeBootList(list);
    }

    /* 引导器：检测 MBR 引导码类型 */
    {
        int sysDisk = MBR_GetSystemDiskNumber();
        MBR_BOOT_TYPE t = (sysDisk >= 0) ? MBR_DetectBootType(sysDisk) : MBR_BOOT_UNKNOWN;
        switch (t) {
            case MBR_BOOT_LIMINE:   BMCard_SetLine(hLoader, 1, L"当前 MBR: Limine"); break;
            case MBR_BOOT_GRUB4DOS: BMCard_SetLine(hLoader, 1, L"当前 MBR: GRUB4DOS"); break;
            case MBR_BOOT_WINDOWS:  BMCard_SetLine(hLoader, 1, L"当前 MBR: Windows"); break;
            default:                BMCard_SetLine(hLoader, 1, L"当前 MBR: 未知"); break;
        }
        BMCard_SetLine(hLoader, 2, L"rEFInd / Limine 安装管理");
    }

    /* 备份：统计 backups 目录 */
    {
        WCHAR dir[MAX_PATH], pattern[MAX_PATH];
        WIN32_FIND_DATAW fd;
        HANDLE h;
        int count = 0;
        if (BackupGetBackupDir(dir, MAX_PATH)) {
            swprintf(pattern, MAX_PATH, L"%s\\*.*", dir);
            h = FindFirstFileW(pattern, &fd);
            if (h != INVALID_HANDLE_VALUE) {
                do { if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) count++; }
                while (FindNextFileW(h, &fd));
                FindClose(h);
            }
        }
        if (count == 0) {
            BMCard_SetLine(hBackup, 1, L"尚无备份");
            BMCard_SetLine(hBackup, 2, L"建议先做一次全量备份");
        } else {
            swprintf(l1, 128, L"%d 个备份文件", count);
            BMCard_SetLine(hBackup, 1, l1);
            BMCard_SetLine(hBackup, 2, L"点击备份 / 恢复");
        }
    }
}

static void BuildDashPage(void) {
    int x = MARGIN;
    HWND hOverview;

    g_rowCards[0] = BMCard_Create(g_hHome, CID_BOOTMGR, x, 120, CARD_W, CARD_H,
        L"🖥 引导管理", L"读取中…", L"", L"进入", FALSE);
    g_rowCards[1] = BMCard_Create(g_hHome, CID_LOADER, x + CARD_W + CARD_GAP, 120,
        CARD_W, CARD_H, L"📦 引导器安装", L"检测中…", L"", L"进入", FALSE);
    g_rowCards[2] = BMCard_Create(g_hHome, CID_BACKUP, x + (CARD_W + CARD_GAP) * 2, 120,
        CARD_W, CARD_H, L"🛟 备份恢复", L"检测中…", L"", L"进入", TRUE);

    hOverview = BMCard_Create(g_hHome, CID_OVERVIEW, MARGIN, 120 + CARD_H + CARD_GAP,
                              CARD_W * 3 + CARD_GAP * 2, 110, L"系统概览", L"", L"", NULL, FALSE);
    SetTimer(g_hHome, 1, 300, NULL);

    /* 存句柄给定时器填充用 */
    SetPropW(g_hHome, L"dash0", g_rowCards[0]);
    SetPropW(g_hHome, L"dash1", g_rowCards[1]);
    SetPropW(g_hHome, L"dash2", g_rowCards[2]);
    SetPropW(g_hHome, L"dash3", hOverview);
}

/* ---------------- 引导管理页 ---------------- */

static void FreeBootList(void) {
    if (g_bootList) { BootMgrFreeBootList(g_bootList); g_bootList = NULL; }
}

static void RefreshBootMgrPage(void) {
    /* 重建页面 */
    ClearPage();
    ShowPage(PAGE_BOOTMGR);
}

static void BuildBootMgrPage(void) {
    WCHAR title[64];
    int y = 120;
    int i;
    BOOTMGR_BOOT_ENTRY* e;

    BMFlatButton_Create(g_hHome, BTN_REFRESH, HOME_W - MARGIN - 110, 64, 110, 36,
                        L"🔄 刷新", TRUE, FALSE);

    if (!BootMgrIsAdmin()) {
        BMCard_Create(g_hHome, 900, MARGIN, y, CARD_W * 3 + CARD_GAP * 2, 110,
            L"⚠ 需要管理员权限", L"当前以普通权限运行，无法读取启动项。", L"请右键以管理员身份运行程序。", NULL, TRUE);
        return;
    }

    FreeBootList();
    g_selectedId = 0;
    g_rowCount = 0;
    memset(g_rowCards, 0, sizeof(g_rowCards));

    g_bootList = BootMgrScanBootEntries();
    if (!g_bootList || g_bootList->count == 0) {
        BMCard_Create(g_hHome, 901, MARGIN, y, CARD_W * 3 + CARD_GAP * 2, 110,
            L"未读取到启动项", L"可能原因: bcdedit 不可用 / 无固件启动项", L"点右上角「刷新」重试", NULL, FALSE);
        return;
    }

    e = g_bootList->entries;
    for (i = 0; i < MAX_ROWS && e; i++, e = e->next) {
        WCHAR line1[128], line2[160];
        const WCHAR* name = BootMgrGetEntryName(e);
        const WCHAR* path = BootMgrGetEntryPath(e);
        swprintf(line1, 128, L"%d. %s", i + 1, name && name[0] ? name : L"(未命名)");
        swprintf(line2, 160, L"%s", path && path[0] ? path : L"-");
        g_rowCards[i] = BMCard_Create(g_hHome, 100 + i, MARGIN, y,
                                      CARD_W * 3 + CARD_GAP * 2, 96,
                                      line1, line2, NULL, NULL, FALSE);
        y += 96 + 12;
        g_rowCount++;
    }

    /* 操作栏 */
    y += 8;
    BMFlatButton_Create(g_hHome, BTN_UP,          MARGIN, y, 96, 36, L"↑ 上移", FALSE, FALSE);
    BMFlatButton_Create(g_hHome, BTN_DOWN,        MARGIN + 106, y, 96, 36, L"↓ 下移", FALSE, FALSE);
    BMFlatButton_Create(g_hHome, BTN_SETDEFAULT,  MARGIN + 212, y, 120, 36, L"设为默认", FALSE, FALSE);
    BMFlatButton_Create(g_hHome, BTN_DELETE,      MARGIN + 342, y, 96, 36, L"删除", FALSE, TRUE);
    (void)title;
}

static HWND GetRowCard(DWORD idx) {
    return (idx >= 0 && idx < (DWORD)g_rowCount) ? g_rowCards[idx] : NULL;
}

static DWORD RowIndexToId(DWORD idx) {
    BOOTMGR_BOOT_ENTRY* e;
    DWORD i;
    if (!g_bootList) return 0;
    e = g_bootList->entries;
    for (i = 0; e && i < idx; i++) e = e->next;
    return e ? e->id : 0;
}

static void HandleBootMgrAction(WORD btn) {
    DWORD idx;
    /* 找到选中行 */
    for (idx = 0; idx < (DWORD)g_rowCount; idx++) {
        HWND h = GetRowCard(idx);
        if (h && BMCard_IsSelected(h)) break;
    }
    if (idx >= (DWORD)g_rowCount) {
        MessageBoxW(g_hHome, L"请先点击选择一个启动项", L"提示", MB_OK | MB_ICONINFORMATION);
        return;
    }

    switch (btn) {
    case BTN_UP:
        BootMgrMoveBootEntryUp(g_bootList, RowIndexToId(idx));
        RefreshBootMgrPage();
        break;
    case BTN_DOWN:
        BootMgrMoveBootEntryDown(g_bootList, RowIndexToId(idx));
        RefreshBootMgrPage();
        break;
    case BTN_SETDEFAULT:
        BootMgrSetDefaultBootEntry(g_bootList, RowIndexToId(idx));
        RefreshBootMgrPage();
        break;
    case BTN_DELETE: {
        WCHAR msg[128];
        swprintf(msg, 128, L"确定删除启动项 %d 吗？此操作会写入固件 NVRAM。", idx + 1);
        if (MessageBoxW(g_hHome, msg, L"确认删除", MB_YESNO | MB_ICONWARNING) == IDYES) {
            BootMgrDeleteBootEntry(RowIndexToId(idx));
            RefreshBootMgrPage();
        }
        break;
    }
    }
}

/* ---------------- 引导器安装页 ---------------- */

static BOOL EnsureEspMounted(void) {
    if (g_espDrive[0]) return TRUE;
    if (!EspMount(g_espDrive, 4)) {
        MessageBoxW(g_hHome, L"无法挂载 ESP 分区\n\nPE 环境请确认磁盘可访问", L"错误", MB_OK | MB_ICONERROR);
        return FALSE;
    }
    return TRUE;
}

static void RefreshLoaderPage(void) {
    g_espDrive[0] = 0;
    ClearPage();
    ShowPage(PAGE_LOADER);
}

static void BuildLoaderPage(void) {
    BOOL refindOk = FALSE, limineOk = FALSE;
    WCHAR l1[128], l2[128];
    int y = 120;

    BMFlatButton_Create(g_hHome, BTN_REFRESH, HOME_W - MARGIN - 110, 64, 110, 36,
                        L"🔄 刷新", TRUE, FALSE);

    if (EnsureEspMounted()) {
        WCHAR p[MAX_PATH];
        swprintf(p, MAX_PATH, L"%s\\EFI\\refind\\refind_x64.efi", g_espDrive);
        refindOk = (GetFileAttributesW(p) != INVALID_FILE_ATTRIBUTES);
        swprintf(p, MAX_PATH, L"%s\\EFI\\limine", g_espDrive);
        limineOk = (GetFileAttributesW(p) != INVALID_FILE_ATTRIBUTES);
    }

    /* rEFInd 卡 */
    swprintf(l1, 128, L"状态: %s", refindOk ? L"已安装 ✓" : L"未安装");
    swprintf(l2, 128, L"ESP: %s", g_espDrive[0] ? g_espDrive : L"未挂载");
    g_hCardRefind = BMCard_Create(g_hHome, 800, MARGIN, y, CARD_W, CARD_H + 30,
                                  L"rEFInd", l1, l2,
                                  refindOk ? L"卸载" : L"安装", refindOk ? TRUE : FALSE);
    /* Limine 卡 */
    swprintf(l1, 128, L"状态: %s", limineOk ? L"已安装 ✓" : L"未安装");
    g_hCardLimine = BMCard_Create(g_hHome, 801, MARGIN + CARD_W + CARD_GAP, y,
                                  CARD_W, CARD_H + 30, L"Limine", l1, l2,
                                  limineOk ? L"卸载" : L"安装", limineOk ? TRUE : FALSE);

    swprintf(l2, 128, L"说明: 安装/卸载会写入 ESP 分区 %s", g_espDrive[0] ? g_espDrive : L"(未挂载)");
    BMCard_Create(g_hHome, 802, MARGIN, y + CARD_H + 42, CARD_W * 2 + CARD_GAP, 90,
                  L"ℹ 操作说明", L"点击卡片右下角按钮执行安装或卸载", l2, NULL, FALSE);
}

static void HandleLoaderAction(int cardId, int code) {
    BOOL isRefind = (cardId == 800);
    BOOL installed;

    if (code != BMN_BUTTON && code != BMN_OPEN) return;
    if (!EnsureEspMounted()) return;

    if (isRefind) {
        installed = RefindIsInstalled(g_espDrive);
        if (!installed) {
            WCHAR src[MAX_PATH], exePath[MAX_PATH], *slash;
            GetModuleFileNameW(NULL, exePath, MAX_PATH);
            slash = wcsrchr(exePath, L'\\');
            if (slash) *slash = 0;
            swprintf(src, MAX_PATH, L"%s\\refind", exePath);
            if (!RefindInstall(src, g_espDrive))
                MessageBoxW(g_hHome, RefindGetLastErrorMessage(), L"rEFInd 安装失败", MB_OK | MB_ICONERROR);
        } else {
            if (MessageBoxW(g_hHome, L"确定卸载 rEFInd 吗？", L"确认", MB_YESNO | MB_ICONQUESTION) == IDYES)
                RefindUninstall(g_espDrive);
            else return;
        }
    } else {
        /* Limine 状态检测：\EFI\limine 目录 */
        WCHAR p[MAX_PATH];
        swprintf(p, MAX_PATH, L"%s\\EFI\\limine", g_espDrive);
        installed = (GetFileAttributesW(p) != INVALID_FILE_ATTRIBUTES);
        if (!installed) {
            WCHAR src[MAX_PATH];
            if (LimineFindSource(src, MAX_PATH)) {
                if (!LimineInstallToUEFI(g_espDrive, src))
                    MessageBoxW(g_hHome, L"Limine 安装失败", L"错误", MB_OK | MB_ICONERROR);
            } else {
                MessageBoxW(g_hHome, L"未找到 Limine 源文件\n请在程序目录创建 limine 文件夹", L"提示", MB_OK | MB_ICONINFORMATION);
                return;
            }
        } else {
            if (MessageBoxW(g_hHome, L"确定卸载 Limine（ESP 部分）吗？", L"确认", MB_YESNO | MB_ICONQUESTION) == IDYES)
                LimineUninstall(g_espDrive);
            else return;
        }
    }
    RefreshLoaderPage();
}

/* ---------------- 备份恢复页 ---------------- */

static void BuildBackupPage(void) {
    int y = 120;
    WCHAR dir[MAX_PATH];

    BMFlatButton_Create(g_hHome, BTN_BACKUPNOW, HOME_W - MARGIN - 140, 64, 140, 36,
                        L"💾 立即全量备份", TRUE, FALSE);
    BMFlatButton_Create(g_hHome, BTN_RESTOREMBR, HOME_W - MARGIN - 290, 64, 140, 36,
                        L"↩ 恢复最近MBR", FALSE, TRUE);
    BMFlatButton_Create(g_hHome, BTN_OPENDIR, HOME_W - MARGIN - 440, 64, 140, 36,
                        L"📂 备份目录", FALSE, FALSE);

    if (BackupGetBackupDir(dir, MAX_PATH)) {
        /* 列出最近的备份文件（简单排序：按名称倒序=时间倒序） */
        WCHAR pattern[MAX_PATH];
        WIN32_FIND_DATAW fds[64];
        int count = 0, i, j;

        swprintf(pattern, MAX_PATH, L"%s\\*.*", dir);
        {
            HANDLE h = FindFirstFileW(pattern, &fds[0]);
            if (h != INVALID_HANDLE_VALUE) {
                do {
                    if (!(fds[count].dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && count < 64)
                        count++;
                    else if (count < 64)
                        continue;
                    else
                        break;
                } while (FindNextFileW(h, &fds[count]));
                FindClose(h);
            }
        }
        /* 冒泡按文件名（时间戳）倒序 */
        for (i = 0; i < count - 1; i++)
            for (j = 0; j < count - 1 - i; j++)
                if (lstrcmpW(fds[j].cFileName, fds[j+1].cFileName) < 0) {
                    WIN32_FIND_DATAW t = fds[j]; fds[j] = fds[j+1]; fds[j+1] = t;
                }

        if (count == 0) {
            BMCard_Create(g_hHome, 810, MARGIN, y, CARD_W * 3 + CARD_GAP * 2, 100,
                          L"暂无备份", L"点右上角「立即全量备份」创建第一个备份点",
                          L"备份包含: MBR · BCD · NVRAM 启动项", NULL, FALSE);
        } else {
            for (i = 0; i < count && i < 8; i++) {
                WCHAR line1[128], line2[128];
                swprintf(line1, 128, L"%s", fds[i].cFileName);
                swprintf(line2, 128, L"%lu KB", (unsigned long)(fds[i].nFileSizeLow / 1024));
                BMCard_Create(g_hHome, 820 + i, MARGIN, y, CARD_W * 3 + CARD_GAP * 2, 76,
                              line1, line2, NULL, NULL, FALSE);
                y += 76 + 10;
            }
            if (count > 8) {
                WCHAR l1[64];
                swprintf(l1, 64, L"… 共 %d 个文件，其余请在备份目录查看", count);
                BMCard_Create(g_hHome, 830, MARGIN, y, CARD_W * 3 + CARD_GAP * 2, 60,
                              l1, L"", NULL, NULL, FALSE);
            }
        }
    }
}

static void HandleBackupAction(WORD btn) {
    WCHAR dir[MAX_PATH];

    if (!BackupGetBackupDir(dir, MAX_PATH)) {
        MessageBoxW(g_hHome, L"无法定位备份目录", L"错误", MB_OK | MB_ICONERROR);
        return;
    }

    switch (btn) {
    case BTN_BACKUPNOW: {
        if (MessageBoxW(g_hHome, L"开始全量备份？\n\n包含: MBR · BCD · NVRAM 启动项",
                        L"确认备份", MB_YESNO | MB_ICONQUESTION) == IDYES) {
            if (BackupAll(dir, BACKUP_MBR | BACKUP_BCD | BACKUP_NVRAM)) {
                MessageBoxW(g_hHome, L"备份完成 ✓", L"成功", MB_OK | MB_ICONINFORMATION);
                RefreshBootMgrPage(); ShowPage(PAGE_BACKUP);
            } else {
                MessageBoxW(g_hHome, L"备份未全部成功，请查看日志", L"警告", MB_OK | MB_ICONWARNING);
                ClearPage(); ShowPage(PAGE_BACKUP);
            }
        }
        break;
    }
    case BTN_RESTOREMBR: {
        /* 恢复 = 从 backups 目录里最新的 mbr.bin 恢复引导码（走 ops 事务层） */
        WCHAR pattern[MAX_PATH];
        WIN32_FIND_DATAW fd;
        HANDLE h;
        WCHAR best[MAX_PATH] = {0};
        swprintf(pattern, MAX_PATH, L"%s\\*\\mbr.bin", dir);
        h = FindFirstFileW(pattern, &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                WCHAR full[MAX_PATH];
                swprintf(full, MAX_PATH, L"%s\\%s\\mbr.bin", dir, fd.cFileName);
                if (lstrcmpW(full, best) > 0) lstrcpynW(best, full, MAX_PATH);
            } while (FindNextFileW(h, &fd));
            FindClose(h);
        }
        if (!best[0]) {
            MessageBoxW(g_hHome, L"没有可用的 MBR 备份", L"提示", MB_OK | MB_ICONINFORMATION);
            return;
        }
        {
            WCHAR msg[MAX_PATH + 64];
            swprintf(msg, MAX_PATH + 64, L"用以下备份恢复 MBR 引导码？\n\n%s\n\n将保留当前分区表，并自动先做安全备份。", best);
            if (MessageBoxW(g_hHome, msg, L"确认恢复", MB_YESNO | MB_ICONWARNING) == IDYES) {
                int sysDisk = MBR_GetSystemDiskNumber();
                if (sysDisk < 0) sysDisk = 0;
                {
                    BM_RESULT r = OpsRestoreBootCodeFromFile(sysDisk, best);
                    MessageBoxW(g_hHome, r.message[0] ? r.message : L"恢复完成 ✓",
                                r.status == BM_OK ? L"成功" : L"失败",
                                r.status == BM_OK ? MB_OK | MB_ICONINFORMATION : MB_OK | MB_ICONERROR);
                }
            }
        }
        break;
    }
    case BTN_OPENDIR:
        ShellExecuteW(g_hHome, L"open", dir, NULL, NULL, SW_SHOWNORMAL);
        break;
    }
}

/* ---------------- 页面框架 ---------------- */

static void ClearPage(void) {
    HWND h = GetWindow(g_hHome, GW_CHILD);
    HWND next;
    while (h) {
        next = GetWindow(h, GW_HWNDNEXT);
        DestroyWindow(h);
        h = next;
    }
    RemovePropW(g_hHome, L"dash0"); RemovePropW(g_hHome, L"dash1");
    RemovePropW(g_hHome, L"dash2"); RemovePropW(g_hHome, L"dash3");
}

static void ShowPage(int page) {
    g_page = page;
    ClearPage();

    /* 顶栏：标题 + 返回 */
    {
        static HFONT hTitle = NULL;
        if (!hTitle)
            hTitle = CreateFontW(-19, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
        HWND hLabel = CreateWindowExW(0, L"STATIC",
            page == PAGE_DASH ? L"Boot Manager Pro"
            : page == PAGE_BOOTMGR ? L"🖥 引导管理"
            : page == PAGE_LOADER ? L"📦 引导器安装"
            : L"🛟 备份恢复",
            WS_CHILD | WS_VISIBLE | SS_OWNERDRAW, MARGIN, 28, 500, 32,
            g_hHome, NULL, NULL, NULL);
            SendMessageW(hLabel, WM_SETFONT, (WPARAM)hTitle, TRUE);
        if (page != PAGE_DASH)
            BMFlatButton_Create(g_hHome, BTN_BACK, MARGIN, 64, 96, 36, L"← 返回", FALSE, FALSE);
    }

    switch (page) {
    case PAGE_DASH:    BuildDashPage(); break;
    case PAGE_BOOTMGR: BuildBootMgrPage(); break;
    case PAGE_LOADER:  BuildLoaderPage(); break;
    case PAGE_BACKUP:  BuildBackupPage(); break;
    }
    InvalidateRect(g_hHome, NULL, TRUE);
}

static void HandleCardClick(int id, int code) {
    if (g_page == PAGE_DASH) {
        int page = 0;
        if (id == CID_BOOTMGR) page = PAGE_BOOTMGR;
        else if (id == CID_LOADER) page = PAGE_LOADER;
        else if (id == CID_BACKUP) page = PAGE_BACKUP;
        if (page) ShowPage(page);
        return;
    }
    if (g_page == PAGE_LOADER && (id == 800 || id == 801)) {
        HandleLoaderAction(id, code);
        return;
    }
    if (g_page == PAGE_BOOTMGR && id >= 100 && id < 100 + MAX_ROWS) {
        /* 行选中 */
        DWORD i;
        for (i = 0; i < (DWORD)g_rowCount; i++)
            if (g_rowCards[i])
                BMCard_SetSelected(g_rowCards[i], (100 + (int)i) == id);
        g_selectedId = 0;
        return;
    }
}

static LRESULT CALLBACK HomeProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE:
        /* 页面构建延迟到 g_hHome 赋值之后（见 HomeMain），否则子控件父窗口为 NULL */
        PostMessageW(hWnd, WM_APP + 1, 0, 0);
        return 0;

    case WM_APP + 1:
        ShowPage(PAGE_DASH);
        return 0;

    case WM_TIMER:
        if (wParam == 1) {
            KillTimer(hWnd, 1);
            {
                HWND c0 = (HWND)GetPropW(hWnd, L"dash0");
                HWND c1 = (HWND)GetPropW(hWnd, L"dash1");
                HWND c2 = (HWND)GetPropW(hWnd, L"dash2");
                HWND c3 = (HWND)GetPropW(hWnd, L"dash3");
                if (c0 && IsWindow(c0)) {
                    FillDashStatus(c0, c1, c2);
                    if (c3 && IsWindow(c3)) FillOverview(c3);
                }
            }
        }
        return 0;

    case WM_COMMAND: {
        int id = LOWORD(wParam);
        int code = HIWORD(wParam);

        if (id == BTN_BACK) { ShowPage(PAGE_DASH); return 0; }
        if (id == BTN_REFRESH) {
            if (g_page == PAGE_BOOTMGR) RefreshBootMgrPage();
            else if (g_page == PAGE_LOADER) RefreshLoaderPage();
            else if (g_page == PAGE_BACKUP) { ClearPage(); ShowPage(PAGE_BACKUP); }
            return 0;
        }
        if (g_page == PAGE_BOOTMGR &&
            (id == BTN_UP || id == BTN_DOWN || id == BTN_SETDEFAULT || id == BTN_DELETE)) {
            HandleBootMgrAction((WORD)id);
            return 0;
        }
        if (g_page == PAGE_BACKUP &&
            (id == BTN_BACKUPNOW || id == BTN_RESTOREMBR || id == BTN_OPENDIR)) {
            HandleBackupAction((WORD)id);
            return 0;
        }
        HandleCardClick(id, code);
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
        EndPaint(hWnd, &ps);
        return 0;
    }

    case WM_DESTROY:
        FreeBootList();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

int HomeMain(HINSTANCE hInst, int nCmdShow) {
    WNDCLASSEXW wc = {0};

    /* 需要管理员的操作占绝大多数：非 PE 环境自动提权 */
    if (!EnvIsWinPE() && !BootMgrIsAdmin()) {
        if (RelaunchAsAdmin()) return 0;   /* 提权实例接管，当前实例退出 */
        MessageBoxW(NULL,
            L"未以管理员身份运行，以下功能将不可用：\n· 读取/管理 UEFI 启动项\n· 安装引导器\n· 备份恢复",
            L"权限不足", MB_OK | MB_ICONWARNING);
    }

    BMCard_RegisterClass(hInst);
    BMFlatButton_RegisterClass(hInst);

    if (!g_bgBrush) g_bgBrush = CreateSolidBrush(HOME_BG);

    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = HomeProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = g_bgBrush;
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
