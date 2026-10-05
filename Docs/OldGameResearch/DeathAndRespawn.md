# Old Game Research: Death, Respawn, HP Regeneration

Sources studied: `server_src/game/src/cmd_general.cpp` (do_restart), `server_src/game/src/char.cpp`
(recovery_event, Disconnect), `server_src/game/src/char_battle.cpp`.

## Respawn ("restart") flow

Client sends `restart_here` / `restart_town` (only allowed while POS_DEAD, `cmd.cpp:340-341`);
handled by `do_restart` (`cmd_general.cpp:477`).

Normal (non-war, non-event) path (`cmd_general.cpp:624-647`):

1. Anti-abuse timers: cannot restart before ~10s after death (~7s for town) — the dead-event
   countdown starts at 180s and restart is refused while more than 170s/173s remain.
2. `SetPosition(POS_STANDING)` and `StartRecoveryEvent()` (passive regen resumes).
3. **HP is set to a flat 50** — `PointChange(POINT_HP, 50 - GetHP())` — *not* a percentage and not
   full. The passive recovery event then heals the character back up over time.
4. Restart-town warps to the empire recall position; restart-here revives in place with
   `ReviveInvisible(5)` (5 seconds of untargetable revive-invisibility).
5. `DeathPenalty(1)` on town restart, `DeathPenalty(0)` on restart-here (exp loss roll).
6. Special maps (guild war, threeway war, dungeons) instead restore FULL HP/SP — those are the
   exception, not the rule.

Also: logging out while dead applies the same flat-50 HP before saving (`char.cpp:1326`).

## Passive HP regeneration (recovery_event, `char.cpp:2373-2505`)

- PC tick every 3 seconds. No regen while poisoned; event never runs while dead
  (`StartRecoveryEvent` refuses if dead/stunned).
- Heal per tick: `15 + MaxHP * percent / 100`, where percent is **1 if the character moved/attacked
  /was hit within the last ~3s, else 5** (lookup `{1,5,5,...}` indexed by idle seconds/3).
- `POINT_HP_REGEN` gear stat adds a further percentage on top.
- Mobs use their mob_proto `RegenCycle`/`RegenPercent` in the same event instead.

## UE recreation mapping

- `AMT2PlayerCharacter::ServerRequestRespawn` sets health to `min(50, MaxHealth)` instead of max;
  the already-implemented 3s regen loop (mirroring recovery_event) heals the character up.
- Death penalty, revive-invisibility, and the 10s restart delay are not yet implemented (TODO).
