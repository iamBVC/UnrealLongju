# UnrealLongju Project Status

Source review: 2026-10-05; area-tooling update: 2026-10-06. This inventory is based on current C++, configuration, scripts, and recorded porting audits. It is not a claim that every target or gameplay path was tested today.

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
- Player duels use the existing target UI for challenge, acceptance, combat and
  revenge, with replicated server-authoritative agreements and teardown/idle cleanup.
  Safezones remain authoritative; agreed deaths bypass current PK karma/equipment
  penalties. Arena/tournament matches are separate. See [player duels](Duels.md).
- Damage feedback routes server-resolved basic/skill hits to both player participants:
  outgoing numbers retain their hit-type colours, while received hits show red above
  the victim on that player's owning client. The existing unreliable client RPC,
  local-only floating actor, movement/fade, and outgoing hit sounds are retained;
  incoming notifications do not replay attacker-side audio.

- Gateway account/login/character flow, coordinator routing and persistence, map registration/heartbeats, admission tickets, and transfer code.
- Coordinator SQLite persistence for characters, items, quests, guilds, and messenger state. Skills, quickslots, and affects use compact player TEXT fields. Schema creation exists; automatic versioned migration does not.
- Coordinator guild core with 15 editable ranks, invitations, membership, guild chat, and owner-client UI state. Local JSON guild code also remains; do not confuse it with cluster authority.
- Messenger friend requests, presence, stored cluster messages, separate whisper UI, and notifications. Coordinator-free fallback is session-local, not persistent.
- Mount definition/component/item foundation, with progression and full presentation fidelity still separate work.
- Imported area-attribute grids and authoritative BANPK safezones. Player chat reports `safezone area` / `unprotected area` on initial status and transitions. BLOCK/OBJECT now constrain player/mob movement through an attribute-aware character movement component; water generation and AI route planning around these cells remain separate work. See the [2026-10-06 validation](OldGameResearch/SafeZones.md).
- Dedicated editor-only [area painter](AreaPainting.md) with native-resolution bit editing, colored terrain overlays, independent visibility, paint/erase brushes, and sparse undo/redo. The current transient preview uses native Landscape triangles/current terrain LOD with +4-unit world-Z clearance and original byte textures; water surfaces remain separate work.

Area-tooling validation (2026-10-06): Editor Development build succeeded and all 36 area-paint/world/quest tests passed (`Saved/Logs/AreaPaintTests.log`). This verifies data editing, editor transactions, and regression fixtures, not live viewport appearance or cooked multiplayer behavior.

Subsequent overlay correction (2026-10-06): explicit standard-pass translucency and editor-compositing shaders, read-only attribute dimensions in Details, and terrain-preview diagnostics were added. Editor Development rebuilt successfully; all 37 tests passed (`Saved/Logs/AreaPaintOverlayTests.log`). Final live GPU appearance still requires verification.

Viewport coverage update (2026-10-06): the user confirmed colors render in Yongan. The overlay now follows visible loaded terrain across the camera view rather than a mouse-centered radius; preview flags are cached, and saved resolution is unchanged. Editor Development built and all 38 tests passed (`Saved/Logs/AreaPaintViewportTests.log`). Viewport-wide coverage/performance still needs live verification.

Camera-alignment correction (2026-10-06): preview groups now use fixed source-origin alignment and nested power-of-two strides with LOD hysteresis. Editor Development built and all 39 tests passed (`Saved/Logs/AreaPaintAlignmentTests.log`). The reported camera-motion artifact still needs a live visual retest.

Terrain-surface update (2026-10-06): the user confirmed camera alignment, but corner-sampled quads cut through hills. The preview now uses the native Landscape editor-tool triangles/current terrain LOD with a +4-unit world-Z offset and original byte textures per component. The prior custom-quad grouping/hysteresis path is superseded. Base Landscape assets and saved attribute dimensions are unchanged; transient tool state is restored on exit/PIE. Editor Development built and all 39 revised area/world/quest fixtures passed (`Saved/Logs/AreaPaintSurfaceFinalTests.log`). GPU shader and live viewport validation are recorded in [Area Painting](AreaPainting.md).

Initial opacity correction (2026-10-06): explicit premultiplied-alpha compositing configured visible flags at 50% opacity and clear/hidden cells at zero. Editor Development, all 39 regressions, and the real-RHI native shader fixture passed (`Saved/Logs/AreaPaintOpacityTests.log`, `Saved/Logs/AreaPaintOpacityShaderTests.log`), but the user's next screenshot confirmed the rendered result was still opaque. These checks did not read rendered pixels.

Follow-up alpha correction (2026-10-06): Unreal's editor-compositing shader permutation forced output alpha to 1. The preview now disables that usage flag while retaining native Landscape shader support and clearance. Actual GPU pixel-readback coverage was added for clear, half-opacity, and hidden cells; see [Area Painting](AreaPainting.md) for validation and scope.

Code presence does not establish full original-game parity. Check system-specific documents and tests before enabling content in a release.

PIE guild-map travel correction (2026-10-06): the TestMap/gm_guild_build startup
quest exposed a client World Partition teardown assertion and a destination-cell
visibility-report race. PIE-only lifecycle handling, pending-travel guards, and
bounded visibility-report deferral now preserve the transition without disabling
native network validation. Editor Development built and all 43 relevant tests
passed, including real networked TestMap-to-Yongan travel and clean PIE shutdown
(`Saved/Logs/PIEGuildTravelFinalTests.log`). The test used FXAA to isolate a separate
existing TSR shader ensure. See [PIE map travel](PIETravel.md) for scope and limitations.

PIE startup placement correction (2026-10-06): native Player Start/current-camera
placement and the open map are preserved. Automatic entry-event warp nodes are
suppressed only in their PIE execution context; subsequent explicit warps and
production spawning remain enabled. PIE no longer starts the town fall-recovery
timer. The old automatic-city-travel fixture and its two INI path entries were
retired. Editor Development built and all 45 regressions passed, including real
TestMap launches in both startup modes (`Saved/Logs/PIESpawnVerifiedTests.log`).
The path audit passed with 250 catalog entries, and Content stayed clean. See
[PIE map travel](PIETravel.md) for test scope and Blueprint-hook caveats.

Incoming damage validation (2026-10-06): Editor Win64 Development built; all eight
combat/config/world regressions passed (`Saved/Logs/IncomingDamageFinalTests.log`).
The native-world fixture checks mob and PvP basic/skill hits, one received popup
per hit, post-defense amounts, red colour, existing outgoing colours, and fading.
The final run used NullRHI: live viewport appearance, remote-client delivery, and
cooked targets were not exercised. The configured-path audit passed; no asset or
INI changes were needed.

Duel logic validation (2026-10-06): Editor Development built and all 49 Metin2
regressions passed (`Saved/Logs/DuelVerifiedRegressionTests.log`). New native-world
tests cover agreement/revenge/expiry and lethal-hit karma handling; remote-client
duel UI, late joining, and cooked server/client execution remain unverified.
See [player duels](Duels.md) for the legacy comparison and deliberate safety limits.

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
