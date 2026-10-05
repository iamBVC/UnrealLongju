@echo off
setlocal
rem ============================================================================
rem  Builds the patcher as a self-contained single-file exe (no .NET runtime
rem  needed on the player's machine). Output:
rem    Patcher\bin\Release\net8.0-windows\win-x64\publish\UnrealLongjuPatcher.exe
rem  Requires the .NET 8 SDK (bundled with recent Visual Studio 2022).
rem ============================================================================
cd /d "%~dp0"

where dotnet >nul 2>&1
if errorlevel 1 (
    echo [Patcher] .NET SDK not found. Install the .NET 8 SDK, then retry.
    exit /b 1
)

dotnet publish UnrealLongjuPatcher.csproj -c Release
if errorlevel 1 (
    echo [Patcher] Build FAILED.
    exit /b 1
)

echo.
echo [Patcher] Done. Ship this next to the client files:
echo   %~dp0bin\Release\net8.0-windows\win-x64\publish\UnrealLongjuPatcher.exe
endlocal
