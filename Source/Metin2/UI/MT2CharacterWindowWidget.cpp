/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "UI/MT2CharacterWindowWidget.h"

#include "Characters/MT2CharacterAppearanceSettings.h"
#include "Characters/MT2PlayerCharacter.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "UI/MT2QuestLogWidget.h"
#include "UI/MT2SkillPageWidget.h"
#include "Components/Image.h"
#include "Components/MT2HealthComponent.h"
#include "Components/MT2ManaComponent.h"
#include "Components/MT2MovementSpeedComponent.h"
#include "Components/TextBlock.h"
#include "Engine/Texture2D.h"
#include "Player/MT2PlayerState.h"
#include "Stats/MT2CombatStatsComponent.h"
#include "Stats/MT2PrimaryStatsComponent.h"
#include "UI/MT2AtlasImage.h"
#include "UI/MT2TitleBarWidget.h"

namespace
{
	void SetNumber(UTextBlock* Text, int64 Value)
	{
		if (Text)
		{
			Text->SetText(FText::AsNumber(Value));
		}
	}
}

void UMT2CharacterWindowWidget::NativeConstruct()
{
	Super::NativeConstruct();
	TitleBarWidget->InitializeTitleBar(185.0f, FText::FromString(TEXT("Character")));
	TitleBarWidget->OnCloseClicked.AddUniqueDynamic(this, &UMT2CharacterWindowWidget::HandleCloseClicked);
	StatsTabButton->OnClicked.AddUniqueDynamic(this, &UMT2CharacterWindowWidget::HandleStatsClicked);
	SkillsTabButton->OnClicked.AddUniqueDynamic(this, &UMT2CharacterWindowWidget::HandleSkillsClicked);
	EmotionsTabButton->OnClicked.AddUniqueDynamic(this, &UMT2CharacterWindowWidget::HandleEmotionsClicked);
	QuestsTabButton->OnClicked.AddUniqueDynamic(this, &UMT2CharacterWindowWidget::HandleQuestsClicked);
	ConstitutionPlusButton->OnClicked.AddUniqueDynamic(this, &UMT2CharacterWindowWidget::HandleConstitutionPlusClicked);
	IntelligencePlusButton->OnClicked.AddUniqueDynamic(this, &UMT2CharacterWindowWidget::HandleIntelligencePlusClicked);
	StrengthPlusButton->OnClicked.AddUniqueDynamic(this, &UMT2CharacterWindowWidget::HandleStrengthPlusClicked);
	DexterityPlusButton->OnClicked.AddUniqueDynamic(this, &UMT2CharacterWindowWidget::HandleDexterityPlusClicked);
	// The skill page content is fully native (old-client layout); the Blueprint page canvas only
	// hosts it. Existing designer children of SkillsPage stay underneath.
	if (SkillsPage && !SkillPageWidget)
	{
		SkillPageWidget = CreateWidget<UMT2SkillPageWidget>(GetOwningPlayer(), UMT2SkillPageWidget::StaticClass());
		if (UCanvasPanelSlot* PageSlot = SkillsPage->AddChildToCanvas(SkillPageWidget))
		{
			PageSlot->SetAnchors(FAnchors(0.0f, 0.0f, 1.0f, 1.0f));
			PageSlot->SetOffsets(FMargin(0.0f));
		}
	}

	// Same arrangement for the quest log: native content hosted by the Blueprint's page canvas.
	if (QuestsPage && !QuestLogWidget)
	{
		QuestLogWidget = CreateWidget<UMT2QuestLogWidget>(GetOwningPlayer(), UMT2QuestLogWidget::StaticClass());
		if (UCanvasPanelSlot* PageSlot = QuestsPage->AddChildToCanvas(QuestLogWidget))
		{
			PageSlot->SetAnchors(FAnchors(0.0f, 0.0f, 1.0f, 1.0f));
			PageSlot->SetOffsets(FMargin(0.0f));
		}
	}

	BindPlayerState();
	BindPlayerCharacter();
	SetActivePage(ActivePage);
	SetVisibility(ESlateVisibility::Collapsed);
}

void UMT2CharacterWindowWidget::NativeDestruct()
{
	UnbindPlayerCharacter();
	UnbindPlayerState();
	Super::NativeDestruct();
}

void UMT2CharacterWindowWidget::ToggleWindow(EMT2CharacterWindowPage Page)
{
	SetActivePage(Page);
	const bool bShow = GetVisibility() != ESlateVisibility::Visible;
	SetVisibility(bShow ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	if (bShow)
	{
		BindPlayerState();
		BindPlayerCharacter();
		RefreshStats();
	}
}

void UMT2CharacterWindowWidget::SetActivePage(EMT2CharacterWindowPage Page)
{
	ActivePage = Page;
	StatsPage->SetVisibility(Page == EMT2CharacterWindowPage::Stats ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	SkillsPage->SetVisibility(Page == EMT2CharacterWindowPage::Skills ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	EmotionsPage->SetVisibility(Page == EMT2CharacterWindowPage::Emotions ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	QuestsPage->SetVisibility(Page == EMT2CharacterWindowPage::Quests ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	StatsTabButton->SetIsEnabled(Page != EMT2CharacterWindowPage::Stats);
	SkillsTabButton->SetIsEnabled(Page != EMT2CharacterWindowPage::Skills);
	EmotionsTabButton->SetIsEnabled(Page != EMT2CharacterWindowPage::Emotions);
	QuestsTabButton->SetIsEnabled(Page != EMT2CharacterWindowPage::Quests);
	const TCHAR* Title = Page == EMT2CharacterWindowPage::Stats ? TEXT("Character")
		: Page == EMT2CharacterWindowPage::Skills ? TEXT("Skills")
		: Page == EMT2CharacterWindowPage::Emotions ? TEXT("Emotions") : TEXT("Quests");
	TitleBarWidget->InitializeTitleBar(185.0f, FText::FromString(Title));
	RefreshStats();
}

void UMT2CharacterWindowWidget::RefreshStats()
{
	BindPlayerState();
	BindPlayerCharacter();
	const AMT2PlayerState* State = BoundPlayerState.Get();
	if (!State)
	{
		return;
	}

	CharacterNameText->SetText(FText::FromString(State->GetCharacterName()));
	GuildNameText->SetText(FText::FromString(State->GetGuildName().IsEmpty() ? TEXT("-") : State->GetGuildName()));
	SetNumber(LevelValueText, State->GetCharacterLevel());
	SetNumber(ExperienceValueText, State->GetExperience());
	SetNumber(RequiredExperienceValueText, State->GetRequiredExperienceForNextLevel());
	SetNumber(StatPointsValueText, State->GetUnspentStatPoints());
	SetNumber(SkillPointsValueText, State->GetUnspentSkillPoints());
	const FMT2CharacterAppearanceAsset* AppearanceAsset =
		GetDefault<UMT2CharacterAppearanceSettings>()->FindAppearance(
			State->GetCharacterAppearance());
	if (UTexture2D* FaceTexture = AppearanceAsset
		? AppearanceAsset->FaceTexture.LoadSynchronous() : nullptr)
	{
		FaceImage->SetBrushFromTexture(FaceTexture, true);
	}
	else
	{
		// Do not leave the Blueprint's default warrior portrait visible when an appearance asset is
		// missing from a cook or otherwise fails to load.
		FaceImage->SetBrushFromTexture(nullptr);
	}

	FMT2PrimaryStats PrimaryStats;
	FMT2PrimaryStats BasePrimaryStats;
	if (const UMT2PrimaryStatsComponent* StateStats = State->GetPrimaryStatsComponent())
	{
		PrimaryStats = StateStats->GetCalculatedStats();
		BasePrimaryStats = StateStats->GetBaseStats();
	}
	const AMT2PlayerCharacter* Character = BoundPlayerCharacter.Get();
	if (Character && Character->GetPrimaryStatsComponent())
	{
		const FMT2PrimaryStats PawnBonuses = Character->GetPrimaryStatsComponent()->GetCalculatedStats();
		PrimaryStats.Strength += PawnBonuses.Strength;
		PrimaryStats.Dexterity += PawnBonuses.Dexterity;
		PrimaryStats.Constitution += PawnBonuses.Constitution;
		PrimaryStats.Intelligence += PawnBonuses.Intelligence;
	}
	SetNumber(ConstitutionValueText, PrimaryStats.Constitution);
	SetNumber(IntelligenceValueText, PrimaryStats.Intelligence);
	SetNumber(StrengthValueText, PrimaryStats.Strength);
	SetNumber(DexterityValueText, PrimaryStats.Dexterity);

	const bool bCanSpend = State->GetUnspentStatPoints() > 0;
	ConstitutionPlusButton->SetIsEnabled(bCanSpend && BasePrimaryStats.Constitution < 90);
	IntelligencePlusButton->SetIsEnabled(bCanSpend && BasePrimaryStats.Intelligence < 90);
	StrengthPlusButton->SetIsEnabled(bCanSpend && BasePrimaryStats.Strength < 90);
	DexterityPlusButton->SetIsEnabled(bCanSpend && BasePrimaryStats.Dexterity < 90);

	if (!Character)
	{
		return;
	}
	const UMT2HealthComponent* Health = Character->GetHealthComponent();
	const UMT2ManaComponent* Mana = Character->GetManaComponent();
	HealthValueText->SetText(FText::FromString(FString::Printf(TEXT("%d/%d"),
		FMath::RoundToInt(Health ? Health->GetHealth() : 0.0f), FMath::RoundToInt(Health ? Health->GetMaxHealth() : 0.0f))));
	ManaValueText->SetText(FText::FromString(FString::Printf(TEXT("%d/%d"),
		FMath::RoundToInt(Mana ? Mana->GetMana() : 0.0f), FMath::RoundToInt(Mana ? Mana->GetMaxMana() : 0.0f))));

	const FMT2CombatStats CombatStats = Character->GetCombatStatsComponent()
		? Character->GetCombatStatsComponent()->GetCalculatedStats() : FMT2CombatStats();
	AttackValueText->SetText(FText::FromString(FString::Printf(TEXT("%d-%d"),
		FMath::RoundToInt(CombatStats.DamageMin), FMath::RoundToInt(CombatStats.DamageMax))));
	SetNumber(DefenseValueText, FMath::RoundToInt(CombatStats.Defense));
	SetNumber(AttackSpeedValueText, CombatStats.AttackSpeed);
	SetNumber(MovementSpeedValueText, FMath::RoundToInt(Character->GetMovementSpeedComponent()
		? Character->GetMovementSpeedComponent()->GetMovementSpeed() : CombatStats.MovementSpeed));
	SetNumber(MagicAttackValueText, FMath::RoundToInt(CombatStats.MagicAttack));
	SetNumber(MagicDefenseValueText, FMath::RoundToInt(CombatStats.MagicDefense));
	FNumberFormattingOptions PercentFormat;
	PercentFormat.MaximumFractionalDigits = 1;
	EvasionValueText->SetText(FText::AsPercent(CombatStats.Evasion / 100.0f, &PercentFormat));
}

void UMT2CharacterWindowWidget::BindPlayerState()
{
	AMT2PlayerState* State = GetOwningPlayerState<AMT2PlayerState>();
	if (BoundPlayerState.Get() == State)
	{
		return;
	}
	UnbindPlayerState();
	BoundPlayerState = State;
	if (!State)
	{
		return;
	}
	State->OnExperienceChanged.AddUniqueDynamic(this, &UMT2CharacterWindowWidget::HandleExperienceChanged);
	State->OnLevelChanged.AddUniqueDynamic(this, &UMT2CharacterWindowWidget::HandleLevelChanged);
	State->OnStatPointsChanged.AddUniqueDynamic(this, &UMT2CharacterWindowWidget::HandleProgressionPointsChanged);
	State->OnSkillPointsChanged.AddUniqueDynamic(this, &UMT2CharacterWindowWidget::HandleProgressionPointsChanged);
	State->OnCharacterNameChanged.AddUniqueDynamic(this, &UMT2CharacterWindowWidget::HandleCharacterNameChanged);
	State->OnCharacterAppearanceChanged.AddUniqueDynamic(this, &UMT2CharacterWindowWidget::HandleAppearanceChanged);
	State->OnGuildChanged.AddUniqueDynamic(this, &UMT2CharacterWindowWidget::HandleGuildChanged);
	if (State->GetPrimaryStatsComponent())
	{
		State->GetPrimaryStatsComponent()->OnPrimaryStatsChanged.AddUniqueDynamic(
			this, &UMT2CharacterWindowWidget::HandlePrimaryStatsChanged);
	}
}

void UMT2CharacterWindowWidget::UnbindPlayerState()
{
	if (AMT2PlayerState* State = BoundPlayerState.Get())
	{
		State->OnExperienceChanged.RemoveDynamic(this, &UMT2CharacterWindowWidget::HandleExperienceChanged);
		State->OnLevelChanged.RemoveDynamic(this, &UMT2CharacterWindowWidget::HandleLevelChanged);
		State->OnStatPointsChanged.RemoveDynamic(this, &UMT2CharacterWindowWidget::HandleProgressionPointsChanged);
		State->OnSkillPointsChanged.RemoveDynamic(this, &UMT2CharacterWindowWidget::HandleProgressionPointsChanged);
		State->OnCharacterNameChanged.RemoveDynamic(this, &UMT2CharacterWindowWidget::HandleCharacterNameChanged);
		State->OnCharacterAppearanceChanged.RemoveDynamic(this, &UMT2CharacterWindowWidget::HandleAppearanceChanged);
		State->OnGuildChanged.RemoveDynamic(this, &UMT2CharacterWindowWidget::HandleGuildChanged);
		if (State->GetPrimaryStatsComponent())
		{
			State->GetPrimaryStatsComponent()->OnPrimaryStatsChanged.RemoveDynamic(
				this, &UMT2CharacterWindowWidget::HandlePrimaryStatsChanged);
		}
	}
	BoundPlayerState.Reset();
}

void UMT2CharacterWindowWidget::BindPlayerCharacter()
{
	AMT2PlayerCharacter* Character = Cast<AMT2PlayerCharacter>(GetOwningPlayerPawn());
	if (BoundPlayerCharacter.Get() == Character)
	{
		return;
	}
	UnbindPlayerCharacter();
	BoundPlayerCharacter = Character;
	if (!Character)
	{
		return;
	}
	Character->GetPrimaryStatsComponent()->OnPrimaryStatsChanged.AddUniqueDynamic(this, &UMT2CharacterWindowWidget::HandlePrimaryStatsChanged);
	Character->GetCombatStatsComponent()->OnCombatStatsChanged.AddUniqueDynamic(this, &UMT2CharacterWindowWidget::HandleCombatStatsChanged);
	Character->GetHealthComponent()->OnValueChanged.AddUniqueDynamic(this, &UMT2CharacterWindowWidget::HandleResourceChanged);
	Character->GetHealthComponent()->OnMaxValueChanged.AddUniqueDynamic(this, &UMT2CharacterWindowWidget::HandleResourceChanged);
	Character->GetManaComponent()->OnValueChanged.AddUniqueDynamic(this, &UMT2CharacterWindowWidget::HandleResourceChanged);
	Character->GetManaComponent()->OnMaxValueChanged.AddUniqueDynamic(this, &UMT2CharacterWindowWidget::HandleResourceChanged);
	Character->GetMovementSpeedComponent()->OnMovementSpeedChanged.AddUniqueDynamic(this, &UMT2CharacterWindowWidget::HandleMovementSpeedChanged);
}

void UMT2CharacterWindowWidget::UnbindPlayerCharacter()
{
	if (AMT2PlayerCharacter* Character = BoundPlayerCharacter.Get())
	{
		Character->GetPrimaryStatsComponent()->OnPrimaryStatsChanged.RemoveDynamic(this, &UMT2CharacterWindowWidget::HandlePrimaryStatsChanged);
		Character->GetCombatStatsComponent()->OnCombatStatsChanged.RemoveDynamic(this, &UMT2CharacterWindowWidget::HandleCombatStatsChanged);
		Character->GetHealthComponent()->OnValueChanged.RemoveDynamic(this, &UMT2CharacterWindowWidget::HandleResourceChanged);
		Character->GetHealthComponent()->OnMaxValueChanged.RemoveDynamic(this, &UMT2CharacterWindowWidget::HandleResourceChanged);
		Character->GetManaComponent()->OnValueChanged.RemoveDynamic(this, &UMT2CharacterWindowWidget::HandleResourceChanged);
		Character->GetManaComponent()->OnMaxValueChanged.RemoveDynamic(this, &UMT2CharacterWindowWidget::HandleResourceChanged);
		Character->GetMovementSpeedComponent()->OnMovementSpeedChanged.RemoveDynamic(this, &UMT2CharacterWindowWidget::HandleMovementSpeedChanged);
	}
	BoundPlayerCharacter.Reset();
}

void UMT2CharacterWindowWidget::SpendStatPoint(EMT2PrimaryStat Stat)
{
	if (AMT2PlayerState* State = BoundPlayerState.Get())
	{
		State->ServerSpendStatPoint(Stat);
	}
}

void UMT2CharacterWindowWidget::HandleExperienceChanged(int64, int64) { RefreshStats(); }
void UMT2CharacterWindowWidget::HandleLevelChanged(int32, int32) { RefreshStats(); }
void UMT2CharacterWindowWidget::HandleProgressionPointsChanged(int32, int32) { RefreshStats(); }
void UMT2CharacterWindowWidget::HandleCharacterNameChanged(const FString&) { RefreshStats(); }
void UMT2CharacterWindowWidget::HandleAppearanceChanged(const FMT2CharacterAppearance&) { RefreshStats(); }
void UMT2CharacterWindowWidget::HandleGuildChanged(int32, const FString&) { RefreshStats(); }
void UMT2CharacterWindowWidget::HandlePrimaryStatsChanged(FMT2PrimaryStats, FMT2PrimaryStats) { RefreshStats(); }
void UMT2CharacterWindowWidget::HandleCombatStatsChanged(FMT2CombatStats, FMT2CombatStats) { RefreshStats(); }
void UMT2CharacterWindowWidget::HandleResourceChanged(float, float) { RefreshStats(); }
void UMT2CharacterWindowWidget::HandleMovementSpeedChanged(float, float) { RefreshStats(); }

void UMT2CharacterWindowWidget::HandleCloseClicked() { SetVisibility(ESlateVisibility::Collapsed); }
void UMT2CharacterWindowWidget::HandleStatsClicked() { SetActivePage(EMT2CharacterWindowPage::Stats); }
void UMT2CharacterWindowWidget::HandleSkillsClicked() { SetActivePage(EMT2CharacterWindowPage::Skills); }
void UMT2CharacterWindowWidget::HandleEmotionsClicked() { SetActivePage(EMT2CharacterWindowPage::Emotions); }
void UMT2CharacterWindowWidget::HandleQuestsClicked() { SetActivePage(EMT2CharacterWindowPage::Quests); }
void UMT2CharacterWindowWidget::HandleConstitutionPlusClicked() { SpendStatPoint(EMT2PrimaryStat::Constitution); }
void UMT2CharacterWindowWidget::HandleIntelligencePlusClicked() { SpendStatPoint(EMT2PrimaryStat::Intelligence); }
void UMT2CharacterWindowWidget::HandleStrengthPlusClicked() { SpendStatPoint(EMT2PrimaryStat::Strength); }
void UMT2CharacterWindowWidget::HandleDexterityPlusClicked() { SpendStatPoint(EMT2PrimaryStat::Dexterity); }
