/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "CoreMinimal.h"
#include "UI/MT2UserWidget.h"
#include "MT2NotificationsWidget.generated.h"

class UButton;
class UMT2MessengerComponent;
class UMT2QuestManagerComponent;
class UPanelWidget;
class USizeBox;
class UTexture2D;

// A strip of standing notifications on the HUD: one entry per active quest and one per companion with
// unread private messages. Each entry is an icon with its name underneath - a scroll for a quest, a
// letter for messages - and clicking it opens the thing it refers to.
UCLASS()
class METIN2_API UMT2NotificationsWidget : public UMT2UserWidget
{
	GENERATED_BODY()

public:
	// Rebuilds the strip. Bound to the quest journal and messenger change delegates.
	UFUNCTION(BlueprintCallable, Category = "Notifications")
	void RefreshNotifications();

	UFUNCTION(BlueprintCallable, Category = "Notifications")
	void ToggleNotifications();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Notifications|Style")
	TSoftObjectPtr<UTexture2D> QuestIcon =
		TSoftObjectPtr<UTexture2D>(FSoftObjectPath(TEXT("/Game/ymir_work/icon/item/T_scroll_open.T_scroll_open")));

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Notifications|Style")
	TSoftObjectPtr<UTexture2D> MessageIcon =
		TSoftObjectPtr<UTexture2D>(FSoftObjectPath(TEXT("/Game/ymir_work/icon/action/T_letter.T_letter")));

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Notifications|Style")
	FVector2D IconSize = FVector2D(32.0, 32.0);

	// Entries are as wide as their icon plus room for a name; a long name is elided rather than
	// pushing its neighbours around.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Notifications|Style")
	float EntryWidth = 72.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Notifications|Style")
	int32 LabelFontSize = 8;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Notifications|Style")
	FLinearColor QuestLabelColor = FLinearColor(1.0f, 0.85f, 0.45f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Notifications|Style")
	FLinearColor MessageLabelColor = FLinearColor(0.65f, 0.82f, 1.0f);

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	// ---- bound from the Widget Blueprint ----
	// Entries are generated into this panel, so it must accept children (a HorizontalBox gives the
	// old game's row; a WrapBox copes better with many quests).
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UPanelWidget> NotificationsBox;

private:
	UFUNCTION() void HandleEntryClicked();
	UFUNCTION() void HandleSourcesChanged();

	UMT2MessengerComponent* ResolveMessenger() const;
	UMT2QuestManagerComponent* ResolveQuestManager() const;
	void BindSources();

	// An entry knows which quest or companion it stands for; the clicked button is found by hover,
	// since the rows are rebuilt on every refresh.
	struct FNotificationTarget
	{
		FName QuestId;
		FString CompanionId;
	};

	UPROPERTY(Transient) TArray<TObjectPtr<UButton>> EntryButtons;
	TArray<FNotificationTarget> EntryTargets;
	bool bUserHidden = false;

	UPROPERTY(Transient) TWeakObjectPtr<UMT2MessengerComponent> BoundMessenger;
	UPROPERTY(Transient) TWeakObjectPtr<UMT2QuestManagerComponent> BoundQuestManager;
};
