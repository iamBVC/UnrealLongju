# UnrealLongju Patcher

Standalone Windows launcher that downloads a manifest, compares local files by size and SHA-256, downloads changes, and starts `UnrealLongjuClient.exe`.

Source review: 2026-10-05. No release build, download, or deployment was performed during this review.

## Build

Install the .NET 8 SDK; Visual Studio alone does not guarantee it is available. From the parent project root:

```bat
Patcher\build.bat
```

Self-contained Win64 output:
`Patcher/bin/Release/net8.0-windows/win-x64/publish/UnrealLongjuPatcher.exe`.

Players do not need to install the .NET runtime for this published executable.

## Release workflow

1. Select the compatible source-built UE engine through `UE_ROOT`. Review `Config/DefaultCrypto.ini`.
2. If intentionally changing the shared release/network version, run `Scripts/IncreaseVersion.bat`.
   Packaging reads `ProjectVersion` from `Config/DefaultGame.ini`; it does **not** increment it.
3. Configure actual hosting/gateway endpoints and the baked manifest URL in `PatchConfig.cs`.
4. Run `Scripts/BuildShipping.bat`. It builds/cooks/stages both targets, builds/copies the patcher,
   and runs `GenerateManifest.ps1` for the client.
5. Upload the complete authorized `Saved/StagedShipping/WindowsClient` distribution to the configured patch host, preserving its relative paths. Do not publish private `Saved/Symbols` archives.
6. Players start the patcher from the staged client root, next to `UnrealLongjuClient.exe`, `Engine/`, and `UnrealLongju/`.

Example in Command Prompt; replace all host placeholders before use:

```bat
set UE_ROOT=F:\Engine2
set PATCH_BASE_URL=https://YOUR_PATCH_HOST/client/
set GATEWAY_ADDRESS=YOUR_GATEWAY_HOST:11000
Scripts\BuildShipping.bat
```

## Manifest and endpoints

`manifest.json` records relative file paths, sizes, SHA-256 hashes, a version string, download `baseUrl`, and the launch executable/arguments. The patcher must reside at the client root.

- `PatchConfig.cs::ManifestUrl` is baked into the executable. Changing the download base URL does not change this URL.
- Download `baseUrl`, executable, and gateway arguments come from the manifest.
- Current packaging defaults are `https://mt2ue.iambvc.it/client/` and `rm2.zapto.org:11000`.
  They are not verified deployment endpoints.
- `PatchConfig.cs` currently uses placeholder manifest/fallback host values. Replace these and rebuild before distribution.
- `MT2UE_PATCHER_ARGS_OVERRIDE` lets the patcher replace launch arguments for local testing.
- Normal Shipping clients require the inherited `MT2UE_PATCHER_TOKEN` and matching launch argument.
  `StartAllShipping.bat` has a separate local-development token path; it launches the client directly.
  A launch token is not account authentication or an anti-cheat trust boundary.

## Limitations and security

The patcher does not update itself; distribute a replacement manually. Its own executable is excluded from the generated manifest.

Size/hash checks detect mismatches against the fetched manifest, not an attacker-controlled manifest. No signed-manifest trust scheme is documented here. Use HTTPS, protect publishing credentials and hosting, and validate actual pak signing/encryption before making tamper-resistance claims.

Review third-party asset permissions before distributing any client content; the software license and rights notice do not grant redistribution rights for converted legacy assets.
