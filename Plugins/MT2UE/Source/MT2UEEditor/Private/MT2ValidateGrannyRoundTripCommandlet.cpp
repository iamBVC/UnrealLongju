/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "MT2ValidateGrannyRoundTripCommandlet.h"
#include "Config/MT2PathSettings.h"

#include "Animation/AnimData/IAnimationDataController.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimationPoseData.h"
#include "Animation/AttributesRuntime.h"
#include "Animation/Skeleton.h"
#include "BoneContainer.h"
#include "BonePose.h"
#include "Engine/SkeletalMesh.h"
#include "Rendering/SkeletalMeshLODModel.h"
#include "Rendering/SkeletalMeshModel.h"
#include "SkeletalMeshTypes.h"
#include "Importers/MT2GrannyMeshConverter.h"
#include "MT2AssetScanner.h"
#include "InterchangeGenericAssetsPipeline.h"
#include "InterchangeGenericMeshPipeline.h"
#include "InterchangeManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"

THIRD_PARTY_INCLUDES_START
#include "granny.h"
THIRD_PARTY_INCLUDES_END

namespace
{
	struct FSourceMeshData
	{
		FString Name;
		TArray<FVector3f> Positions;
		TArray<float> Weights;
		TArray<uint8> LocalBoneIndices;
		TArray<int32> SourceSkeletonIndices;
		TArray<int32> UnrealSkeletonIndices;
	};

	bool CopyVertexFloats(
		granny_mesh* Mesh,
		const char* MemberName,
		int32 ComponentCount,
		TArray<float>& OutValues)
	{
		granny_variant Ignore;
		if (!GrannyFindMatchingMember(
			GrannyGetMeshVertexType(Mesh), GrannyGetMeshVertices(Mesh), MemberName, &Ignore))
		{
			return false;
		}
		granny_data_type_definition Type[] =
		{
			{ GrannyReal32Member, MemberName, 0, ComponentCount },
			{ GrannyEndMember }
		};
		OutValues.SetNumZeroed(GrannyGetMeshVertexCount(Mesh) * ComponentCount);
		GrannyCopyMeshVertices(Mesh, Type, OutValues.GetData());
		return true;
	}

	bool CopyVertexIndices(granny_mesh* Mesh, TArray<uint8>& OutValues)
	{
		granny_variant Ignore;
		if (!GrannyFindMatchingMember(
			GrannyGetMeshVertexType(Mesh), GrannyGetMeshVertices(Mesh),
			GrannyVertexBoneIndicesName, &Ignore))
		{
			return false;
		}
		granny_data_type_definition Type[] =
		{
			{ GrannyUInt8Member, GrannyVertexBoneIndicesName, 0, 4 },
			{ GrannyEndMember }
		};
		OutValues.SetNumZeroed(GrannyGetMeshVertexCount(Mesh) * 4);
		GrannyCopyMeshVertices(Mesh, Type, OutValues.GetData());
		return true;
	}

	int32 FindGrannyBone(const granny_skeleton* Skeleton, const char* Name)
	{
		if (!Skeleton || !Name)
		{
			return INDEX_NONE;
		}
		for (granny_int32x BoneIndex = 0; BoneIndex < Skeleton->BoneCount; ++BoneIndex)
		{
			if (FCStringAnsi::Stricmp(Skeleton->Bones[BoneIndex].Name, Name) == 0)
			{
				return BoneIndex;
			}
		}
		return INDEX_NONE;
	}

	FVector3f TransformGrannyPoint(const granny_real32* Matrix, const FVector3f& Point)
	{
		return FVector3f(
			Point.X * Matrix[0] + Point.Y * Matrix[4] + Point.Z * Matrix[8] + Matrix[12],
			Point.X * Matrix[1] + Point.Y * Matrix[5] + Point.Z * Matrix[9] + Matrix[13],
			Point.X * Matrix[2] + Point.Y * Matrix[6] + Point.Z * Matrix[10] + Matrix[14]);
	}

	FVector3f GrannyToUnreal(const FVector3f& Value)
	{
		// Imported meshes and animations mirror source X (right-handed Granny to left-handed
		// Unreal, same convention as the map import). Round-trip comparisons must apply it too.
		return FVector3f(-Value.X, Value.Y, Value.Z);
	}

	void BuildComponentPose(
		const FReferenceSkeleton& Skeleton,
		const TArray<FTransform>& LocalPose,
		TArray<FTransform>& OutComponentPose)
	{
		OutComponentPose.SetNum(LocalPose.Num());
		for (int32 BoneIndex = 0; BoneIndex < LocalPose.Num(); ++BoneIndex)
		{
			const int32 ParentIndex = Skeleton.GetParentIndex(BoneIndex);
			OutComponentPose[BoneIndex] = ParentIndex == INDEX_NONE
				? LocalPose[BoneIndex]
				: LocalPose[BoneIndex] * OutComponentPose[ParentIndex];
		}
	}

	USkeletalMesh* ImportTemporaryMesh(
		const FString& GltfPath,
		const FString& DestinationPath,
		const FString& DestinationName)
	{
		UInterchangeGenericAssetsPipeline* Pipeline =
			NewObject<UInterchangeGenericAssetsPipeline>(GetTransientPackage());
		if (!Pipeline || !Pipeline->MeshPipeline)
		{
			return nullptr;
		}
		Pipeline->MeshPipeline->bCreatePhysicsAsset = false;
		Pipeline->ClearFlags(RF_Public | RF_Standalone | RF_Transactional);

		UInterchangeManager& InterchangeManager = UInterchangeManager::GetInterchangeManager();
		UInterchangeSourceData* SourceData = InterchangeManager.CreateSourceData(GltfPath);
		if (!SourceData)
		{
			return nullptr;
		}
		FImportAssetParameters Parameters;
		Parameters.bIsAutomated = true;
		Parameters.bReplaceExisting = true;
		Parameters.DestinationName = DestinationName;
		Parameters.OverridePipelines.Add(Pipeline);
		const UE::Interchange::FAssetImportResultRef Result = InterchangeManager.ImportAssetWithResult(
			DestinationPath, SourceData, Parameters);
		return Cast<USkeletalMesh>(Result->GetFirstAssetOfClass(USkeletalMesh::StaticClass()));
	}

	FTransform ApplyAnimationRelativeRetarget(
		const FTransform& AnimatedTransform,
		const FTransform& SourceReferenceTransform,
		const FTransform& TargetReferenceTransform)
	{
		FTransform Result = AnimatedTransform;
		Result.SetRotation(
			Result.GetRotation() * SourceReferenceTransform.GetRotation().Inverse() *
			TargetReferenceTransform.GetRotation());
		Result.SetTranslation(
			Result.GetTranslation() +
			(TargetReferenceTransform.GetTranslation() - SourceReferenceTransform.GetTranslation()));
		Result.SetScale3D(
			Result.GetScale3D() *
			(TargetReferenceTransform.GetScale3D() *
				SourceReferenceTransform.GetSafeScaleReciprocal(SourceReferenceTransform.GetScale3D())));
		Result.NormalizeRotation();
		return Result;
	}

	bool EvaluateAnimationOnMesh(
		const UAnimSequence* Sequence,
		const USkeletalMesh* TargetMesh,
		double Time,
		TArray<FTransform>& OutLocalPose)
	{
		if (!Sequence || !TargetMesh)
		{
			return false;
		}
		FMemMark MemMark(FMemStack::Get());
		TArray<FBoneIndexType> RequiredBoneIndices;
		RequiredBoneIndices.SetNumUninitialized(TargetMesh->GetRefSkeleton().GetNum());
		for (int32 BoneIndex = 0; BoneIndex < RequiredBoneIndices.Num(); ++BoneIndex)
		{
			RequiredBoneIndices[BoneIndex] = static_cast<FBoneIndexType>(BoneIndex);
		}

		FBoneContainer RequiredBones;
		RequiredBones.InitializeTo(
			RequiredBoneIndices,
			UE::Anim::FCurveFilterSettings(UE::Anim::ECurveFilterMode::DisallowAll),
			*TargetMesh);
		RequiredBones.SetUseRAWData(true);
		RequiredBones.SetDisableRetargeting(false);

		FCompactPose CompactPose;
		CompactPose.SetBoneContainer(&RequiredBones);
		CompactPose.ResetToRefPose();
		FBlendedCurve Curve;
		Curve.InitFrom(RequiredBones);
		UE::Anim::FStackAttributeContainer Attributes;
		FAnimationPoseData PoseData(CompactPose, Curve, Attributes);
		FAnimExtractContext ExtractContext(Time, false);
		Sequence->GetAnimationPose(PoseData, ExtractContext);

		OutLocalPose.SetNum(TargetMesh->GetRefSkeleton().GetNum());
		for (FCompactPoseBoneIndex CompactIndex(0);
			CompactIndex < CompactPose.GetNumBones(); ++CompactIndex)
		{
			const FSkeletonPoseBoneIndex SkeletonIndex =
				RequiredBones.GetSkeletonPoseIndexFromCompactPoseIndex(CompactIndex);
			if (OutLocalPose.IsValidIndex(SkeletonIndex.GetInt()))
			{
				OutLocalPose[SkeletonIndex.GetInt()] = CompactPose[CompactIndex];
			}
		}
		return true;
	}

	UAnimSequence* BuildTemporaryAnimation(
		USkeletalMesh* Mesh,
		const FMT2GrannyAnimationClip& Clip)
	{
		if (!Mesh || !Mesh->GetSkeleton())
		{
			return nullptr;
		}
		UAnimSequence* Sequence = NewObject<UAnimSequence>(GetTransientPackage());
		Sequence->SetSkeleton(Mesh->GetSkeleton());
		Sequence->SetPreviewMesh(Mesh, false);
		IAnimationDataController& Controller = Sequence->GetController();
		Controller.InitializeModel();
		Controller.ResetModel(false);
		Controller.SetFrameRate(FFrameRate(Clip.FrameRate, 1), false);
		Controller.SetNumberOfFrames(FFrameNumber(Clip.NumberOfFrames), false);
		const FReferenceSkeleton& RefSkeleton = Mesh->GetRefSkeleton();
		for (const FMT2GrannyAnimationTrack& Track : Clip.Tracks)
		{
			const FName BoneName(*Track.BoneName);
			if (RefSkeleton.FindBoneIndex(BoneName) != INDEX_NONE &&
				Controller.AddBoneCurve(BoneName, false))
			{
				Controller.SetBoneTrackKeys(
					BoneName, Track.TranslationKeys, Track.RotationKeys, Track.ScaleKeys, false);
			}
		}
		Controller.NotifyPopulated();
		Sequence->PostEditChange();
		return Sequence;
	}

	bool ReadSourceMeshes(
		const granny_file_info* ModelInfo,
		const granny_skeleton* SourceSkeleton,
		const FReferenceSkeleton& UnrealSkeleton,
		TArray<FSourceMeshData>& OutMeshes,
		FString& OutError)
	{
		for (granny_int32x MeshIndex = 0; MeshIndex < ModelInfo->MeshCount; ++MeshIndex)
		{
			granny_mesh* Mesh = ModelInfo->Meshes[MeshIndex];
			if (!Mesh)
			{
				continue;
			}
			TArray<float> PositionValues;
			FSourceMeshData& Data = OutMeshes.AddDefaulted_GetRef();
			Data.Name = Mesh->Name ? UTF8_TO_TCHAR(Mesh->Name) : FString::Printf(TEXT("Mesh_%d"), MeshIndex);
			if (!CopyVertexFloats(Mesh, GrannyVertexPositionName, 3, PositionValues) ||
				!CopyVertexFloats(Mesh, GrannyVertexBoneWeightsName, 4, Data.Weights) ||
				!CopyVertexIndices(Mesh, Data.LocalBoneIndices))
			{
				OutError = FString::Printf(TEXT("Mesh %s lacks skinned PWB vertex data."), *Data.Name);
				return false;
			}
			for (int32 Index = 0; Index + 2 < PositionValues.Num(); Index += 3)
			{
				Data.Positions.Emplace(
					PositionValues[Index], PositionValues[Index + 1], PositionValues[Index + 2]);
			}
			Data.SourceSkeletonIndices.SetNum(Mesh->BoneBindingCount);
			Data.UnrealSkeletonIndices.SetNum(Mesh->BoneBindingCount);
			for (granny_int32x BindingIndex = 0; BindingIndex < Mesh->BoneBindingCount; ++BindingIndex)
			{
				const char* BoneName = Mesh->BoneBindings[BindingIndex].BoneName;
				Data.SourceSkeletonIndices[BindingIndex] = FindGrannyBone(SourceSkeleton, BoneName);
				Data.UnrealSkeletonIndices[BindingIndex] = UnrealSkeleton.FindBoneIndex(
					FName(FMT2AssetScanner::SanitizePackagePathSegment(UTF8_TO_TCHAR(BoneName))));
				if (Data.SourceSkeletonIndices[BindingIndex] == INDEX_NONE ||
					Data.UnrealSkeletonIndices[BindingIndex] == INDEX_NONE)
				{
					OutError = FString::Printf(TEXT("Unmapped binding %s in mesh %s."),
						UTF8_TO_TCHAR(BoneName), *Data.Name);
					return false;
				}
			}
		}
		return !OutMeshes.IsEmpty();
	}
}

UMT2ValidateGrannyRoundTripCommandlet::UMT2ValidateGrannyRoundTripCommandlet()
{
	IsClient = false;
	IsEditor = true;
	IsServer = false;
	LogToConsole = true;
	ShowErrorCount = true;
}

int32 UMT2ValidateGrannyRoundTripCommandlet::Main(const FString& Params)
{
	FString ModelPath;
	FString AnimationPath;
	FString AnimationModelPath;
	FString ExistingMeshPath;
	FString ExistingAnimationPath;
	const bool bInspectOnly = FParse::Param(*Params, TEXT("InspectOnly"));
	FParse::Value(*Params, TEXT("Model="), ModelPath);
	FParse::Value(*Params, TEXT("Animation="), AnimationPath);
	FParse::Value(*Params, TEXT("AnimationModel="), AnimationModelPath);
	FParse::Value(*Params, TEXT("ExistingMesh="), ExistingMeshPath);
	FParse::Value(*Params, TEXT("ExistingAnimation="), ExistingAnimationPath);
	if (ModelPath.IsEmpty() || (!bInspectOnly && AnimationPath.IsEmpty()))
	{
		UE_LOG(LogTemp, Error,
			TEXT("Usage: -Model=<model.gr2> (-InspectOnly | -Animation=<animation.gr2>)"));
		return 1;
	}
	if (bInspectOnly)
	{
		FString InspectionError;
		FMT2GrannyFileInspection Inspection;
		if (!FMT2GrannyMeshConverter::InspectGrannyFile(
			ModelPath, Inspection, InspectionError))
		{
			UE_LOG(LogTemp, Error, TEXT("Could not initialize Granny inspection: %s"),
				*InspectionError);
			return 1;
		}
		granny_file* SourceFile = GrannyReadEntireFile(TCHAR_TO_UTF8(*ModelPath));
		granny_file_info* SourceInfo = SourceFile ? GrannyGetFileInfo(SourceFile) : nullptr;
		if (!SourceInfo)
		{
			UE_LOG(LogTemp, Error, TEXT("Could not inspect Granny model: %s"), *ModelPath);
			if (SourceFile)
			{
				GrannyFreeFile(SourceFile);
			}
			return 1;
		}

		UE_LOG(LogTemp, Display, TEXT("MT2_MODEL_INSPECT file=%s models=%d meshes=%d skeletons=%d"),
			*ModelPath, SourceInfo->ModelCount, SourceInfo->MeshCount, SourceInfo->SkeletonCount);
		for (granny_int32x ModelIndex = 0; ModelIndex < SourceInfo->ModelCount; ++ModelIndex)
		{
			granny_model* SourceModel = SourceInfo->Models ? SourceInfo->Models[ModelIndex] : nullptr;
			if (!SourceModel)
			{
				continue;
			}
			const granny_transform& Placement = SourceModel->InitialPlacement;
			UE_LOG(LogTemp, Display,
				TEXT("MT2_MODEL_PLACEMENT index=%d name=%s position=(%.6f,%.6f,%.6f) rotation=(%.6f,%.6f,%.6f,%.6f) bones=%d bindings=%d"),
				ModelIndex, SourceModel->Name ? UTF8_TO_TCHAR(SourceModel->Name) : TEXT("None"),
				Placement.Position[0], Placement.Position[1], Placement.Position[2],
				Placement.Orientation[0], Placement.Orientation[1],
				Placement.Orientation[2], Placement.Orientation[3],
				SourceModel->Skeleton ? SourceModel->Skeleton->BoneCount : 0,
				SourceModel->MeshBindingCount);
			if (SourceModel->Skeleton)
			{
				for (granny_int32x BoneIndex = 0;
					 BoneIndex < SourceModel->Skeleton->BoneCount; ++BoneIndex)
				{
					const granny_bone& Bone = SourceModel->Skeleton->Bones[BoneIndex];
					const bool bHasScaleShear =
						!FMath::IsNearlyEqual(Bone.LocalTransform.ScaleShear[0][0], 1.0f) ||
						!FMath::IsNearlyEqual(Bone.LocalTransform.ScaleShear[1][1], 1.0f) ||
						!FMath::IsNearlyEqual(Bone.LocalTransform.ScaleShear[2][2], 1.0f) ||
						!FMath::IsNearlyZero(Bone.LocalTransform.ScaleShear[0][1]) ||
						!FMath::IsNearlyZero(Bone.LocalTransform.ScaleShear[0][2]) ||
						!FMath::IsNearlyZero(Bone.LocalTransform.ScaleShear[1][0]) ||
						!FMath::IsNearlyZero(Bone.LocalTransform.ScaleShear[1][2]) ||
						!FMath::IsNearlyZero(Bone.LocalTransform.ScaleShear[2][0]) ||
						!FMath::IsNearlyZero(Bone.LocalTransform.ScaleShear[2][1]);
					if (Bone.ParentIndex < 0 ||
						bHasScaleShear ||
						(Bone.Name && (FCStringAnsi::Stricmp(Bone.Name, "equip_right_hand") == 0 ||
						 FCStringAnsi::Stricmp(Bone.Name, "equip_right") == 0)))
					{
						UE_LOG(LogTemp, Display,
							TEXT("MT2_MODEL_BONE index=%d parent=%d name=%s position=(%.6f,%.6f,%.6f) scale_shear=[%.6f %.6f %.6f; %.6f %.6f %.6f; %.6f %.6f %.6f]"),
							BoneIndex, Bone.ParentIndex,
							Bone.Name ? UTF8_TO_TCHAR(Bone.Name) : TEXT("None"),
							Bone.LocalTransform.Position[0], Bone.LocalTransform.Position[1],
							Bone.LocalTransform.Position[2],
							Bone.LocalTransform.ScaleShear[0][0], Bone.LocalTransform.ScaleShear[0][1],
							Bone.LocalTransform.ScaleShear[0][2], Bone.LocalTransform.ScaleShear[1][0],
							Bone.LocalTransform.ScaleShear[1][1], Bone.LocalTransform.ScaleShear[1][2],
							Bone.LocalTransform.ScaleShear[2][0], Bone.LocalTransform.ScaleShear[2][1],
							Bone.LocalTransform.ScaleShear[2][2]);
					}
				}
			}
		}

		for (granny_int32x MeshIndex = 0; MeshIndex < SourceInfo->MeshCount; ++MeshIndex)
		{
			granny_mesh* SourceMesh = SourceInfo->Meshes ? SourceInfo->Meshes[MeshIndex] : nullptr;
			if (!SourceMesh)
			{
				continue;
			}
			TArray<float> Positions;
			if (!CopyVertexFloats(SourceMesh, GrannyVertexPositionName, 3, Positions))
			{
				continue;
			}
			granny_model* OwnerModel = nullptr;
			for (granny_int32x ModelIndex = 0; ModelIndex < SourceInfo->ModelCount && !OwnerModel; ++ModelIndex)
			{
				granny_model* Candidate = SourceInfo->Models ? SourceInfo->Models[ModelIndex] : nullptr;
				for (granny_int32x BindingIndex = 0;
					 Candidate && BindingIndex < Candidate->MeshBindingCount; ++BindingIndex)
				{
					if (Candidate->MeshBindings[BindingIndex].Mesh == SourceMesh)
					{
						OwnerModel = Candidate;
						break;
					}
				}
			}

			FBox RawBounds(EForceInit::ForceInit);
			FBox InversePlacementBounds(EForceInit::ForceInit);
			granny_transform InversePlacement = {};
			if (OwnerModel)
			{
				GrannyBuildInverse(&InversePlacement, &OwnerModel->InitialPlacement);
			}
			for (int32 PositionIndex = 0; PositionIndex + 2 < Positions.Num(); PositionIndex += 3)
			{
				granny_real32 Raw[3] = { Positions[PositionIndex], Positions[PositionIndex + 1],
					Positions[PositionIndex + 2] };
				RawBounds += FVector(Raw[0], Raw[1], Raw[2]);
				granny_real32 Local[3] = { Raw[0], Raw[1], Raw[2] };
				if (OwnerModel)
				{
					GrannyTransformPoint(Local, &InversePlacement, Raw);
				}
				InversePlacementBounds += FVector(Local[0], Local[1], Local[2]);
			}
			UE_LOG(LogTemp, Display,
				TEXT("MT2_MODEL_MESH index=%d name=%s vertices=%d bone_bindings=%d raw_min=%s raw_max=%s inverse_min=%s inverse_max=%s"),
				MeshIndex, SourceMesh->Name ? UTF8_TO_TCHAR(SourceMesh->Name) : TEXT("None"),
				Positions.Num() / 3, SourceMesh->BoneBindingCount,
				*RawBounds.Min.ToString(), *RawBounds.Max.ToString(),
				*InversePlacementBounds.Min.ToString(), *InversePlacementBounds.Max.ToString());
		}
		GrannyFreeFile(SourceFile);
		return 0;
	}
	if (AnimationModelPath.IsEmpty())
	{
		AnimationModelPath = ModelPath;
	}
	USkeletalMesh* ExistingMesh = nullptr;
	UAnimSequence* ExistingAnimation = nullptr;
	if (!ExistingMeshPath.IsEmpty())
	{
		const double LoadStartTime = FPlatformTime::Seconds();
		ExistingMesh = LoadObject<USkeletalMesh>(nullptr, *ExistingMeshPath);
		if (!ExistingMesh)
		{
			UE_LOG(LogTemp, Error, TEXT("Could not load existing skeletal mesh: %s"), *ExistingMeshPath);
			return 1;
		}
		UE_LOG(LogTemp, Display, TEXT("Loaded existing skeletal mesh %s in %.3f seconds."),
			*ExistingMeshPath, FPlatformTime::Seconds() - LoadStartTime);
	}
	if (!ExistingAnimationPath.IsEmpty())
	{
		const double LoadStartTime = FPlatformTime::Seconds();
		UE_LOG(LogTemp, Display, TEXT("Loading existing animation: %s"), *ExistingAnimationPath);
		ExistingAnimation = LoadObject<UAnimSequence>(nullptr, *ExistingAnimationPath);
		if (!ExistingAnimation)
		{
			UE_LOG(LogTemp, Error, TEXT("Could not load existing animation: %s"), *ExistingAnimationPath);
			return 1;
		}
		UE_LOG(LogTemp, Display, TEXT("Loaded existing animation in %.3f seconds; reading data model."),
			FPlatformTime::Seconds() - LoadStartTime);
		const IAnimationDataModel* ExistingDataModel = ExistingAnimation->GetDataModel();
		if (!ExistingDataModel)
		{
			UE_LOG(LogTemp, Error, TEXT("Existing animation has no data model: %s"), *ExistingAnimationPath);
			return 1;
		}
		UE_LOG(LogTemp, Display, TEXT("Existing animation data model has %d frames; sampling first track."),
			ExistingDataModel->GetNumberOfFrames());
		TArray<FName> ExistingTrackNames;
		ExistingDataModel->GetBoneTrackNames(ExistingTrackNames);
		if (!ExistingTrackNames.IsEmpty())
		{
			ExistingDataModel->GetBoneTrackTransform(ExistingTrackNames[0], FFrameNumber(0));
		}
		UE_LOG(LogTemp, Display, TEXT("Existing animation data model sampling completed."));
	}

	FString Error;
	FMT2GrannyFileInspection Inspection;
	if (!FMT2GrannyMeshConverter::InspectGrannyFile(ModelPath, Inspection, Error))
	{
		UE_LOG(LogTemp, Error, TEXT("Granny initialization failed: %s"), *Error);
		return 1;
	}

	const FString DiagnosticRoot = UMT2PathSettings::Path(TEXT("GrannyDiagnosticsDirectory"));
	const FString GltfPath = DiagnosticRoot / UMT2PathSettings::Path(TEXT("Part_warrior_4_1"));
	const FString AnimationModelGltfPath = DiagnosticRoot / UMT2PathSettings::Path(TEXT("Part_animation_model"));
	TArray<FMT2GrannyMaterialSlot> MaterialSlots;
	if (!FMT2GrannyMeshConverter::ConvertGrannyToGltf(
		ModelPath,
		GltfPath,
		Error,
		&MaterialSlots,
		EMT2MeshUVTransform::Original,
		FPaths::IsSamePath(AnimationModelPath, ModelPath) ? FString() : AnimationModelPath))
	{
		UE_LOG(LogTemp, Error, TEXT("Mesh conversion failed: %s"), *Error);
		return 1;
	}
	if (!FMT2GrannyMeshConverter::ConvertGrannyToGltf(
		AnimationModelPath,
		AnimationModelGltfPath,
		Error,
		nullptr,
		EMT2MeshUVTransform::Original,
		FString()))
	{
		UE_LOG(LogTemp, Error, TEXT("Animation model conversion failed: %s"), *Error);
		return 1;
	}
	USkeletalMesh* UnrealMesh = ExistingMesh ? ExistingMesh : ImportTemporaryMesh(
		GltfPath,
		UMT2PathSettings::Path(TEXT("__MT2Validation_Warrior41")),
		TEXT("SK_Warrior41_RoundTrip"));
	if (!UnrealMesh)
	{
		UE_LOG(LogTemp, Error, TEXT("Temporary glTF import produced no skeletal mesh."));
		return 1;
	}
	USkeletalMesh* AnimationModelMesh = FPaths::IsSamePath(AnimationModelPath, ModelPath)
		? UnrealMesh
		: ImportTemporaryMesh(
			AnimationModelGltfPath,
			UMT2PathSettings::Path(TEXT("__MT2Validation_AnimationModel")),
			TEXT("SK_AnimationModel_RoundTrip"));
	if (!AnimationModelMesh)
	{
		UE_LOG(LogTemp, Error, TEXT("Temporary animation-model glTF import produced no skeletal mesh."));
		return 1;
	}
	if (UnrealMesh->GetSkeleton() && AnimationModelMesh->GetSkeleton() &&
		UnrealMesh->GetSkeleton() != AnimationModelMesh->GetSkeleton())
	{
		UnrealMesh->GetSkeleton()->AddCompatibleSkeleton(AnimationModelMesh->GetSkeleton());
		AnimationModelMesh->GetSkeleton()->AddCompatibleSkeleton(UnrealMesh->GetSkeleton());
	}

	FMT2GrannyAnimationClip Clip;
	if (!FMT2GrannyMeshConverter::ExtractAnimationClipForModel(
		AnimationPath, AnimationModelPath, Clip, Error, &AnimationModelMesh->GetRefSkeleton()))
	{
		UE_LOG(LogTemp, Error, TEXT("Animation conversion failed: %s"), *Error);
		return 1;
	}
	if (!FPaths::IsSamePath(AnimationModelPath, ModelPath))
	{
		FMT2GrannyAnimationClip ModelBoundClip;
		if (!FMT2GrannyMeshConverter::ExtractAnimationClipForModel(
			AnimationPath, ModelPath, ModelBoundClip, Error, &UnrealMesh->GetRefSkeleton()))
		{
			UE_LOG(LogTemp, Error, TEXT("Model-bound comparison conversion failed: %s"), *Error);
			return 1;
		}

		float WorstTranslationVariation = 0.0f;
		float WorstRotationVariationDegrees = 0.0f;
		FString WorstTranslationBone;
		FString WorstRotationBone;
		for (const FMT2GrannyAnimationTrack& TargetTrack : ModelBoundClip.Tracks)
		{
			const FMT2GrannyAnimationTrack* SourceTrack = Clip.Tracks.FindByPredicate(
				[&TargetTrack](const FMT2GrannyAnimationTrack& Candidate)
				{
					return Candidate.BoneName == TargetTrack.BoneName;
				});
			if (!SourceTrack || TargetTrack.TranslationKeys.IsEmpty() ||
				SourceTrack->TranslationKeys.Num() != TargetTrack.TranslationKeys.Num())
			{
				continue;
			}

			FTransform FirstDelta;
			for (int32 FrameIndex = 0; FrameIndex < TargetTrack.TranslationKeys.Num(); ++FrameIndex)
			{
				const FTransform Source(
					FQuat(SourceTrack->RotationKeys[FrameIndex]),
					FVector(SourceTrack->TranslationKeys[FrameIndex]),
					FVector(SourceTrack->ScaleKeys[FrameIndex]));
				const FTransform Target(
					FQuat(TargetTrack.RotationKeys[FrameIndex]),
					FVector(TargetTrack.TranslationKeys[FrameIndex]),
					FVector(TargetTrack.ScaleKeys[FrameIndex]));
				const FTransform Delta = Target.GetRelativeTransform(Source);
				if (FrameIndex == 0)
				{
					FirstDelta = Delta;
					continue;
				}
				const float TranslationVariation = FVector::Distance(
					Delta.GetTranslation(), FirstDelta.GetTranslation());
				const float RotationVariation = FMath::RadiansToDegrees(
					Delta.GetRotation().AngularDistance(FirstDelta.GetRotation()));
				if (TranslationVariation > WorstTranslationVariation)
				{
					WorstTranslationVariation = TranslationVariation;
					WorstTranslationBone = TargetTrack.BoneName;
				}
				if (RotationVariation > WorstRotationVariationDegrees)
				{
					WorstRotationVariationDegrees = RotationVariation;
					WorstRotationBone = TargetTrack.BoneName;
				}
			}
		}
		UE_LOG(LogTemp, Display,
			TEXT("MT2_MODEL_BOUND_DELTA max_translation_variation_cm=%.9f bone=%s max_rotation_variation_deg=%.9f bone=%s"),
			WorstTranslationVariation,
			*WorstTranslationBone,
			WorstRotationVariationDegrees,
			*WorstRotationBone);
	}
	TArray<FString> HelperTracks;
	for (const FMT2GrannyAnimationTrack& Track : Clip.Tracks)
	{
		if (Track.BoneName.Contains(TEXT("front"), ESearchCase::IgnoreCase))
		{
			HelperTracks.Add(Track.BoneName);
		}
	}
	UE_LOG(LogTemp, Display, TEXT("MT2_GRANNY_TRACKS imported=%d helper_tracks=%s"),
		Clip.Tracks.Num(), *FString::Join(HelperTracks, TEXT(",")));
	for (const TCHAR* DiagnosticBone : { TEXT("Bip01"), TEXT("Bip01_Pelvis"),
		TEXT("bone_front_01"), TEXT("equip_right_hand"), TEXT("Bone20"),
		TEXT("Bone21"), TEXT("Bone22"), TEXT("Bone23"), TEXT("Bone24"),
		TEXT("Bone25"), TEXT("Bone26"), TEXT("Bone27"), TEXT("Bip01_L_Finger0"),
		TEXT("Bip01_L_Finger1"), TEXT("Bip01_L_Finger2") })
	{
		const FMT2GrannyAnimationTrack* Track = Clip.Tracks.FindByPredicate(
			[DiagnosticBone](const FMT2GrannyAnimationTrack& Candidate)
			{
				return Candidate.BoneName == DiagnosticBone;
			});
		const int32 RefIndex = AnimationModelMesh->GetRefSkeleton().FindBoneIndex(FName(DiagnosticBone));
		if (Track && !Track->TranslationKeys.IsEmpty() && RefIndex != INDEX_NONE)
		{
			const FTransform Key(
				FQuat(Track->RotationKeys[0]), FVector(Track->TranslationKeys[0]),
				FVector(Track->ScaleKeys[0]));
			UE_LOG(LogTemp, Display, TEXT("MT2_LOCAL_KEY bone=%s ref=%s key=%s"),
				DiagnosticBone,
				*AnimationModelMesh->GetRefSkeleton().GetRefBonePose()[RefIndex].ToHumanReadableString(),
				*Key.ToHumanReadableString());
		}
	}
	FMT2GrannyAnimationClip PlaybackClip = Clip;
	UAnimSequence* UnrealAnimation = ExistingAnimation
		? ExistingAnimation
		: BuildTemporaryAnimation(AnimationModelMesh, PlaybackClip);
	if (!UnrealAnimation || !UnrealAnimation->GetDataModel())
	{
		UE_LOG(LogTemp, Error, TEXT("Could not build temporary UE animation."));
		return 1;
	}
	UE_LOG(LogTemp, Display,
		TEXT("MT2_EXISTING_PAIR mesh=%s mesh_skeleton=%s animation=%s animation_skeleton=%s mesh_bones=%d animation_frames=%d"),
		*UnrealMesh->GetPathName(),
		UnrealMesh->GetSkeleton() ? *UnrealMesh->GetSkeleton()->GetPathName() : TEXT("None"),
		*UnrealAnimation->GetPathName(),
		UnrealAnimation->GetSkeleton() ? *UnrealAnimation->GetSkeleton()->GetPathName() : TEXT("None"),
		UnrealMesh->GetRefSkeleton().GetNum(),
		UnrealAnimation->GetDataModel()->GetNumberOfFrames());
	if (ExistingAnimation)
	{
		const IAnimationDataModel* ExistingModel = ExistingAnimation->GetDataModel();
		const int32 DiagnosticFrame = FMath::Min(Clip.NumberOfFrames / 2, ExistingModel->GetNumberOfFrames());
		for (const TCHAR* DiagnosticBone : { TEXT("Bip01"), TEXT("Bip01_Pelvis"),
			TEXT("Bip01_Head"), TEXT("bone_front_03"), TEXT("equip_right_hand"),
			TEXT("equip_left_hand") })
		{
			const FName BoneName(DiagnosticBone);
			const FMT2GrannyAnimationTrack* ExpectedTrack = Clip.Tracks.FindByPredicate(
				[&BoneName](const FMT2GrannyAnimationTrack& Track)
				{
					return FName(*Track.BoneName) == BoneName;
				});
			if (!ExpectedTrack || !ExistingModel->IsValidBoneTrackName(BoneName) ||
				!ExpectedTrack->TranslationKeys.IsValidIndex(DiagnosticFrame))
			{
				continue;
			}
			const FTransform Stored = ExistingModel->GetBoneTrackTransform(
				BoneName, FFrameNumber(DiagnosticFrame));
			const FTransform Expected(
				FQuat(ExpectedTrack->RotationKeys[DiagnosticFrame]),
				FVector(ExpectedTrack->TranslationKeys[DiagnosticFrame]),
				FVector(ExpectedTrack->ScaleKeys[DiagnosticFrame]));
			UE_LOG(LogTemp, Display,
				TEXT("MT2_STORED_KEY bone=%s frame=%d expected=%s stored=%s"),
				DiagnosticBone, DiagnosticFrame,
				*Expected.ToHumanReadableString(), *Stored.ToHumanReadableString());
		}
	}

	granny_file* ModelFile = GrannyReadEntireFile(TCHAR_TO_UTF8(*ModelPath));
	granny_file* AnimationFile = GrannyReadEntireFile(TCHAR_TO_UTF8(*AnimationPath));
	granny_file* PoseModelFile = FPaths::IsSamePath(AnimationModelPath, ModelPath)
		? nullptr
		: GrannyReadEntireFile(TCHAR_TO_UTF8(*AnimationModelPath));
	granny_file_info* ModelInfo = ModelFile ? GrannyGetFileInfo(ModelFile) : nullptr;
	granny_file_info* AnimationInfo = AnimationFile ? GrannyGetFileInfo(AnimationFile) : nullptr;
	granny_file_info* PoseModelInfo = PoseModelFile ? GrannyGetFileInfo(PoseModelFile) : ModelInfo;
	granny_model* Model = PoseModelInfo && PoseModelInfo->ModelCount > 0
		? PoseModelInfo->Models[0]
		: nullptr;
	granny_animation* Animation = AnimationInfo && AnimationInfo->AnimationCount > 0
		? AnimationInfo->Animations[0] : nullptr;
	if (!Model || !Model->Skeleton || !Animation)
	{
		UE_LOG(LogTemp, Error, TEXT("Invalid source model or animation."));
		if (PoseModelFile) GrannyFreeFile(PoseModelFile);
		if (AnimationFile) GrannyFreeFile(AnimationFile);
		if (ModelFile) GrannyFreeFile(ModelFile);
		return 1;
	}

	TArray<FSourceMeshData> SourceMeshes;
	if (!ReadSourceMeshes(
		ModelInfo, Model->Skeleton, UnrealMesh->GetRefSkeleton(), SourceMeshes, Error))
	{
		UE_LOG(LogTemp, Error, TEXT("Source mesh read failed: %s"), *Error);
		if (PoseModelFile) GrannyFreeFile(PoseModelFile);
		GrannyFreeFile(AnimationFile);
		GrannyFreeFile(ModelFile);
		return 1;
	}

	FBox SourceBounds(EForceInit::ForceInit);
	for (const FSourceMeshData& SourceMesh : SourceMeshes)
	{
		for (const FVector3f& Position : SourceMesh.Positions)
		{
			SourceBounds += FVector(GrannyToUnreal(Position));
		}
	}
	const FBoxSphereBounds ImportedBounds = UnrealMesh->GetImportedBounds();
	UE_LOG(LogTemp, Display,
		TEXT("MT2_GRANNY_BOUNDS source_min=%s source_max=%s ue_origin=%s ue_extent=%s"),
		*SourceBounds.Min.ToString(), *SourceBounds.Max.ToString(),
		*ImportedBounds.Origin.ToString(), *ImportedBounds.BoxExtent.ToString());

	TArray<FVector3f> SourcePositions;
	for (const FSourceMeshData& SourceMesh : SourceMeshes)
	{
		SourcePositions.Append(SourceMesh.Positions);
	}
	TArray<FSoftSkinVertex> ImportedVertices;
	const FSkeletalMeshModel* ImportedModel = UnrealMesh->GetImportedModel();
	if (ImportedModel && ImportedModel->LODModels.IsValidIndex(0))
	{
		ImportedModel->LODModels[0].GetNonClothVertices(ImportedVertices);
	}
	if (SourcePositions.IsEmpty() || ImportedVertices.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("Imported vertex validation has no source or UE vertices."));
		if (PoseModelFile) GrannyFreeFile(PoseModelFile);
		GrannyFreeFile(AnimationFile);
		GrannyFreeFile(ModelFile);
		return 1;
	}

	double GeometrySquaredError = 0.0;
	float GeometryMaxError = 0.0f;
	for (const FSoftSkinVertex& ImportedVertex : ImportedVertices)
	{
		float NearestSquared = MAX_flt;
		for (const FVector3f& SourcePosition : SourcePositions)
		{
			NearestSquared = FMath::Min(
				NearestSquared, FVector3f::DistSquared(ImportedVertex.Position, SourcePosition));
		}
		GeometrySquaredError += NearestSquared;
		GeometryMaxError = FMath::Max(GeometryMaxError, FMath::Sqrt(NearestSquared));
	}
	const double GeometryRmsError = FMath::Sqrt(
		GeometrySquaredError / static_cast<double>(ImportedVertices.Num()));
	UE_LOG(LogTemp, Display,
		TEXT("MT2_IMPORTED_GEOMETRY source_vertices=%d ue_vertices=%d rms_cm=%.9f max_cm=%.9f"),
		SourcePositions.Num(), ImportedVertices.Num(), GeometryRmsError, GeometryMaxError);
	const FReferenceSkeleton& DiagnosticSkeleton = UnrealMesh->GetRefSkeleton();
	for (const TCHAR* DiagnosticBoneName : { TEXT("Bip01"), TEXT("Bip01_Pelvis"),
		TEXT("Bip01_R_UpperArm"), TEXT("bone_front_01"), TEXT("bone_front_03"),
		TEXT("equip_right_hand"), TEXT("equip_left_hand") })
	{
		const int32 UnrealBoneIndex = DiagnosticSkeleton.FindBoneIndex(FName(DiagnosticBoneName));
		const FString RawBoneName = FString(DiagnosticBoneName).Replace(TEXT("_"), TEXT(" "));
		int32 GrannyBoneIndex = FindGrannyBone(Model->Skeleton, TCHAR_TO_UTF8(*RawBoneName));
		if (GrannyBoneIndex == INDEX_NONE)
		{
			GrannyBoneIndex = FindGrannyBone(Model->Skeleton, TCHAR_TO_UTF8(DiagnosticBoneName));
		}
		if (UnrealBoneIndex != INDEX_NONE && GrannyBoneIndex != INDEX_NONE)
		{
			const granny_transform& SourceTransform = Model->Skeleton->Bones[GrannyBoneIndex].LocalTransform;
			const FTransform& UnrealTransform = DiagnosticSkeleton.GetRefBonePose()[UnrealBoneIndex];
			UE_LOG(LogTemp, Display,
				TEXT("MT2_GRANNY_BONE name=%s source_pos=(%.6f,%.6f,%.6f) source_q=(%.6f,%.6f,%.6f,%.6f) source_ss=[%.6f %.6f %.6f; %.6f %.6f %.6f; %.6f %.6f %.6f] ue=%s"),
				DiagnosticBoneName,
				SourceTransform.Position[0], SourceTransform.Position[1], SourceTransform.Position[2],
				SourceTransform.Orientation[0], SourceTransform.Orientation[1],
				SourceTransform.Orientation[2], SourceTransform.Orientation[3],
				SourceTransform.ScaleShear[0][0], SourceTransform.ScaleShear[0][1], SourceTransform.ScaleShear[0][2],
				SourceTransform.ScaleShear[1][0], SourceTransform.ScaleShear[1][1], SourceTransform.ScaleShear[1][2],
				SourceTransform.ScaleShear[2][0], SourceTransform.ScaleShear[2][1], SourceTransform.ScaleShear[2][2],
				*UnrealTransform.ToHumanReadableString());
		}
	}

	granny_model_instance* Instance = GrannyInstantiateModel(Model);
	granny_control* Control = Instance ? GrannyPlayControlledAnimation(0.0f, Animation, Instance) : nullptr;
	granny_local_pose* LocalPose = Instance ? GrannyNewLocalPose(Model->Skeleton->BoneCount) : nullptr;
	granny_world_pose* WorldPose = Instance ? GrannyNewWorldPose(Model->Skeleton->BoneCount) : nullptr;
	if (!Instance || !Control || !LocalPose || !WorldPose)
	{
		UE_LOG(LogTemp, Error, TEXT("Could not create Granny sampling state."));
		if (WorldPose) GrannyFreeWorldPose(WorldPose);
		if (LocalPose) GrannyFreeLocalPose(LocalPose);
		if (Control) GrannyFreeControl(Control);
		if (Instance) GrannyFreeModelInstance(Instance);
		if (PoseModelFile) GrannyFreeFile(PoseModelFile);
		GrannyFreeFile(AnimationFile);
		GrannyFreeFile(ModelFile);
		return 1;
	}
	GrannySetControlWeight(Control, 1.0f);
	GrannySetControlEaseIn(Control, false);
	GrannySetControlEaseOut(Control, false);
	GrannySetControlLoopCount(Control, 1);
	GrannySetControlForceClampedLooping(Control, true);

	const FReferenceSkeleton& RefSkeleton = UnrealMesh->GetRefSkeleton();
	const FReferenceSkeleton& AnimationRefSkeleton = AnimationModelMesh->GetRefSkeleton();
	TArray<FTransform> ReferenceLocalPose = RefSkeleton.GetRefBonePose();
	TArray<FTransform> ReferenceComponentPose;
	BuildComponentPose(RefSkeleton, ReferenceLocalPose, ReferenceComponentPose);
	const IAnimationDataModel* DataModel = UnrealAnimation->GetDataModel();
	TArray<FString> CsvLines = { TEXT("frame,mesh,vertex,error_cm,source_x,source_y,source_z,unreal_x,unreal_y,unreal_z,bones") };
	double TotalSquaredError = 0.0;
	int64 ComparedVertices = 0;
	float MaxError = 0.0f;
	FString MaxErrorContext;
	float FrontBoneMaxError = 0.0f;

	const TArray<int32> Frames = { 0, Clip.NumberOfFrames / 4, Clip.NumberOfFrames / 2,
		(Clip.NumberOfFrames * 3) / 4, Clip.NumberOfFrames };
	for (const int32 FrameIndex : Frames)
	{
		const float SampleTime = Clip.Duration * static_cast<float>(FrameIndex) /
			static_cast<float>(Clip.NumberOfFrames);
		GrannySetModelClock(Instance, SampleTime);
		if (!GrannySampleSingleModelAnimation(
			Instance, Control, 0, Model->Skeleton->BoneCount, LocalPose))
		{
			UE_LOG(LogTemp, Error, TEXT("Granny sampling failed at frame %d."), FrameIndex);
			continue;
		}
		GrannyBuildWorldPose(
			Model->Skeleton, 0, Model->Skeleton->BoneCount, LocalPose, nullptr, WorldPose);

		TArray<FTransform> AnimatedLocalPose;
		if (FPaths::IsSamePath(AnimationModelPath, ModelPath))
		{
			AnimatedLocalPose = ReferenceLocalPose;
			for (int32 BoneIndex = 0; BoneIndex < RefSkeleton.GetNum(); ++BoneIndex)
			{
				const FName BoneName = RefSkeleton.GetBoneName(BoneIndex);
				if (DataModel->IsValidBoneTrackName(BoneName))
				{
					AnimatedLocalPose[BoneIndex] = DataModel->GetBoneTrackTransform(
						BoneName, FFrameNumber(FrameIndex));
				}
			}
		}
		else if (!EvaluateAnimationOnMesh(UnrealAnimation, UnrealMesh, SampleTime, AnimatedLocalPose))
		{
			UE_LOG(LogTemp, Error, TEXT("UE pose extraction failed at frame %d."), FrameIndex);
			continue;
		}
		TArray<FTransform> AnimatedComponentPose;
		BuildComponentPose(RefSkeleton, AnimatedLocalPose, AnimatedComponentPose);

		TArray<FMatrix> UnrealCompositeMatrices;
		UnrealCompositeMatrices.SetNum(RefSkeleton.GetNum());
		for (int32 BoneIndex = 0; BoneIndex < RefSkeleton.GetNum(); ++BoneIndex)
		{
			UnrealCompositeMatrices[BoneIndex] =
				ReferenceComponentPose[BoneIndex].ToInverseMatrixWithScale() *
				AnimatedComponentPose[BoneIndex].ToMatrixWithScale();
		}

		for (const FSourceMeshData& Mesh : SourceMeshes)
		{
			for (int32 VertexIndex = 0; VertexIndex < Mesh.Positions.Num(); ++VertexIndex)
			{
				FVector3f GrannySkinned = FVector3f::ZeroVector;
				FVector UnrealSkinned = FVector::ZeroVector;
				float TotalWeight = 0.0f;
				bool bFrontBoneVertex = false;
				TArray<FString> BoneNames;
				for (int32 InfluenceIndex = 0; InfluenceIndex < 4; ++InfluenceIndex)
				{
					const int32 ValueIndex = VertexIndex * 4 + InfluenceIndex;
					const float Weight = Mesh.Weights[ValueIndex];
					const int32 LocalBoneIndex = Mesh.LocalBoneIndices[ValueIndex];
					if (Weight <= UE_SMALL_NUMBER ||
						!Mesh.SourceSkeletonIndices.IsValidIndex(LocalBoneIndex))
					{
						continue;
					}
					const int32 SourceBoneIndex = Mesh.SourceSkeletonIndices[LocalBoneIndex];
					const int32 UnrealBoneIndex = Mesh.UnrealSkeletonIndices[LocalBoneIndex];
					const granny_real32* GrannyComposite =
						GrannyGetWorldPoseComposite4x4(WorldPose, SourceBoneIndex);
					GrannySkinned += TransformGrannyPoint(
						GrannyComposite, Mesh.Positions[VertexIndex]) * Weight;
					const FVector UnrealPosition = FVector(GrannyToUnreal(Mesh.Positions[VertexIndex]));
					UnrealSkinned += UnrealCompositeMatrices[UnrealBoneIndex].TransformPosition(
						UnrealPosition) * Weight;
					TotalWeight += Weight;
					const FString BoneName = RefSkeleton.GetBoneName(UnrealBoneIndex).ToString();
					BoneNames.Add(BoneName);
					bFrontBoneVertex |= BoneName.StartsWith(TEXT("bone_front_"), ESearchCase::IgnoreCase);
				}
				if (TotalWeight <= UE_SMALL_NUMBER)
				{
					continue;
				}
				GrannySkinned /= TotalWeight;
				UnrealSkinned /= TotalWeight;
				const FVector SourceInUnreal = FVector(GrannyToUnreal(GrannySkinned));
				const float ErrorCm = FVector::Distance(SourceInUnreal, UnrealSkinned);
				TotalSquaredError += FMath::Square(static_cast<double>(ErrorCm));
				ComparedVertices++;
				if (bFrontBoneVertex)
				{
					FrontBoneMaxError = FMath::Max(FrontBoneMaxError, ErrorCm);
				}
				if (ErrorCm > MaxError)
				{
					MaxError = ErrorCm;
					MaxErrorContext = FString::Printf(TEXT("frame=%d mesh=%s vertex=%d bones=%s"),
						FrameIndex, *Mesh.Name, VertexIndex, *FString::Join(BoneNames, TEXT("|")));
				}
				if (ErrorCm > 0.01f)
				{
					CsvLines.Add(FString::Printf(
						TEXT("%d,%s,%d,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%s"),
						FrameIndex, *Mesh.Name, VertexIndex, ErrorCm,
						SourceInUnreal.X, SourceInUnreal.Y, SourceInUnreal.Z,
						UnrealSkinned.X, UnrealSkinned.Y, UnrealSkinned.Z,
						*FString::Join(BoneNames, TEXT("|"))));
				}
			}
		}
	}

	const double RmsError = ComparedVertices > 0
		? FMath::Sqrt(TotalSquaredError / static_cast<double>(ComparedVertices)) : 0.0;
	const FString CsvPath = DiagnosticRoot / UMT2PathSettings::Path(TEXT("GrannyVertexReportFilename"));
	FFileHelper::SaveStringArrayToFile(CsvLines, *CsvPath);
	UE_LOG(LogTemp, Display,
		TEXT("MT2_GRANNY_ROUNDTRIP vertices=%lld frames=%d rms_cm=%.9f max_cm=%.9f front_max_cm=%.9f max_context=\"%s\" report=%s"),
		ComparedVertices, Frames.Num(), RmsError, MaxError, FrontBoneMaxError,
		*MaxErrorContext, *CsvPath);

	GrannyFreeWorldPose(WorldPose);
	GrannyFreeLocalPose(LocalPose);
	GrannyFreeControl(Control);
	GrannyFreeModelInstance(Instance);
	if (PoseModelFile) GrannyFreeFile(PoseModelFile);
	GrannyFreeFile(AnimationFile);
	GrannyFreeFile(ModelFile);
	const bool bGeometryMatches = GeometryRmsError <= 0.001 && GeometryMaxError <= 0.01f;
	const bool bAnimationMatches = RmsError <= 0.05 && MaxError <= 2.0f;
	return ComparedVertices > 0 && bGeometryMatches && bAnimationMatches ? 0 : 1;
}
