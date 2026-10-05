/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "UI/MT2DraggableWindowWidget.h"
#include "Guild/MT2GuildTypes.h"
#include "MT2GuildWidget.generated.h"

class UButton;
class UEditableTextBox;
class UPanelWidget;
class UTextBlock;
class UMT2TitleBarWidget;
class UMT2AtlasImage;

UCLASS()
class METIN2_API UMT2GuildWidget : public UMT2DraggableWindowWidget
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Guild") void RefreshGuild();
	void ToggleWindow();

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

private:
	class UMT2GuildComponent* ResolveGuild() const;
	void BindGuild();
	void BuildMemberRows(const FMT2GuildSnapshot& Snapshot);
	void SelectPage(int32 PageIndex);
	UFUNCTION() void HandleGuildChanged();
	UFUNCTION() void HandleCreate();
	UFUNCTION() void HandleLeave();
	UFUNCTION() void HandleDisband();
	UFUNCTION() void HandleClose();
	UFUNCTION() void HandleInfoTab();
	UFUNCTION() void HandleBoardTab();
	UFUNCTION() void HandleMemberTab();
	UFUNCTION() void HandleRankTab();
	UFUNCTION() void HandleSkillTab();
	UFUNCTION() void HandleBaseInfoTab();
	UFUNCTION() void HandleUploadMark();
	UFUNCTION() void HandleActionResult(FName Action, EMT2GuildResult Result);

	UPROPERTY(meta = (BindWidget)) TObjectPtr<UMT2TitleBarWidget> TitleBarWidget;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UPanelWidget> NoGuildPanel;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UEditableTextBox> GuildNameInput;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> CreateGuildButton;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UPanelWidget> GuildPanel;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> GuildNameText;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> GuildLevelText;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> MemberCountText;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> GuildMasterText;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> GuildExperienceText;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> GuildYangText;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> AverageLevelText;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<class UImage> GuildMarkImage;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> UploadMarkButton;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UPanelWidget> MemberList;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> LeaveGuildButton;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> DisbandGuildButton;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> InfoTabButton;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> BoardTabButton;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> MemberTabButton;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> RankTabButton;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> SkillTabButton;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> BaseInfoTabButton;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UMT2AtlasImage> GuildTabImage;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UPanelWidget> InfoPage;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UPanelWidget> BoardPage;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UPanelWidget> MemberPage;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UPanelWidget> RankPage;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UPanelWidget> SkillPage;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UPanelWidget> BaseInfoPage;
	UPROPERTY(Transient) TWeakObjectPtr<UMT2GuildComponent> BoundGuild;
};
