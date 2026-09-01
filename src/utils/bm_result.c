/**
 * bm_result.c - 统一结果码实现
 */
#include "../../include/bm_result.h"
#include <stdio.h>
#include <wchar.h>
#include <stdarg.h>

BM_RESULT BM_Ok(void) {
    BM_RESULT r;
    r.status = BM_OK;
    r.win32Error = 0;
    r.message[0] = L'\0';
    return r;
}

BM_RESULT BM_Fail(BM_STATUS status, const WCHAR* fmt, ...) {
    BM_RESULT r;
    va_list args;
    r.status = status;
    r.win32Error = 0;
    if (fmt) {
        va_start(args, fmt);
        _vsnwprintf(r.message, 256, fmt, args);
        va_end(args);
        r.message[255] = L'\0';
    } else {
        r.message[0] = L'\0';
    }
    return r;
}

BM_RESULT BM_FailWin32(BM_STATUS status, DWORD err, const WCHAR* fmt, ...) {
    BM_RESULT r;
    va_list args;
    int len;
    r.status = status;
    r.win32Error = err;
    if (fmt) {
        va_start(args, fmt);
        _vsnwprintf(r.message, 200, fmt, args);
        va_end(args);
        r.message[200] = L'\0';
        len = (int)wcslen(r.message);
        _snwprintf(r.message + len, 256 - len - 1, L" (Win32 错误 %lu)", err);
        r.message[255] = L'\0';
    } else {
        _snwprintf(r.message, 256, L"操作失败 (Win32 错误 %lu)", err);
        r.message[255] = L'\0';
    }
    return r;
}

BOOL BM_IsOk(const BM_RESULT* r) {
    return r && r->status == BM_OK;
}
