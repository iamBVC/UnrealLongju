# Gr2ToObj

Small command-line converter for the first MT2UE static mesh pass.

It reads a Metin2 `.gr2` with the bundled Granny 2.11 headers/library and writes:

- `output.obj`
- `output.mtl`

The converter exports rigid mesh geometry with the same PNT3322 vertex layout used by the original client: position, normal, UV0, and UV1. It also writes material groups and best-effort diffuse/opacity texture references from Granny materials.

## Build

This tool must be built as **Win32/x86**, because the bundled Granny library in `client_src/extern/library` is 32-bit.

From a Visual Studio developer command prompt:

```bat
cd /d C:\Users\brian\Desktop\RudeMetin2\MT2UE\Tools\Gr2ToObj
build_win32.bat
```

Expected output:

```text
MT2UE\Tools\Gr2ToObj\bin\Win32\Gr2ToObj.exe
```

## Usage

```bat
Gr2ToObj.exe input.gr2 output.obj [extracted_pack_root]
```

The UE4 plugin calls converters as:

```text
converter.exe "input.gr2" "output.obj" "client/pack"
```

So this tool can be pasted directly into the plugin's converter executable field.

## Notes

- This is for static props/buildings/trees first, not skeletal meshes or animations.
- Geometry is exported in the source Granny coordinate space for the first validation pass.
- OBJ/MTL texture paths are resolved to local extracted files when `extracted_pack_root` is provided. If resolution fails, the original Granny material path is preserved.
