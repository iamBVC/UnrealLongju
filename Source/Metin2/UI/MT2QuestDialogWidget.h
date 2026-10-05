/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "CoreMinimal.h"
#include "Types/SlateEnums.h"
#include "Quests/MT2QuestTypes.h"
#include "UI/MT2UserWidget.h"
#include "MT2QuestDialogWidget.generated.h"

class UButton;
class UEditableTextBox;
class UMT2BoardWidget;
class USizeBox;
class UTextBlock;
class UVerticalBox;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMT2DialogOptionSignature, int32, ChoiceIndex);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMT2DialogTextSignature, const FString&, Text);

// The old quest conversation window (uiQuest): board, title line, say() text, select() option
// buttons. Built natively; the quest component feeds it dialog payloads and receives the answer.
UCLASS()
class METIN2_API UMT2QuestDialogWidget : public UMT2UserWidget
{
	GENERATED_BODY()

public:
	void ShowDialog(const FMT2DialogPayload& Payload);
	void CloseDialog();

	// 1-based option index; 0 = dismissed (Escape / close).
	UPROPERTY(BlueprintAssignable, Category = "Quest")
	FMT2DialogOptionSignature OnOptionSelected;

	UPROPERTY(BlueprintAssignable, Category = "Quest")
	FMT2DialogTextSignature OnTextSubmitted;

	// The board sizes itself to its text, so short lines produce a tiny window. These set the floor it
	// can shrink to; the board still grows past them for longer dialogs. 0 leaves that dimension free.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Quest Dialog", meta = (ClampMin = "0.0"))
	float MinimumWidth = 320.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Quest Dialog", meta = (ClampMin = "0.0"))
	float MinimumHeight = 140.0f;

	// Applies the current minimums, so they can be changed at runtime as well as in the Blueprint.
	UFUNCTION(BlueprintCallable, Category = "Quest Dialog")
	void ApplyMinimumSize();

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual FReply NativeOnPreviewKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

private:
	UFUNCTION() void HandleOptionClicked();
	UFUNCTION() void HandleCloseClicked();
	UFUNCTION() void HandleInputCommitted(const FText& Text, ETextCommit::Type CommitMethod);
	UFUNCTION() void HandleInputConfirmed();

	UPROPERTY(Transient) TObjectPtr<USizeBox> BoardSizeBox;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TitleText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> BodyText;
	UPROPERTY(Transient) TObjectPtr<UVerticalBox> OptionsBox;
	UPROPERTY(Transient) TObjectPtr<UEditableTextBox> InputBox;
	UPROPERTY(Transient) TObjectPtr<UButton> InputConfirmButton;
	UPROPERTY(Transient) TArray<TObjectPtr<UButton>> OptionButtons;
	bool bNumericInputActive = false;

	int32 FindClickedOption() const;
};
