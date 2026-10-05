/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "Items/MT2Item.h"

#include "Core/MT2VnumRegistrySubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

void UMT2Item::PostLoad()
{
	Super::PostLoad();
	if (Count_DEPRECATED > 0)
	{
		InstanceData.Count = Count_DEPRECATED;
		InstanceData.Bonuses = MoveTemp(Bonuses_DEPRECATED);
		Sockets_DEPRECATED.Reset();
		Count_DEPRECATED = 0;
	}
}

UMT2Item* UMT2Item::CreateItem(UObject* WorldContextObject, int32 Vnum, int32 Count)
{
	UWorld* World = WorldContextObject ? WorldContextObject->GetWorld() : nullptr;
	UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	UMT2VnumRegistrySubsystem* Registry = GameInstance
		? GameInstance->GetSubsystem<UMT2VnumRegistrySubsystem>() : nullptr;
	return CreateItemFromTemplate(
		WorldContextObject, Registry ? Registry->ResolveItemTemplateClass(Vnum) : nullptr, Count);
}

UMT2Item* UMT2Item::CreateItemFromTemplate(
	UObject* WorldContextObject, TSubclassOf<UMT2ItemTemplate> TemplateClass, int32 Count)
{
	const UMT2ItemTemplate* TemplateDefaults = TemplateClass.GetDefaultObject();
	if (!WorldContextObject || !TemplateDefaults || TemplateDefaults->Vnum <= 0)
	{
		return nullptr;
	}

	UMT2Item* Item = NewObject<UMT2Item>(WorldContextObject);
	Item->InstanceId = FGuid::NewGuid();
	Item->Template = TemplateClass;
	Item->InstanceData = TemplateDefaults->MakeInstanceData(Count);
	return Item;
}

int32 UMT2Item::GetVnum() const
{
	const UMT2ItemTemplate* TemplateDefaults = GetTemplate();
	return TemplateDefaults ? TemplateDefaults->Vnum : 0;
}
