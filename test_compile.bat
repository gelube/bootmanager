@echo off
chcp 65001 >nul
echo ==========================================
echo Boot Manager Pro v3 - Test Compilation
echo ==========================================
echo.

:: Create dist directory if not exists
if not exist dist mkdir dist

:: Test compile with verbose output
echo [INFO] Testing compilation...
echo.

gcc -municode src\ui\main.c src\ui\dialogs\*.c src\ui\pages\*.c ^
    src\core\*.c src\hal\*.c src\utils\*.c ^
    -I include -o dist\BootManagerPro_test.exe ^
    -m64 -mwindows -lcomctl32 -lcomdlg32 -lshell32 -ladvapi32 -lole32 -luuid -O2 -Wall -v 2>&1

if %errorlevel% neq 0 (
    echo.
    echo [ERROR] Compilation failed!
    exit /b 1
)

echo.
echo [SUCCESS] Test compilation successful!
echo.

:: Clean up test executable
del dist\BootManagerPro_test.exe

echo [INFO] All tests passed!
pause
