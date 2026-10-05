/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "UI/MT2UserWidget.h"
#include "MT2GuildInviteDialogWidget.generated.h"

class UButton;
class UTextBlock;

UCLASS()
class METIN2_API UMT2GuildInviteDialogWidget : public UMT2UserWidget
{
	GENERATED_BODY()

public:
	void OpenInvite(const FString& InviterName, const FString& GuildName);

protected:
	virtual void NativeConstruct() override;

private:
	UFUNCTION() void HandleAccept();
	UFUNCTION() void HandleDecline();
	void Respond(bool bAccept);
	class UMT2GuildComponent* ResolveGuild() const;

	UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> MessageText;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> AcceptButton;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> DeclineButton;
};
