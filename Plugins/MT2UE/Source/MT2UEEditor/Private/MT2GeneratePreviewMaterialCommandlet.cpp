/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "MT2GeneratePreviewMaterialCommandlet.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionOneMinus.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Misc/PackageName.h"
#include "UObject/SavePackage.h"

UMT2GeneratePreviewMaterialCommandlet::UMT2GeneratePreviewMaterialCommandlet()
{
	IsClient = false;
	IsEditor = true;
	IsServer = false;
	LogToConsole = true;
}

int32 UMT2GeneratePreviewMaterialCommandlet::Main(const FString& Params)
{
	static const FString PackageName = TEXT("/Game/UI/Materials/M_MT2CharacterPreviewComposite");
	static const FName AssetName(TEXT("M_MT2CharacterPreviewComposite"));
	UPackage* Package = LoadPackage(nullptr, *PackageName, LOAD_None);
	if (!Package)
	{
		Package = CreatePackage(*PackageName);
	}
	UMaterial* Material = FindObject<UMaterial>(Package, *AssetName.ToString());
	const bool bCreated = Material == nullptr;
	if (!Material)
	{
		Material = NewObject<UMaterial>(
			Package, AssetName, RF_Public | RF_Standalone | RF_Transactional);
	}
	UMaterialEditorOnlyData* Data = Material ? Material->GetEditorOnlyData() : nullptr;
	if (!Data)
	{
		UE_LOG(LogTemp, Error, TEXT("Could not create the character preview material."));
		return 1;
	}

	Material->Modify();
	Material->MaterialDomain = MD_UI;
	Material->BlendMode = BLEND_Translucent;
	Material->TwoSided = true;
	Data->ExpressionCollection.Empty();
	Data->EmissiveColor.Expression = nullptr;
	Data->Opacity.Expression = nullptr;

	UMaterialExpressionTextureSampleParameter2D* Texture =
		NewObject<UMaterialExpressionTextureSampleParameter2D>(Material);
	Texture->Material = Material;
	Texture->ParameterName = TEXT("PreviewTexture");
	Texture->Texture = LoadObject<UTexture>(
		nullptr, TEXT("/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture"));
	Texture->SamplerType = SAMPLERTYPE_Color;
	Texture->MaterialExpressionEditorX = -300;
	Data->ExpressionCollection.AddExpression(Texture);

	UMaterialExpressionOneMinus* Alpha = NewObject<UMaterialExpressionOneMinus>(Material);
	Alpha->Material = Material;
	Alpha->Input.Expression = Texture;
	Alpha->Input.OutputIndex = 4;
	Alpha->Input.Mask = 1;
	Alpha->Input.MaskA = 1;
	Alpha->MaterialExpressionEditorX = 0;
	Alpha->MaterialExpressionEditorY = 160;
	Data->ExpressionCollection.AddExpression(Alpha);

	Data->EmissiveColor.Expression = Texture;
	Data->Opacity.Expression = Alpha;
	Material->PostEditChange();
	Package->MarkPackageDirty();
	if (bCreated)
	{
		FAssetRegistryModule::AssetCreated(Material);
	}

	const FString Filename = FPackageName::LongPackageNameToFilename(
		PackageName, FPackageName::GetAssetPackageExtension());
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.SaveFlags = SAVE_NoError;
	if (!UPackage::SavePackage(Package, Material, *Filename, SaveArgs))
	{
		UE_LOG(LogTemp, Error, TEXT("Could not save %s."), *Filename);
		return 1;
	}

	UE_LOG(LogTemp, Display, TEXT("Created %s."), *PackageName);
	return 0;
}
