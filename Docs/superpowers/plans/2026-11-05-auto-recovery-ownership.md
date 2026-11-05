# Automatic recovery — Batch 13 / OWN-04A

Continue the approved item tracker on main, without staging or commits.

## Contract and boundary

AutoRecoveryTemplate owns use eligibility, resource selection and pool-transfer policy. Inventory
owns reservoir instances, atomic splitting/switching and scheduling. No USE_SPECIAL subtype route
for this item remains in generic UseItem. Automatic recovery transfers the uncovered integer
resource deficit into the shared queued recovery pool, without PotionBonus scaling or direct healing.
It pauses while dead/stunned; activation rejects while trading. Exhausted reservoirs cannot refill through activation/reload.
Activation/switching/restore must not leave duplicate active reservoirs or stale source markers.
The processing interval is a Project Setting; actual HP/MP drain remains the existing one-second
affect processor and its configured rate. Preserve authored capacity/state and save/network shape.

Special X reservoir +4% max-resource bonus, arena policy, full USE_SPECIAL/alias importer audit and
client SE_AUTO_HPUP/SPUP visuals remain OWN-04B/SYS-04; do not claim those variants fully ported.
This batch does not reimport Content.

## Steps

- [x] Observe RED for direct class dispatch, pending-aware pool transfer and exhausted-stack preservation.
- [x] Implement dedicated policy and state transaction; restore active scheduling safely.
- [x] Validate depletion, switching, pause, removal, malformed active saves and interval configuration.
- [x] Run prior regressions; archive/hash passing sources and remove temporary tests.
- [x] Build Editor/Server, review diff, refresh/query graph and update tracker/catalog.

## Completion

`Saved/Automation/ItemImplementation/Batch13-Full/index.json`: 54/54 successful (33 clean,
21 successful with fixture missing-mesh warnings), zero failed/not-run. Initial policy and
lifecycle RED failures observed before their fixes. Fourteen temporary sources archived and
SHA-256 verified in `Batch13-VerifiedTests`, then removed; four pre-existing plugin tests remain.
Final Editor/Server Development Win64 builds succeeded after cleanup. Fresh read-only review
found no actionable defect. Unreal graph refreshed and policy/restore symbols queried; 48 existing
plugin/third-party parser warnings remain. No Content/save/network-schema changes. Work stays
unstaged/uncommitted on main. Character-service persistence, trade/travel/expiry integration and
packaged/live multiplayer were not exercised; those remain BASE-03/REL-01 alongside OWN-04B.

## Evidence

UE 5.7.4; main HEAD 8990848, clean worktree at start. Read all three Graphify reports and queried
ToggleAutoRecoveryItem, legacy AutoRecoveryItemProcess and client SetAutoPotionInfo. Unreal graph
predates latest commit but includes the unchanged auto-recovery paths; source inspected directly.
Legacy char_item.cpp ~3920–4050 activation, ~7285–7380 processing; queued affect processing in
char_affect.cpp. Current MT2InventoryAutoRecovery.cpp and restore/removal/trade call sites inspected.

