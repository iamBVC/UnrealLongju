# UnrealLongju Project Status

Updated: 2026-08-12

## Working Foundation

- UE 5.7 project builds as Editor, Client, Server, and standalone Game targets.
- MT2UE imports textures, static/skeletal meshes, animations, sounds, effects, landscapes, and map objects.
- Imported assets use `T_`, `SM_`, `SK_`, `M_`, and `MI_` naming where applicable.
- Landscape import reuses existing terrain and map objects instead of replacing imported assets.
- Player framework uses standard UE GameMode, GameState, PlayerState, PlayerController, Character, Enhanced Input, replication, and GAS.
- Reusable health, mana, stamina, movement speed, skills, status effects, equipment, combat, footsteps, and persistence components exist.
- Race, sex, style, empire, hair, player mesh, movement, click movement, camera control, walk/run, stamina slowdown, and held attack input exist.
- Generated player and mob Animation Blueprints use native animation instances and imported animations.
- Mob definitions, Blueprints, Animation Blueprints, AI, movement, attacks, health, death, and rewards exist.
- Imported mob proto values are split into reusable primary-stat and combat-stat components, mob lifecycle settings, and reward settings.
- Player ST/DX/HT/IQ values use the same primary-stat component and are included in versioned persistence JSON.
- Runtime mob and item lookup loads a saved VNUM registry from `/Game/Logic`; PIE and servers do not scan assets. Map spawn entries store only VNUMs.
- MT2UE automatically rebuilds the saved registry after relevant Blueprint changes and rejects duplicate mob or item VNUMs.
- Item definitions use a Blueprintable `UMT2ItemTemplate` hierarchy with specialized weapon, armor, consumable, material, equipment, and legacy-type classes.
- Runtime `UMT2Item` objects store `TSubclassOf<UMT2ItemTemplate>`, count, bonuses, sockets, and an instance GUID. The item proto importer now updates 5,743 item Blueprint classes and the replicated inventory/equipment authority layer is active.
- Equipped weapons snap to race-correct legacy hand bones and select the matching animation set. Body armor resolves race/sex skeletal meshes from the legacy MSM shape tables.
- Equipment validation is server-authoritative and enforces item race, sex, level, STR, DEX, INT, and CON requirements before changing slots.
- Backpack slots replicate only to their owner; worn equipment replicates to every relevant client so remote character visuals stay correct.
- Inventory icons support full multi-cell click, right-click, tooltip, drag, and drop interaction. Chat opening uses deterministic text focus.
- SQLite persistence has one coordinator-only normalized database, serialized access, WAL, integrity checks, migrations, an actor component, and a versioned transport contract.
- Dedicated servers support distributed map and coordinator modes configured entirely by startup parameters.
- Map servers register map/channel/public endpoint, report load, and proxy persistence to the coordinator. The coordinator alone owns DB credentials.
- Coordinator routing, channel selection, authenticated heartbeats, and single-use transfer-ticket transport exist.
- Gateway registration/login, normalized accounts and characters, server-held sessions, character creation/selection, readiness-aware admission, delayed persistent spawn, and map travel exist.
- Client session state survives controller replacement and exposes connection, character selection, travel, failure, and in-world phases to Blueprint UI.

## Current Mob Slice

- `AMT2MobSpawnActor` is the level-owned map spawn manager. A component cannot exist by itself in a level, so the actor owns `UMT2MobSpawnComponent`.
- Map import creates or updates one spawn actor tagged per map. Reimporting a map does not duplicate the actor.
- Spawn entries contain only mob VNUMs. The runtime registry resolves each VNUM through its mob data asset and soft Blueprint class link; missing assets are logged and skipped.
- Server `regen.txt`, `npc.txt`, `boss.txt`, and `stone.txt` are parsed. Direct mobs, groups, weighted group-groups, bounds, direction, count, chance, and respawn delay are represented.
- The authoritative server spawns groups gradually, traces landscape height, tracks living group members, and respawns a group after all its members are gone.
- Player unarmed attacks use GAS, a server pawn sweep, visibility validation, and the damage Gameplay Effect. Imported `attack` and `attack_1` clips play through `DefaultSlot` when available.
- Damage records the last authoritative instigator, allowing mob death rewards to identify the killer.
- Mob defense now reduces incoming basic-attack damage. Regeneration and resurrection VNUM behavior are owned by `UMT2MobLifecycleComponent`; resurrecting mobs grant experience but suppress item/gold drops like the original server.

## Loot

- Each generated `AMT2Mob` Blueprint stores its own editable `LootEntries` array directly on the class defaults.
- Independent `drop` entries and weighted `kill` groups from `mob_drop_item.txt` are represented, including count, chance/weight, kill average, and rare-attribute chance.
- Loot rolling supports independent drop chances and weighted kill groups without separate loot-table assets.
- `UMT2MobLootComponent` rolls multiple items and broadcasts one reward bundle containing killer, experience, gold, and all item results.
- Mob and map import workflows parse `mob_drop_item.txt` and update inline arrays on existing mob Blueprints. Item assets are intentionally not required yet.

## Persistence Boundary

- Static mob, item, skill, map, shop, spawn, and loot definitions belong in UE assets.
- Accounts, characters, progression, currency, inventory instances, guild state, quests, social state, and mutable world state belong in normalized tables in coordinator `metin2.db`.
- Ordinary mob instances and normal respawn timers are runtime state and are not persisted.

## Guild Core

- The coordinator owns guild authority and stores normalized guild state in `guilds`, `guild_members`, and `guild_ranks` tables.
- Core guild creation follows the old level-40, 200,000 Yang, 12-character-name rules and creates 15 editable ranks.
- Guild membership survives map travel and works across map servers. Login/logout refreshes the roster's online, map, channel, and level data.
- Invitations, acceptance, leave, kick, rank assignment, rank permission editing, disband, and guild chat are server-authoritative.
- The four original rank permissions are represented: invite members, remove members, write notices, and use guild skills.
- `%message` sends guild chat. The player target board's Guild action sends an invitation when the caller has permission.
- `UMT2GuildWidget` and `UMT2GuildInviteDialogWidget` contain behavior only; their required `BindWidget` controls must be authored as child Widget Blueprints inside `MT2GameHUD`.
- Guild wars, rankings, land, emblems, notices, donations, progression, treasury, and guild skills remain separate later slices.

## Next Priorities

1. Validate imported map spawn coordinates, group composition, aggressive override, and respawn behavior in PIE and dedicated server.
2. Build the login and character-selection widgets on the completed Gateway/session APIs, then run cooked multi-process integration tests.
3. Replace reward-only loot output with replicated pickup actors, ownership protection, pickup interaction, and inventory insertion.
4. Persist unique inventory/equipment item instances through the coordinator, including bonuses and sockets.
5. Add critical/piercing hits, race bonuses, resistances, hit reactions, death credit, experience, and gold grants.
6. Add weapon-specific combo animation sets, attack timing windows, target-facing, and server range checks at the damage frame.
7. Finish inventory and taskbar UI with Blueprint-authored widget trees and native interaction logic.
8. Add NPC interaction, shops, quests, portals, normalized inventory/guild repositories, and coordinator maintenance APIs.

## Known Gaps

- Spawn and loot import use the configured client source plus the current Italy server-data fallback path. This should become an editor setting before supporting multiple server datasets.
- Group members currently spawn at independent random points inside the regen bounds; original leader-relative formation is not reproduced yet.
- Loot rare-attribute chance is preserved but cannot be applied until unique item instances exist.
- Missing mob VNUMs are retried on later spawn updates but only logged once per spawn component.
- Automated importer tests and dedicated-server gameplay tests are still needed.
- Distributed runtime smoke testing needs a cooked/staged server build. The direct uncooked Development Server executable currently crashes while loading its premade asset registry before GameInstance startup; coordinator code is not reached.
- Client/Gateway traffic and Gateway/coordinator traffic are not encrypted. Production deployment requires TLS or a private VPN/VLAN.
