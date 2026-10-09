@echo off
setlocal EnableExtensions DisableDelayedExpansion

rem Launch only the staged Development client. Servers must already be running.
rem Optional environment overrides: MT2_STAGE_DIR (project-relative), MT2_CLIENT_ADDRESS.
rem Additional command-line arguments are forwarded to the client unchanged.
if /I "%~1"=="--help" goto :help

for %%I in ("%~dp0..") do set "MT2_PROJECT_DIR=%%~fI"
if not defined MT2_STAGE_DIR set "MT2_STAGE_DIR=Saved\Staged"
if not defined MT2_CLIENT_ADDRESS set "MT2_CLIENT_ADDRESS=127.0.0.1:11000"
set "MT2_CLIENT_DIR=%MT2_PROJECT_DIR%\%MT2_STAGE_DIR%\WindowsClient"
set "MT2_CLIENT_EXE=%MT2_CLIENT_DIR%\UnrealLongjuClient.exe"

if not exist "%MT2_CLIENT_EXE%" (
    echo [StartClientDevelopment] Missing client: "%MT2_CLIENT_EXE%"
    echo [StartClientDevelopment] Build it first with Scripts\BuildDevelopment.bat.
    exit /b 1
)

start "UnrealLongju Development Client" /D "%MT2_CLIENT_DIR%" "%MT2_CLIENT_EXE%" %MT2_CLIENT_ADDRESS% ^
    -windowed -ResX=1280 -ResY=720 -voice_port_offset=100 %*
if errorlevel 1 (
    echo [StartClientDevelopment] Failed to start the client.
    exit /b 1
)
exit /b 0

:help
echo Usage: StartClientDevelopment.bat [additional Unreal command-line arguments]
echo Launches Saved\Staged\WindowsClient\UnrealLongjuClient.exe only.
echo Default gateway: 127.0.0.1:11000. Servers must already be running.
echo Set MT2_CLIENT_ADDRESS to change the gateway address.
echo Set MT2_STAGE_DIR to change the project-relative staging folder.
exit /b 0
