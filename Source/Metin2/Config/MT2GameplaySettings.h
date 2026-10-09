/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "CoreMinimal.h"
#include "Config/MT2PathSettings.h"
#include "Engine/DeveloperSettings.h"
#include "Items/MT2ItemTypes.h"
#include "Player/MT2PlayerTypes.h"
#include "UI/MT2AtlasTypes.h"
#include "MT2GameplaySettings.generated.h"

class UMT2ItemTemplate;
class UStaticMesh;
class UMaterialInterface;
class UParticleSystem;
class USoundBase;
class UTexture2D;

// A complete item instance granted to a newly-created character. A struct is used instead of an
// instanced UObject so every field is serialized reliably by Developer Settings into Game config.
USTRUCT(BlueprintType)
struct METIN2_API FMT2StartingItem
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, config, BlueprintReadOnly, Category = "Item")
	TSubclassOf<UMT2ItemTemplate> Template;

	UPROPERTY(EditAnywhere, config, BlueprintReadOnly, Category = "Item")
	FMT2ItemInstanceData InstanceData;
};

// Old client EFFECT_REFINED registration for one refinement tier. The source uses different
// emitter volumes for full-size swords, bows, fan/bell weapons and small weapons, plus a body
// emitter attached to Bip01. Keeping the set configurable allows replacing an imported effect
// without touching equipment code.
USTRUCT(BlueprintType)
struct METIN2_API FMT2RefinementGlitterSet
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, config, BlueprintReadOnly, Category = "Glitter",
		meta = (ClampMin = "7", ClampMax = "9"))
	int32 RefinementLevel = 7;

	UPROPERTY(EditAnywhere, config, BlueprintReadOnly, Category = "Glitter")
	TSoftObjectPtr<UParticleSystem> Sword;

	UPROPERTY(EditAnywhere, config, BlueprintReadOnly, Category = "Glitter")
	TSoftObjectPtr<UParticleSystem> Bow;

	UPROPERTY(EditAnywhere, config, BlueprintReadOnly, Category = "Glitter")
	TSoftObjectPtr<UParticleSystem> FanBell;

	UPROPERTY(EditAnywhere, config, BlueprintReadOnly, Category = "Glitter")
	TSoftObjectPtr<UParticleSystem> SmallWeapon;

	UPROPERTY(EditAnywhere, config, BlueprintReadOnly, Category = "Glitter")
	TSoftObjectPtr<UParticleSystem> BodyArmor;

	FMT2RefinementGlitterSet() = default;

	explicit FMT2RefinementGlitterSet(int32 InLevel)
		: RefinementLevel(InLevel)
	{
		const FString Tier = FString::FromInt(InLevel);
		Sword = TSoftObjectPtr<UParticleSystem>(FSoftObjectPath(
			UMT2PathSettings::Format(TEXT("RefinementSwordTemplate"), TEXT("%s%s"), *Tier, *Tier)));
		Bow = TSoftObjectPtr<UParticleSystem>(FSoftObjectPath(
			UMT2PathSettings::Format(TEXT("RefinementBowTemplate"), TEXT("%s%s"), *Tier, *Tier)));
		FanBell = TSoftObjectPtr<UParticleSystem>(FSoftObjectPath(
			UMT2PathSettings::Format(TEXT("RefinementFanBellTemplate"), TEXT("%s%s"), *Tier, *Tier)));
		SmallWeapon = TSoftObjectPtr<UParticleSystem>(FSoftObjectPath(
			UMT2PathSettings::Format(TEXT("RefinementSmallWeaponTemplate"), TEXT("%s%s"), *Tier, *Tier)));
		BodyArmor = TSoftObjectPtr<UParticleSystem>(FSoftObjectPath(
			UMT2PathSettings::Format(TEXT("RefinementBodyTemplate"), TEXT("%s%s"), *Tier, *Tier)));
	}
};

// Project-wide gameplay asset references, kept out of the code as soft pointers so they can be
// reassigned from Project Settings -> Metin2, or by hand in DefaultGame.ini under
// [/Script/Metin2.MT2GameplaySettings]. Anything the gameplay code used to hardcode as a "/Game/..."
// string should live here instead. UI/widget assets stay owned by the user's Blueprints.
UCLASS(config = Game, defaultconfig, meta = (DisplayName = "Metin2 Gameplay"))
class METIN2_API UMT2GameplaySettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	virtual FName GetCategoryName() const override { return TEXT("Metin2"); }

	static const UMT2GameplaySettings& Get() { return *GetDefault<UMT2GameplaySettings>(); }

	// Sweeps and overlaps remain enabled without registering player capsules as Chaos bodies.
	UPROPERTY(EditAnywhere, config, Category = "Movement",
		meta = (ToolTip = "Enable player capsule physics collision and pushing rigid bodies. Disabled for legacy-style query-only movement. Requires matching client/server settings and respawning players."))
	bool bPlayerPhysicsInteraction = false;

	// A surface material/instance for the client-generated water mesh. Unset disables rendering.
	UPROPERTY(EditAnywhere, config, Category = "Water")
	TSoftObjectPtr<UMaterialInterface> WaterMaterial;
	UPROPERTY(EditAnywhere, config, Category = "Water", meta = (ClampMin = "1.0", Units = "cm"))
	float WaterUVTileSize = 1000.f;
	UPROPERTY(EditAnywhere, config, Category = "Water", meta = (Units = "cm"))
	float WaterSurfaceOffset = 1.f;
	// Bake-only visual overlap under banks; never changes server water/no-walk/safezone flags.
	UPROPERTY(EditAnywhere, config, Category = "Water", meta = (ClampMin = "0", ClampMax = "4"))
	int32 WaterShorelinePaddingCells = 1;

	// ---- PvP and empire presentation ----

	UPROPERTY(EditAnywhere, config, Category = "PvP", meta = (ClampMin = "0"))
	int32 AggressivePlayerKillKarmaPenalty = 2000;

	UPROPERTY(EditAnywhere, Config, Category = "Party", meta = (ClampMin = "2", ClampMax = "8"))
	int32 PartyMaximumMembers = 8;

	UPROPERTY(EditAnywhere, Config, Category = "Party", meta = (ClampMin = "5.0", Units = "s"))
	float PartyInviteTimeout = 30.0f;

	UPROPERTY(EditAnywhere, Config, Category = "Party", meta = (ClampMin = "0.0", Units = "cm"))
	float PartyExperienceShareRange = 10000.0f;

	// Global server-side multipliers for rewards generated by defeated mobs. These do not affect
	// loot crates, quests, admin grants, or other non-mob reward sources.
	UPROPERTY(EditAnywhere, config, Category = "Mobs|Rewards", meta = (ClampMin = "0.0"))
	float MobGoldAmountMultiplier = 1.0f;

	UPROPERTY(EditAnywhere, config, Category = "Mobs|Rewards", meta = (ClampMin = "0.0"))
	float MobExperienceAmountMultiplier = 1.0f;

	UPROPERTY(EditAnywhere, config, Category = "Mobs|Rewards", meta = (ClampMin = "0.0"))
	float MobItemDropChanceMultiplier = 1.0f;

	// Old IntroEmpire.dds atlas used for compact empire flags beside player names.
	UPROPERTY(EditAnywhere, config, Category = "UI|Empire")
	TSoftObjectPtr<UTexture2D> EmpireFlagAtlas = TSoftObjectPtr<UTexture2D>(FSoftObjectPath(
		UMT2PathSettings::Path(TEXT("ymir_work_ui_T_introempire"))));

	UPROPERTY(EditAnywhere, config, Category = "UI|Empire")
	TMap<EMT2Empire, FMT2AtlasRect> EmpireFlagRegions = {
		{EMT2Empire::Shinsoo, FMT2AtlasRect(313.0f, 357.0f, 128.0f, 79.0f)},
		{EMT2Empire::Chunjo, FMT2AtlasRect(359.0f, 479.0f, 128.0f, 79.0f)},
		{EMT2Empire::Jinno, FMT2AtlasRect(0.0f, 669.0f, 128.0f, 79.0f)}
	};

	UPROPERTY(EditAnywhere, config, Category = "UI|Empire", meta = (ClampMin = "1.0"))
	FVector2D EmpireFlagDisplaySize = FVector2D(16.0f, 10.0f);

	// ---- Target indicator (old EFFECT_SELECT: the rotating red ring at the target's feet) ----

	// Mesh spun flat at the target's feet. Defaults to an engine primitive so a marker shows even
	// before the real imported ring asset is assigned; point this at the converted click_select
	// ring to match the old game. See Docs/OldGameResearch/TargetAndSelectEffect.md.
	UPROPERTY(EditAnywhere, config, Category = "Targeting",
		meta = (AllowedClasses = "/Script/Engine.StaticMesh"))
	TSoftObjectPtr<UStaticMesh> TargetIndicatorMesh =
		TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(UMT2PathSettings::Path(TEXT("Engine_BasicShapes_Cylinder"))));

	// Optional material override for the indicator mesh (e.g. the imported additive ring material).
	UPROPERTY(EditAnywhere, config, Category = "Targeting",
		meta = (AllowedClasses = "/Script/Engine.MaterialInterface"))
	TSoftObjectPtr<UMaterialInterface> TargetIndicatorMaterial;

	// Tint applied to the indicator (old click_select ColorFactor was red-orange). Only takes effect
	// when the assigned material exposes a "Color" vector parameter.
	UPROPERTY(EditAnywhere, config, Category = "Targeting")
	FLinearColor TargetIndicatorColor = FLinearColor(1.0f, 0.055f, 0.0f, 1.0f);

	// World-space scale of the indicator mesh (the engine cylinder is 100uu across and 100 tall, so
	// the default flattens it into a ~140uu disc).
	UPROPERTY(EditAnywhere, config, Category = "Targeting")
	FVector TargetIndicatorScale = FVector(1.4f, 1.4f, 0.02f);

	// Lifted slightly off the ground so it doesn't z-fight with the floor.
	UPROPERTY(EditAnywhere, config, Category = "Targeting", meta = (Units = "cm"))
	float TargetIndicatorHeight = 3.0f;

	// Spin speed of the ring, matching the old mesh's baked rotation.
	UPROPERTY(EditAnywhere, config, Category = "Targeting", meta = (Units = "deg/s"))
	float TargetIndicatorSpinSpeed = 120.0f;

	// ---- World drops ----

	// Shown for a dropped item whose item template has no world mesh (old game's generic loot bag).
	UPROPERTY(EditAnywhere, config, Category = "World Items",
		meta = (AllowedClasses = "/Script/Engine.StaticMesh"))
	TSoftObjectPtr<UStaticMesh> FallbackWorldItemMesh =
		TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(UMT2PathSettings::Path(TEXT("ymir_work_item_etc_SM_item_bag"))));

	// Spatial sounds emitted by AMT2WorldItem when a drop enters the world. The item family
	// selection matches the old inventory sounds, but playback belongs to the replicated world
	// actor so mob and player drops are heard by every nearby player.
	UPROPERTY(EditAnywhere, config, Category = "World Items|Audio")
	TSoftObjectPtr<USoundBase> WorldItemDefaultDropSound =
		TSoftObjectPtr<USoundBase>(FSoftObjectPath(UMT2PathSettings::Path(TEXT("sound_ui_drop"))));

	UPROPERTY(EditAnywhere, config, Category = "World Items|Audio")
	TSoftObjectPtr<USoundBase> WorldItemWeaponDropSound =
		TSoftObjectPtr<USoundBase>(FSoftObjectPath(
			UMT2PathSettings::Path(TEXT("sound_ui_equip_metal_weapon"))));

	UPROPERTY(EditAnywhere, config, Category = "World Items|Audio")
	TSoftObjectPtr<USoundBase> WorldItemBowDropSound =
		TSoftObjectPtr<USoundBase>(FSoftObjectPath(UMT2PathSettings::Path(TEXT("sound_ui_equip_bow"))));

	UPROPERTY(EditAnywhere, config, Category = "World Items|Audio")
	TSoftObjectPtr<USoundBase> WorldItemArmorDropSound =
		TSoftObjectPtr<USoundBase>(FSoftObjectPath(
			UMT2PathSettings::Path(TEXT("sound_ui_equip_metal_armor"))));

	UPROPERTY(EditAnywhere, config, Category = "World Items|Audio")
	TSoftObjectPtr<USoundBase> WorldItemAccessoryDropSound =
		TSoftObjectPtr<USoundBase>(FSoftObjectPath(
			UMT2PathSettings::Path(TEXT("sound_ui_equip_ring_amulet"))));

	UPROPERTY(EditAnywhere, config, Category = "World Items|Audio",
		meta = (ClampMin = "0.0", Units = "cm"))
	float WorldItemDropSoundInnerRadius = 300.0f;

	UPROPERTY(EditAnywhere, config, Category = "World Items|Audio",
		meta = (ClampMin = "1.0", Units = "cm"))
	float WorldItemDropSoundFalloffDistance = 2700.0f;

	UPROPERTY(EditAnywhere, config, Category = "World Items",
		meta = (ClampMin = "0.0", Units = "s"))
	float LootCrateOverflowOwnershipSeconds = 600.0f;

	UPROPERTY(EditAnywhere, config, Category = "World Items",
		meta = (ClampMin = "0.0", Units = "s"))
	float PvPEquipmentDropOwnershipSeconds = 120.0f;

	// Granted once, after the new character's first persistent inventory has been restored. Complete
	// instance data is preserved, including normal/rare bonuses and sockets.
	UPROPERTY(EditAnywhere, config, Category = "New Characters")
	TArray<FMT2StartingItem> StartingItems;

	// Imported quest ids excluded at runtime. The stock source includes developer/test quests such as
	// arne_test, whose login trigger gives item 10 on every new session.
	UPROPERTY(EditAnywhere, config, Category = "Quests")
	TSet<FName> DisabledQuestIds = {TEXT("arne_test")};

	// Played for each 25/50/75% EXP milestone. The fourth step uses LevelUpSound instead.
	UPROPERTY(EditAnywhere, config, Category = "Progression")
	TSoftObjectPtr<USoundBase> ExperienceStepSound =
		TSoftObjectPtr<USoundBase>(FSoftObjectPath(
			UMT2PathSettings::Path(TEXT("sound_effect_etc_levelup_2_levelup1_2"))));

	// Old POINT_LEVEL_STEP visual (EFFECT_SKILLUP).
	UPROPERTY(EditAnywhere, config, Category = "Progression")
	TSoftObjectPtr<UParticleSystem> ExperienceStepEffect =
		TSoftObjectPtr<UParticleSystem>(FSoftObjectPath(
			UMT2PathSettings::Path(TEXT("ymir_work_effect_etc_skillup_PS_skillup_1"))));

	// Played when the fourth EXP step completes and the character gains a level.
	UPROPERTY(EditAnywhere, config, Category = "Progression")
	TSoftObjectPtr<USoundBase> LevelUpSound =
		TSoftObjectPtr<USoundBase>(FSoftObjectPath(
			UMT2PathSettings::Path(TEXT("sound_effect_etc_levelup_1_levelup1_1"))));

	// Old POINT_LEVEL visual (EFFECT_LEVELUP).
	UPROPERTY(EditAnywhere, config, Category = "Progression")
	TSoftObjectPtr<UParticleSystem> LevelUpEffect =
		TSoftObjectPtr<UParticleSystem>(FSoftObjectPath(
			UMT2PathSettings::Path(TEXT("ymir_work_effect_etc_levelup_1_PS_level_up"))));

	// Cosmetic particles that fly from a defeated mob to every player who actually receives EXP.
	UPROPERTY(EditAnywhere, config, Category = "Progression|Experience Orbs")
	TSoftObjectPtr<UParticleSystem> ExperienceOrbEffect =
		TSoftObjectPtr<UParticleSystem>(FSoftObjectPath(
			UMT2PathSettings::Path(TEXT("ymir_work_effect_etc_gathering_PS_ga_piece_yellow2"))));

	UPROPERTY(EditAnywhere, config, Category = "Progression|Experience Orbs")
	TSoftObjectPtr<UParticleSystem> ExperienceOrbImpactEffect =
		TSoftObjectPtr<UParticleSystem>(FSoftObjectPath(
			UMT2PathSettings::Path(TEXT("ymir_work_effect_etc_gathering_PS_ga_center_small_yellow"))));

	UPROPERTY(EditAnywhere, config, Category = "Progression|Experience Orbs")
	TSoftObjectPtr<UMaterialInterface> ExperienceOrbMaterial =
		TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(
			UMT2PathSettings::Path(TEXT("ymir_work_effect_etc_gathering_M_PS_ga_piece_yellow2_00"))));

	UPROPERTY(EditAnywhere, config, Category = "Progression|Experience Orbs",
		meta = (ClampMin = "1", ClampMax = "12"))
	int32 ExperienceOrbMaximumCount = 12;

	UPROPERTY(EditAnywhere, config, Category = "Progression|Experience Orbs",
		meta = (ClampMin = "0.0", Units = "cm/s"))
	float ExperienceOrbInitialSpeed = 100.0f;

	UPROPERTY(EditAnywhere, config, Category = "Progression|Experience Orbs",
		meta = (ClampMin = "0.0", ClampMax = "180.0", Units = "deg"))
	float ExperienceOrbSpreadConeAngle = 90.0f;

	UPROPERTY(EditAnywhere, config, Category = "Progression|Experience Orbs",
		meta = (ClampMin = "0.0", Units = "cm/s^2"))
	float ExperienceOrbAcceleration = 120.0f;

	UPROPERTY(EditAnywhere, config, Category = "Progression|Experience Orbs",
		meta = (ClampMin = "1.0", Units = "cm/s"))
	float ExperienceOrbMaximumSpeed = 300.0f;

	UPROPERTY(EditAnywhere, config, Category = "Progression|Experience Orbs",
		meta = (ClampMin = "0.0", Units = "s"))
	float ExperienceOrbHomingStartTime = 1.0f;

	UPROPERTY(EditAnywhere, config, Category = "Progression|Experience Orbs",
		meta = (ClampMin = "0.0", Units = "deg/s"))
	float ExperienceOrbHomingTurnRate = 300.0f;

	UPROPERTY(EditAnywhere, config, Category = "Progression|Experience Orbs",
		meta = (ClampMin = "1.0", Units = "cm"))
	float ExperienceOrbMaximumRange = 10000.0f;

	UPROPERTY(EditAnywhere, config, Category = "Progression|Experience Orbs",
		meta = (ClampMin = "1.0", Units = "cm"))
	float ExperienceOrbHitRadius = 40.0f;

	UPROPERTY(EditAnywhere, config, Category = "Progression|Experience Orbs",
		meta = (ClampMin = "0.0", Units = "s"))
	float ExperienceOrbMinimumVisibleTime = 0.6f;

	UPROPERTY(EditAnywhere, config, Category = "Progression|Experience Orbs",
		meta = (ClampMin = "0.01", Units = "s"))
	float ExperienceOrbVisualOffsetPeriod = 0.4f;

	UPROPERTY(EditAnywhere, config, Category = "Progression|Experience Orbs",
		meta = (ClampMin = "0.0", Units = "cm"))
	float ExperienceOrbVisualOffsetAmplitude = 150.0f;

	UPROPERTY(EditAnywhere, config, Category = "Progression|Experience Orbs",
		meta = (Units = "deg/s"))
	float ExperienceOrbVisualAngularVelocity = 450.0f;

	UPROPERTY(EditAnywhere, config, Category = "Progression|Experience Orbs",
		meta = (ClampMin = "0.01"))
	float ExperienceOrbScale = 1.0f;

	UPROPERTY(EditAnywhere, config, Category = "Progression|Experience Orbs",
		meta = (ClampMin = "1.0", Units = "cm"))
	float ExperienceOrbSpriteSize = 11.0f;

	// Flat EXP spent for every skill-book read, whether the read succeeds or fails.
	UPROPERTY(EditAnywhere, config, Category = "Skills", meta = (ClampMin = "0"))
	int64 SkillBookExperienceCost = 10000;

	// One independent cooldown is recorded per skill. 86400 seconds = one day.
	UPROPERTY(EditAnywhere, config, Category = "Skills",
		meta = (ClampMin = "0", Units = "s"))
	int32 SkillBookCooldownSeconds = 86400;

	UPROPERTY(EditAnywhere, config, Category = "Skills",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float SkillBookSuccessChance = 0.35f;

	UPROPERTY(EditAnywhere, config, Category = "Skills")
	int32 SkillBookCooldownBypassItemVnum = 71001;

	UPROPERTY(EditAnywhere, config, Category = "Skills")
	int32 SkillBookGuaranteedSuccessItemVnum = 71094;

	// NPCs that accept ordinary item refinement. The normal village blacksmith is 20016.
	// Add specialized/guild/tower smith VNUMs here when their item-category rules are implemented.
	UPROPERTY(EditAnywhere, config, Category = "Items|Refinement")
	TArray<int32> RefinementBlacksmithVnums = {20016};

	// Exact old-client sounds: RefineSuceededMessage / RefineFailedMessage in game.py.
	UPROPERTY(EditAnywhere, config, Category = "Items|Refinement")
	TSoftObjectPtr<USoundBase> RefinementSuccessSound =
		TSoftObjectPtr<USoundBase>(FSoftObjectPath(
			UMT2PathSettings::Path(TEXT("sound_ui_make_soket"))));

	UPROPERTY(EditAnywhere, config, Category = "Items|Refinement")
	TSoftObjectPtr<USoundBase> RefinementFailureSound =
		TSoftObjectPtr<USoundBase>(FSoftObjectPath(
			UMT2PathSettings::Path(TEXT("sound_ui_jaeryun_fail"))));

	// The old critical.mss plays gicheon_effect.wav with the critical hit effect. The stock client
	// has no separate penetration sound, so both default to it but remain independently replaceable.
	UPROPERTY(EditAnywhere, config, Category = "Combat|Hit Feedback")
	TSoftObjectPtr<USoundBase> CriticalHitSound =
		TSoftObjectPtr<USoundBase>(FSoftObjectPath(
			UMT2PathSettings::Path(TEXT("sound_pc_shaman_skill_gicheon_effect"))));

	UPROPERTY(EditAnywhere, config, Category = "Combat|Hit Feedback")
	TSoftObjectPtr<USoundBase> PenetratingHitSound =
		TSoftObjectPtr<USoundBase>(FSoftObjectPath(
			UMT2PathSettings::Path(TEXT("sound_pc_shaman_skill_gicheon_effect"))));

	// The old impact effects select a common sword-hit sample when the attack actually connects.
	// These are separate from swing sounds authored in the attack animation's .mss file.
	UPROPERTY(EditAnywhere, config, Category = "Combat|Hit Feedback")
	TArray<TSoftObjectPtr<USoundBase>> BasicHitSounds = {
		TSoftObjectPtr<USoundBase>(FSoftObjectPath(UMT2PathSettings::Path(TEXT("sound_common_hit_hit_sword_1")))),
		TSoftObjectPtr<USoundBase>(FSoftObjectPath(UMT2PathSettings::Path(TEXT("sound_common_hit_hit_sword_2")))),
		TSoftObjectPtr<USoundBase>(FSoftObjectPath(UMT2PathSettings::Path(TEXT("sound_common_hit_hit_sword_3"))))
	};

	// Persistent +7/+8/+9 equipment glitter effects. Weapon subtypes select the matching old-client
	// emitter volume; body armor uses the armor effect attached to the animated body root.
	UPROPERTY(EditAnywhere, config, Category = "Items|Refinement|Glitter")
	TArray<FMT2RefinementGlitterSet> RefinementGlitterEffects = {
		FMT2RefinementGlitterSet(7),
		FMT2RefinementGlitterSet(8),
		FMT2RefinementGlitterSet(9)
	};

	// sword_7/8/9.mse author the emitter along local Z, centered at 70 with a length of 100.
	// Sword and two-hand weapon effects are fitted to the imported mesh bounds at runtime.
	UPROPERTY(EditAnywhere, config, Category = "Items|Refinement|Glitter",
		meta = (ClampMin = "1.0"))
	float SwordGlitterAuthoredLength = 100.0f;

	UPROPERTY(EditAnywhere, config, Category = "Items|Refinement|Glitter")
	float SwordGlitterAuthoredCenter = 70.0f;

	UPROPERTY(EditAnywhere, config, Category = "Items|Refinement|Glitter",
		meta = (ClampMin = "0.01"))
	float SwordGlitterMinimumLengthScale = 0.25f;

	UPROPERTY(EditAnywhere, config, Category = "Items|Refinement|Glitter",
		meta = (ClampMin = "0.01"))
	float SwordGlitterMaximumLengthScale = 5.0f;

	// Old aiGrandMasterSkillBook* tables, indexed G1..G10.
	UPROPERTY(EditAnywhere, config, Category = "Skills|Grand Master")
	TArray<int32> GrandMasterSuccessDenominators = {3, 3, 5, 5, 7, 7, 10, 10, 10, 20};

	UPROPERTY(EditAnywhere, config, Category = "Skills|Grand Master")
	TArray<int32> GrandMasterMinimumReads = {1, 1, 1, 2, 2, 3, 3, 4, 5, 6};

	UPROPERTY(EditAnywhere, config, Category = "Skills|Grand Master")
	TArray<int32> GrandMasterMaximumReads = {5, 7, 9, 11, 13, 15, 20, 25, 30, 35};

	// ---- Proximity voice chat (self-hosted UDP relay; see Source/Metin2/Voice) ----

	// UDP port the server's voice relay listens on. Clients send to the game server's host at this
	// port; must be reachable/forwarded alongside the game port.
	UPROPERTY(EditAnywhere, config, Category = "Voice Chat", meta = (ClampMin = "1024", ClampMax = "65535"))
	// FALLBACK only: the voice relay normally binds <server game port> + 100 (11001 -> 11101);
	// this fixed port is used when no game port is known.
	int32 VoiceChatPort = 11101;

	// How far a voice transmission carries. The relay only forwards a speaker's packets to players
	// inside this radius (resolved through the player spatial grid), and playback attenuates to
	// silence at this distance.
	UPROPERTY(EditAnywhere, config, Category = "Voice Chat", meta = (ClampMin = "500.0", Units = "cm"))
	float VoiceChatRange = 3500.0f;

	UPROPERTY(EditAnywhere, config, Category="Combat|Duels", meta=(ClampMin="1", Units="s"))
	float DuelIdleTimeoutSeconds = 600.f;
	UPROPERTY(EditAnywhere, config, Category="Combat|Duels", meta=(ClampMin="1", Units="cm"))
	float DuelRequestRange = 3000.f;
	UPROPERTY(EditAnywhere, config, Category="Combat|Duels", meta=(ClampMin="0", Units="s"))
	float DuelRequestCooldownSeconds = 1.f;
	UPROPERTY(EditAnywhere, config, Category="Combat|Duels", meta=(ClampMin="1", ClampMax="128"))
	int32 MaximumDuelAgreements = 32;

	// Audio buffered before a speaker's stream starts playing. Higher = more latency but smoother
	// under jitter. Latency is explicitly acceptable here; streams are not synced across clients.
	UPROPERTY(EditAnywhere, config, Category = "Voice Chat", meta = (ClampMin = "20", ClampMax = "1000", Units = "ms"))
	int32 VoiceJitterBufferMs = 120;

	// How long playout waits for an out-of-order packet before declaring it lost and skipping ahead.
	// The skipped span is simply silence - never stretched or repeated audio.
	UPROPERTY(EditAnywhere, config, Category = "Voice Chat", meta = (ClampMin = "0", ClampMax = "500", Units = "ms"))
	int32 VoiceGapWaitMs = 80;

	// Icon shown beside a player's name on their nameplate while their voice is heard (and on your
	// own while transmitting). Unset = the speaking logic still runs but no icon is drawn.
	UPROPERTY(EditAnywhere, config, Category = "Voice Chat",
		meta = (AllowedClasses = "/Script/Engine.Texture2D"))
	TSoftObjectPtr<UTexture2D> VoiceSpeakingIcon;
};
