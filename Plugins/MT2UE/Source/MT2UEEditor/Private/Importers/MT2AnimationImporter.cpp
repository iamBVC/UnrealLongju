/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "Importers/MT2AnimationImporter.h"
#include "Config/MT2PathSettings.h"
#include "Importers/MT2MotionScriptParser.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Animation/AnimData/IAnimationDataController.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#include "Animation/IAnimationSequenceCompiler.h"
#include "Animation/AnimNotifies/AnimNotify_PlaySound.h"
#include "Animation/AnimSequence.h"
#include "Animation/MT2AnimationMotionData.h"
#include "Animation/AnimTypes.h"
#include "Animation/Skeleton.h"
#include "Engine/SkeletalMesh.h"
#include "HAL/FileManager.h"
#include "Importers/MT2AudioImporter.h"
#include "Importers/MT2GrannyMeshConverter.h"
#include "Importers/MT2SkeletalMeshImporter.h"
#include "Logging/LogMacros.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/ScopedSlowTask.h"
#include "Modules/ModuleManager.h"
#include "Sound/SoundBase.h"
#include "UObject/Package.h"

DEFINE_LOG_CATEGORY_STATIC(LogMT2AnimationSound, Log, All);

FMT2AnimationImporter::FMT2AnimationImporter()
	: FMT2ImporterBase(EMT2ImportDomain::Animations, TEXT("AnimationImporter"))
{
}

namespace
{
	FString BuildAnimationObjectPath(const FMT2ImportContext& Context, const FMT2AssetRecord& Record)
	{
		const FString PackagePath = FMT2AssetScanner::BuildContentPackagePath(Context.DestinationRoot, Record);
		FString AssetName = FMT2AssetScanner::SanitizePackagePathSegment(FPaths::GetBaseFilename(Record.ContentPath));
		if (!AssetName.StartsWith(TEXT("A_"), ESearchCase::IgnoreCase))
		{
			AssetName = TEXT("A_") + AssetName;
		}
		return PackagePath / AssetName + TEXT(".") + AssetName;
	}

	FString BuildAnimationSourceKey(const FMT2AssetRecord& Record)
	{
		FString Key = FPaths::ChangeExtension(Record.ContentPath, TEXT(""));
		Key.ReplaceInline(TEXT("\\"), TEXT("/"));
		return Key.ToLower();
	}

	bool ReadQuotedSetting(const FString& ScriptPath, const FString& SettingName, FString& OutValue)
	{
		OutValue.Reset();
		FString Script;
		if (!FFileHelper::LoadFileToString(Script, *ScriptPath))
		{
			return false;
		}

		TArray<FString> Lines;
		Script.ParseIntoArrayLines(Lines, false);
		for (FString Line : Lines)
		{
			Line.TrimStartAndEndInline();
			if (!Line.StartsWith(SettingName, ESearchCase::IgnoreCase))
			{
				continue;
			}

			int32 FirstQuote = INDEX_NONE;
			int32 LastQuote = INDEX_NONE;
			if (Line.FindChar(TEXT('"'), FirstQuote) && Line.FindLastChar(TEXT('"'), LastQuote) && LastQuote > FirstQuote)
			{
				OutValue = Line.Mid(FirstQuote + 1, LastQuote - FirstQuote - 1);
				return !OutValue.IsEmpty();
			}
		}
		return false;
	}

	bool ReadMotionFileName(const FString& MotionScriptPath, FString& OutMotionFileName)
	{
		return ReadQuotedSetting(MotionScriptPath, TEXT("MotionFileName"), OutMotionFileName);
	}

	bool ReadMotionFloat(const FString& Script, const FString& SettingName, float& OutValue)
	{
		TArray<FString> Lines;
		Script.ParseIntoArrayLines(Lines, false);
		for (FString Line : Lines)
		{
			Line.TrimStartAndEndInline();
			if (!Line.StartsWith(SettingName, ESearchCase::IgnoreCase))
			{
				continue;
			}
			TArray<FString> Tokens;
			Line.ParseIntoArrayWS(Tokens);
			if (Tokens.Num() >= 2)
			{
				OutValue = FCString::Atof(*Tokens[1]);
				return true;
			}
		}
		return false;
	}

	bool ReadMotionVector(const FString& Script, const FString& SettingName, FVector& OutValue)
	{
		TArray<FString> Lines;
		Script.ParseIntoArrayLines(Lines, false);
		for (FString Line : Lines)
		{
			Line.TrimStartAndEndInline();
			if (!Line.StartsWith(SettingName, ESearchCase::IgnoreCase)) continue;
			TArray<FString> Tokens;
			Line.ParseIntoArrayWS(Tokens);
			if (Tokens.Num() >= 4)
			{
				OutValue = FVector(
					FCString::Atof(*Tokens[1]), FCString::Atof(*Tokens[2]), FCString::Atof(*Tokens[3]));
				return true;
			}
		}
		return false;
	}

	void ApplyMotionMetadata(
		const FMT2ImportContext& Context, const FMT2AssetRecord& Record, UAnimSequence* AnimSequence)
	{
		if (!AnimSequence || !Record.Extension.Equals(TEXT("msa"), ESearchCase::IgnoreCase)) return;

		FString Script;
		if (!FFileHelper::LoadFileToString(Script, *Record.AbsolutePath)) return;

		UMT2AnimationMotionData* MotionData = AnimSequence->GetAssetUserData<UMT2AnimationMotionData>();
		if (!MotionData)
		{
			MotionData = NewObject<UMT2AnimationMotionData>(AnimSequence);
			AnimSequence->AddAssetUserData(MotionData);
		}
		ReadMotionFloat(Script, TEXT("MotionDuration"), MotionData->MotionDuration);
		// .msa AttackingData: knockback strength + hit kind (1 = blow, 2 = normal).
		UMT2AnimationMotionData::ReadKnockbackMetadata(Script, MotionData->ExternalForce, MotionData->HittingType);
		UMT2AnimationMotionData::ReadAttackEvents(Script, MotionData->AttackEvents);
		MotionData->bHasAccumulation = ReadMotionVector(Script, TEXT("Accumulation"), MotionData->Accumulation) &&
			!MotionData->Accumulation.IsNearlyZero();
		MotionData->bHasComboInputData =
			ReadMotionFloat(Script, TEXT("DirectInputTime"), MotionData->DirectInputTime);
		ReadMotionFloat(Script, TEXT("PreInputTime"), MotionData->PreInputTime);
		ReadMotionFloat(Script, TEXT("InputLimitTime"), MotionData->InputLimitTime);
		ReadMotionFloat(Script, TEXT("LinkTime"), MotionData->LinkTime);
		MT2MotionScriptParser::PopulateEffectEvents(
			Script, Record, Context.DestinationRoot, MotionData);

		// Metin2 applies .msa accumulation through gameplay movement while retaining the authored root
		// transform in the visual pose. UE root extraction/locking distorts Granny locomotion.
		AnimSequence->bEnableRootMotion = false;
		AnimSequence->RootMotionRootLock = ERootMotionRootLock::RefPose;
		AnimSequence->bForceRootLock = false;
	}

	FString NormalizeSourcePath(FString Path)
	{
		Path.ReplaceInline(TEXT("\\"), TEXT("/"));
		while (Path.Contains(TEXT("//")))
		{
			Path.ReplaceInline(TEXT("//"), TEXT("/"));
		}
		return Path.ToLower();
	}

	FString NormalizeCanonicalSourcePath(FString Path)
	{
		Path = NormalizeSourcePath(MoveTemp(Path));
		Path.RemoveFromStart(TEXT("d:/"), ESearchCase::IgnoreCase);
		Path.RemoveFromStart(TEXT("/"));
		return Path;
	}

	struct FMT2MotionSoundEvent
	{
		float TimeSeconds = 0.0f;
		FString SoundRelativePath;
	};

	// The client keys per-frame motion sounds by a "sound/<same relative path>.mss" sidecar
	// next to each "ymir work/<...>.msa" motion script (e.g. ymir work/monster/stray_dog/20.msa
	// -> sound/monster/stray_dog/20.mss), rather than embedding them in the motion data itself.
	FString FindMotionSoundScriptPath(const FString& MotionScriptAbsolutePath)
	{
		FString NormalizedPath = MotionScriptAbsolutePath;
		NormalizedPath.ReplaceInline(TEXT("\\"), TEXT("/"));

		static const FString YmirWorkMarker = TEXT("/ymir work/");
		const int32 MarkerIndex = NormalizedPath.Find(YmirWorkMarker, ESearchCase::IgnoreCase);
		if (MarkerIndex == INDEX_NONE)
		{
			return FString();
		}

		const FString SourceRoot = NormalizedPath.Left(MarkerIndex);
		const FString RelativeToMotionRoot = NormalizedPath.Mid(MarkerIndex + YmirWorkMarker.Len());
		return SourceRoot / UMT2PathSettings::Path(TEXT("Part_sound")) / FPaths::ChangeExtension(RelativeToMotionRoot, TEXT("mss"));
	}

	bool ParseMotionSoundScript(const FString& SoundScriptPath, TArray<FMT2MotionSoundEvent>& OutEvents)
	{
		FString Script;
		if (!FFileHelper::LoadFileToString(Script, *SoundScriptPath))
		{
			return false;
		}

		TArray<FString> Lines;
		Script.ParseIntoArrayLines(Lines, false);
		for (FString Line : Lines)
		{
			Line.TrimStartAndEndInline();
			if (!Line.StartsWith(TEXT("SoundData"), ESearchCase::IgnoreCase))
			{
				continue;
			}

			int32 FirstQuote = INDEX_NONE;
			int32 LastQuote = INDEX_NONE;
			if (!Line.FindChar(TEXT('"'), FirstQuote) || !Line.FindLastChar(TEXT('"'), LastQuote) || LastQuote <= FirstQuote)
			{
				continue;
			}

			TArray<FString> Tokens;
			Line.Left(FirstQuote).ParseIntoArrayWS(Tokens);
			if (Tokens.Num() < 2)
			{
				continue;
			}

			FMT2MotionSoundEvent& Event = OutEvents.AddDefaulted_GetRef();
			Event.TimeSeconds = FCString::Atof(*Tokens.Last());
			Event.SoundRelativePath = Line.Mid(FirstQuote + 1, LastQuote - FirstQuote - 1);
		}
		return OutEvents.Num() > 0;
	}

	// Finds the scanned Audio record for a sound referenced by relative path (e.g.
	// "sound/monster/stray_dog/attack_1.wav"), the same way ResolveGrannyAnimationPath/
	// FindBestImportedSkeleton match records below - by filename plus best path suffix. This is
	// deliberately NOT a hand-built path: reusing the scanner's own record and
	// FMT2AudioImporter::BuildObjectPath guarantees we compute the exact same object path the
	// Audio importer used when it actually created the asset, instead of re-deriving it from the
	// raw (decades-old, inconsistently-cased) string embedded in the .mss file.
	const FMT2AssetRecord* FindAudioRecordForRelativePath(const FMT2ImportContext& Context, const FString& SoundRelativePath)
	{
		if (!Context.ScanResult)
		{
			return nullptr;
		}

		const FString NormalizedReference = NormalizeSourcePath(SoundRelativePath);
		const FString ReferenceFileName = FPaths::GetCleanFilename(NormalizedReference);

		const FMT2AssetRecord* BestRecord = nullptr;
		int32 BestSuffixLength = INDEX_NONE;
		for (const FMT2AssetRecord& Candidate : Context.ScanResult->Records)
		{
			if (Candidate.Kind != EMT2AssetKind::Audio ||
				!FPaths::GetCleanFilename(Candidate.AbsolutePath).Equals(ReferenceFileName, ESearchCase::IgnoreCase))
			{
				continue;
			}

			const TArray<FString> CandidatePaths =
			{
				NormalizeSourcePath(Candidate.VirtualPath),
				NormalizeSourcePath(Candidate.ContentPath),
				NormalizeSourcePath(Candidate.RelativePath)
			};
			int32 SuffixLength = 0;
			for (const FString& CandidatePath : CandidatePaths)
			{
				if (NormalizedReference.EndsWith(CandidatePath, ESearchCase::IgnoreCase))
				{
					SuffixLength = FMath::Max(SuffixLength, CandidatePath.Len());
				}
			}
			if (SuffixLength > BestSuffixLength)
			{
				BestSuffixLength = SuffixLength;
				BestRecord = &Candidate;
			}
		}
		return BestRecord;
	}

	void ApplyMotionSoundNotifies(
		const FMT2ImportRequest& Request,
		const FMT2AssetRecord& Record,
		UAnimSequence* AnimSequence,
		FMT2ImportResult& OutResult)
	{
		if (!Record.Extension.Equals(TEXT("msa"), ESearchCase::IgnoreCase))
		{
			return;
		}

		const FString SoundScriptPath = FindMotionSoundScriptPath(Record.AbsolutePath);
		if (SoundScriptPath.IsEmpty() || !IFileManager::Get().FileExists(*SoundScriptPath))
		{
			return;
		}

		TArray<FMT2MotionSoundEvent> SoundEvents;
		if (!ParseMotionSoundScript(SoundScriptPath, SoundEvents))
		{
			return;
		}

		AnimSequence->Notifies.Reset();
		for (const FMT2MotionSoundEvent& SoundEvent : SoundEvents)
		{
			const FMT2AssetRecord* AudioRecord = FindAudioRecordForRelativePath(Request.Context, SoundEvent.SoundRelativePath);
			if (!AudioRecord)
			{
				UE_LOG(LogMT2AnimationSound, Warning,
					TEXT("[MT2Sound] No scanned Audio asset matches motion sound reference '%s' (from %s). Re-run Scan and make sure the source .wav is under the scanned dump before importing Audio."),
					*SoundEvent.SoundRelativePath, *SoundScriptPath);
				OutResult.AddWarning(FString::Printf(
					TEXT("Motion sound event references '%s' which was not found in the scan results."),
					*SoundEvent.SoundRelativePath), SoundScriptPath);
				continue;
			}

			const FString SoundObjectPath = FMT2AudioImporter::BuildObjectPath(Request.Context, *AudioRecord);
			USoundBase* Sound = LoadObject<USoundBase>(nullptr, *SoundObjectPath);
			if (!Sound)
			{
				UE_LOG(LogMT2AnimationSound, Warning,
					TEXT("[MT2Sound] Sound asset not found at expected path '%s' for motion sound reference '%s' (from %s). Import Audio for this asset first."),
					*SoundObjectPath, *SoundEvent.SoundRelativePath, *SoundScriptPath);
				OutResult.AddWarning(FString::Printf(
					TEXT("Motion sound event references unimported sound '%s' (expected at %s). Import Audio for this asset first."),
					*SoundEvent.SoundRelativePath, *SoundObjectPath), SoundScriptPath);
				continue;
			}

			FAnimNotifyEvent& NotifyEvent = AnimSequence->Notifies.AddDefaulted_GetRef();
			NotifyEvent.NotifyName = FName(*FPaths::GetBaseFilename(SoundEvent.SoundRelativePath));
			NotifyEvent.TrackIndex = 0;
			UAnimNotify_PlaySound* PlaySoundNotify = NewObject<UAnimNotify_PlaySound>(AnimSequence, NAME_None, RF_Transactional);
			PlaySoundNotify->Sound = Sound;
			NotifyEvent.Notify = PlaySoundNotify;
			NotifyEvent.SetTime(SoundEvent.TimeSeconds);

			// A one-shot notify sitting exactly at time 0 (extremely common here - many .mss entries
			// use StartingTime 0.000000) never fires on the very first playback: UAnimSequenceBase::
			// GetAnimNotifiesFromDeltaPositions requires NotifyEndTime > PreviousPosition, and on the
			// first tick PreviousPosition is also 0, so the strict inequality fails. The editor works
			// around this by nudging such notifies via a small TriggerTimeOffset whenever they're
			// touched in the timeline (which is why manually moving one and moving it back "fixes" it) -
			// CalculateOffsetForNotify()/RefreshTriggerOffset() is that exact mechanism, applied here so
			// imported notifies get it automatically instead of needing a manual nudge per animation.
			NotifyEvent.RefreshTriggerOffset(AnimSequence->CalculateOffsetForNotify(NotifyEvent.GetTime()));
		}
		AnimSequence->InitializeNotifyTrack();
	}

	bool ResolveGrannyAnimationPath(const FMT2ImportRequest& Request, const FMT2AssetRecord& Record, FString& OutGrannyPath, FString& OutError)
	{
		OutGrannyPath.Reset();
		OutError.Reset();
		if (Record.Kind == EMT2AssetKind::Granny)
		{
			OutGrannyPath = Record.AbsolutePath;
			return true;
		}

		if (Record.Kind != EMT2AssetKind::MotionScript)
		{
			OutError = TEXT("Selected record is not a Granny animation or motion script.");
			return false;
		}

		const FString SiblingGrannyPath = FPaths::ChangeExtension(Record.AbsolutePath, TEXT("gr2"));
		if (IFileManager::Get().FileExists(*SiblingGrannyPath))
		{
			OutGrannyPath = SiblingGrannyPath;
			return true;
		}

		FString MotionFileName;
		if (!ReadMotionFileName(Record.AbsolutePath, MotionFileName))
		{
			OutError = TEXT("MotionFileName was not found in the motion script.");
			return false;
		}

		FString RelativeReference = NormalizeSourcePath(MotionFileName);
		RelativeReference.RemoveFromStart(TEXT("d:/"), ESearchCase::IgnoreCase);
		RelativeReference.RemoveFromStart(TEXT("ymir work/"), ESearchCase::IgnoreCase);
		const FString RootCandidate = Request.Context.SourceRoot / RelativeReference;
		if (IFileManager::Get().FileExists(*RootCandidate))
		{
			OutGrannyPath = RootCandidate;
			return true;
		}

		if (Request.Context.ScanResult)
		{
			const FString NormalizedReference = NormalizeSourcePath(MotionFileName);
			const FString ReferenceFileName = FPaths::GetCleanFilename(NormalizedReference);
			int32 BestSuffixLength = INDEX_NONE;
			for (const FMT2AssetRecord& Candidate : Request.Context.ScanResult->Records)
			{
				if (Candidate.Kind != EMT2AssetKind::Granny ||
					!FPaths::GetCleanFilename(Candidate.AbsolutePath).Equals(ReferenceFileName, ESearchCase::IgnoreCase))
				{
					continue;
				}

				const FString CandidateContentPath = NormalizeSourcePath(Candidate.ContentPath);
				const int32 SuffixLength = NormalizedReference.EndsWith(CandidateContentPath, ESearchCase::IgnoreCase)
					? CandidateContentPath.Len()
					: 0;
				if (SuffixLength > BestSuffixLength)
				{
					BestSuffixLength = SuffixLength;
					OutGrannyPath = Candidate.AbsolutePath;
				}
			}
		}

		if (OutGrannyPath.IsEmpty())
		{
			OutError = FString::Printf(TEXT("Could not resolve referenced Granny animation: %s"), *MotionFileName);
			return false;
		}
		return true;
	}

	int32 CountCommonPathSegments(const FString& A, const FString& B)
	{
		TArray<FString> AParts;
		TArray<FString> BParts;
		A.ParseIntoArray(AParts, TEXT("/"), true);
		B.ParseIntoArray(BParts, TEXT("/"), true);
		const int32 Count = FMath::Min(AParts.Num(), BParts.Num());
		int32 CommonCount = 0;
		for (int32 Index = 0; Index < Count; ++Index)
		{
			if (!AParts[Index].Equals(BParts[Index], ESearchCase::IgnoreCase))
			{
				break;
			}
			CommonCount++;
		}
		return CommonCount;
	}

	FString FindSourceGrannyForPreviewMesh(const FMT2ImportContext& Context, const USkeletalMesh* PreviewMesh)
	{
		if (!Context.ScanResult || !PreviewMesh)
		{
			return FString();
		}

		const FString PreviewObjectPath = PreviewMesh->GetPathName();
		const FString PreviewPackagePath = FPackageName::GetLongPackagePath(PreviewMesh->GetOutermost()->GetName());
		FString BestSourcePath;
		int32 BestScore = INDEX_NONE;
		for (const FMT2AssetRecord& Candidate : Context.ScanResult->Records)
		{
			if (Candidate.Kind != EMT2AssetKind::Granny)
			{
				continue;
			}

			const FString BasePackagePath = FMT2AssetScanner::BuildContentPackagePath(Context.DestinationRoot, Candidate);
			const FString BaseName = FMT2AssetScanner::SanitizePackagePathSegment(FPaths::GetBaseFilename(Candidate.ContentPath));
			const FString AssetName = BaseName.StartsWith(TEXT("SK_"), ESearchCase::IgnoreCase) ? BaseName : TEXT("SK_") + BaseName;
			if (!PreviewMesh->GetName().Equals(AssetName, ESearchCase::IgnoreCase))
			{
				continue;
			}

			TArray<FString> CandidateObjectPaths;
			CandidateObjectPaths.Add(BasePackagePath / AssetName + TEXT(".") + AssetName);
			CandidateObjectPaths.Add(BasePackagePath / UMT2PathSettings::Path(TEXT("Part_SkeletalMeshes")) / AssetName + TEXT(".") + AssetName);
			CandidateObjectPaths.Add(BasePackagePath / BaseName / UMT2PathSettings::Path(TEXT("Part_SkeletalMeshes")) / AssetName + TEXT(".") + AssetName);

			int32 Score = CountCommonPathSegments(BasePackagePath, PreviewPackagePath) * 100;
			for (const FString& CandidateObjectPath : CandidateObjectPaths)
			{
				if (CandidateObjectPath.Equals(PreviewObjectPath, ESearchCase::IgnoreCase))
				{
					Score += 1000000;
					break;
				}
			}
			if (Score > BestScore)
			{
				BestScore = Score;
				BestSourcePath = Candidate.AbsolutePath;
			}
		}
		return BestSourcePath;
	}

	struct FCanonicalModelMappingIndex
	{
		TMap<FString, const FMT2AssetRecord*> ModelsBySourceDirectory;
	};

	void AddCanonicalModelDirectory(
		FCanonicalModelMappingIndex& Index,
		const FString& SourcePath,
		const FMT2AssetRecord* ModelRecord)
	{
		const FString Directory = NormalizeCanonicalSourcePath(FPaths::GetPath(SourcePath));
		if (!Directory.IsEmpty() && ModelRecord)
		{
			Index.ModelsBySourceDirectory.FindOrAdd(Directory, ModelRecord);
		}
	}

	FCanonicalModelMappingIndex BuildCanonicalModelMappings(const FMT2AssetScanResult& ScanResult)
	{
		FCanonicalModelMappingIndex Index;
		TMap<FString, const FMT2AssetRecord*> GrannyRecordsByPath;
		for (const FMT2AssetRecord& Record : ScanResult.Records)
		{
			if (Record.Kind != EMT2AssetKind::Granny)
			{
				continue;
			}
			GrannyRecordsByPath.FindOrAdd(NormalizeCanonicalSourcePath(Record.VirtualPath), &Record);
			GrannyRecordsByPath.FindOrAdd(NormalizeCanonicalSourcePath(Record.ContentPath), &Record);
			GrannyRecordsByPath.FindOrAdd(NormalizeCanonicalSourcePath(Record.RelativePath), &Record);
		}

		for (const FMT2AssetRecord& ScriptRecord : ScanResult.Records)
		{
			if (ScriptRecord.Kind != EMT2AssetKind::ModelScript)
			{
				continue;
			}

			FString BaseModelReference;
			if (!ReadQuotedSetting(ScriptRecord.AbsolutePath, TEXT("BaseModelFileName"), BaseModelReference))
			{
				continue;
			}

			const FString NormalizedReference = NormalizeCanonicalSourcePath(BaseModelReference);
			const FMT2AssetRecord* const* ExactRecord = GrannyRecordsByPath.Find(NormalizedReference);
			const FMT2AssetRecord* ModelRecord = ExactRecord ? *ExactRecord : nullptr;
			if (!ModelRecord)
			{
				const FString ReferenceFileName = FPaths::GetCleanFilename(NormalizedReference);
				int32 BestSuffixLength = INDEX_NONE;
				for (const TPair<FString, const FMT2AssetRecord*>& Candidate : GrannyRecordsByPath)
				{
					if (!Candidate.Value ||
						!FPaths::GetCleanFilename(Candidate.Key).Equals(ReferenceFileName, ESearchCase::IgnoreCase))
					{
						continue;
					}
					const int32 SuffixLength = NormalizedReference.EndsWith(Candidate.Key, ESearchCase::IgnoreCase)
						? Candidate.Key.Len()
						: 0;
					if (SuffixLength > BestSuffixLength)
					{
						BestSuffixLength = SuffixLength;
						ModelRecord = Candidate.Value;
					}
				}
			}

			if (ModelRecord)
			{
				AddCanonicalModelDirectory(Index, NormalizedReference, ModelRecord);
				AddCanonicalModelDirectory(Index, ModelRecord->VirtualPath, ModelRecord);
				AddCanonicalModelDirectory(Index, ModelRecord->ContentPath, ModelRecord);
				AddCanonicalModelDirectory(Index, ModelRecord->RelativePath, ModelRecord);
			}
		}
		return Index;
	}

	const FMT2AssetRecord* FindCanonicalModelRecord(
		const FCanonicalModelMappingIndex& Index,
		const FMT2AssetRecord& AnimationRecord)
	{
		const FString SourcePaths[] =
		{
			AnimationRecord.VirtualPath,
			AnimationRecord.ContentPath,
			AnimationRecord.RelativePath
		};
		for (const FString& SourcePath : SourcePaths)
		{
			FString Directory = NormalizeCanonicalSourcePath(FPaths::GetPath(SourcePath));
			while (!Directory.IsEmpty())
			{
				if (const FMT2AssetRecord* const* ModelRecord = Index.ModelsBySourceDirectory.Find(Directory))
				{
					return *ModelRecord;
				}
				const FString ParentDirectory = FPaths::GetPath(Directory);
				if (ParentDirectory == Directory)
				{
					break;
				}
				Directory = ParentDirectory;
			}
		}
		return nullptr;
	}

	struct FAnimationSkeletonMatch
	{
		USkeleton* Skeleton = nullptr;
		USkeletalMesh* PreviewMesh = nullptr;
		FString GrannyModelPath;
		int32 MatchedTrackCount = 0;
		int64 Score = MIN_int64;
	};

	FAnimationSkeletonMatch FindBestImportedSkeleton(
		const FMT2ImportContext& Context,
		const FMT2AssetRecord& Record,
		const FMT2GrannyAnimationClip& Clip,
		const FCanonicalModelMappingIndex& CanonicalModelMappings)
	{
		FAnimationSkeletonMatch BestMatch;
		const FMT2AssetRecord* CanonicalModelRecord = FindCanonicalModelRecord(CanonicalModelMappings, Record);

		if (CanonicalModelRecord)
		{
			USkeletalMesh* CanonicalMesh = FMT2SkeletalMeshImporter::LoadImportedMesh(Context, *CanonicalModelRecord);
			USkeleton* CanonicalSkeleton = CanonicalMesh ? CanonicalMesh->GetSkeleton() : nullptr;
			if (!CanonicalSkeleton)
			{
				return BestMatch;
			}

			const FReferenceSkeleton& RefSkeleton = CanonicalSkeleton->GetReferenceSkeleton();
			for (const FMT2GrannyAnimationTrack& Track : Clip.Tracks)
			{
				if (RefSkeleton.FindBoneIndex(FName(*Track.BoneName)) != INDEX_NONE)
				{
					BestMatch.MatchedTrackCount++;
				}
			}
			if (BestMatch.MatchedTrackCount > 0)
			{
				BestMatch.Skeleton = CanonicalSkeleton;
				BestMatch.PreviewMesh = CanonicalMesh;
				BestMatch.GrannyModelPath = CanonicalModelRecord->AbsolutePath;
				BestMatch.Score = MAX_int64;
			}
			return BestMatch;
		}

		FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		TArray<FAssetData> SkeletalMeshAssets;
		AssetRegistryModule.Get().GetAssetsByClass(USkeletalMesh::StaticClass()->GetClassPathName(), SkeletalMeshAssets, true);

		FString DestinationRoot = Context.DestinationRoot;
		DestinationRoot.RemoveFromEnd(TEXT("/"));
		const FString AnimationPackagePath = FMT2AssetScanner::BuildContentPackagePath(DestinationRoot, Record);
		const FString SanitizedTrackGroupName = Clip.TrackGroupName.IsEmpty()
			? FString()
			: FMT2AssetScanner::SanitizePackagePathSegment(Clip.TrackGroupName);

		for (const FAssetData& AssetData : SkeletalMeshAssets)
		{
			const FString CandidatePackagePath = AssetData.PackagePath.ToString();
			if (!CandidatePackagePath.StartsWith(DestinationRoot, ESearchCase::IgnoreCase))
			{
				continue;
			}

			USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(AssetData.GetAsset());
			USkeleton* Skeleton = SkeletalMesh ? SkeletalMesh->GetSkeleton() : nullptr;
			if (!Skeleton)
			{
				continue;
			}

			const FReferenceSkeleton& RefSkeleton = Skeleton->GetReferenceSkeleton();
			int32 MatchedTracks = 0;
			for (const FMT2GrannyAnimationTrack& Track : Clip.Tracks)
			{
				if (RefSkeleton.FindBoneIndex(FName(*Track.BoneName)) != INDEX_NONE)
				{
					MatchedTracks++;
				}
			}
			if (MatchedTracks == 0)
			{
				continue;
			}

			int64 Score = static_cast<int64>(MatchedTracks) * 10000;
			Score += static_cast<int64>(CountCommonPathSegments(AnimationPackagePath, CandidatePackagePath)) * 100;
			if (CandidatePackagePath.Equals(AnimationPackagePath, ESearchCase::IgnoreCase))
			{
				Score += 1000000;
			}
			const FString AssetName = AssetData.AssetName.ToString();
			if (!SanitizedTrackGroupName.IsEmpty() && AssetName.Contains(SanitizedTrackGroupName, ESearchCase::IgnoreCase))
			{
				Score += 500000;
			}
			if (AssetName.Contains(TEXT("_lod"), ESearchCase::IgnoreCase))
			{
				Score -= 1000;
			}

			if (Score > BestMatch.Score)
			{
				BestMatch.Skeleton = Skeleton;
				BestMatch.PreviewMesh = SkeletalMesh;
				BestMatch.MatchedTrackCount = MatchedTracks;
				BestMatch.Score = Score;
			}
		}
		BestMatch.GrannyModelPath = FindSourceGrannyForPreviewMesh(Context, BestMatch.PreviewMesh);
		return BestMatch;
	}

	bool AnimationDataMatchesClip(const UAnimSequence* AnimSequence, const FMT2GrannyAnimationClip& Clip)
	{
		const IAnimationDataModel* DataModel = AnimSequence ? AnimSequence->GetDataModel() : nullptr;
		if (!DataModel || DataModel->GetNumberOfFrames() != Clip.NumberOfFrames)
		{
			return false;
		}

		int32 ComparedTrackCount = 0;
		const int32 FramesToTest[] = { 0, Clip.NumberOfFrames / 2, Clip.NumberOfFrames };
		for (const FMT2GrannyAnimationTrack& Track : Clip.Tracks)
		{
			const FName BoneName(*Track.BoneName);
			if (!DataModel->IsValidBoneTrackName(BoneName))
			{
				continue;
			}

			for (const int32 FrameIndex : FramesToTest)
			{
				if (!Track.TranslationKeys.IsValidIndex(FrameIndex) ||
					!Track.RotationKeys.IsValidIndex(FrameIndex) ||
					!Track.ScaleKeys.IsValidIndex(FrameIndex))
				{
					return false;
				}

				const FTransform ExistingTransform = DataModel->GetBoneTrackTransform(BoneName, FFrameNumber(FrameIndex));
				const FVector ExpectedTranslation(Track.TranslationKeys[FrameIndex]);
				const FVector ExpectedScale(Track.ScaleKeys[FrameIndex]);
				const FQuat ExpectedRotation(Track.RotationKeys[FrameIndex]);
				if (!ExistingTransform.GetTranslation().Equals(ExpectedTranslation, 0.01) ||
					!ExistingTransform.GetScale3D().Equals(ExpectedScale, 0.001) ||
					FMath::Abs(ExistingTransform.GetRotation() | ExpectedRotation) < 0.99999)
				{
					return false;
				}
			}

			ComparedTrackCount++;
			if (ComparedTrackCount >= 3)
			{
				break;
			}
		}
		return ComparedTrackCount > 0;
	}
}

bool FMT2AnimationImporter::Discover(const FMT2ImportContext& Context, const FMT2AssetScanResult& ScanResult, FMT2ImportDiscovery& OutDiscovery) const
{
	OutDiscovery.Domain = GetDomain();
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	const auto IsAlreadyImported = [&Context, &AssetRegistryModule](const FMT2AssetRecord& Record)
	{
		if (Context.bReplaceExisting)
		{
			return false;
		}
		const FString ObjectPath = BuildAnimationObjectPath(Context, Record);
		return AssetRegistryModule.Get().GetAssetByObjectPath(FSoftObjectPath(ObjectPath)).IsValid();
	};

	for (const FMT2AssetRecord& Record : ScanResult.GetRecordsByKind(EMT2AssetKind::MotionScript))
	{
		if (!Record.Extension.Equals(TEXT("msa"), ESearchCase::IgnoreCase) || IsAlreadyImported(Record))
		{
			continue;
		}

		FString MotionFileName;
		if (ReadMotionFileName(Record.AbsolutePath, MotionFileName))
		{
			OutDiscovery.AssetRecords.Add(Record);
		}
	}

	TSet<FString> MotionScriptSourceKeys;
	for (const FMT2AssetRecord& Record : OutDiscovery.AssetRecords)
	{
		MotionScriptSourceKeys.Add(BuildAnimationSourceKey(Record));
	}

	for (const FMT2AssetRecord& Record : ScanResult.GetRecordsByKind(EMT2AssetKind::Granny))
	{
		if (IsAlreadyImported(Record) || MotionScriptSourceKeys.Contains(BuildAnimationSourceKey(Record)))
		{
			continue;
		}

		FMT2GrannyFileInspection Inspection;
		FString Error;
		if (FMT2GrannyMeshConverter::InspectGrannyFile(Record.AbsolutePath, Inspection, Error) &&
			Inspection.Type == EMT2GrannyFileType::Animation)
		{
			OutDiscovery.AssetRecords.Add(Record);
		}
	}
	OutDiscovery.ItemsDiscovered = OutDiscovery.AssetRecords.Num();
	return true;
}

bool FMT2AnimationImporter::Import(const FMT2ImportRequest& Request, FMT2ImportResult& OutResult)
{
	if (!CanImport(Request, OutResult))
	{
		return false;
	}

	if (Request.Selection.AssetRecords.Num() == 0)
	{
		OutResult.AddWarning(TEXT("No animation records were selected."));
		OutResult.bSucceeded = true;
		return true;
	}

	const int32 MaxItems = Request.MaxItems > 0 ? Request.MaxItems : Request.Selection.AssetRecords.Num();
	FScopedSlowTask AnimationProgress(
		static_cast<float>(MaxItems + 1),
		NSLOCTEXT("FMT2AnimationImporter", "ImportAnimationsProgress", "Importing animations..."));
	AnimationProgress.MakeDialog(true);
	AnimationProgress.EnterProgressFrame(
		1.0f,
		NSLOCTEXT("FMT2AnimationImporter", "PrepareAnimationImportProgress", "Preparing animation skeleton mappings..."));

	const FCanonicalModelMappingIndex CanonicalModelMappings = Request.Context.ScanResult
		? BuildCanonicalModelMappings(*Request.Context.ScanResult)
		: FCanonicalModelMappingIndex();
	UE_LOG(LogMT2AnimationSound, Display, TEXT("Prepared %d canonical animation model directories."),
		CanonicalModelMappings.ModelsBySourceDirectory.Num());
	int32 ConsideredCount = 0;
	TSet<FString> ProcessedObjectPaths;
	for (const FMT2AssetRecord& Record : Request.Selection.AssetRecords)
	{
		if (ConsideredCount >= MaxItems)
		{
			break;
		}

		AnimationProgress.EnterProgressFrame(1.0f, FText::Format(
			NSLOCTEXT("FMT2AnimationImporter", "ImportAnimationProgressFormat", "Importing animation {0} of {1}: {2}"),
			FText::AsNumber(ConsideredCount + 1),
			FText::AsNumber(MaxItems),
			FText::FromString(Record.VirtualPath.IsEmpty() ? Record.ContentPath : Record.VirtualPath)));
		if (Request.Context.IsStopRequested() || AnimationProgress.ShouldCancel())
		{
			OutResult.AddWarning(TEXT("Animation import stopped by user."));
			break;
		}

		ConsideredCount++;
		OutResult.ItemsDiscovered++;
		const FString ObjectPath = BuildAnimationObjectPath(Request.Context, Record);
		const FString ObjectPathKey = ObjectPath.ToLower();
		if (ProcessedObjectPaths.Contains(ObjectPathKey))
		{
			OutResult.ItemsSkipped++;
			continue;
		}
		ProcessedObjectPaths.Add(ObjectPathKey);

		FString GrannyAnimationPath;
		FString Error;
		if (!ResolveGrannyAnimationPath(Request, Record, GrannyAnimationPath, Error))
		{
			OutResult.ItemsSkipped++;
			OutResult.AddError(FString::Printf(TEXT("Animation source resolution failed for %s: %s"), *Record.VirtualPath, *Error), Record.AbsolutePath);
			continue;
		}
		if (ConsideredCount == 1)
		{
			UE_LOG(LogMT2AnimationSound, Display, TEXT("First animation source resolved: %s"), *GrannyAnimationPath);
		}

		FMT2GrannyAnimationClip Clip;
		if (!FMT2GrannyMeshConverter::ExtractAnimationClip(GrannyAnimationPath, Clip, Error))
		{
			OutResult.ItemsSkipped++;
			OutResult.AddError(FString::Printf(TEXT("Granny animation extraction failed for %s: %s"), *Record.VirtualPath, *Error), GrannyAnimationPath);
			continue;
		}
		if (ConsideredCount == 1)
		{
			UE_LOG(LogMT2AnimationSound, Display, TEXT("First animation raw clip extracted: %d tracks, %d frames."),
				Clip.Tracks.Num(), Clip.NumberOfFrames);
		}

		const FAnimationSkeletonMatch SkeletonMatch = FindBestImportedSkeleton(Request.Context, Record, Clip, CanonicalModelMappings);
		if (!SkeletonMatch.Skeleton || !SkeletonMatch.PreviewMesh)
		{
			OutResult.ItemsSkipped++;
			OutResult.AddError(FString::Printf(TEXT("No imported skeletal mesh matches animation %s (%d tracks). Import its skeletal mesh first."), *Record.VirtualPath, Clip.Tracks.Num()), GrannyAnimationPath);
			continue;
		}
		if (ConsideredCount == 1)
		{
			UE_LOG(LogMT2AnimationSound, Display, TEXT("First animation matched mesh: %s"),
				*SkeletonMatch.PreviewMesh->GetPathName());
		}

		if (Request.Context.bDryRun)
		{
			OutResult.ItemsSkipped++;
			OutResult.CreatedPackages.Add(ObjectPath);
			continue;
		}
		if (SkeletonMatch.GrannyModelPath.IsEmpty())
		{
			OutResult.ItemsSkipped++;
			OutResult.AddError(FString::Printf(TEXT("Could not map preview mesh %s back to its source model GR2."), *SkeletonMatch.PreviewMesh->GetPathName()), GrannyAnimationPath);
			continue;
		}

		FMT2GrannyAnimationClip BoundClip;
		if (!FMT2GrannyMeshConverter::ExtractAnimationClipForModel(
			GrannyAnimationPath,
			SkeletonMatch.GrannyModelPath,
			BoundClip,
			Error,
			&SkeletonMatch.PreviewMesh->GetRefSkeleton()))
		{
			OutResult.ItemsSkipped++;
			OutResult.AddError(FString::Printf(TEXT("Granny model-bound animation extraction failed for %s: %s"), *Record.VirtualPath, *Error), GrannyAnimationPath);
			continue;
		}
		Clip = MoveTemp(BoundClip);
		if (ConsideredCount == 1)
		{
			UE_LOG(LogMT2AnimationSound, Display, TEXT("First animation model-bound clip extracted."));
		}

		if (ConsideredCount == 1)
		{
			UE_LOG(LogMT2AnimationSound, Display, TEXT("Loading first existing animation asset: %s"), *ObjectPath);
		}
		UAnimSequence* AnimSequence = LoadObject<UAnimSequence>(nullptr, *ObjectPath);
		if (ConsideredCount == 1)
		{
			UE_LOG(LogMT2AnimationSound, Display, TEXT("First existing animation asset load completed."));
		}
		const bool bCreatedAsset = AnimSequence == nullptr;
		const bool bExistingSkeletonMismatch = AnimSequence && AnimSequence->GetSkeleton() != SkeletonMatch.Skeleton;
		const bool bExistingDataMismatch = AnimSequence && !Request.Context.bReplaceExisting &&
			!AnimationDataMatchesClip(AnimSequence, Clip);
		if (AnimSequence && !Request.Context.bReplaceExisting && !bExistingSkeletonMismatch && !bExistingDataMismatch)
		{
			if (AnimSequence->GetPreviewMesh() != SkeletonMatch.PreviewMesh)
			{
				AnimSequence->SetPreviewMesh(SkeletonMatch.PreviewMesh);
				AnimSequence->MarkPackageDirty();
			}
			OutResult.ItemsSkipped++;
			OutResult.AddInfo(FString::Printf(TEXT("Skipped already imported animation: %s"), *ObjectPath), GrannyAnimationPath);
			continue;
		}

		if (!AnimSequence)
		{
			const FString PackageName = FPackageName::ObjectPathToPackageName(ObjectPath);
			const FString AssetName = FPackageName::ObjectPathToObjectName(ObjectPath);
			UPackage* Package = CreatePackage(*PackageName);
			AnimSequence = NewObject<UAnimSequence>(Package, *AssetName, RF_Public | RF_Standalone | RF_Transactional);
		}
		if (!AnimSequence)
		{
			OutResult.ItemsSkipped++;
			OutResult.AddError(FString::Printf(TEXT("Failed to create animation asset: %s"), *ObjectPath), Record.AbsolutePath);
			continue;
		}
		if (!bCreatedAsset)
		{
			// The editor may still be compressing this sequence after its skeleton or preview mesh was
			// reimported. Synchronize it before replacing the raw model; SetSkeleton otherwise waits on
			// that task from inside the modal import operation.
			UE::Anim::IAnimSequenceCompilingManager::FinishCompilation({AnimSequence});
		}
		if (ConsideredCount == 1)
		{
			UE_LOG(LogMT2AnimationSound, Display, TEXT("First animation pending compilation synchronized."));
		}

		AnimSequence->Modify();
		if (AnimSequence->GetSkeleton() != SkeletonMatch.Skeleton)
		{
			AnimSequence->SetSkeleton(SkeletonMatch.Skeleton);
		}
		if (AnimSequence->GetPreviewMesh() != SkeletonMatch.PreviewMesh)
		{
			AnimSequence->SetPreviewMesh(SkeletonMatch.PreviewMesh, false);
		}
		IAnimationDataController& Controller = AnimSequence->GetController();
		Controller.InitializeModel();
		if (ConsideredCount == 1)
		{
			UE_LOG(LogMT2AnimationSound, Display, TEXT("First animation data controller initialized."));
		}
		int32 ImportedTrackCount = 0;
		{
			IAnimationDataController::FScopedBracket ImportBracket(
				Controller,
				NSLOCTEXT("FMT2AnimationImporter", "ImportGrannyAnimationBracket", "Importing Granny animation"),
				false);
			Controller.ResetModel(false);
			Controller.SetFrameRate(FFrameRate(Clip.FrameRate, 1), false);
			Controller.SetNumberOfFrames(FFrameNumber(Clip.NumberOfFrames), false);

			const FReferenceSkeleton& RefSkeleton = SkeletonMatch.PreviewMesh->GetRefSkeleton();
			for (const FMT2GrannyAnimationTrack& Track : Clip.Tracks)
			{
				const FName BoneName(*Track.BoneName);
				if (RefSkeleton.FindBoneIndex(BoneName) == INDEX_NONE)
				{
					continue;
				}
				if (Controller.AddBoneCurve(BoneName, false) &&
					Controller.SetBoneTrackKeys(BoneName, Track.TranslationKeys, Track.RotationKeys, Track.ScaleKeys, false))
				{
					ImportedTrackCount++;
				}
			}
			Controller.NotifyPopulated();
		}
		if (ConsideredCount == 1)
		{
			UE_LOG(LogMT2AnimationSound, Display, TEXT("First animation raw tracks replaced."));
		}

		if (ImportedTrackCount == 0)
		{
			OutResult.ItemsSkipped++;
			OutResult.AddError(FString::Printf(TEXT("No animation tracks could be mapped to skeleton %s."), *SkeletonMatch.Skeleton->GetPathName()), GrannyAnimationPath);
			continue;
		}

		ApplyMotionSoundNotifies(Request, Record, AnimSequence, OutResult);
		ApplyMotionMetadata(Request.Context, Record, AnimSequence);

		AnimSequence->PostEditChange();
		AnimSequence->MarkPackageDirty();
		if (bCreatedAsset)
		{
			FAssetRegistryModule::AssetCreated(AnimSequence);
		}
		OutResult.CreatedPackages.Add(ObjectPath);
		OutResult.ItemsImported++;
		if (ConsideredCount == 1)
		{
			UE_LOG(LogMT2AnimationSound, Display, TEXT("First animation import completed: %s"), *ObjectPath);
		}

		if (Request.Context.bEnableDebugLogs)
		{
			OutResult.AddInfo(FString::Printf(
				TEXT("[Debug][Animation] Source='%s' Model='%s' Clip='%s' Group='%s' Duration=%.6f Rate=%d Frames=%d Tracks=%d/%d Skeleton='%s' Preview='%s'"),
				*GrannyAnimationPath,
				*SkeletonMatch.GrannyModelPath,
				*Clip.Name,
				*Clip.TrackGroupName,
				Clip.Duration,
				Clip.FrameRate,
				Clip.NumberOfFrames,
				ImportedTrackCount,
				Clip.Tracks.Num(),
				*SkeletonMatch.Skeleton->GetPathName(),
				*SkeletonMatch.PreviewMesh->GetPathName()),
				Record.AbsolutePath);
		}
	}

	OutResult.bSucceeded = !OutResult.HasErrors();
	return OutResult.bSucceeded;
}
