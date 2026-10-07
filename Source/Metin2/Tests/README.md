# Metin2 automation tests

All first-party Unreal automation test implementations live in this directory.
Test names, behaviour, and automation categories are unchanged by the move.

| Files | Compiling module |
| --- | --- |
| `MT2DamageFeedbackTests.cpp`, `MT2DuelTests.cpp`, `MT2MapAttributeTests.cpp`, `MT2PathSettingsTests.cpp`, `MT2ProfilingPortsTests.cpp`, `MT2QuestPlayerApiTests.cpp` | `Metin2` runtime module |
| `MT2AreaPaintTests.h`, `MT2PIETravelTests.h` | `MT2UEEditor`, through `Private/MT2TestRegistration.cpp` |
| `MT2QuestImporterTests.h` | `MT2UEEditor`, included at the end of `Private/Importers/MT2QuestImporter.cpp` |

The editor test headers contain implementations, not public APIs. Include each
from its designated editor translation unit only. Keeping registration there
avoids adding editor dependencies to the runtime module, and lets importer tests
exercise existing private parser helpers without exposing them or duplicating
production code. `WITH_DEV_AUTOMATION_TESTS` guards are retained.

Build `UnrealLongjuEditor` and run the `Metin2` group through Session Frontend's
Automation tab. Save work before running PIE integration tests: they load the
configured editor startup map and start/stop PIE. GPU/PIE tests require a rendering
editor; NullRHI does not validate those paths. See
[PIE map travel](../../../Docs/PIETravel.md) and
[area painting](../../../Docs/AreaPainting.md) for fixture details.

Relocation validation (2026-10-06, before adding duels): Editor Win64 Development built successfully, and all 47
`Metin2` tests passed in `Saved/Logs/CentralizedTestsValidation.log`, including
real standalone PIE startup and the area-preview fixtures. The run used an
offscreen RHI with FXAA to isolate the existing TSR shader ensure; project
settings were not changed. The configured-path audit also passed. Cooked targets
were not rebuilt for this source-only reorganization.

Duel coverage includes `Metin2.Combat.Duels.Lifecycle`, `.DeathPenalties`, and
`.ButtonLabels`. See [duels](../../../Docs/Duels.md) for the latest validation
scope and log.

`Metin2.Config.FrameProPort` covers the startup game-port offset, independent
voice-port overrides, engine URL defaults, explicit profiler overrides, and
TCP port bounds.

Water-specific fixtures were removed at the user's request after validation.
See [water](../../../Docs/Water.md) for the retained map-bake and read-only verification workflow.
