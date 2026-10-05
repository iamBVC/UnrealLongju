/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "Abilities/MT2CoreAttributeSet.h"

#include "Abilities/MT2GameplayTags.h"
#include "GameplayEffectExtension.h"
#include "Net/UnrealNetwork.h"

UMT2CoreAttributeSet::UMT2CoreAttributeSet()
{
	InitMaxHealth(100.0f);
	InitHealth(100.0f);
	InitMaxMana(100.0f);
	InitMana(100.0f);
	InitMaxStamina(100.0f);
	InitStamina(100.0f);
	InitMovementSpeed(100.0f);
	InitDamage(0.0f);
}

void UMT2CoreAttributeSet::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME_CONDITION_NOTIFY(UMT2CoreAttributeSet, Health, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UMT2CoreAttributeSet, MaxHealth, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UMT2CoreAttributeSet, Mana, COND_OwnerOnly, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UMT2CoreAttributeSet, MaxMana, COND_OwnerOnly, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UMT2CoreAttributeSet, Stamina, COND_OwnerOnly, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UMT2CoreAttributeSet, MaxStamina, COND_OwnerOnly, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UMT2CoreAttributeSet, MovementSpeed, COND_None, REPNOTIFY_Always);
}

void UMT2CoreAttributeSet::PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue)
{
	Super::PreAttributeChange(Attribute, NewValue);

	if (Attribute == GetMaxHealthAttribute())
	{
		NewValue = FMath::Max(NewValue, 1.0f);
	}
	else if (Attribute == GetMaxManaAttribute() || Attribute == GetMaxStaminaAttribute())
	{
		NewValue = FMath::Max(NewValue, 0.0f);
	}
	else if (Attribute == GetHealthAttribute())
	{
		NewValue = FMath::Clamp(NewValue, 0.0f, GetMaxHealth());
	}
	else if (Attribute == GetManaAttribute())
	{
		NewValue = FMath::Clamp(NewValue, 0.0f, GetMaxMana());
	}
	else if (Attribute == GetStaminaAttribute())
	{
		NewValue = FMath::Clamp(NewValue, 0.0f, GetMaxStamina());
	}
	else if (Attribute == GetMovementSpeedAttribute())
	{
		NewValue = FMath::Max(NewValue, 0.0f);
	}
}

void UMT2CoreAttributeSet::PostGameplayEffectExecute(const FGameplayEffectModCallbackData& Data)
{
	Super::PostGameplayEffectExecute(Data);

	const FGameplayAttribute& ModifiedAttribute = Data.EvaluatedData.Attribute;
	if (ModifiedAttribute == GetDamageAttribute())
	{
		const float AppliedDamage = FMath::Max(GetDamage(), 0.0f);
		SetDamage(0.0f);
		if (AppliedDamage > 0.0f)
		{
			SetHealth(FMath::Clamp(GetHealth() - AppliedDamage, 0.0f, GetMaxHealth()));
		}
	}
	else if (ModifiedAttribute == GetHealthAttribute() || ModifiedAttribute == GetMaxHealthAttribute())
	{
		SetMaxHealth(FMath::Max(GetMaxHealth(), 1.0f));
		SetHealth(FMath::Clamp(GetHealth(), 0.0f, GetMaxHealth()));
	}
	else if (ModifiedAttribute == GetManaAttribute() || ModifiedAttribute == GetMaxManaAttribute())
	{
		SetMaxMana(FMath::Max(GetMaxMana(), 0.0f));
		SetMana(FMath::Clamp(GetMana(), 0.0f, GetMaxMana()));
	}
	else if (ModifiedAttribute == GetStaminaAttribute() || ModifiedAttribute == GetMaxStaminaAttribute())
	{
		SetMaxStamina(FMath::Max(GetMaxStamina(), 0.0f));
		SetStamina(FMath::Clamp(GetStamina(), 0.0f, GetMaxStamina()));
	}
	else if (ModifiedAttribute == GetMovementSpeedAttribute())
	{
		SetMovementSpeed(FMath::Max(GetMovementSpeed(), 0.0f));
	}

	if (ModifiedAttribute == GetDamageAttribute() || ModifiedAttribute == GetHealthAttribute() ||
		ModifiedAttribute == GetMaxHealthAttribute())
	{
		if (UAbilitySystemComponent* AbilitySystem = GetOwningAbilitySystemComponent())
		{
			AbilitySystem->SetLooseGameplayTagCount(
				MT2GameplayTags::Status_Dead,
				GetHealth() <= 0.0f ? 1 : 0,
				EGameplayTagReplicationState::TagOnly);
		}
	}
}

void UMT2CoreAttributeSet::OnRep_Health(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UMT2CoreAttributeSet, Health, OldValue);
}

void UMT2CoreAttributeSet::OnRep_MaxHealth(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UMT2CoreAttributeSet, MaxHealth, OldValue);
}

void UMT2CoreAttributeSet::OnRep_Mana(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UMT2CoreAttributeSet, Mana, OldValue);
}

void UMT2CoreAttributeSet::OnRep_MaxMana(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UMT2CoreAttributeSet, MaxMana, OldValue);
}

void UMT2CoreAttributeSet::OnRep_Stamina(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UMT2CoreAttributeSet, Stamina, OldValue);
}

void UMT2CoreAttributeSet::OnRep_MaxStamina(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UMT2CoreAttributeSet, MaxStamina, OldValue);
}

void UMT2CoreAttributeSet::OnRep_MovementSpeed(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UMT2CoreAttributeSet, MovementSpeed, OldValue);
}
