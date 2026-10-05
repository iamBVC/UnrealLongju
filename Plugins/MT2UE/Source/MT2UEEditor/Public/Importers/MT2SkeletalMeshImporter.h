/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "CoreMinimal.h"
#include "API/MT2ImporterInterface.h"

class USkeletalMesh;

class FMT2SkeletalMeshImporter : public FMT2ImporterBase
{
public:
	FMT2SkeletalMeshImporter();

	virtual bool Discover(const FMT2ImportContext& Context, const FMT2AssetScanResult& ScanResult, FMT2ImportDiscovery& OutDiscovery) const override;
	virtual bool Import(const FMT2ImportRequest& Request, FMT2ImportResult& OutResult) override;

	static USkeletalMesh* LoadImportedMesh(const FMT2ImportContext& Context, const FMT2AssetRecord& Record, FString* OutObjectPath = nullptr);
};
