# Quest & Dialog System (porting the old Lua .quest scripts)

Source review: 2026-10-05. For measured conversion coverage and the historical implementation passes, use [QuestPortingStatus](QuestPortingStatus.md). Its latest recorded baseline is **482 unconverted statements, 67 failed gates, and 27 unsupported triggers**. This review did not rerun imports or tests.

## Original model

Legacy quests use `quest / state / when` structure around Lua bodies. Each player owns quest state and flags. Events select current-state triggers, NPC/item/target identities, and optional `with` conditions.

Dialogue is resumable: `say` accumulates text, while `select`, `wait`, and `input` suspend execution until a player response. Quest letters and named targets are separate state, not only dialogue decoration.

External corpus counts vary by dataset and import selection. Older 285-script/2,040-TODO figures are historical, not the current baseline.

## Unreal representation

Quest Blueprints under `/Game/Quests` derive from `UMT2Quest`. Their defaults carry states, triggers, conditions, and instanced execution nodes. Blueprint event hooks remain available; do not assume imported complex scripts necessarily live in handwritten Event Graphs.

| Type | Responsibility |
| --- | --- |
| `UMT2Quest` | Quest definition, constants, states, functions, and Blueprint event hook. |
| `UMT2QuestNode` / `UMT2QuestCondition` | Executable steps and conditions. |
| `UMT2QuestRegistrySubsystem` | Asset-registry discovery and cached definitions, filtered by the generated active-quest manifest when present. |
| `UMT2QuestTableAsset` | Converted library tables and active quest IDs. |
| `UMT2QuestManagerComponent` | Server-owned per-player state/flags, dispatch, resumable executor, journal/targets, and persistence capture/restore. |
| `UMT2QuestComponent` | Owner-client dialogue transport. |
| `FMT2QuestExpression` | Supported Lua-like values, expressions, native bindings, and result-list evaluation. |

The quest registry has its own discovery path; it is not the mob/item VNUM registry. Merely dropping an old quest Blueprint into the folder may not activate it when an active manifest is present.

## Dispatch and ownership

Triggers filter by event type, state, optional VNUM, and named event/target. Converted helper-backed `with` gates execute before the body claims an event or appears as an NPC choice. Unsupported gates stay rejected; accepting a keyword without a real dispatch source is not a completed port.

NPC conversation locks use server-owned leases. Completion, cancellation, actor removal, pawn changes, and teardown clean up ownership. New interactive requests cannot replace a suspended conversation; deferred broadcast delivery and dialogue-generation validation still need fidelity review.

Quest entity IDs identify live character instances, not template VNUMs. `find_npc_by_vnum`, `npc.get_vid`, and entity-backed `target.vid` use the per-world registry. Older serialized VNUM-only target nodes retain their previous meaning until reimport. Cross-map entity lookup/relevancy and optional target arguments remain separate work.

## Executor and Lua subset

Node frames retain progress through dialogue suspension, branching, loops, callbacks, and quest-function calls.

Implemented subsets include:

- Arithmetic, comparisons, Lua truthiness, typed nil/booleans, short-circuit operand-preserving `and/or`, and supported math/string/native reads.
- Read-only library tables, runtime mutable tables, indexed/named writes, nil deletion, and positional `table.insert`.
- While/repeat/numeric-for, sequence `ipairs`, recognized anonymous foreach callbacks, and loop-local `break`.
- Quest-local function frames, caller-side argument capture, parameter restoration, and explicit/implicit returns.
- Native result lists and simultaneous bare-variable assignments; direct quest-function calls can publish multiple results.
- Dialogue, rewards, flags/state, journal/targets, timers, implemented inventory/affect/shop/warp operations.

This is **not arbitrary Lua**. Full lexical scope, closures/general function values, named callbacks, table-field assignment target capture, and result lists through nested resumable expressions remain incomplete. Some loop/menu/constructor consumers still lack resumable expression integration.

A final eligible call expands in a result list; parentheses and scalar contexts suppress expansion. Mixed resumable RHS lists are rejected rather than silently losing results. Native return counts still need broader auditing.

Library tables are generated before trigger translation. Runtime values do not mutate Blueprint class defaults. Table cycles, depth/entry limits, iterator budgets, and string work/output bounds are explicit constraints, not legacy-perfect behavior.

## Dialogue and string semantics

Say text is resolved when the node executes, so buffered loop dialogue retains the correct iteration values. `select_table` supports runtime-built menus. Closing wait/select/input cancels the run rather than awarding subsequent rewards.

`string.format` executes the supported legacy formatting set. Plain concatenation does **not** reinterpret literal `%s` text as a formatting request. `string.gsub` and `string.find` use the bounded legacy byte-pattern implementation. General arbitrary byte-string storage and first-class function replacements remain unsupported.

The value model uses FString; transformations producing invalid UTF-8 fail explicitly rather than claiming full Lua byte-string parity.

## Persistence and presentation

Player capture/restore includes quest state, flags, and journal data; the coordinator stores these in `player_quests` and `player_quest_flags`. The JSON envelope is transport, not the database schema. Flags remain signed 32-bit; timestamp/2038 limitations are not solved by the current port.

Journal entries and target markers replicate owner-only. Quest log UI, NPC arrows, map highlighting, and notifications consume that state. Exact presentation and network relevancy need live validation with the matching content revision.

Timers currently belong to per-player quest execution. Converting `server_timer` syntax does not create a fully authoritative dungeon-wide timer owner.

## Import and inspection

Build the editor and provide authorized source datasets. From the selected engine:

```text
UnrealEditor-Cmd.exe <project.uproject> -run=MT2ImportQuests -Source=<quest-directory> -ClientSource=<extracted-client-root> -Destination=/Game -Verify=BP_Quest_blacksmith
```

This command **writes/refreshes assets before verification**. `-Verify` prints a generated definition after import; it does not skip conversion. Review `Saved/MT2QuestConversionReport.txt`, process warnings/errors, and both repositories' diffs.

Untranslated statements retain their source/line in `UMT2QuestNode_Unconverted`. Some stop execution; others allow surrounding converted nodes to continue. Newly recovered incomplete triggers can also carry explicit false gates. An inert diagnostic is not proof that the entire quest is safe to enable.

## Next work and validation

Continue from [the current audit's remaining work](QuestPortingStatus.md#remaining-work-in-recommended-order), not the older milestone counts. Zero diagnostics alone is insufficient: audit accepted placeholder bindings and play through quests in a real authoritative session.

The latest recorded pass reports 238 parsed scripts, 237 refreshed Blueprints, 6,595 triggers, and 27,761 nodes. Its 32 quest/safezone tests passed, but networked dialogue, cooked targets, and end-to-end quest parity were not established by that pass or this documentation review.
