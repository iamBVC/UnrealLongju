# Legacy item-template variant catalog

Task dependencies, ownership cleanup, validation criteria and the per-class completion checklist are tracked in the [Item Classes Implementation Plan and TODO Tracker](superpowers/plans/2026-10-20-item-implementation-todo.md).

The importer selects a native Unreal item-template class from each supported legacy ITEM_TYPE and ITEM_SUBTYPE pair. Each concrete subtype class has its own header and implementation. Specialized behavior families, such as bonus consumables and item actions, provide intermediate bases where needed; the catalog test checks the actual immediate parent.

This catalog is based on the legacy server common/enums.h and db/src/ProtoReader.cpp subtype-name tables. The importer uses parsed numeric subtype ordinals, retaining source-table aliases. Enum sentinels such as ARMOR_NUM_TYPES are not behavior variants. Item types with no legacy subtype table use their existing family template directly.

The classes below establish import and authoring identity; their individual header TODOs mark behavior that still needs a focused legacy code-path port and tests. Existing bespoke behavior templates remain more-specific importer parents until their exact proto pairs are reconciled.

| Native class | Item type | Legacy subtype symbol | Raw subtype | Status |
|---|---|---|---:|---|
| UMT2ItemArmorBodyTemplate | Armor | ARMOR_BODY | 0 | Placeholder; behavior TODO |
| UMT2ItemArmorHeadTemplate | Armor | ARMOR_HEAD | 1 | Placeholder; behavior TODO |
| UMT2ItemArmorShieldTemplate | Armor | ARMOR_SHIELD | 2 | Placeholder; behavior TODO |
| UMT2ItemArmorWristTemplate | Armor | ARMOR_WRIST | 3 | Placeholder; behavior TODO |
| UMT2ItemArmorFootwearTemplate | Armor | ARMOR_FOOTS | 4 | Placeholder; behavior TODO |
| UMT2ItemArmorNeckTemplate | Armor | ARMOR_NECK | 5 | Placeholder; behavior TODO |
| UMT2ItemArmorEarTemplate | Armor | ARMOR_EAR | 6 | Placeholder; behavior TODO |
| UMT2ItemAutoUsePotionTemplate | AutoUse | AUTOUSE_POTION | 0 | Placeholder; behavior TODO |
| UMT2ItemAutoUseAbilityUpTemplate | AutoUse | AUTOUSE_ABILITY_UP | 1 | Placeholder; behavior TODO |
| UMT2ItemAutoUseBombTemplate | AutoUse | AUTOUSE_BOMB | 2 | Placeholder; behavior TODO |
| UMT2ItemAutoUseGoldTemplate | AutoUse | AUTOUSE_GOLD | 3 | Placeholder; behavior TODO |
| UMT2ItemAutoUseMoneybagTemplate | AutoUse | AUTOUSE_MONEYBAG | 4 | Placeholder; behavior TODO |
| UMT2ItemAutoUseTreasureBoxTemplate | AutoUse | AUTOUSE_TREASURE_BOX | 5 | Placeholder; behavior TODO |
| UMT2ItemCostumeBodyTemplate | Costume | COSTUME_BODY | 0 | Placeholder; behavior TODO |
| UMT2ItemCostumeHairTemplate | Costume | COSTUME_HAIR | 1 | Placeholder; behavior TODO |
| UMT2ItemDragonSoulSlot1Template | DragonSoul | DS_SLOT1 | 0 | Placeholder; behavior TODO |
| UMT2ItemDragonSoulSlot2Template | DragonSoul | DS_SLOT2 | 1 | Placeholder; behavior TODO |
| UMT2ItemDragonSoulSlot3Template | DragonSoul | DS_SLOT3 | 2 | Placeholder; behavior TODO |
| UMT2ItemDragonSoulSlot4Template | DragonSoul | DS_SLOT4 | 3 | Placeholder; behavior TODO |
| UMT2ItemDragonSoulSlot5Template | DragonSoul | DS_SLOT5 | 4 | Placeholder; behavior TODO |
| UMT2ItemDragonSoulSlot6Template | DragonSoul | DS_SLOT6 | 5 | Placeholder; behavior TODO |
| UMT2ItemExtractDragonSoulTemplate | Extract | EXTRACT_DRAGON_SOUL | 0 | Placeholder; behavior TODO |
| UMT2ItemExtractDragonHeartTemplate | Extract | EXTRACT_DRAGON_HEART | 1 | Placeholder; behavior TODO |
| UMT2ItemFishAliveTemplate | Fish | FISH_ALIVE | 0 | Placeholder; behavior TODO |
| UMT2ItemFishDeadTemplate | Fish | FISH_DEAD | 1 | Placeholder; behavior TODO |
| UMT2ItemLotteryTicketTemplate | Lottery | LOTTERY_TICKET | 0 | Placeholder; behavior TODO |
| UMT2ItemLotteryInstantTemplate | Lottery | LOTTERY_INSTANT | 1 | Placeholder; behavior TODO |
| UMT2ItemMaterialLeatherTemplate | Material | MATERIAL_LEATHER | 0 | Placeholder; behavior TODO |
| UMT2ItemMaterialBloodTemplate | Material | MATERIAL_BLOOD | 1 | Placeholder; behavior TODO |
| UMT2ItemMaterialRootTemplate | Material | MATERIAL_ROOT | 2 | Placeholder; behavior TODO |
| UMT2ItemMaterialNeedleTemplate | Material | MATERIAL_NEEDLE | 3 | Placeholder; behavior TODO |
| UMT2ItemMaterialJewelTemplate | Material | MATERIAL_JEWEL | 4 | Placeholder; behavior TODO |
| UMT2ItemMaterialDragonSoulRefineNormalTemplate | Material | MATERIAL_DS_REFINE_NORMAL | 5 | Placeholder; behavior TODO |
| UMT2ItemMaterialDragonSoulRefineBlessedTemplate | Material | MATERIAL_DS_REFINE_BLESSED | 6 | Placeholder; behavior TODO |
| UMT2ItemMaterialDragonSoulRefineHolyTemplate | Material | MATERIAL_DS_REFINE_HOLLY | 7 | Placeholder; behavior TODO |
| UMT2ItemMetinNormalTemplate | Metin | METIN_NORMAL | 0 | Placeholder; behavior TODO |
| UMT2ItemMetinGoldTemplate | Metin | METIN_GOLD | 1 | Placeholder; behavior TODO |
| UMT2ItemResourceFishboneTemplate | Resource | RESOURCE_FISHBONE | 0 | Placeholder; behavior TODO |
| UMT2ItemResourceWaterstonePieceTemplate | Resource | RESOURCE_WATERSTONEPIECE | 1 | Placeholder; behavior TODO |
| UMT2ItemResourceWaterstoneTemplate | Resource | RESOURCE_WATERSTONE | 2 | Placeholder; behavior TODO |
| UMT2ItemResourceBloodPearlTemplate | Resource | RESOURCE_BLOOD_PEARL | 3 | Placeholder; behavior TODO |
| UMT2ItemResourceBluePearlTemplate | Resource | RESOURCE_BLUE_PEARL | 4 | Placeholder; behavior TODO |
| UMT2ItemResourceWhitePearlTemplate | Resource | RESOURCE_WHITE_PEARL | 5 | Placeholder; behavior TODO |
| UMT2ItemResourceBucketTemplate | Resource | RESOURCE_BUCKET | 6 | Placeholder; behavior TODO |
| UMT2ItemResourceCrystalTemplate | Resource | RESOURCE_CRYSTAL | 7 | Placeholder; behavior TODO |
| UMT2ItemResourceGemTemplate | Resource | RESOURCE_GEM | 8 | Placeholder; behavior TODO |
| UMT2ItemResourceStoneTemplate | Resource | RESOURCE_STONE | 9 | Placeholder; behavior TODO |
| UMT2ItemResourceMetinTemplate | Resource | RESOURCE_METIN | 10 | Placeholder; behavior TODO |
| UMT2ItemResourceOreTemplate | Resource | RESOURCE_ORE | 11 | Placeholder; behavior TODO |
| UMT2ItemSpecialMapTemplate | Special | SPECIAL_MAP | 0 | Placeholder; behavior TODO |
| UMT2ItemSpecialKeyTemplate | Special | SPECIAL_KEY | 1 | Placeholder; behavior TODO |
| UMT2ItemSpecialDocumentTemplate | Special | SPECIAL_DOC | 2 | Placeholder; behavior TODO |
| UMT2ItemSpecialSpiritTemplate | Special | SPECIAL_SPIRIT | 3 | Placeholder; behavior TODO |
| UMT2ItemSpecialDragonSoulSlot1Template | SpecialDragonSoul | DS_SLOT1 | 0 | Placeholder; behavior TODO |
| UMT2ItemSpecialDragonSoulSlot2Template | SpecialDragonSoul | DS_SLOT2 | 1 | Placeholder; behavior TODO |
| UMT2ItemSpecialDragonSoulSlot3Template | SpecialDragonSoul | DS_SLOT3 | 2 | Placeholder; behavior TODO |
| UMT2ItemSpecialDragonSoulSlot4Template | SpecialDragonSoul | DS_SLOT4 | 3 | Placeholder; behavior TODO |
| UMT2ItemSpecialDragonSoulSlot5Template | SpecialDragonSoul | DS_SLOT5 | 4 | Placeholder; behavior TODO |
| UMT2ItemSpecialDragonSoulSlot6Template | SpecialDragonSoul | DS_SLOT6 | 5 | Placeholder; behavior TODO |
| UMT2ItemToolFishingRodTemplate | Tool | TOOL_FISHING_ROD | 0 | Placeholder; behavior TODO |
| UMT2ItemUniqueNoneTemplate | Unique | UNIQUE_NONE | 0 | Placeholder; behavior TODO |
| UMT2ItemUniqueBookTemplate | Unique | UNIQUE_BOOK | 1 | Placeholder; behavior TODO |
| UMT2ItemUniqueSpecialRideTemplate | Unique | UNIQUE_SPECIAL_RIDE | 2 | Placeholder; behavior TODO |
| UMT2ItemUniqueSpecialMountRideTemplate | Unique | UNIQUE_3 (legacy enum value UNIQUE_SPECIAL_MOUNT_RIDE) | 3 | Placeholder; behavior TODO |
| UMT2ItemUniqueLegacySubtype4Template | Unique | UNIQUE_4 | 4 | Placeholder; behavior TODO |
| UMT2ItemUniqueLegacySubtype5Template | Unique | UNIQUE_5 | 5 | Placeholder; behavior TODO |
| UMT2ItemUniqueLegacySubtype6Template | Unique | UNIQUE_6 | 6 | Placeholder; behavior TODO |
| UMT2ItemUniqueLegacySubtype7Template | Unique | UNIQUE_7 | 7 | Placeholder; behavior TODO |
| UMT2ItemUniqueLegacySubtype8Template | Unique | UNIQUE_8 | 8 | Placeholder; behavior TODO |
| UMT2ItemUniqueLegacySubtype9Template | Unique | UNIQUE_9 | 9 | Placeholder; behavior TODO |
| UMT2ItemUniqueSpecialUseTemplate | Unique | USE_SPECIAL accepted in ITEM_UNIQUE table | 10 | Placeholder; behavior TODO |
| UMT2ItemUsePotionTemplate | Use | USE_POTION | 0 | Placeholder; behavior TODO |
| UMT2ItemUseTalismanTemplate | Use | USE_TALISMAN | 1 | Placeholder; behavior TODO |
| UMT2ItemUseTuningTemplate | Use | USE_TUNING | 2 | Placeholder; behavior TODO |
| UMT2ItemUseMoveTemplate | Use | USE_MOVE | 3 | Placeholder; behavior TODO |
| UMT2ItemUseTreasureBoxTemplate | Use | USE_TREASURE_BOX | 4 | Placeholder; behavior TODO |
| UMT2ItemUseMoneybagTemplate | Use | USE_MONEYBAG | 5 | Placeholder; behavior TODO |
| UMT2ItemUseBaitTemplate | Use | USE_BAIT | 6 | Implemented as a native item action; imports Value0 into subtype-owned BaitPower, requires an equipped rod and no active fishing attempt, replaces rod bait, and consumes one bait. |
| UMT2ItemUseAbilityUpTemplate | Use | USE_ABILITY_UP | 7 | Placeholder; behavior TODO |
| UMT2ItemUseAffectTemplate | Use | USE_AFFECT | 8 | Placeholder; behavior TODO |
| UMT2ItemUseCreateStoneTemplate | Use | USE_CREATE_STONE | 9 | Uniformly selects one imported stone variant through typed template references; legacy VNUM-range offsets are explicit; source is consumed only when result delivery succeeds. |
| UMT2ItemUseSpecialTemplate | Use | USE_SPECIAL | 10 | Placeholder; behavior TODO |
| UMT2ItemUsePotionNoDelayTemplate | Use | USE_POTION_NODELAY | 11 | Placeholder; behavior TODO |
| UMT2ItemUseClearTemplate | Use | USE_CLEAR | 12 | Placeholder; behavior TODO |
| UMT2ItemUseInvisibilityTemplate | Use | USE_INVISIBILITY | 13 | Placeholder; behavior TODO |
| UMT2ItemUseDetachmentTemplate | Use | USE_DETACHMENT | 14 | Placeholder; behavior TODO |
| UMT2ItemUseBucketTemplate | Use | USE_BUCKET | 15 | Placeholder; behavior TODO |
| UMT2ItemUsePotionContinueTemplate | Use | USE_POTION_CONTINUE | 16 | Placeholder; behavior TODO |
| UMT2ItemUseCleanSocketTemplate | Use | USE_CLEAN_SOCKET | 17 | Placeholder; behavior TODO |
| UMT2ItemUseChangeAttributeTemplate | Use | USE_CHANGE_ATTRIBUTE | 18 | Class-owned normal reroll; beginner items use the specialized override below. Shared cooldowns, special distributions and addon slot parity remain pending (OWN-01). |
| UMT2ItemUseAddAttributeTemplate | Use | USE_ADD_ATTRIBUTE | 19 | Class-owned addition policy; standard item 71085 tested. Per-slot chances, prototype-apply exclusion and failure consumption implemented. Beginner variants use the specialized override below; zero-value addon slot accounting remains pending (OWN-01). |
| UMT2ItemUseAddAccessorySocketTemplate | Use | USE_ADD_ACCESSORY_SOCKET | 20 | Placeholder; behavior TODO |
| UMT2ItemUsePutIntoAccessorySocketTemplate | Use | USE_PUT_INTO_ACCESSORY_SOCKET | 21 | Placeholder; behavior TODO |
| UMT2ItemUseAddAttribute2Template | Use | USE_ADD_ATTRIBUTE2 | 22 | Class-owned fifth-attribute policy; item 70024 tested. Requires four normal attributes by default; configured fifth-slot chance and consume-on-failure implemented. Zero-value addon slot accounting remains pending (OWN-01). |
| UMT2ItemUseRecipeTemplate | Use | USE_RECIPE | 23 | Atomic legacy transaction using typed ingredient, bottle and result references; failed rolls consume ingredients and bottle, recipe item remains. |
| UMT2ItemUseChangeAttribute2Template | Use | USE_CHANGE_ATTRIBUTE2 | 24 | Placeholder; behavior TODO |
| UMT2ItemUseBindTemplate | Use | USE_BIND | 25 | Placeholder; behavior TODO |
| UMT2ItemUseUnbindTemplate | Use | USE_UNBIND | 26 | Placeholder; behavior TODO |
| UMT2ItemUseTimeChargePercentTemplate | Use | USE_TIME_CHARGE_PER | 27 | Placeholder; behavior TODO |
| UMT2ItemUseTimeChargeFixedTemplate | Use | USE_TIME_CHARGE_FIX | 28 | Placeholder; behavior TODO |
| UMT2ItemUsePutIntoBeltSocketTemplate | Use | USE_PUT_INTO_BELT_SOCKET | 29 | Placeholder; behavior TODO |
| UMT2ItemUsePutIntoRingSocketTemplate | Use | USE_PUT_INTO_RING_SOCKET | 30 | Placeholder; behavior TODO |
| UMT2ItemWeaponSwordTemplate | Weapon | WEAPON_SWORD | 0 | Placeholder; behavior TODO |
| UMT2ItemWeaponDaggerTemplate | Weapon | WEAPON_DAGGER | 1 | Placeholder; behavior TODO |
| UMT2ItemWeaponBowTemplate | Weapon | WEAPON_BOW | 2 | Placeholder; behavior TODO |
| UMT2ItemWeaponTwoHandedTemplate | Weapon | WEAPON_TWO_HANDED | 3 | Placeholder; behavior TODO |
| UMT2ItemWeaponBellTemplate | Weapon | WEAPON_BELL | 4 | Placeholder; behavior TODO |
| UMT2ItemWeaponFanTemplate | Weapon | WEAPON_FAN | 5 | Placeholder; behavior TODO |
| UMT2ItemWeaponArrowTemplate | Weapon | WEAPON_ARROW | 6 | Placeholder; behavior TODO |
| UMT2ItemWeaponMountSpearTemplate | Weapon | WEAPON_MOUNT_SPEAR | 7 | Placeholder; behavior TODO |

## Beginner bonus-item overrides

These are specific classes for content variants, not additional legacy subtype ordinals.

| Native class | Parent | Imported items | Current policy / remaining work |
| --- | --- | --- | --- |
| UMT2ItemBeginnerBonusAdditionTemplate | UMT2ItemUseAddAttributeTemplate | 71152, 76024 | Weapon/body-only; all target level requirements must be within the configured cap (default 40). Ordinary addition chance/transaction inherited. Shared addon slot audit remains pending. |
| UMT2ItemBeginnerBonusRerollTemplate | UMT2ItemUseChangeAttributeTemplate | 71151, 76023 | Same target restriction; ordinary reroll/count/rare preservation inherited. Shared reroll cooldown and addon audits remain pending. |

## Legacy parser edge cases

- ITEM_UNIQUE accepts ordinals 0–10 from its parser table, including placeholder aliases UNIQUE_4 through UNIQUE_9 and USE_SPECIAL. They remain distinct variants; undocumented behavior is not inferred.
- Legacy ordinal 3 is represented by the server enum as UNIQUE_SPECIAL_MOUNT_RIDE, while the parser token is UNIQUE_3. The native variant uses the server enum meaning and documents the mismatch.
- ARMOR_NUM_TYPES is a parser-array sentinel, not an armor behavior class, and remains rejected.
- Subtype ordinals are scoped to their item family; they are never shared across families.

