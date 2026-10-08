# Map water

Implementation: 2026-10-07, UE 5.7.4.
Visual-water paint mode added: 2026-10-08.

## Setup

Assign **Project Settings > Metin2 > Metin2 Gameplay > Water > Water Material**
to a surface material or material instance. The reference is stored as
`WaterMaterial` in `Config/DefaultGame.ini`, under
`[/Script/Metin2.MT2GameplaySettings]`. An unset material deliberately renders
no water; the client logs a warning instead of displaying a default opaque plane.
The mesh supplies upward normals, tangents, and continuous world-aligned UV0.
`WaterUVTileSize` controls UV repetition in centimetres; `WaterSurfaceOffset`
defaults to one centimetre above the imported surface height.

A suitable water shader can provide transparency, normals, reflections, and
animated waves. This system generates surface geometry; it does not create a
material or implement swimming, buoyancy, underwater effects, or fluid simulation.
Merged rectangles preserve cell coverage but are not a densely tessellated wave
mesh; animated pixel normals are suitable, while large vertex-displacement waves
may require additional tessellation and seam handling.
Ensure the configured material and its dependencies are cooked: config-only soft
references are not a substitute for a cooking rule. Existing `/Game/ymir_work`
assets are in an always-cook directory; for a custom water asset elsewhere, add
its directory under Packaging > Additional Asset Directories to Cook.

Loaded editor maps restore their preview when the water component registers.
Changes to the water material, UV size, and offset in Project Settings refresh
loaded editor previews automatically. **Refresh Water Rendering** remains
available on the map-presentation actor. Restart clients after changing
configuration in a packaged build.

## Baked data and import

`server_attr` supplies the water bit (`0x02`), not surface elevation. The legacy
client stores elevation in each terrain tile's `water.wtr`: a packed seven-byte
header (magic 5426, 128 by 128 cells, layer count), byte layer indices, and a
little-endian 16- or 32-bit height table. Layer `255` denotes no water. Heights
are multiplied once by the map's `Setting.txt` `HeightScale`, matching terrain
conversion. The format filename is configured through `Part_water_wtr` in the
project path catalogue.

The map importer bakes the client's **visual water-layer coverage**, independently
of the server's gameplay water/fishing flags. A walkable bridge is not a hole in
the visual surface merely because the server water bit is clear. The native
128-cell tile grid and height boundaries are preserved; server attributes retain
their original resolution and values. Equal-height cells merge into rectangles
within culling chunks without overlapping translucent surfaces.

`WaterShorelinePaddingCells` (Project Settings > Water; default 1, range 0..4)
extends visual coverage under riverbanks by a bounded number of native water
cells. At the usual source scale one cell is 200 centimetres. Terrain occlusion
and the material's depth fade define the visible bank instead of exposing the
gameplay mask's stair-stepped cutout. Padding does not propagate repeatedly,
replace source heights, or join equally near surfaces at different levels. It is
a **bake setting**: re-bake maps after changing it. It cannot hide a genuine
raised terrain ridge above water; that requires inspecting/sculpting the terrain,
not disabling depth testing on the water material.

`WaterGridSize` and `WaterRectangles` are serialized in the map-presentation actor.
All visual tile files must be available and valid. Water rendering validates the
native grid dimensions and rectangles, without bake-version fields or gameplay
attribute checksums. Safezone/no-walk edits do not invalidate visual water.
The obsolete attribute-mask compatibility path has been removed; all 28 launcher
maps already contain independent visual-water bakes. Existing saved geometry
fields retain their names and types. Rebuild/cook packaged targets after changing
reflected source code rather than mixing old cooked packages with new binaries.

For existing maps, bake only water without reimporting landscapes or changing
lighting/music/spawn settings:

```powershell
& "$EngineRoot\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" $ProjectFile `
  -run=MT2BakeMapWater "-Maps=/Game/Maps/Game/yongan" `
  "-MapRoot=$LegacyMapRoot" -nullrhi -nosound -unattended -nop4
```

`$LegacyMapRoot` contains map directories such as `metin2_map_a1`, with their
`Setting.txt` and numbered tile folders. `-Maps=` accepts comma-separated map
packages. **The default command saves map/presentation packages**; close the
editor and review the Content submodule diff. `-Inspect` reads sources and
reports the candidate geometry without saving. `-Verify` compares saved data
against a fresh bake without saving. Failed maps are not saved.

Re-bake after changing source water files or shoreline padding. Area-painter
water-bit edits affect gameplay attributes, not the independent visual water
layer; they neither remove water under crossings nor define new surface heights.
Visual water can now be edited directly in Unreal without changing legacy files.

## Editing visual water

Open **Window > Metin2 Visual Water Paint**, or choose **Metin2 Visual Water**
in the editor mode selector. The mode uses a loaded map-presentation actor;
if the level contains several maps, select the intended actor in the Outliner
and click **Use selected map presentation**.

* Set **Water height** to the surface's world Z in centimetres (before the global
  `WaterSurfaceOffset`). Press **E** over existing water to sample its height.
* Set the brush radius; zero edits one native cell. **LMB** adds water or changes
  the height of covered cells; **Shift+LMB**, or **Erase water**, removes it.
* **Pick on water-height plane** allows painting beneath bridges and other
  objects. Uncheck it to pick the loaded landscape instead. Terrain still
  occludes surfaces below the riverbed; painting does not sculpt terrain.
* **Ctrl+Z/Y** undo/redo a stroke. Save the map and changed actors normally.

Preview changes use the configured Water Material and refresh only affected
render chunks during strokes. Edits retain the native visual grid and existing
serialized `WaterGridSize`/`WaterRectangles` format. Server water/fishing,
safezone and no-walk flags remain unchanged: edit those in **Metin2 Areas**.
No legacy water files are required for this editor mode. Reimporting or running
the water bake commandlet **replaces authored visual-water edits** with source
data; `-Verify` will intentionally report a mismatch after such edits.

## Runtime

Clients build transient, non-replicated procedural mesh chunks from the saved
rectangles once at map startup. Editor construction also supports previews.
Dedicated servers do not generate meshes or load the water material. There is
no geometry replication, per-frame rebuild, collision generation, or navigation
obstacle creation. Attribute-based safezones and no-walk rules are unchanged.
Mesh chunks are replaced on refresh and destroyed with their owning actor.

## Validation

Before cleanup, all eight water tests passed with a real RHI in
`Saved/Logs/WaterVisualCoverageTests.log`. Coverage included independent visual
water at crossings, bounded shoreline overlap, height boundaries, unchanged
gameplay flags, component/settings lifecycle, both legacy height formats, and
above/below GPU depth readback. All 28 launcher maps were baked and verified
against their source data (`Saved/Logs/WaterVisualMapBake.log`,
`Saved/Logs/WaterVisualMapVerification.log`). The user subsequently confirmed
the water's appearance, including the shoreline and crossing corrections.

The three water-test source files and obsolete bake-version/checksum logic were
removed at the user's request after validation. Unrelated automation tests remain
in place. The manual `-Inspect` and read-only `-Verify` commandlet modes are
retained for authoring diagnostics. Saved geometry property names and types are
unchanged; cleanup does not require asset resaving. These records do not establish
Shipping or freshly cooked packaged-runtime validation.

Cleanup validation (2026-10-07): Editor, Client, and Server Win64 Development
built successfully. All 28 existing saved bakes loaded and matched source data
in the read-only `Saved/Logs/WaterCleanupAssetVerification.log` run, without
resaving assets. The configured-path audit and diff whitespace checks passed.
