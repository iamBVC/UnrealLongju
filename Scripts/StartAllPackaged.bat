@echo off
setlocal EnableExtensions EnableDelayedExpansion
cd /d "%~dp0.."

rem ============================================================================
rem  Shared packaged-cluster launcher used by StartAllDevelopment.bat and
rem  StartAllShipping.bat. Edit the ports and map list only in this file.
rem
rem  Map entries use: MAP_ENTRY_nn=server_map_id^|cooked_world_asset_name
rem ============================================================================
set "GATEWAY_PORT=11000"
set "COORDINATOR_PORT=11099"
set "GAME_PORT_START=11001"
set "VOICE_PORT_START=11101"
set "PUBLIC_HOST=127.0.0.1"
set "CHANNEL=1"
set "MAX_MAP_PLAYERS=999"

if not "%~1"=="" set "PUBLIC_HOST=%~1"

set "MAP_ENTRY_01=metin2_map_a1|yongan"
set "MAP_ENTRY_02=metin2_map_a3|yayang"
set "MAP_ENTRY_03=metin2_map_b1|joan"
set "MAP_ENTRY_04=metin2_map_b3|bokjung"
set "MAP_ENTRY_05=metin2_map_c1|pyungmoo"
set "MAP_ENTRY_06=metin2_map_c3|bakra"
set "MAP_ENTRY_07=map_n_snowm_01|mount_sohan"
set "MAP_ENTRY_08=metin2_map_n_flame_01|doyyumhwan"
set "MAP_ENTRY_09=metin2_map_n_desert_01|yongbi_desert"
set "MAP_ENTRY_10=map_n_threeway|seungryong_valley"
set "MAP_ENTRY_11=metin2_map_milgyo|hwang_temple"
set "MAP_ENTRY_12=metin2_map_deviltower1|devil_tower"
set "MAP_ENTRY_13=metin2_map_trent|lungsam"
set "MAP_ENTRY_14=metin2_map_trent02|red_forest"
set "MAP_ENTRY_15=metin2_map_WL_01|snakefield"
set "MAP_ENTRY_16=metin2_map_spiderdungeon_02|spiderdungeon_02"
set "MAP_ENTRY_17=metin2_map_wedding_01|wedding"
set "MAP_ENTRY_18=metin2_map_spiderdungeon|spiderdungeon_01"
set "MAP_ENTRY_19=metin2_map_monkey_dungeon|monkeydungeon_01"
set "MAP_ENTRY_20=metin2_map_monkey_dungeon2|monkeydungeon_02"
set "MAP_ENTRY_21=metin2_map_monkey_dungeon3|monkeydungeon_03"
set "MAP_ENTRY_22=metin2_map_duel|duel_arena"
set "MAP_ENTRY_23=metin2_map_devilcatacomb|devils_catacomb"
set "MAP_ENTRY_24=metin2_map_spiderdungeon_03|spiderdungeon_03"
set "MAP_ENTRY_25=metin2_map_CapeDragonHead|cape_dragon_fire"
set "MAP_ENTRY_26=metin2_map_dawnmistwood|gautama_cliff"
set "MAP_ENTRY_27=metin2_map_BayBlackSand|nephrite_bay"
set "MAP_ENTRY_28=metin2_map_Mt_Thunder|thunder_mountains"

rem ============================================================================
rem  Derived paths and shared parameters. No per-map configuration below here.
rem ============================================================================
if not defined MT2_BUILD_KIND (
  echo Packaged build kind is not configured.
  exit /b 1
)
if not defined MT2_STAGE_DIR (
  echo Packaged staging directory is not configured.
  exit /b 1
)
if not defined MT2_SERVER_BINARY (
  echo Packaged server binary is not configured.
  exit /b 1
)

set "SERVER=%CD%\%MT2_STAGE_DIR%\WindowsServer\UnrealLongju\Binaries\Win64\%MT2_SERVER_BINARY%"
set "CLIENT=%CD%\%MT2_STAGE_DIR%\WindowsClient\UnrealLongjuClient.exe"
set "DB_ROOT=%CD%\Saved\LocalServer\Database"
set "MAP_ROOT=/Game/Maps/Game"
set "MT2_COORDINATOR_TOKEN=mt2-local-development-token-2026"
set /a "VOICE_PORT_OFFSET=VOICE_PORT_START-GAME_PORT_START"
set "CLIENT_AUTH_ARGS="

if "%MT2_REQUIRE_PATCHER_TOKEN%"=="1" (
  for /f %%T in ('powershell -NoProfile -Command "[Guid]::NewGuid().ToString('N')"') do set "MT2UE_PATCHER_TOKEN=%%T"
  if not defined MT2UE_PATCHER_TOKEN (
    echo Could not generate the Shipping client launch token.
    exit /b 1
  )
  set "CLIENT_AUTH_ARGS=-MT2PatcherToken=!MT2UE_PATCHER_TOKEN!"
)

if not exist "%SERVER%" (
  echo Missing %SERVER%
  echo Build the %MT2_BUILD_KIND% cluster first with Scripts\%MT2_BUILD_SCRIPT%.
  exit /b 1
)
if not exist "%CLIENT%" (
  echo Missing %CLIENT%
  echo Build the %MT2_BUILD_KIND% cluster first with Scripts\%MT2_BUILD_SCRIPT%.
  exit /b 1
)
if not exist "%DB_ROOT%" mkdir "%DB_ROOT%"

start "UnrealLongju Coordinator" "%SERVER%" /Game/Maps/System/Coordinator ^
  -log -NoSound -Coordinator -port=%COORDINATOR_PORT% -allow_registration=1 ^
  -db_root="%DB_ROOT%" ^
  -db_synchronous=FULL -db_busy_timeout=5000 -db_integrity_check=1 ^
  -autosave=60 -heartbeat=5 -server_timeout=20 -ticket_lifetime=30 -request_timeout=15

timeout 1 > NUL

set /a "MAP_INDEX=0"
for /f "tokens=1,* delims==" %%I in ('set MAP_ENTRY_ 2^>nul') do (
  for /f "tokens=1,2 delims=|" %%A in ("%%J") do (
    set /a "GAME_PORT=GAME_PORT_START+MAP_INDEX"
    set /a "VOICE_PORT=VOICE_PORT_START+MAP_INDEX"
    if !GAME_PORT! GTR 65535 (
      echo Game port range exceeds 65535.
      exit /b 1
    )
    if !VOICE_PORT! GTR 65535 (
      echo Voice port range exceeds 65535.
      exit /b 1
    )

    echo Starting %%A CH%CHANNEL% on game port !GAME_PORT! and voice port !VOICE_PORT!...
    start "UnrealLongju %%A CH%CHANNEL%" "%SERVER%" "%MAP_ROOT%/%%B" ^
      -log -NoSound -map="%MAP_ROOT%/%%B" -map_id="%%A" -channel=%CHANNEL% ^
      -instance_id="local-%%A-ch%CHANNEL%" -public_ip="%PUBLIC_HOST%" ^
      -port=!GAME_PORT! -voice_port=!VOICE_PORT! ^
      -max_players=%MAX_MAP_PLAYERS%
    set /a "MAP_INDEX+=1"
  )
)

timeout 5 > NUL

start "UnrealLongju Gateway" "%SERVER%" /Game/Maps/System/Gateway ^
  -log -NoSound -Gateway -public_ip="%PUBLIC_HOST%" -port=%GATEWAY_PORT% ^
  -instance_id=local-gateway-1 -max_players=32

start "UnrealLongju Client" "%CLIENT%" %PUBLIC_HOST%:%GATEWAY_PORT% ^
  -windowed -ResX=1280 -ResY=720 -voice_port_offset=%VOICE_PORT_OFFSET% ^
  !CLIENT_AUTH_ARGS!

echo Started %MAP_INDEX% map servers from the %MT2_BUILD_KIND% build.
endlocal
