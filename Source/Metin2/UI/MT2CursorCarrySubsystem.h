/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateBrush.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "MT2CursorCarrySubsystem.generated.h"

class UMT2CursorCarryWidget;

UENUM(BlueprintType)
enum class EMT2CarryKind : uint8
{
	None,
	InventoryItem,   // Payload = source inventory slot index, Vnum = item vnum
	EquippedItem,    // Payload = wear position
	Skill            // Vnum = skill vnum
};

// Old-client style "attached icon" moving: one left click picks a skill/item up onto the cursor,
// the next left click on a valid slot places it. Replaces hold-to-drag everywhere.
UCLASS()
class METIN2_API UMT2CursorCarrySubsystem : public ULocalPlayerSubsystem
{
	GENERATED_BODY()

public:
	// SourceQuickSlot: global quickslot index the carry was lifted from (INDEX_NONE otherwise);
	// placing it elsewhere - or on the ground - clears that source binding (move semantics).
	void BeginCarry(EMT2CarryKind Kind, int32 Vnum, int32 Payload, const FSlateBrush& IconBrush,
		int32 InSourceQuickSlot = INDEX_NONE);

	UFUNCTION(BlueprintCallable, Category = "Carry")
	void CancelCarry();

	UFUNCTION(BlueprintPure, Category = "Carry")
	bool IsCarrying() const { return CarryKind != EMT2CarryKind::None; }

	EMT2CarryKind GetCarryKind() const { return CarryKind; }
	int32 GetCarryVnum() const { return CarryVnum; }
	int32 GetCarryPayload() const { return CarryPayload; }
	int32 GetSourceQuickSlot() const { return SourceQuickSlot; }

	// Consumes the carry (the caller placed it) and hides the cursor icon.
	void EndCarry();

private:
	EMT2CarryKind CarryKind = EMT2CarryKind::None;
	int32 CarryVnum = 0;
	int32 CarryPayload = INDEX_NONE;
	int32 SourceQuickSlot = INDEX_NONE;

	UPROPERTY(Transient)
	TObjectPtr<UMT2CursorCarryWidget> CarryWidget;
};
