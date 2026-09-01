/**
 * strconv.h - 编码转换 helper
 * 项目内唯一允许的 MultiByteToWideChar/WideCharToMultiByte 封装。
 * codepage 在这里显式声明，业务代码禁止直接调转换 API。
 */
#pragma once
#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 控制台子进程（bcdedit/mountvol 等）输出 → 宽字符，用 OEM 代码页 */
BOOL OemOutputToWide(const char* in, WCHAR* out, DWORD outChars);

/* 宽字符 → UTF-8（写 limine.conf 等磁盘文本）。返回所需/写入字节数 */
int WideToUtf8(const WCHAR* in, char* out, int outBytes);

/* UTF-8 → 宽字符 */
int Utf8ToWide(const char* in, WCHAR* out, DWORD outChars);

#ifdef __cplusplus
}
#endif
