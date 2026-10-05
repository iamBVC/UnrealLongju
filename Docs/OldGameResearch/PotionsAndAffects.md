# Potions & Affects (item vnums 27001-27006, 27100-27105)

Sources studied: old server `game/src/char_item.cpp` (USE_POTION / USE_ABILITY_UP handlers),
`game/src/char_affect.cpp` (the 3s affect event that drains the recovery pool), `game/src/char.cpp`
(`recovery_event`, natural regen), and `item_proto` (client dump `--dump_proto--/item_proto.txt`).

## Item data (from item_proto)

| Vnum        | Type / SubType        | VALUE0 | VALUE1 | VALUE2 | Meaning |
|-------------|-----------------------|--------|--------|--------|---------|
| 27001-27003 | ITEM_USE / USE_POTION | HP 300/800/1200 | 0 | - | recover HP over time |
| 27004-27006 | ITEM_USE / USE_POTION | 0 | SP 100/250/400 | - | recover SP over time |
| 27100-27102 | ITEM_USE / USE_ABILITY_UP | 7 (APPLY_ATT_SPEED) | 600 | 10/20/30 | +% attack speed, 10 min |
| 27103-27105 | ITEM_USE / USE_ABILITY_UP | 8 (APPLY_MOV_SPEED) | 600 | 10/20/40 | +% move speed, 10 min |

USE sub-type ordinals (common/enums.h): USE_POTION=0, USE_ABILITY_UP=7, USE_AFFECT=8, USE_POTION_NODELAY=11.

## USE_POTION = recovery OVER TIME (not instant)

`char_item.cpp` USE_POTION does **not** heal instantly. It deposits the value into a recovery pool
and starts the affect event:

```
if (item->GetValue(0)) {                              // HP potion
    if (GetPoint(POINT_HP_RECOVERY) + GetHP() >= GetMaxHP()) return false;   // deny if (pending+current) full
    PointChange(POINT_HP_RECOVERY, value0 * min(200,100+POTION_BONUS)/100);  // add to pool
}
// likewise value(1) -> POINT_SP_RECOVERY for SP
```

The pool drains in `char_affect.cpp`'s affect event, which runs **every 3 seconds**:

```
if (POINT_HP_RECOVERY > 0) { iVal = MIN(pool, GetMaxHP() * 9/100); PointChange(POINT_HP, iVal); pool -= iVal; }
if (POINT_SP_RECOVERY > 0) { iVal = MIN(pool, GetMaxSP() * 7/100); PointChange(POINT_SP, iVal); pool -= iVal; }
```

So each 3s tick heals up to **9% of max HP** (7% of max SP) from the pool until it is empty. Deny
condition: the potion is rejected when current + pending-pool already reaches the max.

## USE_ABILITY_UP = timed stat buff

```
switch (value0) {
  case APPLY_ATT_SPEED: AddAffect(AFFECT_ATT_SPEED, POINT_ATT_SPEED, value2, AFF_ATT_SPEED_POTION, value1, ...);
  case APPLY_MOV_SPEED: AddAffect(AFFECT_MOV_SPEED, POINT_MOV_SPEED, value2, AFF_MOV_SPEED_POTION, value1, ...);
  // + STR/DEX/CON/INT/CAST_SPEED/ATT_GRADE/DEF_GRADE ...
}
```

value2 = magnitude (%), value1 = duration (seconds). The old game *refreshes* the affect on re-use;
this project instead **denies re-use while the same buff is active** (design choice: potions don't stack).

## How this maps to the UE project

- One component, `UMT2StatusEffectComponent`, manages every affect (already replicated + persisted).
- Each effect's behaviour lives in a `UMT2StatusEffectDefinition` subclass (one class per effect),
  resolved from the new `FMT2StatusEffect::Kind`:
  - `HealthRecovery` / `ManaRecovery` — a **single shared pool per resource** (one effect, id
    `MT2AffectId::HealthRecovery`/`ManaRecovery`), stored in `ApplyValue`, drained at a constant ~3%/s of
    max HP (matching 9%/3s) and ~3%/s of max SP; ends when the pool empties. A drink *merges* into the
    existing pool (`UseItem` reads it via `GetEffectValueByType` and re-adds with `bOverride`), capped so
    the target `current + pool` never exceeds the max — so extra potions raise the target toward 100%
    without ever exceeding it and without speeding up the regen. Damage lowers `current`, so the pool
    heals toward a correspondingly lower target (the damage is subtracted from the goal). Denied (no
    consume) once `current + pending` already reaches the max.
  - `AttackSpeed` / `MovementSpeed` — carry `ApplyType` 7/8 + `ApplyValue` %, so the existing
    equipment/affect stat recompute applies them; `DeniesReapplyWhileActive` blocks re-use while active.
- `USE_POTION` (0) now applies the over-time recovery effect (was instant); `USE_POTION_NODELAY` (11)
  stays instant; `USE_ABILITY_UP` (7) applies the timed speed buff.
