/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "MT2GeneratePlayerAnimationCommandlet.h"

#include "Animation/AnimSequence.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/MT2CharacterAnimInstance.h"
#include "Animation/Skeleton.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Curves/CurveFloat.h"
#include "MT2CharacterAnimationGenerator.h"
#include "Misc/PackageName.h"
#include "Modules/ModuleManager.h"
#include "Misc/Parse.h"
#include "UObject/Package.h"

namespace
{
	int32 CreateExperienceCurve()
	{
		const FString PackageName = TEXT("/Game/Logic/XPCurve");
		UCurveFloat* Curve = LoadObject<UCurveFloat>(nullptr, TEXT("/Game/Logic/XPCurve.XPCurve"));
		const bool bCreated = Curve == nullptr;
		UPackage* Package = Curve ? Curve->GetOutermost() : CreatePackage(*PackageName);
		if (bCreated)
		{
			Curve = NewObject<UCurveFloat>(Package, TEXT("XPCurve"), RF_Public | RF_Standalone | RF_Transactional);
			FAssetRegistryModule::AssetCreated(Curve);
		}
		if (!Curve) return 1;

		Curve->Modify();
		Curve->FloatCurve.Reset();
		// Seven exact samples, one every 20 levels. Tangents are a relative-error least-squares fit
		// against every integer level of the original exponential table; max interpolation error is
		// about 3%, while keeping the asset compact and pleasant to edit.
		constexpr int32 Levels[] = {0, 20, 40, 60, 80, 100, 120};
		constexpr float Tangents[] = {
			5145.3030f, 40230.3359f, 354294.7188f, 3161559.5f,
			28236546.0f, 252074448.0f, 2242929920.0f
		};
		for (int32 Index = 0; Index < UE_ARRAY_COUNT(Levels); ++Index)
		{
			const int32 Level = Levels[Index];
			const double RequiredExperience = Level == 0 ? 0.0 :
				44318.12 * (FMath::Pow(1.1157, static_cast<double>(Level)) - 1.0);
			const FKeyHandle Key = Curve->FloatCurve.AddKey(Level, static_cast<float>(RequiredExperience));
			Curve->FloatCurve.SetKeyInterpMode(Key, RCIM_Cubic, false);
			Curve->FloatCurve.SetKeyTangentMode(Key, RCTM_Break, false);
			FRichCurveKey& RichKey = Curve->FloatCurve.GetKey(Key);
			RichKey.ArriveTangent = Tangents[Index];
			RichKey.LeaveTangent = Tangents[Index];
		}
		Curve->MarkPackageDirty();

		const FString Filename = FPackageName::LongPackageNameToFilename(
			PackageName, FPackageName::GetAssetPackageExtension());
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		SaveArgs.SaveFlags = SAVE_NoError;
		const bool bSaved = UPackage::SavePackage(Package, Curve, *Filename, SaveArgs);
		UE_LOG(LogTemp, Display, TEXT("MT2 XP curve %s: %s (%d keys)"),
			bCreated ? TEXT("created") : TEXT("updated"), *Curve->GetPathName(),
			Curve->FloatCurve.GetNumKeys());
		return bSaved ? 0 : 1;
	}

	int32 InspectAttackAnimations()
	{
		TArray<FAssetData> Assets;
		FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"))
			.Get().GetAssetsByPath(TEXT("/Game/ymir_work"), Assets, true, false);

		for (const FAssetData& Asset : Assets)
		{
			const FString Name = Asset.AssetName.ToString().ToLower();
			if (!Name.Contains(TEXT("attack")) && !Name.Contains(TEXT("combo")))
			{
				continue;
			}
			const UAnimSequence* Sequence = Cast<UAnimSequence>(Asset.GetAsset());
			if (!Sequence || Sequence->GetPlayLength() <= UE_SMALL_NUMBER)
			{
				continue;
			}

			constexpr int32 SampleCount = 20;
			const double Length = Sequence->GetPlayLength();
			const FTransform FirstRoot = Sequence->ExtractRootTrackTransform(FAnimExtractContext(0.0), nullptr);
			FTransform ReferenceRoot = FTransform::Identity;
			if (const USkeleton* Skeleton = Sequence->GetSkeleton();
				Skeleton && !Skeleton->GetReferenceSkeleton().GetRefBonePose().IsEmpty())
			{
				ReferenceRoot = Skeleton->GetReferenceSkeleton().GetRefBonePose()[0];
			}
			const FVector Total = Sequence->ExtractRootMotionFromRange(0.0, Length, FAnimExtractContext()).GetTranslation();
			int32 FirstMovingSample = INDEX_NONE;
			int32 LastMovingSample = INDEX_NONE;
			for (int32 Index = 0; Index < SampleCount; ++Index)
			{
				const double Start = Length * Index / SampleCount;
				const double End = Length * (Index + 1) / SampleCount;
				const FVector Delta = Sequence->ExtractRootMotionFromRange(Start, End, FAnimExtractContext()).GetTranslation();
				if (Delta.Size2D() > 0.5f)
				{
					if (FirstMovingSample == INDEX_NONE) FirstMovingSample = Index;
					LastMovingSample = Index;
				}
			}
			UE_LOG(LogTemp, Display,
				TEXT("MT2_ATTACK_INSPECT %s Length=%.3f Distance=%.2f Delta=%s MoveSamples=%d-%d FirstRoot=%s RefRoot=%s"),
				*Asset.GetSoftObjectPath().ToString(), Length, Total.Size2D(), *Total.ToCompactString(),
				FirstMovingSample, LastMovingSample,
				*FirstRoot.ToHumanReadableString(), *ReferenceRoot.ToHumanReadableString());
		}
		return 0;
	}

	int32 InspectPlayerAnimationSets()
	{
		static const TCHAR* BlueprintNames[] =
		{
			TEXT("ABP_Warrior_Male"), TEXT("ABP_Warrior_Female"),
			TEXT("ABP_Assassin_Female"), TEXT("ABP_Assassin_Male"),
			TEXT("ABP_Sura_Male"), TEXT("ABP_Sura_Female"),
			TEXT("ABP_Shaman_Female"), TEXT("ABP_Shaman_Male")
		};
		for (const TCHAR* BlueprintName : BlueprintNames)
		{
			const FString Path = FString::Printf(
				TEXT("/Game/Characters/Animations/%s.%s"), BlueprintName, BlueprintName);
			const UAnimBlueprint* Blueprint = LoadObject<UAnimBlueprint>(nullptr, *Path);
			const UMT2CharacterAnimInstance* Defaults = Blueprint && Blueprint->GeneratedClass
				? Cast<UMT2CharacterAnimInstance>(Blueprint->GeneratedClass->GetDefaultObject()) : nullptr;
			if (!Defaults)
			{
				UE_LOG(LogTemp, Error, TEXT("MT2_ANIM_SET missing %s"), *Path);
				continue;
			}
			UE_LOG(LogTemp, Display, TEXT("MT2_ANIM_SET %s default=%s fields wait=%s walk=%s run=%s"),
				BlueprintName, *Defaults->DefaultAnimationSet.ToString(),
				*GetPathNameSafe(Defaults->WaitAnimation), *GetPathNameSafe(Defaults->WalkAnimation),
				*GetPathNameSafe(Defaults->RunAnimation));
			for (const TPair<FName, FMT2AnimationActionSet>& SetPair : Defaults->AnimationSets)
			{
				for (const FName Action : {FName(TEXT("wait")), FName(TEXT("walk")), FName(TEXT("run"))})
				{
					const FMT2AnimationSequenceSet* Sequences = SetPair.Value.Actions.Find(Action);
					if (Sequences && !Sequences->Animations.IsEmpty())
					{
						UE_LOG(LogTemp, Display, TEXT("MT2_ANIM_SET %s set=%s action=%s first=%s count=%d"),
							BlueprintName, *SetPair.Key.ToString(), *Action.ToString(),
							*GetPathNameSafe(Sequences->Animations[0]), Sequences->Animations.Num());
					}
				}
			}
		}
		return 0;
	}
}

UMT2GeneratePlayerAnimationCommandlet::UMT2GeneratePlayerAnimationCommandlet()
{
	IsClient = false;
	IsEditor = true;
	IsServer = false;
	LogToConsole = true;
}

int32 UMT2GeneratePlayerAnimationCommandlet::Main(const FString& Params)
{
	if (FParse::Param(*Params, TEXT("CreateXPCurve")))
	{
		return CreateExperienceCurve();
	}
	if (FParse::Param(*Params, TEXT("InspectAttacks")))
	{
		return InspectAttackAnimations();
	}
	if (FParse::Param(*Params, TEXT("InspectPlayerAnimationSets")))
	{
		return InspectPlayerAnimationSets();
	}

	FString SourceRoot;
	FParse::Value(*Params, TEXT("SourceRoot="), SourceRoot);
	if (SourceRoot.IsEmpty())
	{
		SourceRoot = TEXT("D:/Giochi/Metin2/Development/Dumps/my_dump");
	}

	FMT2CharacterAnimationGenerationResult Result;
	const bool bSucceeded = FMT2CharacterAnimationGenerator::Generate(SourceRoot, true, Result);
	UE_LOG(LogTemp, Display, TEXT("MT2 animation generation:\n%s"), *Result.BuildSummary());
	for (const FString& Error : Result.Errors)
	{
		UE_LOG(LogTemp, Error, TEXT("%s"), *Error);
	}
	return bSucceeded ? 0 : 1;
}
