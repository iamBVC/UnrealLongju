# Target / select effect and targeting-on-hit

Sources: `40250 client sources/source/UserInterface/InstanceBase.cpp` (`OnSelected`/`OnUnselected`),
`InstanceBaseEffect.cpp` (`__AttachSelectEffect`), `Dumps/my_dump/playersettingmodule.py`.

## The "current target" ring

- Selecting a character calls `CInstanceBase::OnSelected` -> `__AttachSelectEffect()` ->
  `__EffectContainer_AttachEffect(EFFECT_SELECT)`; `OnUnselected` detaches it.
- The effect is registered once in `playersettingmodule.py`:
  ```python
  chrmgr.RegisterCacheEffect(chrmgr.EFFECT_SELECT, "", "d:/ymir work/effect/etc/click/click_select.mse")
  ```
  The empty attach-bone string ("") means it attaches at the **actor origin / feet**, not a bone.
- `click_select.mse` (effect/etc/click/) is a looping `Group Mesh` playing `click_select.mde` with
  `ColorFactor 1.0 0.0549 0.0 1.0` (red-orange) and additive blending
  (`BlendingSrcType 5` = SRCALPHA, `BlendingDestType 6` = INVSRCALPHA). The mesh spins via its baked
  mesh animation (`MeshAnimationLoopEnable 1`). Net effect: a flat red ring rotating at the feet.
- Variants exist per relation: `pc_click_select`, `npc_click_select`, `mob_click_select`,
  `pvp_click_select`, plus `_glow` and `_big` sizes. Base game only wires the generic `click_select`.
- The ring texture is imported in this project as `T_click_select`
  (`/Game/ymir_work/effect/etc/click/T_click_select`).

## Targeting on hit

The old client keeps a separate "selected" (the ring) vs. the combat victim. Our port already
routes a click into `UMT2CombatComponent::SetSelectedTarget`. Auto-selecting the thing you hit when
you have no valid selection is a convenience the client approximates by keeping the last damaged
actor id (`m_dwLastDmgActorVID`); we set the selected target on the first landed hit when the
current one is missing or dead.

## Skill animation lock

Old client `IsUsingSkill()` / `CanUseSkill()` prevents a new skill while one is mid-motion. We gate
`UMT2SkillCastComponent::TryUseSkill` with a `SkillCastLockUntil` timestamp = now + the cast animation's motion duration
(the same `MotionDuration`/`GetPlayLength` the multicast uses), so skills can't be chained instantly.

## Asset references via settings

Centralized configurable gameplay asset references resolve through
`UMT2GameplaySettings` (a `UDeveloperSettings`, category "Metin2", saved to `DefaultGame.ini` under
`[/Script/Metin2.MT2GameplaySettings]`), so they are editable in Project Settings or the ini.

Source review: 2026-10-05. Some runtime/UI paths still contain explicit asset references; this is not a claim that every hard-coded path has been removed. Asset availability requires the matching content submodule revision.
