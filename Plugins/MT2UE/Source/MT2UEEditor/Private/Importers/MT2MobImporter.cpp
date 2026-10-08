/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "Importers/MT2MobImporter.h"
#include "Config/MT2PathSettings.h"
#include "MT2AnimBlueprintBuilder.h"

#include "Animation/AnimBlueprint.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimNodeBase.h"
#include "Animation/AnimSequence.h"
#include "Animation/MT2MobAnimInstance.h"
#include "AnimationGraph.h"
#include "AnimGraphNode_BlendListByBool.h"
#include "AnimGraphNode_Root.h"
#include "AnimGraphNode_SequencePlayer.h"
#include "AnimGraphNode_Slot.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "MT2AssetScanner.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture2D.h"
#include "Factories/AnimBlueprintFactory.h"
#include "FileHelpers.h"
#include "HAL/FileManager.h"
#include "Importers/MT2MobProtoReader.h"
#include "K2Node_VariableGet.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/ScopedSlowTask.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Mobs/MT2Mob.h"
#include "Mobs/MT2MetinStone.h"
#include "Npcs/MT2Npc.h"
#include "Npcs/MT2NpcInteractionComponent.h"
#include "Npcs/MT2NpcShopComponent.h"
#include "Npcs/MT2NpcTypes.h"
#include "Mobs/MT2LootTypes.h"
#include "Items/MT2Item.h"
#include "Loot/MT2LootTable.h"
#include "Modules/ModuleManager.h"
#include "UObject/SavePackage.h"

namespace
{
	FString SanitizeName(const FString& Value)
	{
		FString Result;
		Result.Reserve(Value.Len());
		for (TCHAR Character : Value)
		{
			Result.AppendChar(FChar::IsAlnum(Character) ? Character : TEXT('_'));
		}
		while (Result.Contains(TEXT("__")))
		{
			Result.ReplaceInline(TEXT("__"), TEXT("_"));
		}
		return Result.TrimChar(TEXT('_'));
	}

	FString NormalizeDestinationRoot(const FString& DestinationRoot)
	{
		FString Result = DestinationRoot.IsEmpty() ? UMT2PathSettings::Path(TEXT("ImportDestinationRoot")) : DestinationRoot;
		Result.ReplaceInline(TEXT("\\"), TEXT("/"));
		while (Result.EndsWith(TEXT("/")))
		{
			Result.LeftChopInline(1);
		}
		return Result;
	}

	// NPC/Warp/Goto proto rows become interactable AMT2Npc Blueprints under /Game/Npcs.
	bool IsNpcDefinition(const FMT2MobDefinition& Definition)
	{
		return Definition.Type == EMT2MobType::NPC ||
			Definition.Type == EMT2MobType::Warp ||
			Definition.Type == EMT2MobType::Goto;
	}

	// Stone rows become AMT2MetinStone Blueprints under /Game/Metins.
	bool IsMetinDefinition(const FMT2MobDefinition& Definition)
	{
		return Definition.Type == EMT2MobType::Stone;
	}

	// group.txt: "Group <name> { Leader <n> <vnum>  Vnum <groupVnum>  1 <n> <vnum> ... }". The leader
	// and members are the mobs the old SpawnGroup(groupVnum) places. Keyed by group vnum.
	bool LoadSpawnGroups(const FString& Path, TMap<int32, FMT2MetinSpawnGroup>& OutGroups)
	{
		FString Content;
		if (!FFileHelper::LoadFileToString(Content, *Path))
		{
			return false;
		}
		TArray<FString> Lines;
		Content.ParseIntoArrayLines(Lines);

		FMT2MetinSpawnGroup Current;
		bool bInGroup = false;
		for (FString Line : Lines)
		{
			Line.TrimStartAndEndInline();
			if (Line.StartsWith(TEXT("Group")))
			{
				Current = FMT2MetinSpawnGroup();
				TArray<FString> Tokens;
				Line.ParseIntoArrayWS(Tokens);
				Current.GroupName = Tokens.IsValidIndex(1) ? Tokens[1] : FString();
				continue;
			}
			if (Line.StartsWith(TEXT("{"))) { bInGroup = true; continue; }
			if (Line.StartsWith(TEXT("}")))
			{
				if (bInGroup && Current.GroupVnum > 0 && !Current.Entries.IsEmpty())
				{
					OutGroups.Add(Current.GroupVnum, Current);
				}
				bInGroup = false;
				continue;
			}
			if (!bInGroup)
			{
				continue;
			}

			TArray<FString> Tokens;
			Line.ParseIntoArrayWS(Tokens);
			if (Tokens.Num() >= 2 && Tokens[0].Equals(TEXT("Vnum"), ESearchCase::IgnoreCase))
			{
				Current.GroupVnum = FCString::Atoi(*Tokens[1]);
			}
			// "Leader <name> <vnum>" and numbered "<n> <name> <vnum>" rows both end in the mob vnum.
			// The old CMobGroup loader AddMember()s the leader first, then every numbered row, and
			// SpawnGroup spawns each entry - so repeats are extra copies, not duplicates to drop.
			else if (Tokens.Num() >= 3)
			{
				const int32 MobVnum = FCString::Atoi(*Tokens.Last());
				if (MobVnum > 0)
				{
					if (FMT2MetinSpawnEntry* Existing = Current.Entries.FindByPredicate(
						[MobVnum](const FMT2MetinSpawnEntry& Entry) { return Entry.MobVnum == MobVnum; }))
					{
						++Existing->Count;
					}
					else
					{
						FMT2MetinSpawnEntry& Entry = Current.Entries.AddDefaulted_GetRef();
						Entry.MobVnum = MobVnum;
						Entry.Count = 1;
					}
				}
			}
		}
		return !OutGroups.IsEmpty();
	}

	// A stone's proto reuses ATTACK_SPEED..MOVING_SPEED as its spawn-group vnum range (they are not
	// speeds for stones) - see Docs/OldGameResearch/MetinStones.md.
	void ResolveMetinSpawnGroups(
		FMT2MobDefinition& Definition, const TMap<int32, FMT2MetinSpawnGroup>& AllGroups)
	{
		Definition.MetinSpawnGroups.Reset();
		const int32 MinVnum = FMath::Min(Definition.AttackSpeed, Definition.MovementSpeed);
		const int32 MaxVnum = FMath::Max(Definition.AttackSpeed, Definition.MovementSpeed);
		if (MinVnum <= 0)
		{
			return;
		}
		for (int32 GroupVnum = MinVnum; GroupVnum <= MaxVnum; ++GroupVnum)
		{
			if (const FMT2MetinSpawnGroup* Group = AllGroups.Find(GroupVnum))
			{
				Definition.MetinSpawnGroups.Add(*Group);
			}
		}
	}

	FString BuildAssetStem(const FMT2MobDefinition& Definition)
	{
		return FString::Printf(
			TEXT("%05d_%s"), Definition.Vnum,
			*SanitizeName(Definition.ResourceName.IsEmpty() ? Definition.InternalName : Definition.ResourceName));
	}

	bool AssetExists(const FString& ObjectPath)
	{
		if (FindObject<UObject>(nullptr, *ObjectPath))
		{
			return true;
		}
		return FPackageName::DoesPackageExist(FPackageName::ObjectPathToPackageName(ObjectPath));
	}

	bool LoadNpcList(const FString& FilePath, TMap<int32, FString>& OutResources)
	{
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *FilePath))
		{
			return false;
		}
		TArray<FString> Lines;
		Text.ParseIntoArrayLines(Lines, true);
		for (FString& Line : Lines)
		{
			Line.TrimStartAndEndInline();
			TArray<FString> Tokens;
			Line.ParseIntoArrayWS(Tokens);
			if (Tokens.Num() < 2)
			{
				continue;
			}
			const int32 Vnum = FCString::Atoi(*Tokens[0]);
			if (Vnum > 0)
			{
				OutResources.FindOrAdd(Vnum) = Tokens.Last();
			}
		}
		return true;
	}

	bool LoadLootDefinitions(const FString& FilePath, TMap<int32, TArray<FMT2LootEntry>>& OutEntries)
	{
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *FilePath)) return false;
		TArray<FString> Lines;
		Text.ParseIntoArrayLines(Lines, true);
		int32 MobVnum = 0;
		int32 KillAverage = 0;
		int32 RollGroup = 0;
		FString Type;
		for (FString Line : Lines)
		{
			Line.TrimStartAndEndInline();
			if (Line.StartsWith(TEXT("Group"), ESearchCase::IgnoreCase))
			{
				MobVnum = 0;
				KillAverage = 0;
				Type.Reset();
				++RollGroup;
				continue;
			}
			TArray<FString> Tokens;
			Line.ParseIntoArrayWS(Tokens);
			if (Tokens.Num() < 2) continue;
			if (Tokens[0].Equals(TEXT("Mob"), ESearchCase::IgnoreCase))
			{
				MobVnum = FCString::Atoi(*Tokens.Last());
			}
			else if (Tokens[0].Equals(TEXT("Type"), ESearchCase::IgnoreCase))
			{
				Type = Tokens.Last().ToLower();
			}
			else if (Tokens[0].Equals(TEXT("Kill_drop"), ESearchCase::IgnoreCase))
			{
				KillAverage = FCString::Atoi(*Tokens.Last());
			}
			else if (MobVnum > 0 && Tokens.Num() >= 4 && FChar::IsDigit(Tokens[0][0]))
			{
				FMT2LootEntry Entry;
				Entry.ItemVnum = FCString::Atoi(*Tokens[1]);
				Entry.MinCount = Entry.MaxCount = FMath::Max(FCString::Atoi(*Tokens[2]), 1);
				Entry.RollGroup = RollGroup;
				if (Type == TEXT("kill"))
				{
					Entry.RollType = EMT2LootRollType::WeightedKill;
					Entry.KillAverage = FMath::Max(KillAverage, 1);
					Entry.Weight = FMath::Max(FCString::Atoi(*Tokens[3]), 1);
					if (Tokens.Num() >= 5) Entry.RareAttributeChancePercent = FCString::Atof(*Tokens[4]);
				}
				else if (Type == TEXT("drop"))
				{
					Entry.RollType = EMT2LootRollType::IndependentChance;
					// Original server stores these as fPercent*10000 and rolls against 4,000,000
					// at the neutral level/rate multiplier: displayed probability is fPercent/4 %.
					Entry.ChancePercent = FCString::Atof(*Tokens[3]) / 4.0f;
				}
				else
				{
					continue;
				}
				if (Entry.ItemVnum > 0) OutEntries.FindOrAdd(MobVnum).Add(Entry);
			}
		}
		return !OutEntries.IsEmpty();
	}

	FString RawBytesToHex(const uint8* Data, int32 Length)
	{
		static const TCHAR Digits[] = TEXT("0123456789abcdef");
		FString Result;
		Result.Reserve(Length * 2);
		for (int32 Index = 0; Index < Length; ++Index)
		{
			Result.AppendChar(Digits[(Data[Index] >> 4) & 0x0f]);
			Result.AppendChar(Digits[Data[Index] & 0x0f]);
		}
		return Result;
	}

	bool LoadEtcDropProbabilities(
		const FString& SourceRoot, TMap<int32, float>& OutChancePercentByVnum)
	{
		const TArray<FString> DatabaseCandidates = {
			SourceRoot / UMT2PathSettings::Path(TEXT("Part_db")),
			UMT2PathSettings::Path(TEXT("Legacy_db"))
		};
		FString DatabasePath;
		for (const FString& Candidate : DatabaseCandidates)
		{
			if (FPaths::FileExists(Candidate))
			{
				DatabasePath = Candidate;
				break;
			}
		}
		FString Sql;
		if (DatabasePath.IsEmpty() || !FFileHelper::LoadFileToString(Sql, *DatabasePath))
		{
			return false;
		}

		// Keep original item names as raw-byte hex. This avoids corrupting the legacy Korean codepage
		// while matching etc_drop_item.txt against db.sql's `_binary 0x...` names.
		TMap<FString, int32> VnumByOriginalNameHex;
		TArray<FString> SqlLines;
		Sql.ParseIntoArrayLines(SqlLines, true);
		const FString BinaryMarker = TEXT("_binary 0x");
		for (FString Line : SqlLines)
		{
			Line.TrimStartInline();
			if (!Line.StartsWith(TEXT("(")))
			{
				continue;
			}
			const int32 FirstComma = Line.Find(TEXT(","));
			const int32 MarkerIndex = Line.Find(BinaryMarker, ESearchCase::IgnoreCase,
				ESearchDir::FromStart, FirstComma);
			if (FirstComma <= 1 || MarkerIndex == INDEX_NONE)
			{
				continue;
			}
			const int32 HexStart = MarkerIndex + BinaryMarker.Len();
			const int32 HexEnd = Line.Find(TEXT(","), ESearchCase::CaseSensitive,
				ESearchDir::FromStart, HexStart);
			if (HexEnd <= HexStart)
			{
				continue;
			}
			const int32 Vnum = FCString::Atoi(*Line.Mid(1, FirstComma - 1));
			if (Vnum > 0)
			{
				VnumByOriginalNameHex.Add(Line.Mid(HexStart, HexEnd - HexStart).ToLower(), Vnum);
			}
		}

		const TArray<FString> EtcCandidates = {
			SourceRoot / UMT2PathSettings::Path(TEXT("Part_locale_en_etc_drop_item")),
			SourceRoot / UMT2PathSettings::Path(TEXT("Part_etc_drop_item")),
			UMT2PathSettings::Path(TEXT("Legacy_etc_drop_item"))
		};
		FString EtcPath;
		for (const FString& Candidate : EtcCandidates)
		{
			if (FPaths::FileExists(Candidate))
			{
				EtcPath = Candidate;
				break;
			}
		}
		TArray<uint8> Bytes;
		if (EtcPath.IsEmpty() || !FFileHelper::LoadFileToArray(Bytes, *EtcPath))
		{
			return false;
		}

		int32 LineStart = 0;
		while (LineStart < Bytes.Num())
		{
			int32 LineEnd = LineStart;
			while (LineEnd < Bytes.Num() && Bytes[LineEnd] != '\n') ++LineEnd;
			int32 ContentEnd = LineEnd;
			while (ContentEnd > LineStart &&
				(Bytes[ContentEnd - 1] == '\r' || Bytes[ContentEnd - 1] == ' '))
			{
				--ContentEnd;
			}
			int32 TabIndex = ContentEnd - 1;
			while (TabIndex >= LineStart && Bytes[TabIndex] != '\t') // probability is after last tab
			{
				--TabIndex;
			}
			if (TabIndex > LineStart)
			{
				int32 NameEnd = TabIndex;
				while (NameEnd > LineStart && Bytes[NameEnd - 1] == ' ') // old parser excludes padding
				{
					--NameEnd;
				}
				TArray<ANSICHAR> ProbabilityText;
				for (int32 Index = TabIndex + 1; Index < ContentEnd; ++Index)
				{
					ProbabilityText.Add(static_cast<ANSICHAR>(Bytes[Index]));
				}
				ProbabilityText.Add('\0');
				const float SourceProbability = FCStringAnsi::Atof(ProbabilityText.GetData());
				const FString NameHex = RawBytesToHex(
					Bytes.GetData() + LineStart, NameEnd - LineStart);
				if (const int32* Vnum = VnumByOriginalNameHex.Find(NameHex))
				{
					OutChancePercentByVnum.Add(*Vnum, SourceProbability / 4.0f);
				}
			}
			LineStart = LineEnd + 1;
		}
		return !OutChancePercentByVnum.IsEmpty();
	}

	void BuildMotionDirectoryMap(const FString& SourceRoot, TMultiMap<FString, FString>& OutDirectories)
	{
		TArray<FString> Motlists;
		IFileManager::Get().FindFilesRecursive(Motlists, *SourceRoot, TEXT("motlist.txt"), true, false);
		for (const FString& Motlist : Motlists)
		{
			const FString Directory = FPaths::GetPath(Motlist);
			OutDirectories.Add(FPaths::GetCleanFilename(Directory).ToLower(), Directory);
		}
	}

	void BuildMobScriptMap(const FString& SourceRoot, TMultiMap<FString, FString>& OutScripts)
	{
		TArray<FString> Scripts;
		IFileManager::Get().FindFilesRecursive(Scripts, *SourceRoot, TEXT("*.msm"), true, false);
		for (const FString& Script : Scripts)
		{
			const FString ResourceName = FPaths::GetBaseFilename(Script).ToLower();
			OutScripts.Add(ResourceName, Script);
			const FString DirectoryName = FPaths::GetCleanFilename(FPaths::GetPath(Script)).ToLower();
			if (!DirectoryName.Equals(ResourceName, ESearchCase::CaseSensitive))
			{
				// A large part of the client names the race script "shape.msm" and identifies it
				// through its containing resource directory in npclist.txt.
				OutScripts.Add(DirectoryName, Script);
			}
		}
	}

	FString MakeGamePathFromSource(const FString& SourceRoot, const FString& SourceDirectory)
	{
		FString Relative = SourceDirectory;
		FPaths::MakePathRelativeTo(Relative, *(FPaths::ConvertRelativePathToFull(SourceRoot) / TEXT("")));
		Relative.ReplaceInline(TEXT("\\"), TEXT("/"));
		if (Relative.StartsWith(TEXT("ymir work/"), ESearchCase::IgnoreCase))
		{
			Relative.RightChopInline(10);
		}
		return UMT2PathSettings::Path(TEXT("ymir_work_Prefix")) + Relative;
	}

	FString FindMobMesh(const FString& GameDirectory, const FString& ResourceName)
	{
		FAssetRegistryModule& RegistryModule =
			FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		TArray<FAssetData> Assets;
		RegistryModule.Get().GetAssetsByPath(FName(*GameDirectory), Assets, true, false);

		const FString ExpectedName = TEXT("SK_") + ResourceName;
		const FAssetData* Best = nullptr;
		int32 BestScore = 0;
		for (const FAssetData& Asset : Assets)
		{
			if (Asset.AssetClassPath != USkeletalMesh::StaticClass()->GetClassPathName())
			{
				continue;
			}
			const FString Name = Asset.AssetName.ToString();
			int32 Score = Name.Equals(ExpectedName, ESearchCase::IgnoreCase) ? 100 : 0;
			Score += Name.Contains(ResourceName, ESearchCase::IgnoreCase) ? 20 : 0;
			Score -= Name.Contains(TEXT("lod"), ESearchCase::IgnoreCase) ? 50 : 0;
			if (Score > BestScore)
			{
				BestScore = Score;
				Best = &Asset;
			}
		}
		return Best ? Best->GetSoftObjectPath().ToString() : FString();
	}

	FString ResolveMobSourceReference(
		const FString& SourceRoot, const FString& ScriptPath,
		const FString& BasePath, FString Reference)
	{
		Reference.ReplaceInline(TEXT("\\"), TEXT("/"));
		FString Combined = Reference;
		if (FPaths::IsRelative(Combined) && !BasePath.IsEmpty())
		{
			Combined = BasePath / Combined;
		}
		Combined.ReplaceInline(TEXT("\\"), TEXT("/"));
		const int32 YmirWorkIndex = Combined.Find(TEXT("ymir work/"), ESearchCase::IgnoreCase);
		if (YmirWorkIndex != INDEX_NONE)
		{
			Combined = SourceRoot / Combined.Mid(YmirWorkIndex);
		}
		else if (FPaths::IsRelative(Combined))
		{
			Combined = FPaths::GetPath(ScriptPath) / Combined;
		}
		FPaths::NormalizeFilename(Combined);
		return Combined;
	}

	FString BuildImportedTextureObjectPath(
		const FString& SourceRoot, const FString& DestinationRoot,
		const FString& AbsoluteTexturePath)
	{
		FString RelativePath = AbsoluteTexturePath;
		FPaths::MakePathRelativeTo(
			RelativePath, *(FPaths::ConvertRelativePathToFull(SourceRoot) / TEXT("")));
		RelativePath.ReplaceInline(TEXT("\\"), TEXT("/"));
		FMT2AssetRecord Record;
		Record.Kind = EMT2AssetKind::Texture;
		Record.ContentPath = RelativePath;
		return FMT2AssetScanner::BuildContentObjectPath(DestinationRoot, Record);
	}

	FString FindMobMeshFromScript(
		const FString& SourceRoot,
		const FString& ScriptPath,
		const FString& DestinationRoot,
		FString& OutSourceDirectory,
		TArray<FMT2MobMaterialOverrideRecord>& OutMaterialOverrides)
	{
		FString Script;
		if (!FFileHelper::LoadFileToString(Script, *ScriptPath))
		{
			return FString();
		}

		FString BaseModelPath;
		FString ShapePath;
		FString ShapeModel;
		FString SourceSkins[2];
		FString TargetSkins[2];
		bool bInShapeData = false;
		TArray<FString> Lines;
		Script.ParseIntoArrayLines(Lines, true);
		for (FString Line : Lines)
		{
			Line.TrimStartAndEndInline();
			if (Line.StartsWith(TEXT("BaseModelFileName"), ESearchCase::IgnoreCase))
			{
				BaseModelPath = Line.RightChop(17).TrimStartAndEnd().TrimQuotes();
				ShapePath = FPaths::GetPath(BaseModelPath.Replace(TEXT("\\"), TEXT("/")));
			}
			else if (Line.Equals(TEXT("Group ShapeData"), ESearchCase::IgnoreCase))
			{
				bInShapeData = true;
			}
			else if (bInShapeData && Line.StartsWith(TEXT("PathName"), ESearchCase::IgnoreCase))
			{
				const FString Value = Line.RightChop(8).TrimStartAndEnd().TrimQuotes();
				if (!Value.IsEmpty()) ShapePath = Value;
			}
			else if (bInShapeData && ShapeModel.IsEmpty() &&
				Line.StartsWith(TEXT("Model"), ESearchCase::IgnoreCase))
			{
				ShapeModel = Line.RightChop(5).TrimStartAndEnd().TrimQuotes();
			}
			else if (bInShapeData && SourceSkins[1].IsEmpty() &&
				Line.StartsWith(TEXT("SourceSkin2"), ESearchCase::IgnoreCase))
			{
				SourceSkins[1] = Line.RightChop(11).TrimStartAndEnd().TrimQuotes();
			}
			else if (bInShapeData && TargetSkins[1].IsEmpty() &&
				Line.StartsWith(TEXT("TargetSkin2"), ESearchCase::IgnoreCase))
			{
				TargetSkins[1] = Line.RightChop(11).TrimStartAndEnd().TrimQuotes();
			}
			else if (bInShapeData && SourceSkins[0].IsEmpty() &&
				Line.StartsWith(TEXT("SourceSkin"), ESearchCase::IgnoreCase))
			{
				SourceSkins[0] = Line.RightChop(10).TrimStartAndEnd().TrimQuotes();
			}
			else if (bInShapeData && TargetSkins[0].IsEmpty() &&
				Line.StartsWith(TEXT("TargetSkin"), ESearchCase::IgnoreCase))
			{
				TargetSkins[0] = Line.RightChop(10).TrimStartAndEnd().TrimQuotes();
			}
		}
		const FString ModelPath = ShapeModel.IsEmpty() ? BaseModelPath : ShapeModel;
		if (ModelPath.IsEmpty())
		{
			return FString();
		}

		const FString ResolvedModelPath = ResolveMobSourceReference(
			SourceRoot, ScriptPath, ShapeModel.IsEmpty() ? FString() : ShapePath, ModelPath);
		OutSourceDirectory = FPaths::GetPath(ScriptPath);
		for (int32 SkinIndex = 0; SkinIndex < UE_ARRAY_COUNT(SourceSkins); ++SkinIndex)
		{
			if (SourceSkins[SkinIndex].IsEmpty() || TargetSkins[SkinIndex].IsEmpty()) continue;
			const FString ResolvedSourceSkin = ResolveMobSourceReference(
				SourceRoot, ScriptPath, ShapePath, SourceSkins[SkinIndex]);
			const FString ResolvedTargetSkin = ResolveMobSourceReference(
				SourceRoot, ScriptPath, ShapePath, TargetSkins[SkinIndex]);
			FMT2MobMaterialOverrideRecord& Override = OutMaterialOverrides.AddDefaulted_GetRef();
			Override.SourceTextureObjectPath = BuildImportedTextureObjectPath(
				SourceRoot, DestinationRoot, ResolvedSourceSkin);
			Override.TargetTextureObjectPath = BuildImportedTextureObjectPath(
				SourceRoot, DestinationRoot, ResolvedTargetSkin);
		}
		return FindMobMesh(
			MakeGamePathFromSource(SourceRoot, FPaths::GetPath(ResolvedModelPath)),
			FPaths::GetBaseFilename(ResolvedModelPath));
	}

	bool ParseSqlFields(const FString& TupleLine, TArray<FString>& OutFields)
	{
		OutFields.Reset();
		FString Field;
		bool bQuoted = false;
		for (int32 Index = 0; Index < TupleLine.Len(); ++Index)
		{
			const TCHAR Character = TupleLine[Index];
			if (Character == TEXT('\\') && bQuoted && Index + 1 < TupleLine.Len())
			{
				Field.AppendChar(Character);
				Field.AppendChar(TupleLine[++Index]);
				continue;
			}
			if (Character == TEXT('\''))
			{
				bQuoted = !bQuoted;
				Field.AppendChar(Character);
				continue;
			}
			if (Character == TEXT(',') && !bQuoted)
			{
				OutFields.Add(Field.TrimStartAndEnd());
				Field.Reset();
				continue;
			}
			Field.AppendChar(Character);
		}
		OutFields.Add(Field.TrimStartAndEnd());
		return OutFields.Num() >= 9;
	}

	int32 ParseServerAIFlags(FString Value)
	{
		Value = Value.TrimStartAndEnd().TrimQuotes();
		int32 Flags = 0;
		const TPair<const TCHAR*, EMT2MobAIFlag> FlagNames[] = {
			{TEXT("AGGR"), EMT2MobAIFlag::Aggressive},
			{TEXT("NOMOVE"), EMT2MobAIFlag::NoMove},
			{TEXT("COWARD"), EMT2MobAIFlag::Coward},
			{TEXT("NOATTSHINSU"), EMT2MobAIFlag::NoAttackShinsoo},
			{TEXT("NOATTCHUNJO"), EMT2MobAIFlag::NoAttackChunjo},
			{TEXT("NOATTJINNO"), EMT2MobAIFlag::NoAttackJinno},
			{TEXT("ATTMOB"), EMT2MobAIFlag::AttackMobs},
			{TEXT("BERSERK"), EMT2MobAIFlag::Berserk},
			{TEXT("STONESKIN"), EMT2MobAIFlag::StoneSkin},
			{TEXT("GODSPEED"), EMT2MobAIFlag::GodSpeed},
			{TEXT("DEATHBLOW"), EMT2MobAIFlag::DeathBlow},
			{TEXT("REVIVE"), EMT2MobAIFlag::Revive}
		};
		TArray<FString> Names;
		Value.ParseIntoArray(Names, TEXT(","), true);
		for (FString Name : Names)
		{
			Name.TrimStartAndEndInline();
			for (const TPair<const TCHAR*, EMT2MobAIFlag>& Pair : FlagNames)
			{
				if (Name.Equals(Pair.Key, ESearchCase::IgnoreCase))
				{
					Flags |= 1 << static_cast<uint8>(Pair.Value);
					break;
				}
			}
		}
		return Flags;
	}

	struct FServerMobOverride
	{
		int32 AIFlags = 0;
		int64 GoldMin = 0;
		int64 GoldMax = 0;
	};

	int32 LoadServerMobOverrides(
		const FString& SqlPath, TMap<int32, FServerMobOverride>& OutOverrides)
	{
		FString Sql;
		if (!FFileHelper::LoadFileToString(Sql, *SqlPath))
		{
			return 0;
		}
		TArray<FString> Lines;
		Sql.ParseIntoArrayLines(Lines, true);
		bool bReadingMobProto = false;
		for (FString Line : Lines)
		{
			Line.TrimStartAndEndInline();
			if (!bReadingMobProto)
			{
				bReadingMobProto = Line.StartsWith(TEXT("REPLACE INTO `mob_proto`"));
				continue;
			}
			if (!Line.StartsWith(TEXT("(")))
			{
				if (Line.StartsWith(TEXT("/*!40000 ALTER TABLE `mob_proto` ENABLE")))
				{
					break;
				}
				continue;
			}
			Line.RemoveFromStart(TEXT("("));
			while (Line.EndsWith(TEXT(",")) || Line.EndsWith(TEXT(";")))
			{
				Line.LeftChopInline(1);
			}
			Line.RemoveFromEnd(TEXT(")"));
			TArray<FString> Fields;
			if (ParseSqlFields(Line, Fields))
			{
				const int32 Vnum = FCString::Atoi(*Fields[0]);
				if (Vnum > 0)
				{
					FServerMobOverride& Override = OutOverrides.FindOrAdd(Vnum);
					Override.AIFlags = ParseServerAIFlags(Fields[8]);
					// db.sql mob_proto columns: gold_min=24, gold_max=25. The packed client
					// proto omits these values in the currently supported English layout.
					if (Fields.Num() > 25)
					{
						Override.GoldMin = FMath::Max<int64>(FCString::Atoi64(*Fields[24]), 0);
						Override.GoldMax = FMath::Max<int64>(
							FCString::Atoi64(*Fields[25]), Override.GoldMin);
					}
				}
			}
		}
		return OutOverrides.Num();
	}

	bool ParseMotionName(const FString& Token, EMT2MobMotion& OutMotion)
	{
		const FString Name = Token.ToUpper();
		if (Name.StartsWith(TEXT("WAIT"))) OutMotion = EMT2MobMotion::Wait;
		else if (Name == TEXT("WALK")) OutMotion = EMT2MobMotion::Walk;
		else if (Name == TEXT("RUN")) OutMotion = EMT2MobMotion::Run;
		else if (Name.StartsWith(TEXT("NORMAL_ATTACK"))) OutMotion = EMT2MobMotion::NormalAttack;
		else if (Name.StartsWith(TEXT("FRONT_DAMAGE"))) OutMotion = EMT2MobMotion::FrontDamage;
		else if (Name.StartsWith(TEXT("FRONT_DEAD"))) OutMotion = EMT2MobMotion::FrontDead;
		else if (Name.StartsWith(TEXT("FRONT_KNOCKDOWN"))) OutMotion = EMT2MobMotion::FrontKnockdown;
		else if (Name.StartsWith(TEXT("FRONT_STANDUP"))) OutMotion = EMT2MobMotion::FrontStandup;
		else if (Name.StartsWith(TEXT("BACK_DAMAGE"))) OutMotion = EMT2MobMotion::BackDamage;
		else if (Name.StartsWith(TEXT("BACK_DEAD"))) OutMotion = EMT2MobMotion::BackDead;
		else if (Name.StartsWith(TEXT("BACK_KNOCKDOWN"))) OutMotion = EMT2MobMotion::BackKnockdown;
		else if (Name.StartsWith(TEXT("BACK_STANDUP"))) OutMotion = EMT2MobMotion::BackStandup;
		else if (Name == TEXT("SPECIAL")) OutMotion = EMT2MobMotion::Special1;
		else if (Name == TEXT("SPECIAL1")) OutMotion = EMT2MobMotion::Special2;
		else if (Name == TEXT("SPECIAL2")) OutMotion = EMT2MobMotion::Special3;
		else if (Name == TEXT("SPECIAL3")) OutMotion = EMT2MobMotion::Special4;
		else if (Name == TEXT("SPECIAL4")) OutMotion = EMT2MobMotion::Special5;
		else return false;
		return true;
	}

	FMT2MobMotionSet LoadMotionSet(const FMT2MobImportRecord& Record)
	{
		FMT2MobMotionSet Result;
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *(Record.SourceDirectory / UMT2PathSettings::Path(TEXT("Part_motlist")))))
		{
			return Result;
		}

		FString RelativeDirectory = Record.SourceRelativeDirectory;
		RelativeDirectory.ReplaceInline(TEXT("\\"), TEXT("/"));
		if (RelativeDirectory.StartsWith(TEXT("ymir work/"), ESearchCase::IgnoreCase))
		{
			RelativeDirectory.RightChopInline(10);
		}
		const FString GameDirectory = UMT2PathSettings::Path(TEXT("ymir_work_Prefix")) + RelativeDirectory;
		TArray<FString> Lines;
		Text.ParseIntoArrayLines(Lines, true);
		for (FString& Line : Lines)
		{
			TArray<FString> Tokens;
			Line.ParseIntoArrayWS(Tokens);
			EMT2MobMotion Motion;
			if (Tokens.Num() < 3 || !ParseMotionName(Tokens[1], Motion))
			{
				continue;
			}
			const FString BaseName = FPaths::GetBaseFilename(Tokens[2]);
			const FString AssetName = TEXT("A_") + BaseName;
			const FString ObjectPath = FString::Printf(
				TEXT("%s/%s.%s"), *GameDirectory, *AssetName, *AssetName);
			if (!LoadObject<UAnimSequence>(nullptr, *ObjectPath))
			{
				continue;
			}
			FMT2MobMotionVariant Variant;
			Variant.Animation = TSoftObjectPtr<UAnimSequence>(FSoftObjectPath(ObjectPath));
			Variant.Weight = Tokens.Num() > 3 ? FMath::Max(FCString::Atoi(*Tokens[3]), 1) : 100;
			Result.Motions.FindOrAdd(Motion).Items.Add(MoveTemp(Variant));
		}
		return Result;
	}

	UEdGraphPin* FindMobPosePin(UEdGraphNode* Node, EEdGraphPinDirection Direction, int32 Index = 0)
	{
		int32 CurrentIndex = 0;
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin && Pin->Direction == Direction && Pin->PinType.PinSubCategoryObject.Get() == FPoseLink::StaticStruct())
			{
				if (CurrentIndex++ == Index) return Pin;
			}
		}
		return nullptr;
	}

	UEdGraphPin* FindBoolInput(UEdGraphNode* Node)
	{
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin && Pin->Direction == EGPD_Input && Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Boolean)
			{
				return Pin;
			}
		}
		return nullptr;
	}

	template <typename T>
	T* AddNode(UEdGraph* Graph, int32 X, int32 Y)
	{
		FGraphNodeCreator<T> Creator(*Graph);
		T* Node = Creator.CreateNode();
		Node->NodePosX = X;
		Node->NodePosY = Y;
		Creator.Finalize();
		return Node;
	}

	void SetBlendTime(UAnimGraphNode_BlendListByBool* Node)
	{
		Node->ReconstructNode();
		FArrayProperty* Property = FindFProperty<FArrayProperty>(FAnimNode_BlendListBase::StaticStruct(), TEXT("BlendTime"));
		FFloatProperty* FloatProperty = Property ? CastField<FFloatProperty>(Property->Inner) : nullptr;
		if (!Property || !FloatProperty) return;
		FScriptArrayHelper Helper(Property, Property->ContainerPtrToValuePtr<void>(&Node->Node));
		if (Helper.Num() < 2) Helper.AddValues(2 - Helper.Num());
		for (int32 Index = 0; Index < Helper.Num(); ++Index)
		{
			FloatProperty->SetFloatingPointPropertyValue(Helper.GetRawPtr(Index), 0.2f);
		}
	}

	UEdGraphPin* ExposePlayRate(UAnimGraphNode_SequencePlayer* Node)
	{
		const FName Name(TEXT("PlayRate"));
		FOptionalPinFromProperty* Optional = Node->ShowPinForProperties.FindByPredicate(
			[Name](const FOptionalPinFromProperty& Value) { return Value.PropertyName == Name; });
		if (Optional) Optional->bShowPin = true;
		else Node->ShowPinForProperties.Add(FOptionalPinFromProperty(
			Name, true, true, TEXT("Play Rate"), FText::GetEmpty(), false, TEXT("Settings"), false));
		Node->ReconstructNode();
		return Node->FindPin(Name);
	}

	UAnimSequence* FirstMotion(const FMT2MobMotionSet& Set, EMT2MobMotion Motion)
	{
		const FMT2MobMotionVariants* Variants = Set.Motions.Find(Motion);
		return Variants && !Variants->Items.IsEmpty() ? Variants->Items[0].Animation.LoadSynchronous() : nullptr;
	}

	UAnimBlueprint* CreateAnimBlueprint(
		const FString& DestinationRoot,
		const FMT2MobImportRecord& Record,
		USkeletalMesh* Mesh,
		const FMT2MobMotionSet& Motions,
		FMT2MobImportResult& OutResult)
	{
		const FString Name = TEXT("ABP_Mob_") + SanitizeName(Record.Definition.ResourceName);
		const FString PackagePath = NormalizeDestinationRoot(DestinationRoot) / UMT2PathSettings::Path(TEXT("Relative_Mobs_Animations"));
		const FString ObjectPath = PackagePath / Name + TEXT(".") + Name;
		if (AssetExists(ObjectPath))
		{
			return LoadObject<UAnimBlueprint>(nullptr, *ObjectPath);
		}

		UAnimSequence* Idle = FirstMotion(Motions, EMT2MobMotion::Wait);
		UAnimSequence* Walk = FirstMotion(Motions, EMT2MobMotion::Walk);
		UAnimSequence* Run = FirstMotion(Motions, EMT2MobMotion::Run);
		if (!Mesh || !Mesh->GetSkeleton() || !Idle)
		{
			OutResult.Errors.Add(FString::Printf(TEXT("%d: missing mesh skeleton or WAIT animation."), Record.Definition.Vnum));
			return nullptr;
		}
		Walk = Walk ? Walk : (Run ? Run : Idle);
		Run = Run ? Run : Walk;

		UAnimBlueprintFactory* Factory = NewObject<UAnimBlueprintFactory>();
		Factory->ParentClass = UMT2MobAnimInstance::StaticClass();
		Factory->TargetSkeleton = Mesh->GetSkeleton();
		Factory->PreviewSkeletalMesh = Mesh;
		IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
		UAnimBlueprint* Blueprint = Cast<UAnimBlueprint>(AssetTools.CreateAsset(
			Name, PackagePath, UAnimBlueprint::StaticClass(), Factory));
		if (!Blueprint) return nullptr;

		UAnimationGraph* Graph = nullptr;
		for (UEdGraph* Candidate : Blueprint->FunctionGraphs)
		{
			if ((Graph = Cast<UAnimationGraph>(Candidate))) break;
		}
		UAnimGraphNode_Root* Root = Graph ? FBlueprintEditorUtils::GetAnimGraphRoot(Graph) : nullptr;
		if (!Graph || !Root) return nullptr;

		UAnimGraphNode_SequencePlayer* IdleNode = AddNode<UAnimGraphNode_SequencePlayer>(Graph, -900, -200);
		UAnimGraphNode_SequencePlayer* WalkNode = AddNode<UAnimGraphNode_SequencePlayer>(Graph, -900, 100);
		UAnimGraphNode_SequencePlayer* RunNode = AddNode<UAnimGraphNode_SequencePlayer>(Graph, -900, 400);
		IdleNode->SetAnimationAsset(Idle); IdleNode->ReconstructNode();
		WalkNode->SetAnimationAsset(Walk); WalkNode->ReconstructNode();
		RunNode->SetAnimationAsset(Run); RunNode->ReconstructNode();
		UAnimGraphNode_BlendListByBool* WalkBlend = AddNode<UAnimGraphNode_BlendListByBool>(Graph, -550, 250);
		UAnimGraphNode_BlendListByBool* MoveBlend = AddNode<UAnimGraphNode_BlendListByBool>(Graph, -250, 0);
		SetBlendTime(WalkBlend);
		SetBlendTime(MoveBlend);
		UAnimGraphNode_Slot* Slot = AddNode<UAnimGraphNode_Slot>(Graph, 50, 0);
		Slot->Node.SlotName = TEXT("DefaultSlot");
		Slot->ReconstructNode();

		UClass* Parent = UMT2MobAnimInstance::StaticClass();
		auto AddVariable = [Graph, Parent](const TCHAR* PropertyName, int32 Y)
		{
			FProperty* Property = Parent->FindPropertyByName(PropertyName);
			FGraphNodeCreator<UK2Node_VariableGet> Creator(*Graph);
			UK2Node_VariableGet* Node = Creator.CreateNode();
			Node->SetFromProperty(Property, true, Parent);
			Node->NodePosX = -900;
			Node->NodePosY = Y;
			Creator.Finalize();
			return Node;
		};
		UK2Node_VariableGet* IsMoving = AddVariable(TEXT("bIsMoving"), 700);
		UK2Node_VariableGet* ShouldWalk = AddVariable(TEXT("bShouldWalk"), 800);
		UK2Node_VariableGet* PlayRate = AddVariable(TEXT("MovementPlayRate"), 900);

		const UEdGraphSchema* Schema = Graph->GetSchema();
		UEdGraphPin* WalkPlayRate = ExposePlayRate(WalkNode);
		UEdGraphPin* RunPlayRate = ExposePlayRate(RunNode);
		UEdGraphPin* RunOutput = FindMobPosePin(RunNode, EGPD_Output);
		UEdGraphPin* WalkOutput = FindMobPosePin(WalkNode, EGPD_Output);
		UEdGraphPin* RunInput = FindMobPosePin(WalkBlend, EGPD_Input, 0);
		UEdGraphPin* WalkInput = FindMobPosePin(WalkBlend, EGPD_Input, 1);
		UEdGraphPin* ShouldWalkInput = FindBoolInput(WalkBlend);
		UEdGraphPin* IdleOutput = FindMobPosePin(IdleNode, EGPD_Output);
		UEdGraphPin* IdleInput = FindMobPosePin(MoveBlend, EGPD_Input, 0);
		UEdGraphPin* LocomotionOutput = FindMobPosePin(WalkBlend, EGPD_Output);
		UEdGraphPin* LocomotionInput = FindMobPosePin(MoveBlend, EGPD_Input, 1);
		UEdGraphPin* IsMovingInput = FindBoolInput(MoveBlend);
		UEdGraphPin* MoveOutput = FindMobPosePin(MoveBlend, EGPD_Output);
		UEdGraphPin* SlotInput = FindMobPosePin(Slot, EGPD_Input);
		UEdGraphPin* SlotOutput = FindMobPosePin(Slot, EGPD_Output);
		UEdGraphPin* RootInput = FindMobPosePin(Root, EGPD_Input);
		bool bConnected = Schema != nullptr;
		auto Connect = [&OutResult, &Record, Schema, &bConnected](
			const TCHAR* Label, UEdGraphPin* Output, UEdGraphPin* Input)
		{
			if (!bConnected)
			{
				return;
			}
			if (!Output || !Input)
			{
				OutResult.Errors.Add(FString::Printf(
					TEXT("%d: AnimBP link %s has a missing pin."), Record.Definition.Vnum, Label));
				bConnected = false;
				return;
			}
			if (!Schema->TryCreateConnection(Output, Input))
			{
				OutResult.Errors.Add(FString::Printf(
					TEXT("%d: AnimBP link %s was rejected."), Record.Definition.Vnum, Label));
				bConnected = false;
			}
		};
		Connect(TEXT("Run->WalkBlendFalse"), RunOutput, RunInput);
		Connect(TEXT("Walk->WalkBlendTrue"), WalkOutput, WalkInput);
		Connect(TEXT("ShouldWalk->WalkBlend"), ShouldWalk->GetValuePin(), ShouldWalkInput);
		Connect(TEXT("PlayRate->Walk"), PlayRate->GetValuePin(), WalkPlayRate);
		Connect(TEXT("PlayRate->Run"), PlayRate->GetValuePin(), RunPlayRate);
		Connect(TEXT("Idle->MoveBlendFalse"), IdleOutput, IdleInput);
		Connect(TEXT("Locomotion->MoveBlendTrue"), LocomotionOutput, LocomotionInput);
		Connect(TEXT("IsMoving->MoveBlend"), IsMoving->GetValuePin(), IsMovingInput);
		Connect(TEXT("MoveBlend->Slot"), MoveOutput, SlotInput);
		Connect(TEXT("Slot->Root"), SlotOutput, RootInput);
		if (!bConnected)
		{
			if (!Schema)
			{
				OutResult.Errors.Add(FString::Printf(TEXT("%d: AnimBP graph has no schema."), Record.Definition.Vnum));
			}
			return nullptr;
		}

		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
		FKismetEditorUtilities::CompileBlueprint(Blueprint);
		Blueprint->MarkPackageDirty();
		OutResult.AnimationBlueprintsCreated++;
		return Blueprint;
	}

	UAnimBlueprint* CreateDerivedMobAnimBlueprint(
		const FString& DestinationRoot,
		const FMT2MobImportRecord& Record,
		USkeletalMesh* Mesh,
		const FMT2MobMotionSet& Motions,
		UAnimBlueprint* BaseBlueprint,
		FMT2MobImportResult& OutResult)
	{
		UAnimSequence* Idle = FirstMotion(Motions, EMT2MobMotion::Wait);
		UAnimSequence* Walk = FirstMotion(Motions, EMT2MobMotion::Walk);
		UAnimSequence* Run = FirstMotion(Motions, EMT2MobMotion::Run);
		if (!Mesh || !Mesh->GetSkeleton() || !Idle)
		{
			OutResult.Errors.Add(FString::Printf(
				TEXT("%d: missing mesh skeleton or WAIT animation."), Record.Definition.Vnum));
			return nullptr;
		}
		Walk = Walk ? Walk : (Run ? Run : Idle);
		Run = Run ? Run : Walk;

		const FString Name = TEXT("ABP_Mob_") + SanitizeName(Record.Definition.ResourceName);
		const FString PackagePath = NormalizeDestinationRoot(DestinationRoot) / UMT2PathSettings::Path(TEXT("Relative_Mobs_Animations"));
		bool bCreated = false;
		UAnimBlueprint* Blueprint = MT2AnimBlueprintBuilder::CreateOrUpdateChild(
			PackagePath, Name, BaseBlueprint, Mesh, OutResult.Errors, bCreated);
		if (!Blueprint || !Blueprint->GeneratedClass)
		{
			return nullptr;
		}

		UMT2MobAnimInstance* Defaults = Cast<UMT2MobAnimInstance>(Blueprint->GeneratedClass->GetDefaultObject());
		if (!Defaults)
		{
			OutResult.Errors.Add(FString::Printf(TEXT("%d: could not configure AnimBP defaults."), Record.Definition.Vnum));
			return nullptr;
		}
		Defaults->Modify();
		Defaults->WaitAnimation = Idle;
		Defaults->WalkAnimation = Walk;
		Defaults->RunAnimation = Run;
		Defaults->DefaultAnimationSet = TEXT("general");
		Defaults->AnimationSets.Reset();
		Defaults->MotionAnimations.Reset();
		for (const TPair<EMT2MobMotion, FMT2MobMotionVariants>& Pair : Motions.Motions)
		{
			FString ActionName = StaticEnum<EMT2MobMotion>()->GetNameStringByValue(static_cast<int64>(Pair.Key));
			ActionName.ToLowerInline();
			FMT2AnimationSequenceSet& ActionVariants =
				Defaults->AnimationSets.FindOrAdd(TEXT("general")).Actions.FindOrAdd(FName(*ActionName));
			for (const FMT2MobMotionVariant& Variant : Pair.Value.Items)
			{
				if (UAnimSequence* Sequence = Variant.Animation.LoadSynchronous())
				{
					ActionVariants.Animations.AddUnique(Sequence);
				}
			}
			if (!ActionVariants.Animations.IsEmpty())
			{
				Defaults->MotionAnimations.Add(Pair.Key, ActionVariants.Animations[0]);
			}
		}

		// Many original mob motlists intentionally provide RUN but no WALK. The old client falls
		// back to RUN in that case. Store the resolved fallbacks in the action maps too, because
		// SetAnimationSet() below rebuilds the direct locomotion properties from these maps.
		auto EnsureLocomotionAction = [Defaults](
			FName ActionName, EMT2MobMotion Motion, UAnimSequence* Animation)
		{
			if (!Animation)
			{
				return;
			}

			FMT2AnimationSequenceSet& Action =
				Defaults->AnimationSets.FindOrAdd(TEXT("general")).Actions.FindOrAdd(ActionName);
			if (Action.Animations.IsEmpty())
			{
				Action.Animations.Add(Animation);
			}
			Defaults->MotionAnimations.FindOrAdd(Motion) = Action.Animations[0];
		};
		EnsureLocomotionAction(TEXT("wait"), EMT2MobMotion::Wait, Idle);
		EnsureLocomotionAction(TEXT("walk"), EMT2MobMotion::Walk, Walk);
		EnsureLocomotionAction(TEXT("run"), EMT2MobMotion::Run, Run);
		Defaults->SetAnimationSet(Defaults->DefaultAnimationSet);

		// CreateOrUpdateChild() already compiled this Blueprint, but that happened BEFORE these
		// defaults were set above - without recompiling again here, the compiled class/CDO keeps
		// whatever it had at that earlier compile (typically nothing), and the only way to make the
		// new Wait/Walk/Run/AnimationSets values actually "take" is to reopen the asset and manually
		// hit Compile. Recompiling again now bakes them in and reinstances any already-placed actors.
		FKismetEditorUtilities::CompileBlueprint(Blueprint);
		Blueprint->MarkPackageDirty();
		bCreated ? ++OutResult.AnimationBlueprintsCreated : ++OutResult.AnimationBlueprintsUpdated;
		return Blueprint;
	}

	// Lightweight stats-only refresh for a mob that was already fully imported: re-applies the
	// mob_proto definition (HP, damage, speeds, flags, rewards...) onto the existing DataAsset while
	// leaving mesh/AnimBP/Blueprint untouched. This is what lets proto-layout fixes (see
	// Docs/OldGameResearch/MobProtoFormats.md) reach previously-imported mobs without a full rebuild.
	// Stats-only refresh of an already-imported mob: re-applies the freshly-decoded mob_proto
	// definition onto the existing Blueprint's class defaults (actor fields + component fields),
	// leaving mesh/AnimBP/motions untouched. Lets proto fixes reach imported mobs without a rebuild.
	UBlueprint* RefreshExistingMobDefinition(
		const FString& DestinationRoot, const FMT2MobImportRecord& Record)
	{
		const FString ObjectPath = FMT2MobImporter::BuildBlueprintObjectPath(DestinationRoot, Record.Definition);
		UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *ObjectPath);
		AMT2Mob* Defaults = Blueprint && Blueprint->GeneratedClass
			? Cast<AMT2Mob>(Blueprint->GeneratedClass->GetDefaultObject()) : nullptr;
		if (!Defaults)
		{
			return nullptr;
		}
		Defaults->Modify();
		Defaults->ConfigureFromDefinition(Record.Definition);
		// Recompiling bakes the new class defaults and reinstances already-placed actors.
		FKismetEditorUtilities::CompileBlueprint(Blueprint);
		Blueprint->MarkPackageDirty();
		return Blueprint;
	}

	int32 FindMobMaterialSlot(USkeletalMesh* Mesh, const FString& SourceTextureObjectPath)
	{
		if (!Mesh || SourceTextureObjectPath.IsEmpty()) return INDEX_NONE;
		UTexture2D* SourceTexture = LoadObject<UTexture2D>(nullptr, *SourceTextureObjectPath);
		FString SourceStem = FPackageName::ObjectPathToObjectName(SourceTextureObjectPath);
		SourceStem.RemoveFromStart(TEXT("T_"), ESearchCase::IgnoreCase);
		const TArray<FSkeletalMaterial>& Materials = Mesh->GetMaterials();
		for (int32 SlotIndex = 0; SlotIndex < Materials.Num(); ++SlotIndex)
		{
			UMaterialInterface* Material = Materials[SlotIndex].MaterialInterface;
			if (!Material) continue;
			UTexture* Diffuse = nullptr;
			if (SourceTexture && Material->GetTextureParameterValue(
				FHashedMaterialParameterInfo(TEXT("Diffuse")), Diffuse) && Diffuse == SourceTexture)
			{
				return SlotIndex;
			}
			if (Material->GetName().Contains(SourceStem, ESearchCase::IgnoreCase) ||
				Materials[SlotIndex].MaterialSlotName.ToString().Contains(SourceStem, ESearchCase::IgnoreCase) ||
				Materials[SlotIndex].ImportedMaterialSlotName.ToString().Contains(SourceStem, ESearchCase::IgnoreCase))
			{
				return SlotIndex;
			}
		}
		return INDEX_NONE;
	}

	UMaterialInstanceConstant* CreateMobMaterialOverride(
		const FString& DestinationRoot, int32 Vnum, int32 SlotIndex,
		UMaterialInterface* Parent, UTexture2D* TargetTexture,
		TArray<UPackage*>& OutPackagesToSave)
	{
		if (!Parent || !TargetTexture) return nullptr;
		const FString AssetName = FString::Printf(
			TEXT("MI_Mob_%05d_%d_%s"), Vnum, SlotIndex, *SanitizeName(TargetTexture->GetName()));
		const FString PackageName = NormalizeDestinationRoot(DestinationRoot) /
			TEXT("Materials/MobOverrides") / AssetName;
		const FString ObjectPath = PackageName + TEXT(".") + AssetName;
		UMaterialInstanceConstant* Instance = LoadObject<UMaterialInstanceConstant>(nullptr, *ObjectPath);
		const bool bCreated = Instance == nullptr;
		UPackage* Package = bCreated ? CreatePackage(*PackageName) : Instance->GetOutermost();
		if (bCreated)
		{
			Instance = NewObject<UMaterialInstanceConstant>(
				Package, *AssetName, RF_Public | RF_Standalone | RF_Transactional);
		}
		if (!Instance || !Package) return nullptr;
		Instance->Modify();
		Instance->SetParentEditorOnly(Parent);
		Instance->SetTextureParameterValueEditorOnly(
			FMaterialParameterInfo(TEXT("Diffuse")), TargetTexture);
		Instance->PostEditChange();
		Package->MarkPackageDirty();
		if (bCreated) FAssetRegistryModule::AssetCreated(Instance);
		OutPackagesToSave.AddUnique(Package);
		return Instance;
	}

	void ApplyMobMaterialOverrides(
		const FString& DestinationRoot, const FMT2MobImportRecord& Record,
		USkeletalMesh* Mesh, USkeletalMeshComponent* MeshComponent,
		TArray<UPackage*>& OutPackagesToSave)
	{
		if (!Mesh || !MeshComponent) return;
		for (const FMT2MobMaterialOverrideRecord& Override : Record.MaterialOverrides)
		{
			if (Override.SourceTextureObjectPath.Equals(
				Override.TargetTextureObjectPath, ESearchCase::IgnoreCase)) continue;
			const int32 SlotIndex = FindMobMaterialSlot(Mesh, Override.SourceTextureObjectPath);
			if (!Mesh->GetMaterials().IsValidIndex(SlotIndex))
			{
				UE_LOG(LogTemp, Warning, TEXT("[MT2MobImporter] Mob %d cannot match source skin %s on %s."),
					Record.Definition.Vnum, *Override.SourceTextureObjectPath, *Record.MeshObjectPath);
				continue;
			}
			UTexture2D* TargetTexture = LoadObject<UTexture2D>(nullptr, *Override.TargetTextureObjectPath);
			UMaterialInstanceConstant* Material = CreateMobMaterialOverride(
				DestinationRoot, Record.Definition.Vnum, SlotIndex,
				Mesh->GetMaterials()[SlotIndex].MaterialInterface, TargetTexture, OutPackagesToSave);
			if (Material) MeshComponent->SetMaterial(SlotIndex, Material);
		}
	}

	// The mob Blueprint IS the mob's data: every imported mob_proto field is written directly onto
	// the Blueprint's class defaults - identity/flags/motions on the actor, stats/AI/regen/rewards on
	// the matching components, mesh + AnimBP on the mesh component. No side-car DataAsset.
	UBlueprint* CreateMobBlueprint(
		const FString& DestinationRoot,
		const FMT2MobImportRecord& Record,
		USkeletalMesh* Mesh,
		UAnimBlueprint* AnimBlueprint,
		const FMT2MobMotionSet& Motions,
		TArray<UPackage*>& OutPackagesToSave)
	{
		const FString ObjectPath = FMT2MobImporter::BuildBlueprintObjectPath(DestinationRoot, Record.Definition);
		const FString PackageName = FPackageName::ObjectPathToPackageName(ObjectPath);
		const FString Name = FPackageName::GetShortName(PackageName);

		UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *ObjectPath);
		if (!Blueprint)
		{
			UPackage* Package = CreatePackage(*PackageName);
			// NPC/Warp/Goto rows become AMT2Npc Blueprints (interactable, no combat), matching the
			// old game where NPCs are just mob_proto rows with an OnClick behavior.
			UClass* ParentClass = IsNpcDefinition(Record.Definition) ? AMT2Npc::StaticClass()
				: IsMetinDefinition(Record.Definition) ? AMT2MetinStone::StaticClass()
				: AMT2Mob::StaticClass();
			Blueprint = FKismetEditorUtilities::CreateBlueprint(
				ParentClass, Package, *Name, BPTYPE_Normal,
				UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass(), TEXT("MT2MobImporter"));
			if (!Blueprint) return nullptr;
			FKismetEditorUtilities::CompileBlueprint(Blueprint);
		}

		if (AMT2Mob* Defaults = Cast<AMT2Mob>(Blueprint->GeneratedClass->GetDefaultObject()))
		{
			Defaults->Modify();
			Defaults->ConfigureFromDefinition(Record.Definition);
			Defaults->SetMotionSet(Motions);
			if (USkeletalMeshComponent* MeshComponent = Defaults->GetMesh())
			{
				MeshComponent->SetSkeletalMeshAsset(Mesh);
				ApplyMobMaterialOverrides(
					DestinationRoot, Record, Mesh, MeshComponent, OutPackagesToSave);
				if (AnimBlueprint && AnimBlueprint->GeneratedClass)
				{
					MeshComponent->SetAnimInstanceClass(AnimBlueprint->GeneratedClass);
				}
			}
		}

		// Recompiling is what actually bakes the new class defaults into the generated class and
		// reinstances any already-placed actors, instead of leaving them on stale values.
		FKismetEditorUtilities::CompileBlueprint(Blueprint);
		Blueprint->MarkPackageDirty();
		return Blueprint;
	}

	UBlueprint* CreateMobLootTableBlueprint(
		const FString& DestinationRoot, int32 Vnum, TArray<UPackage*>& OutPackagesToSave)
	{
		const FString Name = FString::Printf(TEXT("LT_Mob_%05d"), Vnum);
		const FString PackageName =
			NormalizeDestinationRoot(DestinationRoot) / UMT2PathSettings::Path(TEXT("Relative_Mobs_LootTables")) / Name;
		const FString ObjectPath = PackageName + TEXT(".") + Name;
		UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *ObjectPath);
		if (!Blueprint)
		{
			UPackage* Package = CreatePackage(*PackageName);
			Blueprint = FKismetEditorUtilities::CreateBlueprint(
				UMT2LootTable::StaticClass(), Package, *Name, BPTYPE_Normal,
				UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass(),
				TEXT("MT2MobImporter"));
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

FString FMT2MobImportResult::BuildSummary() const
{
	return FString::Printf(
		TEXT("Mob BPs: %d created, %d stats-refreshed | AnimBPs: %d created, %d updated | Skipped: %d | Errors: %d"),
		MobBlueprintsCreated, DefinitionsRefreshed, AnimationBlueprintsCreated, AnimationBlueprintsUpdated,
		Skipped, Errors.Num());
}

FString FMT2MobImporter::BuildBlueprintObjectPath(
	const FString& DestinationRoot, const FMT2MobDefinition& Definition)
{
	if (IsNpcDefinition(Definition))
	{
		const FString Name = TEXT("BP_Npc_") + BuildAssetStem(Definition);
		return NormalizeDestinationRoot(DestinationRoot) / UMT2PathSettings::Path(TEXT("Relative_Npcs_Blueprints")) / Name + TEXT(".") + Name;
	}
	if (IsMetinDefinition(Definition))
	{
		const FString Name = TEXT("BP_Metin_") + BuildAssetStem(Definition);
		return NormalizeDestinationRoot(DestinationRoot) / UMT2PathSettings::Path(TEXT("Relative_Metins_Blueprints")) / Name + TEXT(".") + Name;
	}
	const FString Name = TEXT("BP_Mob_") + BuildAssetStem(Definition);
	return NormalizeDestinationRoot(DestinationRoot) / UMT2PathSettings::Path(TEXT("Relative_Mobs_Blueprints")) / Name + TEXT(".") + Name;
}

int32 FMT2MobImporter::ImportMobLootEntries(
	const FString& SourceRoot,
	const FString& DestinationRoot,
	TArray<FString>& OutWarnings)
{
	const TArray<FString> Candidates = {
		SourceRoot / UMT2PathSettings::Path(TEXT("Part_locale_en_mob_drop_item")),
		SourceRoot / UMT2PathSettings::Path(TEXT("Part_mob_drop_item")),
		UMT2PathSettings::Path(TEXT("Legacy_mob_drop_item"))
	};
	FString LootPath;
	for (const FString& Candidate : Candidates)
	{
		if (FPaths::FileExists(Candidate))
		{
			LootPath = Candidate;
			break;
		}
	}
	if (LootPath.IsEmpty())
	{
		OutWarnings.Add(TEXT("mob_drop_item.txt was not found; mob loot tables were not generated."));
		return 0;
	}

	TMap<int32, TArray<FMT2LootEntry>> EntriesByMob;
	if (!LoadLootDefinitions(LootPath, EntriesByMob))
	{
		OutWarnings.Add(FString::Printf(TEXT("No supported loot groups were parsed from %s."), *LootPath));
		return 0;
	}
	FMT2MobProtoReadResult Proto;
	FString ProtoError;
	if (!FMT2MobProtoReader::Read(SourceRoot / UMT2PathSettings::Path(TEXT("Part_locale_en_mob_proto")), Proto, ProtoError))
	{
		OutWarnings.Add(FString::Printf(TEXT("mob_proto could not be read for loot tables: %s"), *ProtoError));
		return 0;
	}
	TMap<int32, float> EtcDropChanceByItemVnum;
	if (!LoadEtcDropProbabilities(SourceRoot, EtcDropChanceByItemVnum))
	{
		OutWarnings.Add(TEXT(
			"etc_drop_item.txt probabilities could not be matched; mob_proto drop_item rewards "
			"will be omitted instead of incorrectly becoming guaranteed drops."));
	}
	TMap<int32, const FMT2MobDefinition*> DefinitionsByVnum;
	for (const FMT2MobDefinition& Definition : Proto.Definitions)
	{
		DefinitionsByVnum.Add(Definition.Vnum, &Definition);
	}
	TMap<int32, FServerMobOverride> ServerOverrides;
	LoadServerMobOverrides(SourceRoot / UMT2PathSettings::Path(TEXT("Part_db")), ServerOverrides);

	FAssetRegistryModule& RegistryModule =
		FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	TArray<FAssetData> MobBlueprintAssets;
	for (const TCHAR* RelativeFolder : { UMT2PathSettings::Path(TEXT("Relative_Mobs_Blueprints")), UMT2PathSettings::Path(TEXT("Relative_Metins_Blueprints")) })
	{
		TArray<FAssetData> FolderAssets;
		RegistryModule.Get().GetAssetsByPath(
			FName(*(NormalizeDestinationRoot(DestinationRoot) / RelativeFolder)),
			FolderAssets, true, false);
		MobBlueprintAssets.Append(MoveTemp(FolderAssets));
	}
	if (MobBlueprintAssets.IsEmpty())
	{
		OutWarnings.Add(TEXT("No existing mob Blueprints were found; mob loot tables were not generated."));
		return 0;
	}

	TArray<UPackage*> PackagesToSave;
	int32 ConfiguredMobCount = 0;
	int32 MobsWithLootCount = 0;
	TSet<int32> VisitedMobVnums;
	for (const FAssetData& Asset : MobBlueprintAssets)
	{
		UBlueprint* Blueprint = Cast<UBlueprint>(Asset.GetAsset());
		if (!Blueprint || !Blueprint->GeneratedClass)
		{
			continue;
		}
		AMT2Mob* Defaults = Cast<AMT2Mob>(Blueprint->GeneratedClass->GetDefaultObject());
		if (!Defaults)
		{
			continue;
		}
		const int32 MobVnum = Defaults->GetMobVnum();
		const FMT2MobDefinition* const* DefinitionPtr = DefinitionsByVnum.Find(MobVnum);
		if (MobVnum <= 0 || !DefinitionPtr)
		{
			continue;
		}
		const FMT2MobDefinition& Definition = **DefinitionPtr;
		VisitedMobVnums.Add(MobVnum);

		static const TArray<FMT2LootEntry> EmptyEntries;
		const TArray<FMT2LootEntry>* ImportedEntries = EntriesByMob.Find(MobVnum);
		const TArray<FMT2LootEntry>& DesiredEntries = ImportedEntries ? *ImportedEntries : EmptyEntries;
		++ConfiguredMobCount;
		if (ImportedEntries && !ImportedEntries->IsEmpty())
		{
			++MobsWithLootCount;
		}
		UBlueprint* LootBlueprint = CreateMobLootTableBlueprint(
			DestinationRoot, MobVnum, PackagesToSave);
		UMT2LootTable* LootDefaults = LootBlueprint && LootBlueprint->GeneratedClass
			? Cast<UMT2LootTable>(LootBlueprint->GeneratedClass->GetDefaultObject()) : nullptr;
		if (!LootDefaults)
		{
			OutWarnings.Add(FString::Printf(TEXT("Mob %d loot table creation failed."), MobVnum));
			continue;
		}

		LootDefaults->Modify();
		LootDefaults->Rewards.Reset();
		auto AddSpecialReward = [LootDefaults](
			EMT2LootCrateRewardKind Kind, int64 Minimum, int64 Maximum)
		{
			if (Maximum <= 0) return;
			FMT2LootTableEntry& Entry = LootDefaults->Rewards.AddDefaulted_GetRef();
			Entry.Kind = Kind;
			Entry.MinimumAmount = FMath::Max<int64>(Minimum, 0);
			Entry.MaximumAmount = FMath::Max(Maximum, Entry.MinimumAmount);
			Entry.RollMode = EMT2LootRollMode::Guaranteed;
		};
		AddSpecialReward(
			EMT2LootCrateRewardKind::Experience,
			Definition.ExperienceReward, Definition.ExperienceReward);
		const FServerMobOverride* ServerOverride = ServerOverrides.Find(MobVnum);
		const int64 GoldMin = ServerOverride ? ServerOverride->GoldMin : Definition.GoldMin;
		const int64 GoldMax = ServerOverride ? ServerOverride->GoldMax : Definition.GoldMax;
		AddSpecialReward(EMT2LootCrateRewardKind::Yang, GoldMin, GoldMax);

		auto AddItemReward = [&](
			int32 ItemVnum, int32 MinCount, int32 MaxCount, EMT2LootRollMode RollMode,
			float Chance, int32 Weight, int32 Group, int32 KillAverage, float RareChance)
		{
			const FString ItemName = FString::Printf(TEXT("BP_Item_%05d"), ItemVnum);
			const FString ItemPath = NormalizeDestinationRoot(DestinationRoot) /
				UMT2PathSettings::Path(TEXT("Relative_Items_Blueprints")) / ItemName + TEXT(".") + ItemName;
			UBlueprint* ItemBlueprint = LoadObject<UBlueprint>(nullptr, *ItemPath);
			if (!ItemBlueprint || !ItemBlueprint->GeneratedClass)
			{
				OutWarnings.Add(FString::Printf(
					TEXT("Mob %d reward item %d has no imported Blueprint."), MobVnum, ItemVnum));
				return;
			}
			FMT2LootTableEntry& Entry = LootDefaults->Rewards.AddDefaulted_GetRef();
			Entry.Kind = EMT2LootCrateRewardKind::Item;
			Entry.MinimumAmount = FMath::Max(MinCount, 1);
			Entry.MaximumAmount = FMath::Max(MaxCount, MinCount);
			Entry.RollMode = RollMode;
			Entry.ChancePercent = Chance;
			Entry.Weight = Weight;
			Entry.RollGroup = Group;
			Entry.KillAverage = KillAverage;
			Entry.RareAttributeChancePercent = RareChance;
			Entry.ItemTemplate =
				TSubclassOf<UMT2ItemTemplate>(ItemBlueprint->GeneratedClass.Get());
			Entry.ItemData.Count = FMath::Clamp<int64>(
				Entry.MinimumAmount, 1, MAX_int32);
		};

		bool bAddedProtoDropItem = false;
		if (const float* DropChance =
			EtcDropChanceByItemVnum.Find(Definition.DropItemVnum);
			Definition.DropItemVnum > 0 && DropChance && *DropChance > 0.0f)
		{
			AddItemReward(
				Definition.DropItemVnum, 1, 1, EMT2LootRollMode::IndependentChance,
				*DropChance, 1, 0, 1, 0.0f);
			bAddedProtoDropItem = true;
		}
		for (const FMT2LootEntry& SourceEntry : DesiredEntries)
		{
			if (bAddedProtoDropItem && SourceEntry.ItemVnum == Definition.DropItemVnum)
			{
				continue;
			}
			AddItemReward(
				SourceEntry.ItemVnum, SourceEntry.MinCount, SourceEntry.MaxCount,
				SourceEntry.RollType == EMT2LootRollType::WeightedKill
					? EMT2LootRollMode::WeightedGroup : EMT2LootRollMode::IndependentChance,
				SourceEntry.ChancePercent, SourceEntry.Weight, SourceEntry.RollGroup,
				SourceEntry.KillAverage, SourceEntry.RareAttributeChancePercent);
		}
		FKismetEditorUtilities::CompileBlueprint(LootBlueprint);
		LootBlueprint->MarkPackageDirty();

		Blueprint->Modify();
		Defaults->Modify();
		const TSubclassOf<UMT2LootTable> LootTableClass(LootBlueprint->GeneratedClass.Get());
		Defaults->SetLootTable(LootTableClass);
		FKismetEditorUtilities::CompileBlueprint(Blueprint);
		Defaults = Blueprint->GeneratedClass
			? Cast<AMT2Mob>(Blueprint->GeneratedClass->GetDefaultObject()) : nullptr;
		if (!Defaults || Defaults->GetLootTable() != LootTableClass)
		{
			OutWarnings.Add(FString::Printf(
				TEXT("Mob %d loot validation failed after Blueprint compilation."), MobVnum));
			continue;
		}
		Blueprint->MarkPackageDirty();
		PackagesToSave.AddUnique(Blueprint->GetOutermost());
	}
	if (!PackagesToSave.IsEmpty())
	{
		UEditorLoadingAndSavingUtils::SavePackages(PackagesToSave, true);
	}
	int32 MissingBlueprintCount = 0;
	for (const TPair<int32, TArray<FMT2LootEntry>>& Pair : EntriesByMob)
	{
		if (!VisitedMobVnums.Contains(Pair.Key))
		{
			++MissingBlueprintCount;
		}
	}
	OutWarnings.Add(FString::Printf(
		TEXT("Loot validation: %d source mobs, %d imported mob Blueprints with loot, %d source mobs missing a Blueprint."),
		EntriesByMob.Num(), MobsWithLootCount, MissingBlueprintCount));
	return ConfiguredMobCount;
}

int32 FMT2MobImporter::ImportNpcShops(
	const FString& DatabasePath,
	const FString& DestinationRoot,
	TArray<FString>& OutWarnings)
{
	FString Sql;
	if (!FFileHelper::LoadFileToString(Sql, *DatabasePath))
	{
		OutWarnings.Add(FString::Printf(TEXT("Could not read db.sql at %s; shops not imported."), *DatabasePath));
		return 0;
	}

	// Extract every `(a, 'name', b)` / `(a, b, c)` tuple from the shop and shop_item REPLACE blocks.
	auto ExtractTuples = [&Sql](const TCHAR* TableMarker, TArray<TArray<FString>>& OutRows)
	{
		int32 Cursor = 0;
		while ((Cursor = Sql.Find(TableMarker, ESearchCase::CaseSensitive, ESearchDir::FromStart, Cursor)) != INDEX_NONE)
		{
			const int32 ValuesStart = Sql.Find(TEXT("VALUES"), ESearchCase::IgnoreCase, ESearchDir::FromStart, Cursor);
			if (ValuesStart == INDEX_NONE)
			{
				break;
			}
			const int32 BlockEnd = Sql.Find(TEXT(";"), ESearchCase::CaseSensitive, ESearchDir::FromStart, ValuesStart);
			const FString Block = Sql.Mid(ValuesStart, (BlockEnd == INDEX_NONE ? Sql.Len() : BlockEnd) - ValuesStart);
			// Split each parenthesised tuple.
			int32 Open = 0;
			while ((Open = Block.Find(TEXT("("), ESearchCase::CaseSensitive, ESearchDir::FromStart, Open)) != INDEX_NONE)
			{
				const int32 Close = Block.Find(TEXT(")"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Open);
				if (Close == INDEX_NONE)
				{
					break;
				}
				FString Inner = Block.Mid(Open + 1, Close - Open - 1);
				TArray<FString> Columns;
				Inner.ParseIntoArray(Columns, TEXT(","));
				for (FString& Column : Columns)
				{
					Column = Column.TrimStartAndEnd().TrimQuotes();
				}
				OutRows.Add(Columns);
				Open = Close + 1;
			}
			Cursor = BlockEnd == INDEX_NONE ? Sql.Len() : BlockEnd;
		}
	};

	// shop.vnum -> npc_vnum (skip the my_shop template rows with npc_vnum 0).
	TArray<TArray<FString>> ShopRows;
	ExtractTuples(TEXT("INTO `shop`"), ShopRows);
	TMap<int32, int32> ShopVnumToNpc;
	for (const TArray<FString>& Row : ShopRows)
	{
		if (Row.Num() >= 3)
		{
			const int32 NpcVnum = FCString::Atoi(*Row[2]);
			if (NpcVnum > 0)
			{
				ShopVnumToNpc.Add(FCString::Atoi(*Row[0]), NpcVnum);
			}
		}
	}

	// shop_item rows -> per-npc entries.
	TArray<TArray<FString>> ItemRows;
	ExtractTuples(TEXT("INTO `shop_item`"), ItemRows);
	TMap<int32, TArray<FMT2ShopEntry>> EntriesByNpc;
	for (const TArray<FString>& Row : ItemRows)
	{
		if (Row.Num() < 2)
		{
			continue;
		}
		const int32* NpcVnum = ShopVnumToNpc.Find(FCString::Atoi(*Row[0]));
		if (!NpcVnum)
		{
			continue;
		}
		FMT2ShopEntry Entry;
		Entry.ItemVnum = FCString::Atoi(*Row[1]);
		Entry.Count = Row.Num() >= 3 ? FMath::Max(1, FCString::Atoi(*Row[2])) : 1;
		if (Entry.ItemVnum > 0)
		{
			EntriesByNpc.FindOrAdd(*NpcVnum).Add(Entry);
		}
	}

	FAssetRegistryModule& RegistryModule =
		FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	const FString NpcFolder = NormalizeDestinationRoot(DestinationRoot) / UMT2PathSettings::Path(TEXT("Relative_Npcs_Blueprints"));
	TArray<FAssetData> NpcAssets;
	RegistryModule.Get().GetAssetsByPath(FName(*NpcFolder), NpcAssets, true, false);

	TArray<UPackage*> PackagesToSave;
	int32 ConfiguredShops = 0;
	for (const FAssetData& Asset : NpcAssets)
	{
		const FString BlueprintName = Asset.AssetName.ToString();
		if (!BlueprintName.StartsWith(TEXT("BP_Npc_")))
		{
			continue;
		}
		const int32 VnumEnd = BlueprintName.Find(TEXT("_"), ESearchCase::CaseSensitive, ESearchDir::FromStart, 7);
		const int32 NpcVnum = VnumEnd == INDEX_NONE ? 0 : FCString::Atoi(*BlueprintName.Mid(7, VnumEnd - 7));
		const TArray<FMT2ShopEntry>* Entries = EntriesByNpc.Find(NpcVnum);
		if (!Entries || Entries->IsEmpty())
		{
			continue;
		}

		UBlueprint* Blueprint = Cast<UBlueprint>(Asset.GetAsset());
		AMT2Npc* Defaults = Blueprint && Blueprint->GeneratedClass
			? Cast<AMT2Npc>(Blueprint->GeneratedClass->GetDefaultObject()) : nullptr;
		if (!Defaults || !Defaults->GetShopComponent() || !Defaults->GetInteractionComponent())
		{
			continue;
		}

		Blueprint->Modify();
		Defaults->Modify();
		Defaults->GetShopComponent()->SetShopEntries(*Entries);
		// A shop_item listing implies this NPC is a shop even if the proto OnClickType said otherwise.
		Defaults->GetInteractionComponent()->SetOnClickType(EMT2NpcOnClickType::Shop);
		FKismetEditorUtilities::CompileBlueprint(Blueprint);
		Blueprint->MarkPackageDirty();
		PackagesToSave.AddUnique(Blueprint->GetOutermost());
		++ConfiguredShops;
	}

	if (!PackagesToSave.IsEmpty())
	{
		UEditorLoadingAndSavingUtils::SavePackages(PackagesToSave, true);
	}
	OutWarnings.Add(FString::Printf(
		TEXT("Shops: %d shop rows, %d NPC shops configured."), ShopVnumToNpc.Num(), ConfiguredShops));
	return ConfiguredShops;
}

bool FMT2MobImporter::Discover(
	const FString& SourceRoot,
	const FString& DestinationRoot,
	TArray<FMT2MobImportRecord>& OutRecords,
	TArray<FString>& OutWarnings,
	FString& OutError,
	bool bIncludeExisting)
{
	OutRecords.Reset();
	// Metin stones summon group.txt groups; load them once so each stone definition can carry its
	// own candidate waves (see Docs/OldGameResearch/MetinStones.md).
	TMap<int32, FMT2MetinSpawnGroup> SpawnGroups;
	{
		const TArray<FString> GroupCandidates = {
			SourceRoot / UMT2PathSettings::Path(TEXT("Part_group")),
			SourceRoot / UMT2PathSettings::Path(TEXT("Part_locale_en_group")),
			UMT2PathSettings::Path(TEXT("Legacy_group"))
		};
		for (const FString& Candidate : GroupCandidates)
		{
			if (FPaths::FileExists(Candidate) && LoadSpawnGroups(Candidate, SpawnGroups))
			{
				break;
			}
		}
		if (SpawnGroups.IsEmpty())
		{
			OutWarnings.Add(TEXT("group.txt not found; metin stones will have no spawn waves."));
		}
	}

	OutWarnings.Reset();
	OutError.Reset();

	const FString ProtoPath = SourceRoot / UMT2PathSettings::Path(TEXT("Part_locale_en_mob_proto"));
	FMT2MobProtoReadResult Proto;
	if (!FMT2MobProtoReader::Read(ProtoPath, Proto, OutError)) return false;

	TMap<int32, FString> Resources;
	if (!LoadNpcList(SourceRoot / UMT2PathSettings::Path(TEXT("Part_npclist")), Resources))
	{
		OutError = TEXT("Could not read npclist.txt.");
		return false;
	}
	TMultiMap<FString, FString> MotionDirectories;
	BuildMotionDirectoryMap(SourceRoot, MotionDirectories);
	TMultiMap<FString, FString> MobScripts;
	BuildMobScriptMap(SourceRoot, MobScripts);
	TMap<int32, FServerMobOverride> ServerOverrides;
	const int32 ServerFlagCount = LoadServerMobOverrides(
		SourceRoot / UMT2PathSettings::Path(TEXT("Part_db")), ServerOverrides);
	FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get().SearchAllAssets(true);

	int32 MissingMeshCount = 0;
	int32 MatchingMeshCount = 0;
	int32 ExistingBlueprintCount = 0;
	int32 OverlaidAIFlagCount = 0;
	for (FMT2MobDefinition Definition : Proto.Definitions)
	{
		if (const FServerMobOverride* Override = ServerOverrides.Find(Definition.Vnum))
		{
			if (Definition.AIFlags != Override->AIFlags)
			{
				++OverlaidAIFlagCount;
			}
			Definition.AIFlags = Override->AIFlags;
			Definition.GoldMin = Override->GoldMin;
			Definition.GoldMax = Override->GoldMax;
		}
		if (Definition.Type == EMT2MobType::Stone)
		{
			Definition.AIFlags |= 1 << static_cast<uint8>(EMT2MobAIFlag::NoMove);
		}

		const FString* Resource = Resources.Find(Definition.Vnum);
		Definition.ResourceName = Resource ? *Resource : Definition.SourceFolder;
		if (Definition.ResourceName.IsEmpty()) continue;

		FString SourceDirectory;
		FString MeshObjectPath;
		TArray<FMT2MobMaterialOverrideRecord> MaterialOverrides;
		TArray<FString> CandidateScripts;
		MobScripts.MultiFind(Definition.ResourceName.ToLower(), CandidateScripts);
		CandidateScripts.Sort();
		for (const FString& ScriptPath : CandidateScripts)
		{
			TArray<FMT2MobMaterialOverrideRecord> CandidateOverrides;
			MeshObjectPath = FindMobMeshFromScript(
				SourceRoot, ScriptPath, DestinationRoot, SourceDirectory, CandidateOverrides);
			if (!MeshObjectPath.IsEmpty())
			{
				MaterialOverrides = MoveTemp(CandidateOverrides);
				break;
			}
		}

		TArray<FString> CandidateDirectories;
		if (MeshObjectPath.IsEmpty())
		{
			MotionDirectories.MultiFind(Definition.ResourceName.ToLower(), CandidateDirectories);
			if (CandidateDirectories.IsEmpty() && !Definition.SourceFolder.IsEmpty())
			{
				MotionDirectories.MultiFind(Definition.SourceFolder.ToLower(), CandidateDirectories);
			}
			for (const FString& Candidate : CandidateDirectories)
			{
				const FString CandidateMesh = FindMobMesh(
					MakeGamePathFromSource(SourceRoot, Candidate), Definition.ResourceName);
				if (!CandidateMesh.IsEmpty())
				{
					SourceDirectory = Candidate;
					MeshObjectPath = CandidateMesh;
					break;
				}
			}
		}
		if (MeshObjectPath.IsEmpty())
		{
			MissingMeshCount++;
			continue;
		}
		++MatchingMeshCount;

		if (AssetExists(BuildBlueprintObjectPath(DestinationRoot, Definition)))
		{
			++ExistingBlueprintCount;
			// "Show Imported" lists these anyway so they can be re-imported (stats refresh).
			if (!bIncludeExisting)
			{
				continue;
			}
		}

		if (IsMetinDefinition(Definition))
		{
			ResolveMetinSpawnGroups(Definition, SpawnGroups);
		}

		FMT2MobImportRecord& Record = OutRecords.AddDefaulted_GetRef();
		Record.Definition = MoveTemp(Definition);
		Record.SourceDirectory = SourceDirectory;
		Record.SourceRelativeDirectory = SourceDirectory;
		FPaths::MakePathRelativeTo(Record.SourceRelativeDirectory, *(FPaths::ConvertRelativePathToFull(SourceRoot) / TEXT("")));
		Record.MeshObjectPath = MeshObjectPath;
		Record.MaterialOverrides = MoveTemp(MaterialOverrides);
		Record.BlueprintObjectPath = BuildBlueprintObjectPath(DestinationRoot, Record.Definition);
	}
	OutRecords.Sort([](const FMT2MobImportRecord& A, const FMT2MobImportRecord& B)
	{
		return A.Definition.Vnum < B.Definition.Vnum;
	});
	OutWarnings = MoveTemp(Proto.Warnings);
	if (ServerFlagCount > 0)
	{
		OutWarnings.Add(FString::Printf(
			TEXT("Applied authoritative AI flags from db.sql to %d mobs (%d client values corrected)."),
			ServerFlagCount, OverlaidAIFlagCount));
	}
	OutWarnings.Add(FString::Printf(
		TEXT("Decoded %d mob_proto rows; %d have matching imported meshes; %d already exist (%s); %d had no matching mesh."),
		Proto.Definitions.Num(), MatchingMeshCount, ExistingBlueprintCount,
		bIncludeExisting ? TEXT("listed for re-import") : TEXT("hidden - enable Show Imported to list them"),
		MissingMeshCount));
	return true;
}

bool FMT2MobImporter::Import(
	const TArray<FMT2MobImportRecord>& Records,
	const FString& SourceRoot,
	const FString& DestinationRoot,
	TFunctionRef<bool()> ShouldCancel,
	FMT2MobImportResult& OutResult)
{
	FScopedSlowTask Progress(Records.Num(), NSLOCTEXT("MT2MobImporter", "Progress", "Creating Metin2 mobs..."));
	Progress.MakeDialog(true);
	TArray<UPackage*> PackagesToSave;
	TMap<FString, UAnimBlueprint*> AnimBlueprintCache;
	const FString AnimationPackagePath = NormalizeDestinationRoot(DestinationRoot) / UMT2PathSettings::Path(TEXT("Relative_Mobs_Animations"));
	bool bBaseCreated = false;
	UAnimBlueprint* BaseAnimBlueprint = MT2AnimBlueprintBuilder::CreateOrUpdateBase(
		AnimationPackagePath, TEXT("ABP_MT2MobBase"), UMT2MobAnimInstance::StaticClass(),
		OutResult.Errors, bBaseCreated);
	if (!BaseAnimBlueprint)
	{
		return false;
	}
	PackagesToSave.AddUnique(BaseAnimBlueprint->GetOutermost());
	bBaseCreated ? ++OutResult.AnimationBlueprintsCreated : ++OutResult.AnimationBlueprintsUpdated;

	for (const FMT2MobImportRecord& Record : Records)
	{
		Progress.EnterProgressFrame(1.0f, FText::Format(
			NSLOCTEXT("MT2MobImporter", "Mob", "Creating mob {0}: {1}"),
			FText::AsNumber(Record.Definition.Vnum), FText::FromString(Record.Definition.DisplayName)));
		if (Progress.ShouldCancel() || ShouldCancel()) break;
		USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, *Record.MeshObjectPath);
		if (!Mesh)
		{
			OutResult.Errors.Add(FString::Printf(TEXT("%d: mesh could not be loaded."), Record.Definition.Vnum));
			continue;
		}
		const FMT2MobMotionSet Motions = LoadMotionSet(Record);
		const FString ResourceKey = Record.Definition.ResourceName.ToLower();
		UAnimBlueprint* AnimBlueprint = AnimBlueprintCache.FindRef(ResourceKey);
		if (!AnimBlueprint)
		{
			AnimBlueprint = CreateDerivedMobAnimBlueprint(
				DestinationRoot, Record, Mesh, Motions, BaseAnimBlueprint, OutResult);
			if (AnimBlueprint)
			{
				AnimBlueprintCache.Add(ResourceKey, AnimBlueprint);
			}
		}
		if (!AnimBlueprint)
		{
			continue;
		}
		if (AssetExists(Record.BlueprintObjectPath))
		{
			// Existing mobs need both fresh proto data and fresh motion bindings. Previously this branch
			// skipped CreateDerivedMobAnimBlueprint entirely, leaving stale or incomplete RUN mappings
			// in place no matter how many times the mob was reimported.
			if (UBlueprint* RefreshedBlueprint = RefreshExistingMobDefinition(DestinationRoot, Record))
			{
				AMT2Mob* Defaults = RefreshedBlueprint->GeneratedClass
					? Cast<AMT2Mob>(RefreshedBlueprint->GeneratedClass->GetDefaultObject()) : nullptr;
				if (USkeletalMeshComponent* MeshComponent = Defaults ? Defaults->GetMesh() : nullptr)
				{
					MeshComponent->SetSkeletalMeshAsset(Mesh);
					MeshComponent->SetAnimInstanceClass(AnimBlueprint->GeneratedClass);
					ApplyMobMaterialOverrides(
						DestinationRoot, Record, Mesh, MeshComponent, PackagesToSave);
					FKismetEditorUtilities::CompileBlueprint(RefreshedBlueprint);
					RefreshedBlueprint->MarkPackageDirty();
				}
				PackagesToSave.AddUnique(RefreshedBlueprint->GetOutermost());
				OutResult.DefinitionsRefreshed++;
			}
			else
			{
				OutResult.Skipped++;
			}
			continue;
		}

		if (UBlueprint* Blueprint = CreateMobBlueprint(
			DestinationRoot, Record, Mesh, AnimBlueprint, Motions, PackagesToSave))
		{
			OutResult.MobBlueprintsCreated++;
			PackagesToSave.AddUnique(Blueprint->GetOutermost());
			PackagesToSave.AddUnique(AnimBlueprint->GetOutermost());
		}
		else
		{
			OutResult.Errors.Add(FString::Printf(TEXT("%d: Blueprint creation failed."), Record.Definition.Vnum));
		}
	}

	if (!PackagesToSave.IsEmpty())
	{
		UEditorLoadingAndSavingUtils::SavePackages(PackagesToSave, true);
	}
	TArray<FString> LootWarnings;
	ImportMobLootEntries(SourceRoot, DestinationRoot, LootWarnings);
	for (const FString& Warning : LootWarnings)
	{
		UE_LOG(LogTemp, Warning, TEXT("MT2 loot import: %s"), *Warning);
	}

	// Shops live in the server DB dump, keyed by npc vnum.
	const FString DatabasePath = UMT2PathSettings::Path(TEXT("Legacy_db"));
	TArray<FString> ShopWarnings;
	ImportNpcShops(DatabasePath, DestinationRoot, ShopWarnings);
	for (const FString& Warning : ShopWarnings)
	{
		UE_LOG(LogTemp, Warning, TEXT("MT2 shop import: %s"), *Warning);
	}
	return OutResult.Errors.IsEmpty();
}
