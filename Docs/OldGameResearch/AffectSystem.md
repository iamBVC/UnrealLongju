# Old Game Research: Affect System (Buffs/Debuffs)

Sources studied: `server_src/game/src/affect.h`, `server_src/game/src/char_affect.cpp`,
`Dumps/my_dump/db.sql` (`affect` table).

## Data model

An affect (the old game's word for a status effect) is a small record (`affect.h:4-16`):

| Field | Meaning |
|---|---|
| `dwType` | Affect id. Skill vnums for skill buffs; dedicated ids for potions/premium/hair etc. (`AFFECT_*` constants). |
| `bApplyOn` | Which point it modifies — an `EApplyTypes`/POINT ordinal, the **same apply system items use** (e.g. ATT_SPEED=7, MOV_SPEED=8). |
| `lApplyValue` | Signed amount added to that point while active. |
| `dwFlag` | Bitmask OR'd into the character's `m_afAffectFlag` (visual/state flags like AFF_STUN, AFF_POISON, AFF_INVISIBILITY). |
| `lDuration` | Remaining seconds. "Infinite" is just a huge value (the db dump has one with ~1.89 billion seconds). |
| `lSPCost` | SP drained per tick; if SP runs out the affect ends. |

A character holds a plain list of these (`m_list_pkAffect`). The same type can appear more than
once with different `bApplyOn` values.

## Lifecycle

- **Add** — `CHARACTER::AddAffect(type, applyOn, value, flag, duration, spCost, bOverride, isCube)`
  (`char_affect.cpp:534`). If an affect of the same type already exists and `bOverride` is true, the
  old one's effect is removed first and the record is reused (refreshed values/duration); otherwise a
  new record is appended (stacking is allowed by design). Duration 0 is coerced to 1. Adding
  immediately calls `ComputeAffect(pkAff, true)` and starts the affect event if not running.
- **Tick** — `ProcessAffect()` (`char_affect.cpp:221`) runs **once per second** from an event. Each
  tick: drains `lSPCost` from SP (ends the affect if SP is insufficient), decrements `lDuration` by 1,
  and removes the affect when duration hits 0. Guild-war skills also end when the war ends. When all
  affects are gone the event stops.
- **Apply/remove effect** — `ComputeAffect(pkAff, bAdd)` (`char_affect.cpp:640`): sets/resets the
  `dwFlag` bits on the affect-flag mask, then `PointChange(bApplyOn, ±lApplyValue)`. That's the whole
  stat integration — affects ride the exact same POINT/apply pipeline as item bonuses.
- **Remove** — `RemoveAffect` reverses the effect, notifies the client (affect remove packet), and
  frees the record.

## Persistence

- DB table `affect` (db.sql:233): `dwPID, bType, bApplyOn, lApplyValue, dwFlag, lDuration, lSPCost`,
  primary key `(PID, type, applyOn, applyValue)`.
- `SaveAffect()` (`char_affect.cpp:345`) pushes every affect to the DB process on logout/save;
  affects marked `IS_NO_SAVE_AFFECT` (e.g. revive-invisibility) are skipped.
- `LoadAffect()` re-adds saved affects on login with their remaining duration, calling
  `ComputeAffect(..., true)` for each.
- On logout while dead, HP is forced to 50 before the save (`char.cpp:1326`).

## UE recreation mapping

- `UMT2StatusEffectComponent` (on `AMT2CharacterBase`, so both players and mobs get it) holds a
  replicated `TArray<FMT2StatusEffect>` mirroring CAffect's fields.
- A 1-second server timer mirrors `ProcessAffect` (duration decrement, SP cost, expiry).
- `Duration < 0` encodes "infinite" instead of a giant number.
- Stat application goes through the same APPLY ordinal switch used for equipment bonuses
  (see `AMT2PlayerCharacter::RecalculateEquipmentStats`), triggered via `OnStatusEffectsChanged`.
- Player persistence: serialized into the PlayerState's persistent JSON payload
  (`AMT2PlayerState::CapturePersistentStateJson`) as an `affects` array, restored on load.
