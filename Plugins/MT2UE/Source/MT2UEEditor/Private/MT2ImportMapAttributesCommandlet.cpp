#include "MT2ImportMapAttributesCommandlet.h"
#include "Importers/MT2MapAttributeReader.h"
#include "World/MT2MapPresentationActor.h"
#include "EngineUtils.h"
#include "FileHelpers.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"

UMT2ImportMapAttributesCommandlet::UMT2ImportMapAttributesCommandlet()
{
	IsClient = false; IsEditor = true; IsServer = false; LogToConsole = true;
}
int32 UMT2ImportMapAttributesCommandlet::Main(const FString& Params)
{
	FString MapsText, LocaleRoot, ClientRoot;
	FParse::Value(*Params, TEXT("Maps="), MapsText, false);
	FParse::Value(*Params, TEXT("LocaleRoot="), LocaleRoot);
	FParse::Value(*Params, TEXT("ClientRoot="), ClientRoot);
	TArray<FString> Maps;
	MapsText.ParseIntoArray(Maps, TEXT(","), true);
	const bool bVerify = FParse::Param(*Params, TEXT("Verify"));
	if (Maps.IsEmpty() || LocaleRoot.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("Require -Maps=/Game/Maps/Game/... -LocaleRoot=<server locale directory>")); return 1;
	}
	bool bFailed = false;
	for (const FString& MapPath : Maps)
	{
		UWorld* World = UEditorLoadingAndSavingUtils::LoadMap(FPackageName::LongPackageNameToFilename(MapPath, FPackageName::GetMapPackageExtension()));
		if (!World) { bFailed = true; continue; }
		int32 Imported = 0;
		bool bMapFailed = false;
		for (TActorIterator<AMT2MapPresentationActor> It(World); It; ++It)
		{
			FMT2MapAttributes Attributes;
			FString Error;
			const FString Filename = LocaleRoot / TEXT("map") / It->MapId / TEXT("server_attr");
			bool bRead = FMT2MapAttributeReader::Read(Filename, Attributes, Error) && Attributes.Size == It->MapCells * 512;
			if (!bRead && !ClientRoot.IsEmpty())
			{
				bRead = FMT2MapAttributeReader::ReadClient(ClientRoot / TEXT("ymir work") / It->MapId, It->MapCells, Attributes, Error);
				if (bRead) { UE_LOG(LogTemp, Warning, TEXT("Using matching client attr.atr for %s; server data missing or dimensions differ."), *MapPath); }
			}
			if (!bRead)
			{
				UE_LOG(LogTemp, Error, TEXT("%s: %s (or dimensions mismatch)"), *Filename, *Error);
				bMapFailed = true; continue;
			}
			if (bVerify)
			{
				if (It->Attributes.Size != Attributes.Size || It->Attributes.Flags != Attributes.Flags)
				{
					UE_LOG(LogTemp, Error, TEXT("Saved attributes differ from source: %s"), *MapPath);
					bMapFailed = true; continue;
				}
			}
			else
			{
				It->Modify();
				It->Attributes = MoveTemp(Attributes);
				It->MarkPackageDirty();
			}
			int32 SafeCells = 0;
			for (uint8 Flags : It->Attributes.Flags) { SafeCells += (Flags & 4) != 0; }
			UE_LOG(LogTemp, Display, TEXT("Attributes %s: %dx%d cells, %d safe cells."), *MapPath, It->Attributes.Size.X, It->Attributes.Size.Y, SafeCells);
			++Imported;
		}
		if (Imported == 0 || bMapFailed || (!bVerify && !UEditorLoadingAndSavingUtils::SaveMap(World, MapPath))) { bFailed = true; }
	}
	return bFailed ? 1 : 0;
}
