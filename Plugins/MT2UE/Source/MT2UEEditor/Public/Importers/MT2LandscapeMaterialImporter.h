/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "CoreMinimal.h"
#include "API/MT2ImporterInterface.h"

class UMaterialInterface;

class FMT2LandscapeMaterialImporter : public FMT2ImporterBase
{
public:
	FMT2LandscapeMaterialImporter();

	virtual bool Import(const FMT2ImportRequest& Request, FMT2ImportResult& OutResult) override;

	static UMaterialInterface* CreateMaterialInstanceForPreparedMap(const FMT2ImportContext& Context, const FMT2MapTerrainInfo& Map, const FMT2PreparedMapTerrain& Prepared, FMT2ImportResult& OutResult);
	static FName BuildLayerNameForTile(const FMT2ImportContext& Context, const FMT2MapTerrainInfo& Map, uint8 TileIndex);
	static FString BuildMaterialObjectPath(const FMT2ImportContext& Context, const FString& MapName);
	static FString BuildMaterialInstanceObjectPath(const FMT2ImportContext& Context, const FString& MapName);
};
