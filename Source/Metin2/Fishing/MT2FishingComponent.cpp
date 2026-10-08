#include "Fishing/MT2FishingComponent.h"
#include "Characters/MT2PlayerCharacter.h"
#include "Components/MT2HealthComponent.h"
#include "Animation/MT2CharacterAnimInstance.h"
#include "Animation/AnimSequence.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Config/MT2GameplaySettings.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Items/MT2InventoryComponent.h"
#include "Items/MT2ItemTemplate.h"
#include "Items/MT2ItemUtils.h"
#include "Items/MT2WorldItem.h"
#include "Core/MT2VnumRegistrySubsystem.h"
#include "Mounts/MT2MountComponent.h"
#include "Player/MT2PlayerController.h"
#include "Player/MT2PlayerState.h"
#include "World/MT2MapAttributes.h"
#include "World/MT2MapPresentationActor.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Net/UnrealNetwork.h"

namespace
{
	const UMT2ItemRodTemplate* EquippedRod(const AMT2PlayerCharacter* Player)
	{
		const auto* Inventory = Player ? Player->GetInventoryComponent() : nullptr;
		const auto* GI = Player ? Player->GetGameInstance() : nullptr;
		auto* Registry = GI ? GI->GetSubsystem<UMT2VnumRegistrySubsystem>() : nullptr;
		return Inventory && Registry && Inventory->GetEquipment().IsValidIndex(4)
			? Cast<UMT2ItemRodTemplate>(Registry->ResolveItemTemplateClass(Inventory->GetEquipment()[4].Vnum).GetDefaultObject()) : nullptr;
	}
	int32 Socket(const FMT2ItemSlot& Item, int32 Index) { return Item.MetinSockets.IsValidIndex(Index) ? Item.MetinSockets[Index].Value : 0; }
}
UMT2FishingComponent::UMT2FishingComponent()
{
	SetIsReplicatedByDefault(true);
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.bAllowTickOnDedicatedServer = false;
}
void UMT2FishingComponent::BeginPlay()
{
	Super::BeginPlay();
	if (auto* Player = Cast<AMT2PlayerCharacter>(GetOwner()); Player && Player->HasAuthority())
		Player->GetInventoryComponent()->OnEquipmentChanged.AddUniqueDynamic(this, &UMT2FishingComponent::OnEquipmentChanged);
}
void UMT2FishingComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	if (auto* Player = Cast<AMT2PlayerCharacter>(GetOwner()); Player && Player->HasAuthority())
	{
		const bool Consume = State.Phase == EMT2FishingPhase::Bite;
		State.Phase = EMT2FishingPhase::Idle;
		const auto& Worn = Player->GetInventoryComponent()->GetEquipment();
		if (Consume && Worn.IsValidIndex(4) && Worn[4].Vnum == RodVnum && Socket(Worn[4], 2) == BaitPower) { Player->GetInventoryComponent()->FinishFishingAttempt(false); }
		Player->GetInventoryComponent()->OnEquipmentChanged.RemoveDynamic(this, &UMT2FishingComponent::OnEquipmentChanged);
	}
	if (GetWorld()) { GetWorld()->GetTimerManager().ClearAllTimersForObject(this); }
	ClearFloat();
	State.Phase = EMT2FishingPhase::Idle; Super::EndPlay(Reason);
}
void UMT2FishingComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps); DOREPLIFETIME(UMT2FishingComponent, State);
}
void UMT2FishingComponent::RequestToggleFishing() { ServerToggleFishing(IsFishing() ? State.Session : 0); }
bool UMT2FishingComponent::HasRodEquipped() const { return EquippedRod(Cast<AMT2PlayerCharacter>(GetOwner())) != nullptr; }
void UMT2FishingComponent::RequestCancelFishing() { if (IsFishing() && !bCancelRequested) { bCancelRequested = true; ServerCancelFishing(State.Session); } }
void UMT2FishingComponent::ServerToggleFishing_Implementation(uint32 ExpectedSession)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !GetWorld()) { return; }
	if (IsFishing()) { if (ExpectedSession == State.Session) { ReelIn(); } return; }
	const double Now = GetWorld()->GetTimeSeconds();
	if (ExpectedSession != 0 || Now - LastRequestTime < .25 || Now < NextStartTime) { return; }
	LastRequestTime = Now; StartFishing();
}
void UMT2FishingComponent::ServerCancelFishing_Implementation(uint32 ExpectedSession)
{
	if (IsFishing() && State.Session == ExpectedSession) { CancelFishing(); }
}
bool UMT2FishingComponent::StartFishing()
{
	auto* Player = Cast<AMT2PlayerCharacter>(GetOwner());
	if (!Player || !Player->HasAuthority() || !GetWorld() || IsFishing() || Player->GetHealthComponent()->IsDead() ||
		Player->GetMountComponent()->IsMounted() || !Player->GetCharacterMovement()->IsMovingOnGround()) { return false; }
	const auto* Rod = EquippedRod(Player);
	if (!Rod) { Say(TEXT("Equip a fishing rod first.")); return false; }
	const FMT2ItemSlot& Item = Player->GetInventoryComponent()->GetEquipment()[4];
	if (Socket(Item, 2) <= 0) { Say(TEXT("Attach bait to your fishing rod first.")); return false; }
	const auto& Settings = *GetDefault<UMT2FishingSettings>(); FString Error;
	if (!Settings.Validate(Error)) { UE_LOG(LogTemp, Error, TEXT("Fishing configuration: %s"), *Error); Say(TEXT("Fishing is unavailable: invalid server configuration.")); return false; }
	auto* Attributes = GetWorld()->GetSubsystem<UMT2MapAttributeSubsystem>(); uint8 Flags = 0;
	auto* SourceMap = Attributes->FindMapAt(Player->GetActorLocation(), Flags);
	if (!SourceMap || (Flags & MT2MapAttribute::NoWalk)) { Say(TEXT("You cannot fish here.")); return false; }
	TableIndex = Settings.FindTable(SourceMap->MapIndex);
	if (TableIndex == INDEX_NONE) { Say(TEXT("Fishing is not enabled on this map.")); return false; }
	// Port the client's +/-10 degree search to the authority. No client hook location or height is accepted.
	bool Found = false; FVector Hook = FVector::ZeroVector; float CastYaw = 0;
	for (int32 Step = 0; Step <= 18 && !Found; ++Step)
		for (int32 Sign : {1, -1})
		{
			CastYaw = Player->GetActorRotation().Yaw + Step * 10 * Sign;
			Hook = Player->GetActorLocation() + FRotator(0, CastYaw, 0).Vector() * Settings.CastDistance;
			if (SourceMap->Attributes.Query(Hook, SourceMap->WorldMin, SourceMap->WorldMax, Flags) && (Flags & MT2MapAttribute::Water)) { Found = true; break; }
		}
	if (!Found) { Say(TEXT("Stand near water and face the fishing spot.")); return false; }
	Map = SourceMap; CastOrigin = Player->GetActorLocation(); RodVnum = Item.Vnum; BaitPower = Socket(Item, 2);
	Player->GetCharacterMovement()->StopMovementImmediately(); Player->SetActorRotation(FRotator(0, CastYaw, 0));
	++SessionCounter; if (SessionCounter == 0) { ++SessionCounter; }
	State.Session = SessionCounter; State.Phase = EMT2FishingPhase::Waiting; State.HookLocation = Hook;
	Player->ForceNetUpdate(); MulticastFishingEvent(EMT2FishingEvent::Cast, nullptr, State.HookLocation);
	GetWorld()->GetTimerManager().SetTimer(BiteTimer, this, &UMT2FishingComponent::Bite, FMath::RandRange(Settings.MinimumWaitSeconds, Settings.MaximumWaitSeconds), false);
	GetWorld()->GetTimerManager().SetTimer(GuardTimer, this, &UMT2FishingComponent::CheckAttempt, .2f, true);
	Say(TEXT("You cast your line. Wait for a bite.")); return true;
}
bool UMT2FishingComponent::ValidateAttempt() const
{
	const auto* Player = Cast<AMT2PlayerCharacter>(GetOwner()); const auto* SourceMap = Map.Get();
	if (!Player || !Player->HasAuthority() || !SourceMap || !IsFishing() || Player->GetHealthComponent()->IsDead() ||
		Player->GetMountComponent()->IsMounted() || !Player->GetCharacterMovement()->IsMovingOnGround() ||
		FVector::Dist(Player->GetActorLocation(), CastOrigin) > GetDefault<UMT2FishingSettings>()->MovementTolerance) { return false; }
	const auto& Worn = Player->GetInventoryComponent()->GetEquipment(); uint8 GroundFlags = 0, WaterFlags = 0;
	return Worn.IsValidIndex(4) && Worn[4].Vnum == RodVnum && Socket(Worn[4], 2) == BaitPower && EquippedRod(Player) &&
		SourceMap->Attributes.Query(Player->GetActorLocation(), SourceMap->WorldMin, SourceMap->WorldMax, GroundFlags) && !(GroundFlags & MT2MapAttribute::NoWalk) &&
		SourceMap->Attributes.Query(State.HookLocation, SourceMap->WorldMin, SourceMap->WorldMax, WaterFlags) && (WaterFlags & MT2MapAttribute::Water);
}
void UMT2FishingComponent::CheckAttempt() { if (!ValidateAttempt()) { CancelFishing(); } }
void UMT2FishingComponent::OnEquipmentChanged() { if (IsFishing()) { CancelFishing(); } }
void UMT2FishingComponent::Bite()
{
	if (!ValidateAttempt()) { CancelFishing(); return; }
	const auto& Settings = *GetDefault<UMT2FishingSettings>(); FString Error;
	if (!Settings.Validate(Error)) { CancelFishing(); return; }
	int32 Total = 0; for (const auto& Entry : Settings.CatchTable) { Total += Entry.Weights[TableIndex]; }
	const int32 Index = Settings.PickCatch(TableIndex, FMath::RandRange(1, Total));
	if (!Settings.CatchTable.IsValidIndex(Index)) { CancelFishing(); return; }
	Catch = Settings.CatchTable[Index]; BiteTime = GetWorld()->GetTimeSeconds(); State.Phase = EMT2FishingPhase::Bite;
	GetOwner()->ForceNetUpdate(); MulticastFishingEvent(EMT2FishingEvent::Bite, nullptr, State.HookLocation);
	Say(TEXT("A bite! Reel in within six seconds."));
	GetWorld()->GetTimerManager().SetTimer(ExpiryTimer, FTimerDelegate::CreateWeakLambda(this, [this] { Finish(EMT2FishingEvent::Failed); }), 6.f, false);
}
void UMT2FishingComponent::ReelIn()
{
	if (bResolving) { return; }
	TGuardValue<bool> ResolveGuard(bResolving, true);
	if (!ValidateAttempt()) { CancelFishing(); return; }
	if (State.Phase != EMT2FishingPhase::Bite) { Finish(EMT2FishingEvent::Cancelled); return; }
	auto* Player = CastChecked<AMT2PlayerCharacter>(GetOwner()); const auto* Rod = EquippedRod(Player);
	const double Elapsed = (GetWorld()->GetTimeSeconds() - BiteTime) * 1000;
	const int32 Milliseconds = Elapsed > 6000 ? 6001 : FMath::Max(0, FMath::FloorToInt(Elapsed));
	const int32 Power = int32(FMath::Clamp(int64(Rod->FishingDelayTenths) + BaitPower, int64(0), int64(MAX_int32)));
	if (!GetDefault<UMT2FishingSettings>()->ResolveCatch(Catch, Milliseconds, Power, FMath::RandRange(1, 100), FMath::RandRange(1, FMath::Max(1, Catch.Difficulty))))
	{ Finish(EMT2FishingEvent::Failed, nullptr, true); return; }
	const TSubclassOf<UMT2ItemTemplate> ItemClass = Catch.ItemTemplate.LoadSynchronous();
	const auto* Template = ItemClass.GetDefaultObject();
	if (!Template || Template->Vnum <= 0 || ItemClass->HasAnyClassFlags(CLASS_Abstract))
	{ UE_LOG(LogTemp, Error, TEXT("Fishing reward class '%s' is invalid."), *Catch.ItemTemplate.ToString()); Finish(EMT2FishingEvent::Failed, nullptr, true); return; }
	// Inventory/save formats retain the template's numeric identity; loot selection does not.
	FMT2ItemSlot Reward(Template->Vnum, Template->MakeInstanceData(1)); Reward.MetinSockets.SetNum(3);
	const auto& Length = Catch.LengthRange;
	if (FMath::RandRange(0, 99))
	{
		int32 Sum = 0; for (int32 Roll = 0; Roll < 5; ++Roll) { Sum += FMath::RandRange(0, 2000); }
		Reward.MetinSockets[0].Value = Length[0] + int32(int64(Length[1] - Length[0]) * Sum / 10000);
	}
	else { Reward.MetinSockets[0].Value = Length[1] + FMath::TruncToInt((Length[2] - Length[1]) * 2.0 * FMath::Asin(FMath::RandRange(0, 10000) / 10000.0) / PI); }
	if (Player->GetInventoryComponent()->AddItemSlotPartial(Reward) != 1)
	{
		const auto* PS = Player->GetPlayerState<AMT2PlayerState>();
		if (!AMT2WorldItem::SpawnWorldItemInstance(GetWorld(), Player->GetActorLocation(), Reward, Player,
			AMT2WorldItem::ResolvePlayerIdentity(PS), PS ? PS->GetPlayerName() : Player->GetName(), GetDefault<UMT2FishingSettings>()->DroppedRewardOwnershipSeconds))
		{ UE_LOG(LogTemp, Error, TEXT("Fishing reward delivery failed for %s."), *Player->GetName()); Finish(EMT2FishingEvent::Failed, nullptr, true); return; }
	}
	Finish(EMT2FishingEvent::Caught, ItemClass, true);
}
void UMT2FishingComponent::Finish(EMT2FishingEvent Event, TSubclassOf<UMT2ItemTemplate> ItemClass, bool bPractice)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !IsFishing()) { return; }
	const bool ConsumeBait = State.Phase == EMT2FishingPhase::Bite;
	State.Phase = EMT2FishingPhase::Idle; bCancelRequested = false;
	GetWorld()->GetTimerManager().ClearTimer(BiteTimer); GetWorld()->GetTimerManager().ClearTimer(GuardTimer); GetWorld()->GetTimerManager().ClearTimer(ExpiryTimer);
	const auto* Player = CastChecked<AMT2PlayerCharacter>(GetOwner());
	const auto& Worn = Player->GetInventoryComponent()->GetEquipment();
	if (ConsumeBait && Worn.IsValidIndex(4) && Worn[4].Vnum == RodVnum && Socket(Worn[4], 2) == BaitPower)
	{ Player->GetInventoryComponent()->FinishFishingAttempt(bPractice); }
	NextStartTime = GetWorld()->GetTimeSeconds() + .5; Map.Reset(); GetOwner()->ForceNetUpdate();
	MulticastFishingEvent(Event, ItemClass, State.HookLocation);
	const auto* Template = ItemClass.GetDefaultObject();
	Say(Event == EMT2FishingEvent::Caught ? FString::Printf(TEXT("You caught %s."), *MT2ItemUtils::GetDisplayName(Template, Template ? Template->Vnum : 0).ToString()) : Event == EMT2FishingEvent::Failed ? TEXT("The catch escaped.") : TEXT("Fishing stopped."));
}
void UMT2FishingComponent::CancelFishing() { Finish(EMT2FishingEvent::Cancelled); }
void UMT2FishingComponent::Say(const FString& Message) const
{
	if (const auto* Player = Cast<AMT2PlayerCharacter>(GetOwner()))
		if (auto* Controller = Cast<AMT2PlayerController>(Player->GetController())) { Controller->SendSystemChatMessage(Message); }
}
void UMT2FishingComponent::OnRep_State()
{
	bCancelRequested = false;
	bFloatBiting = State.Phase == EMT2FishingPhase::Bite;
	if (IsFishing()) { ShowFloat(State.HookLocation); } else { ClearFloat(); }
}
void UMT2FishingComponent::MulticastFishingEvent_Implementation(EMT2FishingEvent Event, TSubclassOf<UMT2ItemTemplate> ItemClass, FVector HookPosition)
{
	OnFishingEvent.Broadcast(Event, ItemClass);
	if (GetNetMode() == NM_DedicatedServer) { return; }
	if (Event == EMT2FishingEvent::Cast || Event == EMT2FishingEvent::Bite)
	{
		bFloatBiting = Event == EMT2FishingEvent::Bite;
		ShowFloat(HookPosition);
	}
	else { ClearFloat(); }
	const auto* Player = Cast<AMT2PlayerCharacter>(GetOwner());
	auto* Anim = Player && Player->GetMesh() ? Cast<UMT2CharacterAnimInstance>(Player->GetMesh()->GetAnimInstance()) : nullptr;
	const FName Action = Event == EMT2FishingEvent::Cast ? TEXT("throw") : Event == EMT2FishingEvent::Bite ? TEXT("fishing_react") :
		Event == EMT2FishingEvent::Caught ? TEXT("fishing_catch") : Event == EMT2FishingEvent::Failed ? TEXT("fishing_fail") : TEXT("fishing_cancel");
	UAnimSequence* Sequence = Anim ? Anim->GetAnimation(TEXT("fishing"), Action) : nullptr;
	if (Sequence)
	{ Anim->PlaySlotAnimationAsDynamicMontage(Sequence, TEXT("DefaultSlot"), .15f, .15f, 1, 1); }
}

void UMT2FishingComponent::ClearFloat()
{
	SetComponentTickEnabled(false);
	FloatBobTime = 0; FloatDip = 0; bFloatBiting = false;
	if (FloatComponent) { FloatComponent->DestroyComponent(); FloatComponent = nullptr; }
}

void UMT2FishingComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (GetNetMode() == NM_DedicatedServer || !IsValid(FloatComponent)) { SetComponentTickEnabled(false); return; }
	if (!FMath::IsFinite(DeltaTime) || DeltaTime <= 0) { return; }
	const auto& Settings = *GetDefault<UMT2FishingSettings>();
	const double Period = FMath::IsFinite(Settings.FloatBobPeriod) ? FMath::Max(.1f, Settings.FloatBobPeriod) : 4.f;
	const float Amplitude = FMath::IsFinite(Settings.FloatBobAmplitude) ? FMath::Max(0.f, Settings.FloatBobAmplitude) : 0.f;
	const float Depth = FMath::IsFinite(Settings.FloatBiteDipDepth) ? FMath::Max(0.f, Settings.FloatBiteDipDepth) : 0.f;
	const float Response = FMath::IsFinite(Settings.FloatBiteDipResponseTime) ? FMath::Max(.01f, Settings.FloatBiteDipResponseTime) : .15f;
	FloatBobTime = FMath::Fmod(FloatBobTime + DeltaTime, Period);
	// Frame-rate-independent dip; reduce the bob as the fish pulls the float down.
	FloatDip = FMath::Lerp(FloatDip, bFloatBiting ? Depth : 0.f, 1.f - FMath::Exp(-DeltaTime / Response));
	const float BobWeight = Depth > 0 ? FMath::Clamp(1.f - FloatDip / Depth, 0.f, 1.f) : 1.f;
	const double Bob = Amplitude * BobWeight * FMath::Sin(2.0 * PI * FloatBobTime / Period);
	FloatComponent->SetWorldLocation(FloatBaseLocation + FVector(0, 0, Bob - FloatDip));
}
void UMT2FishingComponent::ShowFloat(const FVector& HookPosition)
{
	if (!GetWorld() || GetNetMode() == NM_DedicatedServer || !GetOwner() || !GetOwner()->GetRootComponent()) { return; }
	const auto& Settings = *GetDefault<UMT2FishingSettings>();
	if (Settings.FloatMesh.IsNull() || Settings.FloatScale.ContainsNaN() || Settings.FloatScale.GetMin() <= 0 || !FMath::IsFinite(Settings.FloatHeightOffset)) { ClearFloat(); return; }
	uint8 Flags;
	const auto* MapActor = GetWorld()->GetSubsystem<UMT2MapAttributeSubsystem>()->FindMapAt(HookPosition, Flags);
	if (!MapActor || MapActor->WaterGridSize.X <= 0 || MapActor->WaterGridSize.Y <= 0) { ClearFloat(); return; }
	const FVector2D Extent = MapActor->WorldMax - MapActor->WorldMin;
	if (Extent.X <= 0 || Extent.Y <= 0) { ClearFloat(); return; }
	const FVector2D Cell = (MapActor->WorldMax - FVector2D(HookPosition)) * FVector2D(MapActor->WaterGridSize) / Extent;
	const auto* Rect = MapActor->WaterRectangles.FindByPredicate([&](const FMT2WaterRectangle& R)
	{
		return Cell.X >= R.Cell.X && Cell.Y >= R.Cell.Y && Cell.X < int64(R.Cell.X) + R.Size.X && Cell.Y < int64(R.Cell.Y) + R.Size.Y;
	});
	if (!Rect || !FMath::IsFinite(Rect->Height)) { ClearFloat(); return; }
	UMaterialInterface* Material = Settings.FloatMaterial.IsNull() ? nullptr : Settings.FloatMaterial.LoadSynchronous();
	UStaticMesh* Mesh = Settings.FloatMesh.LoadSynchronous();
	if (!Mesh) { ClearFloat(); UE_LOG(LogTemp, Warning, TEXT("Cannot load configured fishing float mesh.")); return; }
	if (!FloatComponent)
	{
		FloatBobTime = 0; FloatDip = 0;
		FloatComponent = NewObject<UStaticMeshComponent>(GetOwner(), NAME_None, RF_Transient | RF_DuplicateTransient);
		FloatComponent->SetupAttachment(GetOwner()->GetRootComponent()); FloatComponent->SetAbsolute(true, true, true);
		FloatComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision); FloatComponent->SetCanEverAffectNavigation(false);
		FloatComponent->SetCastShadow(false); FloatComponent->RegisterComponent();
	}
	FloatComponent->SetStaticMesh(Mesh); FloatComponent->SetMaterial(0, Material);
	FloatComponent->SetWorldScale3D(Settings.FloatScale);
	FloatBaseLocation = FVector(HookPosition.X, HookPosition.Y, Rect->Height + GetDefault<UMT2GameplaySettings>()->WaterSurfaceOffset + Settings.FloatHeightOffset);
	FloatComponent->SetWorldLocation(FloatBaseLocation - FVector(0, 0, FloatDip));
	SetComponentTickEnabled(true);
}
