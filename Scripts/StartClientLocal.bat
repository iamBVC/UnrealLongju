@echo off
setlocal
cd /d "%~dp0.."

set "RUNTIME=F:\Engine2\Engine\Binaries\Win64\UnrealEditor.exe"
set "PROJECT=%CD%\UnrealLongju.uproject"
if not exist "%RUNTIME%" (
  echo Missing %RUNTIME%
  exit /b 1
)

start "UnrealLongju Client" "%RUNTIME%" "%PROJECT%" 127.0.0.1:11000 ^
  -game -log -windowed -ResX=1280 -ResY=720

endlocal
