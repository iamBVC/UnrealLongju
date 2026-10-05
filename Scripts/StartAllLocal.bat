@echo off
setlocal
cd /d "%~dp0.."

set "RUNTIME=F:\Engine2\Engine\Binaries\Win64\UnrealEditor.exe"
set "PROJECT=%CD%\UnrealLongju.uproject"
set "MT2_COORDINATOR_TOKEN=mt2-local-development-token-2026"
set "DB_ROOT=%CD%\Saved\LocalServer\Database"
set "MAP_PATH=/Game/Maps/Game/metin2_map_"

if not exist "%RUNTIME%" (
  echo Missing %RUNTIME%
  exit /b 1
)
if not exist "%DB_ROOT%" mkdir "%DB_ROOT%"

start "UnrealLongju Coordinator" "%RUNTIME%" "%PROJECT%" /Game/Maps/System/Coordinator ^
  -server -unattended -NoSound -Coordinator -log -port=11100 ^
  -co_map=/Game/Maps/System/Coordinator -co_bind=127.0.0.1 -co_port=11099 ^
  -co_token_env=MT2_COORDINATOR_TOKEN -allow_registration=1 ^
  -db_root="%DB_ROOT%" -db_file=metin2.db ^
  -db_synchronous=FULL -db_busy_timeout=5000 -db_integrity_check=1 ^
  -autosave=60 -heartbeat=5 -server_timeout=20 -ticket_lifetime=30 -request_timeout=15 -FrameProPort=11300

start "UnrealLongju map_a1 CH1" "%RUNTIME%" "%PROJECT%" "%MAP_PATH%a1" ^
  -server -unattended -NoSound -log -map="%MAP_PATH%a1" -map_id="map_a1" -channel=1 ^
  -instance_id=local-a1-ch1 -public_ip=127.0.0.1 ^
  -port=11001 -max_players=999 -co_ip=127.0.0.1 -co_port=11099 ^
  -co_token_env=MT2_COORDINATOR_TOKEN -heartbeat=5 -request_timeout=15 -server_tick_rate=30 -FrameProPort=11201

start "UnrealLongju map_b1 CH1" "%RUNTIME%" "%PROJECT%" "%MAP_PATH%b1" ^
  -server -unattended -NoSound -log -map="%MAP_PATH%b1" -map_id="map_b1" -channel=1 ^
  -instance_id=local-b1-ch1 -public_ip=127.0.0.1 ^
  -port=11002 -max_players=999 -co_ip=127.0.0.1 -co_port=11099 ^
  -co_token_env=MT2_COORDINATOR_TOKEN -heartbeat=5 -request_timeout=15 -server_tick_rate=30 -FrameProPort=11202

start "UnrealLongju map_c1 CH1" "%RUNTIME%" "%PROJECT%" "%MAP_PATH%c1" ^
  -server -unattended -NoSound -log -map="%MAP_PATH%c1" -map_id="map_c1" -channel=1 ^
  -instance_id=local-c1-ch1 -public_ip=127.0.0.1 ^
  -port=11003 -max_players=999 -co_ip=127.0.0.1 -co_port=11099 ^
  -co_token_env=MT2_COORDINATOR_TOKEN -heartbeat=5 -request_timeout=15 -server_tick_rate=30 -FrameProPort=11203

start "UnrealLongju Gateway" "%RUNTIME%" "%PROJECT%" /Game/Maps/System/Gateway ^
  -server -unattended -NoSound -Gateway -log -port=11000 -gateway_map=/Game/Maps/System/Gateway ^
  -instance_id=local-gateway-1 -public_ip=127.0.0.1 -max_players=32 ^
  -co_ip=127.0.0.1 -co_port=11099 ^
  -co_token_env=MT2_COORDINATOR_TOKEN -heartbeat=5 -request_timeout=15 -server_tick_rate=30 -FrameProPort=11299

start "UnrealLongju Client" "%RUNTIME%" "%PROJECT%" 127.0.0.1:11000 ^
  -game -log -windowed -ResX=1280 -ResY=720

endlocal
