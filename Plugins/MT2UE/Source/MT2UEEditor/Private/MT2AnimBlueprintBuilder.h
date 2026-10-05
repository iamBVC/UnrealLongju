/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "CoreMinimal.h"

class UAnimBlueprint;
class USkeletalMesh;

namespace MT2AnimBlueprintBuilder
{
	UAnimBlueprint* CreateOrUpdateBase(
		const FString& PackagePath,
		const FString& AssetName,
		UClass* NativeParentClass,
		TArray<FString>& OutErrors,
		bool& bOutCreated);

	UAnimBlueprint* CreateOrUpdateChild(
		const FString& PackagePath,
		const FString& AssetName,
		UAnimBlueprint* BaseBlueprint,
		USkeletalMesh* PreviewMesh,
		TArray<FString>& OutErrors,
		bool& bOutCreated);
}
