/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "CoreMinimal.h"
#include "API/MT2ImporterInterface.h"

struct FScopedSlowTask;

class FMT2MapTerrainImporter : public FMT2ImporterBase
{
public:
	FMT2MapTerrainImporter();

	virtual bool Discover(const FMT2ImportContext& Context, const FMT2AssetScanResult& ScanResult, FMT2ImportDiscovery& OutDiscovery) const override;
	virtual bool Import(const FMT2ImportRequest& Request, FMT2ImportResult& OutResult) override;

	static bool CreateLandscapeActor(const FMT2ImportContext& Context, const FMT2MapTerrainInfo& Map, const FMT2PreparedMapTerrain& Prepared, FMT2ImportResult& OutResult);
	static void ImportAndPlaceStaticObjectsForMap(const FMT2ImportContext& Context, const FString& MapName, float WorldSizeX, FMT2ImportResult& OutResult, FScopedSlowTask* Progress = nullptr);
};
