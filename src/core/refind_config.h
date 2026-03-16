#ifndef REFIND_CONFIG_H
#define REFIND_CONFIG_H

#include <windows.h>

// menuentry 结构
typedef struct _REFIND_MENU_ENTRY {
    WCHAR title[256];       // menuentry 标题
    WCHAR loader[512];      // loader 路径（EFI 文件，相对 ESP 根）
    WCHAR options[512];     // options 行（可选）
    BOOL isManaged;         // 是否由 Boot Manager Pro 管理
    struct _REFIND_MENU_ENTRY* next;
} REFIND_MENU_ENTRY;

// 读取 ESP 上的 refind.conf，返回 menuentry 链表（调用方负责 free）
REFIND_MENU_ENTRY* RefindConfigLoad(const WCHAR* espDrive);

// 追加一条 menuentry 到 refind.conf（不存在则创建）
BOOL RefindConfigAddMenuEntry(const WCHAR* espDrive, const WCHAR* title, const WCHAR* loader, const WCHAR* options);

// 删除指定 title 的 menuentry
BOOL RefindConfigRemoveMenuEntry(const WCHAR* espDrive, const WCHAR* title);

void RefindConfigFreeEntries(REFIND_MENU_ENTRY* head);

#endif // REFIND_CONFIG_H
