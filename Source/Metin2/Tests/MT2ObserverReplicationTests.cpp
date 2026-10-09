#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/World.h"
#include "Characters/MT2PlayerCharacter.h"
#include "Items/MT2InventoryComponent.h"
#include "Player/MT2PlayerState.h"
#include "AbilitySystemComponent.h"
#include "Components/ActorComponent.h"
#include "Config/MT2GameplaySettings.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/RootMotionSource.h"
#include "Net/UnrealNetwork.h"
#include "UObject/UnrealType.h"
#include "UObject/CoreNet.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMT2ObserverReplicationTest, "Metin2.Network.ObserverPolicy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMT2ObserverReplicationTest::RunTest(const FString&)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("World"), World)) return false;
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	auto* Player = World->SpawnActor<AMT2PlayerCharacter>();
	auto* State = World->SpawnActor<AMT2PlayerState>();
	TestTrue(TEXT("PlayerState filters components through registered replication"), State->IsUsingRegisteredSubObjectList());
	TestEqual(TEXT("ASC remains visible to observers"), int32(State->AllowActorComponentToReplicate(State->GetAbilitySystemComponent())), int32(COND_None));
	TestFalse(TEXT("ASC native subobject replication remains enabled"), State->GetAbilitySystemComponent()->IsUsingRegisteredSubObjectList());
	int32 PrivateComponents = 0;
	for (const UActorComponent* Component : State->GetReplicatedComponents())
	{
		const FName Name = Component->GetFName();
		const bool bPrivate = Name == TEXT("QuestComponent") || Name == TEXT("QuestManagerComponent") ||
			Name == TEXT("MessengerComponent") || Name == TEXT("GuildComponent") || Name == TEXT("TradeComponent");
		TestEqual(FString::Printf(TEXT("%s component routing"), *Name.ToString()),
			int32(State->AllowActorComponentToReplicate(Component)), int32(bPrivate ? COND_OwnerOnly : Component->GetReplicationCondition()));
		if (bPrivate) ++PrivateComponents;
	}
	TestEqual(TEXT("All five private components are filtered before observer replication"), PrivateComponents, 5);
	auto* Inventory = Player->GetInventoryComponent();
	TArray<FMT2ItemSlot> Worn; Worn.SetNum(32);
	Worn[0].Vnum = 11209; Worn[0].Count = 1;
	Worn[4].Vnum = 19; Worn[4].Count = 1;
	Worn[4].MetinSockets.SetNum(3); Worn[4].MetinSockets[0].Value = 777;
	Inventory->RestoreItems({}, Worn);
	Inventory->RefreshPublicAppearance();
	TestEqual(TEXT("Public armor identifier"), Inventory->PublicAppearance.BodyVnum, 11209);
	TestEqual(TEXT("Public weapon identifier"), Inventory->PublicAppearance.WeaponVnum, 19);
	Inventory->GetClass()->SetUpRuntimeReplicationData();
	TArray<FLifetimeProperty> Properties; Inventory->GetLifetimeReplicatedProps(Properties);
	auto Condition = [&](FName Name)
	{
		const auto* Property = FindFProperty<FProperty>(Inventory->GetClass(), Name);
		for (const auto& Entry : Properties) if (Entry.RepIndex == Property->RepIndex) return Entry.Condition;
		return COND_None;
	};
	TestEqual(TEXT("Full equipment stays owner-only"), int32(Condition(TEXT("Equipment"))), int32(COND_OwnerOnly));
	TestEqual(TEXT("Appearance goes only to observers"), int32(Condition(TEXT("PublicAppearance"))), int32(COND_SkipOwner));
	auto* Observer = World->SpawnActor<AMT2PlayerCharacter>();
	auto* ObserverInventory = Observer->GetInventoryComponent();
	ObserverInventory->PublicAppearance = Inventory->PublicAppearance;
	ObserverInventory->OnRep_PublicAppearance();
	TestEqual(TEXT("Late observer reconstructs weapon"), ObserverInventory->GetEquipment()[4].Vnum, 19);
	TestTrue(TEXT("Observer receives no socket state"), ObserverInventory->GetEquipment()[4].MetinSockets.IsEmpty());
	TestEqual(TEXT("Owner socket state preserved"), Inventory->GetEquipment()[4].MetinSockets[0].Value, 777);
	Inventory->Equipment[4] = FMT2ItemSlot(); Inventory->RefreshPublicAppearance();
	ObserverInventory->PublicAppearance = Inventory->PublicAppearance; ObserverInventory->OnRep_PublicAppearance();
	TestTrue(TEXT("Observer sees unequip"), ObserverInventory->GetEquipment()[4].IsEmpty());
	const auto& Settings = UMT2GameplaySettings::Get();
	const float Baseline = FMath::Clamp(Settings.PlayerMovementReplicationRate, 1.f, 30.f);
	TestEqual(TEXT("Observer baseline"), Player->GetNetUpdateFrequency(), Baseline);
	TestEqual(TEXT("Observer position uses centimeter precision"), int32(Player->GetReplicatedMovement().LocationQuantizationLevel), int32(EVectorQuantization::RoundWholeNumber));
	FRepMovement Packed = Player->GetReplicatedMovement();
	Packed.Location = FVector(123456.4, -56789.6, 123.2); Packed.LinearVelocity = FVector(600, 0, 0);
	FNetBitWriter Writer(nullptr, 1024); bool bSuccess = false;
	Packed.NetSerialize(Writer, nullptr, bSuccess);
	TestTrue(TEXT("Movement snapshot serializes"), bSuccess && !Writer.IsError());
	FRepMovement Decoded = Observer->GetReplicatedMovement();
	FNetBitReader Reader(nullptr, Writer.GetData(), Writer.GetNumBits());
	Decoded.NetSerialize(Reader, nullptr, bSuccess);
	TestTrue(TEXT("Observer decodes centimeter snapshot"), bSuccess && !Reader.IsError() && Decoded.Location.Equals(Packed.Location, 1.0));
	auto Source = MakeShared<FRootMotionSource_ConstantForce>(); Source->Duration = 1.f;
	Player->GetCharacterMovement()->ApplyRootMotionSource(Source);
	Player->RefreshObserverReplicationPolicy();
	TestEqual(TEXT("Root motion retains high update rate"), Player->GetNetUpdateFrequency(), FMath::Max(Baseline, FMath::Clamp(Settings.PlayerRootMotionReplicationRate, 1.f, 60.f)));
	Player->GetCharacterMovement()->CurrentRootMotion.Clear(); Player->RefreshObserverReplicationPolicy();
	TestEqual(TEXT("Root motion end restores baseline"), Player->GetNetUpdateFrequency(), Baseline);
	TestTrue(TEXT("Native movement replication retained"), Player->IsReplicatingMovement());
	return true;
}
#endif
