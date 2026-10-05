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
the existing reliable owner-client system-chat RPC on initial possession and
on status changes. Remaining in the same area does not repeat the message.
No replicated per-frame status or client-authored protection decision is used.

Block and water flags are preserved, but walkability and water placement are
not implemented by this change.

## Import and verification

The `MT2ImportMapAttributes` editor commandlet accepts comma-separated package
paths via `-Maps`, the legacy locale directory via `-LocaleRoot`, and the
extracted client root via `-ClientRoot` (containing `ymir work`). It changes
only presentation attributes, preserving map environment and spawn settings.
`-Verify` reloads maps and compares serialized dimensions and every byte against
the selected source without saving.

The recorded import used client fallback for five maps: devils_catacomb, monkeydungeon_01,
monkeydungeon_02, monkeydungeon_03, and red_forest. The first four have different
server directory names; red_forest has different server/client map dimensions.
The remaining maps use server data.

`Metin2.World.SafeZones` tests axis mirroring, half-open bounds, both attacker
and defender protection, restoration of the existing PvP policy after leaving,
and removal of protection when a map unregisters. Multiplayer chat presentation,
dedicated-server execution, and cooked builds require separate playtesting.

Recorded alignment-pass recheck (2026-10-05): UnrealLongjuEditor Win64 Development build succeeded, and
`Metin2.World.SafeZones` plus all 25 quest regression tests passed without unexpected
errors (`Saved/Logs/SafeZonesAlignmentFinal.log`). The reported compile error was
not reproduced in this target. Live multiplayer transition-chat display remains unverified.

Documentation review: 2026-10-06. The earlier 25-quest-test count belongs to that alignment run; the later function-result-list pass records 32 quest/safezone tests. No tests, map reimport, or multiplayer chat check was performed in this documentation review. Revalidate source selection after changing datasets or content revisions.
