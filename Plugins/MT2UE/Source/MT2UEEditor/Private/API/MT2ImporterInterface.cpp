/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "API/MT2ImporterInterface.h"

bool IMT2Importer::CanImport(const FMT2ImportRequest& Request, FMT2ImportResult& OutResult) const
{
	return Request.Selection.Domain == GetDomain();
}

bool IMT2Importer::Discover(const FMT2ImportContext& Context, const FMT2AssetScanResult& ScanResult, FMT2ImportDiscovery& OutDiscovery) const
{
	OutDiscovery.Domain = GetDomain();
	return true;
}

FMT2ImporterBase::FMT2ImporterBase(EMT2ImportDomain InDomain, FName InName)
	: Domain(InDomain)
	, ImporterName(InName)
{
}

FName FMT2ImporterBase::GetImporterName() const
{
	return ImporterName;
}

EMT2ImportDomain FMT2ImporterBase::GetDomain() const
{
	return Domain;
}

bool FMT2ImporterBase::CanImport(const FMT2ImportRequest& Request, FMT2ImportResult& OutResult) const
{
	return IsDomainRequest(Request, OutResult);
}

bool FMT2ImporterBase::Discover(const FMT2ImportContext& Context, const FMT2AssetScanResult& ScanResult, FMT2ImportDiscovery& OutDiscovery) const
{
	OutDiscovery.Domain = Domain;
	return true;
}

bool FMT2ImporterBase::IsDomainRequest(const FMT2ImportRequest& Request, FMT2ImportResult& OutResult) const
{
	if (Request.Selection.Domain != Domain)
	{
		OutResult.AddError(FString::Printf(TEXT("%s importer cannot process %s requests."),
			*LexToString(Domain),
			*LexToString(Request.Selection.Domain)));
		return false;
	}
	return true;
}
