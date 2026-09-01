/**
 * ops.h - 安全事务层
 * 所有破坏性操作的唯一入口。UI 层禁止绕过本层直接调用服务层写函数。
 *
 * 事务流程：Validate → Backup(强制) → Execute → Verify(失败自动回滚)
 */
#pragma once
#include <windows.h>
#include "bm_result.h"
#include "mbr_manager.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 安装引导码到 MBR（Limine/Windows/GRUB4DOS），自动备份+验证 */
BM_RESULT OpsInstallMbrBootCode(int diskNumber, MBR_BOOT_TYPE type);

/* 恢复引导码（仅覆盖前 446 字节，保留分区表），自动备份+验证 */
BM_RESULT OpsRestoreBootCodeFromFile(int diskNumber, const WCHAR* backupFile);

/* 恢复整个 MBR。preservePartTable=TRUE 时保留当前分区表 */
BM_RESULT OpsRestoreMbrFromFile(int diskNumber, const WCHAR* backupFile, BOOL preservePartTable);

/* 设置活动分区，自动备份+验证 */
BM_RESULT OpsSetActivePartition(int diskNumber, int partitionNumber);

/* 从当前 MBR 提取的分区表与磁盘签名（校验用） */
typedef struct _OPS_SECTOR_GUARD {
    BYTE  before[512];
    WCHAR autoBackupPath[MAX_PATH];  /* 本次事务自动生成的备份文件 */
} OPS_SECTOR_GUARD;

/* 供 ops 层内部与扩展使用：事务原语 */
BM_RESULT OpsGuardBegin(int diskNumber, OPS_SECTOR_GUARD* guard);
BM_RESULT OpsGuardVerify(int diskNumber, const OPS_SECTOR_GUARD* guard, BOOL verifyPartitionTable);
BM_RESULT OpsGuardRollback(int diskNumber, const OPS_SECTOR_GUARD* guard);

#ifdef __cplusplus
}
#endif
