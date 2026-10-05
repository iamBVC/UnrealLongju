# MT2UE

Standalone UE 4.25 editor plugin for importing unpacked Metin2 client assets.

## Current Scope

- Scans an extracted `client/pack` folder.
- Normalizes extracted pack paths such as `PC/ymir work/pc/...` back to Metin2-style virtual paths such as `d:/ymir work/pc/...`.
- Counts key asset families: textures, `.gr2`, `.msm`, `.msa/.mss`, properties, terrain files, and map text files.
- Adds an editor tab at `Window > MT2UE Importer`.
- Provides a first automated texture import pass for `.dds`, `.tga`, `.jpg`, `.png`, and `.bmp`.
- Resolves map `AreaData.txt` object placements through `YPRT` property CRCs.
- Exports `Saved/MT2UE/StaticObjectReport.csv` for resolved static object inspection.
- Provides an external static mesh conversion hook that calls `converter.exe input.gr2 output.obj`, caches converted mesh files, and imports them into UE.
- Includes `Tools/Gr2ToObj`, a Win32 helper source project that exports rigid `.gr2` static meshes to `.obj/.mtl`.
- Places imported static mesh actors into the current editor level from `AreaData.txt`, with a map-name filter and max actor cap for staged validation.
- Uses a selectable importer browser: choose an object type, refresh entries, tick the entries to process, then import only those entries.
- Prepares selected map terrains into `Saved/MT2UE/LandscapeSources/<map>` with stitched `.r16` heightmaps, stitched tile indices, per-tile weightmaps, and a manifest.
- Creates a UE Landscape actor from the UE-sized heightmap and imports per-tile landscape weight layers through generated `ULandscapeLayerInfoObject` assets.
- Generates a parent landscape material per map from `textureset/*.txt`, using `tile_###` landscape layers that match `tile.raw` indices.
- Generates a landscape material instance per map that binds `Texture_tile_###` parameters to imported terrain textures, then assigns the instance to the generated Landscape.
- Adds a base importer API skeleton with a registry, pipeline, import request/result types, and dedicated importer classes for each planned asset family.

## API Skeleton

The plugin now has a service layer under `Source/MT2UEEditor/Public/API` and `Source/MT2UEEditor/Public/Importers`.

- `FMT2ImportPipeline` owns source scanning, discovery, and import dispatch.
- `FMT2ImportRegistry` maps each import domain to a concrete importer.
- `FMT2ImportRequest`, `FMT2ImportSelection`, `FMT2ImportContext`, `FMT2ImportDiscovery`, and `FMT2ImportResult` provide the shared data contract.
- API implementations now exist for texture imports, audio imports, static mesh conversion/import, static object/property resolution, map terrain source preparation, landscape actor creation, map object actor placement, and landscape material/material-instance generation.
- Map terrain import now also resolves static placements for the selected map, imports missing static mesh assets first, attempts to import material textures referenced by converted `.mtl` files, and then places the map actors.
- Import results now emit more detailed warning/error messages for missing texture references, missing `.mtl` sidecars, failed converter runs, missing imported static meshes, and actor spawn failures.
- Importer stubs remain for characters, skeletons, animations, effects, scripts, and archives because those need deeper format-specific implementation work.
- The importer widget now uses `FMT2ImportPipeline` for the visible scan, discovery, and selected import flow. Older direct widget helper methods remain temporarily as fallback code until the UE 4.25 compile pass validates the service implementations.

## Install

Copy or keep this `MT2UE` folder under a UE4 project's `Plugins` directory, then regenerate project files and build the editor target.

The default source path assumes this repository layout:

```text
RudeMetin2/
  client/pack/
  MT2UE/
```

If the plugin is copied into a different project, edit the source path in the importer tab.

## Next Milestones

1. Compile in UE 4.25 and fix any API signature drift around `ALandscape::Import`, `FLandscapeImportLayerInfo`, and editor-only material instance setters.
2. Remove duplicated direct import logic from the Slate widget once the service path compiles cleanly.
3. Validate texture/static mesh API imports on a small selected batch.
4. Validate one small map terrain import with actor creation disabled first, then with landscape creation enabled.
5. Continue with characters, skeletons, and animations after static meshes and terrain are stable.

## Granny Note

UE4 Editor is 64-bit. The Metin2 client source includes old 32-bit Granny libraries, so we should not link those directly into this editor plugin. `Tools/Gr2ToObj` is intentionally a separate Win32 process; the 64-bit UE4 editor talks to it through command-line conversion.
