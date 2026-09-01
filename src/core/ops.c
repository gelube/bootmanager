/**
 * ops.c - 安全事务层实现
 * Validate → Backup(强制) → Execute → Verify(失败自动回滚)
 */
#include "../../include/ops.h"
#include "../core/backup.h"
#include <wchar.h>
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------
 * 事务原语
 * ------------------------------------------------------------ */

static BOOL ReadSector0(int diskNumber, BYTE buf[512]) {
    WCHAR path[MAX_PATH];
    HANDLE h;
    DWORD read = 0;
    swprintf(path, MAX_PATH, L"\\\\.\\PhysicalDrive%d", diskNumber);
    h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                    NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return FALSE;
    {
        BOOL ok = ReadFile(h, buf, 512, &read, NULL) && read == 512;
        CloseHandle(h);
        return ok;
    }
}

/* 生成自动备份路径：backups/auto/YYYYMMDD_HHMMSS_diskN.bin */
static BOOL MakeAutoBackupPath(int diskNumber, WCHAR out[MAX_PATH]) {
    WCHAR dir[MAX_PATH];
    WCHAR ts[32];
    SYSTEMTIME st;

    if (!BackupGetBackupDir(dir, MAX_PATH)) return FALSE;
    if (wcslen(dir) + 64 >= MAX_PATH) return FALSE;
    wcscat(dir, L"\\auto");
    CreateDirectoryW(dir, NULL);

    GetLocalTime(&st);
    swprintf(out, MAX_PATH, L"%s\\%04u%02u%02u_%02u%02u%02u_disk%d.bin",
             dir, st.wYear, st.wMonth, st.wDay,
             st.wHour, st.wMinute, st.wSecond, diskNumber);
    (void)ts;
    return TRUE;
}

BM_RESULT OpsGuardBegin(int diskNumber, OPS_SECTOR_GUARD* guard) {
    HANDLE hFile;
    DWORD written = 0;

    if (!guard) return BM_Fail(BM_ERR_INVALID_PARAM, L"参数无效");

    /* 1. 备份到内存 */
    if (!ReadSector0(diskNumber, guard->before))
        return BM_Fail(BM_ERR_READ_DISK, L"无法读取磁盘 %d 的 MBR", diskNumber);

    /* 2. 备份到磁盘（强制，失败即中止事务） */
    if (!MakeAutoBackupPath(diskNumber, guard->autoBackupPath))
        return BM_Fail(BM_ERR_BACKUP_FAILED, L"无法生成自动备份路径");

    hFile = CreateFileW(guard->autoBackupPath, GENERIC_WRITE, 0,
                        NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE)
        return BM_FailWin32(BM_ERR_BACKUP_FAILED, GetLastError(),
                            L"无法创建自动备份文件");

    {
        BOOL ok = WriteFile(hFile, guard->before, 512, &written, NULL) && written == 512;
        CloseHandle(hFile);
        if (!ok)
            return BM_Fail(BM_ERR_BACKUP_FAILED, L"写入自动备份失败，已中止操作");
    }
    return BM_Ok();
}

BM_RESULT OpsGuardVerify(int diskNumber, const OPS_SECTOR_GUARD* guard,
                         BOOL verifyPartitionTable) {
    BYTE after[512];
    if (!guard) return BM_Fail(BM_ERR_INVALID_PARAM, L"参数无效");
    if (!ReadSector0(diskNumber, after))
        return BM_Fail(BM_ERR_VERIFY_FAILED, L"写入后回读失败");

    if (after[510] != 0x55 || after[511] != 0xAA)
        return BM_Fail(BM_ERR_VERIFY_FAILED, L"写入后签名校验失败 (55AA 丢失)");

    if (verifyPartitionTable &&
        memcmp(&after[446], &guard->before[446], 66) != 0)
        return BM_Fail(BM_ERR_VERIFY_FAILED, L"分区表在操作后发生变化");

    return BM_Ok();
}

BM_RESULT OpsGuardRollback(int diskNumber, const OPS_SECTOR_GUARD* guard) {
    HANDLE h, hFile;
    BYTE mbr[512];
    DWORD read = 0, written = 0;
    WCHAR path[MAX_PATH];
    BOOL ok;

    if (!guard) return BM_Fail(BM_ERR_INVALID_PARAM, L"参数无效");

    /* 从自动备份文件回滚（不用内存副本，验证备份确实可读） */
    hFile = CreateFileW(guard->autoBackupPath, GENERIC_READ, 0,
                        NULL, OPEN_EXISTING, 0, NULL);
    if (hFile == INVALID_HANDLE_VALUE)
        return BM_FailWin32(BM_ERR_BACKUP_FAILED, GetLastError(), L"回滚时无法打开备份");

    ok = ReadFile(hFile, mbr, 512, &read, NULL) && read == 512;
    CloseHandle(hFile);
    if (!ok) return BM_Fail(BM_ERR_READ_DISK, L"回滚时读取备份失败");

    swprintf(path, MAX_PATH, L"\\\\.\\PhysicalDrive%d", diskNumber);
    h = CreateFileW(path, GENERIC_READ | GENERIC_WRITE,
                    FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return BM_FailWin32(BM_ERR_OPEN_DISK, GetLastError(), L"回滚时无法打开磁盘");

    SetFilePointer(h, 0, NULL, FILE_BEGIN);
    ok = WriteFile(h, mbr, 512, &written, NULL) && written == 512;
    CloseHandle(h);
    return ok ? BM_Ok()
              : BM_Fail(BM_ERR_WRITE_DISK, L"回滚写入失败，请立即用备份文件手动恢复");
}

/* ------------------------------------------------------------
 * 事务包装的操作
 * ------------------------------------------------------------ */

/* 通用三段式：Backup → action() → Verify */
static BM_RESULT RunDiskWriteTransaction(
    int diskNumber, BOOL verifyPartitionTable,
    BM_RESULT (*action)(void* ctx), void* ctx,
    const WCHAR* actionName)
{
    OPS_SECTOR_GUARD guard;
    BM_RESULT r = OpsGuardBegin(diskNumber, &guard);
    if (r.status != BM_OK) return r;

    r = action(ctx);
    if (r.status != BM_OK) return r;   /* 执行失败：盘未动或部分写，交由上层提示 */

    r = OpsGuardVerify(diskNumber, &guard, verifyPartitionTable);
    if (r.status != BM_OK) {
        BM_RESULT rb = OpsGuardRollback(diskNumber, &guard);
        if (rb.status != BM_OK) {
            /* 回滚也失败：把两条信息都带出去 */
            return BM_Fail(r.status, L"%s失败且回滚失败！请立即用备份恢复: %s",
                           actionName, (const WCHAR*)guard.autoBackupPath);
        }
        return BM_Fail(r.status, L"%s失败，已自动回滚。备份: %s",
                       actionName, (const WCHAR*)guard.autoBackupPath);
    }
    return BM_Ok();
}

/* ---- MBR 引导码安装 ---- */

typedef struct _INSTALL_CTX {
    int diskNumber;
    MBR_BOOT_TYPE type;
} INSTALL_CTX;

static BM_RESULT action_install(void* p) {
    INSTALL_CTX* c = (INSTALL_CTX*)p;
    WCHAR err[512] = {0};
    if (MBR_Install(c->diskNumber, c->type, err, 512)) return BM_Ok();
    return BM_Fail(BM_ERR_EXTERNAL, L"%s", err[0] ? err : L"引导码写入失败");
}

BM_RESULT OpsInstallMbrBootCode(int diskNumber, MBR_BOOT_TYPE type) {
    INSTALL_CTX ctx = { diskNumber, type };
    if (MBR_IsDiskGPT(diskNumber))
        return BM_Fail(BM_ERR_UNSUPPORTED, L"磁盘 %d 是 GPT 格式，不适用 MBR 引导", diskNumber);
    return RunDiskWriteTransaction(diskNumber, TRUE, action_install, &ctx,
                                   L"安装引导码");
}

/* ---- 引导码恢复 ---- */

typedef struct _RESTORE_CTX {
    int diskNumber;
    const WCHAR* file;
    BOOL preservePartTable;
} RESTORE_CTX;

static BM_RESULT action_restore_mbr(void* p) {
    RESTORE_CTX* c = (RESTORE_CTX*)p;
    WCHAR err[512] = {0};
    if (MBR_Restore(c->diskNumber, c->file, c->preservePartTable != FALSE, err, 512))
        return BM_Ok();
    return BM_Fail(BM_ERR_EXTERNAL, L"%s", err[0] ? err : L"恢复失败");
}

BM_RESULT OpsRestoreMbrFromFile(int diskNumber, const WCHAR* file, BOOL preservePartTable) {
    RESTORE_CTX ctx = { diskNumber, file, preservePartTable };
    return RunDiskWriteTransaction(diskNumber, preservePartTable ? TRUE : FALSE,
                                   action_restore_mbr, &ctx, L"恢复 MBR");
}

BM_RESULT OpsRestoreBootCodeFromFile(int diskNumber, const WCHAR* file) {
    return OpsRestoreMbrFromFile(diskNumber, file, TRUE);
}

/* ---- 活动分区 ---- */

typedef struct _ACTIVE_CTX {
    int diskNumber;
    int partitionNumber;
} ACTIVE_CTX;

static BM_RESULT action_set_active(void* p) {
    ACTIVE_CTX* c = (ACTIVE_CTX*)p;
    WCHAR err[512] = {0};
    if (MBR_SetActivePartition(c->diskNumber, c->partitionNumber, err, 512))
        return BM_Ok();
    return BM_Fail(BM_ERR_EXTERNAL, L"%s", err[0] ? err : L"设置活动分区失败");
}

BM_RESULT OpsSetActivePartition(int diskNumber, int partitionNumber) {
    ACTIVE_CTX ctx = { diskNumber, partitionNumber };
    return RunDiskWriteTransaction(diskNumber, TRUE, action_set_active, &ctx,
                                   L"设置活动分区");
}
