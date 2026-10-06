/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "CoreMinimal.h"
#include "Config/MT2PathSettings.h"
#include "API/MT2ImportPipeline.h"
#include "MT2AssetScanner.h"
#include "MT2MapTerrainBuilder.h"
#include "Importers/MT2ItemImporter.h"
#include "Importers/MT2MobImporter.h"
#include "MT2PropertyResolver.h"
#include "Types/SlateEnums.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Views/STableRow.h"
#include "Widgets/SCompoundWidget.h"

template<typename ItemType> class SListView;
class STableViewBase;
template<typename OptionType> class SComboBox;
class UMaterial;
class UMaterialInterface;

enum class EMT2ImportObjectType : uint8
{
	Textures,
	StaticMeshes,
	SkeletalMeshes,
	Animations,
	Effects,
	Sounds,
	Mobs,
	Items,
	MapTerrains,
	MapObjects
};

struct FMT2ImportTypeOption
{
	EMT2ImportObjectType Type = EMT2ImportObjectType::Textures;
	FString Label;
};

struct FMT2ImportListEntry
{
	EMT2ImportObjectType Type = EMT2ImportObjectType::Textures;
	FString DisplayName;
	FString Detail;
	FMT2AssetRecord AssetRecord;
	FMT2MapTerrainInfo MapTerrain;
	FMT2MobImportRecord MobRecord;
	FMT2ItemImportRecord ItemRecord;
	FString MapName;
	bool bSelected = false;
};

class SMT2UEImporterWidget : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMT2UEImporterWidget) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	FReply Scan();
	FReply ImportTextures();
	FReply AnalyzeStaticObjects();
	FReply ExportStaticObjectReport();
	FReply ImportStaticMeshes();
	FReply PlaceMapStaticObjects();
	FReply RefreshImportEntries();
	FReply SelectAllImportEntries();
	FReply SelectNoImportEntries();
	FReply ImportSelectedEntries();
	FReply StopImport();

	FText GetSourceRootText() const;
	void OnSourceRootCommitted(const FText& InText, ETextCommit::Type CommitType);

	FText GetDestinationRootText() const;
	void OnDestinationRootCommitted(const FText& InText, ETextCommit::Type CommitType);

	FText GetMaxStaticMeshImportsText() const;
	void OnMaxStaticMeshImportsCommitted(const FText& InText, ETextCommit::Type CommitType);

	FText GetMapNameFilterText() const;
	void OnMapNameFilterCommitted(const FText& InText, ETextCommit::Type CommitType);

	FText GetMaxMapPlacementsText() const;
	void OnMaxMapPlacementsCommitted(const FText& InText, ETextCommit::Type CommitType);

	TSharedRef<SWidget> GenerateImportTypeWidget(TSharedPtr<FMT2ImportTypeOption> Option) const;
	void OnImportTypeChanged(TSharedPtr<FMT2ImportTypeOption> NewSelection, ESelectInfo::Type SelectInfo);
	FText GetSelectedImportTypeText() const;
	TSharedRef<ITableRow> GenerateImportEntryRow(TSharedPtr<FMT2ImportListEntry> Entry, const TSharedRef<STableViewBase>& OwnerTable);
	void OnImportEntrySelectionChanged(TSharedPtr<FMT2ImportListEntry> Entry, ESelectInfo::Type SelectInfo);
	void SyncImportEntrySelectionFromList();
	ECheckBoxState GetEntryCheckState(TSharedPtr<FMT2ImportListEntry> Entry) const;
	void OnEntryCheckStateChanged(ECheckBoxState NewState, TSharedPtr<FMT2ImportListEntry> Entry);

	FText GetSummaryText() const;
	FText GetLogText() const;

	void InitializeImportTypes();
	void RebuildImportEntries();
	void BuildImportEntriesFromDiscovery(const FMT2ImportDiscovery& Discovery);
	void BuildTextureEntries();
	void BuildStaticMeshEntries();
	void BuildMapTerrainEntries();
	void BuildMapObjectEntries();
	void BuildMobEntries();
	void BuildItemEntries();
	int32 GetSelectedEntryCount() const;
	TArray<FMT2AssetRecord> GetSelectedTextureRecords() const;
	TArray<const FMT2AssetRecord*> GetSelectedStaticMeshRecords() const;
	TArray<FMT2AssetRecord> GetSelectedAssetRecords(EMT2ImportObjectType Type) const;
	TArray<FMT2MapTerrainInfo> GetSelectedMapTerrains() const;
	TSet<FString> GetSelectedMapNames() const;
	TArray<FMT2MobImportRecord> GetSelectedMobRecords() const;
	FReply ImportMobRecords(const TArray<FMT2MobImportRecord>& Records);
	TArray<FMT2ItemImportRecord> GetSelectedItemRecords() const;
	FReply ImportItemRecords(const TArray<FMT2ItemImportRecord>& Records);
	FReply ImportTextureRecords(const TArray<FMT2AssetRecord>& Textures);
	FReply ImportStaticMeshRecords(const TArray<const FMT2AssetRecord*>& StaticMeshes);
	FReply PrepareMapTerrainSources(const TArray<FMT2MapTerrainInfo>& Maps);
	bool CreateLandscapeFromPreparedTerrain(const FMT2MapTerrainInfo& Map, const FMT2PreparedMapTerrain& Prepared, FString& OutError);
	UMaterialInterface* CreateLandscapeMaterialInstanceForMap(const FMT2MapTerrainInfo& Map, const FMT2PreparedMapTerrain& Prepared);
	FReply PlaceMapStaticObjectsForMaps(const TSet<FString>& MapNames);
	FReply ImportSelectionWithPipeline(const FMT2ImportSelection& Selection, int32 MaxItems);
	bool ShouldStopImport() const;
	bool IsRecordAlreadyImported(const FMT2AssetRecord& Record) const;
	bool IsRecordAlreadyImportedForType(const FMT2AssetRecord& Record, EMT2ImportObjectType Type) const;

	FMT2ImportContext BuildImportContext() const;
	EMT2ImportDomain GetImportDomain(EMT2ImportObjectType Type) const;
	FString BuildDefaultSourceRoot() const;
	FString BuildDestinationPathForRecord(const FMT2AssetRecord& Record) const;
	FString BuildStaticMeshDestinationPath(const FMT2AssetRecord& Record) const;
	FString BuildConvertedMeshPathForRecord(const FMT2AssetRecord& Record) const;
	FString BuildImportedStaticMeshObjectPath(const FMT2AssetRecord& Record) const;
	FString BuildImportedSkeletalMeshObjectPath(const FMT2AssetRecord& Record) const;
	FString BuildImportedAnimationObjectPath(const FMT2AssetRecord& Record) const;
	FString BuildImportedEffectObjectPath(const FMT2AssetRecord& Record) const;
	FString BuildImportedSoundObjectPath(const FMT2AssetRecord& Record) const;
	FString BuildImportedTextureObjectPath(const FMT2AssetRecord& Record) const;
	const FMT2AssetRecord* FindTextureRecordForReference(const FString& ReferencePath) const;
	bool ConvertGrannyToMeshSource(const FMT2AssetRecord& Record, FString& OutMeshPath);
	static FString NormalizeMetin2ReferencePath(const FString& ReferencePath);
	static FVector ConvertMetin2LocationToUnreal(const FMT2MapObjectPlacement& Placement);
	static FRotator ConvertMetin2RotationToUnreal(const FMT2MapObjectPlacement& Placement);
	static FString EscapeCsv(const FString& Value);
	void AppendImportResult(const FString& Prefix, const FMT2ImportResult& Result);
	void AppendLog(const FString& Message);

private:
	FMT2ImportPipeline ImportPipeline;
	FString SourceRoot;
	FString DestinationRoot = UMT2PathSettings::Path(TEXT("ImportDestinationRoot"));
	FString MapNameFilter;
	int32 MaxStaticMeshImports = 25;
	int32 MaxMapPlacements = 1000;
	FMT2AssetScanResult LastScan;
	FMT2ResolvedWorldResult LastResolvedWorld;
	FString LogText;
	bool bHasScan = false;
	bool bHasResolvedWorld = false;
	bool bIsImporting = false;
	bool bStopRequested = false;
	bool bEnableDebugLogs = false;
	// When true, Discover() includes assets that are already imported (so they can be re-selected
	// and re-imported), and Import() is allowed to overwrite them.
	bool bShowImported = false;
	TArray<TSharedPtr<FMT2ImportTypeOption>> ImportTypeOptions;
	TSharedPtr<FMT2ImportTypeOption> SelectedImportType;
	TArray<TSharedPtr<FMT2ImportListEntry>> ImportEntries;
	TSharedPtr<SComboBox<TSharedPtr<FMT2ImportTypeOption>>> ImportTypeComboBox;
	TSharedPtr<SListView<TSharedPtr<FMT2ImportListEntry>>> ImportEntryListView;
};
