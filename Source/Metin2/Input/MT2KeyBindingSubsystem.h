/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "MT2KeyBindingSubsystem.generated.h"

USTRUCT()
struct FMT2KeyBindingAction
{
	GENERATED_BODY()

	FName Id;
	FString DisplayName;
	FKey DefaultKey;
	FKey CurrentKey;
};

// Rebindable action keys for the hardcoded gameplay hotkeys (window toggles, quick slots, voice
// push-to-talk, chat, system menu). Overrides persist in GameUserSettings.ini [MT2KeyBindings];
// the player controller rebuilds its key bindings whenever OnBindingsChanged fires. Assigning a
// key already used by another action unbinds that other action (shown as unbound in the menu).
UCLASS()
class METIN2_API UMT2KeyBindingSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	const TArray<FMT2KeyBindingAction>& GetActions() const { return Actions; }
	FKey GetKey(FName ActionId) const;
	void SetKey(FName ActionId, const FKey& NewKey);
	void ResetKey(FName ActionId);
	void ResetAllKeys();

	FSimpleMulticastDelegate OnBindingsChanged;

	// Well-known action ids (quick slots are "QuickSlot1".."QuickSlot8").
	static const FName ToggleCharacter;
	static const FName ToggleSkills;
	static const FName ToggleBelt;
	static const FName ToggleMessenger;
	static const FName ToggleMap;
	static const FName ToggleInventory;
	static const FName PickupItems;
	static const FName ToggleFirstPerson;
	static const FName VoiceTalk;
	static const FName ToggleChat;
	static const FName SystemMenu;
	static FName QuickSlot(int32 Index);

private:
	FMT2KeyBindingAction* FindAction(FName ActionId);
	void SaveAction(const FMT2KeyBindingAction& Action);

	TArray<FMT2KeyBindingAction> Actions;
};
