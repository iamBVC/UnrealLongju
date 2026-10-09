# Old Game Research: mob_proto Binary Format Variants

Sources studied: `Sources/40250 client sources/source/UserInterface/PythonNonPlayer.h/.cpp`
(the client build that reads the newer GF-style protos), plus the classic
`my_server/client_src/source/common/tables.h`.

## Container

All MMPT mob_proto files share the container: `'MMPT'` fourcc, element count, payload size, then a
TEA-encrypted + compressed blob of packed records (`PythonNonPlayer.cpp:7-56`). The payload must
divide evenly by `sizeof(TMobTable)` — the client validates exactly that, nothing more, which means
**a wrong-but-same-size record layout decodes without any error while producing garbage fields**.

## Record layout variants (all `#pragma pack(1)`)

Legacy data uses several record layouts. The current importer supports only the 40250/255-byte format from `<SourceRoot>/locale/en/` protos, using MMPT + TEA + LZO (`MCOZ`).

| Variant | Size | Status | Distinguishing traits |
|---|---|---|---|
| Classic raw | 291 | **unsupported** | `bRank` before `bType`; flags+empire+folder[101] before stats; gold/exp/HP mid-record |
| GF v21/v25 | 292 | **unsupported** | Extended: scale percent, elemental attacks, extra resists, hit range, folder[65] |
| **40250 client** | **255** | **the only supported format** | See below |

Likewise `item_proto` is MIPX v1 only (TEA + LZO `MCOZ`, 156-byte records matching the 40250
`CItemData::TItemTable`); the header carries an explicit stride that the reader validates.
Verified against `my_dump/locale/en/item_proto`: stride 0x9C (156), 5743 rows.

## The 255-byte 40250 layout (PythonNonPlayer.h:57)

Order (verified against the actual client source — do NOT assume the classic order):

1. `vnum:u32`, `name[25]`, `localeName[25]`
2. `type:u8`, `rank:u8` ← **type first** (classic has rank first)
3. `battleType:u8`, `level:u8`, `size:u8`
4. **`goldMin:u32`, `goldMax:u32`, `exp:u32`, `maxHP:u32`, `regenCycle:u8`, `regenPercent:u8`,
   `def:u16`** ← economy/HP block immediately after size (classic keeps it mid-record)
5. `aiFlag:u32`, `raceFlag:u32`, `immuneFlag:u32` (no mount-capacity byte in between)
6. `str/dex/con/int : u8×4`, `damageRange:u32×2`
7. `attackSpeed:s16`, `movingSpeed:s16`, `aggressiveHPPct:u8`, `aggressiveSight:u16`,
   `attackRange:u16`
8. `enchants[6]`, `resists[11]` (curse/slow/poison/stun/crit/penetrate; sword/twohand/dagger/bell/
   fan/bow/fire/elect/magic/wind/poison)
9. `resurrectionVnum:u32`, `dropItemVnum:u32` ← resurrection first (classic is reversed)
10. `mountCapacity:u8`, `onClickType:u8`, `empire:u8`, `folder[65]`
11. `damMultiply:f32`, `summonVnum:u32`, `drainSP:u32`, `monsterColor:u32`, `polymorphItemVnum:u32`
12. `skills[5]{vnum:u32, level:u8}`, then berserk/stoneSkin/godSpeed/deathBlow/revive points (u8×5)

Total: 255 bytes. Constants in this build: name 24+1, enchants 6, resists 11, skills 5.

## Lesson learned (the wrong-HP bug)

Our first `FMobProtoRecord255` guessed "classic order squeezed into 255 bytes" — the size matched,
the static_assert passed, records parsed, and every field after `bSize` was silently wrong (HP read
from the middle of the flags/folder region). When adding a new proto variant, always locate the
exact client build that consumes the file and copy its struct order verbatim; size equality proves
nothing.

## item_proto note

The same 40250 build's `CItemData::TItemTable` (`GameLib/ItemData.h:381`) also differs from classic
(vnumRange at offset 4, buy/sell prices after immuneFlag, refine fields at the tail). Its MIPX
container carries an explicit `stride` field which the reader validates, so layout drift there is
detected instead of silent.
