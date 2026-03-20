# Boot Manager Pro v1.0

一个面向 Windows 的 **UEFI 引导管理工具**，提供 UEFI 启动项管理、第三方引导管理器安装、引导备份与修复功能。

> ⚠️ **注意**：本工具主要针对 UEFI 模式设计。

## 功能特性

### 1. 引导管理（UEFI 启动项）
- 扫描 UEFI 固件启动项
- 添加 / 删除 UEFI 启动项
- 调整启动顺序
- 设置默认启动项

### 2. 第三方引导管理器

**rEFInd**
- 安装 / 卸载 rEFInd 到 ESP 分区
- 自动注册到 NVRAM 启动项

**Limine**
- 安装 / 卸载 Limine 到 ESP 分区
- 启动项配置管理（limine.conf）
- 自动扫描系统中的 EFI 文件
- 添加自定义启动项

### 3. 备份与恢复
- **MBR 备份**：备份磁盘主引导记录
- **MBR 恢复**：从备份恢复 MBR
- **修复 Windows MBR**：重置 MBR 引导代码
- **UEFI 引导修复**：修复 UEFI 引导（仅 UEFI 模式）

## 系统要求

- Windows 10/11 x64
- UEFI 固件模式（推荐）
- 管理员权限

## 编译

```bat
build.bat
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
```

## 资源文件

**Limine** 下载地址：https://github.com/limine-bootloader/limine/releases

**rEFInd** 下载地址：https://www.rodsbooks.com/refind/

## 第三方组件许可

| 组件 | 许可证 |
|------|--------|
| [Limine](https://github.com/limine-bootloader/limine) | BSD-2-Clause |
| [rEFInd](https://www.rodsbooks.com/refind/) | GPL-3.0 |

## 许可证

MIT License
