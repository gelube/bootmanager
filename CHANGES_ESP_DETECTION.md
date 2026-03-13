# ESP 分区检测逻辑修正

## 问题描述
原实现遍历 C: 到 Z: 盘符是错误的，因为：
- ESP 分区可能没有盘符
- 用户可能临时挂载 ESP 分区
- 应该根据选择的**启动磁盘**来枚举该磁盘上的分区

## 修改内容

### 1. 新增 API：`GetVolumeDiskNumber()`
**位置**: `src/ui/dialog.c`

**功能**: 获取卷所属的物理磁盘编号

**实现**:
```c
INT GetVolumeDiskNumber(const WCHAR* volumeName)
{
    HANDLE hVolume = CreateFileW(volumeName, 0, 
        FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, 
        OPEN_EXISTING, 0, NULL);
    
    STORAGE_DEVICE_NUMBER storageNum = {0};
    DeviceIoControl(hVolume, IOCTL_STORAGE_GET_DEVICE_NUMBER,
        NULL, 0, &storageNum, sizeof(storageNum), &bytesReturned, NULL);
    
    return (INT)storageNum.DeviceNumber;
}
```

### 2. 重写：`EnumEspPartitionsForDisk()`
**位置**: `src/ui/dialog.c`

**功能**: 枚举指定磁盘上的所有 FAT32 分区

**实现流程**:
1. 使用 `FindFirstVolumeW()` 枚举所有卷
2. 对每个卷，使用 `GetVolumePathNamesForVolumeNameW()` 获取盘符
3. 使用 `GetVolumeDiskNumber()` 检查是否属于指定磁盘
4. 使用 `GetVolumeInformationW()` 检查文件系统是否为 FAT32
5. 检查是否包含 EFI 文件夹

### 3. 修改对话框初始化逻辑
**位置**: `src/ui/dialog.c` - `CreateAddEfiDialog()`

**变化**:
- 初始化时只枚举磁盘，不枚举分区
- 分区下拉框初始显示 "(请先选择磁盘)" 并禁用
- 用户选择磁盘后，动态更新分区列表

### 4. 添加磁盘选择事件处理
**位置**: `src/ui/dialog.c` - 消息循环

**功能**: 监听 `CBN_SELCHANGE` 消息，当用户选择磁盘后：
1. 获取选中的磁盘编号
2. 释放旧的分区列表
3. 调用 `EnumEspPartitionsForDisk()` 枚举新分区
4. 更新分区下拉框
5. 启用/禁用分区下拉框（根据是否有分区）

### 5. 更新头文件
**位置**: `src/ui/dialog.h`

**变化**:
- 更新函数声明：`EnumEspPartitions()` → `EnumEspPartitionsForDisk(INT diskNumber, ...)`
- 新增函数声明：`GetVolumeDiskNumber()`
- 更新结构体：添加 `hComboDisk` 和 `hComboPart` 句柄

## UI 交互流程

```
对话框打开:
┌─────────────────────────────────────────┐
│  添加 EFI 启动项                    [×] │
├─────────────────────────────────────────┤
│  菜单标题： [New Boot Entry_______]     │
│  启动磁盘： [磁盘 0 - 465GB    ▼]  ← 自动枚举 │
│  启动分区： [(请先选择磁盘)    ▼]  ← 灰色/禁用 │
│  启动文件： [____________________]      │
│             [浏览...]                   │
│         [确定]        [取消]            │
└─────────────────────────────────────────┘

用户选择磁盘 0 后:
┌─────────────────────────────────────────┐
│  启动分区： [ESP 分区 - FAT32 [Y:] ▼]  ← 自动填充 │
│             (枚举磁盘 0 上的所有 FAT32 分区)    │
└─────────────────────────────────────────┘
```

## 技术要点

### 1. 卷枚举 API
- `FindFirstVolumeW()` / `FindNextVolumeW()` - 枚举所有卷
- `GetVolumePathNamesForVolumeNameW()` - 获取卷的盘符
- `GetVolumeInformationW()` - 获取文件系统信息
- `IOCTL_STORAGE_GET_DEVICE_NUMBER` - 获取卷关联的物理磁盘

### 2. 动态更新
- 用户选择磁盘后立即更新分区列表
- 自动释放旧资源，避免内存泄漏
- 根据分区数量启用/禁用下拉框

### 3. 错误处理
- 卷句柄打开失败时返回 -1
- 没有盘符的卷自动跳过
- 不属于指定磁盘的分区自动过滤

## 编译测试

```bash
cd C:\Users\Administrator\.openclaw\workspace\bootmanager
gcc -municode src/ui/main.c src/ui/dialog.c src/core/*.c \
    -o dist/BootManagerPro.exe -m64 -mwindows \
    -lcomctl32 -lshell32 -ladvapi32 -lole32 -luuid -O2 -Wall
```

**结果**: ✅ 编译成功

## 修改文件清单

| 文件 | 修改内容 |
|------|----------|
| `src/ui/dialog.c` | 新增 `GetVolumeDiskNumber()`、重写 `EnumEspPartitionsForDisk()`、修改对话框初始化、添加磁盘选择事件处理 |
| `src/ui/dialog.h` | 更新函数声明、添加控件句柄字段 |

## 后续优化建议

1. **临时挂载无盘符分区**: 对于没有盘符的 ESP 分区，可以使用 `DefineDosDevice()` 临时挂载检查
2. **GPT 分区类型检测**: 可以进一步检查分区 GUID 确认是否为 EFI 系统分区
3. **缓存机制**: 对已检测的磁盘分区结果进行缓存，避免重复枚举
