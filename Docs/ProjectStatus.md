# UnrealLongju Project Status

Source review: 2026-10-05. This inventory is based on current C++, configuration, scripts, and recorded porting audits. It is not a claim that every target or gameplay path was tested today.

## Development baseline

- Windows x64, source-built Unreal Engine 5.7; the locally verified engine version is 5.7.4.
- Editor, Client, Server, and standalone target definitions exist. Client/Server use a Unique build environment.
- Project checkout: `F:\UnrealLongju`. Engine scripts commonly expect `F:\Engine2`; packaging supports `UE_ROOT`.
- `Content/` is the [UnrealLongju-Content](https://github.com/iamBVC/UnrealLongju-Content) submodule. Clone/update recursively and review its asset rights notice. Availability of content in a local checkout is not proof of redistribution permission or fresh-clone completeness.

See the [setup guide](../README.md) for supported commands and configuration caveats.

## Implemented foundations

- Standard Unreal gameplay framework, Enhanced Input, replicated characters, GAS-backed resources, primary/combat stats, appearance, equipment, movement, and animation systems.
- Import services for textures, static/skeletal meshes, character assets, animations, audio, effects, landscapes, map objects, mob/item/skill data, and quest conversion. Generic archive/script services and the separate skeleton domain remain skeleton implementations; skeletal mesh import has its own functional path.
- Saved VNUM registry for mob/item lookup; quest discovery has a separate registry and active-quest manifest.
- Map spawn actor/component, regen/group definitions, mob lifecycle, damage records, experience/loot distribution, and ground pickup/inventory interaction code.
- Server-authoritative inventory/equipment, multi-page grid, sockets/bonuses, commerce/trade, skill learning/casting, critical/penetrating damage, and presentation components.
- Gateway account/login/character flow, coordinator routing and persistence, map registration/heartbeats, admission tickets, and transfer code.
- Coordinator SQLite persistence for characters, items, quests, guilds, and messenger state. Skills, quickslots, and affects use compact player TEXT fields. Schema creation exists; automatic versioned migration does not.
- Coordinator guild core with 15 editable ranks, invitations, membership, guild chat, and owner-client UI state. Local JSON guild code also remains; do not confuse it with cluster authority.
- Messenger friend requests, presence, stored cluster messages, separate whisper UI, and notifications. Coordinator-free fallback is session-local, not persistent.
- Mount definition/component/item foundation, with progression and full presentation fidelity still separate work.
- Imported area-attribute grids and authoritative BANPK safezones. Player chat reports `safezone area` / `unprotected area` on initial status and transitions. BLOCK/OBJECT now constrain player/mob movement through an attribute-aware character movement component; water generation and AI route planning around these cells remain separate work. See the [2026-10-06 validation](OldGameResearch/SafeZones.md).

Code presence does not establish full original-game parity. Check system-specific documents and tests before enabling content in a release.

## Quest porting baseline

The latest recorded import in [QuestPortingStatus](OldGameResearch/QuestPortingStatus.md) reports:

| Measure | Recorded value |
| --- | ---: |
| Parsed scripts / refreshed Blueprints | 238 / 237 |
| Imported triggers / generated nodes | 6,595 / 27,761 |
| Unconverted statements | 482 |
| Failed gates / unsupported triggers | 67 / 27 |
| Total diagnostic entries | 576 |

The recorded function-result-list validation passed 32 quest/safezone tests. These results are historical evidence from that pass, not tests rerun during this documentation review. Zero unconverted statements has **not** been achieved, and accepted bindings can still have fidelity gaps.

## Remaining priorities

1. Continue quest control-flow/scoping, resumable expression/result-list propagation, table-field assignment, function values, and rejected trigger/gate work.
2. Build real authoritative backends for unresolved dungeon/instance, cube, safebox/mall, pet, marriage, guild-war/building, and horse APIs rather than accepting placeholders.
3. Audit already accepted quest bindings and unfinished quest triggers; conversion counts alone do not establish correctness.
4. Implement water placement, and validate attribute-based movement with live multiplayer, AI routes, and authored spawn/warp destinations.
5. Validate current content in cooked client/server builds, multiplayer travel, persistence/reconnect, and complete quest playthroughs.
6. Establish reproducible engine/dependency revisions, explicit database migration/backup procedures, and reviewed production security/release configuration.

## Important limitations

- Legacy datasets and map-name fallbacks are machine-specific in parts of the importer.
- Some content conversions and runtime paths need visual, networked, and cooked validation.
- The old uncooked Server startup crash report is historical; it was not reproduced or cleared by this documentation review. Use the documented editor or cooked/staged workflow and capture new evidence for current failures.
- Cluster/client traffic is not secured merely by tokens. The internal TCP protocol is authenticated but unencrypted.
- Engine source is not pinned to a commit in this repository.
- Asset ownership, licensing, and distribution permission must be reviewed independently of the software license.
