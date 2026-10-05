/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "CoreMinimal.h"
#include "Player/MT2PlayerTypes.h"
#include "Stats/MT2StatTypes.h"
#include "UI/MT2DraggableWindowWidget.h"
#include "MT2CharacterWindowWidget.generated.h"

class UButton;
class UCanvasPanel;
class UImage;
class UTextBlock;
class UMT2AtlasImage;
class UMT2BoardWidget;
class UMT2SkillPageWidget;
class UMT2QuestLogWidget;
class UMT2TitleBarWidget;
class AMT2PlayerCharacter;
class AMT2PlayerState;

UENUM(BlueprintType)
enum class EMT2CharacterWindowPage : uint8
{
	Stats,
	Skills,
	Emotions,
	Quests
};

UCLASS()
class METIN2_API UMT2CharacterWindowWidget : public UMT2DraggableWindowWidget
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Character Window") void ToggleWindow(EMT2CharacterWindowPage Page);
	UFUNCTION(BlueprintCallable, Category = "Character Window") void SetActivePage(EMT2CharacterWindowPage Page);

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

private:
	UFUNCTION() void HandleCloseClicked();
	UFUNCTION() void HandleStatsClicked();
	UFUNCTION() void HandleSkillsClicked();
	UFUNCTION() void HandleEmotionsClicked();
	UFUNCTION() void HandleQuestsClicked();
	UFUNCTION() void HandleConstitutionPlusClicked();
	UFUNCTION() void HandleIntelligencePlusClicked();
	UFUNCTION() void HandleStrengthPlusClicked();
	UFUNCTION() void HandleDexterityPlusClicked();
	UFUNCTION() void HandleExperienceChanged(int64 OldExperience, int64 NewExperience);
	UFUNCTION() void HandleLevelChanged(int32 OldLevel, int32 NewLevel);
	UFUNCTION() void HandleProgressionPointsChanged(int32 OldPoints, int32 NewPoints);
	UFUNCTION() void HandleCharacterNameChanged(const FString& CharacterName);
	UFUNCTION() void HandleAppearanceChanged(const FMT2CharacterAppearance& Appearance);
	UFUNCTION() void HandleGuildChanged(int32 GuildId, const FString& GuildName);
	UFUNCTION() void HandlePrimaryStatsChanged(FMT2PrimaryStats OldStats, FMT2PrimaryStats NewStats);
	UFUNCTION() void HandleCombatStatsChanged(FMT2CombatStats OldStats, FMT2CombatStats NewStats);
	UFUNCTION() void HandleResourceChanged(float OldValue, float NewValue);
	UFUNCTION() void HandleMovementSpeedChanged(float OldSpeed, float NewSpeed);
	void BindPlayerState();
	void UnbindPlayerState();
	void BindPlayerCharacter();
	void UnbindPlayerCharacter();
	void RefreshStats();
	void SpendStatPoint(EMT2PrimaryStat Stat);
	EMT2CharacterWindowPage ActivePage = EMT2CharacterWindowPage::Stats;
	TWeakObjectPtr<AMT2PlayerState> BoundPlayerState;
	TWeakObjectPtr<AMT2PlayerCharacter> BoundPlayerCharacter;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UMT2BoardWidget> BoardWidget;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UMT2TitleBarWidget> TitleBarWidget;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UMT2AtlasImage> TabBackground;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> StatsTabButton;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> SkillsTabButton;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> EmotionsTabButton;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> QuestsTabButton;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UCanvasPanel> StatsPage;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UCanvasPanel> SkillsPage;
	UPROPERTY(Transient) TObjectPtr<UMT2SkillPageWidget> SkillPageWidget;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UCanvasPanel> EmotionsPage;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UCanvasPanel> QuestsPage;
	UPROPERTY(Transient) TObjectPtr<UMT2QuestLogWidget> QuestLogWidget;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UImage> FaceImage;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> GuildNameText;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> CharacterNameText;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> LevelValueText;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> ExperienceValueText;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> RequiredExperienceValueText;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> StatPointsValueText;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> ConstitutionValueText;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> IntelligenceValueText;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> StrengthValueText;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> DexterityValueText;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> HealthValueText;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> ManaValueText;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> AttackValueText;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> DefenseValueText;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> MovementSpeedValueText;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> AttackSpeedValueText;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> MagicAttackValueText;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> MagicDefenseValueText;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> EvasionValueText;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> SkillPointsValueText;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> ConstitutionPlusButton;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> IntelligencePlusButton;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> StrengthPlusButton;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> DexterityPlusButton;
};
