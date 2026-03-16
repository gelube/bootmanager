/**
 * refind_config.c - 生成/读取 refind.conf 中的 menuentry
 * 只操作 ESP 文件，不依赖 bcdedit/NVRAM
 */

#include "refind_config.h"
#include <wchar.h>
#include <stdlib.h>
#include <string.h>

#define CONF_PATH_FMT L"%s\\EFI\\refind\\refind.conf"
#define MANAGED_TAG L"# BootManagerPro"

static void GetConfPath(const WCHAR* espDrive, WCHAR* out, DWORD size) {
    swprintf(out, size, CONF_PATH_FMT, espDrive);
}

// ============================================================
// 解析 refind.conf，提取所有 menuentry 块
// ============================================================
REFIND_MENU_ENTRY* RefindConfigLoad(const WCHAR* espDrive) {
    WCHAR confPath[MAX_PATH];
    GetConfPath(espDrive, confPath, MAX_PATH);

    HANDLE hFile = CreateFileW(confPath, GENERIC_READ, FILE_SHARE_READ,
                               NULL, OPEN_EXISTING, 0, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return NULL;

    DWORD size = GetFileSize(hFile, NULL);
    if (size == 0 || size == INVALID_FILE_SIZE) { CloseHandle(hFile); return NULL; }

    char* buf = (char*)malloc(size + 2);
    if (!buf) { CloseHandle(hFile); return NULL; }

    DWORD read = 0;
    ReadFile(hFile, buf, size, &read, NULL);
    CloseHandle(hFile);
    buf[read] = '\0';

    // 转宽字符
    int wlen = MultiByteToWideChar(CP_UTF8, 0, buf, -1, NULL, 0);
    WCHAR* wbuf = (WCHAR*)malloc(wlen * sizeof(WCHAR));
    MultiByteToWideChar(CP_UTF8, 0, buf, -1, wbuf, wlen);
    free(buf);

    REFIND_MENU_ENTRY* head = NULL;
    REFIND_MENU_ENTRY* tail = NULL;

    WCHAR* p = wbuf;
    while (*p) {
        // 跳过空白行和注释
        while (*p == L'\r' || *p == L'\n' || *p == L' ' || *p == L'\t') p++;
        if (!*p) break;

        if (wcsncmp(p, MANAGED_TAG, wcslen(MANAGED_TAG)) == 0) {
            WCHAR* nextLine = wcschr(p, L'\n');
            p = nextLine ? nextLine + 1 : p + wcslen(p);
            while (*p == L'\r' || *p == L'\n') p++;
            if (wcsncmp(p, L"menuentry", 9) != 0) {
                continue;
            }
        }

        if (wcsncmp(p, L"menuentry", 9) == 0) {
            REFIND_MENU_ENTRY* e = (REFIND_MENU_ENTRY*)calloc(1, sizeof(REFIND_MENU_ENTRY));
            WCHAR* entryStart = p;

            // 如果上一行是管理标记，则把该条目视为可安全删除的托管条目
            e->isManaged = FALSE;
            if (entryStart > wbuf) {
                WCHAR* lineStart = entryStart;
                while (lineStart > wbuf && *(lineStart - 1) != L'\n') lineStart--;
                if (lineStart > wbuf) {
                    WCHAR* prevStart = lineStart - 1;
                    while (prevStart > wbuf && *(prevStart - 1) != L'\n') prevStart--;
                    if (wcsncmp(prevStart, MANAGED_TAG, wcslen(MANAGED_TAG)) == 0) {
                        e->isManaged = TRUE;
                    }
                }
            }

            // 提取标题（引号内）
            WCHAR* q = wcschr(p, L'"');
            if (q) {
                q++;
                WCHAR* end = wcschr(q, L'"');
                if (end) {
                    DWORD tlen = (DWORD)(end - q);
                    if (tlen >= 256) tlen = 255;
                    wcsncpy(e->title, q, tlen);
                    e->title[tlen] = L'\0';
                }
            }

            // 找到 { 块
            WCHAR* brace = wcschr(p, L'{');
            WCHAR* close = brace ? wcschr(brace, L'}') : NULL;
            if (brace && close) {
                // 在块内找 loader 和 options
                WCHAR block[2048] = {0};
                DWORD blen = (DWORD)(close - brace - 1);
                if (blen >= 2048) blen = 2047;
                wcsncpy(block, brace + 1, blen);

                WCHAR* line = block;
                while (*line) {
                    while (*line == L'\r' || *line == L'\n' || *line == L' ' || *line == L'\t') line++;
                    if (!*line) break;
                    WCHAR* eol = wcschr(line, L'\n');
                    if (!eol) eol = line + wcslen(line);
                    WCHAR tmp[512] = {0};
                    DWORD ll = (DWORD)(eol - line);
                    if (ll >= 512) ll = 511;
                    wcsncpy(tmp, line, ll);
                    // trim \r
                    WCHAR* cr = wcschr(tmp, L'\r');
                    if (cr) *cr = L'\0';

                    if (wcsncmp(tmp, L"loader", 6) == 0) {
                        WCHAR* v = tmp + 6;
                        while (*v == L' ' || *v == L'\t') v++;
                        wcsncpy(e->loader, v, 511);
                    } else if (wcsncmp(tmp, L"options", 7) == 0) {
                        WCHAR* v = tmp + 7;
                        while (*v == L' ' || *v == L'\t') v++;
                        // 去掉引号
                        if (*v == L'"') { v++; WCHAR* eq = wcschr(v, L'"'); if (eq) *eq = L'\0'; }
                        wcsncpy(e->options, v, 511);
                    }
                    line = (*eol) ? eol + 1 : eol;
                }
                p = close + 1;
            } else {
                // 格式异常，跳到行尾
                while (*p && *p != L'\n') p++;
            }

            if (head == NULL) { head = tail = e; }
            else { tail->next = e; tail = e; }
        } else {
            // 跳到行尾
            while (*p && *p != L'\n') p++;
        }
    }

    free(wbuf);
    return head;
}

// ============================================================
// 追加 menuentry 到 refind.conf
// ============================================================
BOOL RefindConfigAddMenuEntry(const WCHAR* espDrive, const WCHAR* title,
                               const WCHAR* loader, const WCHAR* options) {
    if (!espDrive || !title || !loader) return FALSE;

    WCHAR confPath[MAX_PATH];
    GetConfPath(espDrive, confPath, MAX_PATH);

    // 构造追加内容
    WCHAR block[1024];
    if (options && wcslen(options) > 0) {
        swprintf(block, 1024,
            L"\r\n" MANAGED_TAG L"\r\nmenuentry \"%s\" {\r\n    loader %s\r\n    options \"%s\"\r\n}\r\n",
            title, loader, options);
    } else {
        swprintf(block, 1024,
            L"\r\n" MANAGED_TAG L"\r\nmenuentry \"%s\" {\r\n    loader %s\r\n}\r\n",
            title, loader);
    }

    // 转 UTF-8
    int nbytes = WideCharToMultiByte(CP_UTF8, 0, block, -1, NULL, 0, NULL, NULL);
    char* utf8 = (char*)malloc(nbytes);
    if (!utf8) return FALSE;
    WideCharToMultiByte(CP_UTF8, 0, block, -1, utf8, nbytes, NULL, NULL);
    nbytes--; // 去掉 null terminator

    HANDLE hFile = CreateFileW(confPath, FILE_APPEND_DATA, 0, NULL,
                               OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) { free(utf8); return FALSE; }

    DWORD written = 0;
    BOOL ok = WriteFile(hFile, utf8, (DWORD)nbytes, &written, NULL);
    CloseHandle(hFile);
    free(utf8);
    return ok && written == (DWORD)nbytes;
}

// ============================================================
// 删除指定 title 的 menuentry 块
// ============================================================
BOOL RefindConfigRemoveMenuEntry(const WCHAR* espDrive, const WCHAR* title) {
    if (!espDrive || !title) return FALSE;

    WCHAR confPath[MAX_PATH];
    GetConfPath(espDrive, confPath, MAX_PATH);

    HANDLE hFile = CreateFileW(confPath, GENERIC_READ, FILE_SHARE_READ,
                               NULL, OPEN_EXISTING, 0, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return FALSE;

    DWORD fsize = GetFileSize(hFile, NULL);
    char* buf = (char*)malloc(fsize + 2);
    DWORD rd = 0;
    ReadFile(hFile, buf, fsize, &rd, NULL);
    CloseHandle(hFile);
    buf[rd] = '\0';

    int wlen = MultiByteToWideChar(CP_UTF8, 0, buf, -1, NULL, 0);
    WCHAR* wbuf = (WCHAR*)malloc(wlen * sizeof(WCHAR));
    MultiByteToWideChar(CP_UTF8, 0, buf, -1, wbuf, wlen);
    free(buf);

    // 构造搜索模式：menuentry "title"
    WCHAR pattern[300];
    swprintf(pattern, 300, L"menuentry \"%s\"", title);

    WCHAR* found = wcsstr(wbuf, pattern);
    if (!found) { free(wbuf); return FALSE; }

    // 找到对应的 { }
    WCHAR* brace = wcschr(found, L'{');
    if (!brace) { free(wbuf); return FALSE; }
    WCHAR* close = wcschr(brace, L'}');
    if (!close) { free(wbuf); return FALSE; }

    // 如果是 Boot Manager Pro 托管条目，连同管理标记一并删除
    WCHAR* start = found;
    while (start > wbuf && (*(start-1) == L'\n' || *(start-1) == L'\r')) start--;

    if (start > wbuf) {
        WCHAR* lineStart = start;
        while (lineStart > wbuf && *(lineStart - 1) != L'\n') lineStart--;
        if (lineStart > wbuf) {
            WCHAR* prevStart = lineStart - 1;
            while (prevStart > wbuf && *(prevStart - 1) != L'\n') prevStart--;
            if (wcsncmp(prevStart, MANAGED_TAG, wcslen(MANAGED_TAG)) == 0) {
                start = prevStart;
                while (start > wbuf && (*(start - 1) == L'\n' || *(start - 1) == L'\r')) start--;
            }
        }
    }

    // 往后跳过换行
    WCHAR* end = close + 1;
    while (*end == L'\r' || *end == L'\n') end++;

    // 拼接去掉该块的新内容
    DWORD newLen = (DWORD)(start - wbuf) + (DWORD)wcslen(end) + 1;
    WCHAR* newbuf = (WCHAR*)malloc(newLen * sizeof(WCHAR));
    wcsncpy(newbuf, wbuf, (DWORD)(start - wbuf));
    newbuf[start - wbuf] = L'\0';
    wcscat(newbuf, end);
    free(wbuf);

    // 写回
    int nbytes = WideCharToMultiByte(CP_UTF8, 0, newbuf, -1, NULL, 0, NULL, NULL);
    char* out = (char*)malloc(nbytes);
    WideCharToMultiByte(CP_UTF8, 0, newbuf, -1, out, nbytes, NULL, NULL);
    nbytes--;
    free(newbuf);

    hFile = CreateFileW(confPath, GENERIC_WRITE, 0, NULL,
                        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) { free(out); return FALSE; }
    DWORD wr = 0;
    BOOL ok = WriteFile(hFile, out, (DWORD)nbytes, &wr, NULL);
    CloseHandle(hFile);
    free(out);
    return ok;
}

void RefindConfigFreeEntries(REFIND_MENU_ENTRY* head) {
    while (head) {
        REFIND_MENU_ENTRY* next = head->next;
        free(head);
        head = next;
    }
}
