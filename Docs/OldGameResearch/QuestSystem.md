# Quest & Dialog System (porting the old Lua .quest scripts)

Current audit: [QuestPortingStatus.md](QuestPortingStatus.md) (2026-10-04). The measurements and
limitations below describe successive historical implementation stages, not current coverage.

Sources studied: `my_server/server_src/share/locale/italy/quest/*.quest` (285 scripts) and the old
`game/src/quest` manager. See also Docs/OldGameResearch/SkillBooks.md for the same "data + escape
hatch" pattern used elsewhere.

## What the old content actually looks like (measured, not guessed)

| Measure | Value |
|---|---|
| Scripts | 285 (median 151 lines, p90 536, max 6188) |
| No loops/functions (mechanical) | 175 |
| Uses loops / local functions / closures | 110 |

Most-used quest API calls across the corpus:

```
1359 pc.getqf     1074 pc.setqf     857 pc.count_item   839 target.delete   800 pc.give_item2
 357 pc.remove_item  265 pc.get_map_index  238 pc.level  212 pc.give_exp2   195 pc.change_money
 189 game.get_event_flag   126 game.set_event_flag   70 pc.warp
```

Trigger keywords: `letter` 1078, `button` 799, `chat` 782, `target` 550, `kill` 436, `click` 318,
`use` 299, `login` 249, `arrive` 246, `enter` 237, `info` 202, `timer` 107, `leave` 100.

**Conclusion:** quest *flags* (`getqf`/`setqf`, 2433 calls) are the backbone, and the dominant script
shape is `check flag/level/item -> say -> select -> give/take -> set flag`, which is pure data. The
110 scripts with real control flow are the tail that needs actual logic.

## The UE model: one Blueprint per quest

Every quest is a **Blueprint asset under `/Game/Quests`**, subclassing `UMT2Quest`. A Blueprint carries
both halves of the problem:

- **Class defaults = data.** `States[] -> Triggers[] -> Nodes[]` covers the mechanical majority with no
  graph work. `Instanced` UPROPERTYs make nodes/conditions author inline in the details panel.
- **Event graph = logic.** `OnQuestEvent` is a `BlueprintImplementableEvent`; returning true swallows the
  event and skips the declarative triggers. This is where the 110 complex scripts put their logic.

Nothing registers a quest: drop the Blueprint in `/Game/Quests` and `UMT2QuestRegistrySubsystem` finds it.

### Classes

| Class | Role |
|---|---|
| `UMT2Quest` | Quest asset base (Blueprintable). `QuestName`, `States`, `OnQuestEvent` hook. |
| `UMT2QuestNode` | One step. Subclasses: `Say`, `Select`, `If`, `GiveItem`, `TakeItem`, `GiveReward`, `SetFlag`, `SetState`, `Notice`, `Close`. |
| `UMT2QuestCondition` | One test. Subclasses: `Level`, `HasItem`, `Flag`, `Empire`, `Gold`. `bInvert` covers the negative cases. |
| `UMT2QuestRegistrySubsystem` | Scans `/Game/Quests` once per process, caches quest CDOs by id. |
| `UMT2QuestManagerComponent` | Per-player: flags, per-quest state, event dispatch, the executor, persistence. |
| `UMT2QuestComponent` | Pre-existing dialog transport (client RPCs). The manager decides *what* to say; this ships it. |

### Two semantics worth preserving

1. **`say()` accumulates, `select()` flushes.** Say nodes append to a pending page; a Select flushes it
   with its options, and the end of a block flushes whatever is left as a plain "Close" page. This is
   exactly the old script behaviour and it is why Say/Select are separate nodes.
2. **Execution is resumable, not recursive.** `select()` suspends mid-script. The manager keeps a call
   stack of `(node list, index)` frames; a node returns `Suspend` and the dialog answer resumes the
   same block. `If`/`Select` branches push a nested frame rather than recursing, so a suspend deep in a
   branch still resumes correctly.

### Flags and state

- Flags are stored `"<questid>.<flag>"`. An unqualified name in a node scopes to the running quest
  (old `pc.getqf`); a dotted name reads another quest's flag. Value 0 is the default and is not stored.
- Per-quest state defaults to `start` (old `state start begin`). `SetState` moves the player on.
- Both persist through the PlayerState JSON (`quest_states`, `quest_flags`), so progress survives
  logout and cross-server transfer.

## Converting the old scripts (importer phase)

The importer parses `quest X begin / state Y begin / when <vnum>.<event> begin ... end` and emits a
quest Blueprint per script, translating the mechanical subset:

| Lua | Node |
|---|---|
| `say_title()`, `say()`, `say_reward()` | `Say` |
| `select()` / `select_table()` | `Select` (each branch becomes the option's node list) |
| `if pc.get_level() >= N`, `pc.count_item`, `pc.getqf`, `pc.get_empire` | `If` + matching condition |
| `pc.give_item2`, `pc.remove_item` | `GiveItem` / `TakeItem` |
| `pc.give_exp2`, `pc.change_money` | `GiveReward` |
| `pc.setqf` | `SetFlag` |
| `set_state()` | `SetState` |

Anything it cannot translate is
**not** silently dropped: it becomes a `UMT2QuestNode_Unconverted` ("TODO") node carrying the original
Lua and its source line, plus a line in `Saved/MT2QuestConversionReport.txt`. The TODO node is inert at
runtime, but surrounding converted nodes can still execute. An unfinished quest is not gameplay-safe
merely because its unsupported statements are inert; review the entire trigger before enabling it.

Run it from **MT2 > Import Quests**, or headless:

```
UnrealEditor-Cmd.exe <project> -run=MT2ImportQuests [-Source=<quest dir>] [-Verify=BP_Quest_blacksmith]
```

`-Verify` loads a generated quest back and prints its states/triggers/nodes (asset export data is
compressed, so inspecting the .uasset directly proves nothing).

### Measured result

| | First run | Current |
|---|---|---|
| Scripts parsed | 285 | 285 |
| Quest Blueprints written | 285 | 285 (`/Game/Quests/BP_Quest_*`) |
| Triggers converted | 3,315 | **6,385** |
| Nodes generated | 9,996 | **22,478** |
| Statements left as TODO | 8,395 | **2,040** |
| Quests with zero TODOs | - | **105 / 285** |

Re-running refreshes assets in place (0 created / 285 refreshed), so the import is idempotent.

### What closed the gap

- **A runtime expression evaluator** (`FMT2QuestExpression`): arithmetic, comparisons, `and/or/not`
  and the quest API bound as callable functions. `if`, `local x = ...` and computed call arguments now
  ride as expression strings on the nodes instead of becoming TODOs. The importer only emits an
  expression when *every* call in it is bound and every bare name is a captured local or quest constant -
  so coverage grows by binding more functions, never by guessing.
- **`define NAME VALUE` constants** are captured as quest-scope constants, seeded into the script
  variables before a block runs. This also resolves named vnums in trigger specs (`when MOB1_1.kill`).
- **`when a or b with <cond>`** expands to one trigger per event, sharing the block and the gate.
- **`select()` publishes its result into a variable** (`ResultVariable`) so the following `if s == 2`
  chain converts as ordinary conditions. This replaced pattern-matching the chain, which could not cope
  with blank lines, nesting, or reuse of `s` later.
- **Quest journal and target markers** became real per-player features (`q.*`/`send_letter` and
  `target.*`), rather than being reported as missing.

Two importer bugs found only by running the conversion and checking the output:
`TranslateSelect` read the *untrimmed* line, so `local s= select(...)` produced the variable name
`"local s"` and silently broke every select dispatch; and identifier validation missed calls written
with a space before the paren (`number (1,100)`).

### Features built to close the gap

Timers, sockets and spawning were closed by building the features (see
Docs/OldGameResearch/GuildAndTimers.md), plus these conversions:

- **Timers** - `server_timer`/`timer`/`loop_timer`/`clear_server_timer` and `get_server_timer_arg()`,
  with name-matched triggers (`when mytimer.server_timer`).
- **Guild** - a real guild subsystem; `guild.*` reads resolve against it.
- **Item sockets** - `item.get_socket`/`set_socket` against the real socket data, which also gave the
  quest context the item that raised the event, making `when <vnum>.use` triggers work.
- **Spawning / drops / warps** - `mob.spawn`, `d.spawn_mob`, `game.drop_item`, `pc.warp`/`warp_local`
  (with computed coordinates), `item.remove()`.
- **Journal clock** - `q.set_clock`.
- **NPC context** - `npc.race`/`vnum`/`empire`/`is_pc`/`is_near` bound to the actor that raised the event.

Three more importer bugs surfaced by re-running and checking the output each time:

- The select *detection* used a plain `Contains("select(")`, which also matched
  `pc.give_item2_**select(**...)` and misrouted 39 give-item statements into the select path.
- Statements spanning several lines (long `select()` option lists) were parsed as fragments; logical
  lines are now joined until the parentheses balance.
- `local x` with no assignment left `x` unknown, so every later expression referencing it failed.

## Quest presentation (log, arrow, map markers)

The journal and target markers are **replicated to the owning client** (`COND_OwnerOnly` - another
player's quest log is private), so the UI reads them locally instead of asking the server:

- **Quest log** - `UMT2QuestLogWidget`, hosted by the character window's existing `QuestsPage` canvas
  the same way the skill page is. Code-built (no UI asset): one entry per active quest with its title,
  summary and progress counter, completed quests marked. **N** opens the character window straight on
  this page (`AMT2HUD::ToggleQuestWindow`).
- **Overhead arrow** - `UMT2QuestArrowComponent` on every NPC. It shows only while an active quest
  points at that NPC's vnum, bobs gently, and ticks only while visible. Screen-space like the
  nameplates, and never built on a dedicated server. It currently reuses the minimap atlas's arrow art
  rotated 180 degrees; swap `ArrowTexturePath`/`ArrowRegion` if dedicated quest-marker art is imported.
- **Flashing map marker** - quest targets pull red/white on the minimap *and* full map
  (`UMT2MapViewWidget::GetQuestTargetFlashColor`, with the full map overriding its blanket green).
  The flash is deliberately ~1Hz: markers only recolour on the 0.25s marker refresh, so a faster blink
  would alias against that sampling.

One semantic worth recording: `find_npc_by_vnum(x)` returns **the vnum itself** (0 when no such NPC is
on the map) rather than the old server's entity id, which would be meaningless here. That keeps the
scripts' `if v ~= 0` truthiness tests working *and* makes `target.vid(name, v)` carry an identifier the
arrow and map marker can actually match against.

### The remaining ~2,040

Overwhelmingly `if` (458) and `local` (278) over **Lua tables and script-defined functions**
(`local setting = flame_dungeon.setting()`, `special.questscroll[pc.getqf("idx")]`,
`for i, material in ipairs(list) do`), plus self-contained event/mini-game scripts
(`event_mystery_box.drop_box`, `harvest_festival.kill_action`) and the dungeon API (`d.*`).

Closing these needs table values, user-defined functions callable across quest files, multiple return
values and `for` loops in the evaluator - i.e. a Lua-subset interpreter, which is a much larger and
riskier piece of work than the expression evaluator was. It is a deliberate decision point, not an
oversight.

Every remaining statement is an inert `TODO (Unconverted Lua)` node carrying its original Lua and
source line; `Saved/MT2QuestConversionReport.txt` lists these translation failures. This does not prove
semantic parity for the surrounding converted nodes or for runtime API bindings.

### Historical limitations (superseded; see current audit)

- **Named-NPC triggers.** 337 click/chat triggers name their NPC (`when blacksmith.chat...`) instead of
  using a vnum, which the importer cannot resolve. They are written out with `Vnum = 0` and the
  dispatcher **refuses to match NPC events on vnum 0**, so they stay inert rather than firing that
  dialog on every NPC in the game. Fill the NPC vnum in on the trigger to activate them; the report
  lists every one.
- **Quest journal / letter API** (`send_letter`, `q.start`, `q.set_title`, `q.done`,
  `makequestbutton`, ~1,900 statements) needs a quest-log feature that does not exist yet.
- **Quest target markers** (`target.delete`, `target.pos`, ~1,000) need a waypoint feature.
- **`find_npc_by_vnum` proximity checks** (~700) need a runtime NPC lookup.
- Compound conditions (`and`/`or`) and non-literal arguments stay unconverted by design: emitting a
  branch that silently takes the wrong path is worse than an explicit TODO.

`setskin` (pure window styling) is dropped as a genuine no-op rather than reported as a TODO, so the
report reflects real missing behaviour instead of noise.

## Lua constant tables (`special.*`, `locale.*`)

Measured in `share/locale/italy/quest/`: the scripts do not only call the quest API, they also read
large constant tables defined in the library files that every quest is loaded alongside.

- `questlib.lua` declares `special = {}` (line 621) and then fills it: `special.levelup_quest`
  (line 903), `special.active_skill_list` (line 659), `special.lvq_map`, the `levelup_reward_*`
  tables, and others.
- `locale.lua` adds `special.questscroll` (line 1471) plus the `locale.questscroll5.*`,
  `locale.monster_chat` and `locale.quiz` tables.
- `questing.lua` adds `col.list`.

Shapes are positional arrays, not maps. `special.levelup_quest` is one row per level of
`{ mobVnum, qty, mob2Vnum, qty2, expPercent }`:

```lua
{	171	,	10	,	172	,	5	,	10	}	,	--	lev	2
```

Read depth in the scripts is at most three levels — `special.levelup_quest[lev][s*2-1]` (two) and
`special.active_skill_list[job][group][n]` (three) are the extremes; most reads are one level.

### How this is represented

The importer converts every `<name>.<name> = { ... }` definition in those three library files into
`UMT2QuestTableAsset` at `/Game/Quests/DA_MT2QuestTables` (35 tables at last import). Nesting is
stored **flat** — `FMT2QuestTableNode` holds either a value or the indices of its children — because
a directly recursive `USTRUCT` cannot be a `UPROPERTY`. `FMT2QuestTable::GetChildIndex` takes the
1-based index the script uses, so the translation from Lua indexing stays in one place.

The tables are generated *before* any quest is translated in the same import run: the expression
validator consults them to decide whether a table read can be converted, so they have to exist first.

At runtime `FMT2QuestExpression` resolves a bare dotted identifier that names a converted table to a
table reference, and a postfix `[expr]` loop walks into it. Leaves become numbers/strings; nested
tables stay references so `t[a][b]` chains. Indexing a non-table, or an index that does not exist,
fails the expression rather than producing a silent zero — the same rule the rest of the evaluator
follows.

This is what makes the level-up quest option render: `"Kill {=mob_name(special.levelup_quest[lev][s*2-1])}!"`
now resolves the monster name instead of an empty string.

### Locale strings used as concatenation instead of format

Some GF locale strings are written with printf placeholders but *concatenated* by the script rather
than passed through `string.format`. `levelup.quest` line 233:

```lua
mob_name(special.levelup_quest[lev][1]).." "..special.levelup_quest[lev][2]..gameforge.levelup._155_say
```

with `gameforge.levelup._155_say = "Kill %s. "` — plain concatenation prints the placeholder verbatim
("Grizzly 20Kill %s."). The importer therefore folds the surrounding pieces into the format string
when one piece of a concatenation carries a placeholder, producing "Kill Grizzly 20." — what the text
was written to say. Placeholder detection only accepts a real printf conversion (flags/width, then
one of `diouxXeEfgGcsq`), so ordinary text such as "50% damage" is not mistaken for one.

Journal text (`q.set_title`, summaries) is token-expanded **when it is written**, not when it is
displayed: the entry is stored and replicated as plain text, and a title like `Kill %s.` has to
capture the monster it was set for.

### Bare property reads vs calls

The API is not uniformly callable. Some values are read as plain fields, and the kill block of
`levelup.quest` (line 362) depends on it:

```lua
if lev != 0 and npc.race == (special.levelup_quest[lev][sel*2-1]) and pc.getqf("buttonstate") == -1 then
```

`npc.race`, `npc.vnum`, `npc.empire`, `npc.level`, `guild.level` and `guild.name` appear without
parentheses, alongside the `get_*` call forms. The evaluator lists them in *both* its property and
function sets — a name known only as a function is rejected when read bare, which silently dropped
this entire condition (and with it the kill counter) until it was fixed.

### levelup.quest button states

`buttonstate` drives the quest-window button and gates the kill counter:

| value | meaning |
|-------|---------|
| `2`   | a new level's mission is offered |
| `-1`  | mission accepted, hunt in progress (set at the end of the `button` block, line 332) |
| `3`   | required kills done, reward waiting |
| `1`   | mission info can be reviewed |

The `button` block has no branch for `-1`, so clicking the quest entry mid-hunt does nothing in the
original game too — the entry becomes interactive again once the last kill flips the state to `3`.

## Script-defined functions

252 `function name(args) ... end` blocks are defined across the 285 scripts, inside their state
blocks. They are plain subroutines — no closures, no recursion, and (apart from constant-table
accessors) no return values — so a call site is translated by **inlining the body**: parameters
become ordinary block locals bound to the caller's argument expressions, then the body is translated
into the call site. There is nothing to call at runtime, which is why inlining rather than a call
node is the right shape.

Inlining is attempted only when the body converts **completely**. A partially converted body would
report its own unconvertible statements once per call site — `event_mystery_box.drop_box` alone has
55 — and bake a TODO blob into every copy. One TODO on the call is both smaller and more honest, so
the translator rolls back the report, the counters and the nodes when a body comes back dirty.

A bare `return` inside a block (or an inlined body) converts to a **Return** node, which stops the
block without touching quest state — the early-out guard the old scripts use.

## Bare-name constant lists

`questlib.lua` lines 333-340 declare the skill-teacher NPC lists as bare ALL_CAPS names:

```lua
WARRIOR1_NPC_LIST 	= {20300, 20320, 20340, }
```

The table converter originally required a dotted name (to distinguish `special.*` from script
locals); it now also accepts a bare name that is entirely upper case, which is exactly this family.
53 tables convert, up from 35. `npc_is_same_job()` (questlib.lua line 209) reads them, and with it
the 24 skill-teacher trigger gates in `find_senior_soldier.quest` convert.

## Spelling variants in the quest API

The old API is not spelled consistently, and a name the evaluator does not know fails the *whole*
expression it appears in - so one unbound name silently drops every condition and trigger gate around
it. These were bound: `pc.is_mount`, `pc.hasguild`, `pc.getempire`, `pc.get_guild`,
`pc.isguildmaster` / `pc.is_guild_master`, `pc.get_gm_level`, `pc.in_dungeon`, `pc.is_dead`,
`pc.get_player_id`, `npc.get_guild`, `horse.is_dead`, `horse.is_ride`, `tonumber`, `math.mod`,
`table.getn`, `table_is_in`, plus the questlib helpers `pc_is_novice` (line 118),
`npc_is_same_empire` (line 147) and `npc_is_same_job` (line 209).

`pc.get_guild_id` previously returned a hardcoded 0 even though the guild subsystem exists; all the
guild reads now answer from the real record.
