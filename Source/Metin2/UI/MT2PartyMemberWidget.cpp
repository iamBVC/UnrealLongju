/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "UI/MT2PartyMemberWidget.h"

#include "Characters/MT2CharacterAppearanceSettings.h"
#include "Components/Button.h"
#include "Components/Image.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "Engine/Texture2D.h"
#include "Player/MT2PlayerController.h"
#include "Player/MT2PlayerState.h"

void UMT2PartyMemberWidget::NativeConstruct()
{
	Super::NativeConstruct();
	KickButton->OnClicked.AddUniqueDynamic(this, &UMT2PartyMemberWidget::HandleKickClicked);
}

void UMT2PartyMemberWidget::SetMember(const FMT2PartyMemberData& Member, bool bCanKick)
{
	MemberState = Member.PlayerState;
	MemberCharacterId = Member.CharacterId;
	NameText->SetText(FText::FromString(Member.bLeader
		? FString::Printf(TEXT("%s  [Leader]"), *Member.CharacterName)
		: Member.CharacterName));
	LevelText->SetText(FText::Format(
		NSLOCTEXT("MT2Party", "MemberLevel", "Lv {0}"), FText::AsNumber(Member.Level)));
	HealthBar->SetPercent(Member.MaxHealth > 0.0f
		? FMath::Clamp(Member.Health / Member.MaxHealth, 0.0f, 1.0f) : 0.0f);
	HealthBar->SetToolTipText(FText::Format(
		NSLOCTEXT("MT2Party", "MemberHealth", "HP: {0} / {1}"),
		FText::AsNumber(FMath::RoundToInt(Member.Health)),
		FText::AsNumber(FMath::RoundToInt(Member.MaxHealth))));

	const FMT2CharacterAppearanceAsset* Appearance =
		GetDefault<UMT2CharacterAppearanceSettings>()->FindAppearance(Member.Appearance);
	if (UTexture2D* FaceTexture = Appearance ? Appearance->FaceTexture.LoadSynchronous() : nullptr)
	{
		FaceImage->SetBrushFromTexture(FaceTexture, true);
	}
	KickButton->SetVisibility(bCanKick
		? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
}

void UMT2PartyMemberWidget::HandleKickClicked()
{
	if (AMT2PlayerController* Controller = Cast<AMT2PlayerController>(GetOwningPlayer()))
	{
		if (!MemberCharacterId.IsEmpty()) Controller->RequestKickPartyMemberById(MemberCharacterId);
		else Controller->RequestKickPartyMember(MemberState.Get());
	}
}
