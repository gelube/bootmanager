#include "../../include/bcdedit.h"

#include <stdio.h>
#include <string.h>
#include <wchar.h>

static BOOL ExtractGuid(const CHAR* text, WCHAR* guid, DWORD guidSize) {
    const CHAR* start;
    const CHAR* end;
    CHAR guidA[64];

    if (!text || !guid || guidSize == 0) {
        return FALSE;
    }

    start = strchr(text, '{');
    if (!start) {
        return FALSE;
    }

    end = strchr(start, '}');
    if (!end || end <= start) {
        return FALSE;
    }

    if ((size_t)(end - start + 2) > sizeof(guidA)) {
        return FALSE;
    }

    memcpy(guidA, start, (size_t)(end - start + 1));
    guidA[end - start + 1] = '\0';

    return MultiByteToWideChar(CP_ACP, 0, guidA, -1, guid, (int)guidSize) > 0;
}

BOOL BcdEditExecute(const WCHAR* command, WCHAR* output, DWORD outputSize) {
    SECURITY_ATTRIBUTES sa = {0};
    HANDLE hReadPipe = NULL;
    HANDLE hWritePipe = NULL;
    STARTUPINFOW si = {0};
    PROCESS_INFORMATION pi = {0};
    WCHAR cmdLine[2048];
    CHAR outA[8192] = {0};
    DWORD total = 0;

    if (!command) {
        return FALSE;
    }

    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    if (!CreatePipe(&hReadPipe, &hWritePipe, &sa, 0)) {
        return FALSE;
    }

    SetHandleInformation(hReadPipe, HANDLE_FLAG_INHERIT, 0);

    swprintf(cmdLine, 2048, L"cmd.exe /c bcdedit %s", command);

    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.hStdOutput = hWritePipe;
    si.hStdError = hWritePipe;
    si.wShowWindow = SW_HIDE;

    if (!CreateProcessW(NULL, cmdLine, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        CloseHandle(hReadPipe);
        CloseHandle(hWritePipe);
        return FALSE;
    }

    CloseHandle(hWritePipe);

    while (1) {
        CHAR chunk[512];
        DWORD bytesRead = 0;
        if (!ReadFile(hReadPipe, chunk, sizeof(chunk), &bytesRead, NULL) || bytesRead == 0) {
            break;
        }

        if (total + bytesRead < sizeof(outA) - 1) {
            memcpy(outA + total, chunk, bytesRead);
            total += bytesRead;
        }
    }

    outA[total] = '\0';

    WaitForSingleObject(pi.hProcess, 30000);

    DWORD exitCode = 1;
    GetExitCodeProcess(pi.hProcess, &exitCode);

    CloseHandle(hReadPipe);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    if (output && outputSize > 0) {
        if (MultiByteToWideChar(CP_ACP, 0, outA, -1, output, (int)outputSize) == 0) {
            output[0] = L'\0';
        }
    }

    return exitCode == 0;
}

BOOL BcdEditCreate(const WCHAR* description, const WCHAR* application, WCHAR* guid, DWORD guidSize) {
    WCHAR command[1024];
    WCHAR output[4096];
    CHAR outputA[4096];

    if (!description || !application || !guid || guidSize == 0) {
        return FALSE;
    }

    swprintf(command, 1024, L"/create /d \"%s\" /application %s", description, application);
    if (!BcdEditExecute(command, output, 4096)) {
        return FALSE;
    }

    if (WideCharToMultiByte(CP_ACP, 0, output, -1, outputA, 4096, NULL, NULL) == 0) {
        return FALSE;
    }

    return ExtractGuid(outputA, guid, guidSize);
}

BOOL BcdEditSet(const WCHAR* guid, const WCHAR* property, const WCHAR* value) {
    WCHAR command[1024];

    if (!guid || !property || !value) {
        return FALSE;
    }

    swprintf(command, 1024, L"/set %s %s %s", guid, property, value);
    return BcdEditExecute(command, NULL, 0);
}

BOOL BcdEditDelete(const WCHAR* guid) {
    WCHAR command[512];

    if (!guid) {
        return FALSE;
    }

    swprintf(command, 512, L"/delete %s", guid);
    return BcdEditExecute(command, NULL, 0);
}

BOOL BcdEditEnum(const WCHAR* type, WCHAR* output, DWORD outputSize) {
    WCHAR command[128];

    if (!type) {
        return FALSE;
    }

    swprintf(command, 128, L"/enum %s", type);
    return BcdEditExecute(command, output, outputSize);
}
