# Damage sharing, reward distribution and multi-target attacks

Sources read:
- `server_src/game/src/char_battle.cpp` (`CHARACTER::Damage`, `CHARACTER::DistributeExp`, `CHARACTER::Reward`)
- `server_src/game/src/char.h` (`m_map_kDamage`)
- `server_src/game/src/char_skill.cpp` (`FuncSplashDamage`, `CHARACTER::ComputeSkill`)
- `40250 client sources/source/UserInterface/InstanceBaseBattle.cpp` (`CInstanceBase::AttackProcess`, `CheckAttacking`)
- `40250 client sources/source/GameLib/ActorInstanceCollisionDetection.cpp`
  (`AttackingProcess`, `__NormalAttackProcess`, `__SplashAttackProcess`)

## 1. The damage map

Every character carries `TDamageMap m_map_kDamage` - `VID -> TBattleInfo { iTotalDamage, iTotalHealth }`.
It is only ever filled **on NPC victims**, in `CHARACTER::Damage` (char_battle.cpp:2299):

```cpp
if (pAttacker && dam > 0 && IsNPC())
{
    TDamageMap::iterator it = m_map_kDamage.find(pAttacker->GetVID());
    if (it == m_map_kDamage.end())
        m_map_kDamage.insert(TDamageMap::value_type(pAttacker->GetVID(), TBattleInfo(dam, 0)));
    else
        it->second.iTotalDamage += dam;

    StartRecoveryEvent();
    UpdateAggrPointEx(pAttacker, type, dam, it->second);
}
```

Notes:
- `dam` recorded is the **final applied damage** (post-defense), not the raw roll.
- The map is cleared at the end of `Reward()` (char_battle.cpp:964) and drives aggro too.

## 2. Experience: `CHARACTER::DistributeExp` (char_battle.cpp:2652)

1. `iExpToDistribute = GetExp()`; bail if <= 0.
2. Walk the damage map. **Skip** an entry when the attacker is gone, `IsNPC()`, or
   `DISTANCE_APPROX(...) > 5000` (50 m). So a mob that helped kill another mob earns nothing,
   and a player who ran away loses their share.
3. Accumulate `iTotalDam`; track `pkChrMostAttacked` = single biggest damager.
4. Party members are folded into one `TDamageInfo` per party (their damage sums).
5. `SetExp(0)` - the mob's exp pool is consumed exactly once.
6. If `iTotalDam == 0`, nobody gets anything.
7. **If the mob was spawned by a metin stone (`m_pkChrStone`), the stone takes half the exp**
   (`iExp = iExpToDistribute >> 1`), which is later paid out when the stone itself dies.
8. `DistributeHP(pkChrMostAttacked)` - the "blood" system.
9. **Top damager bonus**: the biggest damager (or their party) is handed
   `iExp = iExpToDistribute / 5` (a flat **20%** of the pool) **plus** their proportional cut of the
   remaining 80%: `iExp += iExpToDistribute * (di->iDam / iTotalDam)`.
   If their percentage is 1.0 (they did all the damage) the function returns immediately.
10. Everyone else gets `iExpToDistribute * (di.iDam / iTotalDam)` of the remaining **80%**.
    Note `fPercent` uses the *original* total damage and is not renormalised after the top damager
    is removed, so the sum stays consistent.
11. Returns `pkChrMostAttacked` - this is `pkAttacker` for the whole of `Reward()`.

So exp is proportional to damage dealt, **except** the top damager takes an extra flat 20% off the
top. A solo killer gets 100% either way.

## 3. Gold and loot: `CHARACTER::Reward` (char_battle.cpp:~760-960)

`pkAttacker` here is always the **top damager** returned by `DistributeExp`.

- Alignment, quest kill, `KILL_HP_RECOVERY` / `KILL_SP_RECOVER`: all applied to the top damager only.
- `RewardGold(pkAttacker)` - gold is rolled and credited against the top damager.
- Items: `ITEM_MANAGER::CreateDropItem(this, pkAttacker, s_vec_item)`.
  - **The drop roll itself is not split.** Every rolled item always drops; what gets distributed is
    *ownership* (the exclusive right to pick it up before it becomes free-for-all).
  - 1 item -> ownership goes to `pkAttacker`.
  - More than 1 item:
    ```cpp
    while (!pq.empty() && pq.top().first * 10 >= total_dam)   // >= 10% of total damage
        v.push_back(pq.top().second);
    ```
    i.e. only damagers who dealt **>= 10%** of the total damage are eligible, walked in descending
    damage order (`std::priority_queue`). Items are then handed out **round-robin** over `v`,
    wrapping around - *not* weighted by damage.
  - If `v` is empty, everything drops with **no ownership** (free for anyone).
  - A party member's ownership is redirected through `GetNextOwnership` (party round-robin).
- Drops scatter at `number(-7, 7) * 20` around the corpse.
- `m_map_kDamage.clear()` at the very end.

**Takeaway**: exp is damage-proportional (+20% top-damager bonus); loot is *not* damage-proportional
- all items drop regardless, and only pickup ownership is shared round-robin among >=10% damagers.

## 4. Attacks are multi-target (AoE) by design

`CInstanceBase::AttackProcess` (InstanceBaseBattle.cpp:413) loops over **every** character instance
in the world each frame of a swing and tests each one:

```cpp
while (rkChrMgr.CharacterInstanceEnd() != i) {
    CInstanceBase* pkInstEach = *i; ++i;
    if (!IsAttackableInstance(*pkInstEach)) continue;   // faction / type filter
    if (pkInstEach != this && CheckAttacking(*pkInstEach)) pkInstLast = pkInstEach;
}
```

There is no "pick the closest target" step - one swing attacks **everything it collides with**, and
an attack packet is sent per victim. `m_dwLastDmgActorVID` only remembers the last one for UI.

`CActorInstance::AttackingProcess` = `__SplashAttackProcess` first, then `__NormalAttackProcess`:

- `__NormalAttackProcess` (ActorInstanceCollisionDetection.cpp:333): broad-phase distance reject at
  **300** units (**500** for `IS_HUGE_RACE` victims), then sphere-vs-sphere collision of the motion's
  `HitDataContainer` (the .msa attack spheres) against the victim's body spheres. A per-swing
  `THitDataMap -> THittedInstanceMap` guarantees **each victim is hit at most once per hit event**
  (combo motions re-arm per hit).
- `__SplashAttackProcess` (:267): broad-phase reject at **1000** units, requires `__IsInSplashTime()`,
  and keeps its own `HittedInstanceMap` so each victim is splashed once per window.

Server side, splash skills use `FuncSplashDamage` (char_skill.cpp:1063, dispatched at :1791):

```cpp
FuncSplashDamage f(posTarget.x, posTarget.y, pkSk, this, iAmount, iAG, pkSk->lMaxHit, ...);
if (IS_SET(pkSk->dwFlag, SKILL_FLAG_SPLASH)) GetSectree()->ForEachAround(f);
else f(this);
```

- The splash is centred on the **target position**, not the caster.
- Radius = `pkSk->iSplashRange`; victims outside are rejected.
- `battle_is_attackable(m_pkChr, pkChrVictim)` is the faction filter.
- `m_iMaxHit` (`pkSk->lMaxHit`) caps how many victims a single cast may hit.

## Mapping to the UE port

| Old | Ours |
| --- | --- |
| `m_map_kDamage` | `AMT2Mob::RecordDamage` / `GetDamageRecords()` |
| `DistributeExp` | `MT2Rewards::DistributeExperience` |
| ownership round-robin over >=10% damagers | `MT2Rewards::BuildLootOwners` |
| `battle_is_attackable` | `AMT2CharacterBase::IsHostileTo` |
| `AttackProcess` per-instance loop | `UMT2CombatComponent::FindBasicAttackTargets` (all hits, not nearest) |
| `iSplashRange` / `lMaxHit` | `UMT2SkillDefinition::SplashRange` / `MaxHitCount` |

Old units are treated as centimetres 1:1 in UE, so the 5000 (50 m) exp range check and the
300/1000 attack ranges carry over unchanged.
