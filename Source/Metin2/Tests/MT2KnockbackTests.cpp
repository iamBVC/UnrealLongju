#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Combat/MT2CombatComponent.h"
#include "Characters/MT2PlayerCharacter.h"
#include "Components/MT2HealthComponent.h"
#include "Mobs/MT2Mob.h"
#include "Mobs/MT2MetinStone.h"
#include "Mobs/MT2MobAIComponent.h"
#include "Animation/MT2AnimationMotionData.h"
#include "Animation/MT2CharacterAnimInstance.h"
#include "Animation/MT2MobAnimInstance.h"
#include "Core/MT2VnumRegistrySubsystem.h"
#include "Combat/MT2KnockbackRootMotion.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimSequence.h"
#include "Config/MT2PathSettings.h"
#include "GameFramework/RootMotionSource.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Components/BoxComponent.h"
#include "AbilitySystemComponent.h"
#include "Abilities/MT2CoreAttributeSet.h"
#include "UObject/StrongObjectPtr.h"
#include "World/MT2MapPresentationActor.h"
#include "World/MT2MapAttributes.h"
#include "Player/MT2PlayerState.h"
#include "Components/SkeletalMeshComponent.h"
#include "TimerManager.h"
#include "Stats/MT2CombatStatsComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMT2KnockbackRulesTest, "Metin2.Combat.KnockbackRules", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMT2KnockbackRulesTest::RunTest(const FString&)
{
	float ParsedForce; int32 ParsedType;
	UMT2AnimationMotionData::ReadKnockbackMetadata(TEXT("Group AttackingData\n{\nHittingType 2\nExternalForce 0\n}\nGroup Event01\n{\nMotionEventType 4\nHittingType 1\nExternalForce 17\n}\n"), ParsedForce, ParsedType);
	TestEqual(TEXT("Nested sword finisher overrides zero base force"), ParsedForce, 17.f);
	TestEqual(TEXT("Nested force keeps its own hit type"), ParsedType, 1);
	TArray<FMT2MotionAttackEvent> Events;
	UMT2AnimationMotionData::ReadAttackEvents(TEXT("Group AttackingData\n{\nHittingType 2\nExternalForce 3\nGroup HitData00\n{\nAttackingStartTime 0.2\nAttackingEndTime 0.3\n}\n}\nGroup Event00\n{\nMotionEventType 4\nStartingTime 0.65\nDuringTime 0.2\nHittingType 1\nExternalForce 17\n}\n"), Events);
	TestEqual(TEXT("Normal window and special event remain separate"), Events.Num(), 2);
	if (Events.Num() == 2)
	{
		TestEqual(TEXT("Normal window starts at authored time"), Events[0].TimeSeconds, .2f);
		TestEqual(TEXT("Normal window inherits its own force"), Events[0].ExternalForce, 3.f);
		TestEqual(TEXT("Finisher keeps its event time"), Events[1].TimeSeconds, .65f);
		TestEqual(TEXT("Finisher keeps its event force"), Events[1].ExternalForce, 17.f);
	}
	TestEqual(TEXT("No external force"), UMT2CombatComponent::MotionKnockbackDistance(0), 0.f);
	TestTrue(TEXT("Legacy force 3 distance"), FMath::IsNearlyEqual(UMT2CombatComponent::MotionKnockbackDistance(3), 13.5f, .01f));
	TestTrue(TEXT("Legacy force 20 finisher distance"), FMath::IsNearlyEqual(UMT2CombatComponent::MotionKnockbackDistance(20), 656.7f, .01f));
	const FString Name(TEXT("ABP_Warrior_Male"));
	const auto* BP = LoadObject<UAnimBlueprint>(nullptr, *UMT2PathSettings::Format(TEXT("Characters_Animations_Name"), TEXT("%s%s"), *Name, *Name));
	const auto* Anim = BP && BP->GeneratedClass ? Cast<UMT2CharacterAnimInstance>(BP->GeneratedClass->GetDefaultObject()) : nullptr;
	if (!TestNotNull(TEXT("Imported animation defaults"), Anim)) { return false; }
	int32 PushMotions = 0;
	for (const auto& Set : Anim->AnimationSets)
		for (const auto& Action : Set.Value.Actions)
			for (UAnimSequence* Sequence : Action.Value.Animations)
				if (Sequence)
					if (const auto* Data = Sequence->GetAssetUserData<UMT2AnimationMotionData>(); Data && Data->ExternalForce > 0)
					{
						++PushMotions;
						AddInfo(FString::Printf(TEXT("Motion %s force=%.1f hit=%d"), *Sequence->GetName(), Data->ExternalForce, Data->HittingType));
					}
	TestTrue(TEXT("Existing motion metadata provides authored pushes"), PushMotions > 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMT2KnockbackMovementTest, "Metin2.Combat.KnockbackMovement", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMT2KnockbackMovementTest::RunTest(const FString&)
{
	TStrongObjectPtr<UGameInstance> GI(NewObject<UGameInstance>(GEngine)); GI->InitializeStandalone();
	UWorld* World = GI->GetWorld(); if (!TestNotNull(TEXT("World"), World)) { return false; }
	ON_SCOPE_EXIT { GI->Shutdown(); World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
	auto* Floor = World->SpawnActor<AActor>();
	auto* Box = NewObject<UBoxComponent>(Floor); Floor->SetRootComponent(Box);
	Box->SetBoxExtent(FVector(5000,5000,50)); Box->SetCollisionProfileName(TEXT("BlockAll")); Box->RegisterComponent();
	Floor->SetActorLocation(FVector(0,0,-50));
	auto* Attacker = World->SpawnActor<AMT2Mob>(); Attacker->SetActorLocation(FVector(0,0,90));
	auto* Victim = World->SpawnActor<AMT2Mob>(); Victim->SetActorLocation(FVector(300,0,90));
	auto* ASC = Victim->GetAbilitySystemComponent();
	ASC->AddAttributeSetSubobject(NewObject<UMT2CoreAttributeSet>(Victim));
	ASC->InitAbilityActorInfo(Victim, Victim);
	ASC->SetNumericAttributeBase(UMT2CoreAttributeSet::GetMaxHealthAttribute(), 100);
	ASC->SetNumericAttributeBase(UMT2CoreAttributeSet::GetHealthAttribute(), 100);
	Victim->GetHealthComponent()->InitializeWithAbilitySystem(ASC);
	auto* Move = Victim->GetCharacterMovement(); Move->bRunPhysicsWithNoController = true;
	Move->SetMovementMode(MOVE_Walking);
	AddInfo(FString::Printf(TEXT("Fixture authority=%d health=%.1f movable=%d type=%d"), Victim->HasAuthority(), Victim->GetHealthComponent()->GetHealth(), Victim->CanBeKnockedBack(), int32(Victim->GetMobType())));
	Victim->ApplyKnockback(Attacker, 200);
	TestTrue(TEXT("Authoritative shove installs root motion"), Move->HasRootMotionSources());
	TestTrue(TEXT("Stationary victim movement tick enabled"), Move->IsComponentTickEnabled());
	Victim->GetMobAIComponent()->ApplyTickPolicy();
	TestTrue(TEXT("AI cannot disable active shove"), Move->IsComponentTickEnabled());
	for (int32 Step = 0; Step < 21; ++Step) { Move->TickComponent(.05f, LEVELTICK_All, nullptr); }
	AddInfo(FString::Printf(TEXT("Victim position after slide: %s"), *Victim->GetActorLocation().ToString()));
	TestTrue(TEXT("Victim moved away a bounded distance"), Victim->GetActorLocation().X > 450 && Victim->GetActorLocation().X < 520);
	TestTrue(TEXT("Slide stays grounded"), Move->IsMovingOnGround());
	TestTrue(TEXT("Completed shove has no residual horizontal velocity"), Move->Velocity.Size2D() < 1);
	TestFalse(TEXT("Idle AI tick policy restored after shove"), Move->IsComponentTickEnabled());
	Victim->SetActorLocation(FVector(300,0,110)); Move->SetMovementMode(MOVE_Walking);
	Victim->ApplyKnockback(Attacker, 200, 0.f, true);
	for (int32 Step = 0; Step < 21; ++Step) { Move->TickComponent(.05f, LEVELTICK_All, nullptr); }
	TestTrue(TEXT("Horse-style shove is sideways"), Victim->GetActorLocation().Y > 150 && FMath::Abs(Victim->GetActorLocation().X - 300) < 1);
	auto* Map = World->SpawnActor<AMT2MapPresentationActor>();
	Map->WorldMin = FVector2D(0,-1000); Map->WorldMax = FVector2D(1000,1000);
	Map->Attributes.Size = FIntPoint(10,20); Map->Attributes.Flags.Init(0,200);
	Map->Attributes.Flags[10*10+5] = MT2MapAttribute::Block;
	World->GetSubsystem<UMT2MapAttributeSubsystem>()->RegisterMap(Map);
	Victim->SetActorLocation(FVector(300,0,110)); Move->SetMovementMode(MOVE_Walking);
	Victim->ApplyKnockback(Attacker, 200);
	for (int32 Step = 0; Step < 21; ++Step) { Move->TickComponent(.05f, LEVELTICK_All, nullptr); }
	TestTrue(TEXT("Shove respects no-walk boundary"), Victim->GetActorLocation().X > 300 && Victim->GetActorLocation().X < 400.1);
	World->GetSubsystem<UMT2MapAttributeSubsystem>()->UnregisterMap(Map);
	auto* Wall = World->SpawnActor<AActor>(); auto* WallBox = NewObject<UBoxComponent>(Wall);
	Wall->SetRootComponent(WallBox); WallBox->SetBoxExtent(FVector(10,200,200));
	WallBox->SetCollisionProfileName(TEXT("BlockAll")); WallBox->RegisterComponent(); Wall->SetActorLocation(FVector(440,0,100));
	Victim->SetActorLocation(FVector(300,0,110)); Move->SetMovementMode(MOVE_Walking);
	Victim->ApplyKnockback(Attacker, 200);
	for (int32 Step = 0; Step < 21; ++Step) { Move->TickComponent(.05f, LEVELTICK_All, nullptr); }
	TestTrue(TEXT("Shove sweeps against physical walls"), Victim->GetActorLocation().X > 300 && Victim->GetActorLocation().X < 430);
	Victim->ApplyKnockback(Attacker, 200);
	ASC->SetNumericAttributeBase(UMT2CoreAttributeSet::GetHealthAttribute(), 0);
	for (int32 Step = 0; Step < 21; ++Step) { Move->TickComponent(.05f, LEVELTICK_All, nullptr); }
	TestFalse(TEXT("Death cancels the active shove"), Move->HasRootMotionSources());
	Victim->ApplyKnockback(Attacker, 200);
	TestFalse(TEXT("Dead victims reject further pushes"), Move->HasRootMotionSources());
	auto* Stone = World->SpawnActor<AMT2MetinStone>(); Stone->ApplyKnockback(Attacker, 200);
	TestFalse(TEXT("Stone cannot be pushed"), Stone->GetCharacterMovement()->HasRootMotionSources());
	Move->RemoveRootMotionSource(FName(TEXT("MT2Knockback")));
	FMT2MobDefinition Definition; Definition.AIFlags = 1 << static_cast<uint8>(EMT2MobAIFlag::NoMove);
	Victim->ConfigureFromDefinition(Definition);
	TestFalse(TEXT("NOMOVE rejects shoves"), Victim->CanBeKnockedBack());
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMT2WarriorComboKnockbackTest, "Metin2.Combat.WarriorSwordKnockback", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMT2WarriorComboKnockbackTest::RunTest(const FString&)
{
	TStrongObjectPtr<UGameInstance> GI(NewObject<UGameInstance>(GEngine)); GI->InitializeStandalone();
	UWorld* World = GI->GetWorld(); if (!TestNotNull(TEXT("World"), World)) { return false; }
	ON_SCOPE_EXIT { GI->Shutdown(); World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
	auto* Player = World->SpawnActor<AMT2PlayerCharacter>(); auto* State = World->SpawnActor<AMT2PlayerState>();
	World->InitializeActorsForPlay(FURL());
	Player->SetPlayerState(State); Player->SetWeaponAnimationSet(0, false);
	Player->GetMesh()->SetAnimInstanceClass(nullptr);
	State->GetAbilitySystemComponent()->AddAttributeSetSubobject(State->GetCoreAttributes());
	State->InitializeAbilitySystem(Player);
	Player->GetHealthComponent()->InitializeWithAbilitySystem(State->GetAbilitySystemComponent());
	State->GetAbilitySystemComponent()->SetNumericAttributeBase(UMT2CoreAttributeSet::GetMaxHealthAttribute(), 10000);
	State->GetAbilitySystemComponent()->SetNumericAttributeBase(UMT2CoreAttributeSet::GetHealthAttribute(), 10000);
	Player->GetCombatComponent()->ConfigureBasicAttack(2000, 500, 1, 1, 4);
	Player->GetCombatComponent()->OnBasicAttackPerformed.AddUniqueDynamic(Player, &AMT2PlayerCharacter::HandleBasicAttackPerformed);
	TestNull(TEXT("Headless attacker has no live AnimInstance"), Player->GetMesh()->GetAnimInstance());
	const auto* BP = LoadObject<UAnimBlueprint>(nullptr, *UMT2PathSettings::Format(TEXT("Characters_Animations_Name"), TEXT("%s%s"), TEXT("ABP_Warrior_Male"), TEXT("ABP_Warrior_Male")));
	const auto* Defaults = BP && BP->GeneratedClass ? Cast<UMT2CharacterAnimInstance>(BP->GeneratedClass->GetDefaultObject()) : nullptr;
	if (!TestNotNull(TEXT("Warrior motion defaults"), Defaults)) { return false; }
	TGuardValue<uint64> FrameGuard(GFrameCounter, GFrameCounter);
	const auto TickTimers = [World](float Delta)
	{
		++GFrameCounter;
		World->GetTimerManager().Tick(Delta);
	};
	TickTimers(.001f);
	for (int32 Combo = 0; Combo < 4; ++Combo)
	{
		UAnimSequence* Sequence = Defaults->GetComboAnimation(TEXT("onehand_sword"), Combo);
		const auto* Motion = Sequence ? Sequence->GetAssetUserData<UMT2AnimationMotionData>() : nullptr;
		if (!TestNotNull(TEXT("Sword motion data"), Motion)) { return false; }
		if (!TestTrue(TEXT("Baked sword hit event"), !Motion->AttackEvents.IsEmpty())) { return false; }
		Player->HandleBasicAttackPerformed(Combo);
		const float Expected = Motion->HittingType != 0 ? UMT2CombatComponent::MotionKnockbackDistance(Motion->ExternalForce) : 0.f;
		TestEqual(FString::Printf(TEXT("Sword combo %d selects its authored force without an AnimInstance"), Combo), Player->GetCombatComponent()->GetPendingKnockback(), Expected);
		if (Combo == 3) { TestTrue(TEXT("Sword finisher provides a visible shove"), Expected > 100); }
		auto* Target = World->SpawnActor<AMT2Mob>(); Target->SetActorLocation(FVector(100,0,0));
		auto* TargetASC = Target->GetAbilitySystemComponent();
		TargetASC->AddAttributeSetSubobject(NewObject<UMT2CoreAttributeSet>(Target));
		TargetASC->InitAbilityActorInfo(Target, Target);
		TargetASC->SetNumericAttributeBase(UMT2CoreAttributeSet::GetMaxHealthAttribute(), 10000);
		TargetASC->SetNumericAttributeBase(UMT2CoreAttributeSet::GetHealthAttribute(), 10000);
		Target->GetHealthComponent()->InitializeWithAbilitySystem(TargetASC);
		TestTrue(TEXT("Sword hit reaches mob damage pipeline"), Player->GetCombatComponent()->PerformBasicAttackOnTarget(Target));
		TestFalse(TEXT("No knockback at swing start"), Target->GetCharacterMovement()->HasRootMotionSources());
		TestEqual(TEXT("No damage at swing start"), Target->GetHealthComponent()->GetHealth(), 10000.f);
		const float PlayRate = FMath::Max(Player->GetCombatStatsComponent()->GetCalculatedStats().AttackSpeed / 100.f, .1f);
		const float HitDelay = Motion->AttackEvents[0].TimeSeconds / PlayRate;
		TickTimers(HitDelay - .001f);
		TestFalse(TEXT("No knockback before authored hit"), Target->GetCharacterMovement()->HasRootMotionSources());
		TestEqual(TEXT("No damage before authored hit"), Target->GetHealthComponent()->GetHealth(), 10000.f);
		TickTimers(.002f);
		TestTrue(TEXT("Damage lands with authored hit"), Target->GetHealthComponent()->GetHealth() < 10000.f);
		const auto Source = Target->GetCharacterMovement()->GetRootMotionSource(FName(TEXT("MT2Knockback")));
		TestTrue(TEXT("Landed sword hit starts mob knockback"), Expected <= 0 || Source.IsValid());
		if (Source.IsValid())
		{
			const auto Force = StaticCastSharedPtr<FRootMotionSource_ConstantForce>(Source);
			TestTrue(TEXT("Sword hit uses the correct authored displacement"), FMath::IsNearlyEqual(Force->Force.Size2D() * Force->Duration, double(Expected), .01));
		}
	}
	// Separate timings and force values survive speed scaling and timer capture.
	Player->GetCombatComponent()->OnBasicAttackPerformed.Clear();
	auto* Sequence = NewObject<UAnimSequence>(Player);
	auto* Data = NewObject<UMT2AnimationMotionData>(Sequence); Sequence->AddAssetUserData(Data);
	for (int32 Index = 0; Index < 3; ++Index)
	{
		FMT2MotionAttackEvent& Event = Data->AttackEvents.AddDefaulted_GetRef();
		Event.TimeSeconds = Index == 0 ? .2f : Index == 1 ? .4f : .8f;
		Event.ExternalForce = Index == 2 ? 20.f : 0.f;
		Event.HittingType = Index == 2 ? 1 : 2;
	}
	Player->GetCombatComponent()->SetBasicAttackMotion(Sequence, 2.f);
	auto* Target = World->SpawnActor<AMT2Mob>(); Target->SetActorLocation(FVector(100,0,0));
	auto* ASC = Target->GetAbilitySystemComponent(); ASC->AddAttributeSetSubobject(NewObject<UMT2CoreAttributeSet>(Target));
	ASC->InitAbilityActorInfo(Target, Target);
	ASC->SetNumericAttributeBase(UMT2CoreAttributeSet::GetMaxHealthAttribute(), 10000);
	ASC->SetNumericAttributeBase(UMT2CoreAttributeSet::GetHealthAttribute(), 10000);
	Target->GetHealthComponent()->InitializeWithAbilitySystem(ASC);
	Player->GetCombatComponent()->PerformBasicAttackOnTarget(Target);
	TickTimers(.101f);
	const float FirstHealth = Target->GetHealthComponent()->GetHealth();
	TestTrue(TEXT("First hit scales to 0.1 seconds"), FirstHealth < 10000.f);
	TestFalse(TEXT("First hit does not borrow finisher force"), Target->GetCharacterMovement()->HasRootMotionSources());
	TickTimers(.1f);
	TestTrue(TEXT("Second hit has independent damage"), Target->GetHealthComponent()->GetHealth() < FirstHealth);
	TestFalse(TEXT("Second hit does not borrow finisher force"), Target->GetCharacterMovement()->HasRootMotionSources());
	TickTimers(.2f);
	TestTrue(TEXT("Final event alone starts knockback"), Target->GetCharacterMovement()->HasRootMotionSources());
	Player->GetCombatComponent()->PerformBasicAttackOnTarget(Target);
	const float BeforeCancel = Target->GetHealthComponent()->GetHealth();
	Player->GetCombatComponent()->CancelPendingBasicAttackHits();
	TickTimers(1.f);
	TestEqual(TEXT("Interrupted motion cancels pending damage"), Target->GetHealthComponent()->GetHealth(), BeforeCancel);
	Player->GetCombatComponent()->PerformBasicAttackOnTarget(Target);
	Target->SetActorLocation(FVector(5000,0,0));
	TickTimers(1.f);
	TestEqual(TEXT("Moved target is revalidated at hit time"), Target->GetHealthComponent()->GetHealth(), BeforeCancel);
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMT2MobKnockdownTest, "Metin2.Combat.MobKnockdownRecovery", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMT2MobKnockdownTest::RunTest(const FString&)
{
	TStrongObjectPtr<UGameInstance> GI(NewObject<UGameInstance>(GEngine)); GI->InitializeStandalone();
	UWorld* World = GI->GetWorld(); if (!TestNotNull(TEXT("World"), World)) { return false; }
	ON_SCOPE_EXIT { GI->Shutdown(); World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
	const auto MobClass = GI->GetSubsystem<UMT2VnumRegistrySubsystem>()->ResolveMobClass(101);
	if (!TestNotNull(TEXT("Imported wild dog class"), MobClass.Get())) { return false; }
	auto* Victim = World->SpawnActor<AMT2Mob>(MobClass);
	auto* Attacker = World->SpawnActor<AMT2Mob>();
	World->InitializeActorsForPlay(FURL());
	const UClass* AnimClass = Victim->GetMesh()->GetAnimClass();
	auto* ASC = Victim->GetAbilitySystemComponent();
	ASC->AddAttributeSetSubobject(NewObject<UMT2CoreAttributeSet>(Victim));
	ASC->InitAbilityActorInfo(Victim, Victim);
	ASC->SetNumericAttributeBase(UMT2CoreAttributeSet::GetMaxHealthAttribute(), 10000);
	ASC->SetNumericAttributeBase(UMT2CoreAttributeSet::GetHealthAttribute(), 10000);
	Victim->GetHealthComponent()->InitializeWithAbilitySystem(ASC);
	TestTrue(TEXT("Living movable reaction victim"), !Victim->GetHealthComponent()->IsDead() && Victim->CanBeKnockedBack());
	const auto* AnimDefaults = AnimClass ? Cast<UMT2MobAnimInstance>(AnimClass->GetDefaultObject()) : nullptr;
	if (!TestNotNull(TEXT("Imported mob animation defaults"), AnimDefaults)) { return false; }
	const auto* Fall = AnimDefaults->MotionAnimations.Find(EMT2MobMotion::FrontKnockdown);
	const auto* Standup = AnimDefaults->MotionAnimations.Find(EMT2MobMotion::FrontStandup);
	if (!TestTrue(TEXT("Imported fall and recovery exist"), Fall && *Fall && Standup && *Standup)) { return false; }
	const float FallDuration = (*Fall)->GetPlayLength();
	const float StandupDuration = (*Standup)->GetPlayLength();
	AddInfo(FString::Printf(TEXT("Authored fall %.3fs, recovery %.3fs"), FallDuration, StandupDuration));
	Victim->GetMesh()->SetAnimInstanceClass(nullptr); // Dedicated-server-like animation scheduling.
	TGuardValue<uint64> FrameGuard(GFrameCounter, GFrameCounter);
	const auto TickTimers = [World](float Delta)
	{
		++GFrameCounter; // TimerManager deliberately ticks at most once per engine frame.
		World->GetTimerManager().Tick(Delta);
	};
	TickTimers(.001f); // Promote pending timers before timing the reaction.
	Victim->SetActorRotation(FRotator(0,180,0)); Attacker->SetActorRotation(FRotator::ZeroRotator);
	Victim->PlayHitReaction(Attacker, 2);
	TestFalse(TEXT("GOOD hit does not knock down"), Victim->IsKnockdownMotionLocked());
	Victim->PlayHitReaction(Attacker, 1);
	TestTrue(TEXT("GREAT hit starts fall"), Victim->IsKnockdownMotionLocked());
	TestFalse(TEXT("Attack rejected during fall"), Victim->TryAttackTarget(Attacker));
	TestFalse(TEXT("Damage cannot replace fall"), Victim->PlayMobMotion(EMT2MobMotion::FrontDamage));
	TickTimers(FallDuration * .5f);
	Victim->PlayHitReaction(Attacker, 1);
	TickTimers(FallDuration * .5f + .01f);
	TestTrue(TEXT("Stand-up remains locked"), Victim->IsKnockdownMotionLocked());
	TestFalse(TEXT("Attack rejected during stand-up"), Victim->TryAttackTarget(Attacker));
	TestFalse(TEXT("Damage cannot replace stand-up"), Victim->PlayMobMotion(EMT2MobMotion::BackDamage));
	TickTimers(StandupDuration + .01f);
	TestFalse(TEXT("Authored sequence unlocks without fixed stun"), Victim->IsCombatMotionLocked());
	Victim->SetActorRotation(FRotator::ZeroRotator);
	Victim->PlayHitReaction(Attacker, 1);
	TestTrue(TEXT("Back hit starts fall"), Victim->IsKnockdownMotionLocked());
	TestTrue(TEXT("Death can interrupt fall"), Victim->PlayMobMotion(EMT2MobMotion::FrontDead));
	TestFalse(TEXT("Death clears recovery lock"), Victim->IsKnockdownMotionLocked());
	TickTimers(FallDuration + StandupDuration + .1f);
	TestFalse(TEXT("Cancelled recovery never relocks"), Victim->IsKnockdownMotionLocked());
	FMT2KnockbackRootMotion Source; Source.Force = FVector(100,0,0); Source.Duration = 2.f;
	Source.PrepareRootMotion(.1f, .1f, *Victim, *Victim->GetCharacterMovement());
	const double EarlySpeed = Source.RootMotionParams.GetRootMotionTransform().GetTranslation().X;
	Source.SetTime(1.8f);
	Source.PrepareRootMotion(.1f, .1f, *Victim, *Victim->GetCharacterMovement());
	const double LateSpeed = Source.RootMotionParams.GetRootMotionTransform().GetTranslation().X;
	TestTrue(TEXT("Legacy ease-out decelerates"), EarlySpeed > LateSpeed && LateSpeed > 0);
	TestEqual(TEXT("Serialization retains average force"), Source.Force.X, 100.0);
	TUniquePtr<FRootMotionSource> Clone(Source.Clone());
	TestEqual(TEXT("Native source preserves derived type when cloned"), Clone->GetScriptStruct(), FMT2KnockbackRootMotion::StaticStruct());
	return true;
}
#endif
