# Area painting editor

The MT2UE editor module provides a dedicated **Metin2 Areas** mode for viewing
and editing map attributes. It edits the serialized byte grid on
`AMT2MapPresentationActor`, not Landscape texture weights. Original imported
dimensions and cell spacing are retained: server grids normally have 50-unit
cells; client fallback grids retain their own 100-unit cells.

## Open and paint

1. Build `UnrealLongjuEditor` and restart the editor after installing this code.
2. Open an imported map with valid area attributes. Load the Landscape cells you
   want to work on if the map uses World Partition.
3. Choose **Window > Metin2 Area Paint**, or **Metin2 Areas** in the editor Modes
   selector. A visible map is selected automatically for the overlay; hover its
   Landscape to choose the grid for painting.
4. Click a colored bit button to select what to paint. Its adjacent checkbox
   controls visibility independently; other bits remain unchanged.
5. Set the brush radius in world units. **0** paints a single original cell per
   stamp and traces crossed cells when dragged; a positive radius uses a circular
   brush swept between cursor samples.
6. Left-drag to paint. **Shift + left-drag**, or the erase checkbox, clears only
   the selected bit. **Alt + mouse** retains viewport navigation.
7. Use the editor's normal **Undo/Redo** commands. Save the changed map/presentation
   actor using the normal editor save workflow; the tool does not autosave assets.

The status panel shows the map identifier, dimensions, cell spacing, cursor byte,
number of changed cells in the current stroke, and sampled terrain-quad count. Painting is disabled during
PIE. A stroke ends on mouse release, focus loss, a missed Landscape/grid hit, or
switching maps, so it cannot bridge an unseen gap.

## Bits and colors

| Mask | Overlay color | Meaning |
| --- | --- | --- |
| `0x01` | Red | BLOCK / no walk |
| `0x02` | Blue | WATER attribute |
| `0x04` | Green | BANPK / PvP safezone |
| `0x08` | Yellow | Reserved bit 3 |
| `0x10` | Purple | Reserved bit 4 |
| `0x20` | Cyan | Reserved bit 5 |
| `0x40` | Pink | Reserved bit 6 |
| `0x80` | Orange | OBJECT / no walk |

Overlapping visible flags mix their colors. Disable other visibility checkboxes
to inspect a single bit unambiguously. Reserved flags are preserved and editable,
but no new gameplay meaning is assigned to them. WATER painting does **not**
create water meshes or surfaces. Runtime BLOCK/OBJECT movement and BANPK protection
use the same saved grid; see [area runtime behavior](OldGameResearch/SafeZones.md).

## Resolution, storage, and preview

No attribute resampling is performed. Mirrored source axes and half-open cell
ownership match runtime queries. Each stroke records only changed indices and
the selected bit for undo, preserving overlapping flags without copying the
entire grid. No Landscape materials, paint layers, textures, or overlay actors
are created or modified. The overlay material and geometry are editor-only and
transient; saving persists only the existing actor's attribute data.

The colored preview covers the selected map's loaded Landscape across the
viewport, independently of cursor position. Camera-frustum clipping determines
coverage; there is no mouse-radius or 6,000-unit distance cap. This also supports
an overview of the whole map when its terrain is loaded and visible. Geometry
uses adaptive display-only grouping (approximately 16,384 preview quads) to bound
cost; distant groups show the union of their visible bits, while close views can
show individual cells. Painting always addresses original cells, regardless of
preview grouping. Zoom in for exact boundaries.

Terrain geometry and grouped flag summaries are cached. Painting updates the
cached flags; Undo/Redo and Refresh invalidate the preview. Loaded component-bound
changes also invalidate cached geometry. Conservative frustum clipping includes
the full terrain height range, with cell-boundary quantization to reduce rebuilding
for tiny camera movements. No part of the saved grid is cropped by the view.

Terrain height samples are cached. Use **Refresh terrain overlay** after sculpting
or loading additional Landscape cells if the preview is stale. Unloaded terrain
cannot be painted and has no overlay. Terrain collision must be enabled for
cursor picking. This tool does not rebuild navigation, validate spawn/warp
destinations, or export changes back into legacy `server_attr`/`attr.atr` files.

**Reimporting map attributes replaces authored edits.** Save/back up the content
submodule and review its changes before running imports. Publish matching cooked
client/server map data when changing movement attributes; a client-side paint
tool does not replace server-authoritative runtime checks.

## Troubleshooting invisible areas

- **No valid area grid selected:** hover loaded Landscape ground. The painter
  needs an imported, valid attribute grid and matching map bounds; it cannot
  create one for a new/custom map. In Selection Mode, inspect the presentation
  actor's **Attributes > Size**, now visible read-only. The byte array is deliberately
  hidden to avoid expanding millions of entries in Details.
- **Valid dimensions and nonzero cursor flags, but no color:** enable **Show
  colored overlay** and the corresponding bit's visibility checkbox. The status
  should show sampled terrain quads; if zero, load terrain and refresh. A nonzero
  count with missing color indicates a rendering issue rather than missing flags.
- The initial material used Unreal's default **After DOF** translucency pass,
  which the standard PDI view-mesh pass skips. The corrected transient material
  explicitly uses **Before DOF** and requests editor-compositing shaders. Rebuild
  and restart the editor to pick up this correction; wait for shader compilation
  before inspecting it.

## Validation

Automation fixtures live under `Metin2.Editor.AreaPaint`: brush/cell ownership,
continuous strokes, overlapping/unknown flags, targeted erasure, sparse undo/redo,
reflected serialization, and actual editor transactions. Run these alongside
`Metin2.World` and `Metin2.Quests` using the [project test workflow](../README.md).
Viewport interaction, rendering, World Partition streaming, multiplayer, and
cooked builds require separate validation; automated data tests do not establish
visual correctness.

Recorded validation (2026-10-06): `UnrealLongjuEditor Win64 Development` built
successfully with UE 5.7.4. All 36 area-paint/world/quest tests passed in a
NullRHI editor run, including the three new area-paint fixtures. No errors were
reported; existing unrelated deprecation and fixture warnings remain. Log:
`Saved/Logs/AreaPaintTests.log`. No content assets were rewritten. The colored
overlay and interactive brush have not yet been visually verified in a live
viewport, and cooked/multiplayer validation remains separate work.

Overlay correction validation (2026-10-06): Editor Development build succeeded
and all 37 area-paint/world/quest tests passed (`Saved/Logs/AreaPaintOverlayTests.log`).
The added fixture verifies standard-pass translucency, editor-compositing usage,
connected material outputs, and read-only Details visibility for dimensions.
This NullRHI run validates configuration, not the final GPU-rendered overlay.

Viewport coverage validation (2026-10-06): Editor Development rebuilt successfully
and all 38 area-paint/world/quest tests passed (`Saved/Logs/AreaPaintViewportTests.log`).
The new coverage fixture checks frustum filtering/cropping, mirrored cell ranges,
map-bound clipping, an overview of a 2048x2560 grid beyond the old distance cap,
and unchanged native dimensions. The user confirmed the earlier material fix
renders colors in Yongan; this new viewport-wide coverage still requires live
GPU/camera-movement verification. No content assets were modified.
