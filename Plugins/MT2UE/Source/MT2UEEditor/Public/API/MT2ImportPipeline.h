/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "CoreMinimal.h"
#include "API/MT2ImportRegistry.h"
#include "MT2AssetScanner.h"

class FMT2ImportPipeline
{
public:
	FMT2ImportPipeline();
	explicit FMT2ImportPipeline(TSharedRef<FMT2ImportRegistry> InRegistry);

	bool ScanSource(const FMT2ImportContext& Context, FMT2AssetScanResult& OutScanResult, FMT2ImportResult& OutResult) const;
	bool Discover(const FMT2ImportContext& Context, const FMT2AssetScanResult& ScanResult, EMT2ImportDomain Domain, FMT2ImportDiscovery& OutDiscovery, FMT2ImportResult& OutResult) const;
	bool Import(const FMT2ImportRequest& Request, FMT2ImportResult& OutResult) const;

	TSharedRef<FMT2ImportRegistry> GetRegistry() const;

private:
	TSharedRef<FMT2ImportRegistry> Registry;
};
