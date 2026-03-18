/**
 * Boot Manager Pro v3 - Dialog Implementation
 * 添加 EFI 菜单项对话框
 * 
 * 功能:
 * - 模态对话框，类似 BOOTICE 风格
 * - 自动枚举物理磁盘
 * - 根据选中的磁盘动态枚举 ESP 分区
 * - 文件浏览选择 EFI 文件
 * - 验证输入并生成 rEFInd 菜单项参数
 */

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <shlobj.h>
#include <wchar.h>
#include <stdio.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#include "add_efi_dialog.h"

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
// 临时挂载卷到指定盘符
// ============================================
static BOOL TempMountVolume(const WCHAR* volumeGuid, WCHAR driveLetter)
{
    WCHAR deviceName[8];
    deviceName[0] = driveLetter;
    deviceName[1] = L':';
    deviceName[2] = L'\\';
    deviceName[3] = L'\0';
    
    return SetVolumeMountPointW(deviceName, volumeGuid);
}

// ============================================
// 卸载临时卷
// ============================================
static BOOL TempUnmountVolume(WCHAR driveLetter)
{
    WCHAR deviceName[8];
    deviceName[0] = driveLetter;
    deviceName[1] = L':';
    deviceName[2] = L'\\';
    deviceName[3] = L'\0';
    
    return DeleteVolumeMountPointW(deviceName);
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
// 获取卷所属磁盘编号
// ============================================
static INT GetVolumeDiskNumber(const WCHAR* volumeName)
{
    if (!volumeName || volumeName[0] == L'\0') {
        return -1;
    }

    // FindFirstVolumeW 返回的路径带末尾反斜杠，CreateFileW 需要去掉它。
    WCHAR volumePath[MAX_PATH];
    size_t len = wcslen(volumeName);
    if (len >= MAX_PATH) {
        return -1;
    }

    wcsncpy(volumePath, volumeName, MAX_PATH - 1);
    volumePath[MAX_PATH - 1] = L'\0';

    if (len > 0 && volumePath[len - 1] == L'\\') {
        volumePath[len - 1] = L'\0';
    }

    HANDLE hVolume = CreateFileW(
        volumePath,
        0,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_EXISTING,
        0,
        NULL
    );

    if (hVolume == INVALID_HANDLE_VALUE) {
        return -1;
    }

    BYTE extentsBuffer[sizeof(VOLUME_DISK_EXTENTS) + sizeof(DISK_EXTENT) * 8] = {0};
    PVOLUME_DISK_EXTENTS extents = (PVOLUME_DISK_EXTENTS)extentsBuffer;
    DWORD bytesReturned = 0;

    BOOL ok = DeviceIoControl(
        hVolume,
        IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS,
        NULL,
        0,
        extents,
        (DWORD)sizeof(extentsBuffer),
        &bytesReturned,
        NULL
    );

    CloseHandle(hVolume);

    if (!ok || extents->NumberOfDiskExtents == 0) {
        return -1;
    }

    return (INT)extents->Extents[0].DiskNumber;
}

// ============================================
// 枚举 ESP 分区
// 通过 FAT/FAT32 + EFI 目录判定，并匹配选中的磁盘
// ============================================
INT EnumEspPartitionsForDisk(INT diskNumber, PARTITION_INFO** partitions)
{
    INT count = 0;
    INT maxPartitions = 32;
    WCHAR volumeName[MAX_PATH];
    HANDLE hFind = INVALID_HANDLE_VALUE;

    *partitions = (PARTITION_INFO*)calloc(maxPartitions, sizeof(PARTITION_INFO));
    if (!*partitions) return 0;

    // 枚举所有卷 (\\.\Volume{GUID}\)
    hFind = FindFirstVolumeW(volumeName, MAX_PATH);
    if (hFind == INVALID_HANDLE_VALUE) {
        return 0;
    }

    do {
        // 获取此卷的盘符 (如果有)
        WCHAR pathNames[MAX_PATH] = {0};
        DWORD pathNamesLen = 0;
        BOOL hasDriveLetter = GetVolumePathNamesForVolumeNameW(volumeName, pathNames, MAX_PATH, &pathNamesLen);
#ifdef _DEBUG
        {
            WCHAR debugMsg[256];
            swprintf(debugMsg, 256, L"[ESP] GetVolumePathNamesForVolumeNameW(%s) => %d\n", volumeName, (INT)hasDriveLetter);
            OutputDebugStringW(debugMsg);
        }
#endif

        // 仅保留属于选中磁盘的卷
        INT volDiskNum = GetVolumeDiskNumber(volumeName);
#ifdef _DEBUG
        {
            WCHAR debugMsg[256];
            swprintf(debugMsg, 256, L"[ESP] GetVolumeDiskNumber(%s) => %d\n", volumeName, volDiskNum);
            OutputDebugStringW(debugMsg);
        }
#endif
        if (volDiskNum < 0 || volDiskNum != diskNumber) {
            continue;
        }

        // 获取文件系统
        WCHAR fsName[32] = {0};
        WCHAR volumeLabel[128] = {0};
        BOOL gotVolumeInfo = GetVolumeInformationW(volumeName, volumeLabel, 128, NULL, NULL, NULL, fsName, 32);
#ifdef _DEBUG
        {
            WCHAR debugMsg[256];
            swprintf(debugMsg, 256, L"[ESP] GetVolumeInformationW(%s) => %d, fs=%s\n", volumeName, (INT)gotVolumeInfo, fsName);
            OutputDebugStringW(debugMsg);
        }
#endif
        if (!gotVolumeInfo) {
            continue;
        }

        // 仅处理 FAT/FAT32 分区
        if (_wcsicmp(fsName, L"FAT32") != 0 && _wcsicmp(fsName, L"FAT") != 0) {
            continue;
        }

        // 检查 EFI 文件夹
        BOOL hasEfi = FALSE;
        WCHAR driveLetter = L'\0';
        BOOL isMountedTemporarily = FALSE;

        if (hasDriveLetter && pathNames[0] != L'\0' && pathNames[0] != L'\\') {
            // 有盘符，直接检查
            driveLetter = pathNames[0];
            WCHAR rootPath[8] = {driveLetter, L':', L'\\', 0};
            hasEfi = CheckEfiFolder(rootPath);
        } else {
            // 没有盘符，尝试临时挂载
            for (WCHAR d = L'Z'; d >= L'C'; d--) {
                WCHAR testPath[4] = {d, L':', L'\\', 0};
                if (GetDriveTypeW(testPath) == DRIVE_NO_ROOT_DIR) {
                    if (TempMountVolume(volumeName, d)) {
                        driveLetter = d;
                        isMountedTemporarily = TRUE;
                        WCHAR rootPath[8] = {d, L':', L'\\', 0};
                        hasEfi = CheckEfiFolder(rootPath);
                        TempUnmountVolume(d);
                        // 临时挂载仅用于检查，找到 EFI 后不显示临时盘符
                        if (hasEfi) {
                            driveLetter = L'\0';
                        }
                        break;
                    }
                }
            }

            // 如果临时挂载失败，尝试直接使用 Volume GUID 路径检查
            if (!hasEfi && !isMountedTemporarily) {
                WCHAR efiPath[MAX_PATH];
                swprintf(efiPath, MAX_PATH, L"%sEFI", volumeName);
                DWORD attr = GetFileAttributesW(efiPath);
                if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY)) {
                    hasEfi = TRUE;
                    driveLetter = L'\0';
                }
            }
        }

        // 如果找到 EFI 文件夹，添加到列表（即使没有盘符）
        if (hasEfi && count < maxPartitions) {
            (*partitions)[count].driveLetter = driveLetter;
            wcsncpy((*partitions)[count].fileSystem, fsName, 32);
            (*partitions)[count].isESP = TRUE;

            // 构建显示名称 - 简化版
            if (wcslen(volumeLabel) > 0) {
                swprintf((*partitions)[count].label, 128,
                    L"ESP 分区 - %s (%s)", fsName, volumeLabel);
            } else {
                swprintf((*partitions)[count].label, 128,
                    L"ESP 分区 - %s", fsName);
            }

            count++;
        }

    } while (FindNextVolumeW(hFind, volumeName, MAX_PATH));

    FindVolumeClose(hFind);
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
static void UpdatePartitionCombo(ADD_EFI_DIALOG_DATA* data)
{
    HWND hComboDisk = data->hComboDisk;
    HWND hComboPart = data->hComboPart;

    INT sel = (INT)SendMessageW(hComboDisk, CB_GETCURSEL, 0, 0);
    data->selectedDisk = sel;
    data->selectedPartition = -1;

    if (data->partitions) {
        FreePartitionList(data->partitions, data->partitionCount);
        data->partitions = NULL;
        data->partitionCount = 0;
    }

    SendMessageW(hComboPart, CB_RESETCONTENT, 0, 0);

    if (sel == CB_ERR || sel < 0 || sel >= data->diskCount) {
        SendMessageW(hComboPart, CB_ADDSTRING, 0, (LPARAM)L"(请先选择磁盘)");
        SendMessageW(hComboPart, CB_SETCURSEL, 0, 0);
        EnableWindow(hComboPart, FALSE);
        return;
    }

    INT diskNum = data->disks[sel].diskNumber;
    data->partitionCount = EnumEspPartitionsForDisk(diskNum, &data->partitions);

    if (data->partitionCount <= 0) {
        SendMessageW(hComboPart, CB_ADDSTRING, 0, (LPARAM)L"(此磁盘上没有 FAT32 分区)");
        SendMessageW(hComboPart, CB_SETCURSEL, 0, 0);
        EnableWindow(hComboPart, FALSE);
        return;
    }

    for (INT i = 0; i < data->partitionCount; ++i) {
        SendMessageW(hComboPart, CB_ADDSTRING, 0, (LPARAM)data->partitions[i].label);
        if (data->partitions[i].isESP && data->selectedPartition == -1) {
            data->selectedPartition = i;
        }
    }

    if (data->selectedPartition < 0) {
        data->selectedPartition = 0;
    }

    SendMessageW(hComboPart, CB_SETCURSEL, data->selectedPartition, 0);
    EnableWindow(hComboPart, TRUE);
}

static LRESULT CALLBACK AddEfiDialogProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    ADD_EFI_DIALOG_DATA* data = (ADD_EFI_DIALOG_DATA*)GetWindowLongPtrW(hDlg, GWLP_USERDATA);

    switch (msg) {
    case WM_NCCREATE:
        SetWindowLongPtrW(hDlg, GWLP_USERDATA,
            (LONG_PTR)((CREATESTRUCTW*)lParam)->lpCreateParams);
        return TRUE;

    case WM_COMMAND:
        if (!data) break;

        switch (LOWORD(wParam)) {
        case IDC_COMBO_DISK:
            if (HIWORD(wParam) == CBN_SELCHANGE) {
                UpdatePartitionCombo(data);
                return 0;
            }
            break;

        case IDC_COMBO_PARTITION:
            if (HIWORD(wParam) == CBN_SELCHANGE) {
                data->selectedPartition =
                    (INT)SendMessageW(data->hComboPart, CB_GETCURSEL, 0, 0);
                return 0;
            }
            break;

        case IDC_BTN_BROWSE:
            if (HIWORD(wParam) == BN_CLICKED) {
                OPENFILENAMEW ofn = {0};
                WCHAR szFilter[] = L"EFI Files (*.efi)\0*.efi\0All Files (*.*)\0*.*\0";
                WCHAR filePath[MAX_PATH] = {0};

                ofn.lStructSize = sizeof(OPENFILENAMEW);
                ofn.hwndOwner = hDlg;
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
                return 0;
            }
            break;

        case IDOK:
            if (HIWORD(wParam) == BN_CLICKED) {
                GetDlgItemTextW(hDlg, IDC_EDIT_TITLE, data->menuTitle, 256);
                GetDlgItemTextW(hDlg, IDC_EDIT_PATH, data->filePath, 512);

                if (wcslen(data->menuTitle) == 0) {
                    MessageBoxW(hDlg, L"请输入菜单标题", L"提示", MB_OK | MB_ICONWARNING);
                    SetFocus(GetDlgItem(hDlg, IDC_EDIT_TITLE));
                    return 0;
                }

                if (wcslen(data->filePath) == 0) {
                    MessageBoxW(hDlg, L"请输入启动文件路径", L"提示", MB_OK | MB_ICONWARNING);
                    SetFocus(GetDlgItem(hDlg, IDC_EDIT_PATH));
                    return 0;
                }

                // 选中分区有盘符且输入相对 EFI 路径时，补全为绝对路径
                if (data->selectedPartition >= 0 && data->partitions &&
                    data->selectedPartition < data->partitionCount &&
                    data->partitions[data->selectedPartition].driveLetter >= L'A' &&
                    data->partitions[data->selectedPartition].driveLetter <= L'Z' &&
                    data->filePath[0] == L'\\') {
                    WCHAR fullPath[512] = {0};
                    swprintf(fullPath, 512, L"%c:%s",
                        data->partitions[data->selectedPartition].driveLetter,
                        data->filePath);
                    wcsncpy(data->filePath, fullPath, 511);
                    data->filePath[511] = L'\0';
                }

                // 保存结果并关闭对话框
                data->result = IDOK;
                DestroyWindow(hDlg);
                return 0;
            }
            break;

        case IDCANCEL:
            if (HIWORD(wParam) == BN_CLICKED) {
                data->result = IDCANCEL;
                DestroyWindow(hDlg);
                return 0;
            }
            break;
        }
        break;

    case WM_CLOSE:
        if (data) {
            data->result = IDCANCEL;
        }
        DestroyWindow(hDlg);
        return 0;
    }

    return DefWindowProcW(hDlg, msg, wParam, lParam);
}

static INT_PTR CreateAddEfiDialog(HWND hParent, WCHAR* outTitle, WCHAR* outPath, WCHAR* outDriveLetter)
{
    static const WCHAR kAddEfiDialogClass[] = L"BootManagerAddEfiDialog";
    static BOOL classRegistered = FALSE;

    ADD_EFI_DIALOG_DATA data = {0};
    data.menuTitle[0] = L'\0';
    data.filePath[0] = L'\0';
    data.selectedDisk = 0;
    data.selectedPartition = 0;
    data.result = IDCANCEL;

    if (!classRegistered) {
        WNDCLASSEXW wc = {0};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = AddEfiDialogProc;
        wc.hInstance = GetModuleHandleW(NULL);
        wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = kAddEfiDialogClass;

        if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            return IDCANCEL;
        }
        classRegistered = TRUE;
    }

    // 创建对话框窗口
    HWND hDlg = CreateWindowExW(
        WS_EX_DLGMODALFRAME | WS_EX_WINDOWEDGE,
        kAddEfiDialogClass,
        L"添加 EFI 菜单项",
        DS_MODALFRAME | DS_CENTER | WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        0, 0, 420, 380,
        hParent,
        NULL,
        GetModuleHandleW(NULL),
        &data
    );

    if (!hDlg) return IDCANCEL;

    data.hDlg = hDlg;

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

    // 存储控件句柄到 data (用于后续更新)
    data.hComboDisk = hComboDisk;
    data.hComboPart = hComboPart;

    // 枚举磁盘并填充
    data.diskCount = EnumPhysicalDisks(&data.disks);
    for (INT i = 0; i < data.diskCount; i++) {
        SendMessageW(hComboDisk, CB_ADDSTRING, 0, (LPARAM)data.disks[i].diskName);
    }

    if (data.diskCount > 0) {
        SendMessageW(hComboDisk, CB_SETCURSEL, 0, 0);
    }

    // 初始化分区列表
    UpdatePartitionCombo(&data);

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

    // 消息循环
    MSG msg;
    while (IsWindow(hDlg) && GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    // 清理
    if (IsWindow(hDlg)) DestroyWindow(hDlg);
    EnableWindow(hParent, TRUE);
    SetForegroundWindow(hParent);

    // 复制输出
    if (data.result == IDOK) {
        wcsncpy(outTitle, data.menuTitle, 256);
        wcsncpy(outPath, data.filePath, 512);
        
        // 返回选中分区的盘符
        if (outDriveLetter && data.selectedPartition >= 0 && 
            data.partitions && data.selectedPartition < data.partitionCount) {
            WCHAR letter = data.partitions[data.selectedPartition].driveLetter;
            if (letter >= L'A' && letter <= L'Z') {
                swprintf(outDriveLetter, 4, L"%c:", letter);
            } else {
                outDriveLetter[0] = L'\0';
            }
        } else if (outDriveLetter) {
            outDriveLetter[0] = L'\0';
        }
    }

    // 释放资源
    FreeDiskList(data.disks, data.diskCount);
    FreePartitionList(data.partitions, data.partitionCount);

    return data.result;
}

BOOL ShowAddEfiDialog(HWND hParent, WCHAR* outTitle, WCHAR* outPath, WCHAR* outDriveLetter)
{
    return CreateAddEfiDialog(hParent, outTitle, outPath, outDriveLetter) == IDOK;
}
