
with open(r'C:\Users\Administrator\.openclaw\workspace\bootmanager\src\core\wimboot.c', 'rb') as f:
    c = f.read().decode('utf-8')

# 保留 header + ExecuteCommand，替换 WimAddBootEntry 到 WimSelectFileDialog 之间的内容
header_end = c.find('BOOL WimAddBootEntry(')
select_start = c.find('// \u6587\u4ef6\u5bf9\u8bdd\u6846\u51fd\u6570')
if select_start == -1:
    select_start = c.find('BOOL WimSelectFileDialog(')

new_funcs = r"""// Write WIM boot entry as rEFInd menuentry
// WIM must be accessible from UEFI (on ESP or same disk)
BOOL WimAddBootEntry(const WCHAR* name, const WCHAR* wimPath, const WCHAR* imageIndex) {
    if (!name || !wimPath) return FALSE;

    // Convert Windows path to EFI-style backslash path (strip drive letter)
    WCHAR efiPath[MAX_PATH] = {0};
    const WCHAR* src = wimPath;
    if (src[1] == L':') src += 2;  // skip "C:"
    wcsncpy(efiPath, src, MAX_PATH - 1);

    // Mount ESP to find refind.conf
    WCHAR espDrive[4] = {0};
    // Try to find already-mounted ESP or use mountvol
    for (WCHAR d = L'B'; d <= L'Z'; d++) {
        WCHAR probe[MAX_PATH];
        swprintf(probe, MAX_PATH, L"%c:\\EFI\\refind\\refind.conf", d);
        if (GetFileAttributesW(probe) != INVALID_FILE_ATTRIBUTES) {
            swprintf(espDrive, 4, L"%c:", d);
            break;
        }
    }
    if (espDrive[0] == L'\0') return FALSE;

    WCHAR options[128] = {0};
    if (imageIndex && wcslen(imageIndex) > 0)
        swprintf(options, 128, L"index=%s", imageIndex);

    return RefindConfigAddMenuEntry(espDrive, name,
        L"\\EFI\\refind\\tools\\wimboot.efi", options[0] ? options : NULL);
}

// Write VHD boot entry as rEFInd menuentry (chainload via bootmgfw)
BOOL VhdAddBootEntry(const WCHAR* name, const WCHAR* vhdPath) {
    if (!name || !vhdPath) return FALSE;

    WCHAR efiPath[MAX_PATH] = {0};
    const WCHAR* src = vhdPath;
    if (src[1] == L':') src += 2;
    wcsncpy(efiPath, src, MAX_PATH - 1);

    WCHAR espDrive[4] = {0};
    for (WCHAR d = L'B'; d <= L'Z'; d++) {
        WCHAR probe[MAX_PATH];
        swprintf(probe, MAX_PATH, L"%c:\\EFI\\refind\\refind.conf", d);
        if (GetFileAttributesW(probe) != INVALID_FILE_ATTRIBUTES) {
            swprintf(espDrive, 4, L"%c:", d);
            break;
        }
    }
    if (espDrive[0] == L'\0') return FALSE;

    WCHAR options[MAX_PATH + 16];
    swprintf(options, MAX_PATH + 16, L"vhd=%s", efiPath);

    return RefindConfigAddMenuEntry(espDrive, name,
        L"\\EFI\\Microsoft\\Boot\\bootmgfw.efi", options);
}

"""

new_content = c[:header_end] + new_funcs + c[select_start:]
with open(r'C:\Users\Administrator\.openclaw\workspace\bootmanager\src\core\wimboot.c', 'wb') as f:
    f.write(new_content.encode('utf-8'))
print('Done. Length:', len(new_content))
