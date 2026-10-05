/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "CoreMinimal.h"
#include "UI/MT2UserWidget.h"
#include "MT2FriendRequestDialogWidget.generated.h"

class UButton;
class UTextBlock;

// "{Name} sent you a friend request" with accept and deny, shown to whoever was asked. Built on the
// same shape as the party invite dialog, since it answers the same kind of question.
UCLASS()
class METIN2_API UMT2FriendRequestDialogWidget : public UMT2UserWidget
{
	GENERATED_BODY()

public:
	void OpenRequest(const FString& RequesterId, const FString& RequesterName);

protected:
	virtual void NativeConstruct() override;

private:
	UFUNCTION() void HandleAcceptClicked();
	UFUNCTION() void HandleDenyClicked();
	void Respond(bool bAccept);

	FString PendingRequesterId;

	UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> MessageText;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> AcceptButton;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> DenyButton;
};
