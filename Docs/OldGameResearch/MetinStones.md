# Metin stones (old game)

Sources: `game/src/char_state.cpp` (`__StateIdle_Stone`, `StateIdle`, `StateBattle`),
`char.cpp` (`IsStone`, `DetermineDropMetinStone`), mob_proto (`bType = CHAR_TYPE_STONE`).

## 1. Stones never fight like mobs

`StateBattle()` starts with:

```cpp
if (IsStone()) { sys_err("Stone must not use battle state (name %s)", GetName()); return; }
```

and `StateIdle()` routes stones to their own `__StateIdle_Stone()` before any monster logic. So a
stone has **no chase, no aggro, no melee swing** - it only ever runs the idle pulse below. Stones
also carry AIFLAG_NOMOVE, so they never slide from a CRUSH either.

## 2. `__StateIdle_Stone`: fire once per HP threshold crossed

```cpp
void CHARACTER::__StateIdle_Stone()
{
    m_dwStateDuration = PASSES_PER_SEC(1);            // re-evaluated once per second
    int iPercent = (GetHP() * 100) / GetMaxHP();
    DWORD dwVnum = number(MIN(sAttackSpeed, sMovingSpeed), MAX(sAttackSpeed, sMovingSpeed));

    if (iPercent <= 10 && GetMaxSP() < 10) {
        SetMaxSP(10);                                  // "already fired" marker for this step
        SendMovePacket(FUNC_ATTACK, 0, GetX(), GetY(), 0);   // the stone's attack animation
        CHARACTER_MANAGER::instance().SelectStone(this);
        SpawnGroup(dwVnum, map, x-500,  y-500,  x+500,  y+500);
        SpawnGroup(dwVnum, map, x-1000, y-1000, x+1000, y+1000);
        SpawnGroup(dwVnum, map, x-1500, y-1500, x+1500, y+1500);
        CHARACTER_MANAGER::instance().SelectStone(NULL);
    }
    else if (iPercent <= 20 && GetMaxSP() < 9)  { SetMaxSP(9);  ... }
    else if (iPercent <= 30 && GetMaxSP() < 8)  { SetMaxSP(8);  ... }
    else if (iPercent <= 40 && GetMaxSP() < 7)  { SetMaxSP(7);  ... }
    else if (iPercent <= 50 && GetMaxSP() < 6)  { SetMaxSP(6);  ... }
    else if (iPercent <= 60 && GetMaxSP() < 5)  { SetMaxSP(5);  ... }
    else if (iPercent <= 70 && GetMaxSP() < 4)  { SetMaxSP(4);  ... }
    ...
}
```

Key mechanics:
- **Every 10% of HP lost** is a step (70/60/50/40/30/20/10%). Each step fires **once**: MaxSP is
  abused as a bitmask-ish counter (4..10) so a crossed step can't retrigger.
- On each step the stone plays its **attack motion in place** (`SendMovePacket(FUNC_ATTACK)`) -
  that is the "1 attack every X% of HP" the stone does. It has no target and deals no melee damage;
  it's the spawn tell.
- It then **spawns 2-3 groups of mobs** in nested boxes around itself (±500, ±1000, ±1500 units).
  The exact box mix varies per step (lower HP = wider spread).
- **Which mobs**: `dwVnum = number(min(sAttackSpeed, sMovingSpeed), max(...))` - the stone's
  mob_proto **ATTACK_SPEED / MOVING_SPEED columns are reused as a spawn-group vnum range**, not as
  real speeds. `SpawnGroup` spawns a whole group definition (group_group/mob_group table).
- `SelectStone(this)` marks the spawns as belonging to this stone, so the spawned mobs aggro the
  stone's attacker (the player fighting it).

## UE recreation

- Source review: 2026-10-06. `AMT2MetinStone : AMT2Mob` exists; it is not a proposed `AMT2Metin` class. It disables movement, registers with the world simulation scheduler, and checks health changes authoritatively.
- `ProcessStoneBehavior` walks every crossed threshold once using `LastFiredStep`; it can process several thresholds after a large hit. It plays the attack motion as a spawn tell.
- Imported `MetinSpawnGroups` supply group entries, resolved through the VNUM registry and spawned around the stone. Group import is not wholly missing; direct VNUM spawning is not the documented current design.
- Spawned mobs retain a weak stone link, and available AI targets the stone's attacker. Stone death removes its surviving wave without normal combat rewards; mob death banks half its experience on the stone.
- Ring placement, group composition, animations, and scheduler cadence still need dataset-specific/live validation. This source review did not run a stone encounter.

## Stone <-> spawned mob linkage (char.cpp:4955-4990, char_battle.cpp:1488, 2734)

`CHARACTER::SetStone(pkChrStone)` is a two-way link: the spawn keeps `m_pkChrStone`, and the stone
keeps `m_set_pkChrSpawnedBy`. Two behaviours hang off it:

1. **Killing the stone kills its wave.** `CHARACTER::Dead` calls `ClearStone()`, which runs
   `FuncDeadSpawnedByStone` over every mob the stone spawned:
   ```cpp
   struct FuncDeadSpawnedByStone {
       void operator () (LPCHARACTER ch) { ch->Dead(NULL); ch->SetStone(NULL); }
   };
   ```
   `Dead(NULL)` means **no killer**, so those mobs pay out no exp, gold or drops - they just die.

2. **Half a spawn's exp is banked on the stone.** In `DistributeExp` (char_battle.cpp:2734):
   ```cpp
   if (m_pkChrStone) {                       // exp is handed over to the stone
       int iExp = iExpToDistribute >> 1;
       m_pkChrStone->SetExp(m_pkChrStone->GetExp() + iExp);
       iExpToDistribute -= iExp;
   }
   ```
   So a wave mob only pays out half its exp to its killers; the other half accumulates on the stone
   and is paid out by the stone's own `DistributeExp` when the stone is broken.

`GetProtege()` also returns `m_pkChrStone`, i.e. wave mobs treat their stone as the thing to defend.
