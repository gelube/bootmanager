@echo off
chcp 65001 >nul
echo ==========================================
echo Boot Manager Pro v3 - Build Script
echo ==========================================
echo.

:: Create dist directory if not exists
if not exist dist mkdir dist

:: Check for gcc
where gcc >nul 2>&1
if %errorlevel% neq 0 (
    echo [ERROR] GCC not found in PATH.
    echo Please install MinGW-w64 and add it to PATH.
    pause
    exit /b 1
)

echo [INFO] Compiling Boot Manager Pro v3...
echo.

:: Compile - explicit source files (no wildcard to avoid missing file errors)
gcc -municode ^
    src\ui\main.c src\ui\dialog.c src\ui\home.c src\ui\cards.c ^
    src\core\ops.c ^
    src\utils\bm_result.c src\utils\strconv.c ^
    src\ui\dialogs\add_efi_dialog.c ^
    src\core\boot.c src\core\boot_mode.c src\core\esp.c ^
    src\core\limine.c src\core\mbr_manager.c ^
    src\core\refind.c src\core\uefi.c src\core\uefi_nvram.c ^
    src\core\backup.c ^
    src\hal\bcdedit.c ^
    src\utils\error.c src\utils\logger.c ^
    -I include -I src ^
    -o dist\BootManagerPro.exe ^
    -m64 -mwindows -lcomctl32 -lcomdlg32 -lshell32 -ladvapi32 -lole32 -luuid -O2 -Wall

if %errorlevel% neq 0 (
    echo.
    echo [ERROR] Compilation failed!
    pause
    exit /b 1
)

echo.
echo [SUCCESS] Build completed successfully!
echo Output: dist\BootManagerPro.exe
echo.

:: Check for bundled rEFInd source
echo [INFO] Checking bundled rEFInd source...
if exist ".\refind\refind_x64.efi" (
    echo [OK] rEFInd source found at .\refind\
) else (
    echo [WARN] rEFInd source not found at .\refind\
    echo        Please copy rEFInd files into the project refind directory.
)

echo.
pause
