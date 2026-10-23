# Item behavior and legacy parity

## Runtime model

Imported item Blueprints inherit from dedicated native family classes under `Items/Templates`
and `Items/Use`; `MT2ItemTemplate.h` contains only the shared base. A class
mapping alone does not establish working gameplay: right-click use, drag-to-apply, equipping,
per-instance generation, persistence and tooltips must all implement the corresponding behavior.
The server validates and performs inventory transactions. Clients request actions and display results.

Legacy `ITEM_TYPE_*` and `USE_*` declarations include unused entries. `AUTOUSE`, `MATERIAL`,
`SPECIAL`, `TOOL` and `LOTTERY` have no generic right-click operation in `CHARACTER::UseItemEx`;
their real behavior belongs to specific content, quests or another system. Assigning an invented
generic consume action would break parity.

## Coverage and dependencies

| Family or subtype group | Current behavior / work required |
| --- | --- |
| Weapons, armor, unique equipment, costumes, rings, belts, totems | Equipment/stat/visual foundation exists. Audit family-specific wear restrictions, expiry and belt inventory rules. |
| Metin stones, accessory materials | Native attachment, refinement and accessory wear systems exist. Audit detachment/cleaning and remaining drag-to-apply subtypes. |
| Recovery potions, speed potions, timed affects, auto recovery | Ability-up supports the nine legacy stat applies with refresh semantics. Continuous HP/SP recovery uses fixed per-second amounts for the full duration, including time spent at full resources. Cleansing removes poison, fire, stun, slow and configured skill debuffs. |
| Quest items and quest-specific special items | Quest item-use/give/socket dispatch exists. Check individual imported scripts rather than implementing a second competing handler. |
| Skill books, horse books, horse care, mount items, hair looks, mob lure | Dedicated runtime handlers exist. Verify item-specific conditions against legacy content. |
| Fishing rods, bait, fish, pickaxes, raw/refined ores | Fishing/mining/refining/accessory foundations exist. `USE_BAIT` imports legacy `Value0` as the bait item's own `BaitPower`; server use requires an equipped rod and an idle fishing attempt, replaces the rod's current bait, and consumes exactly one bait. Live fish have table-driven opening outcomes; live/dead fish can be given to a campfire to roast the entire stack. |
| Gift boxes, treasure boxes/keys | Native reward/crate/key transactions exist. Audit special non-item crate rewards and content groups. |
| Blend items | `blend.txt` supplies independent weighted magnitude/duration rolls. Instance sockets persist and appear in tooltips. Duplicate APPLY affects are rejected. Energy amplifies random equipment attributes, with separate eligible slots for energy and costume-attribute effects. |
| Polymorph balls and practice books | Dedicated item handlers and a transformation system exist, including mob presentation and configured combat scaling. Balls and books retain separate use policies. Complete eligibility, consumption, persistence and network parity audits remain tracked in the item implementation plan. |
| Skill-forget items | Legacy skill-level downgrade is implemented with per-item selected skills, eligibility rules and separate combat/sub-skill/horse refund pools. |
| Talisman/recall items | Need persistent destination sockets, delayed/cancellable travel and existing map-server transfer validation. |
| Binding/unbinding items | Need persistent binding state enforced by drop, trade, sell, refinement and equipment operations, including delayed unbinding. |
| Dragon Soul and special Dragon Soul | Need qualified character state, dedicated inventory and two equipment decks, activation/wear timers, generated attributes, table-driven refinement and UI. |
| Extract / time-charge items and Dragon Soul refining materials | Depend on the Dragon Soul system; implement after its inventory/state/transaction layer. |
| Recipe items | Implemented from the legacy `USE_RECIPE` values: ingredient and bottle requirements, result, and success chance are imported as named fields with typed item-class references. The server atomically validates and consumes required inputs; failed rolls still consume ingredients and one bottle, successful rolls grant one result, and the recipe item itself is retained. Missing inputs or no result capacity leave inventory unchanged. |
| Create-stone / remaining special-use items | `USE_CREATE_STONE` now grants one uniformly selected item from the legacy result range using typed item-class references, and consumes the source only when the server can atomically place the result. Other special-use variants still need explicit parameters and complete transactions where legacy behavior exists; retain legacy no-op entries where the original server has no behavior. |
| Yang, secondary coins | Currency acquisition and shop/payment behavior; no arbitrary right-click conversion. |
| None, container, material, resource, special, tool, lottery, auto-use | Preserve family identity. Resolve actual per-item behavior through the content's supported system/quest; unused enum names are not missing consumable actions. |

## Implementation order

1. Audit recipe and create-stone edge cases against the complete imported proto set; implement remaining targeted-item transactions.
2. Audit the implemented transformation system; implement item binding and recall with their dependent item actions.
3. Implement Dragon Soul inventory/decks, attributes/timers, refinement, extraction and charging.
4. Finish the equipment expiry/belt inventory audit and per-item quest/content checks. Keep
   unavailable/unused content explicit.

## Normal bonus-addition items

Standard addition item 71085 and fifth-attribute item 70024 own their policies in
`UMT2ItemUseAddAttributeTemplate` and `UMT2ItemUseAddAttribute2Template`. Inventory performs
the authoritative staged transaction and notifications. By default, the first item adds up to
four normal attributes; the second requires four and attempts the fifth. Success chances by
destination slot are 100%, 80%, 60%, 50%, 30%, matching legacy `aiItemAttributeAddPercent`.
Edit the five named percentages under Project Settings → Game → MT2 Item Bonuses → Rules →
Normal Bonus Addition. The settings use `NormalBonusAdditionChances` with separate
`FirstAttributeSuccessPercent` through `FifthAttributeSuccessPercent` fields; custom config
overrides must use these named fields.

A valid attempted addition consumes one source item on success or failure. Failure leaves the
target unchanged. Ineligible targets, missing eligible definitions, empty value distributions,
death and trading reject without consumption. Normal attributes cannot duplicate prototype
applies or another normal attribute; rare attributes retain their independent duplicate set.
Normal and rare value-level weight arrays are ordered: repeated entries, including zeroes,
must use Unreal config's `.` array-add syntax after an explicit clear, not unique-add `+`.

Rare addition 71051 and reroll 71052 use their dedicated `UseAddRareBonus` and
`UseChangeRareBonuses` templates. Both choose uniformly among eligible rare types, matching
legacy `CItem::AddRareAttribute`; `SelectionWeight` affects normal types only. A zero rare
definition weight does not disable that definition: exclude an equipment family with its
eligibility flags or maximum-level map instead. Rare types cannot repeat another rare type,
but may match normal/prototype applies. Value levels remain independently configurable in
`RareValueLevelWeights` (level five by default), capped by each equipment-family maximum.
Invalid pools/distributions reject without consumption; an incomplete reroll restores the
original attributes. Successful use consumes one item. Regional availability is a separate
pending parity check, not inferred from selection behavior.

Beginner addition items 71152/76024 use `UMT2ItemBeginnerBonusAdditionTemplate`; beginner
reroll items 71151/76023 use `UMT2ItemBeginnerBonusRerollTemplate`. Each has a dedicated
header/implementation and inherits its ordinary addition/reroll policy. They accept only
weapons or body armor. Every level requirement on the target must be at or below
`BeginnerBonusMaximumRequiredLevel` (40 by default), editable under Game → MT2 Item Bonuses →
Rules → Beginner Bonus Items. This checks the equipment requirement, not the player's level;
equipment without a level requirement is eligible. Rejected use preserves both items.

The importer decodes these source IDs into native classes; gameplay does not check numeric
item IDs. To refresh only their four Blueprints with existing source/destination settings, run
`UnrealEditor-Cmd.exe UnrealLongju.uproject -run=MT2ConfigureItemBehaviors -BeginnerBonusesOnly -unattended -NullRHI`.
`-Preview` discovers and validates the selected records without saving.

Enchant Item (b), 76014, uses `UMT2ItemLevelChestBonusRerollTemplate`. It is a level-chest
variant, not a beginner-only item: it accepts the ordinary reroll equipment families without
the beginner level cap. Its value-level weights are 0/10/50/39/1, matching the legacy special
case, instead of the ordinary 0/10/40/35/15 distribution. Configure these under Game →
MT2 Item Bonuses → Rules → Level Chest Reroll. Weights address value levels one through five
and are capped by each bonus definition's supported level for the equipment family.

The concrete class chooses its probability policy; the ordinary reroll class preserves the
normal attribute count and rare attributes, while Inventory owns validation, consumption and
notifications. Existing proto anti-flags retain this item's trade restrictions. No new runtime
binding state, item-ID dispatch, save format or RPC is introduced. To refresh only this asset,
use `-run=MT2ImportItems -OnlyVnum=76014` with the existing configured import paths.

This is a bounded parity pass, not completion of every bonus item: shared reroll cooldowns,
other special distributions,
rare-region rules and zero-value addon slot accounting still
require the checks in OWN-01 of the implementation tracker.

Damage-addon generation stores only nonzero average/skill-damage results, matching the
legacy `ApplyAddonTo` → `AddAttribute` path. This applies to new addon equipment and normal
rerolls. A zero result does not allocate a bonus entry; the reroller fills remaining normal
slots from its configured pool while preserving rare attributes. Negative nonzero values
are retained. If the available pool cannot complete the reroll, the transaction rejects and
restores the original item without consumption. Existing typed-zero records, one-slot addon
rerolls and exact signed formula quantization still require OWN-01D2 verification; this
change does not migrate saved items or change the configurable damage formula.

## Instance sockets and equipment amplification

Common instance data contains count, expiry, presentation state and one typed family payload.
Equipment bonuses, Metin sockets, accessory sockets, rod proficiency/bait, pickaxe proficiency,
fish length, skill definitions, blend parameters and recovery reservoirs have separate named state
structs under `Items/State`. Initial family state is authored on the corresponding template, not
on the shared base. Item/skill references use class/object pickers. Refinement chains and fish
reward links use soft class references to avoid circular Blueprint loads.

Save JSON retains `socket_0` through `socket_2` for legacy compatibility. Native codecs translate
those columns and Lua `item.get_socket/set_socket` into the appropriate named state. Arbitrary
quest scratch values and unresolved legacy records stay in non-Blueprint compatibility structs;
they are not designer-facing reference selectors. Binary archives carry a custom version, with
readers for the previous flat record and its Blueprint default text. Missing definitions must not
cause opaque records to be discarded. Stack identity includes socket values; transferring or
restoring an item must not reroll an existing blend.
Restoring a valid blend with all three sockets zero initializes its first roll; restoring nonzero
socket data never rerolls it.
If a template or registry is unavailable, normalization preserves the existing socket representation
instead of inferring that the item is numeric and discarding its equipment data.

## Skill-forget items

Skill-forget items retain the old selected skill in legacy socket 0, represented at runtime by a
typed skill-definition reference. Newly generated standard items choose from the configured
standard vnum range; the anti-skill item uses its separate configured range. The selected skill is
stored on the item once and is never rerolled when loading an existing item. Both item vnums and
skill ranges are editable in Project Settings → Gameplay → Skills → Skill Forget Items.

Using the item is server-authoritative and follows the legacy `CHARACTER::SkillLevelDown` gates:
the character must have selected a skill group, the skill definition must exist and be learnable
for the character, the skill must have a nonzero level below normal mastery, and polymorph blocks
the action. Anti-skill alternatives remain mutually exclusive within their legacy families. A
successful use reduces exactly one level and awards one point to the matching combat, sub-skill or
horse-skill pool. A failed downgrade leaves the item intact; a malformed unassigned item is
destroyed and reports failure, as in `CHARACTER::UseItemEx`.

Combat points use the existing player field. Sub-skill and horse-skill points are owner-replicated
and saved in a separate `player_skill_points` table so existing databases migrate without altering
the original `players` table. Old saves without this row load both pools as zero.

Energy sums normal/rare runtime item attributes by APPLY across the eligible worn slots, then
truncates `sum * percent / 100` once per APPLY. It does not amplify prototype applies, weapon base
damage or socket stones. Equipment changes and affect changes rebuild the bonuses. Eligible slots
are editable under Gameplay → Items → Attribute Amplification.

## Fish and campfires

The item importer reads `fishing.txt`, preserving its ASCII column separators despite CP949 names,
and ignores its spreadsheet totals footer. Localized display names still come from the item proto.
Opening a live fish rolls over 10,000 outcomes: 6,000 dead-fish outcomes, 2,000 fishbone outcomes,
and the remaining 2,000 distributed among the ten source use results. Empty/water-stone/treasure-map
results intentionally award nothing, as in the reference server. Dead fish cannot be opened.

Campfire item 27600 spawns the stationary NPC 12000 in front of the player on valid, dry ground.
The authoritative actor expires after 40 seconds by default. Placement distance, lifetime, trace
extents, collision and imported log mesh/fire effect are editable in Project Settings → Metin2
Fishing → Campfire. Visual assets load only on rendering instances, not on dedicated servers.
Giving a supported live/dead fish stack to a nearby campfire exchanges the whole stack for its
table-defined roasted item. Cooked fish use their normal consumable subtype.

Conversions plan output placement before committing inventory changes. Missing definitions,
insufficient space, death, remote interaction and trading reject the action without taking fish.
This deliberately avoids losing the input or partially delivering rewards when inventory is full.

`MT2ConfigureItemBehaviors` refreshes only blend, fish, campfire and affected consumable Blueprints;
`-Preview` validates/displays the selection without saving assets. It uses configured legacy source
paths; packaged runtime never reads those external tables.

`MT2ItemAuthoringMigration` snapshots item assets and their complete defaults under
`Saved/ItemAuthoringMigration`. Its `-Apply` phase validates family defaults against that snapshot,
resolves typed references after all item classes exist, compiles the Blueprints and saves them.
It retains the original backups. Normal imports refresh source-owned links and can resolve targets
from the existing registry during a partial import.

## Sources and verification

Primary server sources: `game/src/char_item.cpp`, `char_skill.cpp`, `item.cpp`, `char_affect.cpp`, `blend_item.cpp`,
`polymorph.cpp`, `dragon_soul.cpp`, `char_dragonsoul.cpp`, `fishing.cpp` and `common/enums.h`.
Tables: `share/locale/italy/blend.txt`, `dragon_soul_table.txt` and quest scripts.
Primary client sources: `source/UserInterface/InstanceBase*`, `PythonPlayer*`, item protocol handling
and `uitooltip.py`/inventory/Dragon Soul windows in the client scripts.

Reference text proto inspected for the audit contains 5,743 rows. It is research input, not the
runtime registry: imported/cooked item Blueprints and their actual generated data determine what can
be played. The audit must not equate the number of C++ family classes with functional item coverage.
