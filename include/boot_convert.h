#pragma once
#include <windows.h>

BOOL BootConvertIsUEFI(void);
BOOL BootConvertIsGPT(int diskNumber);
BOOL BootConvertMBRtoUEFI(int diskNumber, WCHAR* error, DWORD errorSize);
BOOL BootConvertUEFItoMBR(int diskNumber, WCHAR* error, DWORD errorSize);
