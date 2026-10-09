# Skill system (old game)

Sources studied:
- Server: `game/src/skill.h` / `skill.cpp` (CSkillProto, CSkillManager), `char_skill.cpp`
  (learn/level-up/use/compute, 3645 lines), `skill_power.cpp` (per-level power tables),
  `char.cpp` (skill points on level-up), `cmd.cpp` (`/skillup` command), `common/enums.h`.
- Client: `UserInterface/PythonSkill.h/.cpp` (client skill table + desc), `PythonPlayerSkill.cpp`
  (client-side use checks), `playersettingmodule.py` (skill slot assignment per job/group),
  `uicharacter.py` (character window skill pages).
- Data: `my_dump/936skilltable.txt` (server proto as text), `my_dump/936skilldesc.txt`
  (client-side names/descriptions/formulas/icons).

Sections 1–5 describe the external original game, not current port coverage. Current learning/casting is implemented in `UMT2SkillComponent` / `UMT2SkillCastComponent`; current book rules and grand-master backend are documented in [SkillBooks](SkillBooks.md) and [QuestPortingStatus](QuestPortingStatus.md). The implementation does not establish complete gameplay parity.

## 1. Data model

### Server proto (`CSkillProto`, loaded from skill_proto / skilltable)
Per skill vnum:
- `dwType`: 0 = job-independent (support/sub), 1 = Warrior, 2 = Assassin, 3 = Sura, 4 = Shaman,
  5 = Horse, 6 = ? (treated like job skills for points). Type-1..4 skills only learnable by the
  matching job (`pkSkill->dwType - 1 == GetJob()`).
- `bMaxLevel` (usually 40 for player skills), `bLevelLimit` (character level required),
  `preSkillVnum`/`preSkillLevel` (prerequisite skill).
- `bPointOn` + `kPointPoly`: which point the skill modifies (HP for damage skills - negative
  amount, ATT_SPEED, DEF_GRADE, MOV_SPEED...) and the formula. Up to **three** point/formula
  pairs (`bPointOn2/3`, `kPointPoly2/3`) - e.g. Jeongwi = ATT_SPEED + MOV_SPEED; the third slot
  only applies at Grand Master+ mastery.
- Polys (formulas evaluated with a tiny expression engine `CPoly`): `kSPCostPoly`,
  `kDurationPoly(2/3)`, `kDurationSPCostPoly` (toggle upkeep), `kCooldownPoly`,
  `kMasterBonusPoly` (replaces amount at GM+), `kSplashAroundDamageAdjustPoly`,
  `kGrandMasterAddSPCostPoly`.
- `dwFlag` (`ESkillFlags`): ATTACK, USE_MELEE/MAGIC/ARROW_DAMAGE, COMPUTE_MAGIC_DAMAGE, SELFONLY,
  SPLASH, TOGGLE, USE_HP_AS_COST, PENETRATE, IGNORE_TARGET_RATING, STUN/SLOW/POISON/FIRE_CONT,
  REMOVE_BAD/GOOD_AFFECT, CRUSH, DISABLE_BY_POINT_UP, attribute bits (WIND/ELEC/FIRE).
- `dwAffectFlag(2)`: AFF_* bit granted while the buff runs.
- `iSplashRange`, `dwTargetRange` (server re-checks distance +50cm slack), `lMaxHit`,
  `bSkillAttrType` (NORMAL/MELEE/RANGE/MAGIC column in skilltable).

### Formula variables
Set before eval: `k` (see power below), `atk` (melee/magic/arrow damage by flag), `lv`, `iq`(INT),
`str`, `dex`, `con`, `def`, `odef`, `maxhp`, `maxsp`, `maxv` (for MOV_SPEED cap), `ar` (attack
rating vs victim), `wep` (weapon damage, via SetPolyVarForAttack), `chain`, `horse_level`,
`number(a,b)` (random). Example (Samyeon): `-(1.1*atk + (0.3*atk + 0.5*str + wep)*k)`.

### Skill vnums (`ESkillIndexes`)
Player skills come in per-job **groups of ~5-6 actives**: Warrior body 1-5, Warrior mental 16-20,
Assassin blade 31-35, Assassin archery 46-50, Sura weapons 61-66, Sura magic 76-81, Shaman dragon
91-96, Shaman healing 106-111. Support skills shared by all: 121 Leadership, 122 Combo, 123 Create,
124 Mining, 126-128 Languages, 129 Polymorph, 130/131/137-140 Horse, 141 AddHP, 142 Resist
Penetrate. Guild skills 151-162. `SKILL_MAX_NUM = 255`, `SKILL_MAX_LEVEL = 40`.

## 2. Learning and mastery (server, char_skill.cpp)

### Per-skill state (`TPlayerSkill`)
`bLevel` (0-40) + `bMasterType`, derived in `SetSkillLevel`:
- 0-19 = `SKILL_NORMAL`, 20-29 = `SKILL_MASTER` (M1-M10), 30-39 = `SKILL_GRAND_MASTER` (G1-G10),
  40 = `SKILL_PERFECT_MASTER` (P).

### Skill points
- On each level-up (only after a skill group is chosen): level >= 5 grants +1 `POINT_SKILL`
  (active points), level >= 9 grants +1 `POINT_SUB_SKILL` (support points). Horse skills use
  `POINT_HORSE_SKILL`.
- Skill group: chosen once at level >= 5 (`SetSkillGroup`, 1 or 2); without a group nothing can
  be learned and the client shows the group-selection page.

### SkillLevelUp(vnum, method)
Methods: `SKILL_UP_BY_POINT` (the `/skillup <vnum>` command the client sends when the + button is
pressed), `_BY_BOOK`, `_BY_TRAIN`, `_BY_QUEST`.
- Common gates: `IsLearnableSkill` (job/group match, below proto max), character level >=
  `bLevelLimit`, prerequisite skill satisfied, skill group chosen.
- BY_POINT only while `SKILL_NORMAL` (levels 1-19) and costs exactly 1 point per press
  (level down via reset gives the point back, `SkillLevelDown`).
- **Mastery breakthroughs are random**: while NORMAL, from level 17+ each further point-up rolls
  `number(1, 21 - level) == 1` and on success jumps the skill straight to 20 (Master). So M can
  arrive anywhere between 17 and 20 (at 20 the roll is 1/1). While MASTER, from 30+ the same
  pattern rolls to fix at 30 (Grand Master). GM->40 is quest-driven (`SKILL_UP_BY_QUEST` +
  grand-master book reading with counts per G-level); at 40 it's forced to PERFECT.
- BY_BOOK: reading skill books levels a MASTER skill (M1-M10); prob rolls per read, one book per
  cooldown (`GetSkillNextReadTime`, bypassable with the AFFECT_SKILL_NO_BOOK_DELAY item),
  20k EXP fee. Type-0 skills (support) can be learned from books from scratch.

## 3. Skill power (the `k` variable)

`k = GetSkillPower(vnum) * bMaxLevel / 100` where `GetSkillPower` = per-level percentage from a
**table indexed by [job*2+group][skillLevel]** (`CTableBySkill::GetSkillPowerByLevelFromType`,
streamed from the DB at boot). So the displayed skill percentage (1%..100%ish per level 1..40)
is data, not formula. Mobs use table row 0 indexed by their skill level. Guild skills:
`100 * guildSkillLevel / 7 / 7`.

## 4. Using a skill (server `UseSkill` -> `ComputeSkill`)

`UseSkill` order:
1. `CanUseSkill`, not polymorphed/observer, movement allowed; riding gates horse skills both ways.
2. Level > 0 required. Weapon rod/pick blocks. Charge skills (Tanhwan/Horse charge) have a
   two-phase dash: first use buffs self with AFF_TANHWAN_DASH, second (with target) computes.
3. Toggle skills with an active affect simply remove the affect (turn off) and return.
4. `k` computed; cooldown = `kCooldownPoly` adjusted by POINT_CASTING_SPEED
   (`ComputeCooltime` -> `CalculateDuration`).
5. SP cost = `kSPCostPoly` (vars maxhp/maxv/v); `USE_HP_AS_COST` skills pay HP instead
   (Sura's Heuksin etc.). Grand-master use pays `kGrandMasterAddSPCostPoly`.
6. Per-skill `TSkillUseInfo` tracks `dwNextSkillUsableTime` + hit budget (`lMaxHit`) so one cast
   can only damage N targets; splash re-uses the budget via `HitOnce`.
7. SELFONLY forces victim = self. Party buffs (94,95,96,109,110,111) apply to nearby party
   members (`ComputeSkillParty`).

`ComputeSkill` (also called per motion hit event for attack skills):
- Re-checks range (`dwTargetRange`+50), evaluates amounts 1-3.
- Attack skills with `bPointOn == HP` and negative amount run `FuncSplashDamage` (single target
  or `ForEachAround` within `iSplashRange`); damage type SKILL, elemental flags from proto.
  Splash victims beyond the first get damage scaled by `kSplashAroundDamageAdjustPoly`.
- Non-damage points with duration > 0 become **affects**: `AddAffect(vnum, bPointOn, amount,
  dwAffectFlag, duration, 0, ...)` (stacks 2nd/3rd affect with `bPointOn2/3`); duration gets
  +POINT_PARTY_BUFFER_BONUS. Duration == 0 -> instant `PointChange`.
- Grand-master mastery replaces amount with `kMasterBonusPoly` and unlocks the 3rd point slot.
- Muyeong (Sura's flying sword) is special: self affect + a 3s repeating event that auto-attacks
  a nearby victim with `ComputeSkill`. Chain lightning bounces via `SKILL_CHAIN` re-computes.

## 5. Client side

### Data (`CPythonSkill`)
The client loads its own copy of the skill table (same polys, parsed to `SSkillData`) plus
`skilldesc.txt` with per-skill: job, 3 grade names (base/Master/Grand names differ!), description,
condition lines, attribute flags (NEED_TARGET, STANDING_SKILL, TOGGLE, CAN_CHANGE_DIRECTION,
WEAPON_LIMITATION + weapon bitmask, FAN_RANGE, CHARGE_ATTACK, MOVING_SKILL...), icon name,
**motion index + per-grade motion count** (skill anims differ per mastery grade), target count
formula, and up to 3 affect description lines with min/max formulas for the tooltip
(`�ܹ����� %.0f-%.0f` + MinATK/MaxATK polys). `SKILL_GRADE_COUNT = 3`, grade icon offset
`SKILL_GRADEGAP = 25` (icon file index + 25 per grade).

### Client-side use checks (`PythonPlayerSkill.cpp`), before sending anything
Level learned, weapon type matches (`CanUseWeaponType`), arrows present for bow skills,
cooldown timer (client mirrors it: `fLastUsedTime + fCoolTime`), enough SP (`GetNeedSP` from the
same poly with current percentage) or HP for HP-cost skills, safe-zone block for horse skills,
NEED_TARGET enforcement (fires `OnCannotUseSkill(NEED_TARGET)` to the UI otherwise).
Then `SendUseSkillPacket(skillVnum [, targetVID])`; the motion plays and per-hit motion events
drive the server damage.

### Character window skill page (`uicharacter.py`)
- Two point pools shown: active (`player.SKILL_ACTIVE`) and support (`player.SKILL_SUPPORT`).
- Skill slots are **player skill slots** (1..N actives, 101+ supports) filled at login by
  `playersettingmodule.RegisterSkill(race, group, empire)` from `SKILL_INDEX_DICT[job][group]`
  (e.g. Warrior group 1 = vnums 1,2,3,4,5 + horse 137/138/139; support list
  122,123,121,124,125,129,130,131[,141,142]; the two languages of the other empires at 107+).
- Each active skill renders **one slot per mastery grade** (`__GetRealSkillSlot(grade, slot)`);
  only the current grade's slot is interactable, showing the in-grade level count. Support skills
  show a single slot.
- A **+ button** appears on a slot when: points > 0, grade is NORMAL, `skill.CanLevelUpSkill`,
  level limit met. Pressing sends the chat command `/skillup <vnum>` (yes - skill-ups ride the
  chat command pipe, GM_PLAYER level, exactly like our chat commands).
- Clicking a skill slot uses it (`player.ClickSkillSlot` -> use pipeline); slots can also be
  dragged to the quickslot bar (quickslot type SKILL stores the *slot* index).
- `OnUseSkill(slot, cooltime)` paints the cooldown pie on both the character window and
  quickslots; `OnActivateSkill/OnDeactivateSkill` toggle the "on" highlight for toggle skills.
- Group selection page: before a group is chosen (level 5+) the window shows the two group
  names for the job (e.g. Body/Mental) and `net.SendChatPacket("/group <n>")`-style selection
  (via `player.SetSkillGroupFake` + confirm dialog).

## 6. Recreation notes for UE

- The importer produces `UMT2SkillDefinition` assets and skill sets. Definitions carry type/job, group, max level, level
  limit, prerequisite, up to 3 (point, formula) pairs, SP/duration/cooldown/master-bonus
  formulas, flags, affect flags, splash/target range, max hits, per-grade motion + icon.
- Skill definitions carry `SkillPowerPercentByLevel` curves, and the runtime formula evaluator supplies supported variables. The original table is source data, not a promised separate runtime table.
- Mastery is derived from level; point progression includes the 17–20 Master breakthrough. Master/Grand Master progression then uses the current book/training paths, not a blanket claim that all original breakthrough paths are reproduced.
- The original `/skillup` was a player-level command. In this port `ServerExecuteChatCommand` remains admin-gated; ordinary skill progression uses skill RPC/component paths, not a promised command whitelist.
- Affect application reuses our UMT2StatusEffectComponent (AddAffect(vnum, applyType, value,
  affectFlag, duration)) - same pipeline as item/affect systems already built.
- Cooldown/SP checks belong both client-side (responsiveness, `OnCannotUseSkill` UX strings)
  and server-side (authoritative TSkillUseInfo with next-usable-time + hit budget).

## Tooltip layout (uitooltip.py SkillToolTip)

`AppendSkillDataNew` + `AppendSkillLevelDescriptionNew` print, in order:

1. **Title** = grade name, plus the "<base name> Master/Grand Master/Perfect Master" line.
2. **Required level** (`GetSkillLevelLimit`), red when the player is below it.
3. **Description**.
4. **Current level** block ("Level: N", or "Master level N" at the cap), lit (ENABLE_COLOR):
   - **Affect lines** from `GetNewAffectData(skillIndex, i, percentage)` -> `(type, minValue, maxValue)`.
     `AFFECT_NAME_DICT` maps POINT_ON to the label:
     | POINT_ON | Label | Suffix |
     | --- | --- | --- |
     | `HP` | Attack Power (or **Heal** when the value is positive) | |
     | `ATT_GRADE` | Attack Value | |
     | `DEF_GRADE` | Defence | |
     | `ATT_SPEED` | Attack Speed | |
     | `MOV_SPEED` | Movement Speed | |
     | `DODGE` | Evasion Chance | `%` |
     | `RESIST_NORMAL` | Melee Resistance | `%` |
     | `REFLECT_MELEE` | Melee Reflection | `%` |
     Unmapped types are **skipped** (`if not AFFECT_NAME_DICT.has_key(type): continue`).
     Text = label + `min` + (`" - " + max` when they differ) + suffix.
     `HP` is negated for display when both ends are negative; otherwise it becomes the Heal line.
   - **Duration** (`GetDuration`), **Cooltime** (`GetSkillCoolTime`), **SP** (`GetSkillNeedSP`, printed
     as HP instead when `IsUseHPSkill`, plus `GetSkillContinuationSP` for channelled costs).
5. **Next level** block, identical but greyed (DISABLE_COLOR), headed "Next level: N/Max".
6. **Requirements** (`AppendSkillRequirement`): prerequisite skill + level (green when satisfied,
   red when not) and any required stats.

### How the min-max range is produced (PythonSkill.cpp:2136)

```cpp
CPoly minPoly, maxPoly;
minPoly.SetRandom(CPoly::RANDOM_TYPE_FORCE_MIN);
maxPoly.SetRandom(CPoly::RANDOM_TYPE_FORCE_MAX);
// ... both SetStr(strPointPoly)
float fMinValue = pSkillData->ProcessFormula(&minPoly, fSkillLevel, VALUE_TYPE_MIN);
float fMaxValue = pSkillData->ProcessFormula(&maxPoly, fSkillLevel, VALUE_TYPE_MAX);
```

`ProcessFormula` (:1250) walks the poly's variables and fills them from live character state
(`GetState(name, &state, iMinMaxType)`), with `k`/`SkillPoint` = the skill percentage and `ar`
divided by 100. So **the tooltip evaluates the same poly the server damages with** - it just forces
the weapon roll to each end. Our port mirrors this with
`AMT2PlayerCharacter::BuildSkillFormulaVariables(Definition, Level, EMT2SkillFormulaValue)`, shared
by the cast and the tooltip.

## Target rules (client PythonPlayerSkill.cpp __RunUseSkill, ~line 600)

The **client** decides the victim before the skill packet is ever sent, so a refused cast costs
nothing. With nothing currently targeted:

```cpp
if (pSkillData->IsAutoSearchTarget()) {                       // SKILL_ATTRIBUTE_SEARCH_TARGET
    if (pkInstMain->NEW_GetFrontInstance(&pkInstTarget, 2000.0f)) { SetTarget(...); rangecheck; }
    else { OnCannotUseSkill("NEED_TARGET"); return false; }
}
if (pSkillData->CanUseForMe())      pkInstTarget = pkInstMain;   // SKILL_ATTRIBUTE_CAN_USE_FOR_ME
else if (pSkillData->IsNeedCorpse()) { OnCannotUseSkill("ONLY_FOR_CORPSE"); return false; }
else                                 { OnCannotUseSkill("NEED_TARGET");     return false; }
```

So only `SEARCH_TARGET` skills auto-aim; everything else that needs a target is **refused**, never
auto-aimed and never silently dropped.

`NEW_GetFrontInstance` (InstanceBaseBattle.cpp:145) is a **distance-narrowing fan**:

```cpp
const float HALF_FAN_ROT_MIN = 10.0f;          // half-angle at 1000+ units
const float HALF_FAN_ROT_MAX = 50.0f;          // half-angle at point blank
const float HALF_FAN_ROT_MIN_DISTANCE = 1000.0f;
const float RPM = (HALF_FAN_ROT_MAX-HALF_FAN_ROT_MIN)/HALF_FAN_ROT_MIN_DISTANCE;   // 0.04
float fHalfFanRot = (HALF_FAN_ROT_MAX-HALF_FAN_ROT_MIN) - RPM*min(dist,1000) + HALF_FAN_ROT_MIN;
```

Candidates go into a `std::multimap<float, CInstanceBase*>` keyed by distance, so the **nearest**
one inside the fan wins. Ported as `UMT2CombatComponent::FindFrontTarget`.

### SKILL_ATTRIBUTE_* names (client PythonSkill.h:91)

`NEED_TARGET`, `TOGGLE`, `WEAPON_LIMITATION`, `MELEE_ATTACK`, `USE_HP`, `CAN_CHANGE_DIRECTION`,
`STANDING_SKILL`, `ONLY_FOR_ALLIANCE`, `CAN_USE_FOR_ME`, `NEED_CORPSE`, `FAN_RANGE`,
`CAN_USE_IF_NOT_ENOUGH`, `NEED_EMPTY_BOTTLE`, `NEED_POISON_BOTTLE`, `ATTACK_SKILL`,
`TIME_INCREASE_SKILL`, `CHARGE_ATTACK`, `PASSIVE`, `CANNOT_LEVEL_UP`, `ONLY_FOR_GUILD_WAR`,
`MOVING_SKILL`, `HORSE_SKILL`, `CIRCLE_RANGE`, `SEARCH_TARGET`.
They live in the skilldesc ATTRIBUTE column, `|`-separated; read them with
`UMT2SkillDefinition::HasClientAttribute` (whole-name match - a substring test would make
"NEED_TARGET" match a search for "TARGET").

### Refusal messages (locale/en/locale_game.txt)

`game.py OnCannotUseSkill` shows `USE_SKILL_ERROR_TAIL_DICT[type]` over the character:
- `CANNOT_SKILL_NEED_TARGET` = "Who is the target?"
- `CANNOT_SKILL_NOT_MATCHABLE_WEAPON` = "I cannot use this skill with this weapon."
- `CANNOT_SKILL_WAIT_COOLTIME` = "I cannot use this skill yet."
- `CANNOT_SKILL_NOT_ENOUGH_SP` = "I do not have enough SP!"

We have no text-tail system yet, so `ClientNotifySkillDenied` prints these into chat instead.

## Skill damage timing (hit frames, ticks, single vs splash)

Sources: server `input_main.cpp` (`CInputMain::Attack`), `char_battle.cpp` (`CHARACTER::Attack`),
`char_skill.cpp` (`UseSkill`, `ComputeSkill`, `FuncSplashDamage`, `TSkillUseInfo`); client
`GameLib/GameType.h` (`SAttackData`/`SHitData`/`SMotionAttackData`), `RaceMotionDataEvent.h`
(`SMotionEventDataAttack`); the `.msa` motion files under `pc/.../skill/`.

**Damage is NOT applied on cast.** The old flow:

1. Client `SendUseSkillPacket(index)` on use. Server `UseSkill` spends SP/HP, sets the cooldown, and
   stores the hit budget: `m_SkillUseInfo[vnum].UseSkill(..., splashcount, lMaxHit)` sets
   `iHitCount = lMaxHit`. **For an ATTACK skill it does NOT call `ComputeSkill` here** - look at the
   tail of `UseSkill`: only SELFONLY / non-ATTACK / a few named skills compute immediately.
2. The client plays the skill motion. The motion's `.msa` carries the hit timing:
   - `Group MotionEventData` entries with `MotionEventType 4` (SPECIAL_ATTACKING) - each is one hit,
     with a `StartingTime` (seconds into the motion), `DuringTime`, `HitLimitCount` (splash cap) and
     a collision sphere (`Radius`, `Position`).
   - and/or the top-level `Group AttackingData` with `HitData` windows
     (`AttackingStartTime`/`AttackingEndTime`) used by the normal-attack collision path.
   At each hit frame the client's `AttackingProcess` (`__SplashAttackProcess` /
   `__NormalAttackProcess`) detects a collision and sends `HEADER_CG_ATTACK` with `bType = vnum`.
3. Server `CInputMain::Attack`: `CheckSkillHitCount(bType, victimVID)` -> `TSkillUseInfo::HitOnce`
   decrements `iHitCount` (allows the hit while budget remains; `-1` = unlimited), then
   `CHARACTER::Attack(victim, bType)` -> `ComputeSkill(bType, victim)` applies the actual damage
   (`FuncSplashDamage` when `SKILL_FLAG_SPLASH`, else single target).

**So the number of `MotionEventType 4` events = the number of damage ticks, and each event's
`StartingTime` = when that tick lands.** Concrete examples (both 1.333s motions):
- `warrior/skill/samyeon.msa` (Samyeon, vnum 1): THREE type-4 events at `0.162 / 0.435 / 0.850`,
  `HitLimitCount 6` -> three area hits.
- `assassin/skill/amseup.msa`: ONE type-4 event at `0.428`, `HitLimitCount 10`, sphere radius 130 ->
  one area hit.
- A pure buff (e.g. `warrior/skill/cheongeun.msa`) has only `MotionEventType 1` (EFFECT) events and
  no attack event -> no damage.

`lMaxHit` (skilltable `iMaxHit`, our `Definition->MaxHits`) is the splash victim cap per hit, and
also the total hit budget in `TSkillUseInfo`.

### Port

Since our combat is fully server-authoritative (no client hit packets), we replicate the timing on
the server: the importer records each type-4 event's `StartingTime` into
`UMT2AnimationMotionData::AttackHitTimes` (per cast animation / grade), and
`UMT2SkillCastComponent` schedules `ApplySkillDamageToTargets` at each of those offsets after the
cast starts (re-resolving the target each tick), instead of once on cast. Multiple times = multiple
ticks; SPLASH vs single-target is unchanged (SplashRange + MaxHits). With no recorded times (buffs,
or not-yet-reimported skills) it falls back to a single hit at a fraction of the motion.

## Skill formula variables (how skill damage scales with stats)

Source: `char_skill.cpp` `ComputeSkill` -> `SetPointVar(...)`, and `battle.cpp` `CalcMeleeDamage` /
`CalcMagicDamage`; the attack-power derivation is in `char.cpp` `PointChange(POINT_ATT_GRADE, ...)`.

Before evaluating a skill's point poly, the old server fills these variables:

| Var | Old source | Notes |
| --- | --- | --- |
| `k` | `GetSkillPower(vnum,lvl) * bMaxLevel / 100` | skill power factor |
| `atk` | `CalcMeleeDamage` **or** `CalcMagicDamage` **or** `CalcArrowDamage` | chosen by the skill's `USE_MELEE_DAMAGE` / `USE_MAGIC_DAMAGE` / `USE_ARROW_DAMAGE` flag |
| `lv` | `GetLevel()` | |
| `str` `dex` `con` `iq` | `GetPoint(POINT_ST/DX/HT/IQ)` | raw primary stats |
| `maxhp` `maxsp` | `GetMaxHP()` / `GetMaxSP()` | |
| `def` `odef` | `GetPoint(POINT_DEF_GRADE)` (odef removes the def bonus) | |
| `ar` | `CalcAttackRating(this, this)` | 0..1 factor |
| `chain` `horse_level` | 0 / `GetHorseLevel()` | |

**Attack power carries the stats.** `POINT_ATT_GRADE = 2*level + statAttack (+ weapon, *AR)`, where
`statAttack` is `2*STR` (warrior/sura), `(4*STR+2*DEX)/3` (assassin), `(4*STR+2*IQ)/3` (shaman).
`POINT_MAGIC_ATT_GRADE = 2*level + 2*IQ`. So a physical skill (e.g. Samyeon,
`-(1.1*atk + (0.5*atk + 1.5*str)*k)`) scales with STR both through `atk` and the raw `str` term,
while a magic skill scales with IQ through the magic `atk`.

### Port

`AMT2PlayerCharacter::BuildSkillFormulaVariables` provides all of the above. `atk` is selected
by the skill's `USE_MAGIC_DAMAGE` flag (magic -> `Combat.MagicAttack`, else `Combat.DamageMax`),
both of which already move with allocated points via `MT2PlayerStatFormula::Calculate`
(`DamageMax = 2*level + statAttack`, `MagicAttack = 2*level + 2*IQ`).  `maxhp/maxsp/def/odef/ar` are
populated too. The cast and tooltip share this function for formula inputs.

## Motion effect orientation (.msa MotionEventData)

Skill effects are attached by the motion script, not the skill definition. Each `Group EventNN` in a
`.msa` names a bone and flags:

| skill | motion | bone | following |
|-------|--------|------|-----------|
| Spirit Strike (W) | `gigongcham` | `Bip01 Spine1`, `Bip01 Head` | yes / no |
| Sword Strike | `geompung` | `Bip01 R Hand` | yes |
| Bash | `gyeoksan` | `Bip01 Footsteps` | yes |

The client's basis is **-Y forward**, UE's is **+X**. The motion parser already converts effect
positions through it — `EffectPosition (x,y,z)` becomes `(-y, x, z)`, which is a **+90° yaw** — but
the rotation the emitter spawned with never went through the same conversion, so every bone-attached
effect came out turned 90°. The three skills above make it obvious because their effects are
directional planes rather than radial bursts.

The +90° yaw is applied on every motion-effect spawn path (independent, attached-following,
attached-snapshot, and mesh-root fallback). The separate **+90° roll about local X** for weapon
attachments (`equip_*`, `* R Hand`, `* L Hand`) is a different correction — it compensates the
imported weapon mesh, not the basis — so weapon-attached effects carry both, composed as
`BasisYaw * WeaponRoll`.
