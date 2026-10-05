/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "CoreMinimal.h"
#include "API/MT2ImporterInterface.h"

class FMT2MapObjectImporter : public FMT2ImporterBase
{
public:
	FMT2MapObjectImporter();

	virtual bool Discover(const FMT2ImportContext& Context, const FMT2AssetScanResult& ScanResult, FMT2ImportDiscovery& OutDiscovery) const override;
	virtual bool Import(const FMT2ImportRequest& Request, FMT2ImportResult& OutResult) override;

	static FVector ConvertMetin2LocationToUnreal(const FMT2MapObjectPlacement& Placement);
	static FRotator ConvertMetin2RotationToUnreal(const FMT2MapObjectPlacement& Placement);

	// Removes broken imported actors (missing component/mesh) and exact placement duplicates from a
	// loaded level through the editor deletion path, including World Partition external actor records.
	static int32 RepairImportedActors(UWorld* World, ULevel* Level, int32& OutBrokenCount,
		int32& OutDuplicateCount, TArray<FString>& OutWarnings);
};
