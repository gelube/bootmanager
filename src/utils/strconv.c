/**
 * strconv.c - 编码转换 helper 实现
 */
#include "../../include/strconv.h"

BOOL OemOutputToWide(const char* in, WCHAR* out, DWORD outChars) {
    if (!in || !out || outChars == 0) return FALSE;
    if (MultiByteToWideChar(CP_OEMCP, 0, in, -1, out, (int)outChars) == 0) {
        /* OEM 转换失败时退回 ACP，至少不产生空输出 */
        if (MultiByteToWideChar(CP_ACP, 0, in, -1, out, (int)outChars) == 0) {
            out[0] = L'\0';
            return FALSE;
        }
    }
    return TRUE;
}

int WideToUtf8(const WCHAR* in, char* out, int outBytes) {
    if (!in || !out || outBytes <= 0) return 0;
    int n = WideCharToMultiByte(CP_UTF8, 0, in, -1, out, outBytes, NULL, NULL);
    if (n == 0 && outBytes > 0) out[0] = '\0';
    return n;
}

int Utf8ToWide(const char* in, WCHAR* out, DWORD outChars) {
    if (!in || !out || outChars == 0) return 0;
    int n = MultiByteToWideChar(CP_UTF8, 0, in, -1, out, (int)outChars);
    if (n == 0 && outChars > 0) out[0] = L'\0';
    return n;
}
