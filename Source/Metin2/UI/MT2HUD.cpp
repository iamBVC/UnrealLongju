/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "UI/MT2HUD.h"

#include "Player/MT2PlayerController.h"
#include "Player/MT2PlayerState.h"

#include "UI/MT2MessengerWidget.h"
#include "UI/MT2FriendAddDialogWidget.h"
#include "UI/MT2FriendRequestDialogWidget.h"
#include "UI/MT2NotificationsWidget.h"
#include "UI/MT2WhisperWidget.h"

#include "Messenger/MT2MessengerComponent.h"

#include "Blueprint/UserWidget.h"
#include "Characters/MT2CharacterAppearanceComponent.h"
#include "Characters/MT2PlayerCharacter.h"
#include "Engine/LevelStreaming.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "UI/MT2CharacterWindowWidget.h"
#include "UI/MT2GameHUDWidget.h"
#include "UI/MT2LoadingScreenWidget.h"
#include "UI/MT2StatusEffectBarWidget.h"
#include "UI/MT2PartyPanelWidget.h"
#include "UI/MT2PartyInviteDialogWidget.h"
#include "UI/MT2SystemMenuWidget.h"
#include "UI/MT2GuildWidget.h"
#include "UI/MT2GuildInviteDialogWidget.h"

void AMT2HUD::BeginPlay()
{
	Super::BeginPlay();

	APlayerController* OwningController = GetOwningPlayerController();
	if (OwningController && OwningController->IsLocalController())
	{
		UMT2SystemMenuWidget::ApplySavedAudioVideoSettings(this);
		const TSubclassOf<UMT2GameHUDWidget> GameHUDClass = ResolveGameHUDClass();
		if (!GameHUDClass)
		{
			UE_LOG(LogTemp, Error, TEXT("Required UI asset /Game/UI/MT2GameHUD is missing or invalid."));
			return;
		}
		GameHUDWidget = CreateWidget<UMT2GameHUDWidget>(OwningController, GameHUDClass);
		if (GameHUDWidget)
		{
			GameHUDWidget->AddToViewport();
		}

		// Fixed in-game windows are children of MT2GameHUD. Their Canvas positions stay editable in
		// the HUD Blueprint instead of being imposed by code.
		StatusEffectBarWidget = GameHUDWidget ? GameHUDWidget->GetStatusEffectBarWidget() : nullptr;
		PartyPanelWidget = GameHUDWidget ? GameHUDWidget->GetPartyPanelWidget() : nullptr;

		// The notification strip is part of the HUD, so it comes up with it rather than on demand.
		EnsureNotificationsWidget();

		StartWorldRevealCover();
	}
}

void AMT2HUD::ShowPartyInvite(int32 InviteId, const FString& InviterName)
{
	PartyInviteDialogWidget = GameHUDWidget ? GameHUDWidget->GetPartyInviteDialogWidget() : nullptr;
	if (PartyInviteDialogWidget)
	{
		PartyInviteDialogWidget->OpenInvite(InviteId, InviterName);
	}
}

void AMT2HUD::StartWorldRevealCover()
{
	WorldRevealWidget = GameHUDWidget ? GameHUDWidget->GetLoadingScreenWidget() : nullptr;
	if (!WorldRevealWidget)
	{
		UE_LOG(LogTemp, Error, TEXT("MT2GameHUD requires a BindWidget child named LoadingScreenWidget."));
		return;
	}
	WorldRevealWidget->SetVisibility(ESlateVisibility::Visible);
	WorldRevealWidget->SetLoadingText(TEXT("Entering the world..."));
	WorldRevealStartTime = FPlatformTime::Seconds();
	GetWorldTimerManager().SetTimer(
		WorldRevealTimer, this, &AMT2HUD::HandleWorldRevealCheck, 0.25f, true);
}

void AMT2HUD::HandleWorldRevealCheck()
{
	constexpr double MinCoverSeconds = 1.0;
	constexpr double MaxCoverSeconds = 20.0;
	const double Elapsed = FPlatformTime::Seconds() - WorldRevealStartTime;
	if (Elapsed >= MaxCoverSeconds)
	{
		// Never trap the player behind the cover: reveal even if something failed to load.
		RevealWorld();
		return;
	}
	if (Elapsed < MinCoverSeconds)
	{
		return;
	}

	// 1. The local pawn exists and its async-loaded mesh/anim assets are installed (no T-pose).
	const AMT2PlayerCharacter* Pawn = Cast<AMT2PlayerCharacter>(GetOwningPawn());
	const UMT2CharacterAppearanceComponent* Appearance =
		Pawn ? Pawn->GetAppearanceComponent() : nullptr;
	if (!Appearance || !Appearance->HasAppliedAppearance())
	{
		return;
	}

	// 2. Every streaming level that wants to be loaded actually is.
	if (const UWorld* World = GetWorld())
	{
		for (const ULevelStreaming* Streaming : World->GetStreamingLevels())
		{
			if (Streaming && Streaming->ShouldBeLoaded() && !Streaming->IsLevelLoaded())
			{
				return;
			}
		}
	}

	// 3. No async package loads still in flight (equipment/hair visuals stream the same way).
	if (IsAsyncLoading())
	{
		return;
	}
	RevealWorld();
}

void AMT2HUD::RevealWorld()
{
	GetWorldTimerManager().ClearTimer(WorldRevealTimer);
	if (WorldRevealWidget)
	{
		WorldRevealWidget->SetVisibility(ESlateVisibility::Collapsed);
	}
}

bool AMT2HUD::EnsureSystemMenuWidget()
{
	SystemMenuWidget = GameHUDWidget ? GameHUDWidget->GetSystemMenuWidget() : nullptr;
	if (!SystemMenuWidget)
	{
		UE_LOG(LogTemp, Error, TEXT("MT2GameHUD requires a BindWidget child named SystemMenuWidget."));
		return false;
	}
	return true;
}

void AMT2HUD::ToggleSystemMenu()
{
	if (EnsureSystemMenuWidget())
	{
		SystemMenuWidget->ToggleMenu();
	}
}

TSubclassOf<UMT2GameHUDWidget> AMT2HUD::ResolveGameHUDClass() const
{
	if (UClass* WidgetClass = LoadClass<UMT2GameHUDWidget>(nullptr, TEXT("/Game/UI/MT2GameHUD.MT2GameHUD_C")))
	{
		return WidgetClass;
	}
	return nullptr;
}

void AMT2HUD::ToggleInventory()
{
	if (GameHUDWidget)
	{
		GameHUDWidget->ToggleInventory();
	}
}

void AMT2HUD::ActivateQuickSlot(int32 SlotIndex)
{
	if (GameHUDWidget)
	{
		GameHUDWidget->ActivateQuickSlot(SlotIndex);
	}
}

void AMT2HUD::ToggleCharacterWindow()
{
	if (GameHUDWidget)
	{
		GameHUDWidget->ToggleCharacterWindow(EMT2CharacterWindowPage::Stats);
	}
}

void AMT2HUD::ToggleSkillWindow()
{
	if (GameHUDWidget)
	{
		GameHUDWidget->ToggleCharacterWindow(EMT2CharacterWindowPage::Skills);
	}
}

void AMT2HUD::ToggleBeltWindow()
{
	UE_LOG(LogTemp, Display, TEXT("MT2 UI hotkey B: belt window not implemented yet."));
}

bool AMT2HUD::EnsureMessengerWidget()
{
	MessengerWidget = GameHUDWidget ? GameHUDWidget->GetMessengerWidget() : nullptr;
	if (!MessengerWidget)
	{
		UE_LOG(LogTemp, Error, TEXT("MT2GameHUD requires a BindWidget child named MessengerWidget."));
	}
	return MessengerWidget != nullptr;
}

bool AMT2HUD::EnsureFriendAddDialogWidget()
{
	FriendAddDialogWidget = GameHUDWidget ? GameHUDWidget->GetFriendAddDialogWidget() : nullptr;
	if (!FriendAddDialogWidget)
	{
		UE_LOG(LogTemp, Error, TEXT("MT2GameHUD requires a BindWidget child named FriendAddDialogWidget."));
	}
	return FriendAddDialogWidget != nullptr;
}

void AMT2HUD::ToggleMessengerWindow()
{
	if (!EnsureMessengerWidget())
	{
		return;
	}
	const bool bVisible = MessengerWidget->GetVisibility() != ESlateVisibility::Collapsed;
	MessengerWidget->SetVisibility(bVisible ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
	if (!bVisible)
	{
		MessengerWidget->RefreshMessenger();
	}
}

void AMT2HUD::OpenFriendAddDialog()
{
	if (EnsureFriendAddDialogWidget())
	{
		FriendAddDialogWidget->OpenDialog();
	}
}

bool AMT2HUD::EnsureWhisperWidget()
{
	WhisperWidget = GameHUDWidget ? GameHUDWidget->GetWhisperWidget() : nullptr;
	if (!WhisperWidget)
	{
		UE_LOG(LogTemp, Error, TEXT("MT2GameHUD requires a BindWidget child named WhisperWidget."));
	}
	return WhisperWidget != nullptr;
}

bool AMT2HUD::EnsureNotificationsWidget()
{
	NotificationsWidget = GameHUDWidget ? GameHUDWidget->GetNotificationsWidget() : nullptr;
	if (!NotificationsWidget)
	{
		UE_LOG(LogTemp, Error, TEXT("MT2GameHUD requires a BindWidget child named NotificationsWidget."));
	}
	return NotificationsWidget != nullptr;
}

void AMT2HUD::ShowFriendRequest(const FString& RequesterId, const FString& RequesterName)
{
	FriendRequestDialogWidget = GameHUDWidget ? GameHUDWidget->GetFriendRequestDialogWidget() : nullptr;
	if (FriendRequestDialogWidget)
	{
		FriendRequestDialogWidget->OpenRequest(RequesterId, RequesterName);
	}
}

void AMT2HUD::OpenWhisperWindow()
{
	if (EnsureWhisperWidget())
	{
		WhisperWidget->RefreshWhisper();
	}
}

void AMT2HUD::OpenMessengerConversation(AActor* TargetPlayer)
{
	// The target board's "Message" button: the whisper dialog opens straight onto that player, with
	// no detour through the friend list.
	// Every failure below is reported: a button that silently does nothing is impossible to tell
	// apart from a broken one.
	auto Report = [this](const TCHAR* Reason)
	{
		if (AMT2PlayerController* Controller = Cast<AMT2PlayerController>(GetOwningPlayerController()))
		{
			Controller->AddInfoChatLine(Reason);
		}
	};
	if (!TargetPlayer)
	{
		return;
	}
	if (!EnsureWhisperWidget())
	{
		Report(TEXT("Whisper: the messenger window could not be opened."));
		return;
	}
	const AMT2PlayerState* State = GetOwningPlayerController()
		? GetOwningPlayerController()->GetPlayerState<AMT2PlayerState>() : nullptr;
	UMT2MessengerComponent* Messenger = State ? State->GetMessengerComponent() : nullptr;
	if (!Messenger)
	{
		Report(TEXT("Whisper: your messenger is not ready yet."));
		return;
	}
	// The dialog shows itself once the server answers with the target's character id, which is what
	// fills in the name and the stored history.
	Messenger->OpenConversationWithPlayer(TargetPlayer);
}

void AMT2HUD::ToggleQuestWindow()
{
	if (GameHUDWidget)
	{
		GameHUDWidget->ToggleCharacterWindow(EMT2CharacterWindowPage::Quests);
	}
}

void AMT2HUD::ToggleMinimapWindow()
{
	if (GameHUDWidget)
	{
		GameHUDWidget->ToggleFullMap();
	}
}

void AMT2HUD::ToggleGuildWindow()
{
	GuildWidget = GameHUDWidget ? GameHUDWidget->GetGuildWidget() : nullptr;
	if (GuildWidget) GuildWidget->ToggleWindow();
}

void AMT2HUD::ShowGuildInvite(const FString& InviterName, const FString& GuildName)
{
	GuildInviteDialogWidget = GameHUDWidget ? GameHUDWidget->GetGuildInviteDialogWidget() : nullptr;
	if (GuildInviteDialogWidget) GuildInviteDialogWidget->OpenInvite(InviterName, GuildName);
}
