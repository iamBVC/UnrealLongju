#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Fishing/MT2FishingSettings.h"
#include "Fishing/MT2FishingComponent.h"
#include "Characters/MT2PlayerCharacter.h"
#include "Components/MT2HealthComponent.h"
#include "Items/MT2InventoryComponent.h"
#include "Items/MT2ItemTemplate.h"
#include "Items/MT2ItemUtils.h"
#include "Core/MT2VnumRegistrySubsystem.h"
#include "World/MT2MapPresentationActor.h"
#include "World/MT2MapAttributes.h"
#include "Player/MT2PlayerState.h"
#include "Quests/MT2QuestExpression.h"
#include "Quests/MT2QuestManagerComponent.h"
#include "Npcs/MT2Npc.h"
#include "Abilities/MT2CoreAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "UObject/StrongObjectPtr.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimMontage.h"
#include "Animation/MT2CharacterAnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Equipment/MT2EquipmentComponent.h"
#include "Characters/MT2CharacterAppearanceSettings.h"
#include "Config/MT2PathSettings.h"
#include "Config/MT2GameplaySettings.h"
#include "Engine/SkeletalMesh.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMT2FishingRulesTest, "Metin2.Fishing.LegacyRules", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMT2FishingRulesTest::RunTest(const FString&)
{
	const auto& Settings = *GetDefault<UMT2FishingSettings>(); FString Error;
	if (!TestTrue(TEXT("Valid configured table"), Settings.Validate(Error))) { AddError(Error); return false; }
	TestEqual(TEXT("Legacy rows"), Settings.CatchTable.Num(), 37);
	TestEqual(TEXT("Town table"), Settings.FindTable(1), 0); TestEqual(TEXT("Second town"), Settings.FindTable(23), 1);
	TestEqual(TEXT("Unsupported map denied"), Settings.FindTable(99), INDEX_NONE);
	TestEqual(TEXT("Normal peak at three seconds"), Settings.TimingChance(0, 3000), 80);
	TestEqual(TEXT("Slow peak at five seconds"), Settings.TimingChance(1, 5000), 80);
	TestEqual(TEXT("Quick peak at one second"), Settings.TimingChance(2, 1000), 80);
	TestEqual(TEXT("Expired always fails"), Settings.TimingChance(3, 6001), 0);
	int32 Totals[4] = {0}; for (const auto& Row : Settings.CatchTable) { for (int32 I=0; I<4; ++I) { Totals[I] += Row.Weights[I]; } }
	TestEqual(TEXT("Town weight total"), Totals[0], 9950); TestEqual(TEXT("Premium total"), Totals[2], 9800);
	TestEqual(TEXT("First weighted row is miss"), Settings.PickCatch(0, 1), 0);
	TestEqual(TEXT("Exact weighted boundary"), Settings.PickCatch(0, 3000), 0);
	TestEqual(TEXT("Next row"), Settings.PickCatch(0, 3001), 1);
	TestEqual(TEXT("Out of range roll"), Settings.PickCatch(0, Totals[0]+1), INDEX_NONE);
	const auto& Catch = Settings.CatchTable[3];
	TestTrue(TEXT("Power and timing rolls succeed"), Settings.ResolveCatch(Catch, 3000, 13, 100, 13));
	TestFalse(TEXT("Insufficient rod power"), Settings.ResolveCatch(Catch, 3000, 12, 100, 13));
	TestFalse(TEXT("Late roll denied"), Settings.ResolveCatch(Catch, 6001, 1000, 1, 1));
	TestFalse(TEXT("Miss entry never rewards"), Settings.ResolveCatch(Settings.CatchTable[0], 3000, 1000, 1, 1));
	FMT2ItemSlot A; A.Vnum=27803; A.MetinSockets.SetNum(1); A.MetinSockets[0].Value=1000;
	FMT2ItemSlot B=A; B.MetinSockets[0].Value=2000;
	TestFalse(TEXT("Fish lengths cannot merge"), MT2ItemUtils::HaveSameInstanceData(A,B));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMT2FishingAuthorityTest, "Metin2.Fishing.Authority", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMT2FishingAuthorityTest::RunTest(const FString&)
{
	TStrongObjectPtr<UGameInstance> GI(NewObject<UGameInstance>(GEngine)); GI->InitializeStandalone();
	UWorld* World=GI->GetWorld(); if (!TestNotNull(TEXT("World"),World)) { return false; }
	ON_SCOPE_EXIT { GI->Shutdown(); World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
	auto* Registry=GI->GetSubsystem<UMT2VnumRegistrySubsystem>();
	const auto* Rod=Cast<UMT2ItemRodTemplate>(Registry->ResolveItemTemplateClass(27400).GetDefaultObject());
	if (!TestNotNull(TEXT("Imported rod"),Rod)) { return false; }
	TestTrue(TEXT("Rod practice imported"),Rod->PracticeChanceDenominator>0);
	auto* Player=World->SpawnActor<AMT2PlayerCharacter>(); auto* PS=World->SpawnActor<AMT2PlayerState>(); Player->SetPlayerState(PS);
	auto* ASC=PS->GetAbilitySystemComponent(); ASC->AddAttributeSetSubobject(PS->GetCoreAttributes()); ASC->InitAbilityActorInfo(PS,Player);
	ASC->SetNumericAttributeBase(UMT2CoreAttributeSet::GetMaxHealthAttribute(),100); ASC->SetNumericAttributeBase(UMT2CoreAttributeSet::GetHealthAttribute(),100);
	Player->GetHealthComponent()->InitializeWithAbilitySystem(ASC);
	Player->SetActorLocation(FVector(-1000,-1000,0)); Player->SetActorRotation(FRotator::ZeroRotator); Player->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
	auto* Inventory=Player->GetInventoryComponent(); TArray<FMT2ItemSlot> Slots,Worn; Slots.SetNum(180); Worn.SetNum(32);
	Worn[4]=FMT2ItemSlot(27400,Rod->MakeInstanceData(1)); Worn[4].MetinSockets.SetNum(3); Worn[4].MetinSockets[2].Value=40; Inventory->RestoreItems(Slots,Worn);
	auto* Map=World->SpawnActor<AMT2MapPresentationActor>(); Map->MapIndex=1; Map->WorldMin=FVector2D(-2000,-2000); Map->WorldMax=FVector2D::ZeroVector;
	Map->Attributes.Size=FIntPoint(20,20); Map->Attributes.Flags.SetNumZeroed(400); World->GetSubsystem<UMT2MapAttributeSubsystem>()->RegisterMap(Map);
	auto* Fishing=Player->GetFishingComponent();
	TestFalse(TEXT("Dry ground denied by authority"),Fishing->StartFishing());
	Map->Attributes.Flags[10*20+4]=MT2MapAttribute::Water;
	TestTrue(TEXT("Water cell permits fishing"),Fishing->StartFishing());
	if (!Fishing->IsFishing()) { return false; }
	const uint32 First=Fishing->State.Session; Fishing->ServerToggleFishing_Implementation(0);
	TestTrue(TEXT("Duplicate start cannot stop active attempt"),Fishing->IsFishing());
	Fishing->ServerCancelFishing_Implementation(First+1); TestTrue(TEXT("Wrong session rejected"),Fishing->IsFishing());
	Fishing->ReelIn(); TestFalse(TEXT("Early stop cancels"),Fishing->IsFishing());
	TestEqual(TEXT("Early stop keeps bait"),Inventory->GetEquipment()[4].MetinSockets[2].Value,40);
	TestTrue(TEXT("Second cast"),Fishing->StartFishing()); Fishing->Bite();
	TestEqual(TEXT("Server bite state"),int32(Fishing->State.Phase),int32(EMT2FishingPhase::Bite));
	Fishing->BiteTime=World->GetTimeSeconds()-7; Fishing->ReelIn();
	TestFalse(TEXT("Expired reel completes"),Fishing->IsFishing()); TestEqual(TEXT("Expired reel consumes bait"),Inventory->GetEquipment()[4].MetinSockets[2].Value,0);
	TestFalse(TEXT("No bait cannot recast"),Fishing->StartFishing());
	Worn[4].MetinSockets[2].Value=40; Inventory->RestoreItems(Slots,Worn); TestTrue(TEXT("Rebait permits cast"),Fishing->StartFishing());
	Player->SetActorLocation(FVector(-1100,-1000,0)); Fishing->CheckAttempt(); TestFalse(TEXT("Movement cancels"),Fishing->IsFishing());
	Player->SetActorLocation(FVector(-1000,-1000,0)); Map->Attributes.Flags[10*20+10]=MT2MapAttribute::Block;
	TestFalse(TEXT("Blocked source denied"),Fishing->StartFishing());
	Map->Attributes.Flags[10*20+10]=0; Map->MapIndex=99; TestFalse(TEXT("Unsupported map denied"),Fishing->StartFishing());
	Map->MapIndex=1; Inventory->RestoreItems(Slots,Worn); TestTrue(TEXT("Reward cast"),Fishing->StartFishing()); Fishing->Bite();
	Fishing->Catch=GetDefault<UMT2FishingSettings>()->CatchTable[3]; Fishing->BiteTime=World->GetTimeSeconds()-3;
	const int32 BeforeFish=Inventory->CountItemByVnum(27803); Fishing->ReelIn();
	TestEqual(TEXT("Successful reel gives one fish"),Inventory->CountItemByVnum(27803),BeforeFish+1);
	const auto* Fish=Inventory->GetSlots().FindByPredicate([](const FMT2ItemSlot& Item) { return Item.Vnum==27803; });
	TestTrue(TEXT("Fish length stored"),Fish && Fish->MetinSockets.IsValidIndex(0) && Fish->MetinSockets[0].Value>=1000 && Fish->MetinSockets[0].Value<=2800);
	const int32 OriginalLength=Fish ? Fish->MetinSockets[0].Value : 0;
	const FString FishPayload=PS->CapturePersistentStateJson_Implementation();
	TestTrue(TEXT("Fish payload restores"),PS->ApplyPersistentStateJson_Implementation(FishPayload));
	const auto* LoadedFish=Inventory->GetSlots().FindByPredicate([](const FMT2ItemSlot& Item) { return Item.Vnum==27803; });
	TestTrue(TEXT("Fish length survives persistence"),LoadedFish && LoadedFish->MetinSockets[0].Value==OriginalLength);
	Fishing->ServerToggleFishing_Implementation(First); TestEqual(TEXT("Old reel cannot duplicate reward"),Inventory->CountItemByVnum(27803),BeforeFish+1);
	Inventory->RestoreItems(Slots,Worn); TestTrue(TEXT("Swap test cast"),Fishing->StartFishing()); Fishing->Bite();
	TestTrue(TEXT("Unequip allowed after cancelling"),Inventory->UnequipItem(4)); TestFalse(TEXT("Unequip cancelled cast"),Fishing->IsFishing());
	const auto* Returned=Inventory->GetSlots().FindByPredicate([](const FMT2ItemSlot& Item) { return Item.Vnum==27400; });
	TestTrue(TEXT("Bait consumed on original rod before swap"),Returned && Returned->MetinSockets[2].Value==0);
	auto* MutableRod=const_cast<UMT2ItemRodTemplate*>(Rod); const int32 OriginalDenominator=MutableRod->PracticeChanceDenominator;
	ON_SCOPE_EXIT { MutableRod->PracticeChanceDenominator=OriginalDenominator; };
	MutableRod->PracticeChanceDenominator=1; Worn[4].MetinSockets[0].Value=Rod->ImprovementPointsRequired-1; Inventory->RestoreItems(Slots,Worn);
	TestTrue(TEXT("Practice applied"),Inventory->FinishFishingAttempt(true));
	TestEqual(TEXT("Proficiency reaches cap"),Inventory->GetEquipment()[4].MetinSockets[0].Value,Rod->ImprovementPointsRequired);
	Inventory->FinishFishingAttempt(true); TestEqual(TEXT("Proficiency cannot exceed cap"),Inventory->GetEquipment()[4].MetinSockets[0].Value,Rod->ImprovementPointsRequired);
	const FString RodPayload=PS->CapturePersistentStateJson_Implementation();
	TestTrue(TEXT("Rod payload restores"),PS->ApplyPersistentStateJson_Implementation(RodPayload));
	TestEqual(TEXT("Rod proficiency survives persistence"),Inventory->GetEquipment()[4].MetinSockets[0].Value,Rod->ImprovementPointsRequired);
	TestEqual(TEXT("Empty bait remains empty after persistence"),Inventory->GetEquipment()[4].MetinSockets[2].Value,0);
	TestEqual(TEXT("Equipped rod cannot refine"),Inventory->RefineFishingRod(0),2);
	TestTrue(TEXT("Unequip trained rod"),Inventory->UnequipItem(4));
	const int32 RodIndex=Inventory->GetSlots().IndexOfByPredicate([](const FMT2ItemSlot& Item) { return Item.Vnum==27400; });
	const int32 OriginalSuccess=MutableRod->RefinementSuccessPercent; ON_SCOPE_EXIT { MutableRod->RefinementSuccessPercent=OriginalSuccess; };
	MutableRod->RefinementSuccessPercent=100;
	auto* Fisher=World->SpawnActor<AMT2Npc>(); FMT2MobDefinition Definition; Definition.Vnum=9009; Fisher->ConfigureFromDefinition(Definition); Fisher->SetActorLocation(Player->GetActorLocation());
	FMT2QuestContext Context; Context.Player=Player; Context.PlayerState=PS; Context.Manager=PS->GetQuestManagerComponent(); Context.TargetActor=Fisher;
	Context.EventItemSlot=RodIndex; Context.EventItemSnapshot=Inventory->GetSlots()[RodIndex]; Context.bHasEventItemSnapshot=true;
	bool Ok=false;
	TestEqual(TEXT("Trained rod refines through fisherman quest"),FMT2QuestExpression::Evaluate(FString::Printf(TEXT("__fish_real_refine_rod(%d)"),RodIndex),Context,Ok).AsInt(),1);
	TestTrue(TEXT("Fishing quest binding recognized"),Ok);
	TestEqual(TEXT("Replayed refinement snapshot rejected"),FMT2QuestExpression::Evaluate(FString::Printf(TEXT("__fish_real_refine_rod(%d)"),RodIndex),Context,Ok).AsInt(),2);
	TestEqual(TEXT("Rod replaced with next vnum"),Inventory->GetSlots()[RodIndex].Vnum,Rod->RefinedVnum);
	TestEqual(TEXT("New rod resets proficiency"),Inventory->GetSlots()[RodIndex].MetinSockets[0].Value,0);
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMT2FishingPresentationTest, "Metin2.Fishing.Presentation", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMT2FishingPresentationTest::RunTest(const FString&)
{
	TStrongObjectPtr<UGameInstance> GI(NewObject<UGameInstance>(GEngine)); GI->InitializeStandalone();
	UWorld* World=GI->GetWorld(); if (!World) { return false; }
	ON_SCOPE_EXIT { GI->Shutdown(); World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
	auto* Player=World->SpawnActor<AMT2PlayerCharacter>(); auto* PS=World->SpawnActor<AMT2PlayerState>(); Player->SetPlayerState(PS);
	const auto* Appearance=GetDefault<UMT2CharacterAppearanceSettings>()->FindAppearance(PS->GetCharacterAppearance());
	if (!TestNotNull(TEXT("Default appearance"),Appearance)) { return false; }
	USkeletalMesh* Body=Appearance->Mesh.LoadSynchronous(); if (!TestNotNull(TEXT("Body mesh"),Body)) { return false; }
	const FString Path=UMT2PathSettings::Format(TEXT("Characters_Animations_Name"),TEXT("%s%s"),TEXT("ABP_Warrior_Male"),TEXT("ABP_Warrior_Male"));
	auto* Blueprint=LoadObject<UAnimBlueprint>(nullptr,*Path); if (!TestNotNull(TEXT("AnimBP"),Blueprint)) { return false; }
	Player->GetMesh()->SetSkeletalMesh(Body,true); Player->GetMesh()->SetAnimInstanceClass(Blueprint->GeneratedClass);
	auto* Anim=Cast<UMT2CharacterAnimInstance>(Player->GetMesh()->GetAnimInstance()); if (!TestNotNull(TEXT("AnimInstance"),Anim)) { return false; }
	const auto* Rod=Cast<UMT2ItemRodTemplate>(GI->GetSubsystem<UMT2VnumRegistrySubsystem>()->ResolveItemTemplateClass(27400).GetDefaultObject());
	if (!TestNotNull(TEXT("Rod template"),Rod)) { return false; }
	const TSoftObjectPtr<UStaticMesh> RodMesh=Rod->WorldMesh.IsNull() ? GetDefault<UMT2FishingSettings>()->RodMesh : Rod->WorldMesh;
	TestNotNull(TEXT("Configured rod mesh loads"),RodMesh.LoadSynchronous());
	auto* Equipment=Player->GetEquipmentComponent(); const int32 Request=Equipment->ArmorRequestId;
	Equipment->EquipWeapon(RodMesh,TEXT("equip_right_hand"),INDEX_NONE);
	Equipment->OnWeaponLoaded(Equipment->WeaponRequestId,RodMesh.ToSoftObjectPath());
	TestTrue(TEXT("Rod visible on hand"),Equipment->GetWeaponMeshComponent()->IsVisible() && Equipment->GetWeaponMeshComponent()->GetStaticMesh()==RodMesh.Get());
	auto* Fishing=Player->GetFishingComponent(); Fishing->MulticastFishingEvent_Implementation(EMT2FishingEvent::Cancelled,0,FVector::ZeroVector);
	auto* Montage=Anim->GetCurrentActiveMontage(); if (!TestNotNull(TEXT("Stop animation starts"),Montage)) { return false; }
	Equipment->OnArmorLoaded(Request,FSoftObjectPath(Body));
	TestEqual(TEXT("Unchanged body preserves AnimInstance"),Player->GetMesh()->GetAnimInstance(),static_cast<UAnimInstance*>(Anim));
	TestTrue(TEXT("Stop montage survives equipment refresh"),Anim->Montage_IsActive(Montage));
	auto* Map=World->SpawnActor<AMT2MapPresentationActor>(); Map->WorldMin=FVector2D(-2000,-2000); Map->WorldMax=FVector2D::ZeroVector;
	Map->MapCells=FIntPoint(1,1); Map->Attributes.Size=FIntPoint(1,1); Map->Attributes.Flags={2}; Map->WaterGridSize=FIntPoint(128,128);
	FMT2WaterRectangle Rect; Rect.Size=Map->WaterGridSize; Rect.Height=150; Map->WaterRectangles.Add(Rect);
	World->GetSubsystem<UMT2MapAttributeSubsystem>()->RegisterMap(Map);
	Fishing->ShowFloat(FVector(-500,-1000,10000));
	if (TestNotNull(TEXT("Visible float created"),Fishing->FloatComponent.Get()))
	{
		TestEqual(TEXT("Float placed on visual water, not pawn Z"),Fishing->FloatComponent->GetComponentLocation().Z,
			150.0+GetDefault<UMT2GameplaySettings>()->WaterSurfaceOffset+GetDefault<UMT2FishingSettings>()->FloatHeightOffset);
		TestEqual(TEXT("Float is non-colliding"),Fishing->FloatComponent->GetCollisionEnabled(),ECollisionEnabled::NoCollision);
		TestNotNull(TEXT("Float has mesh"),Fishing->FloatComponent->GetStaticMesh().Get());
	}
	Fishing->State.Phase=EMT2FishingPhase::Idle; Fishing->OnRep_State(); TestNull(TEXT("Idle replication removes float"),Fishing->FloatComponent.Get());
	return true;
}
#endif
