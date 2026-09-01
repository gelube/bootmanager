/**
 * envinfo.h - 运行环境探测（WinPE / 权限 / bcdedit 可用性）
 * 启动时探测一次，各模块按能力降级，不再各自散落特判。
 */
#pragma once
#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 是否运行在 WinPE 下（注册表 MiniNT 键） */
BOOL EnvIsWinPE(void);

/* bcdedit.exe 是否可用（PATH 或程序目录） */
BOOL EnvHasBcdEdit(void);

#ifdef __cplusplus
}
#endif
