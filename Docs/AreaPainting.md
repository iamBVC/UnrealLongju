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
number of changed cells in the current stroke, and native overlay-component count. Painting is disabled during
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
entire grid. No Landscape material assets, paint layers, or source textures are
modified, and no overlay actors are created. Preview materials and flag textures
are transient/editor-only; saving persists only the existing actor's attribute data.

The colored preview covers the selected map's loaded Landscape across the
viewport, independently of cursor position. Unreal's native Landscape editor-tool
pass draws the same triangles, height morphing, and current terrain LOD as the
Landscape, with a constant **+4 world-unit Z offset**. This replaces the earlier
corner-sampled preview quads, which could cut through hills between samples.
Depth testing remains enabled, so opaque buildings still occlude the overlay.

Visible flagged cells blend at **50% opacity** using premultiplied alpha
compositing, so the Landscape textures remain visible below their tint.
Unflagged cells, hidden bits, and points outside the grid contribute zero color
and zero opacity. Explicit alpha compositing also keeps the opacity override
active in the engine's Substrate material path.
The material deliberately disables `bUsedWithEditorCompositing`: that shader
permutation forces output alpha to 1 in UE 5.7's weighted-Z editor compositing
path, making even clear cells opaque black. Landscape vertex-factory support
comes from the transient Landscape material instance, not that flag.

Each loaded Landscape component uses a nearest-filtered, single-mip, one-byte
flag texture containing its original source-grid cells. Flags are decoded and
colored per pixel; no byte truncation, attribute resampling, or camera-dependent
grouping occurs. Painting uploads only changed rectangular regions. Texture
coordinates follow mirrored map coordinates and remain fixed as the camera moves.
The former adaptive-quad budget/LOD hysteresis no longer controls this preview.
Zoom in to inspect small cells that are subpixel at a distance.

The native pass follows Landscape sculpt/LOD updates. **Refresh terrain overlay**
rebuilds transient bindings/textures if data was reimported or the preview is stale.
Prior editor-tool materials are restored on mode exit, disabling the overlay, or
PIE. These references are nontransactional, transient, and excluded from PIE
duplication. Unloaded terrain
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
  should show native overlay components; if zero, load terrain and refresh. A nonzero
  count with missing color indicates a rendering issue rather than missing flags.
- The initial material used Unreal's default **After DOF** translucency pass,
  which the standard PDI view-mesh pass skips. The corrected transient material
  explicitly uses **Before DOF** and requests editor-compositing shaders. Rebuild
  and restart the editor to pick up this correction; wait for shader compilation
  before inspecting it.

The terrain-conforming pass targets ordinary imported heightfield Landscapes.
Custom Landscape material WPO, Nanite-specific geometry, and unusual rendering
paths require separate verification. A component exceeding GPU texture-dimension
limits reports an error rather than reducing attribute resolution.

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

Camera-alignment correction (2026-10-06): view-dependent grouping origins and
arbitrary strides were replaced by source-origin-aligned, power-of-two partitions
with LOD hysteresis. Editor Development built successfully, and all 39
area-paint/world/quest tests passed (`Saved/Logs/AreaPaintAlignmentTests.log`).
The new fixture verifies that overlapping source cells retain the same group
after a camera pan, close views refine to individual cells, and overview geometry
stays in budget. Visual confirmation of the reported camera-motion artifact is
still required; these tests do not reproduce GPU/temporal-AA behavior.

Native-surface correction (2026-10-06): the user confirmed camera stability, then
reported terrain intersecting the coarse preview triangles. The custom quad
renderer and its now-unused grouping helper were replaced by Landscape's native
editor-tool surface pass. Editor Development built successfully; all 39 revised
area/world/quest fixtures passed without errors (`Saved/Logs/AreaPaintSurfaceFinalTests.log`).
The native-surface fixture also passed with a real RHI in an offscreen editor,
including successful Landscape shader compilation (`Saved/Logs/AreaPaintSurfaceShaderFinalTests.log`).
It verifies original-byte tile copies, partial-update bounds, +4-unit world-Z WPO,
depth testing, Landscape shader support, prior-material restoration, and transient
PIE-excluded references. Earlier failed runs exposed a test-fixture Outer error
and concurrent startup registry-save contention; final validation was isolated
and clean. The Content submodule has no changes. Visual hill clearance, sculpting,
streaming, large-map performance, Nanite/custom-WPO cases still require live testing.

Opacity correction (2026-10-06): after the user reported opaque colors and black
unflagged ground, the preview switched to explicit alpha-composite blending with
premultiplied color. Flagged cells use 0.5 opacity; all other cells use zero.
Editor Development built, all 39 regressions passed (`Saved/Logs/AreaPaintOpacityTests.log`),
and the native-surface shader test passed on the real RHI
(`Saved/Logs/AreaPaintOpacityShaderTests.log`). Checks now cover active opacity
and the effective Landscape/GPU blend mode, rather than only expression connections.
The Content submodule remains unchanged; final viewport appearance needs a retest.

Follow-up alpha correction (2026-10-06): the user confirmed the previous blending
change was insufficient. Engine shader inspection found that editor-compositing
usage overwrote output alpha with 1; this flag is now disabled while native
Landscape shader support, depth testing, and +4-unit clearance are retained.
The regression fixture now reads actual GPU pixels over a known background,
checking clear cells, 50/50 flagged tint, and hidden flags. Its Canvas-only draw
removes vertex displacement to avoid screen-space clipping; Landscape WPO and
vertex-factory compilation are checked separately. This is not a live Yongan
viewport test.
Editor Development rebuilt successfully and all 39 area/world/quest tests passed
with the real RHI (`Saved/Logs/AreaPaintAlphaReadbackFinalTests.log`), including
the pixel-readback assertions and native Landscape shader compilation. The first
readback run failed because +4-unit WPO clipped the Canvas tile; only the fixture
was corrected, without changing production terrain clearance. No Content assets
were changed. Restart Unreal to replace existing transient preview materials;
live Yongan viewport verification remains required.
