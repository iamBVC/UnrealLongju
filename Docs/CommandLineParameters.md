# UnrealLongju UE5 — Process Command-Line Parameters

All roles are decided at startup by `UMT2ServerRuntimeSubsystem::ParseCommandLine`
([MT2ServerRuntimeSubsystem.cpp](../Source/Metin2/Server/MT2ServerRuntimeSubsystem.cpp)).
A process is a **client** unless it runs as a dedicated server (or passes `-Coordinator` /
`-Gateway`). Dedicated servers without `-Coordinator`/`-Gateway` are **map servers**.

Executables:

- Development, uncooked: `UnrealEditor.exe <path-to>\UnrealLongju.uproject <map-or-url> <flags>`
- Staged/dev build: `Saved\Staged\WindowsClient\UnrealLongjuClient.exe` / `Saved\Staged\WindowsServer\UnrealLongju\Binaries\Win64\UnrealLongjuServer.exe`
- Shipping: `Saved\StagedShipping\WindowsClient\UnrealLongjuClient.exe` / `Saved\StagedShipping\WindowsServer\UnrealLongju\Binaries\Win64\UnrealLongjuServer-Win64-Shipping.exe`
  (server exe implies `-server`; the client exe never runs server roles)

Source review: 2026-10-05. Defaults below are runtime defaults, not launcher overrides. MT2 boolean parameters use `-name=1` / `-name=0` (not bare switches), except the role switches
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
| `-server_timeout=<s>` | 20 (minimum 2×heartbeat) | Coordinator declares a server dead after this silence |
| `-ticket_lifetime=<s>` | 30 (clamped 5–300) | Validity of map-transfer admission tickets |
| `-request_timeout=<s>` | 15 (clamped 2–120) | Timeout for cluster RPC requests |
| `-allow_registration=<0/1>` | 0 | Whether account registration is accepted (read cluster-wide; set it on the coordinator) |
| `-server_tick_rate=<n>` | 30 (clamped 10–60) | Requested server tick rate |
| `-incremental_gc=<0/1>` | 1 | Server incremental garbage-collection setting |
| `-gc_budget_ms=<ms>` | 2 (clamped 0.25–10) | Incremental GC time budget |
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
| `-db_synchronous=<FULL/NORMAL>` | FULL | SQLite synchronous mode; invalid values reject startup |
| `-db_busy_timeout=<ms>` | 5000 (clamped 0–120000) | SQLite busy timeout |
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
| `-public_ip=<ip>` | required, no default | Address advertised to clients |
| `-port=<port>` | 11000 | Game (Unreal net) port clients connect to |
| `-gateway_map=<package>` | `/Game/Maps/System/Gateway` | Gateway map |
| `-max_players=<n>` | 1000 (minimum 1) | Connection cap |

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
| `-map=<package>` | required | Map package path (e.g. `/Game/Maps/Game/yongan`) |
| `-map_id=<id>` | map package short name | Logical routing id. Set `metin2_map_a1` explicitly for Yongan; prefix normalization occurs during routing, not default derivation. |
| `-channel=<n>` | 1 (min 1) | Channel number |
| `-public_ip=<ip>` | required, no default | Address advertised in transfer tickets |
| `-port=<port>` | 11000+channel | Game port |
| `-max_players=<n>` | 1000 (minimum 1) | Connection cap |

Example:

```bat
UnrealEditor.exe UnrealLongju.uproject /Game/Maps/Game/yongan ^
  -server -unattended -NoSound -log -map=/Game/Maps/Game/yongan ^
  -map_id=metin2_map_a1 -channel=1 -instance_id=local-a1-ch1 -public_ip=127.0.0.1 ^
  -port=11001 -max_players=999 -co_ip=127.0.0.1 -co_port=11099 ^
  -co_token_env=MT2_COORDINATOR_TOKEN -heartbeat=5 -request_timeout=15
```

## Client

The first positional argument is the gateway URL. Common UE
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
| `-voice_port_offset=<n>` | MT2 voice relay offset relative to the connected game port; defaults to 100 |

Example:

```bat
UnrealEditor.exe UnrealLongju.uproject 127.0.0.1:11000 -game -log -windowed -ResX=1280 -ResY=720
```

Shipping is started through `UnrealLongjuPatcher.exe`. The patcher passes the gateway and window arguments
from `manifest.json`. `StartClientShipping.bat` uses the patcher override path, while `StartAllShipping.bat`
generates a local `MT2UE_PATCHER_TOKEN` and launches directly with the matching `-MT2PatcherToken` argument.
This is a local launch check, not account authentication. No `-game` argument is needed for a cooked client.

## Notes

- The coordinator rejects startup (and exits) on invalid DB config or a token shorter than
  16 characters; gateway/map servers exit if their required parameters are missing.
- Voice chat: by default each server's UDP voice relay binds its **game port + 100** (map server 11001 →
  voice 11101); the client derives the same port from the connection it is on.
  `UMT2GameplaySettings.VoiceChatPort` (11100) is only a fallback when no game port is known.
  `-voice_port=<port>` overrides the server relay; clients need a matching `-voice_port_offset`.
- Packaged launcher defaults: **11000** gateway (UDP), **11001–11028** for its 28 map processes (UDP),
  **11099** coordinator (TCP, private), and **11101–11128** map voice relays (UDP). Map processes all use channel 1;
  their game ports increase by map index, not by channel. Plan distinct endpoints if adding processes/channels.
- `Scripts\StartAllPackaged.bat` is the shared current map/port list. The uncooked `StartAllLocal.bat`
  and `StartClientLocal.bat` still use machine-specific/older map paths. Previously documented root
  `StartCoordinatorLocal.bat`, `StartGatewayLocal.bat`, and `StartMapServerLocal.bat` files are absent.
- Examples require the corresponding assets in the content submodule and a token of at least 16 characters in `MT2_COORDINATOR_TOKEN`.
