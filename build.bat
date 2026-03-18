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

:: Compile
gcc -municode src\ui\main.c src\ui\dialog.c src\ui\pages\*.c src\ui\dialogs\*.c ^
    src\core\*.c src\hal\*.c src\utils\*.c ^
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
