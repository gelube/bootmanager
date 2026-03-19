# Boot Manager Pro v3.2.0

一个面向 Windows 的 **UEFI 引导管理工具**，提供 UEFI 启动项管理、第三方引导管理器安装、引导备份与修复功能。

> ⚠️ **注意**：本工具主要针对 UEFI 模式设计。Legacy BIOS (MBR) 模式下功能有限。

## 功能特性

### 1. UEFI 启动项管理
- 扫描 UEFI 固件启动项（读取 `BootOrder`，解析 `BootXXXX`）
- 添加 / 删除 UEFI 启动项
- 调整启动顺序
- 设置默认启动项

### 2. 第三方引导管理器
支持安装和管理流行的第三方 UEFI 引导管理器：

**rEFInd**
- 自动检测并安装到 ESP 分区
- 卸载时自动清理 NVRAM 启动项

**Limine**
- 现代化的引导加载器
- 支持启动项配置管理（limine.conf）
- 自动扫描系统中的 EFI 文件

### 3. 备份与恢复
- **MBR 备份与恢复**：备份/恢复磁盘主引导记录
- **修复 Windows MBR**：重置 MBR 引导代码
- **UEFI 引导修复**：修复损坏的 UEFI 引导

## 系统要求

- Windows 10/11 x64
- **UEFI 固件模式**（推荐）
- 管理员权限

## 编译

### 使用批处理脚本
```bat
build.bat
```

### 手动编译
```bat
gcc -municode src\ui\main.c src\ui\dialog.c src\ui\pages\*.c src\ui\dialogs\*.c src\core\*.c -I include -o dist\BootManagerPro.exe -m64 -mwindows -lcomctl32 -lcomdlg32 -lshell32 -ladvapi32 -lole32 -luuid -O2 -Wall
```

## 使用方法

1. **以管理员身份运行** `BootManagerPro.exe`
2. 在「引导管理」页面管理 UEFI 启动项
3. 在「第三方引导管理器」页面安装 rEFInd 或 Limine
4. 在「备份恢复」页面执行备份与修复操作

## 目录结构

```text
dist/
  BootManagerPro.exe      # 主程序
  limine/                 # Limine 资源文件
  refind/                 # rEFInd 资源文件
  backups/                # 备份文件存储目录

limine/                   # Limine 源文件（编译时复制）
refind/                   # rEFInd 源文件（编译时复制）
```

## 资源文件准备

程序需要以下资源文件夹才能正常安装第三方引导管理器：

**Limine** (`dist/limine/` 或 `limine/`)
```
limine-bios.sys
limine.exe
limine-efi/BOOTX64.EFI
```
下载地址：https://github.com/limine-bootloader/limine/releases

**rEFInd** (`dist/refind/` 或 `refind/`)
```
refind_x64.efi
refind.conf-sample
icons/
```
下载地址：https://www.rodsbooks.com/refind/

## 注意事项

- 修改启动项前建议先备份
- 仅支持 UEFI 模式下的完整功能
- Legacy BIOS 模式下仅支持 MBR 备份与修复
- 引导修复操作会影响系统启动，请谨慎操作

## 第三方组件许可

本程序包含以下第三方组件：

| 组件 | 许可证 | 文件 |
|------|--------|------|
| [Limine](https://github.com/limine-bootloader/limine) | BSD-2-Clause | `limine/LICENSE` |
| [rEFInd](https://www.rodsbooks.com/refind/) | GPL-3.0 | `refind/LICENSE` |

## 许可证

MIT License