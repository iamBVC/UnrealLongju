# Recovery Affect Timing and Rounding Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Match queued HP/MP recovery pool drains to the legacy server's once-per-second affect update, regional percentages, and integer truncation.

**Architecture:** Keep `UMT2StatusEffectComponent` as the shared one-second scheduler. Make the queued recovery definition calculate an integer legacy point chunk from Project Settings, and set the standard editable rates to 7% HP and 7% MP; deployments using legacy YMIR set the existing HP rate to 9%.

**Tech Stack:** Unreal Engine 5.7.4, C++17, Unreal Automation Framework, Graphify.

**Spec:** [2026-11-02-recovery-affect-parity-design.md](../specs/2026-11-02-recovery-affect-parity-design.md)

## Global Constraints

- Work on `main`; do not create branches or worktrees, commit, push, or stage files. Leave the final changes for the user to commit.
- Keep item-use policy in its item template and status-effect ticking in the shared status-effect system.
- Do not add a project-wide locale selector, a second recovery timer, Content changes, UI changes, or save/network schema changes.
- Temporary regression tests must be archived and hash-verified, then removed after they pass. Remove `Source/Metin2/Tests` only after verifying it is empty; preserve unrelated plugin tests.
- Update current-state documentation and the item TODO tracker; do not add obsolete implementation history to product documentation.
- Use `F:\Engine2` and `F:\UnrealLongju`; validate Editor and dedicated Server Development Win64 targets.

## Review Focus

1. **Regional rates:** standard HP/MP default to 7%, while HP can be overridden to 9% for YMIR. Test the defaults and both configured HP rates.
2. **Fractional max resource:** legacy points are integers; convert the positive maximum to whole points before multiplying. Test max 114.99 at 7% and 9%.
3. **Pool smaller than one drain chunk:** heal only the remaining pool and finish the effect. Test a pool of 5 with a 7 point chunk.
4. **Resource near maximum:** clamp actual HP/MP but subtract the requested legacy chunk from the pool. Test a resource 2 points below maximum.
5. **Zero integer chunk:** do not force a minimum heal; leave the pool active and unchanged when a low maximum produces a zero legacy chunk. Test max 10 at 7%.
6. **Invalid configured rate:** bound config-loaded values to the Project Settings range [1, 100]; test a negative HP rate clamps to 1% while a valid 7% rate that truncates to zero remains a no-op pass.

---

### Task 1: Restore regressions and add recovery parity tests

**Files:**
- Restore the nine archived runtime test files from `Saved/Automation/ItemImplementation/Batch10-VerifiedTests/Runtime/` to `Source/Metin2/Tests/`.
- Restore `MT2ContinuousPotionImportTests.cpp` from `Saved/Automation/ItemImplementation/Batch10-VerifiedTests/Editor/` to `Plugins/MT2UE/Source/MT2UEEditor/Private/Tests/`.
- Create temporary test `Source/Metin2/Tests/MT2RecoveryAffectParityTests.cpp`.

**Interfaces:**
- Use `UMT2StatusEffectDefinition::Get(EMT2StatusEffectKind::HealthRecovery)` and `...ManaRecovery` to exercise the existing effect policies directly.
- Reuse the archived runtime fixture pattern: standalone `UGameInstance`, spawned player/state, initialized GAS attributes, and the real health/mana components.

- [x] **Step 1: Restore the verified Batch10 regression suite** to its original source paths; confirm file hashes against the Saved archive before compiling.
- [x] **Step 2: Write two failing automation tests** in `MT2RecoveryAffectParityTests.cpp`:
  - `Metin2.StatusEffects.Recovery.LegacyDefaults`: assert Project Settings defaults HP=7 and MP=7.
  - `Metin2.StatusEffects.Recovery.LegacyPoolDrain`: assert standard HP 7%, YMIR-configured HP 9%, MP 7%, integer maximum truncation at 114.99, pool smaller than the chunk, near-cap clamping with full requested-pool deduction, and zero-chunk behavior at maximum 10.
- [x] **Step 3: Build the Editor target and run the focused tests** with `Automation RunTests Metin2.StatusEffects.Recovery`.

Run:

```powershell
& F:/Engine2/Engine/Build/BatchFiles/Build.bat UnrealLongjuEditor Win64 Development -Project=F:/UnrealLongju/UnrealLongju.uproject -WaitMutex -NoHotReloadFromIDE *> F:/UnrealLongju/Saved/Logs/ItemBatch11-RedBuild.log
& F:/Engine2/Engine/Binaries/Win64/UnrealEditor-Cmd.exe F:/UnrealLongju/UnrealLongju.uproject -unattended -NullRHI -nosplash -nop4 '-ExecCmds=Automation RunTests Metin2.StatusEffects.Recovery' '-TestExit=Automation Test Queue Empty' -ReportExportPath=F:/UnrealLongju/Saved/Automation/ItemImplementation/Batch11-Red -abslog=F:/UnrealLongju/Saved/Logs/ItemBatch11-Red.log *> F:/UnrealLongju/Saved/Logs/ItemBatch11-Red-console.log
```

Expected: the defaults fail at 3%, fractional maximum rounds the current float-based drain up to 8 instead of the legacy 7, the low-maximum case heals due to the current forced minimum of 1, and a negative configured rate fails to clamp to the valid 1% minimum.

---

### Task 2: Implement integer legacy drain rates

**Files:**
- Modify `Source/Metin2/Components/MT2StatusEffectDefinition.cpp`.
- Modify `Source/Metin2/Config/MT2GameplaySettings.h`.
- Modify `Config/DefaultGame.ini`.
- Test with `Source/Metin2/Tests/MT2RecoveryAffectParityTests.cpp`.

**Interfaces:**
- Keep the existing `HealthRecoveryPercentPerSecond` and `ManaRecoveryPercentPerSecond` Project Settings properties.
- Keep the existing one-second `ProcessEffects` timer and recovery effect `Tick` interfaces.

- [x] **Step 1: Implement the failing integer drain cases** using whole maximum resource points and a 64-bit multiply/divide, cap the chunk at the remaining pool, bound config-loaded rates to 1–100, and remove the forced minimum of one. Preserve the full-resource early completion and existing resource setter clamp.
- [x] **Step 2: Change the reflected and config defaults** to `HealthRecoveryPercentPerSecond=7` and `ManaRecoveryPercentPerSecond=7`. Add property tooltips that explain the one-second legacy rate and that YMIR deployments set HP to 9.
- [x] **Step 3: Run the focused recovery tests** and require both tests to pass.
- [x] **Step 4: Run the full Metin2 automation suite** and inspect the exported `Batch11-Green/index.json` for success/failure/not-run counts; verify the archived queued, instant, continuous, item and persistence regressions still pass.

Run:

```powershell
& F:/Engine2/Engine/Binaries/Win64/UnrealEditor-Cmd.exe F:/UnrealLongju/UnrealLongju.uproject -unattended -NullRHI -nosplash -nop4 '-ExecCmds=Automation RunTests Metin2' '-TestExit=Automation Test Queue Empty' -ReportExportPath=F:/UnrealLongju/Saved/Automation/ItemImplementation/Batch11-Green -abslog=F:/UnrealLongju/Saved/Logs/ItemBatch11-Green.log *> F:/UnrealLongju/Saved/Logs/ItemBatch11-Green-console.log
```

Expected: all available Metin2 automation tests succeed, including the two Batch11 cases, with no failed or not-run tests.

---

### Task 3: Clean temporary tests, update docs, and verify final targets

**Files:**
- Archive and remove the restored Batch10 tests and new Batch11 test from `Source/Metin2/Tests/`; retain the review-fix archive and its SHA-256 manifest separately.
- Archive and remove only the restored Batch10 importer test from `Plugins/MT2UE/Source/MT2UEEditor/Private/Tests/`.
- Modify `Docs/ItemBehavior.md`.
- Modify `Docs/ItemTemplateVariantCatalog.md`.
- Modify `Docs/superpowers/plans/2026-10-20-item-implementation-todo.md`.
- Create ignored validation notes at `Saved/Automation/ItemImplementation/Batch11-progress.md`.
- Refresh `graphify-out/graph.json` and inspect the updated status-effect/recovery symbols.

- [x] **Step 1: Archive and hash-verify every temporary source file** under `Saved/Automation/ItemImplementation/Batch11-VerifiedTests/{Runtime,Editor}`.
- [x] **Step 2: Delete only the verified temporary source files** and remove `Source/Metin2/Tests/` only if empty. Confirm existing plugin-local tests remain and the temporary importer test is gone.
- [x] **Step 3: Build the Editor and dedicated Server targets after test cleanup.**

```powershell
& F:/Engine2/Engine/Build/BatchFiles/Build.bat UnrealLongjuEditor Win64 Development -Project=F:/UnrealLongju/UnrealLongju.uproject -WaitMutex -NoHotReloadFromIDE *> F:/UnrealLongju/Saved/Logs/ItemBatch11-ReviewFix-FinalEditorBuild.log
& F:/Engine2/Engine/Build/BatchFiles/Build.bat UnrealLongjuServer Win64 Development -Project=F:/UnrealLongju/UnrealLongju.uproject -WaitMutex -NoHotReloadFromIDE *> F:/UnrealLongju/Saved/Logs/ItemBatch11-ReviewFix-FinalServerBuild.log
```

- [x] **Step 4: Update current behavior documentation** with one-second queued pool drains, integer truncation, HP/MP defaults, and the YMIR HP override; state that OWN-02S contextual hooks remain open.
- [x] **Step 5: Mark OWN-02D complete** and add a completion-log row containing legacy source paths, regression results, cleanup/hash evidence, final build results, and any unverified packaged/live behavior.
- [x] **Step 6: Refresh the Unreal Graphify code graph** and query `DrainRecoveryPool`, `UMT2StatusEffect_HealthRecovery::Tick`, and `UMT2StatusEffect_ManaRecovery::Tick`. Leave legacy graphs unchanged.

```powershell
& 'C:\Users\Brian\AppData\Local\Programs\Python\Python312\Scripts\graphify.exe' update F:/UnrealLongju *> F:/UnrealLongju/Saved/Logs/ItemBatch11-GraphUpdate.log
& 'C:\Users\Brian\AppData\Local\Programs\Python\Python312\Scripts\graphify.exe' query 'DrainRecoveryPool' --graph F:/UnrealLongju/graphify-out/graph.json --budget 1400
& 'C:\Users\Brian\AppData\Local\Programs\Python\Python312\Scripts\graphify.exe' query 'UMT2StatusEffect_HealthRecovery::Tick' --graph F:/UnrealLongju/graphify-out/graph.json --budget 1400
& 'C:\Users\Brian\AppData\Local\Programs\Python\Python312\Scripts\graphify.exe' query 'UMT2StatusEffect_ManaRecovery::Tick' --graph F:/UnrealLongju/graphify-out/graph.json --budget 1400
```
- [x] **Step 7: Review final diff and repository states.** Require clean `git diff --check`, no temporary test sources, no Content changes or index changes, branch still `main`, and all implementation changes left uncommitted.
