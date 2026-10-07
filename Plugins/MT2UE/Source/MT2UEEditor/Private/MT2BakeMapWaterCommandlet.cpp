#include "MT2BakeMapWaterCommandlet.h"
#include "Importers/MT2MapWaterReader.h"
#include "MT2MapTerrainBuilder.h"
#include "World/MT2MapPresentationActor.h"
#include "Config/MT2PathSettings.h"
#include "EngineUtils.h"
#include "FileHelpers.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"

UMT2BakeMapWaterCommandlet::UMT2BakeMapWaterCommandlet()
{
	IsClient = false; IsEditor = true; IsServer = false; LogToConsole = true;
}

int32 UMT2BakeMapWaterCommandlet::Main(const FString& Params)
{
	FString MapsText, MapRoot;
	FParse::Value(*Params, TEXT("Maps="), MapsText, false);
	FParse::Value(*Params, TEXT("MapRoot="), MapRoot);
	const bool bVerify = FParse::Param(*Params, TEXT("Verify"));
	const bool bInspect = FParse::Param(*Params, TEXT("Inspect"));
	TArray<FString> Maps; MapsText.ParseIntoArray(Maps, TEXT(","), true);
	if (Maps.IsEmpty() || MapRoot.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("Require -Maps=<map package list> -MapRoot=<directory containing legacy map folders> [-Verify]")); return 1;
	}
	bool bFailed = false;
	for (const FString& MapPath : Maps)
	{
		UWorld* World = UEditorLoadingAndSavingUtils::LoadMap(FPackageName::LongPackageNameToFilename(MapPath, FPackageName::GetMapPackageExtension()));
		if (!World) { bFailed = true; continue; }
		bool bMapFailed = false; int32 Count = 0;
		for (TActorIterator<AMT2MapPresentationActor> It(World); It; ++It)
		{
			++Count;
			FMT2AssetRecord Setting; Setting.AbsolutePath = MapRoot / It->MapId / UMT2PathSettings::Path(TEXT("Part_Setting"));
			Setting.ContentPath = It->MapId / UMT2PathSettings::Path(TEXT("Part_Setting"));
			FMT2AssetScanResult Scan; Scan.Records.Add(Setting);
			TArray<FMT2MapTerrainInfo> Infos; TArray<FMT2WaterRectangle> Rectangles; FString Error; FIntPoint Grid;
			if (!FMT2MapTerrainBuilder::DiscoverMaps(Scan, Infos, Error) || Infos.Num() != 1 ||
				FIntPoint(Infos[0].MapSizeX, Infos[0].MapSizeY) != It->MapCells ||
				!FMT2MapWaterReader::Bake(Infos[0].MapDirectory, It->MapCells, Infos[0].HeightScale, It->Attributes, Rectangles, Error, Grid))
			{
				UE_LOG(LogTemp, Error, TEXT("%s: cannot bake water; check Setting.txt dimensions and water.wtr: %s"), *MapPath, *Error);
				bMapFailed = true; continue;
			}
			if (bVerify)
			{
				bool bEqual = It->WaterGridSize == Grid && It->WaterRectangles.Num() == Rectangles.Num();
				for (int32 Index = 0; bEqual && Index < Rectangles.Num(); ++Index)
				{
					const auto& A = Rectangles[Index]; const auto& B = It->WaterRectangles[Index];
					bEqual = A.Cell == B.Cell && A.Size == B.Size && A.Height == B.Height;
				}
				if (!bEqual) { UE_LOG(LogTemp, Error, TEXT("%s: saved water bake differs from source"), *MapPath); bMapFailed = true; }
			}
			else if (!bInspect)
			{
				It->Modify(); It->WaterGridSize = Grid; It->WaterRectangles = MoveTemp(Rectangles);
				It->MarkPackageDirty();
			}
			UE_LOG(LogTemp, Display, TEXT("%s: %d water rectangles (%s)"), *MapPath,
				bInspect ? Rectangles.Num() : It->WaterRectangles.Num(), bInspect ? TEXT("inspect only") : TEXT("baked"));
		}
		if (Count == 0 || bMapFailed || (!bVerify && !bInspect && !UEditorLoadingAndSavingUtils::SaveMap(World, MapPath))) { bFailed = true; }
	}
	return bFailed ? 1 : 0;
}
