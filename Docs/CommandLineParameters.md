# UnrealLongju UE5 — Process Command-Line Parameters

All roles are decided at startup by `UMT2ServerRuntimeSubsystem::ParseCommandLine`
([MT2ServerRuntimeSubsystem.cpp](../Source/Metin2/Server/MT2ServerRuntimeSubsystem.cpp)).
A process is a **client** unless it runs as a dedicated server (or passes `-Coordinator` /
`-Gateway`). Dedicated servers without `-Coordinator`/`-Gateway` are **map servers**.

Executables:

- Development, uncooked: `UnrealEditor.exe <path-to>\UnrealLongju.uproject <map-or-url> <flags>`
- Staged/dev build: `Saved\Staged\WindowsClient\UnrealLongjuClient.exe` / `WindowsServer\UnrealLongjuServer.exe`
- Shipping: `Saved\StagedShipping\WindowsClient\UnrealLongjuClient.exe` / `WindowsServer\UnrealLongjuServer.exe`
  (server exe implies `-server`; the client exe never runs server roles)

Boolean parameters use `-name=1` / `-name=0` (not bare switches), except the role switches
`-Coordinator` and `-Gateway` which are bare.

## Shared parameters (all server roles)

| Parameter | Default | Meaning |
|---|---|---|
| `-instance_id=<id>` | random GUID | Stable identifier of this server instance in the cluster |
| `-co_ip=<ip>` | 127.0.0.1 | Coordinator address to connect to |
| `-co_port=<port>` | 11099 | Coordinator TCP port |
| `-co_token=<secret>` | — | Cluster auth token, **min 16 chars** (prefer the env variant) |
| `-co_token_env=<VAR>` | `MT2_COORDINATOR_TOKEN` | Environment variable holding the token |
| `-heartbeat=<s>` | 5 (clamped 1–60) | Heartbeat interval to the coordinator |
| `-server_timeout=<s>` | ≥ 2×heartbeat | Coordinator declares a server dead after this silence |
| `-ticket_lifetime=<s>` | 30 (clamped 5–300) | Validity of map-transfer admission tickets |
| `-request_timeout=<s>` | 15 (clamped 2–120) | Timeout for cluster RPC requests |
| `-allow_registration=<0/1>` | 0 | Whether account registration is accepted (read cluster-wide; set it on the coordinator) |
| `-WaitForParentPid=<pid>` | — | Waits (max 30 s) for that process to exit before starting — used by `/reboot` self-restart |

A **startup map** is passed as the first positional argument (e.g.
`/Game/Maps/System/Coordinator`); the runtime also self-travels to its configured map
(`-co_map` / `-gateway_map` / `-map`) if the loaded one differs.

Useful stock UE flags for any server: `-server -log -unattended -NoSound`, `-port=<game port>`.

## Coordinator (`-Coordinator`)

Owns the SQLite database, account auth, session tokens, routing and transfer tickets.

| Parameter | Default | Meaning |
|---|---|---|
| `-co_bind=<ip>` | 127.0.0.1 | Interface the coordinator listens on |
| `-co_map=<package>` | `/Game/Maps/System/Coordinator` | Map the coordinator process idles in |
| `-db_root=<dir>` | `Saved/Database` | Directory of the SQLite DB (**local disk only**, no UNC paths) |
| `-db_file=<name>` | metin2.db | Plain filename below db_root |
| `-db_synchronous=<FULL/NORMAL>` | — (required valid) | SQLite synchronous mode |
| `-db_busy_timeout=<ms>` | clamped 0–120000 | SQLite busy timeout |
| `-db_integrity_check=<0/1>` | 1 | Run integrity check at startup |
| `-autosave=<s>` | 60 (min 5) | Cluster autosave interval pushed to persistence components |

Example (local dev):

```bat
UnrealEditor.exe UnrealLongju.uproject /Game/Maps/System/Coordinator ^
  -server -unattended -NoSound -Coordinator -log -port=11100 ^
  -co_map=/Game/Maps/System/Coordinator -co_bind=127.0.0.1 -co_port=11099 ^
  -co_token_env=MT2_COORDINATOR_TOKEN -allow_registration=1 ^
  -db_root="...\Saved\LocalServer\Database" -db_file=metin2.db ^
  -db_synchronous=FULL -db_busy_timeout=5000 -db_integrity_check=1 ^
  -autosave=60 -heartbeat=5 -server_timeout=20 -ticket_lifetime=30 -request_timeout=15
```

## Gateway (`-Gateway`)

Public login/character-select server; clients connect here first (default game port 11000).

| Parameter | Default | Meaning |
|---|---|---|
| `-public_ip=<ip>` | 127.0.0.1 | Address advertised to clients |
| `-port=<port>` | 11000 | Game (Unreal net) port clients connect to |
| `-gateway_map=<package>` | `/Game/Maps/System/Gateway` | Gateway map |
| `-max_players=<n>` | ≥1 | Connection cap |

Example:

```bat
UnrealEditor.exe UnrealLongju.uproject /Game/Maps/System/Gateway ^
  -server -unattended -NoSound -Gateway -log -port=11000 ^
  -gateway_map=/Game/Maps/System/Gateway -instance_id=local-gateway-1 ^
  -public_ip=127.0.0.1 -max_players=32 -co_ip=127.0.0.1 -co_port=11099 ^
  -co_token_env=MT2_COORDINATOR_TOKEN -heartbeat=5 -request_timeout=15
```

## Map server (dedicated server, no role switch)

Runs one game map on one channel.

| Parameter | Default | Meaning |
|---|---|---|
| `-map=<package>` | — (required) | Map package path (`/Game/Maps/Game/metin2_map_a1`) |
| `-map_id=<id>` | derived from map name | Logical map id used for routing (`map_a1`; `metin2_map_`/`map_` prefixes are stripped when deriving) |
| `-channel=<n>` | 1 (min 1) | Channel number |
| `-public_ip=<ip>` | 127.0.0.1 | Address advertised in transfer tickets |
| `-port=<port>` | 11000+channel | Game port |
| `-max_players=<n>` | ≥1 | Connection cap |

Example:

```bat
UnrealEditor.exe UnrealLongju.uproject /Game/Maps/Game/metin2_map_a1 ^
  -server -unattended -NoSound -log -map=/Game/Maps/Game/metin2_map_a1 ^
  -map_id=map_a1 -channel=1 -instance_id=local-a1-ch1 -public_ip=127.0.0.1 ^
  -port=11001 -max_players=999 -co_ip=127.0.0.1 -co_port=11099 ^
  -co_token_env=MT2_COORDINATOR_TOKEN -heartbeat=5 -request_timeout=15
```

## Client

No MT2-specific parameters — the first positional argument is the gateway URL. Common UE
flags:

| Parameter | Meaning |
|---|---|
| `<ip>:<port>` | Gateway to connect to (e.g. `127.0.0.1:11000`); a returning map server travel uses `?ticket=` internally |
| `-game` | Run the editor binary as a standalone game (uncooked dev only) |
| `-log` | Show the log window |
| `-windowed` / `-fullscreen` | Window mode override |
| `-ResX=<w> -ResY=<h>` | Resolution override |
| `-NoSound` | Disable audio |
| `-culture=<code>` | Language override |

Example:

```bat
UnrealEditor.exe UnrealLongju.uproject 127.0.0.1:11000 -game -log -windowed -ResX=1280 -ResY=720
```

Shipping is started through `UnrealLongjuPatcher.exe`. The patcher passes the gateway and window arguments
from `manifest.json`; local test scripts override them through the patcher without launching the
client directly. No `-game` argument is needed because a cooked client is always a game.

## Notes

- The coordinator rejects startup (and exits) on invalid DB config or a token shorter than
  16 characters; gateway/map servers exit if their required parameters are missing.
- Voice chat: each server's UDP voice relay binds its **game port + 100** (map server 11001 →
  voice 11101); the client derives the same port from the connection it is on.
  `UMT2GameplaySettings.VoiceChatPort` (11100) is only a fallback when no game port is known.
- Port plan: **11000** gateway (UDP), **11001–11098** map servers (UDP, `11000+channel`),
  **11099** coordinator (TCP, internal only — do not forward), **11101–11198** voice chat
  (UDP, per map server = game port + 100).
- The local scripts `Scripts\StartAllLocal.bat`, `StartCoordinatorLocal.bat`, `StartGatewayLocal.bat`,
  `StartMapServerLocal.bat` (`args: mapPath mapId channel port`) and `Scripts\StartClientLocal.bat`
  are working references for every role.
