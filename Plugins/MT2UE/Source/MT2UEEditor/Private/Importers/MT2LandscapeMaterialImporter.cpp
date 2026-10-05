/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "Importers/MT2LandscapeMaterialImporter.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/Texture2D.h"
#include "Importers/MT2TextureImporter.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionAdd.h"
#include "Materials/MaterialExpressionConstant2Vector.h"
#include "Materials/MaterialExpressionDesaturation.h"
#include "Materials/MaterialExpressionLandscapeLayerCoords.h"
#include "Materials/MaterialExpressionLandscapeGrassOutput.h"
#include "Materials/MaterialExpressionLandscapeLayerBlend.h"
#include "Materials/MaterialExpressionLandscapeLayerSample.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionOneMinus.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Misc/Crc.h"
#include "Misc/Paths.h"

namespace
{
	struct FLandscapeMaterialPaths
	{
		FString MaterialAssetName;
		FString MaterialPackage;
		FString MaterialObject;
		FString InstanceAssetName;
		FString InstancePackage;
		FString InstanceObject;
	};

	FLandscapeMaterialPaths BuildPaths(const FMT2ImportContext& Context, const FString& MapName)
	{
		FLandscapeMaterialPaths Paths;
		const FString SanitizedMapName = FMT2AssetScanner::SanitizePackagePathSegment(MapName);
		const FString Folder = Context.DestinationRoot / TEXT("Materials") / SanitizedMapName;
		Paths.MaterialAssetName = TEXT("M_") + SanitizedMapName + TEXT("_Landscape");
		Paths.MaterialPackage = Folder / Paths.MaterialAssetName;
		Paths.MaterialObject = Paths.MaterialPackage + TEXT(".") + Paths.MaterialAssetName;
		Paths.InstanceAssetName = TEXT("MI_") + SanitizedMapName + TEXT("_Landscape");
		Paths.InstancePackage = Folder / Paths.InstanceAssetName;
		Paths.InstanceObject = Paths.InstancePackage + TEXT(".") + Paths.InstanceAssetName;
		return Paths;
	}

	void GatherMapLayerNames(
		const FMT2ImportContext& Context, const FMT2MapTerrainInfo& Map, TArray<FName>& OutLayerNames)
	{
		TSet<FName> UniqueNames;
		for (int32 TextureIndex = 0; TextureIndex < Map.TextureSetTextures.Num(); ++TextureIndex)
		{
			UniqueNames.Add(FMT2LandscapeMaterialImporter::BuildLayerNameForTile(
				Context, Map, static_cast<uint8>(TextureIndex + 1)));
		}
		OutLayerNames = UniqueNames.Array();
		OutLayerNames.Sort(FNameLexicalLess());
	}

	const FMT2TerrainTextureInfo* FindTextureInfoForLayer(
		const FMT2ImportContext& Context, const FMT2MapTerrainInfo& Map, const FName LayerName)
	{
		for (int32 TextureIndex = 0; TextureIndex < Map.TextureSetTextures.Num(); ++TextureIndex)
		{
			if (FMT2LandscapeMaterialImporter::BuildLayerNameForTile(
				Context, Map, static_cast<uint8>(TextureIndex + 1)) == LayerName)
			{
				return Map.TextureSetEntries.IsValidIndex(TextureIndex)
					? &Map.TextureSetEntries[TextureIndex]
					: nullptr;
			}
		}
		return nullptr;
	}

	FString FindTextureReferenceForLayer(
		const FMT2ImportContext& Context, const FMT2MapTerrainInfo& Map, const FName LayerName)
	{
		for (int32 TextureIndex = 0; TextureIndex < Map.TextureSetTextures.Num(); ++TextureIndex)
		{
			if (FMT2LandscapeMaterialImporter::BuildLayerNameForTile(
				Context, Map, static_cast<uint8>(TextureIndex + 1)) == LayerName)
			{
				return Map.TextureSetTextures[TextureIndex];
			}
		}
		return FString();
	}

	UTexture2D* LoadTerrainTexture(
		const FMT2ImportContext& Context, const FString& Reference, FMT2ImportResult& OutResult)
	{
		if (!Context.ScanResult || Reference.IsEmpty())
		{
			return nullptr;
		}
		const FMT2AssetRecord* Record = FMT2TextureImporter::FindRecordForReference(*Context.ScanResult, Reference);
		if (!Record)
		{
			OutResult.AddWarning(FString::Printf(TEXT("Could not resolve landscape texture: %s"), *Reference));
			return nullptr;
		}
		const FString ObjectPath = FMT2TextureImporter::BuildObjectPath(Context, *Record);
		UTexture2D* Texture = LoadObject<UTexture2D>(nullptr, *ObjectPath);
		if (!Texture)
		{
			OutResult.AddWarning(FString::Printf(
				TEXT("Landscape texture asset is missing. Reference='%s', expected asset='%s'."),
				*Reference, *ObjectPath));
		}
		return Texture;
	}

	bool IsUnsetGeneratedTexture(const UTexture* Texture)
	{
		return !Texture || Texture->GetPackage()->GetName().StartsWith(TEXT("/Engine/"));
	}

	UMaterialExpressionLandscapeLayerCoords* FindGeneratedLayerCoords(
		const UMaterialExpressionTextureSampleParameter2D* TextureSample)
	{
		const UMaterialExpressionAdd* OffsetCoords =
			Cast<UMaterialExpressionAdd>(TextureSample ? TextureSample->Coordinates.Expression : nullptr);
		const UMaterialExpressionMultiply* ScaledCoords =
			Cast<UMaterialExpressionMultiply>(OffsetCoords ? OffsetCoords->A.Expression : nullptr);
		return Cast<UMaterialExpressionLandscapeLayerCoords>(ScaledCoords ? ScaledCoords->A.Expression : nullptr);
	}

	FString BuildAntiTilingDescription(const FName LayerName)
	{
		return TEXT("MT2 Terrain Anti-Tiling: ") + LayerName.ToString();
	}

	UMaterialExpressionTextureSampleParameter2D* FindGeneratedBaseTextureSample(
		UMaterialExpression* LayerExpression, const FName LayerName)
	{
		if (UMaterialExpressionTextureSampleParameter2D* TextureSample =
			Cast<UMaterialExpressionTextureSampleParameter2D>(LayerExpression))
		{
			return TextureSample;
		}

		const UMaterialExpressionMultiply* AntiTiling = Cast<UMaterialExpressionMultiply>(LayerExpression);
		if (!AntiTiling || AntiTiling->Desc != BuildAntiTilingDescription(LayerName))
		{
			return nullptr;
		}
		return Cast<UMaterialExpressionTextureSampleParameter2D>(AntiTiling->A.Expression);
	}

	void LayoutGeneratedLayerNodes(
		UMaterialExpressionTextureSampleParameter2D* TextureSample,
		UMaterialExpression* LayerExpression, const int32 LayerIndex)
	{
		if (!TextureSample)
		{
			return;
		}

		constexpr int32 RowSpacing = 400;
		const int32 Y = LayerIndex * RowSpacing;
		TextureSample->MaterialExpressionEditorX = -700;
		TextureSample->MaterialExpressionEditorY = Y;

		if (UMaterialExpressionAdd* OffsetCoords =
			Cast<UMaterialExpressionAdd>(TextureSample->Coordinates.Expression))
		{
			OffsetCoords->MaterialExpressionEditorX = -900;
			OffsetCoords->MaterialExpressionEditorY = Y;
			if (UMaterialExpressionMultiply* ScaledCoords = Cast<UMaterialExpressionMultiply>(OffsetCoords->A.Expression))
			{
				ScaledCoords->MaterialExpressionEditorX = -1100;
				ScaledCoords->MaterialExpressionEditorY = Y;
				if (UMaterialExpression* LayerCoords = ScaledCoords->A.Expression)
				{
					LayerCoords->MaterialExpressionEditorX = -1300;
					LayerCoords->MaterialExpressionEditorY = Y;
				}
				if (UMaterialExpression* Scale = ScaledCoords->B.Expression)
				{
					Scale->MaterialExpressionEditorX = -1300;
					Scale->MaterialExpressionEditorY = Y + 110;
				}
			}
			if (UMaterialExpression* Offset = OffsetCoords->B.Expression)
			{
				Offset->MaterialExpressionEditorX = -1100;
				Offset->MaterialExpressionEditorY = Y + 110;
			}
		}

		UMaterialExpressionMultiply* AntiTiledLayer = Cast<UMaterialExpressionMultiply>(LayerExpression);
		if (!AntiTiledLayer)
		{
			return;
		}
		AntiTiledLayer->MaterialExpressionEditorX = 700;
		AntiTiledLayer->MaterialExpressionEditorY = Y;

		UMaterialExpressionAdd* MacroMultiplier = Cast<UMaterialExpressionAdd>(AntiTiledLayer->B.Expression);
		if (!MacroMultiplier)
		{
			return;
		}
		MacroMultiplier->MaterialExpressionEditorX = 500;
		MacroMultiplier->MaterialExpressionEditorY = Y + 120;

		UMaterialExpressionMultiply* WeightedMacro = Cast<UMaterialExpressionMultiply>(MacroMultiplier->A.Expression);
		UMaterialExpressionOneMinus* MinimumBrightness = Cast<UMaterialExpressionOneMinus>(MacroMultiplier->B.Expression);
		if (WeightedMacro)
		{
			WeightedMacro->MaterialExpressionEditorX = 300;
			WeightedMacro->MaterialExpressionEditorY = Y + 120;
			if (UMaterialExpressionDesaturation* MacroLuminance =
				Cast<UMaterialExpressionDesaturation>(WeightedMacro->A.Expression))
			{
				MacroLuminance->MaterialExpressionEditorX = 100;
				MacroLuminance->MaterialExpressionEditorY = Y + 120;
				if (UMaterialExpressionTextureSampleParameter2D* MacroTexture =
					Cast<UMaterialExpressionTextureSampleParameter2D>(MacroLuminance->Input.Expression))
				{
					MacroTexture->MaterialExpressionEditorX = -100;
					MacroTexture->MaterialExpressionEditorY = Y + 120;
					if (UMaterialExpressionAdd* OffsetMacroCoords =
						Cast<UMaterialExpressionAdd>(MacroTexture->Coordinates.Expression))
					{
						OffsetMacroCoords->MaterialExpressionEditorX = -300;
						OffsetMacroCoords->MaterialExpressionEditorY = Y + 120;
						if (UMaterialExpression* MacroCoords = OffsetMacroCoords->A.Expression)
						{
							MacroCoords->MaterialExpressionEditorX = -500;
							MacroCoords->MaterialExpressionEditorY = Y + 120;
						}
						if (UMaterialExpression* MacroOffset = OffsetMacroCoords->B.Expression)
						{
							MacroOffset->MaterialExpressionEditorX = -500;
							MacroOffset->MaterialExpressionEditorY = Y + 230;
						}
					}
				}
			}
			if (UMaterialExpression* TwiceStrength = WeightedMacro->B.Expression)
			{
				TwiceStrength->MaterialExpressionEditorX = 100;
				TwiceStrength->MaterialExpressionEditorY = Y + 230;
			}
		}
		if (MinimumBrightness)
		{
			MinimumBrightness->MaterialExpressionEditorX = 300;
			MinimumBrightness->MaterialExpressionEditorY = Y + 230;
		}
	}

	UMaterialExpressionScalarParameter* EnsureScalarParameter(
		UMaterial* Material, UMaterialEditorOnlyData* EditorData, const FName Name, const float DefaultValue,
		const int32 EditorY, bool& bMaterialChanged)
	{
		for (UMaterialExpression* Expression : Material->GetExpressions())
		{
			if (UMaterialExpressionScalarParameter* Parameter = Cast<UMaterialExpressionScalarParameter>(Expression);
				Parameter && Parameter->ParameterName == Name)
			{
				return Parameter;
			}
		}

		UMaterialExpressionScalarParameter* Parameter = NewObject<UMaterialExpressionScalarParameter>(Material);
		Parameter->Material = Material;
		Parameter->ParameterName = Name;
		Parameter->DefaultValue = DefaultValue;
		Parameter->MaterialExpressionEditorX = 250;
		Parameter->MaterialExpressionEditorY = EditorY;
		EditorData->ExpressionCollection.AddExpression(Parameter);
		bMaterialChanged = true;
		return Parameter;
	}

	UMaterialInstanceConstant* EnsureMapLandscapeMaterial(
		const FMT2ImportContext& Context, const FMT2MapTerrainInfo& Map, FMT2ImportResult& OutResult)
	{
		TArray<FName> RequiredLayerNames;
		GatherMapLayerNames(Context, Map, RequiredLayerNames);
		const FLandscapeMaterialPaths Paths = BuildPaths(Context, Map.MapName);

		UPackage* MaterialPackage = CreatePackage(*Paths.MaterialPackage);
		UMaterial* Material = LoadObject<UMaterial>(nullptr, *Paths.MaterialObject);
		const bool bCreatedMaterial = Material == nullptr;
		if (!Material)
		{
			Material = NewObject<UMaterial>(MaterialPackage, *Paths.MaterialAssetName,
				RF_Public | RF_Standalone | RF_Transactional);
		}
		if (!Material)
		{
			OutResult.AddError(FString::Printf(TEXT("Failed to create landscape material for %s."), *Map.MapName));
			return nullptr;
		}

		UMaterialEditorOnlyData* EditorData = Material->GetEditorOnlyData();
		if (!EditorData)
		{
			OutResult.AddError(FString::Printf(TEXT("Failed to access landscape material editor data for %s."), *Map.MapName));
			return nullptr;
		}

		UMaterialExpressionLandscapeLayerBlend* LayerBlend = nullptr;
		UMaterialExpressionLandscapeGrassOutput* GrassOutput = nullptr;
		TMap<FName, UMaterialExpressionLandscapeLayerSample*> LayerSamples;
		TMap<FName, UMaterialExpressionTextureSampleParameter2D*> TextureSamples;
		for (UMaterialExpression* Expression : Material->GetExpressions())
		{
			if (UMaterialExpressionLandscapeLayerBlend* ExistingBlend =
				Cast<UMaterialExpressionLandscapeLayerBlend>(Expression))
			{
				LayerBlend = ExistingBlend;
			}
			else if (UMaterialExpressionLandscapeGrassOutput* ExistingGrassOutput =
				Cast<UMaterialExpressionLandscapeGrassOutput>(Expression))
			{
				GrassOutput = ExistingGrassOutput;
			}
			else if (UMaterialExpressionLandscapeLayerSample* ExistingSample =
				Cast<UMaterialExpressionLandscapeLayerSample>(Expression))
			{
				LayerSamples.FindOrAdd(ExistingSample->ParameterName) = ExistingSample;
			}
			else if (UMaterialExpressionTextureSampleParameter2D* ExistingTextureSample =
				Cast<UMaterialExpressionTextureSampleParameter2D>(Expression))
			{
				TextureSamples.FindOrAdd(ExistingTextureSample->ParameterName) = ExistingTextureSample;
			}
		}

		bool bMaterialChanged = bCreatedMaterial;
		Material->Modify();
		if (!LayerBlend)
		{
			LayerBlend = NewObject<UMaterialExpressionLandscapeLayerBlend>(Material);
			LayerBlend->Material = Material;
			LayerBlend->MaterialExpressionEditorX = -350;
			LayerBlend->MaterialExpressionEditorY = 0;
			EditorData->ExpressionCollection.AddExpression(LayerBlend);
			bMaterialChanged = true;
		}
		if (!GrassOutput)
		{
			GrassOutput = NewObject<UMaterialExpressionLandscapeGrassOutput>(Material);
			GrassOutput->Material = Material;
			GrassOutput->MaterialExpressionEditorX = 350;
			GrassOutput->MaterialExpressionEditorY = 400;
			GrassOutput->GrassTypes.Reset();
			EditorData->ExpressionCollection.AddExpression(GrassOutput);
			bMaterialChanged = true;
		}

		UMaterialExpressionScalarParameter* MacroVariationScale = EnsureScalarParameter(
			Material, EditorData, TEXT("TerrainMacroVariationScale"), 0.0138f, -200, bMaterialChanged);
		UMaterialExpressionScalarParameter* MacroVariationStrength = EnsureScalarParameter(
			Material, EditorData, TEXT("TerrainMacroVariationStrength"), 0.50f, -100, bMaterialChanged);
		MacroVariationScale->MaterialExpressionEditorX = -700;
		MacroVariationScale->MaterialExpressionEditorY = -300;
		MacroVariationStrength->MaterialExpressionEditorX = 100;
		MacroVariationStrength->MaterialExpressionEditorY = -300;

		TSet<FName> ExistingLayerNames;
		for (const FLayerBlendInput& Input : LayerBlend->Layers)
		{
			ExistingLayerNames.Add(Input.LayerName);
		}
		for (const FName LayerName : RequiredLayerNames)
		{
			if (LayerName.IsNone() || ExistingLayerNames.Contains(LayerName))
			{
				continue;
			}
			FLayerBlendInput& Input = LayerBlend->Layers.AddDefaulted_GetRef();
			Input.LayerName = LayerName;
			Input.BlendType = LB_WeightBlend;
			Input.PreviewWeight = LayerBlend->Layers.Num() == 1 ? 1.0f : 0.0f;
			ExistingLayerNames.Add(LayerName);
			bMaterialChanged = true;
		}

		int32 TextureLayerIndex = 0;
		for (FLayerBlendInput& Input : LayerBlend->Layers)
		{
			if (!RequiredLayerNames.Contains(Input.LayerName))
			{
				continue;
			}

			const FMT2TerrainTextureInfo* TextureInfo = FindTextureInfoForLayer(Context, Map, Input.LayerName);
			const FString Reference = TextureInfo
				? TextureInfo->Reference
				: FindTextureReferenceForLayer(Context, Map, Input.LayerName);
			const FName ParameterName(*(TEXT("Texture_") + Input.LayerName.ToString()));
			UMaterialExpression* ExistingLayerExpression = Input.LayerInput.Expression;
			UMaterialExpressionTextureSampleParameter2D* TextureSample =
				FindGeneratedBaseTextureSample(ExistingLayerExpression, Input.LayerName);
			if (TextureSample && TextureSample->ParameterName != ParameterName)
			{
				// A differently named node is a manual layer customization and must remain untouched.
				++TextureLayerIndex;
				continue;
			}
			if (!TextureSample && ExistingLayerExpression)
			{
				++TextureLayerIndex;
				continue;
			}
			if (!TextureSample)
			{
				TextureSample = TextureSamples.FindRef(ParameterName);
			}
			if (!TextureSample)
			{
				TextureSample = NewObject<UMaterialExpressionTextureSampleParameter2D>(Material);
				TextureSample->Material = Material;
				TextureSample->ParameterName = ParameterName;
				TextureSample->MaterialExpressionEditorX = -350;
				TextureSample->MaterialExpressionEditorY = -300 + TextureLayerIndex * 180;
				EditorData->ExpressionCollection.AddExpression(TextureSample);
				TextureSamples.Add(ParameterName, TextureSample);
				bMaterialChanged = true;
			}
			if (IsUnsetGeneratedTexture(TextureSample->Texture))
			{
				if (UTexture2D* ImportedTexture = LoadTerrainTexture(Context, Reference, OutResult))
				{
					TextureSample->Texture = ImportedTexture;
					bMaterialChanged = true;
				}
			}
			if (UMaterialExpressionLandscapeLayerCoords* ExistingLayerCoords = FindGeneratedLayerCoords(TextureSample))
			{
				const float IncorrectCentimeterScale = 16.0f * static_cast<float>(Map.CellScale);
				if (FMath::IsNearlyEqual(ExistingLayerCoords->MappingScale, IncorrectCentimeterScale))
				{
					ExistingLayerCoords->MappingScale = 16.0f;
					bMaterialChanged = true;
				}
			}
			if (!TextureSample->Coordinates.Expression)
			{
				UMaterialExpressionLandscapeLayerCoords* LayerCoords =
					NewObject<UMaterialExpressionLandscapeLayerCoords>(Material);
				LayerCoords->Material = Material;
				LayerCoords->MappingType = TCMT_XY;
				// UE landscape XY coordinates are measured in quads. The old client divides world
				// coordinates by PATCH_XSIZE * CELLSCALE, so CELLSCALE cancels here.
				LayerCoords->MappingScale = 16.0f;
				LayerCoords->MaterialExpressionEditorX = -650;
				LayerCoords->MaterialExpressionEditorY = TextureSample->MaterialExpressionEditorY;
				EditorData->ExpressionCollection.AddExpression(LayerCoords);

				UMaterialExpressionConstant2Vector* Scale = NewObject<UMaterialExpressionConstant2Vector>(Material);
				Scale->Material = Material;
				Scale->R = TextureInfo ? TextureInfo->UScale : 4.0f;
				Scale->G = TextureInfo ? -TextureInfo->VScale : -4.0f;
				Scale->MaterialExpressionEditorX = -650;
				Scale->MaterialExpressionEditorY = TextureSample->MaterialExpressionEditorY + 50;
				EditorData->ExpressionCollection.AddExpression(Scale);

				UMaterialExpressionMultiply* ScaledCoords = NewObject<UMaterialExpressionMultiply>(Material);
				ScaledCoords->Material = Material;
				ScaledCoords->A.Expression = LayerCoords;
				ScaledCoords->B.Expression = Scale;
				ScaledCoords->MaterialExpressionEditorX = -500;
				ScaledCoords->MaterialExpressionEditorY = TextureSample->MaterialExpressionEditorY;
				EditorData->ExpressionCollection.AddExpression(ScaledCoords);

				UMaterialExpressionConstant2Vector* Offset = NewObject<UMaterialExpressionConstant2Vector>(Material);
				Offset->Material = Material;
				Offset->R = TextureInfo ? TextureInfo->UOffset : 0.0f;
				Offset->G = TextureInfo ? -TextureInfo->VOffset : 0.0f;
				Offset->MaterialExpressionEditorX = -500;
				Offset->MaterialExpressionEditorY = TextureSample->MaterialExpressionEditorY + 50;
				EditorData->ExpressionCollection.AddExpression(Offset);

				UMaterialExpressionAdd* OffsetCoords = NewObject<UMaterialExpressionAdd>(Material);
				OffsetCoords->Material = Material;
				OffsetCoords->A.Expression = ScaledCoords;
				OffsetCoords->B.Expression = Offset;
				OffsetCoords->MaterialExpressionEditorX = -425;
				OffsetCoords->MaterialExpressionEditorY = TextureSample->MaterialExpressionEditorY;
				EditorData->ExpressionCollection.AddExpression(OffsetCoords);
				TextureSample->Coordinates.Expression = OffsetCoords;
				bMaterialChanged = true;
			}

			if (ExistingLayerExpression == TextureSample || !ExistingLayerExpression)
			{
				// Keep the exact old-client detail UVs and modulate them with a much larger,
				// deterministically offset copy. This breaks distant repetition without
				// blurring or replacing the close-range terrain detail.
				UMaterialExpressionMultiply* MacroCoords = NewObject<UMaterialExpressionMultiply>(Material);
				MacroCoords->Material = Material;
				MacroCoords->A.Expression = TextureSample->Coordinates.Expression;
				MacroCoords->B.Expression = MacroVariationScale;
				MacroCoords->MaterialExpressionEditorX = -150;
				MacroCoords->MaterialExpressionEditorY = TextureSample->MaterialExpressionEditorY + 35;
				EditorData->ExpressionCollection.AddExpression(MacroCoords);

				const uint32 LayerHash = GetTypeHash(Input.LayerName);
				UMaterialExpressionConstant2Vector* MacroOffset =
					NewObject<UMaterialExpressionConstant2Vector>(Material);
				MacroOffset->Material = Material;
				MacroOffset->R = 0.13f + static_cast<float>(LayerHash & 0xffffu) / 65535.0f * 0.73f;
				MacroOffset->G = 0.13f + static_cast<float>((LayerHash >> 16u) & 0xffffu) / 65535.0f * 0.73f;
				MacroOffset->MaterialExpressionEditorX = -150;
				MacroOffset->MaterialExpressionEditorY = TextureSample->MaterialExpressionEditorY + 85;
				EditorData->ExpressionCollection.AddExpression(MacroOffset);

				UMaterialExpressionAdd* OffsetMacroCoords = NewObject<UMaterialExpressionAdd>(Material);
				OffsetMacroCoords->Material = Material;
				OffsetMacroCoords->A.Expression = MacroCoords;
				OffsetMacroCoords->B.Expression = MacroOffset;
				OffsetMacroCoords->MaterialExpressionEditorX = 0;
				OffsetMacroCoords->MaterialExpressionEditorY = TextureSample->MaterialExpressionEditorY + 35;
				EditorData->ExpressionCollection.AddExpression(OffsetMacroCoords);

				UMaterialExpressionTextureSampleParameter2D* MacroTextureSample =
					NewObject<UMaterialExpressionTextureSampleParameter2D>(Material);
				MacroTextureSample->Material = Material;
				MacroTextureSample->ParameterName = ParameterName;
				MacroTextureSample->Texture = TextureSample->Texture;
				MacroTextureSample->Coordinates.Expression = OffsetMacroCoords;
				MacroTextureSample->MaterialExpressionEditorX = 150;
				MacroTextureSample->MaterialExpressionEditorY = TextureSample->MaterialExpressionEditorY + 35;
				EditorData->ExpressionCollection.AddExpression(MacroTextureSample);

				UMaterialExpressionDesaturation* MacroLuminance =
					NewObject<UMaterialExpressionDesaturation>(Material);
				MacroLuminance->Material = Material;
				MacroLuminance->Input.Expression = MacroTextureSample;
				MacroLuminance->MaterialExpressionEditorX = 300;
				MacroLuminance->MaterialExpressionEditorY = TextureSample->MaterialExpressionEditorY + 35;
				EditorData->ExpressionCollection.AddExpression(MacroLuminance);

				UMaterialExpressionMultiply* TwiceStrength = NewObject<UMaterialExpressionMultiply>(Material);
				TwiceStrength->Material = Material;
				TwiceStrength->A.Expression = MacroVariationStrength;
				TwiceStrength->ConstB = 2.0f;
				TwiceStrength->MaterialExpressionEditorX = 300;
				TwiceStrength->MaterialExpressionEditorY = TextureSample->MaterialExpressionEditorY + 85;
				EditorData->ExpressionCollection.AddExpression(TwiceStrength);

				UMaterialExpressionMultiply* WeightedMacro = NewObject<UMaterialExpressionMultiply>(Material);
				WeightedMacro->Material = Material;
				WeightedMacro->A.Expression = MacroLuminance;
				WeightedMacro->B.Expression = TwiceStrength;
				WeightedMacro->MaterialExpressionEditorX = 450;
				WeightedMacro->MaterialExpressionEditorY = TextureSample->MaterialExpressionEditorY + 35;
				EditorData->ExpressionCollection.AddExpression(WeightedMacro);

				UMaterialExpressionOneMinus* MinimumBrightness = NewObject<UMaterialExpressionOneMinus>(Material);
				MinimumBrightness->Material = Material;
				MinimumBrightness->Input.Expression = MacroVariationStrength;
				MinimumBrightness->MaterialExpressionEditorX = 450;
				MinimumBrightness->MaterialExpressionEditorY = TextureSample->MaterialExpressionEditorY + 85;
				EditorData->ExpressionCollection.AddExpression(MinimumBrightness);

				UMaterialExpressionAdd* MacroMultiplier = NewObject<UMaterialExpressionAdd>(Material);
				MacroMultiplier->Material = Material;
				MacroMultiplier->A.Expression = WeightedMacro;
				MacroMultiplier->B.Expression = MinimumBrightness;
				MacroMultiplier->MaterialExpressionEditorX = 600;
				MacroMultiplier->MaterialExpressionEditorY = TextureSample->MaterialExpressionEditorY + 35;
				EditorData->ExpressionCollection.AddExpression(MacroMultiplier);

				UMaterialExpressionMultiply* AntiTiledLayer = NewObject<UMaterialExpressionMultiply>(Material);
				AntiTiledLayer->Material = Material;
				AntiTiledLayer->A.Expression = TextureSample;
				AntiTiledLayer->B.Expression = MacroMultiplier;
				AntiTiledLayer->Desc = BuildAntiTilingDescription(Input.LayerName);
				AntiTiledLayer->MaterialExpressionEditorX = 750;
				AntiTiledLayer->MaterialExpressionEditorY = TextureSample->MaterialExpressionEditorY;
				EditorData->ExpressionCollection.AddExpression(AntiTiledLayer);

				Input.LayerInput.Expression = AntiTiledLayer;
				bMaterialChanged = true;
			}
			LayoutGeneratedLayerNodes(TextureSample, Input.LayerInput.Expression, TextureLayerIndex);
			bMaterialChanged = true;
			++TextureLayerIndex;
		}
		LayerBlend->MaterialExpressionEditorX = 950;
		LayerBlend->MaterialExpressionEditorY = FMath::Max(0, (TextureLayerIndex - 1) * 200);

		TSet<FName> ExistingGrassNames;
		for (const FGrassInput& GrassInput : GrassOutput->GrassTypes)
		{
			ExistingGrassNames.Add(GrassInput.Name);
		}
		int32 GrassLayerIndex = 0;
		for (const FName LayerName : RequiredLayerNames)
		{
			if (LayerName.IsNone() || ExistingGrassNames.Contains(LayerName))
			{
				continue;
			}
			if (GrassOutput->GrassTypes.Num() >= UMaterialExpressionLandscapeGrassOutput::MaxGrassTypes)
			{
				OutResult.AddWarning(FString::Printf(
					TEXT("Landscape Grass Output supports at most %d entries; %d terrain layers were discovered."),
					UMaterialExpressionLandscapeGrassOutput::MaxGrassTypes, RequiredLayerNames.Num()));
				break;
			}

			UMaterialExpressionLandscapeLayerSample* LayerSample = LayerSamples.FindRef(LayerName);
			if (!LayerSample)
			{
				LayerSample = NewObject<UMaterialExpressionLandscapeLayerSample>(Material);
				LayerSample->Material = Material;
				LayerSample->ParameterName = LayerName;
				LayerSample->PreviewWeight = 0.0f;
				LayerSample->MaterialExpressionEditorX = 0;
				LayerSample->MaterialExpressionEditorY = 400 + GrassLayerIndex * 80;
				EditorData->ExpressionCollection.AddExpression(LayerSample);
				LayerSamples.Add(LayerName, LayerSample);
			}

			FGrassInput& GrassInput = GrassOutput->GrassTypes.AddDefaulted_GetRef();
			GrassInput.Name = LayerName;
			GrassInput.Input.Expression = LayerSample;
			ExistingGrassNames.Add(LayerName);
			++GrassLayerIndex;
			bMaterialChanged = true;
		}
		GrassOutput->MaterialExpressionEditorX = 1200;
		GrassOutput->MaterialExpressionEditorY = TextureLayerIndex * 400 + 250;
		GrassLayerIndex = 0;
		for (const FName LayerName : RequiredLayerNames)
		{
			if (UMaterialExpressionLandscapeLayerSample* LayerSample = LayerSamples.FindRef(LayerName))
			{
				LayerSample->MaterialExpressionEditorX = 950;
				LayerSample->MaterialExpressionEditorY = TextureLayerIndex * 400 + 250 + GrassLayerIndex * 90;
				++GrassLayerIndex;
			}
		}

		// Upgrade old generated materials to a directly usable diffuse landscape while preserving
		// any material graph that was customized by hand.
		if ((!EditorData->BaseColor.Expression || EditorData->BaseColor.Expression == LayerBlend)
			&& (!EditorData->MaterialAttributes.Expression || EditorData->MaterialAttributes.Expression == LayerBlend))
		{
			EditorData->MaterialAttributes.Expression = nullptr;
			EditorData->BaseColor.Expression = LayerBlend;
			Material->bUseMaterialAttributes = false;
			bMaterialChanged = true;
		}

		UMaterialExpressionScalarParameter* Roughness = EnsureScalarParameter(
			Material, EditorData, TEXT("TerrainRoughness"), 1.0f, 0, bMaterialChanged);
		UMaterialExpressionScalarParameter* Specular = EnsureScalarParameter(
			Material, EditorData, TEXT("TerrainSpecular"), 0.05f, 100, bMaterialChanged);
		if (!EditorData->Roughness.Expression)
		{
			EditorData->Roughness.Expression = Roughness;
			bMaterialChanged = true;
		}
		if (!EditorData->Specular.Expression)
		{
			EditorData->Specular.Expression = Specular;
			bMaterialChanged = true;
		}

		if (bMaterialChanged)
		{
			Material->PostEditChange();
			MaterialPackage->MarkPackageDirty();
			if (bCreatedMaterial)
			{
				FAssetRegistryModule::AssetCreated(Material);
				OutResult.CreatedPackages.Add(Paths.MaterialObject);
			}
			OutResult.AddInfo(FString::Printf(TEXT("Landscape material for %s contains %d layer(s)."),
				*Map.MapName, LayerBlend->Layers.Num()));
		}

		UPackage* InstancePackage = CreatePackage(*Paths.InstancePackage);
		UMaterialInstanceConstant* Instance =
			LoadObject<UMaterialInstanceConstant>(nullptr, *Paths.InstanceObject);
		const bool bCreatedInstance = Instance == nullptr;
		if (!Instance)
		{
			Instance = NewObject<UMaterialInstanceConstant>(InstancePackage, *Paths.InstanceAssetName,
				RF_Public | RF_Standalone | RF_Transactional);
		}
		if (!Instance)
		{
			OutResult.AddError(FString::Printf(TEXT("Failed to create landscape material instance for %s."), *Map.MapName));
			return nullptr;
		}

		if (bCreatedInstance || Instance->Parent != Material)
		{
			Instance->Modify();
			Instance->SetParentEditorOnly(Material);
			Instance->PostEditChange();
			InstancePackage->MarkPackageDirty();
			if (bCreatedInstance)
			{
				FAssetRegistryModule::AssetCreated(Instance);
				OutResult.CreatedPackages.Add(Paths.InstanceObject);
			}
		}
		return Instance;
	}
}

FMT2LandscapeMaterialImporter::FMT2LandscapeMaterialImporter()
	: FMT2ImporterBase(EMT2ImportDomain::LandscapeMaterials, TEXT("LandscapeMaterialImporter"))
{
}

bool FMT2LandscapeMaterialImporter::Import(const FMT2ImportRequest& Request, FMT2ImportResult& OutResult)
{
	if (!CanImport(Request, OutResult))
	{
		return false;
	}
	if (Request.Selection.MapTerrains.IsEmpty())
	{
		OutResult.AddWarning(TEXT("No map terrain records were selected for landscape material generation."));
		OutResult.bSucceeded = true;
		return true;
	}

	const int32 MaxItems = Request.MaxItems > 0
		? FMath::Min(Request.MaxItems, Request.Selection.MapTerrains.Num())
		: Request.Selection.MapTerrains.Num();
	FMT2PreparedMapTerrain UnusedPrepared;
	for (int32 MapIndex = 0; MapIndex < MaxItems; ++MapIndex)
	{
		const FMT2MapTerrainInfo& Map = Request.Selection.MapTerrains[MapIndex];
		++OutResult.ItemsDiscovered;
		if (Request.Context.bDryRun)
		{
			++OutResult.ItemsSkipped;
			OutResult.CreatedPackages.Add(BuildMaterialObjectPath(Request.Context, Map.MapName));
			OutResult.CreatedPackages.Add(BuildMaterialInstanceObjectPath(Request.Context, Map.MapName));
			continue;
		}
		if (CreateMaterialInstanceForPreparedMap(Request.Context, Map, UnusedPrepared, OutResult))
		{
			++OutResult.ItemsImported;
		}
		else
		{
			++OutResult.ItemsSkipped;
		}
	}
	OutResult.AddInfo(Request.Context.bDryRun
		? FString::Printf(TEXT("Dry run: would create or update %d map landscape material pair(s)."), MaxItems)
		: FString::Printf(TEXT("Created or updated %d map landscape material pair(s)."), OutResult.ItemsImported));
	OutResult.bSucceeded = !OutResult.HasErrors();
	return OutResult.bSucceeded;
}

UMaterialInterface* FMT2LandscapeMaterialImporter::CreateMaterialInstanceForPreparedMap(
	const FMT2ImportContext& Context, const FMT2MapTerrainInfo& Map,
	const FMT2PreparedMapTerrain&, FMT2ImportResult& OutResult)
{
	return EnsureMapLandscapeMaterial(Context, Map, OutResult);
}

FName FMT2LandscapeMaterialImporter::BuildLayerNameForTile(
	const FMT2ImportContext& Context, const FMT2MapTerrainInfo& Map, uint8 TileIndex)
{
	(void)Context;
	const int32 TextureSetIndex = static_cast<int32>(TileIndex) - 1;
	if (!Map.TextureSetTextures.IsValidIndex(TextureSetIndex))
	{
		return FName(*FString::Printf(TEXT("tile_%03d"), TileIndex));
	}

	auto BuildName = [](const FString& Reference)
	{
		return TEXT("T_") + FMT2AssetScanner::SanitizePackagePathSegment(FPaths::GetBaseFilename(Reference));
	};

	const FString& Reference = Map.TextureSetTextures[TextureSetIndex];
	const FString BaseName = BuildName(Reference);
	const FString NormalizedReference = FMT2TextureImporter::NormalizeReferencePath(Reference).ToLower();
	for (const FString& OtherReference : Map.TextureSetTextures)
	{
		if (BuildName(OtherReference).Equals(BaseName, ESearchCase::IgnoreCase)
			&& !FMT2TextureImporter::NormalizeReferencePath(OtherReference).Equals(
				NormalizedReference, ESearchCase::IgnoreCase))
		{
			return FName(*(BaseName + FString::Printf(TEXT("_%08x"), FCrc::StrCrc32(*NormalizedReference))));
		}
	}
	return FName(*BaseName);
}

FString FMT2LandscapeMaterialImporter::BuildMaterialObjectPath(
	const FMT2ImportContext& Context, const FString& MapName)
{
	return BuildPaths(Context, MapName).MaterialObject;
}

FString FMT2LandscapeMaterialImporter::BuildMaterialInstanceObjectPath(
	const FMT2ImportContext& Context, const FString& MapName)
{
	return BuildPaths(Context, MapName).InstanceObject;
}
