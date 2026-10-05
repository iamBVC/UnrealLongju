/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "Player/MT2AdminCommandComponent.h"

#include "Characters/MT2PlayerCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Components/MT2StatusEffectComponent.h"
#include "Components/MT2HealthComponent.h"
#include "Components/MT2ManaComponent.h"
#include "Components/MT2StaminaComponent.h"
#include "Core/MT2VnumRegistrySubsystem.h"
#include "Core/MT2VnumRegistry.h"
#include "Engine/World.h"
#include "GameFramework/GameModeBase.h"
#include "HAL/IConsoleManager.h"
#include "Items/MT2InventoryComponent.h"
#include "Items/MT2ItemTemplate.h"
#include "Items/MT2ItemTypes.h"
#include "Mobs/MT2Mob.h"
#include "Net/NetworkProfiler.h"
#include "Persistence/MT2PersistenceComponent.h"
#include "Player/MT2PlayerState.h"
#include "Player/MT2PlayerController.h"
#include "Quests/MT2QuestTableAsset.h"
#include "Server/MT2ServerRuntimeSubsystem.h"
#include "Skills/MT2SkillComponent.h"
#include "Stats/MT2CombatStatsComponent.h"
#include "Stats/MT2PrimaryStatsComponent.h"
#include "World/MT2MapUtils.h"

DEFINE_LOG_CATEGORY_STATIC(LogMT2AdminCmd, Log, All);

UMT2AdminCommandComponent::UMT2AdminCommandComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

AMT2PlayerCharacter* UMT2AdminCommandComponent::GetPlayer() const
{
	return Cast<AMT2PlayerCharacter>(GetOwner());
}

const TArray<UMT2AdminCommandComponent::FCommand>& UMT2AdminCommandComponent::GetCommandTable()
{
	static const TArray<FCommand> Table = {
		{TEXT("/m"),             2, &UMT2AdminCommandComponent::CmdSpawnMob,      TEXT("/m <vnum> [count]")},
		{TEXT("/i"),             2, &UMT2AdminCommandComponent::CmdGiveItem,      TEXT("/i <vnum> [count]")},
		{TEXT("/xp"),            2, &UMT2AdminCommandComponent::CmdAddExperience, TEXT("/xp <positive amount>")},
		{TEXT("/money"),         2, &UMT2AdminCommandComponent::CmdAddMoney,      TEXT("/money <positive amount>")},
		{TEXT("/a"),             3, &UMT2AdminCommandComponent::CmdApplyEffect,   TEXT("/a <applyType> <value> [durationSeconds]")},
		{TEXT("/setskillgroup"), 2, &UMT2AdminCommandComponent::CmdSetSkillGroup, TEXT("/setskillgroup <0|1|2>")},
		{TEXT("/setskill"),      3, &UMT2AdminCommandComponent::CmdSetSkill,      TEXT("/setskill <vnum> <level 0-40>")},
		{TEXT("/race"),          2, &UMT2AdminCommandComponent::CmdSetRace,       TEXT("/race <warrior|assassin|sura|shaman>")},
		{TEXT("/gender"),        2, &UMT2AdminCommandComponent::CmdSetGender,     TEXT("/gender <male|female>")},
		{TEXT("/style"),         2, &UMT2AdminCommandComponent::CmdSetStyle,      TEXT("/style <red|blue>")},
		{TEXT("/goto"),          2, &UMT2AdminCommandComponent::CmdGoto,          TEXT("/goto <mapname>")},
		{TEXT("/tp"),            3, &UMT2AdminCommandComponent::CmdTeleport,      TEXT("/tp <x> <y> [z]")},
		{TEXT("/persist"),       1, &UMT2AdminCommandComponent::CmdPersist,       TEXT("/persist")},
		{TEXT("/shutdown"),      1, &UMT2AdminCommandComponent::CmdShutdown,      TEXT("/shutdown")},
		{TEXT("/reboot"),        1, &UMT2AdminCommandComponent::CmdReboot,        TEXT("/reboot")},
		{TEXT("/servers"),       1, &UMT2AdminCommandComponent::CmdServers,       TEXT("/servers")},
		{TEXT("/pk"),            1, &UMT2AdminCommandComponent::CmdTogglePk,      TEXT("/pk")},
		{TEXT("/tpall"),         1, &UMT2AdminCommandComponent::CmdTeleportAll,  TEXT("/tpall")},
		{TEXT("/netprofiler"),   3, &UMT2AdminCommandComponent::CmdNetworkProfiler,
			TEXT("/netprofiler <client|server> <0|1>")},
		{TEXT("/stats"),         1, &UMT2AdminCommandComponent::CmdStats,          TEXT("/stats")},
		{TEXT("/itemlist"),      1, &UMT2AdminCommandComponent::CmdItemList,       TEXT("/itemlist [page]")},
		{TEXT("/moblist"),       1, &UMT2AdminCommandComponent::CmdMobList,        TEXT("/moblist [page]")},
	};
	return Table;
}

namespace
{
	constexpr int32 RegistryListPageSize = 30;

	template <typename EntryMapType, typename NameResolverType>
	void SendRegistryPage(
		AMT2PlayerController* Controller, const TCHAR* Label, const EntryMapType& Entries,
		const TArray<FString>& Args, NameResolverType&& ResolveName)
	{
		if (!Controller)
		{
			return;
		}

		TArray<int32> Vnums;
		Entries.GetKeys(Vnums);
		Vnums.Sort();
		const int32 PageCount = FMath::Max(1, FMath::DivideAndRoundUp(Vnums.Num(), RegistryListPageSize));
		const int32 RequestedPage = Args.IsValidIndex(1) ? FCString::Atoi(*Args[1]) : 1;
		if (RequestedPage < 1 || RequestedPage > PageCount)
		{
			Controller->SendSystemChatMessage(FString::Printf(
				TEXT("[%s] Invalid page. Choose 1-%d."), Label, PageCount));
			return;
		}

		Controller->SendSystemChatMessage(FString::Printf(
			TEXT("[%s] Page %d/%d - %d entries"), Label, RequestedPage, PageCount, Vnums.Num()));
		const int32 FirstIndex = (RequestedPage - 1) * RegistryListPageSize;
		const int32 LastIndex = FMath::Min(FirstIndex + RegistryListPageSize, Vnums.Num());
		for (int32 Index = FirstIndex; Index < LastIndex; ++Index)
		{
			const int32 Vnum = Vnums[Index];
			Controller->SendSystemChatMessage(FString::Printf(
				TEXT("%d - %s"), Vnum, *ResolveName(Vnum)));
		}
	}
}

void UMT2AdminCommandComponent::CmdItemList(const TArray<FString>& Args)
{
	const AMT2PlayerCharacter* Player = GetPlayer();
	AMT2PlayerController* Controller = Player ? Cast<AMT2PlayerController>(Player->GetController()) : nullptr;
	UGameInstance* GameInstance = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
	UMT2VnumRegistrySubsystem* RegistrySubsystem =
		GameInstance ? GameInstance->GetSubsystem<UMT2VnumRegistrySubsystem>() : nullptr;
	UMT2VnumRegistry* Registry = RegistrySubsystem ? RegistrySubsystem->GetRegistry() : nullptr;
	if (!Controller || !RegistrySubsystem || !Registry)
	{
		SendResult(false, TEXT("Item registry is unavailable."));
		return;
	}

	SendRegistryPage(Controller, TEXT("Items"), Registry->GetItemTemplates(), Args,
		[RegistrySubsystem](int32 Vnum)
		{
			const TSubclassOf<UMT2ItemTemplate> ItemClass = RegistrySubsystem->ResolveItemTemplateClass(Vnum);
			const UMT2ItemTemplate* Item = ItemClass ? ItemClass->GetDefaultObject<UMT2ItemTemplate>() : nullptr;
			return Item && !Item->DisplayName.IsEmpty() ? Item->DisplayName.ToString() : TEXT("Unknown item");
		});
}

void UMT2AdminCommandComponent::CmdMobList(const TArray<FString>& Args)
{
	const AMT2PlayerCharacter* Player = GetPlayer();
	AMT2PlayerController* Controller = Player ? Cast<AMT2PlayerController>(Player->GetController()) : nullptr;
	UGameInstance* GameInstance = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
	UMT2VnumRegistrySubsystem* RegistrySubsystem =
		GameInstance ? GameInstance->GetSubsystem<UMT2VnumRegistrySubsystem>() : nullptr;
	UMT2VnumRegistry* Registry = RegistrySubsystem ? RegistrySubsystem->GetRegistry() : nullptr;
	if (!Controller || !RegistrySubsystem || !Registry)
	{
		SendResult(false, TEXT("Mob registry is unavailable."));
		return;
	}

	SendRegistryPage(Controller, TEXT("Mobs"), Registry->GetMobClasses(), Args,
		[RegistrySubsystem](int32 Vnum)
		{
			const TSubclassOf<AMT2Mob> MobClass = RegistrySubsystem->ResolveMobClass(Vnum);
			const AMT2Mob* Mob = MobClass ? MobClass->GetDefaultObject<AMT2Mob>() : nullptr;
			return Mob ? Mob->GetMobDisplayName() : TEXT("Unknown mob");
		});
}

void UMT2AdminCommandComponent::CmdStats(const TArray<FString>&)
{
	const AMT2PlayerCharacter* Player = GetPlayer();
	AMT2PlayerController* Controller = Player ? Cast<AMT2PlayerController>(Player->GetController()) : nullptr;
	const AMT2PlayerState* State = Player ? Player->GetPlayerState<AMT2PlayerState>() : nullptr;
	const UMT2CombatStatsComponent* CombatComponent = Player ? Player->GetCombatStatsComponent() : nullptr;
	const UMT2PrimaryStatsComponent* PrimaryComponent = Player ? Player->GetPrimaryStatsComponent() : nullptr;
	if (!Player || !Controller || !State || !CombatComponent || !PrimaryComponent)
	{
		SendResult(false, TEXT("Player stats are unavailable."));
		return;
	}

	const FMT2PrimaryStats Primary = PrimaryComponent->GetCalculatedStats();
	const FMT2PrimaryStats PrimaryBonus = PrimaryComponent->GetBonusStats();
	const FMT2CombatStats Combat = CombatComponent->GetCalculatedStats();
	const FMT2CombatStatBonuses CombatBonus = CombatComponent->GetBonuses();
	Controller->SendSystemChatMessage(FString::Printf(
		TEXT("[Stats] Level %d | EXP %lld/%lld"), State->GetCharacterLevel(), State->GetExperience(),
		State->GetRequiredExperienceForNextLevel()));
	Controller->SendSystemChatMessage(FString::Printf(
		TEXT("[Stats] STR %d (+%d) DEX %d (+%d) VIT %d (+%d) INT %d (+%d)"),
		Primary.Strength, PrimaryBonus.Strength, Primary.Dexterity, PrimaryBonus.Dexterity,
		Primary.Constitution, PrimaryBonus.Constitution, Primary.Intelligence, PrimaryBonus.Intelligence));
	Controller->SendSystemChatMessage(FString::Printf(
		TEXT("[Stats] Attack %.0f-%.0f (item %.0f-%.0f) | Defense %.0f (+%.0f) | Magic %.0f/%.0f"),
		Combat.DamageMin, Combat.DamageMax, CombatBonus.DamageMin, CombatBonus.DamageMax,
		Combat.Defense, CombatBonus.Defense, Combat.MagicAttack, Combat.MagicDefense));
	Controller->SendSystemChatMessage(FString::Printf(
		TEXT("[Stats] AttackSpeed %d (+%d) MoveSpeed %d (+%d) Range %.0f | Damage x%.3f"),
		Combat.AttackSpeed, CombatBonus.AttackSpeed, Combat.MovementSpeed, CombatBonus.MovementSpeed,
		Combat.AttackRange, Combat.DamageMultiplier));
	Controller->SendSystemChatMessage(FString::Printf(
		TEXT("[Stats] HP %.0f/%.0f | MP %.0f/%.0f | Stamina %.0f/%.0f"),
		Player->GetHealthComponent()->GetHealth(), Player->GetHealthComponent()->GetMaxHealth(),
		Player->GetManaComponent()->GetMana(), Player->GetManaComponent()->GetMaxMana(),
		Player->GetStaminaComponent()->GetStamina(), Player->GetStaminaComponent()->GetMaxStamina()));

	TArray<FString> ActiveBonuses;
	const UEnum* BonusEnum = StaticEnum<EMT2ItemBonusType>();
	for (int32 Type = 1; Type <= static_cast<int32>(EMT2ItemBonusType::AntiPenetratingChance); ++Type)
	{
		const int32 Value = Player->GetItemApplyBonus(Type);
		if (Value != 0)
		{
			const FString Name = BonusEnum
				? BonusEnum->GetDisplayNameTextByValue(Type).ToString() : FString::FromInt(Type);
			ActiveBonuses.Add(FString::Printf(TEXT("%s %+d"), *Name, Value));
		}
	}
	Controller->SendSystemChatMessage(FString::Printf(
		TEXT("[Bonuses] Critical %d%% | Penetrating %d%% | Crit resist %d%% | Pen resist %d%%"),
		Player->GetItemApplyBonus(15), Player->GetItemApplyBonus(16),
		Player->GetItemApplyBonus(90), Player->GetItemApplyBonus(91)));
	Controller->SendSystemChatMessage(ActiveBonuses.IsEmpty()
		? TEXT("[Bonuses] No active item/status bonuses.")
		: FString::Printf(TEXT("[Bonuses] %s"), *FString::Join(ActiveBonuses, TEXT(" | "))));
}

bool UMT2AdminCommandComponent::Execute(const FString& CommandLine)
{
	TArray<FString> Tokens;
	CommandLine.ParseIntoArrayWS(Tokens);
	if (Tokens.IsEmpty())
	{
		return false;
	}

	for (const FCommand& Command : GetCommandTable())
	{
		if (!Tokens[0].Equals(Command.Name, ESearchCase::IgnoreCase))
		{
			continue;
		}
		if (Tokens.Num() < Command.MinTokens)
		{
			UE_LOG(LogMT2AdminCmd, Warning, TEXT("[MT2Chat] Usage: %s"), Command.Usage);
			return false;
		}
		(this->*Command.Handler)(Tokens);
		return true;
	}
	UE_LOG(LogMT2AdminCmd, Warning, TEXT("[MT2Chat] Unknown command '%s'."), *Tokens[0]);
	return false;
}

void UMT2AdminCommandComponent::CmdSpawnMob(const TArray<FString>& Args)
{
	AMT2PlayerCharacter* Player = GetPlayer();
	UWorld* World = GetWorld();
	UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	UMT2VnumRegistrySubsystem* Registry =
		GameInstance ? GameInstance->GetSubsystem<UMT2VnumRegistrySubsystem>() : nullptr;
	const int32 Vnum = FCString::Atoi(*Args[1]);
	const int32 Count = Args.IsValidIndex(2) ? FMath::Clamp(FCString::Atoi(*Args[2]), 1, 20) : 1;
	const TSubclassOf<AMT2Mob> MobClass = Registry ? Registry->ResolveMobClass(Vnum) : nullptr;
	if (!Player || !MobClass)
	{
		UE_LOG(LogMT2AdminCmd, Warning, TEXT("[MT2Chat] /m %d: no mob registered for that VNUM."), Vnum);
		return;
	}

	for (int32 Index = 0; Index < Count; ++Index)
	{
		// Search out from a ring in front of the player for ground the mob's capsule actually fits
		// on, rather than dropping it at a fixed offset and letting the spawn handler shove it out
		// of whatever it landed inside.
		const FVector Location = AMT2Mob::FindGroundSpawnLocation(
			World, MobClass, Player->GetActorLocation(), 150.0f, Player);
		const FTransform SpawnTransform(Player->GetActorRotation(), Location);

		FActorSpawnParameters SpawnParameters;
		SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
		World->SpawnActor<AMT2Mob>(MobClass, SpawnTransform, SpawnParameters);
	}
}

void UMT2AdminCommandComponent::CmdGiveItem(const TArray<FString>& Args)
{
	const AMT2PlayerCharacter* Player = GetPlayer();
	UMT2InventoryComponent* Inventory = Player ? Player->GetInventoryComponent() : nullptr;
	const int32 Vnum = FCString::Atoi(*Args[1]);
	const int32 Count = Args.IsValidIndex(2) ? FMath::Max(FCString::Atoi(*Args[2]), 1) : 1;
	if (!Inventory || !Inventory->AddItemByVnum(Vnum, Count))
	{
		UE_LOG(LogMT2AdminCmd, Warning,
			TEXT("[MT2Chat] /i %d: item not registered for that VNUM, or inventory full."), Vnum);
	}
}

void UMT2AdminCommandComponent::CmdAddExperience(const TArray<FString>& Args)
{
	const AMT2PlayerCharacter* Player = GetPlayer();
	AMT2PlayerState* State = Player ? Player->GetPlayerState<AMT2PlayerState>() : nullptr;
	const int64 Amount = FCString::Atoi64(*Args[1]);
	if (Amount <= 0 || !State)
	{
		UE_LOG(LogMT2AdminCmd, Warning, TEXT("[MT2Chat] Usage: /xp <positive amount>"));
		return;
	}
	State->AddExperience(Amount);
	UE_LOG(LogMT2AdminCmd, Display,
		TEXT("[MT2Chat] Granted %lld EXP. Level=%d EXP=%lld/%lld"),
		Amount, State->GetCharacterLevel(), State->GetExperience(),
		State->GetRequiredExperienceForNextLevel());
}

void UMT2AdminCommandComponent::CmdAddMoney(const TArray<FString>& Args)
{
	const AMT2PlayerCharacter* Player = GetPlayer();
	AMT2PlayerState* State = Player ? Player->GetPlayerState<AMT2PlayerState>() : nullptr;
	const int64 Amount = FCString::Atoi64(*Args[1]);
	if (Amount <= 0 || !State)
	{
		UE_LOG(LogMT2AdminCmd, Warning, TEXT("[MT2Chat] Usage: /money <positive amount>"));
		return;
	}
	State->AddYang(Amount);
	UE_LOG(LogMT2AdminCmd, Display, TEXT("[MT2Chat] Granted %lld Yang. Total=%lld"), Amount, State->GetYang());
}

void UMT2AdminCommandComponent::CmdApplyEffect(const TArray<FString>& Args)
{
	AMT2PlayerCharacter* Player = GetPlayer();
	UMT2StatusEffectComponent* StatusEffects = Player ? Player->GetStatusEffectComponent() : nullptr;
	if (!StatusEffects)
	{
		return;
	}
	// Test hook for the status-effect system: "/a applyType value [durationSeconds]".
	// e.g. "/a 8 40 30" = +40 movement speed for 30s; omit duration (or pass -1) for infinite.
	FMT2StatusEffect Effect;
	Effect.Type = 1000 + FCString::Atoi(*Args[1]);
	Effect.ApplyType = FCString::Atoi(*Args[1]);
	Effect.ApplyValue = FCString::Atoi(*Args[2]);
	Effect.RemainingSeconds = Args.IsValidIndex(3) ? FCString::Atoi(*Args[3]) : -1;
	StatusEffects->AddEffect(Effect, true);
}

void UMT2AdminCommandComponent::CmdSetSkillGroup(const TArray<FString>& Args)
{
	const AMT2PlayerCharacter* Player = GetPlayer();
	AMT2PlayerState* State = Player ? Player->GetPlayerState<AMT2PlayerState>() : nullptr;
	UMT2SkillComponent* Skills = State ? State->GetSkillComponent() : nullptr;
	const int32 Group = FCString::Atoi(*Args[1]);
	if (!Skills || Group < 0 || Group > 2)
	{
		UE_LOG(LogMT2AdminCmd, Warning, TEXT("[MT2Chat] Usage: /setskillgroup <0|1|2>"));
		return;
	}
	// GM override: bypasses the level-5/once-only gates (0 clears the choice again).
	Skills->RestorePersistedSkillGroup(Group);
	if (UMT2PersistenceComponent* Persistence = State->GetPersistenceComponent())
	{
		Persistence->MarkDirty();
	}
	UE_LOG(LogMT2AdminCmd, Display, TEXT("[MT2Chat] Skill group set to %d."), Group);
}

void UMT2AdminCommandComponent::CmdSetSkill(const TArray<FString>& Args)
{
	const AMT2PlayerCharacter* Player = GetPlayer();
	AMT2PlayerState* State = Player ? Player->GetPlayerState<AMT2PlayerState>() : nullptr;
	UMT2SkillComponent* Skills = State ? State->GetSkillComponent() : nullptr;
	const int32 Vnum = FCString::Atoi(*Args[1]);
	const int32 Level = FCString::Atoi(*Args[2]);
	if (!Skills || Vnum <= 0 || Level < 0)
	{
		UE_LOG(LogMT2AdminCmd, Warning, TEXT("[MT2Chat] Usage: /setskill <vnum> <level 0-40>"));
		return;
	}
	Skills->SetSkillLevel(Vnum, Level);
	UE_LOG(LogMT2AdminCmd, Display,
		TEXT("[MT2Chat] Skill %d set to level %d."), Vnum, Skills->GetSkillLevel(Vnum));
}

void UMT2AdminCommandComponent::CmdSetRace(const TArray<FString>& Args)
{
	AMT2PlayerCharacter* Player = GetPlayer();
	AMT2PlayerState* State = Player ? Player->GetPlayerState<AMT2PlayerState>() : nullptr;
	if (!State)
	{
		return;
	}
	const FString Value = Args[1].ToLower();
	int32 RaceIndex = Value.IsNumeric() ? FCString::Atoi(*Value) :
		Value == TEXT("warrior") ? 0 : Value == TEXT("assassin") ? 1 :
		Value == TEXT("sura") ? 2 : Value == TEXT("shaman") ? 3 : INDEX_NONE;
	if (RaceIndex < 0 || RaceIndex > 3)
	{
		UE_LOG(LogMT2AdminCmd, Warning, TEXT("[MT2Chat] Usage: /race <warrior|assassin|sura|shaman>"));
		return;
	}
	FMT2CharacterAppearance Appearance = State->GetCharacterAppearance();
	if (Appearance.Race == static_cast<EMT2CharacterRace>(RaceIndex))
	{
		return;
	}
	if (!Player->GetInventoryComponent()->UnequipAllItems())
	{
		UE_LOG(LogMT2AdminCmd, Warning, TEXT("[MT2Chat] Race change failed: inventory has no room for all equipped items."));
		return;
	}
	State->ResetSkillsForRaceChange();
	Appearance.Race = static_cast<EMT2CharacterRace>(RaceIndex);
	State->SetCharacterAppearance(Appearance);
}

void UMT2AdminCommandComponent::CmdSetGender(const TArray<FString>& Args)
{
	const AMT2PlayerCharacter* Player = GetPlayer();
	AMT2PlayerState* State = Player ? Player->GetPlayerState<AMT2PlayerState>() : nullptr;
	if (!State)
	{
		return;
	}
	const FString Value = Args[1].ToLower();
	const int32 SexIndex = Value.IsNumeric() ? FCString::Atoi(*Value) :
		Value == TEXT("male") ? 0 : Value == TEXT("female") ? 1 : INDEX_NONE;
	if (SexIndex < 0 || SexIndex > 1)
	{
		UE_LOG(LogMT2AdminCmd, Warning, TEXT("[MT2Chat] Usage: /gender <male|female>"));
		return;
	}
	FMT2CharacterAppearance Appearance = State->GetCharacterAppearance();
	Appearance.Sex = static_cast<EMT2CharacterSex>(SexIndex);
	State->SetCharacterAppearance(Appearance);
}

void UMT2AdminCommandComponent::CmdSetStyle(const TArray<FString>& Args)
{
	const AMT2PlayerCharacter* Player = GetPlayer();
	AMT2PlayerState* State = Player ? Player->GetPlayerState<AMT2PlayerState>() : nullptr;
	if (!State)
	{
		return;
	}
	const FString Value = Args[1].ToLower();
	const int32 StyleIndex = Value.IsNumeric() ? FCString::Atoi(*Value) :
		Value == TEXT("red") ? 0 : Value == TEXT("blue") ? 1 : INDEX_NONE;
	if (StyleIndex < 0 || StyleIndex > 1)
	{
		UE_LOG(LogMT2AdminCmd, Warning, TEXT("[MT2Chat] Usage: /style <red|blue>"));
		return;
	}
	FMT2CharacterAppearance Appearance = State->GetCharacterAppearance();
	Appearance.Style = static_cast<EMT2CharacterStyle>(StyleIndex);
	State->SetCharacterAppearance(Appearance);
}

void UMT2AdminCommandComponent::SendResult(bool bSucceeded, const FString& Message) const
{
	const AMT2PlayerCharacter* Player = GetPlayer();
	AMT2PlayerController* Controller = Player ? Cast<AMT2PlayerController>(Player->GetController()) : nullptr;
	if (Controller)
	{
		Controller->SendSystemChatMessage(
			FString::Printf(TEXT("%s %s"), bSucceeded ? TEXT("[OK]") : TEXT("[ERROR]"), *Message));
	}
	UE_LOG(LogMT2AdminCmd, Display, TEXT("[MT2Chat] %s"), *Message);
}

void UMT2AdminCommandComponent::CmdGoto(const TArray<FString>& Args)
{
	AMT2PlayerCharacter* Player = GetPlayer();
	AMT2PlayerController* Controller = Player ? Cast<AMT2PlayerController>(Player->GetController()) : nullptr;
	UWorld* World = GetWorld();
	const FString RequestedMapName = Args[1].TrimStartAndEnd();
	FString MapName = RequestedMapName;
	if (!Controller || MapName.IsEmpty())
	{
		SendResult(false, TEXT("A player controller and destination map are required."));
		return;
	}
	const bool bMapIndexArgument = MapName.IsNumeric();
	bool bResolvedMapIndex = false;
	FString WorldPackagePath;
	if (const UMT2QuestTableAsset* Maps = LoadObject<UMT2QuestTableAsset>(
		nullptr, UMT2QuestTableAsset::GetAssetPath()))
	{
		const FMT2QuestMapDefinition* Map = bMapIndexArgument
			? Maps->FindMapByIndex(FCString::Atoi(*MapName))
			: Maps->FindMapById(MapName);
		if (Map)
		{
			MapName = Map->MapId;
			WorldPackagePath = Map->WorldPackagePath;
			bResolvedMapIndex = true;
		}
	}
	if (bMapIndexArgument && !bResolvedMapIndex)
	{
		SendResult(false, TEXT("Unknown map ID. Use an index present in the server map/index file."));
		return;
	}

#if WITH_EDITOR
	if (World && World->WorldType == EWorldType::PIE)
	{
		TArray<FString> MapAliases{RequestedMapName, MapName};
		if (!WorldPackagePath.IsEmpty())
		{
			MapAliases.AddUnique(WorldPackagePath);
		}
		const FString TargetPackage = MT2MapUtils::ResolvePIEWorldPackage(MapAliases);
		if (TargetPackage.IsEmpty())
		{
			SendResult(false, FString::Printf(
				TEXT("Map '%s' is not imported in this PIE project."), *MapName));
			return;
		}

		if (IConsoleVariable* AllowPIESeamlessTravel =
			IConsoleManager::Get().FindConsoleVariable(TEXT("net.AllowPIESeamlessTravel")))
		{
			AllowPIESeamlessTravel->Set(1, ECVF_SetByCode);
		}
		if (AGameModeBase* GameMode = World->GetAuthGameMode())
		{
			GameMode->bUseSeamlessTravel = true;
		}

		const FString TravelUrl = TargetPackage + TEXT("?pie_portal_town=1?SeamlessTravel");
		if (!World->ServerTravel(TravelUrl, true))
		{
			SendResult(false, FString::Printf(
				TEXT("PIE could not start travel to %s."), *MapName));
			return;
		}
		SendResult(true, FString::Printf(TEXT("Traveling to %s in PIE."), *MapName));
		return;
	}
#endif

	UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	UMT2ServerRuntimeSubsystem* Runtime = GameInstance
		? GameInstance->GetSubsystem<UMT2ServerRuntimeSubsystem>() : nullptr;
	if (!Runtime || !Runtime->IsMapServer())
	{
		SendResult(false, TEXT("Map transfer requires an active map-server runtime."));
		return;
	}
	const int32 PreferredChannel = Runtime->GetConfig().Channel;
	if (!Controller->RequestMapTransferFromServer(MapName, PreferredChannel, true))
	{
		SendResult(false, TEXT("Map transfer could not be started."));
		return;
	}
	SendResult(true, FString::Printf(
		TEXT("Traveling to %s (preferring channel %d)."), *MapName, PreferredChannel));
}

void UMT2AdminCommandComponent::CmdTeleport(const TArray<FString>& Args)
{
	AMT2PlayerCharacter* Player = GetPlayer();
	UWorld* World = GetWorld();
	auto ParseCoordinate = [](const FString& Text, double& OutValue)
	{
		if (!Text.IsNumeric()) return false;
		OutValue = FCString::Atod(*Text);
		return FMath::IsFinite(OutValue);
	};
	double X = 0.0;
	double Y = 0.0;
	double Z = 0.0;
	if (!Player || !World || !ParseCoordinate(Args[1], X) || !ParseCoordinate(Args[2], Y) ||
		(Args.IsValidIndex(3) && !ParseCoordinate(Args[3], Z)))
	{
		SendResult(false, TEXT("Usage: /tp <x> <y> [z]"));
		return;
	}

	if (!Args.IsValidIndex(3))
	{
		const FVector TraceStart(X, Y, 1000000.0);
		const FVector TraceEnd(X, Y, -1000000.0);
		FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(MT2AdminTeleportGround), false, Player);
		FHitResult GroundHit;
		if (!World->LineTraceSingleByObjectType(
			GroundHit, TraceStart, TraceEnd, FCollisionObjectQueryParams(ECC_WorldStatic), QueryParams))
		{
			SendResult(false, TEXT("No ground found at those coordinates."));
			return;
		}
		const UCapsuleComponent* Capsule = Player->GetCapsuleComponent();
		Z = GroundHit.ImpactPoint.Z + (Capsule ? Capsule->GetScaledCapsuleHalfHeight() : 90.0f) + 2.0f;
	}

	const FVector Destination(X, Y, Z);
	if (!Player->TeleportTo(Destination, Player->GetActorRotation(), false, true))
	{
		SendResult(false, TEXT("Teleport destination is blocked."));
		return;
	}
	if (AController* Controller = Player->GetController()) Controller->StopMovement();
	if (AMT2PlayerState* State = Player->GetPlayerState<AMT2PlayerState>())
	{
		if (UMT2PersistenceComponent* Persistence = State->GetPersistenceComponent()) Persistence->MarkDirty();
	}
	SendResult(true, FString::Printf(TEXT("Teleported to %.0f %.0f %.0f."), X, Y, Z));
}

void UMT2AdminCommandComponent::CmdPersist(const TArray<FString>&)
{
	UGameInstance* GameInstance = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
	UMT2ServerRuntimeSubsystem* Runtime = GameInstance
		? GameInstance->GetSubsystem<UMT2ServerRuntimeSubsystem>() : nullptr;
	if (!Runtime)
	{
		SendResult(false, TEXT("Server runtime is unavailable."));
		return;
	}
	TWeakObjectPtr<UMT2AdminCommandComponent> WeakThis(this);
	Runtime->RequestClusterPersist([WeakThis](bool bSucceeded, const FString& Message)
	{
		if (WeakThis.IsValid()) WeakThis->SendResult(bSucceeded, Message);
	});
}

void UMT2AdminCommandComponent::CmdShutdown(const TArray<FString>&)
{
	UGameInstance* GameInstance = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
	UMT2ServerRuntimeSubsystem* Runtime = GameInstance
		? GameInstance->GetSubsystem<UMT2ServerRuntimeSubsystem>() : nullptr;
	if (!Runtime)
	{
		SendResult(false, TEXT("Server runtime is unavailable."));
		return;
	}
	TWeakObjectPtr<UMT2AdminCommandComponent> WeakThis(this);
	Runtime->RequestClusterShutdown(false, [WeakThis](bool bSucceeded, const FString& Message)
	{
		if (WeakThis.IsValid()) WeakThis->SendResult(bSucceeded, Message);
	});
}

void UMT2AdminCommandComponent::CmdReboot(const TArray<FString>&)
{
	UGameInstance* GameInstance = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
	UMT2ServerRuntimeSubsystem* Runtime = GameInstance
		? GameInstance->GetSubsystem<UMT2ServerRuntimeSubsystem>() : nullptr;
	if (!Runtime)
	{
		SendResult(false, TEXT("Server runtime is unavailable."));
		return;
	}
	TWeakObjectPtr<UMT2AdminCommandComponent> WeakThis(this);
	Runtime->RequestClusterShutdown(true, [WeakThis](bool bSucceeded, const FString& Message)
	{
		if (WeakThis.IsValid()) WeakThis->SendResult(bSucceeded, Message);
	});
}

void UMT2AdminCommandComponent::CmdServers(const TArray<FString>&)
{
	UGameInstance* GameInstance = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
	UMT2ServerRuntimeSubsystem* Runtime = GameInstance
		? GameInstance->GetSubsystem<UMT2ServerRuntimeSubsystem>() : nullptr;
	if (!Runtime)
	{
		SendResult(false, TEXT("Server runtime is unavailable."));
		return;
	}
	TWeakObjectPtr<UMT2AdminCommandComponent> WeakThis(this);
	Runtime->RequestClusterServerList([WeakThis](bool bSucceeded, const FString& Message)
	{
		if (WeakThis.IsValid()) WeakThis->SendResult(bSucceeded, Message);
	});
}

void UMT2AdminCommandComponent::CmdTogglePk(const TArray<FString>&)
{
	const AMT2PlayerCharacter* Player = GetPlayer();
	AMT2PlayerState* State = Player ? Player->GetPlayerState<AMT2PlayerState>() : nullptr;
	if (!State)
	{
		SendResult(false, TEXT("Player state is unavailable."));
		return;
	}
	State->SetAggressiveMode(!State->IsAggressiveMode());
	SendResult(true, FString::Printf(TEXT("Aggressive (PK) mode is now %s."),
		State->IsAggressiveMode() ? TEXT("ON") : TEXT("OFF")));
}

void UMT2AdminCommandComponent::CmdTeleportAll(const TArray<FString>&)
{
	const AMT2PlayerCharacter* Player = GetPlayer();
	UGameInstance* GameInstance = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
	UMT2ServerRuntimeSubsystem* Runtime = GameInstance
		? GameInstance->GetSubsystem<UMT2ServerRuntimeSubsystem>() : nullptr;
	if (!Player || !Runtime)
	{
		SendResult(false, TEXT("Server runtime is unavailable."));
		return;
	}
	const FVector Location = Player->GetActorLocation();
	TWeakObjectPtr<UMT2AdminCommandComponent> WeakThis(this);
	Runtime->RequestSummonAllPlayers(Location, [WeakThis](bool bSucceeded, const FString& Message)
	{
		if (WeakThis.IsValid()) WeakThis->SendResult(bSucceeded, Message);
	});
}

void UMT2AdminCommandComponent::CmdNetworkProfiler(const TArray<FString>& Args)
{
	const FString Target = Args[1].ToLower();
	const bool bValidValue = Args[2] == TEXT("0") || Args[2] == TEXT("1");
	if ((Target != TEXT("client") && Target != TEXT("server")) || !bValidValue)
	{
		SendResult(false, TEXT("Usage: /netprofiler <client|server> <0|1>"));
		return;
	}

	const bool bEnabled = Args[2] == TEXT("1");
	AMT2PlayerCharacter* Player = GetPlayer();
	AMT2PlayerController* Controller =
		Player ? Cast<AMT2PlayerController>(Player->GetController()) : nullptr;
	if (Target == TEXT("client"))
	{
		if (!Controller)
		{
			SendResult(false, TEXT("Owning client is unavailable."));
			return;
		}
		Controller->ClientSetNetworkProfilerEnabled(bEnabled);
		SendResult(true, FString::Printf(TEXT("Requested client network profiler %s."),
			bEnabled ? TEXT("start") : TEXT("stop")));
		return;
	}

#if USE_NETWORK_PROFILER
	GNetworkProfiler.Exec(GetWorld(), bEnabled ? TEXT("ENABLE") : TEXT("DISABLE"), *GLog);
	SendResult(true, FString::Printf(TEXT("Server network profiler %s."),
		bEnabled ? TEXT("enabled") : TEXT("disabled")));
#else
	SendResult(false, TEXT("Network profiler is unavailable in this build."));
#endif
}
