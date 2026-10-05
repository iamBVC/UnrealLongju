/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "UI/MT2TaskbarWidget.h"

#include "Characters/MT2CharacterBase.h"
#include "Components/Button.h"
#include "Components/MT2HealthComponent.h"
#include "Components/MT2ManaComponent.h"
#include "Components/MT2StaminaComponent.h"
#include "Components/MT2StatusEffectComponent.h"
#include "Components/MT2StatusEffectDefinition.h"
#include "Player/MT2PlayerState.h"
#include "UI/MT2ExperienceGaugeWidget.h"
#include "UI/MT2QuickSlotBarWidget.h"
#include "UI/MT2ResourceGaugeWidget.h"

void UMT2TaskbarWidget::NativeConstruct()
{
	Super::NativeConstruct();
	QuickSlotBarWidget->OnSlotActivated.AddUniqueDynamic(this, &UMT2TaskbarWidget::HandleQuickSlotActivated);
	QuickSlotBarWidget->OnChatClicked.AddUniqueDynamic(this, &UMT2TaskbarWidget::HandleChatClicked);
	MoneyButton->OnClicked.AddUniqueDynamic(this, &UMT2TaskbarWidget::HandleMoneyClicked);
	CharacterButton->OnClicked.AddUniqueDynamic(this, &UMT2TaskbarWidget::HandleCharacterClicked);
	InventoryButton->OnClicked.AddUniqueDynamic(this, &UMT2TaskbarWidget::HandleInventoryClicked);
	MessengerButton->OnClicked.AddUniqueDynamic(this, &UMT2TaskbarWidget::HandleMessengerClicked);
	SystemButton->OnClicked.AddUniqueDynamic(this, &UMT2TaskbarWidget::HandleSystemClicked);
	BindPlayerState();
	RefreshResourceBars();
	RefreshExperienceBar();
}

void UMT2TaskbarWidget::NativeDestruct()
{
	UnbindPlayerState();
	Super::NativeDestruct();
}

void UMT2TaskbarWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	BindPlayerState();
	RefreshResourceBars();
}

void UMT2TaskbarWidget::ActivateQuickSlot(int32 SlotIndex)
{
	QuickSlotBarWidget->ActivateSlot(SlotIndex);
}

void UMT2TaskbarWidget::RefreshResourceBars()
{
	const AMT2CharacterBase* Character = GetOwningPlayerPawn<AMT2CharacterBase>();
	const UMT2HealthComponent* Health = Character ? Character->GetHealthComponent() : nullptr;
	const UMT2ManaComponent* Mana = Character ? Character->GetManaComponent() : nullptr;
	const UMT2StaminaComponent* Stamina = Character ? Character->GetStaminaComponent() : nullptr;
	// Pending potion recovery still to be applied, so the gauge can draw the translucent target bar.
	const UMT2StatusEffectComponent* StatusEffects = Character ? Character->GetStatusEffectComponent() : nullptr;
	const float HealthPool = StatusEffects ? StatusEffects->GetEffectValueByType(MT2AffectId::HealthRecovery) : 0.0f;
	const float ManaPool = StatusEffects ? StatusEffects->GetEffectValueByType(MT2AffectId::ManaRecovery) : 0.0f;
	ResourceGaugeWidget->SetResourceValues(
		Health ? Health->GetHealth() : 0.0f,
		Health ? Health->GetMaxHealth() : 0.0f,
		Mana ? Mana->GetMana() : 0.0f,
		Mana ? Mana->GetMaxMana() : 0.0f,
		Stamina ? Stamina->GetStamina() : 0.0f,
		Stamina ? Stamina->GetMaxStamina() : 0.0f,
		HealthPool, ManaPool);
}

void UMT2TaskbarWidget::BindPlayerState()
{
	AMT2PlayerState* State = GetOwningPlayerState<AMT2PlayerState>();
	if (BoundPlayerState.Get() == State)
	{
		return;
	}
	UnbindPlayerState();
	BoundPlayerState = State;
	if (State)
	{
		State->OnExperienceChanged.AddUniqueDynamic(this, &UMT2TaskbarWidget::HandleExperienceChanged);
		State->OnLevelChanged.AddUniqueDynamic(this, &UMT2TaskbarWidget::HandleLevelChanged);
		RefreshExperienceBar();
	}
}

void UMT2TaskbarWidget::UnbindPlayerState()
{
	if (AMT2PlayerState* State = BoundPlayerState.Get())
	{
		State->OnExperienceChanged.RemoveDynamic(this, &UMT2TaskbarWidget::HandleExperienceChanged);
		State->OnLevelChanged.RemoveDynamic(this, &UMT2TaskbarWidget::HandleLevelChanged);
	}
	BoundPlayerState.Reset();
}

void UMT2TaskbarWidget::RefreshExperienceBar()
{
	const AMT2PlayerState* State = BoundPlayerState.Get();
	ExperienceGaugeWidget->SetExperience(
		State ? State->GetExperience() : 0,
		State ? State->GetRequiredExperienceForNextLevel() : 0);
}

void UMT2TaskbarWidget::HandleExperienceChanged(int64 OldExperience, int64 NewExperience) { RefreshExperienceBar(); }
void UMT2TaskbarWidget::HandleLevelChanged(int32 OldLevel, int32 NewLevel) { RefreshExperienceBar(); }

void UMT2TaskbarWidget::HandleInventoryClicked() { OnInventoryClicked.Broadcast(); }
void UMT2TaskbarWidget::HandleCharacterClicked() { OnCharacterClicked.Broadcast(); }
void UMT2TaskbarWidget::HandleMessengerClicked() { OnMessengerClicked.Broadcast(); }
void UMT2TaskbarWidget::HandleSystemClicked() { OnSystemClicked.Broadcast(); }
void UMT2TaskbarWidget::HandleChatClicked() { OnChatClicked.Broadcast(); }
void UMT2TaskbarWidget::HandleMoneyClicked() { OnMoneyClicked.Broadcast(); }
void UMT2TaskbarWidget::HandleQuickSlotActivated(int32 SlotIndex) { OnQuickSlotActivated.Broadcast(SlotIndex); }
