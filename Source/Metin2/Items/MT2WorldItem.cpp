/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "Items/MT2WorldItem.h"
#include "Audio/MT2SoundPlaybackSubsystem.h"

#include "Characters/MT2PlayerCharacter.h"
#include "Components/SceneComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "Items/MT2InventoryComponent.h"
#include "Items/MT2ItemTemplate.h"
#include "Items/MT2ItemUtils.h"
#include "Net/UnrealNetwork.h"
#include "Persistence/MT2PersistenceComponent.h"
#include "Party/MT2Party.h"
#include "Player/MT2PlayerState.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundAttenuation.h"
#include "Skills/MT2SkillDefinition.h"
#include "Skills/MT2SkillSet.h"
#include "TimerManager.h"
#include "Config/MT2GameplaySettings.h"
#include "UI/MT2NameplateComponent.h"

AMT2WorldItem::AMT2WorldItem()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	bReplicates = true;
	// The actor root is spawned at its final ground transform. Only the local cosmetic mesh falls,
	// so replicating movement would poll an immutable transform for the item's entire lifetime.
	SetReplicateMovement(false);
	InitialLifeSpan = 180.0f;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	InteractionSphere = CreateDefaultSubobject<USphereComponent>(TEXT("InteractionSphere"));
	InteractionSphere->SetupAttachment(SceneRoot);
	InteractionSphere->SetSphereRadius(45.0f);
	InteractionSphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	InteractionSphere->SetCollisionObjectType(ECC_WorldDynamic);
	InteractionSphere->SetCollisionResponseToAllChannels(ECR_Ignore);
	InteractionSphere->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);

	MeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ItemMesh"));
	MeshComponent->SetupAttachment(SceneRoot);
	MeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	// Imported Granny item meshes need the same X-axis correction while displayed on the ground.
	MeshComponent->SetRelativeRotation(FRotator(0.0f, 0.0f, 90.0f));

	NameplateComponent = CreateDefaultSubobject<UMT2NameplateComponent>(TEXT("NameplateComponent"));
	NameplateComponent->SetupAttachment(SceneRoot);
}

void AMT2WorldItem::BeginPlay()
{
	Super::BeginPlay();
	if (GetNetMode() != NM_DedicatedServer)
	{
		StartFallAnimation();
	}
}

void AMT2WorldItem::StartFallAnimation()
{
	// Drop from a fixed 1m above the resting spot.
	FallStartRelativeLocation = FVector(0.0f, 0.0f, 100.0f);
	FallStartRelativeQuat = FQuat::MakeFromEuler(FVector(
		FMath::FRandRange(0.0f, 360.0f), FMath::FRandRange(0.0f, 360.0f), FMath::FRandRange(0.0f, 360.0f)));
	FallEndRelativeQuat = MeshComponent->GetRelativeRotation().Quaternion();
	FallElapsed = 0.0f;
	bFalling = true;

	MeshComponent->SetRelativeLocation(FallStartRelativeLocation);
	MeshComponent->SetRelativeRotation(FallStartRelativeQuat);
	SetActorTickEnabled(true);
}

void AMT2WorldItem::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bFalling)
	{
		SetActorTickEnabled(false);
		return;
	}

	FallElapsed += DeltaSeconds;
	const float Alpha = FMath::Clamp(FallElapsed / FallDuration, 0.0f, 1.0f);
	// Squared alpha gives the accelerating, gravity-like descent; the rotation converges linearly
	// onto the final resting orientation so the item visibly tumbles on the way down.
	MeshComponent->SetRelativeLocation(FMath::Lerp(FallStartRelativeLocation, FVector::ZeroVector, Alpha * Alpha));
	MeshComponent->SetRelativeRotation(FQuat::Slerp(FallStartRelativeQuat, FallEndRelativeQuat, Alpha));

	if (Alpha >= 1.0f)
	{
		MeshComponent->SetRelativeLocation(FVector::ZeroVector);
		MeshComponent->SetRelativeRotation(FallEndRelativeQuat);
		bFalling = false;
		SetActorTickEnabled(false);
	}
}

AMT2WorldItem* AMT2WorldItem::SpawnWorldItem(
	UWorld* World, const FVector& GroundLocation, int32 InVnum, int32 InCount,
	AActor* SourceActor, const FString& InOwnerCharacterId,
	const FString& InOwnerDisplayName, float OwnershipDuration)
{
	if (!World || InVnum <= 0 || InCount <= 0)
	{
		return nullptr;
	}

	// Nudge sideways while another dropped item already occupies the spot, so simultaneous drops
	// spread into a ring instead of stacking into a tower.
	FVector DesiredLocation = GroundLocation;
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(MT2WorldItemGround), true, SourceActor);
	constexpr float ItemSpacing = 50.0f;
	for (int32 Attempt = 0; Attempt < 16; ++Attempt)
	{
		TArray<FOverlapResult> Overlaps;
		World->OverlapMultiByObjectType(
			Overlaps, DesiredLocation, FQuat::Identity,
			FCollisionObjectQueryParams(ECC_WorldDynamic),
			FCollisionShape::MakeSphere(ItemSpacing), QueryParams);
		const bool bOccupied = Overlaps.ContainsByPredicate([](const FOverlapResult& Overlap)
		{
			return Cast<AMT2WorldItem>(Overlap.GetActor()) != nullptr;
		});
		if (!bOccupied)
		{
			break;
		}
		const float Angle = FMath::FRandRange(0.0f, 2.0f * UE_PI);
		DesiredLocation = GroundLocation + FVector(
			FMath::Cos(Angle), FMath::Sin(Angle), 0.0f) * ItemSpacing * (1.0f + Attempt / 8.0f);
	}

	// Snap to the ground only: trace static world geometry so already-dropped items (whose
	// interaction spheres block visibility traces) can never become a landing surface.
	FVector SpawnLocation = DesiredLocation;
	FHitResult GroundHit;
	if (World->LineTraceSingleByObjectType(
		GroundHit, DesiredLocation + FVector(0.0f, 0.0f, 1000.0f),
		DesiredLocation - FVector(0.0f, 0.0f, 2000.0f),
		FCollisionObjectQueryParams(ECC_WorldStatic), QueryParams))
	{
		SpawnLocation = GroundHit.ImpactPoint + FVector(0.0f, 0.0f, 5.0f);
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Owner = SourceActor;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	const FRotator SpawnRotation(0.0f, FMath::FRandRange(0.0f, 360.0f), 0.0f);
	AMT2WorldItem* Item = World->SpawnActor<AMT2WorldItem>(
		StaticClass(), SpawnLocation, SpawnRotation, SpawnParameters);
	if (Item)
	{
		Item->InitializeItem(
			InVnum, InCount, InOwnerCharacterId, InOwnerDisplayName, OwnershipDuration);
		// Delay one frame so the replicated actor exists on relevant client channels before the
		// multicast. BeginPlay is intentionally not used: a late relevancy spawn must stay silent.
		Item->GetWorldTimerManager().SetTimerForNextTick(
			Item, &AMT2WorldItem::BroadcastDropSound);
	}
	return Item;
}

AMT2WorldItem* AMT2WorldItem::SpawnWorldItemInstance(
	UWorld* World, const FVector& GroundLocation, const FMT2ItemSlot& Item,
	AActor* SourceActor, const FString& InOwnerCharacterId,
	const FString& InOwnerDisplayName, float OwnershipDuration)
{
	AMT2WorldItem* WorldItem = SpawnWorldItem(
		World, GroundLocation, Item.Vnum, Item.Count, SourceActor,
		InOwnerCharacterId, InOwnerDisplayName, OwnershipDuration);
	if (WorldItem)
	{
		WorldItem->InitializeItemInstance(
			Item, InOwnerCharacterId, InOwnerDisplayName, OwnershipDuration);
	}
	return WorldItem;
}

AMT2WorldItem* AMT2WorldItem::SpawnWorldYang(
	UWorld* World, const FVector& GroundLocation, int64 Amount,
	AActor* SourceActor, const FString& InOwnerCharacterId,
	const FString& InOwnerDisplayName, float OwnershipDuration)
{
	if (Amount <= 0)
	{
		return nullptr;
	}
	AMT2WorldItem* WorldItem = SpawnWorldItem(
		World, GroundLocation, 1, 1, SourceActor,
		InOwnerCharacterId, InOwnerDisplayName, OwnershipDuration);
	if (WorldItem)
	{
		WorldItem->YangAmount = Amount;
		WorldItem->RefreshVisuals();
		WorldItem->ForceNetUpdate();
	}
	return WorldItem;
}

void AMT2WorldItem::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AMT2WorldItem, ItemInstance);
	DOREPLIFETIME(AMT2WorldItem, YangAmount);
	DOREPLIFETIME(AMT2WorldItem, OwnerCharacterId);
	DOREPLIFETIME(AMT2WorldItem, OwnerDisplayName);
}

void AMT2WorldItem::InitializeItem(
	int32 InVnum, int32 InCount, const FString& InOwnerCharacterId,
	const FString& InOwnerDisplayName, float OwnershipDuration)
{
	const UMT2ItemTemplate* Template = MT2ItemUtils::ResolveTemplate(this, InVnum);
	FMT2ItemSlot Item(InVnum, Template
		? Template->MakeInstanceData(InCount) : FMT2ItemInstanceData());
	Item.Count = FMath::Max(InCount, 1);
	InitializeItemInstance(Item, InOwnerCharacterId, InOwnerDisplayName, OwnershipDuration);
}

void AMT2WorldItem::InitializeItemInstance(
	const FMT2ItemSlot& Item, const FString& InOwnerCharacterId,
	const FString& InOwnerDisplayName, float OwnershipDuration)
{
	if (!HasAuthority())
	{
		return;
	}
	ItemInstance = Item;
	ItemInstance.Count = FMath::Max(ItemInstance.Count, 1);
	YangAmount = ItemInstance.Vnum == 1 ? static_cast<int64>(ItemInstance.Count) : 0;
	OwnerCharacterId = InOwnerCharacterId;
	OwnerDisplayName = InOwnerDisplayName;
	if (!OwnerCharacterId.IsEmpty() && OwnershipDuration > 0.0f)
	{
		GetWorldTimerManager().SetTimer(
			OwnershipTimer, this, &AMT2WorldItem::ClearOwnership, OwnershipDuration, false);
	}
	RefreshVisuals();
	ForceNetUpdate();
}

FString AMT2WorldItem::ResolvePlayerIdentity(const AMT2PlayerState* PlayerState)
{
	if (!PlayerState)
	{
		return FString();
	}
	// AI controllers do not receive the GameMode's human PlayerId allocation.
	// Keep each bot's loot reservation distinct without creating a persistence identity.
	if (PlayerState->IsABot())
	{
		return FString::Printf(TEXT("Bot:%u"), PlayerState->GetUniqueID());
	}
	if (const UMT2PersistenceComponent* Persistence = PlayerState->GetPersistenceComponent())
	{
		if (!Persistence->GetEntityId().IsEmpty())
		{
			return Persistence->GetEntityId();
		}
	}
	return FString::Printf(TEXT("PlayerState:%d"), PlayerState->GetPlayerId());
}

bool AMT2WorldItem::CanBePickedUpBy(const AMT2PlayerCharacter* Character) const
{
	return Character && (OwnerCharacterId.IsEmpty() || OwnerCharacterId ==
		ResolvePlayerIdentity(Character->GetPlayerState<AMT2PlayerState>()));
}

AMT2PlayerCharacter* AMT2WorldItem::ResolvePickupRecipient(
	AMT2PlayerCharacter* RequestingCharacter) const
{
	if (!RequestingCharacter)
	{
		return nullptr;
	}
	AMT2PlayerState* RequestingState =
		RequestingCharacter->GetPlayerState<AMT2PlayerState>();
	if (OwnerCharacterId.IsEmpty() || OwnerCharacterId == ResolvePlayerIdentity(RequestingState))
	{
		return RequestingCharacter;
	}

	AMT2Party* Party = RequestingState ? RequestingState->GetParty() : nullptr;
	if (!Party)
	{
		return nullptr;
	}
	for (AMT2PlayerState* MemberState : Party->GetMemberStates())
	{
		if (MemberState && ResolvePlayerIdentity(MemberState) == OwnerCharacterId)
		{
			return Cast<AMT2PlayerCharacter>(MemberState->GetPawn());
		}
	}
	return nullptr;
}

int64 AMT2WorldItem::TryPickup(
	AMT2PlayerCharacter* RequestingCharacter,
	AMT2PlayerCharacter** OutReceivingCharacter)
{
	if (OutReceivingCharacter)
	{
		*OutReceivingCharacter = nullptr;
	}
	if (!HasAuthority() || ItemInstance.IsEmpty())
	{
		return 0;
	}
	AMT2PlayerCharacter* ReceivingCharacter = ResolvePickupRecipient(RequestingCharacter);
	if (!ReceivingCharacter || !CanBePickedUpBy(ReceivingCharacter))
	{
		return 0;
	}
	if (OutReceivingCharacter)
	{
		*OutReceivingCharacter = ReceivingCharacter;
	}
	if (ItemInstance.Vnum == 1)
	{
		AMT2PlayerState* PlayerState = ReceivingCharacter->GetPlayerState<AMT2PlayerState>();
		const int64 PickedUpYang = PlayerState && PlayerState->AddYang(YangAmount)
			? YangAmount : 0;
		if (PickedUpYang > 0)
		{
			Destroy();
		}
		return PickedUpYang;
	}
	UMT2InventoryComponent* Inventory = ReceivingCharacter->GetInventoryComponent();
	const int32 AddedCount = Inventory ? Inventory->AddItemSlotPartial(ItemInstance) : 0;
	if (AddedCount <= 0)
	{
		return 0;
	}
	WakeForReplication();
	ItemInstance.Count -= AddedCount;
	if (ItemInstance.Count <= 0)
	{
		Destroy();
	}
	else
	{
		ForceNetUpdate();
	}
	return AddedCount;
}

FString AMT2WorldItem::GetItemDisplayName() const
{
	if (ItemInstance.Vnum == 1)
	{
		return FText::Format(
			NSLOCTEXT("MT2WorldItem", "YangName", "{0} Yang"),
			FText::AsNumber(YangAmount)).ToString();
	}
	const UMT2ItemTemplate* Template = MT2ItemUtils::ResolveTemplate(this, ItemInstance.Vnum);
	FString Name = Template && !Template->DisplayName.IsEmpty()
		? Template->DisplayName.ToString()
		: (Template && !Template->InternalName.IsEmpty()
			? Template->InternalName : FString::Printf(TEXT("Item %d"), ItemInstance.Vnum));
	if (Cast<UMT2ItemSkillBookTemplate>(Template) && ItemInstance.SkillVnum > 0)
	{
		if (const UMT2SkillDefinition* Skill =
			UMT2SkillSet::FindSkillAcrossSets(ItemInstance.SkillVnum))
		{
			Name = FString::Printf(
				TEXT("%s Skill Book"), *Skill->GetGradeDisplayName(0).ToString());
		}
	}
	if (ItemInstance.Count > 1)
	{
		Name += FString::Printf(TEXT(" x%d"), ItemInstance.Count);
	}
	return Name;
}

void AMT2WorldItem::ClearOwnership()
{
	if (!HasAuthority())
	{
		return;
	}
	WakeForReplication();
	OwnerCharacterId.Reset();
	OwnerDisplayName.Reset();
	ForceNetUpdate();
}

void AMT2WorldItem::OnRep_ItemData()
{
	RefreshVisuals();
}

void AMT2WorldItem::BroadcastDropSound()
{
	if (HasAuthority() && !ItemInstance.IsEmpty())
	{
		MulticastPlayDropSound(ItemInstance.Vnum);
		SetNetDormancy(DORM_DormantAll);
	}
}

void AMT2WorldItem::WakeForReplication()
{
	if (HasAuthority() && NetDormancy > DORM_Awake)
	{
		FlushNetDormancy();
	}
}

void AMT2WorldItem::MulticastPlayDropSound_Implementation(int32 DroppedVnum)
{
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	const UMT2ItemTemplate* Template = MT2ItemUtils::ResolveTemplate(this, DroppedVnum);
	const UMT2GameplaySettings& Settings = UMT2GameplaySettings::Get();

	const TSoftObjectPtr<USoundBase>* SelectedSound = &Settings.WorldItemDefaultDropSound;
	if (const UMT2ItemWeaponTemplate* Weapon = Cast<UMT2ItemWeaponTemplate>(Template))
	{
		SelectedSound = Weapon->WeaponSubType == 2
			? &Settings.WorldItemBowDropSound
			: Weapon->WeaponSubType == 6
				? &Settings.WorldItemDefaultDropSound
				: &Settings.WorldItemWeaponDropSound;
	}
	else if (const UMT2ItemArmorTemplate* Armor = Cast<UMT2ItemArmorTemplate>(Template))
	{
		SelectedSound = Armor->ArmorSubType == 0
			? &Settings.WorldItemArmorDropSound
			: (Armor->ArmorSubType == 5 || Armor->ArmorSubType == 6)
				? &Settings.WorldItemAccessoryDropSound
				: &Settings.WorldItemDefaultDropSound;
	}

	USoundBase* Sound = SelectedSound->LoadSynchronous();
	if (!Sound)
	{
		return;
	}

	USoundAttenuation* Attenuation = NewObject<USoundAttenuation>(GetTransientPackage());
	Attenuation->Attenuation.bAttenuate = true;
	Attenuation->Attenuation.bSpatialize = true;
	Attenuation->Attenuation.AttenuationShape = EAttenuationShape::Sphere;
	Attenuation->Attenuation.AttenuationShapeExtents =
		FVector(FMath::Max(Settings.WorldItemDropSoundInnerRadius, 0.0f), 0.0f, 0.0f);
	Attenuation->Attenuation.FalloffDistance =
		FMath::Max(Settings.WorldItemDropSoundFalloffDistance, 1.0f);

	UMT2SoundPlaybackSubsystem::PlayExclusiveAtLocation(
		this, Sound, GetActorLocation(), 1.0f, 1.0f, Attenuation);
}

void AMT2WorldItem::RefreshVisuals()
{
	const UMT2ItemTemplate* Template = MT2ItemUtils::ResolveTemplate(this, ItemInstance.Vnum);

	UStaticMesh* Mesh = Template ? Template->WorldMesh.LoadSynchronous() : nullptr;
	if (!Mesh)
	{
		Mesh = UMT2GameplaySettings::Get().FallbackWorldItemMesh.LoadSynchronous();
	}
	MeshComponent->SetStaticMesh(Mesh);
	MeshComponent->SetVisibility(Mesh != nullptr, true);
}
