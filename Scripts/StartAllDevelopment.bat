@echo off
setlocal

set "MT2_BUILD_KIND=Development"
set "MT2_STAGE_DIR=Saved\Staged"
set "MT2_SERVER_BINARY=UnrealLongjuServer.exe"
set "MT2_BUILD_SCRIPT=BuildDevelopment.bat"
set "MT2_REQUIRE_PATCHER_TOKEN=0"

call "%~dp0StartAllPackaged.bat" %*
set "EXIT_CODE=%ERRORLEVEL%"
endlocal & exit /b %EXIT_CODE%
