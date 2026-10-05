/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "Importers/MT2GrannyMeshConverter.h"

#include "ReferenceSkeleton.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFilemanager.h"
#include "Interfaces/IPluginManager.h"
#include "MT2AssetScanner.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

THIRD_PARTY_INCLUDES_START
#include "granny.h"
THIRD_PARTY_INCLUDES_END

namespace
{
	constexpr float GrannyUnitsToGltfMeters = 0.01f;
	constexpr const TCHAR* StaticMeshConversionVersion =
		TEXT("MT2UE_STATIC_CONVERSION_V4_SKELETAL_ATTACHMENT_BASIS");

	bool EnsureGrannyDllLoaded(FString& OutError)
	{
		static bool bAttemptedLoad = false;
		static bool bLoaded = false;

		if (bAttemptedLoad)
		{
			if (!bLoaded)
			{
				OutError = TEXT("granny2_x64.dll was not loaded.");
			}
			return bLoaded;
		}

		bAttemptedLoad = true;
		TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("MT2UE"));
		if (!Plugin.IsValid())
		{
			OutError = TEXT("Could not locate MT2UE plugin.");
			return false;
		}

		const FString DllPath = Plugin->GetBaseDir() / TEXT("Binaries/ThirdParty/Granny/Win64/granny2_x64.dll");
		void* Handle = FPlatformProcess::GetDllHandle(*DllPath);
		bLoaded = Handle != nullptr;
		if (!bLoaded)
		{
			OutError = FString::Printf(TEXT("Could not load Granny DLL: %s"), *DllPath);
		}
		return bLoaded;
	}

	bool CopyVertexFloats(granny_mesh* Mesh, const char* MemberName, int32 ComponentCount, TArray<float>& OutValues)
	{
		granny_variant Ignore;
		if (!GrannyFindMatchingMember(GrannyGetMeshVertexType(Mesh), GrannyGetMeshVertices(Mesh), MemberName, &Ignore))
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

	bool CopyVertexUInt8(granny_mesh* Mesh, const char* MemberName, int32 ComponentCount, TArray<uint8>& OutValues)
	{
		granny_variant Ignore;
		if (!GrannyFindMatchingMember(GrannyGetMeshVertexType(Mesh), GrannyGetMeshVertices(Mesh), MemberName, &Ignore))
		{
			return false;
		}

		granny_data_type_definition Type[] =
		{
			{ GrannyUInt8Member, MemberName, 0, ComponentCount },
			{ GrannyEndMember }
		};

		OutValues.SetNumZeroed(GrannyGetMeshVertexCount(Mesh) * ComponentCount);
		GrannyCopyMeshVertices(Mesh, Type, OutValues.GetData());
		return true;
	}

	granny_model* FindOwningModel(const granny_file_info* Info, const granny_mesh* Mesh)
	{
		if (!Info || !Mesh || !Info->Models)
		{
			return nullptr;
		}
		for (granny_int32x ModelIndex = 0; ModelIndex < Info->ModelCount; ++ModelIndex)
		{
			granny_model* Model = Info->Models[ModelIndex];
			if (!Model || !Model->MeshBindings)
			{
				continue;
			}
			for (granny_int32x BindingIndex = 0; BindingIndex < Model->MeshBindingCount; ++BindingIndex)
			{
				if (Model->MeshBindings[BindingIndex].Mesh == Mesh)
				{
					return Model;
				}
			}
		}
		return nullptr;
	}

	void ApplyInverseModelInitialPlacement(
		const granny_file_info* Info, granny_mesh* Mesh, TArray<float>& Positions, TArray<float>& Normals)
	{
		granny_model* Model = FindOwningModel(Info, Mesh);
		if (!Model)
		{
			return;
		}

		// Granny's InitialPlacement describes where the exported model lived in its source scene.
		// A child model attached to a character bone is normalized by the inverse placement. Baking
		// the forward placement moves weapon vertices away from the grip a second time; this is most
		// visible on two-hand swords whose source placement can exceed 150 units.
		granny_transform InversePlacement;
		GrannyBuildInverse(&InversePlacement, &Model->InitialPlacement);
		for (int32 Index = 0; Index + 2 < Positions.Num(); Index += 3)
		{
			granny_real32 Source[3] =
			{
				Positions[Index], Positions[Index + 1], Positions[Index + 2]
			};
			granny_real32 Result[3];
			GrannyTransformPoint(Result, &InversePlacement, Source);
			Positions[Index] = Result[0];
			Positions[Index + 1] = Result[1];
			Positions[Index + 2] = Result[2];
		}

		for (int32 Index = 0; Index + 2 < Normals.Num(); Index += 3)
		{
			granny_real32 Source[3] =
			{
				Normals[Index], Normals[Index + 1], Normals[Index + 2]
			};
			granny_real32 Result[3];
			GrannyTransformVector(Result, &InversePlacement, Source);
			const FVector3f Normal(Result[0], Result[1], Result[2]);
			const FVector3f SafeNormal = Normal.GetSafeNormal();
			Normals[Index] = SafeNormal.X;
			Normals[Index + 1] = SafeNormal.Y;
			Normals[Index + 2] = SafeNormal.Z;
		}
	}

	bool IsWeaponAttachmentMesh(const FString& InputGrannyPath)
	{
		FString NormalizedPath = InputGrannyPath;
		FPaths::NormalizeFilename(NormalizedPath);
		return NormalizedPath.Contains(TEXT("/item/weapon/"), ESearchCase::IgnoreCase);
	}

	void ConvertWeaponToSkeletalAttachmentBasis(
		const FString& InputGrannyPath, TArray<float>& Positions, TArray<float>& Normals)
	{
		if (!IsWeaponAttachmentMesh(InputGrannyPath))
		{
			return;
		}

		// Unreal's OBJ importer converts source (X,Y,Z) to (X,-Y,Z), while the Granny
		// skeletal pipeline converts it to (-X,Y,Z). Weapons attach directly to skeletal
		// bones, so rotate their source vertices 180 degrees around Z before OBJ import.
		// The composed result is (-X,Y,Z), including the baked Granny pivot.
		for (int32 Index = 0; Index + 2 < Positions.Num(); Index += 3)
		{
			Positions[Index] = -Positions[Index];
			Positions[Index + 1] = -Positions[Index + 1];
		}
		for (int32 Index = 0; Index + 2 < Normals.Num(); Index += 3)
		{
			Normals[Index] = -Normals[Index];
			Normals[Index + 1] = -Normals[Index + 1];
		}
	}

	FString JsonFloat(float Value)
	{
		if (!FMath::IsFinite(Value))
		{
			return TEXT("0");
		}
		return FString::SanitizeFloat(Value);
	}

	FString JsonString(const FString& Value)
	{
		FString Escaped = Value;
		Escaped.ReplaceInline(TEXT("\\"), TEXT("\\\\"));
		Escaped.ReplaceInline(TEXT("\""), TEXT("\\\""));
		return FString::Printf(TEXT("\"%s\""), *Escaped);
	}

	void AlignBuffer(TArray<uint8>& Buffer, int32 Alignment = 4)
	{
		while ((Buffer.Num() % Alignment) != 0)
		{
			Buffer.Add(0);
		}
	}

	template<typename T>
	int32 AppendTypedBuffer(TArray<uint8>& Buffer, const TArray<T>& Values)
	{
		AlignBuffer(Buffer, alignof(T));
		const int32 Offset = Buffer.Num();
		const int32 Bytes = Values.Num() * sizeof(T);
		Buffer.AddUninitialized(Bytes);
		if (Bytes > 0)
		{
			FMemory::Memcpy(Buffer.GetData() + Offset, Values.GetData(), Bytes);
		}
		AlignBuffer(Buffer);
		return Offset;
	}

	int32 FindSkeletonBoneIndexByName(granny_skeleton* Skeleton, const FString& BoneName)
	{
		if (!Skeleton)
		{
			return INDEX_NONE;
		}

		for (granny_int32x BoneIndex = 0; BoneIndex < Skeleton->BoneCount; ++BoneIndex)
		{
			if (BoneName.Equals(UTF8_TO_TCHAR(Skeleton->Bones[BoneIndex].Name), ESearchCase::IgnoreCase))
			{
				return BoneIndex;
			}
		}
		return INDEX_NONE;
	}

	FMatrix GetGrannyToGltfBasis()
	{
		// Interchange converts glTF (right-handed, Y-up) to Unreal (left-handed, Z-up) by
		// swapping Y and Z. Pre-transform Granny data with (x, y, z) -> (-x, z, y) so the two
		// conversions compose to (-x, y, z) in Unreal: the same source-X mirror the landscape
		// and map object import already use. Unlike a plain Y/Z swap this pre-transform is a
		// proper rotation (determinant +1), so it does not mirror the mesh by itself; the single
		// handedness change happens exactly once, in Interchange's conversion. The matrix is a
		// symmetric involution, so conjugation below can use Basis.GetTransposed() * M * Basis.
		return FMatrix(
			FPlane(-1.0, 0.0, 0.0, 0.0),
			FPlane(0.0, 0.0, 1.0, 0.0),
			FPlane(0.0, 1.0, 0.0, 0.0),
			FPlane(0.0, 0.0, 0.0, 1.0));
	}

	FVector3f ConvertGrannyVectorToGltf(const FVector3f& Vector)
	{
		return FVector3f(-Vector.X, Vector.Z, Vector.Y);
	}

	FMatrix MakeGrannyMatrix(const granny_real32* Matrix)
	{
		return FMatrix(
			FPlane(Matrix[0], Matrix[1], Matrix[2], Matrix[3]),
			FPlane(Matrix[4], Matrix[5], Matrix[6], Matrix[7]),
			FPlane(Matrix[8], Matrix[9], Matrix[10], Matrix[11]),
			FPlane(Matrix[12], Matrix[13], Matrix[14], Matrix[15]));
	}

	FMatrix ConvertGrannyMatrixToUnreal(const granny_real32* Matrix)
	{
		// Granny is right-handed Z-up, Unreal left-handed Z-up. Match the skeletal mesh and map
		// import convention by mirroring source X: conjugate with N = diag(-1, 1, 1, 1), which
		// negates every element whose row or column index (but not both) is 0.
		FMatrix Result = MakeGrannyMatrix(Matrix);
		Result.M[0][1] = -Result.M[0][1];
		Result.M[0][2] = -Result.M[0][2];
		Result.M[0][3] = -Result.M[0][3];
		Result.M[1][0] = -Result.M[1][0];
		Result.M[2][0] = -Result.M[2][0];
		Result.M[3][0] = -Result.M[3][0];
		return Result;
	}

	// Bone-local transforms extracted for animation tracks go through the same source-X mirror
	// as the mesh and skeleton: translation (x, y, z) -> (-x, y, z), rotation quaternion
	// (x, y, z, w) -> (x, -y, -z, w), scale unchanged.
	FTransform ConvertGrannyLocalTransformToUnreal(const FTransform& Transform)
	{
		FTransform Result;
		const FQuat Rotation = Transform.GetRotation();
		Result.SetRotation(FQuat(Rotation.X, -Rotation.Y, -Rotation.Z, Rotation.W));
		const FVector Translation = Transform.GetTranslation();
		Result.SetTranslation(FVector(-Translation.X, Translation.Y, Translation.Z));
		Result.SetScale3D(Transform.GetScale3D());
		Result.NormalizeRotation();
		return Result;
	}

	FTransform ConvertGrannyWorldMatrixToUnreal(const granny_real32* Matrix)
	{
		FTransform Result;
		Result.SetFromMatrix(ConvertGrannyMatrixToUnreal(Matrix));
		Result.NormalizeRotation();
		return Result;
	}

	FString MakeJsonFloatArray(const TArray<float>& Values)
	{
		TArray<FString> Parts;
		Parts.Reserve(Values.Num());
		for (float Value : Values)
		{
			Parts.Add(JsonFloat(Value));
		}
		return TEXT("[") + FString::Join(Parts, TEXT(",")) + TEXT("]");
	}

	FString MakeJsonIntArray(const TArray<int32>& Values)
	{
		TArray<FString> Parts;
		Parts.Reserve(Values.Num());
		for (int32 Value : Values)
		{
			Parts.Add(FString::FromInt(Value));
		}
		return TEXT("[") + FString::Join(Parts, TEXT(",")) + TEXT("]");
	}

	FString ToObjName(const char* Name, int32 FallbackIndex)
	{
		if (Name && Name[0] != '\0')
		{
			return FMT2AssetScanner::SanitizePackagePathSegment(UTF8_TO_TCHAR(Name));
		}
		return FString::Printf(TEXT("mesh_%d"), FallbackIndex);
	}

	FString TextureFileName(granny_texture* Texture)
	{
		if (Texture && Texture->FromFileName && Texture->FromFileName[0] != '\0')
		{
			return UTF8_TO_TCHAR(Texture->FromFileName);
		}
		return FString();
	}

	void ApplyUVTransform(TArray<float>& TexCoords, EMT2MeshUVTransform UVTransform)
	{
		for (int32 Index = 0; Index + 1 < TexCoords.Num(); Index += 2)
		{
			float U = TexCoords[Index];
			float V = TexCoords[Index + 1];
			switch (UVTransform)
			{
			case EMT2MeshUVTransform::FlipU:
				U = 1.0f - U;
				break;
			case EMT2MeshUVTransform::FlipV:
				V = 1.0f - V;
				break;
			case EMT2MeshUVTransform::FlipUV:
				U = 1.0f - U;
				V = 1.0f - V;
				break;
			case EMT2MeshUVTransform::Swap:
				Swap(U, V);
				break;
			case EMT2MeshUVTransform::SwapFlipU:
				Swap(U, V);
				U = 1.0f - U;
				break;
			case EMT2MeshUVTransform::SwapFlipV:
				Swap(U, V);
				V = 1.0f - V;
				break;
			case EMT2MeshUVTransform::SwapFlipUV:
				Swap(U, V);
				U = 1.0f - U;
				V = 1.0f - V;
				break;
			case EMT2MeshUVTransform::Original:
			default:
				break;
			}
			TexCoords[Index] = U;
			TexCoords[Index + 1] = V;
		}
	}

	granny_texture* FindMaterialTextureRecursive(granny_material* Material, granny_material_texture_type Type, const TArray<const char*>& ChannelNames, TSet<const granny_material*>& VisitedMaterials)
	{
		if (!Material || VisitedMaterials.Contains(Material))
		{
			return nullptr;
		}

		VisitedMaterials.Add(Material);

		if (granny_texture* Texture = GrannyGetMaterialTextureByType(Material, Type))
		{
			return Texture;
		}

		for (const char* ChannelName : ChannelNames)
		{
			if (granny_texture* Texture = GrannyGetMaterialTextureByChannelName(Material, ChannelName))
			{
				return Texture;
			}
		}

		if (Material->Texture && Type == GrannyDiffuseColorTexture)
		{
			return Material->Texture;
		}

		for (granny_int32x MapIndex = 0; MapIndex < Material->MapCount; ++MapIndex)
		{
			if (Material->Maps && Material->Maps[MapIndex].Material)
			{
				if (granny_texture* Texture = FindMaterialTextureRecursive(Material->Maps[MapIndex].Material, Type, ChannelNames, VisitedMaterials))
				{
					return Texture;
				}
			}
		}

		return nullptr;
	}

	granny_texture* FindMaterialTexture(granny_material* Material, granny_material_texture_type Type, const TArray<const char*>& ChannelNames)
	{
		TSet<const granny_material*> VisitedMaterials;
		return FindMaterialTextureRecursive(Material, Type, ChannelNames, VisitedMaterials);
	}

	bool IsBlendMaterial(granny_material* Material)
	{
		return Material &&
			Material->MapCount > 1 &&
			Material->Name &&
			FCStringAnsi::Strnicmp(Material->Name, "Blend", 5) == 0;
	}

	bool ReadMaterialReal32ArrayRecursive(granny_material* Material, const char* FieldName, int32 ComponentCount, TArray<float>& OutValues, TSet<const granny_material*>& VisitedMaterials)
	{
		if (!Material || VisitedMaterials.Contains(Material))
		{
			return false;
		}
		VisitedMaterials.Add(Material);

		if (Material->ExtendedData.Type && Material->ExtendedData.Object)
		{
			granny_variant Result;
			if (GrannyFindMatchingMember(Material->ExtendedData.Type, Material->ExtendedData.Object, FieldName, &Result) && Result.Type)
			{
				granny_data_type_definition FieldType[] =
				{
					{ GrannyReal32Member, FieldName, nullptr, ComponentCount > 1 ? ComponentCount : 0 },
					{ GrannyEndMember }
				};
				OutValues.SetNumZeroed(ComponentCount);
				GrannyConvertSingleObject(Result.Type, Result.Object, FieldType, OutValues.GetData(), nullptr);
				return true;
			}
		}

		for (granny_int32x MapIndex = 0; MapIndex < Material->MapCount; ++MapIndex)
		{
			if (Material->Maps && Material->Maps[MapIndex].Material &&
				ReadMaterialReal32ArrayRecursive(Material->Maps[MapIndex].Material, FieldName, ComponentCount, OutValues, VisitedMaterials))
			{
				return true;
			}
		}

		return false;
	}

	bool ReadMaterialReal32Array(granny_material* Material, const char* FieldName, int32 ComponentCount, TArray<float>& OutValues)
	{
		TSet<const granny_material*> VisitedMaterials;
		return ReadMaterialReal32ArrayRecursive(Material, FieldName, ComponentCount, OutValues, VisitedMaterials);
	}

	bool ReadMaterialReal32(granny_material* Material, const char* FieldName, float& OutValue)
	{
		TArray<float> Values;
		if (!ReadMaterialReal32Array(Material, FieldName, 1, Values) || Values.Num() == 0)
		{
			return false;
		}
		OutValue = Values[0];
		return true;
	}

	bool ReadMaterialInt32Recursive(granny_material* Material, const char* FieldName, int32& OutValue, TSet<const granny_material*>& VisitedMaterials)
	{
		if (!Material || VisitedMaterials.Contains(Material))
		{
			return false;
		}
		VisitedMaterials.Add(Material);

		if (Material->ExtendedData.Type && Material->ExtendedData.Object)
		{
			granny_variant Result;
			if (GrannyFindMatchingMember(Material->ExtendedData.Type, Material->ExtendedData.Object, FieldName, &Result) && Result.Type)
			{
				granny_data_type_definition FieldType[] =
				{
					{ GrannyInt32Member, FieldName },
					{ GrannyEndMember }
				};
				granny_int32 Value = 0;
				GrannyConvertSingleObject(Result.Type, Result.Object, FieldType, &Value, nullptr);
				OutValue = static_cast<int32>(Value);
				return true;
			}
		}

		for (granny_int32x MapIndex = 0; MapIndex < Material->MapCount; ++MapIndex)
		{
			if (Material->Maps && Material->Maps[MapIndex].Material &&
				ReadMaterialInt32Recursive(Material->Maps[MapIndex].Material, FieldName, OutValue, VisitedMaterials))
			{
				return true;
			}
		}
		return false;
	}

	bool ReadMaterialInt32(granny_material* Material, const char* FieldName, int32& OutValue)
	{
		TSet<const granny_material*> VisitedMaterials;
		return ReadMaterialInt32Recursive(Material, FieldName, OutValue, VisitedMaterials);
	}

	bool ReadMaterialColor(granny_material* Material, const char* FieldName, FLinearColor& OutColor)
	{
		TArray<float> Values;
		if (!ReadMaterialReal32Array(Material, FieldName, 3, Values) || Values.Num() < 3)
		{
			return false;
		}
		OutColor = FLinearColor(Values[0], Values[1], Values[2], 1.0f);
		return true;
	}

	bool IsTwoSided(granny_material* Material)
	{
		int32 TwoSided = 0;
		return ReadMaterialInt32(Material, "Two-sided", TwoSided) && TwoSided == 1;
	}

	FMT2GrannyMaterialSlot BuildMaterialSlot(granny_material* Material, int32 Index)
	{
		FMT2GrannyMaterialSlot Slot;
		Slot.SlotName = ToObjName(Material ? Material->Name : nullptr, Index);
		Slot.ImportedSlotName = Slot.SlotName;
		Slot.bTwoSided = IsTwoSided(Material);
		Slot.bHasDiffuseColor = ReadMaterialColor(Material, "Diffuse Color", Slot.DiffuseColor);
		Slot.bHasSpecularColor = ReadMaterialColor(Material, "Specular Color", Slot.SpecularColor);
		Slot.bHasMaterialProperties = Slot.bHasDiffuseColor || Slot.bHasSpecularColor;

		float Transparency = 0.0f;
		Slot.bHasMaterialProperties |= ReadMaterialReal32(Material, "Opacity", Slot.Opacity);
		if (ReadMaterialReal32(Material, "Transparency", Transparency))
		{
			Slot.Opacity *= 1.0f - FMath::Clamp(Transparency, 0.0f, 1.0f);
			Slot.bHasMaterialProperties = true;
		}
		Slot.bHasMaterialProperties |= ReadMaterialReal32(Material, "Shininess", Slot.Shininess);
		Slot.bHasMaterialProperties |= ReadMaterialReal32(Material, "Shininess Strength", Slot.ShininessStrength);
		Slot.bHasMaterialProperties |= ReadMaterialReal32(Material, "Reflection Level", Slot.ReflectionLevel);
		Slot.bHasMaterialProperties |= ReadMaterialReal32(Material, "Index of Refraction", Slot.IndexOfRefraction);
		Slot.Opacity = FMath::Clamp(Slot.Opacity, 0.0f, 1.0f);

		granny_texture* AmbientTexture = nullptr;
		granny_texture* DiffuseTexture = nullptr;
		granny_texture* SpecularTexture = nullptr;
		granny_texture* OpacityTexture = nullptr;
		granny_texture* BumpTexture = nullptr;
		granny_texture* ReflectionTexture = nullptr;
		const TArray<const char*> AmbientChannelNames = { "Ambient Color", "Ambient" };
		const TArray<const char*> DiffuseChannelNames =
		{
			"Diffuse Color",
			"Diffuse",
			"Color",
			"Texture",
			"Base Color",
			"Map"
		};
		const TArray<const char*> OpacityChannelNames =
		{
			"Opacity",
			"Alpha",
			"Transparency"
		};
		const TArray<const char*> SpecularChannelNames = { "Specular Color", "Specular", "Gloss", "Shininess" };
		const TArray<const char*> BumpChannelNames = { "Bump", "Bump Map", "Normal", "Normal Map" };
		const TArray<const char*> ReflectionChannelNames = { "Reflection", "Environment", "Sphere Map" };
		if (IsBlendMaterial(Material))
		{
			DiffuseTexture = FindMaterialTexture(Material->Maps[0].Material, GrannyDiffuseColorTexture, DiffuseChannelNames);
			OpacityTexture = FindMaterialTexture(Material->Maps[1].Material, GrannyDiffuseColorTexture, DiffuseChannelNames);
		}
		else if (Material)
		{
			DiffuseTexture = FindMaterialTexture(Material, GrannyDiffuseColorTexture, DiffuseChannelNames);
			OpacityTexture = FindMaterialTexture(Material, GrannyOpacityTexture, OpacityChannelNames);
		}
		AmbientTexture = FindMaterialTexture(Material, GrannyAmbientColorTexture, AmbientChannelNames);
		SpecularTexture = FindMaterialTexture(Material, GrannySpecularColorTexture, SpecularChannelNames);
		BumpTexture = FindMaterialTexture(Material, GrannyBumpHeightTexture, BumpChannelNames);
		ReflectionTexture = FindMaterialTexture(Material, GrannyReflectionTexture, ReflectionChannelNames);

		Slot.AmbientTextureReference = TextureFileName(AmbientTexture);
		Slot.DiffuseTextureReference = TextureFileName(DiffuseTexture);
		Slot.SpecularTextureReference = TextureFileName(SpecularTexture);
		Slot.OpacityTextureReference = TextureFileName(OpacityTexture);
		Slot.BumpTextureReference = TextureFileName(BumpTexture);
		Slot.ReflectionTextureReference = TextureFileName(ReflectionTexture);
		return Slot;
	}

	int32 AddUniqueMaterialSlot(granny_material* Material, int32 Index, TArray<FMT2GrannyMaterialSlot>& OutMaterialSlots, TMap<const granny_material*, int32>& OutMaterialSlotIndexByPointer)
	{
		if (Material)
		{
			if (const int32* ExistingIndex = OutMaterialSlotIndexByPointer.Find(Material))
			{
				return *ExistingIndex;
			}
		}

		const int32 NewIndex = OutMaterialSlots.Num();
		OutMaterialSlots.Add(BuildMaterialSlot(Material, Index));
		if (Material)
		{
			OutMaterialSlotIndexByPointer.Add(Material, NewIndex);
		}
		return NewIndex;
	}

	void AppendMaterialLibrary(const FString& OutputObjPath, const TArray<FMT2GrannyMaterialSlot>& MaterialSlots)
	{
		IFileManager::Get().Delete(*FPaths::ChangeExtension(OutputObjPath, TEXT(".mtl")), false, true);
	}

	void BuildMaterialSlotsFromFileInfo(granny_file_info* Info, TArray<FMT2GrannyMaterialSlot>& OutMaterialSlots, TMap<const granny_material*, int32>* OutMaterialSlotIndexByPointer = nullptr)
	{
		OutMaterialSlots.Reset();
		TMap<const granny_material*, int32> LocalMaterialSlotIndexByPointer;
		bool bAddedMeshBindingMaterials = false;
		if (!Info)
		{
			return;
		}

		for (granny_int32x MeshIndex = 0; MeshIndex < Info->MeshCount; ++MeshIndex)
		{
			granny_mesh* Mesh = Info->Meshes[MeshIndex];
			if (!Mesh || Mesh->MaterialBindingCount <= 0 || !Mesh->MaterialBindings)
			{
				continue;
			}

			for (granny_int32x MaterialIndex = 0; MaterialIndex < Mesh->MaterialBindingCount; ++MaterialIndex)
			{
				AddUniqueMaterialSlot(Mesh->MaterialBindings[MaterialIndex].Material, MaterialIndex, OutMaterialSlots, LocalMaterialSlotIndexByPointer);
				bAddedMeshBindingMaterials = true;
			}
		}

		if (!bAddedMeshBindingMaterials)
		{
			for (granny_int32x MaterialIndex = 0; MaterialIndex < Info->MaterialCount; ++MaterialIndex)
			{
				AddUniqueMaterialSlot(Info->Materials[MaterialIndex], MaterialIndex, OutMaterialSlots, LocalMaterialSlotIndexByPointer);
			}
		}

		if (OutMaterialSlotIndexByPointer)
		{
			*OutMaterialSlotIndexByPointer = MoveTemp(LocalMaterialSlotIndexByPointer);
		}
	}

	void FillInspection(granny_file_info* Info, FMT2GrannyFileInspection& OutInspection)
	{
		OutInspection = FMT2GrannyFileInspection();
		if (!Info)
		{
			return;
		}

		OutInspection.MeshCount = Info->MeshCount;
		OutInspection.SkeletonCount = Info->SkeletonCount;
		OutInspection.AnimationCount = Info->AnimationCount;
		OutInspection.TrackGroupCount = Info->TrackGroupCount;

		for (granny_int32x SkeletonIndex = 0; SkeletonIndex < Info->SkeletonCount; ++SkeletonIndex)
		{
			if (Info->Skeletons && Info->Skeletons[SkeletonIndex])
			{
				OutInspection.BoneCount = FMath::Max(OutInspection.BoneCount, static_cast<int32>(Info->Skeletons[SkeletonIndex]->BoneCount));
			}
		}
		for (granny_int32x ModelIndex = 0; ModelIndex < Info->ModelCount; ++ModelIndex)
		{
			if (Info->Models && Info->Models[ModelIndex] && Info->Models[ModelIndex]->Skeleton)
			{
				OutInspection.BoneCount = FMath::Max(OutInspection.BoneCount, static_cast<int32>(Info->Models[ModelIndex]->Skeleton->BoneCount));
			}
		}

		for (granny_int32x MeshIndex = 0; MeshIndex < Info->MeshCount; ++MeshIndex)
		{
			if (Info->Meshes[MeshIndex])
			{
				OutInspection.BoneBindingCount += Info->Meshes[MeshIndex]->BoneBindingCount;
				OutInspection.MaxBoneBindingCount = FMath::Max(OutInspection.MaxBoneBindingCount, static_cast<int32>(Info->Meshes[MeshIndex]->BoneBindingCount));
			}
		}

		const bool bHasMesh = OutInspection.MeshCount > 0;
		const int32 EffectiveBoneCount = OutInspection.BoneCount > 0 ? OutInspection.BoneCount : OutInspection.MaxBoneBindingCount;
		const bool bRequiresSkinning = EffectiveBoneCount > 1;
		const bool bHasAnimation = OutInspection.AnimationCount > 0 || OutInspection.TrackGroupCount > 0;
		if (bHasMesh && bRequiresSkinning)
		{
			OutInspection.Type = EMT2GrannyFileType::SkeletalMesh;
		}
		else if (bHasAnimation)
		{
			OutInspection.Type = EMT2GrannyFileType::Animation;
		}
		else if (bHasMesh)
		{
			OutInspection.Type = EMT2GrannyFileType::StaticMesh;
		}
	}
}

bool FMT2GrannyMeshConverter::InspectGrannyFile(const FString& InputGrannyPath, FMT2GrannyFileInspection& OutInspection, FString& OutError)
{
	OutInspection = FMT2GrannyFileInspection();
	if (!EnsureGrannyDllLoaded(OutError))
	{
		return false;
	}

	granny_file* GrannyFile = GrannyReadEntireFile(TCHAR_TO_UTF8(*InputGrannyPath));
	if (!GrannyFile)
	{
		OutError = FString::Printf(TEXT("Failed to read Granny file: %s"), *InputGrannyPath);
		return false;
	}

	granny_file_info* Info = GrannyGetFileInfo(GrannyFile);
	if (!Info)
	{
		OutError = FString::Printf(TEXT("Failed to read Granny file info: %s"), *InputGrannyPath);
		GrannyFreeFile(GrannyFile);
		return false;
	}

	FillInspection(Info, OutInspection);
	GrannyFreeFile(GrannyFile);
	return true;
}

bool FMT2GrannyMeshConverter::ExtractAnimationClip(const FString& InputGrannyPath, FMT2GrannyAnimationClip& OutClip, FString& OutError)
{
	OutClip = FMT2GrannyAnimationClip();
	if (!EnsureGrannyDllLoaded(OutError))
	{
		return false;
	}

	granny_file* GrannyFile = GrannyReadEntireFile(TCHAR_TO_UTF8(*InputGrannyPath));
	if (!GrannyFile)
	{
		OutError = FString::Printf(TEXT("Failed to read Granny animation: %s"), *InputGrannyPath);
		return false;
	}

	granny_file_info* Info = GrannyGetFileInfo(GrannyFile);
	if (!Info)
	{
		OutError = FString::Printf(TEXT("Failed to read Granny animation info: %s"), *InputGrannyPath);
		GrannyFreeFile(GrannyFile);
		return false;
	}

	granny_animation* Animation = nullptr;
	for (granny_int32x AnimationIndex = 0; AnimationIndex < Info->AnimationCount; ++AnimationIndex)
	{
		if (Info->Animations && Info->Animations[AnimationIndex])
		{
			Animation = Info->Animations[AnimationIndex];
			break;
		}
	}

	granny_track_group* TrackGroup = nullptr;
	if (Animation)
	{
		for (granny_int32x GroupIndex = 0; GroupIndex < Animation->TrackGroupCount; ++GroupIndex)
		{
			granny_track_group* Candidate = Animation->TrackGroups ? Animation->TrackGroups[GroupIndex] : nullptr;
			if (Candidate && (!TrackGroup || Candidate->TransformTrackCount > TrackGroup->TransformTrackCount))
			{
				TrackGroup = Candidate;
			}
		}
	}
	if (!TrackGroup)
	{
		for (granny_int32x GroupIndex = 0; GroupIndex < Info->TrackGroupCount; ++GroupIndex)
		{
			granny_track_group* Candidate = Info->TrackGroups ? Info->TrackGroups[GroupIndex] : nullptr;
			if (Candidate && (!TrackGroup || Candidate->TransformTrackCount > TrackGroup->TransformTrackCount))
			{
				TrackGroup = Candidate;
			}
		}
	}

	if (!TrackGroup || TrackGroup->TransformTrackCount <= 0 || !TrackGroup->TransformTracks)
	{
		OutError = FString::Printf(TEXT("No transform animation tracks found in: %s"), *InputGrannyPath);
		GrannyFreeFile(GrannyFile);
		return false;
	}

	float Duration = Animation ? Animation->Duration : 0.0f;
	if (!FMath::IsFinite(Duration) || Duration <= KINDA_SMALL_NUMBER)
	{
		for (granny_int32x TrackIndex = 0; TrackIndex < TrackGroup->TransformTrackCount; ++TrackIndex)
		{
			const granny_transform_track& Track = TrackGroup->TransformTracks[TrackIndex];
			const granny_curve2* Curves[] = { &Track.OrientationCurve, &Track.PositionCurve, &Track.ScaleShearCurve };
			const granny_real32 Identity4[] = { 0.0f, 0.0f, 0.0f, 1.0f };
			const granny_real32 Identity3[] = { 0.0f, 0.0f, 0.0f };
			const granny_real32 Identity9[] = { 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f };
			const granny_real32* Identities[] = { Identity4, Identity3, Identity9 };
			for (int32 CurveIndex = 0; CurveIndex < 3; ++CurveIndex)
			{
				const granny_int32x KnotCount = GrannyCurveGetKnotCount(Curves[CurveIndex]);
				if (KnotCount > 0)
				{
					granny_real32 Control[9] = {};
					const granny_real32 LastKnot = GrannyCurveExtractKnotValue(Curves[CurveIndex], KnotCount - 1, Control, Identities[CurveIndex]);
					if (FMath::IsFinite(LastKnot))
					{
						Duration = FMath::Max(Duration, LastKnot);
					}
				}
			}
		}
	}
	if (!FMath::IsFinite(Duration) || Duration <= KINDA_SMALL_NUMBER)
	{
		OutError = FString::Printf(TEXT("Animation has no usable duration: %s"), *InputGrannyPath);
		GrannyFreeFile(GrannyFile);
		return false;
	}

	const float SourceTimeStep = Animation ? Animation->TimeStep : 0.0f;
	const int32 FrameRate = FMath::Clamp(
		FMath::RoundToInt(SourceTimeStep > SMALL_NUMBER ? 1.0f / SourceTimeStep : 30.0f),
		1,
		240);
	const int32 NumberOfFrames = FMath::Max(1, FMath::RoundToInt(Duration * static_cast<float>(FrameRate)));

	OutClip.Name = Animation && Animation->Name ? UTF8_TO_TCHAR(Animation->Name) : FPaths::GetBaseFilename(InputGrannyPath);
	OutClip.TrackGroupName = TrackGroup->Name ? UTF8_TO_TCHAR(TrackGroup->Name) : FString();
	OutClip.Duration = Duration;
	OutClip.FrameRate = FrameRate;
	OutClip.NumberOfFrames = NumberOfFrames;
	OutClip.Tracks.Reserve(TrackGroup->TransformTrackCount);

	const granny_real32 IdentityPosition[] = { 0.0f, 0.0f, 0.0f };
	const granny_real32 IdentityOrientation[] = { 0.0f, 0.0f, 0.0f, 1.0f };
	const granny_real32 IdentityScaleShear[] = { 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f };
	TSet<FString> AddedTrackNames;

	for (granny_int32x TrackIndex = 0; TrackIndex < TrackGroup->TransformTrackCount; ++TrackIndex)
	{
		const granny_transform_track& SourceTrack = TrackGroup->TransformTracks[TrackIndex];
		const FString RawTrackName = SourceTrack.Name ? UTF8_TO_TCHAR(SourceTrack.Name) : FString();
		const FString BoneName = FMT2AssetScanner::SanitizePackagePathSegment(RawTrackName);
		if (RawTrackName.IsEmpty() || AddedTrackNames.Contains(BoneName))
		{
			continue;
		}

		FMT2GrannyAnimationTrack& OutTrack = OutClip.Tracks.AddDefaulted_GetRef();
		OutTrack.BoneName = BoneName;
		OutTrack.TranslationKeys.Reserve(NumberOfFrames + 1);
		OutTrack.RotationKeys.Reserve(NumberOfFrames + 1);
		OutTrack.ScaleKeys.Reserve(NumberOfFrames + 1);
		AddedTrackNames.Add(BoneName);

		FQuat4f PreviousRotation = FQuat4f::Identity;
		for (int32 FrameIndex = 0; FrameIndex <= NumberOfFrames; ++FrameIndex)
		{
			const float SampleTime = Duration * static_cast<float>(FrameIndex) / static_cast<float>(NumberOfFrames);
			granny_real32 Position[3];
			granny_real32 Orientation[4];
			granny_real32 ScaleShear[9];
			GrannyEvaluateCurveAtT(3, false, false, &SourceTrack.PositionCurve, false, Duration, SampleTime, Position, IdentityPosition);
			GrannyEvaluateCurveAtT(4, true, false, &SourceTrack.OrientationCurve, false, Duration, SampleTime, Orientation, IdentityOrientation);
			GrannyEvaluateCurveAtT(9, false, false, &SourceTrack.ScaleShearCurve, false, Duration, SampleTime, ScaleShear, IdentityScaleShear);

			granny_transform SampledTransform = {};
			SampledTransform.Flags = GrannyHasPosition | GrannyHasOrientation | GrannyHasScaleShear;
			FMemory::Memcpy(SampledTransform.Position, Position, sizeof(Position));
			FMemory::Memcpy(SampledTransform.Orientation, Orientation, sizeof(Orientation));
			FMemory::Memcpy(SampledTransform.ScaleShear, ScaleShear, sizeof(ScaleShear));
			granny_real32 SampledMatrix[16];
			GrannyBuildCompositeTransform4x4(&SampledTransform, SampledMatrix);
			FTransform ConvertedSample;
			ConvertedSample.SetFromMatrix(ConvertGrannyMatrixToUnreal(SampledMatrix));
			ConvertedSample.NormalizeRotation();

			FVector3f Translation(ConvertedSample.GetTranslation());
			if (!FMath::IsFinite(Translation.X) || !FMath::IsFinite(Translation.Y) || !FMath::IsFinite(Translation.Z))
			{
				Translation = FVector3f::ZeroVector;
			}

			FQuat4f Rotation(ConvertedSample.GetRotation());
			if (!Rotation.IsNormalized())
			{
				Rotation.Normalize();
			}
			if (!Rotation.IsNormalized())
			{
				Rotation = FQuat4f::Identity;
			}
			if (FrameIndex > 0 && (PreviousRotation | Rotation) < 0.0f)
			{
				Rotation.X = -Rotation.X;
				Rotation.Y = -Rotation.Y;
				Rotation.Z = -Rotation.Z;
				Rotation.W = -Rotation.W;
			}
			PreviousRotation = Rotation;

			FVector3f Scale(ConvertedSample.GetScale3D());
			if (!FMath::IsFinite(Scale.X) || !FMath::IsFinite(Scale.Y) || !FMath::IsFinite(Scale.Z))
			{
				Scale = FVector3f::OneVector;
			}

			OutTrack.TranslationKeys.Add(Translation);
			OutTrack.RotationKeys.Add(Rotation);
			OutTrack.ScaleKeys.Add(Scale);
		}
	}

	GrannyFreeFile(GrannyFile);
	if (OutClip.Tracks.Num() == 0)
	{
		OutError = FString::Printf(TEXT("No usable transform animation tracks found in: %s"), *InputGrannyPath);
		return false;
	}
	return true;
}

bool FMT2GrannyMeshConverter::ExtractAnimationClipForModel(
	const FString& InputGrannyPath,
	const FString& InputModelGrannyPath,
	FMT2GrannyAnimationClip& OutClip,
	FString& OutError,
	const FReferenceSkeleton* TargetReferenceSkeleton)
{
	FMT2GrannyAnimationClip RawClip;
	if (!ExtractAnimationClip(InputGrannyPath, RawClip, OutError))
	{
		return false;
	}

	granny_file* AnimationFile = GrannyReadEntireFile(TCHAR_TO_UTF8(*InputGrannyPath));
	granny_file* ModelFile = GrannyReadEntireFile(TCHAR_TO_UTF8(*InputModelGrannyPath));
	if (!AnimationFile || !ModelFile)
	{
		OutError = FString::Printf(TEXT("Failed to read animation/model pair: %s | %s"), *InputGrannyPath, *InputModelGrannyPath);
		if (AnimationFile)
		{
			GrannyFreeFile(AnimationFile);
		}
		if (ModelFile)
		{
			GrannyFreeFile(ModelFile);
		}
		return false;
	}

	granny_file_info* AnimationInfo = GrannyGetFileInfo(AnimationFile);
	granny_file_info* ModelInfo = GrannyGetFileInfo(ModelFile);
	granny_animation* Animation = nullptr;
	if (AnimationInfo)
	{
		for (granny_int32x AnimationIndex = 0; AnimationIndex < AnimationInfo->AnimationCount; ++AnimationIndex)
		{
			if (AnimationInfo->Animations && AnimationInfo->Animations[AnimationIndex])
			{
				Animation = AnimationInfo->Animations[AnimationIndex];
				break;
			}
		}
	}

	TSet<FString> AnimationBoneNames;
	for (const FMT2GrannyAnimationTrack& Track : RawClip.Tracks)
	{
		AnimationBoneNames.Add(Track.BoneName);
	}

	granny_model* Model = nullptr;
	int32 BestMatchedBoneCount = INDEX_NONE;
	if (ModelInfo)
	{
		for (granny_int32x ModelIndex = 0; ModelIndex < ModelInfo->ModelCount; ++ModelIndex)
		{
			granny_model* Candidate = ModelInfo->Models ? ModelInfo->Models[ModelIndex] : nullptr;
			if (!Candidate || !Candidate->Skeleton)
			{
				continue;
			}

			int32 MatchedBoneCount = 0;
			for (granny_int32x BoneIndex = 0; BoneIndex < Candidate->Skeleton->BoneCount; ++BoneIndex)
			{
				const char* BoneName = Candidate->Skeleton->Bones[BoneIndex].Name;
				if (BoneName && AnimationBoneNames.Contains(FMT2AssetScanner::SanitizePackagePathSegment(UTF8_TO_TCHAR(BoneName))))
				{
					MatchedBoneCount++;
				}
			}
			if (MatchedBoneCount > BestMatchedBoneCount)
			{
				BestMatchedBoneCount = MatchedBoneCount;
				Model = Candidate;
			}
		}
	}

	if (!Animation || !Model || !Model->Skeleton || BestMatchedBoneCount <= 0)
	{
		OutError = FString::Printf(TEXT("Animation could not be bound to model: %s | %s"), *InputGrannyPath, *InputModelGrannyPath);
		GrannyFreeFile(ModelFile);
		GrannyFreeFile(AnimationFile);
		return false;
	}

	granny_model_instance* ModelInstance = GrannyInstantiateModel(Model);
	granny_control* Control = ModelInstance ? GrannyPlayControlledAnimation(0.0f, Animation, ModelInstance) : nullptr;
	granny_local_pose* LocalPose = ModelInstance ? GrannyNewLocalPose(Model->Skeleton->BoneCount) : nullptr;
	granny_world_pose* WorldPose = ModelInstance ? GrannyNewWorldPose(Model->Skeleton->BoneCount) : nullptr;
	if (!ModelInstance || !Control || !LocalPose || !WorldPose)
	{
		OutError = FString::Printf(TEXT("Failed to create Granny model-bound animation sampler: %s | %s"), *InputGrannyPath, *InputModelGrannyPath);
		if (Control)
		{
			GrannyFreeControl(Control);
		}
		if (LocalPose)
		{
			GrannyFreeLocalPose(LocalPose);
		}
		if (WorldPose)
		{
			GrannyFreeWorldPose(WorldPose);
		}
		if (ModelInstance)
		{
			GrannyFreeModelInstance(ModelInstance);
		}
		GrannyFreeFile(ModelFile);
		GrannyFreeFile(AnimationFile);
		return false;
	}

	GrannySetControlWeight(Control, 1.0f);
	GrannySetControlEaseIn(Control, false);
	GrannySetControlEaseOut(Control, false);
	GrannySetControlLoopCount(Control, 1);
	GrannySetControlForceClampedLooping(Control, true);

	TArray<FMT2GrannyAnimationTrack> BoundTracks;
	TArray<int32> TrackIndexByBone;
	TrackIndexByBone.Init(INDEX_NONE, Model->Skeleton->BoneCount);
	TSet<FString> AddedBoneNames;
	for (granny_int32x BoneIndex = 0; BoneIndex < Model->Skeleton->BoneCount; ++BoneIndex)
	{
		const char* RawBoneName = Model->Skeleton->Bones[BoneIndex].Name;
		if (!RawBoneName || RawBoneName[0] == '\0')
		{
			continue;
		}

		const FString BoneName = FMT2AssetScanner::SanitizePackagePathSegment(UTF8_TO_TCHAR(RawBoneName));
		if (!AnimationBoneNames.Contains(BoneName))
		{
			continue;
		}
		const int32 TargetBoneIndex = TargetReferenceSkeleton
			? TargetReferenceSkeleton->FindBoneIndex(FName(*BoneName))
			: BoneIndex;
		if (TargetBoneIndex == INDEX_NONE || AddedBoneNames.Contains(BoneName))
		{
			continue;
		}

		FMT2GrannyAnimationTrack& Track = BoundTracks.AddDefaulted_GetRef();
		Track.BoneName = BoneName;
		Track.TranslationKeys.Reserve(RawClip.NumberOfFrames + 1);
		Track.RotationKeys.Reserve(RawClip.NumberOfFrames + 1);
		Track.ScaleKeys.Reserve(RawClip.NumberOfFrames + 1);
		TrackIndexByBone[BoneIndex] = BoundTracks.Num() - 1;
		AddedBoneNames.Add(BoneName);
	}

	bool bSampleSucceeded = true;
	for (int32 FrameIndex = 0; FrameIndex <= RawClip.NumberOfFrames; ++FrameIndex)
	{
		const float SampleTime = RawClip.Duration * static_cast<float>(FrameIndex) / static_cast<float>(RawClip.NumberOfFrames);
		GrannySetModelClock(ModelInstance, SampleTime);
		if (!GrannySampleSingleModelAnimation(ModelInstance, Control, 0, Model->Skeleton->BoneCount, LocalPose))
		{
			bSampleSucceeded = false;
			break;
		}
		GrannyBuildWorldPose(
			Model->Skeleton, 0, Model->Skeleton->BoneCount, LocalPose, nullptr, WorldPose);
		TArray<FTransform> ReconstructedComponents;
		ReconstructedComponents.SetNum(Model->Skeleton->BoneCount);
		for (granny_int32x BoneIndex = 0; BoneIndex < Model->Skeleton->BoneCount; ++BoneIndex)
		{
			const int32 TrackIndex = TrackIndexByBone[BoneIndex];
			granny_transform* SampledLocalTransform = GrannyGetLocalPoseTransform(LocalPose, BoneIndex);
			const granny_real32* SampledWorldMatrix = GrannyGetWorldPose4x4(WorldPose, BoneIndex);
			if (!SampledLocalTransform || !SampledWorldMatrix)
			{
				continue;
			}

			FTransform LocalTransform(
				FQuat(
					SampledLocalTransform->Orientation[0],
					SampledLocalTransform->Orientation[1],
					SampledLocalTransform->Orientation[2],
					SampledLocalTransform->Orientation[3]),
				FVector(
					SampledLocalTransform->Position[0],
					SampledLocalTransform->Position[1],
					SampledLocalTransform->Position[2]),
				FVector(
					SampledLocalTransform->ScaleShear[0][0],
					SampledLocalTransform->ScaleShear[1][1],
					SampledLocalTransform->ScaleShear[2][2]));
			LocalTransform.NormalizeRotation();
			const FVector DesiredWorldPosition(
				SampledWorldMatrix[12], SampledWorldMatrix[13], SampledWorldMatrix[14]);
			const int32 ParentIndex = Model->Skeleton->Bones[BoneIndex].ParentIndex;
			if (ParentIndex >= 0)
			{
				LocalTransform.SetTranslation(
					ReconstructedComponents[ParentIndex].InverseTransformPosition(DesiredWorldPosition));
			}
			else
			{
				LocalTransform.SetTranslation(DesiredWorldPosition);
			}
			ReconstructedComponents[BoneIndex] = ParentIndex >= 0
				? LocalTransform * ReconstructedComponents[ParentIndex]
				: LocalTransform;
			if (!BoundTracks.IsValidIndex(TrackIndex))
			{
				continue;
			}

			FMT2GrannyAnimationTrack& Track = BoundTracks[TrackIndex];

			// The skeleton's reference pose was converted to Unreal by mirroring source X, so the
			// animated locals must go through the same conjugation or every track plays mirrored
			// against the bind pose. ReconstructedComponents stays in Granny space; conjugation
			// distributes over the parent chain, so converting the final local is equivalent.
			const FTransform UnrealLocalTransform = ConvertGrannyLocalTransformToUnreal(LocalTransform);
			FVector3f Translation(UnrealLocalTransform.GetTranslation());
			FQuat4f Rotation(UnrealLocalTransform.GetRotation());
			if (!Rotation.IsNormalized())
			{
				Rotation.Normalize();
			}
			if (!Rotation.IsNormalized())
			{
				Rotation = FQuat4f::Identity;
			}
			if (Track.RotationKeys.Num() > 0 && (Track.RotationKeys.Last() | Rotation) < 0.0f)
			{
				Rotation.X = -Rotation.X;
				Rotation.Y = -Rotation.Y;
				Rotation.Z = -Rotation.Z;
				Rotation.W = -Rotation.W;
			}

			FVector3f Scale(LocalTransform.GetScale3D());
			if (!FMath::IsFinite(Scale.X) || !FMath::IsFinite(Scale.Y) || !FMath::IsFinite(Scale.Z))
			{
				Scale = FVector3f::OneVector;
			}

			Track.TranslationKeys.Add(Translation);
			Track.RotationKeys.Add(Rotation);
			Track.ScaleKeys.Add(Scale);
		}
	}

	GrannyFreeControl(Control);
	GrannyFreeLocalPose(LocalPose);
	GrannyFreeWorldPose(WorldPose);
	GrannyFreeModelInstance(ModelInstance);
	GrannyFreeFile(ModelFile);
	GrannyFreeFile(AnimationFile);

	if (!bSampleSucceeded)
	{
		OutError = FString::Printf(TEXT("Granny failed to sample bound animation pose: %s | %s"), *InputGrannyPath, *InputModelGrannyPath);
		return false;
	}

	OutClip = MoveTemp(RawClip);
	OutClip.Tracks = MoveTemp(BoundTracks);
	return OutClip.Tracks.Num() > 0;
}

bool FMT2GrannyMeshConverter::ExtractMaterialSlots(const FString& InputGrannyPath, TArray<FMT2GrannyMaterialSlot>& OutMaterialSlots, FString& OutError)
{
	OutMaterialSlots.Reset();
	if (!EnsureGrannyDllLoaded(OutError))
	{
		return false;
	}

	granny_file* GrannyFile = GrannyReadEntireFile(TCHAR_TO_UTF8(*InputGrannyPath));
	if (!GrannyFile)
	{
		OutError = FString::Printf(TEXT("Failed to read Granny file: %s"), *InputGrannyPath);
		return false;
	}

	granny_file_info* Info = GrannyGetFileInfo(GrannyFile);
	if (!Info)
	{
		OutError = FString::Printf(TEXT("Failed to read Granny file info: %s"), *InputGrannyPath);
		GrannyFreeFile(GrannyFile);
		return false;
	}

	BuildMaterialSlotsFromFileInfo(Info, OutMaterialSlots);
	GrannyFreeFile(GrannyFile);
	return true;
}

bool FMT2GrannyMeshConverter::ConvertGrannyToObj(const FString& InputGrannyPath, const FString& OutputObjPath, FString& OutError, TArray<FMT2GrannyMaterialSlot>* OutMaterialSlots)
{
	if (!EnsureGrannyDllLoaded(OutError))
	{
		return false;
	}

	if (!IFileManager::Get().FileExists(*InputGrannyPath))
	{
		OutError = FString::Printf(TEXT("Missing GR2 file: %s"), *InputGrannyPath);
		return false;
	}

	granny_file* GrannyFile = GrannyReadEntireFile(TCHAR_TO_UTF8(*InputGrannyPath));
	if (!GrannyFile)
	{
		OutError = FString::Printf(TEXT("Failed to read Granny file: %s"), *InputGrannyPath);
		return false;
	}

	granny_file_info* Info = GrannyGetFileInfo(GrannyFile);
	if (!Info)
	{
		OutError = FString::Printf(TEXT("Failed to read Granny file info: %s"), *InputGrannyPath);
		GrannyFreeFile(GrannyFile);
		return false;
	}

	if (Info->MeshCount <= 0)
	{
		OutError = FString::Printf(TEXT("No meshes found in: %s"), *InputGrannyPath);
		GrannyFreeFile(GrannyFile);
		return false;
	}

	TArray<FMT2GrannyMaterialSlot> MaterialSlots;
	TMap<const granny_material*, int32> MaterialSlotIndexByPointer;
	BuildMaterialSlotsFromFileInfo(Info, MaterialSlots, &MaterialSlotIndexByPointer);

	FString Obj;
	Obj += TEXT("# Generated by MT2UE embedded Granny converter\n");
	Obj += FString::Printf(TEXT("# %s\n"), StaticMeshConversionVersion);
	Obj += FString::Printf(TEXT("# Source: %s\n\n"), *InputGrannyPath);

	int32 VertexOffset = 0;
	int32 TexCoordOffset = 0;
	int32 NormalOffset = 0;
	int32 MeshesWritten = 0;

	for (granny_int32x MeshIndex = 0; MeshIndex < Info->MeshCount; ++MeshIndex)
	{
		granny_mesh* Mesh = Info->Meshes[MeshIndex];
		if (!Mesh)
		{
			continue;
		}

		TArray<float> Positions;
		TArray<float> Normals;
		TArray<float> TexCoords;
		CopyVertexFloats(Mesh, GrannyVertexPositionName, 3, Positions);
		CopyVertexFloats(Mesh, GrannyVertexNormalName, 3, Normals);
		CopyVertexFloats(Mesh, GrannyVertexTextureCoordinatesName "0", 2, TexCoords);
		ApplyInverseModelInitialPlacement(Info, Mesh, Positions, Normals);
		ConvertWeaponToSkeletalAttachmentBasis(InputGrannyPath, Positions, Normals);

		if (Positions.Num() == 0)
		{
			continue;
		}

		Obj += FString::Printf(TEXT("g %s\n"), *ToObjName(Mesh->Name, MeshIndex));

		for (int32 Index = 0; Index + 2 < Positions.Num(); Index += 3)
		{
			Obj += FString::Printf(TEXT("v %.9g %.9g %.9g\n"), Positions[Index], Positions[Index + 1], Positions[Index + 2]);
		}
		for (int32 Index = 0; Index + 2 < Normals.Num(); Index += 3)
		{
			Obj += FString::Printf(TEXT("vn %.9g %.9g %.9g\n"), Normals[Index], Normals[Index + 1], Normals[Index + 2]);
		}
		for (int32 Index = 0; Index + 1 < TexCoords.Num(); Index += 2)
		{
			Obj += FString::Printf(TEXT("vt %.9g %.9g\n"), TexCoords[Index], 1.0f - TexCoords[Index + 1]);
		}

		TArray<granny_int32> Indices;
		Indices.SetNumZeroed(GrannyGetMeshIndexCount(Mesh));
		if (Indices.Num() > 0)
		{
			GrannyCopyMeshIndices(Mesh, 4, Indices.GetData());
		}

		auto AppendTriangle = [&Obj, &Indices, VertexOffset, TexCoordOffset, NormalOffset, &TexCoords, &Normals](int32 Index)
		{
			const int32 V0 = Indices[Index + 0];
			const int32 V1 = Indices[Index + 1];
			const int32 V2 = Indices[Index + 2];
			if (V0 == V1 || V0 == V2 || V1 == V2)
			{
				return;
			}

			Obj += TEXT("f ");
			for (int32 Corner = 0; Corner < 3; ++Corner)
			{
				const int32 LocalIndex = Indices[Index + Corner] + 1;
				const int32 VertexIndex = VertexOffset + LocalIndex;
				Obj += FString::FromInt(VertexIndex);
				Obj += TEXT("/");
				if (TexCoords.Num() > 0)
				{
					Obj += FString::FromInt(TexCoordOffset + LocalIndex);
				}
				Obj += TEXT("/");
				if (Normals.Num() > 0)
				{
					Obj += FString::FromInt(NormalOffset + LocalIndex);
				}
				Obj += Corner == 2 ? TEXT("\n") : TEXT(" ");
			}
		};

		const granny_tri_topology* Topology = Mesh->PrimaryTopology;
		if (Topology && Topology->GroupCount > 0 && Topology->Groups)
		{
			for (granny_int32 GroupIndex = 0; GroupIndex < Topology->GroupCount; ++GroupIndex)
			{
				const granny_tri_material_group& Group = Topology->Groups[GroupIndex];
				if (Mesh->MaterialBindings && Group.MaterialIndex >= 0 && Group.MaterialIndex < Mesh->MaterialBindingCount)
				{
					granny_material* Material = Mesh->MaterialBindings[Group.MaterialIndex].Material;
					const int32 GlobalMaterialIndex = MaterialSlots.IndexOfByPredicate([Material](const FMT2GrannyMaterialSlot& Slot)
					{
						return Material && Slot.SlotName == ToObjName(Material->Name, 0);
					});
					Obj += FString::Printf(TEXT("usemtl %s\n"), GlobalMaterialIndex != INDEX_NONE ? *MaterialSlots[GlobalMaterialIndex].SlotName : *ToObjName(Material ? Material->Name : nullptr, Group.MaterialIndex));
				}

				const int32 FirstIndex = FMath::Max(0, Group.TriFirst * 3);
				const int32 EndIndex = FMath::Min(Indices.Num(), (Group.TriFirst + Group.TriCount) * 3);
				for (int32 Index = FirstIndex; Index + 2 < EndIndex; Index += 3)
				{
					AppendTriangle(Index);
				}
			}
		}
		else
		{
			if (MaterialSlots.Num() > 0)
			{
				Obj += FString::Printf(TEXT("usemtl %s\n"), *MaterialSlots[0].SlotName);
			}
			for (int32 Index = 0; Index + 2 < Indices.Num(); Index += 3)
			{
				AppendTriangle(Index);
			}
		}

		Obj += TEXT("\n");
		VertexOffset += Positions.Num() / 3;
		TexCoordOffset += TexCoords.Num() / 2;
		NormalOffset += Normals.Num() / 3;
		MeshesWritten++;
	}

	GrannyFreeFile(GrannyFile);

	if (MeshesWritten == 0)
	{
		OutError = FString::Printf(TEXT("No exportable meshes found in: %s"), *InputGrannyPath);
		return false;
	}

	FPlatformFileManager::Get().GetPlatformFile().CreateDirectoryTree(*FPaths::GetPath(OutputObjPath));
	if (!FFileHelper::SaveStringToFile(Obj, *OutputObjPath))
	{
		OutError = FString::Printf(TEXT("Failed to write OBJ: %s"), *OutputObjPath);
		return false;
	}

	AppendMaterialLibrary(OutputObjPath, MaterialSlots);
	if (OutMaterialSlots)
	{
		*OutMaterialSlots = MoveTemp(MaterialSlots);
	}

	return true;
}

bool FMT2GrannyMeshConverter::ConvertGrannyToGltf(
	const FString& InputGrannyPath,
	const FString& OutputGltfPath,
	FString& OutError,
	TArray<FMT2GrannyMaterialSlot>* OutMaterialSlots,
	EMT2MeshUVTransform UVTransform,
	const FString& ReferenceSkeletonGrannyPath)
{
	if (!EnsureGrannyDllLoaded(OutError))
	{
		return false;
	}

	granny_file* GrannyFile = GrannyReadEntireFile(TCHAR_TO_UTF8(*InputGrannyPath));
	if (!GrannyFile)
	{
		OutError = FString::Printf(TEXT("Failed to read Granny file: %s"), *InputGrannyPath);
		return false;
	}

	granny_file_info* Info = GrannyGetFileInfo(GrannyFile);
	if (!Info)
	{
		OutError = FString::Printf(TEXT("Failed to read Granny file info: %s"), *InputGrannyPath);
		GrannyFreeFile(GrannyFile);
		return false;
	}

	granny_skeleton* Skeleton = nullptr;
	if (Info->ModelCount > 0 && Info->Models && Info->Models[0] && Info->Models[0]->Skeleton)
	{
		Skeleton = Info->Models[0]->Skeleton;
	}
	else if (Info->SkeletonCount > 0 && Info->Skeletons)
	{
		Skeleton = Info->Skeletons[0];
	}

	if (!Skeleton || Skeleton->BoneCount <= 0)
	{
		OutError = FString::Printf(TEXT("No skeleton found in: %s"), *InputGrannyPath);
		GrannyFreeFile(GrannyFile);
		return false;
	}

	if (Info->MeshCount <= 0)
	{
		OutError = FString::Printf(TEXT("No meshes found in: %s"), *InputGrannyPath);
		GrannyFreeFile(GrannyFile);
		return false;
	}

	granny_file* ReferenceSkeletonFile = nullptr;
	if (!ReferenceSkeletonGrannyPath.IsEmpty() &&
		!FPaths::IsSamePath(InputGrannyPath, ReferenceSkeletonGrannyPath))
	{
		ReferenceSkeletonFile = GrannyReadEntireFile(TCHAR_TO_UTF8(*ReferenceSkeletonGrannyPath));
		granny_file_info* ReferenceInfo = ReferenceSkeletonFile
			? GrannyGetFileInfo(ReferenceSkeletonFile)
			: nullptr;
		granny_skeleton* ReferenceSkeleton = nullptr;
		if (ReferenceInfo && ReferenceInfo->ModelCount > 0 && ReferenceInfo->Models &&
			ReferenceInfo->Models[0])
		{
			ReferenceSkeleton = ReferenceInfo->Models[0]->Skeleton;
		}
		if (!ReferenceSkeleton && ReferenceInfo && ReferenceInfo->SkeletonCount > 0 &&
			ReferenceInfo->Skeletons)
		{
			ReferenceSkeleton = ReferenceInfo->Skeletons[0];
		}
		if (!ReferenceSkeleton || ReferenceSkeleton->BoneCount <= 0)
		{
			OutError = FString::Printf(TEXT("No reference skeleton found in: %s"),
				*ReferenceSkeletonGrannyPath);
			if (ReferenceSkeletonFile)
			{
				GrannyFreeFile(ReferenceSkeletonFile);
			}
			GrannyFreeFile(GrannyFile);
			return false;
		}
		Skeleton = ReferenceSkeleton;
	}

	TArray<FMT2GrannyMaterialSlot> MaterialSlots;
	TMap<const granny_material*, int32> MaterialSlotIndexByPointer;
	BuildMaterialSlotsFromFileInfo(Info, MaterialSlots, &MaterialSlotIndexByPointer);
	if (MaterialSlots.Num() == 0)
	{
		FMT2GrannyMaterialSlot DefaultSlot;
		DefaultSlot.SlotName = TEXT("Default");
		MaterialSlots.Add(DefaultSlot);
	}

	TArray<FString> GltfMaterialNames;
	GltfMaterialNames.Reserve(MaterialSlots.Num());
	for (int32 MaterialIndex = 0; MaterialIndex < MaterialSlots.Num(); ++MaterialIndex)
	{
		const FString SlotName = MaterialSlots[MaterialIndex].SlotName.IsEmpty() ? FString::Printf(TEXT("material_%d"), MaterialIndex) : MaterialSlots[MaterialIndex].SlotName;
		FString GltfMaterialName = FMT2AssetScanner::SanitizePackagePathSegment(SlotName);
		if (GltfMaterialNames.Contains(GltfMaterialName))
		{
			GltfMaterialName = FMT2AssetScanner::SanitizePackagePathSegment(FString::Printf(TEXT("%s_%d"), *SlotName, MaterialIndex));
		}
		MaterialSlots[MaterialIndex].ImportedSlotName = GltfMaterialName;
		GltfMaterialNames.Add(GltfMaterialName);
	}

	auto ResolveGltfMaterialIndex = [&MaterialSlotIndexByPointer](const granny_mesh* Mesh, int32 LocalMaterialIndex) -> int32
	{
		if (Mesh && Mesh->MaterialBindings && LocalMaterialIndex >= 0 && LocalMaterialIndex < Mesh->MaterialBindingCount)
		{
			granny_material* Material = Mesh->MaterialBindings[LocalMaterialIndex].Material;
			if (const int32* MaterialSlotIndex = MaterialSlotIndexByPointer.Find(Material))
			{
				return *MaterialSlotIndex;
			}
		}

		return 0;
	};

	TArray<uint8> Binary;
	TArray<FString> BufferViews;
	TArray<FString> Accessors;
	TArray<FString> Primitives;

	auto AddBufferView = [&BufferViews](int32 Offset, int32 Length, int32 Target) -> int32
	{
		const int32 Index = BufferViews.Num();
		FString View = FString::Printf(TEXT("{\"buffer\":0,\"byteOffset\":%d,\"byteLength\":%d"), Offset, Length);
		if (Target != 0)
		{
			View += FString::Printf(TEXT(",\"target\":%d"), Target);
		}
		View += TEXT("}");
		BufferViews.Add(View);
		return Index;
	};

	auto AddAccessor = [&Accessors](int32 BufferView, int32 ComponentType, int32 Count, const TCHAR* Type, const FString& Extra = FString()) -> int32
	{
		const int32 Index = Accessors.Num();
		FString Accessor = FString::Printf(TEXT("{\"bufferView\":%d,\"componentType\":%d,\"count\":%d,\"type\":\"%s\""), BufferView, ComponentType, Count, Type);
		if (!Extra.IsEmpty())
		{
			Accessor += TEXT(",");
			Accessor += Extra;
		}
		Accessor += TEXT("}");
		Accessors.Add(Accessor);
		return Index;
	};

	int32 MeshesWritten = 0;
	for (granny_int32x MeshIndex = 0; MeshIndex < Info->MeshCount; ++MeshIndex)
	{
		granny_mesh* Mesh = Info->Meshes[MeshIndex];
		if (!Mesh)
		{
			continue;
		}

		TArray<float> Positions;
		TArray<float> Normals;
		TArray<float> TexCoords;
		TArray<float> TexCoords1;
		TArray<float> BoneWeights;
		TArray<uint8> BoneIndices;
		CopyVertexFloats(Mesh, GrannyVertexPositionName, 3, Positions);
		CopyVertexFloats(Mesh, GrannyVertexNormalName, 3, Normals);
		CopyVertexFloats(Mesh, GrannyVertexTextureCoordinatesName "0", 2, TexCoords);
		CopyVertexFloats(Mesh, GrannyVertexTextureCoordinatesName "1", 2, TexCoords1);
		CopyVertexFloats(Mesh, GrannyVertexBoneWeightsName, 4, BoneWeights);
		CopyVertexUInt8(Mesh, GrannyVertexBoneIndicesName, 4, BoneIndices);

		const int32 VertexCount = Positions.Num() / 3;
		if (VertexCount <= 0)
		{
			continue;
		}
		for (int32 Index = 0; Index + 2 < Positions.Num(); Index += 3)
		{
			const FVector3f Converted = ConvertGrannyVectorToGltf(FVector3f(
				Positions[Index], Positions[Index + 1], Positions[Index + 2])) * GrannyUnitsToGltfMeters;
			Positions[Index] = Converted.X;
			Positions[Index + 1] = Converted.Y;
			Positions[Index + 2] = Converted.Z;
		}

		if (Normals.Num() != VertexCount * 3)
		{
			Normals.Init(0.0f, VertexCount * 3);
		}
		else
		{
			for (int32 Index = 0; Index + 2 < Normals.Num(); Index += 3)
			{
				const FVector3f Converted = ConvertGrannyVectorToGltf(FVector3f(
					Normals[Index], Normals[Index + 1], Normals[Index + 2])).GetSafeNormal();
				Normals[Index] = Converted.X;
				Normals[Index + 1] = Converted.Y;
				Normals[Index + 2] = Converted.Z;
			}
		}
		if (TexCoords.Num() != VertexCount * 2)
		{
			TexCoords.Init(0.0f, VertexCount * 2);
		}
		ApplyUVTransform(TexCoords, UVTransform);
		const bool bHasTexCoords1 = TexCoords1.Num() == VertexCount * 2;
		if (bHasTexCoords1)
		{
			ApplyUVTransform(TexCoords1, UVTransform);
		}
		if (BoneWeights.Num() != VertexCount * 4)
		{
			BoneWeights.Init(0.0f, VertexCount * 4);
			for (int32 VertexIndex = 0; VertexIndex < VertexCount; ++VertexIndex)
			{
				BoneWeights[VertexIndex * 4] = 1.0f;
			}
		}
		if (BoneIndices.Num() != VertexCount * 4)
		{
			BoneIndices.Init(0, VertexCount * 4);
		}

		if (Mesh->BoneBindingCount > 0 && Mesh->BoneBindings)
		{
			TArray<uint8> RemappedBoneIndices = BoneIndices;
			TArray<int32> SkeletonIndexByBinding;
			SkeletonIndexByBinding.SetNumUninitialized(Mesh->BoneBindingCount);
			for (int32 BindingIndex = 0; BindingIndex < Mesh->BoneBindingCount; ++BindingIndex)
			{
				const char* BindingName = Mesh->BoneBindings[BindingIndex].BoneName;
				SkeletonIndexByBinding[BindingIndex] = BindingName
					? FindSkeletonBoneIndexByName(Skeleton, UTF8_TO_TCHAR(BindingName))
					: INDEX_NONE;
			}
			for (int32 VertexIndex = 0; VertexIndex < VertexCount; ++VertexIndex)
			{
				for (int32 InfluenceIndex = 0; InfluenceIndex < 4; ++InfluenceIndex)
				{
					const int32 ValueIndex = VertexIndex * 4 + InfluenceIndex;
					const int32 LocalBoneIndex = BoneIndices[ValueIndex];
					const bool bWeightedInfluence = BoneWeights[ValueIndex] > UE_SMALL_NUMBER;
					if (!SkeletonIndexByBinding.IsValidIndex(LocalBoneIndex))
					{
						if (!bWeightedInfluence)
						{
							RemappedBoneIndices[ValueIndex] = 0;
							continue;
						}
						OutError = FString::Printf(
							TEXT("Mesh %s vertex %d references invalid local bone %d (binding count %d)."),
							Mesh->Name ? UTF8_TO_TCHAR(Mesh->Name) : TEXT("None"),
							VertexIndex, LocalBoneIndex, Mesh->BoneBindingCount);
						if (ReferenceSkeletonFile)
						{
							GrannyFreeFile(ReferenceSkeletonFile);
						}
						GrannyFreeFile(GrannyFile);
						return false;
					}

					const int32 SkeletonBoneIndex = SkeletonIndexByBinding[LocalBoneIndex];
					if (SkeletonBoneIndex == INDEX_NONE || SkeletonBoneIndex > MAX_uint8)
					{
						if (!bWeightedInfluence)
						{
							RemappedBoneIndices[ValueIndex] = 0;
							continue;
						}
						const char* BindingName = Mesh->BoneBindings[LocalBoneIndex].BoneName;
						OutError = FString::Printf(
							TEXT("Mesh %s weighted bone '%s' cannot be mapped to the selected skeleton."),
							Mesh->Name ? UTF8_TO_TCHAR(Mesh->Name) : TEXT("None"),
							BindingName ? UTF8_TO_TCHAR(BindingName) : TEXT("None"));
						if (ReferenceSkeletonFile)
						{
							GrannyFreeFile(ReferenceSkeletonFile);
						}
						GrannyFreeFile(GrannyFile);
						return false;
					}
					RemappedBoneIndices[ValueIndex] = static_cast<uint8>(SkeletonBoneIndex);
				}
			}
			BoneIndices = MoveTemp(RemappedBoneIndices);
		}

		for (int32 VertexIndex = 0; VertexIndex < VertexCount; ++VertexIndex)
		{
			float TotalWeight = 0.0f;
			for (int32 InfluenceIndex = 0; InfluenceIndex < 4; ++InfluenceIndex)
			{
				TotalWeight += BoneWeights[VertexIndex * 4 + InfluenceIndex];
			}
			if (TotalWeight <= UE_SMALL_NUMBER)
			{
				BoneWeights[VertexIndex * 4] = 1.0f;
			}
			else
			{
				for (int32 InfluenceIndex = 0; InfluenceIndex < 4; ++InfluenceIndex)
				{
					BoneWeights[VertexIndex * 4 + InfluenceIndex] /= TotalWeight;
				}
			}
		}

		TArray<uint32> Indices;
		Indices.SetNumZeroed(GrannyGetMeshIndexCount(Mesh));
		if (Indices.Num() > 0)
		{
			GrannyCopyMeshIndices(Mesh, 4, Indices.GetData());
		}

		const int32 PositionOffset = AppendTypedBuffer(Binary, Positions);
		const int32 NormalOffset = AppendTypedBuffer(Binary, Normals);
		const int32 TexCoordOffset = AppendTypedBuffer(Binary, TexCoords);
		const int32 TexCoord1Offset = bHasTexCoords1 ? AppendTypedBuffer(Binary, TexCoords1) : INDEX_NONE;
		const int32 JointOffset = AppendTypedBuffer(Binary, BoneIndices);
		const int32 WeightOffset = AppendTypedBuffer(Binary, BoneWeights);

		TArray<float> MinPos = { Positions[0], Positions[1], Positions[2] };
		TArray<float> MaxPos = MinPos;
		for (int32 Index = 0; Index + 2 < Positions.Num(); Index += 3)
		{
			MinPos[0] = FMath::Min(MinPos[0], Positions[Index + 0]);
			MinPos[1] = FMath::Min(MinPos[1], Positions[Index + 1]);
			MinPos[2] = FMath::Min(MinPos[2], Positions[Index + 2]);
			MaxPos[0] = FMath::Max(MaxPos[0], Positions[Index + 0]);
			MaxPos[1] = FMath::Max(MaxPos[1], Positions[Index + 1]);
			MaxPos[2] = FMath::Max(MaxPos[2], Positions[Index + 2]);
		}

		const int32 PositionAccessor = AddAccessor(AddBufferView(PositionOffset, Positions.Num() * sizeof(float), 34962), 5126, VertexCount, TEXT("VEC3"), FString::Printf(TEXT("\"min\":%s,\"max\":%s"), *MakeJsonFloatArray(MinPos), *MakeJsonFloatArray(MaxPos)));
		const int32 NormalAccessor = AddAccessor(AddBufferView(NormalOffset, Normals.Num() * sizeof(float), 34962), 5126, VertexCount, TEXT("VEC3"));
		const int32 TexCoordAccessor = AddAccessor(AddBufferView(TexCoordOffset, TexCoords.Num() * sizeof(float), 34962), 5126, VertexCount, TEXT("VEC2"));
		const int32 TexCoord1Accessor = bHasTexCoords1 ? AddAccessor(AddBufferView(TexCoord1Offset, TexCoords1.Num() * sizeof(float), 34962), 5126, VertexCount, TEXT("VEC2")) : INDEX_NONE;
		const int32 JointAccessor = AddAccessor(AddBufferView(JointOffset, BoneIndices.Num() * sizeof(uint8), 34962), 5121, VertexCount, TEXT("VEC4"));
		const int32 WeightAccessor = AddAccessor(AddBufferView(WeightOffset, BoneWeights.Num() * sizeof(float), 34962), 5126, VertexCount, TEXT("VEC4"));

		auto AddPrimitive = [&](const TArray<uint32>& PrimitiveIndices, int32 MaterialIndex)
		{
			if (PrimitiveIndices.Num() <= 0)
			{
				return;
			}

			const int32 PrimitiveIndexOffset = AppendTypedBuffer(Binary, PrimitiveIndices);
			const int32 IndexAccessor = AddAccessor(AddBufferView(PrimitiveIndexOffset, PrimitiveIndices.Num() * sizeof(uint32), 34963), 5125, PrimitiveIndices.Num(), TEXT("SCALAR"));
			FString Attributes = FString::Printf(TEXT("\"POSITION\":%d,\"NORMAL\":%d,\"TEXCOORD_0\":%d"),
				PositionAccessor,
				NormalAccessor,
				TexCoordAccessor);
			if (TexCoord1Accessor != INDEX_NONE)
			{
				Attributes += FString::Printf(TEXT(",\"TEXCOORD_1\":%d"), TexCoord1Accessor);
			}
			Attributes += FString::Printf(TEXT(",\"JOINTS_0\":%d,\"WEIGHTS_0\":%d"),
				JointAccessor,
				WeightAccessor);
			Primitives.Add(FString::Printf(TEXT("{\"attributes\":{%s},\"indices\":%d,\"material\":%d}"),
				*Attributes,
				IndexAccessor,
				FMath::Clamp(MaterialIndex, 0, GltfMaterialNames.Num() - 1)));
		};

		const granny_tri_topology* Topology = Mesh->PrimaryTopology;
		if (Topology && Topology->GroupCount > 0 && Topology->Groups)
		{
			for (granny_int32 GroupIndex = 0; GroupIndex < Topology->GroupCount; ++GroupIndex)
			{
				const granny_tri_material_group& Group = Topology->Groups[GroupIndex];
				const int32 FirstIndex = FMath::Max(0, Group.TriFirst * 3);
				const int32 EndIndex = FMath::Min(Indices.Num(), (Group.TriFirst + Group.TriCount) * 3);
				TArray<uint32> GroupIndices;
				GroupIndices.Reserve(FMath::Max(0, EndIndex - FirstIndex));
				for (int32 Index = FirstIndex; Index + 2 < EndIndex; Index += 3)
				{
					GroupIndices.Add(Indices[Index + 0]);
					GroupIndices.Add(Indices[Index + 1]);
					GroupIndices.Add(Indices[Index + 2]);
				}
				AddPrimitive(GroupIndices, ResolveGltfMaterialIndex(Mesh, Group.MaterialIndex));
			}
		}
		else
		{
			AddPrimitive(Indices, 0);
		}
		MeshesWritten++;
	}

	if (MeshesWritten == 0)
	{
		OutError = FString::Printf(TEXT("No exportable meshes found in: %s"), *InputGrannyPath);
		if (ReferenceSkeletonFile)
		{
			GrannyFreeFile(ReferenceSkeletonFile);
		}
		GrannyFreeFile(GrannyFile);
		return false;
	}

	TArray<FString> Nodes;
	TArray<int32> RootNodes;
	for (granny_int32x BoneIndex = 0; BoneIndex < Skeleton->BoneCount; ++BoneIndex)
	{
		const granny_bone& Bone = Skeleton->Bones[BoneIndex];
		TArray<int32> Children;
		for (granny_int32x ChildIndex = 0; ChildIndex < Skeleton->BoneCount; ++ChildIndex)
		{
			if (Skeleton->Bones[ChildIndex].ParentIndex == BoneIndex)
			{
				Children.Add(ChildIndex);
			}
		}

		granny_real32 LocalMatrix[16];
		GrannyBuildCompositeTransform4x4(&Bone.LocalTransform, LocalMatrix);
		FMatrix GrannyLocalMatrix = MakeGrannyMatrix(LocalMatrix);
		if (Bone.ParentIndex < 0)
		{
			// Granny's root inverse bind includes the model initial placement even though
			// the root bone local transform is commonly identity. Preserve that rigid root
			// placement while keeping every child in its authored local transform. Asking
			// UE's glTF reader to derive all locals from inverse binds decomposes shear
			// created by rotated children below non-uniformly scaled parents.
			GrannyLocalMatrix = MakeGrannyMatrix(&Bone.InverseWorld4x4[0][0]).Inverse();
		}
		FTransform ConvertedLocal;
		const FMatrix Basis = GetGrannyToGltfBasis();
		ConvertedLocal.SetFromMatrix(Basis.GetTransposed() * GrannyLocalMatrix * Basis);
		ConvertedLocal.NormalizeRotation();
		const FVector ConvertedTranslation = ConvertedLocal.GetTranslation() * GrannyUnitsToGltfMeters;
		const FQuat ConvertedRotation = ConvertedLocal.GetRotation();
		ConvertedLocal.SetTranslation(ConvertedTranslation);
		const FVector ConvertedScale = ConvertedLocal.GetScale3D();
		TArray<float> Translation = {
			static_cast<float>(ConvertedLocal.GetTranslation().X),
			static_cast<float>(ConvertedLocal.GetTranslation().Y),
			static_cast<float>(ConvertedLocal.GetTranslation().Z)
		};
		TArray<float> Rotation = {
			static_cast<float>(ConvertedRotation.X),
			static_cast<float>(ConvertedRotation.Y),
			static_cast<float>(ConvertedRotation.Z),
			static_cast<float>(ConvertedRotation.W)
		};
		TArray<float> Scale = {
			static_cast<float>(ConvertedScale.X),
			static_cast<float>(ConvertedScale.Y),
			static_cast<float>(ConvertedScale.Z)
		};
		for (float& ScaleValue : Scale)
		{
			if (FMath::IsNearlyZero(ScaleValue))
			{
				ScaleValue = 1.0f;
			}
		}
		FString Node = FString::Printf(TEXT("{\"name\":%s,\"translation\":%s,\"rotation\":%s,\"scale\":%s"),
			*JsonString(ToObjName(Bone.Name, BoneIndex)),
			*MakeJsonFloatArray(Translation),
			*MakeJsonFloatArray(Rotation),
			*MakeJsonFloatArray(Scale));
		if (Children.Num() > 0)
		{
			Node += FString::Printf(TEXT(",\"children\":%s"), *MakeJsonIntArray(Children));
		}
		Node += TEXT("}");
		Nodes.Add(Node);

		if (Bone.ParentIndex < 0)
		{
			RootNodes.Add(BoneIndex);
		}
	}

	const int32 SkeletonRootNodeIndex = RootNodes.Num() > 0 ? RootNodes[0] : 0;

	const int32 MeshNodeIndex = Nodes.Num();
	Nodes.Add(FString::Printf(TEXT("{\"name\":%s,\"mesh\":0,\"skin\":0}"), *JsonString(FPaths::GetBaseFilename(InputGrannyPath))));

	// Skinned-mesh parent transforms are intentionally ignored by Interchange. Keep the
	// mesh and skeleton as scene roots so both receive the same glTF-to-UE basis conversion.
	TArray<int32> SceneRootNodes = RootNodes;
	SceneRootNodes.Add(MeshNodeIndex);

	TArray<int32> JointNodes;
	for (int32 BoneIndex = 0; BoneIndex < Skeleton->BoneCount; ++BoneIndex)
	{
		JointNodes.Add(BoneIndex);
	}

	const FString BinFileName = FPaths::GetBaseFilename(OutputGltfPath) + TEXT(".bin");
	FString Json;
	Json += TEXT("{\"asset\":{\"version\":\"2.0\",\"generator\":\"MT2UE\"},");
	Json += FString::Printf(TEXT("\"buffers\":[{\"uri\":%s,\"byteLength\":%d}],"), *JsonString(BinFileName), Binary.Num());
	Json += FString::Printf(TEXT("\"bufferViews\":[%s],"), *FString::Join(BufferViews, TEXT(",")));
	Json += FString::Printf(TEXT("\"accessors\":[%s],"), *FString::Join(Accessors, TEXT(",")));
	TArray<FString> GltfMaterials;
	GltfMaterials.Reserve(GltfMaterialNames.Num());
	for (const FString& MaterialName : GltfMaterialNames)
	{
		GltfMaterials.Add(FString::Printf(TEXT("{\"name\":%s,\"pbrMetallicRoughness\":{\"baseColorFactor\":[1,1,1,1],\"metallicFactor\":0,\"roughnessFactor\":0.8}}"), *JsonString(MaterialName)));
	}
	Json += FString::Printf(TEXT("\"materials\":[%s],"), *FString::Join(GltfMaterials, TEXT(",")));
	Json += FString::Printf(TEXT("\"meshes\":[{\"name\":%s,\"primitives\":[%s]}],"), *JsonString(FPaths::GetBaseFilename(InputGrannyPath)), *FString::Join(Primitives, TEXT(",")));
	// UE 5.7's glTF reader decomposes global inverse-bind matrices before deriving local
	// joints. That corrupts children below non-uniformly scaled bones. Omitting the optional
	// matrices makes it consume the exact authored local TRS nodes instead.
	Json += FString::Printf(
		TEXT("\"skins\":[{\"name\":%s,\"skeleton\":%d,\"joints\":%s}],"),
		*JsonString(ToObjName(Skeleton->Name, 0)),
		SkeletonRootNodeIndex,
		*MakeJsonIntArray(JointNodes));
	Json += FString::Printf(TEXT("\"nodes\":[%s],"), *FString::Join(Nodes, TEXT(",")));
	Json += FString::Printf(TEXT("\"scenes\":[{\"nodes\":%s}],\"scene\":0}"), *MakeJsonIntArray(SceneRootNodes));

	if (ReferenceSkeletonFile)
	{
		GrannyFreeFile(ReferenceSkeletonFile);
	}
	GrannyFreeFile(GrannyFile);

	const FString OutputDirectory = FPaths::GetPath(OutputGltfPath);
	FPlatformFileManager::Get().GetPlatformFile().CreateDirectoryTree(*OutputDirectory);
	const FString OutputBinPath = OutputDirectory / BinFileName;
	if (!FFileHelper::SaveArrayToFile(Binary, *OutputBinPath))
	{
		OutError = FString::Printf(TEXT("Failed to write glTF binary: %s"), *OutputBinPath);
		return false;
	}
	if (!FFileHelper::SaveStringToFile(Json, *OutputGltfPath))
	{
		OutError = FString::Printf(TEXT("Failed to write glTF: %s"), *OutputGltfPath);
		return false;
	}

	if (OutMaterialSlots)
	{
		*OutMaterialSlots = MoveTemp(MaterialSlots);
	}
	return true;
}
