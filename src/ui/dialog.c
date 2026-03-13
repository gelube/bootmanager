/**
 * Boot Manager Pro v3 - Dialog Implementation
 * 添加 EFI 启动项对话框
 * 
 * 功能:
 * - 模态对话框，类似 BOOTICE 风格
 * - 自动枚举物理磁盘
 * - 自动搜索 ESP 分区
 * - 文件浏览选择 EFI 文件
 * - 验证输入并创建启动项
 */

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <shlobj.h>
#include <wchar.h>
#include <stdio.h>
#include "dialog.h"

// ============================================
// 获取磁盘类型字符串
// ============================================
static const WCHAR* GetDriveTypeString(UINT type)
{
    switch (type) {
        case DRIVE_FIXED:     return L"本地磁盘";
        case DRIVE_REMOVABLE: return L"可移动磁盘";
        case DRIVE_REMOTE:    return L"网络磁盘";
        case DRIVE_CDROM:     return L"光盘";
        case DRIVE_RAMDISK:   return L"RAM 磁盘";
        default:              return L"未知";
    }
}

// ============================================
// 获取磁盘大小 (使用 DeviceIoControl)
// ============================================
static BOOL GetDiskSize(HANDLE hDevice, ULONGLONG* sizeInBytes)
{
    DISK_GEOMETRY_EX geometry = {0};
    DWORD bytesReturned = 0;
    
    if (DeviceIoControl(hDevice, IOCTL_DISK_GET_DRIVE_GEOMETRY_EX,
        NULL, 0, &geometry, sizeof(geometry), &bytesReturned, NULL)) {
        *sizeInBytes = geometry.DiskSize.QuadPart;
        return TRUE;
    }
    
    return FALSE;
}

// ============================================
// 格式化磁盘大小显示
// ============================================
static void FormatDiskSize(ULONGLONG bytes, WCHAR* outBuffer, INT bufferSize)
{
    if (bytes >= 1000000000000ULL) {
        swprintf(outBuffer, bufferSize, L"%.1fTB", bytes / 1000000000000.0);
    } else if (bytes >= 1000000000ULL) {
        swprintf(outBuffer, bufferSize, L"%.0fGB", bytes / 1000000000.0);
    } else if (bytes >= 1000000ULL) {
        swprintf(outBuffer, bufferSize, L"%.0fMB", bytes / 1000000.0);
    } else {
        swprintf(outBuffer, bufferSize, L"%lluB", bytes);
    }
}

// ============================================
// 枚举物理磁盘
// 使用 \\\\.\\PhysicalDriveX 方式检测
// 显示详细信息：磁盘编号 - 容量 (类型)
// ============================================
INT EnumPhysicalDisks(DISK_INFO** disks)
{
    INT count = 0;
    INT maxDisks = 16;  // 最多支持 16 个磁盘
    
    *disks = (DISK_INFO*)calloc(maxDisks, sizeof(DISK_INFO));
    if (!*disks) return 0;
    
    // 枚举 PhysicalDrive0 ~ PhysicalDrive15
    for (INT i = 0; i < maxDisks; i++) {
        WCHAR devicePath[64];
        swprintf(devicePath, 64, L"\\\\.\\PhysicalDrive%d", i);
        
        // 尝试打开磁盘设备
        HANDLE hDevice = CreateFileW(devicePath, 0, 
            FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, 
            OPEN_EXISTING, 0, NULL);
        
        if (hDevice != INVALID_HANDLE_VALUE) {
            // 磁盘存在
            (*disks)[count].diskNumber = i;
            
            // 获取磁盘大小
            ULONGLONG diskSize = 0;
            if (GetDiskSize(hDevice, &diskSize)) {
                WCHAR sizeStr[32] = {0};
                FormatDiskSize(diskSize, sizeStr, 32);
                
                // 检测磁盘类型 (通过检查第一个分区的类型)
                UINT driveType = DRIVE_FIXED;  // 默认为本地磁盘
                for (WCHAR d = L'C'; d <= L'Z'; d++) {
                    WCHAR root[4] = {d, L':', L'\\', 0};
                    UINT type = GetDriveTypeW(root);
                    if (type == DRIVE_FIXED || type == DRIVE_REMOVABLE) {
                        // 检查是否属于此物理磁盘 (简化：假设第一个检测到的)
                        driveType = type;
                        break;
                    }
                }
                
                const WCHAR* typeStr = GetDriveTypeString(driveType);
                swprintf((*disks)[count].diskName, 64, L"磁盘 %d - %s (%s)", i, sizeStr, typeStr);
            } else {
                // 无法获取大小时的备用显示
                swprintf((*disks)[count].diskName, 64, L"磁盘 %d", i);
            }
            
            (*disks)[count].hasESP = FALSE;
            (*disks)[count].espDrive[0] = L'\0';
            count++;
            CloseHandle(hDevice);
        } else {
            // 如果前 3 个磁盘都没找到，停止搜索
            if (i < 3 && count == 0) continue;
            if (i >= 3 && count > 0) break;
        }
    }
    
    // 搜索每个磁盘的 ESP 分区
    for (INT i = 0; i < count; i++) {
        // 枚举盘符，检查是否为 FAT32 且有 EFI 文件夹
        for (WCHAR d = L'C'; d <= L'Z'; d++) {
            WCHAR root[4] = {d, L':', L'\\', 0};
            
            if (GetDriveTypeW(root) == DRIVE_FIXED || 
                GetDriveTypeW(root) == DRIVE_REMOVABLE) {
                
                // 检查文件系统
                WCHAR fsName[32] = {0};
                if (GetVolumeInformationW(root, NULL, 0, NULL, NULL, NULL, fsName, 32)) {
                    // 检查是否为 FAT32
                    if (_wcsicmp(fsName, L"FAT32") == 0 || 
                        _wcsicmp(fsName, L"FAT") == 0) {
                        
                        // 检查是否有 EFI 文件夹
                        WCHAR efiPath[MAX_PATH];
                        swprintf(efiPath, MAX_PATH, L"%sEFI", root);
                        
                        if (GetFileAttributesW(efiPath) != INVALID_FILE_ATTRIBUTES) {
                            (*disks)[i].hasESP = TRUE;
                            (*disks)[i].espDrive[0] = d;
                            (*disks)[i].espDrive[1] = L':';
                            (*disks)[i].espDrive[2] = L'\0';
                            break;
                        }
                    }
                }
            }
        }
    }
    
    return count;
}

// ============================================
// 枚举 ESP 分区
// 遍历所有盘符，查找 FAT32 格式且有 EFI 文件夹的分区
// 同时检查 mountvol /S 挂载的分区
// ============================================
INT EnumEspPartitions(PARTITION_INFO** partitions)
{
    INT count = 0;
    INT maxPartitions = 26;  // 最多 26 个盘符
    
    *partitions = (PARTITION_INFO*)calloc(maxPartitions, sizeof(PARTITION_INFO));
    if (!*partitions) return 0;
    
    // 调试：开始枚举
    // MessageBoxW(NULL, L"开始枚举 ESP 分区", L"调试", MB_OK);
    
    for (WCHAR d = L'C'; d <= L'Z'; d++) {
        WCHAR root[4] = {d, L':', L'\\', 0};
        UINT driveType = GetDriveTypeW(root);
        
        // 跳过不存在的驱动器
        if (driveType == DRIVE_NO_ROOT_DIR) {
            continue;
        }
        
        // 只检查固定磁盘、可移动磁盘
        if (driveType != DRIVE_FIXED && driveType != DRIVE_REMOVABLE) {
            continue;
        }
        
        // 获取卷标和文件系统信息
        WCHAR volumeLabel[128] = {0};
        WCHAR fsName[32] = {0};
        
        if (!GetVolumeInformationW(root, volumeLabel, 128, NULL, NULL, NULL, fsName, 32)) {
            continue;  // 无法获取卷信息，跳过
        }
        
        (*partitions)[count].driveLetter = d;
        wcsncpy((*partitions)[count].fileSystem, fsName, 32);
        
        // 检查是否为 FAT32 或 FAT 文件系统
        BOOL isFat32 = (_wcsicmp(fsName, L"FAT32") == 0 || 
                        _wcsicmp(fsName, L"FAT") == 0);
        
        // 检查是否存在 EFI 文件夹 (改进：检查具体路径)
        WCHAR efiPath1[MAX_PATH];  // X:\EFI\BOOT\bootx64.efi
        WCHAR efiPath2[MAX_PATH];  // X:\EFI\Microsoft
        swprintf(efiPath1, MAX_PATH, L"%sEFI\\BOOT\\bootx64.efi", root);
        swprintf(efiPath2, MAX_PATH, L"%sEFI\\Microsoft", root);
        
        BOOL hasEfiBoot = (GetFileAttributesW(efiPath1) != INVALID_FILE_ATTRIBUTES);
        BOOL hasEfiMicrosoft = (GetFileAttributesW(efiPath2) != INVALID_FILE_ATTRIBUTES);
        BOOL hasEfiFolder = hasEfiBoot || hasEfiMicrosoft;
        
        // 如果是 FAT32 且有 EFI 文件夹，则是 ESP 分区
        if (isFat32 && hasEfiFolder) {
            (*partitions)[count].isESP = TRUE;
            
            // 构建显示名称
            if (wcslen(volumeLabel) > 0) {
                swprintf((*partitions)[count].label, 128, 
                    L"ESP 分区 - %s (%s) [%c:]", fsName, volumeLabel, d);
            } else {
                swprintf((*partitions)[count].label, 128, 
                    L"ESP 分区 - %s [%c:]", fsName, d);
            }
            
            count++;
        } else if (isFat32) {
            // FAT32 但没有 EFI 文件夹，也可能是 ESP（用户可能还未创建）
            (*partitions)[count].isESP = FALSE;
            swprintf((*partitions)[count].label, 128, 
                L"FAT32 分区 - %s (%s) [%c:]", fsName, volumeLabel, d);
            count++;
        }
    }
    
    // 如果没有找到任何分区，添加一个提示选项
    if (count == 0) {
        (*partitions)[0].driveLetter = L'\0';
        wcsncpy((*partitions)[0].label, L"未找到 ESP 分区，请手动挂载", 128);
        (*partitions)[0].isESP = FALSE;
        count = 1;
    }
    
    // 调试：显示找到的分区数量
    // WCHAR msg[64];
    // swprintf(msg, 64, L"找到 %d 个分区", count);
    // MessageBoxW(NULL, msg, L"调试", MB_OK);
    
    return count;
}

// ============================================
// 释放磁盘列表
// ============================================
VOID FreeDiskList(DISK_INFO* disks, INT count)
{
    (void)count;
    if (disks) free(disks);
}

// ============================================
// 释放分区列表
// ============================================
VOID FreePartitionList(PARTITION_INFO* partitions, INT count)
{
    (void)count;
    if (partitions) free(partitions);
}

// ============================================
// 使用代码创建对话框 (简化版本)
// 直接创建窗口模拟对话框
// ============================================
static INT_PTR CreateAddEfiDialog(HWND hParent, WCHAR* outTitle, WCHAR* outPath)
{
    ADD_EFI_DIALOG_DATA data = {0};
    data.menuTitle[0] = L'\0';
    data.filePath[0] = L'\0';
    data.selectedDisk = 0;
    data.selectedPartition = 0;
    
    // 创建对话框窗口 - 增加高度以容纳所有控件
    HWND hDlg = CreateWindowExW(
        WS_EX_DLGMODALFRAME | WS_EX_WINDOWEDGE,
        L"#32770",  // 对话框类
        L"添加 EFI 启动项",
        DS_MODALFRAME | DS_CENTER | WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        0, 0, 420, 380,  // 增加高度到 380
        hParent,
        NULL,
        GetModuleHandle(NULL),
        NULL
    );
    
    if (!hDlg) return IDCANCEL;
    
    // 禁用父窗口 (模态)
    EnableWindow(hParent, FALSE);
    
    // 创建控件
    // 静态文本 - 菜单标题
    CreateWindowExW(0, L"STATIC", L"菜单标题:",
        WS_CHILD | WS_VISIBLE,
        20, 20, 80, 20, hDlg, NULL, NULL, NULL);
    
    // 编辑框 - 菜单标题 (默认值改为英文)
    HWND hEditTitle = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"New Boot Entry",
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
        20, 45, 360, 28, hDlg, (HMENU)IDC_EDIT_TITLE, NULL, NULL);
    
    // 提示文本
    CreateWindowExW(0, L"STATIC", L"(启动项显示名称)",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        20, 78, 200, 16, hDlg, (HMENU)IDC_STATIC_HINT, NULL, NULL);
    
    // 静态文本 - 启动磁盘
    CreateWindowExW(0, L"STATIC", L"启动磁盘:",
        WS_CHILD | WS_VISIBLE,
        20, 105, 80, 20, hDlg, NULL, NULL, NULL);
    
    // 下拉框 - 启动磁盘
    HWND hComboDisk = CreateWindowExW(0, L"COMBOBOX", L"",
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
        20, 130, 360, 100, hDlg, (HMENU)IDC_COMBO_DISK, NULL, NULL);
    
    // 静态文本 - 启动分区
    CreateWindowExW(0, L"STATIC", L"启动分区:",
        WS_CHILD | WS_VISIBLE,
        20, 170, 80, 20, hDlg, NULL, NULL, NULL);
    
    // 下拉框 - 启动分区
    HWND hComboPart = CreateWindowExW(0, L"COMBOBOX", L"",
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
        20, 195, 360, 100, hDlg, (HMENU)IDC_COMBO_PARTITION, NULL, NULL);
    
    // 静态文本 - 启动文件
    CreateWindowExW(0, L"STATIC", L"启动文件:",
        WS_CHILD | WS_VISIBLE,
        20, 235, 80, 20, hDlg, NULL, NULL, NULL);
    
    // 编辑框 - 启动文件
    HWND hEditPath = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"\\EFI\\Microsoft\\Boot\\bootmgfw.efi",
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
        20, 260, 280, 28, hDlg, (HMENU)IDC_EDIT_PATH, NULL, NULL);
    
    // 浏览按钮
    CreateWindowExW(0, L"BUTTON", L"浏览...",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        310, 260, 70, 28, hDlg, (HMENU)IDC_BTN_BROWSE, NULL, NULL);
    
    // 确定按钮 (居中布局)
    HWND hBtnOK = CreateWindowExW(0, L"BUTTON", L"确定",
        WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
        140, 320, 90, 32, hDlg, (HMENU)IDOK, NULL, NULL);
    
    // 取消按钮 (居中布局)
    HWND hBtnCancel = CreateWindowExW(0, L"BUTTON", L"取消",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        250, 320, 90, 32, hDlg, (HMENU)IDCANCEL, NULL, NULL);
    
    // 设置字体 (使用系统对话框字体)
    HFONT hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    SendMessageW(hEditTitle, WM_SETFONT, (WPARAM)hFont, TRUE);
    SendMessageW(hComboDisk, WM_SETFONT, (WPARAM)hFont, TRUE);
    SendMessageW(hComboPart, WM_SETFONT, (WPARAM)hFont, TRUE);
    SendMessageW(hEditPath, WM_SETFONT, (WPARAM)hFont, TRUE);
    SendMessageW(hBtnOK, WM_SETFONT, (WPARAM)hFont, TRUE);
    SendMessageW(hBtnCancel, WM_SETFONT, (WPARAM)hFont, TRUE);
    
    // 枚举磁盘并填充
    data.diskCount = EnumPhysicalDisks(&data.disks);
    for (INT i = 0; i < data.diskCount; i++) {
        SendMessageW(hComboDisk, CB_ADDSTRING, 0, (LPARAM)data.disks[i].diskName);
    }
    SendMessageW(hComboDisk, CB_SETCURSEL, 0, 0);
    
    // 枚举 ESP 分区并填充
    data.partitionCount = EnumEspPartitions(&data.partitions);
    for (INT i = 0; i < data.partitionCount; i++) {
        SendMessageW(hComboPart, CB_ADDSTRING, 0, (LPARAM)data.partitions[i].label);
        if (data.partitions[i].isESP) {
            SendMessageW(hComboPart, CB_SETCURSEL, i, 0);
            data.selectedPartition = i;
        }
    }
    
    // 居中显示
    RECT rcDlg, rcParent;
    GetWindowRect(hDlg, &rcDlg);
    GetWindowRect(hParent, &rcParent);
    
    int x = rcParent.left + (rcParent.right - rcParent.left - (rcDlg.right - rcDlg.left)) / 2;
    int y = rcParent.top + (rcParent.bottom - rcParent.top - (rcDlg.bottom - rcDlg.top)) / 2;
    SetWindowPos(hDlg, NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
    
    // 设置焦点
    SetFocus(hEditTitle);
    SendMessageW(hEditTitle, EM_SETSEL, 0, -1);
    
    // 消息循环 (模态)
    MSG msg;
    BOOL bRet;
    INT_PTR result = IDCANCEL;
    BOOL processing = FALSE;  // 防止重入
    
    while (IsWindow(hDlg)) {
        bRet = GetMessageW(&msg, NULL, 0, 0);
        if (bRet == 0 || bRet == -1) break;
        
        if (msg.hwnd == hDlg || IsChild(hDlg, msg.hwnd)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        } else {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        
        // 检查是否点击了确定/取消
        if (msg.message == WM_COMMAND && !processing) {
            processing = TRUE;
            
            if (LOWORD(msg.wParam) == IDOK) {
                // 验证输入
                GetDlgItemTextW(hDlg, IDC_EDIT_TITLE, data.menuTitle, 256);
                GetDlgItemTextW(hDlg, IDC_EDIT_PATH, data.filePath, 512);
                
                if (wcslen(data.menuTitle) == 0) {
                    MessageBoxW(hDlg, L"请输入菜单标题", L"提示", MB_OK | MB_ICONWARNING);
                    SetFocus(hEditTitle);
                    processing = FALSE;
                    continue;
                }
                
                if (wcslen(data.filePath) == 0) {
                    MessageBoxW(hDlg, L"请输入启动文件路径", L"提示", MB_OK | MB_ICONWARNING);
                    SetFocus(hEditPath);
                    processing = FALSE;
                    continue;
                }
                
                result = IDOK;
                break;
            } else if (LOWORD(msg.wParam) == IDCANCEL) {
                result = IDCANCEL;
                break;
            } else if (LOWORD(msg.wParam) == IDC_BTN_BROWSE) {
                // 浏览文件 - 修复文件对话框
                OPENFILENAMEW ofn = {0};
                // 使用双\0 分隔的过滤器字符串
                WCHAR szFilter[] = L"EFI Files (*.efi)\0*.efi\0All Files (*.*)\0*.*\0";
                WCHAR filePath[MAX_PATH] = {0};
                
                ofn.lStructSize = sizeof(OPENFILENAMEW);
                ofn.hwndOwner = hDlg;  // 关键：使用对话框句柄
                ofn.lpstrFilter = szFilter;
                ofn.nFilterIndex = 1;
                ofn.lpstrFile = filePath;
                ofn.nMaxFile = MAX_PATH;
                ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER | OFN_NOCHANGEDIR;
                ofn.lpstrDefExt = L"efi";
                ofn.lpstrTitle = L"选择 EFI 启动文件";
                
                if (GetOpenFileNameW(&ofn)) {
                    SetDlgItemTextW(hDlg, IDC_EDIT_PATH, filePath);
                }
            } else if (LOWORD(msg.wParam) == IDC_COMBO_DISK) {
                if (HIWORD(msg.wParam) == CBN_SELCHANGE) {
                    data.selectedDisk = (INT)SendMessageW(hComboDisk, CB_GETCURSEL, 0, 0);
                }
            } else if (LOWORD(msg.wParam) == IDC_COMBO_PARTITION) {
                if (HIWORD(msg.wParam) == CBN_SELCHANGE) {
                    data.selectedPartition = (INT)SendMessageW(hComboPart, CB_GETCURSEL, 0, 0);
                }
            }
            
            processing = FALSE;
        }
        
        // 处理 WM_CLOSE
        if (msg.message == WM_CLOSE) {
            result = IDCANCEL;
            break;
        }
    }
    
    // 清理
    DestroyWindow(hDlg);
    EnableWindow(hParent, TRUE);
    SetForegroundWindow(hParent);
    
    // 复制输出
    if (result == IDOK) {
        wcsncpy(outTitle, data.menuTitle, 256);
        wcsncpy(outPath, data.filePath, 512);
    }
    
    // 释放资源
    FreeDiskList(data.disks, data.diskCount);
    FreePartitionList(data.partitions, data.partitionCount);
    
    return result;
}

// ============================================
// 公共接口：显示添加 EFI 启动项对话框
// ============================================
BOOL ShowAddEfiDialog(HWND hParent, WCHAR* outTitle, WCHAR* outPath)
{
    return CreateAddEfiDialog(hParent, outTitle, outPath) == IDOK;
}
