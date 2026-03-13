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
gcc -municode src\ui\main.c src\ui\dialog.c src\core\*.c -o dist\BootManagerPro.exe ^
    -m64 -mwindows -lcomctl32 -lshell32 -ladvapi32 -lole32 -luuid -O2 -Wall

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

:: Check for rEFInd source
echo [INFO] Checking rEFInd source...
if exist "Z:\refind0.14.2\refind\refind_x64.efi" (
    echo [OK] rEFInd source found at Z:\refind0.14.2\refind\
) else (
    echo [WARN] rEFInd source not found at Z:\refind0.14.2\refind\
    echo        Please ensure rEFInd files are available before installing.
)

echo.
pause
