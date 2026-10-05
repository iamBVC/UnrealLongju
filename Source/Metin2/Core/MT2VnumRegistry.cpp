/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "Core/MT2VnumRegistry.h"

#include "Items/MT2ItemTemplate.h"
#include "Mobs/MT2Mob.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

void UMT2VnumRegistry::SetEntries(
	TMap<int32, TSoftClassPtr<AMT2Mob>>&& NewMobClasses,
	TMap<int32, TSoftClassPtr<UMT2ItemTemplate>>&& NewItemTemplates)
{
	MobClasses = MoveTemp(NewMobClasses);
	ItemTemplates = MoveTemp(NewItemTemplates);
	MarkPackageDirty();
}

#if WITH_EDITOR
EDataValidationResult UMT2VnumRegistry::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	for (const TPair<int32, TSoftClassPtr<AMT2Mob>>& Entry : MobClasses)
	{
		const UClass* MobClass = Entry.Value.LoadSynchronous();
		const AMT2Mob* MobDefaults = MobClass ? MobClass->GetDefaultObject<AMT2Mob>() : nullptr;
		const int32 ClassVnum = MobDefaults ? MobDefaults->GetMobVnum() : 0;
		if (Entry.Key <= 0 || !MobDefaults || ClassVnum != Entry.Key)
		{
			Context.AddError(FText::FromString(FString::Printf(
				TEXT("Mob registry entry %d is missing or its Blueprint now declares VNUM %d."),
				Entry.Key, ClassVnum)));
			Result = EDataValidationResult::Invalid;
		}
	}
	for (const TPair<int32, TSoftClassPtr<UMT2ItemTemplate>>& Entry : ItemTemplates)
	{
		const UClass* ItemClass = Entry.Value.LoadSynchronous();
		const UMT2ItemTemplate* ItemDefaults = ItemClass
			? ItemClass->GetDefaultObject<UMT2ItemTemplate>() : nullptr;
		const int32 ClassVnum = ItemDefaults ? ItemDefaults->Vnum : 0;
		if (Entry.Key <= 0 || !ItemDefaults || ClassVnum != Entry.Key)
		{
			Context.AddError(FText::FromString(FString::Printf(
				TEXT("Item registry entry %d is missing or its Blueprint now declares VNUM %d."),
				Entry.Key, ClassVnum)));
			Result = EDataValidationResult::Invalid;
		}
	}
	return Result == EDataValidationResult::NotValidated
		? EDataValidationResult::Valid : Result;
}
#endif
