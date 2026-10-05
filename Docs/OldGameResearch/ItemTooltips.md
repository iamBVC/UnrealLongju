# Item tooltips (old client, uitooltip.py)

`ToolTipItem.AddItemData(vnum, ...)` builds the hovering tooltip by **item type**, appending text
lines top-to-bottom. One dispatch (`if item.ITEM_TYPE_X == itemType`) per type.

## Colors (ToolTipItem class constants)
- NORMAL_COLOR gray `(0.76,0.76,0.76)` - names, neutral text
- POSITIVE_COLOR green `(0.54,0.72,0.56)` - attack power, positive stat bonuses
- NEGATIVE_COLOR red `(0.90,0.47,0.46)` - negative bonuses, unmet requirements
- SPECIAL_TITLE_COLOR gold `(1.0,0.78,0.0)` - special items / high price
- `GetChangeTextLineColor(v)` = green if v>0 else red.
- `GetLimitTextLineColor(cur, req)` = red if cur < req (requirement not met) else normal.

## Common sections (helpers)
- `__AppendLimitInformation`: for each item LIMIT (LIMIT_LEVEL mainly), "Required level N",
  colored red if the player's level < N.
- `__AppendAffectInformation`: for each of the 3 applies, "STR +5" / "Movement Speed +10%" via an
  APPLY→locale map, colored by sign.
- `AppendWearableInformation`: which races may wear it (from antiflags), white if wearable.
- `__AppendMetinSlotInfo`: Metin socket boxes. Runtime socket/stone operations exist; complete tooltip/legacy socket presentation should be checked against current item data rather than assumed absent.

## Per-type layout
- WEAPON: limits, space, **Attack Power min-max** (Values[3]+[5] .. Values[4]+[5], green),
  **Magic Attack** (Values[1]+[5] .. Values[2]+[5]); Fan shows magic first. Then affects, attribute
  (random attrs), wearable, metin slots.
- ARMOR: limits, **Defense** (Values[1]+Values[5]*2, green), magic defense, affects, wearable,
  metin slots.
- RING / BELT / accessory: limits, affects (accessories have their own metin material).
- USE: limits; potion/ability sub-info; description. Auto-potions, warp scrolls etc. show remaining
  amounts (metin slot data) - not modeled here.
- METIN / FISH / BLEND / MATERIAL / QUEST / UNIQUE: name + description (+ any affects).

Every tooltip starts with the item **name** (title color varies by rarity/antiflag) and, for many,
a **description** line. Weight and price are NOT in the base game tooltip - price is only shown in
shop context, which is our addition (green if affordable, red if not).

## UE recreation
- One `UMT2ItemTooltipWidget` subclass per item family (weapon/armor/accessory/use/default).
- The template picks its class: `UMT2ItemTemplate::GetDefaultTooltipClass()` (virtual, overridden
  per template subclass) with an optional `TooltipWidgetClassOverride` soft-class UPROPERTY so a
  designer can point a template at a bespoke Blueprint tooltip.
- The tooltip resolver instantiates that class and calls `BuildTooltip(Template, Count, Viewer)`.
- Weapon damage math reuses `UMT2ItemWeaponTemplate::GetPhysical/MagicDamageMin/Max`; defense uses
  Values[1] + Values[5]*2; applies use the shared APPLY-ordinal names; limits colored vs the
  viewer's level.
