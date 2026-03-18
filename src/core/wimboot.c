/**
 * WIM/VHD Boot Entry Management - Implementation
 * 通过 BCD 或 rEFInd 配置添加各种启动项
 * 
 * 支持类型：
 * - WIM: Windows 映像文件
 * - VHD: 虚拟磁盘
 * - RAM: 内存启动（WIM + boot.sdi）
 * - WinPE: Windows 预安装环境（本质是 WIM）
 * - eSD: Windows 压缩映像（Windows 10+）
 * - ISO: 光盘镜像（需要 Limine/Grub4dos）
 */

#include "wimboot.h"
#include "refind_config.h"
#include <wchar.h>
#include <stdlib.h>
#include <string.h>
#include <commctrl.h>
#include <shlobj.h>

#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "shell32.lib")

// ============================================
// 内部辅助函数
// ============================================

// 查找 boot.sdi（RAM 启动必需）
static BOOL FindBootSdi(const WCHAR* wimPath, WCHAR* sdiPath, DWORD sdiSize)
{
    WCHAR p[MAX_PATH];
    
    if (!wimPath || !sdiPath || sdiSize < 4) return FALSE;
    
    // 方法1: WIM 同目录下的 boot.sdi
    wcsncpy(p, wimPath, MAX_PATH);
    WCHAR* lastSlash = wcsrchr(p, L'\\');
    if (lastSlash) {
        wcscpy(lastSlash + 1, L"boot.sdi");
        if (GetFileAttributesW(p) != INVALID_FILE_ATTRIBUTES) {
            wcsncpy(sdiPath, p, sdiSize);
            return TRUE;
        }
    }
    
    // 方法2: WIM 所在盘的 \boot\boot.sdi
    if (wimPath[1] == L':') {
        swprintf(p, MAX_PATH, L"%c:\\boot\\boot.sdi", wimPath[0]);
        if (GetFileAttributesW(p) != INVALID_FILE_ATTRIBUTES) {
            wcsncpy(sdiPath, p, sdiSize);
            return TRUE;
        }
    }
    
    // 方法3: Windows 系统目录
    WCHAR winDir[MAX_PATH];
    GetWindowsDirectoryW(winDir, MAX_PATH);
    swprintf(p, MAX_PATH, L"%s\\boot\\boot.sdi", winDir);
    if (GetFileAttributesW(p) != INVALID_FILE_ATTRIBUTES) {
        wcsncpy(sdiPath, p, sdiSize);
        return TRUE;
    }
    
    return FALSE;
}

// 获取 ESP 盘符
static BOOL GetESPDrive(WCHAR* espDrive, DWORD size)
{
    if (!espDrive || size < 4) return FALSE;
    
    // 查找已挂载的 ESP
    for (WCHAR d = L'B'; d <= L'Z'; d++) {
        WCHAR probe[MAX_PATH];
        swprintf(probe, MAX_PATH, L"%c:\\EFI", d);
        if (GetFileAttributesW(probe) != INVALID_FILE_ATTRIBUTES) {
            swprintf(espDrive, size, L"%c:", d);
            return TRUE;
        }
    }
    
    return FALSE;
}

// Windows 路径转 EFI 路径
static void WinPathToEfiPath(const WCHAR* winPath, WCHAR* efiPath, DWORD size)
{
    if (!winPath || !efiPath || size == 0) return;
    
    const WCHAR* src = winPath;
    if (src[1] == L':') src += 2;  // 跳过盘符
    
    wcsncpy(efiPath, src, size);
    efiPath[size - 1] = L'\0';
}

// ============================================
// WIM 启动项
// ============================================
BOOL WimAddBootEntry(const WCHAR* name, const WCHAR* wimPath, const WCHAR* imageIndex) {
    if (!name || !wimPath) return FALSE;

    WCHAR efiPath[MAX_PATH] = {0};
    WinPathToEfiPath(wimPath, efiPath, MAX_PATH);

    WCHAR espDrive[4] = {0};
    if (!GetESPDrive(espDrive, 4)) return FALSE;

    WCHAR options[128] = {0};
    if (imageIndex && wcslen(imageIndex) > 0)
        swprintf(options, 128, L"index=%s", imageIndex);

    return RefindConfigAddMenuEntry(espDrive, name,
        L"\\EFI\\refind\\tools\\wimboot.efi", options[0] ? options : NULL);
}

// ============================================
// VHD 启动项
// ============================================
BOOL VhdAddBootEntry(const WCHAR* name, const WCHAR* vhdPath) {
    if (!name || !vhdPath) return FALSE;

    WCHAR efiPath[MAX_PATH] = {0};
    WinPathToEfiPath(vhdPath, efiPath, MAX_PATH);

    WCHAR espDrive[4] = {0};
    if (!GetESPDrive(espDrive, 4)) return FALSE;

    WCHAR options[MAX_PATH + 16];
    swprintf(options, MAX_PATH + 16, L"vhd=%s", efiPath);

    return RefindConfigAddMenuEntry(espDrive, name,
        L"\\EFI\\Microsoft\\Boot\\bootmgfw.efi", options);
}

// ============================================
// RAM 启动（WIM 加载到内存）
// ============================================
BOOL RamAddBootEntry(const WCHAR* name, const WCHAR* wimPath, const WCHAR* sdiPath)
{
    if (!name || !wimPath) return FALSE;
    
    // 查找 boot.sdi
    WCHAR foundSdi[MAX_PATH] = {0};
    if (!sdiPath || wcslen(sdiPath) == 0) {
        if (!FindBootSdi(wimPath, foundSdi, MAX_PATH)) {
            return FALSE;  // 没有 boot.sdi 无法 RAM 启动
        }
        sdiPath = foundSdi;
    }
    
    // RAM 启动需要：
    // 1. boot.sdi 作为 ramdisk 镜像
    // 2. WIM 文件路径
    // 3. 可选的映像索引
    
    WCHAR espDrive[4] = {0};
    if (!GetESPDrive(espDrive, 4)) return FALSE;
    
    WCHAR efiWimPath[MAX_PATH] = {0};
    WCHAR efiSdiPath[MAX_PATH] = {0};
    WinPathToEfiPath(wimPath, efiWimPath, MAX_PATH);
    WinPathToEfiPath(sdiPath, efiSdiPath, MAX_PATH);
    
    // 构建 ramdisk 选项
    // wimboot 格式: initrd=boot.sdi path\to\file.wim
    WCHAR options[MAX_PATH * 2];
    swprintf(options, MAX_PATH * 2, L"initrd=%s %s", efiSdiPath, efiWimPath);
    
    return RefindConfigAddMenuEntry(espDrive, name,
        L"\\EFI\\refind\\tools\\wimboot.efi", options);
}

// ============================================
// WinPE 启动（本质是 WIM）
// ============================================
BOOL WinPeAddBootEntry(const WCHAR* name, const WCHAR* bootWimPath)
{
    // WinPE 的 boot.wim 通常只有一个映像（索引 1）
    return WimAddBootEntry(name, bootWimPath, L"1");
}

// ============================================
// eSD 启动（Windows 10+ 压缩映像）
// ============================================
BOOL EsdAddBootEntry(const WCHAR* name, const WCHAR* esdPath, const WCHAR* imageIndex)
{
    // eSD 格式类似 WIM，但压缩率更高
    // wimboot 支持直接加载 eSD
    
    if (!name || !esdPath) return FALSE;
    
    WCHAR espDrive[4] = {0};
    if (!GetESPDrive(espDrive, 4)) return FALSE;
    
    WCHAR efiPath[MAX_PATH] = {0};
    WinPathToEfiPath(esdPath, efiPath, MAX_PATH);
    
    WCHAR options[128] = {0};
    if (imageIndex && wcslen(imageIndex) > 0) {
        swprintf(options, 128, L"index=%s", imageIndex);
    }
    
    return RefindConfigAddMenuEntry(espDrive, name,
        L"\\EFI\\refind\\tools\\wimboot.efi", options[0] ? options : NULL);
}

// ============================================
// ISO 启动（通过 Limine）
// ============================================
BOOL IsoAddBootEntry(const WCHAR* name, const WCHAR* isoPath)
{
    if (!name || !isoPath) return FALSE;
    
    // ISO 启动需要 Limine 或 Grub4dos
    // Limine 配置格式：
    // /Entry Name
    //     protocol: chain
    //     path: /path/to/file.iso
    
    WCHAR espDrive[4] = {0};
    if (!GetESPDrive(espDrive, 4)) return FALSE;
    
    WCHAR efiPath[MAX_PATH] = {0};
    WinPathToEfiPath(isoPath, efiPath, MAX_PATH);
    
    // 添加 Limine 配置条目
    // 需要 limine.conf 支持 chain loading
    return RefindConfigAddMenuEntry(espDrive, name, efiPath, L"chain");
}

// ============================================
// 文件选择对话框
// ============================================
BOOL WimSelectFileDialog(HWND hWnd, WCHAR* outPath, DWORD outPathSize) {
    if (!outPath || outPathSize < MAX_PATH) return FALSE;
    
    WCHAR szFile[MAX_PATH] = L"";
    
    OPENFILENAMEW ofn = {0};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hWnd;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = L"WIM 文件 (*.wim;*.swm)\0*.wim;*.swm\0所有文件 (*.*)\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrFileTitle = NULL;
    ofn.nMaxFileTitle = 0;
    ofn.lpstrInitialDir = NULL;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
    
    if (GetOpenFileNameW(&ofn)) {
        wcsncpy(outPath, szFile, outPathSize);
        return TRUE;
    }
    
    return FALSE;
}

BOOL VhdSelectFileDialog(HWND hWnd, WCHAR* outPath, DWORD outPathSize) {
    if (!outPath || outPathSize < MAX_PATH) return FALSE;
    
    WCHAR szFile[MAX_PATH] = L"";
    
    OPENFILENAMEW ofn = {0};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hWnd;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = L"VHD 文件 (*.vhd;*.vhdx)\0*.vhd;*.vhdx\0所有文件 (*.*)\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrFileTitle = NULL;
    ofn.nMaxFileTitle = 0;
    ofn.lpstrInitialDir = NULL;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
    
    if (GetOpenFileNameW(&ofn)) {
        wcsncpy(outPath, szFile, outPathSize);
        return TRUE;
    }
    
    return FALSE;
}

BOOL RamSelectFileDialog(HWND hWnd, WCHAR* outPath, DWORD outPathSize) {
    if (!outPath || outPathSize < MAX_PATH) return FALSE;
    
    WCHAR szFile[MAX_PATH] = L"";
    
    OPENFILENAMEW ofn = {0};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hWnd;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = L"启动映像 (*.wim;*.esd)\0*.wim;*.esd\0所有文件 (*.*)\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrFileTitle = NULL;
    ofn.nMaxFileTitle = 0;
    ofn.lpstrInitialDir = NULL;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
    
    if (GetOpenFileNameW(&ofn)) {
        wcsncpy(outPath, szFile, outPathSize);
        return TRUE;
    }
    
    return FALSE;
}

BOOL WinPeSelectFileDialog(HWND hWnd, WCHAR* outPath, DWORD outPathSize) {
    if (!outPath || outPathSize < MAX_PATH) return FALSE;
    
    WCHAR szFile[MAX_PATH] = L"";
    
    OPENFILENAMEW ofn = {0};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hWnd;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = L"WinPE 启动文件 (boot.wim)\0boot.wim\0WIM 文件 (*.wim)\0*.wim\0所有文件 (*.*)\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrFileTitle = NULL;
    ofn.nMaxFileTitle = 0;
    ofn.lpstrInitialDir = NULL;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
    
    if (GetOpenFileNameW(&ofn)) {
        wcsncpy(outPath, szFile, outPathSize);
        return TRUE;
    }
    
    return FALSE;
}

BOOL EsdSelectFileDialog(HWND hWnd, WCHAR* outPath, DWORD outPathSize) {
    if (!outPath || outPathSize < MAX_PATH) return FALSE;
    
    WCHAR szFile[MAX_PATH] = L"";
    
    OPENFILENAMEW ofn = {0};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hWnd;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = L"eSD 文件 (*.esd)\0*.esd\0所有文件 (*.*)\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrFileTitle = NULL;
    ofn.nMaxFileTitle = 0;
    ofn.lpstrInitialDir = NULL;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
    
    if (GetOpenFileNameW(&ofn)) {
        wcsncpy(outPath, szFile, outPathSize);
        return TRUE;
    }
    
    return FALSE;
}

BOOL IsoSelectFileDialog(HWND hWnd, WCHAR* outPath, DWORD outPathSize) {
    if (!outPath || outPathSize < MAX_PATH) return FALSE;
    
    WCHAR szFile[MAX_PATH] = L"";
    
    OPENFILENAMEW ofn = {0};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hWnd;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = L"ISO 镜像 (*.iso)\0*.iso\0所有文件 (*.*)\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrFileTitle = NULL;
    ofn.nMaxFileTitle = 0;
    ofn.lpstrInitialDir = NULL;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
    
    if (GetOpenFileNameW(&ofn)) {
        wcsncpy(outPath, szFile, outPathSize);
        return TRUE;
    }
    
    return FALSE;
}
