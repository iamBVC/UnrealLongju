# UnrealLongju project status

This inventory describes the current implementation. Code presence does not establish complete Metin2 parity, production readiness or multiplayer capacity.

## Development baseline

- Windows x64 with source-built Unreal Engine 5.7; the local engine version is 5.7.4.
- Editor, Client, Server and standalone targets. Client/Server use a Unique build environment.
- Build scripts use project-relative paths; the engine location can be configured through `UE_ROOT` where supported.
- `Content/` is the [UnrealLongju-Content](https://github.com/iamBVC/UnrealLongju-Content) submodule. Clone/update recursively and use the parent-pinned revision. Asset availability and redistribution permission are separate requirements.

See the [setup guide](../README.md) for supported workflows.

## Implemented systems

- Unreal gameplay framework, Enhanced Input, replicated characters, GAS-backed resources, primary/combat stats, appearance, equipment and animation.
- Import services for textures, static/skeletal meshes, character assets, animations, audio, effects, landscapes, map objects, mob/item/skill data and quests. Generic archive/script services and the separate skeleton domain remain incomplete.
- Saved mob/item VNUM registry, separate quest discovery and active-quest manifest.
- Map spawning, regen/group definitions, mob lifecycle, damage attribution, experience/loot distribution, world pickup and inventory interaction.
- Server-authoritative inventory/equipment, multi-page grid, sockets/bonuses, commerce/trade, skill learning/casting and critical/penetrating damage.
- [Mob movement and replication](OldGameResearch/NpcMovementReplication.md): timed movement segments, client interpolation, configurable legacy state deadlines, active-region scheduling and forced gameplay-state updates. Unreal collision, leash rules and simulation-region activation remain deliberate differences from the legacy game.
- Animation-event-driven [knockback](OldGameResearch/Knockback.md), mob fall/recovery locks and server-side hit timing.
- [Player duels](Duels.md): challenge, acceptance, combat, revenge, expiry and teardown. Safezones remain authoritative; agreed deaths bypass the implemented PK penalties. Arena/tournament matches are separate.
- [Damage feedback](OldGameResearch/DamageFeedback.md): outgoing hit-type colors and red incoming damage numbers on the victim's owning client.
- [Fishing](Fishing.md): server-attribute spot validation, timed reeling, configurable weighted rewards, rod proficiency/fisherman refinement, numeric-socket persistence, character animations, animation-notify sounds and a locally animated hook float.
- Independent chat and loot/info histories. Player/admin chat and admin-command feedback use chat; loot, experience, fishing and other system notifications use the info log.
- Gateway login/character flow, coordinator routing/persistence, map registration, heartbeats, admission tickets and per-character transfers.
- Coordinator SQLite persistence for characters, items, quests, guilds and messenger state. Skills, quickslots and affects use compact player TEXT fields. Schema creation exists; automatic versioned migration does not.
- Coordinator guild core with editable ranks, invitations, membership, guild chat and owner-client UI state. Local JSON guild code is a distinct fallback, not cluster authority.
- Messenger friend requests, presence, stored cluster messages and separate whisper UI. Coordinator-free fallback is session-local.
- Mount definition/component/item foundation; progression and complete presentation fidelity remain incomplete.
- [Gameplay map attributes](OldGameResearch/SafeZones.md): authoritative BANPK safezones and BLOCK/OBJECT movement checks. Transition messages use the info log. AI routing around blocked cells requires separate implementation.
- [Area painter](AreaPainting.md): native-resolution bit editing, viewport-wide translucent terrain overlays, independent visibility, paint/erase and sparse undo/redo.
- [Visual water](Water.md): independent coverage/heights stored in map-presentation data, client-generated non-colliding chunks and configurable material. Visual-water painting is separate from gameplay water flags.
- [PIE startup and travel](PIETravel.md): editor-selected map/placement, entry-warp suppression and editor-only travel lifecycle handling.

## Quest conversion

The importer and runtime support a substantial Lua-like subset, but zero unconverted statements has not been established. Accepted bindings can also lack their authoritative gameplay backend.

Use `Saved/MT2QuestConversionReport.txt` from an import of the intended dataset to inspect unconverted statements, failed gates and unsupported triggers. A rejected gate can conceal unsupported operations in its body; counts are not a percentage of gameplay ported. See [quest porting status](OldGameResearch/QuestPortingStatus.md).

## Remaining priorities

1. Complete quest scoping, resumable expression/result-list propagation, function values, table-field assignment and unresolved triggers/gates.
2. Implement authoritative dungeon/instance, cube, safebox/mall, pet, marriage, guild-war/building and remaining horse backends.
3. Audit accepted quest bindings and complete end-to-end quest playthroughs.
4. Validate AI routes, authored spawn/warp destinations, water materials, animation and editor streaming with the intended content.
5. Measure multiplayer CPU/bandwidth and test latency, packet loss, travel, reconnect and cooked client/server builds. A 200-player map is a capacity target, not an established result.
6. Pin engine/dependency revisions and establish production security, database migration and backup/retention procedures.

## Operational limits

- External datasets and importer defaults require configuration for each development environment.
- Internal cluster TCP traffic is authenticated but unencrypted.
- Engine source is not pinned to a commit in this repository.
- Asset ownership, licensing and distribution permission require review independently of the software license.
