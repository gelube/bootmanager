/**
 * advanced_page.c - MBR/Limine repair and UEFI repair page
 * 
 * 包含：
 * - MBR 修复
 * - Limine 引导管理器 (BIOS/MBR)
 * - PBR 备份/恢复
 * - UEFI 修复
 */

#include <windows.h>
#include <stdio.h>
#include "../../../include/advanced_page.h"
#include "../../../include/mbr_io.h"
#include "../../../include/uefi_repair.h"
#include "../../../include/limine.h"
#include "../../../include/esp.h"
#include "../../core/backup.h"  // 备份工具函数

#define ID_STATUS_TEXT       500
#define CONTENT_PADDING      32
#define BTN_HEIGHT           38

static HWND s_hStatus = NULL;

static void SetStatus(const WCHAR* t) { if (s_hStatus) SetWindowTextW(s_hStatus, t); }

static void DoMBRRepair(HWND hWnd)
{
    if (MessageBoxW(hWnd,
        L"此操作将恢复 Windows 默认 MBR 引导代码，不会影响分区表。\n\n"
        L"⚠ 高风险操作，请确保已备份重要数据！\n\n确定继续？",
        L"MBR 修复确认", MB_YESNO | MB_ICONWARNING) != IDYES) return;

    SetStatus(L"⏳ 正在修复 MBR...");

    if (RepairMBR_Native(0)) {
        MessageBoxW(hWnd, L"MBR 引导代码已成功写入。\n\n请重启计算机验证修复结果。",
            L"MBR 修复成功", MB_OK | MB_ICONINFORMATION);
        SetStatus(L"✓ MBR 修复完成");
    } else {
        DWORD err = GetLastError();
        WCHAR msg[256];
        swprintf(msg, 256,
            L"MBR 修复失败（错误码：%lu）。\n\n请确保以管理员身份运行。", err);
        MessageBoxW(hWnd, msg, L"MBR 修复失败", MB_OK | MB_ICONERROR);
        SetStatus(L"✗ MBR 修复失败");
    }
}

static void DoUEFIRepair(HWND hWnd)
{
    if (MessageBoxW(hWnd,
        L"此操作将复制 Windows EFI 引导文件到 ESP 分区。\n\n确定继续？",
        L"UEFI 修复确认", MB_YESNO | MB_ICONQUESTION) != IDYES) return;

    SetStatus(L"⏳ 正在修复 UEFI 引导链...");

    if (RepairUEFI_Native()) {
        MessageBoxW(hWnd,
            L"UEFI 引导文件已成功复制到 ESP 分区。\n\n请重启计算机验证修复结果。",
            L"UEFI 修复成功", MB_OK | MB_ICONINFORMATION);
        SetStatus(L"✓ UEFI 修复完成");
    } else {
        MessageBoxW(hWnd,
            L"UEFI 修复失败。可能原因：\n\n"
            L"• 未找到 Windows\\Boot\\EFI\\bootmgfw.efi\n"
            L"• ESP 分区挂载失败\n"
            L"• 权限不足（请以管理员身份运行）\n\n"
            L"如在 Windows PE 中，请使用 Windows 安装盘修复。",
            L"UEFI 修复失败", MB_OK | MB_ICONWARNING);
        SetStatus(L"✗ UEFI 修复失败");
    }
}

// ============================================
// Limine 安装
// ============================================
static void DoLimineInstall(HWND hWnd)
{
    WCHAR src[MAX_PATH] = {0};
    
    // 查找 Limine 源文件
    if (!LimineFindSource(src, MAX_PATH)) {
        MessageBoxW(hWnd,
            L"未找到 Limine 源文件。\n\n"
            L"请将 Limine 文件放入以下任一目录：\n"
            L"• <程序目录>\\limine\\\n"
            L"• <程序目录>\\resources\\limine\\\n"
            L"• Z:\\limine\\\n\n"
            L"必需文件：\n"
            L"• BIOS 模式: limine-bios.sys\n"
            L"• UEFI 模式: limine-efi\\BOOTX64.EFI",
            L"Limine 源文件缺失", MB_OK | MB_ICONWARNING);
        SetStatus(L"✗ 未找到 Limine 源文件");
        return;
    }
    
    // 检测当前引导模式
    typedef BOOL (WINAPI *GetFirmwareTypeFn)(PFIRMWARE_TYPE);
    HMODULE hKernel = GetModuleHandleW(L"kernel32.dll");
    GetFirmwareTypeFn fn = NULL;
    FIRMWARE_TYPE fwType = FirmwareTypeUnknown;
    BOOL isUefi = FALSE;
    
    if (hKernel) {
        fn = (GetFirmwareTypeFn)GetProcAddress(hKernel, "GetFirmwareType");
        if (fn && fn(&fwType)) {
            isUefi = (fwType == FirmwareTypeUefi);
        }
    }
    
    // 显示确认对话框
    WCHAR msg[512];
    swprintf(msg, 512,
        L"即将安装 Limine 引导管理器。\n\n"
        L"当前引导模式: %s\n"
        L"安装目标: %s\n\n"
        L"是否继续？",
        isUefi ? L"UEFI" : L"BIOS/MBR",
        isUefi ? L"ESP 分区 (EFI\\limine\\)" : L"系统磁盘 MBR (boot\\limine\\)");
    
    if (MessageBoxW(hWnd, msg, L"安装 Limine", MB_YESNO | MB_ICONQUESTION) != IDYES) {
        return;
    }
    
    SetStatus(L"⏳ 正在安装 Limine...");
    
    // 备份当前引导（保存到程序目录下的 backups 文件夹）
    SYSTEMTIME st;
    GetLocalTime(&st);
    
    WCHAR backupDir[MAX_PATH] = {0};
    BackupGetBackupDir(backupDir, MAX_PATH);
    
    if (!isUefi) {
        // MBR 模式备份
        WCHAR mbrBak[MAX_PATH];
        swprintf(mbrBak, MAX_PATH, L"%s\\MBR_before_limine_%04d%02d%02d_%02d%02d%02d.bin",
            backupDir, st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
        LimineBackupMBRFull(LimineGetSystemDiskIndex(), mbrBak);
    }
    
    // 执行安装
    BOOL success = LimineInstall(src);
    
    if (success) {
        MessageBoxW(hWnd,
            L"Limine 安装成功！\n\n"
            L"重启后将显示 Limine 启动菜单。\n\n"
            L"配置文件位置:\n"
            L"• BIOS: boot\\limine\\limine.conf\n"
            L"• UEFI: EFI\\limine\\limine.conf",
            L"安装成功", MB_OK | MB_ICONINFORMATION);
        SetStatus(L"✓ Limine 安装成功");
    } else {
        const WCHAR* err = LimineGetLastErrorMessage();
        WCHAR failMsg[1024];
        swprintf(failMsg, 1024,
            L"Limine 安装失败。\n\n"
            L"错误: %s\n\n"
            L"可能原因:\n"
            L"• 没有管理员权限\n"
            L"• ESP 分区挂载失败 (UEFI)\n"
            L"• 磁盘写入被拒绝 (BIOS)",
            (err && wcslen(err) > 0) ? err : L"未知错误");
        MessageBoxW(hWnd, failMsg, L"安装失败", MB_OK | MB_ICONERROR);
        SetStatus(L"✗ Limine 安装失败");
    }
}

// ============================================
// Limine 卸载
// ============================================
static void DoLimineUninstall(HWND hWnd)
{
    // 找到系统盘
    WCHAR systemDir[MAX_PATH];
    WCHAR drive[4] = {0};
    GetWindowsDirectoryW(systemDir, MAX_PATH);
    drive[0] = systemDir[0];
    drive[1] = L':';
    drive[2] = 0;
    
    // 检查是否已安装
    LIMINE_STATUS status = LimineCheckInstalled(drive);
    if (status == LIMINE_NOT_INSTALLED) {
        // UEFI 模式检查 ESP
        WCHAR esp[4] = {0};
        if (EspFind(esp, 4)) {
            status = LimineCheckInstalled(esp);
        }
    }
    
    if (status == LIMINE_NOT_INSTALLED) {
        MessageBoxW(hWnd, L"未检测到已安装的 Limine。", L"提示", MB_OK | MB_ICONINFORMATION);
        return;
    }
    
    const WCHAR* installType;
    switch (status) {
        case LIMINE_INSTALLED_MBR: installType = L"MBR 模式"; break;
        case LIMINE_INSTALLED_PBR: installType = L"PBR 模式"; break;
        case LIMINE_INSTALLED_UEFI: installType = L"UEFI 模式"; break;
        default: installType = L"未知"; break;
    }
    
    WCHAR msg[256];
    swprintf(msg, 256, L"检测到 Limine (%s)。\n\n确定要卸载吗？\n\n将尝试恢复原始引导。", installType);
    
    if (MessageBoxW(hWnd, msg, L"卸载 Limine", MB_YESNO | MB_ICONQUESTION) != IDYES) {
        return;
    }
    
    SetStatus(L"⏳ 正在卸载 Limine...");
    
    BOOL success = LimineUninstall(drive);
    
    // UEFI 模式也尝试卸载 ESP
    if (!success) {
        WCHAR esp[4] = {0};
        if (EspFind(esp, 4)) {
            success = LimineUninstall(esp);
        }
    }
    
    if (success) {
        MessageBoxW(hWnd, L"Limine 已卸载。\n\n原始引导已恢复。", L"卸载成功", MB_OK | MB_ICONINFORMATION);
        SetStatus(L"✓ Limine 已卸载");
    } else {
        MessageBoxW(hWnd, L"Limine 卸载失败。", L"卸载失败", MB_OK | MB_ICONERROR);
        SetStatus(L"✗ Limine 卸载失败");
    }
}

// ============================================
// PBR 备份
// ============================================
static void DoPBRBackup(HWND hWnd)
{
    WCHAR systemDir[MAX_PATH];
    WCHAR drive[4] = {0};
    GetWindowsDirectoryW(systemDir, MAX_PATH);
    drive[0] = systemDir[0];
    drive[1] = L':';
    drive[2] = 0;
    
    // 获取备份目录（程序目录下的 backups 文件夹）
    WCHAR backupDir[MAX_PATH] = {0};
    if (!BackupGetBackupDir(backupDir, MAX_PATH)) {
        MessageBoxW(hWnd, L"无法创建备份目录。", L"错误", MB_OK | MB_ICONERROR);
        return;
    }
    
    SYSTEMTIME st;
    GetLocalTime(&st);
    
    WCHAR file[MAX_PATH];
    swprintf(file, MAX_PATH, L"%s\\PBR_%c_%04d%02d%02d_%02d%02d%02d.bin",
        backupDir, drive[0], st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    
    SetStatus(L"⏳ 正在备份 PBR...");
    
    if (LimineBackupPBR(drive, file)) {
        WCHAR msg[MAX_PATH + 128];
        swprintf(msg, MAX_PATH + 128, 
            L"PBR 备份成功！\n\n"
            L"驱动器: %c:\n"
            L"备份位置:\n%s",
            drive[0], file);
        MessageBoxW(hWnd, msg, L"备份成功", MB_OK | MB_ICONINFORMATION);
        
        // 状态栏显示简短路径
        WCHAR status[128];
        swprintf(status, 128, L"✓ PBR 已备份到 backups\\PBR_%c_*.bin", drive[0]);
        SetStatus(status);
    } else {
        MessageBoxW(hWnd, L"PBR 备份失败。\n\n请确保以管理员身份运行。", L"备份失败", MB_OK | MB_ICONERROR);
        SetStatus(L"✗ PBR 备份失败");
    }
}

// ============================================
// PBR 恢复
// ============================================
static void DoPRRRestore(HWND hWnd)
{
    // 获取备份目录
    WCHAR backupDir[MAX_PATH] = {0};
    BackupGetBackupDir(backupDir, MAX_PATH);
    
    OPENFILENAMEW ofn = {0};
    WCHAR file[MAX_PATH] = {0};
    
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hWnd;
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = L"PBR Backup (*.bin)\0*.bin\0All Files (*.*)\0*.*\0";
    ofn.lpstrTitle = L"选择 PBR 备份文件";
    ofn.lpstrInitialDir = backupDir;  // 默认打开备份目录
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    
    if (!GetOpenFileNameW(&ofn)) return;
    
    // 从文件名推断驱动器
    WCHAR drive[4] = {0};
    WCHAR* p = wcsrchr(file, L'\\');
    if (p) {
        // 文件名格式: PBR_X_日期时间.bin
        WCHAR* fname = p + 1;
        if (wcslen(fname) > 5 && wcsncmp(fname, L"PBR_", 4) == 0) {
            drive[0] = fname[4];  // PBR_X_... 中的 X
            drive[1] = L':';
            drive[2] = 0;
        }
    }
    
    if (drive[0] == 0) {
        // 使用系统盘
        WCHAR systemDir[MAX_PATH];
        GetWindowsDirectoryW(systemDir, MAX_PATH);
        drive[0] = systemDir[0];
        drive[1] = L':';
        drive[2] = 0;
    }
    
    WCHAR msg[256];
    swprintf(msg, 256, L"确定恢复 PBR 到驱动器 %c: ?\n\n这将覆盖当前分区引导记录。", drive[0]);
    
    if (MessageBoxW(hWnd, msg, L"确认恢复", MB_YESNO | MB_ICONWARNING) != IDYES) {
        return;
    }
    
    SetStatus(L"⏳ 正在恢复 PBR...");
    
    if (LimineRestorePBR(drive, file)) {
        MessageBoxW(hWnd, L"PBR 恢复成功！", L"恢复成功", MB_OK | MB_ICONINFORMATION);
        SetStatus(L"✓ PBR 已恢复");
    } else {
        MessageBoxW(hWnd, L"PBR 恢复失败。\n\n请确保以管理员身份运行。", L"恢复失败", MB_OK | MB_ICONERROR);
        SetStatus(L"✗ PBR 恢复失败");
    }
}

// ============================================
// 设置活动分区
// ============================================
static void DoSetActivePartition(HWND hWnd)
{
    MessageBoxW(hWnd,
        L"设置活动分区功能需要选择目标分区。\n\n"
        L"此功能仅适用于 MBR 分区表。\n\n"
        L"请谨慎操作，错误的设置可能导致系统无法启动。",
        L"设置活动分区", MB_OK | MB_ICONINFORMATION);
}

void AdvancedPageCommand(HWND hWnd, WPARAM wParam)
{
    switch (LOWORD(wParam)) {
        case ID_BTN_MBR_REPAIR:      DoMBRRepair(hWnd); break;
        case ID_BTN_UEFI_REPAIR:     DoUEFIRepair(hWnd); break;
        case ID_BTN_LIMINE_INSTALL:  DoLimineInstall(hWnd); break;
        case ID_BTN_LIMINE_UNINSTALL: DoLimineUninstall(hWnd); break;
        case ID_BTN_PBR_BACKUP:      DoPBRBackup(hWnd); break;
        case ID_BTN_PBR_RESTORE:     DoPRRRestore(hWnd); break;
        case ID_BTN_SET_ACTIVE:      DoSetActivePartition(hWnd); break;
    }
}

void AdvancedPageBuild(HWND hParent, HFONT fontTitle, HFONT fontBody, HFONT fontSmall)
{
    s_hStatus = NULL;

    RECT rc; GetClientRect(hParent, &rc);
    int w = rc.right - CONTENT_PADDING * 2;
    int h = rc.bottom;
    int y = CONTENT_PADDING;
    const int labelH = 22, descH = 20, btnH = BTN_HEIGHT, gap = 6, sectionGap = 24;

    // 标题
    HWND hTitle = CreateWindowExW(0, L"STATIC", L"MBR 引导管理",
        WS_CHILD | WS_VISIBLE, CONTENT_PADDING, y, w, 32,
        hParent, NULL, NULL, NULL);
    SendMessageW(hTitle, WM_SETFONT, (WPARAM)fontTitle, TRUE);
    y += 40;

    // 描述
    HWND hDesc = CreateWindowExW(0, L"STATIC",
        L"MBR 修复、Limine 引导管理器、PBR 备份恢复。",
        WS_CHILD | WS_VISIBLE,
        CONTENT_PADDING, y, w, 24,
        hParent, NULL, NULL, NULL);
    SendMessageW(hDesc, WM_SETFONT, (WPARAM)fontBody, TRUE);
    y += 36;

    // ========== MBR 修复 ==========
    HWND hMBRLabel = CreateWindowExW(0, L"STATIC", L"MBR 修复",
        WS_CHILD | WS_VISIBLE,
        CONTENT_PADDING, y, w, labelH,
        hParent, NULL, NULL, NULL);
    SendMessageW(hMBRLabel, WM_SETFONT, (WPARAM)fontBody, TRUE);
    y += labelH + gap;

    HWND hMBRDesc = CreateWindowExW(0, L"STATIC",
        L"恢复 Windows 默认 MBR 引导代码，不影响分区表。",
        WS_CHILD | WS_VISIBLE,
        CONTENT_PADDING, y, w, descH,
        hParent, NULL, NULL, NULL);
    SendMessageW(hMBRDesc, WM_SETFONT, (WPARAM)fontSmall, TRUE);
    y += descH + gap;

    HWND hBtnMBR = CreateWindowExW(0, L"BUTTON", L"MBR 修复",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        CONTENT_PADDING, y, 140, btnH,
        hParent, (HMENU)ID_BTN_MBR_REPAIR, NULL, NULL);
    SendMessageW(hBtnMBR, WM_SETFONT, (WPARAM)fontBody, TRUE);
    y += btnH + sectionGap;

    // ========== Limine 引导管理器 ==========
    HWND hLimineLabel = CreateWindowExW(0, L"STATIC", L"Limine 引导管理器",
        WS_CHILD | WS_VISIBLE,
        CONTENT_PADDING, y, w, labelH,
        hParent, NULL, NULL, NULL);
    SendMessageW(hLimineLabel, WM_SETFONT, (WPARAM)fontBody, TRUE);
    y += labelH + gap;

    HWND hLimineDesc = CreateWindowExW(0, L"STATIC",
        L"现代化跨平台引导管理器，支持 BIOS/MBR 和 UEFI 模式。",
        WS_CHILD | WS_VISIBLE,
        CONTENT_PADDING, y, w, descH,
        hParent, NULL, NULL, NULL);
    SendMessageW(hLimineDesc, WM_SETFONT, (WPARAM)fontSmall, TRUE);
    y += descH + gap;

    HWND hBtnLimineInstall = CreateWindowExW(0, L"BUTTON", L"安装 Limine",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        CONTENT_PADDING, y, 120, btnH,
        hParent, (HMENU)ID_BTN_LIMINE_INSTALL, NULL, NULL);
    SendMessageW(hBtnLimineInstall, WM_SETFONT, (WPARAM)fontBody, TRUE);

    HWND hBtnLimineUninstall = CreateWindowExW(0, L"BUTTON", L"卸载 Limine",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        CONTENT_PADDING + 130, y, 120, btnH,
        hParent, (HMENU)ID_BTN_LIMINE_UNINSTALL, NULL, NULL);
    SendMessageW(hBtnLimineUninstall, WM_SETFONT, (WPARAM)fontBody, TRUE);
    y += btnH + sectionGap;

    // ========== PBR 备份恢复 ==========
    HWND hPBRLabel = CreateWindowExW(0, L"STATIC", L"PBR 分区引导记录",
        WS_CHILD | WS_VISIBLE,
        CONTENT_PADDING, y, w, labelH,
        hParent, NULL, NULL, NULL);
    SendMessageW(hPBRLabel, WM_SETFONT, (WPARAM)fontBody, TRUE);
    y += labelH + gap;

    HWND hPBRDesc = CreateWindowExW(0, L"STATIC",
        L"备份和恢复分区引导记录（Partition Boot Record）。",
        WS_CHILD | WS_VISIBLE,
        CONTENT_PADDING, y, w, descH,
        hParent, NULL, NULL, NULL);
    SendMessageW(hPBRDesc, WM_SETFONT, (WPARAM)fontSmall, TRUE);
    y += descH + gap;

    HWND hBtnPBRBackup = CreateWindowExW(0, L"BUTTON", L"备份 PBR",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        CONTENT_PADDING, y, 100, btnH,
        hParent, (HMENU)ID_BTN_PBR_BACKUP, NULL, NULL);
    SendMessageW(hBtnPBRBackup, WM_SETFONT, (WPARAM)fontBody, TRUE);

    HWND hBtnPBRRestore = CreateWindowExW(0, L"BUTTON", L"恢复 PBR",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        CONTENT_PADDING + 110, y, 100, btnH,
        hParent, (HMENU)ID_BTN_PBR_RESTORE, NULL, NULL);
    SendMessageW(hBtnPBRRestore, WM_SETFONT, (WPARAM)fontBody, TRUE);
    y += btnH + sectionGap;

    // ========== UEFI 修复 ==========
    HWND hUEFILabel = CreateWindowExW(0, L"STATIC", L"UEFI 修复",
        WS_CHILD | WS_VISIBLE,
        CONTENT_PADDING, y, w, labelH,
        hParent, NULL, NULL, NULL);
    SendMessageW(hUEFILabel, WM_SETFONT, (WPARAM)fontBody, TRUE);
    y += labelH + gap;

    HWND hUEFIDesc = CreateWindowExW(0, L"STATIC",
        L"检查 ESP 分区的 Windows UEFI 引导链完整性。",
        WS_CHILD | WS_VISIBLE,
        CONTENT_PADDING, y, w, descH,
        hParent, NULL, NULL, NULL);
    SendMessageW(hUEFIDesc, WM_SETFONT, (WPARAM)fontSmall, TRUE);
    y += descH + gap;

    HWND hBtnUEFI = CreateWindowExW(0, L"BUTTON", L"UEFI 修复",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        CONTENT_PADDING, y, 140, btnH,
        hParent, (HMENU)ID_BTN_UEFI_REPAIR, NULL, NULL);
    SendMessageW(hBtnUEFI, WM_SETFONT, (WPARAM)fontBody, TRUE);
    y += btnH + 20;

    // 状态栏
    s_hStatus = CreateWindowExW(0, L"STATIC", L"就绪",
        WS_CHILD | WS_VISIBLE,
        CONTENT_PADDING, h - 30, w, 24,
        hParent, (HMENU)ID_STATUS_TEXT, NULL, NULL);
    SendMessageW(s_hStatus, WM_SETFONT, (WPARAM)fontSmall, TRUE);
}
