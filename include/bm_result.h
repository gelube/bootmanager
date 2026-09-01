/**
 * bm_result.h - 统一结果码
 * 所有模块对外的错误通道；UI 层只需展示 message 字段。
 */
#pragma once
#include <windows.h>

typedef enum _BM_STATUS {
    BM_OK = 0,
    BM_ERR_INVALID_PARAM,
    BM_ERR_NO_MEMORY,
    BM_ERR_OPEN_DISK,
    BM_ERR_READ_DISK,
    BM_ERR_WRITE_DISK,
    BM_ERR_VERIFY_FAILED,   /* 回读验证失败 */
    BM_ERR_BACKUP_FAILED,   /* 写前备份失败（事务中止） */
    BM_ERR_NOT_FOUND,       /* 目标/资源未找到 */
    BM_ERR_UNSUPPORTED,     /* 当前环境不支持（如非MBR盘） */
    BM_ERR_EXTERNAL,        /* 子进程/外部工具失败 */
    BM_ERR_CANCELLED
} BM_STATUS;

typedef struct _BM_RESULT {
    BM_STATUS status;
    WCHAR     message[256];
    DWORD     win32Error;
} BM_RESULT;

#ifdef __cplusplus
extern "C" {
#endif

BM_RESULT BM_Ok(void);
BM_RESULT BM_Fail(BM_STATUS status, const WCHAR* fmt, ...);
BM_RESULT BM_FailWin32(BM_STATUS status, DWORD err, const WCHAR* fmt, ...);
BOOL      BM_IsOk(const BM_RESULT* r);

#ifdef __cplusplus
}
#endif
