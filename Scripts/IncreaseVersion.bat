@echo off
setlocal

rem ============================================================================
rem  Manually increments the shared UnrealLongju client/server ProjectVersion.
rem  Run this before building releases that must reject older clients or servers.
rem ============================================================================

for %%I in ("%~dp0..") do set "PROJECT_DIR=%%~fI"
if "%PROJECT_DIR:~-1%"=="\" set "PROJECT_DIR=%PROJECT_DIR:~0,-1%"
set "VERSION_SCRIPT=%~dp0IncrementShippingVersion.ps1"

if not exist "%VERSION_SCRIPT%" (
    echo [IncreaseVersion] Version script not found at "%VERSION_SCRIPT%".
    exit /b 1
)
if not exist "%PROJECT_DIR%\Config\DefaultGame.ini" (
    echo [IncreaseVersion] Project settings not found under "%PROJECT_DIR%".
    exit /b 1
)

powershell -NoProfile -ExecutionPolicy Bypass -File "%VERSION_SCRIPT%" ^
 -ProjectRoot "%PROJECT_DIR%"
if errorlevel 1 (
    echo [IncreaseVersion] FAILED - ProjectVersion was not changed.
    exit /b 1
)

set "PROJECT_VERSION="
for /f "tokens=2 delims==" %%V in ('findstr /b "ProjectVersion=" "%PROJECT_DIR%\Config\DefaultGame.ini"') do set "PROJECT_VERSION=%%V"
if not defined PROJECT_VERSION (
    echo [IncreaseVersion] FAILED - ProjectVersion could not be read after the update.
    exit /b 1
)

echo [IncreaseVersion] ProjectVersion is now %PROJECT_VERSION%.
endlocal
