/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "UI/MT2NameplateWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Config/MT2GameplaySettings.h"
#include "Engine/Texture2D.h"
#include "InputCoreTypes.h"
#include "UI/MT2EmpireFlagImage.h"
#include "Guild/MT2GuildMarkCacheSubsystem.h"

namespace
{
	const FLinearColor LevelColor(152.0f / 255.0f, 1.0f, 51.0f / 255.0f);
	const FLinearColor PlayerNameColor(1.0f, 0.82f, 0.08f);
	const FLinearColor PartyMemberNameColor(0.0f, 0.9f, 1.0f);
	const FLinearColor AggressivePlayerNameColor(1.0f, 0.38f, 0.04f);
	const FLinearColor MobNameColor(1.0f, 0.12f, 0.08f);
	const FLinearColor NpcNameColor(0.18f, 1.0f, 0.32f);
	const FLinearColor GuildColor(0.9f, 0.9f, 0.95f);
	const FLinearColor ItemNameColor(1.0f, 0.86f, 0.22f);
	const FLinearColor ItemOwnerColor(0.55f, 0.82f, 1.0f);
}

void UMT2NameplateWidget::SetPlayerInfo(
	int32 GuildId, const FString& GuildName, int32 Level, int32 KarmaPoints,
	const FString& PlayerName, EMT2Empire Empire, bool bAggressiveMode,
	bool bPartyMember)
{
	bItemNameplate = false;
	SetLabel(GuildText, FText::FromString(GuildName), GuildColor, !GuildName.IsEmpty());
	UTexture2D* GuildMark = nullptr;
	if (UGameInstance* GameInstance = GetGameInstance())
		if (UMT2GuildMarkCacheSubsystem* Cache = GameInstance->GetSubsystem<UMT2GuildMarkCacheSubsystem>()) GuildMark = Cache->GetMark(GuildId);
	GuildMarkImage->SetBrushFromTexture(GuildMark);
	GuildMarkImage->SetVisibility(GuildMark ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	SetLabel(LevelText, FText::Format(NSLOCTEXT("MT2Nameplate", "PlayerLevel", "Lv {0}"),
		FText::AsNumber(Level)), LevelColor, true);
	SetLabel(KarmaText, GetKarmaTitle(KarmaPoints), GetKarmaColor(KarmaPoints), true);
	SetLabel(NameText, FText::FromString(PlayerName),
		bPartyMember ? PartyMemberNameColor
			: (bAggressiveMode ? AggressivePlayerNameColor : PlayerNameColor),
		!PlayerName.IsEmpty());
	SetEmpireFlag(Empire);
}

void UMT2NameplateWidget::SetEmpireFlag(EMT2Empire Empire)
{
	if (!EmpireFlagIcon)
	{
		UPanelWidget* NameRow = NameText ? NameText->GetParent() : nullptr;
		if (!NameRow || !WidgetTree)
		{
			return;
		}
		EmpireFlagIcon = WidgetTree->ConstructWidget<UMT2EmpireFlagImage>(
			UMT2EmpireFlagImage::StaticClass(), TEXT("EmpireFlagIcon"));
		if (UHorizontalBox* Row = Cast<UHorizontalBox>(NameRow))
		{
			if (UHorizontalBoxSlot* FlagSlot = Row->AddChildToHorizontalBox(EmpireFlagIcon))
			{
				FlagSlot->SetPadding(FMargin(3.0f, 0.0f));
				FlagSlot->SetVerticalAlignment(VAlign_Center);
			}
		}
		else
		{
			NameRow->AddChild(EmpireFlagIcon);
		}
	}
	EmpireFlagIcon->SetEmpire(Empire);
}

void UMT2NameplateWidget::SetSpeaking(bool bSpeaking)
{
	// Nothing to hide and nothing requested: skip the lazy construction entirely.
	if (!bSpeaking && !SpeakerIcon)
	{
		return;
	}

	if (!SpeakerIcon)
	{
		// No icon assigned in Project Settings -> Metin2 -> Voice Chat: the speaking logic still
		// runs, there is just nothing to draw.
		UTexture2D* IconTexture = UMT2GameplaySettings::Get().VoiceSpeakingIcon.LoadSynchronous();
		UPanelWidget* NameRow = NameText ? NameText->GetParent() : nullptr;
		if (!IconTexture || !NameRow || !WidgetTree)
		{
			return;
		}
		SpeakerIcon = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("SpeakerIcon"));
		SpeakerIcon->SetBrushFromTexture(IconTexture, false);
		SpeakerIcon->SetDesiredSizeOverride(FVector2D(14.0f, 14.0f));
		// The generated nameplate puts NameText in a horizontal row, so appending lands the icon
		// right beside the name; any other panel still gets the icon, just wherever it appends.
		if (UHorizontalBox* Row = Cast<UHorizontalBox>(NameRow))
		{
			if (UHorizontalBoxSlot* IconSlot = Row->AddChildToHorizontalBox(SpeakerIcon))
			{
				IconSlot->SetPadding(FMargin(2.0f, 0.0f));
				IconSlot->SetVerticalAlignment(VAlign_Center);
			}
		}
		else
		{
			NameRow->AddChild(SpeakerIcon);
		}
	}

	SpeakerIcon->SetVisibility(
		bSpeaking ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
}

void UMT2NameplateWidget::ShowChatMessage(const FString& Message, bool bWorldBroadcast)
{
	if (Message.IsEmpty() || !WidgetTree)
	{
		return;
	}
	if (!WorldChatText)
	{
		UCanvasPanel* Root = Cast<UCanvasPanel>(WidgetTree->RootWidget);
		if (!Root)
		{
			return;
		}
		WorldChatText = WidgetTree->ConstructWidget<UTextBlock>(
			UTextBlock::StaticClass(), TEXT("WorldChatText"));
		WorldChatText->SetJustification(ETextJustify::Center);
		WorldChatText->SetAutoWrapText(true);
		WorldChatText->SetWrapTextAt(400.0f);
		FSlateFontInfo Font = WorldChatText->GetFont();
		Font.Size = 10;
		Font.OutlineSettings.OutlineSize = 1;
		Font.OutlineSettings.OutlineColor = FLinearColor::Black;
		WorldChatText->SetFont(Font);
		WorldChatText->SetShadowOffset(FVector2D(1.0f));
		WorldChatText->SetShadowColorAndOpacity(FLinearColor::Black);
		if (UCanvasPanelSlot* CanvasSlot = Root->AddChildToCanvas(WorldChatText))
		{
			CanvasSlot->SetPosition(FVector2D(210.0f, 45.0f));
			CanvasSlot->SetAlignment(FVector2D(0.5f, 0.0f));
			CanvasSlot->SetAutoSize(true);
		}
	}
	WorldChatText->SetColorAndOpacity(FSlateColor(bWorldBroadcast
		? FLinearColor::FromSRGBColor(FColor(0x60, 0xC0, 0x60))
		: FLinearColor(0.94f, 0.93f, 0.82f, 1.0f)));
	WorldChatText->SetText(FText::FromString(Message));
	WorldChatText->SetVisibility(ESlateVisibility::HitTestInvisible);
	WorldChatVisibleUntil = (GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0) + 5.0;
}

void UMT2NameplateWidget::NativeTick(
	const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	if (WorldChatText && WorldChatText->GetVisibility() != ESlateVisibility::Collapsed &&
		GetWorld() && GetWorld()->GetTimeSeconds() >= WorldChatVisibleUntil)
	{
		WorldChatText->SetVisibility(ESlateVisibility::Collapsed);
	}
}

void UMT2NameplateWidget::SetMobInfo(int32 Level, const FString& MobName)
{
	bItemNameplate = false;
	SetEmpireFlag(EMT2Empire::None);
	SetLabel(GuildText, FText::GetEmpty(), GuildColor, false);
	GuildMarkImage->SetVisibility(ESlateVisibility::Collapsed);
	SetLabel(LevelText, FText::Format(NSLOCTEXT("MT2Nameplate", "MobLevel", "Lv {0}"),
		FText::AsNumber(Level)), LevelColor, true);
	SetLabel(KarmaText, FText::GetEmpty(), FLinearColor::White, false);
	SetLabel(NameText, FText::FromString(MobName), MobNameColor, !MobName.IsEmpty());
}

void UMT2NameplateWidget::SetNpcInfo(const FString& NpcName)
{
	bItemNameplate = false;
	SetEmpireFlag(EMT2Empire::None);
	SetLabel(GuildText, FText::GetEmpty(), GuildColor, false);
	GuildMarkImage->SetVisibility(ESlateVisibility::Collapsed);
	SetLabel(LevelText, FText::GetEmpty(), LevelColor, false);
	SetLabel(KarmaText, FText::GetEmpty(), FLinearColor::White, false);
	SetLabel(NameText, FText::FromString(NpcName), NpcNameColor, !NpcName.IsEmpty());
}

void UMT2NameplateWidget::SetItemInfo(const FString& ItemName, const FString& OwnerName)
{
	bItemNameplate = true;
	SetEmpireFlag(EMT2Empire::None);
	SetLabel(GuildText,
		OwnerName.IsEmpty() ? FText::GetEmpty() : FText::Format(
			NSLOCTEXT("MT2Nameplate", "ItemOwner", "Owner: {0}"), FText::FromString(OwnerName)),
		ItemOwnerColor, !OwnerName.IsEmpty());
	GuildMarkImage->SetVisibility(ESlateVisibility::Collapsed);
	SetLabel(LevelText, FText::GetEmpty(), FLinearColor::White, false);
	SetLabel(KarmaText, FText::GetEmpty(), FLinearColor::White, false);
	SetLabel(NameText, FText::FromString(ItemName), ItemNameColor, !ItemName.IsEmpty());
}

FReply UMT2NameplateWidget::NativeOnMouseButtonDown(
	const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (bItemNameplate && InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		OnItemClicked.Broadcast();
		return FReply::Handled();
	}
	return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
}

FText UMT2NameplateWidget::GetKarmaTitle(int32 KarmaPoints)
{
	if (KarmaPoints >= 12000) return NSLOCTEXT("MT2Karma", "Chivalric", "Chivalric");
	if (KarmaPoints >= 8000) return NSLOCTEXT("MT2Karma", "Noble", "Noble");
	if (KarmaPoints >= 4000) return NSLOCTEXT("MT2Karma", "Good", "Good");
	if (KarmaPoints >= 1000) return NSLOCTEXT("MT2Karma", "Friendly", "Friendly");
	if (KarmaPoints >= 0) return NSLOCTEXT("MT2Karma", "Neutral", "Neutral");
	if (KarmaPoints > -4000) return NSLOCTEXT("MT2Karma", "Aggressive", "Aggressive");
	if (KarmaPoints > -8000) return NSLOCTEXT("MT2Karma", "Fraudulent", "Fraudulent");
	if (KarmaPoints > -12000) return NSLOCTEXT("MT2Karma", "Malicious", "Malicious");
	return NSLOCTEXT("MT2Karma", "Cruel", "Cruel");
}

FLinearColor UMT2NameplateWidget::GetKarmaColor(int32 KarmaPoints)
{
	if (KarmaPoints >= 12000) return FLinearColor(0.18f, 0.48f, 1.0f);
	if (KarmaPoints >= 8000) return FLinearColor(0.12f, 0.7f, 1.0f);
	if (KarmaPoints >= 4000) return FLinearColor(0.1f, 0.9f, 0.85f);
	if (KarmaPoints >= 1000) return FLinearColor(0.25f, 1.0f, 0.45f);
	if (KarmaPoints >= 0) return FLinearColor(0.9f, 0.9f, 0.9f);
	if (KarmaPoints > -4000) return FLinearColor(1.0f, 0.68f, 0.12f);
	if (KarmaPoints > -8000) return FLinearColor(1.0f, 0.38f, 0.08f);
	if (KarmaPoints > -12000) return FLinearColor(1.0f, 0.12f, 0.08f);
	return FLinearColor(0.65f, 0.01f, 0.01f);
}

void UMT2NameplateWidget::SetLabel(
	UTextBlock* Label, const FText& Text, const FLinearColor& Color, bool bVisible)
{
	if (!Label)
	{
		return;
	}
	Label->SetVisibility(bVisible ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	if (bVisible)
	{
		if (!Label->GetText().EqualTo(Text)) Label->SetText(Text);
		Label->SetColorAndOpacity(FSlateColor(Color));
	}
}
