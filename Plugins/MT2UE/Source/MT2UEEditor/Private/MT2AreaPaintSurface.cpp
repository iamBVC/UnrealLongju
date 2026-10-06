#include "MT2AreaPaintSurface.h"
#include "MT2AreaPaintData.h"
#include "World/MT2MapPresentationActor.h"
#include "LandscapeComponent.h"
#include "LandscapeInfo.h"
#include "LandscapeProxy.h"
#include "LandscapeRender.h"
#include "LandscapeMaterialInstanceConstant.h"
#include "ConvexVolume.h"
#include "Engine/Texture2D.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionTextureObjectParameter.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionWorldPosition.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "RHIGlobals.h"

namespace
{
	UTexture2D* MakeFlagsTexture(FIntPoint Dimensions, const TArray<uint8>& Bytes)
	{
		UTexture2D* Texture = UTexture2D::CreateTransient(Dimensions.X, Dimensions.Y, PF_G8, NAME_None,
			TConstArrayView64<uint8>(Bytes.GetData(), Bytes.Num()));
		if (Texture)
		{
			Texture->SRGB = false; Texture->Filter = TF_Nearest;
			Texture->AddressX = TA_Clamp; Texture->AddressY = TA_Clamp;
			Texture->CompressionSettings = TC_Grayscale; Texture->NeverStream = true;
			Texture->UpdateResource();
		}
		return Texture;
	}
	FIntRect Intersection(FIntRect A, FIntRect B)
	{
		return FIntRect(FMath::Max(A.Min.X, B.Min.X), FMath::Max(A.Min.Y, B.Min.Y),
			FMath::Min(A.Max.X, B.Max.X), FMath::Min(A.Max.Y, B.Max.Y));
	}
}

UMaterial* FMT2AreaPaintSurface::CreateMaterial()
{
	UMaterial* Material = NewObject<UMaterial>(GetTransientPackage(), NAME_None, RF_Transient);
	// AlphaComposite keeps the explicit opacity override active in UE5's Substrate path.
	// Its color output must be premultiplied by that opacity.
	Material->BlendMode = BLEND_AlphaComposite; Material->TranslucencyPass = MTP_BeforeDOF;
	// Editor weighted-Z compositing forces BasePassPixelShader's output alpha to 1.
	// This is a regular translucent Landscape pass, not an editor-primitive composite.
	Material->bUsedWithEditorCompositing = false; Material->TwoSided = true;
	Material->SetShadingModel(MSM_Unlit);
	UMaterialEditorOnlyData* Data = Material->GetEditorOnlyData();
	auto Add = [&](UMaterialExpression* Expression) { Data->ExpressionCollection.AddExpression(Expression); };
	UMaterialExpressionTextureObjectParameter* Texture = NewObject<UMaterialExpressionTextureObjectParameter>(Material);
	Texture->ParameterName = TEXT("FlagsTexture"); Texture->SamplerType = SAMPLERTYPE_LinearGrayscale;
	Texture->Texture = MakeFlagsTexture(FIntPoint(1, 1), TArray<uint8>{0}); Add(Texture);
	UMaterialExpressionWorldPosition* Position = NewObject<UMaterialExpressionWorldPosition>(Material); Add(Position);
	UMaterialExpressionVectorParameter* Maximum = NewObject<UMaterialExpressionVectorParameter>(Material);
	Maximum->ParameterName = TEXT("GridMaximum"); Add(Maximum);
	UMaterialExpressionVectorParameter* Extent = NewObject<UMaterialExpressionVectorParameter>(Material);
	Extent->ParameterName = TEXT("GridExtent"); Extent->DefaultValue = FLinearColor(1, 1, 0, 0); Add(Extent);
	UMaterialExpressionScalarParameter* Visible = NewObject<UMaterialExpressionScalarParameter>(Material);
	Visible->ParameterName = TEXT("VisibleBits"); Visible->DefaultValue = 255; Add(Visible);
	UMaterialExpressionCustom* Color = NewObject<UMaterialExpressionCustom>(Material);
	Color->OutputType = CMOT_Float4;
	const TCHAR* Names[] = {TEXT("FlagsTexture"), TEXT("Position"), TEXT("Maximum"), TEXT("Extent"), TEXT("Visible")};
	UMaterialExpression* Expressions[] = {Texture, Position, Maximum, Extent, Visible};
	for (int32 I = 0; I < 5; ++I) { FCustomInput Input; Input.InputName = Names[I]; Input.Input.Expression = Expressions[I]; Color->Inputs.Add(Input); }
	Color->Code = TEXT("float2 uv = (Maximum.xy - Position.xy) / Extent.xy;\n")
		TEXT("if (any(uv < 0) || any(uv >= 1)) return float4(0,0,0,0);\n")
		TEXT("uint flags = (uint)round(Texture2DSampleLevel(FlagsTexture, FlagsTextureSampler, uv, 0).r * 255.0) & (uint)Visible;\n")
		TEXT("float3 color = float3(0,0,0); float count = 0;\n");
	for (int32 Bit = 0; Bit < 8; ++Bit)
	{
		const FLinearColor RGB = MT2AreaPaint::BitColor(Bit);
		Color->Code += FString::Printf(TEXT("if ((flags & %du) != 0u) { color += float3(%.9f,%.9f,%.9f); count += 1; }\n"), 1 << Bit, RGB.R, RGB.G, RGB.B);
	}
	Color->Code += TEXT("float opacity = count > 0 ? 0.5 : 0.0;\n")
		TEXT("return float4((color / max(count,1.0)) * opacity, opacity);\n"); Add(Color);
	UMaterialExpressionComponentMask* RGB = NewObject<UMaterialExpressionComponentMask>(Material);
	RGB->Input.Expression = Color; RGB->R = RGB->G = RGB->B = true; RGB->A = false; Add(RGB);
	UMaterialExpressionComponentMask* Alpha = NewObject<UMaterialExpressionComponentMask>(Material);
	Alpha->Input.Expression = Color; Alpha->R = Alpha->G = Alpha->B = false; Alpha->A = true; Add(Alpha);
	UMaterialExpressionConstant3Vector* Offset = NewObject<UMaterialExpressionConstant3Vector>(Material);
	Offset->Constant = FLinearColor(0, 0, 4); Add(Offset);
	Data->EmissiveColor.Expression = RGB; Data->Opacity.Expression = Alpha; Data->WorldPositionOffset.Expression = Offset;
	Material->PostEditChange();
	return Material;
}

TArray<uint8> FMT2AreaPaintSurface::CopyFlags(const AMT2MapPresentationActor& Map, FIntRect Rect)
{
	TArray<uint8> Bytes;
	if (!MT2AreaPaint::IsValidGrid(&Map) || Rect.Min.X < 0 || Rect.Min.Y < 0 ||
		Rect.Max.X > Map.Attributes.Size.X || Rect.Max.Y > Map.Attributes.Size.Y || Rect.Width() <= 0 || Rect.Height() <= 0) { return Bytes; }
	Bytes.SetNumUninitialized(Rect.Width() * Rect.Height());
	for (int32 Y = 0; Y < Rect.Height(); ++Y)
	{
		FMemory::Memcpy(Bytes.GetData() + Y * Rect.Width(), Map.Attributes.Flags.GetData() + (Rect.Min.Y + Y) * Map.Attributes.Size.X + Rect.Min.X, Rect.Width());
	}
	return Bytes;
}

ULandscapeMaterialInstanceConstant* FMT2AreaPaintSurface::CreateLandscapeMaterial(UMaterial& Material)
{
	ULandscapeMaterialInstanceConstant* Parent = NewObject<ULandscapeMaterialInstanceConstant>(GetTransientPackage(), NAME_None, RF_Transient);
	Parent->bEditorToolUsage = true;
	Parent->SetParentEditorOnly(&Material);
	Parent->PostEditChange();
	return Parent;
}

void FMT2AreaPaintSurface::Restore(FBinding& Binding)
{
	ULandscapeComponent* Component = Binding.Component.Get();
	if (Component && Component->EditToolRenderData.ToolMaterial == Binding.Material)
	{
		Component->EditToolRenderData.ToolMaterial = Binding.PreviousMaterial;
		Component->UpdateEditToolRenderData();
	}
}
void FMT2AreaPaintSurface::Clear()
{
	for (FBinding& Binding : Bindings) { Restore(Binding); }
	Bindings.Reset(); LandscapeMaterial = nullptr; CurrentMap.Reset(); DirtyCells = FIntRect(0, 0, 0, 0);
	if (bOwnsEditModeFlag) { GLandscapeEditModeActive = bPreviousEditModeFlag; bOwnsEditModeFlag = false; }
}
void FMT2AreaPaintSurface::InvalidateCells(FIntRect Rect)
{
	if (Rect.Width() <= 0 || Rect.Height() <= 0) { return; }
	if (DirtyCells.Width() <= 0 || DirtyCells.Height() <= 0) { DirtyCells = Rect; }
	else { DirtyCells.Min.X = FMath::Min(DirtyCells.Min.X, Rect.Min.X); DirtyCells.Min.Y = FMath::Min(DirtyCells.Min.Y, Rect.Min.Y);
		DirtyCells.Max.X = FMath::Max(DirtyCells.Max.X, Rect.Max.X); DirtyCells.Max.Y = FMath::Max(DirtyCells.Max.Y, Rect.Max.Y); }
}
void FMT2AreaPaintSurface::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(LandscapeMaterial);
	for (FBinding& Binding : Bindings)
	{
		Collector.AddReferencedObject(Binding.Texture); Collector.AddReferencedObject(Binding.Material); Collector.AddReferencedObject(Binding.PreviousMaterial);
	}
}

void FMT2AreaPaintSurface::Sync(AMT2MapPresentationActor& Map, ALandscapeProxy& Terrain, UMaterial& Material, uint8 VisibleBits)
{
	check(IsInGameThread());
	if (!MT2AreaPaint::IsValidGrid(&Map)) { Clear(); return; }
	if (CurrentMap != &Map || Size != Map.Attributes.Size || WorldMin != Map.WorldMin || WorldMax != Map.WorldMax)
	{
		Clear(); CurrentMap = &Map; Size = Map.Attributes.Size; WorldMin = Map.WorldMin; WorldMax = Map.WorldMax;
	}
	ULandscapeInfo* Info = Terrain.GetLandscapeInfo();
	if (!Info) { Clear(); return; }
	if (!LandscapeMaterial) { LandscapeMaterial = CreateLandscapeMaterial(Material); }
	if (!bOwnsEditModeFlag) { bPreviousEditModeFlag = GLandscapeEditModeActive; GLandscapeEditModeActive = true; bOwnsEditModeFlag = true; }
	TSet<ULandscapeComponent*> Loaded;
	for (const auto& Pair : Info->XYtoComponentMap) { if (IsValid(Pair.Value) && Pair.Value->IsRegistered()) { Loaded.Add(Pair.Value); } }
	for (int32 I = Bindings.Num() - 1; I >= 0; --I)
	{
		if (!Loaded.Contains(Bindings[I].Component.Get()) || Bindings[I].Bounds != Bindings[I].Component->Bounds.GetBox())
		{ Restore(Bindings[I]); Bindings.RemoveAtSwap(I); }
	}
	const FVector2D Cell = (WorldMax - WorldMin) / FVector2D(Size);
	for (ULandscapeComponent* Component : Loaded)
	{
		if (Bindings.ContainsByPredicate([&](const FBinding& Binding) { return Binding.Component == Component; })) { continue; }
		const FIntRect Rect = MT2AreaPaint::VisibleGridRect(Map, TArray<FBox>{Component->Bounds.GetBox()}, FConvexVolume());
		if (Rect.Width() <= 0 || Rect.Height() <= 0) { continue; }
		if (uint32(Rect.Width()) > GetMax2DTextureDimension() || uint32(Rect.Height()) > GetMax2DTextureDimension())
		{ UE_LOG(LogTemp, Error, TEXT("Area preview component exceeds GPU texture dimensions; grid is not resampled.")); continue; }
		FBinding Binding; Binding.Component = Component; Binding.Bounds = Component->Bounds.GetBox(); Binding.Cells = Rect;
		Binding.Texture = MakeFlagsTexture(Rect.Size(), CopyFlags(Map, Rect));
		if (!Binding.Texture) { UE_LOG(LogTemp, Error, TEXT("Could not create native area-preview texture.")); continue; }
		Binding.Material = UMaterialInstanceDynamic::Create(LandscapeMaterial, GetTransientPackage()); Binding.Material->SetFlags(RF_Transient);
		Binding.Material->SetTextureParameterValue(TEXT("FlagsTexture"), Binding.Texture);
		const FVector2D Maximum = WorldMax - FVector2D(Rect.Min) * Cell, Extent = FVector2D(Rect.Size()) * Cell;
		Binding.Material->SetVectorParameterValue(TEXT("GridMaximum"), FLinearColor(Maximum.X, Maximum.Y, 0, 0));
		Binding.Material->SetVectorParameterValue(TEXT("GridExtent"), FLinearColor(Extent.X, Extent.Y, 0, 0));
		Binding.Material->SetScalarParameterValue(TEXT("VisibleBits"), VisibleBits);
		Binding.PreviousMaterial = Component->EditToolRenderData.ToolMaterial;
		Component->EditToolRenderData.ToolMaterial = Binding.Material; Component->UpdateEditToolRenderData();
		Bindings.Add(MoveTemp(Binding));
	}
	bool bPendingUpload = false;
	for (FBinding& Binding : Bindings)
	{
		if (LastVisibleBits != VisibleBits) { Binding.Material->SetScalarParameterValue(TEXT("VisibleBits"), VisibleBits); }
		const FIntRect Changed = Intersection(Binding.Cells, DirtyCells);
		if (Changed.Width() <= 0 || Changed.Height() <= 0) { continue; }
		if (!Binding.Texture->GetResource()) { bPendingUpload = true; continue; }
		const TArray<uint8> Bytes = CopyFlags(Map, Changed);
		uint8* Upload = new uint8[Bytes.Num()]; FMemory::Memcpy(Upload, Bytes.GetData(), Bytes.Num());
		FUpdateTextureRegion2D* Region = new FUpdateTextureRegion2D(Changed.Min.X - Binding.Cells.Min.X, Changed.Min.Y - Binding.Cells.Min.Y, 0, 0, Changed.Width(), Changed.Height());
		Binding.Texture->UpdateTextureRegions(0, 1, Region, Changed.Width(), 1, Upload,
			[](uint8* Data, const FUpdateTextureRegion2D* Area) { delete[] Data; delete Area; });
	}
	LastVisibleBits = VisibleBits;
	if (!bPendingUpload) { DirtyCells = FIntRect(0, 0, 0, 0); }
}
