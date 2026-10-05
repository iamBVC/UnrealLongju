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
- It's a slide (Sync + Goto + CalculateMoveDuration), not a teleport - the victim travels there.

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

- Source review: 2026-10-06. The following mechanisms exist in source; they are not wholly unimplemented proposals.
- Import `ExternalForce` + `HittingType` from the .msa `AttackingData` into
  `UMT2AnimationMotionData` (alongside the existing MotionDuration/Accumulation).
- On a landed hit, push the victim away from the attacker by a distance scaled from
  ExternalForce; skip when the victim is NOMOVE (matching the CRUSH rule).
- The current player skill path uses CRUSH/CRUSH_LONG distances of 200/400, independent of motion force. Original NPC-attacker 400/800 variants are not implemented by that player-only casting path.
- `AMT2CharacterBase` uses `LaunchCharacter` for authoritative sliding and rejects unsupported/NOMOVE victims. Player attack motion queues distance from `ExternalForce`; skill-cast code handles CRUSH distances separately.
- Exact physics, distance, and motion fidelity require playtesting; source inspection is not proof of equivalence to legacy Goto/CalculateMoveDuration.
