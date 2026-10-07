# Map water

Implementation: 2026-10-07, UE 5.7.4.

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

The map importer bakes water whenever it imports attributes, including when it
keeps an existing presentation actor. It preserves the exact attribute-grid
footprint (50-unit server cells or the existing client-attribute fallback), height
boundaries, holes, and mixed flags. Adjacent equal-height cells merge into
rectangles within 128-attribute-cell culling chunks; no mask downsampling occurs.
`WaterGridSize`, `WaterRectangles`, and an attribute checksum are serialized in
the map-presentation actor. Missing or malformed heights fail the bake without
inventing terrain-following or zero-height water.

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

Re-bake after editing the attribute grid. A checksum mismatch disables stale
water and logs a warning. New water flags require a valid height layer in the
corresponding legacy tile; the bit alone cannot define a new surface elevation.

## Runtime

Clients build transient, non-replicated procedural mesh chunks from the saved
rectangles once at map startup. Editor construction also supports previews.
Dedicated servers do not generate meshes or load the water material. There is
no geometry replication, per-frame rebuild, collision generation, or navigation
obstacle creation. Attribute-based safezones and no-walk rules are unchanged.
Mesh chunks are replaced on refresh and destroyed with their owning actor.

## Validation

`Metin2.World.Water` covers exact water-cell coverage, overlap with BANPK, holes,
height boundaries, chunk boundaries, atomic failures, mirrored map coordinates,
triangle winding, UVs, both height-table formats, malformed data, material
assignment, collision/replication flags, repeated component rebuilds, and cleanup.
NullRHI validates data and component setup, not the visual appearance of a user's
water material. Rebuild/cook packaged targets after source or map-data changes.

Recorded validation (2026-10-07): Editor, Client, and Server Win64 Development built successfully;
all four water tests passed in `Saved/Logs/WaterRegressionTests.log`. The existing
world regression group also reported nine successful tests in
`Saved/Logs/WaterWorldRegressionTests.log`, including safezones and no-walk.
These NullRHI results do not establish rendered PIE or water-material appearance.
The existing
launcher-map batch baked 22 of 28 maps, and all 22 saved bakes matched a fresh
read-only verification (`Saved/Logs/WaterMapVerification.log`). Map and
presentation-actor asset changes are in the Content submodule.

Six maps were left unchanged because at least one water-flagged attribute cell
had no valid surface height in the available source: `joan`, `bokjung`,
`yongbi_desert`, `red_forest`, `snakefield`, and `nephrite_bay`. See
`Saved/Logs/WaterMapBake.log` for the first offending cell in each map. Resolving
these source/attribute mismatches or authoring explicit replacement heights is
required before those maps can be baked; no default elevation was substituted.
The selected production water material, visual appearance, and packaged runtime
have not been tested.

Visibility correction (2026-10-07): triangle indices now follow Unreal's
upward-facing procedural-grid winding instead of a right-handed cross-product
assumption. Loaded editor maps rebuild their water on component registration;
water Project Settings changes refresh previews without polling. Unregistration
clears generated chunks and removes settings delegates, and preview chunks are
not duplicated into PIE. Editor, Client, and Server Development builds passed.
All six water tests passed with a real RHI in
`Saved/Logs/WaterVisibilityDepthTests.log`, including above/below GPU depth
readback with a two-sided control and editor settings/registration checks.
Earlier color-capture attempts did not validate visibility; depth readback
isolates geometry/culling from material color and lighting. This does not prove
the visual appearance of the selected production translucent material.
