/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "UI/MT2QuickSlotBarWidget.h"

#include "Characters/MT2PlayerCharacter.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Core/MT2VnumRegistrySubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Items/MT2InventoryComponent.h"
#include "Items/MT2ItemTemplate.h"
#include "Player/MT2PlayerState.h"
#include "Skills/MT2SkillComponent.h"
#include "Skills/MT2SkillDefinition.h"
#include "UI/MT2CursorCarrySubsystem.h"
#include "UI/MT2QuickSlotWidget.h"
#include "UI/MT2UIStyle.h"

void UMT2QuickSlotBarWidget::NativeConstruct()
{
	Super::NativeConstruct();
	Slots = {QuickSlot1, QuickSlot2, QuickSlot3, QuickSlot4, QuickSlot5, QuickSlot6, QuickSlot7, QuickSlot8};
	UTexture2D* Public = FMT2UIStyle::LoadTexture(TEXT("/Game/ymir_work/ui/T_public.T_public"));
	for (int32 Index = 0; Index < Slots.Num(); ++Index)
	{
		const FText Hotkey = FText::FromString(Index < 4 ? FString::FromInt(Index + 1) : FString::Printf(TEXT("F%d"), Index - 3));
		Slots[Index]->InitializeQuickSlot(Index, Hotkey, Public);
		Slots[Index]->OnActivated.AddUniqueDynamic(this, &UMT2QuickSlotBarWidget::HandleSlotActivated);
		Slots[Index]->OnRightClicked.AddUniqueDynamic(this, &UMT2QuickSlotBarWidget::HandleSlotRightClicked);
	}
	ChatButton->OnClicked.AddUniqueDynamic(this, &UMT2QuickSlotBarWidget::HandleChat);
	PreviousPageButton->OnClicked.AddUniqueDynamic(this, &UMT2QuickSlotBarWidget::HandlePreviousPage);
	NextPageButton->OnClicked.AddUniqueDynamic(this, &UMT2QuickSlotBarWidget::HandleNextPage);
	SetPage(0);
	BindPlayerState();
}

void UMT2QuickSlotBarWidget::NativeDestruct()
{
	if (AMT2PlayerState* State = BoundPlayerState.Get())
	{
		State->OnQuickSlotsChanged.RemoveDynamic(this, &UMT2QuickSlotBarWidget::HandleQuickSlotsChanged);
	}
	if (UMT2SkillComponent* Skills = BoundSkillComponent.Get())
	{
		Skills->OnSkillLevelsChanged.RemoveDynamic(
			this, &UMT2QuickSlotBarWidget::HandleSkillLevelsChanged);
	}
	if (UMT2InventoryComponent* Inventory = BoundInventory.Get())
	{
		Inventory->OnInventoryChanged.RemoveDynamic(this, &UMT2QuickSlotBarWidget::HandleInventoryChanged);
	}
	BoundSkillComponent.Reset();
	Super::NativeDestruct();
}

void UMT2QuickSlotBarWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	// PlayerState/pawn replicate after construction; retry until both bindings stick.
	if (!BoundPlayerState.IsValid() || !BoundInventory.IsValid() || !BoundSkillComponent.IsValid())
	{
		BindPlayerState();
	}
}

AMT2PlayerState* UMT2QuickSlotBarWidget::GetOwningMT2PlayerState() const
{
	const APlayerController* Controller = GetOwningPlayer();
	return Controller ? Controller->GetPlayerState<AMT2PlayerState>() : nullptr;
}

UMT2InventoryComponent* UMT2QuickSlotBarWidget::GetInventoryComponent() const
{
	const AMT2PlayerCharacter* Character = Cast<AMT2PlayerCharacter>(GetOwningPlayerPawn());
	return Character ? Character->GetInventoryComponent() : nullptr;
}

void UMT2QuickSlotBarWidget::BindPlayerState()
{
	AMT2PlayerState* State = GetOwningMT2PlayerState();
	if (State && BoundPlayerState.Get() != State)
	{
		if (AMT2PlayerState* PreviousState = BoundPlayerState.Get())
		{
			PreviousState->OnQuickSlotsChanged.RemoveDynamic(
				this, &UMT2QuickSlotBarWidget::HandleQuickSlotsChanged);
		}
		BoundPlayerState = State;
		State->OnQuickSlotsChanged.AddUniqueDynamic(this, &UMT2QuickSlotBarWidget::HandleQuickSlotsChanged);
		RefreshQuickSlots();
	}

	// The replicated skill component may become available after PlayerState. Keep this binding
	// independent from the PlayerState transition so a late component cannot miss level changes.
	UMT2SkillComponent* Skills = State ? State->GetSkillComponent() : nullptr;
	if (BoundSkillComponent.Get() != Skills)
	{
		if (UMT2SkillComponent* PreviousSkills = BoundSkillComponent.Get())
		{
			PreviousSkills->OnSkillLevelsChanged.RemoveDynamic(
				this, &UMT2QuickSlotBarWidget::HandleSkillLevelsChanged);
		}
		BoundSkillComponent = Skills;
		if (Skills)
		{
			Skills->OnSkillLevelsChanged.AddUniqueDynamic(
				this, &UMT2QuickSlotBarWidget::HandleSkillLevelsChanged);
			RefreshQuickSlots();
		}
	}

	UMT2InventoryComponent* Inventory = GetInventoryComponent();
	if (Inventory && BoundInventory.Get() != Inventory)
	{
		BoundInventory = Inventory;
		Inventory->OnInventoryChanged.AddUniqueDynamic(this, &UMT2QuickSlotBarWidget::HandleInventoryChanged);
		RefreshQuickSlots();
	}
}

int32 UMT2QuickSlotBarWidget::ToGlobalSlotIndex(int32 SlotIndex) const
{
	return ActivePage * MT2QuickSlots::SlotsPerPage + SlotIndex;
}

void UMT2QuickSlotBarWidget::RefreshQuickSlots()
{
	const AMT2PlayerState* State = BoundPlayerState.Get();
	if (!State)
	{
		return;
	}
	const TArray<FMT2QuickSlotAssignment>& Assignments = State->GetQuickSlots();
	const UMT2SkillComponent* Skills = State->GetSkillComponent();
	const UMT2InventoryComponent* Inventory = GetInventoryComponent();
	UGameInstance* GameInstance = GetGameInstance();
	UMT2VnumRegistrySubsystem* Registry = GameInstance
		? GameInstance->GetSubsystem<UMT2VnumRegistrySubsystem>() : nullptr;

	for (int32 Index = 0; Index < Slots.Num(); ++Index)
	{
		UMT2QuickSlotWidget* SlotWidget = Slots[Index];
		const int32 GlobalIndex = ToGlobalSlotIndex(Index);
		if (!Assignments.IsValidIndex(GlobalIndex) || Assignments[GlobalIndex].IsEmpty())
		{
			SlotWidget->ClearIcon();
			SlotWidget->SetItemActive(false);
			continue;
		}

		const FMT2QuickSlotAssignment& Assignment = Assignments[GlobalIndex];
		if (Assignment.Type == EMT2QuickSlotType::Skill)
		{
			SlotWidget->SetItemActive(false);
			const UMT2SkillDefinition* Definition =
				Skills ? Skills->FindSkillDefinition(Assignment.Vnum) : nullptr;
			if (Definition)
			{
				// The slot renders skills through the skill window's own cell, so it picks the
				// icon, level text and cooldown up from the definition itself.
				SlotWidget->SetStackCount(0);
				SlotWidget->SetSkillBinding(Definition, Skills->GetSkillLevel(Assignment.Vnum));
			}
			else
			{
				SlotWidget->ClearIcon();
			}
		}
		else if (Assignment.Type == EMT2QuickSlotType::Item)
		{
			const TSubclassOf<UMT2ItemTemplate> TemplateClass = Registry
				? Registry->ResolveItemTemplateClass(Assignment.Vnum) : nullptr;
			const UMT2ItemTemplate* Template = TemplateClass.GetDefaultObject();
			UTexture2D* Icon = Template ? Template->Icon.LoadSynchronous() : nullptr;
			SlotWidget->SetIcon(Icon);
			SlotWidget->SetSkillBinding(nullptr, 0);

			int32 TotalCount = 0;
			bool bItemActive = false;
			if (Inventory)
			{
				for (const FMT2ItemSlot& ItemSlot : Inventory->GetSlots())
				{
					if (ItemSlot.Vnum == Assignment.Vnum)
					{
						TotalCount += ItemSlot.Count;
						bItemActive |= ItemSlot.bAutoRecoveryActive;
					}
				}
			}
			SlotWidget->SetStackCount(TotalCount);
			SlotWidget->SetItemActive(bItemActive);
		}
	}
}

void UMT2QuickSlotBarWidget::ActivateSlot(int32 SlotIndex)
{
	// Hotkey path (1-4/F1-F4): always executes the binding, never the carry logic.
	UseSlotBinding(SlotIndex);
}

bool UMT2QuickSlotBarWidget::MakeAssignmentBrush(
	const FMT2QuickSlotAssignment& Assignment, FSlateBrush& OutBrush) const
{
	if (Assignment.Type == EMT2QuickSlotType::Skill)
	{
		const AMT2PlayerState* State = BoundPlayerState.Get();
		const UMT2SkillComponent* Skills = State ? State->GetSkillComponent() : nullptr;
		const UMT2SkillDefinition* Definition =
			Skills ? Skills->FindSkillDefinition(Assignment.Vnum) : nullptr;
		UTexture2D* Atlas = Definition ? Definition->IconAtlas.LoadSynchronous() : nullptr;
		const FMT2AtlasRect Region = Definition
			? Definition->GetIconRegion(0) : FMT2AtlasRect(0, 0, 0, 0);
		if (Atlas && Region.IsValid())
		{
			OutBrush = FMT2UIStyle::AtlasBrush(Atlas, FMT2AtlasRegion(
				Region.X, Region.Y, Region.X + Region.Width, Region.Y + Region.Height));
			return true;
		}
		return false;
	}

	UGameInstance* GameInstance = GetGameInstance();
	UMT2VnumRegistrySubsystem* Registry = GameInstance
		? GameInstance->GetSubsystem<UMT2VnumRegistrySubsystem>() : nullptr;
	const TSubclassOf<UMT2ItemTemplate> TemplateClass = Registry
		? Registry->ResolveItemTemplateClass(Assignment.Vnum) : nullptr;
	const UMT2ItemTemplate* Template = TemplateClass.GetDefaultObject();
	UTexture2D* Icon = Template ? Template->Icon.LoadSynchronous() : nullptr;
	if (Icon)
	{
		OutBrush = FMT2UIStyle::TextureBrush(Icon);
		return true;
	}
	return false;
}

void UMT2QuickSlotBarWidget::UseSlotBinding(int32 SlotIndex)
{
	const AMT2PlayerState* State = BoundPlayerState.Get();
	AMT2PlayerCharacter* Character = Cast<AMT2PlayerCharacter>(GetOwningPlayerPawn());
	if (!State || !Character)
	{
		return;
	}
	const TArray<FMT2QuickSlotAssignment>& Assignments = State->GetQuickSlots();
	const int32 GlobalIndex = ToGlobalSlotIndex(SlotIndex);
	if (!Assignments.IsValidIndex(GlobalIndex) || Assignments[GlobalIndex].IsEmpty())
	{
		return;
	}
	const FMT2QuickSlotAssignment& Assignment = Assignments[GlobalIndex];
	if (Assignment.Type == EMT2QuickSlotType::Skill)
	{
		Character->ServerUseSkill(Assignment.Vnum);
	}
	else if (Assignment.Type == EMT2QuickSlotType::Item)
	{
		Character->ServerUseInventoryItemByVnum(Assignment.Vnum);
	}
}

void UMT2QuickSlotBarWidget::SetPage(int32 PageIndex)
{
	ActivePage = (PageIndex + MT2QuickSlots::PageCount) % MT2QuickSlots::PageCount;
	PageText->SetText(FText::AsNumber(ActivePage + 1));
	RefreshQuickSlots();
	OnPageChanged.Broadcast(ActivePage);
}

void UMT2QuickSlotBarWidget::HandleSlotActivated(int32 SlotIndex)
{
	OnSlotActivated.Broadcast(SlotIndex);

	AMT2PlayerState* State = BoundPlayerState.Get();
	UMT2CursorCarrySubsystem* Carry = GetOwningLocalPlayer()
		? GetOwningLocalPlayer()->GetSubsystem<UMT2CursorCarrySubsystem>() : nullptr;
	if (!State || !Carry)
	{
		return;
	}
	const int32 GlobalIndex = ToGlobalSlotIndex(SlotIndex);

	// A carried skill/item lands here: bind it (and clear the source cell when it was lifted from
	// another quickslot - move semantics).
	if (Carry->IsCarrying())
	{
		const EMT2QuickSlotType SlotType = Carry->GetCarryKind() == EMT2CarryKind::Skill
			? EMT2QuickSlotType::Skill : EMT2QuickSlotType::Item;
		if (Carry->GetCarryVnum() > 0)
		{
			State->ServerSetQuickSlot(GlobalIndex, SlotType, Carry->GetCarryVnum());
			if (Carry->GetSourceQuickSlot() != INDEX_NONE && Carry->GetSourceQuickSlot() != GlobalIndex)
			{
				State->ServerSetQuickSlot(Carry->GetSourceQuickSlot(), EMT2QuickSlotType::None, 0);
			}
		}
		Carry->EndCarry();
		return;
	}

	// A bound cell gets picked up onto the cursor for rearranging; hotkeys stay the "use" path.
	const TArray<FMT2QuickSlotAssignment>& Assignments = State->GetQuickSlots();
	if (!Assignments.IsValidIndex(GlobalIndex) || Assignments[GlobalIndex].IsEmpty())
	{
		return;
	}
	const FMT2QuickSlotAssignment& Assignment = Assignments[GlobalIndex];
	FSlateBrush Brush;
	if (MakeAssignmentBrush(Assignment, Brush))
	{
		Carry->BeginCarry(
			Assignment.Type == EMT2QuickSlotType::Skill ? EMT2CarryKind::Skill : EMT2CarryKind::InventoryItem,
			Assignment.Vnum, INDEX_NONE, Brush, GlobalIndex);
	}
}

void UMT2QuickSlotBarWidget::HandleSlotRightClicked(int32 SlotIndex)
{
	// Right click executes the binding, same as the hotkey. Removing a binding = left-click pick
	// up, then click the ground.
	UseSlotBinding(SlotIndex);
}

void UMT2QuickSlotBarWidget::HandleQuickSlotsChanged() { RefreshQuickSlots(); }
void UMT2QuickSlotBarWidget::HandleSkillLevelsChanged() { RefreshQuickSlots(); }
void UMT2QuickSlotBarWidget::HandleInventoryChanged() { RefreshQuickSlots(); }
void UMT2QuickSlotBarWidget::HandlePreviousPage() { SetPage(ActivePage - 1); }
void UMT2QuickSlotBarWidget::HandleNextPage() { SetPage(ActivePage + 1); }
void UMT2QuickSlotBarWidget::HandleChat() { OnChatClicked.Broadcast(); }
