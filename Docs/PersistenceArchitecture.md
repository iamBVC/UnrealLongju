# Persistence Architecture

## Authority and threading

Only coordinator mode opens the cluster SQLite database. Gateway and map processes proxy requests through their authenticated coordinator connection; clients never receive database paths or cluster credentials.

`UMT2PersistenceComponent` handles actor load, dirty tracking, periodic save, and end-play save. Its versioned JSON transport is parsed by `FMT2PersistenceBackend` into SQLite columns and child rows. Stable character IDs come from authentication, not transient actor names.

`UMT2PersistenceManager` async operations use the UE thread pool and deliver callbacks on the game thread. The local backend serializes database access with a mutex. Some coordinator guild/messenger handlers use the synchronous local backend directly; not every query is asynchronous.

## Current schema

One database, `metin2.db`, lives below `db_root`. The schema is embedded in [MT2PersistenceBackend.cpp](../Source/Metin2/Persistence/MT2PersistenceBackend.cpp).

| Tables | Stored state |
| --- | --- |
| `accounts` | Identity, password verifier, status, failed-login lock state. |
| `players` | Character ownership, progression, appearance, stats, resources, currency, location, skill group, and compact skills/quickslots/affects fields. |
| `items` | Inventory/equipment rows, slot, VNUM, count, five normal/two rare bonuses, and three sockets. |
| `player_quests`, `player_quest_flags` | Quest state/journal fields and per-player flags. |
| `admins` | Normalized character-name authority and enabled state. |
| `guilds`, `guild_members`, `guild_ranks` | Coordinator guild records, membership, contributions, and editable ranks. |
| `messenger_friends`, `messenger_messages` | Friend graph and stored private messages. |

JSON is a transport envelope, not a generic SQLite entity blob. Skills, quickslots, and affects currently use compact delimited TEXT fields on `players`; they are not fully normalized child tables. Item rows include a persisted instance identifier, but the runtime slot/quest-selection model still lacks the stable item identity required for complete legacy `item.select` parity.

WAL, foreign keys, busy timeout, integrity checks, transactions, and checkpoints are configured by the backend. Missing tables are created with `CREATE TABLE IF NOT EXISTS`. There is no automatic versioned schema migration: older tables are not upgraded merely by restarting. Back up data and plan explicit migrations when changing the schema.

## Static definitions versus mutable state

UE content owns static mob/item/skill/refine/shop definitions, effects, animations, spawn templates, map metadata, balancing data, and localization.

SQLite owns supported mutable account/character, inventory/equipment, quest, guild, and messenger state. Do not infer persistence for every gameplay subsystem from the existence of these tables. Ordinary mobs and their normal respawn timers remain runtime state. Exceptional persistent world state needs an explicit domain schema.

The older `UMT2GuildSubsystem` still has a local `Saved/MT2Guilds.json` path; it is not the coordinator database or a second authoritative cluster store. Keep local/fallback behavior separate from coordinator-backed guild operations.

## Operations and security

Store the database on coordinator-local storage. Do not independently copy a live DB and its WAL/SHM files as a backup. Use a controlled offline backup or a reviewed SQLite online-backup procedure. The current maintenance path checkpoints the database; that is not a scheduled backup/retention service.

Passwords use per-account random salts and PBKDF2-HMAC-SHA256 with 210,000 iterations. Login sessions and character leases are memory-only. A valid new login revokes the prior account session; map heartbeats refresh leases for characters actually online.

Cluster TCP authentication is not encryption. Keep the coordinator private and review network protection before deployment. See [Distributed Server Architecture](DistributedServerArchitecture.md).
