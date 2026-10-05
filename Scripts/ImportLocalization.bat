@echo off
setlocal
if "%UE_ROOT%"=="" set "UE_ROOT=F:\Engine2"
"%UE_ROOT%\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "%~dp0..\UnrealLongju.uproject" -run=GatherText -config="%~dp0..\Config\Localization\Game_Import.ini" -unattended -nop4
exit /b %errorlevel%
