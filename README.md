# Boot Manager Pro v3.2.0

一个面向 Windows 的引导管理工具，当前以 `rEFInd + refind.conf` 为统一入口，同时保留固件启动项管理与引导修复能力。

## 功能特性

### 1. 固件启动项管理
- 扫描固件启动项（读取 `BootOrder`，解析 `BootXXXX`）
- 删除固件启动项
- 调整启动顺序
- 设置默认启动项

说明：这一页管理的是 UEFI 固件里的启动项，不等同于 `rEFInd` 的 `menuentry`。

### 2. rEFInd 管理
- 挂载 ESP 分区
- 安装 / 卸载 `rEFInd`
- 读取 `refind.conf` 中的 `menuentry`
- 添加 EFI / WIM / VHD 菜单项
- 删除 Boot Manager Pro 托管的 `menuentry`

说明：程序新增的 EFI / WIM / VHD 启动配置，优先写入 `refind.conf`，不再依赖 BCD / NVRAM 创建菜单入口。

### 3. 备份与修复
- 备份 MBR
- 备份 BCD
- 备份 NVRAM
- 恢复备份
- 修复引导（`bootrec` / `bcdboot`）

## 当前架构

- 固件启动项管理：仍通过 `bcdedit` / `fwbootmgr` 读取和调整 `BootOrder`
- rEFInd 菜单管理：通过 `ESP:\EFI\refind\refind.conf` 读写 `menuentry`
- rEFInd 安装/卸载：只走 ESP 文件链路，不再依赖 `bcdedit` / NVRAM 注册

## 代码结构

```text
src/
  ui/
    main.c                 - 主窗口与页面逻辑
    dialogs/add_efi_dialog.c
  core/
    boot.c                 - 固件启动项与 BootOrder 管理
    uefi.h                 - 对 boot.c 的兼容别名层
    refind.c               - rEFInd 安装 / 卸载
    refind_config.c        - refind.conf menuentry 读写
    wimboot.c              - WIM / VHD 菜单项写入
    backup.c               - 备份与修复
dist/
  BootManagerPro.exe
```

## 编译

### 使用批处理脚本
```bat
build.bat
```

### 手动编译
```bat
gcc -municode src\ui\main.c src\ui\dialog.c src\ui\pages\*.c src\ui\dialogs\*.c src\core\*.c src\hal\*.c src\utils\*.c -I include -o dist\BootManagerPro.exe -m64 -mwindows -lcomctl32 -lcomdlg32 -lshell32 -ladvapi32 -lole32 -luuid -O2 -Wall
```

## 使用方法

1. 以管理员身份运行 `BootManagerPro.exe`
2. 在 `启动项管理` 页面管理固件启动项与 `BootOrder`
3. 在 `rEFInd 管理` 页面安装 / 卸载 `rEFInd`，并管理 `menuentry`
4. 在 `备份与修复` 页面执行备份、恢复与修复操作

## 注意事项

- 修改固件启动项或引导配置前，建议先做备份
- `rEFInd` 安装依赖程序目录中的 `refind` 资源文件夹
- 仅 Boot Manager Pro 托管条目支持在界面内直接删除
- 引导修复会直接影响系统启动，请谨慎操作

## 许可证

MIT License
