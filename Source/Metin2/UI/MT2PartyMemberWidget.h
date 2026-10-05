/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "CoreMinimal.h"
#include "Party/MT2PartyTypes.h"
#include "UI/MT2UserWidget.h"
#include "MT2PartyMemberWidget.generated.h"

class UImage;
class UButton;
class UProgressBar;
class UTextBlock;
class AMT2PlayerState;

UCLASS()
class METIN2_API UMT2PartyMemberWidget : public UMT2UserWidget
{
	GENERATED_BODY()

public:
	void SetMember(const FMT2PartyMemberData& Member, bool bCanKick);

protected:
	virtual void NativeConstruct() override;

private:
	UFUNCTION()
	void HandleKickClicked();

	UPROPERTY(meta = (BindWidget)) TObjectPtr<UImage> FaceImage;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> NameText;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> LevelText;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UProgressBar> HealthBar;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> KickButton;

	TWeakObjectPtr<AMT2PlayerState> MemberState;
	FString MemberCharacterId;
};
