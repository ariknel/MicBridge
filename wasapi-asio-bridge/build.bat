@echo off
setlocal enabledelayedexpansion
title WASAPI-ASIO Bridge - Build

cd /d "%~dp0"

echo ============================================================
echo  WASAPI-ASIO Bridge - Build Script
echo ============================================================
echo.

if not exist "%~dp0CMakeLists.txt" (
    echo [ERROR] CMakeLists.txt not found in: %~dp0
    pause & exit /b 1
)
echo [OK] Found CMakeLists.txt

:: Find vswhere
set "VSWHERE="
for %%p in (
    "%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
    "%ProgramFiles%\Microsoft Visual Studio\Installer\vswhere.exe"
    "%ProgramW6432%\Microsoft Visual Studio\Installer\vswhere.exe"
) do (
    if exist %%p if "!VSWHERE!"=="" set "VSWHERE=%%~p"
)

if "!VSWHERE!"=="" (
    echo [ERROR] vswhere.exe not found.
    echo Install Build Tools: https://visualstudio.microsoft.com/visual-cpp-build-tools/
    pause & exit /b 1
)
echo [OK] vswhere found.

:: Find VS install path
set "VS_PATH="
for /f "usebackq delims=" %%i in (`"!VSWHERE!" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2^>nul`) do (
    if "!VS_PATH!"=="" set "VS_PATH=%%i"
)

if "!VS_PATH!"=="" (
    echo [ERROR] No MSVC compiler found.
    echo Open Visual Studio Installer - ensure "Desktop development with C++" is checked.
    pause & exit /b 1
)
echo [OK] MSVC: !VS_PATH!

:: Call vcvars64.bat
set "VCVARS=!VS_PATH!\VC\Auxiliary\Build\vcvars64.bat"
if not exist "!VCVARS!" (
    echo [ERROR] vcvars64.bat not found: !VCVARS!
    pause & exit /b 1
)

echo [..] Activating MSVC x64 environment...
call "!VCVARS!"
if %errorlevel% neq 0 (
    echo [ERROR] vcvars64.bat failed.
    pause & exit /b 1
)
echo [OK] MSVC x64 ready.

:: Add VS-bundled CMake and Ninja to PATH
set "VS_CMAKE=!VS_PATH!\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"
set "VS_NINJA=!VS_PATH!\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja"
if exist "!VS_CMAKE!\cmake.exe" set "PATH=!VS_CMAKE!;!PATH!"
if exist "!VS_NINJA!\ninja.exe" set "PATH=!VS_NINJA!;!PATH!"

:: Verify cmake
echo.
where cmake >nul 2>&1
if %errorlevel% neq 0 (
    echo [ERROR] CMake not found. Install from https://cmake.org
    pause & exit /b 1
)
cmake --version | findstr /i "cmake version"

:: Check for ninja
set "USE_NINJA=0"
where ninja >nul 2>&1
if not %errorlevel% neq 0 (
    echo [OK] Ninja found.
    set "USE_NINJA=1"
) else (
    echo [WARN] Ninja not found - using NMake.
)
echo.

:: Paths
set "SRC=%~dp0"
if "!SRC:~-1!"=="\" set "SRC=!SRC:~0,-1!"
set "BLD=!SRC!\build"
set "OUT=!SRC!\output"

echo [INFO] Source : !SRC!
echo [INFO] Build  : !BLD!
echo.

:: Wipe stale cache
if exist "!BLD!" (
    echo [INFO] Clearing old build cache...
    rmdir /s /q "!BLD!"
)
mkdir "!BLD!"

:: Configure
echo [1/3] Configuring CMake...
if "!USE_NINJA!"=="1" (
    cmake "!SRC!" -B "!BLD!" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl
) else (
    cmake "!SRC!" -B "!BLD!" -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl
)
if %errorlevel% neq 0 (
    echo [ERROR] CMake configure failed.
    pause & exit /b 1
)
echo [OK] Configured.

:: Build
echo.
echo [2/3] Building...
cmake --build "!BLD!" --config Release --parallel
if %errorlevel% neq 0 (
    echo [ERROR] Build failed.
    pause & exit /b 1
)

:: Copy outputs
echo.
echo [3/3] Copying outputs...
if not exist "!OUT!" mkdir "!OUT!"

for %%F in (
    "!BLD!\bin\Release\bridge.exe"
    "!BLD!\Release\bridge.exe"
    "!BLD!\bridge.exe"
) do if exist %%F (
    copy /Y %%F "!OUT!\bridge.exe" >nul
    echo [OK] bridge.exe
)
for %%F in (
    "!BLD!\bin\Release\bridge_gui.exe"
    "!BLD!\Release\bridge_gui.exe"
    "!BLD!\bridge_gui.exe"
) do if exist %%F (
    copy /Y %%F "!OUT!\bridge_gui.exe" >nul
    echo [OK] bridge_gui.exe
)
copy /Y "!SRC!\README.md" "!OUT!\README.md" >nul 2>&1

echo.
echo ============================================================
echo  BUILD COMPLETE - Executables in: !OUT!
echo.
echo  CLI:  output\bridge.exe --list
echo  GUI:  output\bridge_gui.exe
echo.
echo  To install VB-Cable + copy app to your PC:
echo    Right-click install.bat then Run as Administrator
echo ============================================================
echo.
pause
