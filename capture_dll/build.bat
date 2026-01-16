@echo off
REM ============================================================================
REM MGS2 Animation Capture DLL - Build Script
REM ============================================================================
REM 
REM PREREQUISITES:
REM   1. Visual Studio 2022 (or 2019) with C++ Desktop Development workload
REM   2. Windows SDK 10.0
REM
REM USAGE:
REM   1. Open "x64 Native Tools Command Prompt for VS 2022" from Start Menu
REM   2. Navigate to this folder: cd /d d:\PROGRAM\VIBEPRO\MGS_EXTRACT\capture_dll
REM   3. Run: build.bat
REM   4. DLL will be created in the "build" subfolder
REM
REM ============================================================================

echo ============================================
echo   MGS2 Animation Capture DLL Builder
echo ============================================
echo.

REM Check if cl.exe is available
where cl.exe >nul 2>&1
if %ERRORLEVEL% neq 0 (
    echo ERROR: Visual Studio compiler not found!
    echo.
    echo Please run this script from "x64 Native Tools Command Prompt for VS 2022"
    echo You can find it in Start Menu under Visual Studio 2022 folder.
    echo.
    pause
    exit /b 1
)

REM Create build directory
if not exist "build" mkdir build

echo Compiling bone_capture.cpp...
cl.exe /c /EHsc /O2 /MD /DUNICODE /D_UNICODE ^
    /I"C:\Program Files (x86)\Windows Kits\10\Include\10.0.22621.0\um" ^
    /I"C:\Program Files (x86)\Windows Kits\10\Include\10.0.22621.0\shared" ^
    bone_capture.cpp /Fo"build\bone_capture.obj"

if %ERRORLEVEL% neq 0 (
    echo ERROR: Failed to compile bone_capture.cpp
    pause
    exit /b 1
)

echo Compiling d3d11_proxy.cpp...
cl.exe /c /EHsc /O2 /MD /DUNICODE /D_UNICODE ^
    /I"C:\Program Files (x86)\Windows Kits\10\Include\10.0.22621.0\um" ^
    /I"C:\Program Files (x86)\Windows Kits\10\Include\10.0.22621.0\shared" ^
    d3d11_proxy.cpp /Fo"build\d3d11_proxy.obj"

if %ERRORLEVEL% neq 0 (
    echo ERROR: Failed to compile d3d11_proxy.cpp
    pause
    exit /b 1
)

echo Linking d3d11.dll...
link.exe /DLL /OUT:"build\d3d11.dll" /DEF:d3d11.def ^
    build\bone_capture.obj build\d3d11_proxy.obj ^
    d3d11.lib dxgi.lib user32.lib gdi32.lib

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
echo Output: build\d3d11.dll
echo.
echo NEXT STEPS:
echo   1. Copy build\d3d11.dll to "D:\Metal Gear Solid 2\"
echo   2. Run the game normally
echo   3. Press F9 to start/stop capture
echo   4. Animation saved to: D:\Metal Gear Solid 2\animation_capture.json
echo.
pause
