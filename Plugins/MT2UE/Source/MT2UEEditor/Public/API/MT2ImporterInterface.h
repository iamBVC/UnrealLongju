/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "CoreMinimal.h"
#include "API/MT2ImportTypes.h"

class IMT2Importer
{
public:
	virtual ~IMT2Importer() {}

	virtual FName GetImporterName() const = 0;
	virtual EMT2ImportDomain GetDomain() const = 0;

	virtual bool CanImport(const FMT2ImportRequest& Request, FMT2ImportResult& OutResult) const;
	virtual bool Discover(const FMT2ImportContext& Context, const FMT2AssetScanResult& ScanResult, FMT2ImportDiscovery& OutDiscovery) const;
	virtual bool Import(const FMT2ImportRequest& Request, FMT2ImportResult& OutResult) = 0;
};

class FMT2ImporterBase : public IMT2Importer
{
public:
	explicit FMT2ImporterBase(EMT2ImportDomain InDomain, FName InName);

	virtual FName GetImporterName() const override;
	virtual EMT2ImportDomain GetDomain() const override;
	virtual bool CanImport(const FMT2ImportRequest& Request, FMT2ImportResult& OutResult) const override;
	virtual bool Discover(const FMT2ImportContext& Context, const FMT2AssetScanResult& ScanResult, FMT2ImportDiscovery& OutDiscovery) const override;

protected:
	bool IsDomainRequest(const FMT2ImportRequest& Request, FMT2ImportResult& OutResult) const;

private:
	EMT2ImportDomain Domain;
	FName ImporterName;
};
