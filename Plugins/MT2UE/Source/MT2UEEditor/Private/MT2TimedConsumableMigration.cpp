#include "MT2TimedConsumableMigration.h"

#include "Engine/Blueprint.h"
#include "Engine/Engine.h"
#include "Items/Templates/MT2ItemUseAbilityUpTemplate.h"
#include "Items/Templates/MT2ItemUseAffectTemplate.h"
#include "Items/Templates/MT2ItemUseDewTemplate.h"
#include "Items/Templates/MT2ItemUseClearTemplate.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

bool MigrateMT2TimedConsumable(UBlueprint& Blueprint, UClass* DesiredParent, FString& OutError)
{
	auto* Old = Blueprint.GeneratedClass ? Cast<UMT2ItemUseTemplate>(Blueprint.GeneratedClass->GetDefaultObject()) : nullptr;
	if (!Old || (DesiredParent != UMT2ItemUseAbilityUpTemplate::StaticClass() &&
		DesiredParent != UMT2ItemUseAffectTemplate::StaticClass() && DesiredParent != UMT2ItemUseDewTemplate::StaticClass() &&
		DesiredParent != UMT2ItemUseClearTemplate::StaticClass()))
	{
		OutError = TEXT("Expected a use template and a supported timed parent."); return false;
	}
	// Keep every serialized property, not a small hand-picked list. This preserves authored names,
	// quantities, custom bonuses, references, meshes, and old inherited compatibility defaults.
	TStrongObjectPtr<UMT2ItemUseTemplate> Snapshot(DuplicateObject(Old, GetTransientPackage()));
	if (!Old->IsA(DesiredParent))
	{
		Blueprint.ParentClass = DesiredParent;
		FBlueprintEditorUtils::RefreshAllNodes(&Blueprint);
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(&Blueprint);
		FKismetEditorUtilities::CompileBlueprint(&Blueprint);
		if (Blueprint.Status == BS_Error || !Blueprint.GeneratedClass)
		{
			OutError = TEXT("Timed item Blueprint did not compile; package was not saved."); return false;
		}
		auto* New = Cast<UMT2ItemUseTemplate>(Blueprint.GeneratedClass->GetDefaultObject());
		UEngine::FCopyPropertiesForUnrelatedObjectsParams Params;
		Params.bDoDelta = false; Params.bNotifyObjectReplacement = false;
		UEngine::CopyPropertiesForUnrelatedObjects(Snapshot.Get(), New, Params);
		// Old generic parents have only compatibility fields. Do not reimport proto defaults:
		// migrate the actual authored values, including explicit zero/negative overrides.
		if (!Snapshot->IsA<UMT2ItemUseAbilityUpTemplate>())
			if (auto* Ability = Cast<UMT2ItemUseAbilityUpTemplate>(New))
			{
				Ability->BonusType = Snapshot->GrantedBonus; Ability->BonusAmount = Snapshot->GrantedBonusValue;
				Ability->DurationSeconds = Snapshot->BuffDurationSeconds;
			}
		if (!Snapshot->IsA<UMT2ItemUseAffectTemplate>())
			if (auto* Affect = Cast<UMT2ItemUseAffectTemplate>(New))
			{
				Affect->EffectIdentity = Snapshot->AffectType; Affect->BonusType = Snapshot->GrantedBonus;
				Affect->BonusAmount = Snapshot->GrantedBonusValue; Affect->DurationSeconds = Snapshot->BuffDurationSeconds;
			}
	}
	auto* New = Cast<UMT2ItemUseTemplate>(Blueprint.GeneratedClass->GetDefaultObject());
	for (TFieldIterator<FProperty> It(Snapshot->GetClass()); It; ++It)
	{
		const FProperty* Before = *It;
		if (Before->HasAnyPropertyFlags(CPF_Transient)) continue;
		const FProperty* After = FindFProperty<FProperty>(New->GetClass(), Before->GetFName());
		FString OldText, NewText;
		Before->ExportText_InContainer(0, OldText, Snapshot.Get(), nullptr, nullptr, PPF_None);
		if (After) After->ExportText_InContainer(0, NewText, New, nullptr, nullptr, PPF_None);
		if (!After || OldText != NewText)
		{
			OutError = FString::Printf(TEXT("Authored property changed: %s. Package was not saved."), *Before->GetName());
			return false;
		}
	}
	Blueprint.MarkPackageDirty();
	return true;
}
