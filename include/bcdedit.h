#ifndef BOOTMGR_BCD_EDIT_H
#define BOOTMGR_BCD_EDIT_H

#include <windows.h>

BOOL BcdEditExecute(const WCHAR* command, WCHAR* output, DWORD outputSize);
BOOL BcdEditCreate(const WCHAR* description, const WCHAR* application, WCHAR* guid, DWORD guidSize);
BOOL BcdEditSet(const WCHAR* guid, const WCHAR* property, const WCHAR* value);
BOOL BcdEditDelete(const WCHAR* guid);
BOOL BcdEditEnum(const WCHAR* type, WCHAR* output, DWORD outputSize);

#endif
