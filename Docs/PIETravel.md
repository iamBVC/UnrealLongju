# PIE map travel

PIE uses seamless server travel for quest warps, portals, and administrative map
changes. This moves every player connected to that PIE server; it is not the
coordinator's production per-character map-server transfer.

## Editor-selected startup (2026-10-06)

Starting PIE keeps the map open in the editor and Unreal's native placement:
the Play menu's **Default Player Start** or **Current Camera Location**, including
Play From Here. Initial spawning no longer invokes the imported town-spawn repair
unless an explicit portal/travel destination is pending.

Login, Enter, and initial Letter quest events still execute, but their warp nodes
preserve the editor-selected spawn. This context follows suspended entry dialogs
and deferred state-entry/letter refreshes. Later NPC interactions, quest events,
portals, and administrative warps remain enabled. Packaged/server entry warps are
unchanged. Custom Blueprint hooks must honor `bPreservePIESpawn` if they perform
their own movement instead of using the native Warp node.

The map-bounds/ground fall-recovery timer is disabled in PIE so it cannot replace
a deliberately selected editor position with a town spawn. Normal physics still
applies: starting in mid-air falls, and invalid/colliding starts retain Unreal's
native spawn handling. Production fall recovery remains enabled.

## Guild-map startup crash (2026-10-06)

`TestMap` contains the `gm_guild_build` presentation (map index 200). The imported
`entry_event_map` quest previously warped its player to Yongan at PIE startup.
Login and Enter both requested that warp. The client then asserted in
`FWorldDataLayersEffectiveStatesAccessor::Get` during
`UWorldPartition::Uninitialize`, called by seamless-travel source-world cleanup.
Both editor maps contain a WorldDataLayers actor; the failure concerns its
client-world reference during teardown, not an absent editor-map actor.

The project workaround is limited to PIE/editor builds:

- `AMT2PlayerController::PreClientTravel` uninitializes an initialized source
  client partition while its WorldDataLayers reference is still available, before
  native seamless travel prepares/cleans up worlds. Later normal cleanup sees an
  already-uninitialized partition. Other world types and non-seamless travel are
  unchanged.
- Quest/portal/admin entry points reject or coalesce further requests while a
  PIE world has pending travel. The first request wins; a second quest warp does
  not overwrite an in-flight transition.
- A fast client can report destination `/Memory` streaming cells before the
  server switches worlds. The controller temporarily buffers the two sealed
  engine visibility RPCs via reflected event dispatch, with a 1,024-report bound.
  `PostSeamlessTravel` replays them through the native batched RPC, retaining its
  validation and level checks. It does not disable network validation. Other
  events, non-PIE worlds, schema mismatches, and overflow use normal dispatch.

No engine source or map assets are patched. Packaged client/server behavior,
replicated schemas, and serialized map attributes are unchanged. This workaround
depends on the locally verified UE 5.7.4 travel and visibility-RPC lifecycle;
recheck it when upgrading the engine.

## Regression tests

Run from an idle editor with saved work. Spawn integration loads the existing
`EditorStartupMap` configuration and starts/stops standalone PIE. Play settings
are a transient copy, not edits to the user's settings. No existing map actors are
replaced or saved. If no Player Start is loaded, a transient fixture is added and
removed after PIE; it is never saved into the map.

- `Metin2.World.PIETravelGuard`: pending-request handling and non-PIE isolation.
- `Metin2.World.PIEVisibilityQueue`: capture/replay of single and batched reflected
  parameters, release of storage, idle dispatch, and native validation flags.
- `Metin2.World.PIEEntryWarp`: entry-context relocation suppression, later explicit
  warps, and non-PIE isolation, using an in-memory world/collision fixture.
- `Metin2.Editor.PIESpawn.PlayerStart` and `.CurrentCamera`: native launch placement
  and map identity retained across the automatic entry events and recovery intervals.
  Movement is disabled on the test pawn to isolate placement from gravity/input;
  these check XY placement, not movement or camera rendering.

The old automatic-city-travel smoke fixture was retired because that startup
behavior is no longer desired. Its two path-catalog INI entries were removed;
retained tests do not need dedicated regression-map configuration.

Startup validation: Editor Win64 Development built successfully; all 45 config,
area, world, quest, and spawn tests passed in `Saved/Logs/PIESpawnVerifiedTests.log`.
Both real standalone PIE launch modes retained TestMap and their selected XY
placement through Login/Enter, using a temporary Player Start where none existed.
The configured-path audit passed (250 entries, 443 call sites); Content remained
clean. Networked startup and cooked targets were not rerun for this spawn change.

Historical crash-fix validation: Editor Win64 Development built successfully; all 43 config, area,
world, quest, and PIE-travel tests passed with a real RHI in
`Saved/Logs/PIEGuildTravelFinalTests.log`. The configured-path audit passed.
The Content submodule remained clean. Earlier test runs reproduced the original
assertion and the subsequent `MissingLevelPackage` disconnect, rather than
treating the disappearance of the assertion as complete validation.

The existing TSR global-shader precache ensure is separate. This test process
used `r.AntiAliasingMethod 1` (FXAA) to isolate that renderer issue; project
anti-aliasing settings were not changed. Normal user-viewport appearance with TSR
and cooked multiplayer travel were not validated by this test.
