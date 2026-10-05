/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "CoreMinimal.h"
#include "API/MT2ImportTypes.h"

struct FReferenceSkeleton;

struct FMT2GrannyMaterialSlot
{
	FString SlotName;
	FString ImportedSlotName;
	FString AmbientTextureReference;
	FString DiffuseTextureReference;
	FString SpecularTextureReference;
	FString OpacityTextureReference;
	FString BumpTextureReference;
	FString ReflectionTextureReference;
	FLinearColor DiffuseColor = FLinearColor::White;
	FLinearColor SpecularColor = FLinearColor::Black;
	float Opacity = 1.0f;
	float Shininess = 0.0f;
	float ShininessStrength = 0.0f;
	float ReflectionLevel = 0.0f;
	float IndexOfRefraction = 1.0f;
	bool bHasDiffuseColor = false;
	bool bHasSpecularColor = false;
	bool bHasMaterialProperties = false;
	bool bTwoSided = false;
};

enum class EMT2GrannyFileType : uint8
{
	Unknown,
	StaticMesh,
	SkeletalMesh,
	Animation
};

struct FMT2GrannyFileInspection
{
	EMT2GrannyFileType Type = EMT2GrannyFileType::Unknown;
	int32 MeshCount = 0;
	int32 SkeletonCount = 0;
	int32 BoneCount = 0;
	int32 AnimationCount = 0;
	int32 TrackGroupCount = 0;
	int32 BoneBindingCount = 0;
	int32 MaxBoneBindingCount = 0;
};

struct FMT2GrannyAnimationTrack
{
	FString BoneName;
	TArray<FVector3f> TranslationKeys;
	TArray<FQuat4f> RotationKeys;
	TArray<FVector3f> ScaleKeys;
};

struct FMT2GrannyAnimationClip
{
	FString Name;
	FString TrackGroupName;
	float Duration = 0.0f;
	int32 FrameRate = 30;
	int32 NumberOfFrames = 1;
	TArray<FMT2GrannyAnimationTrack> Tracks;
};

class FMT2GrannyMeshConverter
{
public:
	static bool InspectGrannyFile(const FString& InputGrannyPath, FMT2GrannyFileInspection& OutInspection, FString& OutError);
	static bool ExtractAnimationClip(const FString& InputGrannyPath, FMT2GrannyAnimationClip& OutClip, FString& OutError);
	static bool ExtractAnimationClipForModel(const FString& InputGrannyPath, const FString& InputModelGrannyPath, FMT2GrannyAnimationClip& OutClip, FString& OutError, const FReferenceSkeleton* TargetReferenceSkeleton = nullptr);
	static bool ConvertGrannyToObj(const FString& InputGrannyPath, const FString& OutputObjPath, FString& OutError, TArray<FMT2GrannyMaterialSlot>* OutMaterialSlots = nullptr);
	static bool ConvertGrannyToGltf(
		const FString& InputGrannyPath,
		const FString& OutputGltfPath,
		FString& OutError,
		TArray<FMT2GrannyMaterialSlot>* OutMaterialSlots = nullptr,
		EMT2MeshUVTransform UVTransform = EMT2MeshUVTransform::Original,
		const FString& ReferenceSkeletonGrannyPath = FString());
	static bool ExtractMaterialSlots(const FString& InputGrannyPath, TArray<FMT2GrannyMaterialSlot>& OutMaterialSlots, FString& OutError);
};
