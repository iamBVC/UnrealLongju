/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "CoreMinimal.h"
#include "UI/MT2UserWidget.h"
#include "MT2PartyInviteDialogWidget.generated.h"

class UButton;
class UTextBlock;

UCLASS()
class METIN2_API UMT2PartyInviteDialogWidget : public UMT2UserWidget
{
	GENERATED_BODY()

public:
	void OpenInvite(int32 InviteId, const FString& InviterName);

protected:
	virtual void NativeConstruct() override;

private:
	UFUNCTION() void HandleAcceptClicked();
	UFUNCTION() void HandleDeclineClicked();
	void Respond(bool bAccept);

	int32 PendingInviteId = 0;

	UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> MessageText;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> AcceptButton;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> DeclineButton;
};

