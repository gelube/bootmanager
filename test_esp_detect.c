/**
 * ESP 分区检测测试程序
 * 用于诊断为什么 ESP 分区检测失败
 */

#include <windows.h>
#include <stdio.h>

int main(void)
{
    printf("=== ESP 分区检测测试 ===\n\n");
    
    // 方法 1: 遍历所有盘符
    printf("方法 1: 遍历 C: 到 Z:\n");
    for (WCHAR d = L'C'; d <= L'Z'; d++) {
        WCHAR root[4] = {d, L':', L'\\', 0};
        
        if (GetDriveTypeW(root) == DRIVE_FIXED || GetDriveTypeW(root) == DRIVE_REMOVABLE) {
            WCHAR fsName[32] = {0};
            WCHAR volumeLabel[128] = {0};
            
            if (GetVolumeInformationW(root, volumeLabel, 128, NULL, NULL, NULL, fsName, 32)) {
                if (_wcsicmp(fsName, L"FAT32") == 0 || _wcsicmp(fsName, L"FAT") == 0) {
                    printf("  %c: - FAT32 (卷标：%s)\n", d, volumeLabel);
                    
                    // 检查 EFI 文件夹
                    WCHAR efiPath[MAX_PATH];
                    swprintf(efiPath, MAX_PATH, L"%c:\\EFI", d);
                    
                    DWORD attr = GetFileAttributesW(efiPath);
                    if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY)) {
                        printf("      ✓ 找到 EFI 文件夹!\n");
                    }
                } else {
                    printf("  %c: - %s (卷标：%s)\n", d, fsName, volumeLabel);
                }
            }
        }
    }
    
    // 方法 2: 使用 FindFirstVolumeW
    printf("\n方法 2: 使用 FindFirstVolumeW 枚举所有卷:\n");
    
    WCHAR volumeGuid[MAX_PATH];
    HANDLE hFind = FindFirstVolumeW(volumeGuid, ARRAYSIZE(volumeGuid));
    
    if (hFind == INVALID_HANDLE_VALUE) {
        printf("  FindFirstVolumeW 失败！错误代码：%d\n", GetLastError());
    } else {
        int count = 0;
        do {
            count++;
            WCHAR fsName[32] = {0};
            WCHAR volumeLabel[128] = {0};
            
            printf("  [%d] %s\n", count, volumeGuid);
            
            if (GetVolumeInformationW(volumeGuid, volumeLabel, 128, NULL, NULL, NULL, fsName, 32)) {
                printf("      文件系统：%s, 卷标：%s\n", fsName, volumeLabel);
                
                if (_wcsicmp(fsName, L"FAT32") == 0 || _wcsicmp(fsName, L"FAT") == 0) {
                    printf("      ✓ 这是 FAT32 分区，可能是 ESP!\n");
                }
            } else {
                printf("      GetVolumeInformationW 失败！错误代码：%d\n", GetLastError());
            }
            
        } while (FindNextVolumeW(hFind, volumeGuid, ARRAYSIZE(volumeGuid)));
        
        FindVolumeClose(hFind);
        printf("\n共找到 %d 个卷\n", count);
    }
    
    // 方法 3: 执行 mountvol 命令
    printf("\n方法 3: 执行 mountvol 命令:\n");
    printf("  请在命令行运行：mountvol\n");
    
    printf("\n=== 测试完成 ===\n");
    printf("\n按回车键退出...");
    getchar();
    
    return 0;
}
