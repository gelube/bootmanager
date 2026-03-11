# Boot Manager Pro v3

一个专业的 UEFI 启动管理工具，适用于 Windows 系统。

## 功能特性

### 1. UEFI 启动项管理
- 扫描 UEFI Boot Entries（读取 BootOrder，解析 BootXXXX）
- 添加启动项（构建 EFI_LOAD_OPTION）
- 删除启动项
- 设置 BootOrder

### 2. rEFInd 安装/卸载
- 挂载 ESP 分区
- 复制 rEFInd 文件（从 Z:\refind0.14.2\refind\）
- 覆盖 bootx64.efi（并备份为 .bak）
- 添加 NVRAM 启动项
- 卸载时恢复备份

### 3. 备份恢复
- 备份 MBR（512字节）
- 备份 BCD（使用 bcdedit）
- 备份 NVRAM（导出启动项）
- 恢复功能
- 修复引导（bootrec / bcdboot）

## 界面特性

- Win32 API，纯 C 编写
- 左右布局：左侧导航 + 右侧内容
- 深色主题（RGB(18,18,22) 背景）
- 圆角按钮（6px 圆角）
- Microsoft YaHei UI 字体
- 按钮可点击，消息处理可靠

## 代码结构

```
src/
  ui/
    main.c          - 主程序 + UI
  core/
    uefi.c          - UEFI 操作
    uefi.h
    refind.c        - rEFInd 安装
    refind.h
    backup.c        - 备份恢复
    backup.h
dist/
  BootManagerPro.exe - 编译输出
```

## 编译

### 使用批处理脚本
```bash
build.bat
```

### 手动编译
```bash
gcc -municode src\ui\main.c src\core\*.c -o dist\BootManagerPro.exe -m64 -mwindows -lcomctl32 -lshell32 -ladvapi32 -O2
```

## 系统要求

- Windows 10/11 (64-bit)
- UEFI 固件
- 管理员权限（用于修改启动项）
- MinGW-w64 编译器（用于编译）

## 使用方法

1. 以管理员身份运行 BootManagerPro.exe
2. 使用左侧导航栏切换功能页面
3. 在 UEFI 页面管理启动项
4. 在 rEFInd 页面安装/卸载 rEFInd
5. 在备份页面进行备份和修复操作

## 注意事项

- 修改启动项前请务必备份
- rEFInd 安装需要源文件位于 Z:\refind0.14.2\refind\
- 修复操作可能会影响系统启动，请谨慎使用

## 许可证

MIT License
