/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "CoreMinimal.h"
#include "Importers/MT2GrannyMeshConverter.h"

class FMT2SpeedTreeConverter
{
public:
	static bool ConvertToObj(const FString& InputTreePath, const FString& OutputObjPath,
		TArray<FMT2GrannyMaterialSlot>& OutMaterialSlots, FString& OutError);

private:
	static bool ReadMaterialManifest(const FString& ManifestPath,
		TArray<FMT2GrannyMaterialSlot>& OutMaterialSlots, FString& OutError);
};
