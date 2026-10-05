# SpeedTreeToObj

Bundled Win32 bridge for legacy SpeedTreeRT 1.6 `.spt` assets. The legacy SDK is x86-only, so Unreal Editor launches this helper and imports its OBJ output instead of loading the SDK in the 64-bit editor process.

The converter exports highest-detail branches, fronds, and expanded leaf cards plus a material manifest. MT2UE imports the referenced branch/composite textures and creates masked, two-sided foliage materials.
