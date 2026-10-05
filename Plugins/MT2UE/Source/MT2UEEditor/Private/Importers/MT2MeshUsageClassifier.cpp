/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "Importers/MT2MeshUsageClassifier.h"

#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
	FString NormalizeAbsolutePath(FString Path)
	{
		FPaths::NormalizeFilename(Path);
		FPaths::CollapseRelativeDirectories(Path);
		return FPaths::ConvertRelativePathToFull(Path).ToLower();
	}

	bool ReadSetting(FString Line, FString& OutKey, FString& OutValue)
	{
		Line.TrimStartAndEndInline();
		if (Line.IsEmpty() || Line.StartsWith(TEXT("#")) || Line.StartsWith(TEXT("//")))
		{
			return false;
		}

		int32 Separator = INDEX_NONE;
		for (int32 Index = 0; Index < Line.Len(); ++Index)
		{
			if (FChar::IsWhitespace(Line[Index]))
			{
				Separator = Index;
				break;
			}
		}
		if (Separator == INDEX_NONE)
		{
			return false;
		}

		OutKey = Line.Left(Separator);
		OutValue = Line.Mid(Separator).TrimStartAndEnd().TrimQuotes();
		return !OutKey.IsEmpty() && !OutValue.IsEmpty();
	}

	bool IsPlayerScript(const FMT2AssetRecord& Script)
	{
		static const TSet<FString> PlayerScripts = {
			TEXT("warrior_m"), TEXT("warrior_w"), TEXT("assassin_m"), TEXT("assassin_w"),
			TEXT("sura_m"), TEXT("sura_w"), TEXT("shaman_m"), TEXT("shaman_w")
		};
		FString NormalizedPath = Script.AbsolutePath;
		NormalizedPath.ReplaceInline(TEXT("\\"), TEXT("/"));
		NormalizedPath = NormalizedPath.ToLower();
		return PlayerScripts.Contains(FPaths::GetBaseFilename(Script.AbsolutePath).ToLower()) ||
			NormalizedPath.Contains(TEXT("/ymir work/pc/"), ESearchCase::CaseSensitive) ||
			NormalizedPath.Contains(TEXT("/ymir work/pc2/"), ESearchCase::CaseSensitive);
	}

	void ReadNpcResources(const FString& SourceRoot, TSet<FString>& OutResources, TArray<FString>& OutWarnings)
	{
		FString Text;
		const FString NpcListPath = SourceRoot / TEXT("npclist.txt");
		if (!FFileHelper::LoadFileToString(Text, *NpcListPath))
		{
			OutWarnings.Add(FString::Printf(TEXT("Could not read NPC mesh usage table: %s"), *NpcListPath));
			return;
		}

		TArray<FString> Lines;
		Text.ParseIntoArrayLines(Lines, true);
		for (FString Line : Lines)
		{
			Line.TrimStartAndEndInline();
			TArray<FString> Tokens;
			Line.ParseIntoArrayWS(Tokens);
			if (Tokens.Num() >= 2)
			{
				OutResources.Add(Tokens.Last().ToLower());
			}
		}
	}

	bool IsNpcOrMobScript(const FMT2AssetRecord& Script, const TSet<FString>& Resources)
	{
		const FString ScriptName = FPaths::GetBaseFilename(Script.AbsolutePath).ToLower();
		const FString DirectoryName = FPaths::GetCleanFilename(FPaths::GetPath(Script.AbsolutePath)).ToLower();
		return Resources.Contains(ScriptName) || Resources.Contains(DirectoryName);
	}

	const FMT2AssetRecord* ResolveModel(
		const FString& SourceRoot,
		const FString& ScriptPath,
		const FString& BasePath,
		FString Reference,
		const TMap<FString, const FMT2AssetRecord*>& RecordsByAbsolutePath)
	{
		Reference.ReplaceInline(TEXT("\\"), TEXT("/"));
		FString Combined = Reference;
		if (FPaths::IsRelative(Combined))
		{
			FString EffectiveBase = BasePath;
			EffectiveBase.ReplaceInline(TEXT("\\"), TEXT("/"));
			if (EffectiveBase.IsEmpty())
			{
				EffectiveBase = FPaths::GetPath(ScriptPath);
			}
			else if (FPaths::IsRelative(EffectiveBase))
			{
				EffectiveBase = FPaths::GetPath(ScriptPath) / EffectiveBase;
			}
			Combined = EffectiveBase / Combined;
		}

		Combined.ReplaceInline(TEXT("\\"), TEXT("/"));
		const int32 YmirWorkIndex = Combined.Find(TEXT("ymir work/"), ESearchCase::IgnoreCase);
		TArray<FString> Candidates;
		if (YmirWorkIndex != INDEX_NONE)
		{
			const FString YmirRelative = Combined.Mid(YmirWorkIndex);
			Candidates.Add(SourceRoot / YmirRelative);
			Candidates.Add(SourceRoot / YmirRelative.RightChop(10));
		}
		else
		{
			Candidates.Add(Combined);
		}

		for (const FString& Candidate : Candidates)
		{
			const FString Normalized = NormalizeAbsolutePath(Candidate);
			if (const FMT2AssetRecord* const* Record = RecordsByAbsolutePath.Find(Normalized))
			{
				return *Record;
			}
		}
		return nullptr;
	}

	void ParseModelScript(
		const FString& SourceRoot,
		const FMT2AssetRecord& Script,
		const TMap<FString, const FMT2AssetRecord*>& RecordsByAbsolutePath,
		TSet<FString>& OutModelPaths,
		FString& OutCanonicalModelPath)
	{
		OutModelPaths.Reset();
		OutCanonicalModelPath.Reset();
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *Script.AbsolutePath))
		{
			return;
		}

		FString PathName;
		FString SpecialPath;
		TArray<FString> Lines;
		Text.ParseIntoArrayLines(Lines, true);
		for (FString Line : Lines)
		{
			FString Key;
			FString Value;
			if (!ReadSetting(MoveTemp(Line), Key, Value))
			{
				continue;
			}

			if (Key.Equals(TEXT("BaseModelFileName"), ESearchCase::IgnoreCase))
			{
				if (const FMT2AssetRecord* Model = ResolveModel(
					SourceRoot, Script.AbsolutePath, FString(), Value, RecordsByAbsolutePath))
				{
					OutCanonicalModelPath = NormalizeAbsolutePath(Model->AbsolutePath);
					OutModelPaths.Add(OutCanonicalModelPath);
				}
			}
			else if (Key.Equals(TEXT("PathName"), ESearchCase::IgnoreCase))
			{
				PathName = Value;
			}
			else if (Key.Equals(TEXT("SpecialPath"), ESearchCase::IgnoreCase))
			{
				SpecialPath = Value;
			}
			else if (Key.Equals(TEXT("Model"), ESearchCase::IgnoreCase) ||
				Key.Equals(TEXT("ModelFileName"), ESearchCase::IgnoreCase))
			{
				if (const FMT2AssetRecord* Model = ResolveModel(
					SourceRoot, Script.AbsolutePath,
					SpecialPath.IsEmpty() ? PathName : SpecialPath, Value,
					RecordsByAbsolutePath))
				{
					OutModelPaths.Add(NormalizeAbsolutePath(Model->AbsolutePath));
				}
				SpecialPath.Reset();
			}
		}
	}

	bool DirectoryContainsMotion(
		const FString& ModelPath,
		const TSet<FString>& MotionDirectories)
	{
		const FString ModelDirectory = FPaths::GetPath(ModelPath);
		for (const FString& MotionDirectory : MotionDirectories)
		{
			if (MotionDirectory.Equals(ModelDirectory, ESearchCase::CaseSensitive) ||
				MotionDirectory.StartsWith(ModelDirectory + TEXT("/"), ESearchCase::CaseSensitive))
			{
				return true;
			}
		}
		return false;
	}
}

FMT2MeshUsageClassifier::FMT2MeshUsageClassifier(const FMT2AssetScanResult* ScanResult)
{
	if (!ScanResult || ScanResult->RootDirectory.IsEmpty())
	{
		Warnings.Add(TEXT("No asset scan was provided; skeletal meshes will be kept conservatively."));
		return;
	}

	TMap<FString, const FMT2AssetRecord*> GrannyRecordsByAbsolutePath;
	for (const FMT2AssetRecord& Record : ScanResult->Records)
	{
		if (Record.Kind == EMT2AssetKind::Granny)
		{
			GrannyRecordsByAbsolutePath.FindOrAdd(NormalizeAbsolutePath(Record.AbsolutePath), &Record);
		}
	}

	TSet<FString> NpcResources;
	ReadNpcResources(ScanResult->RootDirectory, NpcResources, Warnings);
	TSet<FString> MotionDirectories;
	TSet<FString> SelfCanonicalModelPaths;
	for (const FMT2AssetRecord& Record : ScanResult->Records)
	{
		if (Record.Kind == EMT2AssetKind::MotionScript &&
			Record.Extension.Equals(TEXT("msa"), ESearchCase::IgnoreCase))
		{
			MotionDirectories.Add(FPaths::GetPath(NormalizeAbsolutePath(Record.AbsolutePath)));
		}
	}

	for (const FMT2AssetRecord& Script : ScanResult->Records)
	{
		if (Script.Kind != EMT2AssetKind::ModelScript)
		{
			continue;
		}

		TSet<FString> ScriptModelPaths;
		FString CanonicalModelPath;
		ParseModelScript(ScanResult->RootDirectory, Script,
			GrannyRecordsByAbsolutePath, ScriptModelPaths, CanonicalModelPath);
		if (!CanonicalModelPath.IsEmpty())
		{
			// A model's own BaseModelFileName declaration is authoritative even when an
			// incomplete NPC table prevents this particular script from being classified.
			CanonicalModelPaths.Add(CanonicalModelPath, CanonicalModelPath);
			SelfCanonicalModelPaths.Add(CanonicalModelPath);
		}
		if (IsPlayerScript(Script) || IsNpcOrMobScript(Script, NpcResources))
		{
			CharacterMeshPaths.Append(ScriptModelPaths);
			if (!CanonicalModelPath.IsEmpty())
			{
				for (const FString& ModelPath : ScriptModelPaths)
				{
					const bool bIsSelfCanonical = ModelPath.Equals(
						CanonicalModelPath, ESearchCase::CaseSensitive);
					if (!bIsSelfCanonical && !SelfCanonicalModelPaths.Contains(ModelPath))
					{
						// Race scripts can reference complete mob models for polymorph shapes. Do not
						// let those foreign references replace the mob's own skeleton mapping.
						CanonicalModelPaths.FindOrAdd(ModelPath, CanonicalModelPath);
					}
				}
			}
		}
		if (!CanonicalModelPath.IsEmpty() &&
			DirectoryContainsMotion(CanonicalModelPath, MotionDirectories))
		{
			AnimationBoundMeshPaths.Append(ScriptModelPaths);
		}
	}

	// A missing NPC table would make a destructive classification incomplete. Keep everything skeletal.
	bReady = !NpcResources.IsEmpty() && !CharacterMeshPaths.IsEmpty();
	if (!bReady)
	{
		Warnings.Add(TEXT("Character mesh usage could not be resolved completely; skeletal meshes will be kept conservatively."));
	}
}

bool FMT2MeshUsageClassifier::IsUsedByCharacter(const FMT2AssetRecord& Record) const
{
	return !bReady || CharacterMeshPaths.Contains(NormalizeAbsolutePath(Record.AbsolutePath));
}

FString FMT2MeshUsageClassifier::GetCanonicalModelPath(const FMT2AssetRecord& Record) const
{
	if (const FString* CanonicalPath = CanonicalModelPaths.Find(NormalizeAbsolutePath(Record.AbsolutePath)))
	{
		return *CanonicalPath;
	}
	return FString();
}

bool FMT2MeshUsageClassifier::HasBoundAnimation(
	const FMT2AssetRecord& Record,
	const FMT2GrannyFileInspection& Inspection) const
{
	return Inspection.AnimationCount > 0 || Inspection.TrackGroupCount > 0 ||
		AnimationBoundMeshPaths.Contains(NormalizeAbsolutePath(Record.AbsolutePath));
}

bool FMT2MeshUsageClassifier::RequiresSkeletalMesh(
	const FMT2AssetRecord& Record,
	const FMT2GrannyFileInspection& Inspection) const
{
	return IsUsedByCharacter(Record) || HasBoundAnimation(Record, Inspection);
}
