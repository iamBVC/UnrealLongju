/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "Components/ActorComponent.h"
#include "CoreMinimal.h"
#include "Player/MT2PlayerTypes.h"
#include "MT2CharacterAppearanceComponent.generated.h"

class UAnimInstance;
class USkeletalMesh;
class USkeletalMeshComponent;
class UTexture2D;
struct FStreamableHandle;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FMT2AppearanceAppliedSignature, const FMT2CharacterAppearance&, Appearance);

UCLASS(BlueprintType, ClassGroup = "MT2", meta = (BlueprintSpawnableComponent))
class METIN2_API UMT2CharacterAppearanceComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMT2CharacterAppearanceComponent();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	void SetMeshComponent(USkeletalMeshComponent* InMeshComponent);

	UFUNCTION(BlueprintCallable, Category = "Appearance")
	bool ApplyCharacterAppearance(const FMT2CharacterAppearance& Appearance);

	void ApplyVisualProfile(
		TSoftObjectPtr<USkeletalMesh> Mesh,
		TSoftObjectPtr<UTexture2D> DiffuseTexture,
		TSoftClassPtr<UAnimInstance> AnimInstanceClass,
		const FMT2CharacterAppearance& Appearance);

	UFUNCTION(BlueprintPure, Category = "Appearance")
	const FMT2CharacterAppearance& GetAppliedAppearance() const { return AppliedAppearance; }

	// True once the async-loaded mesh/anim assets have actually been installed on the mesh
	// component at least once - i.e. the pawn is no longer the T-posed placeholder.
	bool HasAppliedAppearance() const { return bHasAppliedAppearance; }

	UPROPERTY(BlueprintAssignable, Category = "Appearance")
	FMT2AppearanceAppliedSignature OnAppearanceApplied;

private:
	void OnVisualAssetsLoaded(
		int32 RequestId,
		FMT2CharacterAppearance Appearance,
		FSoftObjectPath MeshPath,
		FSoftObjectPath TexturePath,
		FSoftObjectPath AnimClassPath);

	UPROPERTY(Transient)
	TObjectPtr<USkeletalMeshComponent> MeshComponent;

	UPROPERTY(Transient)
	FMT2CharacterAppearance AppliedAppearance;

	TSharedPtr<FStreamableHandle> LoadHandle;
	int32 ActiveRequestId = 0;
	bool bHasAppliedAppearance = false;
};
