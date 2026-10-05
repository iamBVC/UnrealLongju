# Gr2ToObj

Optional legacy Win32 rigid-mesh converter. This is not the current MT2UE skeletal/animation conversion path; the plugin also has its own Win64 Granny converter.

Source review: 2026-10-05.

## Build constraints

`build_win32.bat` requires an x86 Visual Studio developer environment with `cl.exe`. It computes its dependency root as three directories above this tool, currently `Plugins/MT2UE`, and expects:

- `client_src/extern/include`: Granny headers.
- `client_src/extern/library/granny2.lib`: compatible 32-bit library.
- `client/granny2.dll`: compatible runtime, copied beside the output if present.

These legacy SDK paths are not supplied by a normal project checkout. Obtain licensed matching dependencies and review/adapt the script's paths before building. A Win64 DLL cannot satisfy this Win32 executable.

From the project root in an **x86 Developer Command Prompt**:

```bat
cd Plugins\MT2UE\Tools\Gr2ToObj
build_win32.bat
```

Output: `Plugins/MT2UE/Tools/Gr2ToObj/bin/Win32/Gr2ToObj.exe`.

## Usage

```text
Gr2ToObj.exe input.gr2 output.obj [extracted_pack_root]
```

The helper writes OBJ geometry and an MTL sidecar, exporting rigid meshes and best-effort texture references. Providing the extracted pack root lets it resolve local texture files; unresolved references retain source paths.

It does not replace skeletal mesh, skinning, animation, or separate SpeedTree conversion. Validate output coordinates/materials and establish the required rights for SDK dependencies and source assets.
