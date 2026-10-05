@echo off
setlocal
set MSBUILDDISABLENODEREUSE=1
set MSBuildDisableNodeReuse=1

"%~dp0..\Engine2\Engine\Binaries\DotNET\UnrealBuildTool\UnrealBuildTool.exe" -game -projectfiles -project="%~dp0UnrealLongju.uproject"
set GPF_EXITCODE=%ERRORLEVEL%
call "%~dp0..\Engine2\Engine\Build\BatchFiles\GetDotnetPath.bat" >nul 2>&1
dotnet build-server shutdown >nul 2>&1
if not "%GPF_NO_PAUSE%" == "1" pause
exit /B %GPF_EXITCODE%
