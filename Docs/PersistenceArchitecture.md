# Persistence Architecture

## Authority

Only the coordinator process opens SQLite. Map servers proxy persistence requests through their authenticated coordinator connection, and clients never access database paths. Coordinator queries run on the UE thread pool and return on the game thread.

`UMT2PersistenceComponent` gives a persistent actor automatic load, dirty tracking, periodic save, and end-play save. Its current map-server/coordinator transport is versioned JSON, but JSON is never stored in SQLite. The coordinator parses the message and writes typed relational columns in one transaction. Stable IDs must come from authentication or the owning domain repository; transient UE actor names are not valid identities.

## SQLite Databases

The coordinator creates exactly one active database, `metin2.db`, below `db_root`. SQLite does not support databases nested inside another database; domain separation is represented by normalized tables in that file.

- `accounts`: login identity, password verifier, status, and failed-login lock state.
- `players`: one row per character with the currently implemented scalar character state.
- `items`: one row per occupied inventory or equipment slot.
- `admins`: character admin authority.

There is no generic entity table and no JSON/blob state column. Player progression, assigned ST/DX/HT/IQ, available points, resources, appearance, location, currency, inventory, and equipment have explicit columns or item rows. WAL, foreign keys, busy timeout, integrity checks, transactions, and graceful checkpoints are configured automatically.

The runtime creates missing tables but performs no schema migration or database versioning. Schema changes are manual for now. `Saved/LocalServer/Database` contains only the active `metin2.db` and SQLite's temporary WAL/SHM files while the coordinator is running.

## Store In UE Assets

- Mob, item, skill, refine, object, effect, animation, and shop definitions.
- Static drop-table definitions and spawn templates.
- Map data, NPC placement, localization, and balancing constants.
- Visual and audio asset references.

## Store In SQLite

- Accounts and character ownership. Authentication remains a separate service boundary.
- Character progression, appearance, currency, resources, assigned stats, available points, and position.
- Current inventory/equipment item VNUM, count, and slot.

Skill levels, quick slots, active effects, item sockets/bonuses, guilds, quests, social state, logs, and mutable world state are intentionally not persisted until their schemas are agreed.

Ordinary mobs are not persisted. Persist only exceptional world state such as a scheduled boss respawn or an explicitly persistent spawned actor.

## Operations

The database must remain on coordinator-local storage. Back up a running database through a coordinator maintenance operation using SQLite backup or `VACUUM INTO`; do not copy the active database, WAL, or journal files independently. A later maintenance API will expose scheduled online backup and retention controls.

The four-table schema is embedded in the runtime so cooked coordinator builds do not depend on loose SQL files.

Account passwords use per-account random salts and PBKDF2-HMAC-SHA256 with 210,000 iterations. The coordinator applies a temporary lock after repeated failed attempts. Active login sessions and character leases are memory-only. A new valid login revokes the old account session and active character lease; map heartbeats refresh leases for characters actually online.
