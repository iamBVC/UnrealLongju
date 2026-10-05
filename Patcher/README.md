# UnrealLongju Patcher

A small standalone launcher that keeps a player's client up to date, then starts the game. It fetches
a manifest from the website, compares each local file by size + SHA-256, downloads only what changed
(verifying each download), and launches `UnrealLongjuClient.exe`.

## How the pieces fit together

```
Scripts\BuildShipping.bat ──▶ Saved\StagedShipping\WindowsClient\   (cooked client + pakchunkN + manifest.json)
                              │
        upload contents ──────┼────────────▶  https://mt2ue.iambvc.it/client/   (your website)
                              │                     ├─ manifest.json
                              │                     ├─ UnrealLongjuClient.exe
                              │                     ├─ UnrealLongju/Content/Paks/pakchunk0-Windows.pak / .ucas / .utoc
                              │                     └─ … every other client file …
                              ▼
   Player runs  UnrealLongjuPatcher.exe  (placed at the client root)
        → downloads manifest.json → diffs local files → downloads changed ones → launches the game
```

- `Scripts\BuildShipping.bat` builds and copies `UnrealLongjuPatcher.exe` into the staged client root, then
  generates `manifest.json`
  (`Patcher\GenerateManifest.ps1`). It lists every staged client file with size + SHA-256, a version
  string, the download `baseUrl`, and the launch command.
- File paths in the manifest are relative to the client root, so the patcher must sit **at the client
  root** (next to `UnrealLongjuClient.exe`, `Engine\`, `UnrealLongju\`).

## Build the patcher

Requires the **.NET 8 SDK** (bundled with recent Visual Studio 2022).

```bat
Patcher\build.bat
```

Output (self-contained, no runtime needed on players' PCs):

```
Patcher\bin\Release\net8.0-windows\win-x64\publish\UnrealLongjuPatcher.exe
```

## Release workflow

1. `Scripts\BuildShipping.bat` increments the shared numeric client/server `ProjectVersion`, cooks
   both targets, and writes `WindowsClient\manifest.json` with the same version.
   - Override the hosting URL / gateway before running, if needed:
     ```bat
     set PATCH_BASE_URL=https://mt2ue.iambvc.it/client/
     set GATEWAY_ADDRESS=mt2ue.iambvc.it:11000
     Scripts\BuildShipping.bat
     ```
2. Upload the **entire** `Saved\StagedShipping\WindowsClient\` folder (including `manifest.json`) to
   `https://mt2ue.iambvc.it/client/`, preserving the folder structure.
3. Distribute the staged `WindowsClient` folder. Players start `UnrealLongjuPatcher.exe`; the Shipping
   client rejects direct launches. Later runs download only files that changed.

## Configuration

- **Manifest URL** is baked into the patcher (`PatchConfig.cs`, `ManifestUrl`). Change it there only
  if you move the manifest. Everything else — download base URL, which exe to launch, and the gateway
  address it's launched with — comes from the manifest, so you can change those server-side without
  reshipping the patcher.
- **Gateway address**: set once via `GATEWAY_ADDRESS` at build time; it ends up in
  `manifest.launch.args` and is passed to the client on launch (the client auto-connects to it).

## Notes / limitations

- **The patcher does not update itself** (a running exe can't overwrite its own file). If you ever
  change the patcher, players download the new `UnrealLongjuPatcher.exe` manually. It's deliberately kept
  out of the manifest.
- Downloads are verified by SHA-256; a corrupt or tampered file is rejected and never overwrites the
  good copy. Combined with your pak signing, a tampered pak also fails at load time in the client.
- Serve the files over **HTTPS**. Make sure your web server sends the correct `Content-Length` and
  doesn't gzip the `.pak/.ucas/.utoc` (they're already compressed).
