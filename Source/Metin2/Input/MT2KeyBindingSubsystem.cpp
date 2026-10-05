/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "Input/MT2KeyBindingSubsystem.h"

#include "Misc/ConfigCacheIni.h"

namespace
{
	const TCHAR* KeyBindingsConfigSection = TEXT("MT2KeyBindings");
}

const FName UMT2KeyBindingSubsystem::ToggleCharacter(TEXT("ToggleCharacter"));
const FName UMT2KeyBindingSubsystem::ToggleSkills(TEXT("ToggleSkills"));
const FName UMT2KeyBindingSubsystem::ToggleBelt(TEXT("ToggleBelt"));
const FName UMT2KeyBindingSubsystem::ToggleMessenger(TEXT("ToggleMessenger"));
const FName UMT2KeyBindingSubsystem::ToggleMap(TEXT("ToggleMap"));
const FName UMT2KeyBindingSubsystem::ToggleInventory(TEXT("ToggleInventory"));
const FName UMT2KeyBindingSubsystem::PickupItems(TEXT("PickupItems"));
const FName UMT2KeyBindingSubsystem::ToggleFirstPerson(TEXT("ToggleFirstPerson"));
const FName UMT2KeyBindingSubsystem::VoiceTalk(TEXT("VoiceTalk"));
const FName UMT2KeyBindingSubsystem::ToggleChat(TEXT("ToggleChat"));
const FName UMT2KeyBindingSubsystem::SystemMenu(TEXT("SystemMenu"));

FName UMT2KeyBindingSubsystem::QuickSlot(int32 Index)
{
	return FName(*FString::Printf(TEXT("QuickSlot%d"), Index + 1));
}

void UMT2KeyBindingSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	auto Add = [this](FName Id, const TCHAR* DisplayName, const FKey& DefaultKey)
	{
		FMT2KeyBindingAction Action;
		Action.Id = Id;
		Action.DisplayName = DisplayName;
		Action.DefaultKey = DefaultKey;
		Action.CurrentKey = DefaultKey;
		if (GConfig)
		{
			FString Saved;
			if (GConfig->GetString(KeyBindingsConfigSection, *Id.ToString(), Saved, GGameUserSettingsIni))
			{
				Action.CurrentKey = Saved.IsEmpty() ? EKeys::Invalid : FKey(*Saved);
			}
		}
		Actions.Add(MoveTemp(Action));
	};

	Add(ToggleInventory, TEXT("Inventory"), EKeys::I);
	Add(ToggleCharacter, TEXT("Character"), EKeys::C);
	Add(ToggleSkills, TEXT("Skills"), EKeys::K);
	Add(ToggleBelt, TEXT("Belt"), EKeys::B);
	Add(ToggleMessenger, TEXT("Messenger"), EKeys::N);
	Add(ToggleMap, TEXT("Map"), EKeys::M);
	Add(PickupItems, TEXT("Pick up items"), EKeys::Z);
	Add(ToggleFirstPerson, TEXT("First person"), EKeys::X);
	Add(VoiceTalk, TEXT("Voice chat (hold)"), EKeys::V);
	Add(ToggleChat, TEXT("Chat"), EKeys::Enter);
	Add(SystemMenu, TEXT("System menu"), EKeys::Escape);
	const FKey QuickSlotDefaults[] = {
		EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four,
		EKeys::F1, EKeys::F2, EKeys::F3, EKeys::F4};
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(QuickSlotDefaults); ++Index)
	{
		Add(QuickSlot(Index), *FString::Printf(TEXT("Quick slot %d"), Index + 1), QuickSlotDefaults[Index]);
	}
}

FMT2KeyBindingAction* UMT2KeyBindingSubsystem::FindAction(FName ActionId)
{
	return Actions.FindByPredicate(
		[ActionId](const FMT2KeyBindingAction& Action) { return Action.Id == ActionId; });
}

FKey UMT2KeyBindingSubsystem::GetKey(FName ActionId) const
{
	const FMT2KeyBindingAction* Action = Actions.FindByPredicate(
		[ActionId](const FMT2KeyBindingAction& Each) { return Each.Id == ActionId; });
	return Action ? Action->CurrentKey : EKeys::Invalid;
}

void UMT2KeyBindingSubsystem::SaveAction(const FMT2KeyBindingAction& Action)
{
	if (GConfig)
	{
		GConfig->SetString(KeyBindingsConfigSection, *Action.Id.ToString(),
			Action.CurrentKey.IsValid() ? *Action.CurrentKey.ToString() : TEXT(""),
			GGameUserSettingsIni);
	}
}

void UMT2KeyBindingSubsystem::SetKey(FName ActionId, const FKey& NewKey)
{
	FMT2KeyBindingAction* Action = FindAction(ActionId);
	if (!Action || !NewKey.IsValid())
	{
		return;
	}
	// A key can drive only one action: stealing it unbinds the previous owner.
	for (FMT2KeyBindingAction& Other : Actions)
	{
		if (Other.Id != ActionId && Other.CurrentKey == NewKey)
		{
			Other.CurrentKey = EKeys::Invalid;
			SaveAction(Other);
		}
	}
	Action->CurrentKey = NewKey;
	SaveAction(*Action);
	if (GConfig)
	{
		GConfig->Flush(false, GGameUserSettingsIni);
	}
	OnBindingsChanged.Broadcast();
}

void UMT2KeyBindingSubsystem::ResetKey(FName ActionId)
{
	FMT2KeyBindingAction* Action = FindAction(ActionId);
	if (!Action)
	{
		return;
	}
	// Restoring a default steals the key back from whoever holds it now.
	for (FMT2KeyBindingAction& Other : Actions)
	{
		if (Other.Id != ActionId && Other.CurrentKey == Action->DefaultKey)
		{
			Other.CurrentKey = EKeys::Invalid;
			SaveAction(Other);
		}
	}
	Action->CurrentKey = Action->DefaultKey;
	SaveAction(*Action);
	if (GConfig)
	{
		GConfig->Flush(false, GGameUserSettingsIni);
	}
	OnBindingsChanged.Broadcast();
}

void UMT2KeyBindingSubsystem::ResetAllKeys()
{
	for (FMT2KeyBindingAction& Action : Actions)
	{
		Action.CurrentKey = Action.DefaultKey;
		SaveAction(Action);
	}
	if (GConfig)
	{
		GConfig->Flush(false, GGameUserSettingsIni);
	}
	OnBindingsChanged.Broadcast();
}
