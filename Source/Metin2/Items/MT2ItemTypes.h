/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "CoreMinimal.h"
#include "MT2ItemTypes.generated.h"

// Old item_proto masks remain int32 at runtime. These enums expose their bits as named editor
// checkboxes without changing serialized values or importer compatibility.
UENUM(meta = (Bitflags))
enum class EMT2ItemFlag : uint8
{
	Refineable = 0,
	Save = 1,
	Stackable = 2,
	CountPerGold = 3,
	SlowQuery = 4,
	Rare = 5,
	Unique = 6,
	MakeCount = 7,
	Irremovable = 8,
	ConfirmWhenUse = 9,
	QuestUse = 10,
	QuestUseMultiple = 11,
	QuestGive = 12,
	Log = 13,
	Applicable = 14
};

UENUM(meta = (Bitflags))
enum class EMT2ItemAntiFlag : uint8
{
	Female = 0,
	Male = 1,
	Warrior = 2,
	Assassin = 3,
	Sura = 4,
	Shaman = 5,
	Get = 6,
	Drop = 7,
	Sell = 8,
	Shinsoo = 9,
	Chunjo = 10,
	Jinno = 11,
	Save = 12,
	Give = 13,
	PlayerKillDrop = 14,
	Stack = 15,
	PrivateShop = 16,
	Safebox = 17
};

UENUM(meta = (Bitflags))
enum class EMT2ItemImmuneFlag : uint8
{
	Paralysis = 0,
	Curse = 1,
	Stun = 2,
	Sleep = 3,
	Slow = 4,
	Poison = 5,
	Terror = 6
};

UENUM(meta = (Bitflags))
enum class EMT2ItemWearFlag : uint8
{
	Body = 0,
	Head = 1,
	Foots = 2,
	Wrist = 3,
	Weapon = 4,
	Neck = 5,
	Ear = 6,
	Unique1 = 7,
	Unique2 = 8,
	Arrow = 9,
	Shield = 10,
	Ability1 = 11,
	Ability2 = 12,
	Ability3 = 13,
	Ability4 = 14,
	Ability5 = 15,
	Ability6 = 16,
	Ability7 = 17,
	Ability8 = 18,
	CostumeBody = 19,
	CostumeHair = 20,
	Ring1 = 21,
	Ring2 = 22,
	Belt = 23
};

UENUM(BlueprintType)
enum class EMT2RefinementResult : uint8
{
	Invalid,
	Success,
	FailedDestroyed,
	FailedDowngraded,
	FailedAtMinimum,
	NotEnoughYang,
	MissingMaterials,
	NoInventorySpace
};

USTRUCT(BlueprintType)
struct METIN2_API FMT2RefinementMaterial
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Refinement", meta = (ClampMin = "1"))
	int32 ItemVnum = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Refinement", meta = (ClampMin = "1"))
	int32 Count = 1;
};

USTRUCT(BlueprintType)
struct METIN2_API FMT2RefinementRecipe
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Refinement", meta = (ClampMin = "0"))
	int64 YangCost = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Refinement",
		meta = (ClampMin = "0", ClampMax = "100", Units = "%"))
	int32 SuccessPercent = -1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Refinement")
	TArray<FMT2RefinementMaterial> Materials;

	bool IsValid() const { return SuccessPercent >= 0; }
};

class UMT2ItemMetinStoneTemplate;
class UMT2ItemTemplate;

UENUM(BlueprintType)
enum class EMT2ItemBonusKind : uint8
{
	Normal,
	Rare
};

// Exact EApplyTypes ordinals from the original client/server common/enums.h. Explicit values keep
// imported assets, replication and SQLite rows compatible while presenting readable editor choices.
UENUM(BlueprintType)
enum class EMT2ItemBonusType : uint8
{
	None = 0 UMETA(DisplayName = "None"),
	MaxHP = 1 UMETA(DisplayName = "Maximum HP"),
	MaxSP = 2 UMETA(DisplayName = "Maximum SP"),
	Constitution = 3 UMETA(DisplayName = "Constitution (VIT)"),
	Intelligence = 4 UMETA(DisplayName = "Intelligence (INT)"),
	Strength = 5 UMETA(DisplayName = "Strength (STR)"),
	Dexterity = 6 UMETA(DisplayName = "Dexterity (DEX)"),
	AttackSpeed = 7 UMETA(DisplayName = "Attack Speed"),
	MovementSpeed = 8 UMETA(DisplayName = "Movement Speed"),
	CastSpeed = 9 UMETA(DisplayName = "Casting Speed"),
	HPRegeneration = 10 UMETA(DisplayName = "HP Regeneration"),
	SPRegeneration = 11 UMETA(DisplayName = "SP Regeneration"),
	PoisonChance = 12 UMETA(DisplayName = "Poison Chance"),
	StunChance = 13 UMETA(DisplayName = "Stun Chance"),
	SlowChance = 14 UMETA(DisplayName = "Slow Chance"),
	CriticalChance = 15 UMETA(DisplayName = "Critical Hit Chance"),
	PenetratingChance = 16 UMETA(DisplayName = "Penetrating Hit Chance"),
	StrongAgainstHuman = 17 UMETA(DisplayName = "Strong Against Humans"),
	StrongAgainstAnimal = 18 UMETA(DisplayName = "Strong Against Animals"),
	StrongAgainstOrc = 19 UMETA(DisplayName = "Strong Against Orcs"),
	StrongAgainstMystic = 20 UMETA(DisplayName = "Strong Against Mystics"),
	StrongAgainstUndead = 21 UMETA(DisplayName = "Strong Against Undead"),
	StrongAgainstDevil = 22 UMETA(DisplayName = "Strong Against Devils"),
	StealHP = 23 UMETA(DisplayName = "HP Absorption"),
	StealSP = 24 UMETA(DisplayName = "SP Absorption"),
	ManaBurnChance = 25 UMETA(DisplayName = "Mana Burn Chance"),
	RecoverSPOnDamage = 26 UMETA(DisplayName = "Recover SP on Damage"),
	BlockChance = 27 UMETA(DisplayName = "Block Chance"),
	DodgeChance = 28 UMETA(DisplayName = "Dodge Chance"),
	SwordResistance = 29 UMETA(DisplayName = "Sword Resistance"),
	TwoHandResistance = 30 UMETA(DisplayName = "Two-Handed Sword Resistance"),
	DaggerResistance = 31 UMETA(DisplayName = "Dagger Resistance"),
	BellResistance = 32 UMETA(DisplayName = "Bell Resistance"),
	FanResistance = 33 UMETA(DisplayName = "Fan Resistance"),
	BowResistance = 34 UMETA(DisplayName = "Arrow Resistance"),
	FireResistance = 35 UMETA(DisplayName = "Fire Resistance"),
	LightningResistance = 36 UMETA(DisplayName = "Lightning Resistance"),
	MagicResistance = 37 UMETA(DisplayName = "Magic Resistance"),
	WindResistance = 38 UMETA(DisplayName = "Wind Resistance"),
	ReflectMelee = 39 UMETA(DisplayName = "Reflect Melee Damage"),
	ReflectCurse = 40 UMETA(DisplayName = "Reflect Curse"),
	PoisonResistance = 41 UMETA(DisplayName = "Poison Resistance"),
	RecoverSPOnKill = 42 UMETA(DisplayName = "Recover SP on Kill"),
	DoubleExperienceChance = 43 UMETA(DisplayName = "Double Experience Chance"),
	DoubleYangChance = 44 UMETA(DisplayName = "Double Yang Chance"),
	ItemDropBonus = 45 UMETA(DisplayName = "Item Drop Bonus"),
	PotionBonus = 46 UMETA(DisplayName = "Potion Effect Bonus"),
	RecoverHPOnKill = 47 UMETA(DisplayName = "Recover HP on Kill"),
	ImmuneStun = 48 UMETA(DisplayName = "Immune to Stun"),
	ImmuneSlow = 49 UMETA(DisplayName = "Immune to Slow"),
	ImmuneFall = 50 UMETA(DisplayName = "Immune to Knockdown"),
	Skill = 51 UMETA(DisplayName = "Skill"),
	BowDistance = 52 UMETA(DisplayName = "Bow Distance"),
	AttackValue = 53 UMETA(DisplayName = "Attack Value"),
	DefenseBonus = 54 UMETA(DisplayName = "Defense Bonus"),
	MagicAttackValue = 55 UMETA(DisplayName = "Magic Attack Value"),
	MagicDefenseValue = 56 UMETA(DisplayName = "Magic Defense Value"),
	CurseChance = 57 UMETA(DisplayName = "Curse Chance"),
	MaxStamina = 58 UMETA(DisplayName = "Maximum Stamina"),
	StrongAgainstWarrior = 59 UMETA(DisplayName = "Strong Against Warriors"),
	StrongAgainstAssassin = 60 UMETA(DisplayName = "Strong Against Assassins"),
	StrongAgainstSura = 61 UMETA(DisplayName = "Strong Against Sura"),
	StrongAgainstShaman = 62 UMETA(DisplayName = "Strong Against Shamans"),
	StrongAgainstMonster = 63 UMETA(DisplayName = "Strong Against Monsters"),
	MallAttackBonus = 64 UMETA(DisplayName = "Mall Attack Bonus"),
	MallDefenseBonus = 65 UMETA(DisplayName = "Mall Defense Bonus"),
	MallExperienceBonus = 66 UMETA(DisplayName = "Mall Experience Bonus"),
	MallItemDropBonus = 67 UMETA(DisplayName = "Mall Item Drop Bonus"),
	MallYangBonus = 68 UMETA(DisplayName = "Mall Yang Bonus"),
	MaxHPPercent = 69 UMETA(DisplayName = "Maximum HP (%)"),
	MaxSPPercent = 70 UMETA(DisplayName = "Maximum SP (%)"),
	SkillDamage = 71 UMETA(DisplayName = "Skill Damage"),
	AverageDamage = 72 UMETA(DisplayName = "Average Damage"),
	SkillDamageResistance = 73 UMETA(DisplayName = "Skill Damage Resistance"),
	AverageDamageResistance = 74 UMETA(DisplayName = "Average Damage Resistance"),
	PCBangExperienceBonus = 75 UMETA(DisplayName = "PC Bang Experience Bonus"),
	PCBangDropBonus = 76 UMETA(DisplayName = "PC Bang Drop Bonus"),
	ExtractHP = 77 UMETA(DisplayName = "Extract HP"),
	WarriorResistance = 78 UMETA(DisplayName = "Warrior Resistance"),
	AssassinResistance = 79 UMETA(DisplayName = "Assassin Resistance"),
	SuraResistance = 80 UMETA(DisplayName = "Sura Resistance"),
	ShamanResistance = 81 UMETA(DisplayName = "Shaman Resistance"),
	Energy = 82 UMETA(DisplayName = "Energy"),
	DefenseValue = 83 UMETA(DisplayName = "Defense Value"),
	CostumeAttributeBonus = 84 UMETA(DisplayName = "Costume Attribute Bonus"),
	MagicAttackPercent = 85 UMETA(DisplayName = "Magic Attack (%)"),
	MeleeMagicAttackPercent = 86 UMETA(DisplayName = "Melee/Magic Attack (%)"),
	IceResistance = 87 UMETA(DisplayName = "Ice Resistance"),
	EarthResistance = 88 UMETA(DisplayName = "Earth Resistance"),
	DarkResistance = 89 UMETA(DisplayName = "Dark Resistance"),
	AntiCriticalChance = 90 UMETA(DisplayName = "Critical Resistance"),
	AntiPenetratingChance = 91 UMETA(DisplayName = "Penetrating Resistance")
};

UENUM(BlueprintType)
enum class EMT2ItemBonusApplyResult : uint8
{
	Invalid,
	Failed,
	Success
};

UENUM(BlueprintType)
enum class EMT2LootCrateOpenResult : uint8
{
	Invalid,
	LevelTooLow,
	KeyRequired,
	WrongKey,
	NoRewards,
	Success
};

UENUM(BlueprintType)
enum class EMT2LootCrateRewardKind : uint8
{
	Item,
	Yang,
	Experience
};

USTRUCT(BlueprintType)
struct METIN2_API FMT2ItemBonus
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item Bonus")
	EMT2ItemBonusType Type = EMT2ItemBonusType::None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item Bonus")
	int32 Value = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item Bonus")
	EMT2ItemBonusKind Kind = EMT2ItemBonusKind::Normal;

	int32 GetTypeId() const { return static_cast<int32>(Type); }
	bool IsValid() const { return Type != EMT2ItemBonusType::None && Value != 0; }
};

// Mutable data shared by every representation of an item instance. Static definition data belongs
// on UMT2ItemTemplate; identity/location belongs to the object or container holding this payload.
UENUM(BlueprintType)
enum class EMT2MetinSocketType : uint8
{
	Silver,
	Gold
};

USTRUCT(BlueprintType)
struct METIN2_API FMT2MetinSocket
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item|Sockets")
	EMT2MetinSocketType Type = EMT2MetinSocketType::Silver;

	// Null means the socket is open.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item|Sockets")
	TSubclassOf<UMT2ItemMetinStoneTemplate> Stone;

	// Scripted per-item storage. The old quests reuse item sockets as scratch numbers on quest items
	// (timestamps, counters) via item.set_socket/get_socket, independently of any mounted stone.
	// A socket holding a stone reports that stone's vnum instead of this value.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item|Sockets")
	int32 Value = 0;
};

// Accessory minerals use a different system in the original game. They are intentionally not mixed
// with weapon/armor Metin sockets.
USTRUCT(BlueprintType)
struct METIN2_API FMT2AccessorySocketState
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item|Accessory Sockets")
	int32 CurrentGrade = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item|Accessory Sockets")
	int32 MaximumGrade = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item|Accessory Sockets")
	int32 RemainingSeconds = 0;

	bool operator==(const FMT2AccessorySocketState& Other) const = default;
};

USTRUCT(BlueprintType)
struct METIN2_API FMT2ItemInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item", meta = (ClampMin = "0"))
	int32 Count = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item")
	TArray<FMT2ItemBonus> Bonuses;

	// Empty array = no Metin sockets. Each entry preserves its silver/gold type and directly
	// references the installed stone Blueprint (null means open).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item|Sockets")
	TArray<FMT2MetinSocket> MetinSockets;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item|Accessory Sockets")
	FMT2AccessorySocketState AccessorySockets;

	// Skill books with a generic template (notably vnum 50300) roll their actual skill per item.
	// Persisted through the legacy socket_0 database column, matching the original server.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item|Skill Book", meta = (ClampMin = "0"))
	int32 SkillVnum = 0;

	// Runtime reservoir used by USE_SPECIAL automatic HP/MP potions. MaximumAmount is retained on
	// the instance so partially depleted potions remain self-contained through drops and trades.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item|Automatic Recovery")
	int32 AutoRecoveryRemainingAmount = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item|Automatic Recovery")
	int32 AutoRecoveryMaximumAmount = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item|Automatic Recovery")
	bool bAutoRecoveryActive = false;

	// Transient gameplay marker: set only when an item is freshly received and cleared when its
	// tooltip is first shown. Persistence intentionally does not serialize it.
	UPROPERTY(BlueprintReadWrite, Category = "Item|Presentation")
	bool bNewlyAcquired = false;
};

UENUM(BlueprintType)
enum class EMT2MetinSocketApplyResult : uint8
{
	Invalid,
	Success,
	Failed,
	NoSocket,
	Incompatible,
	DuplicateStone
};

// Runtime item instance used by inventory/equipment arrays. The historical name is retained because
// existing Widget Blueprints serialize this exact reflected type; the array index is the location.
USTRUCT(BlueprintType)
struct METIN2_API FMT2ItemSlot : public FMT2ItemInstanceData
{
	GENERATED_BODY()

	FMT2ItemSlot() = default;
	FMT2ItemSlot(int32 InVnum, const FMT2ItemInstanceData& InInstanceData)
		: FMT2ItemInstanceData(InInstanceData), Vnum(InVnum)
	{
	}

	UPROPERTY(BlueprintReadOnly, Category = "Item")
	int32 Vnum = 0;

	bool IsEmpty() const { return Vnum <= 0 || Count <= 0; }
};

// One entry of the old client's aApplies[ITEM_APPLY_MAX_NUM] (common/tables.h): a stat bonus granted
// while the item is equipped. ApplyType is an EApplyTypes ordinal from the old common/enums.h
// (APPLY_STR=5, APPLY_ATT_SPEED=7, APPLY_MOV_SPEED=8, APPLY_DEF_GRADE=83, ...).
USTRUCT(BlueprintType)
struct METIN2_API FMT2ItemApply
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Apply")
	EMT2ItemBonusType Type = EMT2ItemBonusType::None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Apply")
	int32 Value = 0;
};

UENUM(BlueprintType)
enum class EMT2ItemLimitType : uint8
{
	None = 0,
	Level = 1,
	Strength = 2,
	Dexterity = 3,
	Intelligence = 4,
	Constitution = 5,
	PCBang = 6,
	RealTime = 7,
	RealTimeFromFirstUse = 8,
	TimerWhileEquipped = 9
};

USTRUCT(BlueprintType)
struct METIN2_API FMT2ItemLimit
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Limit")
	EMT2ItemLimitType Type = EMT2ItemLimitType::None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Limit")
	int32 Value = 0;
};

USTRUCT(BlueprintType)
struct METIN2_API FMT2ItemArmorMaterialOverrideDefinition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Armor")
	FString SourceTextureObjectPath;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Armor")
	FString TargetTextureObjectPath;
};

USTRUCT(BlueprintType)
struct METIN2_API FMT2ItemArmorVisualDefinition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Armor")
	uint8 Race = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Armor")
	uint8 Sex = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Armor")
	FString MeshObjectPath;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Armor")
	TArray<FMT2ItemArmorMaterialOverrideDefinition> MaterialOverrides;
};

USTRUCT(BlueprintType)
struct METIN2_API FMT2ItemHairVisualDefinition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hair")
	uint8 Race = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hair")
	uint8 Sex = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hair")
	FString MeshObjectPath;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hair")
	FString TextureObjectPath;
};

// Mirrors the old client's SItemTable (common/tables.h) as decoded by FMT2ItemProtoReader.
USTRUCT(BlueprintType)
struct METIN2_API FMT2ItemDefinition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	int32 Vnum = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	FString InternalName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	FString DisplayName;

	// locale itemdesc.txt third column (tooltip flavor text); empty when the item has no entry.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	FString Description;

	// EItemTypes ordinal from the old client's common/enums.h (ITEM_TYPE_WEAPON=1, ITEM_TYPE_ARMOR=2, ...).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	int32 ItemType = 0;

	// EWeaponSubTypes / EArmorSubTypes ordinal; meaning depends on ItemType.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	int32 SubType = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	int32 Size = 1;

	// item_proto WEIGHT; carried but unused (the old game never enforced weight either).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	int32 Weight = 0;

	// Vnums in [Vnum, Vnum+VnumRange] resolve to this same item (old client SelectItemData ranges).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	int32 VnumRange = 0;

	// EWearPositions bitmask (1 << WEAR_WEAPON, etc.); 0 if not equippable.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Flags",
		meta = (Bitmask, BitmaskEnum = "/Script/Metin2.EMT2ItemWearFlag"))
	int32 WearFlags = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Flags",
		meta = (Bitmask, BitmaskEnum = "/Script/Metin2.EMT2ItemAntiFlag"))
	int32 AntiFlags = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Flags",
		meta = (Bitmask, BitmaskEnum = "/Script/Metin2.EMT2ItemFlag"))
	int32 Flags = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Flags",
		meta = (Bitmask, BitmaskEnum = "/Script/Metin2.EMT2ItemImmuneFlag"))
	int32 ImmuneFlags = 0;

	// 40250 TItemTable dwIBuyItemPrice: what shops charge the player.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Economy")
	int64 BuyPrice = 0;

	// 40250 TItemTable dwISellItemPrice: what shops pay the player.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Economy")
	int64 SellPrice = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Refine")
	int32 RefinedVnum = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Refine")
	int32 RefineSet = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Refine")
	int32 PreviousRefinedVnum = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Refine", meta = (ClampMin = "0", ClampMax = "9"))
	int32 RefinementLevel = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Refine")
	FMT2RefinementRecipe RefinementRecipe;

	// item_proto MAGIC_PCT: chance to roll magic attributes when acquired; not wired yet.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Refine")
	int32 AlterToMagicItemPercent = 0;

	// item_proto GAIN_SOCKET_PCT: chance to gain a socket on refine; not wired yet.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Refine")
	int32 GainSocketPercent = 0;

	// Old item_proto addon_type. -1 generates the paired average/skill-damage attributes.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stats")
	int32 BonusAddonType = 0;

	// item_proto Specular strength (visual shine); not wired yet.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Presentation")
	int32 Specular = 0;

	// Raw Value0..5 from item_proto. Weapon slots are magic min/max at 1/2, physical min/max at 3/4,
	// and refinement power at 5.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stats")
	TArray<int32> Values;

	// aApplies[3] from item_proto - the equipped stat bonuses.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stats")
	TArray<FMT2ItemApply> Applies;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stats")
	TArray<FMT2ItemLimit> Limits;

	// Mutable defaults carried from item_proto into the generated item template.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stats")
	FMT2ItemInstanceData DefaultInstanceData;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Presentation")
	FString IconObjectPath;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Presentation")
	FString WorldMeshObjectPath;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Presentation")
	TArray<FMT2ItemArmorVisualDefinition> ArmorVisuals;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Presentation")
	TArray<FMT2ItemHairVisualDefinition> HairVisuals;
};
