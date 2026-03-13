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
// 临时挂载卷到盘符
// 使用 SetVolumeMountPoint 将 Volume GUID 路径映射到空目录
// 注意：对于系统分区，DefineDosDeviceW 可能不工作
// ============================================
static BOOL TempMountVolume(const WCHAR* volumeGuid, WCHAR driveLetter)
{
    WCHAR deviceName[8];
    
    // 构建盘符名称 "X:"
    deviceName[0] = driveLetter;
    deviceName[1] = L':';
    deviceName[2] = L'\\';
    deviceName[3] = L'\0';
    
    // 使用 SetVolumeMountPoint 挂载
    // 注意：这个 API 需要卷已经格式化且健康
    BOOL result = SetVolumeMountPointW(deviceName, volumeGuid);
    return result;
}

// ============================================
// 卸载临时盘符
// 使用 DeleteVolumeMountPoint 移除映射
// ============================================
static BOOL TempUnmountVolume(WCHAR driveLetter)
{
    WCHAR deviceName[8];
    
    // 构建盘符名称 "X:\"
    deviceName[0] = driveLetter;
    deviceName[1] = L':';
    deviceName[2] = L'\\';
    deviceName[3] = L'\0';
    
    // 移除映射
    BOOL result = DeleteVolumeMountPointW(deviceName);
    return result;
}

// ============================================
// 检查 EFI 文件夹
// 检查分区是否包含 EFI\BOOT 或 EFI\Microsoft 目录
// ============================================
static BOOL CheckEfiFolder(const WCHAR* rootPath)
{
    WCHAR efiPath1[MAX_PATH];  // X:\EFI\BOOT\bootx64.efi
    WCHAR efiPath2[MAX_PATH];  // X:\EFI\Microsoft
    WCHAR efiDir[MAX_PATH];    // X:\EFI
    
    swprintf(efiPath1, MAX_PATH, L"%sEFI\\BOOT\\bootx64.efi", rootPath);
    swprintf(efiPath2, MAX_PATH, L"%sEFI\\Microsoft", rootPath);
    swprintf(efiDir, MAX_PATH, L"%sEFI", rootPath);
    
    // 检查 bootx64.efi 文件
    BOOL hasEfiBoot = (GetFileAttributesW(efiPath1) != INVALID_FILE_ATTRIBUTES);
    
    // 检查 EFI\Microsoft 目录
    BOOL hasEfiMicrosoft = (GetFileAttributesW(efiPath2) != INVALID_FILE_ATTRIBUTES);
    
    // 检查 EFI 目录
    BOOL hasEfiDir = (GetFileAttributesW(efiDir) != INVALID_FILE_ATTRIBUTES);
    
    // 只要有 EFI 目录或 bootx64.efi 文件，就认为是 ESP 分区
    return hasEfiBoot || hasEfiMicrosoft || hasEfiDir;
}

// ============================================
// 获取卷的盘符（如果有）
// 使用 GetVolumePathNamesForVolumeNameW 获取所有关联的盘符
// ============================================
static BOOL GetVolumeDriveLetter(const WCHAR* volumeGuid, WCHAR* outDriveLetter)
{
    *outDriveLetter = L'\0';
    
    WCHAR pathNames[MAX_PATH];
    DWORD charCount = MAX_PATH;
    
    if (GetVolumePathNamesForVolumeNameW(volumeGuid, pathNames, charCount, &charCount)) {
        // pathNames 是多字符串，第一个就是盘符（如果有）
        if (pathNames[0] != L'\0') {
            // 提取盘符字母 "X:\"
            *outDriveLetter = pathNames[0];
            return TRUE;
        }
    }
    
    return FALSE;
}

// ============================================
// 枚举 ESP 分区（改进版）
// 使用 FindFirstVolumeW 枚举所有卷（包括没有盘符的）
// 对每个卷：
//   1. 检查文件系统是否为 FAT32
//   2. 获取已有盘符（如果有）
//   3. 如果没有盘符，临时挂载检查 EFI 文件夹
//   4. 检查完成后卸载临时盘符
// ============================================
INT EnumEspPartitions(PARTITION_INFO** partitions)
{
    INT count = 0;
    INT maxPartitions = 32;
    WCHAR volumeGuid[MAX_PATH];
    HANDLE hFind;
    size_t volLen;
    BOOL isFat32, hasEfi, hasDriveLetter;
    WCHAR fsName[32];
    WCHAR volumeLabel[128];
    WCHAR existingDrive;
    WCHAR rootPath[8];
    WCHAR tempDrive;
    WCHAR guidOnly[64];
    size_t guidLen;
    
    *partitions = (PARTITION_INFO*)calloc(maxPartitions, sizeof(PARTITION_INFO));
    if (!*partitions) return 0;
    
    hFind = FindFirstVolumeW(volumeGuid, ARRAYSIZE(volumeGuid));
    
    if (hFind == INVALID_HANDLE_VALUE) {
        // 枚举失败，返回空列表
        FreePartitionList(*partitions, 0);
        *partitions = NULL;
        return 0;
    }
    
    do {
        // 验证 Volume GUID 路径格式：\\?\Volume{guid}\
        volLen = wcslen(volumeGuid);
        if (volLen < 5 || 
            volumeGuid[0] != L'\\' || volumeGuid[1] != L'\\' ||
            volumeGuid[2] != L'?' || volumeGuid[3] != L'\\' ||
            volumeGuid[volLen - 1] != L'\\') {
            continue;  // 无效路径，跳过
        }
        
        // 获取文件系统信息
        fsName[0] = L'\0';
        volumeLabel[0] = L'\0';
        
        // 注意：Volume GUID 路径需要尾部反斜杠
        if (!GetVolumeInformationW(volumeGuid, volumeLabel, ARRAYSIZE(volumeLabel), 
                                    NULL, NULL, NULL, fsName, ARRAYSIZE(fsName))) {
            continue;  // 无法获取卷信息，跳过
        }
        
        // 检查是否为 FAT32 或 FAT 文件系统
        isFat32 = (_wcsicmp(fsName, L"FAT32") == 0 || 
                    _wcsicmp(fsName, L"FAT") == 0);
        
        if (!isFat32) {
            continue;  // 不是 FAT32，跳过
        }
        
        // 获取已有盘符
        existingDrive = L'\0';
        GetVolumeDriveLetter(volumeGuid, &existingDrive);
        
        // 构建根路径用于检查文件
        hasDriveLetter = (existingDrive != L'\0');
        
        if (hasDriveLetter) {
            // 已有盘符，直接使用
            rootPath[0] = existingDrive;
            rootPath[1] = L':';
            rootPath[2] = L'\\';
            rootPath[3] = L'\0';
        } else {
            // 没有盘符，需要临时挂载
            // 找一个可用的盘符（从 Z: 向下找）
            tempDrive = L'\0';
            {
                WCHAR d;
                for (d = L'Z'; d >= L'C'; d--) {
                    WCHAR testPath[4] = {d, L':', L'\\', 0};
                    if (GetDriveTypeW(testPath) == DRIVE_NO_ROOT_DIR) {
                        tempDrive = d;
                        break;
                    }
                }
            }
            
            if (tempDrive == L'\0') {
                continue;  // 没有可用盘符，跳过
            }
            
            // 临时挂载
            if (!TempMountVolume(volumeGuid, tempDrive)) {
                continue;  // 挂载失败，跳过
            }
            
            // 构建根路径
            rootPath[0] = tempDrive;
            rootPath[1] = L':';
            rootPath[2] = L'\\';
            rootPath[3] = L'\0';
            
            // 检查 EFI 文件夹
            hasEfi = CheckEfiFolder(rootPath);
            
            // 立即卸载临时盘符
            TempUnmountVolume(tempDrive);
            
            if (!hasEfi) {
                continue;  // 没有 EFI 文件夹，跳过
            }
            
            // 是 ESP 分区，添加到列表
            (*partitions)[count].driveLetter = L'\0';  // 无永久盘符
            wcsncpy((*partitions)[count].fileSystem, fsName, 32);
            (*partitions)[count].isESP = TRUE;
            
            // 提取 Volume GUID（去掉 \\?\ 和尾部 \）
            wcsncpy(guidOnly, &volumeGuid[4], 63);
            guidOnly[63] = L'\0';
            guidLen = wcslen(guidOnly);
            if (guidLen > 0 && guidOnly[guidLen - 1] == L'\\') {
                guidOnly[guidLen - 1] = L'\0';
            }
            
            // 构建显示名称
            if (wcslen(volumeLabel) > 0) {
                swprintf((*partitions)[count].label, 128, 
                    L"ESP 分区 - %s (%s) (未挂载) [%s]", fsName, volumeLabel, guidOnly);
            } else {
                swprintf((*partitions)[count].label, 128, 
                    L"ESP 分区 - %s (未挂载) [%s]", fsName, guidOnly);
            }
            
            count++;
            continue;
        }
        
        // 有盘符，直接检查 EFI 文件夹
        hasEfi = CheckEfiFolder(rootPath);
        
        // 添加到列表
        (*partitions)[count].driveLetter = existingDrive;
        wcsncpy((*partitions)[count].fileSystem, fsName, 32);
        (*partitions)[count].isESP = hasEfi;
        
        // 构建显示名称
        if (hasEfi) {
            if (wcslen(volumeLabel) > 0) {
                swprintf((*partitions)[count].label, 128, 
                    L"ESP 分区 - %s (%s) [%c:]", fsName, volumeLabel, existingDrive);
            } else {
                swprintf((*partitions)[count].label, 128, 
                    L"ESP 分区 - %s [%c:]", fsName, existingDrive);
            }
        } else {
            // FAT32 但不是 ESP
            if (wcslen(volumeLabel) > 0) {
                swprintf((*partitions)[count].label, 128, 
                    L"FAT32 分区 - %s (%s) [%c:]", fsName, volumeLabel, existingDrive);
            } else {
                swprintf((*partitions)[count].label, 128, 
                    L"FAT32 分区 - %s [%c:]", fsName, existingDrive);
            }
        }
        
        count++;
        
    } while (FindNextVolumeW(hFind, volumeGuid, ARRAYSIZE(volumeGuid)));
    
    FindVolumeClose(hFind);
    
    // 如果没有找到任何分区，添加一个提示选项
    if (count == 0) {
        (*partitions)[0].driveLetter = L'\0';
        wcsncpy((*partitions)[0].label, L"未找到 ESP 分区，请手动挂载", 128);
        (*partitions)[0].isESP = FALSE;
        count = 1;
    }
    
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
