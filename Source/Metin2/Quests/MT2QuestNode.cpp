/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "Quests/MT2QuestNode.h"
#include "Config/MT2PathSettings.h"

#include "Characters/MT2PlayerCharacter.h"
#include "Components/MT2StatusEffectComponent.h"
#include "Components/MT2ManaComponent.h"
#include "Components/MT2HealthComponent.h"
#include "Trade/MT2TradeComponent.h"
#include "Core/MT2VnumRegistrySubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Game/MT2GameStateBase.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameModeBase.h"
#include "HAL/IConsoleManager.h"
#include "Items/MT2InventoryComponent.h"
#include "Items/MT2WorldItem.h"
#include "Items/Use/MT2MountItemTemplate.h"
#include "Mobs/MT2Mob.h"
#include "Mounts/MT2MountComponent.h"
#include "Mounts/MT2MountDefinition.h"
#include "Npcs/MT2Npc.h"
#include "Npcs/MT2NpcShopComponent.h"
#include "Party/MT2Party.h"
#include "Player/MT2PlayerController.h"
#include "Player/MT2PlayerState.h"
#include "Quests/MT2Quest.h"
#include "Quests/MT2QuestCondition.h"
#include "Quests/MT2QuestExpression.h"
#include "Quests/MT2QuestEntitySubsystem.h"
#include "Quests/MT2QuestManagerComponent.h"
#include "Quests/MT2QuestTableAsset.h"
#include "Skills/MT2SkillComponent.h"
#include "World/MT2MapPresentationActor.h"
#include "World/MT2MapUtils.h"
#include "Components/CapsuleComponent.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"

namespace
{
	// Expression fields override their literal counterpart when filled in. A malformed expression falls
	// back to the literal rather than silently acting on a garbage value.
	int32 ResolveAmount(const FString& Expression, int32 Literal, const FMT2QuestContext& Context)
	{
		if (Expression.IsEmpty())
		{
			return Literal;
		}
		bool bOk = true;
		const FMT2QuestValue Value = FMT2QuestExpression::Evaluate(Expression, Context, bOk);
		return bOk ? Value.AsInt() : Literal;
	}

	int64 ResolveAmount64(const FString& Expression, int64 Literal, const FMT2QuestContext& Context)
	{
		if (Expression.IsEmpty())
		{
			return Literal;
		}
		bool bOk = true;
		const FMT2QuestValue Value = FMT2QuestExpression::Evaluate(Expression, Context, bOk);
		return bOk ? static_cast<int64>(Value.Number) : Literal;
	}

	bool AllConditionsPass(
		const TArray<TObjectPtr<UMT2QuestCondition>>& Conditions, const FMT2QuestContext& Context)
	{
		for (const TObjectPtr<UMT2QuestCondition>& Condition : Conditions)
		{
			if (Condition && !Condition->Evaluate(Context))
			{
				return false;
			}
		}
		return true;
	}

	FString ResolveCurrentMapId(const AMT2PlayerCharacter* Player)
	{
		if (!Player || !Player->GetWorld())
		{
			return FString();
		}
		if (const AMT2GameStateBase* GameState =
			Player->GetWorld()->GetGameState<AMT2GameStateBase>())
		{
			if (!GameState->GetMapId().IsEmpty())
			{
				return FPackageName::GetShortName(GameState->GetMapId());
			}
		}
		for (TActorIterator<AMT2MapPresentationActor> It(Player->GetWorld()); It; ++It)
		{
			if (!It->MapId.IsEmpty())
			{
				return FPackageName::GetShortName(It->MapId);
			}
		}
		return FPackageName::GetShortName(Player->GetWorld()->GetOutermost()->GetName());
	}

	bool TeleportQuestPlayerOnCurrentMap(AMT2PlayerCharacter* Player, const FVector2D& Position)
	{
		if (!Player || !Player->GetWorld())
		{
			return false;
		}
		FHitResult GroundHit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(MT2QuestWarpGround), false, Player);
		const FVector Start(Position.X, Position.Y, 1000000.0);
		const FVector End(Position.X, Position.Y, -1000000.0);
		if (!Player->GetWorld()->LineTraceSingleByObjectType(
			GroundHit, Start, End, FCollisionObjectQueryParams(ECC_WorldStatic), Params))
		{
			return false;
		}
		const UCapsuleComponent* Capsule = Player->GetCapsuleComponent();
		const float HalfHeight = Capsule ? Capsule->GetScaledCapsuleHalfHeight() : 90.0f;
		Player->GetCharacterMovement()->StopMovementImmediately();
		return Player->SetActorLocation(
			GroundHit.ImpactPoint + FVector(0.0, 0.0, HalfHeight + 2.0), false, nullptr,
			ETeleportType::ResetPhysics);
	}

#if WITH_EDITOR
	FString ResolvePIEWorldPackage(const FMT2QuestMapDefinition& Map)
	{
		TArray<FString> Candidates;
		if (!Map.WorldPackagePath.IsEmpty())
		{
			Candidates.Add(Map.WorldPackagePath);
		}
		Candidates.AddUnique(UMT2PathSettings::Format(TEXT("GameMapTemplate"), TEXT("%s"), *FPaths::GetCleanFilename(Map.MapId)));

		// Old server and client identifiers sometimes differ. atlasinfo.txt and server Setting.txt
		// share the authoritative global origin and dimensions, so use those instead of aliases.
		TArray<FString> AtlasLines;
		const FString AtlasPath =
			UMT2PathSettings::Path(TEXT("Legacy_atlasinfo"));
		if (FFileHelper::LoadFileToStringArray(AtlasLines, *AtlasPath))
		{
			for (FString Line : AtlasLines)
			{
				int32 CommentIndex = Line.Find(TEXT("#"));
				if (CommentIndex != INDEX_NONE)
				{
					Line.LeftInline(CommentIndex);
				}
				TArray<FString> Parts;
				Line.ParseIntoArrayWS(Parts);
				if (Parts.Num() < 5)
				{
					continue;
				}
				Parts[0].RemoveFromStart(FString::Chr(static_cast<TCHAR>(0xFEFF)));

				const FVector2D Origin(FCString::Atod(*Parts[1]), FCString::Atod(*Parts[2]));
				const FVector2D Size(FCString::Atod(*Parts[3]) * 128.0 * 200.0,
					FCString::Atod(*Parts[4]) * 128.0 * 200.0);
				if (Origin.Equals(Map.GlobalOrigin, 1.0) && Size.Equals(Map.WorldSize, 1.0))
				{
					Candidates.AddUnique(UMT2PathSettings::Format(TEXT("GameMapTemplate"), TEXT("%s"), *FPaths::GetCleanFilename(Parts[0])));
				}
			}
		}

		return MT2MapUtils::ResolvePIEWorldPackage(Candidates);
	}

	bool TravelQuestPlayerInPIE(
		AMT2PlayerCharacter* Player, const FMT2QuestMapDefinition& Map,
		const FVector2D& Position)
	{
		UWorld* World = Player ? Player->GetWorld() : nullptr;
		if (!World || World->WorldType != EWorldType::PIE)
		{
			return false;
		}
		const FString TargetPackage = ResolvePIEWorldPackage(Map);
		if (TargetPackage.IsEmpty())
		{
			UE_LOG(LogMT2QuestRuntime, Error,
				TEXT("[Quest] PIE map %s has no imported world package."), *Map.MapId);
			return false;
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
		const FString TravelUrl = FString::Printf(
			TEXT("%s?pie_portal_x=%.9g?pie_portal_y=%.9g?SeamlessTravel"),
			*TargetPackage, Position.X, Position.Y);
		UE_LOG(LogMT2QuestRuntime, Display,
			TEXT("[Quest] PIE traveling from server map id %s to world %s at %.0f, %.0f."),
			*Map.MapId, *TargetPackage, Position.X, Position.Y);
		return World->ServerTravel(TravelUrl, true);
	}
#endif
}

bool UMT2QuestNode::PassesConditions(const FMT2QuestContext& Context) const
{
	return AllConditionsPass(Conditions, Context);
}

EMT2QuestNodeResult UMT2QuestNode::Execute(const FMT2QuestContext& Context)
{
	// Base class routes to the Blueprint hook, so a node Blueprint only implements Run.
	Run(Context);
	return EMT2QuestNodeResult::Continue;
}

EMT2QuestNodeResult UMT2QuestNode_Say::Execute(const FMT2QuestContext& Context)
{
	if (Context.Manager)
	{
		Context.Manager->AppendSay(Title, Lines);
	}
	return EMT2QuestNodeResult::Continue;
}

bool UMT2QuestNode_Select::BuildTableOptions(const FMT2QuestContext& Context, TArray<FText>& OutOptions) const
{
	OutOptions.Reset();
	bool bOk = false;
	const FMT2QuestValue Table = FMT2QuestExpression::Evaluate(TableExpression, Context, bOk);
	if (!bOk || !Table.IsTableRef()) { return false; }
	const int32 Length = Table.GetTableLength();
	OutOptions.Reserve(Length);
	for (int32 Index = 1; Index <= Length; ++Index)
	{
		const FMT2QuestValue Value = Table.GetTableValue(FMT2QuestValue(Index));
		if (Value.bIsNil || Value.bIsBoolean || Value.IsTableRef()) { OutOptions.Reset(); return false; }
		OutOptions.Add(FText::FromString(Value.bIsText ? Value.Text : FString::Printf(TEXT("%.14g"), Value.Number)));
	}
	return !OutOptions.IsEmpty();
}

EMT2QuestNodeResult UMT2QuestNode_Select::Execute(const FMT2QuestContext& Context)
{
	if (!Context.Manager)
	{
		return EMT2QuestNodeResult::Stop;
	}

	// Only offer choices whose conditions pass, keeping labels and branches index-aligned.
	TArray<FText> Options;
	TArray<FMT2QuestChoiceBranch> Branches;
	if (!TableExpression.IsEmpty())
	{
		if (!BuildTableOptions(Context, Options))
		{
			UE_LOG(LogMT2QuestRuntime, Warning, TEXT("Quest select_table has invalid or empty options: %s"), *TableExpression);
			return EMT2QuestNodeResult::Stop;
		}
		Context.Manager->PresentChoices(Options, {}, ResultVariable, ResultExpression, false);
		return EMT2QuestNodeResult::Suspend;
	}
	for (const FMT2QuestChoice& Choice : Choices)
	{
		if (!AllConditionsPass(Choice.Conditions, Context))
		{
			continue;
		}
		Options.Add(Choice.Label);
		FMT2QuestChoiceBranch& Branch = Branches.AddDefaulted_GetRef();
		Branch.Nodes = Choice.Nodes;
	}

	if (Options.IsEmpty())
	{
		// Nothing selectable: fall through and let the block end (the page still flushes).
		return EMT2QuestNodeResult::Continue;
	}

	// In variable mode the dispatch is the if-chain that follows, so no branches are pushed.
	if (!ResultVariable.IsNone())
	{
		Branches.Reset();
	}
	Context.Manager->PresentChoices(Options, Branches, ResultVariable, ResultExpression);
	return EMT2QuestNodeResult::Suspend;
}

EMT2QuestNodeResult UMT2QuestNode_NpcLock::Execute(const FMT2QuestContext& Context)
{
	if (!Context.Manager) { return EMT2QuestNodeResult::Stop; }
	if (bUnlock) { return Context.Manager->UnlockQuestNpc(Context) ? EMT2QuestNodeResult::Continue : EMT2QuestNodeResult::Stop; }
	// A standalone Lua call discards the result. Conditional lock calls use the expression binding.
	Context.Manager->TryLockQuestNpc(Context);
	return EMT2QuestNodeResult::Continue;
}

EMT2QuestNodeResult UMT2QuestNode_PurgeNpc::Execute(const FMT2QuestContext& Context)
{
	if (!Context.Manager || !Context.Manager->PurgeQuestNpc(Context))
	{
		UE_LOG(LogTemp, Error, TEXT("Quest npc.purge rejected invalid authority, player context or NPC target."));
		return EMT2QuestNodeResult::Stop;
	}
	return EMT2QuestNodeResult::Continue;
}

EMT2QuestNodeResult UMT2QuestNode_If::Execute(const FMT2QuestContext& Context)
{
	// The chosen branch is pushed as a nested frame, so it runs before the rest of this block.
	const TArray<TObjectPtr<UMT2QuestNode>>& Branch = AllConditionsPass(Test, Context) ? Then : Else;
	if (Context.Manager)
	{
		Context.Manager->PushFrame(Branch);
	}
	return EMT2QuestNodeResult::Continue;
}

EMT2QuestNodeResult UMT2QuestNode_GiveItem::Execute(const FMT2QuestContext& Context)
{
	if (UMT2InventoryComponent* Inventory =
		Context.Player ? Context.Player->GetInventoryComponent() : nullptr)
	{
		int32 ResolvedVnum = ResolveAmount(ItemVnumExpression, ItemVnum, Context);
		if (Context.PlayerState)
		{
			if (const int32* RaceVnum = ItemVnumByRace.Find(
				Context.PlayerState->GetCharacterAppearance().Race))
			{
				ResolvedVnum = *RaceVnum;
			}
		}
		const int32 RequestedCount = FMath::Max(ResolveAmount(CountExpression, Count, Context), 1);
		const int32 AddedCount = Inventory->AddItemByVnumPartial(ResolvedVnum, RequestedCount);
		UE_LOG(LogMT2QuestRuntime, Log,
			TEXT("[Quest] Player=%s Quest=%s gave ItemVnum=%d Count=%d/%d."),
			Context.PlayerState ? *Context.PlayerState->GetCharacterName() : TEXT("Unknown"),
			Context.Quest ? *Context.Quest->GetQuestId().ToString() : TEXT("Unknown"),
			ResolvedVnum, AddedCount, RequestedCount);
	}
	return EMT2QuestNodeResult::Continue;
}

EMT2QuestNodeResult UMT2QuestNode_TakeItem::Execute(const FMT2QuestContext& Context)
{
	if (UMT2InventoryComponent* Inventory =
		Context.Player ? Context.Player->GetInventoryComponent() : nullptr)
	{
		Inventory->RemoveItemByVnum(
			ResolveAmount(ItemVnumExpression, ItemVnum, Context),
			FMath::Max(ResolveAmount(CountExpression, Count, Context), 1));
	}
	return EMT2QuestNodeResult::Continue;
}

EMT2QuestNodeResult UMT2QuestNode_GiveReward::Execute(const FMT2QuestContext& Context)
{
	if (AMT2PlayerState* State = Context.PlayerState)
	{
		int64 ExperienceAmount = ResolveAmount64(ExperienceExpression, Experience, Context);

		// give_exp_perc pays a percentage of one level's worth of experience. The level it is measured
		// against is the mission's, not the player's, so a player who out-levelled the quest still gets
		// what the quest promised.
		const float Percent = ExperiencePercentExpression.IsEmpty()
			? ExperiencePercent
			: static_cast<float>(ResolveAmount64(ExperiencePercentExpression, 0, Context));
		if (Percent != 0.0f)
		{
			const int32 Level = static_cast<int32>(
				ResolveAmount64(ExperienceLevelExpression, ExperienceLevel, Context));
			const int64 LevelCost = State->GetRequiredExperienceForLevel(
				Level > 0 ? Level : State->GetCharacterLevel());
			ExperienceAmount += static_cast<int64>(LevelCost * Percent / 100.0);
		}
		const int64 GoldAmount = ResolveAmount64(GoldExpression, Gold, Context);
		if (ExperienceAmount != 0)
		{
			State->AddExperience(ExperienceAmount);
			UE_LOG(LogMT2QuestRuntime, Log, TEXT("[Quest] Player=%s Quest=%s gave Experience=%lld."),
				*State->GetCharacterName(),
				Context.Quest ? *Context.Quest->GetQuestId().ToString() : TEXT("Unknown"),
				ExperienceAmount);
		}
		if (GoldAmount != 0)
		{
			State->AddYang(GoldAmount); // negative takes gold (old pc.change_money)
			UE_LOG(LogMT2QuestRuntime, Log, TEXT("[Quest] Player=%s Quest=%s changed Yang=%lld."),
				*State->GetCharacterName(),
				Context.Quest ? *Context.Quest->GetQuestId().ToString() : TEXT("Unknown"), GoldAmount);
		}
	}
	return EMT2QuestNodeResult::Continue;
}

EMT2QuestNodeResult UMT2QuestNode_SetSkillLevel::Execute(const FMT2QuestContext& Context)
{
	if (UMT2SkillComponent* Skills =
		Context.PlayerState ? Context.PlayerState->GetSkillComponent() : nullptr)
	{
		const int32 ResolvedVnum = ResolveAmount(SkillVnumExpression, SkillVnum, Context);
		const int32 ResolvedLevel = FMath::Max(ResolveAmount(LevelExpression, Level, Context), 0);
		Skills->SetSkillLevel(ResolvedVnum, ResolvedLevel);
	}
	return EMT2QuestNodeResult::Continue;
}

EMT2QuestNodeResult UMT2QuestNode_ChangeMana::Execute(const FMT2QuestContext& Context)
{
	UMT2ManaComponent* Mana = Context.Player ? Context.Player->GetManaComponent() : nullptr;
	if (!Mana || !Context.Player->HasAuthority())
	{
		return EMT2QuestNodeResult::Continue;
	}
	const int32 ResolvedDelta = ResolveAmount(DeltaExpression, Delta, Context);
	if (ResolvedDelta < 0 && Mana->GetMana() < -ResolvedDelta)
	{
		return EMT2QuestNodeResult::Continue;
	}
	Mana->SetMana(Mana->GetMana() + ResolvedDelta);
	return EMT2QuestNodeResult::Continue;
}

EMT2QuestNodeResult UMT2QuestNode_While::Execute(const FMT2QuestContext& Context)
{
	if (!Context.Manager ||
		(!(bExecuteBodyFirst && !Context.Manager->IsExecutingLoop(this)) &&
		 !FMT2QuestExpression::EvaluateBool(ConditionExpression, Context)))
	{
		return EMT2QuestNodeResult::Continue;
	}
	if (!Context.Manager->ConsumeLoopIteration(this, MaximumIterations))
	{
		return EMT2QuestNodeResult::Stop;
	}

	if (Context.Manager->IsExecutingLoop(this))
	{
		Context.Manager->PushLoopFrame({}, this);
	}
	else
	{
		TArray<TObjectPtr<UMT2QuestNode>> Iteration = Body;
		Iteration.Add(this);
		Context.Manager->PushLoopFrame(Iteration, this);
	}
	return EMT2QuestNodeResult::Continue;
}

EMT2QuestNodeResult UMT2QuestNode_Break::Execute(const FMT2QuestContext& Context)
{
	return Context.Manager && Context.Manager->BreakLoop()
		? EMT2QuestNodeResult::Continue : EMT2QuestNodeResult::Stop;
}

EMT2QuestNodeResult UMT2QuestNode_ForEach::Execute(const FMT2QuestContext& Context)
{
	if (!Context.Manager) { return EMT2QuestNodeResult::Stop; }
	bool bOk = false;
	const FMT2QuestValue Table = FMT2QuestExpression::Evaluate(TableExpression, Context, bOk);
	if (!bOk)
	{
		UE_LOG(LogTemp, Error, TEXT("Quest ipairs could not evaluate table expression: %s"), *TableExpression);
		return EMT2QuestNodeResult::Stop;
	}
	return Context.Manager->PushIpairsFrame(Body, Table, IndexVariable, ValueVariable, MaximumIterations)
		? EMT2QuestNodeResult::Continue : EMT2QuestNodeResult::Stop;
}

EMT2QuestNodeResult UMT2QuestNode_TableCallback::Execute(const FMT2QuestContext& Context)
{
	if (!Context.Manager) { return EMT2QuestNodeResult::Stop; }
	bool bOk = false;
	const FMT2QuestValue Table = FMT2QuestExpression::Evaluate(TableExpression, Context, bOk);
	if (!bOk || !Context.Manager->PushTableCallbackFrame(Body, Table, KeyVariable, ValueVariable,
		bSequence, LocalVariables, ResultVariable, bResultQuestScoped, MaximumIterations))
	{
		UE_LOG(LogMT2QuestRuntime, Warning, TEXT("Quest table callback could not start: %s"), *TableExpression);
		return EMT2QuestNodeResult::Stop;
	}
	return EMT2QuestNodeResult::Continue;
}

EMT2QuestNodeResult UMT2QuestNode_ModifySkills::Execute(const FMT2QuestContext& Context)
{
	UMT2SkillComponent* Skills = Context.PlayerState ? Context.PlayerState->GetSkillComponent() : nullptr;
	if (!Skills || !Context.PlayerState->HasAuthority())
	{
		return EMT2QuestNodeResult::Continue;
	}

	int32 ResolvedValue = Value;
	if (!ValueExpression.IsEmpty())
	{
		bool bOk = false;
		ResolvedValue = FMT2QuestExpression::Evaluate(ValueExpression, Context, bOk).AsInt();
		if (!bOk)
		{
			return EMT2QuestNodeResult::Continue;
		}
	}

	switch (Operation)
	{
	case EMT2QuestSkillOperation::SetGroup:
		// Quest scripts already own their eligibility dialog/gates. Use the authority restore path so
		// this mirrors old pc.set_skill_group instead of applying the UI's one-shot selection gates.
		Skills->RestorePersistedSkillGroup(ResolvedValue);
		break;
	case EMT2QuestSkillOperation::ClearAll:
		Skills->ResetAllSkills();
		break;
	case EMT2QuestSkillOperation::ClearOne:
		Skills->SetSkillLevel(ResolvedValue, 0);
		break;
	default:
		break;
	}
	return EMT2QuestNodeResult::Continue;
}

EMT2QuestNodeResult UMT2QuestNode_CallHorse::Execute(const FMT2QuestContext& Context)
{
	UMT2MountComponent* Mounts = Context.Player ? Context.Player->GetMountComponent() : nullptr;
	UMT2SkillComponent* Skills =
		Context.PlayerState ? Context.PlayerState->GetSkillComponent() : nullptr;
	UGameInstance* GameInstance = Context.Player ? Context.Player->GetGameInstance() : nullptr;
	UMT2VnumRegistrySubsystem* Registry =
		GameInstance ? GameInstance->GetSubsystem<UMT2VnumRegistrySubsystem>() : nullptr;
	if (!Mounts || !Skills || !Registry)
	{
		return EMT2QuestNodeResult::Continue;
	}

	const int32 HorseLevel = Skills->GetSkillLevel(HorseSkillVnum);
	if (HorseLevel <= 0)
	{
		return EMT2QuestNodeResult::Continue;
	}
	const int32 HorseGradeIndex = FMath::Clamp((HorseLevel - 1) / 10, 0, 2);
	const TSubclassOf<UMT2ItemTemplate> BookClass =
		Registry->ResolveItemTemplateClass(BasicHorseBookVnum + HorseGradeIndex);
	const UMT2MountItemTemplate* Book = BookClass
		? Cast<UMT2MountItemTemplate>(BookClass->GetDefaultObject()) : nullptr;
	if (UMT2MountDefinition* Definition = Book ? Book->MountDefinition.LoadSynchronous() : nullptr)
	{
		Mounts->SetCalledHorse(Definition);
	}
	return EMT2QuestNodeResult::Continue;
}

EMT2QuestNodeResult UMT2QuestNode_ModifyHorse::Execute(const FMT2QuestContext& Context)
{
	UMT2MountComponent* Mounts = Context.Player ? Context.Player->GetMountComponent() : nullptr;
	UMT2SkillComponent* Skills =
		Context.PlayerState ? Context.PlayerState->GetSkillComponent() : nullptr;
	if (!Mounts || !Skills)
	{
		return EMT2QuestNodeResult::Continue;
	}

	switch (Operation)
	{
	case EMT2QuestHorseOperation::Ride:
		if (!Mounts->IsMounted()) { Mounts->ServerToggleHorse(); }
		break;
	case EMT2QuestHorseOperation::Unride:
		if (Mounts->IsMounted() && Mounts->GetMountedKind() == EMT2MountKind::Horse)
		{
			Mounts->ForceDismount();
		}
		break;
	case EMT2QuestHorseOperation::Unsummon:
		if (Mounts->IsMounted() && Mounts->GetMountedKind() == EMT2MountKind::Horse)
		{
			Mounts->ForceDismount();
		}
		Mounts->SetCalledHorse(nullptr);
		break;
	case EMT2QuestHorseOperation::Advance:
		Skills->SetSkillLevel(HorseSkillVnum, FMath::Min(
			Skills->GetSkillLevel(HorseSkillVnum) + 1, FMath::Max(MaximumHorseLevel, 1)));
		break;
	default:
		break;
	}
	return EMT2QuestNodeResult::Continue;
}

EMT2QuestNodeResult UMT2QuestNode_SetFlag::Execute(const FMT2QuestContext& Context)
{
	if (bPartyScoped)
	{
		AMT2Party* Party = Context.PlayerState ? Context.PlayerState->GetParty() : nullptr;
		if (Context.PlayerState && Context.PlayerState->HasAuthority())
		{
			FName ResolvedFlagName = FlagName;
			if (!PartyFlagNameExpression.IsEmpty())
			{
				bool bOk = false;
				const FMT2QuestValue Name = FMT2QuestExpression::Evaluate(PartyFlagNameExpression, Context, bOk);
				if (!bOk || Name.bIsNil || Name.bIsBoolean || Name.IsTableRef())
				{
					UE_LOG(LogMT2QuestRuntime, Error, TEXT("[Quest] Invalid party.setqf flag-name expression '%s'."), *PartyFlagNameExpression);
					return EMT2QuestNodeResult::Stop;
				}
				const FString NameText = Name.bIsText ? Name.Text : FString::Printf(TEXT("%.14g"), Name.Number);
				// party.setqf always prefixes the current quest, even when the Lua name contains dots.
				ResolvedFlagName = FName(*(Context.Quest ? Context.Quest->GetQuestId().ToString() + TEXT(".") + NameText : NameText));
			}
			double Number = Value;
			if (!ValueExpression.IsEmpty())
			{
				bool bOk = false;
				const FMT2QuestValue Evaluated = FMT2QuestExpression::Evaluate(TEXT("tonumber(") + ValueExpression + TEXT(")"), Context, bOk);
				if (!bOk)
				{
					UE_LOG(LogMT2QuestRuntime, Error, TEXT("[Quest] Invalid party.setqf value expression '%s'."), *ValueExpression);
					return EMT2QuestNodeResult::Stop;
				}
				Number = Evaluated.bIsNil ? 0.0 : Evaluated.Number;
			}
			const double Rounded = FMath::RoundHalfToEven(Number);
			if (!FMath::IsFinite(Rounded) || Rounded < MIN_int32 || Rounded > MAX_int32)
			{
				UE_LOG(LogMT2QuestRuntime, Error, TEXT("[Quest] party.setqf value is outside signed 32-bit range."));
				return EMT2QuestNodeResult::Stop;
			}
			const int32 Resolved = static_cast<int32>(Rounded);
			auto SetMemberFlag = [&](AMT2PlayerState* Member)
			{
				if (!IsValid(Member) || !Member->HasAuthority() || Member->GetWorld() != Context.PlayerState->GetWorld()) { return; }
				if (UMT2QuestManagerComponent* Manager = Member->GetQuestManagerComponent())
				{
					Manager->SetQuestFlag(ResolvedFlagName, bAdd ? Manager->GetQuestFlag(ResolvedFlagName, Context.Quest) + Resolved : Resolved, Context.Quest);
				}
			};
			if (Party && Party->HasAuthority())
			{
				for (AMT2PlayerState* Member : Party->GetMemberStates()) { SetMemberFlag(Member); }
			}
			else { SetMemberFlag(Context.PlayerState); }
		}
		return EMT2QuestNodeResult::Continue;
	}
	if (Context.Manager)
	{
		const int32 Resolved = ResolveAmount(ValueExpression, Value, Context);
		const int32 NewValue = bAdd
			? Context.Manager->GetQuestFlag(FlagName, Context.Quest) + Resolved : Resolved;
		Context.Manager->SetQuestFlag(FlagName, NewValue, Context.Quest);
	}
	return EMT2QuestNodeResult::Continue;
}

EMT2QuestNodeResult UMT2QuestNode_SetState::Execute(const FMT2QuestContext& Context)
{
	if (Context.Manager)
	{
		Context.Manager->SetQuestState(Context.Quest, StateName);
	}
	return EMT2QuestNodeResult::Continue;
}

EMT2QuestNodeResult UMT2QuestNode_Notice::Execute(const FMT2QuestContext& Context)
{
	if (Context.Player)
	{
		if (AMT2PlayerController* Controller = Cast<AMT2PlayerController>(Context.Player->GetController()))
		{
			Controller->SendSystemChatMessage(Message.ToString());
		}
	}
	return EMT2QuestNodeResult::Continue;
}

EMT2QuestNodeResult UMT2QuestNode_AssignValues::Execute(const FMT2QuestContext& Context)
{
	if (!Context.Manager || VariableNames.IsEmpty() || QuestScopedTargets.Num() != VariableNames.Num())
	{
		UE_LOG(LogMT2QuestRuntime, Warning, TEXT("Quest assignment has invalid context or target scopes."));
		return EMT2QuestNodeResult::Stop;
	}
	for (FName Name : VariableNames) { if (Name.IsNone()) { return EMT2QuestNodeResult::Stop; } }
	bool bOk = false;
	const TArray<FMT2QuestValue> Values = FMT2QuestExpression::EvaluateList(Expressions, Context, bOk);
	if (!bOk)
	{
		UE_LOG(LogMT2QuestRuntime, Warning, TEXT("Quest multiple assignment could not evaluate its RHS."));
		return EMT2QuestNodeResult::Stop;
	}
	// Lua evaluates every RHS against the old environment, then assigns targets left to right.
	for (int32 Index = 0; Index < VariableNames.Num(); ++Index)
	{
		const FMT2QuestValue Value = Values.IsValidIndex(Index) ? Values[Index].ToRuntimeTable() : FMT2QuestValue();
		if (QuestScopedTargets[Index]) { Context.Manager->SetQuestScriptVariable(VariableNames[Index], Value); }
		else { Context.Manager->SetScriptVariable(VariableNames[Index], Value); }
	}
	return EMT2QuestNodeResult::Continue;
}

EMT2QuestNodeResult UMT2QuestNode_SetVariable::Execute(const FMT2QuestContext& Context)
{
	if (Context.Manager && !VariableName.IsNone())
	{
		bool bOk = true;
		FMT2QuestValue Value = bHasInlineTable
			? FMT2QuestValue(&InlineTable, 0).ToRuntimeTable()
			: FMT2QuestExpression::Evaluate(Expression, Context, bOk);
		if (!bOk)
		{
			UE_LOG(LogMT2QuestRuntime, Warning,
				TEXT("[Quest] Could not evaluate assignment '%s = %s'."),
				*VariableName.ToString(), *Expression);
			return EMT2QuestNodeResult::Stop;
		}
		Value = Value.ToRuntimeTable();
		if (bQuestScoped)
		{
			Context.Manager->SetQuestScriptVariable(VariableName, Value);
		}
		else
		{
			Context.Manager->SetScriptVariable(VariableName, Value);
		}
	}
	return EMT2QuestNodeResult::Continue;
}

EMT2QuestNodeResult UMT2QuestNode_ModifyAffect::Execute(const FMT2QuestContext& Context)
{
	UMT2StatusEffectComponent* Effects =
		Context.Player ? Context.Player->GetStatusEffectComponent() : nullptr;
	if (!Effects || !Context.Player->HasAuthority())
	{
		return EMT2QuestNodeResult::Continue;
	}

	if (Operation == EMT2QuestAffectOperation::RemoveAllCollect)
	{
		Effects->RemoveEffectsByType(515); // AFFECT_COLLECT
		return EMT2QuestNodeResult::Continue;
	}
	if (Operation == EMT2QuestAffectOperation::RemoveHair)
	{
		Effects->RemoveEffectsByType(514); // AFFECT_HAIR
		return EMT2QuestNodeResult::Continue;
	}

	FMT2StatusEffect Effect;
	Effect.Kind = EMT2StatusEffectKind::StatBonus;
	Effect.ApplyType = ResolveAmount(ApplyTypeExpression, ApplyType, Context);
	Effect.ApplyValue = ResolveAmount(ValueExpression, Value, Context);
	Effect.RemainingSeconds = ResolveAmount(DurationExpression, DurationSeconds, Context);
	Effect.bRemoveOnDeath = false;
	if (Operation == EMT2QuestAffectOperation::AddHair)
	{
		Effect.Type = 514; // AFFECT_HAIR
		Effects->AddEffect(Effect, true);
	}
	else if (Operation == EMT2QuestAffectOperation::AddCollect)
	{
		Effect.Type = 515;
		Effects->AddEffect(Effect, false);
	}
	else
	{
		// affect.add is unique per quest and apply type in the old server. A stable combined id keeps
		// unrelated quest bonuses from replacing each other in the shared status-effect component.
		const uint32 QuestHash = Context.Quest ? GetTypeHash(Context.Quest->GetQuestId()) : 0;
		Effect.Type = 100000 + static_cast<int32>((QuestHash ^ GetTypeHash(Effect.ApplyType)) & 0x3fffffff);
		Effects->AddEffect(Effect, true);
	}
	return EMT2QuestNodeResult::Continue;
}

EMT2QuestNodeResult UMT2QuestNode_CallFunction::Execute(const FMT2QuestContext& Context)
{
	if (!Context.Manager || ResultVariables.Num() != ResultQuestScopes.Num() ||
		(!ResultVariables.IsEmpty() && !ResultVariable.IsNone()) || ResultVariables.Contains(NAME_None))
	{
		return EMT2QuestNodeResult::Stop;
	}
	// Capture all argument values in the caller's scope before binding any parameter.
	bool bOk = false;
	TArray<FMT2QuestValue> Arguments = FMT2QuestExpression::EvaluateList(ArgumentExpressions, Context, bOk);
	if (!bOk)
	{
		UE_LOG(LogMT2QuestRuntime, Warning, TEXT("Quest function '%s' arguments could not be evaluated."), *FunctionName.ToString());
		return EMT2QuestNodeResult::Stop;
	}
	// All supplied arguments execute, including extras. Missing parameters receive nil.
	Arguments.SetNum(ParameterNames.Num());
	return Context.Manager->BeginScriptFunction(Body, ResultVariable, bResultQuestScoped, ParameterNames, Arguments,
		ResultVariables, ResultQuestScopes)
		? EMT2QuestNodeResult::Continue : EMT2QuestNodeResult::Stop;
}

EMT2QuestNodeResult UMT2QuestNode_MutateTable::Execute(const FMT2QuestContext& Context)
{
	auto Fail = [&]()
	{
		UE_LOG(LogMT2QuestRuntime, Warning, TEXT("Quest table mutation rejected: %s[%s] = %s"),
			*TableExpression, *KeyExpression, *ValueExpression);
		return EMT2QuestNodeResult::Stop;
	};
	bool bOk = false;
	const FMT2QuestValue Table = FMT2QuestExpression::Evaluate(TableExpression, Context, bOk);
	bool bValueOk = false;
	const FMT2QuestValue Value = FMT2QuestExpression::Evaluate(ValueExpression, Context, bValueOk);
	bool bKeyOk = true;
	const int32 Length = Table.GetTableLength();
	const FMT2QuestValue Key = KeyExpression.IsEmpty() ? FMT2QuestValue(Length + 1)
		: FMT2QuestExpression::Evaluate(KeyExpression, Context, bKeyOk);
	if (!bOk || !bValueOk || !bKeyOk || !Table.RuntimeTable) { return Fail(); }
	if (bInsert)
	{
		if (Key.bIsNil || Key.bIsText || Key.IsTableRef() || Key.Number < 1 || Key.Number > Length + 1 ||
			Key.Number != FMath::FloorToDouble(Key.Number) || Length >= 10000) { return Fail(); }
		// Validate before shifting, so an invalid value cannot leave a partially changed sequence.
		if (Table.RuntimeTable->Names.Num() + Table.RuntimeTable->Numbers.Num() >= 10000 ||
			!Table.SetTableValue(Key, Value, false)) { return Fail(); }
		for (int32 Index = Length; Index >= static_cast<int32>(Key.Number); --Index)
		{
			Table.RuntimeTable->Numbers.Add(Index + 1, Table.GetTableValue(FMT2QuestValue(Index)));
		}
	}
	if (!Table.SetTableValue(Key, Value))
	{
		return Fail();
	}
	return EMT2QuestNodeResult::Continue;
}

EMT2QuestNodeResult UMT2QuestNode_Input::Execute(const FMT2QuestContext& Context)
{
	if (!Context.Manager || ResultVariable.IsNone())
	{
		return EMT2QuestNodeResult::Continue;
	}
	Context.Manager->PresentInput(ResultVariable, bNumericOnly);
	return EMT2QuestNodeResult::Suspend;
}

EMT2QuestNodeResult UMT2QuestNode_Return::Execute(const FMT2QuestContext& Context)
{
	const TArray<FString> Expressions = !ValueExpressions.IsEmpty() ? ValueExpressions :
		ValueExpression.IsEmpty() ? TArray<FString>() : TArray<FString>{ValueExpression};
	bool bOk = false;
	FMT2QuestValue Value = FMT2QuestValue::Results(FMT2QuestExpression::EvaluateList(Expressions, Context, bOk));
	if (!bOk)
	{
		UE_LOG(LogMT2QuestRuntime, Warning, TEXT("Quest return expressions could not be evaluated."));
		return EMT2QuestNodeResult::Stop;
	}
	return Context.Manager && Context.Manager->ReturnFromFunction(Value)
		? EMT2QuestNodeResult::Continue : EMT2QuestNodeResult::Stop;
}

EMT2QuestNodeResult UMT2QuestNode_Journal::Execute(const FMT2QuestContext& Context)
{
	if (!Context.Manager || !Context.Quest)
	{
		return EMT2QuestNodeResult::Continue;
	}
	const FName QuestId = Context.Quest->GetQuestId();
	if (Operation == EMT2QuestJournalOp::Restart)
	{
		Context.Manager->RemoveJournalEntry(QuestId);
		Context.Manager->SetQuestState(Context.Quest, FName(TEXT("start")));
		return EMT2QuestNodeResult::Continue;
	}

	// q.done() only clears the old client's active quest entry. It does not change script state;
	// reusable quests such as levelup remain in `start` so a later level-up can open the next hunt.
	if (Operation == EMT2QuestJournalOp::Clear || Operation == EMT2QuestJournalOp::Done)
	{
		Context.Manager->RemoveJournalEntry(QuestId);
		if (Operation == EMT2QuestJournalOp::Done)
		{
			UE_LOG(LogMT2QuestRuntime, Log, TEXT("[Quest] Player=%s Quest=%s journal entry closed."),
				Context.PlayerState ? *Context.PlayerState->GetCharacterName() : TEXT("Unknown"),
				*QuestId.ToString());
		}
		return EMT2QuestNodeResult::Continue;
	}

	// The journal stores plain text, so the importer's tokens are resolved here rather than at display
	// time - a title like "Kill {=mob_name(...)}." has to capture the monster it was set for.
	const FText ExpandedText =
		FText::FromString(Context.Manager->ExpandQuestText(Text, Context));

	FMT2QuestJournalEntry& Entry = Context.Manager->FindOrAddJournalEntry(QuestId);
	switch (Operation)
	{
	case EMT2QuestJournalOp::Start:
		// q.start on a quest that already has an entry just refreshes it; keep any title already set.
		if (Entry.Title.IsEmpty() && !ExpandedText.IsEmpty()) { Entry.Title = ExpandedText; }
		break;
	case EMT2QuestJournalOp::SetTitle:
		Entry.Title = ExpandedText;
		break;
	case EMT2QuestJournalOp::SetSummary:
		Entry.Summary = ExpandedText;
		break;
	case EMT2QuestJournalOp::SetCounter:
		Entry.Counter = ResolveAmount(CounterExpression, CounterValue, Context);
		break;
	default:
		break;
	}
	Context.Manager->NotifyJournalChanged();
	return EMT2QuestNodeResult::Continue;
}

EMT2QuestNodeResult UMT2QuestNode_Target::Execute(const FMT2QuestContext& Context)
{
	if (!Context.Manager || !Context.Quest)
	{
		return EMT2QuestNodeResult::Continue;
	}
	const FName QuestId = Context.Quest->GetQuestId();

	if (Operation == EMT2QuestTargetOp::Clear)
	{
		Context.Manager->ClearTargetMarker(TargetName, QuestId);
		return EMT2QuestNodeResult::Continue;
	}

	FMT2QuestTargetMarker Marker;
	Marker.TargetName = TargetName;
	Marker.QuestId = QuestId;
	if (Operation == EMT2QuestTargetOp::SetActor)
	{
		bool bOk = true;
		const FMT2QuestValue Value = VnumExpression.IsEmpty() ? FMT2QuestValue(Vnum)
			: FMT2QuestExpression::Evaluate(VnumExpression, Context, bOk);
		if (!bOk || Value.bIsNil || Value.bIsBoolean || Value.bIsText || Value.IsTableRef() ||
			!FMath::IsFinite(Value.Number) || Value.Number < 0 || Value.Number > MAX_int32)
		{
			UE_LOG(LogTemp, Error, TEXT("Quest target.vid received an invalid entity ID."));
			return EMT2QuestNodeResult::Stop;
		}
		UMT2QuestEntitySubsystem* Entities = Context.Player && Context.Player->GetWorld()
			? Context.Player->GetWorld()->GetSubsystem<UMT2QuestEntitySubsystem>() : nullptr;
		AMT2CharacterBase* Actor = Entities ? Entities->FindEntity(static_cast<int32>(Value.Number)) : nullptr;
		if (!Actor) { return EMT2QuestNodeResult::Continue; }
		Marker.bTracksActor = true;
		Marker.TargetActor = Actor;
		const FVector Location = Actor->GetActorLocation();
		Marker.WorldPosition = FVector2D(Location.X, Location.Y);
	}
	else if (Operation == EMT2QuestTargetOp::SetNpc)
	{
		Marker.Vnum = ResolveAmount(VnumExpression, Vnum, Context);
		if (Marker.Vnum <= 0)
		{
			return EMT2QuestNodeResult::Continue; // the NPC isn't on this map, so there is nothing to mark
		}
	}
	else
	{
		// Old scripts pass local map coordinates in metres; the world uses centimetres, and the map
		// occupies the +X/-Y quadrant (see Docs/OldGameResearch/MapCoordinateSystem.md).
		const double LocalX = ResolveAmount(PositionXExpression, FMath::RoundToInt(Position.X), Context);
		const double LocalY = ResolveAmount(PositionYExpression, FMath::RoundToInt(Position.Y), Context);
		Marker.WorldPosition = FVector2D(LocalX * 100.0, -LocalY * 100.0);
	}
	Context.Manager->SetTargetMarker(Marker);
	return EMT2QuestNodeResult::Continue;
}

EMT2QuestNodeResult UMT2QuestNode_StartTimer::Execute(const FMT2QuestContext& Context)
{
	if (!Context.Manager)
	{
		return EMT2QuestNodeResult::Continue;
	}
	const float Delay = static_cast<float>(
		ResolveAmount(SecondsExpression, FMath::RoundToInt(Seconds), Context));

	FMT2QuestValue Argument;
	if (!ArgumentExpression.IsEmpty())
	{
		bool bOk = true;
		const FMT2QuestValue Evaluated = FMT2QuestExpression::Evaluate(ArgumentExpression, Context, bOk);
		if (bOk)
		{
			Argument = Evaluated;
		}
	}
	Context.Manager->StartQuestTimer(TimerName, Delay, bServerTimer, bLooping, Argument, Context.Quest);
	return EMT2QuestNodeResult::Continue;
}

EMT2QuestNodeResult UMT2QuestNode_ClearTimer::Execute(const FMT2QuestContext& Context)
{
	if (Context.Manager)
	{
		Context.Manager->ClearQuestTimer(TimerName, Context.Quest);
	}
	return EMT2QuestNodeResult::Continue;
}

EMT2QuestNodeResult UMT2QuestNode_SetItemSocket::Execute(const FMT2QuestContext& Context)
{
	UMT2InventoryComponent* Inventory =
		Context.Player ? Context.Player->GetInventoryComponent() : nullptr;
	if (Inventory)
	{
		Inventory->SetItemSocketValue(
			Context.EventItemSlot, SocketIndex, ResolveAmount(ValueExpression, Value, Context));
	}
	return EMT2QuestNodeResult::Continue;
}

EMT2QuestNodeResult UMT2QuestNode_SetItemAttribute::Execute(const FMT2QuestContext& Context)
{
	UMT2InventoryComponent* Inventory =
		Context.Player ? Context.Player->GetInventoryComponent() : nullptr;
	if (Inventory)
	{
		Inventory->SetItemBonusValue(
			Context.EventItemSlot,
			ResolveAmount(IndexExpression, 0, Context),
			ResolveAmount(ApplyTypeExpression, 0, Context),
			ResolveAmount(ValueExpression, 0, Context));
	}
	return EMT2QuestNodeResult::Continue;
}

EMT2QuestNodeResult UMT2QuestNode_OpenShop::Execute(const FMT2QuestContext& Context)
{
	if (AMT2Npc* Npc = Cast<AMT2Npc>(Context.TargetActor))
	{
		if (Context.Player && Context.Player->HasAuthority() &&
			(!bContinueQuest || (Npc->GetShopComponent() && Npc->GetShopComponent()->CanOpenFor(Context.Player))))
		{
			// Close the menu first: the shop window replaces the conversation.
			if (Context.Manager && !bContinueQuest)
			{
				Context.Manager->CloseDialog();
			}
			Context.Player->ClientOpenNpcShop(Npc);
		}
	}
	return bContinueQuest ? EMT2QuestNodeResult::Continue : EMT2QuestNodeResult::Stop;
}

bool UMT2QuestNode_Warp::ResolveMapDestination(const FMT2QuestContext& Context,
	const UMT2QuestTableAsset& Tables, const FMT2QuestMapDefinition*& OutMap,
	FVector2D& OutPosition, FString& OutError) const
{
	OutMap = nullptr; OutPosition = FVector2D::ZeroVector; OutError.Reset();
	auto ReadInteger = [&Context](const FString& Expression, double Literal, int32& Out, bool bOptional = false) -> bool
	{
		double Number = Literal;
		if (!Expression.IsEmpty())
		{
			bool bOk = false;
			const FMT2QuestValue Value = FMT2QuestExpression::Evaluate(
				TEXT("tonumber(") + Expression + TEXT(")"), Context, bOk);
			if (!bOk) { return false; }
			if (Value.bIsNil)
			{
				Out = 0;
				return bOptional;
			}
			Number = Value.Number;
		}
		if (!FMath::IsFinite(Number) || Number < MIN_int32 || Number > MAX_int32) { return false; }
		Out = static_cast<int32>(Number);
		return true;
	};
	FVector2D GlobalPosition;
	if (bToEmpireVillage)
	{
		// start_position.cpp::g_start_position, not Town.txt's respawn location.
		switch (Context.PlayerState ? Context.PlayerState->GetEmpire() : EMT2Empire::None)
		{
		case EMT2Empire::Shinsoo: GlobalPosition = FVector2D(469300, 964200); break;
		case EMT2Empire::Chunjo: GlobalPosition = FVector2D(55700, 157900); break;
		case EMT2Empire::Jinno: GlobalPosition = FVector2D(969600, 278400); break;
		default: OutError = TEXT("warp_to_village requires a valid player empire"); return false;
		}
	}
	else
	{
		int32 X = 0, Y = 0, MapIndex = 0;
		bool bMapOk = true;
		if (bUsesLegacyMapLocalCoordinates) { bMapOk = ReadInteger(MapIndexExpression, 0, MapIndex); }
		const bool bXOk = ReadInteger(PositionXExpression, Position.X, X);
		const bool bYOk = ReadInteger(PositionYExpression, Position.Y, Y);
		if (!bUsesLegacyMapLocalCoordinates) { bMapOk = ReadInteger(MapIndexExpression, 0, MapIndex, true); }
		if (!bMapOk || !bXOk || !bYOk)
		{
			OutError = TEXT("warp arguments must be finite 32-bit numeric coordinates and map index");
			return false;
		}
		if (bUsesLegacyMapLocalCoordinates)
		{
			const FMT2QuestMapDefinition* Region = Tables.FindMapByIndex(MapIndex);
			if (!Region || X > Region->WorldSize.X || Y > Region->WorldSize.Y)
			{
				OutError = TEXT("pc.warp_local map index is unavailable or local coordinates exceed its size");
				return false;
			}
			// questlua_pc.cpp adds unscaled coordinates to the requested map's global origin.
			GlobalPosition = Region->GlobalOrigin + FVector2D(X, Y);
		}
		else
		{
			if (MapIndex >= 10000)
			{
				OutError = TEXT("pc.warp private-map routing requires the dungeon-instance backend");
				return false;
			}
			// WarpSet ignores a third map index below 10000 and resolves the global coordinates.
			GlobalPosition = FVector2D(X, Y);
		}
	}
	OutMap = Tables.FindMapAtGlobalPosition(GlobalPosition);
	if (!OutMap)
	{
		OutError = FString::Printf(TEXT("warp global position %.0f, %.0f has no server map"), GlobalPosition.X, GlobalPosition.Y);
		return false;
	}
	OutPosition = OutMap->ToUnrealPosition(GlobalPosition);
	return true;
}

EMT2QuestNodeResult UMT2QuestNode_Warp::Execute(const FMT2QuestContext& Context)
{
	if (!IsValid(Context.Player) || !Context.Player->HasAuthority())
	{
		return EMT2QuestNodeResult::Continue;
	}
	if (bCoordinatesAreLocalMetres && !bUsesLegacyMapLocalCoordinates && !bToEmpireVillage)
	{
		const double X = ResolveAmount(PositionXExpression, FMath::RoundToInt(Position.X), Context);
		const double Y = ResolveAmount(PositionYExpression, FMath::RoundToInt(Position.Y), Context);
		TeleportQuestPlayerOnCurrentMap(Context.Player, FVector2D(X * 100.0, -Y * 100.0));
		return EMT2QuestNodeResult::Continue;
	}

	const UMT2QuestTableAsset* Tables = LoadObject<UMT2QuestTableAsset>(
		nullptr, UMT2QuestTableAsset::GetAssetPath());
	const FMT2QuestMapDefinition* DestinationMap = nullptr;
	FVector2D Destination;
	FString Error;
	if (!Tables || !ResolveMapDestination(Context, *Tables, DestinationMap, Destination, Error))
	{
		UE_LOG(LogMT2QuestRuntime, Error,
			TEXT("[Quest] %s."), Tables ? *Error : TEXT("warp map metadata is unavailable"));
		return EMT2QuestNodeResult::Stop;
	}

	if (ResolveCurrentMapId(Context.Player).Equals(DestinationMap->MapId, ESearchCase::IgnoreCase))
	{
		TeleportQuestPlayerOnCurrentMap(Context.Player, Destination);
		return EMT2QuestNodeResult::Continue;
	}

#if WITH_EDITOR
	if (Context.Player->GetWorld() && Context.Player->GetWorld()->WorldType == EWorldType::PIE)
	{
		if (!TravelQuestPlayerInPIE(Context.Player, *DestinationMap, Destination))
		{
			UE_LOG(LogMT2QuestRuntime, Error,
				TEXT("[Quest] PIE pc.warp failed to travel to %s."), *DestinationMap->MapId);
			if (AMT2PlayerController* Controller =
				Cast<AMT2PlayerController>(Context.Player->GetController()))
			{
				Controller->ClientSystemChatMessage(
					TEXT("That destination map is not imported in this PIE project."));
			}
		}
		return EMT2QuestNodeResult::Continue;
	}
#endif

	AMT2PlayerController* Controller = Cast<AMT2PlayerController>(Context.Player->GetController());
	AMT2PlayerState* State = Context.PlayerState;
	if (!Controller || !State)
	{
		return EMT2QuestNodeResult::Continue;
	}
	State->SetPendingSpawnLocation(Destination);
	const AMT2GameStateBase* GameState =
		Context.Player->GetWorld()->GetGameState<AMT2GameStateBase>();
	const int32 Channel = GameState ? GameState->GetServerChannel() : 0;
	if (!Controller->RequestMapTransferFromServer(DestinationMap->MapId, Channel, false, true))
	{
		State->ClearPendingSpawnLocation();
	}
	return EMT2QuestNodeResult::Continue;
}

EMT2QuestNodeResult UMT2QuestNode_SpawnMob::Execute(const FMT2QuestContext& Context)
{
	UWorld* World = Context.Player ? Context.Player->GetWorld() : nullptr;
	UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	UMT2VnumRegistrySubsystem* Registry =
		GameInstance ? GameInstance->GetSubsystem<UMT2VnumRegistrySubsystem>() : nullptr;
	if (!Registry)
	{
		return EMT2QuestNodeResult::Continue;
	}
	const TSubclassOf<AMT2Mob> MobClass =
		Registry->ResolveMobClass(ResolveAmount(MobVnumExpression, MobVnum, Context));
	if (!MobClass)
	{
		return EMT2QuestNodeResult::Continue;
	}

	// Zero coordinates mean "next to the player", which is what most spawn scripts intend.
	const FVector PlayerLocation = Context.Player->GetActorLocation();
	const FVector Base = Position.IsNearlyZero()
		? PlayerLocation : FVector(Position.X * 100.0, -Position.Y * 100.0, PlayerLocation.Z);

	FActorSpawnParameters Parameters;
	Parameters.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	for (int32 Spawned = 0; Spawned < FMath::Max(Count, 1); ++Spawned)
	{
		// Fan multiples out slightly so they don't stack in one spot.
		const FVector Offset = Spawned == 0
			? FVector::ZeroVector : FVector(FMath::RandRange(-150, 150), FMath::RandRange(-150, 150), 0.0);
		World->SpawnActor<AMT2Mob>(MobClass, Base + Offset, FRotator::ZeroRotator, Parameters);
	}
	return EMT2QuestNodeResult::Continue;
}

EMT2QuestNodeResult UMT2QuestNode_DropItem::Execute(const FMT2QuestContext& Context)
{
	if (Context.Player && Context.Player->HasAuthority())
	{
		const int32 ResolvedVnum = ResolveAmount(ItemVnumExpression, ItemVnum, Context);
		const int32 ResolvedCount = FMath::Max(ResolveAmount(CountExpression, Count, Context), 1);
		const FString OwnerId = bOwnedByPlayer
			? AMT2WorldItem::ResolvePlayerIdentity(Context.PlayerState) : FString();
		const FString OwnerName = bOwnedByPlayer && Context.PlayerState
			? Context.PlayerState->GetCharacterName() : FString();
		AMT2WorldItem::SpawnWorldItem(
			Context.Player->GetWorld(), Context.Player->GetActorLocation(), ResolvedVnum, ResolvedCount,
			Context.Player, OwnerId, OwnerName, bOwnedByPlayer ? 120.0f : 0.0f);
		UE_LOG(LogMT2QuestRuntime, Log,
			TEXT("[Quest] Player=%s Quest=%s dropped ItemVnum=%d Count=%d Owned=%d."),
			Context.PlayerState ? *Context.PlayerState->GetCharacterName() : TEXT("Unknown"),
			Context.Quest ? *Context.Quest->GetQuestId().ToString() : TEXT("Unknown"),
			ResolvedVnum, ResolvedCount, bOwnedByPlayer);
	}
	return EMT2QuestNodeResult::Continue;
}

EMT2QuestNodeResult UMT2QuestNode_RemoveUsedItem::Execute(const FMT2QuestContext& Context)
{
	if (UMT2InventoryComponent* Inventory =
		Context.Player ? Context.Player->GetInventoryComponent() : nullptr)
	{
		Inventory->ConsumeItemAtSlot(Context.EventItemSlot);
	}
	return EMT2QuestNodeResult::Continue;
}

EMT2QuestNodeResult UMT2QuestNode_Wait::Execute(const FMT2QuestContext& Context)
{
	if (!Context.Manager)
	{
		return EMT2QuestNodeResult::Stop;
	}
	Context.Manager->PresentWait();
	return EMT2QuestNodeResult::Suspend;
}

EMT2QuestNodeResult UMT2QuestNode_Close::Execute(const FMT2QuestContext& Context)
{
	if (Context.Manager)
	{
		Context.Manager->CloseDialog();
	}
	return EMT2QuestNodeResult::Stop;
}

EMT2QuestNodeResult UMT2QuestNode_CopyItem::Execute(const FMT2QuestContext& Context)
{
	const AMT2Npc* Npc = Cast<AMT2Npc>(Context.TargetActor);
	AMT2PlayerCharacter* Player = Context.Player;
	UMT2InventoryComponent* Inventory = Player ? Player->GetInventoryComponent() : nullptr;
	auto Fail = [&]()
	{
		UE_LOG(LogTemp, Warning, TEXT("Quest item-copy transaction rejected (slot=%d, result=%s)."),
			Context.EventItemSlot, *ResultVnumExpression);
		return EMT2QuestNodeResult::Stop;
	};
	if (!IsValid(Player) || !Player->HasAuthority() || !IsValid(Npc) || !Inventory ||
		!Context.PlayerState || Context.PlayerState->GetPawn() != Player ||
		!Context.bHasEventItemSnapshot || Context.Event != EMT2QuestEvent::ItemTake ||
		Player->GetWorld() != Npc->GetWorld() || Player->GetHealthComponent()->IsDead() ||
		FVector::Dist2D(Player->GetActorLocation(), Npc->GetActorLocation()) > 350.0f ||
		(Context.PlayerState->GetTradeComponent() && Context.PlayerState->GetTradeComponent()->IsTrading()))
	{
		return Fail();
	}
	bool bOk = false;
	const FMT2QuestValue Result = FMT2QuestExpression::Evaluate(ResultVnumExpression, Context, bOk);
	if (!bOk || Result.bIsText || Result.Number <= 0 || Result.Number > MAX_int32 ||
		Result.Number != FMath::FloorToDouble(Result.Number)) { return Fail(); }
	TMap<int32, int32> Materials;
	if (!MaterialTableExpression.IsEmpty())
	{
		const FMT2QuestValue Value = FMT2QuestExpression::Evaluate(MaterialTableExpression, Context, bOk);
		if (!bOk || !Value.IsTableRef()) { return Fail(); }
		for (int32 RowIndex = 1; RowIndex <= Value.GetTableLength(); ++RowIndex)
		{
			const FMT2QuestValue Row = Value.GetTableValue(FMT2QuestValue(RowIndex));
			if (!Row.IsTableRef()) { return Fail(); }
			const FMT2QuestValue Vnum = Row.GetTableValue(FMT2QuestValue(FString(TEXT("vnum"))));
			const FMT2QuestValue Count = Row.GetTableValue(FMT2QuestValue(FString(TEXT("count"))));
			if (Vnum.bIsNil || Count.bIsNil || Vnum.bIsText || Count.bIsText || Vnum.IsTableRef() || Count.IsTableRef() ||
				Vnum.Number <= 0 || Vnum.Number > MAX_int32 || Count.Number <= 0 || Count.Number > MAX_int32 ||
				Vnum.Number != FMath::FloorToDouble(Vnum.Number) || Count.Number != FMath::FloorToDouble(Count.Number)) { return Fail(); }
			int32& Total = Materials.FindOrAdd(static_cast<int32>(Vnum.Number));
			if (Total > MAX_int32 - static_cast<int32>(Count.Number)) { return Fail(); }
			Total += static_cast<int32>(Count.Number);
		}
	}
	return Inventory->CopyAndReplaceQuestItem(Context.EventItemSlot, Context.EventItemSnapshot,
		static_cast<int32>(Result.Number), Materials) ? EMT2QuestNodeResult::Continue : Fail();
}

EMT2QuestNodeResult UMT2QuestNode_Unconverted::Execute(const FMT2QuestContext& Context)
{
	if (bStopExecution)
	{
		UE_LOG(LogTemp, Warning, TEXT("Quest stopped at unsupported transaction %s: %s"),
			*SourceLocation, *SourceLua);
	}
	return bStopExecution ? EMT2QuestNodeResult::Stop : EMT2QuestNodeResult::Continue;
}
