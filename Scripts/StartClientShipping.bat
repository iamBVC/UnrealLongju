@echo off
setlocal
cd /d "%~dp0.."

set "GATEWAY_ENDPOINT=127.0.0.1:11000"
if not "%~1"=="" set "GATEWAY_ENDPOINT=%~1"
set "MT2UE_PATCHER_ARGS_OVERRIDE=%GATEWAY_ENDPOINT%"
start "UnrealLongju Patcher" ".\Saved\StagedShipping\WindowsClient\UnrealLongjuPatcher.exe"
endlocal
