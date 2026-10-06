/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "Importers/MT2ItemImporter.h"
#include "Config/MT2PathSettings.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "MT2AssetScanner.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "FileHelpers.h"
#include "HAL/FileManager.h"
#include "Importers/MT2ItemProtoReader.h"
#include "Items/MT2Item.h"
#include "Items/MT2ItemTemplate.h"
#include "Loot/MT2LootTable.h"
#include "Items/Use/MT2HorseBookItemTemplate.h"
#include "Items/Use/MT2MobLureItemTemplate.h"
#include "Items/Use/MT2MountItemTemplate.h"
#include "Items/Use/MT2SkillTrainingItemTemplates.h"
#include "Core/MT2VnumRegistry.h"
#include "Mobs/MT2Mob.h"
#include "Mounts/MT2MountDefinition.h"
#include "Stats/MT2CombatStatsComponent.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/FileHelper.h"
#include "Misc/Crc.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/ScopedSlowTask.h"
#include "Internationalization/Regex.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialInterface.h"
#include "UObject/SavePackage.h"

namespace
{
	struct FParsedLootCrateGroup
	{
		bool bIndependentRolls = false;
		TArray<FMT2ImportedLootCrateReward> Rewards;
	};

	struct FParsedMountItem
	{
		int32 MountVnum = 0;
		int32 DurationSeconds = 0;
		int32 MinimumPlayerLevel = 0;
		bool bConsumeItem = false;
	};

	int32 ParseIntegerProduct(const FString& Expression)
	{
		TArray<FString> Factors;
		Expression.ParseIntoArray(Factors, TEXT("*"), true);
		int64 Result = 1;
		for (FString Factor : Factors)
		{
			Factor.TrimStartAndEndInline();
			if (!Factor.IsNumeric())
			{
				return 0;
			}
			Result *= FCString::Atoi64(*Factor);
		}
		return static_cast<int32>(FMath::Clamp<int64>(Result, 0, MAX_int32));
	}

	TMap<int32, FParsedMountItem> LoadMountItems(
		const FString& SourceRoot, TArray<FString>& Warnings)
	{
		const FString DevelopmentRoot = FPaths::ConvertRelativePathToFull(SourceRoot / TEXT("../.."));
		const TArray<FString> QuestRoots = {
			SourceRoot / UMT2PathSettings::Path(TEXT("Part_locale_en_quest")),
			SourceRoot / UMT2PathSettings::Path(TEXT("Part_quest")),
			DevelopmentRoot / UMT2PathSettings::Path(TEXT("Part_my_server_server_src_share_locale_italy_quest")),
			UMT2PathSettings::Path(TEXT("Legacy_quest"))
		};

		TMap<int32, FParsedMountItem> Result;
		TSet<FString> LoadedPaths;
		const FRegexPattern RowPattern(
			TEXT("\\[(\\d+)\\]\\s*=\\s*\\{\\s*(\\d+)\\s*,\\s*([^,]+),\\s*apply\\.[A-Z0-9_]+\\s*,\\s*-?\\d+\\s*,\\s*(\\d+)\\s*,\\s*(true|false)"));
		for (const FString& QuestRoot : QuestRoots)
		{
			for (const TCHAR* FileName : { TEXT("ride.quest"), TEXT("ride_upgradable.quest") })
			{
				const FString Path = FPaths::ConvertRelativePathToFull(QuestRoot / FileName);
				if (LoadedPaths.Contains(Path) || !IFileManager::Get().FileExists(*Path))
				{
					continue;
				}
				LoadedPaths.Add(Path);
				FString Content;
				if (!FFileHelper::LoadFileToString(Content, *Path))
				{
					continue;
				}
				FRegexMatcher Matcher(RowPattern, Content);
				while (Matcher.FindNext())
				{
					const int32 ItemVnum = FCString::Atoi(*Matcher.GetCaptureGroup(1));
					FParsedMountItem& MountItem = Result.FindOrAdd(ItemVnum);
					MountItem.MountVnum = FCString::Atoi(*Matcher.GetCaptureGroup(2));
					MountItem.DurationSeconds = ParseIntegerProduct(Matcher.GetCaptureGroup(3));
					MountItem.MinimumPlayerLevel = FCString::Atoi(*Matcher.GetCaptureGroup(4));
					MountItem.bConsumeItem = Matcher.GetCaptureGroup(5).Equals(
						TEXT("true"), ESearchCase::IgnoreCase);
				}
			}
		}
		if (Result.IsEmpty())
		{
			Warnings.Add(TEXT("ride.quest tables were not found; mount items were not configured."));
		}
		return Result;
	}

	TMap<int32, FMT2RefinementRecipe> LoadRefinementRecipes(
		const FString& SourceRoot, TArray<FString>& Warnings)
	{
		const FString DatabasePath = SourceRoot / UMT2PathSettings::Path(TEXT("Part_db"));
		FString Content;
		if (!FFileHelper::LoadFileToString(Content, *DatabasePath))
		{
			Warnings.Add(FString::Printf(
				TEXT("Could not read %s; refinement recipes were not configured."), *DatabasePath));
			return {};
		}

		const int32 TableStart = Content.Find(TEXT("REPLACE INTO `refine_proto`"));
		const int32 TableEnd = TableStart == INDEX_NONE ? INDEX_NONE
			: Content.Find(TEXT("ALTER TABLE `refine_proto` ENABLE KEYS"), ESearchCase::CaseSensitive,
				ESearchDir::FromStart, TableStart);
		if (TableStart == INDEX_NONE || TableEnd == INDEX_NONE)
		{
			Warnings.Add(TEXT("db.sql has no readable refine_proto data block."));
			return {};
		}

		TMap<int32, FMT2RefinementRecipe> Recipes;
		TArray<FString> Lines;
		Content.Mid(TableStart, TableEnd - TableStart).ParseIntoArrayLines(Lines, false);
		for (FString Line : Lines)
		{
			Line.TrimStartAndEndInline();
			if (!Line.StartsWith(TEXT("(")))
			{
				continue;
			}
			Line.RemoveFromStart(TEXT("("));
			while (Line.EndsWith(TEXT(",")) || Line.EndsWith(TEXT(";")))
			{
				Line.LeftChopInline(1);
			}
			Line.RemoveFromEnd(TEXT(")"));

			TArray<FString> Columns;
			Line.ParseIntoArray(Columns, TEXT(","), false);
			if (Columns.Num() != 15)
			{
				continue;
			}

			const int32 RecipeId = FCString::Atoi(*Columns[0]);
			FMT2RefinementRecipe& Recipe = Recipes.Add(RecipeId);
			for (int32 MaterialIndex = 0; MaterialIndex < 5; ++MaterialIndex)
			{
				const int32 Vnum = FCString::Atoi(*Columns[1 + MaterialIndex * 2]);
				const int32 Count = FCString::Atoi(*Columns[2 + MaterialIndex * 2]);
				if (Vnum > 0 && Count > 0)
				{
					FMT2RefinementMaterial& Material = Recipe.Materials.AddDefaulted_GetRef();
					Material.ItemVnum = Vnum;
					Material.Count = Count;
				}
			}
			Recipe.YangCost = FCString::Atoi64(*Columns[11]);
			Recipe.SuccessPercent = FMath::Clamp(FCString::Atoi(*Columns[14]), 0, 100);
		}
		return Recipes;
	}

	TMap<int32, FParsedLootCrateGroup> LoadSpecialItemGroups(
		const FString& SourceRoot, const TMap<FString, int32>& ItemVnumsByName,
		TArray<FString>& Warnings)
	{
		const TArray<FString> Candidates = {
			SourceRoot / UMT2PathSettings::Path(TEXT("Part_locale_en_special_item_group")),
			SourceRoot / UMT2PathSettings::Path(TEXT("Part_special_item_group")),
			UMT2PathSettings::Path(TEXT("Legacy_special_item_group"))
		};
		FString Path;
		for (const FString& Candidate : Candidates)
		{
			if (IFileManager::Get().FileExists(*Candidate))
			{
				Path = Candidate;
				break;
			}
		}
		FString Content;
		if (Path.IsEmpty() || !FFileHelper::LoadFileToString(Content, *Path))
		{
			Warnings.Add(TEXT("special_item_group.txt was not found; loot-crate rewards were not configured."));
			return {};
		}

		const FString LegacyGoldName =
			TEXT("\u00B5\u00B7\u00B2\u00D9\u00B7\u00AF\u00B9\u00CC");
		const FString KoreanGoldName = TEXT("\uB3C8\uAFB8\uB7EC\uBBF8");
		const FString LegacyExperienceName =
			TEXT("\u00B0\u00E6\u00C7\u00E8\u00C4\u00A1");
		const FString KoreanExperienceName = TEXT("\uACBD\uD5D8\uCE58");
		TMap<int32, FParsedLootCrateGroup> Result;
		FParsedLootCrateGroup Current;
		int32 CurrentVnum = 0;
		auto Commit = [&]()
		{
			if (CurrentVnum > 0 && !Current.Rewards.IsEmpty())
			{
				Result.Add(CurrentVnum, MoveTemp(Current));
			}
			Current = FParsedLootCrateGroup();
			CurrentVnum = 0;
		};

		TArray<FString> Lines;
		Content.ParseIntoArrayLines(Lines, false);
		int32 UnsupportedSpecialRows = 0;
		for (FString Line : Lines)
		{
			Line.TrimStartAndEndInline();
			if (Line.IsEmpty() || Line.StartsWith(TEXT("#")) || Line.StartsWith(TEXT("Group")))
			{
				continue;
			}
			if (Line == TEXT("{"))
			{
				Commit();
				continue;
			}
			if (Line == TEXT("}"))
			{
				Commit();
				continue;
			}
			TArray<FString> Tokens;
			Line.ParseIntoArrayWS(Tokens);
			if (Tokens.Num() < 2)
			{
				continue;
			}
			if (Tokens[0].Equals(TEXT("Vnum"), ESearchCase::IgnoreCase))
			{
				CurrentVnum = FCString::Atoi(*Tokens[1]);
				continue;
			}
			if (Tokens[0].Equals(TEXT("Type"), ESearchCase::IgnoreCase))
			{
				Current.bIndependentRolls = Tokens[1].Equals(TEXT("pct"), ESearchCase::IgnoreCase);
				continue;
			}
			if (!Tokens[0].IsNumeric() || Tokens.Num() < 4)
			{
				continue;
			}

			EMT2LootCrateRewardKind RewardKind = EMT2LootCrateRewardKind::Item;
			int32 RewardVnum = 0;
			bool bNonAsciiRewardName = false;
			for (const TCHAR Character : Tokens[1])
			{
				if (Character > 127)
				{
					bNonAsciiRewardName = true;
					break;
				}
			}
			if (Tokens[1].IsNumeric())
			{
				RewardVnum = FCString::Atoi(*Tokens[1]);
			}
			else if (Tokens[1].Equals(TEXT("gold"), ESearchCase::IgnoreCase) ||
				Tokens[1] == LegacyGoldName || Tokens[1] == KoreanGoldName)
			{
				RewardKind = EMT2LootCrateRewardKind::Yang;
			}
			else if (Tokens[1].Equals(TEXT("exp"), ESearchCase::IgnoreCase) ||
				Tokens[1] == LegacyExperienceName || Tokens[1] == KoreanExperienceName)
			{
				RewardKind = EMT2LootCrateRewardKind::Experience;
			}
			else if (const int32* NamedVnum = ItemVnumsByName.Find(Tokens[1].ToLower()))
			{
				RewardVnum = *NamedVnum;
			}
			else if (bNonAsciiRewardName)
			{
				// This legacy table has exactly two localized pseudo-items: money and experience.
				// Money was handled above, so the remaining localized token is experience.
				RewardKind = EMT2LootCrateRewardKind::Experience;
			}
			else
			{
				const FString SpecialName = Tokens[1].ToLower();
				if (SpecialName == TEXT("mob") || SpecialName == TEXT("group") ||
					SpecialName == TEXT("slow") || SpecialName == TEXT("drain_hp") ||
					SpecialName == TEXT("poison"))
				{
					++UnsupportedSpecialRows;
					continue;
				}
				// The legacy Korean EXP token may arrive as narrow replacement characters,
				// depending on the host ANSI codepage. All other named entries resolved above.
				RewardKind = EMT2LootCrateRewardKind::Experience;
			}

			FMT2ImportedLootCrateReward& Reward = Current.Rewards.AddDefaulted_GetRef();
			Reward.Kind = RewardKind;
			Reward.Vnum = RewardVnum;
			Reward.Count = FMath::Max<int64>(FCString::Atoi64(*Tokens[2]), 1);
			Reward.Weight = FMath::Max(FCString::Atoi(*Tokens[3]), 0);
			Reward.RarePercent = Tokens.Num() > 4
				? FMath::Clamp(FCString::Atoi(*Tokens[4]), 0, 100) : 0;
		}
		Commit();
		Warnings.Add(FString::Printf(
			TEXT("Loaded %d loot-crate reward groups from %s."),
			Result.Num(), *Path));
		if (UnsupportedSpecialRows > 0)
		{
			Warnings.Add(FString::Printf(
				TEXT("Skipped %d unsupported special_item_group outcomes (mob/group or status effects)."),
				UnsupportedSpecialRows));
		}
		return Result;
	}

	TArray<FString> ParseSqlTupleFields(const FString& Line)
	{
		TArray<FString> Fields;
		int32 Start = INDEX_NONE;
		int32 End = INDEX_NONE;
		if (!Line.FindChar(TEXT('('), Start) || !Line.FindLastChar(TEXT(')'), End) || End <= Start)
		{
			return Fields;
		}
		FString Field;
		bool bInQuote = false;
		for (int32 Index = Start + 1; Index < End; ++Index)
		{
			const TCHAR Character = Line[Index];
			if (Character == TEXT('\\') && bInQuote && Index + 1 < End)
			{
				Field.AppendChar(Line[++Index]);
			}
			else if (Character == TEXT('\''))
			{
				bInQuote = !bInQuote;
				Field.AppendChar(Character);
			}
			else if (Character == TEXT(',') && !bInQuote)
			{
				Fields.Add(Field.TrimStartAndEnd());
				Field.Reset();
			}
			else
			{
				Field.AppendChar(Character);
			}
		}
		Fields.Add(Field.TrimStartAndEnd());
		return Fields;
	}

	TMap<int32, int32> LoadServerAddonTypes(const FString& SourceRoot, TArray<FString>& Warnings)
	{
		TMap<int32, int32> Result;
		const FString DatabaseDumpPath = SourceRoot / UMT2PathSettings::Path(TEXT("Part_db"));
		FString Content;
		if (!FFileHelper::LoadFileToString(Content, *DatabaseDumpPath))
		{
			Warnings.Add(TEXT("db.sql was not found; item addon_type values remain designer-configurable."));
			return Result;
		}
		TArray<FString> Lines;
		Content.ParseIntoArrayLines(Lines, false);
		bool bReadingItemProto = false;
		for (const FString& Line : Lines)
		{
			if (Line.Contains(TEXT("REPLACE INTO `item_proto`")))
			{
				bReadingItemProto = true;
				continue;
			}
			if (bReadingItemProto && Line.Contains(TEXT("ALTER TABLE `item_proto` ENABLE KEYS")))
			{
				bReadingItemProto = false;
				continue;
			}
			if (!bReadingItemProto || !Line.TrimStart().StartsWith(TEXT("(")))
			{
				continue;
			}
			const TArray<FString> Fields = ParseSqlTupleFields(Line);
			// db.sql item_proto schema: vnum is field 0 and addon_type is field 38.
			if (Fields.Num() > 38)
			{
				Result.Add(FCString::Atoi(*Fields[0]), FCString::Atoi(*Fields[38]));
			}
		}
		return Result;
	}

	FString ReadQuotedValue(const FString& Line)
	{
		int32 FirstQuote = INDEX_NONE;
		int32 LastQuote = INDEX_NONE;
		if (Line.FindChar(TEXT('"'), FirstQuote) && Line.FindLastChar(TEXT('"'), LastQuote) && LastQuote > FirstQuote)
		{
			return Line.Mid(FirstQuote + 1, LastQuote - FirstQuote - 1);
		}
		return FString();
	}

	FString ResolveImportedAssetContentPath(const FString& BaseDirectory, const FString& AssetPath)
	{
		FString ContentPath = AssetPath;
		ContentPath.ReplaceInline(TEXT("\\"), TEXT("/"));
		if (!ContentPath.Contains(TEXT(":")) && !BaseDirectory.IsEmpty())
		{
			ContentPath = BaseDirectory / ContentPath;
		}
		ContentPath.ReplaceInline(TEXT("\\"), TEXT("/"));
		FPaths::CollapseRelativeDirectories(ContentPath);
		if (ContentPath.Len() > 2 && ContentPath[1] == TEXT(':'))
		{
			ContentPath.RightChopInline(3);
		}
		while (ContentPath.StartsWith(TEXT("/")))
		{
			ContentPath.RightChopInline(1);
		}
		return ContentPath;
	}

	FString ResolveImportedSkeletalMeshPath(
		const FString& DestinationRoot,
		const FString& BaseDirectory,
		const FString& ModelPath)
	{
		const FString ContentPath = ResolveImportedAssetContentPath(BaseDirectory, ModelPath);

		FMT2AssetRecord Record;
		Record.ContentPath = ContentPath;
		const FString BasePackagePath = FMT2AssetScanner::BuildContentPackagePath(DestinationRoot, Record);
		const FString BaseName = FMT2AssetScanner::SanitizePackagePathSegment(FPaths::GetBaseFilename(ContentPath));
		const FString AssetName = TEXT("SK_") + BaseName;
		const TArray<FString> Candidates = {
			BasePackagePath / AssetName + TEXT(".") + AssetName,
			BasePackagePath / UMT2PathSettings::Path(TEXT("Part_SkeletalMeshes")) / AssetName + TEXT(".") + AssetName,
			BasePackagePath / BaseName / UMT2PathSettings::Path(TEXT("Part_SkeletalMeshes")) / AssetName + TEXT(".") + AssetName
		};
		for (const FString& Candidate : Candidates)
		{
			if (FPackageName::DoesPackageExist(FPackageName::ObjectPathToPackageName(Candidate)))
			{
				return Candidate;
			}
		}
		return FString();
	}

	FString BuildImportedTextureObjectPath(
		const FString& DestinationRoot,
		const FString& BaseDirectory,
		const FString& TexturePath)
	{
		FMT2AssetRecord Record;
		Record.Kind = EMT2AssetKind::Texture;
		Record.ContentPath = ResolveImportedAssetContentPath(BaseDirectory, TexturePath);
		return FMT2AssetScanner::BuildContentObjectPath(DestinationRoot, Record);
	}

	void LoadArmorShapeFile(
		const FString& FilePath,
		const FString& DestinationRoot,
		uint8 Race,
		uint8 Sex,
		TMap<int32, TArray<FMT2ItemArmorVisualDefinition>>& OutVisuals,
		TArray<FString>& OutWarnings)
	{
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *FilePath))
		{
			OutWarnings.Add(FString::Printf(TEXT("Could not read armor shape table: %s"), *FilePath));
			return;
		}

		TArray<FString> Lines;
		Text.ParseIntoArrayLines(Lines, true);
		FString BaseModelPath;
		FString ShapePath;
		bool bInShapeData = false;
		struct FPendingShape
		{
			int32 ShapeIndex = INDEX_NONE;
			FString BaseDirectory;
			FString Model;
			FString SourceSkins[2];
			FString TargetSkins[2];
		};
		FPendingShape Pending;
		auto FlushPendingShape = [&]()
		{
			if (Pending.ShapeIndex == INDEX_NONE || Pending.Model.IsEmpty())
			{
				Pending = FPendingShape();
				return;
			}

			const FString MeshPath = ResolveImportedSkeletalMeshPath(
				DestinationRoot, Pending.BaseDirectory, Pending.Model);
			if (!MeshPath.IsEmpty())
			{
				FMT2ItemArmorVisualDefinition& Visual =
					OutVisuals.FindOrAdd(Pending.ShapeIndex).AddDefaulted_GetRef();
				Visual.Race = Race;
				Visual.Sex = Sex;
				Visual.MeshObjectPath = MeshPath;
				for (int32 SkinIndex = 0; SkinIndex < UE_ARRAY_COUNT(Pending.SourceSkins); ++SkinIndex)
				{
					if (!Pending.SourceSkins[SkinIndex].IsEmpty() &&
						!Pending.TargetSkins[SkinIndex].IsEmpty())
					{
						FMT2ItemArmorMaterialOverrideDefinition& Override =
							Visual.MaterialOverrides.AddDefaulted_GetRef();
						Override.SourceTextureObjectPath = BuildImportedTextureObjectPath(
							DestinationRoot, Pending.BaseDirectory, Pending.SourceSkins[SkinIndex]);
						Override.TargetTextureObjectPath = BuildImportedTextureObjectPath(
							DestinationRoot, Pending.BaseDirectory, Pending.TargetSkins[SkinIndex]);
					}
				}
			}
			Pending = FPendingShape();
		};
		for (FString Line : Lines)
		{
			Line.TrimStartAndEndInline();
			if (Line.StartsWith(TEXT("BaseModelFileName"), ESearchCase::IgnoreCase))
			{
				BaseModelPath = ReadQuotedValue(Line);
				BaseModelPath.ReplaceInline(TEXT("\\"), TEXT("/"));
				ShapePath = FPaths::GetPath(BaseModelPath);
			}
			else if (Line.Equals(TEXT("Group ShapeData"), ESearchCase::IgnoreCase))
			{
				bInShapeData = true;
			}
			else if (bInShapeData && Line.StartsWith(TEXT("PathName"), ESearchCase::IgnoreCase))
			{
				const FString Path = ReadQuotedValue(Line);
				if (!Path.IsEmpty())
				{
					ShapePath = Path;
					ShapePath.ReplaceInline(TEXT("\\"), TEXT("/"));
				}
			}
			else if (bInShapeData && Line.StartsWith(TEXT("SpecialPath"), ESearchCase::IgnoreCase))
			{
				const FString Path = ReadQuotedValue(Line);
				if (!Path.IsEmpty())
				{
					// CRaceData::LoadRaceData keeps SpecialPath as the active path for following
					// shape groups until another SpecialPath replaces it.
					ShapePath = Path;
					ShapePath.ReplaceInline(TEXT("\\"), TEXT("/"));
				}
			}
			else if (bInShapeData && Line.StartsWith(TEXT("ShapeIndex"), ESearchCase::IgnoreCase))
			{
				FlushPendingShape();
				TArray<FString> Tokens;
				Line.ParseIntoArrayWS(Tokens);
				Pending.ShapeIndex = Tokens.Num() > 1 ? FCString::Atoi(*Tokens.Last()) : INDEX_NONE;
				Pending.BaseDirectory = ShapePath;
			}
			else if (bInShapeData && Pending.ShapeIndex != INDEX_NONE &&
				Line.StartsWith(TEXT("Model"), ESearchCase::IgnoreCase))
			{
				Pending.Model = ReadQuotedValue(Line);
			}
			else if (bInShapeData && Pending.ShapeIndex != INDEX_NONE &&
				Line.StartsWith(TEXT("SourceSkin2"), ESearchCase::IgnoreCase))
			{
				Pending.SourceSkins[1] = ReadQuotedValue(Line);
			}
			else if (bInShapeData && Pending.ShapeIndex != INDEX_NONE &&
				Line.StartsWith(TEXT("TargetSkin2"), ESearchCase::IgnoreCase))
			{
				Pending.TargetSkins[1] = ReadQuotedValue(Line);
			}
			else if (bInShapeData && Pending.ShapeIndex != INDEX_NONE &&
				Line.StartsWith(TEXT("SourceSkin"), ESearchCase::IgnoreCase))
			{
				Pending.SourceSkins[0] = ReadQuotedValue(Line);
			}
			else if (bInShapeData && Pending.ShapeIndex != INDEX_NONE &&
				Line.StartsWith(TEXT("TargetSkin"), ESearchCase::IgnoreCase))
			{
				Pending.TargetSkins[0] = ReadQuotedValue(Line);
			}
		}
		FlushPendingShape();
	}

	TMap<int32, TArray<FMT2ItemArmorVisualDefinition>> LoadArmorShapeVisuals(
		const FString& SourceRoot,
		const FString& DestinationRoot,
		TArray<FString>& OutWarnings)
	{
		struct FShapeFile { const TCHAR* Name; uint8 Race; uint8 Sex; };
		static const FShapeFile ShapeFiles[] = {
			{TEXT("warrior_m.msm"), 0, 0}, {TEXT("warrior_w.msm"), 0, 1},
			{TEXT("assassin_m.msm"), 1, 0}, {TEXT("assassin_w.msm"), 1, 1},
			{TEXT("sura_m.msm"), 2, 0}, {TEXT("sura_w.msm"), 2, 1},
			{TEXT("shaman_m.msm"), 3, 0}, {TEXT("shaman_w.msm"), 3, 1}
		};
		TMap<int32, TArray<FMT2ItemArmorVisualDefinition>> Result;
		for (const FShapeFile& ShapeFile : ShapeFiles)
		{
			LoadArmorShapeFile(
				SourceRoot / ShapeFile.Name,
				DestinationRoot,
				ShapeFile.Race,
				ShapeFile.Sex,
				Result,
				OutWarnings);
		}
		return Result;
	}

	void LoadHairShapeFile(
		const FString& FilePath,
		const FString& DestinationRoot,
		uint8 Race,
		uint8 Sex,
		TMap<int32, TArray<FMT2ItemHairVisualDefinition>>& OutVisuals,
		TArray<FString>& OutWarnings)
	{
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *FilePath))
		{
			OutWarnings.Add(FString::Printf(TEXT("Could not read hair shape table: %s"), *FilePath));
			return;
		}

		FString HairPath;
		bool bInHairData = false;
		int32 HairIndex = INDEX_NONE;
		FString Model;
		FString TargetSkin;
		auto Flush = [&]()
		{
			if (HairIndex != INDEX_NONE && !Model.IsEmpty())
			{
				const FString MeshPath = ResolveImportedSkeletalMeshPath(
					DestinationRoot, HairPath, Model);
				if (!MeshPath.IsEmpty())
				{
					FMT2ItemHairVisualDefinition& Visual =
						OutVisuals.FindOrAdd(HairIndex).AddDefaulted_GetRef();
					Visual.Race = Race;
					Visual.Sex = Sex;
					Visual.MeshObjectPath = MeshPath;
					Visual.TextureObjectPath = BuildImportedTextureObjectPath(
						DestinationRoot, HairPath, TargetSkin);
				}
			}
			HairIndex = INDEX_NONE;
			Model.Reset();
			TargetSkin.Reset();
		};

		TArray<FString> Lines;
		Text.ParseIntoArrayLines(Lines, true);
		for (FString Line : Lines)
		{
			Line.TrimStartAndEndInline();
			if (Line.Equals(TEXT("Group HairData"), ESearchCase::IgnoreCase))
			{
				bInHairData = true;
			}
			else if (bInHairData && Line.StartsWith(TEXT("PathName"), ESearchCase::IgnoreCase))
			{
				HairPath = ReadQuotedValue(Line);
				HairPath.ReplaceInline(TEXT("\\"), TEXT("/"));
			}
			else if (bInHairData && Line.StartsWith(TEXT("HairIndex"), ESearchCase::IgnoreCase))
			{
				Flush();
				TArray<FString> Tokens;
				Line.ParseIntoArrayWS(Tokens);
				HairIndex = Tokens.Num() > 1 ? FCString::Atoi(*Tokens.Last()) : INDEX_NONE;
			}
			else if (bInHairData && HairIndex != INDEX_NONE &&
				Line.StartsWith(TEXT("Model"), ESearchCase::IgnoreCase))
			{
				Model = ReadQuotedValue(Line);
			}
			else if (bInHairData && HairIndex != INDEX_NONE &&
				Line.StartsWith(TEXT("TargetSkin"), ESearchCase::IgnoreCase))
			{
				TargetSkin = ReadQuotedValue(Line);
			}
		}
		Flush();
	}

	TMap<int32, TArray<FMT2ItemHairVisualDefinition>> LoadHairShapeVisuals(
		const FString& SourceRoot,
		const FString& DestinationRoot,
		TArray<FString>& OutWarnings)
	{
		struct FHairFile { const TCHAR* Name; uint8 Race; uint8 Sex; };
		static const FHairFile HairFiles[] = {
			{TEXT("warrior_m.msm"), 0, 0}, {TEXT("warrior_w.msm"), 0, 1},
			{TEXT("assassin_m.msm"), 1, 0}, {TEXT("assassin_w.msm"), 1, 1},
			{TEXT("sura_m.msm"), 2, 0}, {TEXT("sura_w.msm"), 2, 1},
			{TEXT("shaman_m.msm"), 3, 0}, {TEXT("shaman_w.msm"), 3, 1}
		};
		TMap<int32, TArray<FMT2ItemHairVisualDefinition>> Result;
		for (const FHairFile& HairFile : HairFiles)
		{
			LoadHairShapeFile(
				SourceRoot / HairFile.Name, DestinationRoot,
				HairFile.Race, HairFile.Sex, Result, OutWarnings);
		}
		return Result;
	}

	FString NormalizeItemDestinationRoot(const FString& DestinationRoot)
	{
		FString Result = DestinationRoot.IsEmpty() ? UMT2PathSettings::Path(TEXT("ImportDestinationRoot")) : DestinationRoot;
		Result.ReplaceInline(TEXT("\\"), TEXT("/"));
		while (Result.EndsWith(TEXT("/")))
		{
			Result.LeftChopInline(1);
		}
		return Result;
	}

	int32 FindArmorMaterialSlot(
		USkeletalMesh* Mesh,
		const FString& SourceTextureObjectPath)
	{
		if (!Mesh)
		{
			return INDEX_NONE;
		}

		UTexture2D* SourceTexture = LoadObject<UTexture2D>(nullptr, *SourceTextureObjectPath);
		FString SourceStem = FPackageName::ObjectPathToObjectName(SourceTextureObjectPath);
		SourceStem.RemoveFromStart(TEXT("T_"), ESearchCase::IgnoreCase);
		const TArray<FSkeletalMaterial>& Materials = Mesh->GetMaterials();
		for (int32 SlotIndex = 0; SlotIndex < Materials.Num(); ++SlotIndex)
		{
			UMaterialInterface* Material = Materials[SlotIndex].MaterialInterface;
			if (!Material)
			{
				continue;
			}

			UTexture* DiffuseTexture = nullptr;
			if (SourceTexture && Material->GetTextureParameterValue(
				FHashedMaterialParameterInfo(TEXT("Diffuse")), DiffuseTexture) &&
				DiffuseTexture == SourceTexture)
			{
				return SlotIndex;
			}

			if (!SourceStem.IsEmpty() &&
				(Material->GetName().Contains(SourceStem, ESearchCase::IgnoreCase) ||
				 Materials[SlotIndex].MaterialSlotName.ToString().Contains(SourceStem, ESearchCase::IgnoreCase) ||
				 Materials[SlotIndex].ImportedMaterialSlotName.ToString().Contains(SourceStem, ESearchCase::IgnoreCase)))
			{
				return SlotIndex;
			}
		}
		return INDEX_NONE;
	}

	UMaterialInstanceConstant* CreateArmorMaterialOverride(
		const FString& DestinationRoot,
		UMaterialInterface* Parent,
		UTexture2D* TargetTexture,
		TArray<UPackage*>& OutPackagesToSave)
	{
		if (!Parent || !TargetTexture)
		{
			return nullptr;
		}

		FString TextureStem = TargetTexture->GetName();
		TextureStem.RemoveFromStart(TEXT("T_"), ESearchCase::IgnoreCase);
		TextureStem = FMT2AssetScanner::SanitizePackagePathSegment(TextureStem);
		const FString Identity = Parent->GetPathName() + TEXT("|") + TargetTexture->GetPathName();
		const FString AssetName = FString::Printf(
			TEXT("MI_Armor_%s_%08x"), *TextureStem, FCrc::StrCrc32(*Identity));
		const FString PackageName = NormalizeItemDestinationRoot(DestinationRoot) /
			TEXT("Materials/ArmorOverrides") / AssetName;
		const FString ObjectPath = PackageName + TEXT(".") + AssetName;

		UMaterialInstanceConstant* Instance = LoadObject<UMaterialInstanceConstant>(nullptr, *ObjectPath);
		const bool bCreated = Instance == nullptr;
		UPackage* Package = bCreated ? CreatePackage(*PackageName) : Instance->GetOutermost();
		if (bCreated)
		{
			Instance = NewObject<UMaterialInstanceConstant>(
				Package, *AssetName, RF_Public | RF_Standalone | RF_Transactional);
		}
		if (!Instance || !Package)
		{
			return nullptr;
		}

		Instance->Modify();
		Instance->SetParentEditorOnly(Parent);
		Instance->SetTextureParameterValueEditorOnly(FMaterialParameterInfo(TEXT("Diffuse")), TargetTexture);
		Instance->PostEditChange();
		Package->MarkPackageDirty();
		if (bCreated)
		{
			FAssetRegistryModule::AssetCreated(Instance);
		}
		OutPackagesToSave.AddUnique(Package);
		return Instance;
	}

	void PopulateArmorMaterialOverrides(
		const FString& DestinationRoot,
		const FMT2ItemArmorVisualDefinition& DefinitionVisual,
		FMT2ArmorMeshVariant& Variant,
		TArray<UPackage*>& OutPackagesToSave)
	{
		USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, *DefinitionVisual.MeshObjectPath);
		if (!Mesh)
		{
			return;
		}

		for (const FMT2ItemArmorMaterialOverrideDefinition& DefinitionOverride : DefinitionVisual.MaterialOverrides)
		{
			if (DefinitionOverride.SourceTextureObjectPath.IsEmpty() ||
				DefinitionOverride.TargetTextureObjectPath.IsEmpty() ||
				DefinitionOverride.SourceTextureObjectPath.Equals(
					DefinitionOverride.TargetTextureObjectPath, ESearchCase::IgnoreCase))
			{
				continue;
			}

			const int32 MaterialSlotIndex = FindArmorMaterialSlot(
				Mesh, DefinitionOverride.SourceTextureObjectPath);
			if (!Mesh->GetMaterials().IsValidIndex(MaterialSlotIndex))
			{
				UE_LOG(LogTemp, Warning,
					TEXT("[MT2ItemImporter] Could not match armor source skin %s on %s."),
					*DefinitionOverride.SourceTextureObjectPath, *DefinitionVisual.MeshObjectPath);
				continue;
			}

			UTexture2D* TargetTexture = LoadObject<UTexture2D>(
				nullptr, *DefinitionOverride.TargetTextureObjectPath);
			UMaterialInterface* Parent = Mesh->GetMaterials()[MaterialSlotIndex].MaterialInterface;
			UMaterialInstanceConstant* OverrideMaterial = CreateArmorMaterialOverride(
				DestinationRoot, Parent, TargetTexture, OutPackagesToSave);
			if (!OverrideMaterial)
			{
				UE_LOG(LogTemp, Warning,
					TEXT("[MT2ItemImporter] Could not create armor material override %s for %s."),
					*DefinitionOverride.TargetTextureObjectPath, *DefinitionVisual.MeshObjectPath);
				continue;
			}

			FMT2ArmorMaterialOverride& Override = Variant.MaterialOverrides.AddDefaulted_GetRef();
			Override.MaterialSlotIndex = MaterialSlotIndex;
			Override.Material = OverrideMaterial;
		}
	}

	struct FItemListEntry
	{
		FString IconObjectPath;
		FString WorldMeshObjectPath;
	};

	// item_list.txt: Vnum <TAB> Category <TAB> IconRelativePath <TAB> ModelRelativePath(optional).
	// Icons and weapon meshes are already imported at /Game/ymir_work/icon/item/T_<stem> and
	// /Game/ymir_work/item/weapon/SM_<stem>, so the source file's own basename is the asset stem.
	bool LoadItemList(const FString& FilePath, TMap<int32, FItemListEntry>& OutEntries)
	{
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *FilePath))
		{
			return false;
		}
		TArray<FString> Lines;
		Text.ParseIntoArrayLines(Lines, true);
		for (const FString& Line : Lines)
		{
			TArray<FString> Tokens;
			Line.ParseIntoArray(Tokens, TEXT("\t"), false);
			if (Tokens.Num() < 3)
			{
				continue;
			}
			const int32 Vnum = FCString::Atoi(*Tokens[0]);
			if (Vnum == 0)
			{
				continue;
			}
			FItemListEntry Entry;
			const FString IconStem = FPaths::GetBaseFilename(Tokens[2]);
			if (!IconStem.IsEmpty())
			{
				Entry.IconObjectPath = UMT2PathSettings::Format(TEXT("ymir_work_icon_item_T_Name"), TEXT("%s%s"), *IconStem, *IconStem);
			}
			if (Tokens.Num() >= 4 && Tokens[3].Contains(TEXT("item/weapon")))
			{
				const FString MeshStem = FPaths::GetBaseFilename(Tokens[3]);
				if (!MeshStem.IsEmpty())
				{
					Entry.WorldMeshObjectPath = UMT2PathSettings::Format(TEXT("ymir_work_item_weapon_SM_Name"), TEXT("%s%s"), *MeshStem, *MeshStem);
				}
			}
			OutEntries.Add(Vnum, Entry);
		}
		return true;
	}

	// EItemTypes ordinals from the old client's common/enums.h.
	UClass* ResolveTemplateClass(int32 ItemType)
	{
		switch (ItemType)
		{
		case 1: return UMT2ItemWeaponTemplate::StaticClass();
		case 2: return UMT2ItemArmorTemplate::StaticClass();
		case 3: return UMT2ItemUseTemplate::StaticClass();
		case 4: return UMT2ItemAutoUseTemplate::StaticClass();
		case 5: return UMT2ItemMaterialTemplate::StaticClass();
		case 6: return UMT2ItemSpecialTemplate::StaticClass();
		case 7: return UMT2ItemToolTemplate::StaticClass();
		case 8: return UMT2ItemLotteryTemplate::StaticClass();
		case 9: return UMT2ItemGoldTemplate::StaticClass();
		case 10: return UMT2ItemMetinStoneTemplate::StaticClass();
		case 11: return UMT2ItemContainerTemplate::StaticClass();
		case 12: return UMT2ItemFishTemplate::StaticClass();
		case 13: return UMT2ItemRodTemplate::StaticClass();
		case 14: return UMT2ItemResourceTemplate::StaticClass();
		case 15: return UMT2ItemCampfireTemplate::StaticClass();
		case 16: return UMT2ItemUniqueTemplate::StaticClass();
		case 17: return UMT2ItemSkillBookTemplate::StaticClass();
		case 18: return UMT2ItemQuestTemplate::StaticClass();
		case 19: return UMT2ItemPolymorphTemplate::StaticClass();
		case 20: return UMT2ItemTreasureBoxTemplate::StaticClass();
		case 21: return UMT2ItemTreasureKeyTemplate::StaticClass();
		case 22: return UMT2ItemSkillForgetTemplate::StaticClass();
		case 23: return UMT2ItemGiftBoxTemplate::StaticClass();
		case 24: return UMT2ItemPickTemplate::StaticClass();
		case 25: return UMT2ItemHairTemplate::StaticClass();
		case 26: return UMT2ItemTotemTemplate::StaticClass();
		case 27: return UMT2ItemBlendTemplate::StaticClass();
		case 28: return UMT2ItemCostumeTemplate::StaticClass();
		case 29: return UMT2ItemDragonSoulTemplate::StaticClass();
		case 30: return UMT2ItemSpecialDragonSoulTemplate::StaticClass();
		case 31: return UMT2ItemExtractTemplate::StaticClass();
		case 32: return UMT2ItemSecondaryCoinTemplate::StaticClass();
		case 33: return UMT2ItemRingTemplate::StaticClass();
		case 34: return UMT2ItemBeltTemplate::StaticClass();
		default: return UMT2ItemSpecialTemplate::StaticClass();
		}
	}

	bool IsMobLureItem(int32 Vnum)
	{
		switch (Vnum)
		{
		case 39006: // Current tradeable Bravery Cape variant.
		case 70038: // Original Bravery Cape.
		case 70057: // Ramadan reward Bravery Cape.
		case 76007: // Reward-box Bravery Cape.
			return true;
		default:
			return false;
		}
	}

	UBlueprint* CreateItemBlueprint(
		const FString& DestinationRoot,
		const FMT2ItemImportRecord& Record,
		bool& bOutCreated,
		TArray<UPackage*>& OutPackagesToSave)
	{
		const FMT2ItemDefinition& Definition = Record.Definition;
		const FString Name = FString::Printf(TEXT("BP_Item_%05d"), Definition.Vnum);
		const FString PackageName = NormalizeItemDestinationRoot(DestinationRoot) / UMT2PathSettings::Path(TEXT("Relative_Items_Blueprints")) / Name;
		const FString ObjectPath = PackageName + TEXT(".") + Name;
		UClass* ParentClass = Definition.Vnum >= 50051 && Definition.Vnum <= 50053
			? UMT2HorseBookItemTemplate::StaticClass()
			: Record.MountVnum > 0
			? UMT2MountItemTemplate::StaticClass()
			: IsMobLureItem(Definition.Vnum)
			? UMT2MobLureItemTemplate::StaticClass()
			: Definition.ItemType == 3 && Definition.SubType == 10
			? UMT2ItemAutoRecoveryTemplate::StaticClass()
			: Definition.Vnum == 25040
			? UMT2ItemRefinementScrollTemplate::StaticClass()
			: Definition.Vnum == 50513
			? UMT2GrandMasterTrainingItemTemplate::StaticClass()
			: Definition.Vnum == 70102
				? UMT2KarmaRecoveryItemTemplate::StaticClass()
				: ResolveTemplateClass(Definition.ItemType);

		UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *ObjectPath);
		bOutCreated = (Blueprint == nullptr);
		if (!Blueprint)
		{
			UPackage* Package = CreatePackage(*PackageName);
			Blueprint = FKismetEditorUtilities::CreateBlueprint(
				ParentClass, Package, *Name, BPTYPE_Normal,
				UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass(), TEXT("MT2ItemImporter"));
			if (!Blueprint)
			{
				return nullptr;
			}
			// GeneratedClass/its CDO don't exist until the Blueprint has compiled at least once.
			FKismetEditorUtilities::CompileBlueprint(Blueprint);
			FAssetRegistryModule::AssetCreated(Blueprint);
		}
		else if (Blueprint->ParentClass != ParentClass)
		{
			Blueprint->ParentClass = ParentClass;
			FBlueprintEditorUtils::RefreshAllNodes(Blueprint);
			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
			FKismetEditorUtilities::CompileBlueprint(Blueprint);
		}

		if (UMT2ItemTemplate* Defaults = Cast<UMT2ItemTemplate>(Blueprint->GeneratedClass->GetDefaultObject()))
		{
			Defaults->Modify();
			Defaults->Vnum = Definition.Vnum;
			Defaults->InternalName = Definition.InternalName;
			Defaults->DisplayName = FText::FromString(Definition.DisplayName);
			Defaults->Description = FText::FromString(Definition.Description);
			Defaults->Icon = Definition.IconObjectPath.IsEmpty()
				? nullptr : TSoftObjectPtr<UTexture2D>(FSoftObjectPath(Definition.IconObjectPath));
			Defaults->WorldMesh = Definition.WorldMeshObjectPath.IsEmpty()
				? nullptr : TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(Definition.WorldMeshObjectPath));
			Defaults->InventorySize = FMath::Max(Definition.Size, 1);
			Defaults->MaxStackSize = (Definition.Flags & UMT2ItemTemplate::StackableFlag) != 0
				? UMT2ItemTemplate::MaximumStackSize : 1;
			Defaults->VnumRange = Definition.VnumRange;
			Defaults->Weight = Definition.Weight;
			Defaults->BuyPrice = Definition.BuyPrice;
			Defaults->SellPrice = Definition.SellPrice;
			Defaults->AntiFlags = Definition.AntiFlags;
			Defaults->Flags = Definition.Flags;
			Defaults->ImmuneFlags = Definition.ImmuneFlags;
			Defaults->RefinedVnum = Definition.RefinedVnum;
			Defaults->RefineSet = Definition.RefineSet;
			Defaults->PreviousRefinedVnum = Definition.PreviousRefinedVnum;
			Defaults->RefinementLevel = Definition.RefinementLevel;
			Defaults->RefinementRecipe = Definition.RefinementRecipe;
			Defaults->AlterToMagicItemPercent = Definition.AlterToMagicItemPercent;
			Defaults->Applies = Definition.Applies;
			Defaults->Limits = Definition.Limits;
			Defaults->DefaultInstanceData = Definition.DefaultInstanceData;
			Defaults->DefaultInstanceData.MetinSockets.Reset();
			if (UMT2MountItemTemplate* MountItem = Cast<UMT2MountItemTemplate>(Defaults))
			{
				const FString MountName = FString::Printf(TEXT("DA_Mount_%05d"), Record.MountVnum);
				const FString MountPath = NormalizeItemDestinationRoot(DestinationRoot) /
					TEXT("Logic/Mounts") / MountName + TEXT(".") + MountName;
				MountItem->MountDefinition = TSoftObjectPtr<UMT2MountDefinition>(FSoftObjectPath(MountPath));
				MountItem->RideDurationSeconds = Record.MountDurationSeconds;
				MountItem->MinimumPlayerLevel = Record.MountMinimumPlayerLevel;
				MountItem->bConsumeOnMount = Record.bConsumeMountItem;
			}
			if (UMT2HorseBookItemTemplate* HorseBook = Cast<UMT2HorseBookItemTemplate>(Defaults))
			{
				HorseBook->ManaCost = (Definition.Vnum - 50050) * 100;
				HorseBook->SummonSkillVnum = 131;
			}

			if (UMT2ItemEquipmentTemplate* Equipment = Cast<UMT2ItemEquipmentTemplate>(Defaults))
			{
				Equipment->WearFlags = Definition.WearFlags;
				Equipment->DefaultSilverSocketCount =
					FMath::Clamp(Definition.GainSocketPercent, 0, 3);
				Equipment->DefaultGoldSocketCount = 0;
				Equipment->BonusAddonType = Definition.BonusAddonType;
				Equipment->Specular = Definition.Specular;
				for (int32 SocketIndex = 0;
					SocketIndex < Equipment->DefaultSilverSocketCount; ++SocketIndex)
				{
					FMT2MetinSocket& Socket =
						Defaults->DefaultInstanceData.MetinSockets.AddDefaulted_GetRef();
					Socket.Type = EMT2MetinSocketType::Silver;
				}
				for (int32 SocketIndex = 0;
					SocketIndex < FMath::Min(Equipment->DefaultGoldSocketCount,
						3 - Equipment->DefaultSilverSocketCount); ++SocketIndex)
				{
					FMT2MetinSocket& Socket =
						Defaults->DefaultInstanceData.MetinSockets.AddDefaulted_GetRef();
					Socket.Type = EMT2MetinSocketType::Gold;
				}
			}
			if (UMT2ItemWeaponTemplate* Weapon = Cast<UMT2ItemWeaponTemplate>(Defaults))
			{
				Weapon->WeaponSubType = Definition.SubType;
				Weapon->MagicDamageMin = Definition.Values.IsValidIndex(1) ? Definition.Values[1] : 0;
				Weapon->MagicDamageMax = Definition.Values.IsValidIndex(2) ? Definition.Values[2] : 0;
				Weapon->PhysicalDamageMin = Definition.Values.IsValidIndex(3) ? Definition.Values[3] : 0;
				Weapon->PhysicalDamageMax = Definition.Values.IsValidIndex(4) ? Definition.Values[4] : 0;
				Weapon->RefinementDamage = Definition.Values.IsValidIndex(5) ? Definition.Values[5] : 0;
			}
			// USE / AUTOUSE items key their behaviour off the sub-type (USE_POTION, USE_ABILITY_UP, ...);
			// without this every use-item defaults to sub-type 0 and e.g. speed potions never trigger.
			if (UMT2ItemUseTemplate* Use = Cast<UMT2ItemUseTemplate>(Defaults))
			{
				Use->UseSubType = Definition.SubType;
				Use->AffectType = 0;
				Use->HealthRecovery = Definition.Values.IsValidIndex(0) ? Definition.Values[0] : 0;
				Use->ManaRecovery = Definition.Values.IsValidIndex(1) ? Definition.Values[1] : 0;
				Use->HealthRecoveryPercent = Definition.Values.IsValidIndex(3) ? Definition.Values[3] : 0;
				Use->ManaRecoveryPercent = Definition.Values.IsValidIndex(4) ? Definition.Values[4] : 0;
				if (Definition.SubType == 7)
				{
					Use->GrantedBonus = static_cast<EMT2ItemBonusType>(
						Definition.Values.IsValidIndex(0) ? Definition.Values[0] : 0);
					Use->BuffDurationSeconds =
						Definition.Values.IsValidIndex(1) ? Definition.Values[1] : 0;
					Use->GrantedBonusValue =
						Definition.Values.IsValidIndex(2) ? Definition.Values[2] : 0;
				}
				else if (Definition.SubType == 8)
				{
					// USE_AFFECT: Value0=affect id, Value1=EApplyTypes, Value2=magnitude,
					// Value3=duration. This covers the premium rings/gloves/medals generically.
					Use->AffectType = Definition.Values.IsValidIndex(0) ? Definition.Values[0] : 0;
					Use->GrantedBonus = static_cast<EMT2ItemBonusType>(
						Definition.Values.IsValidIndex(1) ? Definition.Values[1] : 0);
					Use->GrantedBonusValue =
						Definition.Values.IsValidIndex(2) ? Definition.Values[2] : 0;
					Use->BuffDurationSeconds =
						Definition.Values.IsValidIndex(3) ? Definition.Values[3] : 0;
				}
			}
			if (UMT2ItemAutoRecoveryTemplate* AutoRecovery =
				Cast<UMT2ItemAutoRecoveryTemplate>(Defaults))
			{
				AutoRecovery->RecoveryCapacity =
					Definition.Values.IsValidIndex(0) ? FMath::Max(Definition.Values[0], 0) : 0;
				AutoRecovery->Resource = Definition.Vnum >= 72727 && Definition.Vnum <= 72730
					? EMT2AutoRecoveryResource::Mana : EMT2AutoRecoveryResource::Health;
				Defaults->DefaultInstanceData.AutoRecoveryMaximumAmount = AutoRecovery->RecoveryCapacity;
				Defaults->DefaultInstanceData.AutoRecoveryRemainingAmount = AutoRecovery->RecoveryCapacity;
				Defaults->DefaultInstanceData.bAutoRecoveryActive = false;
			}
			else if (UMT2ItemAutoUseTemplate* AutoUse = Cast<UMT2ItemAutoUseTemplate>(Defaults))
			{
				AutoUse->AutoUseSubType = Definition.SubType;
			}
			if (UMT2MobLureItemTemplate* MobLure = Cast<UMT2MobLureItemTemplate>(Defaults))
			{
				MobLure->LureRadius = 5000.0f;
				MobLure->AggroChancePercent = 50;
			}
			if (UMT2ItemArmorTemplate* Armor = Cast<UMT2ItemArmorTemplate>(Defaults))
			{
				Armor->ArmorSubType = Definition.SubType;
				Armor->Defense = Definition.Values.IsValidIndex(1) ? Definition.Values[1] : 0;
				Armor->ArmorShapeId = Definition.Values.IsValidIndex(3) ? Definition.Values[3] : 0;
				Armor->RefinementDefense = Definition.Values.IsValidIndex(5) ? Definition.Values[5] : 0;
				Armor->ArmorMesh = nullptr;
				Armor->ArmorMeshVariants.Reset();
				for (const FMT2ItemArmorVisualDefinition& DefinitionVisual : Definition.ArmorVisuals)
				{
					FMT2ArmorMeshVariant& Variant = Armor->ArmorMeshVariants.AddDefaulted_GetRef();
					Variant.Race = static_cast<EMT2CharacterRace>(DefinitionVisual.Race);
					Variant.Sex = static_cast<EMT2CharacterSex>(DefinitionVisual.Sex);
					Variant.Mesh = TSoftObjectPtr<USkeletalMesh>(FSoftObjectPath(DefinitionVisual.MeshObjectPath));
					PopulateArmorMaterialOverrides(
						DestinationRoot, DefinitionVisual, Variant, OutPackagesToSave);
				}
			}
			if (UMT2ItemUniqueTemplate* Unique = Cast<UMT2ItemUniqueTemplate>(Defaults))
			{
				Unique->UniqueSubType = Definition.SubType;
			}
			if (UMT2ItemMetinStoneTemplate* MetinStone =
				Cast<UMT2ItemMetinStoneTemplate>(Defaults))
			{
				MetinStone->MetinSubType = Definition.SubType;
				MetinStone->CompatibleWearFlags = Definition.WearFlags;
				MetinStone->RequiredSocketGrade = FMath::Clamp(
					Definition.Values.IsValidIndex(2) ? Definition.Values[2] : 1, 1, 2);
				MetinStone->StoneCategory =
					Definition.Values.IsValidIndex(5) ? Definition.Values[5] : 0;
			}
			if (UMT2ItemSkillBookTemplate* SkillBook =
				Cast<UMT2ItemSkillBookTemplate>(Defaults))
			{
				SkillBook->SkillVnum =
					Definition.Values.IsValidIndex(0) ? Definition.Values[0] : 0;
			}
			if (UMT2ItemRodTemplate* Rod = Cast<UMT2ItemRodTemplate>(Defaults))
			{
				Rod->FishingDelayTenths = Definition.Values.IsValidIndex(0) ? Definition.Values[0] : 0;
				Rod->ImprovementPointsRequired =
					Definition.Values.IsValidIndex(2) ? Definition.Values[2] : 0;
				Rod->RefinementSuccessPercent =
					Definition.Values.IsValidIndex(3) ? Definition.Values[3] : 0;
			}
			if (UMT2ItemPickTemplate* Pick = Cast<UMT2ItemPickTemplate>(Defaults))
			{
				Pick->ImprovementPointsRequired =
					Definition.Values.IsValidIndex(2) ? Definition.Values[2] : 0;
				Pick->RefinementSuccessPercent =
					Definition.Values.IsValidIndex(3) ? Definition.Values[3] : 0;
			}
			if (UMT2ItemCostumeTemplate* Costume = Cast<UMT2ItemCostumeTemplate>(Defaults))
			{
				Costume->CostumeSubType = Definition.SubType;
				Costume->HairShapeId =
					Definition.Values.IsValidIndex(3) ? Definition.Values[3] : 0;
				Costume->HairMeshVariants.Reset();
				for (const FMT2ItemHairVisualDefinition& DefinitionVisual : Definition.HairVisuals)
				{
					FMT2HairMeshVariant& Variant =
						Costume->HairMeshVariants.AddDefaulted_GetRef();
					Variant.Race = static_cast<EMT2CharacterRace>(DefinitionVisual.Race);
					Variant.Sex = static_cast<EMT2CharacterSex>(DefinitionVisual.Sex);
					Variant.Mesh = TSoftObjectPtr<USkeletalMesh>(
						FSoftObjectPath(DefinitionVisual.MeshObjectPath));
					Variant.DiffuseTexture = TSoftObjectPtr<UTexture2D>(
						FSoftObjectPath(DefinitionVisual.TextureObjectPath));
				}
			}
		}

		// Recompiling is what actually bakes the CDO changes into the class, instead of leaving them
		// silently stuck on whatever the CDO held at the last compile (same fix as the mob importer).
		FKismetEditorUtilities::CompileBlueprint(Blueprint);
		Blueprint->MarkPackageDirty();
		return Blueprint;
	}

	UMT2MountDefinition* CreateMountDefinition(
		const FString& DestinationRoot, int32 MountVnum,
		TArray<UPackage*>& OutPackagesToSave, FString& OutError)
	{
		const FString Name = FString::Printf(TEXT("DA_Mount_%05d"), MountVnum);
		const FString PackageName = NormalizeItemDestinationRoot(DestinationRoot) /
			TEXT("Logic/Mounts") / Name;
		const FString ObjectPath = PackageName + TEXT(".") + Name;
		UMT2MountDefinition* Definition = LoadObject<UMT2MountDefinition>(nullptr, *ObjectPath);
		if (!Definition)
		{
			UPackage* Package = CreatePackage(*PackageName);
			Definition = NewObject<UMT2MountDefinition>(
				Package, *Name, RF_Public | RF_Standalone | RF_Transactional);
			if (!Definition)
			{
				OutError = FString::Printf(TEXT("Mount %d: definition asset could not be created."), MountVnum);
				return nullptr;
			}
			FAssetRegistryModule::AssetCreated(Definition);
		}

		const UMT2VnumRegistry* Registry = LoadObject<UMT2VnumRegistry>(
			nullptr, UMT2PathSettings::Path(TEXT("VnumRegistry")));
		const TSoftClassPtr<AMT2Mob>* MobClassPtr = Registry
			? Registry->GetMobClasses().Find(MountVnum) : nullptr;
		UClass* MobClass = MobClassPtr ? MobClassPtr->LoadSynchronous() : nullptr;
		const AMT2Mob* MobDefaults = MobClass ? Cast<AMT2Mob>(MobClass->GetDefaultObject()) : nullptr;
		if (!MobDefaults || !MobDefaults->GetMesh() || !MobDefaults->GetMesh()->GetSkeletalMeshAsset())
		{
			OutError = FString::Printf(
				TEXT("Mount %d: imported mob Blueprint is missing from the VNUM registry or has no mesh."),
				MountVnum);
			return nullptr;
		}

		Definition->Modify();
		Definition->Vnum = MountVnum;
		Definition->DisplayName = FText::FromString(MobDefaults->GetMobDisplayName());
		Definition->Mesh = MobDefaults->GetMesh()->GetSkeletalMeshAsset();
		Definition->AnimationClass = MobDefaults->GetMesh()->GetAnimClass();
		// Imported mob meshes already carry the Granny-to-UE axis correction on their component.
		// Preserve it when the same mesh is rendered as a mount child.
		Definition->MountRelativeTransform = MobDefaults->GetMesh()->GetRelativeTransform();
		Definition->MountRelativeTransform.SetLocation(FVector::ZeroVector);
		Definition->RiderRelativeTransform = FTransform::Identity;
		// mob_proto movement speed uses 100 as the normal player baseline. Mount rows are normally
		// 150, so preserve the source-defined 1.5x riding speed instead of leaving every mount at 1x.
		if (const UMT2CombatStatsComponent* CombatStats = MobDefaults->GetCombatStatsComponent())
		{
			Definition->MovementSpeedMultiplier =
				FMath::Max(static_cast<float>(CombatStats->GetBaseStats().MovementSpeed) / 100.0f, 0.01f);
		}
		Definition->MountKind = MountVnum >= 20101 && MountVnum <= 20109
			? EMT2MountKind::Horse : EMT2MountKind::SpecialMount;
		const bool bBasicHorse = MountVnum >= 20201 && MountVnum <= 20204;
		const bool bArmedHorse = MountVnum >= 20205 && MountVnum <= 20208;
		Definition->bCanAttack = !bBasicHorse;
		Definition->bCanUseHorseSkills = !bBasicHorse && !bArmedHorse;
		Definition->MarkPackageDirty();
		OutPackagesToSave.AddUnique(Definition->GetOutermost());
		return Definition;
	}

	UBlueprint* CreateLootTableBlueprint(
		const FString& DestinationRoot, int32 Vnum, TArray<UPackage*>& OutPackagesToSave)
	{
		const FString Name = FString::Printf(TEXT("LT_Item_%05d"), Vnum);
		const FString PackageName =
			NormalizeItemDestinationRoot(DestinationRoot) / UMT2PathSettings::Path(TEXT("Relative_Items_LootTables")) / Name;
		const FString ObjectPath = PackageName + TEXT(".") + Name;
		UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *ObjectPath);
		if (!Blueprint)
		{
			UPackage* Package = CreatePackage(*PackageName);
			Blueprint = FKismetEditorUtilities::CreateBlueprint(
				UMT2LootTable::StaticClass(), Package, *Name, BPTYPE_Normal,
				UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass(),
				TEXT("MT2ItemImporter"));
			if (!Blueprint)
			{
				return nullptr;
			}
			FKismetEditorUtilities::CompileBlueprint(Blueprint);
			FAssetRegistryModule::AssetCreated(Blueprint);
		}
		OutPackagesToSave.AddUnique(Blueprint->GetOutermost());
		return Blueprint;
	}
}

bool FMT2ItemImporter::Discover(
	const FString& SourceRoot,
	const FString& DestinationRoot,
	TArray<FMT2ItemImportRecord>& OutRecords,
	TArray<FString>& OutWarnings,
	FString& OutError)
{
	OutRecords.Reset();
	OutWarnings.Reset();
	OutError.Reset();

	const FString ProtoPath = SourceRoot / UMT2PathSettings::Path(TEXT("Part_locale_en_item_proto"));
	if (!FPaths::FileExists(ProtoPath))
	{
		OutError = FString::Printf(TEXT("Could not find item_proto under %s"), *SourceRoot);
		return false;
	}

	FMT2ItemProtoReadResult ReadResult;
	FString ReadError;
	if (!FMT2ItemProtoReader::Read(ProtoPath, ReadResult, ReadError))
	{
		OutError = ReadError;
		return false;
	}
	OutWarnings.Append(ReadResult.Warnings);
	const TMap<int32, int32> AddonTypes = LoadServerAddonTypes(SourceRoot, OutWarnings);
	const TMap<int32, FMT2RefinementRecipe> RefinementRecipes =
		LoadRefinementRecipes(SourceRoot, OutWarnings);
	TMap<int32, int32> PreviousRefinedVnums;
	for (const FMT2ItemDefinition& Definition : ReadResult.Definitions)
	{
		if (Definition.RefinedVnum > 0)
		{
			PreviousRefinedVnums.Add(Definition.RefinedVnum, Definition.Vnum);
		}
	}

	TMap<int32, FItemListEntry> ItemList;
	const FString ListPath = SourceRoot / UMT2PathSettings::Path(TEXT("Part_locale_en_item_list"));
	if (!LoadItemList(ListPath, ItemList))
	{
		OutWarnings.Add(FString::Printf(
			TEXT("Could not read item_list.txt under %s; icons/meshes will be unresolved."), *SourceRoot));
	}

	// locale/en/itemdesc.txt: vnum \t English name \t description. The proto's LocaleName column
	// is terse/abbreviated; itemdesc carries the proper display strings, so it wins when present.
	struct FItemDescEntry
	{
		FString Name;
		FString Description;
	};
	TMap<int32, FItemDescEntry> ItemDescs;
	{
		FString DescContent;
		const FString DescPath = SourceRoot / UMT2PathSettings::Path(TEXT("Part_locale_en_itemdesc"));
		if (FFileHelper::LoadFileToString(DescContent, *DescPath))
		{
			TArray<FString> Lines;
			DescContent.ParseIntoArrayLines(Lines);
			for (const FString& Line : Lines)
			{
				TArray<FString> Columns;
				Line.ParseIntoArray(Columns, TEXT("\t"), /*bCullEmpty=*/false);
				if (Columns.Num() >= 2)
				{
					FItemDescEntry& Entry = ItemDescs.Add(FCString::Atoi(*Columns[0]));
					Entry.Name = Columns[1].TrimStartAndEnd();
					Entry.Description = Columns.IsValidIndex(2) ? Columns[2].TrimStartAndEnd() : FString();
				}
			}
		}
		else
		{
			OutWarnings.Add(FString::Printf(
				TEXT("Could not read %s; item names fall back to the proto locale column."), *DescPath));
		}
	}

	OutRecords.Reserve(ReadResult.Definitions.Num());
	const TMap<int32, TArray<FMT2ItemArmorVisualDefinition>> ArmorVisualsByShape =
		LoadArmorShapeVisuals(SourceRoot, DestinationRoot, OutWarnings);
	const TMap<int32, TArray<FMT2ItemHairVisualDefinition>> HairVisualsByShape =
		LoadHairShapeVisuals(SourceRoot, DestinationRoot, OutWarnings);
	int32 MappedBodyArmorCount = 0;
	int32 MappedCostumeHairCount = 0;
	for (FMT2ItemDefinition& Definition : ReadResult.Definitions)
	{
		Definition.BonusAddonType = AddonTypes.FindRef(Definition.Vnum);
		Definition.RefinementLevel = Definition.Vnum % 10;
		Definition.PreviousRefinedVnum = Definition.RefinementLevel > 0
			? PreviousRefinedVnums.FindRef(Definition.Vnum) : 0;
		if (const FMT2RefinementRecipe* Recipe = RefinementRecipes.Find(Definition.RefineSet))
		{
			Definition.RefinementRecipe = *Recipe;
		}
		if (const FItemListEntry* Entry = ItemList.Find(Definition.Vnum))
		{
			Definition.IconObjectPath = Entry->IconObjectPath;
			Definition.WorldMeshObjectPath = Entry->WorldMeshObjectPath;
		}
		if (const FItemDescEntry* Desc = ItemDescs.Find(Definition.Vnum))
		{
			if (!Desc->Name.IsEmpty())
			{
				Definition.DisplayName = Desc->Name;
			}
			Definition.Description = Desc->Description;
		}
		// ARMOR_BODY uses Value3 as the legacy shape index passed to CInstanceBase::SetShape.
		if (Definition.ItemType == 2 && Definition.SubType == 0 && Definition.Values.IsValidIndex(3))
		{
			if (const TArray<FMT2ItemArmorVisualDefinition>* Visuals = ArmorVisualsByShape.Find(Definition.Values[3]))
			{
				Definition.ArmorVisuals = *Visuals;
				MappedBodyArmorCount += Definition.ArmorVisuals.IsEmpty() ? 0 : 1;
			}
		}
		// COSTUME_HAIR uses Value3 as the HairIndex in each race's HairData table.
		if (Definition.ItemType == 28 && Definition.SubType == 1 && Definition.Values.IsValidIndex(3))
		{
			if (const TArray<FMT2ItemHairVisualDefinition>* Visuals =
				HairVisualsByShape.Find(Definition.Values[3]))
			{
				Definition.HairVisuals = *Visuals;
				MappedCostumeHairCount += Definition.HairVisuals.IsEmpty() ? 0 : 1;
			}
		}
		FMT2ItemImportRecord& Record = OutRecords.AddDefaulted_GetRef();
		Record.Definition = MoveTemp(Definition);
	}

	TMap<FString, int32> ItemVnumsByName;
	for (const FMT2ItemImportRecord& Record : OutRecords)
	{
		if (!Record.Definition.InternalName.IsEmpty())
		{
			ItemVnumsByName.Add(Record.Definition.InternalName.ToLower(), Record.Definition.Vnum);
		}
		if (!Record.Definition.DisplayName.IsEmpty())
		{
			ItemVnumsByName.Add(Record.Definition.DisplayName.ToLower(), Record.Definition.Vnum);
		}
	}
	TMap<int32, FParsedLootCrateGroup> LootGroups =
		LoadSpecialItemGroups(SourceRoot, ItemVnumsByName, OutWarnings);
	const TMap<int32, FParsedMountItem> MountItems = LoadMountItems(SourceRoot, OutWarnings);
	int32 ConfiguredLootCrates = 0;
	int32 ConfiguredMountItems = 0;
	for (FMT2ItemImportRecord& Record : OutRecords)
	{
		// The classic horse licences are quest items, so item_proto does not identify their horse.
		// horse_rider.cpp maps grades 1/2/3 to races 20101/20104/20107.
		if (Record.Definition.Vnum >= 50051 && Record.Definition.Vnum <= 50053)
		{
			Record.MountVnum = 20101 + (Record.Definition.Vnum - 50051) * 3;
		}
		if (FParsedLootCrateGroup* Group = LootGroups.Find(Record.Definition.Vnum))
		{
			Record.LootCrateRewards = MoveTemp(Group->Rewards);
			Record.bLootCrateIndependentRolls = Group->bIndependentRolls;
			++ConfiguredLootCrates;
		}
		if (const FParsedMountItem* MountItem = MountItems.Find(Record.Definition.Vnum))
		{
			Record.MountVnum = MountItem->MountVnum;
			Record.MountDurationSeconds = MountItem->DurationSeconds;
			Record.MountMinimumPlayerLevel = MountItem->MinimumPlayerLevel;
			Record.bConsumeMountItem = MountItem->bConsumeItem;
			++ConfiguredMountItems;
		}
	}

	OutWarnings.Add(FString::Printf(TEXT("Decoded %d item_proto rows."), OutRecords.Num()));
	OutWarnings.Add(FString::Printf(
		TEXT("Matched special_item_group rewards to %d imported loot crates."),
		ConfiguredLootCrates));
	OutWarnings.Add(FString::Printf(
		TEXT("Matched ride quest data to %d imported mount items."), ConfiguredMountItems));
	OutWarnings.Add(FString::Printf(
		TEXT("Resolved %d legacy armor shapes and %d body armor visual definitions."),
		ArmorVisualsByShape.Num(), MappedBodyArmorCount));
	OutWarnings.Add(FString::Printf(
		TEXT("Resolved %d legacy hair shapes and %d costume-hair visual definitions."),
		HairVisualsByShape.Num(), MappedCostumeHairCount));
	OutWarnings.Add(FString::Printf(
		TEXT("Loaded %d refinement recipes from db.sql."), RefinementRecipes.Num()));
	return !OutRecords.IsEmpty();
}

bool FMT2ItemImporter::Import(
	const TArray<FMT2ItemImportRecord>& Records,
	const FString& DestinationRoot,
	TFunctionRef<bool()> ShouldCancel,
	FMT2ItemImportResult& OutResult)
{
	OutResult = FMT2ItemImportResult();
	FScopedSlowTask Progress(
		static_cast<float>(Records.Num()), NSLOCTEXT("MT2ItemImporter", "Progress", "Creating Metin2 items..."));
	Progress.MakeDialog(true);
	TArray<UPackage*> PackagesToSave;
	TSet<int32> CreatedMountDefinitions;
	const UMT2VnumRegistry* VnumRegistry = LoadObject<UMT2VnumRegistry>(
		nullptr, UMT2PathSettings::Path(TEXT("VnumRegistry")));
	TArray<int32> RegisteredItemVnums;
	if (VnumRegistry)
	{
		VnumRegistry->GetItemTemplates().GenerateKeyArray(RegisteredItemVnums);
		RegisteredItemVnums.Sort();
	}
	auto ResolveRewardBlueprint = [&](int32 RewardVnum) -> UBlueprint*
	{
		const FString RewardName = FString::Printf(TEXT("BP_Item_%05d"), RewardVnum);
		const FString RewardObjectPath = NormalizeItemDestinationRoot(DestinationRoot) /
			UMT2PathSettings::Path(TEXT("Relative_Items_Blueprints")) / RewardName + TEXT(".") + RewardName;
		UBlueprint* ExactBlueprint = nullptr;
		if (FPackageName::DoesPackageExist(FPackageName::ObjectPathToPackageName(RewardObjectPath)))
		{
			ExactBlueprint = LoadObject<UBlueprint>(nullptr, *RewardObjectPath);
		}
		if (ExactBlueprint)
		{
			return ExactBlueprint;
		}
		if (!VnumRegistry)
		{
			return nullptr;
		}

		int32 Lower = 0;
		int32 Upper = RegisteredItemVnums.Num();
		while (Lower < Upper)
		{
			const int32 Middle = Lower + (Upper - Lower) / 2;
			if (RegisteredItemVnums[Middle] <= RewardVnum)
			{
				Lower = Middle + 1;
			}
			else
			{
				Upper = Middle;
			}
		}
		if (Lower <= 0)
		{
			return nullptr;
		}
		const TSoftClassPtr<UMT2ItemTemplate>* Candidate =
			VnumRegistry->GetItemTemplates().Find(RegisteredItemVnums[Lower - 1]);
		UClass* CandidateClass = Candidate ? Candidate->LoadSynchronous() : nullptr;
		const UMT2ItemTemplate* CandidateDefaults = CandidateClass
			? CandidateClass->GetDefaultObject<UMT2ItemTemplate>() : nullptr;
		if (!CandidateDefaults || CandidateDefaults->VnumRange <= 0 ||
			RewardVnum > CandidateDefaults->Vnum + CandidateDefaults->VnumRange)
		{
			return nullptr;
		}
		return Cast<UBlueprint>(CandidateClass->ClassGeneratedBy);
	};
	for (const FMT2ItemImportRecord& Record : Records)
	{
		if (Record.MountVnum <= 0 || CreatedMountDefinitions.Contains(Record.MountVnum))
		{
			continue;
		}
		CreatedMountDefinitions.Add(Record.MountVnum);
		FString MountError;
		if (!CreateMountDefinition(
			DestinationRoot, Record.MountVnum, PackagesToSave, MountError))
		{
			OutResult.Errors.Add(MoveTemp(MountError));
		}
	}

	for (const FMT2ItemImportRecord& Record : Records)
	{
		Progress.EnterProgressFrame(1.0f, FText::Format(
			NSLOCTEXT("MT2ItemImporter", "Item", "Creating item {0}: {1}"),
			FText::AsNumber(Record.Definition.Vnum), FText::FromString(Record.Definition.DisplayName)));
		if (Progress.ShouldCancel() || ShouldCancel())
		{
			break;
		}

		bool bCreated = false;
		UBlueprint* Blueprint = CreateItemBlueprint(
			DestinationRoot, Record, bCreated, PackagesToSave);
		if (!Blueprint)
		{
			OutResult.Errors.Add(FString::Printf(TEXT("%d: item Blueprint could not be created."), Record.Definition.Vnum));
			++OutResult.Skipped;
			continue;
		}
		bCreated ? ++OutResult.ItemBlueprintsCreated : ++OutResult.ItemBlueprintsUpdated;
		PackagesToSave.AddUnique(Blueprint->GetOutermost());
	}

	// Legacy ITEM_TREASURE_BOX and ITEM_TREASURE_KEY pair through Value0. Resolve this only after
	// every item Blueprint exists so imports remain independent of source row ordering.
	TMap<int32, const FMT2ItemImportRecord*> TreasureKeysByLockId;
	for (const FMT2ItemImportRecord& Record : Records)
	{
		if (Record.Definition.ItemType == 21 && Record.Definition.Values.IsValidIndex(0))
		{
			TreasureKeysByLockId.FindOrAdd(Record.Definition.Values[0], &Record);
		}
	}
	for (const FMT2ItemImportRecord& Record : Records)
	{
		if (Record.Definition.ItemType != 20 || !Record.Definition.Values.IsValidIndex(0))
		{
			continue;
		}
		UBlueprint* CrateBlueprint = LoadObject<UBlueprint>(
			nullptr, *BuildBlueprintObjectPath(DestinationRoot, Record.Definition));
		UMT2ItemLootCrateTemplate* CrateDefaults = CrateBlueprint
			? Cast<UMT2ItemLootCrateTemplate>(CrateBlueprint->GeneratedClass->GetDefaultObject())
			: nullptr;
		if (!CrateDefaults)
		{
			continue;
		}

		TSubclassOf<UMT2ItemTemplate> RequiredKey;
		if (const FMT2ItemImportRecord* const* KeyRecord =
			TreasureKeysByLockId.Find(Record.Definition.Values[0]))
		{
			if (UBlueprint* KeyBlueprint = LoadObject<UBlueprint>(
				nullptr, *BuildBlueprintObjectPath(DestinationRoot, (*KeyRecord)->Definition)))
			{
				RequiredKey = KeyBlueprint->GeneratedClass;
			}
		}
		CrateDefaults->Modify();
		CrateDefaults->RequiredKeyTemplate = RequiredKey;
		FKismetEditorUtilities::CompileBlueprint(CrateBlueprint);
		CrateBlueprint->MarkPackageDirty();
		PackagesToSave.AddUnique(CrateBlueprint->GetOutermost());
	}

	for (const FMT2ItemImportRecord& Record : Records)
	{
		if (Record.Definition.ItemType != 20 && Record.Definition.ItemType != 23)
		{
			continue;
		}
		UBlueprint* CrateBlueprint = LoadObject<UBlueprint>(
			nullptr, *BuildBlueprintObjectPath(DestinationRoot, Record.Definition));
		UMT2ItemLootCrateTemplate* CrateDefaults = CrateBlueprint
			? Cast<UMT2ItemLootCrateTemplate>(CrateBlueprint->GeneratedClass->GetDefaultObject())
			: nullptr;
		if (!CrateDefaults)
		{
			continue;
		}

		UBlueprint* LootBlueprint = CreateLootTableBlueprint(
			DestinationRoot, Record.Definition.Vnum, PackagesToSave);
		UMT2LootTable* LootDefaults = LootBlueprint && LootBlueprint->GeneratedClass
			? Cast<UMT2LootTable>(LootBlueprint->GeneratedClass->GetDefaultObject()) : nullptr;
		if (!LootDefaults)
		{
			OutResult.Errors.Add(FString::Printf(
				TEXT("%d: loot table Blueprint could not be created."), Record.Definition.Vnum));
			continue;
		}

		LootDefaults->Modify();
		LootDefaults->Rewards.Reset();
		for (const FMT2ImportedLootCrateReward& ImportedReward : Record.LootCrateRewards)
		{
			FMT2LootTableEntry Entry;
			Entry.Kind = ImportedReward.Kind;
			Entry.MinimumAmount = ImportedReward.Count;
			Entry.MaximumAmount = ImportedReward.Count;
			Entry.RollMode = Record.bLootCrateIndependentRolls
				? EMT2LootRollMode::IndependentChance : EMT2LootRollMode::WeightedGroup;
			Entry.ChancePercent = ImportedReward.Weight;
			Entry.Weight = ImportedReward.Weight;
			Entry.KillAverage = 1;
			Entry.RareAttributeChancePercent = ImportedReward.RarePercent;
			if (ImportedReward.Kind != EMT2LootCrateRewardKind::Item)
			{
				LootDefaults->Rewards.Add(MoveTemp(Entry));
				continue;
			}
			// A filtered import normally contains only the selected loot crates. Reward items may already
			// exist in the project without being present in Records, so resolve their stable imported path
			// directly instead of incorrectly discarding every unselected reward.
			UBlueprint* RewardBlueprint = ResolveRewardBlueprint(ImportedReward.Vnum);
			if (!RewardBlueprint || !RewardBlueprint->GeneratedClass)
			{
				OutResult.Errors.Add(FString::Printf(
					TEXT("%d: reward item %d has no imported Blueprint or matching VNUM range."),
					Record.Definition.Vnum, ImportedReward.Vnum));
				continue;
			}

			Entry.ItemTemplate =
				TSubclassOf<UMT2ItemTemplate>(RewardBlueprint->GeneratedClass.Get());
			Entry.ItemVnum = ImportedReward.Vnum;
			Entry.ItemData.Count = static_cast<int32>(FMath::Clamp<int64>(
				ImportedReward.Count, 1, MAX_int32));
			LootDefaults->Rewards.Add(MoveTemp(Entry));
		}
		FKismetEditorUtilities::CompileBlueprint(LootBlueprint);
		LootBlueprint->MarkPackageDirty();

		CrateDefaults->Modify();
		CrateDefaults->LootTable =
			TSubclassOf<UMT2LootTable>(LootBlueprint->GeneratedClass.Get());
		FKismetEditorUtilities::CompileBlueprint(CrateBlueprint);
		CrateBlueprint->MarkPackageDirty();
		PackagesToSave.AddUnique(CrateBlueprint->GetOutermost());
	}

	if (!PackagesToSave.IsEmpty())
	{
		UEditorLoadingAndSavingUtils::SavePackages(PackagesToSave, true);
	}
	return OutResult.Errors.IsEmpty();
}

FString FMT2ItemImportResult::BuildSummary() const
{
	return FString::Printf(
		TEXT("Item Blueprints: %d created, %d updated | Skipped: %d | Errors: %d"),
		ItemBlueprintsCreated, ItemBlueprintsUpdated, Skipped, Errors.Num());
}

FString FMT2ItemImporter::BuildBlueprintObjectPath(const FString& DestinationRoot, const FMT2ItemDefinition& Definition)
{
	const FString Name = FString::Printf(TEXT("BP_Item_%05d"), Definition.Vnum);
	return NormalizeItemDestinationRoot(DestinationRoot) / UMT2PathSettings::Path(TEXT("Relative_Items_Blueprints")) / Name + TEXT(".") + Name;
}
