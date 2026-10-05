@echo off
setlocal

rem ============================================================================
rem  UnrealLongju - SHIPPING build + cook + stage for CLIENT and SERVER, with nothing
rem  useful to reverse engineers in the distributed output:
rem    - Shipping config (no console, minimal logging, asserts compiled out)
rem    - -nodebuginfo: no .pdb / debug files staged
rem    - pak encryption + signing applied from Config\DefaultCrypto.ini
rem    - belt-and-braces sweep deletes any *.pdb / *.map that slipped into staging
rem
rem  The PDBs themselves are NOT deleted - they are archived privately under
rem  Saved\Symbols\<timestamp>\ so player crash dumps from this exact build can
rem  be symbolicated later. Never distribute that folder.
rem
rem  Output:
rem    Saved\StagedShipping\WindowsClient  - shippable client
rem    Saved\StagedShipping\WindowsServer  - shippable dedicated server
rem    Saved\Symbols\<timestamp>\          - PRIVATE symbol archive (keep, don't ship)
rem
rem  Engine root defaults to F:\Engine2; override with  set UE_ROOT=...
rem  NOTE: the first run compiles the engine modules in Shipping config - expect
rem  it to take much longer than a Development build.
rem ============================================================================

if "%UE_ROOT%"=="" set "UE_ROOT=F:\Engine2"

for %%I in ("%~dp0..") do set "PROJECT_DIR=%%~fI"
if "%PROJECT_DIR:~-1%"=="\" set "PROJECT_DIR=%PROJECT_DIR:~0,-1%"
set "UPROJECT=%PROJECT_DIR%\UnrealLongju.uproject"
set "STAGE_DIR=%PROJECT_DIR%\Saved\StagedShipping"
set "SYMBOL_ROOT=%PROJECT_DIR%\Saved\Symbols"
set "RUNUAT=%UE_ROOT%\Engine\Build\BatchFiles\RunUAT.bat"

if not exist "%RUNUAT%" (
    echo [BuildShipping] RunUAT not found at "%RUNUAT%". Set UE_ROOT and retry.
    exit /b 1
)
if not exist "%UPROJECT%" (
    echo [BuildShipping] Project not found at "%UPROJECT%".
    exit /b 1
)
if not exist "%PROJECT_DIR%\Config\DefaultCrypto.ini" (
    echo [BuildShipping] WARNING: Config\DefaultCrypto.ini is missing - paks would be UNENCRYPTED.
    echo [BuildShipping] Aborting; create the crypto config first.
    exit /b 1
)

rem --- One shared version is packaged into both client and server. Increase it manually with
rem --- Scripts\IncreaseVersion.bat when this build should become network-incompatible.
set "SHIPPING_VERSION="
for /f "tokens=2 delims==" %%V in ('findstr /b "ProjectVersion=" "%PROJECT_DIR%\Config\DefaultGame.ini"') do set "SHIPPING_VERSION=%%V"
if not defined SHIPPING_VERSION (
    echo [BuildShipping] ProjectVersion was not found in Config\DefaultGame.ini.
    exit /b 1
)

echo [BuildShipping] Engine : %UE_ROOT%
echo [BuildShipping] Project: %UPROJECT%
echo [BuildShipping] Staging: %STAGE_DIR%
echo [BuildShipping] Version: %SHIPPING_VERSION%
echo [BuildShipping] Building + cooking SHIPPING client and server...

rem Remove only legacy-branded staged files left by iterative deploys from before the rename.
del /q "%STAGE_DIR%\WindowsClient\Metin2Client.exe" >nul 2>&1
del /q "%STAGE_DIR%\WindowsServer\Metin2Server.exe" >nul 2>&1
del /q "%STAGE_DIR%\WindowsClient\Metin2Patcher.exe" >nul 2>&1
if exist "%STAGE_DIR%\WindowsClient\Metin2" rmdir /s /q "%STAGE_DIR%\WindowsClient\Metin2"
if exist "%STAGE_DIR%\WindowsServer\Metin2" rmdir /s /q "%STAGE_DIR%\WindowsServer\Metin2"

call "%RUNUAT%" BuildCookRun ^
 -project="%UPROJECT%" ^
 -platform=Win64 ^
 -client -clientconfig=Shipping ^
 -server -serverconfig=Shipping ^
 -build -cook -stage -pak ^
 -iterativecooking -iterativedeploy ^
 -nodebuginfo ^
 -stagingdirectory="%STAGE_DIR%" ^
 -nop4 -utf8output

if errorlevel 1 (
    echo.
    echo [BuildShipping] FAILED - see the UAT log above.
    exit /b 1
)

rem --- Archive the matching PDBs privately (crash-dump symbolication needs the
rem --- EXACT pdb of the shipped exe; losing them makes crash reports useless).
for /f %%i in ('powershell -NoProfile -Command "Get-Date -Format yyyyMMdd_HHmmss"') do set "STAMP=%%i"
set "SYMBOL_DIR=%SYMBOL_ROOT%\%STAMP%"
mkdir "%SYMBOL_DIR%" 2>nul
copy /y "%PROJECT_DIR%\Binaries\Win64\UnrealLongjuClient-Win64-Shipping.pdb" "%SYMBOL_DIR%\" >nul 2>&1
copy /y "%PROJECT_DIR%\Binaries\Win64\UnrealLongjuServer-Win64-Shipping.pdb" "%SYMBOL_DIR%\" >nul 2>&1

rem --- Belt and braces: nothing symbol-like may remain in the staged output.
del /s /q "%STAGE_DIR%\*.pdb" >nul 2>&1
del /s /q "%STAGE_DIR%\*.map" >nul 2>&1

rem --- Build and place the self-contained patcher directly in the shipped client root. It is
rem --- deliberately excluded from manifest.json because a running patcher cannot replace itself.
call "%PROJECT_DIR%\Patcher\build.bat"
if errorlevel 1 (
    echo [BuildShipping] Patcher build FAILED.
    exit /b 1
)
copy /y "%PROJECT_DIR%\Patcher\bin\Release\net8.0-windows\win-x64\publish\UnrealLongjuPatcher.exe" ^
 "%STAGE_DIR%\WindowsClient\UnrealLongjuPatcher.exe" >nul
if errorlevel 1 (
    echo [BuildShipping] Could not stage UnrealLongjuPatcher.exe.
    exit /b 1
)

rem --- Patcher manifest: hash every staged game file into WindowsClient\manifest.json so the
rem --- patcher can diff+download only what changed. Override GATEWAY_ADDRESS for public hosting.
if "%PATCH_BASE_URL%"=="" set "PATCH_BASE_URL=https://mt2ue.iambvc.it/client/"
if "%GATEWAY_ADDRESS%"=="" set "GATEWAY_ADDRESS=rm2.zapto.org:11000"
powershell -NoProfile -ExecutionPolicy Bypass -File "%PROJECT_DIR%\Patcher\GenerateManifest.ps1" ^
 -StageDir "%STAGE_DIR%\WindowsClient" -BaseUrl "%PATCH_BASE_URL%" -Version "%SHIPPING_VERSION%" ^
 -LaunchExe "UnrealLongjuClient.exe" -LaunchArgs "%GATEWAY_ADDRESS%"
if errorlevel 1 (
    echo [BuildShipping] Manifest generation FAILED.
    exit /b 1
)

echo.
echo [BuildShipping] Done.
echo [BuildShipping]   Version: %SHIPPING_VERSION%
echo [BuildShipping]   Client : %STAGE_DIR%\WindowsClient  ^(upload contents + manifest.json to %PATCH_BASE_URL%^)
echo [BuildShipping]   Server : %STAGE_DIR%\WindowsServer
echo [BuildShipping]   Symbols: %SYMBOL_DIR%  ^(PRIVATE - archive, never distribute^)
endlocal
