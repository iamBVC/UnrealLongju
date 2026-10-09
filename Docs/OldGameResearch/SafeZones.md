# Legacy area attributes and safezones

## Source formats

Legacy `sectree.h` defines block as bit 0, water as bit 1, and BANPK as bit 2
(`0x04`). `sectree_manager.cpp::LoadAttribute` reads `server_attr`: two
little-endian 32-bit sectree dimensions followed by row-major, length-prefixed
LZO blocks. Each block contains 128x128 little-endian DWORD attributes;
each cell covers 50 legacy units. The importer rejects extended flags rather
than silently truncating them to bytes.

Client `PRTerrainLib/Terrain.cpp::LoadAttrMap` reads `attr.atr`: a packed
little-endian WORD header (magic 2634, width 256, height 256), followed by
256x256 byte attributes. `GameLib/MapOutdoor.cpp::GetAttr` uses
HALF_CELLSCALE (100 units). A client tile covers the same 25600-unit extent
as 512x512 server cells; client fallback data retains its native resolution.

The terrain importer prefers server data only when its dimensions match the
imported map. Missing or differently sized server data uses complete client
tile grids and produces a warning. No missing tiles are filled with invented
flags, and mismatched server data is not stretched.

## Runtime behavior

`AMT2MapPresentationActor` serializes the grid with the level and registers it
with `UMT2MapAttributeSubsystem` during BeginPlay, including dedicated servers.
Registration is removed at EndPlay; the subsystem keeps weak actor references.
The presentation actor is non-spatially loaded in World Partition maps.

Queries use the existing mirrored map coordinates: source X increases from
WorldMax.X toward WorldMin.X, and source Y increases from WorldMax.Y toward
WorldMin.Y. Bounds are half-open in source coordinates, and Z is ignored.
Lookup is constant-time per registered map and does not search world actors.

`AMT2PlayerCharacter::IsPvPEnabledAgainst` rejects PvP if either participant
has BANPK at their current position, before checking duels, karma, empires,
or aggressive mode. Existing basic-attack and skill damage paths recheck this
policy at the actual hit. PvE policy is unchanged: this is the requested
PvP-safezone scope, although legacy `battle_is_attackable` also checked BANPK
for non-player combat.

The authoritative player sends `safezone area` or `unprotected area` through
the reliable owner-client system-notification RPC on initial possession and
on status changes. Messages appear in the info log. Remaining in the same area does not repeat the message.
No replicated per-frame status or client-authored protection decision is used.

No-walk movement enforcement checks the imported attribute grid. `BLOCK` (`0x01`) and
`OBJECT` (`0x80`) constrain ordinary swept character movement; `WATER` and
`BANPK` alone do not block walking. Water placement remains unimplemented.

`UMT2CharacterMovementComponent` traverses every source-grid cell crossed by the
character center, including corner-adjacent cells, and reports a virtual wall
to the existing capsule movement/slide solver. It cannot skip a blocked strip
merely because the endpoint is clear. The check follows legacy center-position
attributes; it is not a new capsule-radius rasterization or navmesh rebuild.

Players and the specialized mob movement component share the constraint. Server
movement is authoritative; autonomous prediction uses the same cooked grid.
Simulated-proxy corrections, unswept repositioning, and explicit teleports are
not intercepted. A character placed inside a blocked region may escape it, but
cannot re-enter blocked ground after reaching a clear cell. Authoritative spawn
and warp destinations still need valid placement; this does not add destination
search or AI path planning around attribute walls.

Maps without a registered, valid attribute grid retain ordinary UE movement;
they are not implicitly declared walkable by the legacy data. Import/verify the
grid before relying on no-walk or safezone protection in a release.

## Import and verification

For authored edits, use the dedicated [Area Painting editor](../AreaPainting.md).
It edits the original-resolution grid directly and shows all eight bits as
colored terrain overlays without modifying Landscape texture layers. Attribute
reimport replaces these edits; save/back up authored content first.

The `MT2ImportMapAttributes` editor commandlet accepts comma-separated package
paths via `-Maps`, the legacy locale directory via `-LocaleRoot`, and the
extracted client root via `-ClientRoot` (containing `ymir work`). It changes
only presentation attributes, preserving map environment and spawn settings.
`-Verify` reloads maps and compares serialized dimensions and every byte against
the selected source without saving.

Verify the selected source directory and map dimensions for each imported dataset.

`Metin2.World.SafeZones` tests axis mirroring, half-open bounds, both attacker
and defender protection, restoration of the existing PvP policy after leaving,
and removal of protection when a map unregisters. Multiplayer chat presentation,
dedicated-server execution, and cooked builds require separate playtesting.


`Metin2.World.NoWalk` checks BLOCK/OBJECT movement, mirrored coordinates, boundaries, invalid grids and player/mob component integration. Use live multiplayer tests to validate prediction/correction and authored routes; grid tests are not pathfinding.
