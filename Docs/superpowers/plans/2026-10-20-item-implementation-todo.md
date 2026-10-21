# Item Classes Implementation Plan and TODO Tracker

> **For agentic workers:** Use superpowers:executing-plans to execute this checklist task by task. Record verification evidence before marking a task complete and keep the per-class roster synchronized with the task checklist.

**Goal:** Give every supported item its legacy-correct behavior, place item-specific policies and data in the appropriate native item class, and retain a complete, verifiable checklist for all item classes and their dependencies.

**Architecture:** Native item templates own their specific use, target-application, eligibility and result policies. Inventory owns authoritative validation, atomic storage transactions, replication, persistence integration and instance scheduling; character, skill, fishing, mining, travel and quest systems retain their own system responsibilities. Templates are shared UObject defaults and must not hold mutable player, timer or item-instance state.

**Tech stack:** Unreal Engine C++/reflection, Blueprint item templates, the MT2UEEditor importer, project settings, native automation tests and packaged client/server/offline targets. Use the project's source engine at `F:\Engine2`; verify its Build.version before execution.

**Requirements and references:**

- User requirements: dedicated small .h/.cpp files; class-owned item logic and narrowly owned fields; explicit enum/flag selectors; asset/class references for editable item/mob/skill references; configurable gameplay values; preserve legacy behavior.
- [Current behavior and legacy research](../../ItemBehavior.md).
- [Full 110-pair subtype catalog](../../ItemTemplateVariantCatalog.md).
- [Item authoring constraints and state compatibility](../../ItemAuthoringRefactorPlan.md).
- [Legacy item/inventory research](../../OldGameResearch/ItemsAndInventory.md).
- [Potion and affect rules](../../OldGameResearch/PotionsAndAffects.md), [skill books](../../OldGameResearch/SkillBooks.md), [mounts](../../OldGameResearch/MountSystem.md), [mining](../../OldGameResearch/Mining.md), [fishing](../../Fishing.md).

## Tracking rules and definition of done

- Validation sources are temporary by user request: run and retain results, then remove the verified `Source/Metin2/Tests` sources. Their current backup is `Saved/RemovedTests/20261020-BotSpawnFix/Tests`; reports remain under `Saved/Automation/ItemImplementation`. References below to those test filenames describe validation to reproduce, not files currently retained in the source tree. Other plugin-local tests are outside this removal request.
- Batch 02's six beginner-item tests (including authored-default comparison) are archived separately in `Saved/Automation/ItemImplementation/Batch02-VerifiedTests`; its final result report is `Batch02-Final/index.json` under that automation directory.
- `[ ]` means pending; existing code is not proof that its ownership, legacy parity, assets and integration have been validated.
- `[x]` means the task's acceptance criteria and relevant verification passed, with an entry in the completion log.
- For each class, record one final disposition: **implemented**, **shared family behavior sufficient**, **quest/system-owned behavior**, or **legacy unused/no-op**. Record source evidence for the last three.
- A subtype without ExecuteUse is not automatically a missing feature. Check equip, target application, NPC give, pickup, quest dispatch, generation and expiry paths as applicable.
- Finish the owning task, its affected class checkboxes and the corresponding catalog row together. Keep current-state documentation accurate.
- Every confirmed missing supported behavior receives its own task ID before implementation. Unknown/reserved variants keep their identity and explicit evidence-backed disposition.
- Mark a gameplay task complete only after checking consumption/result behavior, invalid requests, imported Blueprint parents/defaults and relevant save/network boundaries.
- Keep implementation and runtime-test evidence in the completion log; creation of a checklist alone is not verification.

## Source baseline

Baseline recorded 2026-10-20 against commit `fc68744`; counts are source inventory, not runtime coverage.

- [x] BASE-00: Enumerate 162 native template classes: 154 under Items/Templates, seven under Items/Use, and the common UMT2ItemTemplate.
- Current roster: 164 native template classes, including the two beginner bonus variants C-163/C-164 added in Batch 02. Legacy type/subtype coverage remains 110 pairs.
- [x] BASE-00A: Enumerate the 110 legacy type/subtype pairs represented by the existing catalog and its test cases.
- [x] BASE-00B: Count 104 variant headers retaining the generic dedicated-behavior TODO.
- [ ] BASE-01: Establish a fresh build/test/asset baseline and reconcile the stale catalog expectations.
- [ ] BASE-02: Complete every class disposition in the roster below.

Source findings that must guide the work:
- Normal/rare bonus templates now own ApplyToEquipment. Their surrounding transaction still needs regression verification.
- ExecuteUse implementations exist for polymorph, blend, campfire, fish, skill-forget, bait, create-stone, recipe and the six action templates under Items/Use.
- Several classes delegate substantial item-specific semantics to Inventory or PlayerCharacter; those need ownership review rather than duplicate implementations.
- Current research explicitly states AUTOUSE, MATERIAL, SPECIAL, TOOL and LOTTERY have no generic right-click branch in legacy CHARACTER::UseItemEx. Preserve supported content/system/quest behavior and verified no-ops.
- Dead fish cannot be opened, but live/dead fish can be roasted through AMT2Campfire::Roast. Lack of a dead-fish ExecuteUse override is not a missing eating/opening feature.
- The catalog test asserts the actual immediate parents, including the bonus-consumable and action-template intermediate families. The relevant imported bonus assets resolve to their dedicated behavior classes.
- The catalog is a coverage tracker, not proof that every row labeled placeholder lacks a runtime consumer. Update each row after its class audit. Polymorph handlers already exist.
- Source engine 5.7.4 is available at F:\Engine2. Engine builds, automation and Graphify require execution outside the restricted subprocess sandbox on this host; access failures there do not imply a missing toolchain.
- Unreal Graphify refreshed after Batch 01 (10,450 nodes / 17,404 edges); queries locate the new chance struct and updated policies. Macro/third-party syntax warnings limit extraction for 61 files; inspected source and passing builds remain authoritative.

## Global constraints

- Preserve reflected class identities, authored Blueprint overrides, saved items and supported legacy quest socket semantics.
- Each concrete template keeps a dedicated matching header/implementation; narrowly shared policies may live in a genuine family base or focused helper.
- Exposed references use TSubclassOf/TSoftClassPtr or appropriate object/soft-object pointers. Quantities keep numeric types with names, units and bounds; visible type/flag choices use enums/enum bitmasks.
- Item-specific mutable gameplay values are authored on that item class; project-wide tuning and default policies go in appropriate project settings.
- Keep numeric IDs only where needed for source decoding, stable identity, persistence/protocols, range offsets or Lua compatibility. Import-time decoding into typed links is distinct from an editable runtime reference.
- Preserve quest interception/order unless the inspected legacy contract explicitly requires another order. Prevent quest and native handlers from applying an action twice.
- Server authority owns outcomes and random rolls. Client requests must not select authoritative results or bypass eligibility.
- All gameplay UObject access occurs on the game thread. Templates/CDOs stay immutable during player actions.
- Preserve server performance: use existing event/timer scheduling rather than adding per-item per-frame polling, and avoid repeated synchronous asset loads in gameplay hot paths.
- Missing definitions or invalid settings must reject safely with useful diagnostics and preserve recoverable instance data.
- No new UI is assumed for code-only tasks. A required UI/interaction change becomes an explicit dependency with its own acceptance criteria.
- No new generic architecture is introduced without an actual consumer; use existing ExecuteUse/ApplyToEquipment and transaction patterns where suitable.
- Do not replace functioning shared equipment, quest, fishing, mining or skill systems with copies inside item classes. Move item-specific policies; delegate system work to its existing owner.

## Review focus

1. Inventory or currency delegates reenter gameplay: stage and validate before publishing, retain no invalidated container references across callbacks, and consume/grant once. Owners: BASE-03 and each OWN task.
2. Reparented or narrowed Blueprint properties lose authored values: validate representative and full imported defaults, keep migration/redirect support where required. Owners: BASE-02, OWN-02/03/07 and REL-01.
3. Item transfer, offline load or late-join resets state: preserve skill choices, blend rolls, bindings, recall targets, socket wear and reservoirs. Owners: BASE-03, SYS tasks and REL-01.
4. A no-op or quest-owned legacy entry becomes an invented consumable: trace complete legacy dispatch and actual content, including aliases and unknown ordinals. Owners: PAR-01/02, EQP-02.
5. Timers or deferred actions survive death, trade, item replacement or map teardown incorrectly: validate exact item identity and cancel/settle according to the legacy contract. Owners: OWN-04, SYS-01/02/03 and REL-01.

## File ownership map

Paths are relative to the repository root unless otherwise stated. Existing files are navigation targets, not permission to rewrite unrelated code.

| Area | Existing files / responsibility |
| --- | --- |
| Common template/use contract | Source/Metin2/Items/MT2ItemTemplate.h/.cpp; Items/Use/MT2ItemActionTemplate.h/.cpp |
| Specific template classes | Source/Metin2/Items/Templates/MT2Item<Name>Template.h/.cpp; exact classes appear in the roster |
| Bespoke action classes | Source/Metin2/Items/Use/MT2<Name>ItemTemplate.h/.cpp |
| Authoritative inventory/dispatch | Source/Metin2/Items/MT2InventoryComponent.h/.cpp |
| Dedicated bonus mutations | Items/Templates/MT2ItemBonusConsumableTemplate and its five concrete children; Items/MT2InventoryBonuses.cpp; Items/MT2ItemBonusUtils.cpp |
| Metin sockets | Items/MT2InventorySockets.cpp; Templates/MT2ItemMetinStoneTemplate, MT2ItemMetinRemovalTemplate, MT2ItemSocketCleanerTemplate |
| Accessory socket rules/state | Items/MT2InventoryAccessorySockets.cpp; Items/MT2AccessorySocketRules.h/.cpp |
| Existing item transactions | Items/MT2InventoryRecipe.cpp, MT2InventoryRefinement.cpp, MT2InventoryFishing.cpp, MT2InventoryMining.cpp, MT2InventoryAutoRecovery.cpp |
| Instance state/legacy boundary | Source/Metin2/Items/State; Items/Legacy; Items/MT2ItemUtils.h/.cpp |
| Character action entry points | Source/Metin2/Characters/MT2PlayerCharacter.h/.cpp |
| Import class selection | Source/Metin2/Items/MT2ItemTemplateResolver.h/.cpp; Plugins/MT2UE/Source/MT2UEEditor/Private/Importers/MT2ItemImporter.cpp |
| Import reference resolution | Plugins/MT2UE/Source/MT2UEEditor/Private/Importers/MT2ItemReferenceResolver.cpp |
| Shared tuning/references | Source/Metin2/Config/MT2GameplaySettings.h/.cpp; relevant existing family settings |
| Regression validation | Use temporary targeted tests, with archived fixtures in Saved/RemovedTests/20261020-BotSpawnFix/Tests and result reports under Saved/Automation/ItemImplementation. Restore/recreate the relevant fixtures for future validation; do not treat removed sources as currently runnable. |
| UI request/presentation integration | Source/Metin2/UI/MT2InventoryWidget.cpp, MT2ItemDropDialogWidget.cpp, MT2ItemTooltipWidget.cpp and MT2ItemTooltipWidgets.cpp |

The abbreviated Items/ and Templates/ paths above are under Source/Metin2. New system files must be named and their interfaces recorded in the relevant SYS task's design deliverable before implementation.

## Phase 0 - Baseline, evidence and common contracts

### BASE-01 - Reproducible baseline and catalog assertions

**Dependencies:** none. **Files:** resolver, catalog tests, importer, existing item test files.

- [x] Read the relevant Graphify reports; query each graph with its explicit path before source research. For legacy comparison consult the separate Unreal/server/client graphs required by AGENTS.md. Record unavailable/stale graphs.
- [x] Build the current Editor target and run Metin2.Items before gameplay changes; retain complete reports under Saved/Automation.
- [x] Reproduce the direct-parent assertion mismatch for the three normal bonus classes. Define whether the intended invariant is exact immediate parent or correct family ancestry, then update expected parents/assertions accordingly; retain rejection of unrelated families.
- [ ] Verify real imported parents, including configured rare bonus overrides and special USE_SPECIAL routing. Record any content mapped to the wrong behavior class.
- [ ] Capture actual class counts, supported proto variants and representative asset IDs in the completion log. Resolve baseline failures before attributing later failures to new changes.

**Done when:** baseline build/test results and any independent failures are recorded; catalog assertions describe the intended hierarchy.

### BASE-02 - Complete legacy disposition and authoring ownership audit

**Dependencies:** BASE-01. **Files:** every roster source pair, ItemTemplateVariantCatalog.md, ItemBehavior.md, importer.

- [ ] For each class, trace its inherited implementation and actual consumer: right-click, drag-to-apply, equip, NPC give, pickup, generation, quest or timed effect.
- [ ] Trace the corresponding legacy server branch, client interaction and content tables/scripts. Record source locations and gates; do not infer functionality from enum labels.
- [ ] Record consumed inputs, rewards, failure behavior, expiry, conflicts and settings/reference requirements for every supported action.
- [ ] Audit all exposed properties: move narrow fields to their owning subtype/family, use explicit enum/flag choices and typed references, and list required migrations before edits.
- [ ] Preserve quest/system-owned and unused variants with explicit comments and an evidence-backed catalog disposition. Remove generic TODO comments only once their actual work/disposition is settled.
- [ ] Update the roster and catalog together; create a bounded implementation task for any additional supported behavior discovered.

**Done when:** every roster class has a verified disposition and there are no unidentified generic TODOs.

### BASE-03 - Transaction, authority and instance-safety contract

**Dependencies:** BASE-01. **Files:** common template/action interfaces, Inventory, focused item transactions, State/Legacy adapters, applicable tests.

**Existing interfaces:** ExecuteUse(AMT2PlayerCharacter&, UMT2InventoryComponent&, int32) const -> EMT2ItemUseExecution; bonus templates use ApplyToEquipment(FMT2ItemSlot&, const UMT2ItemTemplate&, const UMT2ItemBonusSettings&) const -> EMT2ItemBonusApplyResult.

- [ ] Pin rejection/no-consumption, accepted deferred action, success/failure consumption and malformed-item destruction behavior with meaningful tests for actual item paths.
- [ ] Test invalid authority, stale/mismatched source and target snapshots, duplicate requests, full inventory, missing assets and trading/death restrictions.
- [ ] Verify staging/commit order and reentrancy around inventory, equipment, currency and quest delegates. Keep item rules in templates and generic commit/storage work in Inventory.
- [ ] Verify stack identity and load/transfer round trips do not reroll existing generated state or discard unknown records.
- [ ] Document any necessary additional target-application contract with exact signature and first consumer before introducing it.

**Done when:** item-specific refactors can reuse a tested transaction boundary without duplicating storage/authority logic.

## Phase 1 - Move and verify existing behavior

Each OWN task includes: inspect the named legacy path, preserve current authored values through migration when properties move, add targeted behavior regression coverage, update actual Blueprint parents/defaults if necessary, run affected tests, and close affected roster entries. Existing wrapper methods alone do not establish complete class ownership.

- [ ] **OWN-01 - Normal and rare bonus consumables.** Verify AddAttribute, AddAttribute2, ChangeAttribute, AddRareBonus and ChangeRareBonuses each own their selection/chance/compatibility policy. Keep InventoryBonuses staging, commit, consumption and notifications generic. Check maximum slots, existing count preservation, duplicate restrictions, addon pairs, settings-selected items and deterministic success/failure/no-consumption cases. Cover the actual imported five assets, not just native CDOs.
  - [x] **OWN-01A / Batch 01:** Verify standard addition 71085 and fifth-attribute addition 70024 use their dedicated native classes. Put per-slot chances in project settings (100/80/60/50/30), exclude prototype applies from normal selection, stage before mutation, consume valid failed attempts and reject missing definitions/weights, death and trading. Verify native policies, imported class routing and real imported 70024 inventory transactions. Evidence is in the completion log.
  - [x] **OWN-01B / Batch 01:** Preserve repeated entries in configured normal/rare value-level arrays. Verify rare level-five configuration with a regression test. Correct ordered polymorph-power config serialization without changing its authored values; its existing combat-scaling test must pass.
  - [ ] **OWN-01C / Next batch:** Trace normal reroll 71084 and special addition/reroll variants, including 71152/76024 low-level weapon/body restrictions, legacy cooldowns and special distributions. Test actual imported assets and move only item-specific policies into their owning templates.
    - [x] **OWN-01C1 / Batch 02:** Dedicated beginner addition/reroll subclasses for 71152/76024 and 71151/76023; weapon/body-only and every target required-level limit within the configurable default of 40. Ordinary bonus policies reused; importer routing and all four assets refreshed. Rejection preservation, level boundaries/configuration and inventory consumption verified. Shared cooldown/distribution/addon work remains under OWN-01C/D.
  - [ ] **OWN-01D:** Verify zero-valued attribute slot accounting and damage-addon pairs against legacy; current valid-value counting is not proof of legacy slot parity.
  - [ ] **OWN-01E:** Finish rare bonus 71051/71052 regional eligibility and uniform eligible-type selection, plus actual settings-selected-item transactions. Do not infer a weighted rare selection policy from normal attributes.
  - [ ] **OWN-01F / BASE-03:** Validate stale source/target confirmation snapshots, duplicate requests, equipped-target transactions, persistence and real multiplayer/offline behavior. No RPC or saved-item format changes are part of Batch 01.
- [ ] **OWN-02 - Recovery potion variants.** Move Potion, PotionNoDelay and PotionContinue item policies from Inventory::UseItem into their specific templates, with only genuinely shared recovery fields/helpers in an appropriate family. Preserve queued versus instant versus fixed-per-second recovery, cap/refresh behavior and legacy consumption at full resources. Move percent-only fields out of unrelated use classes; test HP-only, MP-only, combined, full-resource and duplicate cases.
- [ ] **OWN-03 - Ability-up, affect and cleanse.** Put subtype policies/data in UseAbilityUp, UseAffect and UseClear templates; retain status-effect execution in the existing effect system. Preserve all supported apply types, effect identity, refresh/duplicate rules, durations, death behavior and the exact set of cleansed effects. Move AffectType and other narrow fields with asset preservation; verify migration defaults.
- [ ] **OWN-04 - Automatic recovery.** Give AutoRecoveryTemplate activation/resource/recovery policy while Inventory retains mutable reservoir state and scheduling. Remove subtype routing for that action from UseItem. Make shared recovery rate/interval tuning configurable where appropriate. Test stacked activation, reservoir exhaustion, switching reservoirs, death/logout/trade/map teardown and reload without granting extra capacity.
- [ ] **OWN-05 - Metin insertion, removal and cleaning.** Move stone compatibility/result and detachment/cleaning policies to their dedicated classes, including the single/all removal modes. Keep inventory/world-delivery commits generic. Replace runtime broken-stone literal references with the existing typed setting. Test duplicates, socket grades, broken stones, no valid target, returned-stone capacity/overflow and source/target replacement.
- [ ] **OWN-06 - Accessory/belt material insertion and wear.** Give socket-opening and inserting items their own compatibility/chance/result policies. Replace configurable runtime mineral references and tunable durations/chances with typed authored links/settings; preserve required legacy arithmetic only at import/compatibility boundaries. Keep per-instance wear timing on its existing owner. Test all grades, maximum capacity, expired wear, equip/unequip/load, simultaneous source/target changes and failure consumption. Ring subtype work is PAR-01/SYS-03 as established by legacy evidence.
- [ ] **OWN-07 - Hair and costume actions.** Move hair-look selection/consumption policy from Inventory into the owning item class, with explicit authored hair selection and no runtime VNUM-band fallback for new typed assets. Preserve race/sex compatibility, costume versus consumable-hair behavior, consumption, appearance persistence and observer updates. Keep mesh application in the appearance system. Test creation/load/equip and wrong-character rejection.
- [ ] **OWN-08 - Skill books and training items.** Move normal book item policy from ServerReadSkillBook to SkillBookTemplate; keep skill progression in SkillComponent and RPC/UI routing in Character. Review GrandMasterTraining and SkillForget for remaining item-specific orchestration elsewhere. Verify selected-skill persistence, eligibility, cooldown/success modifiers, refunds, asynchronous identity checks and legacy consume-on-attempt behavior.
- [ ] **OWN-09 - Fishing, mining, recipes, campfires and stone creation.** Review Fish/Alive/Dead, Rod, Pick, Bait, Campfire, Recipe and CreateStone templates. Move item-specific policies out of InventoryRecipe/Fishing/Mining where applicable; keep fishing/mining/NPC workflows and atomic transactions in their system owners. Verify live fish opening and both fish types' campfire stack roasting, rod/pick proficiency/refinement, reusable recipes, ingredient/bottle failure consumption and typed stone results. Test actual partial imports, full bags, invalid result definitions and restored/generated instances.
- [ ] **OWN-10 - Boxes, keys, reward delivery and refinement scrolls.** Audit LootCrate, GiftBox, TreasureBox, TreasureKey, Container and RefinementScroll. Move item-specific eligibility/key/chance/failure policies into the correct template/family while retaining the shared loot/refinement systems. Verify rewards, Yang/EXP, full-bag overflow ownership, absent/wrong keys, minimum level, failed refinement and downgrade/destruction semantics. Do not assume USE_TREASURE_BOX is covered by ITEM_TREASURE_BOX.
- [ ] **OWN-11 - Mount, horse, lure and karma actions.** Review all six concrete Items/Use actions and the abstract ActionTemplate. Preserve system calls but move remaining item decisions into their classes, including deferred training/horse dialogs. Verify horse grade/feed/revive/summon, mount eligibility/expiry, lure filtering/radius/cooldown and karma recovery under server authority. Confirm typed item/mob/skill settings and import parents for each actual content variant.
- [ ] **OWN-12 - Polymorph integration.** Verify the existing PolymorphTemplate owns ball/book item rules and uses typed settings/references. Test ball/book differences, failed use, consumption, duration, skill/weapon/riding restrictions, transformation/restoration, mob-specific height/attack/audio, death, offline load and remote/late-joining observers. Update the stale documentation coverage statement only with verified evidence.

## Phase 2 - Resolve currently unhandled native USE variants

### PAR-01 - Individual subtype contracts and implementations

**Dependencies:** BASE-02/03 and the relevant OWN task. **Files:** each named existing template .h/.cpp, resolver/importer, relevant system, tests and only UI files proved necessary.

Each checkbox is a separate deliverable: locate the exact legacy branch and consumers; record its input fields/gates/outcomes; implement supported behavior in that class after prerequisites; or close it as a verified no-op/quest/system entry. Record UI dependencies explicitly. Do not invent behavior because the current native class has no override.

- [ ] **USE-01 - UMT2ItemUseChangeAttribute2Template.** Compare with ChangeAttribute and the configured rare reroller; establish the real legacy target/attribute set, roll distribution, count preservation and consumption. Reuse the existing bonus transaction/UI only when the contract matches.
- [ ] **USE-02 - UMT2ItemUseTalismanTemplate.** Determine recall/save/travel semantics and required target state; depends on SYS-01 when applicable.
- [ ] **USE-03 - UMT2ItemUseMoveTemplate.** Establish whether this is an active legacy action, quest item or unused declaration; implement only its proven travel/system policy.
- [ ] **USE-04 - UMT2ItemUseTuningTemplate.** Trace actual tuning dispatch and item variants; use the existing refinement system only where legacy semantics agree.
- [ ] **USE-05 - UMT2ItemUseTreasureBoxTemplate.** Trace this USE subtype independently of ITEM_TREASURE_BOX; verify legacy key/reward/consume behavior before wiring the crate system.
- [ ] **USE-06 - UMT2ItemUseMoneybagTemplate.** Verify native/quest currency behavior and caps/consumption. Do not add arbitrary right-click currency conversion.
- [ ] **USE-07 - UMT2ItemUseInvisibilityTemplate.** Establish whether the reference branch is active. Where active, use the existing affect/visibility rules, duration, hostile-action break conditions and observer replication.
- [ ] **USE-08 - UMT2ItemUseBucketTemplate.** Trace content and interaction targets; verify whether quest/NPC dispatch already supplies its behavior.
- [ ] **USE-09 - UMT2ItemUseBindTemplate.** Implement only after SYS-02 binding state/enforcement; verify valid targets, consume rules and persistence.
- [ ] **USE-10 - UMT2ItemUseUnbindTemplate.** Implement only after SYS-02 timed unbinding; verify completion/cancel restrictions and item replacement handling.
- [ ] **USE-11 - UMT2ItemUseTimeChargePercentTemplate.** Verify eligible timed-item families, source charge data, rounding/caps and consumption; implement after SYS-03 or the actual legacy prerequisite.
- [ ] **USE-12 - UMT2ItemUseTimeChargeFixedTemplate.** Verify fixed-unit charging, caps and target restrictions separately from percentage charging.
- [ ] **USE-13 - UMT2ItemUsePutIntoRingSocketTemplate.** Current accessory helpers do not recognize this subtype. Determine whether the reference supports it; implement the correct ring state/application path only if active, with no reinterpretation as belt/accessory insertion.

**Done when:** all 13 entries have completed, tested native behavior or an evidence-backed supported-system/quest/no-op disposition.

### PAR-02 - AUTOUSE, LOTTERY and other declaration-only/content families

**Dependencies:** BASE-02. **Files:** AutoUse/Lottery templates, relevant content/quest consumers, importer and catalog.

- [ ] Trace and close AutoUsePotion, AutoUseAbilityUp, AutoUseBomb, AutoUseGold, AutoUseMoneybag and AutoUseTreasureBox individually. The reference has no generic right-click action for AUTOUSE; add an automatic-use mechanism only if an actual legacy consumer/content contract requires it.
- [ ] Trace and close LotteryTicket and LotteryInstant individually, including quest/content dependencies and any external lottery service. Do not create a lottery system solely from enum names.
- [ ] Trace SpecialMap/Key/Document/Spirit, ToolFishingRod and passive Material/Resource variants through their actual quest/system consumers.
- [ ] Preserve unused/reserved family identifiers; replace generic TODO comments with precise dispositions and sources.
- [ ] Add regression coverage for real active consumers and unsupported/no-op rejection where there is a meaningful failure mode.

## Phase 3 - Implement prerequisite systems before dependent item actions

### SYS-01 - Recall/talisman destination and travel

**Dependencies:** verified USE-02/03 contracts, BASE-03. **Existing integration:** item state/legacy codecs, map/travel system, quests and owning templates.

- [ ] Write the exact legacy contract: destination creation/overwrite, coordinates/map identity, restrictions, delay/cancellation triggers, source consume moment, local/offline and map-server travel failure outcomes.
- [ ] Record the exact new state type, owning files and public interfaces in this task before coding. Expose map/data-asset references where authored; retain source identifiers at save/protocol boundaries.
- [ ] Implement destination persistence and delayed/cancellable travel through existing transfer validation; keep player-specific pending actions off CDOs.
- [ ] Integrate the proven item variants and any required prompts with explicit UI acceptance criteria.
- [ ] Test invalid destinations, death/stun/combat cancellation where required, disconnect/reconnect, same-map and inter-map use, denied transfer and duplicate completion without double consumption.

### SYS-02 - Binding and delayed unbinding

**Dependencies:** verified USE-09/10 contracts, BASE-03.

- [ ] Specify legacy bind/unbind state, who owns it, timestamps and restrictions; record exact new state/codec/interfaces and source evidence before coding.
- [ ] Implement durable per-instance binding and delayed unbinding, keeping policies in item classes and enforcement in shared operations.
- [ ] Enforce the verified restrictions across trade, drop, sell, refinement, storage/equipment and transfers; reject prohibited requests server-side.
- [ ] Integrate existing item-on-target UI where adequate; list and implement any necessary state labels/prompts as a separate child task.
- [ ] Test load during pending unbinding, clock/timer completion, item moves/replacement, duplication attempts and offline versus online operation.

### SYS-03 - Dragon Soul, extraction, charging and refinement

**Dependencies:** BASE-02/03; separate system design based on legacy server/client/table research.

- [ ] **DS-01:** Document exact qualified-character state, inventory layout, two equipment decks, valid slot/type rules, ownership/replication/save interfaces, table schema and UI flow. Record exact new files/interfaces before implementation.
- [ ] **DS-02:** Implement inventory/deck transactions, qualification, activation/deactivation and persistent item state; validate legacy default/missing-data behavior.
- [ ] **DS-03:** Implement generated attributes, wear/expiry timers and combat/stat integration; preserve generated state on restore/transfer.
- [ ] **DS-04:** Implement table-driven refinement with normal/blessed/holy material items and atomic input/currency/result handling.
- [ ] **DS-05:** Implement ExtractDragonSoul and ExtractDragonHeart with their actual legacy success/failure/delivery policies and required UI.
- [ ] **DS-06:** Implement supported percentage/fixed charging items using the qualified timed-item contract; test caps, rounding, invalid/expired targets and consumption.
- [ ] **DS-07:** Complete explicit UI tasks for inventory/decks, refinement/extraction and tooltips. Mark the dependent items blocked by these tasks until the player can perform them.
- [ ] **DS-08:** Verify all six normal and six special Dragon Soul slot classes and all three refining-material classes against imported assets and actual gameplay.
- [ ] **DS-09:** Test deck swaps, logout/reload, late join, death, expiry while offline where required, trades/transfers and interrupted transactions.

## Phase 4 - Shared equipment, passive variants and import/configuration closure

### EQP-01 - Equipment-family parity

**Dependencies:** BASE-02; relevant OWN/SYS tasks.

- [ ] Verify all eight weapon and seven armor variants: wear positions, race/sex/class/level/anti-flags, attack/stat policies, visual subtype and socket restrictions.
- [ ] Verify costume body/hair, unique equipment, rings, belts and totems: supported slots, applies, expiry, deck/exclusive rules and actual client presentation.
- [ ] Determine whether belt inventory or related restrictions need implementation; if so, add explicit state/UI/transaction child tasks before closing BeltTemplate.
- [ ] Verify shared family behavior where sufficient; do not introduce redundant leaf overrides. Put subtype-specific fields in the narrowest correct class.
- [ ] Test representative imported items from every family, unsupported wear requests and save/equip/observer behavior.

### EQP-02 - Unknown variants and passive item consumers

**Dependencies:** BASE-02, PAR-02.

- [ ] Verify UniqueNone, UniqueBook, UniqueSpecialRide, UniqueSpecialMountRide, UniqueLegacySubtype4..9 and UniqueSpecialUse. Trace importer overrides to actual horse/mount/book classes; preserve unknown aliases without inventing semantics.
- [ ] Verify Material/Resource ingredients, raw/refined ore links, gold/secondary coin and quest items through their actual acquisition, crafting, shop and quest consumers.
- [ ] Resolve every family-only template and subtype's generic TODO using evidence; passive/no-op closures require the concrete legacy source/content reference.
- [ ] Verify parser sentinels, unsupported ordinals, partial imports and missing definitions remain explicit failures or preserved opaque records as appropriate.

### CFG-01 - Typed authoring and configurable policy closure

**Dependencies:** BASE-02 and relevant OWN tasks. **Files:** template headers, gameplay/family settings, importer/reference resolver, authoring tests.

- [ ] Audit runtime literal VNUM checks, especially broken Metin selection, accessory-mineral mapping and hair-look identification. Move runtime referenced content into typed template links/settings with clear scope.
- [ ] Audit importer special-class routing for horse books/care, mount records, lure, automatic recovery, refinement scroll, training and karma items. Keep unavoidable legacy decoding explicit in the importer; configurable classification uses named typed rules/settings and produces typed runtime links.
- [ ] Verify remaining USE_SPECIAL records are genuinely recovery items before importing them as AutoRecoveryTemplate; retain explicit handlers/dispositions for other special variants.
- [ ] Move gameplay tuning that authors need to change into relevant project settings or specific item defaults. Keep protocol ordinals, fixed layout sizes and ABI constants in code.
- [ ] Validate class pickers, enum/flag choices, categories, units/bounds, missing references and circular soft-link behavior in actual Blueprint defaults.
- [ ] Verify clean import, reimport, partial import and cook preserve the intended native parent and do not erase authored overrides.

### DOC-01 - Current-state documentation and tracking

**Dependencies:** task results as they complete.

- [ ] Update ItemTemplateVariantCatalog.md per completed class; distinguish implementation, inheritance, quest/system ownership and verified legacy no-op.
- [ ] Update ItemBehavior.md and affected family research documents from the current implementation. Remove obsolete missing-feature and blanket placeholder claims.
- [ ] Keep this plan's checkboxes and evidence log current in each implementation change.
- [ ] Update/inspect the Unreal source graph after substantive source changes where tooling is available; record access/freshness limitations.

## Phase 5 - Integration and release verification

### REL-01 - Complete acceptance pass

**Dependencies:** relevant implementation phases, all roster dispositions complete.

- [ ] Build affected Editor/runtime/server targets; build the shipping/offline targets when configuration or packaging behavior changed.
- [ ] Run focused automation after each task; run the broader Metin2 suite after shared state/serialization or final integration changes. Record actual counts/results, not historic reports.
- [ ] Validate imported Blueprint parent classes and preservation of authored properties; compile affected assets and perform the appropriate commandlet/cook checks.
- [ ] Test representative old/current saves, inventory transfers, item stacking, generated-state preservation and legacy Lua socket adapters.
- [ ] Smoke-test packaged dedicated server + client, remote observer/late join and standalone offline. Test actual item use, target application, equips, NPC-give paths and failure messages.
- [ ] Review the complete diff for unrelated changes, unsupported invented behavior, hidden errors and misplaced item logic.
- [ ] Close all child tasks and roster entries, update current-state documents and add final evidence to the log.

## Verification commands

These are execution templates, not commands already run. Use the actual source engine/project paths and inspect logs/reports; process exit alone is insufficient proof of completed automation.

```powershell
& 'F:\Engine2\Engine\Build\BatchFiles\Build.bat' UnrealLongjuEditor Win64 Development '-Project=F:\UnrealLongju\UnrealLongju.uproject' -WaitMutex
& 'F:\Engine2\Engine\Build\BatchFiles\Build.bat' UnrealLongjuServer Win64 Development '-Project=F:\UnrealLongju\UnrealLongju.uproject' -WaitMutex
& 'F:\Engine2\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' 'F:\UnrealLongju\UnrealLongju.uproject' -unattended -NullRHI -nosplash '-ExecCmds=Automation RunTests Metin2.Items' '-TestExit=Automation Test Queue Empty' '-ReportExportPath=F:\UnrealLongju\Saved\Automation\ItemImplementation'
```

The catalog test name is `Metin2.Items.Authoring.VariantCatalog`. Discover exact other test names from their IMPLEMENT_*AUTOMATION_TEST declarations; do not invent commandlet names or test identifiers. After shared state changes, use the existing broader `Metin2` automation prefix where appropriate.

## Per-class completion roster

All entries start pending final verification, including those with existing code. The source status describes only what was inspected at baseline. There are 162 unique entries; the 110 legacy subtype identities are included below and remain mapped in the catalog.

For each header link the matching .cpp has the same path/basename. A completed entry requires: owning task complete, appropriate inherited/native/quest/no-op disposition, exact legacy evidence, correct authoring fields and importer parent, and applicable integration verification recorded in the log.

### Legacy subtype classes (110)

- [ ] **C-001** [UMT2ItemArmorBodyTemplate](../../../Source/Metin2/Items/Templates/MT2ItemArmorBodyTemplate.h) — Armor/ARMOR_BODY (0). variant shell; inherited/system/quest/no-op disposition pending. Owner: EQP-01 / EQP-02 / BASE-02.
- [ ] **C-002** [UMT2ItemArmorEarTemplate](../../../Source/Metin2/Items/Templates/MT2ItemArmorEarTemplate.h) — Armor/ARMOR_EAR (6). variant shell; inherited/system/quest/no-op disposition pending. Owner: EQP-01 / EQP-02 / BASE-02.
- [ ] **C-003** [UMT2ItemArmorFootwearTemplate](../../../Source/Metin2/Items/Templates/MT2ItemArmorFootwearTemplate.h) — Armor/ARMOR_FOOTS (4). variant shell; inherited/system/quest/no-op disposition pending. Owner: EQP-01 / EQP-02 / BASE-02.
- [ ] **C-004** [UMT2ItemArmorHeadTemplate](../../../Source/Metin2/Items/Templates/MT2ItemArmorHeadTemplate.h) — Armor/ARMOR_HEAD (1). variant shell; inherited/system/quest/no-op disposition pending. Owner: EQP-01 / EQP-02 / BASE-02.
- [ ] **C-005** [UMT2ItemArmorNeckTemplate](../../../Source/Metin2/Items/Templates/MT2ItemArmorNeckTemplate.h) — Armor/ARMOR_NECK (5). variant shell; inherited/system/quest/no-op disposition pending. Owner: EQP-01 / EQP-02 / BASE-02.
- [ ] **C-006** [UMT2ItemArmorShieldTemplate](../../../Source/Metin2/Items/Templates/MT2ItemArmorShieldTemplate.h) — Armor/ARMOR_SHIELD (2). variant shell; inherited/system/quest/no-op disposition pending. Owner: EQP-01 / EQP-02 / BASE-02.
- [ ] **C-007** [UMT2ItemArmorWristTemplate](../../../Source/Metin2/Items/Templates/MT2ItemArmorWristTemplate.h) — Armor/ARMOR_WRIST (3). variant shell; inherited/system/quest/no-op disposition pending. Owner: EQP-01 / EQP-02 / BASE-02.
- [ ] **C-008** [UMT2ItemAutoUseAbilityUpTemplate](../../../Source/Metin2/Items/Templates/MT2ItemAutoUseAbilityUpTemplate.h) — AutoUse/AUTOUSE_ABILITY_UP (1). variant shell; inherited/system/quest/no-op disposition pending. Owner: PAR-02.
- [ ] **C-009** [UMT2ItemAutoUseBombTemplate](../../../Source/Metin2/Items/Templates/MT2ItemAutoUseBombTemplate.h) — AutoUse/AUTOUSE_BOMB (2). variant shell; inherited/system/quest/no-op disposition pending. Owner: PAR-02.
- [ ] **C-010** [UMT2ItemAutoUseGoldTemplate](../../../Source/Metin2/Items/Templates/MT2ItemAutoUseGoldTemplate.h) — AutoUse/AUTOUSE_GOLD (3). variant shell; inherited/system/quest/no-op disposition pending. Owner: PAR-02.
- [ ] **C-011** [UMT2ItemAutoUseMoneybagTemplate](../../../Source/Metin2/Items/Templates/MT2ItemAutoUseMoneybagTemplate.h) — AutoUse/AUTOUSE_MONEYBAG (4). variant shell; inherited/system/quest/no-op disposition pending. Owner: PAR-02.
- [ ] **C-012** [UMT2ItemAutoUsePotionTemplate](../../../Source/Metin2/Items/Templates/MT2ItemAutoUsePotionTemplate.h) — AutoUse/AUTOUSE_POTION (0). variant shell; inherited/system/quest/no-op disposition pending. Owner: PAR-02.
- [ ] **C-013** [UMT2ItemAutoUseTreasureBoxTemplate](../../../Source/Metin2/Items/Templates/MT2ItemAutoUseTreasureBoxTemplate.h) — AutoUse/AUTOUSE_TREASURE_BOX (5). variant shell; inherited/system/quest/no-op disposition pending. Owner: PAR-02.
- [ ] **C-014** [UMT2ItemCostumeBodyTemplate](../../../Source/Metin2/Items/Templates/MT2ItemCostumeBodyTemplate.h) — Costume/COSTUME_BODY (0). variant shell; inherited/system/quest/no-op disposition pending. Owner: OWN-07 / EQP-01.
- [ ] **C-015** [UMT2ItemCostumeHairTemplate](../../../Source/Metin2/Items/Templates/MT2ItemCostumeHairTemplate.h) — Costume/COSTUME_HAIR (1). variant shell; inherited/system/quest/no-op disposition pending. Owner: OWN-07 / EQP-01.
- [ ] **C-016** [UMT2ItemDragonSoulSlot1Template](../../../Source/Metin2/Items/Templates/MT2ItemDragonSoulSlot1Template.h) — DragonSoul/DS_SLOT1 (0). variant shell; inherited/system/quest/no-op disposition pending. Owner: SYS-03.
- [ ] **C-017** [UMT2ItemDragonSoulSlot2Template](../../../Source/Metin2/Items/Templates/MT2ItemDragonSoulSlot2Template.h) — DragonSoul/DS_SLOT2 (1). variant shell; inherited/system/quest/no-op disposition pending. Owner: SYS-03.
- [ ] **C-018** [UMT2ItemDragonSoulSlot3Template](../../../Source/Metin2/Items/Templates/MT2ItemDragonSoulSlot3Template.h) — DragonSoul/DS_SLOT3 (2). variant shell; inherited/system/quest/no-op disposition pending. Owner: SYS-03.
- [ ] **C-019** [UMT2ItemDragonSoulSlot4Template](../../../Source/Metin2/Items/Templates/MT2ItemDragonSoulSlot4Template.h) — DragonSoul/DS_SLOT4 (3). variant shell; inherited/system/quest/no-op disposition pending. Owner: SYS-03.
- [ ] **C-020** [UMT2ItemDragonSoulSlot5Template](../../../Source/Metin2/Items/Templates/MT2ItemDragonSoulSlot5Template.h) — DragonSoul/DS_SLOT5 (4). variant shell; inherited/system/quest/no-op disposition pending. Owner: SYS-03.
- [ ] **C-021** [UMT2ItemDragonSoulSlot6Template](../../../Source/Metin2/Items/Templates/MT2ItemDragonSoulSlot6Template.h) — DragonSoul/DS_SLOT6 (5). variant shell; inherited/system/quest/no-op disposition pending. Owner: SYS-03.
- [ ] **C-022** [UMT2ItemExtractDragonHeartTemplate](../../../Source/Metin2/Items/Templates/MT2ItemExtractDragonHeartTemplate.h) — Extract/EXTRACT_DRAGON_HEART (1). variant shell; inherited/system/quest/no-op disposition pending. Owner: SYS-03.
- [ ] **C-023** [UMT2ItemExtractDragonSoulTemplate](../../../Source/Metin2/Items/Templates/MT2ItemExtractDragonSoulTemplate.h) — Extract/EXTRACT_DRAGON_SOUL (0). variant shell; inherited/system/quest/no-op disposition pending. Owner: SYS-03.
- [ ] **C-024** [UMT2ItemFishAliveTemplate](../../../Source/Metin2/Items/Templates/MT2ItemFishAliveTemplate.h) — Fish/FISH_ALIVE (0). variant shell; inherited/system/quest/no-op disposition pending. Owner: OWN-09.
- [ ] **C-025** [UMT2ItemFishDeadTemplate](../../../Source/Metin2/Items/Templates/MT2ItemFishDeadTemplate.h) — Fish/FISH_DEAD (1). variant shell; inherited/system/quest/no-op disposition pending. Owner: OWN-09.
- [ ] **C-026** [UMT2ItemLotteryInstantTemplate](../../../Source/Metin2/Items/Templates/MT2ItemLotteryInstantTemplate.h) — Lottery/LOTTERY_INSTANT (1). variant shell; inherited/system/quest/no-op disposition pending. Owner: PAR-02.
- [ ] **C-027** [UMT2ItemLotteryTicketTemplate](../../../Source/Metin2/Items/Templates/MT2ItemLotteryTicketTemplate.h) — Lottery/LOTTERY_TICKET (0). variant shell; inherited/system/quest/no-op disposition pending. Owner: PAR-02.
- [ ] **C-028** [UMT2ItemMaterialBloodTemplate](../../../Source/Metin2/Items/Templates/MT2ItemMaterialBloodTemplate.h) — Material/MATERIAL_BLOOD (1). variant shell; inherited/system/quest/no-op disposition pending. Owner: EQP-01 / EQP-02 / BASE-02.
- [ ] **C-029** [UMT2ItemMaterialDragonSoulRefineBlessedTemplate](../../../Source/Metin2/Items/Templates/MT2ItemMaterialDragonSoulRefineBlessedTemplate.h) — Material/MATERIAL_DS_REFINE_BLESSED (6). variant shell; inherited/system/quest/no-op disposition pending. Owner: SYS-03.
- [ ] **C-030** [UMT2ItemMaterialDragonSoulRefineHolyTemplate](../../../Source/Metin2/Items/Templates/MT2ItemMaterialDragonSoulRefineHolyTemplate.h) — Material/MATERIAL_DS_REFINE_HOLLY (7). variant shell; inherited/system/quest/no-op disposition pending. Owner: SYS-03.
- [ ] **C-031** [UMT2ItemMaterialDragonSoulRefineNormalTemplate](../../../Source/Metin2/Items/Templates/MT2ItemMaterialDragonSoulRefineNormalTemplate.h) — Material/MATERIAL_DS_REFINE_NORMAL (5). variant shell; inherited/system/quest/no-op disposition pending. Owner: SYS-03.
- [ ] **C-032** [UMT2ItemMaterialJewelTemplate](../../../Source/Metin2/Items/Templates/MT2ItemMaterialJewelTemplate.h) — Material/MATERIAL_JEWEL (4). variant shell; inherited/system/quest/no-op disposition pending. Owner: EQP-01 / EQP-02 / BASE-02.
- [ ] **C-033** [UMT2ItemMaterialLeatherTemplate](../../../Source/Metin2/Items/Templates/MT2ItemMaterialLeatherTemplate.h) — Material/MATERIAL_LEATHER (0). variant shell; inherited/system/quest/no-op disposition pending. Owner: EQP-01 / EQP-02 / BASE-02.
- [ ] **C-034** [UMT2ItemMaterialNeedleTemplate](../../../Source/Metin2/Items/Templates/MT2ItemMaterialNeedleTemplate.h) — Material/MATERIAL_NEEDLE (3). variant shell; inherited/system/quest/no-op disposition pending. Owner: EQP-01 / EQP-02 / BASE-02.
- [ ] **C-035** [UMT2ItemMaterialRootTemplate](../../../Source/Metin2/Items/Templates/MT2ItemMaterialRootTemplate.h) — Material/MATERIAL_ROOT (2). variant shell; inherited/system/quest/no-op disposition pending. Owner: EQP-01 / EQP-02 / BASE-02.
- [ ] **C-036** [UMT2ItemMetinGoldTemplate](../../../Source/Metin2/Items/Templates/MT2ItemMetinGoldTemplate.h) — Metin/METIN_GOLD (1). variant shell; inherited/system/quest/no-op disposition pending. Owner: OWN-05.
- [ ] **C-037** [UMT2ItemMetinNormalTemplate](../../../Source/Metin2/Items/Templates/MT2ItemMetinNormalTemplate.h) — Metin/METIN_NORMAL (0). variant shell; inherited/system/quest/no-op disposition pending. Owner: OWN-05.
- [ ] **C-038** [UMT2ItemResourceBloodPearlTemplate](../../../Source/Metin2/Items/Templates/MT2ItemResourceBloodPearlTemplate.h) — Resource/RESOURCE_BLOOD_PEARL (3). variant shell; inherited/system/quest/no-op disposition pending. Owner: EQP-01 / EQP-02 / BASE-02.
- [ ] **C-039** [UMT2ItemResourceBluePearlTemplate](../../../Source/Metin2/Items/Templates/MT2ItemResourceBluePearlTemplate.h) — Resource/RESOURCE_BLUE_PEARL (4). variant shell; inherited/system/quest/no-op disposition pending. Owner: EQP-01 / EQP-02 / BASE-02.
- [ ] **C-040** [UMT2ItemResourceBucketTemplate](../../../Source/Metin2/Items/Templates/MT2ItemResourceBucketTemplate.h) — Resource/RESOURCE_BUCKET (6). variant shell; inherited/system/quest/no-op disposition pending. Owner: EQP-01 / EQP-02 / BASE-02.
- [ ] **C-041** [UMT2ItemResourceCrystalTemplate](../../../Source/Metin2/Items/Templates/MT2ItemResourceCrystalTemplate.h) — Resource/RESOURCE_CRYSTAL (7). variant shell; inherited/system/quest/no-op disposition pending. Owner: EQP-01 / EQP-02 / BASE-02.
- [ ] **C-042** [UMT2ItemResourceFishboneTemplate](../../../Source/Metin2/Items/Templates/MT2ItemResourceFishboneTemplate.h) — Resource/RESOURCE_FISHBONE (0). variant shell; inherited/system/quest/no-op disposition pending. Owner: OWN-09.
- [ ] **C-043** [UMT2ItemResourceGemTemplate](../../../Source/Metin2/Items/Templates/MT2ItemResourceGemTemplate.h) — Resource/RESOURCE_GEM (8). variant shell; inherited/system/quest/no-op disposition pending. Owner: EQP-01 / EQP-02 / BASE-02.
- [ ] **C-044** [UMT2ItemResourceMetinTemplate](../../../Source/Metin2/Items/Templates/MT2ItemResourceMetinTemplate.h) — Resource/RESOURCE_METIN (10). variant shell; inherited/system/quest/no-op disposition pending. Owner: OWN-05.
- [ ] **C-045** [UMT2ItemResourceOreTemplate](../../../Source/Metin2/Items/Templates/MT2ItemResourceOreTemplate.h) — Resource/RESOURCE_ORE (11). variant shell; inherited/system/quest/no-op disposition pending. Owner: EQP-01 / EQP-02 / BASE-02.
- [ ] **C-046** [UMT2ItemResourceStoneTemplate](../../../Source/Metin2/Items/Templates/MT2ItemResourceStoneTemplate.h) — Resource/RESOURCE_STONE (9). variant shell; inherited/system/quest/no-op disposition pending. Owner: EQP-01 / EQP-02 / BASE-02.
- [ ] **C-047** [UMT2ItemResourceWaterstonePieceTemplate](../../../Source/Metin2/Items/Templates/MT2ItemResourceWaterstonePieceTemplate.h) — Resource/RESOURCE_WATERSTONEPIECE (1). variant shell; inherited/system/quest/no-op disposition pending. Owner: EQP-01 / EQP-02 / BASE-02.
- [ ] **C-048** [UMT2ItemResourceWaterstoneTemplate](../../../Source/Metin2/Items/Templates/MT2ItemResourceWaterstoneTemplate.h) — Resource/RESOURCE_WATERSTONE (2). variant shell; inherited/system/quest/no-op disposition pending. Owner: EQP-01 / EQP-02 / BASE-02.
- [ ] **C-049** [UMT2ItemResourceWhitePearlTemplate](../../../Source/Metin2/Items/Templates/MT2ItemResourceWhitePearlTemplate.h) — Resource/RESOURCE_WHITE_PEARL (5). variant shell; inherited/system/quest/no-op disposition pending. Owner: EQP-01 / EQP-02 / BASE-02.
- [ ] **C-050** [UMT2ItemSpecialDocumentTemplate](../../../Source/Metin2/Items/Templates/MT2ItemSpecialDocumentTemplate.h) — Special/SPECIAL_DOC (2). variant shell; inherited/system/quest/no-op disposition pending. Owner: EQP-01 / EQP-02 / BASE-02.
- [ ] **C-051** [UMT2ItemSpecialDragonSoulSlot1Template](../../../Source/Metin2/Items/Templates/MT2ItemSpecialDragonSoulSlot1Template.h) — SpecialDragonSoul/DS_SLOT1 (0). variant shell; inherited/system/quest/no-op disposition pending. Owner: SYS-03.
- [ ] **C-052** [UMT2ItemSpecialDragonSoulSlot2Template](../../../Source/Metin2/Items/Templates/MT2ItemSpecialDragonSoulSlot2Template.h) — SpecialDragonSoul/DS_SLOT2 (1). variant shell; inherited/system/quest/no-op disposition pending. Owner: SYS-03.
- [ ] **C-053** [UMT2ItemSpecialDragonSoulSlot3Template](../../../Source/Metin2/Items/Templates/MT2ItemSpecialDragonSoulSlot3Template.h) — SpecialDragonSoul/DS_SLOT3 (2). variant shell; inherited/system/quest/no-op disposition pending. Owner: SYS-03.
- [ ] **C-054** [UMT2ItemSpecialDragonSoulSlot4Template](../../../Source/Metin2/Items/Templates/MT2ItemSpecialDragonSoulSlot4Template.h) — SpecialDragonSoul/DS_SLOT4 (3). variant shell; inherited/system/quest/no-op disposition pending. Owner: SYS-03.
- [ ] **C-055** [UMT2ItemSpecialDragonSoulSlot5Template](../../../Source/Metin2/Items/Templates/MT2ItemSpecialDragonSoulSlot5Template.h) — SpecialDragonSoul/DS_SLOT5 (4). variant shell; inherited/system/quest/no-op disposition pending. Owner: SYS-03.
- [ ] **C-056** [UMT2ItemSpecialDragonSoulSlot6Template](../../../Source/Metin2/Items/Templates/MT2ItemSpecialDragonSoulSlot6Template.h) — SpecialDragonSoul/DS_SLOT6 (5). variant shell; inherited/system/quest/no-op disposition pending. Owner: SYS-03.
- [ ] **C-057** [UMT2ItemSpecialKeyTemplate](../../../Source/Metin2/Items/Templates/MT2ItemSpecialKeyTemplate.h) — Special/SPECIAL_KEY (1). variant shell; inherited/system/quest/no-op disposition pending. Owner: EQP-01 / EQP-02 / BASE-02.
- [ ] **C-058** [UMT2ItemSpecialMapTemplate](../../../Source/Metin2/Items/Templates/MT2ItemSpecialMapTemplate.h) — Special/SPECIAL_MAP (0). variant shell; inherited/system/quest/no-op disposition pending. Owner: EQP-01 / EQP-02 / BASE-02.
- [ ] **C-059** [UMT2ItemSpecialSpiritTemplate](../../../Source/Metin2/Items/Templates/MT2ItemSpecialSpiritTemplate.h) — Special/SPECIAL_SPIRIT (3). variant shell; inherited/system/quest/no-op disposition pending. Owner: EQP-01 / EQP-02 / BASE-02.
- [ ] **C-060** [UMT2ItemToolFishingRodTemplate](../../../Source/Metin2/Items/Templates/MT2ItemToolFishingRodTemplate.h) — Tool/TOOL_FISHING_ROD (0). variant shell; inherited/system/quest/no-op disposition pending. Owner: OWN-09.
- [ ] **C-061** [UMT2ItemUniqueBookTemplate](../../../Source/Metin2/Items/Templates/MT2ItemUniqueBookTemplate.h) — Unique/UNIQUE_BOOK (1). variant shell; inherited/system/quest/no-op disposition pending. Owner: EQP-01 / EQP-02 / BASE-02.
- [ ] **C-062** [UMT2ItemUniqueLegacySubtype4Template](../../../Source/Metin2/Items/Templates/MT2ItemUniqueLegacySubtype4Template.h) — Unique/UNIQUE_4 (4). variant shell; inherited/system/quest/no-op disposition pending. Owner: EQP-01 / EQP-02 / BASE-02.
- [ ] **C-063** [UMT2ItemUniqueLegacySubtype5Template](../../../Source/Metin2/Items/Templates/MT2ItemUniqueLegacySubtype5Template.h) — Unique/UNIQUE_5 (5). variant shell; inherited/system/quest/no-op disposition pending. Owner: EQP-01 / EQP-02 / BASE-02.
- [ ] **C-064** [UMT2ItemUniqueLegacySubtype6Template](../../../Source/Metin2/Items/Templates/MT2ItemUniqueLegacySubtype6Template.h) — Unique/UNIQUE_6 (6). variant shell; inherited/system/quest/no-op disposition pending. Owner: EQP-01 / EQP-02 / BASE-02.
- [ ] **C-065** [UMT2ItemUniqueLegacySubtype7Template](../../../Source/Metin2/Items/Templates/MT2ItemUniqueLegacySubtype7Template.h) — Unique/UNIQUE_7 (7). variant shell; inherited/system/quest/no-op disposition pending. Owner: EQP-01 / EQP-02 / BASE-02.
- [ ] **C-066** [UMT2ItemUniqueLegacySubtype8Template](../../../Source/Metin2/Items/Templates/MT2ItemUniqueLegacySubtype8Template.h) — Unique/UNIQUE_8 (8). variant shell; inherited/system/quest/no-op disposition pending. Owner: EQP-01 / EQP-02 / BASE-02.
- [ ] **C-067** [UMT2ItemUniqueLegacySubtype9Template](../../../Source/Metin2/Items/Templates/MT2ItemUniqueLegacySubtype9Template.h) — Unique/UNIQUE_9 (9). variant shell; inherited/system/quest/no-op disposition pending. Owner: EQP-01 / EQP-02 / BASE-02.
- [ ] **C-068** [UMT2ItemUniqueNoneTemplate](../../../Source/Metin2/Items/Templates/MT2ItemUniqueNoneTemplate.h) — Unique/UNIQUE_NONE (0). variant shell; inherited/system/quest/no-op disposition pending. Owner: EQP-01 / EQP-02 / BASE-02.
- [ ] **C-069** [UMT2ItemUniqueSpecialMountRideTemplate](../../../Source/Metin2/Items/Templates/MT2ItemUniqueSpecialMountRideTemplate.h) — Unique/UNIQUE_3 (legacy enum value UNIQUE_SPECIAL_MOUNT_RIDE) (3). variant shell; inherited/system/quest/no-op disposition pending. Owner: EQP-01 / EQP-02 / BASE-02.
- [ ] **C-070** [UMT2ItemUniqueSpecialRideTemplate](../../../Source/Metin2/Items/Templates/MT2ItemUniqueSpecialRideTemplate.h) — Unique/UNIQUE_SPECIAL_RIDE (2). variant shell; inherited/system/quest/no-op disposition pending. Owner: EQP-01 / EQP-02 / BASE-02.
- [ ] **C-071** [UMT2ItemUniqueSpecialUseTemplate](../../../Source/Metin2/Items/Templates/MT2ItemUniqueSpecialUseTemplate.h) — Unique/USE_SPECIAL accepted in ITEM_UNIQUE table (10). variant shell; inherited/system/quest/no-op disposition pending. Owner: EQP-01 / EQP-02 / BASE-02.
- [ ] **C-072** [UMT2ItemUseAbilityUpTemplate](../../../Source/Metin2/Items/Templates/MT2ItemUseAbilityUpTemplate.h) — Use/USE_ABILITY_UP (7). behavior currently routed outside the subtype. Owner: OWN-02 / OWN-03.
- [ ] **C-073** [UMT2ItemUseAddAccessorySocketTemplate](../../../Source/Metin2/Items/Templates/MT2ItemUseAddAccessorySocketTemplate.h) — Use/USE_ADD_ACCESSORY_SOCKET (20). behavior currently routed outside the subtype. Owner: OWN-06.
- [ ] **C-074** [UMT2ItemUseAddAttribute2Template](../../../Source/Metin2/Items/Templates/MT2ItemUseAddAttribute2Template.h) — Use/USE_ADD_ATTRIBUTE2 (22). Class-owned fifth-addition policy and imported 70024 success/failure/rejection transactions verified in Batch 01; addon slot accounting and remaining integration checks pending. Owner: OWN-01A / OWN-01D / OWN-01F.
- [ ] **C-075** [UMT2ItemUseAddAttributeTemplate](../../../Source/Metin2/Items/Templates/MT2ItemUseAddAttributeTemplate.h) — Use/USE_ADD_ATTRIBUTE (19). Class-owned standard addition policy, imported 71085 routing and per-slot/eligibility tests verified in Batch 01; special low-level variants, addon slot accounting and remaining integration checks pending. Owner: OWN-01A / OWN-01C / OWN-01D / OWN-01F.
- [ ] **C-076** [UMT2ItemUseAffectTemplate](../../../Source/Metin2/Items/Templates/MT2ItemUseAffectTemplate.h) — Use/USE_AFFECT (8). behavior currently routed outside the subtype. Owner: OWN-02 / OWN-03.
- [ ] **C-077** [UMT2ItemUseBaitTemplate](../../../Source/Metin2/Items/Templates/MT2ItemUseBaitTemplate.h) — Use/USE_BAIT (6). ExecuteUse exists; policy ownership/integration audit pending. Owner: OWN-09.
- [ ] **C-078** [UMT2ItemUseBindTemplate](../../../Source/Metin2/Items/Templates/MT2ItemUseBindTemplate.h) — Use/USE_BIND (25). native handler not found; legacy contract/disposition pending. Owner: PAR-01.
- [ ] **C-079** [UMT2ItemUseBucketTemplate](../../../Source/Metin2/Items/Templates/MT2ItemUseBucketTemplate.h) — Use/USE_BUCKET (15). native handler not found; legacy contract/disposition pending. Owner: PAR-01.
- [ ] **C-080** [UMT2ItemUseChangeAttribute2Template](../../../Source/Metin2/Items/Templates/MT2ItemUseChangeAttribute2Template.h) — Use/USE_CHANGE_ATTRIBUTE2 (24). native handler not found; legacy contract/disposition pending. Owner: PAR-01.
- [ ] **C-081** [UMT2ItemUseChangeAttributeTemplate](../../../Source/Metin2/Items/Templates/MT2ItemUseChangeAttributeTemplate.h) — Use/USE_CHANGE_ATTRIBUTE (18). ApplyToEquipment exists; regression verification pending. Owner: OWN-01.
- [ ] **C-082** [UMT2ItemUseCleanSocketTemplate](../../../Source/Metin2/Items/Templates/MT2ItemUseCleanSocketTemplate.h) — Use/USE_CLEAN_SOCKET (17). behavior currently routed outside the subtype. Owner: OWN-05.
- [ ] **C-083** [UMT2ItemUseClearTemplate](../../../Source/Metin2/Items/Templates/MT2ItemUseClearTemplate.h) — Use/USE_CLEAR (12). behavior currently routed outside the subtype. Owner: OWN-02 / OWN-03.
- [ ] **C-084** [UMT2ItemUseCreateStoneTemplate](../../../Source/Metin2/Items/Templates/MT2ItemUseCreateStoneTemplate.h) — Use/USE_CREATE_STONE (9). ExecuteUse exists; policy ownership/integration audit pending. Owner: OWN-09.
- [ ] **C-085** [UMT2ItemUseDetachmentTemplate](../../../Source/Metin2/Items/Templates/MT2ItemUseDetachmentTemplate.h) — Use/USE_DETACHMENT (14). behavior currently routed outside the subtype. Owner: OWN-05.
- [ ] **C-086** [UMT2ItemUseInvisibilityTemplate](../../../Source/Metin2/Items/Templates/MT2ItemUseInvisibilityTemplate.h) — Use/USE_INVISIBILITY (13). native handler not found; legacy contract/disposition pending. Owner: PAR-01.
- [ ] **C-087** [UMT2ItemUseMoneybagTemplate](../../../Source/Metin2/Items/Templates/MT2ItemUseMoneybagTemplate.h) — Use/USE_MONEYBAG (5). native handler not found; legacy contract/disposition pending. Owner: PAR-01.
- [ ] **C-088** [UMT2ItemUseMoveTemplate](../../../Source/Metin2/Items/Templates/MT2ItemUseMoveTemplate.h) — Use/USE_MOVE (3). native handler not found; legacy contract/disposition pending. Owner: PAR-01.
- [ ] **C-089** [UMT2ItemUsePotionContinueTemplate](../../../Source/Metin2/Items/Templates/MT2ItemUsePotionContinueTemplate.h) — Use/USE_POTION_CONTINUE (16). behavior currently routed outside the subtype. Owner: OWN-02 / OWN-03.
- [ ] **C-090** [UMT2ItemUsePotionNoDelayTemplate](../../../Source/Metin2/Items/Templates/MT2ItemUsePotionNoDelayTemplate.h) — Use/USE_POTION_NODELAY (11). behavior currently routed outside the subtype. Owner: OWN-02 / OWN-03.
- [ ] **C-091** [UMT2ItemUsePotionTemplate](../../../Source/Metin2/Items/Templates/MT2ItemUsePotionTemplate.h) — Use/USE_POTION (0). behavior currently routed outside the subtype. Owner: OWN-02 / OWN-03.
- [ ] **C-092** [UMT2ItemUsePutIntoAccessorySocketTemplate](../../../Source/Metin2/Items/Templates/MT2ItemUsePutIntoAccessorySocketTemplate.h) — Use/USE_PUT_INTO_ACCESSORY_SOCKET (21). behavior currently routed outside the subtype. Owner: OWN-06.
- [ ] **C-093** [UMT2ItemUsePutIntoBeltSocketTemplate](../../../Source/Metin2/Items/Templates/MT2ItemUsePutIntoBeltSocketTemplate.h) — Use/USE_PUT_INTO_BELT_SOCKET (29). behavior currently routed outside the subtype. Owner: OWN-06.
- [ ] **C-094** [UMT2ItemUsePutIntoRingSocketTemplate](../../../Source/Metin2/Items/Templates/MT2ItemUsePutIntoRingSocketTemplate.h) — Use/USE_PUT_INTO_RING_SOCKET (30). native handler not found; legacy contract/disposition pending. Owner: PAR-01.
- [ ] **C-095** [UMT2ItemUseRecipeTemplate](../../../Source/Metin2/Items/Templates/MT2ItemUseRecipeTemplate.h) — Use/USE_RECIPE (23). ExecuteUse exists; policy ownership/integration audit pending. Owner: OWN-09.
- [ ] **C-096** [UMT2ItemUseSpecialTemplate](../../../Source/Metin2/Items/Templates/MT2ItemUseSpecialTemplate.h) — Use/USE_SPECIAL (10). behavior currently routed outside the subtype. Owner: EQP-01 / EQP-02 / BASE-02.
- [ ] **C-097** [UMT2ItemUseTalismanTemplate](../../../Source/Metin2/Items/Templates/MT2ItemUseTalismanTemplate.h) — Use/USE_TALISMAN (1). native handler not found; legacy contract/disposition pending. Owner: PAR-01.
- [ ] **C-098** [UMT2ItemUseTimeChargeFixedTemplate](../../../Source/Metin2/Items/Templates/MT2ItemUseTimeChargeFixedTemplate.h) — Use/USE_TIME_CHARGE_FIX (28). native handler not found; legacy contract/disposition pending. Owner: SYS-03.
- [ ] **C-099** [UMT2ItemUseTimeChargePercentTemplate](../../../Source/Metin2/Items/Templates/MT2ItemUseTimeChargePercentTemplate.h) — Use/USE_TIME_CHARGE_PER (27). native handler not found; legacy contract/disposition pending. Owner: SYS-03.
- [ ] **C-100** [UMT2ItemUseTreasureBoxTemplate](../../../Source/Metin2/Items/Templates/MT2ItemUseTreasureBoxTemplate.h) — Use/USE_TREASURE_BOX (4). native handler not found; legacy contract/disposition pending. Owner: PAR-01.
- [ ] **C-101** [UMT2ItemUseTuningTemplate](../../../Source/Metin2/Items/Templates/MT2ItemUseTuningTemplate.h) — Use/USE_TUNING (2). native handler not found; legacy contract/disposition pending. Owner: PAR-01.
- [ ] **C-102** [UMT2ItemUseUnbindTemplate](../../../Source/Metin2/Items/Templates/MT2ItemUseUnbindTemplate.h) — Use/USE_UNBIND (26). native handler not found; legacy contract/disposition pending. Owner: PAR-01.
- [ ] **C-103** [UMT2ItemWeaponArrowTemplate](../../../Source/Metin2/Items/Templates/MT2ItemWeaponArrowTemplate.h) — Weapon/WEAPON_ARROW (6). variant shell; inherited/system/quest/no-op disposition pending. Owner: EQP-01 / EQP-02 / BASE-02.
- [ ] **C-104** [UMT2ItemWeaponBellTemplate](../../../Source/Metin2/Items/Templates/MT2ItemWeaponBellTemplate.h) — Weapon/WEAPON_BELL (4). variant shell; inherited/system/quest/no-op disposition pending. Owner: EQP-01 / EQP-02 / BASE-02.
- [ ] **C-105** [UMT2ItemWeaponBowTemplate](../../../Source/Metin2/Items/Templates/MT2ItemWeaponBowTemplate.h) — Weapon/WEAPON_BOW (2). variant shell; inherited/system/quest/no-op disposition pending. Owner: EQP-01 / EQP-02 / BASE-02.
- [ ] **C-106** [UMT2ItemWeaponDaggerTemplate](../../../Source/Metin2/Items/Templates/MT2ItemWeaponDaggerTemplate.h) — Weapon/WEAPON_DAGGER (1). variant shell; inherited/system/quest/no-op disposition pending. Owner: EQP-01 / EQP-02 / BASE-02.
- [ ] **C-107** [UMT2ItemWeaponFanTemplate](../../../Source/Metin2/Items/Templates/MT2ItemWeaponFanTemplate.h) — Weapon/WEAPON_FAN (5). variant shell; inherited/system/quest/no-op disposition pending. Owner: EQP-01 / EQP-02 / BASE-02.
- [ ] **C-108** [UMT2ItemWeaponMountSpearTemplate](../../../Source/Metin2/Items/Templates/MT2ItemWeaponMountSpearTemplate.h) — Weapon/WEAPON_MOUNT_SPEAR (7). variant shell; inherited/system/quest/no-op disposition pending. Owner: EQP-01 / EQP-02 / BASE-02.
- [ ] **C-109** [UMT2ItemWeaponSwordTemplate](../../../Source/Metin2/Items/Templates/MT2ItemWeaponSwordTemplate.h) — Weapon/WEAPON_SWORD (0). variant shell; inherited/system/quest/no-op disposition pending. Owner: EQP-01 / EQP-02 / BASE-02.
- [ ] **C-110** [UMT2ItemWeaponTwoHandedTemplate](../../../Source/Metin2/Items/Templates/MT2ItemWeaponTwoHandedTemplate.h) — Weapon/WEAPON_TWO_HANDED (3). variant shell; inherited/system/quest/no-op disposition pending. Owner: EQP-01 / EQP-02 / BASE-02.

### Additional template families and specializations

- [ ] **C-111** [UMT2ItemArmorTemplate](../../../Source/Metin2/Items/Templates/MT2ItemArmorTemplate.h). shared data/state/contract or specialized template; consumer/ownership audit pending. Owner: EQP-01 / EQP-02 / BASE-02.
- [ ] **C-112** [UMT2ItemAutoRecoveryTemplate](../../../Source/Metin2/Items/Templates/MT2ItemAutoRecoveryTemplate.h). shared data/state/contract or specialized template; consumer/ownership audit pending. Owner: OWN-04.
- [ ] **C-113** [UMT2ItemAutoUseTemplate](../../../Source/Metin2/Items/Templates/MT2ItemAutoUseTemplate.h). shared data/state/contract or specialized template; consumer/ownership audit pending. Owner: PAR-02.
- [ ] **C-114** [UMT2ItemBeltTemplate](../../../Source/Metin2/Items/Templates/MT2ItemBeltTemplate.h). shared data/state/contract or specialized template; consumer/ownership audit pending. Owner: EQP-01 / EQP-02 / BASE-02.
- [ ] **C-115** [UMT2ItemBlendTemplate](../../../Source/Metin2/Items/Templates/MT2ItemBlendTemplate.h). ExecuteUse exists; policy ownership/integration audit pending. Owner: EQP-01 / EQP-02 / BASE-02.
- [ ] **C-116** [UMT2ItemBonusConsumableTemplate](../../../Source/Metin2/Items/Templates/MT2ItemBonusConsumableTemplate.h). shared data/state/contract or specialized template; consumer/ownership audit pending. Owner: OWN-01.
- [ ] **C-117** [UMT2ItemCampfireTemplate](../../../Source/Metin2/Items/Templates/MT2ItemCampfireTemplate.h). ExecuteUse exists; policy ownership/integration audit pending. Owner: OWN-09.
- [ ] **C-118** [UMT2ItemConsumableTemplate](../../../Source/Metin2/Items/Templates/MT2ItemConsumableTemplate.h). shared data/state/contract or specialized template; consumer/ownership audit pending. Owner: EQP-01 / EQP-02 / BASE-02.
- [ ] **C-119** [UMT2ItemContainerTemplate](../../../Source/Metin2/Items/Templates/MT2ItemContainerTemplate.h). shared data/state/contract or specialized template; consumer/ownership audit pending. Owner: EQP-01 / EQP-02 / BASE-02.
- [ ] **C-120** [UMT2ItemCostumeTemplate](../../../Source/Metin2/Items/Templates/MT2ItemCostumeTemplate.h). shared data/state/contract or specialized template; consumer/ownership audit pending. Owner: OWN-07 / EQP-01.
- [ ] **C-121** [UMT2ItemDragonSoulTemplate](../../../Source/Metin2/Items/Templates/MT2ItemDragonSoulTemplate.h). shared data/state/contract or specialized template; consumer/ownership audit pending. Owner: SYS-03.
- [ ] **C-122** [UMT2ItemEquipmentTemplate](../../../Source/Metin2/Items/Templates/MT2ItemEquipmentTemplate.h). shared data/state/contract or specialized template; consumer/ownership audit pending. Owner: EQP-01 / EQP-02 / BASE-02.
- [ ] **C-123** [UMT2ItemExtractTemplate](../../../Source/Metin2/Items/Templates/MT2ItemExtractTemplate.h). shared data/state/contract or specialized template; consumer/ownership audit pending. Owner: EQP-01 / EQP-02 / BASE-02.
- [ ] **C-124** [UMT2ItemFishTemplate](../../../Source/Metin2/Items/Templates/MT2ItemFishTemplate.h). ExecuteUse exists; policy ownership/integration audit pending. Owner: OWN-09.
- [ ] **C-125** [UMT2ItemGiftBoxTemplate](../../../Source/Metin2/Items/Templates/MT2ItemGiftBoxTemplate.h). shared data/state/contract or specialized template; consumer/ownership audit pending. Owner: OWN-10.
- [ ] **C-126** [UMT2ItemGoldTemplate](../../../Source/Metin2/Items/Templates/MT2ItemGoldTemplate.h). shared data/state/contract or specialized template; consumer/ownership audit pending. Owner: EQP-01 / EQP-02 / BASE-02.
- [ ] **C-127** [UMT2ItemHairTemplate](../../../Source/Metin2/Items/Templates/MT2ItemHairTemplate.h). shared data/state/contract or specialized template; consumer/ownership audit pending. Owner: OWN-07 / EQP-01.
- [ ] **C-128** [UMT2ItemLootCrateTemplate](../../../Source/Metin2/Items/Templates/MT2ItemLootCrateTemplate.h). shared data/state/contract or specialized template; consumer/ownership audit pending. Owner: OWN-10.
- [ ] **C-129** [UMT2ItemLotteryTemplate](../../../Source/Metin2/Items/Templates/MT2ItemLotteryTemplate.h). shared data/state/contract or specialized template; consumer/ownership audit pending. Owner: PAR-02.
- [ ] **C-130** [UMT2ItemMaterialTemplate](../../../Source/Metin2/Items/Templates/MT2ItemMaterialTemplate.h). shared data/state/contract or specialized template; consumer/ownership audit pending. Owner: EQP-01 / EQP-02 / BASE-02.
- [ ] **C-131** [UMT2ItemMetinRemovalTemplate](../../../Source/Metin2/Items/Templates/MT2ItemMetinRemovalTemplate.h). shared data/state/contract or specialized template; consumer/ownership audit pending. Owner: OWN-05.
- [ ] **C-132** [UMT2ItemMetinStoneTemplate](../../../Source/Metin2/Items/Templates/MT2ItemMetinStoneTemplate.h). shared data/state/contract or specialized template; consumer/ownership audit pending. Owner: OWN-05.
- [ ] **C-133** [UMT2ItemPickTemplate](../../../Source/Metin2/Items/Templates/MT2ItemPickTemplate.h). shared data/state/contract or specialized template; consumer/ownership audit pending. Owner: OWN-09.
- [ ] **C-134** [UMT2ItemPolymorphTemplate](../../../Source/Metin2/Items/Templates/MT2ItemPolymorphTemplate.h). ExecuteUse exists; policy ownership/integration audit pending. Owner: OWN-12.
- [ ] **C-135** [UMT2ItemQuestTemplate](../../../Source/Metin2/Items/Templates/MT2ItemQuestTemplate.h). shared data/state/contract or specialized template; consumer/ownership audit pending. Owner: EQP-01 / EQP-02 / BASE-02.
- [ ] **C-136** [UMT2ItemRefinementScrollTemplate](../../../Source/Metin2/Items/Templates/MT2ItemRefinementScrollTemplate.h). shared data/state/contract or specialized template; consumer/ownership audit pending. Owner: OWN-10.
- [ ] **C-137** [UMT2ItemResourceTemplate](../../../Source/Metin2/Items/Templates/MT2ItemResourceTemplate.h). shared data/state/contract or specialized template; consumer/ownership audit pending. Owner: EQP-01 / EQP-02 / BASE-02.
- [ ] **C-138** [UMT2ItemRingTemplate](../../../Source/Metin2/Items/Templates/MT2ItemRingTemplate.h). shared data/state/contract or specialized template; consumer/ownership audit pending. Owner: EQP-01 / EQP-02 / BASE-02.
- [ ] **C-139** [UMT2ItemRodTemplate](../../../Source/Metin2/Items/Templates/MT2ItemRodTemplate.h). shared data/state/contract or specialized template; consumer/ownership audit pending. Owner: OWN-09.
- [ ] **C-140** [UMT2ItemSecondaryCoinTemplate](../../../Source/Metin2/Items/Templates/MT2ItemSecondaryCoinTemplate.h). shared data/state/contract or specialized template; consumer/ownership audit pending. Owner: EQP-01 / EQP-02 / BASE-02.
- [ ] **C-141** [UMT2ItemSkillBookTemplate](../../../Source/Metin2/Items/Templates/MT2ItemSkillBookTemplate.h). shared data/state/contract or specialized template; consumer/ownership audit pending. Owner: OWN-08.
- [ ] **C-142** [UMT2ItemSkillForgetTemplate](../../../Source/Metin2/Items/Templates/MT2ItemSkillForgetTemplate.h). ExecuteUse exists; policy ownership/integration audit pending. Owner: OWN-08.
- [ ] **C-143** [UMT2ItemSocketCleanerTemplate](../../../Source/Metin2/Items/Templates/MT2ItemSocketCleanerTemplate.h). shared data/state/contract or specialized template; consumer/ownership audit pending. Owner: OWN-05.
- [ ] **C-144** [UMT2ItemSpecialDragonSoulTemplate](../../../Source/Metin2/Items/Templates/MT2ItemSpecialDragonSoulTemplate.h). shared data/state/contract or specialized template; consumer/ownership audit pending. Owner: SYS-03.
- [ ] **C-145** [UMT2ItemSpecialTemplate](../../../Source/Metin2/Items/Templates/MT2ItemSpecialTemplate.h). shared data/state/contract or specialized template; consumer/ownership audit pending. Owner: EQP-01 / EQP-02 / BASE-02.
- [ ] **C-146** [UMT2ItemToolTemplate](../../../Source/Metin2/Items/Templates/MT2ItemToolTemplate.h). shared data/state/contract or specialized template; consumer/ownership audit pending. Owner: EQP-01 / EQP-02 / BASE-02.
- [ ] **C-147** [UMT2ItemTotemTemplate](../../../Source/Metin2/Items/Templates/MT2ItemTotemTemplate.h). shared data/state/contract or specialized template; consumer/ownership audit pending. Owner: EQP-01 / EQP-02 / BASE-02.
- [ ] **C-148** [UMT2ItemTreasureBoxTemplate](../../../Source/Metin2/Items/Templates/MT2ItemTreasureBoxTemplate.h). shared data/state/contract or specialized template; consumer/ownership audit pending. Owner: OWN-10.
- [ ] **C-149** [UMT2ItemTreasureKeyTemplate](../../../Source/Metin2/Items/Templates/MT2ItemTreasureKeyTemplate.h). shared data/state/contract or specialized template; consumer/ownership audit pending. Owner: OWN-10.
- [ ] **C-150** [UMT2ItemUniqueTemplate](../../../Source/Metin2/Items/Templates/MT2ItemUniqueTemplate.h). shared data/state/contract or specialized template; consumer/ownership audit pending. Owner: EQP-01 / EQP-02 / BASE-02.
- [ ] **C-151** [UMT2ItemUseAddRareBonusTemplate](../../../Source/Metin2/Items/Templates/MT2ItemUseAddRareBonusTemplate.h). ApplyToEquipment exists; regression verification pending. Owner: OWN-01.
- [ ] **C-152** [UMT2ItemUseChangeRareBonusesTemplate](../../../Source/Metin2/Items/Templates/MT2ItemUseChangeRareBonusesTemplate.h). ApplyToEquipment exists; regression verification pending. Owner: OWN-01.
- [ ] **C-153** [UMT2ItemUseTemplate](../../../Source/Metin2/Items/Templates/MT2ItemUseTemplate.h). shared data/state/contract or specialized template; consumer/ownership audit pending. Owner: OWN-02 / OWN-03.
- [ ] **C-154** [UMT2ItemWeaponTemplate](../../../Source/Metin2/Items/Templates/MT2ItemWeaponTemplate.h). shared data/state/contract or specialized template; consumer/ownership audit pending. Owner: EQP-01 / EQP-02 / BASE-02.

### Dedicated action templates

- [ ] **C-155** [UMT2GrandMasterTrainingItemTemplate](../../../Source/Metin2/Items/Use/MT2GrandMasterTrainingItemTemplate.h). ExecuteUse exists; policy ownership/integration audit pending. Owner: OWN-08.
- [ ] **C-156** [UMT2HorseBookItemTemplate](../../../Source/Metin2/Items/Use/MT2HorseBookItemTemplate.h). ExecuteUse exists; policy ownership/integration audit pending. Owner: OWN-11.
- [ ] **C-157** [UMT2HorseCareItemTemplate](../../../Source/Metin2/Items/Use/MT2HorseCareItemTemplate.h). ExecuteUse exists; policy ownership/integration audit pending. Owner: OWN-11.
- [ ] **C-158** [UMT2ItemActionTemplate](../../../Source/Metin2/Items/Use/MT2ItemActionTemplate.h). shared data/state/contract or specialized template; consumer/ownership audit pending. Owner: BASE-02 / OWN-11.
- [ ] **C-159** [UMT2KarmaRecoveryItemTemplate](../../../Source/Metin2/Items/Use/MT2KarmaRecoveryItemTemplate.h). ExecuteUse exists; policy ownership/integration audit pending. Owner: OWN-11.
- [ ] **C-160** [UMT2MobLureItemTemplate](../../../Source/Metin2/Items/Use/MT2MobLureItemTemplate.h). ExecuteUse exists; policy ownership/integration audit pending. Owner: OWN-11.
- [ ] **C-161** [UMT2MountItemTemplate](../../../Source/Metin2/Items/Use/MT2MountItemTemplate.h). ExecuteUse exists; policy ownership/integration audit pending. Owner: OWN-11.

### Common template contract

- [ ] **C-162** [UMT2ItemTemplate](../../../Source/Metin2/Items/MT2ItemTemplate.h). shared data/state/contract or specialized template; consumer/ownership audit pending. Owner: EQP-01 / EQP-02 / BASE-02.

### Beginner bonus variants

- [ ] **C-163** [UMT2ItemBeginnerBonusAdditionTemplate](../../../Source/Metin2/Items/Templates/MT2ItemBeginnerBonusAdditionTemplate.h). Beginner eligibility and imported 71152/76024 routing implemented and tested; inherits ordinary addition behavior. Full shared addon-slot/integration audit remains pending. Owner: OWN-01C1 / OWN-01D / OWN-01F.
- [ ] **C-164** [UMT2ItemBeginnerBonusRerollTemplate](../../../Source/Metin2/Items/Templates/MT2ItemBeginnerBonusRerollTemplate.h). Beginner eligibility and imported 71151/76023 routing implemented and tested; inherits ordinary reroll behavior. Shared cooldown/addon/integration work remains pending. Owner: OWN-01C1 / OWN-01C / OWN-01D / OWN-01F.

## Completion log

Append an entry only after performing the work. Keep legacy research citations and test/build report paths specific enough for another engineer to reproduce the decision.

| Task / class IDs | Date | Final disposition / result | Legacy source and current code evidence | Build / automation / asset evidence | Save / network / gameplay checks | Remaining dependency |
| --- | --- | --- | --- | --- | --- | --- |
| PLAN / BASE-00 / BASE-00A / BASE-00B | 2026-10-20 | Tracking plan created; 162 classes, 110 subtype entries and 104 generic TODO headers enumerated from source | Current class headers, subtype catalog, resolver and catalog test source | Document checks only; no gameplay build/test claim | Not run for documentation task | BASE-01 and subsequent tasks |
| OWN-01C1; partial C-163/C-164 | 2026-10-21 | Implemented beginner addition/reroll eligibility in two dedicated native subclasses; project setting controls maximum target required level (40). Runtime has no item-ID branching. | Legacy char_item.cpp lines 4593/4643 (four source IDs, weapon/body, all LIMIT_LEVEL checks). Importer routes matching type/subtype and the targeted behavior commandlet refreshes exactly four assets. | Editor and Server Win64 Development builds passed (UE 5.7.4). Three pre-change imported-asset tests failed because the assets lacked bonus-consumable parents. Final Metin2 suite: all 14 available tests passed, including six beginner-item tests; report Saved/Automation/ItemImplementation/Batch02-Final/index.json. Import: four updated, zero errors. Assets backed up under Batch02-AssetsBefore; passing temporary test source archived under Batch02-VerifiedTests and removed from Source. | Verified weapon/body level-40 boundary, shield rejection, later high-level limit rejection, unrelated strength limits, configurable cap, no level requirement and actual inventory consumption/rejection against imported sources. Backup-versus-refreshed comparison verified all common non-transient item/use defaults were preserved. Fresh read-only review found no code defect. Content changes are in the separate Content repository. No save/RPC layout changes; packaged/live-multiplayer validation not performed. | Ordinary reroll cooldown/special distributions, addon slot parity and remaining OWN-01F integration; full class checkboxes remain pending. |
| Validation cleanup / bot placement | 2026-10-20 | Fixed the bot spawn shortfall, then removed all 43 files under Source/Metin2/Tests and their dependent editor registrations at the user's request. | MT2LoadTestSubsystem uses the capsule's actual collision responses, includes the requester as a blocker and retries actor-spawn collisions within the existing 12-position budget. | Fixed-seed regression reproduced the failure at seed 8 before the fix. Afterward all 190 Metin2 tests passed, including 32 seeded bot-spawn batches. Report: Saved/Automation/ItemImplementation/FakePlayers-FullGreen/index.json. Editor and dedicated-server Win64 Development builds passed after removal. | Entire source folder backed up and hash-verified under Saved/RemovedTests/20261020-BotSpawnFix/Tests; original backup retained separately. Other plugin-local tests were not removed. No gameplay save/network format changes. | Archived tests must be restored/recreated for subsequent regression runs; packaged multiplayer remains separate validation. |
| BASE-01 (first three checks), OWN-01A/B; partial C-074/C-075 | 2026-10-20 | Standard normal-addition policies verified; per-slot chances and normal prototype exclusions corrected. Valid failures consume one, invalid requests preserve inputs. Config preserves ordered duplicate weights and all 41 authored polymorph-power values. | Legacy game/src/constants.cpp aiItemAttributeAddPercent; char_item.cpp USE_ADD_ATTRIBUTE/USE_ADD_ATTRIBUTE2; item_attribute.cpp HasAttr/AddAttribute; client PythonNetworkStreamPhaseGameItem.cpp SendItemUseToItemPacket. Policy remains in the two concrete templates; InventoryBonuses owns generic commit. | UE 5.7.4 Editor and Server Win64 Development builds succeeded. Item baseline: 40 passed, 2 failed; five new policy/transaction tests and rare-config test reproduced failures before fixes. Final Metin2.Items: 51 passed (29 with warnings), zero failures. Full Metin2: 190 passed (80 with warnings), zero failures. Reports: Saved/Automation/ItemImplementation/Batch01-{Baseline,Red,Config-Red,Green,Full}/index.json. Actual imported five bonus classes verified; real 70024 transactions exercised. | Tests cover fifth success/failure consumption, no mutation on rejected definitions, prototype duplicates, empty weights, death, trading and independent rare-kind duplicates. Existing item/polymorph regressions pass. Read-only independent source review found no substantive defect. No live multiplayer, packaging, save round-trip or Blueprint-reference migration validation performed. | OWN-01C/D/E/F and remaining BASE-01 asset routing checks; full class checkboxes intentionally remain pending. Custom settings overrides use the five named NormalBonusAdditionChances fields. |

When a class is complete through inherited/shared behavior, record the implementing ancestor and consumers. When it is quest/system-owned or unused, record the complete dispatch/content evidence and the applicable rejection/no-op behavior. A missing override alone is not sufficient evidence for either conclusion.

