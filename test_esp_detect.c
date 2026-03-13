/**
 * ESP Partition Detection Test
 * 测试 FindFirstVolumeW 枚举所有卷（包括未挂载的）
 */

#include <windows.h>
#include <stdio.h>

// 临时挂载卷
static BOOL TempMountVolume(const WCHAR* volumeGuid, WCHAR driveLetter)
{
    WCHAR deviceName[8];
    WCHAR targetPath[MAX_PATH];
    
    deviceName[0] = driveLetter;
    deviceName[1] = L':';
    deviceName[2] = L'\0';
    
    // 复制 Volume GUID 路径，保留尾部反斜杠
    wcsncpy(targetPath, volumeGuid, MAX_PATH - 1);
    targetPath[MAX_PATH - 1] = L'\0';
    
    wprintf(L"    DefineDosDeviceW: %s -> %s\n", deviceName, targetPath);
    
    // 临时映射：使用 DDD_RAW_TARGET_PATH 直接使用路径
    BOOL result = DefineDosDeviceW(DDD_RAW_TARGET_PATH, deviceName, targetPath);
    DWORD err = GetLastError();
    wprintf(L"    DefineDosDeviceW result: %d, error: %lu\n", result, err);
    
    return result;
}

// 卸载临时盘符
static BOOL TempUnmountVolume(WCHAR driveLetter)
{
    WCHAR deviceName[8];
    deviceName[0] = driveLetter;
    deviceName[1] = L':';
    deviceName[2] = L'\0';
    return DefineDosDeviceW(DDD_REMOVE_DEFINITION, deviceName, NULL);
}

// 检查 EFI 文件夹
static BOOL CheckEfiFolder(const WCHAR* rootPath)
{
    WCHAR efiPath[MAX_PATH];
    swprintf(efiPath, MAX_PATH, L"%sEFI", rootPath);
    
    DWORD attrs = GetFileAttributesW(efiPath);
    wprintf(L"    Check path: %s -> attrs=%lu\n", efiPath, attrs);
    
    return (attrs != INVALID_FILE_ATTRIBUTES);
}

int main()
{
    // 设置控制台为 UTF-8
    SetConsoleOutputCP(CP_UTF8);
    
    WCHAR volumeGuid[MAX_PATH];
    HANDLE hFind = FindFirstVolumeW(volumeGuid, ARRAYSIZE(volumeGuid));
    
    if (hFind == INVALID_HANDLE_VALUE) {
        printf("FindFirstVolumeW failed: %lu\n", GetLastError());
        return 1;
    }
    
    printf("=== ESP Partition Detection Test ===\n\n");
    
    int espCount = 0;
    
    do {
        // 验证路径格式
        size_t volLen = wcslen(volumeGuid);
        if (volLen < 5 || 
            volumeGuid[0] != L'\\' || volumeGuid[1] != L'\\' ||
            volumeGuid[2] != L'?' || volumeGuid[3] != L'\\' ||
            volumeGuid[volLen - 1] != L'\\') {
            continue;
        }
        
        // 获取文件系统信息
        WCHAR fsName[32] = {0};
        WCHAR volumeLabel[128] = {0};
        
        if (!GetVolumeInformationW(volumeGuid, volumeLabel, ARRAYSIZE(volumeLabel), 
                                    NULL, NULL, NULL, fsName, ARRAYSIZE(fsName))) {
            continue;
        }
        
        // 检查是否为 FAT32
        BOOL isFat32 = (_wcsicmp(fsName, L"FAT32") == 0 || _wcsicmp(fsName, L"FAT") == 0);
        if (!isFat32) {
            continue;
        }
        
        // 获取已有盘符
        WCHAR existingDrive = L'\0';
        WCHAR pathNames[MAX_PATH];
        DWORD charCount = MAX_PATH;
        
        if (GetVolumePathNamesForVolumeNameW(volumeGuid, pathNames, charCount, &charCount)) {
            if (pathNames[0] != L'\0') {
                existingDrive = pathNames[0];
            }
        }
        
        // 使用 wprintf 输出宽字符
        wprintf(L"Volume: %s\n", volumeGuid);
        wprintf(L"  FileSystem: %s\n", fsName);
        wprintf(L"  Label: %s\n", volumeLabel[0] ? volumeLabel : L"(none)");
        wprintf(L"  Drive Letter: %c\n", existingDrive ? existingDrive : L'-');
        
        BOOL hasEfi = FALSE;
        
        if (existingDrive) {
            // 已有盘符，直接检查
            WCHAR rootPath[8];
            rootPath[0] = existingDrive;
            rootPath[1] = L':';
            rootPath[2] = L'\\';
            rootPath[3] = L'\0';
            hasEfi = CheckEfiFolder(rootPath);
        } else {
            // 没有盘符，临时挂载
            WCHAR tempDrive = L'\0';
            for (WCHAR d = L'Z'; d >= L'C'; d--) {
                WCHAR testPath[4] = {d, L':', L'\\', 0};
                if (GetDriveTypeW(testPath) == DRIVE_NO_ROOT_DIR) {
                    tempDrive = d;
                    break;
                }
            }
            
            if (tempDrive) {
                BOOL mountResult = TempMountVolume(volumeGuid, tempDrive);
                wprintf(L"  Mount result: %s (error=%lu)\n", mountResult ? L"OK" : L"FAIL", GetLastError());
                
                if (mountResult) {
                    // 短暂延迟让文件系统准备好
                    Sleep(100);
                    
                    // 验证盘符是否真的存在
                    WCHAR testPath[4] = {tempDrive, L':', L'\\', 0};
                    UINT driveType = GetDriveTypeW(testPath);
                    wprintf(L"  Drive type after mount: %u\n", driveType);
                    
                    WCHAR rootPath[8];
                    rootPath[0] = tempDrive;
                    rootPath[1] = L':';
                    rootPath[2] = L'\\';
                    rootPath[3] = L'\0';
                    hasEfi = CheckEfiFolder(rootPath);
                    TempUnmountVolume(tempDrive);
                    wprintf(L"  Temp Mount: %c: (tested)\n", tempDrive);
                }
            }
        }
        
        wprintf(L"  Has EFI Folder: %s\n", hasEfi ? L"Yes" : L"No");
        
        if (hasEfi) {
            espCount++;
            wprintf(L"  *** ESP PARTITION FOUND ***\n");
        }
        
        wprintf(L"\n");
        
    } while (FindNextVolumeW(hFind, volumeGuid, ARRAYSIZE(volumeGuid)));
    
    FindVolumeClose(hFind);
    
    wprintf(L"=== Summary ===\n");
    wprintf(L"Total ESP partitions found: %d\n", espCount);
    
    return 0;
}
