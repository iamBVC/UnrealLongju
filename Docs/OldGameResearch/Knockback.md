# Knockback / CRUSH (old game)

Two separate systems push a victim: **skill CRUSH flags** (server-authoritative slide) and the
**per-motion ExternalForce** carried by attack animations.

## 1. Skill CRUSH (server: char_skill.cpp, FuncSplashDamage::operator())

```cpp
if (IS_SET(m_pkSk->dwFlag, SKILL_FLAG_CRUSH | SKILL_FLAG_CRUSH_LONG) &&
    !IS_SET(pkChrVictim->GetAIFlag(), AIFLAG_NOMOVE))
{
    float fCrushSlidingLength = 200;
    if (m_pkChr->IsNPC()) fCrushSlidingLength = 400;      // mobs shove twice as far
    if (IS_SET(m_pkSk->dwFlag, SKILL_FLAG_CRUSH_LONG)) fCrushSlidingLength *= 2;

    float degree = GetDegreeFromPositionXY(attacker.x, attacker.y, victim.x, victim.y);
    GetDeltaByDegree(degree, fCrushSlidingLength, &fx, &fy);
    victim->Sync(tx, ty); victim->Goto(tx, ty); victim->CalculateMoveDuration();
    // YMIR locale also stuns the main target for 3s (AFF_STUN)
}
```

Key points:
- Distance: **200** units base, **400** if the attacker is a mob, **×2** for `CRUSH_LONG`
  (so 200 / 400 / 800).
- Direction: straight **away from the attacker** (the degree from attacker→victim), so the victim
  slides along that line. Horse wild-attack is the exception: it shoves ±90° sideways.
- **Skipped entirely when the victim has AIFLAG_NOMOVE** (stones, doors, bosses that don't move).
- `Sync` actually sets the authoritative coordinates immediately (`char.cpp`), followed by
  `Goto`/movement-duration bookkeeping and client synchronization. Presentation blends the
  displacement. The UE port intentionally uses swept movement rather than teleporting through obstacles.

## 2. Per-motion ExternalForce (.msa AttackingData)

Every attack motion carries an `AttackingData` block:

```
Group AttackingData {
    AttackType 0
    HittingType 1            // 1 = "blow" hit (knockback), 2 = normal hit
    StiffenTime 0.0
    InvisibleTime 1.0
    ExternalForce 20.0       // how hard the victim is pushed
    HitLimitCount 18
    ...
}
```

Measured from `ymir work/pc/warrior/twohand_sword/`:

| motion | HittingType | ExternalForce |
|---|---|---|
| combo_01 | 2 | 3 |
| combo_02 | 2 | 5 |
| combo_03 | 2 | 5 |
| **combo_04** (chain finisher) | **1** | **20** |
| combo_05 | 2 | 5 |
| combo_06 | 2 | 5 |
| **combo_07** (chain finisher) | **1** | **20** |
| general/attack, attack_1 | 2 | 0 |

So the **last hit of a combo chain** is authored with a much larger ExternalForce and
HittingType 1 - that is the knockback the player feels. Plain unarmed attacks have force 0.
Skills carry their own values (e.g. `skill/palbang.msa` = HittingType 1, ExternalForce 10).

## UE recreation

- Updated implementation review: 2026-10-08.
- Import each normal attack window and SPECIAL_ATTACKING event into
  `UMT2AnimationMotionData::AttackEvents`, preserving start/end time, ExternalForce and HittingType.
  Existing scalar fields remain for backwards compatibility and metadata inspection.
- On a landed hit, push the victim away from the attacker by a distance scaled from
  ExternalForce; skip when the victim is NOMOVE (matching the CRUSH rule).
- The current player skill path uses CRUSH/CRUSH_LONG distances of 200/400, independent of motion force. Original NPC-attacker 400/800 variants are not implemented by that player-only casting path.
- `AMT2CharacterBase` applies a native `FMT2KnockbackRootMotion` source, with reliable
  notification to the autonomous owner. Simulated proxies follow replicated movement rather
  than launching independently. Movement stays collision-aware and retains no-walk checks.
- Mob movement uses UE's walking solver only while root-motion sources are active, and AI tick
  policy keeps the movement component enabled during the shove and is restored afterward.
  Death clears the named source so it cannot resume on revival. Normal mob attacks also use
  the actual played motion's force. Stones, NPCs, buildings, doors, NOMOVE and legacy huge
  race 2493 reject pushes; dead victims are not moved.
- Player swing metadata is resolved from the configured animation-class defaults and the
  authoritative weapon set, even when the server has no live AnimInstance. Equipped swings
  must never silently fall back to the zero-force unarmed attack. The warrior sword regression
  exercises all four hits through damage delivery and checks the target's movement source.
- The warrior sword finisher stores force 17 in a special-attack event at 0.659316 seconds,
  while its base block says force 0 and has no active normal window. Combat now schedules damage,
  reaction and shove together at that event, divided by the actual attack-motion play rate.
  Normal swings use their authored attack-window start. Server timers work without ticking a
  skeletal mesh, so dedicated servers do not depend on visual animation notifies.
- Each scheduled hit captures its own force/type; Samyeon's earlier zero-force hits no longer
  borrow its finishing hit's force. Skill damage scheduling uses the same per-event metadata.
  Current targets/range/hostility are rechecked when the event fires. A new swing, skill cast,
  mob knockdown, death or teardown cancels pending basic-hit timers. Releasing attack input
  does not cancel the currently playing swing. Native/data-less compatibility attacks retain
  their immediate damage path; exact animated timing requires the baked event data.
- `MT2GeneratePlayerAnimation -RepairAttackEventMetadata` uses configured `LegacyYmirWorkRoot`
  to add attack-event metadata without regenerating tracks, notifies, meshes or materials.
  Close the editor before running it. The initial repair updated 835 attack-animation packages
  with zero save failures. Re-running skips unchanged metadata. There is no runtime dependency
  on legacy source files, and no custom asset version or renamed serialized fields.
- Attack force no longer uses the arbitrary force x 10 approximation. It integrates the
  legacy client's mass=1/friction=.3 accumulation for up to 100 steps: force 3 yields 13.5 cm,
  force 20 yields 656.7 cm. The client blends this result for 100 x .02 = 2 seconds. The native
  UE source now integrates the legacy ease-out velocity ramp (twice average speed down to zero)
  over each simulation step. It avoids the old frame-rate-dependent Euler integration and final-frame
  reset, so this is not bit-for-bit legacy displacement at every frame rate.
- Player CRUSH/CRUSH_LONG skills select 200/400 cm independently of motion force. Other attack
  skills use the landed event's force. Horse wild attack (137) shoves laterally. Splash hits
  carry the same push parameters to each victim; rejected hits cannot leak force into another swing.
- The invented 0.25-second CRUSH duration and both timing overrides have been removed from
  Gameplay settings/INI. CRUSH uses `ActorInstance.h::SetBlendingPosition`'s one-second default;
  per-motion pushes retain the legacy two-second physics duration. Neither duration is a stun lock.
- Mob reactions carry the authored hit type: GOOD (2) plays ordinary damage; GREAT (1) selects
  front/back knockdown, queues stand-up, then returns to wait. CRUSH sync uses front knockdown
  as in `ActorInstanceSync::__Push`. Authority locks AI movement, basic attacks and skill motions
  for the actual chosen animation durations, independent of slide duration. Further reactions
  cannot interrupt fall/recovery; active special motions are not interrupted by GREAT hits.
  Death/teardown cancel queued recovery. The selected animation variant is replicated, so peers
  do not randomly select clips of different lengths. `BackStandup` is appended to the enum to
  preserve saved indices, and the importer recognizes `BACK_STANDUP` entries.
- Some legacy mob lists (including wild dog) contain back knockdown but no back stand-up.
  This port reuses the existing front recovery in that case; no additional recovery delay is invented.
  No animation packages or mob Blueprints need resaving for their existing front motions.
- Full legacy parity is not claimed: UE swept collision/no-walk movement intentionally replaces
  server coordinate teleporting; regional CRUSH stun, player resist-fallen affect reactions and
  NPC skill CRUSH casting remain outside this mob animation change. Normal hit timing uses
  attack-window entry with the existing UE overlap/line-of-sight targeting, not a full recreation
  of the original client's per-frame weapon-sphere trajectories.
- Tests: `Metin2.Combat.KnockbackRules` audits imported force metadata; `KnockbackMovement`
  exercises a grounded shove, bounded distance, walls, no-walk cells, sideways pushes,
  tick/death cleanup and immovable targets. `MobKnockdownRecovery` uses the imported wild-dog
  animations to verify animation-driven fall/recovery locks, repeated-hit protection, back-hit
  fallback, death cancellation and ease-out movement. `WarriorSwordKnockback` now verifies no
  damage/push before each imported sword hit event, simultaneous damage/push at the event,
  attack-speed scaling, independent multi-hit forces, cancellation and range revalidation. Live multiplayer
  latency, animation feel and packaged builds still need playtesting.
