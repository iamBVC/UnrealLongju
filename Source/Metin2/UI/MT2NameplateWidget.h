/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "UI/MT2UserWidget.h"
#include "CoreMinimal.h"
#include "Player/MT2PlayerTypes.h"
#include "MT2NameplateWidget.generated.h"

class UImage;
class UTextBlock;
class UMT2EmpireFlagImage;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FMT2ItemNameplateClickedSignature);

UCLASS()
class METIN2_API UMT2NameplateWidget : public UMT2UserWidget
{
	GENERATED_BODY()

public:
	void SetPlayerInfo(int32 GuildId, const FString& GuildName, int32 Level, int32 KarmaPoints,
		const FString& PlayerName, EMT2Empire Empire, bool bAggressiveMode,
		bool bPartyMember);
	// Shows/hides the voice-chat speaker icon beside the name. The icon widget is created lazily at
	// runtime (inserted into NameText's row), so the user-managed nameplate Blueprint needs no
	// change; its texture comes from UMT2GameplaySettings::VoiceSpeakingIcon.
	void SetSpeaking(bool bSpeaking);
	void ShowChatMessage(const FString& Message, bool bWorldBroadcast);
	void SetMobInfo(int32 Level, const FString& MobName);
	void SetNpcInfo(const FString& NpcName);
	void SetItemInfo(const FString& ItemName, const FString& OwnerName);

	UPROPERTY(BlueprintAssignable, Category = "Nameplate")
	FMT2ItemNameplateClickedSignature OnItemClicked;

protected:
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual FReply NativeOnMouseButtonDown(
		const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;

private:
	static FText GetKarmaTitle(int32 KarmaPoints);
	static FLinearColor GetKarmaColor(int32 KarmaPoints);
	static void SetLabel(UTextBlock* Label, const FText& Text, const FLinearColor& Color, bool bVisible);
	void SetEmpireFlag(EMT2Empire Empire);

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> GuildText;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> GuildMarkImage;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> LevelText;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> KarmaText;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> NameText;

	// Runtime-created voice indicator; lives in the same row as NameText.
	UPROPERTY(Transient)
	TObjectPtr<UImage> SpeakerIcon;

	UPROPERTY(Transient)
	TObjectPtr<UMT2EmpireFlagImage> EmpireFlagIcon;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> WorldChatText;

	double WorldChatVisibleUntil = 0.0;

	bool bItemNameplate = false;
};
