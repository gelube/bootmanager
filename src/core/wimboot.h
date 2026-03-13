/**
 * WIM/VHD Boot Entry Management
 * 添加 WIM 和 VHD 启动项支持
 */

#ifndef WIMBOOT_H
#define WIMBOOT_H

#include <windows.h>
#include <stdio.h>

/**
 * 添加 WIM 启动项
 * 
 * @param name 启动项名称
 * @param wimPath WIM 文件完整路径
 * @param imageIndex 镜像索引 (如 "1" 或 "Windows 10 Pro")
 * @return TRUE 成功，FALSE 失败
 */
BOOL WimAddBootEntry(const WCHAR* name, const WCHAR* wimPath, const WCHAR* imageIndex);

/**
 * 添加 VHD 启动项
 * 
 * @param name 启动项名称
 * @param vhdPath VHD/VHDX 文件完整路径
 * @return TRUE 成功，FALSE 失败
 */
BOOL VhdAddBootEntry(const WCHAR* name, const WCHAR* vhdPath);

/**
 * 从文件对话框选择 WIM 文件
 * 
 * @param hWnd 父窗口句柄
 * @param outPath 输出路径缓冲区
 * @param outPathSize 缓冲区大小
 * @return TRUE 用户选择了文件，FALSE 取消或失败
 */
BOOL WimSelectFileDialog(HWND hWnd, WCHAR* outPath, DWORD outPathSize);

/**
 * 从文件对话框选择 VHD 文件
 * 
 * @param hWnd 父窗口句柄
 * @param outPath 输出路径缓冲区
 * @param outPathSize 缓冲区大小
 * @return TRUE 用户选择了文件，FALSE 取消或失败
 */
BOOL VhdSelectFileDialog(HWND hWnd, WCHAR* outPath, DWORD outPathSize);

#endif // WIMBOOT_H
