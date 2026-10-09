# Old Game Research: Items, item_proto, Inventory & Equipment Model

Sources studied: `client_src/source/common/tables.h` (SItemTable), `client_src/source/common/enums.h`
(EItemTypes, EWeaponSubTypes, EArmorSubTypes, EWearPositions, EApplyTypes),
`client_src/source/GameLib/ItemManager.cpp`, `Dumps/GF_v21.4.11/root/uiscript/inventorywindow.py`,
`Dumps/my_dump/locale/it/item_proto` + `item_list.txt`.

## Historical item_proto record (TItemTable, 162 bytes packed)

Key fields: vnum, name[25], localeName[25], type, subType, weight, **size** (grid height in cells,
1-3), antiFlags, flags, **wearFlags**, immuneFlags, gold, shopBuyPrice, refinedVnum, refineSet,
magicPct, `limits[2]{type,value}`, **`applies[3]{type,value}`**, `values[6]`, `sockets[3]`,
specular, gainSocketPct, addonType. The `my_dump` locale item_proto is a **raw unencrypted array**
of these records; the GF dumps use a MIPX+TEA+Snappy container instead (reader in
`GameLib/ItemManager.cpp:255` with key at `:7`).

This record describes legacy data. The current `MT2ItemProtoReader` accepts MIPX v1 with 156-byte 40250 records and TEA/LZO payloads. Raw 162-byte and Snappy variants are not supported by that reader. See [MobProtoFormats](MobProtoFormats.md) for current format constraints.

- **Weapons**: the current template/tooltip convention uses `values[3]/values[4]` for physical attack and `values[1]/values[2]` for magic attack, with `values[5]` refinement addition.
- **Armor**: `values[1]` = defense.
- **Applies**: up to 3 `{EApplyTypes ordinal, value}` stat bonuses granted while equipped — same
  ordinals used by affects (APPLY_MAX_HP=1, CON=3, INT=4, STR=5, DEX=6, ATT_SPEED=7, MOV_SPEED=8,
  DEF_GRADE_BONUS=54, ATT_GRADE_BONUS=53, full list `enums.h:441-536`).
- `item_list.txt` maps vnum → icon path (+ optional world model for weapons); armor rows have no
  model because **body armor swaps the whole character body mesh** (per race/sex variants like
  `pc/warrior/warrior_novice`), it is not an attached mesh.

## Inventory & equipment slots

From `uiscript/inventorywindow.py`:

- The bag is a flat slot array shown 45 cells per page (5 columns × 9 rows), multiple pages.
- A multi-cell item (size 2-3) occupies N vertically-stacked cells in the same column; it is stored
  at its **top** cell and its icon (32×32N) is drawn overflowing downward over the covered cells.
- Equipment slots are the same slot system at index `90 + EWearPositions`:
  BODY=0, HEAD=1, FOOTS=2, WRIST=3, WEAPON=4, NECK=5, EAR=6, UNIQUE1/2=7/8, ARROW=9, SHIELD=10,
  BELT=23 (`enums.h:82-107`). An item's `wearFlags` bit `1 << position` says where it can go.
- Interactions: drag to move/swap; right-click (or drag onto the equip slot) to equip; equipping
  swaps the previously worn item back into the bag slot.

## UE recreation mapping

- `FMT2ItemDefinition`/`UMT2ItemTemplate` mirror the proto fields (Values, Applies, WearFlags,
  InventorySize). Importer: `MT2ItemProtoReader`/`MT2ItemImporter` → one Blueprint per vnum under
  `/Game/Items/Blueprints`, registered in the VNUM registry.
- `UMT2InventoryComponent` = replicated four-page grid (45 cells per page, 180 total) + wear-position-indexed equipment array;
  server-authoritative Move/Equip/Unequip RPCs on `AMT2PlayerCharacter`.
- Equip applies weapon attached mesh / armor body-mesh swap + recomputes stat bonuses from
  Values+Applies of all worn items (`RecalculateEquipmentStats`).
