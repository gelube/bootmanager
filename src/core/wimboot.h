/**
 * WIM/VHD Boot Entry Management
 * 支持多种启动类型：WIM, VHD, RAM, WinPE, eSD, ISO
 */

#ifndef WIMBOOT_H
#define WIMBOOT_H

#include <windows.h>
#include <stdio.h>

// ============================================
// WIM 启动项
// ============================================

/**
 * 添加 WIM 启动项
 * 
 * @param name 启动项名称
 * @param wimPath WIM 文件完整路径
 * @param imageIndex 镜像索引 (如 "1" 或 "Windows 10 Pro")
 * @return TRUE 成功，FALSE 失败
 */
BOOL WimAddBootEntry(const WCHAR* name, const WCHAR* wimPath, const WCHAR* imageIndex);

// ============================================
// VHD 启动项
// ============================================

/**
 * 添加 VHD 启动项
 * 
 * @param name 启动项名称
 * @param vhdPath VHD/VHDX 文件完整路径
 * @return TRUE 成功，FALSE 失败
 */
BOOL VhdAddBootEntry(const WCHAR* name, const WCHAR* vhdPath);

// ============================================
// RAM 启动（WIM 加载到内存运行）
// ============================================

/**
 * 添加 RAM 启动项
 * 
 * @param name 启动项名称
 * @param wimPath WIM 文件路径
 * @param sdiPath boot.sdi 文件路径（可为 NULL，自动查找）
 * @return TRUE 成功，FALSE 失败
 * 
 * 说明：RAM 启动将 WIM 加载到内存运行，速度更快，不依赖硬盘
 */
BOOL RamAddBootEntry(const WCHAR* name, const WCHAR* wimPath, const WCHAR* sdiPath);

// ============================================
// WinPE 启动（Windows 预安装环境）
// ============================================

/**
 * 添加 WinPE 启动项
 * 
 * @param name 启动项名称
 * @param bootWimPath boot.wim 文件路径
 * @return TRUE 成功，FALSE 失败
 * 
 * 说明：WinPE 本质是 WIM 格式，通常为 boot.wim
 */
BOOL WinPeAddBootEntry(const WCHAR* name, const WCHAR* bootWimPath);

// ============================================
// eSD 启动（Windows 10+ 压缩映像）
// ============================================

/**
 * 添加 eSD 启动项
 * 
 * @param name 启动项名称
 * @param esdPath eSD 文件路径
 * @param imageIndex 镜像索引（可为 NULL）
 * @return TRUE 成功，FALSE 失败
 * 
 * 说明：eSD 是 Windows 10+ 的压缩安装格式，压缩率比 WIM 更高
 */
BOOL EsdAddBootEntry(const WCHAR* name, const WCHAR* esdPath, const WCHAR* imageIndex);

// ============================================
// ISO 启动（通过 Limine/Grub4dos）
// ============================================

/**
 * 添加 ISO 启动项
 * 
 * @param name 启动项名称
 * @param isoPath ISO 文件路径
 * @return TRUE 成功，FALSE 失败
 * 
 * 说明：ISO 启动需要 Limine 或 Grub4dos 支持
 */
BOOL IsoAddBootEntry(const WCHAR* name, const WCHAR* isoPath);

// ============================================
// 文件选择对话框
// ============================================

/** 选择 WIM 文件 */
BOOL WimSelectFileDialog(HWND hWnd, WCHAR* outPath, DWORD outPathSize);

/** 选择 VHD 文件 */
BOOL VhdSelectFileDialog(HWND hWnd, WCHAR* outPath, DWORD outPathSize);

/** 选择 RAM 启动文件（WIM/eSD） */
BOOL RamSelectFileDialog(HWND hWnd, WCHAR* outPath, DWORD outPathSize);

/** 选择 WinPE 文件（boot.wim） */
BOOL WinPeSelectFileDialog(HWND hWnd, WCHAR* outPath, DWORD outPathSize);

/** 选择 eSD 文件 */
BOOL EsdSelectFileDialog(HWND hWnd, WCHAR* outPath, DWORD outPathSize);

/** 选择 ISO 文件 */
BOOL IsoSelectFileDialog(HWND hWnd, WCHAR* outPath, DWORD outPathSize);

#endif // WIMBOOT_H
