# Old Game Research: Attack Speed & Movement Speed

Sources studied: `client_src/source/GameLib/ActorInstance.cpp`,
`client_src/source/UserInterface/InstanceBaseMovement.cpp`, `server_src/game/src/utils.cpp`,
`server_src/game/src/char.cpp`, `server_src/game/src/char_state.cpp`.

## Attack speed (client presentation)

- The attack-speed *point* (POINT_ATT_SPEED) is a percent with **100 as baseline**.
- The client converts it straight to an animation play-rate:
  `CInstanceBase::SetAttackSpeed` divides the stat by 100 (`InstanceBaseMovement.cpp:6-12`) and the
  actor plays attack motions with that multiplier (`ActorInstance.cpp:159-162` returns `m_fAtkSpd`;
  attack motions are launched with it in `ActorInstanceBattle.cpp:482/496`).
- So: attack speed 100 → animations play at 1.0×; a basic sword (+22 attack speed apply from
  item_proto) → 122 → **1.22× play rate**. Gear/buff attack speed literally speeds up the swing
  animation, and the swing cadence follows the (shortened) animation.
- Movement speed works identically: `SetMoveSpeed(stat/100)` scales run/walk animation rate.

## Attack/movement timing (server authority)

- The server derives durations from the same percent via `CalculateDuration(speed, duration)`
  (`utils.cpp:136`). In effect: at 100 the duration is unchanged; above 100 the duration shrinks as
  `duration * 100 / (100 + over)`; below 100 it grows as `duration * (100 + under) / 100`
  (e.g. speed 122 → ~82% of the base duration; speed 80 → 120%).
- Uses: movement duration (`char.cpp:2762 CalculateMoveDuration`), move speed
  (`char.cpp:2757 GetMoveSpeed`), monster attack cool (`char_state.cpp:1067`, base 2000ms), skill
  cast time (`char_skill.cpp:153` with POINT_CASTING_SPEED).

## UE recreation mapping

- Attack montages (player and mob) are played with `PlayRate = AttackSpeed / 100` and the
  swing-repeat timer uses `AnimationLength / PlayRate`, so gear/buffs that add APPLY_ATT_SPEED
  (e.g. Sword+0 = +22) visibly and mechanically speed up combat exactly like the old game.
- `UMT2CombatStatsComponent::GetCalculatedStats().AttackSpeed` is the stat source (base 100 +
  equipment/affect bonuses).
