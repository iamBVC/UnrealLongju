# Skill Books (skill mastery progression)

Source review: 2026-10-05. Original references: `game/src/char_skill.cpp`, `char_item.cpp`, and the grand-master training quest. See [QuestPortingStatus](QuestPortingStatus.md#grand-master-learning-pass) for recorded native API tests.

## Mastery model

`MT2SkillMastery::FromLevel` derives mastery from level: 0–19 Normal, 20–29 Master, 30–39 Grand Master, 40 Perfect. Point-ups have a random 17–20 Master breakthrough. M1 is level 20; G1 is level 30.

The original international Master-book path spends EXP on accepted success/failure reads, rolls for progress, and requires 1–10 successful reads for M1→M2 through M10→G1. Original book cooldown and EXP policies are not identical to every earlier port stage.

## Current Master-book path

`UMT2SkillComponent::CanReadSkillBook` requires a resolvable definition, Master mastery, sufficient EXP, and an expired deadline or the no-cooldown affect. `LearnSkillByBook` is authoritative.

Current settings are `SkillBookExperienceCost`, `SkillBookCooldownSeconds`, and `SkillBookSuccessChance`. C++ defaults are 10,000 EXP, 86,400 seconds, and 0.35; project/config overrides may differ. The former documentation's **5% EXP cost and no cooldown** no longer describe this implementation.

Every accepted read spends the configured flat EXP cost and writes a per-skill Unix deadline. Normal success/progress is probabilistic; the guaranteed-success effect changes that next read. Both no-cooldown and guaranteed-success one-shot effects are consumed on the next accepted read. Read counts/deadlines are captured in player skill persistence.

Right-clicking a book uses `ServerReadSkillBook`, resolves the target skill from imported item data, and checks eligibility before consumption. Grade/result chat comes from the authoritative result.

## Grand-master path

G1–G10/Perfect training is implemented; it is not merely an out-of-scope proposal.

`CanTrainGrandMasterSkill` and the training backend check legacy proto type, ownership/group, mastery, ID limits, and special/exclusive skill rules. Configured denominator and minimum/maximum read tables govern attempts. Quest flags `training_grandmaster_skill.skill<Vnum>` hold cumulative counts; old save tallies have a compatibility migration.

The native `pc.learn_grand_master_skill` binding consumes the legacy book-bonus affect by adjusting the denominator and writes an 8–12-hour deadline. It does not itself enforce that deadline or consume the no-delay affect: the legacy calling quest owns those checks. Do not equate this native API with the Master-book guaranteed-success policy.

The simplified native soulstone menu and the imported training quest still have fidelity/translation gaps. A real training backend does not make the full quest playable; consult the current conversion audit.

## Validation boundary

Recorded regression tests cover focused training/flag persistence behavior. This review did not rerun them, test live client UI, or establish full database/reconnect and cooked multiplayer parity.
