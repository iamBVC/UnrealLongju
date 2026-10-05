# Skill Books (skill mastery progression)

Sources: old server `game/src/char_skill.cpp` (`LearnSkillByBook`, `LearnGrandMasterSkill`),
`game/src/char_item.cpp` (ITEM_TYPE_SKILLBOOK / USE_ABILITY use paths, vnum 50300).

## Skill level model (identical in the UE project already)

Skill level 0-40, mastery derived from level (`MT2SkillMastery::FromLevel`):
`0-19 Normal`, `20-29 Master (M1-M10)`, `30-39 Grand Master (G1-G10)`, `40 Perfect Master`.
So **M1 = level 20 … M10 = level 29, G1 = level 30**. A skill reaches M1 through point-ups (the
random 17→20 breakthrough in `LearnSkillByPoint`); books then take it M1 → G1.

## Master book path (`LearnSkillByBook`, non-YMIR / international branch)

For a skill already at Master mastery:

```
need_bookcount = GetSkillLevel() - 20;      // M1(20)=0, M2(21)=1, ... M10(29)=9
PointChange(POINT_EXP, -need_exp);          // exp is spent on EVERY read (success or fail)
percent = 65;
if (number(1,100) > percent) {              // 35% chance the read makes progress ("success")
    if (read_count >= need_bookcount) { SkillLevelUp(); read_count = 0; }   // level up
    else                              { read_count += 1; }                  // one step closer
}
// else: read failed, no progress (exp still spent)
```

So **each read has a 35% success chance**, and going from **M(N) → M(N+1) needs N successful reads**
(read_count climbs 0…N-1, then the next success levels up). That yields exactly:
M1→M2 = 1, M2→M3 = 2, …, **M10→G1 = 10 successful reads** — the progression the design asks for.

`read_count` is per-skill and persisted (old game: quest flag `traning_master_skill.<vnum>.read_count`).
The old game also has a book cooldown (`SKILLBOOK_DELAY`) — the design here drops it (no cooldown).

Exp: the old game spends a flat `need_exp` (20000) per read; **this project spends 5% of current exp
per read instead**, and denies the read when the player has no exp to spend.

## Grand Master path (`LearnGrandMasterSkill`, G1→G10)

Different tables (`aiGrandMasterSkillBookCountForLevelUp` etc.) with min/max read windows and a
per-read 50%-ish roll gated by those windows. Out of scope for now (design only asked for M1 → G1).

## Book item -> target skill (per-skill books)

`char_item.cpp` ITEM_TYPE_SKILLBOOK: a specific book stores the skill vnum it upgrades in `item->GetValue(0)`
(the generic soul book 50300 uses a socket instead). In the proto, e.g. book 50401 has VALUE0 = 1 = skill
vnum 1. So each skill has its own book, keyed by **book Value0 = skill vnum**.

## UE mapping

- `UMT2SkillComponent::LearnSkillByBook(SkillVnum)` implements the master path: gate to Master mastery,
  spend 5% exp, roll 35%, accumulate a per-skill read count, level up on `read_count >= level-20`.
- Read counts live in a server-only map on the component and persist via the PlayerState skills array.
- Reading is by **right-clicking the book in the inventory**: `AMT2PlayerCharacter::ServerReadSkillBook(slot)`
  resolves the book's target skill from its `Values[0]`, consumes that book (only if the skill is eligible),
  reads it, and posts a system chat line with the result and the skill's grade before/after
  (e.g. `[Skill] Three-way Cut: read succeeded! M1 -> M2`).
- Grade labels come from `MT2SkillMastery::LevelLabel` (M1..M10, G1..G10, P).
