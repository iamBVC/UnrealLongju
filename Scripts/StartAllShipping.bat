@echo off
setlocal

set "MT2_BUILD_KIND=Shipping"
set "MT2_STAGE_DIR=Saved\StagedShipping"
set "MT2_SERVER_BINARY=UnrealLongjuServer-Win64-Shipping.exe"
set "MT2_BUILD_SCRIPT=BuildShipping.bat"
set "MT2_REQUIRE_PATCHER_TOKEN=1"

call "%~dp0StartAllPackaged.bat" %*
set "EXIT_CODE=%ERRORLEVEL%"
endlocal & exit /b %EXIT_CODE%
