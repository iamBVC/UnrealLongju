/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "UI/MT2GuildWidget.h"

#include "Components/Button.h"
#include "Components/EditableTextBox.h"
#include "Components/HorizontalBox.h"
#include "Components/Image.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Blueprint/WidgetTree.h"
#include "Guild/MT2GuildComponent.h"
#include "Player/MT2PlayerController.h"
#include "Player/MT2PlayerState.h"
#include "UI/MT2TitleBarWidget.h"
#include "UI/MT2AtlasImage.h"
#include "Guild/MT2GuildMarkCacheSubsystem.h"
#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <commdlg.h>
#include "Windows/HideWindowsPlatformTypes.h"
#endif

namespace
{
	FText GuildResultText(FName Action, EMT2GuildResult Result)
	{
		if (Result == EMT2GuildResult::Success)
		{
			if (Action == TEXT("create")) return NSLOCTEXT("MT2Guild", "CreateSuccess", "Guild created successfully.");
			if (Action == TEXT("invite")) return NSLOCTEXT("MT2Guild", "InviteSuccess", "Guild invitation sent.");
			if (Action == TEXT("leave")) return NSLOCTEXT("MT2Guild", "LeaveSuccess", "You left the guild.");
			if (Action == TEXT("disband")) return NSLOCTEXT("MT2Guild", "DisbandSuccess", "The guild was disbanded.");
			if (Action == TEXT("mark")) return NSLOCTEXT("MT2Guild", "MarkSuccess", "Guild mark updated.");
			return NSLOCTEXT("MT2Guild", "ActionSuccess", "Guild action completed.");
		}

		switch (Result)
		{
		case EMT2GuildResult::Unavailable: return NSLOCTEXT("MT2Guild", "UnavailableError", "The guild service is currently unavailable.");
		case EMT2GuildResult::InvalidName: return NSLOCTEXT("MT2Guild", "InvalidNameError", "Enter a valid guild name between 2 and 12 characters.");
		case EMT2GuildResult::NameUnavailable: return NSLOCTEXT("MT2Guild", "NameUnavailableError", "That guild name is already in use.");
		case EMT2GuildResult::LevelTooLow: return NSLOCTEXT("MT2Guild", "LevelTooLowError", "You must be at least level 40 to create a guild.");
		case EMT2GuildResult::AlreadyInGuild: return NSLOCTEXT("MT2Guild", "AlreadyInGuildError", "This character already belongs to a guild.");
		case EMT2GuildResult::NotInGuild: return NSLOCTEXT("MT2Guild", "NotInGuildError", "This character does not belong to a guild.");
		case EMT2GuildResult::UnknownCharacter: return NSLOCTEXT("MT2Guild", "UnknownCharacterError", "The selected character could not be found.");
		case EMT2GuildResult::PermissionDenied: return NSLOCTEXT("MT2Guild", "PermissionDeniedError", "You do not have permission to perform this guild action.");
		case EMT2GuildResult::GuildFull: return NSLOCTEXT("MT2Guild", "GuildFullError", "The guild has reached its member limit.");
		case EMT2GuildResult::InvitePending: return NSLOCTEXT("MT2Guild", "InvitePendingError", "That character already has a pending guild invitation.");
		case EMT2GuildResult::InviteExpired: return NSLOCTEXT("MT2Guild", "InviteExpiredError", "The guild invitation has expired.");
		case EMT2GuildResult::CannotTargetSelf: return NSLOCTEXT("MT2Guild", "CannotTargetSelfError", "You cannot invite yourself.");
		case EMT2GuildResult::InvalidRank: return NSLOCTEXT("MT2Guild", "InvalidRankError", "The selected guild rank is invalid.");
		case EMT2GuildResult::LeaderCannotLeave: return NSLOCTEXT("MT2Guild", "LeaderCannotLeaveError", "The guild leader must transfer leadership or disband the guild.");
		case EMT2GuildResult::InsufficientYang: return NSLOCTEXT("MT2Guild", "InsufficientYangError", "You do not have enough Yang to create a guild.");
		case EMT2GuildResult::InvalidMark: return NSLOCTEXT("MT2Guild", "InvalidMarkError", "Choose a valid PNG, JPEG, BMP, or TGA image smaller than 4 MB.");
		case EMT2GuildResult::MarkUploadTooSoon: return NSLOCTEXT("MT2Guild", "MarkUploadTooSoonError", "Wait 30 seconds before changing the guild mark again.");
		default: return NSLOCTEXT("MT2Guild", "UnknownError", "The guild action failed.");
		}
	}
}

void UMT2GuildWidget::NativeConstruct()
{
	Super::NativeConstruct();
	CreateGuildButton->OnClicked.AddUniqueDynamic(this, &UMT2GuildWidget::HandleCreate);
	LeaveGuildButton->OnClicked.AddUniqueDynamic(this, &UMT2GuildWidget::HandleLeave);
	DisbandGuildButton->OnClicked.AddUniqueDynamic(this, &UMT2GuildWidget::HandleDisband);
	TitleBarWidget->OnCloseClicked.AddUniqueDynamic(this, &UMT2GuildWidget::HandleClose);
	InfoTabButton->OnClicked.AddUniqueDynamic(this, &UMT2GuildWidget::HandleInfoTab);
	BoardTabButton->OnClicked.AddUniqueDynamic(this, &UMT2GuildWidget::HandleBoardTab);
	MemberTabButton->OnClicked.AddUniqueDynamic(this, &UMT2GuildWidget::HandleMemberTab);
	RankTabButton->OnClicked.AddUniqueDynamic(this, &UMT2GuildWidget::HandleRankTab);
	SkillTabButton->OnClicked.AddUniqueDynamic(this, &UMT2GuildWidget::HandleSkillTab);
	BaseInfoTabButton->OnClicked.AddUniqueDynamic(this, &UMT2GuildWidget::HandleBaseInfoTab);
	UploadMarkButton->OnClicked.AddUniqueDynamic(this, &UMT2GuildWidget::HandleUploadMark);
	BindGuild();
	SelectPage(0);
	SetVisibility(ESlateVisibility::Collapsed);
}

void UMT2GuildWidget::NativeDestruct()
{
	if (UMT2GuildComponent* Guild = BoundGuild.Get())
	{
		Guild->OnGuildChanged.RemoveDynamic(this, &UMT2GuildWidget::HandleGuildChanged);
		Guild->OnGuildActionResult.RemoveDynamic(this, &UMT2GuildWidget::HandleActionResult);
	}
	Super::NativeDestruct();
}

UMT2GuildComponent* UMT2GuildWidget::ResolveGuild() const
{
	const AMT2PlayerState* State = GetOwningPlayerState<AMT2PlayerState>();
	return State ? State->GetGuildComponent() : nullptr;
}

void UMT2GuildWidget::BindGuild()
{
	UMT2GuildComponent* Guild = ResolveGuild();
	if (!Guild || BoundGuild.Get() == Guild) return;
	BoundGuild = Guild;
	Guild->OnGuildChanged.AddUniqueDynamic(this, &UMT2GuildWidget::HandleGuildChanged);
	Guild->OnGuildActionResult.AddUniqueDynamic(this, &UMT2GuildWidget::HandleActionResult);
	RefreshGuild();
}

void UMT2GuildWidget::ToggleWindow()
{
	const bool bOpen = GetVisibility() == ESlateVisibility::Collapsed;
	SetVisibility(bOpen ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	if (bOpen) { BindGuild(); RefreshGuild(); }
}

void UMT2GuildWidget::RefreshGuild()
{
	UMT2GuildComponent* Guild = ResolveGuild();
	const FMT2GuildSnapshot Snapshot = Guild ? Guild->GetSnapshot() : FMT2GuildSnapshot();
	NoGuildPanel->SetVisibility(Snapshot.IsValid() ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
	GuildPanel->SetVisibility(Snapshot.IsValid() ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	if (!Snapshot.IsValid()) return;
	GuildNameText->SetText(FText::FromString(Snapshot.Name));
	GuildLevelText->SetText(FText::Format(NSLOCTEXT("MT2Guild", "Level", "Level {0}"), Snapshot.Level));
	MemberCountText->SetText(FText::Format(
		NSLOCTEXT("MT2Guild", "MemberCount", "{0}/{1}"),
		Snapshot.Members.Num(), MT2Guild::MaximumMembers));
	const FMT2GuildMember* Leader = Snapshot.Members.FindByPredicate([&Snapshot](const FMT2GuildMember& Member)
	{
		return Member.CharacterId == Snapshot.LeaderCharacterId;
	});
	GuildMasterText->SetText(FText::FromString(Leader ? Leader->CharacterName : TEXT("-")));
	GuildExperienceText->SetText(FText::AsNumber(Snapshot.Experience));
	GuildYangText->SetText(FText::AsNumber(Snapshot.Yang));
	int32 TotalLevel = 0;
	for (const FMT2GuildMember& Member : Snapshot.Members) TotalLevel += Member.Level;
	AverageLevelText->SetText(FText::AsNumber(Snapshot.Members.IsEmpty() ? 0 : TotalLevel / Snapshot.Members.Num()));
	UTexture2D* MarkTexture = nullptr;
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (UMT2GuildMarkCacheSubsystem* Cache = GameInstance->GetSubsystem<UMT2GuildMarkCacheSubsystem>())
			MarkTexture = Snapshot.MarkRevision > 0 ? Cache->GetMark(Snapshot.GuildId, Snapshot.MarkRevision) : Cache->GetMark(Snapshot.GuildId);
	}
	GuildMarkImage->SetBrushFromTexture(MarkTexture);
	GuildMarkImage->SetVisibility(MarkTexture ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	UploadMarkButton->SetVisibility(Guild->IsLeader() ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	if (!MarkTexture && Snapshot.MarkRevision > 0) Guild->RequestGuildMark(Snapshot.GuildId, Snapshot.MarkRevision);
	LeaveGuildButton->SetVisibility(Guild->IsLeader() ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
	DisbandGuildButton->SetVisibility(Guild->IsLeader() ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	BuildMemberRows(Snapshot);
}

void UMT2GuildWidget::SelectPage(int32 PageIndex)
{
	PageIndex = FMath::Clamp(PageIndex, 0, 5);
	UPanelWidget* Pages[] = {InfoPage, BoardPage, MemberPage, RankPage, SkillPage, BaseInfoPage};
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Pages); ++Index)
	{
		Pages[Index]->SetVisibility(Index == PageIndex ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
	GuildTabImage->SetAtlasRegion(FMT2AtlasRect(0.0f, PageIndex * 37.0f, 376.0f, 37.0f));
}

void UMT2GuildWidget::BuildMemberRows(const FMT2GuildSnapshot& Snapshot)
{
	MemberList->ClearChildren();
	for (const FMT2GuildMember& Member : Snapshot.Members)
	{
		UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
		UTextBlock* Name = WidgetTree->ConstructWidget<UTextBlock>();
		FSlateFontInfo NameFont = Name->GetFont();
		NameFont.Size = 8;
		Name->SetFont(NameFont);
		Name->SetText(FText::FromString(FString::Printf(TEXT("%s%s"), Member.bOnline ? TEXT("[+] ") : TEXT("[-] "), *Member.CharacterName)));
		Name->SetColorAndOpacity(Member.bOnline ? FLinearColor(0.55f, 0.9f, 0.55f) : FLinearColor(0.55f, 0.55f, 0.58f));
		Row->AddChildToHorizontalBox(Name);
		UTextBlock* Detail = WidgetTree->ConstructWidget<UTextBlock>();
		FSlateFontInfo DetailFont = Detail->GetFont();
		DetailFont.Size = 8;
		Detail->SetFont(DetailFont);
		const FMT2GuildRank* Rank = Snapshot.Ranks.FindByPredicate(
			[&](const FMT2GuildRank& Entry) { return Entry.Rank == Member.Rank; });
		Detail->SetText(FText::FromString(FString::Printf(TEXT("  Lv %d  %s"), Member.Level, Rank ? *Rank->Name : TEXT("Member"))));
		Row->AddChildToHorizontalBox(Detail);
		MemberList->AddChild(Row);
	}
	RefreshWidgetSounds();
}

void UMT2GuildWidget::HandleGuildChanged() { RefreshGuild(); }
void UMT2GuildWidget::HandleCreate()
{
	if (UMT2GuildComponent* Guild = ResolveGuild()) Guild->CreateGuild(GuildNameInput->GetText().ToString());
}
void UMT2GuildWidget::HandleLeave() { if (UMT2GuildComponent* Guild = ResolveGuild()) Guild->LeaveGuild(); }
void UMT2GuildWidget::HandleDisband() { if (UMT2GuildComponent* Guild = ResolveGuild()) Guild->DisbandGuild(); }
void UMT2GuildWidget::HandleClose() { SetVisibility(ESlateVisibility::Collapsed); }
void UMT2GuildWidget::HandleInfoTab() { SelectPage(0); }
void UMT2GuildWidget::HandleBoardTab() { SelectPage(1); }
void UMT2GuildWidget::HandleMemberTab() { SelectPage(2); }
void UMT2GuildWidget::HandleRankTab() { SelectPage(3); }
void UMT2GuildWidget::HandleSkillTab() { SelectPage(4); }
void UMT2GuildWidget::HandleBaseInfoTab() { SelectPage(5); }
void UMT2GuildWidget::HandleUploadMark()
{
#if PLATFORM_WINDOWS
	wchar_t FileBuffer[4096] = {};
	OPENFILENAMEW Dialog = {};
	Dialog.lStructSize = sizeof(Dialog);
	Dialog.hwndOwner = nullptr;
	Dialog.lpstrFilter = L"Images\0*.png;*.jpg;*.jpeg;*.bmp;*.tga\0\0";
	Dialog.lpstrFile = FileBuffer;
	Dialog.nMaxFile = UE_ARRAY_COUNT(FileBuffer);
	Dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
	if (GetOpenFileNameW(&Dialog))
	{
		if (UMT2GuildComponent* Guild = ResolveGuild()) Guild->UploadGuildMarkFromFile(FileBuffer);
	}
#endif
}
void UMT2GuildWidget::HandleActionResult(FName Action, EMT2GuildResult Result)
{
	if (AMT2PlayerController* Controller = Cast<AMT2PlayerController>(GetOwningPlayer()))
	{
		Controller->AddInfoChatLine(GuildResultText(Action, Result).ToString());
	}
}
