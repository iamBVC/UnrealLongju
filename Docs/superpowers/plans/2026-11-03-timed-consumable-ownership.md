# Timed consumables and cleanse — Batch 12

Execute the approved item tracker OWN-03 in the current worktree on main. Do not stage or commit.

## Contract

Ability-up and affect templates own their action and editable data. Inventory owns consumption.
The status-effect component retains scheduling, replication and removal. Preserve old Blueprint
default values with hidden compatibility properties and versioned PostLoad migration, including
zero/negative overrides and omitted inherited CDO values. Each class has its own header/source.

Ordinary ability-up supports the nine legacy applies, uses legacy affect identities and speed flags,
refreshes by identity and clears on death. A dedicated dew subclass imports 50801–50820 ability items,
uses ExperienceBonusEuropeFree, supports magic resistance too, refuses the same identity/apply pair,
allows different applies to coexist and survives death. Decode VNUM variants only in the importer.
Affect items own their identity and refuse duplicate identity/apply pairs; distinct applies coexist.
Use the legacy death-preservation rule for authored affect identities. Cleanse consumes once even
without bad effects, removes poison/fire/stun/slow and the configured legacy Tusok skill, preserves buffs.

Retain contextual dungeon/war accounting and PC-bang service eligibility as explicitly open system
dependencies (OWN-02S/SYS-04); do not invent their services. Malformed unsupported data is rejected.
Keep tunable values authored, enums/semantic affect selectors explicit, and runtime references typed.

## Steps

- [x] Add meaningful runtime policy/consumption/cleanse regressions and observe RED.
- [x] Move ability-up/affect actions and data; add dew class and versioned field migration.
- [x] Update importer and tooltip access; validate import/reimport and inherited defaults.
- [x] Run full regressions, archive/hash temporary sources and remove them after success.
- [x] Build Editor/Server after cleanup; refresh/query Unreal graph; review final diff.
- [x] Update class catalog and tracker with evidence and remaining integration limits.

## Completion evidence

`Saved/Automation/ItemImplementation/Batch12-Final/index.json`: 52/52 successful (34 clean,
18 successful with lightweight fixture missing-mesh warnings), zero failed/not-run. Initial
policy/field RED and saved-effect restore RED were observed. Thirteen passing temporary sources
archived and SHA-256 verified under `Batch12-VerifiedTests`, then removed. Four pre-existing
plugin tests remain. Final Editor/Server Development Win64 builds passed after removal:
`Saved/Logs/ItemBatch12-FinalEditorBuild.log`, `ItemBatch12-FinalServerBuild.log`.

Ninety imported packages migrated with all common serialized properties preserved. Original
backups and SHA-256 manifest are in `Batch12-AssetsBefore`; the suite compares original and
migrated packages after physical reload. Imported coverage: 30 ability items (six dew), 59
affects, one cleanse. Fresh read-only review found the restored-effect identity compatibility
issue; fixed with RED/GREEN evidence. Final review has no remaining actionable findings.
Unreal graph refreshed and affected policy/restore/migration symbols inspected. Source and
Content changes remain unstaged/uncommitted on main. Packaged/live multiplayer and external
Blueprint graphs were not exercised. Contextual service dependencies remain tracked.

## Research

UE 5.7.4. All three Graphify reports/graphs consulted; legacy graphs match commit a6d53fe0.
Legacy char_item.cpp USE_ABILITY_UP (~4275), special 50801–50820 branch (~2162), USE_AFFECT
(~4874), USE_CLEAR (~4055); char_affect.cpp death exemptions and RemoveBadAffect.
Client PythonItem.cpp selects potion sound for USE_ABILITY_UP and sends item use to the server.
Graphs are navigation; source behavior is authoritative.
