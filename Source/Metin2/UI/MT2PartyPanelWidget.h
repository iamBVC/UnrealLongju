/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "CoreMinimal.h"
#include "UI/MT2UserWidget.h"
#include "MT2PartyPanelWidget.generated.h"

class AMT2Party;
class AMT2PlayerState;
class UMT2PartyMemberWidget;
class UButton;
class UVerticalBox;

UCLASS()
class METIN2_API UMT2PartyPanelWidget : public UMT2UserWidget
{
	GENERATED_BODY()

public:
	UMT2PartyPanelWidget(const FObjectInitializer& ObjectInitializer);
	void RefreshPartyBinding();

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
	void RebuildMembers();
	void UnbindParty();

	UFUNCTION() void HandlePartyChanged(AMT2Party* OldParty, AMT2Party* NewParty);
	UFUNCTION() void HandleMembersChanged();
	UFUNCTION() void HandlePlayerSnapshotChanged();
	UFUNCTION() void HandleMembershipChanged(bool bInParty);
	UFUNCTION() void HandleDisbandClicked();
	UFUNCTION() void HandleLeaveClicked();

	UPROPERTY(meta = (BindWidget)) TObjectPtr<UVerticalBox> MemberList;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> DisbandButton;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> LeavePartyButton;
	UPROPERTY(EditDefaultsOnly, Category = "Party")
	TSoftClassPtr<UMT2PartyMemberWidget> MemberWidgetClass;
	UPROPERTY(Transient)
	TMap<FString, TObjectPtr<UMT2PartyMemberWidget>> MemberRows;

	TWeakObjectPtr<AMT2PlayerState> BoundPlayerState;
	TWeakObjectPtr<AMT2Party> BoundParty;
};
