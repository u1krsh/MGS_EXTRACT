@echo off
REM ============================================================================
REM MGS2 Memory Scanner DLL - Build Script
REM ============================================================================
REM 
REM Run from "x64 Native Tools Command Prompt for VS 2022"
REM
REM ============================================================================

echo ============================================
echo   MGS2 Memory Scanner DLL Builder
echo ============================================
echo.

where cl.exe >nul 2>&1
if %ERRORLEVEL% neq 0 (
    echo ERROR: Visual Studio compiler not found!
    echo Please run from "x64 Native Tools Command Prompt for VS 2022"
    pause
    exit /b 1
)

if not exist "build" mkdir build

echo Compiling memory_scanner.cpp...
cl.exe /c /EHsc /O2 /MD /DUNICODE /D_UNICODE ^
    memory_scanner.cpp /Fo"build\memory_scanner.obj"

if %ERRORLEVEL% neq 0 (
    echo ERROR: Failed to compile
    pause
    exit /b 1
)

echo Linking dinput8.dll...
link.exe /DLL /OUT:"build\dinput8.dll" ^
    build\memory_scanner.obj ^
    user32.lib

if %ERRORLEVEL% neq 0 (
    echo ERROR: Failed to link DLL
    pause
    exit /b 1
)

echo.
echo ============================================
echo   BUILD SUCCESSFUL!
echo ============================================
echo.
echo Output: build\dinput8.dll
echo.
echo USAGE:
echo   1. Copy build\dinput8.dll to "D:\Metal Gear Solid 2\"
echo   2. Run the game
echo   3. Press F8 to scan for bone arrays
echo   4. Press F9 to start/stop capture
echo   5. Press F10 to save
echo.
pause
