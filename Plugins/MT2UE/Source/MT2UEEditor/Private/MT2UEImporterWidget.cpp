/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "MT2UEImporterWidget.h"
#include "Config/MT2PathSettings.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "AssetImportTask.h"
#include "Components/StaticMeshComponent.h"
#include "Editor.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/Texture2D.h"
#include "Factories/Factory.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFilemanager.h"
#include "Importers/MT2GrannyMeshConverter.h"
#include "Importers/MT2LandscapeMaterialImporter.h"
#include "Importers/MT2MeshUsageClassifier.h"
#include "Importers/MT2MobImporter.h"
#include "Importers/MT2AudioImporter.h"
#include "Importers/MT2EffectImporter.h"
#include "Interfaces/IPluginManager.h"
#include "Landscape.h"
#include "LandscapeInfo.h"
#include "LandscapeLayerInfoObject.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionLandscapeLayerBlend.h"
#include "Materials/MaterialExpressionLandscapeLayerCoords.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "ScopedTransaction.h"
#include "Styling/AppStyle.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Views/SHeaderRow.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STableRow.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SMT2UEImporterWidget"

DEFINE_LOG_CATEGORY_STATIC(LogMT2UEImporter, Log, All);

void SMT2UEImporterWidget::Construct(const FArguments& InArgs)
{
	SourceRoot = BuildDefaultSourceRoot();
	InitializeImportTypes();

	ChildSlot
	[
		SNew(SVerticalBox)

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8.0f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("Title", "Metin2 Asset Importer"))
			.Font(FAppStyle::GetFontStyle(TEXT("HeadingMedium")))
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8.0f, 0.0f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("SourceRootLabel", "Extracted client pack folder"))
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8.0f, 4.0f)
		[
			SNew(SEditableTextBox)
			.Text(this, &SMT2UEImporterWidget::GetSourceRootText)
			.OnTextCommitted(this, &SMT2UEImporterWidget::OnSourceRootCommitted)
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8.0f, 8.0f, 8.0f, 0.0f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("DestinationRootLabel", "UE destination root"))
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8.0f, 4.0f)
		[
			SNew(SEditableTextBox)
			.Text(this, &SMT2UEImporterWidget::GetDestinationRootText)
			.OnTextCommitted(this, &SMT2UEImporterWidget::OnDestinationRootCommitted)
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8.0f, 8.0f, 8.0f, 0.0f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("ImportTypeLabel", "Object type"))
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8.0f, 4.0f)
		[
			SAssignNew(ImportTypeComboBox, SComboBox<TSharedPtr<FMT2ImportTypeOption>>)
			.OptionsSource(&ImportTypeOptions)
			.OnSelectionChanged(this, &SMT2UEImporterWidget::OnImportTypeChanged)
			.OnGenerateWidget(this, &SMT2UEImporterWidget::GenerateImportTypeWidget)
			.InitiallySelectedItem(SelectedImportType)
			[
				SNew(STextBlock)
				.Text(this, &SMT2UEImporterWidget::GetSelectedImportTypeText)
			]
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8.0f, 8.0f, 8.0f, 0.0f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("MaxStaticMeshImportsLabel", "Max static mesh assets per import run"))
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8.0f, 4.0f)
		[
			SNew(SEditableTextBox)
			.Text(this, &SMT2UEImporterWidget::GetMaxStaticMeshImportsText)
			.OnTextCommitted(this, &SMT2UEImporterWidget::OnMaxStaticMeshImportsCommitted)
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8.0f, 8.0f, 8.0f, 0.0f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("MapNameFilterLabel", "Map name filter for placement"))
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8.0f, 4.0f)
		[
			SNew(SEditableTextBox)
			.Text(this, &SMT2UEImporterWidget::GetMapNameFilterText)
			.OnTextCommitted(this, &SMT2UEImporterWidget::OnMapNameFilterCommitted)
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8.0f, 8.0f, 8.0f, 0.0f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("MaxMapPlacementsLabel", "Max map actors per placement run"))
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8.0f, 4.0f)
		[
			SNew(SEditableTextBox)
			.Text(this, &SMT2UEImporterWidget::GetMaxMapPlacementsText)
			.OnTextCommitted(this, &SMT2UEImporterWidget::OnMaxMapPlacementsCommitted)
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8.0f)
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0.0f, 0.0f, 6.0f, 0.0f)
			[
				SNew(SButton)
				.Text(LOCTEXT("ScanButton", "Scan"))
				.OnClicked(this, &SMT2UEImporterWidget::Scan)
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0.0f, 0.0f, 6.0f, 0.0f)
			[
				SNew(SButton)
				.Text(LOCTEXT("RefreshEntriesButton", "Refresh Entries"))
				.OnClicked(this, &SMT2UEImporterWidget::RefreshImportEntries)
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0.0f, 0.0f, 6.0f, 0.0f)
			[
				SNew(SButton)
				.Text(LOCTEXT("SelectAllEntriesButton", "Select All"))
				.OnClicked(this, &SMT2UEImporterWidget::SelectAllImportEntries)
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0.0f, 0.0f, 6.0f, 0.0f)
			[
				SNew(SButton)
				.Text(LOCTEXT("SelectNoEntriesButton", "Select None"))
				.OnClicked(this, &SMT2UEImporterWidget::SelectNoImportEntries)
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0.0f, 0.0f, 6.0f, 0.0f)
			[
				SNew(SButton)
				.Text(LOCTEXT("ImportSelectedEntriesButton", "Import Selected"))
				.OnClicked(this, &SMT2UEImporterWidget::ImportSelectedEntries)
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0.0f, 0.0f, 6.0f, 0.0f)
			[
				SNew(SCheckBox)
				.IsChecked_Lambda([this]() { return bEnableDebugLogs ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([this](ECheckBoxState NewState) { bEnableDebugLogs = NewState == ECheckBoxState::Checked; })
				[
					SNew(STextBlock)
					.Text(LOCTEXT("EnableDebugLogsCheckbox", "Enable Debug"))
				]
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0.0f, 0.0f, 6.0f, 0.0f)
			[
				SNew(SCheckBox)
				.IsChecked_Lambda([this]() { return bShowImported ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([this](ECheckBoxState NewState)
				{
					bShowImported = NewState == ECheckBoxState::Checked;
					RebuildImportEntries();
				})
				.ToolTipText(LOCTEXT("ShowImportedCheckboxTooltip", "Include already-imported assets in the entry list below (for every asset type) so they can be reselected and re-imported, overwriting the existing asset."))
				[
					SNew(STextBlock)
					.Text(LOCTEXT("ShowImportedCheckbox", "Show Imported"))
				]
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0.0f, 0.0f, 6.0f, 0.0f)
			[
				SNew(SButton)
				.Text(LOCTEXT("StopImportButton", "Stop"))
				.OnClicked(this, &SMT2UEImporterWidget::StopImport)
				.IsEnabled_Lambda([this]() { return bIsImporting; })
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SButton)
				.Text(LOCTEXT("ExportStaticReportButton", "Export Static Report"))
				.OnClicked(this, &SMT2UEImporterWidget::ExportStaticObjectReport)
			]
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8.0f, 0.0f)
		[
			SNew(SSeparator)
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8.0f)
		[
			SNew(STextBlock)
			.Text(this, &SMT2UEImporterWidget::GetSummaryText)
			.AutoWrapText(true)
		]

		+ SVerticalBox::Slot()
		.FillHeight(0.55f)
		.Padding(8.0f, 0.0f, 8.0f, 8.0f)
		[
			SAssignNew(ImportEntryListView, SListView<TSharedPtr<FMT2ImportListEntry>>)
			.ListItemsSource(&ImportEntries)
			.OnGenerateRow(this, &SMT2UEImporterWidget::GenerateImportEntryRow)
			.OnSelectionChanged(this, &SMT2UEImporterWidget::OnImportEntrySelectionChanged)
			.SelectionMode(ESelectionMode::Multi)
			.HeaderRow
			(
				SNew(SHeaderRow)
				+ SHeaderRow::Column(TEXT("Selected"))
				.FixedWidth(32.0f)
				.DefaultLabel(FText::GetEmpty())
				+ SHeaderRow::Column(TEXT("Entry"))
				.DefaultLabel(LOCTEXT("EntryColumn", "Entry"))
				+ SHeaderRow::Column(TEXT("Detail"))
				.DefaultLabel(LOCTEXT("DetailColumn", "Detail"))
			)
		]

		+ SVerticalBox::Slot()
		.FillHeight(0.45f)
		.Padding(8.0f)
		[
			SNew(SScrollBox)
			+ SScrollBox::Slot()
			[
				SNew(STextBlock)
				.Text(this, &SMT2UEImporterWidget::GetLogText)
				.AutoWrapText(true)
			]
		]
	];
}

void SMT2UEImporterWidget::InitializeImportTypes()
{
	ImportTypeOptions.Reset();

	auto AddOption = [this](EMT2ImportObjectType Type, const TCHAR* Label)
	{
		TSharedPtr<FMT2ImportTypeOption> Option = MakeShared<FMT2ImportTypeOption>();
		Option->Type = Type;
		Option->Label = Label;
		ImportTypeOptions.Add(Option);
	};

	AddOption(EMT2ImportObjectType::Textures, TEXT("Textures"));
	AddOption(EMT2ImportObjectType::StaticMeshes, TEXT("Static Meshes"));
	AddOption(EMT2ImportObjectType::SkeletalMeshes, TEXT("Skeletal Meshes"));
	AddOption(EMT2ImportObjectType::Animations, TEXT("Animations"));
	AddOption(EMT2ImportObjectType::Effects, TEXT("Effects"));
	AddOption(EMT2ImportObjectType::Sounds, TEXT("Sounds"));
	AddOption(EMT2ImportObjectType::Mobs, TEXT("Mobs"));
	AddOption(EMT2ImportObjectType::Items, TEXT("Items"));
	AddOption(EMT2ImportObjectType::MapTerrains, TEXT("Map Terrains"));

	SelectedImportType = ImportTypeOptions.Num() > 0 ? ImportTypeOptions[0] : nullptr;
}

TSharedRef<SWidget> SMT2UEImporterWidget::GenerateImportTypeWidget(TSharedPtr<FMT2ImportTypeOption> Option) const
{
	return SNew(STextBlock)
		.Text(FText::FromString(Option.IsValid() ? Option->Label : FString()));
}

void SMT2UEImporterWidget::OnImportTypeChanged(TSharedPtr<FMT2ImportTypeOption> NewSelection, ESelectInfo::Type SelectInfo)
{
	SelectedImportType = NewSelection;
	RebuildImportEntries();
}

FText SMT2UEImporterWidget::GetSelectedImportTypeText() const
{
	return FText::FromString(SelectedImportType.IsValid() ? SelectedImportType->Label : FString(TEXT("Select Type")));
}

TSharedRef<ITableRow> SMT2UEImporterWidget::GenerateImportEntryRow(TSharedPtr<FMT2ImportListEntry> Entry, const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(STableRow<TSharedPtr<FMT2ImportListEntry>>, OwnerTable)
	[
		SNew(SHorizontalBox)

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(4.0f, 2.0f)
		[
			SNew(SCheckBox)
			.IsChecked(this, &SMT2UEImporterWidget::GetEntryCheckState, Entry)
			.OnCheckStateChanged(this, &SMT2UEImporterWidget::OnEntryCheckStateChanged, Entry)
		]

		+ SHorizontalBox::Slot()
		.FillWidth(0.42f)
		.Padding(4.0f, 2.0f)
		[
			SNew(STextBlock)
			.Text(FText::FromString(Entry.IsValid() ? Entry->DisplayName : FString()))
		]

		+ SHorizontalBox::Slot()
		.FillWidth(0.58f)
		.Padding(4.0f, 2.0f)
		[
			SNew(STextBlock)
			.Text(FText::FromString(Entry.IsValid() ? Entry->Detail : FString()))
		]
	];
}

void SMT2UEImporterWidget::OnImportEntrySelectionChanged(TSharedPtr<FMT2ImportListEntry> Entry, ESelectInfo::Type SelectInfo)
{
	SyncImportEntrySelectionFromList();
}

void SMT2UEImporterWidget::SyncImportEntrySelectionFromList()
{
	if (!ImportEntryListView.IsValid())
	{
		return;
	}

	TArray<TSharedPtr<FMT2ImportListEntry>> SelectedItems;
	ImportEntryListView->GetSelectedItems(SelectedItems);
	for (TSharedPtr<FMT2ImportListEntry>& Entry : ImportEntries)
	{
		if (Entry.IsValid())
		{
			Entry->bSelected = SelectedItems.Contains(Entry);
		}
	}

	ImportEntryListView->RequestListRefresh();
}

ECheckBoxState SMT2UEImporterWidget::GetEntryCheckState(TSharedPtr<FMT2ImportListEntry> Entry) const
{
	return Entry.IsValid() && Entry->bSelected ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

void SMT2UEImporterWidget::OnEntryCheckStateChanged(ECheckBoxState NewState, TSharedPtr<FMT2ImportListEntry> Entry)
{
	if (Entry.IsValid())
	{
		Entry->bSelected = NewState == ECheckBoxState::Checked;
		if (ImportEntryListView.IsValid())
		{
			if (Entry->bSelected)
			{
				ImportEntryListView->SetItemSelection(Entry, true, ESelectInfo::Direct);
			}
			else
			{
				ImportEntryListView->SetItemSelection(Entry, false, ESelectInfo::Direct);
			}
		}
	}
}

FReply SMT2UEImporterWidget::RefreshImportEntries()
{
	if (SelectedImportType.IsValid() &&
		(SelectedImportType->Type == EMT2ImportObjectType::Mobs || SelectedImportType->Type == EMT2ImportObjectType::Items))
	{
		RebuildImportEntries();
	}
	else if (!bHasScan)
	{
		Scan();
	}
	else
	{
		RebuildImportEntries();
	}

	return FReply::Handled();
}

FReply SMT2UEImporterWidget::SelectAllImportEntries()
{
	for (TSharedPtr<FMT2ImportListEntry>& Entry : ImportEntries)
	{
		if (Entry.IsValid())
		{
			Entry->bSelected = true;
			if (ImportEntryListView.IsValid())
			{
				ImportEntryListView->SetItemSelection(Entry, true, ESelectInfo::Direct);
			}
		}
	}

	if (ImportEntryListView.IsValid())
	{
		ImportEntryListView->RequestListRefresh();
	}

	return FReply::Handled();
}

FReply SMT2UEImporterWidget::SelectNoImportEntries()
{
	for (TSharedPtr<FMT2ImportListEntry>& Entry : ImportEntries)
	{
		if (Entry.IsValid())
		{
			Entry->bSelected = false;
		}
	}

	if (ImportEntryListView.IsValid())
	{
		ImportEntryListView->ClearSelection();
		ImportEntryListView->RequestListRefresh();
	}

	return FReply::Handled();
}

FReply SMT2UEImporterWidget::StopImport()
{
	bStopRequested = true;
	AppendLog(TEXT("Stop requested."));
	return FReply::Handled();
}

FReply SMT2UEImporterWidget::ImportSelectedEntries()
{
	if (!SelectedImportType.IsValid())
	{
		AppendLog(TEXT("Select an object type first."));
		return FReply::Handled();
	}

	if (GetSelectedEntryCount() == 0)
	{
		AppendLog(TEXT("No entries selected."));
		return FReply::Handled();
	}

	switch (SelectedImportType->Type)
	{
	case EMT2ImportObjectType::Textures:
	{
		FMT2ImportSelection Selection;
		Selection.Domain = EMT2ImportDomain::Textures;
		Selection.AssetRecords = GetSelectedTextureRecords();
		return ImportSelectionWithPipeline(Selection, 0);
	}
	case EMT2ImportObjectType::StaticMeshes:
	{
		FMT2ImportSelection Selection;
		Selection.Domain = EMT2ImportDomain::StaticMeshes;
		for (const FMT2AssetRecord* Record : GetSelectedStaticMeshRecords())
		{
			if (Record)
			{
				Selection.AssetRecords.Add(*Record);
			}
		}
		return ImportSelectionWithPipeline(Selection, MaxStaticMeshImports);
	}
	case EMT2ImportObjectType::SkeletalMeshes:
	{
		FMT2ImportSelection Selection;
		Selection.Domain = EMT2ImportDomain::SkeletalMeshes;
		Selection.AssetRecords = GetSelectedAssetRecords(EMT2ImportObjectType::SkeletalMeshes);
		return ImportSelectionWithPipeline(Selection, 0);
	}
	case EMT2ImportObjectType::Animations:
	{
		FMT2ImportSelection Selection;
		Selection.Domain = EMT2ImportDomain::Animations;
		Selection.AssetRecords = GetSelectedAssetRecords(EMT2ImportObjectType::Animations);
		return ImportSelectionWithPipeline(Selection, 0);
	}
	case EMT2ImportObjectType::Effects:
	{
		FMT2ImportSelection Selection;
		Selection.Domain = EMT2ImportDomain::Effects;
		Selection.AssetRecords = GetSelectedAssetRecords(EMT2ImportObjectType::Effects);
		return ImportSelectionWithPipeline(Selection, 0);
	}
	case EMT2ImportObjectType::Sounds:
	{
		FMT2ImportSelection Selection;
		Selection.Domain = EMT2ImportDomain::Audio;
		Selection.AssetRecords = GetSelectedAssetRecords(EMT2ImportObjectType::Sounds);
		return ImportSelectionWithPipeline(Selection, 0);
	}
	case EMT2ImportObjectType::Mobs:
		return ImportMobRecords(GetSelectedMobRecords());
	case EMT2ImportObjectType::Items:
		return ImportItemRecords(GetSelectedItemRecords());
	case EMT2ImportObjectType::MapTerrains:
	{
		FMT2ImportSelection Selection;
		Selection.Domain = EMT2ImportDomain::MapTerrains;
		Selection.MapTerrains = GetSelectedMapTerrains();
		return ImportSelectionWithPipeline(Selection, 0);
	}
	case EMT2ImportObjectType::MapObjects:
	{
		if (!bHasResolvedWorld)
		{
			AnalyzeStaticObjects();
		}

		FMT2ImportSelection Selection;
		Selection.Domain = EMT2ImportDomain::MapObjects;
		TSet<FString> SelectedMapNames = GetSelectedMapNames();
		for (const FString& MapName : SelectedMapNames)
		{
			Selection.MapNames.Add(MapName);
		}
		return ImportSelectionWithPipeline(Selection, MaxMapPlacements);
	}
	default:
		break;
	}

	return FReply::Handled();
}

void SMT2UEImporterWidget::RebuildImportEntries()
{
	ImportEntries.Reset();

	if (!SelectedImportType.IsValid())
	{
		if (ImportEntryListView.IsValid())
		{
			ImportEntryListView->RequestListRefresh();
		}
		return;
	}

	if (SelectedImportType->Type == EMT2ImportObjectType::Mobs)
	{
		BuildMobEntries();
		if (ImportEntryListView.IsValid()) ImportEntryListView->RequestListRefresh();
		AppendLog(FString::Printf(TEXT("Loaded %d mob entries."), ImportEntries.Num()));
		return;
	}

	if (SelectedImportType->Type == EMT2ImportObjectType::Items)
	{
		BuildItemEntries();
		if (ImportEntryListView.IsValid()) ImportEntryListView->RequestListRefresh();
		AppendLog(FString::Printf(TEXT("Loaded %d item entries."), ImportEntries.Num()));
		return;
	}

	if (!bHasScan)
	{
		if (ImportEntryListView.IsValid()) ImportEntryListView->RequestListRefresh();
		return;
	}

	FMT2ImportDiscovery Discovery;
	FMT2ImportResult DiscoveryResult;
	const EMT2ImportDomain Domain = GetImportDomain(SelectedImportType->Type);
	const bool bDiscoverySucceeded = ImportPipeline.Discover(BuildImportContext(), LastScan, Domain, Discovery, DiscoveryResult);
	if (!bDiscoverySucceeded)
	{
		AppendImportResult(TEXT("Discovery"), DiscoveryResult);
	}
	else
	{
		if (DiscoveryResult.Messages.Num() > 0)
		{
			AppendImportResult(TEXT("Discovery"), DiscoveryResult);
		}
		BuildImportEntriesFromDiscovery(Discovery);
	}

	if (ImportEntryListView.IsValid())
	{
		ImportEntryListView->RequestListRefresh();
	}

	AppendLog(FString::Printf(TEXT("Loaded %d entries for %s."),
		ImportEntries.Num(),
		SelectedImportType.IsValid() ? *SelectedImportType->Label : TEXT("selected type")));
}

void SMT2UEImporterWidget::BuildImportEntriesFromDiscovery(const FMT2ImportDiscovery& Discovery)
{
	switch (SelectedImportType.IsValid() ? SelectedImportType->Type : EMT2ImportObjectType::Textures)
	{
	case EMT2ImportObjectType::Textures:
	{
		TArray<FMT2AssetRecord> Textures = Discovery.AssetRecords;
		Textures.Sort([](const FMT2AssetRecord& Left, const FMT2AssetRecord& Right)
		{
			return Left.RelativePath < Right.RelativePath;
		});

		for (const FMT2AssetRecord& Texture : Textures)
		{
			if (IsRecordAlreadyImported(Texture))
			{
				continue;
			}

			TSharedPtr<FMT2ImportListEntry> Entry = MakeShared<FMT2ImportListEntry>();
			Entry->Type = EMT2ImportObjectType::Textures;
			Entry->AssetRecord = Texture;
			Entry->DisplayName = Texture.RelativePath;
			Entry->Detail = FString::Printf(TEXT("%s | %.1f KB"), *BuildDestinationPathForRecord(Texture), Texture.Size / 1024.0f);
			ImportEntries.Add(Entry);
		}
		break;
	}
	case EMT2ImportObjectType::StaticMeshes:
	{
		TArray<FMT2AssetRecord> Meshes = Discovery.AssetRecords;
		Meshes.Sort([](const FMT2AssetRecord& Left, const FMT2AssetRecord& Right)
		{
			return Left.RelativePath < Right.RelativePath;
		});

		for (const FMT2AssetRecord& Mesh : Meshes)
		{
			TSharedPtr<FMT2ImportListEntry> Entry = MakeShared<FMT2ImportListEntry>();
			Entry->Type = EMT2ImportObjectType::StaticMeshes;
			Entry->AssetRecord = Mesh;
			Entry->DisplayName = Mesh.RelativePath;
			const int32 PlacementCount = Discovery.EntryCountsByName.FindRef(Mesh.AbsolutePath);
			Entry->Detail = PlacementCount > 0
				? FString::Printf(TEXT("%d placement(s) | %s"), PlacementCount, *BuildStaticMeshDestinationPath(Mesh))
				: BuildStaticMeshDestinationPath(Mesh);
			ImportEntries.Add(Entry);
		}
		break;
	}
	case EMT2ImportObjectType::SkeletalMeshes:
	case EMT2ImportObjectType::Animations:
	case EMT2ImportObjectType::Effects:
	case EMT2ImportObjectType::Sounds:
	{
		TArray<FMT2AssetRecord> Records = Discovery.AssetRecords;
		Records.Sort([](const FMT2AssetRecord& Left, const FMT2AssetRecord& Right)
		{
			return Left.RelativePath < Right.RelativePath;
		});

		for (const FMT2AssetRecord& Record : Records)
		{
			if (IsRecordAlreadyImportedForType(Record, SelectedImportType->Type))
			{
				continue;
			}

			TSharedPtr<FMT2ImportListEntry> Entry = MakeShared<FMT2ImportListEntry>();
			Entry->Type = SelectedImportType->Type;
			Entry->AssetRecord = Record;
			Entry->DisplayName = Record.RelativePath;
			FString Destination = BuildDestinationPathForRecord(Record);
			if (SelectedImportType->Type == EMT2ImportObjectType::Sounds)
			{
				Destination = FPackageName::ObjectPathToPackageName(BuildImportedSoundObjectPath(Record));
			}
			else if (SelectedImportType->Type == EMT2ImportObjectType::Effects)
			{
				Destination = FPackageName::ObjectPathToPackageName(BuildImportedEffectObjectPath(Record));
			}
			Entry->Detail = FString::Printf(TEXT("%s | %.1f KB"), *Destination, Record.Size / 1024.0f);
			ImportEntries.Add(Entry);
		}
		break;
	}
	case EMT2ImportObjectType::MapTerrains:
	{
		for (const FMT2MapTerrainInfo& Map : Discovery.MapTerrains)
		{
			TSharedPtr<FMT2ImportListEntry> Entry = MakeShared<FMT2ImportListEntry>();
			Entry->Type = EMT2ImportObjectType::MapTerrains;
			Entry->MapTerrain = Map;
			Entry->MapName = Map.MapName;
			Entry->DisplayName = Map.MapName;
			Entry->Detail = FString::Printf(TEXT("%dx%d cells | height %.3f | %s"),
				Map.MapSizeX,
				Map.MapSizeY,
				Map.HeightScale,
				Map.TextureSetPath.IsEmpty() ? TEXT("texture set unresolved") : *FPaths::GetCleanFilename(Map.TextureSetPath));
			ImportEntries.Add(Entry);
		}
		break;
	}
	case EMT2ImportObjectType::MapObjects:
	{
		for (const FString& MapName : Discovery.EntryNames)
		{
			TSharedPtr<FMT2ImportListEntry> Entry = MakeShared<FMT2ImportListEntry>();
			Entry->Type = EMT2ImportObjectType::MapObjects;
			Entry->MapName = MapName;
			Entry->DisplayName = MapName;
			const int32 PlacementCount = Discovery.EntryCountsByName.FindRef(MapName);
			Entry->Detail = PlacementCount > 0
				? FString::Printf(TEXT("%d static object placement(s)"), PlacementCount)
				: TEXT("static object placement map");
			ImportEntries.Add(Entry);
		}
		break;
	}
	default:
		break;
	}
}

void SMT2UEImporterWidget::BuildTextureEntries()
{
	TArray<FMT2AssetRecord> Textures = LastScan.GetRecordsByKind(EMT2AssetKind::Texture);
	Textures.Sort([](const FMT2AssetRecord& Left, const FMT2AssetRecord& Right)
	{
		return Left.RelativePath < Right.RelativePath;
	});

	for (const FMT2AssetRecord& Texture : Textures)
	{
		if (IsRecordAlreadyImported(Texture))
		{
			continue;
		}

		TSharedPtr<FMT2ImportListEntry> Entry = MakeShared<FMT2ImportListEntry>();
		Entry->Type = EMT2ImportObjectType::Textures;
		Entry->AssetRecord = Texture;
		Entry->DisplayName = Texture.RelativePath;
		Entry->Detail = FString::Printf(TEXT("%s | %.1f KB"), *BuildDestinationPathForRecord(Texture), Texture.Size / 1024.0f);
		ImportEntries.Add(Entry);
	}
}

void SMT2UEImporterWidget::BuildStaticMeshEntries()
{
	if (!bHasResolvedWorld)
	{
		FString Error;
		FMT2PropertyResolver Resolver;
		if (Resolver.Resolve(LastScan, LastResolvedWorld, Error))
		{
			bHasResolvedWorld = true;
		}
		else
		{
			AppendLog(Error);
		}
	}

	TMap<FString, int32> PlacementCountsByAssetPath;
	for (const FMT2MapObjectPlacement& Placement : LastResolvedWorld.MapObjects)
	{
		if (Placement.ReferencedAsset && Placement.ReferencedAsset->Kind == EMT2AssetKind::Granny)
		{
			PlacementCountsByAssetPath.FindOrAdd(Placement.ReferencedAsset->AbsolutePath)++;
		}
	}

	TArray<FMT2AssetRecord> Meshes;
	for (const FMT2AssetRecord& Record : LastScan.Records)
	{
		if (Record.Kind == EMT2AssetKind::Granny && PlacementCountsByAssetPath.Contains(Record.AbsolutePath))
		{
			Meshes.Add(Record);
		}
	}

	Meshes.Sort([](const FMT2AssetRecord& Left, const FMT2AssetRecord& Right)
	{
		return Left.RelativePath < Right.RelativePath;
	});

	for (const FMT2AssetRecord& Mesh : Meshes)
	{
		if (IsRecordAlreadyImported(Mesh))
		{
			continue;
		}

		TSharedPtr<FMT2ImportListEntry> Entry = MakeShared<FMT2ImportListEntry>();
		Entry->Type = EMT2ImportObjectType::StaticMeshes;
		Entry->AssetRecord = Mesh;
		Entry->DisplayName = Mesh.RelativePath;
		Entry->Detail = FString::Printf(TEXT("%d placement(s) | %s"),
			PlacementCountsByAssetPath.FindRef(Mesh.AbsolutePath),
			*BuildStaticMeshDestinationPath(Mesh));
		ImportEntries.Add(Entry);
	}
}

void SMT2UEImporterWidget::BuildMapTerrainEntries()
{
	TArray<FMT2MapTerrainInfo> Maps;
	FString Error;
	if (!FMT2MapTerrainBuilder::DiscoverMaps(LastScan, Maps, Error))
	{
		AppendLog(Error);
		return;
	}

	for (const FMT2MapTerrainInfo& Map : Maps)
	{
		TSharedPtr<FMT2ImportListEntry> Entry = MakeShared<FMT2ImportListEntry>();
		Entry->Type = EMT2ImportObjectType::MapTerrains;
		Entry->MapTerrain = Map;
		Entry->MapName = Map.MapName;
		Entry->DisplayName = Map.MapName;
		Entry->Detail = FString::Printf(TEXT("%dx%d cells | height %.3f | %s"),
			Map.MapSizeX,
			Map.MapSizeY,
			Map.HeightScale,
			Map.TextureSetPath.IsEmpty() ? TEXT("texture set unresolved") : *FPaths::GetCleanFilename(Map.TextureSetPath));
		ImportEntries.Add(Entry);
	}
}

void SMT2UEImporterWidget::BuildMapObjectEntries()
{
	if (!bHasResolvedWorld)
	{
		FString Error;
		FMT2PropertyResolver Resolver;
		if (Resolver.Resolve(LastScan, LastResolvedWorld, Error))
		{
			bHasResolvedWorld = true;
		}
		else
		{
			AppendLog(Error);
		}
	}

	TMap<FString, int32> StaticPlacementCountsByMap;
	for (const FMT2MapObjectPlacement& Placement : LastResolvedWorld.MapObjects)
	{
		if (Placement.ReferencedAsset && Placement.ReferencedAsset->Kind == EMT2AssetKind::Granny)
		{
			StaticPlacementCountsByMap.FindOrAdd(Placement.MapName)++;
		}
	}

	TArray<FString> MapNames;
	StaticPlacementCountsByMap.GetKeys(MapNames);
	MapNames.Sort();

	for (const FString& MapName : MapNames)
	{
		TSharedPtr<FMT2ImportListEntry> Entry = MakeShared<FMT2ImportListEntry>();
		Entry->Type = EMT2ImportObjectType::MapObjects;
		Entry->MapName = MapName;
		Entry->DisplayName = MapName;
		Entry->Detail = FString::Printf(TEXT("%d static object placement(s)"), StaticPlacementCountsByMap.FindRef(MapName));
		ImportEntries.Add(Entry);
	}
}

int32 SMT2UEImporterWidget::GetSelectedEntryCount() const
{
	int32 Count = 0;
	for (const TSharedPtr<FMT2ImportListEntry>& Entry : ImportEntries)
	{
		if (Entry.IsValid() && Entry->bSelected)
		{
			Count++;
		}
	}
	return Count;
}

TArray<FMT2AssetRecord> SMT2UEImporterWidget::GetSelectedTextureRecords() const
{
	TArray<FMT2AssetRecord> Result;
	for (const TSharedPtr<FMT2ImportListEntry>& Entry : ImportEntries)
	{
		if (Entry.IsValid() && Entry->bSelected && Entry->Type == EMT2ImportObjectType::Textures)
		{
			Result.Add(Entry->AssetRecord);
		}
	}
	return Result;
}

TArray<const FMT2AssetRecord*> SMT2UEImporterWidget::GetSelectedStaticMeshRecords() const
{
	TArray<const FMT2AssetRecord*> Result;
	for (const TSharedPtr<FMT2ImportListEntry>& Entry : ImportEntries)
	{
		if (Entry.IsValid() && Entry->bSelected && Entry->Type == EMT2ImportObjectType::StaticMeshes)
		{
			Result.Add(&Entry->AssetRecord);
		}
	}
	return Result;
}

void SMT2UEImporterWidget::BuildMobEntries()
{
	TArray<FMT2MobImportRecord> Records;
	TArray<FString> Warnings;
	FString Error;
	if (!FMT2MobImporter::Discover(SourceRoot, DestinationRoot, Records, Warnings, Error, bShowImported))
	{
		AppendLog(TEXT("Mob discovery failed: ") + Error);
		return;
	}
	for (const FString& Warning : Warnings)
	{
		AppendLog(TEXT("Mobs: ") + Warning);
	}
	for (FMT2MobImportRecord& Record : Records)
	{
		TSharedPtr<FMT2ImportListEntry> Entry = MakeShared<FMT2ImportListEntry>();
		Entry->Type = EMT2ImportObjectType::Mobs;
		Entry->DisplayName = FString::Printf(TEXT("%05d  %s"),
			Record.Definition.Vnum,
			Record.Definition.DisplayName.IsEmpty() ? *Record.Definition.InternalName : *Record.Definition.DisplayName);
		Entry->Detail = FString::Printf(TEXT("Lv.%d | %s | %s"),
			Record.Definition.Level, *Record.Definition.ResourceName, *Record.MeshObjectPath);
		Entry->MobRecord = MoveTemp(Record);
		ImportEntries.Add(Entry);
	}
}

void SMT2UEImporterWidget::BuildItemEntries()
{
	TArray<FMT2ItemImportRecord> Records;
	TArray<FString> Warnings;
	FString Error;
	if (!FMT2ItemImporter::Discover(SourceRoot, DestinationRoot, Records, Warnings, Error))
	{
		AppendLog(TEXT("Item discovery failed: ") + Error);
		return;
	}
	for (const FString& Warning : Warnings)
	{
		AppendLog(TEXT("Items: ") + Warning);
	}
	for (FMT2ItemImportRecord& Record : Records)
	{
		TSharedPtr<FMT2ImportListEntry> Entry = MakeShared<FMT2ImportListEntry>();
		Entry->Type = EMT2ImportObjectType::Items;
		Entry->DisplayName = FString::Printf(TEXT("%05d  %s"),
			Record.Definition.Vnum, *Record.Definition.InternalName);
		Entry->Detail = FString::Printf(TEXT("Type=%d SubType=%d | %s"),
			Record.Definition.ItemType, Record.Definition.SubType, *Record.Definition.IconObjectPath);
		Entry->ItemRecord = MoveTemp(Record);
		ImportEntries.Add(Entry);
	}
}

TArray<FMT2AssetRecord> SMT2UEImporterWidget::GetSelectedAssetRecords(EMT2ImportObjectType Type) const
{
	TArray<FMT2AssetRecord> Result;
	for (const TSharedPtr<FMT2ImportListEntry>& Entry : ImportEntries)
	{
		if (Entry.IsValid() && Entry->bSelected && Entry->Type == Type)
		{
			Result.Add(Entry->AssetRecord);
		}
	}
	return Result;
}

TArray<FMT2MapTerrainInfo> SMT2UEImporterWidget::GetSelectedMapTerrains() const
{
	TArray<FMT2MapTerrainInfo> Result;
	for (const TSharedPtr<FMT2ImportListEntry>& Entry : ImportEntries)
	{
		if (Entry.IsValid() && Entry->bSelected && Entry->Type == EMT2ImportObjectType::MapTerrains)
		{
			Result.Add(Entry->MapTerrain);
		}
	}
	return Result;
}

TSet<FString> SMT2UEImporterWidget::GetSelectedMapNames() const
{
	TSet<FString> Result;
	for (const TSharedPtr<FMT2ImportListEntry>& Entry : ImportEntries)
	{
		if (Entry.IsValid() && Entry->bSelected && Entry->Type == EMT2ImportObjectType::MapObjects)
		{
			Result.Add(Entry->MapName);
		}
	}
	return Result;
}

FReply SMT2UEImporterWidget::Scan()
{
	FMT2ImportResult ScanResult;
	if (!ImportPipeline.ScanSource(BuildImportContext(), LastScan, ScanResult))
	{
		bHasScan = false;
		AppendImportResult(TEXT("Scan"), ScanResult);
		return FReply::Handled();
	}

	bHasScan = true;
	bHasResolvedWorld = false;
	RebuildImportEntries();
	AppendLog(FString::Printf(TEXT("Scanned %d files from %s"), LastScan.Records.Num(), *LastScan.RootDirectory));
	AppendLog(FString::Printf(TEXT("Textures: %d | Granny: %d | Model scripts: %d | Motion scripts: %d | Properties: %d | Terrain: %d | Map text: %d"),
		LastScan.GetCount(EMT2AssetKind::Texture),
		LastScan.GetCount(EMT2AssetKind::Granny),
		LastScan.GetCount(EMT2AssetKind::ModelScript),
		LastScan.GetCount(EMT2AssetKind::MotionScript),
		LastScan.GetCount(EMT2AssetKind::Property),
		LastScan.GetCount(EMT2AssetKind::Terrain),
		LastScan.GetCount(EMT2AssetKind::MapText)));

	return FReply::Handled();
}

FReply SMT2UEImporterWidget::AnalyzeStaticObjects()
{
	if (!bHasScan)
	{
		Scan();
	}

	if (!bHasScan)
	{
		return FReply::Handled();
	}

	FString Error;
	FMT2PropertyResolver Resolver;
	if (!Resolver.Resolve(LastScan, LastResolvedWorld, Error))
	{
		bHasResolvedWorld = false;
		AppendLog(Error);
		return FReply::Handled();
	}

	bHasResolvedWorld = true;
	AppendLog(FString::Printf(TEXT("Read %d property files (%d invalid, %d duplicate CRCs)."),
		LastResolvedWorld.PropertyFilesRead,
		LastResolvedWorld.InvalidPropertyFiles,
		LastResolvedWorld.DuplicatePropertyCrcs));
	AppendLog(FString::Printf(TEXT("Read %d AreaData files with %d object placements."),
		LastResolvedWorld.AreaDataFilesRead,
		LastResolvedWorld.MapObjects.Num()));
	AppendLog(FString::Printf(TEXT("Resolved %d placements to properties and %d placements to referenced assets."),
		LastResolvedWorld.ObjectsWithProperty,
		LastResolvedWorld.ObjectsWithResolvedAsset));
	AppendLog(FString::Printf(TEXT("Static .gr2 map objects ready for conversion/import: %d."),
		LastResolvedWorld.StaticGrannyObjects));

	int32 LoggedExamples = 0;
	for (const FMT2MapObjectPlacement& Placement : LastResolvedWorld.MapObjects)
	{
		if (!Placement.Property || !Placement.ReferencedAsset || Placement.ReferencedAsset->Kind != EMT2AssetKind::Granny)
		{
			continue;
		}

		const FMT2PropertyRecord* Property = Placement.Property;
		AppendLog(FString::Printf(TEXT("Example: %s/%s CRC %u -> %s -> %s"),
			*Placement.MapName,
			*Placement.CellName,
			Placement.PropertyCrc,
			*Property->PropertyName,
			*Property->ReferencedAssetVirtualPath));

		if (++LoggedExamples >= 5)
		{
			break;
		}
	}

	return FReply::Handled();
}

FReply SMT2UEImporterWidget::ExportStaticObjectReport()
{
	if (!bHasResolvedWorld)
	{
		AnalyzeStaticObjects();
	}

	if (!bHasResolvedWorld)
	{
		return FReply::Handled();
	}

	FString ReportDirectory = UMT2PathSettings::Path(TEXT("ImporterWorkingDirectory"));
	FPlatformFileManager::Get().GetPlatformFile().CreateDirectoryTree(*ReportDirectory);

	const FString ReportPath = ReportDirectory / UMT2PathSettings::Path(TEXT("StaticObjectReportFilename"));
	TArray<FString> Lines;
	Lines.Reserve(LastResolvedWorld.MapObjects.Num() + 1);
	Lines.Add(TEXT("Map,Cell,X,Y,Z,Pitch,Yaw,Roll,HeightBias,PropertyCRC,PropertyType,PropertyName,ReferencedAsset,ResolvedAbsolutePath"));

	for (const FMT2MapObjectPlacement& Placement : LastResolvedWorld.MapObjects)
	{
		const FMT2PropertyRecord* Property = Placement.Property;
		const FMT2AssetRecord* Asset = Placement.ReferencedAsset;

		const FString PropertyType = Property ? Property->PropertyType : FString();
		const FString PropertyName = Property ? Property->PropertyName : FString();
		const FString ReferencedAsset = Property ? Property->ReferencedAssetVirtualPath : FString();
		const FString AbsoluteAsset = Asset ? Asset->AbsolutePath : FString();

		TArray<FString> Columns;
		Columns.Add(EscapeCsv(Placement.MapName));
		Columns.Add(EscapeCsv(Placement.CellName));
		Columns.Add(FString::SanitizeFloat(Placement.Position.X));
		Columns.Add(FString::SanitizeFloat(Placement.Position.Y));
		Columns.Add(FString::SanitizeFloat(Placement.Position.Z));
		Columns.Add(FString::SanitizeFloat(Placement.Rotation.Pitch));
		Columns.Add(FString::SanitizeFloat(Placement.Rotation.Yaw));
		Columns.Add(FString::SanitizeFloat(Placement.Rotation.Roll));
		Columns.Add(FString::SanitizeFloat(Placement.HeightBias));
		Columns.Add(FString::Printf(TEXT("%u"), Placement.PropertyCrc));
		Columns.Add(EscapeCsv(PropertyType));
		Columns.Add(EscapeCsv(PropertyName));
		Columns.Add(EscapeCsv(ReferencedAsset));
		Columns.Add(EscapeCsv(AbsoluteAsset));
		Lines.Add(FString::Join(Columns, TEXT(",")));
	}

	if (FFileHelper::SaveStringArrayToFile(Lines, *ReportPath))
	{
		AppendLog(FString::Printf(TEXT("Wrote static object report: %s"), *ReportPath));
	}
	else
	{
		AppendLog(FString::Printf(TEXT("Failed to write static object report: %s"), *ReportPath));
	}

	return FReply::Handled();
}

FReply SMT2UEImporterWidget::ImportStaticMeshes()
{
	if (!bHasResolvedWorld)
	{
		AnalyzeStaticObjects();
	}

	if (!bHasResolvedWorld)
	{
		return FReply::Handled();
	}

	TMap<FString, const FMT2AssetRecord*> UniqueStaticMeshes;
	const FMT2MeshUsageClassifier UsageClassifier(&LastScan);
	for (const FMT2MapObjectPlacement& Placement : LastResolvedWorld.MapObjects)
	{
		if (Placement.ReferencedAsset && Placement.ReferencedAsset->Kind == EMT2AssetKind::Granny)
		{
			FMT2GrannyFileInspection Inspection;
			FString Error;
			if (FMT2GrannyMeshConverter::InspectGrannyFile(Placement.ReferencedAsset->AbsolutePath, Inspection, Error) &&
				(Inspection.Type == EMT2GrannyFileType::StaticMesh ||
					(Inspection.Type == EMT2GrannyFileType::SkeletalMesh &&
						UsageClassifier.IsReady() &&
						!UsageClassifier.RequiresSkeletalMesh(*Placement.ReferencedAsset, Inspection))))
			{
				UniqueStaticMeshes.FindOrAdd(Placement.ReferencedAsset->AbsolutePath, Placement.ReferencedAsset);
			}
		}
	}

	if (UniqueStaticMeshes.Num() == 0)
	{
		AppendLog(TEXT("No resolved static .gr2 assets found."));
		return FReply::Handled();
	}

	TArray<const FMT2AssetRecord*> StaticMeshes;
	UniqueStaticMeshes.GenerateValueArray(StaticMeshes);
	return ImportStaticMeshRecords(StaticMeshes);
}

FReply SMT2UEImporterWidget::ImportStaticMeshRecords(const TArray<const FMT2AssetRecord*>& StaticMeshes)
{
	if (StaticMeshes.Num() == 0)
	{
		AppendLog(TEXT("No static mesh entries selected."));
		return FReply::Handled();
	}

	FMT2ImportSelection Selection;
	Selection.Domain = EMT2ImportDomain::StaticMeshes;
	const FMT2MeshUsageClassifier UsageClassifier(&LastScan);
	for (const FMT2AssetRecord* RecordPtr : StaticMeshes)
	{
		if (!RecordPtr)
		{
			continue;
		}

		FMT2GrannyFileInspection Inspection;
		FString Error;
		if (FMT2GrannyMeshConverter::InspectGrannyFile(RecordPtr->AbsolutePath, Inspection, Error) &&
			(Inspection.Type == EMT2GrannyFileType::StaticMesh ||
				(Inspection.Type == EMT2GrannyFileType::SkeletalMesh &&
					UsageClassifier.IsReady() &&
					!UsageClassifier.RequiresSkeletalMesh(*RecordPtr, Inspection))))
		{
			Selection.AssetRecords.Add(*RecordPtr);
		}
	}

	return ImportSelectionWithPipeline(Selection, MaxStaticMeshImports);
}

TArray<FMT2MobImportRecord> SMT2UEImporterWidget::GetSelectedMobRecords() const
{
	TArray<FMT2MobImportRecord> Result;
	for (const TSharedPtr<FMT2ImportListEntry>& Entry : ImportEntries)
	{
		if (Entry.IsValid() && Entry->bSelected && Entry->Type == EMT2ImportObjectType::Mobs)
		{
			Result.Add(Entry->MobRecord);
		}
	}
	return Result;
}

FReply SMT2UEImporterWidget::ImportMobRecords(const TArray<FMT2MobImportRecord>& Records)
{
	bIsImporting = true;
	bStopRequested = false;
	FMT2MobImportResult Result;
	FMT2MobImporter::Import(Records, SourceRoot, DestinationRoot, [this]() { return ShouldStopImport(); }, Result);
	bIsImporting = false;
	AppendLog(Result.BuildSummary());
	for (const FString& Error : Result.Errors)
	{
		AppendLog(TEXT("Mob error: ") + Error);
	}
	RebuildImportEntries();
	return FReply::Handled();
}

TArray<FMT2ItemImportRecord> SMT2UEImporterWidget::GetSelectedItemRecords() const
{
	TArray<FMT2ItemImportRecord> Result;
	for (const TSharedPtr<FMT2ImportListEntry>& Entry : ImportEntries)
	{
		if (Entry.IsValid() && Entry->bSelected && Entry->Type == EMT2ImportObjectType::Items)
		{
			Result.Add(Entry->ItemRecord);
		}
	}
	return Result;
}

FReply SMT2UEImporterWidget::ImportItemRecords(const TArray<FMT2ItemImportRecord>& Records)
{
	bIsImporting = true;
	bStopRequested = false;
	FMT2ItemImportResult Result;
	FMT2ItemImporter::Import(Records, DestinationRoot, [this]() { return ShouldStopImport(); }, Result);
	bIsImporting = false;
	AppendLog(Result.BuildSummary());
	for (const FString& Error : Result.Errors)
	{
		AppendLog(TEXT("Item error: ") + Error);
	}
	RebuildImportEntries();
	return FReply::Handled();
}

FReply SMT2UEImporterWidget::ImportTextures()
{
	if (!bHasScan)
	{
		Scan();
	}

	if (!bHasScan)
	{
		return FReply::Handled();
	}

	TArray<FMT2AssetRecord> Textures = LastScan.GetRecordsByKind(EMT2AssetKind::Texture);
	return ImportTextureRecords(Textures);
}

FReply SMT2UEImporterWidget::ImportTextureRecords(const TArray<FMT2AssetRecord>& Textures)
{
	if (Textures.Num() == 0)
	{
		AppendLog(TEXT("No texture files found."));
		return FReply::Handled();
	}

	TArray<UAssetImportTask*> Tasks;
	Tasks.Reserve(Textures.Num());

	for (const FMT2AssetRecord& Texture : Textures)
	{
		if (ShouldStopImport())
		{
			AppendLog(TEXT("Texture import stopped."));
			break;
		}

		if (IsRecordAlreadyImported(Texture))
		{
			continue;
		}

		UAssetImportTask* Task = NewObject<UAssetImportTask>();
		Task->AddToRoot();
		Task->Filename = Texture.AbsolutePath;
		Task->DestinationPath = BuildDestinationPathForRecord(Texture);
		Task->DestinationName = FPackageName::ObjectPathToObjectName(BuildImportedTextureObjectPath(Texture));
		Task->bAutomated = true;
		Task->bSave = false;
		Task->bReplaceExisting = false;
		Task->Factory = nullptr;

		Tasks.Add(Task);
	}

	AppendLog(FString::Printf(TEXT("Importing %d texture files into %s"), Tasks.Num(), *DestinationRoot));

	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
	AssetToolsModule.Get().ImportAssetTasks(Tasks);

	for (UAssetImportTask* Task : Tasks)
	{
		Task->RemoveFromRoot();
	}

	AppendLog(TEXT("Texture import tasks completed. Save imported assets from the Content Browser when you are happy with the result."));
	return FReply::Handled();
}

FReply SMT2UEImporterWidget::PlaceMapStaticObjects()
{
	if (!bHasResolvedWorld)
	{
		AnalyzeStaticObjects();
	}

	if (!bHasResolvedWorld)
	{
		return FReply::Handled();
	}

	TSet<FString> EmptyMapFilter;
	return PlaceMapStaticObjectsForMaps(EmptyMapFilter);
}

FReply SMT2UEImporterWidget::PrepareMapTerrainSources(const TArray<FMT2MapTerrainInfo>& Maps)
{
	if (Maps.Num() == 0)
	{
		AppendLog(TEXT("No map terrain entries selected."));
		return FReply::Handled();
	}

	const FString OutputRoot = UMT2PathSettings::Path(TEXT("LandscapeSourceDirectory"));
	FPlatformFileManager::Get().GetPlatformFile().CreateDirectoryTree(*OutputRoot);

	int32 PreparedCount = 0;
	for (const FMT2MapTerrainInfo& Map : Maps)
	{
		FMT2PreparedMapTerrain Prepared;
		FString Error;
		if (!FMT2MapTerrainBuilder::PrepareLandscapeSources(Map, OutputRoot, Prepared, Error))
		{
			AppendLog(Error);
			continue;
		}

		PreparedCount++;
		TArray<FMT2AssetRecord> TerrainTextures;
		for (const FString& TextureReference : Map.TextureSetTextures)
		{
			if (const FMT2AssetRecord* TextureRecord = FindTextureRecordForReference(TextureReference))
			{
				TerrainTextures.Add(*TextureRecord);
			}
		}
		if (TerrainTextures.Num() > 0)
		{
			ImportTextureRecords(TerrainTextures);
		}

		FString LandscapeError;
		if (!CreateLandscapeFromPreparedTerrain(Map, Prepared, LandscapeError))
		{
			AppendLog(LandscapeError);
		}
		AppendLog(FString::Printf(TEXT("Prepared terrain sources for %s: height %dx%d, tile %dx%d, %d weightmaps -> %s"),
			*Map.MapName,
			Prepared.HeightmapWidth,
			Prepared.HeightmapHeight,
			Prepared.TilemapWidth,
			Prepared.TilemapHeight,
			Prepared.WeightmapPaths.Num(),
			*Prepared.OutputDirectory));
	}

	AppendLog(FString::Printf(TEXT("Prepared %d/%d selected map terrain entries."), PreparedCount, Maps.Num()));
	return FReply::Handled();
}

bool SMT2UEImporterWidget::CreateLandscapeFromPreparedTerrain(const FMT2MapTerrainInfo& Map, const FMT2PreparedMapTerrain& Prepared, FString& OutError)
{
	if (!GEditor)
	{
		OutError = TEXT("Cannot create landscape because GEditor is not available.");
		return false;
	}

	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World)
	{
		OutError = TEXT("Cannot create landscape because no editor world is open.");
		return false;
	}

	if (Prepared.LandscapeHeightData.Num() != Prepared.LandscapeWidth * Prepared.LandscapeHeight)
	{
		OutError = FString::Printf(TEXT("Prepared landscape height data is invalid for %s."), *Map.MapName);
		return false;
	}

	const FScopedTransaction Transaction(LOCTEXT("CreateMT2Landscape", "Create Metin2 Landscape"));
	World->Modify();

	const float TotalWorldY = static_cast<float>(Map.MapSizeY * 128 * Map.CellScale);
	FActorSpawnParameters SpawnParameters;
	SpawnParameters.ObjectFlags = RF_Transactional;
	ALandscape* Landscape = World->SpawnActor<ALandscape>(FVector(0.0f, -TotalWorldY, 0.0f), FRotator::ZeroRotator, SpawnParameters);
	if (!Landscape)
	{
		OutError = FString::Printf(TEXT("Failed to spawn landscape actor for %s."), *Map.MapName);
		return false;
	}

	Landscape->Modify();
	Landscape->SetActorLabel(FString::Printf(TEXT("MT2_Landscape_%s"), *Map.MapName));
	Landscape->SetFolderPath(FName(*(TEXT("MT2/") + Map.MapName + TEXT("/Terrain"))));
	Landscape->SetActorScale3D(FVector(Prepared.LandscapeXYScale, Prepared.LandscapeXYScale, Prepared.LandscapeZScale));
	Landscape->LandscapeMaterial = CreateLandscapeMaterialInstanceForMap(Map, Prepared);

	TMap<FGuid, TArray<uint16>> HeightDataPerLayer;
	HeightDataPerLayer.Add(FGuid(), Prepared.LandscapeHeightData);

	const FMT2ImportContext ImportContext = BuildImportContext();
	TMap<FName, TArray<uint8>> WeightDataByLayer;
	TMap<FName, FString> SourcePathByLayer;
	for (const TPair<uint8, TArray<uint8>>& Pair : Prepared.LandscapeWeightDataByTile)
	{
		const FName LayerName = FMT2LandscapeMaterialImporter::BuildLayerNameForTile(ImportContext, Map, Pair.Key);
		TArray<uint8>* ExistingData = WeightDataByLayer.Find(LayerName);
		if (!ExistingData)
		{
			WeightDataByLayer.Add(LayerName, Pair.Value);
			SourcePathByLayer.Add(LayerName, Prepared.LandscapeWeightmapPathByTile.FindRef(Pair.Key));
			continue;
		}
		for (int32 PixelIndex = 0; PixelIndex < FMath::Min(ExistingData->Num(), Pair.Value.Num()); ++PixelIndex)
		{
			(*ExistingData)[PixelIndex] = FMath::Max((*ExistingData)[PixelIndex], Pair.Value[PixelIndex]);
		}
	}

	TArray<FLandscapeImportLayerInfo> ImportLayerInfos;
	TArray<FName> LayerNames;
	WeightDataByLayer.GetKeys(LayerNames);
	LayerNames.Sort([](const FName& A, const FName& B) { return A.LexicalLess(B); });
	for (const FName LayerName : LayerNames)
	{
		const FString LayerAssetName = TEXT("LI_") + FMT2AssetScanner::SanitizePackagePathSegment(LayerName.ToString());
		const FString LayerPackagePath = DestinationRoot / UMT2PathSettings::Path(TEXT("Part_LandscapeLayers"));
		const FString LayerPackageName = LayerPackagePath / LayerAssetName;
		const FString LayerObjectPath = LayerPackageName + TEXT(".") + LayerAssetName;

		UPackage* Package = CreatePackage(*LayerPackageName);
		ULandscapeLayerInfoObject* LayerInfo = LoadObject<ULandscapeLayerInfoObject>(nullptr, *LayerObjectPath);
		if (!LayerInfo)
		{
			LayerInfo = NewObject<ULandscapeLayerInfoObject>(Package, *LayerAssetName, RF_Public | RF_Standalone | RF_Transactional);
			LayerInfo->LayerName = LayerName;
			LayerInfo->SetBlendMethod(ELandscapeTargetLayerBlendMethod::FinalWeightBlending, false);
			LayerInfo->LayerUsageDebugColor = FLinearColor::MakeRandomColor();
			FAssetRegistryModule::AssetCreated(LayerInfo);
			Package->MarkPackageDirty();
		}

		FLandscapeImportLayerInfo ImportLayerInfo;
		ImportLayerInfo.LayerName = LayerName;
		ImportLayerInfo.LayerInfo = LayerInfo;
		ImportLayerInfo.SourceFilePath = SourcePathByLayer.FindRef(LayerName);
		ImportLayerInfo.LayerData = WeightDataByLayer.FindChecked(LayerName);
		ImportLayerInfos.Add(MoveTemp(ImportLayerInfo));
	}

	TMap<FGuid, TArray<FLandscapeImportLayerInfo>> MaterialLayerDataPerLayer;
	MaterialLayerDataPerLayer.Add(FGuid(), MoveTemp(ImportLayerInfos));

	const FGuid LandscapeGuid = FGuid::NewGuid();
	Landscape->Import(
		LandscapeGuid,
		0,
		0,
		Prepared.LandscapeWidth - 1,
		Prepared.LandscapeHeight - 1,
		Prepared.LandscapeNumSubsections,
		Prepared.LandscapeComponentSizeQuads,
		HeightDataPerLayer,
		*Prepared.LandscapeHeightmapPath,
		MaterialLayerDataPerLayer,
		ELandscapeImportAlphamapType::Additive,
		TArrayView<const FLandscapeLayer>());

	if (ULandscapeInfo* LandscapeInfo = Landscape->GetLandscapeInfo())
	{
		LandscapeInfo->UpdateLayerInfoMap(Landscape);
	}

	Landscape->PostEditChange();
	World->MarkPackageDirty();

	OutError = FString();
	return true;
}

UMaterialInterface* SMT2UEImporterWidget::CreateLandscapeMaterialInstanceForMap(const FMT2MapTerrainInfo& Map, const FMT2PreparedMapTerrain& Prepared)
{
	FMT2ImportResult ImportResult;
	if (UMaterialInterface* Material = FMT2LandscapeMaterialImporter::CreateMaterialInstanceForPreparedMap(
		BuildImportContext(), Map, Prepared, ImportResult))
	{
		for (const FMT2ImportMessage& Message : ImportResult.Messages)
		{
			AppendLog(Message.Text);
		}
		return Material;
	}

	const FString SanitizedMapName = FMT2AssetScanner::SanitizePackagePathSegment(Map.MapName);
	const FString MaterialAssetName = FString::Printf(TEXT("M_%s_Landscape"), *SanitizedMapName);
	const FString MaterialPackagePath = DestinationRoot / UMT2PathSettings::Path(TEXT("Part_LandscapeMaterials")) / SanitizedMapName;
	const FString MaterialPackageName = MaterialPackagePath / MaterialAssetName;

	UPackage* Package = CreatePackage(*MaterialPackageName);
	UMaterial* Material = NewObject<UMaterial>(Package, *MaterialAssetName, RF_Public | RF_Standalone | RF_Transactional);
	if (!Material)
	{
		return nullptr;
	}

	Material->Modify();
	UMaterialEditorOnlyData* MaterialEditorOnly = Material->GetEditorOnlyData();

	UMaterialExpressionLandscapeLayerCoords* LayerCoords = NewObject<UMaterialExpressionLandscapeLayerCoords>(Material);
	LayerCoords->Material = Material;
	LayerCoords->MappingScale = 1.0f;
	LayerCoords->MaterialExpressionEditorX = -600;
	LayerCoords->MaterialExpressionEditorY = 0;
	MaterialEditorOnly->ExpressionCollection.AddExpression(LayerCoords);

	UMaterialExpressionLandscapeLayerBlend* LayerBlend = NewObject<UMaterialExpressionLandscapeLayerBlend>(Material);
	LayerBlend->Material = Material;
	LayerBlend->MaterialExpressionEditorX = 0;
	LayerBlend->MaterialExpressionEditorY = 0;
	MaterialEditorOnly->ExpressionCollection.AddExpression(LayerBlend);

	UMaterialExpressionConstant* Specular = NewObject<UMaterialExpressionConstant>(Material);
	Specular->Material = Material;
	Specular->R = 0.05f;
	Specular->MaterialExpressionEditorX = -300;
	Specular->MaterialExpressionEditorY = 240;
	MaterialEditorOnly->ExpressionCollection.AddExpression(Specular);
	MaterialEditorOnly->Specular.Expression = Specular;

	UMaterialExpressionConstant* Roughness = NewObject<UMaterialExpressionConstant>(Material);
	Roughness->Material = Material;
	Roughness->R = 1.0f;
	Roughness->MaterialExpressionEditorX = -300;
	Roughness->MaterialExpressionEditorY = 320;
	MaterialEditorOnly->ExpressionCollection.AddExpression(Roughness);
	MaterialEditorOnly->Roughness.Expression = Roughness;

	TArray<uint8> UsedTiles;
	Prepared.LandscapeWeightDataByTile.GetKeys(UsedTiles);
	UsedTiles.Sort();

	int32 ExpressionY = -220;
	TMap<FName, UTexture2D*> TextureParameters;
	for (const uint8 TileIndex : UsedTiles)
	{
		const FName LayerName(*FString::Printf(TEXT("tile_%03d"), TileIndex));
		const FName TextureParameterName(*FString::Printf(TEXT("Texture_tile_%03d"), TileIndex));
		FLayerBlendInput BlendInput;
		BlendInput.LayerName = LayerName;
		BlendInput.BlendType = LB_WeightBlend;
		BlendInput.PreviewWeight = TileIndex == UsedTiles[0] ? 1.0f : 0.0f;

		UTexture2D* Texture = nullptr;
		const int32 TextureSetIndex = static_cast<int32>(TileIndex) - 1;
		if (Map.TextureSetTextures.IsValidIndex(TextureSetIndex))
		{
			if (const FMT2AssetRecord* TextureRecord = FindTextureRecordForReference(Map.TextureSetTextures[TextureSetIndex]))
			{
				Texture = LoadObject<UTexture2D>(nullptr, *BuildImportedTextureObjectPath(*TextureRecord));
			}
		}

		UMaterialExpressionTextureSampleParameter2D* TextureSample = NewObject<UMaterialExpressionTextureSampleParameter2D>(Material);
		TextureSample->Material = Material;
		TextureSample->ParameterName = TextureParameterName;
		TextureSample->Texture = Texture;
		TextureSample->Coordinates.Expression = LayerCoords;
		TextureSample->MaterialExpressionEditorX = -300;
		TextureSample->MaterialExpressionEditorY = ExpressionY;
		MaterialEditorOnly->ExpressionCollection.AddExpression(TextureSample);
		BlendInput.LayerInput.Expression = TextureSample;

		if (Texture)
		{
			TextureParameters.Add(TextureParameterName, Texture);
		}

		LayerBlend->Layers.Add(BlendInput);
		ExpressionY += 160;
	}

	if (LayerBlend->Layers.Num() == 0)
	{
		UMaterialExpressionConstant3Vector* ConstantColor = NewObject<UMaterialExpressionConstant3Vector>(Material);
		ConstantColor->Material = Material;
		ConstantColor->Constant = FLinearColor(0.2f, 0.35f, 0.15f);
		ConstantColor->MaterialExpressionEditorX = -300;
		ConstantColor->MaterialExpressionEditorY = 0;
		MaterialEditorOnly->ExpressionCollection.AddExpression(ConstantColor);
		MaterialEditorOnly->BaseColor.Expression = ConstantColor;
	}
	else
	{
		MaterialEditorOnly->BaseColor.Expression = LayerBlend;
	}

	Material->PostEditChange();
	Package->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(Material);

	const FString InstanceAssetName = FString::Printf(TEXT("MI_%s_Landscape"), *SanitizedMapName);
	const FString InstancePackagePath = DestinationRoot / UMT2PathSettings::Path(TEXT("Part_LandscapeMaterialInstances")) / SanitizedMapName;
	const FString InstancePackageName = InstancePackagePath / InstanceAssetName;
	UPackage* InstancePackage = CreatePackage(*InstancePackageName);
	UMaterialInstanceConstant* MaterialInstance = NewObject<UMaterialInstanceConstant>(InstancePackage, *InstanceAssetName, RF_Public | RF_Standalone | RF_Transactional);
	if (!MaterialInstance)
	{
		return Material;
	}

	MaterialInstance->Modify();
	MaterialInstance->SetParentEditorOnly(Material);
	for (const TPair<FName, UTexture2D*>& Pair : TextureParameters)
	{
		MaterialInstance->SetTextureParameterValueEditorOnly(Pair.Key, Pair.Value);
	}
	MaterialInstance->PostEditChange();
	InstancePackage->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(MaterialInstance);
	return MaterialInstance;
}

FReply SMT2UEImporterWidget::PlaceMapStaticObjectsForMaps(const TSet<FString>& MapNames)
{
	if (!GEditor)
	{
		AppendLog(TEXT("Cannot place map objects because GEditor is not available."));
		return FReply::Handled();
	}

	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World)
	{
		AppendLog(TEXT("Cannot place map objects because no editor world is open."));
		return FReply::Handled();
	}

	const FString NormalizedMapFilter = MapNameFilter.TrimStartAndEnd().ToLower();
	TSet<FString> NormalizedSelectedMaps;
	for (const FString& MapName : MapNames)
	{
		NormalizedSelectedMaps.Add(MapName.ToLower());
	}

	TMap<FString, UStaticMesh*> StaticMeshCache;
	int32 ConsideredCount = 0;
	int32 PlacedCount = 0;
	int32 MissingMeshCount = 0;
	int32 SkippedByMapFilter = 0;

	const FScopedTransaction Transaction(LOCTEXT("PlaceMT2MapStaticObjects", "Place Metin2 Map Static Objects"));
	World->Modify();

	for (const FMT2MapObjectPlacement& Placement : LastResolvedWorld.MapObjects)
	{
		if (!Placement.ReferencedAsset || Placement.ReferencedAsset->Kind != EMT2AssetKind::Granny)
		{
			continue;
		}

		const FString PlacementMapName = Placement.MapName.ToLower();
		if (NormalizedSelectedMaps.Num() > 0 && !NormalizedSelectedMaps.Contains(PlacementMapName))
		{
			SkippedByMapFilter++;
			continue;
		}

		if (!NormalizedMapFilter.IsEmpty() && PlacementMapName != NormalizedMapFilter)
		{
			SkippedByMapFilter++;
			continue;
		}

		ConsideredCount++;
		if (PlacedCount >= MaxMapPlacements)
		{
			break;
		}

		const FMT2AssetRecord& MeshRecord = *Placement.ReferencedAsset;
		const FString MeshObjectPath = BuildImportedStaticMeshObjectPath(MeshRecord);
		UStaticMesh** CachedMesh = StaticMeshCache.Find(MeshObjectPath);
		UStaticMesh* StaticMesh = CachedMesh ? *CachedMesh : LoadObject<UStaticMesh>(nullptr, *MeshObjectPath);
		if (!CachedMesh)
		{
			StaticMeshCache.Add(MeshObjectPath, StaticMesh);
		}

		if (!StaticMesh)
		{
			MissingMeshCount++;
			continue;
		}

		FActorSpawnParameters SpawnParameters;
		SpawnParameters.Name = NAME_None;
		SpawnParameters.ObjectFlags = RF_Transactional;

		const FVector UnrealLocation = ConvertMetin2LocationToUnreal(Placement);
		const FRotator UnrealRotation = ConvertMetin2RotationToUnreal(Placement);
		AStaticMeshActor* Actor = World->SpawnActor<AStaticMeshActor>(UnrealLocation, UnrealRotation, SpawnParameters);
		if (!Actor)
		{
			continue;
		}

		Actor->Modify();
		Actor->GetStaticMeshComponent()->SetStaticMesh(StaticMesh);
		Actor->GetStaticMeshComponent()->SetMobility(EComponentMobility::Static);
		Actor->SetFolderPath(FName(*(TEXT("MT2/") + Placement.MapName + TEXT("/") + Placement.CellName)));

		const FString PropertyName = Placement.Property ? Placement.Property->PropertyName : FString::Printf(TEXT("crc_%u"), Placement.PropertyCrc);
		Actor->SetActorLabel(FString::Printf(TEXT("MT2_%s_%s_%s"), *Placement.MapName, *Placement.CellName, *PropertyName));
		PlacedCount++;
	}

	World->MarkPackageDirty();

	AppendLog(FString::Printf(TEXT("Map placement finished. Placed %d actors from %d considered static placements. Missing imported meshes: %d. Skipped by map filter: %d."),
		PlacedCount,
		ConsideredCount,
		MissingMeshCount,
		SkippedByMapFilter));

	if (PlacedCount == 0 && MissingMeshCount > 0)
	{
		AppendLog(TEXT("No actors were placed because matching imported StaticMesh assets were not found. Run Import Static Meshes first, then retry placement."));
	}

	return FReply::Handled();
}

FReply SMT2UEImporterWidget::ImportSelectionWithPipeline(const FMT2ImportSelection& Selection, int32 MaxItems)
{
	FMT2ImportRequest Request;
	bIsImporting = true;
	bStopRequested = false;
	Request.Context = BuildImportContext();
	Request.Selection = Selection;
	Request.MaxItems = MaxItems;

	FMT2ImportResult Result;
	ImportPipeline.Import(Request, Result);
	bIsImporting = false;
	AppendImportResult(LexToString(Selection.Domain), Result);
	return FReply::Handled();
}

bool SMT2UEImporterWidget::ShouldStopImport() const
{
	FSlateApplication::Get().PumpMessages();
	FSlateApplication::Get().Tick();
	return bStopRequested;
}

bool SMT2UEImporterWidget::IsRecordAlreadyImported(const FMT2AssetRecord& Record) const
{
	if (bShowImported)
	{
		return false;
	}

	if (Record.Kind != EMT2AssetKind::Texture && Record.Kind != EMT2AssetKind::Granny)
	{
		return false;
	}

	const FString ObjectPath = Record.Kind == EMT2AssetKind::Granny
		? BuildImportedStaticMeshObjectPath(Record)
		: BuildImportedTextureObjectPath(Record);
	const FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	return AssetRegistryModule.Get().GetAssetByObjectPath(FSoftObjectPath(ObjectPath)).IsValid();
}

bool SMT2UEImporterWidget::IsRecordAlreadyImportedForType(const FMT2AssetRecord& Record, EMT2ImportObjectType Type) const
{
	if (bShowImported)
	{
		return false;
	}

	FString ObjectPath;
	switch (Type)
	{
	case EMT2ImportObjectType::Textures:
		ObjectPath = BuildImportedTextureObjectPath(Record);
		break;
	case EMT2ImportObjectType::StaticMeshes:
		ObjectPath = BuildImportedStaticMeshObjectPath(Record);
		break;
	case EMT2ImportObjectType::SkeletalMeshes:
		ObjectPath = BuildImportedSkeletalMeshObjectPath(Record);
		break;
	case EMT2ImportObjectType::Animations:
		ObjectPath = BuildImportedAnimationObjectPath(Record);
		break;
	case EMT2ImportObjectType::Effects:
		ObjectPath = BuildImportedEffectObjectPath(Record);
		break;
	case EMT2ImportObjectType::Sounds:
		ObjectPath = BuildImportedSoundObjectPath(Record);
		break;
	default:
		return IsRecordAlreadyImported(Record);
	}

	const FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	return AssetRegistryModule.Get().GetAssetByObjectPath(FSoftObjectPath(ObjectPath)).IsValid();
}

FText SMT2UEImporterWidget::GetSourceRootText() const
{
	return FText::FromString(SourceRoot);
}

void SMT2UEImporterWidget::OnSourceRootCommitted(const FText& InText, ETextCommit::Type CommitType)
{
	SourceRoot = InText.ToString();
	FPaths::NormalizeDirectoryName(SourceRoot);
	bHasScan = false;
	bHasResolvedWorld = false;
	ImportEntries.Reset();
	if (ImportEntryListView.IsValid())
	{
		ImportEntryListView->RequestListRefresh();
	}
}

FText SMT2UEImporterWidget::GetDestinationRootText() const
{
	return FText::FromString(DestinationRoot);
}

void SMT2UEImporterWidget::OnDestinationRootCommitted(const FText& InText, ETextCommit::Type CommitType)
{
	DestinationRoot = InText.ToString();
	if (!DestinationRoot.StartsWith(TEXT("/Game")))
	{
		DestinationRoot = TEXT("/Game/") + DestinationRoot;
	}
	DestinationRoot.RemoveFromEnd(TEXT("/"));
	RebuildImportEntries();
}

FText SMT2UEImporterWidget::GetMaxStaticMeshImportsText() const
{
	return FText::AsNumber(MaxStaticMeshImports);
}

void SMT2UEImporterWidget::OnMaxStaticMeshImportsCommitted(const FText& InText, ETextCommit::Type CommitType)
{
	MaxStaticMeshImports = FMath::Max(1, FCString::Atoi(*InText.ToString()));
}

FText SMT2UEImporterWidget::GetMapNameFilterText() const
{
	return FText::FromString(MapNameFilter);
}

void SMT2UEImporterWidget::OnMapNameFilterCommitted(const FText& InText, ETextCommit::Type CommitType)
{
	MapNameFilter = InText.ToString();
	MapNameFilter.TrimStartAndEndInline();
}

FText SMT2UEImporterWidget::GetMaxMapPlacementsText() const
{
	return FText::AsNumber(MaxMapPlacements);
}

void SMT2UEImporterWidget::OnMaxMapPlacementsCommitted(const FText& InText, ETextCommit::Type CommitType)
{
	MaxMapPlacements = FMath::Max(1, FCString::Atoi(*InText.ToString()));
}

FText SMT2UEImporterWidget::GetSummaryText() const
{
	if (!bHasScan)
	{
		return LOCTEXT("NoScanSummary", "No scan yet.");
	}

	return FText::FromString(FString::Printf(
		TEXT("Files: %d | Textures: %d | .gr2: %d | .msm: %d | .msa/.mss: %d | Properties: %d | Terrain/map data: %d | Static .gr2 placements: %d"),
		LastScan.Records.Num(),
		LastScan.GetCount(EMT2AssetKind::Texture),
		LastScan.GetCount(EMT2AssetKind::Granny),
		LastScan.GetCount(EMT2AssetKind::ModelScript),
		LastScan.GetCount(EMT2AssetKind::MotionScript),
		LastScan.GetCount(EMT2AssetKind::Property),
		LastScan.GetCount(EMT2AssetKind::Terrain) + LastScan.GetCount(EMT2AssetKind::MapText),
		bHasResolvedWorld ? LastResolvedWorld.StaticGrannyObjects : 0));
}

FText SMT2UEImporterWidget::GetLogText() const
{
	return FText::FromString(LogText);
}

FString SMT2UEImporterWidget::BuildDefaultSourceRoot() const
{
	const FString DumpRoot = UMT2PathSettings::Path(TEXT("LegacyDumpRoot"));
	if (IFileManager::Get().DirectoryExists(*DumpRoot))
	{
		return DumpRoot;
	}

	FString BaseDir;
	TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("MT2UE"));
	if (Plugin.IsValid())
	{
		BaseDir = Plugin->GetBaseDir();
	}
	else
	{
		BaseDir = FPaths::ProjectPluginsDir() / TEXT("MT2UE");
	}

	FString Candidate = FPaths::ConvertRelativePathToFull(BaseDir / UMT2PathSettings::Path(TEXT("Part__client_pack")));
	FPaths::NormalizeDirectoryName(Candidate);
	return Candidate;
}

FMT2ImportContext SMT2UEImporterWidget::BuildImportContext() const
{
	FMT2ImportContext Context;
	Context.SourceRoot = SourceRoot;
	Context.DestinationRoot = DestinationRoot;
	Context.WorkingDirectory = UMT2PathSettings::Path(TEXT("ImporterWorkingDirectory"));
	Context.MapNameFilter = MapNameFilter;
	Context.MaxStaticMeshImports = MaxStaticMeshImports;
	Context.MaxMapObjectPlacements = MaxMapPlacements;
	Context.ScanResult = bHasScan ? &LastScan : nullptr;
	Context.ResolvedWorld = bHasResolvedWorld ? &LastResolvedWorld : nullptr;
	Context.bDryRun = false;
	Context.bReplaceExisting = bShowImported;
	Context.bImportReferencedTextures = true;
	Context.bCreateLandscapeActors = true;
	Context.bImportStaticObjectsWithMaps = true;
	Context.bImportStaticObjectTextures = true;
	Context.bEnableDebugLogs = bEnableDebugLogs;
	Context.MeshUVTransform = EMT2MeshUVTransform::Original;
	Context.ShouldCancel = [this]() { return ShouldStopImport(); };
	return Context;
}

EMT2ImportDomain SMT2UEImporterWidget::GetImportDomain(EMT2ImportObjectType Type) const
{
	switch (Type)
	{
	case EMT2ImportObjectType::Textures:
		return EMT2ImportDomain::Textures;
	case EMT2ImportObjectType::StaticMeshes:
		return EMT2ImportDomain::StaticMeshes;
	case EMT2ImportObjectType::SkeletalMeshes:
		return EMT2ImportDomain::SkeletalMeshes;
	case EMT2ImportObjectType::Animations:
		return EMT2ImportDomain::Animations;
	case EMT2ImportObjectType::Effects:
		return EMT2ImportDomain::Effects;
	case EMT2ImportObjectType::Sounds:
		return EMT2ImportDomain::Audio;
	case EMT2ImportObjectType::Mobs:
		return EMT2ImportDomain::SkeletalMeshes;
	case EMT2ImportObjectType::MapTerrains:
		return EMT2ImportDomain::MapTerrains;
	case EMT2ImportObjectType::MapObjects:
		return EMT2ImportDomain::MapObjects;
	default:
		return EMT2ImportDomain::Textures;
	}
}

FString SMT2UEImporterWidget::BuildDestinationPathForRecord(const FMT2AssetRecord& Record) const
{
	return FMT2AssetScanner::BuildContentPackagePath(DestinationRoot, Record);
}

FString SMT2UEImporterWidget::BuildStaticMeshDestinationPath(const FMT2AssetRecord& Record) const
{
	return FMT2AssetScanner::BuildContentPackagePath(DestinationRoot, Record);
}

FString SMT2UEImporterWidget::BuildConvertedMeshPathForRecord(const FMT2AssetRecord& Record) const
{
	TArray<FString> Parts;
	Record.ContentPath.ParseIntoArray(Parts, TEXT("/"), true);

	FString Result = FString(UMT2PathSettings::Path(TEXT("ConvertedDirectory"))) / FMT2AssetScanner::SanitizePackagePathSegment(Record.PackName);
	for (int32 Index = 0; Index < Parts.Num(); ++Index)
	{
		FString Segment = Parts[Index];
		if (Index == Parts.Num() - 1)
		{
			Result /= FMT2AssetScanner::SanitizePackagePathSegment(FPaths::GetBaseFilename(Segment)) + TEXT(".obj");
			continue;
		}
		Result /= FMT2AssetScanner::SanitizePackagePathSegment(Segment);
	}

	return Result;
}

FString SMT2UEImporterWidget::BuildImportedStaticMeshObjectPath(const FMT2AssetRecord& Record) const
{
	return FMT2AssetScanner::BuildContentObjectPath(DestinationRoot, Record);
}

FString SMT2UEImporterWidget::BuildImportedSkeletalMeshObjectPath(const FMT2AssetRecord& Record) const
{
	const FString PackagePath = FMT2AssetScanner::BuildContentPackagePath(DestinationRoot, Record);
	FString AssetName = FMT2AssetScanner::SanitizePackagePathSegment(FPaths::GetBaseFilename(Record.ContentPath));
	if (!AssetName.StartsWith(TEXT("SK_"), ESearchCase::IgnoreCase))
	{
		AssetName = TEXT("SK_") + AssetName;
	}
	return PackagePath / AssetName + TEXT(".") + AssetName;
}

FString SMT2UEImporterWidget::BuildImportedAnimationObjectPath(const FMT2AssetRecord& Record) const
{
	const FString PackagePath = FMT2AssetScanner::BuildContentPackagePath(DestinationRoot, Record);
	FString AssetName = FMT2AssetScanner::SanitizePackagePathSegment(FPaths::GetBaseFilename(Record.ContentPath));
	if (!AssetName.StartsWith(TEXT("A_"), ESearchCase::IgnoreCase))
	{
		AssetName = TEXT("A_") + AssetName;
	}
	return PackagePath / AssetName + TEXT(".") + AssetName;
}

FString SMT2UEImporterWidget::BuildImportedEffectObjectPath(const FMT2AssetRecord& Record) const
{
	return FMT2EffectImporter::BuildObjectPath(BuildImportContext(), Record);
}

FString SMT2UEImporterWidget::BuildImportedSoundObjectPath(const FMT2AssetRecord& Record) const
{
	return FMT2AudioImporter::BuildObjectPath(BuildImportContext(), Record);
}

FString SMT2UEImporterWidget::BuildImportedTextureObjectPath(const FMT2AssetRecord& Record) const
{
	return FMT2AssetScanner::BuildContentObjectPath(DestinationRoot, Record);
}

const FMT2AssetRecord* SMT2UEImporterWidget::FindTextureRecordForReference(const FString& ReferencePath) const
{
	const FString NormalizedReference = NormalizeMetin2ReferencePath(ReferencePath).ToLower();
	const FString CleanReferenceName = FPaths::GetCleanFilename(NormalizedReference);

	for (const FMT2AssetRecord& Record : LastScan.Records)
	{
		if (Record.Kind != EMT2AssetKind::Texture)
		{
			continue;
		}

		const FString VirtualPath = Record.VirtualPath.ToLower();
		const FString ContentPath = Record.ContentPath.ToLower();
		if (VirtualPath == NormalizedReference ||
			ContentPath == NormalizedReference ||
			NormalizedReference.EndsWith(VirtualPath) ||
			NormalizedReference.EndsWith(ContentPath))
		{
			return &Record;
		}
	}

	for (const FMT2AssetRecord& Record : LastScan.Records)
	{
		if (Record.Kind == EMT2AssetKind::Texture && FPaths::GetCleanFilename(Record.ContentPath).ToLower() == CleanReferenceName)
		{
			return &Record;
		}
	}

	return nullptr;
}

bool SMT2UEImporterWidget::ConvertGrannyToMeshSource(const FMT2AssetRecord& Record, FString& OutMeshPath)
{
	OutMeshPath = BuildConvertedMeshPathForRecord(Record);
	FPaths::NormalizeFilename(OutMeshPath);

	if (IFileManager::Get().FileExists(*OutMeshPath))
	{
		return true;
	}

	const FString OutputDirectory = FPaths::GetPath(OutMeshPath);
	FPlatformFileManager::Get().GetPlatformFile().CreateDirectoryTree(*OutputDirectory);

	FString Error;
	if (!FMT2GrannyMeshConverter::ConvertGrannyToObj(Record.AbsolutePath, OutMeshPath, Error))
	{
		AppendLog(FString::Printf(TEXT("Granny import failed for %s: %s"),
			*Record.VirtualPath,
			*Error));
		return false;
	}

	if (!IFileManager::Get().FileExists(*OutMeshPath))
	{
		AppendLog(FString::Printf(TEXT("Converter finished but did not create mesh source: %s"), *OutMeshPath));
		return false;
	}

	return true;
}

FString SMT2UEImporterWidget::NormalizeMetin2ReferencePath(const FString& ReferencePath)
{
	FString Path = ReferencePath;
	Path.TrimStartAndEndInline();
	Path.RemoveFromStart(TEXT("\""));
	Path.RemoveFromEnd(TEXT("\""));
	Path.ReplaceInline(TEXT("\\"), TEXT("/"));
	if (Path.StartsWith(TEXT("ymir work/"), ESearchCase::IgnoreCase))
	{
		Path = FString(TEXT("d:/")) + Path;
	}
	return Path;
}

FVector SMT2UEImporterWidget::ConvertMetin2LocationToUnreal(const FMT2MapObjectPlacement& Placement)
{
	return FVector(Placement.Position.X, Placement.Position.Y, Placement.Position.Z + Placement.HeightBias);
}

FRotator SMT2UEImporterWidget::ConvertMetin2RotationToUnreal(const FMT2MapObjectPlacement& Placement)
{
	return FRotator(
		-Placement.Rotation.Yaw,
		FMath::UnwindDegrees(180.0f - Placement.Rotation.Roll),
		Placement.Rotation.Pitch);
}

FString SMT2UEImporterWidget::EscapeCsv(const FString& Value)
{
	FString Escaped = Value;
	Escaped.ReplaceInline(TEXT("\""), TEXT("\"\""));
	return FString::Printf(TEXT("\"%s\""), *Escaped);
}

void SMT2UEImporterWidget::AppendImportResult(const FString& Prefix, const FMT2ImportResult& Result)
{
	AppendLog(FString::Printf(TEXT("%s %s. Discovered: %d | Imported: %d | Skipped: %d"),
		*Prefix,
		Result.bSucceeded ? TEXT("succeeded") : TEXT("finished with issues"),
		Result.ItemsDiscovered,
		Result.ItemsImported,
		Result.ItemsSkipped));

	for (const FMT2ImportMessage& Message : Result.Messages)
	{
		const TCHAR* SeverityText = TEXT("Info");
		switch (Message.Severity)
		{
		case EMT2ImportSeverity::Warning:
			SeverityText = TEXT("Warning");
			break;
		case EMT2ImportSeverity::Error:
			SeverityText = TEXT("Error");
			break;
		default:
			break;
		}

		if (Message.SourcePath.IsEmpty())
		{
			AppendLog(FString::Printf(TEXT("%s: %s"), SeverityText, *Message.Text));
		}
		else
		{
			AppendLog(FString::Printf(TEXT("%s: %s (%s)"), SeverityText, *Message.Text, *Message.SourcePath));
		}
	}
}

void SMT2UEImporterWidget::AppendLog(const FString& Message)
{
	UE_LOG(LogMT2UEImporter, Display, TEXT("%s"), *Message);

	if (!LogText.IsEmpty())
	{
		LogText += LINE_TERMINATOR;
	}
	LogText += Message;
}

#undef LOCTEXT_NAMESPACE
