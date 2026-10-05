@echo off
setlocal

rem ============================================================================
rem  UnrealLongju - build + cook + stage (Development) for CLIENT and SERVER.
rem
rem  Output:
rem    Saved\Staged\WindowsClient  - packaged Development client
rem    Saved\Staged\WindowsServer  - packaged Development dedicated server
rem
rem  The engine root defaults to F:\Engine2; override by setting UE_ROOT before
rem  running (e.g.  set UE_ROOT=D:\UE_5.7 && BuildDevelopment.bat).
rem ============================================================================

if "%UE_ROOT%"=="" set "UE_ROOT=F:\Engine2"

for %%I in ("%~dp0..") do set "PROJECT_DIR=%%~fI"
if "%PROJECT_DIR:~-1%"=="\" set "PROJECT_DIR=%PROJECT_DIR:~0,-1%"
set "UPROJECT=%PROJECT_DIR%\UnrealLongju.uproject"
set "STAGE_DIR=%PROJECT_DIR%\Saved\Staged"
set "RUNUAT=%UE_ROOT%\Engine\Build\BatchFiles\RunUAT.bat"

if not exist "%RUNUAT%" (
    echo [BuildDevelopment] RunUAT not found at "%RUNUAT%".
    echo [BuildDevelopment] Set UE_ROOT to your engine root and retry.
    exit /b 1
)
if not exist "%UPROJECT%" (
    echo [BuildDevelopment] Project not found at "%UPROJECT%".
    exit /b 1
)

echo [BuildDevelopment] Engine : %UE_ROOT%
echo [BuildDevelopment] Project: %UPROJECT%
echo [BuildDevelopment] Staging: %STAGE_DIR%
echo [BuildDevelopment] Building + cooking Development client and server...

rem Remove only legacy-branded staged files left by iterative deploys from before the rename.
del /q "%STAGE_DIR%\WindowsClient\Metin2Client.exe" >nul 2>&1
del /q "%STAGE_DIR%\WindowsServer\Metin2Server.exe" >nul 2>&1
if exist "%STAGE_DIR%\WindowsClient\Metin2" rmdir /s /q "%STAGE_DIR%\WindowsClient\Metin2"
if exist "%STAGE_DIR%\WindowsServer\Metin2" rmdir /s /q "%STAGE_DIR%\WindowsServer\Metin2"

call "%RUNUAT%" BuildCookRun ^
 -project="%UPROJECT%" ^
 -platform=Win64 ^
 -client -clientconfig=Development ^
 -server -serverconfig=Development ^
 -build -cook -stage -pak ^
 -iterativecooking -iterativedeploy ^
 -stagingdirectory="%STAGE_DIR%" ^
 -nop4 -utf8output

if errorlevel 1 (
    echo.
    echo [BuildDevelopment] FAILED - see the UAT log above ^(also under %UE_ROOT%\Engine\Programs\AutomationTool\Saved\Logs^).
    exit /b 1
)

echo.
echo [BuildDevelopment] Done.
echo [BuildDevelopment]   Client: %STAGE_DIR%\WindowsClient\UnrealLongjuClient.exe
echo [BuildDevelopment]   Server: %STAGE_DIR%\WindowsServer\UnrealLongjuServer.exe
endlocal
