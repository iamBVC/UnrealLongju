# MT2UE

Bundled Unreal Engine 5 editor tooling for importing authorized, extracted Metin2 data into UnrealLongju. The project baseline is UE 5.7 (local engine 5.7.4), not UE 4.25.

Source review: 2026-10-05. The plugin descriptor declares the editor-only `MT2UEEditor` module and an Interchange dependency. It is enabled for the project editor target; do not install a separate copy.

## Setup and use

Follow the [project setup guide](../../README.md), initialize the `Content` submodule, generate project files, and build `UnrealLongjuEditor` with the compatible source engine.

Open **Window > MT2UE Importer**. Configure the actual extracted client source path; another developer's legacy folders are not included in this checkout. Refresh discovery, select an asset family and a small batch, then inspect warnings and generated assets before importing a whole dataset.

Imports may update/save assets and levels in the content submodule. Back up authored content, review both parent and submodule changes, and establish conversion/publication permissions before processing third-party assets.

## Import services

The service layer lives under `Source/MT2UEEditor/Public/API` and `Public/Importers`:

- `FMT2ImportPipeline`: scanning, discovery, and dispatch.
- `FMT2ImportRegistry`: domain-to-importer registration.
- Shared request, selection, context, discovery, and result types.
- Texture and audio imports; static mesh conversion; skeletal/character/animation paths; effects; property/object placements; terrain sources, landscapes, and landscape materials.
- Mob, item, skill, registry, UI-generation, and quest tooling are exposed through dedicated editor workflows/commandlets.

The separate **Archives**, **Scripts**, and **Skeletons** import services still report skeleton-only implementations. This does not mean skeletal meshes or animations are wholly unimplemented: those have separate conversion/import paths. Provide extracted sources; the generic archive service does not extract legacy packs.

Terrain import handles stitched heightmaps/weights, landscapes, static placements, spawn/map presentation metadata, and area attributes. BANPK safezones and BLOCK/OBJECT character-movement constraints are supported at runtime; attribute-driven water placement remains separate work. See [SafeZones](../../Docs/OldGameResearch/SafeZones.md).

**Window > Metin2 Area Paint** opens the dedicated full-resolution attribute editor. Paint or erase any of the eight bits, toggle their colored Landscape overlays, and use normal Undo/Redo and map saves. This does not use Landscape paint weights or resample the grid. The transient preview uses native Landscape triangles/current terrain LOD with a small upward offset and original flag textures per component. See [Area Painting](../../Docs/AreaPainting.md) for controls, validation limits, and reimport caveats.

## Quest conversion

```text
UnrealEditor-Cmd.exe <project.uproject> -run=MT2ImportQuests -Source=<quest-directory> -ClientSource=<extracted-client-root> -Destination=/Game
```

This writes assets. `-Verify=BP_Quest_<name>` inspects an imported quest after conversion; it is not a read-only mode. Review `Saved/MT2QuestConversionReport.txt` and the [current audit](../../Docs/OldGameResearch/QuestPortingStatus.md). Unsupported statements/gates/triggers remain; successful import is not proof of gameplay parity.

## Native format bridges

The Granny converter contains a Win64 in-process conversion path and uses the bundled Win64 Granny runtime. Preserve its tracked third-party dependency files and review their licenses.

`Tools/Gr2ToObj` is an older optional Win32 rigid-mesh helper, not a prerequisite for the current skeletal/animation importer. Its build script expects an external legacy SDK layout absent from a normal checkout; see [its README](Tools/Gr2ToObj/README.md).

Legacy SpeedTreeRT is x86-only. The editor launches the separate bundled Win32 `SpeedTreeToObj` helper rather than loading that SDK into the 64-bit editor; see [its README](Tools/SpeedTreeToObj/README.md).

## Validation

Inspect a small authorized import before bulk conversion. Recorded commandlet/test/build results apply to their stated historical passes. This documentation review did not run imports or compile the plugin.
