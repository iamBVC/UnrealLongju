# Quest porting audit — 2026-10-05

Scope: the current quest importer/runtime and the original Italian quest corpus. This is not a
completion audit of every gameplay system. Engine used for validation: Unreal Engine 5.7.4.

## Measured coverage

Documentation review: 2026-10-06. The pass-by-pass sections below are a historical audit trail: each limitation and test count applies to that pass, and later sections can supersede it. Latest recorded baseline: 238 scripts, 237 Blueprints, 6,595 triggers, 27,761 nodes, 482 statements, 67 gates, and 27 unsupported triggers (576 entries). This review did not regenerate content or rerun tests.

Fresh `MT2ImportQuests` run against
`D:/Giochi/Metin2/Development/my_server/server_src/share/locale/italy/quest`:

| Import measure | Before cooldown pass | After cooldown pass | After ipairs pass | After item-copy pass | After loop/header pass | After mutable-table pass | After callback pass |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Scripts parsed | 238 | 238 | 238 | 238 | 238 | 238 | 238 |
| Quest Blueprints refreshed | 237 | 237 | 237 | 237 | 237 | 237 | 237 |
| Triggers imported | 6,149 | 6,149 | 6,149 | 6,149 | 6,595 | 6,595 | 6,595 |
| Nodes generated | 25,361 | 25,372 | 25,376 | 25,374 | 25,781 | 26,425 | 26,450 |
| Unconverted statements | 552 | 551 | 549 | 548 | 586 | 517 | 510 |
| Failed trigger gates | 29 | 29 | 29 | 29 | 68 | 68 | 68 |
| Unsupported triggers | 25 | 25 | 25 | 25 | 27 | 27 | 27 |

The Lua-value pass had 238 scripts, 237 refreshed Blueprints, 6,595 triggers, 26,577 nodes
and 502 unconverted statements (down from the callback pass's 510).
The NPC-lease pass had 238 scripts, 237 refreshed Blueprints, 6,595 triggers, 26,636 nodes
and 526 unconverted statements. Its report has 620 entries across 100 quest names:
526 statements, 67 gates, 27 triggers. Removing the broad silent NPC-statement skip exposed a
net 24 additional diagnostics; the newly reported calls are not considered ported.
The resumable-expression pass has 238 scripts, 237 refreshed Blueprints, 6,595 triggers,
27,373 nodes and the same 620 report entries: 526 statements, 67 gates, 27 triggers across 100
quest names. The additional nodes capture expression values and conditional call paths.
The inventory-range pass has 238 scripts, 237 refreshed Blueprints, 6,595 triggers,
27,430 nodes and 523 unconverted statements (down from 526). That report contains
617 entries: 523 statements, 67 failed gates and 27 unsupported triggers.
The NPC-purge pass has 238 scripts, 237 refreshed Blueprints, 6,595 triggers,
27,431 nodes and 513 unconverted statements. That report contains 607 entries:
513 statements, 67 failed gates and 27 unsupported triggers.
The entity-identity/proximity pass has 238 scripts, 237 refreshed Blueprints, 6,595 triggers,
27,444 nodes and 504 unconverted statements. That report contains 598 entries:
504 statements, 67 failed gates and 27 unsupported triggers.
The named-target dispatch pass retains these counts (238 scripts, 237 refreshed Blueprints,
6,595 triggers and 27,444 nodes): it corrects runtime behavior of already accepted triggers.
The pre-dispatch gate pass has the same report counts and 27,435 nodes. Nine body-side gate
wrapper nodes were removed; their helper nodes and result conditions now execute before dispatch.
The item-metadata pass retained 27,435 nodes and reduced statements from 504 to 503.
The default-shop pass retained 27,435 nodes and reduced statements to 497.
The warp-routing pass had 27,487 nodes and 494 statements.
The party-flag pass had 27,489 nodes and 493 statements.
The skill-predicate/affect pass had 27,502 nodes and 490 statements.
The grand-master learning pass had 27,510 nodes and 491 statements.
The alignment/string-format passes had 27,520 nodes and 488 statements.
The pattern-substitution pass had 27,523 nodes and 484 statements (578 entries:
484 statements, 67 failed gates and 27 unsupported triggers).
The native-result-list pass has 27,515 nodes and the same diagnostic counts. It replaces
sequential/mount-specific assignment lowering with a single simultaneous assignment node.
The type-predicate pass has 27,528 nodes and 482 statements (576 entries: 482 statements,
67 failed gates and 27 unsupported triggers).
The function-result-list pass has 27,761 nodes and retains these diagnostic counts.
The loop/header pass recovered 446 previously omitted multiline triggers. The increased diagnostic
count reflects newly inspected code, not removal of previously supported operations.
These counts are translation diagnostics, not a percentage of gameplay ported. A rejected conditional
can conceal many unsupported operations in its body. Supporting its predicate can expose additional
diagnostics rather than removing them all. Quests without diagnostics can still contain placeholders
or differences in API behavior.

The importer refreshed the generated `/Game/Quests` Blueprints and shared quest table asset.
Full line-level findings: `Saved/MT2QuestConversionReport.txt` (regenerated on import).

## What already exists

The older `QuestSystem.md` describes historical stages. The current implementation already has
expression-based conditions/arguments, arithmetic and math/string helpers, converted library tables,
inline literal tables, while/repeat/numeric-for lowering, quest-local function calls/returns, input,
quest journal entries, target markers, timers, affect nodes, and item-attribute mutations.
These are implemented subsets; this does not imply arbitrary Lua support.

## Completed in the cooldown/resource pass

- `next_time_set(value, test_value)` lowers to an existing If node and quest-scoped SetFlag nodes for
  `__NEXT_TIME__`, preserving the legacy test-server branch structure.
- `next_time_is_now()` compares that quest's deadline with Unix time. Missing manager context fails
  evaluation instead of reporting a successful read. The legacy helper ignores its optional argument.
- `get_time()` now uses Unix time, as does `get_global_time()`: both legacy names bind to the same
  `_get_global_time` function. World uptime was incorrect for deadlines persisted across map travel or
  restarts. The port-only `game.get_time()` retains its existing world-time behavior.
- HP, maximum HP, SP, maximum SP, and death reads now query the character's existing health/mana
  components. Added legacy `gethp`, `getmaxhp`, `get_max_sp`, and `getmaxsp` spellings and the
  `pc.maxsp` property. No resource mutation or new network messages are introduced.
- Added focused automation tests for live GAS resource reads, aliases, death, absent pawn, Unix time,
  unset/future/expired cooldowns, quest scoping, and missing manager context.

Legacy references: `game/src/questlua_pc.cpp` resource getters, death predicate and binding table;
`game/src/questlua_global.cpp` time bindings; `questlib.lua` lines 256–271 cooldown helpers.

`find_senior_soldier` still needs its target-selection/functions/closure list. `new_quest_lv80` still
reports an unconverted conditional body, even though `next_time_is_now` itself is now supported.
Neither quest is declared fully ported by this change.

## Completed in the ipairs pass

- Added `UMT2QuestNode_ForEach` for `for index[, value] in ipairs(table) do ... end`.
  Table expressions are evaluated once; an owned, read-only snapshot keeps nested rows valid through
  suspension and when a row escapes into another variable or a function return value.
- Iterator frames retain position across wait/input/select suspension. Nested loops and early
  function returns restore shadowed iterator locals. Iteration stops at the first missing integer
  key; a zero or empty string is still a value. Limits abort the block rather than falling through.
- The importer translates only complete ipairs bodies. Unsupported operations (including break or
  unbound calls) produce a stopping TODO for the whole loop, with the first body failure reported.
  This avoids enabling side effects in a partially converted loop. General `pairs` and other generic
  iterator forms are not supported. Other pre-existing loops retain their previous behavior.
- Both material loops in `cube_opener_list` now translate. `ride_ticket_change` still needs dynamic
  signature-item list construction and `item.select`; its loop remains a stopping TODO.
- Unsupported `item.copy_and_give_before_remove` now stops execution. The newly translated material
  consumption loop cannot run after a skipped item replacement. The replacement transaction itself
  was not ported in that pass; the item-copy pass below supersedes this restriction for the exact
  recognized cube transaction. Complete item-take trigger/gameplay parity is not claimed.
- Dialogue tokens are resolved when Say executes, preserving each iteration's row values instead
  of resolving every buffered line against the last/restored iterator at page delivery time.

This is sequence iteration over the current read-only table model, not Lua table mutation support.
Snapshots copy a table once per loop invocation, not once per iteration. Iterator variables are
lexically restored; full Lua block-local/function-parameter scoping remains separate existing work.

## Completed in the item-copy/item-take pass

- Added a separate `ItemTake` event, appended to preserve existing enum values. Imported `.take`
  filters now identify the NPC rather than the offered item. The existing server item-on-actor RPC
  dispatches this event, retaining player-trade and configured blacksmith-refine priority.
- The manager captures the offered item on the server and refuses a new offer during a suspended
  conversation. The copy node rechecks the current pawn, NPC, range, death/trade state and source
  contents when execution resumes. Cosmetic newly-acquired state does not invalidate the snapshot.
- The importer folds the recognized `item.copy_and_give_before_remove(result)` plus immediately
  following `ipairs` material-removal loop into one `UMT2QuestNode_CopyItem` transaction. Other copy
  call shapes remain stopping TODOs; this is not general support for the Lua function's return value.
- Replacement and material consumption commit once, or leave every inventory slot unchanged.
  Validation covers server authority, single equipment items, template/vnum ranges, page/grid fit,
  source changes, complete material quantities and active auto-recovery items. Grid fit is checked
  against the current layout, so material consumption is not used to free a larger replacement's space.
- Bonuses are preserved. Legacy non-accessory socket copying opens silver sockets, packs surviving
  stones and discards broken Metin (28960); accessory socket state is copied separately. Unknown
  numeric stone payloads fail safely instead of being discarded. The requested destination vnum is
  retained even when resolution uses a ranged prototype.

`cube_opener_list` now has the atomic armor exchange node and its NPC 20378 item-take trigger.
At that point its separate multiline `when ... .take or ...` cube-opening trigger was absent from
the generated asset and unreported. The loop/header pass below recovers it; its
`command("cube open")` backend remains missing. This quest is NOT declared completely playable.
No persistent item-instance identifier exists in the current slot model: snapshot matching compares
item contents, not an identity capable of distinguishing two otherwise identical instances.

## Completed in the loop/header pass

- Added native `break` nodes for while, repeat, numeric-for and ipairs bodies. A break unwinds only
  the nearest loop, restores iterator locals, survives dialogue suspension and cannot escape a
  called function. Import-time function translation also resets lexical loop depth.
- While iterations reuse their execution frame instead of adding one frame per iteration. Repeat
  executes its first body inside that frame, so a first-iteration break exits the loop rather than
  the whole quest. Loop budgets reset on completion, function return or trigger termination.
- Multiline `when` headers are joined through their `begin` line. This recovers 446 triggers,
  including all four additional cube-opening NPC offers. Their missing command is now reported.
- Newly recovered multiline triggers with any unconverted body statement receive an explicit false
  condition. Their nodes and original diagnostics are retained for inspection; partially translated
  item changes/rewards are not enabled. Reimporting after the body is fully converted removes this gate.
- The four directly reported break statements in fortune_telling, hair and levelup are gone.
  Other rejected blocks may conceal more breaks or unrelated unsupported operations.

The zero-unconverted goal is active, not achieved. Item selection also needs stable per-instance
identity and signature-group metadata; using a movable inventory index as an item ID is not safe.
Dynamic item-list construction, mutable tables and multi-return handling remain prerequisites.

## Completed in the mutable-table pass

- Added private runtime tables with shared nested identity, separate from the generated read-only
  table assets. Repeated inline declarations instantiate fresh tables; aliases share mutations, not
  copies. Existing serialized inline-table/node fields remain compatible and are not mutated.
- Dynamic constructors evaluate numeric/string keys, named fields, nested tables and expressions.
  Added indexed/named assignments, nil deletion and `table.insert` append/position forms.
  Locale constants in mutation values/keys are resolved at import time; escaped line breaks and
  numeric concatenation are tokenized correctly instead of corrupting menu labels.
- Runtime ipairs retains the captured table identity across waits, observing changes to later keys
  through aliases. Rebinding the source variable does not restart it. Read-only asset sequences
  retain their existing owned snapshot behavior. Item-copy material parsing accepts both forms.
- Nil is now distinct from numeric zero and missing fields evaluate to nil. Lua boolean typing,
  truthiness and operand-preserving/short-circuit and/or are still separate unfinished work.
- Expression acceptance now validates syntax without executing gameplay reads. Unsupported function
  values/calls such as `func[idx+1]()` remain reported rather than accepted and failing at runtime.
- Invalid runtime mutations log and stop; failed variable expressions also stop their trigger.
  Tables are limited to 10,000 entries and constructor nesting to 64. Reference cycles are rejected
  before mutation to avoid shared-pointer leaks; cyclic Lua graphs are not supported.

The inventory-check table writes in `pre_event_heavens_cave` now generate real mutation nodes.
That quest still reports its separate `choosereward` statement. Ticket exchange still needs
`pc.get_sig_items`, stable item identity/selection and give-item return semantics. Anonymous
`table.foreach/foreachi` callbacks, general function values and multi-return expansion remain.

## Completed in the table-callback/menu pass

- Added `table.foreach(table, function(key[, value]) ... end)` and `table.foreachi` nodes.
  Inline anonymous callback bodies use resumable function frames, with parameter/local restoration.
  Unsupported callback bodies remain one stopping diagnostic instead of enabling partial behavior.
- `foreachi` captures the initial sequence length, reads values at each invocation and still invokes
  a slot deleted during suspension. `foreach` captures numeric and named keys; iteration order is
  deterministic but not a promise of the old Lua hash order. Added keys are not visited; deleted
  keys are skipped. Lua does not define reliable iteration when callbacks introduce new keys.
- Any non-nil callback return (including numeric zero/false) terminates this call and becomes its
  assigned result. Explicit/implicit nil continues. Return does not terminate the enclosing trigger.
  Iteration/nesting limits stop execution and restore locals on failure.
- `select_table` evaluates its table at execution and sends one label per sequence entry, including
  callback-built menus. Runtime labels are not expanded again as dialogue-token templates.
  Invalid/empty/nested-table option lists stop with a diagnostic instead of displaying a table name.
- Original source indices and surrounding quest function definitions are retained when extracting
  multiline callback bodies. Generated-asset verification identifies callbacks and dynamic menus.

The report no longer directly lists the seven previously rejected table-callback statements.
Grand-master training's generated asset contains both callbacks and its dynamic menu. Its skill
training APIs and string-pattern operations still have diagnostics. Guild-war/arena/marriage data
providers also remain unsupported; converting their list callbacks does not make those quests playable.
Named callback values, arbitrary closures, complete block-local scoping and multi-return remain work.

## Completed in the Lua-value/condition pass

- Added a distinct boolean type and Lua truthiness: only nil and false are false. Numeric zero,
  empty strings and tables are true. Comparisons/not produce booleans; boolean and numeric/string
  values do not compare equal through their shared storage. String comparisons are case-sensitive.
- Evaluator `and`/`or` preserve operand values and parse unused operands without gameplay reads,
  table mutation, runtime indexing or arithmetic. Import validation still checks their syntax and
  refuses unbound APIs even on an unused side. Boolean table keys are separate from numeric keys;
  cycle detection, limits, deletion and foreach traversal include them.
- Verified legacy boolean-returning native bindings and questlib helpers now return typed booleans.
  Numeric getters still return numbers. This changes return representation, not the implementation
  of still-placeholder features such as dungeon/marriage/horse state or NPC conversation ownership.
- `tonumber` returns nil for booleans/invalid strings, accepts decimal/exponent strings and does
  not turn them into a successful zero. Arithmetic rejects nil/boolean/table inputs and accepts
  numeric strings. Invalid division/return expressions fail instead of silently returning zero/nil.
  Concatenation follows arithmetic precedence and formats numbers with legacy `%.14g` formatting.
- Parsed conditions take precedence over old substring recognizers. Zero-item comparisons are not
  incorrectly imported as HasItem(count=1); computed levels retain their arithmetic. Bare locals,
  function-result temporaries and cooldown predicates now translate with correct truthiness.
  The eight directly reported empty-reason condition diagnostics are gone; body diagnostics remain.
- `is_destination_village` now matches GFquestlib's map groups (1/21/41, 3/23/43, both groups,
  or map 65) and yields a truthy empty string or nil. Comparing the player's empire was incorrect.

At the end of that pass, resumable quest-local calls were still eagerly hoisted. The expression
lowering pass below supersedes that behavior for its integrated expression contexts; other contexts
still require review. The then-placeholder `npc.lock()` gate is now backed by the NPC-lease pass.
The zero-unconverted goal therefore remains incomplete independently of the report count.

## Completed in the NPC-conversation lease pass

- `npc.lock()` now acquires a server-owned lease on the actual target mob and returns a typed
  boolean: the same player can reacquire it, a different player is refused, and no-NPC/player
  targets retain the legacy successful no-op. Authority and current pawn/player-state identity
  are checked. `npc.unlock()` releases only the caller's lease and returns nil.
- Standalone lock/unlock statements become executable `UMT2QuestNode_NpcLock` nodes rather than
  being discarded. Removed the broad `npc` statement skip: unsupported calls such as `npc.purge`,
  `npc.open_shop`, Santa stock getters and NPC damage multipliers now remain visible in the report.
- Weak references and destruction/pawn-change delegates clean up leases on NPC removal, pawn
  replacement/destruction and manager teardown. Failed trigger probes roll back newly acquired
  leases without releasing pre-existing owned leases. Leases survive wait/input/menu suspension
  and the final displayed page, then release on completion or cancellation.
- Dismissing wait/select aborts the run rather than executing the remaining rewards. Closing an
  input page sends cancellation, not a legitimate empty text submission. Explicit empty input
  submission remains supported. No save-data or RPC schema changes; client UI behavior changed.
- New interactive events cannot replace a pending conversation. Existing broadcast events still
  replace a suspended run, now with explicit cancellation/cleanup before replacement. Deferred
  broadcast delivery and dialogue-generation validation remain separate fidelity/network work;
  this change does not claim to solve those issues.

Legacy reference: `game/src/questlua_npc.cpp`, `npc_lock` and `npc_unlock`.

## Completed in the function-parameter frame pass

- Quest-call arguments are evaluated in the caller's scope before any parameter binding. This
  prevents calls such as `f(b, a)` from overwriting `a` before the second argument is read.
- Function frames save existing parameter bindings and remove previously absent parameters on
  unwind. Explicit/implicit returns, nested calls, dialogue suspension/resume, cancellation and
  runtime failure now share the restoration path. The result is published after restoration,
  including when its destination has the same name as a parameter.
- Evaluation failures stop execution with diagnostics rather than silently passing nil. Missing
  parameters receive Lua nil; extra supplied arguments still evaluate even if ignored. The importer
  retains every supplied argument and no longer rejects a call solely for argument-count mismatch.
- This is parameter-frame correctness, not full lexical scope support: ordinary function-body
  locals, closures, expression-call short-circuit/order lowering and multiple returns still need work.
  Existing saved data, reflected nodes and RPC schemas are unchanged.

## Completed in the resumable-expression lowering pass

- Added an import-time expression syntax tree and lowering to existing SetVariable, If and
  CallFunction nodes. `and`/`or` put all RHS evaluation inside the taken branch and preserve the
  selected operand's value/type. Earlier reads and native-call arguments are captured before later
  quest helpers can change state. Nested helper arguments execute once and can suspend/resume.
- Integrated lowering into full/inline if conditions, scalar assignments, returns, standalone
  calls and the existing trigger-gate prelude path. Inline bodies now retain original function
  definition indices. Grouping, unary expressions, comparisons, arithmetic, concatenation and
  nested indexing are supported; rejected lowering rolls back nodes, temporary names and diagnostics.
- Pure-helper textual expansion is restricted to immutable scalar literal arguments. Dynamic reads
  and calls use real argument capture, preventing duplicate/discarded side effects. Call-like text
  inside quoted strings is no longer mistaken for executable source.
- This is not arbitrary Lua coverage: constructor entries containing quest calls, callable table
  entries, closures, multiple returns, loop/menu/mutation/reward argument integration and full lexical
  scope remain work. At that stage runtime-backed `with` clauses still used a body gate rather than
  a pre-dispatch predicate; the later trigger-gate pass below corrects that dispatch path. No runtime class,
  saved-data or network schema was added; generated node structures change on reimport.

## Completed in the inventory-range helper pass

- Bound `count_item_range(firstVnum, lastVnum)` to inclusive inventory-only counts,
  including all stacks but excluding equipped items. Lookup scans the inventory rather
  than iterating potentially huge identifier ranges; totals use 64-bit accumulation.
- Bound `remove_item_range(amount, firstVnum, lastVnum)` to an authoritative,
  all-or-nothing removal. It consumes ascending vnums and then slot order, matching
  GFquestlib's helper, and publishes the inventory change after commit. Auto-recovery
  effect callbacks run only after all slot edits are finished.
- Preserved typed true/false results and the legacy zero-cost distinction: true when
  a matching owned item is encountered, otherwise nil. Import validation and skipped
  short-circuit branches do not consume items. Missing player/authority or arguments
  outside the supported nonnegative int32 inventory identifier/count domain fail
  expression evaluation; negative/fractional values are not silently coerced.
- The two reported mount-training range conditions and the Easter 2013 exchange
  condition now translate. Other Easter helper bodies, gates and NPC purge diagnostics
  remain visible; no end-to-end quest completion or general inventory parity is claimed.

Legacy references: `GFquestlib.lua` lines 71-101, `questlua_pc.cpp` count/remove bindings,
and `char_item.cpp` CountSpecifyItem/RemoveSpecifyItem. The existing inventory model
does not include legacy private-shop selling-slot exclusions; this pass does not add
that independent system or count its behavior as ported.

Validation: editor Development build and all 11 `Metin2.Quests` tests passed. The new
`.PlayerApi.ItemRanges` test covers range bounds, stacks, equipment exclusion, large
identifier ranges, numeric strings, removal order, partial stacks, insufficient/exact
quantities, zero-cost returns, syntax-only validation, short-circuiting, invalid arguments,
and missing context. The first run exposed incorrectly numeric removal booleans; these
were corrected before the successful full rerun. Full reimport succeeded with zero
errors and three warnings, refreshing 237 Blueprints. Generated `training_mount` nodes
were inspected. No multiplayer, cooked-target or live quest playthrough was performed.
Logs: `Saved/Logs/QuestItemRangesTests.log`, `QuestItemRangesImport.log`.

## Completed in the NPC-purge pass

- `npc.purge()` now translates to an executable `UMT2QuestNode_PurgeNpc`; expression
  calls use the same manager operation and return Lua nil. Syntax validation and
  short-circuited expression branches do not destroy actors.
- The manager validates server authority, current pawn/player-state ownership, NPC
  authority and world identity. Player actors and invalid targets are rejected; a
  missing NPC is a valid no-op. Rejected node execution logs an error and stops the
  block rather than allowing later rewards to run after failed removal.
- The current quest NPC and initiating conversation lease are cleared before
  destruction, matching legacy `SetQuestNPCID(0)`. This prevents NPC-removal cleanup
  from cancelling the initiating script. External destruction still cancels a
  suspended conversation. Removal uses Actor destruction, not health/death handling,
  so it does not intentionally generate combat rewards, corpse behavior or resurrection.
  Existing spawn ownership/respawn handling is left unchanged.
- All ten directly reported purge statements are gone. Other dungeon/Easter/event
  helpers and failed gates remain diagnosed; their removal is not claimed.
  Standalone purge calls with argument expressions remain reported rather than
  silently omitting argument side effects. Expression lowering can evaluate native
  call arguments before invoking purge.

Legacy reference: `game/src/questlua_npc.cpp::npc_purge` clears the quest NPC ID and
uses `M2_DESTROY_CHARACTER`, unlike the separate `npc.kill` death operation.

Validation: final editor Development build and all 12 `Metin2.Quests` tests passed.
`.PlayerApi.NpcPurge` covers syntax-only validation, short-circuiting, initiating
lease cleanup, actual actor destruction, unchanged health/Yang, script continuation,
nil/no-NPC returns, context clearing, authority/pawn/player-target rejection, and
external-destruction cancellation. The initial compile exposed a missing test-friend
declaration; it was added following the existing fixture pattern before validation.
Full reimport succeeded with zero errors and three warnings; both generated purge
nodes in `BP_Quest_event_halloween_hair` were inspected. No multiplayer, cooked target,
live event quest or respawn playthrough was performed.
Logs: `Saved/Logs/QuestNpcPurgeTests.log`, `QuestNpcPurgeImport.log`.

## Completed in the entity-identity/proximity pass

- Added game-thread/server-only quest entity IDs. IDs identify actual character instances,
  not template vnums, UObject indices or persistent player IDs. Repeated reads remain stable;
  IDs are not reused within the process, including across world transitions/PIE worlds.
  Per-world weak registries invalidate IDs on destruction/EndPlay and detach delegates at teardown.
- `pc.get_vid`/`pc.vid`, `npc.get_vid`, `find_npc_by_vnum` and `find_pc_by_name` now use that
  registry. Vnum lookup includes live mobs as well as NPC subclasses. Player-name lookup is
  case-sensitive and currently searches characters resident in the calling world. Cross-map
  resident-player lookup still needs the authoritative multi-map/instance architecture; this pass
  does not claim parity for players not present in that world or implement `pc.select`.
- Added `npc.is_near_vid` against the identified character and current NPC. Corrected already
  accepted `npc.is_near`: default range 10, numeric-string arguments, integer XY coordinates,
  strict less-than comparison and legacy DISTANCE_APPROX coefficients (246*max+102*min)/256.
  Z is ignored. Missing/invalid/cross-world characters return typed false.
- Newly imported `target.vid` nodes resolve entity IDs and retain the exact actor reference;
  duplicate NPC templates no longer share target highlighting or interaction identity.
  The new enum value is appended; older serialized vnum-based target nodes retain their meaning.
  Actor targets are removed after target destruction. The subsequent target-dispatch pass adds
  explicitly subscribed arrival events using live actor positions.
  Unsupported target-ID expressions now remain reported rather than becoming silent zero targets.
- Map actor highlighting and NPC quest arrows consume exact actor identity. Owner-only quest
  marker replication now includes an actor reference; matching client/server builds are required.
  Static template-only full-map markers retain only the legacy vnum-target behavior. Replicated
  reference resolution/relevancy, target label/optional-argument handling and arrows for non-NPC
  actors still require fidelity/playtesting work. Named target selection is corrected below. Existing accepted
  `pc_find_skill_teacher_vid` and other unrelated placeholder bindings are not fixed by this pass.

Legacy references: `questlua_npc.cpp` get_vid/is_near/is_near_vid, `utils.h::DISTANCE_APPROX`,
and `questlua_global.cpp` find_npc_by_vnum/find_pc_by_name. Other calls in the same rejected
conditions remain diagnosed (e.g. flame_dungeon now identifies missing `d.get_unique_vid`).

Validation: final editor Development build and all 13 `Metin2.Quests` tests passed.
`.PlayerApi.Entities` covers distinct duplicate-template instances, stable/non-reused IDs,
world isolation, authority, destruction and EndPlay-delegate cleanup, typed reads/proximity,
default/negative/strict-boundary ranges, Z independence, opponent-versus-NPC comparison,
case-sensitive name lookup, exact actor targets, marker cleanup and old vnum-node compatibility.
EndPlay cleanup was tested by invoking the lifecycle delegate, not by streaming a live level.
Full reimport refreshed 237 Blueprints. The first follow-up asset inspection used the source-file
name rather than the internal quest name; the verification request was corrected to the existing
`BP_Quest_collect_quest_lv30` asset. No live replication, map travel, cooked target or end-to-end
arena/marriage quest validation was performed.
Logs: `Saved/Logs/QuestEntitiesTests.log`, `QuestEntitiesImport.log`.

## Named target dispatch pass

- Importer now distinguishes `name.target.click`, `.arrive` and `.die` instead of discarding
  the verb. New TargetClick/TargetDie enum entries are appended, preserving existing serialized
  event ordinals; the old Target event remains a fallback for pre-reimport definitions.
- Named event dispatch now actually filters TriggerName. Previously EventName was passed in but
  unused, so a timer could run an unrelated timer block and arrivals could run another target block.
- Target interactions are scoped to the marker's quest and exact entity and run before ordinary
  NPC/shop menus, as in `questmanager.cpp::Click` and `questnpc.cpp::OnTarget`. An arrival trigger
  is no longer offered as an NPC click option. Target clicks retain the marker: cleanup belongs
  to the script's target.delete rather than an implicit menu-side deletion.
- Arrivals use live actor XY or a fixed position (including world origin), integer-coordinate
  DISTANCE_APPROX and the source's <=500 radius. They retry while a marker remains, so a failed
  gate cannot permanently consume the event. Suspended conversations are not overwritten.
  Disappeared tracked entities dispatch the quest/name-matched die handler before marker removal,
  matching `target.cpp::target_event`. Current timer cadence remains the existing one-second poll,
  not the old adaptive schedule. Older vnum-only markers lack a unique actor position and retain
  click-only behavior until reimport.
- Unknown target verbs stay diagnostic. `__TARGET__target.click` and `__TARGET__.click` are source
  spellings lacking the actual target-event grammar; they are not silently rewritten into target
  events. Legacy qc.cc treats their prefixes as ordinary object names, not named quest targets.
- Existing FName-based identifiers remain case-insensitive, unlike Lua's string identities.
  Non-NPC interaction routing, target labels/optional arguments, cross-map position targets,
  actor network relevancy and cooked/multiplayer behavior remain fidelity work; this pass does not
  claim those are finished or turn failed trigger gates into successful imports.

Validation: final editor Development build and all 14 `Metin2.Quests` tests passed. New
`.Importer.TargetDispatch` creates transient quest Blueprints and exercises real manager dispatch:
distinct verbs, rejected malformed/unknown target syntax, wrong-name first triggers, quest/name/actor
isolation, target-over-shop priority, retained markers, named timer filtering, live actor XY arrivals,
approximate-distance rejection, repeat arrivals, failed-gate retries, origin targets and entity
disappearance cleanup/dispatch. No shared quest CDOs are mutated by the fixture.
Full reimport succeeded (zero errors, five warnings; one asset MoveFile failure recovered on retry),
and reloaded `BP_Quest_find_senior_soldier` contains distinct named TargetDie and TargetClick
triggers. Coverage remains 504 unconverted statements, 67 failed gates, 27 unsupported triggers;
the zero-unconverted goal is not achieved. Source whitespace checks passed. No networked/cooked
target or end-to-end quest playthrough was performed.
Logs: `Saved/Logs/QuestTargetDispatchTests.log`, `QuestTargetDispatchImport.log`.

## Pre-dispatch trigger-gate pass

- Converted runtime-helper `with` clauses now serialize GatePrelude and GateConditions separately
  from the body. Previously their helper and If nodes lived inside Trigger.Nodes, after the trigger
  had already claimed an event or become an NPC option. A false gate could therefore swallow an
  event or present an unavailable option. All declarative dispatch paths now probe the gate first.
- Probes use a fresh local-variable scope and seed the quest's constants, including pure-condition
  probes that previously evaluated before constants were seeded. Context, caller locals, iterator
  budgets and buffered text are restored afterwards. Helper-generated temporaries and body locals
  do not leak between probes/body execution; the importer no longer accepts body locals in gates.
- Synchronous helpers retain real flag/global side effects, but a failed gate rolls back newly
  acquired NPC leases. Successful helper locks survive until conversation completion/cancellation.
  Normal event re-entry is guarded during the probe. Dialogue/suspension attempts are rejected with
  an explicit runtime error and cannot leave an open dialog or suspended call stack.
- Existing false conditions for unsupported/recovered-incomplete triggers remain in force and
  are checked before helper execution. This does not convert the 67 rejected gates or replace their
  diagnostics with successful imports. Old serialized body-side gate wrappers require reimport;
  all 237 active generated quest Blueprints were refreshed here.

Legacy evidence: `questlua.cpp::IsScriptTrue` executes on the main Lua state via lua_dobuffer;
`questnpc.cpp` checks with conditions before offering/dispatching the associated script.
Presentation side effects inside a gate are intentionally rejected by the current runtime;
arbitrary Blueprint-defined gate helper/presentation parity is not claimed.

Validation: editor Development build and all 15 `Metin2.Quests` tests passed. New
`.Importer.TriggerGates` lowers a real helper and exercises direct probes, unavailable NPC options,
accepted NPC options, one-time helper execution, body isolation, constants, scoped quest-button
dispatch, broadcast fallback after false gates, successful lock retention, failed lock rollback and
rejected suspension cleanup. Full reimport succeeded with zero errors; nine helper-backed gates
were serialized (main_quest_lv55, make_wonso and subquest_48). A subsequent reimport/reload verified
four separate helper preludes/result conditions in `BP_Quest_main_quest_lv55`. The final import
reported seven warnings, including recoverable asset MoveFile retries. Report remains 598 entries:
504 unconverted statements, 67 failed gates, 27 unsupported triggers. Source whitespace checks
passed. No live multiplayer, cooked-target or end-to-end quest playthrough was performed.
Logs: `Saved/Logs/QuestTriggerGateTests.log`, `QuestTriggerGateImport.log`.

## Item metadata query pass

- Added item.get_level_limit using the offered/current inventory item's imported Limits. Like
  questlua_item.cpp, its arguments are ignored, non-weapon/armor and absent items return nil, and
  weapon/armor items without a level limit return numeric zero. The first matching LIMIT_LEVEL
  value wins (CItem::GetLevelLimit), rather than a maximum or the unrelated refinement level.
  This converts energy_system's previously rejected levelLimit assignment and preserves its
  explicit nil rejection branch.
- Added item.get_refine_vnum from the current item's RefinedVnum and item.next_refine_vnum as an
  independent proto lookup through the existing vnum registry. The latter works without a current
  event item, accepts numeric strings, uses existing imported vnum-range resolution, and returns
  zero with an explicit diagnostic for unknown protos, matching the legacy lookup's failure path.
- No item persistence, instance identity, network fields or template assets were changed. This
  does not implement item.select/get_id, SIG-item queries, give_item2 result identity, general
  multiple returns or function-value arguments. Other item.type/subtype fallback limitations and
  suspended-item identity checks remain part of the broader runtime audit.

Validation: final Editor Development build and all 16 Metin2.Quests tests passed. New
.Importer.ItemMetadata exercises real inventory/registry resolution using transient, strongly held
template Blueprints (no existing CDO mutations), level limits, nil/zero distinction, ignored
arguments, refinement queries, numeric strings, vnum aliases, absent items and missing-proto
diagnostics. It also translates and executes the actual energy_system assignment syntax.
Full reimport refreshed all 237 active quest Blueprints; the report has 503 unconverted statements,
67 failed gates and 27 unsupported triggers. No live energy-system quest playthrough, dedicated
server, multiplayer or cooked build was performed.
Logs: Saved/Logs/QuestItemMetadataTests.log, QuestItemMetadataImport.log.

## Default NPC shop call pass

- No-argument `npc.open_shop()` now translates to the existing NPC stock/window backend rather
  than an unconverted node. The owner-client RPC is requested by the authoritative player only;
  stock availability, same-world ownership, interaction distance and active player trade are checked.
- Legacy `questlua_npc.cpp::npc_open_shop` returns no Lua values and does not terminate the caller.
  Imported calls therefore continue, even when opening fails. Existing interaction-menu OpenShop
  nodes retain their original close-and-stop default through a new serialized boolean property.
- Explicit shop-vnum calls remain diagnostic: current stock lives on each NPC's component, and
  there is no independently addressable stock-table backend. No shop-vnum is ignored or mapped to
  an unrelated default stock. Existing buy/sell server RPCs and pricing are unchanged.
- Full legacy shop-session parity is not claimed. Safebox/cube/private-shop exclusion, persistent
  shop-owner session tracking, exact legacy distance and other-empire pricing remain shop-backend
  fidelity work; the new opening check uses the existing project's shop interaction range.

Validation: Editor Development build and all 17 `Metin2.Quests` tests passed. `.Importer.ShopCall`
checks real translation, continuation into the following assignment, missing NPC behavior,
existing menu-action compatibility, explicit-stock rejection, authoritative nearby stock access,
missing stock, absent player and range rejection. It executes the server node's RPC request with
an unpossessed test pawn, not a live client UI. Full reimport refreshed 237 Blueprints with zero
errors and three warnings; all six no-argument calls disappeared from diagnostics. Reloaded
`BP_Quest_fisher` was verified to contain `OpenShop continueQuest=true`. The report
retains 497 statements, 67 failed gates and 27 unsupported triggers. No live shop UI, networked
session, cooked target or end-to-end quest playthrough was validated.
Logs: `Saved/Logs/QuestShopCallTests.log`, `QuestShopCallImport.log`.

## Warp routing / empire-village pass

- `warp_to_village()` now emits a real Warp node. It uses the three global coordinates from
  legacy `start_position.cpp::g_start_position`, not the map's Town.txt respawn location, and
  resolves those through imported map metadata. Same-map movement uses the existing ground trace;
  cross-map movement uses the existing PIE/server transfer and pending-spawn backend.
- Corrected a silently accepted API mismatch: `pc.warp_local(map_index, x, y)` previously dropped
  the third argument and treated map_index as X. New nodes retain all three arguments, resolve the
  requested region, add unscaled local coordinates to its global origin, then mirror into Unreal
  coordinates. Legacy upper-bound checks and truncation toward zero are retained. Numeric strings
  are accepted; booleans and non-finite/out-of-range numeric inputs are rejected.
- `pc.warp(x, y[, private_map_index])` now retains and evaluates its optional third argument.
  Base indices below 10000 are ignored like `CHARACTER::WarpSet`; private indices are explicitly
  rejected with a runtime error, rather than silently sending the character to the public map.
  Private-instance routing remains unfinished and is not counted as working gameplay. Failed
  destination resolution stops the block with a diagnostic, rather than executing an invented
  destination. This fail-stop differs from legacy void-call continuation on failed WarpSet.
- Existing authored nodes keep the old `bCoordinatesAreLocalMetres` path; new serialized flags
  are false by default. Full reimport migrates generated local-warp nodes to correct semantics.
  No player persistence or map-transfer RPC schema changes were introduced.
- `pc.can_warp`, scalar boolean-returning `pc.warp`, `pc.set_warp_location`, exit-location state,
  delayed/broadcast warps, private dungeon routing, and legacy trade/refine/safebox warp cooldown
  parity remain work. This pass does not claim pony-training or event-map quests are fully playable.

Validation: final Editor Development build and all 18 `Metin2.Quests` tests passed. The new
`.Importer.WarpRouting` test translates actual call shapes and checks destination resolution using
isolated map fixtures: map-index/X/Y ordering, unscaled offsets, numeric-string conversion,
truncation, mirroring, global/relative equivalence, optional nonnumeric/base indices, private-index
rejection, coordinate overflow, unknown regions, boolean rejection, all three empire starts,
invalid empire and malformed local-warp arity. These are routing tests, not live travel tests.
Full reimport refreshed 237 Blueprints with zero errors and three warnings. Reloaded
`BP_Quest_pony_levelup` contains map-local Warp nodes with map index and both source coordinates;
`BP_Quest_entry_event_map` contains empire-village Warp nodes. The two direct village calls and
the event-map helper call no longer appear in diagnostics. Coverage is 494 statements, 67 failed
gates and 27 unsupported triggers. Source whitespace checks passed. No multiplayer PIE, dedicated
map-server transfer, ground-placement playtest, cooked build or full quest playthrough was performed.
Logs: `Saved/Logs/QuestWarpRoutingTests.log`, `QuestWarpRoutingImport.log`, `QuestVillageWarpImport.log`.

## Party flag ownership pass

- Added native `party.setf` and standalone-call lowering through the existing expression frames.
  Arguments are captured before execution; dynamic names, numeric-string values and nil returns
  follow `questlua_party.cpp::party_set_flag`. Values truncate toward zero. Missing party and
  invalid Lua argument types do not mutate state; non-finite/out-of-range integers fail evaluation.
  Mutation requires authoritative player/party state in the same world.
- Shared `party.getf/setf` keys now have explicit case-sensitive comparison and hashing, matching
  the legacy `std::string` flag map. Lua numeric names are converted to strings. Existing FName
  accessors remain available for source compatibility; the native Lua path no longer loses case
  through FName or Unreal's default FString map comparison.
- Corrected previously accepted `party.setqf`: it writes each locally online member's own quest
  manager, or the caller's when solo, instead of writing the shared party map. Member flags use
  the caller's quest scope even for dotted names; quoted/computed names evaluate at runtime.
  Value conversion uses legacy rint-style ties-to-even rounding, not setf's truncation. Existing
  manager writes mark each member's persistence dirty; shared party flags remain transient.
- Shared flags are server-local to the current party actor, like legacy CParty's flag map. Dungeon
  assignment/reset and migration lifetime still need the dungeon backend; this does not finish
  flame-dungeon gameplay. Personal quest flag keys retain the wider runtime's FName limitations.
  General quest-local argument lowering and ignored-extra-argument fidelity of older statement
  handlers remain audit work, including the existing setqf handler outside the tested call shapes.
  No player-save or replicated-party snapshot schema was changed.

Validation: final Editor Development build and all 19 `Metin2.Quests` tests passed. New
`.Importer.PartyFlags` checks actual party initialization, shared member visibility, case-sensitive
keys, numeric names, dynamic lowering, numeric-string truncation, nil results, rejected argument
types, short-circuit mutation skipping, player-versus-party storage separation, per-member quest
scoping, dotted names, positive/negative half-even rounding, solo fallback and unsupported argument
diagnostics. Initial failures exposed default string-map comparison and quoted-name bugs; both
were fixed before successful reruns. A concurrent first test/import run also hit a registry-save
error; final tests and import were run sequentially and completed without errors.
Full reimport refreshed 237 Blueprints with zero errors and three warnings. Reloaded
`BP_Quest_flame_dungeon` contains the lowered native `party.setf` assignment; its direct setter
diagnostic disappeared. Current coverage is 493 statements, 67 failed gates and 27 unsupported
triggers. Source whitespace checks passed. No multiplayer PIE, coordinator/migration round-trip,
player-save round-trip, cooked targets or end-to-end dungeon playthrough was performed.
Logs: `Saved/Logs/QuestPartyFlagsTests.log`, `QuestPartyFlagsImport.log`.

## Skill predicates / book-delay affect pass

- Added `pc.has_master_skill` using actual PlayerState skill entries. Like
  `questlua_pc.cpp::pc_has_master_skill`, it checks only legacy IDs below 255 and
  requires both Master-or-higher grade and level >=21. M1 (level 20) remains false.
  The new skill system derives mastery from level, whereas legacy saves stored
  mastery separately; this pass does not introduce a new save/replication field.
- Added `pc.is_skill_book_no_delay` and `pc.remove_skill_book_no_delay` against the
  existing status-effect component and verified legacy affect ID 513. The query
  returns a Lua boolean; removal requires server authority, consumes only that
  affect, and returns nil. Existing affect replication and change notifications
  continue to drive UI and player persistence dirtying. Absent character context
  returns false/nil rather than mutating another player.
- Standalone removal calls lower through native expression frames, rather than
  disappearing. Short-circuit expressions do not consume the affect.
- At this stage `pc.learn_grand_master_skill` remained explicitly unsupported. Its existing
  `TrainGrandMasterSkill` backend is not equivalent to legacy LearnGrandMasterSkill:
  legacy checks proto/learnability/type, consumes the book bonus by halving the
  denominator, sets a per-skill read deadline, and uses quest-scoped cumulative
  read counts. The current backend clears read counts after success and omits
  several of those operations. Binding it unchanged would falsely claim parity.
  Skill reset and soulstone quests are not asserted fully playable by this pass.

Validation: Editor Development build and all 21 `Metin2.Quests` tests passed.
New `.Importer.MasterSkill` checks empty/reset/missing state, levels 19/20/21/29/
30/39/40, ID 254 versus 255, secondary skills, boolean types, and translated
reset-branch evaluation/assignment. `.Importer.BookDelay` uses real restored
affects to check presence, nil results, removal, unrelated bonus retention,
short-circuit skipping and execution of the translated standalone call/query.
Full reimport refreshed 237 Blueprints with zero errors and three warnings;
reloaded `BP_Quest_training_grandmaster_skill` retains the native removal call.
The master predicate and two bypass predicate diagnostics disappeared. The
report still contains 67 failed gates and 27 unsupported triggers; none were
hidden. Source whitespace checks passed. Multiplayer PIE, client/server cooked
builds, persistence round-trip and complete quest playthrough remain untested.
Logs: `Saved/Logs/QuestSkillPredicatesTests.log`, `QuestSkillPredicatesImport.log`.

## Grand-master learning pass

- Bound `pc.learn_grand_master_skill` to a corrected server-authoritative skill backend.
  Numeric strings are accepted and fractional IDs truncate toward zero; wrong Lua types
  return nil, and valid calls return a Lua boolean. Non-finite/out-of-range conversion
  fails explicitly. Native standalone calls also lower through expression frames.
- Preserved raw legacy proto type in `UMT2SkillDefinition::LegacySkillType`, rather
  than guessing job ownership from the client UI category. Eligibility matches the
  inspected IsLearnableSkill/GM checks: GM mastery, legacy ID bounds, non-support
  proto, job/group ownership, Assassin-only ranged horse skill, and exclusivity
  within each of the two anti-skill families. Definitions outside displayed skill
  sets can be resolved through the existing cooked skill directory.
- The skill importer and `MT2ImportSkills -LegacyMetadataOnly` refresh only this
  metadata on existing definitions. Missing assets/save failures are errors; this
  mode does not create incomplete definitions or rewrite sets, icons, curves or
  animations. All 77 definitions were refreshed successfully, with no sets or
  animations changed. Newly authored/imported definitions must carry the raw type.
- Accepted attempts increment `training_grandmaster_skill.skill<Vnum>` even on
  failure and retain the cumulative count after level-up. Quest flags are canonical;
  the existing JSON skill tally remains readable/writable for compatibility. Old
  tallies migrate after quest flags load, without overriding an existing flag.
  An initial regression test exposed stale fallback after setting the flag to zero;
  canonical reads and load-time migration fixed it. No new player-save field was added.
- Book bonus consumes affect 512 and halves the roll denominator with rounding up;
  it is not unconditional success. Minimum/maximum read thresholds retain the legacy
  boundaries. Accepted success/failure writes a random 8..12-hour per-skill deadline.
  This native API does not enforce that deadline or consume no-delay affect 513:
  the legacy native check is commented out, and the calling quest owns next_time.
- Fixed config array entries to retain the repeated values in all ten denominator/
  minimum-read grades. Missing/invalid grade settings now produce an error and reject
  training before mutation, instead of inventing fallback thresholds. The existing
  native soulstone menu now uses the same eligibility checks before consuming items;
  its simplified confirmation/karma policy is still not full legacy quest parity.

Validation: final Editor Development build and all 22 `Metin2.Quests` tests passed.
New `.Importer.GrandMasterTraining` covers actual skill/affect data, configured table
duplicates, job/group/special-skill eligibility, invalid argument types, truncation,
failure counters/deadlines, bonus consumption, forced maximum-read success, minimum
reads, retained cumulative counts, imported call execution and short-circuit skipping,
counter overflow, malformed settings and quest-flag JSON restore/old-save migration.
The malformed-settings test intentionally expects its error diagnostic. This is a
focused flag JSON round-trip, not a full persistence backend/reconnect test.
Full reimport refreshed 237 quest Blueprints with zero errors and three warnings;
reloaded training quest contains the success/failure branch bodies. Removing the
learning predicate diagnostic exposed two nested `pc.change_alignment` calls, so
the count rose from 490 to 491 rather than being artificially reduced. Both remain
explicit diagnostics at that stage along with `pc.get_real_alignment` and string-pattern calls.
Legacy alignment uses tenth-point storage, masked versus real alignment and a
different clamp from current whole-point karma; binding it by naive rounding would
lose behavior. The following pass implements alignment storage/masking. Native localized training chat,
global skill-disable/polymorph progression gates and full soulstone playthrough
remain wider runtime parity work. No multiplayer PIE or cooked client/server build
was performed. Source whitespace checks passed.
Logs: `Saved/Logs/QuestGrandMasterMetadataImport.log`, `QuestGrandMasterTrainingTests.log`,
`QuestGrandMasterTrainingImport.log`.

## Alignment pass

`pc.get_real_alignment`, `pc.get_alignment` (and the existing `pc.get_align` alias),
`pc.change_alignment` and `pc.changealignment` now use real server-owned alignment.
Raw storage retains tenths; Lua reads truncate whole points toward zero, while mutations
multiply by ten before truncation and apply the legacy +/-200,000 raw clamp. Existing
whole-point karma APIs retain their +/-30,000 limits and penalties preserve fractional remainders.
Wearing item 70048 masks public alignment without changing authoritative PvP eligibility.
Real raw alignment replicates owner-only; observers receive the public masked value.
The replication layout changed, requiring matching client/server builds and an editor restart.

The existing JSON `karma` field and SQLite column retain whole-point units but now preserve
fractions. SQLite INTEGER affinity permits these REAL values without a schema migration.
The persistence test saves and reopens both an old whole-point value and a fractional value,
then verifies player restore and capture while masked. Alignment tests cover aliases, fractional
accumulation, equipment masking, negative-alignment PvP, authority, clamps, nil returns and
short-circuiting. Complete expression syntax is now validated before native calls can mutate state;
malformed numeric/trailing syntax is rejected without an alignment change.

Editor Development build succeeded. All 25 quest tests and `Metin2.World.SafeZones` passed,
with no unexpected errors. Full import refreshed 237 Blueprints with zero errors and three
warnings; the reloaded grand-master quest contains both alignment mutations and its real read.
Remaining string-pattern calls, localized training chat and progression gates are not declared ported.
No multiplayer PIE, cooked targets or live reconnect/chat presentation was tested.
Logs: `Saved/Logs/SafeZonesAlignmentFinal.log`, `QuestAlignmentImport.log`.

## String formatting/case pass

The accepted `string.format` placeholder previously returned its template without substitution.
It now executes the Lua 5.0.3 conversion set (`c`, `d`, `i`, `o`, `u`, `x`, `X`, `e`, `E`, `f`,
`g`, `G`, `q`, `s`, and `%%`), preserving flags, two-digit width/precision limits, truncating
integer conversions, numeric-string coercion, byte-oriented string width/precision and legacy
quoting. The legacy long-string/no-precision shortcut is retained. Invalid formats, missing/wrong
arguments and unsafe integer conversions explicitly fail evaluation rather than return a template.
Formatting output is bounded to 1 MiB. Floating formatting permits infinities; integer casts do not.

`string.lower` and `string.upper` implement the legacy server's C-locale ASCII case conversion,
preserving non-ASCII UTF-8 bytes. `string.len` now returns UTF-8 byte length and accepts Lua's
numeric-to-string coercion instead of returning UTF-16 length or silently zero for invalid types.
References: legacy `liblua/include/lua.h` (5.0.3), `liblua/src/lib/lstrlib.c`; the original game
source has no `setlocale` call. These are not Unicode case-folding APIs.

Display conversion now preserves full format/concatenation expressions and resolves localized
arguments before runtime parsing. Removed guessed printf substitutions in ordinary concatenation:
Lua concatenation must leave literal `%s` text unchanged. Expression-token scanning respects quoted
braces/escapes and reads the original template only; returned player input cannot become a second
executable expression. Named display placeholders are not substituted inside Lua string literals.
`[ENTER]` splitting happens after expression evaluation, preserving multiline format calls and
empty formatted Say lines. Failed text expressions emit a runtime warning.

New `.PlayerApi.Strings` and `.Importer.Strings` tests cover conversion families, flags/precision,
quoting, type coercion/errors, UTF-8 byte length, non-ASCII case preservation, overflow, localized
assignments, imported multiline Say execution, empty Say output, quoted braces, non-recursive player
input and ordinary concatenation. An initial test exposed a missing C-string terminator in a
length-delimited UTF-8 conversion; it was fixed before the successful final run. Inspecting the
first regenerated arena asset also exposed premature `[ENTER]` splitting, covered by the Say test.

Editor Development build succeeded; all 27 quest tests plus `Metin2.World.SafeZones` passed without
unexpected errors. Full reimport refreshed 237 Blueprints with zero errors and three warnings.
Reloaded `BP_Quest_arena_manager` retains intact multiline format expressions. The report remains
27,520 nodes, 488 statements, 67 failed gates and 27 unsupported triggers (582 entries): this pass
corrects accepted runtime behavior rather than hiding diagnostics. No live arena dialogue,
multiplayer PIE or cooked client/server build was performed. Existing landscape deprecation
warnings remain unrelated to this change.

At this stage `string.gsub` was still unsupported, including all four grand-master
confirmation-normalization statements; the following pass implements string replacements.
General byte-string values also remain unresolved: FString cannot
represent invalid UTF-8 output from arbitrary `%c` bytes or precision cutting a multibyte character.
Such lossy conversions currently fail explicitly; this is not claimed as full arbitrary Lua string
parity. Pattern/function replacements and multiple-return behavior need the broader runtime work.
Logs: `Saved/Logs/QuestStringsDialogueTests.log`, `QuestStringsDialogueImport.log`.

## Lua pattern/string-replacement pass

`string.gsub` now evaluates string/numeric arguments against a reusable byte-pattern matcher
adapted from the original Lua 5.0.3 `lstrlib.c`, with its license retained. This is not a regex
translation or a special-case whitespace remover. It supports C-locale classes and complements,
bracket classes/ranges/escapes, `.`, anchors, optional/greedy/non-greedy repetition, balanced pairs,
frontiers, captures, capture backreferences and position captures. Replacement strings support
captured strings and positions, escaped replacement characters and optional maximum substitutions.
Lua 5.0 rejects replacement `%0`; it is not silently given newer Lua's whole-match semantics.
Empty matches and anchored substitutions follow the source algorithm, including the final empty
match. Failures distinguish no match from malformed patterns or invalid capture references.

The helper snapshots its inputs before clearing outputs (supporting in-place replacement), and
only publishes the completed result/count. It enforces 32 captures, 128 recursive frames, one
million work units and 1 MiB output. Work accounting includes bracket scans and capture comparisons,
not only backtracking. Exceeding a limit fails evaluation rather than report a partial success.
Invalid UTF-8 results still fail explicitly because the existing value model uses FString.

Both `.PlayerApi.Patterns` and `.Importer.Patterns` passed. Tests cover the pattern families,
greedy/non-greedy and capture rollback paths, byte classes/non-ASCII preservation, match limits,
empty input/matches, malformed patterns/capture references, position captures, table-replacement rejection,
work bounds, in-place replacement and the nested normalization expression. The importer test
translates and executes all four actual grand-master confirmation statements with localized input.
Unsupported replacement calls remain diagnostic. At this historical stage, multi-target native
assignments remained diagnostic rather than becoming one comma-named scalar variable; they are
implemented by the native-result-list pass below.

Editor Development build succeeded; all 29 quest tests plus `Metin2.World.SafeZones` passed with
zero unexpected errors. Full import refreshed 237 Blueprints with zero errors and three warnings.
Reloaded `BP_Quest_training_grandmaster_skill` contains all four string replacement/case calls.
Statements fell from 488 to 484; gates (67) and unsupported triggers (27) are unchanged.
No live soulstone dialogue, multiplayer PIE or cooked target was tested.

At this historical stage, expression calls exposed only the scalar text result. Native multiple-return
expansion and `string.find` are implemented below; first-class function replacements remain. Lua 5.0
does not accept table replacements; function replacements returning a non-string remove the match,
unlike later Lua versions' keep-original behavior. General function values and arbitrary byte-string
storage remain required for full parity, rather than hiding these limitations behind this pass.
Logs: `Saved/Logs/QuestPatternsBoundedTests.log`, `QuestPatternsImport.log`.

## Native result lists and simultaneous assignments

Native calls now carry transient result-list metadata, without changing serialized script values.
`string.gsub` returns text/count; `string.find` returns byte-based, one-based inclusive bounds and
string/position captures, or one nil when absent. The matcher follows the bundled Lua 5.0.3 search
algorithm, including relative/oversized starting offsets, plain-search truthiness and empty matches.
Search uses the existing bounded pattern engine; malformed patterns and lossy UTF-8 captures fail
explicitly. This does not implement arbitrary Lua byte strings.

Only a final call expands in expression/argument lists or the final positional table-constructor
field. Parentheses, logical operators, named fields, variables and stored table elements scalarize.
The three verified zero-return bindings (`pc.change_alignment`, its alias, and
`pc.remove_skill_book_no_delay`) produce empty lists. Other native return counts still need auditing.

`AssignValues` evaluates every RHS before any target writes, keeps excess RHS side effects, pads
missing results with nil and applies targets left-to-right, with existing local/quest scope rules.
Syntax is validated across the complete RHS list before native calls execute. Runtime evaluation
failure stops the assignment without target writes; earlier native side effects are not rolled back.
Only bare variable targets are implemented here; table-field target capture remains required.

The old two-query mount special case is removed. Reloaded `BP_Quest_ride` has two targets (`vnum`,
`remain_time`) and one RHS (`pc.get_special_ride_vnum()`), matching the legacy two-result binding.
Existing SetVariable assets remain compatible; the new reflected node requires a rebuilt/restarted
editor. At this stage quest-local function frames still carried scalar results; direct multiple
destinations are implemented below. Resumable expression lowering still scalarizes nested quest calls.

Editor Development build succeeded and all 32 quest/safezone tests passed. Tests cover native result
expansion, zero results, duplicate targets, swap semantics, nil padding, excess RHS execution,
table/argument scalarization, failed assignments and the actual date-parser statement.
Full import refreshed 237 Blueprints with zero errors and three warnings; counts remain 484 statements,
67 failed gates and 27 unsupported triggers. No live networked dialogue or cooked target was tested.
Logs: `Saved/Logs/QuestResultListsTests.log`, `QuestResultListsImport.log`.

## Lua type predicates

`type(value)` returns the legacy type name for nil, boolean, number, string and runtime/static table
values without converting numeric strings to numbers. Explicit nil is valid; an absent argument
fails evaluation, following `lbaselib.c::luaB_type`. Extra arguments are evaluated by the normal call
list rules and do not change the first argument's type. This does not introduce first-class function,
userdata or thread values; those remain outside the current value model.

Full reimport removed the level-up reward type-branch diagnostic and one dragon-lair-access
diagnostic, reducing statements from 484 to 482. Reloaded `BP_Quest_levelup` contains both reward
paths beneath the formerly rejected branch. Other reported level-up/dungeon statements remain.
Focused expression tests cover every supported type, explicit nil, absent arguments, numeric strings,
native multiple results and ignored extra arguments. The importer test translates and executes
table/scalar reward branches and confirms the selected vnum/count and skipped alternate paths.
Its initial fixture omitted ActiveContext initialization; that was corrected before the successful
full rerun. Editor Development build and all 32 quest/safezone tests passed with no unexpected errors.
Import refreshed 237 Blueprints with zero errors and three warnings. No end-to-end level-up reward,
multiplayer PIE or cooked client/server run was performed.
Logs: `Saved/Logs/QuestTypesValidatedTests.log`, `QuestTypesImport.log`.

## Quest-function result lists

CallFunction nodes and their executor frames now support explicit result destinations/scopes.
Return nodes evaluate an ordered expression list, expanding only its final native call. Function
arguments use the same list semantics before parameter binding, evaluating ignored extras and padding
absent parameters with nil. Result destinations publish left-to-right only after frame unwinding
restores parameters; cancellation/runtime failure does not publish a successful return. Empty bodies
and implicit/bare returns provide zero values, padding each requested destination with nil.
Existing scalar destination and ValueExpression properties remain readable for older assets.

The importer translates a complete unparenthesized quest-function call into multiple bare-variable
targets and preserves local versus quest result scopes. It rejects mixed resumable RHS lists instead
of dropping a trailing expression. Bare local declarations now initialize to nil, not zero.

Editor Development build and all 32 quest/safezone tests passed. Coverage includes suspended/resumed
returns, same-named parameters/destinations, native tail expansion, duplicate targets, nil padding,
implicit/empty functions, cancellation, failed returns and malformed destination scopes. The importer
test translates and executes the date parser's actual five-capture return after suspension.
Full import refreshed 237 Blueprints with zero errors and five warnings; a temporary pony-levelup
asset MoveFile failure recovered on retry. Counts remain 482 statements, 67 gates and 27 triggers.
The actual daily-gift manager still rejects its repeat/until select conditions, so the saved asset
does not yet contain this date-getter call. It is not declared fully converted or playtested.

Remaining requirements include return/argument packets through nested resumable expression lowering,
ordered mixed RHS calls, table-field target capture and resumable loop conditions (including select).
No live networked dialogue, multiplayer PIE or cooked client/server validation was performed.
Logs: `Saved/Logs/QuestFunctionResultsFinalTests.log`, `QuestFunctionResultsImport.log`.

## Remaining work, in recommended order

1. **Importer/control-flow and table semantics.** Named table callbacks,
   closures, general function values, nested/resumable function result-list propagation and captured table-field
   assignment targets remain. Examples: `cube_opener_list`, `levelup`, `ride_ticket_change`, and
   `training_grandmaster_skill`. Some rejected conditions report an empty unsupported reason;
   investigate nested-body conversion/rollback and variable scoping before adding new API names.
   Extend resumable expression lowering to the remaining consumers (especially repeated loop
   conditions and constructor entries). Pre-dispatch helper gating is now corrected, but the
   remaining rejected gates still need their actual predicates/bodies implemented.
2. **Trigger resolution.** Reported failures include item `.pick`/`.sig_use`, named item-scroll
   `.use`, and named quest-target `.click` variants. Investigate their dispatch identity and source
   constants, not just keyword acceptance. Otherwise a trigger can be imported but never dispatched.
3. **Dungeon/instance support.** `d.getf`, `d.select`, `d.find`, unique mobs, regen files, party
   instancing, instance warps and party dungeon state need an authoritative instance model. The
   report contains 164 entries mentioning `d.`; some are whole conditional bodies or function calls.
   Do not bind these to dummy flags or a global map and count them as ported.
4. **Gameplay-backed quest API.** Dragon Soul qualification/refinement (`ds.*`), pets, marriage,
   guild wars/buildings, forked-road events, horse stamina, remaining grand-master quest dependencies and
   safebox/mall operations still appear in the report. Each needs its corresponding gameplay system
   and ownership/authority checks rather than inert node implementations.
5. **Audit already accepted runtime bindings.** Examples still returning placeholders include
   `pc.in_dungeon`, `pc.is_polymorphed`, marriage/engagement predicates, `pc.get_wear` and
   `pc.get_part`. Coordinate reads need parity review; NPC locks are now authoritative, but pending
   broadcast delivery and dialogue-generation validation still need review.
   Conversion-report reductions alone cannot measure these gaps.

Highest report-entry counts after this pass: `deviltower_zone` 52, `devilcatacomb_zone` 51,
`marriage_manage` 45, `guild_building_melt` 41, `forked_road` 36, `flame_dungeon` 32,
`levelup` 29, `hair` 23.

Recommended next bounded slices: lexical scope, remaining resumable expression contexts,
general function values and multi-return. Ticket exchange also needs authoritative `item.select`
for `ride_ticket_change`. The separate cube-opening command still needs an existing gameplay/UI
backend before it can be translated safely.

## Historical safety and compatibility notes

Most pre-existing unconverted nodes return Continue: surrounding converted rewards/state changes
may still run. In this pass, unsupported ipairs bodies and the item-copy transaction are explicitly
marked `bStopExecution`; they stop their block and log the unconverted source location.
Partially imported quests need whole-trigger review before production enablement.

No saved-data or RPC schema changes in this pass. Previously persisted `get_time()` deadlines based
on world uptime now appear expired under Unix time; existing affected cooldowns may become available
once rather than retaining their previous incorrect deadline. Quest flags remain signed 32-bit,
including timestamps (the existing 2038 limitation is not addressed here).

`is_test_server()` still returns false in the current runtime, so normal-delay branches execute;
there is not yet a configurable test-server mode.

## Historical validation record

- `UnrealLongjuEditor Win64 Development` build succeeded. Existing unrelated UI/landscape API
  deprecation warnings remain.
- `Metin2.Quests.PlayerApi.Cooldown` and `.Resources` automation tests both passed. The asset-free
  spawned character emits missing skeletal-mesh socket warnings; no test failures occurred.
- `Metin2.Quests.PlayerApi.Ipairs` also passed: suspended/resumed iteration, source rebinding,
  nested shadowing, owned escaped/returned rows, first-hole termination, zero entries, empty
  sequences, early return, per-row dialogue text, iteration-cap abort, and stopping transaction
  behavior. Suspension was tested by resuming executor frames, not by a live networked dialogue UI.
- Full quest re-import succeeded with zero errors. Generated `BP_Quest_new_quest_lv80` structure
  was inspected through the commandlet's verification output.
- Subsequent full import succeeded with zero errors and verified both generated
  `MT2QuestNode_ForEach` nodes in `BP_Quest_cube_opener_list` before the item-copy pass.
- Item-copy pass: editor Development build succeeded; all four `Metin2.Quests.PlayerApi` tests
  passed. `.ItemCopy` covers atomic success/failure, bonuses, broken-stone removal, ranged destination
  vnums, source modification, material shortage, blocked/page-crossing footprints, absent templates
  and stacked-source rejection. A further case uses the actual imported armor and stone Blueprints.
- Full item-copy re-import succeeded with zero errors and verified the generated copy node and
  NPC-filtered ItemTake trigger in `BP_Quest_cube_opener_list`. Inspection also found the missing
  multiline cube-opening trigger described above.
- Source diff whitespace validation passed.
- Loop/header pass: editor Development build succeeded and all four PlayerApi tests passed, with
  added suspended/nested breaks, while repetition, repeat-first-body exit and function-boundary checks.
  Full reimport recovered multiline headers and verified the five cube NPC offer triggers.
- Mutable-table pass: editor Development build succeeded and all five PlayerApi tests passed.
  `.Tables` checks dynamic constructors, nested aliases, mixed bracket/dot reads, deletion, positional
  insertion, unchanged state after cycle rejection, fresh inline declarations, immutable Blueprint
  defaults, resumed iteration over an aliased mutable table, syntax rejection, concatenation and
  locale escapes. Full reimport succeeded with zero errors and inspected the inventory-check writes
  in generated `BP_Quest_pre_event_heavens_cave`. This is not an end-to-end quest playthrough.
- No multiplayer PIE, cooked client/server build, persistence round-trip, or end-to-end playthrough
  was performed. The automated tests do not claim complete quest parity.

Logs: `Saved/Logs/QuestPortBaseline.log`, `QuestPortAfter.log`, `QuestPlayerApiTests.log`.
Ipairs pass logs: `Saved/Logs/QuestIpairsTests.log`, `QuestIpairsImport.log`.
Item-copy pass logs: `Saved/Logs/QuestItemCopyTests.log`, `QuestItemCopyImport.log`.
Loop/header pass logs: `Saved/Logs/QuestLoopControlTests.log`, `QuestLoopControlImport.log`.
Mutable-table pass logs: `Saved/Logs/QuestMutableTablesTests.log`, `QuestMutableTablesImport.log`.
Table-callback pass: editor Development build and all six PlayerApi tests passed. `.Callbacks`
covers suspension, fixed foreachi length, deleted slots, named/numeric keys, nil/non-nil early
returns, enclosing-trigger continuation, local restoration, iteration-limit abort and runtime menu
labels/mutation/invalid values. Full reimport succeeded with zero errors; both generated callbacks
in `BP_Quest_training_grandmaster_skill` were inspected. No live dialogue or multiplayer claim.
Logs: `Saved/Logs/QuestCallbacksTests.log`, `QuestCallbacksImport.log`.
Lua-value pass: editor Development build and all seven PlayerApi tests passed. `.LuaValues` covers
truthiness, boolean/native return types, typed/case-sensitive equality/order, operand preservation,
skipped reads/indexing/arithmetic, syntax validation, numeric-string conversion, concatenation
precedence, boolean table keys/deletion/cycle prevention, serialized boolean/nil leaves and actual
map-group matching using spawned world/player/map actors. Full reimport succeeded with zero errors
and inspected the function/branch structure in `BP_Quest_new_quest_lv7`; not a live quest playthrough.
Logs: `Saved/Logs/QuestLuaValuesTests.log`, `QuestLuaValuesImport.log`.
NPC-lease pass: editor Development build and all eight PlayerApi tests passed. `.NpcLocks`
covers two-player contention, reacquisition, foreign unlock, typed returns, authority/context
rejection, no-NPC/player targets, checkpoint rollback, suspended waits, confirmation, final-page
retention, input dismissal, pawn replacement/destruction, NPC destruction and manager EndPlay.
An initial test incorrectly invoked EndPlay without BeginPlay; its lifecycle setup was corrected
before the successful full rerun. Tests invoke server callbacks directly, not through a networked UI.
Full reimport succeeded with zero errors (three warnings); generated lock nodes were inspected in
`BP_Quest_arena_manager`. Source whitespace validation passed. No multiplayer PIE, cooked build,
disconnect/reconnect session or end-to-end arena playthrough was performed.
Logs: `Saved/Logs/QuestNpcLocksTests.log`, `QuestNpcLocksImport.log`.
Function-parameter frame pass: editor Development build and all nine PlayerApi tests passed.
`.Functions` covers argument capture before binding, explicit/implicit returns, nested shadowed
parameters, suspension/resume, cancellation, absent-parameter cleanup, same-named result destination,
argument/body evaluation failure, omitted nil arguments and evaluation of ignored extra arguments.
Full reimport succeeded with zero errors (three warnings) and inspected generated CallFunction nodes
in `BP_Quest_new_quest_lv7`. Coverage counts remained 526 statements, 67 gates and 27 unsupported
triggers; this pass corrects accepted runtime behavior rather than reducing diagnostics.
Source whitespace validation passed. No networked dialogue, cooked targets, persistence round-trip
or end-to-end quest playthrough was performed.
Logs: `Saved/Logs/QuestFunctionFramesTests.log`, `QuestFunctionFramesImport.log`.
Resumable-expression pass: editor Development build and all ten `Metin2.Quests` tests passed
(nine PlayerApi tests plus `.Importer.ExpressionLowering`). The new test translates and executes
nodes for nested and/or, zero/nil/boolean operand preservation, unary grouping, skipped invalid
arithmetic, read/mutation ordering, native/nested arguments, single execution of stateful arguments,
quoted call text, dialogue suspension/resume, nested field indexing, scalar assignment/return,
inline-if and standalone calls, and rollback of rejected lowering. An initial test fixture attempted
to instantiate the abstract quest base; ownership was corrected before the successful reruns.
Full reimport succeeded with zero errors and five warnings; a transient asset MoveFile failure
recovered on retry. Generated CallFunction structures in `BP_Quest_new_quest_lv7` were inspected.
Source whitespace validation passed. No networked/cooked target or end-to-end quest validation.
Logs: `Saved/Logs/QuestExpressionLoweringTests.log`, `QuestExpressionLoweringImport.log`.
