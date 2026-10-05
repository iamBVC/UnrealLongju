/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "UI/MT2UserWidget.h"
#include "CoreMinimal.h"
#include "MT2TargetInfoWidget.generated.h"

class UBorder;
class UButton;
class UHorizontalBox;
class UMT2CombatComponent;
class UMT2HealthComponent;
class AMT2PlayerState;
class UProgressBar;
class UTextBlock;

// Fired when one of the player-target action buttons is clicked (old uitarget.py TargetBoard
// buttons). Action is one of: Whisper, Trade, Duel, Party, Friend, Guild. The corresponding
// subsystems (private messages, commerce, duel, party, guilds) bind here once they exist.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FMT2TargetActionSignature, FName, Action, AActor*, Target);

UCLASS()
class METIN2_API UMT2TargetInfoWidget : public UMT2UserWidget
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Target")
	void SetTarget(AActor* NewTarget);

	UPROPERTY(BlueprintAssignable, Category = "Target")
	FMT2TargetActionSignature OnPlayerActionRequested;

	// Called by the always-visible HUD while the replicated pawn is arriving on standalone clients.
	void RefreshCombatBinding();

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
	void BindCombatComponent();
	void UnbindCombatComponent();
	void BindTargetHealth();
	void UnbindTargetHealth();
	void BindTargetPlayerState();
	void UnbindTargetPlayerState();
	void BindLocalPartyState();
	void UnbindLocalPartyState();
	void RefreshTargetInfo();
	FText BuildTargetLabel(const AActor* Target) const;

	UFUNCTION()
	void HandleSelectedTargetChanged(AActor* OldTarget, AActor* NewTarget);

	UFUNCTION()
	void HandleTargetResourceChanged(float OldValue, float NewValue);

	UFUNCTION()
	void HandleTargetDeath();

	UFUNCTION()
	void HandleTargetDestroyed(AActor* DestroyedActor);

	UFUNCTION()
	void HandleTargetPartyMembershipChanged(bool bInParty);

	UFUNCTION()
	void HandleLocalPartyMembershipChanged(bool bInParty);

	UFUNCTION()
	void HandleLocalPartySnapshotChanged();

	UFUNCTION()
	void HandleCloseClicked();

	UFUNCTION() void HandleWhisperClicked();
	UFUNCTION() void HandleTradeClicked();
	UFUNCTION() void HandleDuelClicked();
	UFUNCTION() void HandlePartyClicked();
	UFUNCTION() void HandleFriendClicked();
	UFUNCTION() void HandleGuildClicked();

	// Old uitarget.py: the small-thin-button row under the board, players only. Built at runtime
	// into the generated Blueprint's root canvas so the user-owned UI asset stays untouched.
	void EnsurePlayerActionRow();
	void RequestPlayerAction(FName Action);

	UPROPERTY(Transient)
	TObjectPtr<UHorizontalBox> PlayerActionRow;

	UPROPERTY(Transient)
	TObjectPtr<UButton> PartyButton;

	TWeakObjectPtr<UMT2CombatComponent> BoundCombatComponent;
	TWeakObjectPtr<AActor> TargetActor;
	TWeakObjectPtr<UMT2HealthComponent> TargetHealthComponent;
	TWeakObjectPtr<AMT2PlayerState> TargetPlayerState;
	TWeakObjectPtr<AMT2PlayerState> LocalPartyPlayerState;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UBorder> PanelBackground;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> TargetNameText;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UProgressBar> HealthBar;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> CloseButton;
};
