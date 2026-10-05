# Distributed Server Architecture

## Process Roles

Source review: 2026-10-05. Parameter defaults come from `FMT2ServerRuntimeConfig` and `ParseCommandLine`; launcher overrides are not runtime defaults.

`UnrealLongjuServer.exe` has three server runtime modes selected only through command-line parameters.

- Map mode is the default dedicated-server mode. One process owns one loaded map and one channel.
- Gateway mode is enabled with `-Gateway`. Clients connect here for registration, login, character creation, and character selection. It owns no database files.
- Coordinator mode is enabled with `-Coordinator`. It owns map/channel discovery, transfer tickets, and database access. It rejects game-client logins.
- Game clients never receive coordinator or database credentials.

Processes may run on different machines. Every map server advertises a client-reachable `public_ip` and game `port`, then maintains a separate TCP connection to the coordinator.

## Channels

The coordinator registry key is the unique process `instance_id`; routing filters by `map_id` and optional `channel`.

- A request for channel `N` selects an available process hosting that map/channel.
- A request with channel `0` selects the least-loaded available channel for that map.
- Multiple processes may host the same map/channel later for sharding, although normal operation should use one process per map/channel pair.
- Heartbeats carry player count. Full or timed-out processes are excluded from routing.
- Heartbeats also carry authoritative online character IDs. Character leases are refreshed only for
  characters actually present on that map process and expire after a lost server/client cleanup path.

## Transfers

1. Gateway selection or a source map server requests a destination for `map_id` and optional channel.
2. Coordinator selects a registered `Ready` process and issues a cryptographically random, short-lived transfer ticket.
3. Source saves authoritative player state through coordinator persistence.
4. Client travels to the returned public endpoint and presents the ticket during login.
5. Destination claims the ticket. The coordinator verifies destination instance, expiration, session, and character identity.
6. Destination loads the character through coordinator persistence, spawns it, and completes the ticket.

Claims are two-phase: a failed destination load releases the claim until expiration; a successful load consumes it. Source-map migration freezes movement and forces an authoritative save before travel.

## Login Flow

1. Client connects to a Gateway process and submits account credentials through a server RPC.
2. Gateway proxies the request to the coordinator over its authenticated service connection.
3. Coordinator verifies the PBKDF2-HMAC-SHA256 password in `metin2.db`, creates an in-memory session, and returns character summaries to that Gateway connection.
4. Character creation and selection require the opaque coordinator session held server-side by the Gateway controller. The client never receives this session token.
5. Character selection creates an admission ticket and sends only the selected map endpoint and ticket to the client.

Only one session per account and one lease per character are active. A valid new login replaces an
existing session, disconnects the old Gateway or map client, and reports the takeover to both clients.
This intentionally lets a player recover an account left online by a crashed client.

`UMT2ClientSessionSubsystem` exposes connection, authentication, selection, travel, failure, and in-world states to Blueprint UI across controller replacement and map travel. Passwords are never retained by it.

## Persistence

Only coordinator mode opens the local `metin2.db`. Map servers proxy load/save requests over their authenticated coordinator connection. A map process has no database paths or credentials.

The coordinator stores accounts, players, items, admins, quest state/flags, guilds/members/ranks, and messenger friends/messages. JSON is the transport envelope; skills, quickslots, and affects use compact TEXT player columns. See [Persistence Architecture](PersistenceArchitecture.md) for the current eleven-table schema and the absence of automatic versioned migrations.

## Authentication And Network Security

Map servers authenticate with the shared `co_token`. The comparison is constant-time and unauthenticated peers cannot register, route, transfer, or persist data.

The internal protocol has an explicit version check, bounded receive/send buffers, and finite request
deadlines. Incompatible processes are rejected during authentication instead of failing later.

The current TCP protocol is authenticated but not encrypted. Run it on a private network, WireGuard/VPN, or protected internal VLAN. TLS plus rotating per-instance credentials should be added before exposing the coordinator port to an untrusted network.

Prefer `co_token_env`. Direct `co_token` is supported, but command-line secrets may be visible in process listings.

## Coordinator Parameters

- `-Coordinator`: Enables coordinator mode.
- `-co_bind=127.0.0.1`: Default coordinator listen address. Bind a reviewed private interface when peers run on other machines.
- `-co_map=/Game/Maps/System/Coordinator`: Lightweight map used by the headless coordinator process.
- `-co_port=11099`: Coordinator TCP port.
- `-co_token=...` or `-co_token_env=NAME`: Shared server credential. Minimum 16 characters.
- `-db_root=D:\MT2Data`: Local root directory. Network shares are rejected.
- `-db_file=metin2.db`: Single coordinator database filename.
- `-db_synchronous=FULL`: `FULL` or `NORMAL` durability.
- `-db_busy_timeout=5000`: SQLite lock wait in milliseconds.
- `-db_integrity_check=1`.
- `-autosave=60`, `-server_timeout=20`, `-ticket_lifetime=30`.
- `-request_timeout=15`: Deadline for coordinator requests made by Gateway/map processes.
- `-allow_registration=1`: Enables self-registration. It is disabled by default.

## Map Server Parameters

- `-map=/Game/Maps/map_name`: Package loaded by this process.
- `-map_id=map_name`: Stable routing identity. Defaults to the unmodified map package short name; routing normalizes known legacy prefixes separately. Set it explicitly when cooked asset names differ from legacy IDs.
- `-channel=1`: Channel number.
- `-instance_id=...`: Optional stable process identity. A random GUID is generated when omitted.
- `-public_ip=...`, `-port=11001`: Client-reachable endpoint. The default is `11000 + channel`.
- `-max_players=1000`: Capacity used by coordinator routing.
- `-co_ip=...`, `-co_port=11099`.
- `-co_token=...` or `-co_token_env=NAME`.
- `-heartbeat=5`.

## Gateway Parameters

- `-Gateway`: Enables Gateway mode.
- `-gateway_map=/Game/Maps/System/Gateway`: Lightweight login/selection map.
- New characters start on `metin2_map_a1`, `metin2_map_b1`, or `metin2_map_c1` according to their empire.
- `-public_ip=...`, `-port=11000`: Client-reachable Gateway endpoint.
- `-max_players=1000`: Default concurrent connection capacity; launchers may override it.
- `-co_ip=...`, `-co_port=11099` and coordinator token options.

For uncooked development, `Scripts\StartAllLocal.bat` and `Scripts\StartClientLocal.bat` use
`UnrealEditor.exe -server/-game`, but retain hard-coded engine/older map paths that need review.
The formerly documented root per-role launchers are not present. Prefer `Scripts\StartAllDevelopment.bat`
after packaging for the current map list. Game/server target executables need cooked/staged content.

## Cluster Administration

Authenticated administrators can issue these commands from any map server:

- `/goto <mapname>`: Saves the player and migrates through the coordinator, preferring the current channel and falling back to another ready channel.
- `/tp <x> <y> [z]`: Teleports inside the current map. Without `z`, the server traces the ground at `x,y`.
- `/servers`: Lists the coordinator, gateways, and registered map/channel processes.
- `/persist`: Forces every connected player to save, then checkpoints the coordinator database.
- `/shutdown`: Stops admissions, notifies and disconnects players, saves all players, checkpoints SQLite, then exits every process.
- `/reboot`: Runs the same ordered shutdown and starts replacement processes with their original command lines.

Shutdown and reboot wait for peer readiness for up to 30 seconds. Any unavailable peer is reported,
but the requested cluster exit still completes so maintenance cannot remain half-finished indefinitely.
