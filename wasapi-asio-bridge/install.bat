@echo off
setlocal EnableDelayedExpansion
title WASAPI-ASIO Bridge - Installer
cd /d "%~dp0"

echo ============================================================
echo  WASAPI-ASIO Bridge - Full Installer
echo  Installs: VB-Cable virtual audio driver + Bridge app
echo ============================================================
echo.

:: Must run as Administrator (VB-Cable driver needs it)
net session >nul 2>&1
if %errorlevel% neq 0 (
    echo [!!] This installer requires Administrator privileges.
    echo      Right-click install.bat and choose "Run as administrator"
    echo.
    pause & exit /b 1
)
echo [OK] Running as Administrator.

set "DIR=%~dp0"
if "!DIR:~-1!"=="\" set "DIR=!DIR:~0,-1!"

set "TOOLS=!DIR!\tools"
set "VBZIP=!TOOLS!\vbcable.zip"
set "VBDIR=!TOOLS!\vbcable"
set "INSTDIR=%LOCALAPPDATA%\WasapiAsioBridge"

:: ============================================================
:: STEP 1 - VB-Cable
:: ============================================================
echo.
echo [1/3] VB-Cable Virtual Audio Driver
echo --------------------------------------------------------

:: Check if already installed by looking for the audio device
powershell -NoProfile -ExecutionPolicy Bypass -Command "(Get-WmiObject Win32_SoundDevice | Where-Object {$_.Name -like '*CABLE*'}).Count -gt 0" 2>nul | findstr "True" >nul 2>&1
if not errorlevel 1 (
    echo [OK] VB-Cable already installed - skipping.
    goto INSTALL_BRIDGE
)

echo [..] VB-Cable not found. Downloading (~5 MB) from vb-audio.com...
if not exist "!TOOLS!" mkdir "!TOOLS!"

powershell -NoProfile -ExecutionPolicy Bypass -Command "[Net.ServicePointManager]::SecurityProtocol=[Net.SecurityProtocolType]::Tls12;$ProgressPreference='SilentlyContinue';Invoke-WebRequest -Uri 'https://download.vb-audio.com/Download_CABLE/VBCABLE_Driver_Pack43.zip' -OutFile '!VBZIP!' -UseBasicParsing"

if not exist "!VBZIP!" (
    echo [ERROR] Download failed. Install manually: https://vb-audio.com/Cable/
    goto INSTALL_BRIDGE
)
echo [OK] Downloaded.

echo [..] Extracting...
if exist "!VBDIR!" rd /s /q "!VBDIR!"
powershell -NoProfile -ExecutionPolicy Bypass -Command "Expand-Archive -Path '!VBZIP!' -DestinationPath '!VBDIR!' -Force"

if not exist "!VBDIR!" (
    echo [ERROR] Extraction failed.
    goto INSTALL_BRIDGE
)
echo [OK] Extracted to: !VBDIR!

:: Find the setup exe using PowerShell (handles paths with spaces correctly)
echo [..] Locating setup executable...
set "SETUP="
for /f "usebackq delims=" %%F in (`powershell -NoProfile -ExecutionPolicy Bypass -Command "(Get-ChildItem -Path '!VBDIR!' -Filter 'VBCABLE_Setup_x64.exe' -Recurse | Select-Object -First 1).FullName"`) do set "SETUP=%%F"

if not defined SETUP (
    for /f "usebackq delims=" %%F in (`powershell -NoProfile -ExecutionPolicy Bypass -Command "(Get-ChildItem -Path '!VBDIR!' -Filter '*.exe' -Recurse | Select-Object -First 1).FullName"`) do set "SETUP=%%F"
)

if not defined SETUP (
    echo [ERROR] No .exe found in: !VBDIR!
    echo         Install manually: https://vb-audio.com/Cable/
    goto INSTALL_BRIDGE
)
echo [OK] Found installer: !SETUP!

echo.
echo [..] Installing VB-Cable driver...
echo      Windows may show a "Would you like to install this device software?" prompt.
echo      Click YES / INSTALL when it appears.
echo.

:: start /wait runs the exe and waits for it to fully exit
start /wait "" "!SETUP!"

echo.
echo [..] Waiting for driver to register...
timeout /t 8 /nobreak >nul

:: Verify
set "VBOK=0"
reg query "HKLM\SYSTEM\CurrentControlSet\Services\VBAudioVACMME" >nul 2>&1
if not errorlevel 1 set "VBOK=1"
powershell -NoProfile -ExecutionPolicy Bypass -Command "(Get-WmiObject Win32_SoundDevice | Where-Object {$_.Name -like '*CABLE*'}).Count -gt 0" 2>nul | findstr "True" >nul 2>&1
if not errorlevel 1 set "VBOK=1"

if "!VBOK!"=="1" (
    echo [OK] VB-Cable installed successfully.
) else (
    echo [WARN] VB-Cable may need a reboot to fully activate.
    echo        Reboot when prompted at the end, then run install.bat again.
)

:INSTALL_BRIDGE
:: ============================================================
:: STEP 2 - Copy bridge executables
:: ============================================================
echo.
echo [2/3] Installing Bridge Application
echo --------------------------------------------------------

if not exist "!INSTDIR!" mkdir "!INSTDIR!"

set "SRC=!DIR!\output"
set "COPIED=0"

for %%F in ("!SRC!\bridge.exe" "!SRC!\bridge_gui.exe") do (
    if exist %%F (
        copy /Y %%F "!INSTDIR!\" >nul
        echo [OK] Installed %%~nxF
        set "COPIED=1"
    )
)

if "!COPIED!"=="0" (
    echo [WARN] No executables found in output\
    echo        Run build.bat first, then re-run install.bat
)

:: Desktop shortcut
if exist "!INSTDIR!\bridge_gui.exe" (
    echo [..] Creating Desktop shortcut...
    powershell -NoProfile -ExecutionPolicy Bypass -Command "$d=[Environment]::GetFolderPath('Desktop');$s=(New-Object -COM WScript.Shell).CreateShortcut($d+'\WASAPI-ASIO Bridge.lnk');$s.TargetPath='!INSTDIR!\bridge_gui.exe';$s.WorkingDirectory='!INSTDIR!';$s.Description='WASAPI to ASIO Audio Bridge';$s.Save()"
    echo [OK] Desktop shortcut created.
)

:: ============================================================
:: STEP 3 - FlexASIO config
:: ============================================================
echo.
echo [3/3] FlexASIO Configuration
echo --------------------------------------------------------

set "TOML=%USERPROFILE%\FlexASIO.toml"
if not exist "!TOML!" (
    echo [..] Writing FlexASIO.toml...
    (
        echo backend = "WASAPI"
        echo.
        echo [input]
        echo device = "CABLE Output (VB-Audio Virtual Cable)"
        echo.
        echo [output]
        echo device = "Default"
        echo.
        echo latencyFrames = 480
    ) > "!TOML!"
    echo [OK] Written to: !TOML!
    echo      Note: Install FlexASIO if not already done:
    echo      https://github.com/dechamps/FlexASIO/releases
) else (
    echo [OK] FlexASIO.toml already exists - not overwritten.
)

:: ============================================================
echo.
echo ============================================================
echo  INSTALLATION COMPLETE
echo.
echo  Next steps:
echo  1. Reboot if prompted (VB-Cable driver activation)
echo  2. Install FlexASIO: https://github.com/dechamps/FlexASIO/releases
echo  3. Open "WASAPI-ASIO Bridge" from your Desktop
echo  4. Input: your USB mic   Output: CABLE Input
echo  5. Click START BRIDGE
echo  6. Bitwig: Settings > Audio > Driver = FlexASIO
echo ============================================================
echo.
pause
