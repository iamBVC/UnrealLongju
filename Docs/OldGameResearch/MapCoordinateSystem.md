# Old Game → UE Map Coordinate Mapping

Sources studied: the importer pipeline (`MT2MapTerrainBuilder.cpp`, `MT2MapTerrainImporter.cpp`,
`MT2MapObjectImporter.cpp`) cross-checked against the placement of objects/spawns.

## How the imported map is laid out in UE

- **World bounds** (`MT2MapTerrainImporter` line ~134): the map occupies UE
  `X ∈ [0, WorldSizeX]`, `Y ∈ [-WorldSizeY, 0]` — i.e. the +X / −Y quadrant.
  `WorldSizeX = MapSizeX * 128 * CellScale`, likewise for Y.
- **Landscape actor** is spawned at `(0, -TotalWorldY, 0)` with positive XY scale, so its
  heightmap grid grows into that same quadrant.
- **Objects** (`ConvertMetin2LocationToUnreal`): UE `(WorldSizeX - Position.X, Position.Y, Position.Z+bias)` when map width is known; unknown-width placements retain X.
  These are the authoritative gameplay coordinates (server positions, spawns,
  warps, persistence all use them), so everything else must match them.

## Axis convention (Metin2 → UE)

- Source X maps to `WorldSizeX - X`, so increasing source X moves toward UE **−X**.
- Metin2 **south** → UE **−Y**  (the landscape builder reverses the grid's Y row order to achieve this)
- Height/Z → UE +Z (centered at 32768 in the r16 heightmap, `LandscapeZScale = 128`)

## The bug that was fixed

The terrain builder reversed only the **Y** grid axis (`ReversedDestY`) when writing the
landscape heightmap and weightmaps. Metin2's raw per-cell grid also runs opposite to the
Unreal landscape on the **east-west (X)** axis, so the terrain came out **mirrored left-right**
relative to the correctly-placed objects, spawns and minimap markers. That mismatch is why
object/NPC positions and the minimap looked wrong even though their coordinates were correct.

Fix (`MT2MapTerrainBuilder.cpp`): also reverse X — `ReversedDestX = LandscapeWidth-1-DestX` —
in **both** the heightmap and the weightmap loops, so terrain and painted textures move
together and now align with the object/gameplay coordinate space.

**Requires re-importing maps** for the change to take effect (the builder runs at import time).

## Everything else mirrored to match (UE X = WorldSizeX − Metin2X)

After the terrain X flip was confirmed correct, the same mirror was applied to everything placed
by Metin2 X so it lands on the correct side of the flipped terrain. The import-time items **require a
re-import**; the minimap/marker items are runtime widget code (rebuild the game module only):

- **Static objects** — `MT2MapObjectImporter::ConvertMetin2LocationToUnreal` (WorldSizeX threaded
  onto each placement).
- **Mob / NPC / metin spawns and spawn exclusions** — `MT2MapTerrainImporter` spawn parser.
The minimap/fullmap are rendered **fully mirrored on both X and Y** (per the game's original map view).
The whole view — tiles and markers — flips together, so alignment is preserved. Keep the two X mirrors
in lock-step; changing one without the other slides markers off their tiles.

- **Minimap / fullmap tiles** (`MT2MapViewWidget::AddMapTiles`) — positioned with the sector column
  mirrored: `X = (MapCells.X − 1 − Cell.X) · 128` (`Cell.X` itself is also stored mirrored by the
  importer, `MapSizeX-1-cx`, but that is a separate concern — the widget mirror is what pairs with the
  markers). **No per-tile image flip**: mirroring the column layout already reverses within-tile content
  as part of the single full-view horizontal flip.
- **Actor markers (NPCs / mobs / player)** — positioned by `MT2MapViewWidget::WorldToMap`, which flips
  **both** axes: `U = (WorldMax.X − X)/Extent`, `V = (WorldMax.Y − Y)/Extent`. The X flip mirrors markers
  the same way the tile column is mirrored, so they stay aligned; the Y flip puts north up.
- **Minimap player arrow** — rotation is `Yaw − 90` (both axes flipped ⇒ screen heading
  `(−cos yaw, −sin yaw)`; arrow art points north/up).
- **Minimap centering** — the player is pinned under the `PlayerArrow` widget's true on-screen centre
  (`UMT2MinimapWidget::GetViewCenter` converts the arrow centre into map-canvas local space), not the
  map-canvas geometric centre. The arrow is a separate widget, so a resized minimap would otherwise
  drift the zoom pivot off the player.

## Current source recheck and validation boundary

- Object rotations reflect headings through `180 - source angle`; spawn direction conversion includes the same reflection.
- `LoadTownSpawnLocations` mirrors X and scales its source coordinates by 100; warp resolution uses imported region origins and widths. See the [warp routing audit](QuestPortingStatus.md#gameplay-bindings).
- These source changes require reimport for serialized placements. This documentation review did not visually confirm every object, heading, town spawn, or minimap tile.
