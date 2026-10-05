# UnrealLongju

An Unreal Engine recreation of Metin2, combining a C++ multiplayer runtime, Blueprint-based game content, dedicated-server services, and legacy asset import tools.

The Git repository, Unreal project and build targets are named **UnrealLongju**. Use `UnrealLongju.uproject` when generating project files, building, or launching the editor.

> **Development status:** this is an actively developed port, not a complete replacement for the original game. Imported content and implemented systems do not imply full legacy gameplay or quest parity. See the [project status](Docs/ProjectStatus.md) and [quest porting audit](Docs/OldGameResearch/QuestPortingStatus.md) for scope and known gaps; check each document's update date.

## Contents

- [Project overview](#project-overview)
- [Requirements](#requirements)
- [Clone the repository](#clone-the-repository)
- [Set up Unreal Engine](#set-up-unreal-engine)
- [Build and open the project](#build-and-open-the-project)
- [Run a local multiplayer environment](#run-a-local-multiplayer-environment)
- [Legacy data and asset imports](#legacy-data-and-asset-imports)
- [Build packaged clients and servers](#build-packaged-clients-and-servers)
- [Validation and troubleshooting](#validation-and-troubleshooting)
- [Repository layout](#repository-layout)
- [Submodules and dependency management](#submodules-and-dependency-management)
- [Contributing](#contributing)
- [License](#license)

## Project overview

The project separates authoritative gameplay from client presentation:

- **Runtime:** Unreal gameplay framework, replicated characters and inventory, Gameplay Ability System integration, combat, skills, quests, and persistent player state.
- **Server services:** a coordinator for persistence and routing, a gateway for login and character selection, and map servers for world simulation.
- **Persistence:** coordinator-owned SQLite storage; map servers communicate with the coordinator instead of owning separate character databases.
- **Editor tooling:** the bundled `MT2UE` plugin imports and converts supported legacy asset formats and gameplay definitions into Unreal content.
- **Distribution tooling:** Windows client/server packaging scripts and a standalone .NET patcher.

For implementation details, start with [Runtime Architecture](Docs/Architecture.md), [Distributed Server Architecture](Docs/DistributedServerArchitecture.md), and [Persistence Architecture](Docs/PersistenceArchitecture.md).

## Requirements

The documented development workflow targets **Windows x64**.

| Dependency | Purpose / requirement |
| --- | --- |
| Git | Clone the repository and initialize any recorded submodules. |
| Unreal Engine source build | UE **5.7** project baseline; the local development engine reports **5.7.4**. Use the maintainer-approved engine revision. |
| Visual Studio 2022 | The project targets explicitly select the VS2022 compiler. Install the C++ game-development and desktop-development workloads, MSVC v143 tools, and a compatible Windows SDK. |
| .NET 8 SDK | Required to build the Windows patcher. Unreal's own tools also use the engine-provided .NET tooling. |
| PowerShell and Windows command shell | Used by the repository's `.ps1` and `.bat` scripts. |
| Sufficient disk space | Allow for engine source/dependencies, game assets, compiled targets, shader caches, and separate client/server staging directories. |

Epic's [UE 5.7 Visual Studio setup guide](https://dev.epicgames.com/documentation/en-us/unreal-engine/setting-up-visual-studio-development-environment-for-cplusplus-projects-in-unreal-engine?application_version=5.7) recommends VS2022 17.14 and lists the matching MSVC/SDK requirements. Follow the engine's toolchain diagnostics if your installed compiler is rejected.

**Use a source-built engine for the full workflow.** The client and server targets use `TargetBuildEnvironment.Unique`, which requires building engine modules for those targets. A Launcher-installed engine should not be treated as a drop-in replacement for this configuration.

The repository does **not currently pin an engine source commit**. The GUID in `EngineAssociation` is a local engine registration, not a reproducible dependency lock. Confirm the intended engine checkout with the maintainer before upgrading or substituting a different engine build.

## Clone the repository

The examples below use the existing default layout. Replace the drive and paths if needed:

```text
X:\
  Engine2\                 Unreal Engine source checkout
    Engine\
  UnrealLongju\                  This repository
    UnrealLongju.uproject
```

Run these commands in **PowerShell** from the parent directory where the project should live:

```powershell
Set-Location X:\
git clone --recurse-submodules https://github.com/iamBVC/UnrealLongju.git UnrealLongju
Set-Location .\UnrealLongju
```

The `Content` directory is the [UnrealLongju-Content](https://github.com/iamBVC/UnrealLongju-Content.git) submodule. Recursive cloning initializes the content revision recorded by this repository. A ZIP download is not an equivalent submodule-aware checkout. The initial content revision contains documentation only, not playable game assets; a fresh clone cannot run the game until an authorized, compatible asset set is available.

For an existing checkout cloned without recursive initialization:

```powershell
git submodule update --init --recursive
```

After saving or committing your local changes, update the parent repository and synchronize its recorded dependencies:

```powershell
git pull --ff-only
git submodule sync --recursive
git submodule update --init --recursive
```

These commands check out the submodule commits selected by the parent repository; they do not intentionally upgrade dependencies to their latest remote branches. See [Git's submodule documentation](https://git-scm.com/docs/git-submodule).

## Set up Unreal Engine

### 1. Obtain the compatible engine source

Keep the engine outside this repository, for example at `X:\Engine2`. Obtain the maintainer-approved source checkout, including any required engine modifications. If using Epic's repository, complete its GitHub-access requirements first; see [Downloading Unreal Engine Source](https://dev.epicgames.com/documentation/unreal-engine/downloading-source-code-in-unreal-engine?lang=en-US).

An example clone is:

```powershell
Set-Location X:\
git clone https://github.com/EpicGames/UnrealEngine.git Engine2
```

**Before setup or compilation, select the approved UE 5.7 revision.** Do not build whatever default branch the clone happens to select. Replace the following placeholder with the revision provided by the maintainer; no engine tag or fork is pinned by this project today:

```powershell
git -C X:\Engine2 checkout REPLACE_WITH_APPROVED_ENGINE_REVISION
```

Access to this game repository does not automatically grant access to Epic's engine repository.

### 2. Download engine dependencies and build the editor

From the engine source root:

```powershell
Set-Location X:\Engine2
.\Setup.bat
.\GenerateProjectFiles.bat
.\Engine\Build\BatchFiles\Build.bat UnrealEditor Win64 Development -WaitMutex
```

Run each command only after the previous command succeeds. The initial dependency download and engine compilation can take substantial time and storage. Epic also documents the [source-build workflow](https://dev.epicgames.com/documentation/unreal-engine/building-unreal-engine-from-source?lang=en-US).

### 3. Configure your project paths

Return to the project and set session-local paths:

```powershell
Set-Location X:\UnrealLongju
$EngineRoot = 'X:\Engine2'
$ProjectRoot = (Get-Location).Path
$ProjectFile = Join-Path $ProjectRoot 'UnrealLongju.uproject'
$env:UE_ROOT = $EngineRoot
```

`UE_ROOT` is supported by the Development and Shipping packaging scripts. It is **not** a universal override for every script:

- `GenerateConsoleProjectFiles.bat` expects a sibling `..\Engine2` source checkout.
- `RunEditor.bat` also resolves the sibling `..\Engine2` directory.
- `Scripts\StartAllLocal.bat` and `Scripts\StartClientLocal.bat` explicitly use `X:\Engine2`.

If your layout differs, use the explicit engine commands below and review launcher paths before running those convenience scripts. Opening the editor executable directly avoids depending on another developer's `EngineAssociation` registration.

## Build and open the project

Close running editor instances before a full C++ build, especially after reflected header changes.

Generate project files using your selected engine:

```powershell
& "$EngineRoot\Engine\Build\BatchFiles\GenerateProjectFiles.bat" "-project=$ProjectFile" -game -engine
```

Alternatively, with the sibling-directory layout shown above:

```powershell
.\GenerateConsoleProjectFiles.bat
```

Build the project editor target:

```powershell
& "$EngineRoot\Engine\Build\BatchFiles\Build.bat" UnrealLongjuEditor Win64 Development "-Project=$ProjectFile" -WaitMutex -NoHotReloadFromIDE
```

Then open the editor:

```powershell
& "$EngineRoot\Engine\Binaries\Win64\UnrealEditor.exe" $ProjectFile
```

You can also open the generated `UnrealLongju.sln` in Visual Studio and build **Development Editor / Win64**. The first editor launch may spend time compiling shaders and preparing derived data.

### Available targets

| Target | Purpose |
| --- | --- |
| `UnrealLongjuEditor` | Editor development, asset import, and automation. |
| `UnrealLongjuClient` | Dedicated player client. |
| `UnrealLongjuServer` | Headless dedicated server; supports coordinator, gateway, and map roles. |
| `UnrealLongju` | Standalone game target for development. |

For compile-only client/server builds, substitute the target name:

```powershell
& "$EngineRoot\Engine\Build\BatchFiles\Build.bat" UnrealLongjuClient Win64 Development "-Project=$ProjectFile" -WaitMutex
& "$EngineRoot\Engine\Build\BatchFiles\Build.bat" UnrealLongjuServer Win64 Development "-Project=$ProjectFile" -WaitMutex
```

Compilation alone does not cook or stage the game's content; use the packaging workflow for distributable executables.

## Run a local multiplayer environment

The cluster consists of a **coordinator**, a **gateway**, and one or more **map servers**. Clients connect to the gateway first; it routes authenticated characters to the appropriate map server.

For the current full map list, build a packaged Development client/server pair and run:

```powershell
.\Scripts\BuildDevelopment.bat
.\Scripts\StartAllDevelopment.bat
```

The launcher starts the coordinator, configured map servers, gateway, and one client. It can start many processes: review `Scripts\StartAllPackaged.bat` before using it on a resource-constrained machine. To launch another client against the local gateway:

```powershell
& "$ProjectRoot\Saved\Staged\WindowsClient\UnrealLongjuClient.exe" '127.0.0.1:11000' -windowed -ResX=1280 -ResY=720
```

The uncooked alternative is `Scripts\StartAllLocal.bat`, with `Scripts\StartClientLocal.bat` for an additional client. Those scripts have hard-coded engine and older map-package paths; verify that the referenced maps exist in your checkout before using them. They are not interchangeable with the packaged launcher's current map list.

Local packaged defaults include:

| Setting | Default |
| --- | --- |
| Public host | `127.0.0.1` |
| Gateway game port | `11000` |
| Coordinator service port | `11099` |
| Map game ports | Sequential, starting at `11001` |
| Map voice ports | Sequential, starting at `11101` |
| Database directory | `Saved\LocalServer\Database` |

The launchers enable registration and set a fixed development cluster token. **These are local development defaults, not production configuration.** Before exposing any service, review authentication, registration policy, bind/public addresses, firewall rules, ports, and secret handling. Keep the coordinator and database private, and use the documented environment-variable token mechanism instead of committing production credentials.

See [Command-Line Parameters](Docs/CommandLineParameters.md) for role flags, map identifiers, database options, admission tickets, and connection settings. Back up persistent databases independently of Git and before migrations or deployment changes.

## Legacy data and asset imports

The `MT2UE` editor plugin is included and enabled in `UnrealLongju.uproject`; its editor module builds with `UnrealLongjuEditor`. It is not a separate runtime plugin installation step.

Existing Unreal content and external legacy import sources are different dependencies. For reimports, provide your own authorized extracted client data, server locale/map data, and quest sources. Do not assume a fresh clone includes the original archives or another developer's `D:\Giochi\...` source directories.

Open **Window > MT2UE Importer** to configure the source path and import a selected batch. Begin with a small selection, inspect warnings and generated assets, and review the diff before importing a full corpus. Imports can save or overwrite generated content; use a clean, backed-up working tree.

For example, the quest commandlet accepts explicit source overrides:

```powershell
$QuestSource = 'X:\LegacyData\server\share\locale\italy\quest'
$ClientSource = 'X:\LegacyData\client-extracted'
& "$EngineRoot\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" $ProjectFile -run=MT2ImportQuests "-Source=$QuestSource" "-ClientSource=$ClientSource" -Destination=/Game -nullrhi -nosound -unattended -nop4 -stdout
```

Replace those sample paths with actual datasets. This is an **asset-writing import**, not a read-only validation command. `-Verify=BP_Quest_<name>` inspects a generated quest after importing; it does not skip the import. Review `Saved\MT2QuestConversionReport.txt` as well as the process log: a successful import is not proof of zero unconverted statements or complete gameplay behavior.

Format and system notes live in [Docs/OldGameResearch](Docs/OldGameResearch). The plugin's own README contains historical UE4-era instructions; use this project's UE5 target configuration and current source as the authority for engine setup.

## Build packaged clients and servers

### Development

From the project root, with `UE_ROOT` set to your engine source root:

```powershell
$env:UE_ROOT = $EngineRoot
.\Scripts\BuildDevelopment.bat
```

The script builds, cooks, and stages both targets into:

```text
Saved\Staged\WindowsClient\
Saved\Staged\WindowsServer\
```

Treat staging directories as generated output. Packaging scripts update them and remove legacy-branded leftovers; do not store unrelated files there.

### Shipping and patcher

Shipping builds require the configured crypto file (`Config\DefaultCrypto.ini`) and the patcher's .NET 8 SDK. Review signing/encryption configuration and release endpoints before running:

```powershell
$env:UE_ROOT = $EngineRoot
$env:PATCH_BASE_URL = 'https://YOUR_PATCH_HOST/client/'
$env:GATEWAY_ADDRESS = 'YOUR_GATEWAY_HOST:11000'
.\Scripts\BuildShipping.bat
```

The host values above are placeholders, not usable deployment endpoints. The Shipping script has its own endpoint defaults; override them for your environment. Outputs are staged under `Saved\StagedShipping\WindowsClient` and `WindowsServer`; private debug symbols are archived under `Saved\Symbols` and must not be shipped with the client.

The Shipping workflow also builds `UnrealLongjuPatcher.exe` and generates the client manifest. Normal Shipping launches go through the patcher; development launchers have a separate local launch-token path. The patcher's manifest URL is configured separately in `Patcher/PatchConfig.cs`, so review it as well as the build environment variables.

Client/server releases use `ProjectVersion` from `Config\DefaultGame.ini`. Run `Scripts\IncreaseVersion.bat` when intentionally changing the shared network-compatible release version; do not assume a packaging run automatically increments it.

For a patcher-only build:

```powershell
.\Patcher\build.bat
```

See [Patcher documentation](Patcher/README.md) for manifest structure and hosting. Where older prose differs, follow the current scripts—particularly version handling and endpoint defaults.

## Validation and troubleshooting

### Run quest and safezone regression tests

After building the editor target:

```powershell
& "$EngineRoot\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" $ProjectFile '-ExecCmds=Automation RunTests Metin2.Quests+Metin2.World.SafeZones' '-TestExit=Automation Test Queue Empty' -nullrhi -nosound -unattended -nop4 -nosplash -stdout "-abslog=$ProjectRoot\Saved\Logs\QuestRegression.log"
```

Inspect each `Test Completed` result and unexpected errors in the log; do not rely only on the executable's exit code. These tests are not substitutes for multiplayer, cooking, dedicated-server, or end-to-end gameplay validation. Some fixtures use registry/import infrastructure and can write generated local data; review your working tree afterward.

### Common setup problems

| Symptom | What to check |
| --- | --- |
| Wrong engine opens or engine association is unresolved | Launch your selected engine executable with the explicit `.uproject` path; regenerate project files using that engine. |
| `RunUAT` or `UnrealEditor` is not found | Check that the path contains `Engine\Build\BatchFiles` and `Engine\Binaries\Win64`; remember that not every convenience script reads `UE_ROOT`. |
| Client/server target fails with an installed engine | Use the compatible source-built engine required by the Unique build environment. |
| Missing compiler, SDK, or .NET tooling | Verify Visual Studio workloads/components, the engine's toolchain requirements, and `.NET 8` for the patcher. |
| Build fails while the editor is open | Close the editor and perform a normal editor-target build; restart after reflected C++ layout changes. |
| Missing maps, textures, or import source files | Check the complete content checkout, any recorded submodules, and external dataset paths. Do not substitute invented assets or machine-specific defaults. |
| Gateway connects but character admission/travel fails | Check coordinator and map-server logs, readiness/registration, tokens, map IDs, and advertised host/ports. |
| Shipping client rejects a direct launch | Use the configured patcher, or the dedicated local Shipping launcher for development. |

Project logs are normally under `Saved\Logs`. Packaging diagnostics are also available under the selected engine's `Engine\Programs\AutomationTool\Saved\Logs`. Report the **first relevant compiler/runtime error**, target, configuration, engine revision, and reproduction steps—not just a final “build failed” message.

## Repository layout

| Path | Responsibility |
| --- | --- |
| `Source/Metin2/` | Runtime gameplay, networking, UI integration, and server systems. |
| `Source/*.Target.cs` | Editor, client, server, and standalone target definitions. |
| `Content/` | `UnrealLongju-Content` submodule for Unreal assets, maps, Blueprints, definitions, and localization resources; its initial revision contains documentation only. |
| `Config/` | Project, engine, packaging, localization, and release configuration. |
| `Plugins/MT2UE/` | Bundled editor importer and conversion tooling. |
| `Scripts/` | Build, launch, versioning, and localization automation. |
| `Patcher/` | .NET Windows launcher and manifest tooling. |
| `Docs/` | Architecture, operating parameters, project status, and legacy research. |
| `UnrealLongju.uproject` | Unreal project descriptor. |
| `LICENSE` | Project software license and required notice. |

`Binaries`, `Intermediate`, `Saved`, `DerivedDataCache`, IDE output, and staging directories are generated/local data. Do not manually edit generated code or commit build output. Preserve explicitly tracked third-party importer binaries; they are not interchangeable with ordinary generated plugin output.

Additional workflows: [Localization](Scripts/LOCALIZATION.md), [NPCs and Quests](Docs/OldGameResearch/NPCAndQuestSystem.md), and [Safezones](Docs/OldGameResearch/SafeZones.md).

## Submodules and dependency management

The content dependency is [UnrealLongju-Content](https://github.com/iamBVC/UnrealLongju-Content.git), checked out at `Content/`. `.gitmodules` records its URL and path; the parent repository records the selected commit. Developers and CI need access to both repositories. The current `MT2UE` directory remains bundled source, and `Engine2` remains an external checkout.

Read the [content rights notice](Content/README.md) before importing or publishing assets. Some planned content is obtained by converting Metin2 assets into Unreal Engine 5-compatible formats. Rights in those original assets remain with Gameforge, Ymir, and/or their respective rights holders; this project claims no ownership of them and is not affiliated with or endorsed by those companies. Conversion, attribution, and noncommercial intent do not grant permission to distribute their material. The parent project's software license does not license these third-party assets.

To publish a content update, first commit and push the authorized changes in the content repository. Then stage and commit the updated `Content` pointer in the parent repository. Never record a content commit that collaborators cannot fetch.

When adding further dependencies:

1. Choose its repository URL, repository-relative destination, and access requirements.
2. Review its license and redistribution rules independently of this project's license.
3. Record `.gitmodules` and the selected submodule commit in the parent repository.
4. Update build/import paths and this README to describe the actual dependency; do not assume a new path matches existing scripts.
5. Configure CI and developer checkouts to initialize submodules recursively, including authentication for private dependencies.

Keep builds pinned to the committed dependency revisions. Do not use `git submodule update --remote` as a routine setup step: upgrading a dependency should be an intentional, reviewed parent-repository change. A detached HEAD inside an initialized submodule is normal.

## Contributing

- Keep changes focused and follow the existing Unreal/C++ ownership and naming conventions.
- Preserve server authority, Blueprint compatibility, serialized assets, and network contracts unless the change explicitly requires a migration.
- Build the affected target and run relevant automation; document any untested multiplayer or packaged behavior.
- Review import-generated asset changes and quest diagnostics rather than counting an import as proof of parity.
- Do not commit databases, credentials, private symbols, extracted proprietary datasets, or generated build/cache directories.
- Include reproduction steps, expected/actual behavior, and validation results with fixes.

## License

Copyright © 2026 Castellese Brian Vincenzo.

This project is licensed under the [PolyForm Noncommercial License 1.0.0](LICENSE). Noncommercial use, modification, and redistribution are subject to its terms, including the required notices. Commercial use, commercial distribution, and resale require a separate written commercial license from the copyright holder.

The project software license does not replace the licenses or permissions applicable to Unreal Engine, third-party tools, or legacy game assets. Review those requirements separately before using or distributing them.
