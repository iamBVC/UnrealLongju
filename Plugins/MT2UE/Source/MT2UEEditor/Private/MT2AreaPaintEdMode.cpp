#include "MT2AreaPaintEdMode.h"

#include "MT2AreaPaintData.h"
#include "World/MT2MapPresentationActor.h"
#include "LandscapeProxy.h"
#include "LandscapeInfo.h"
#include "LandscapeComponent.h"
#include "DynamicMeshBuilder.h"
#include "Editor.h"
#include "EditorModeManager.h"
#include "EditorViewportClient.h"
#include "Editor/Transactor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionVertexColor.h"
#include "Materials/MaterialExpressionConstant.h"
#include "SceneView.h"
#include "SceneManagement.h"
#include "Toolkits/BaseToolkit.h"
#include "Toolkits/ToolkitManager.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Text/STextBlock.h"

const FEditorModeID FMT2AreaPaintEdMode::ModeId = TEXT("EM_MT2AreaPaint");

namespace
{
	class FAreaPaintToolkit : public FModeToolkit
	{
	public:
		explicit FAreaPaintToolkit(FMT2AreaPaintEdMode* InMode) : Mode(InMode) {}
		virtual FName GetToolkitFName() const override { return TEXT("MT2AreaPaint"); }
		virtual FText GetBaseToolkitName() const override { return NSLOCTEXT("MT2Areas", "Title", "Metin2 Areas"); }
		virtual FEdMode* GetEditorMode() const override { return Mode; }
		virtual TSharedPtr<SWidget> GetInlineContent() const override { return Content; }
		virtual void Init(const TSharedPtr<IToolkitHost>& Host) override
		{
			TSharedRef<SVerticalBox> Rows = SNew(SVerticalBox);
			Rows->AddSlot().AutoHeight().Padding(4)[SNew(STextBlock).AutoWrapText(true)
				.Text(FText::FromString(TEXT("Areas cover the loaded landscape visible in this viewport. Hover terrain to paint its grid. LMB paints; Shift+LMB erases. Alt+mouse navigates. Save the map/changed actors normally. No terrain textures are changed.")))];
			Rows->AddSlot().AutoHeight().Padding(4)[SNew(STextBlock).AutoWrapText(true)
				.Text_Lambda([this] { return Mode->Status(); })];
			Rows->AddSlot().AutoHeight().Padding(4)[SNew(STextBlock).Text(FText::FromString(TEXT("Paint bit (button) / visible bits (checkboxes)")))];
			for (int32 Bit = 0; Bit < 8; ++Bit)
			{
				Rows->AddSlot().AutoHeight().Padding(2)[SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth()[SNew(SCheckBox)
						.IsChecked_Lambda([this, Bit] { return (Mode->VisibleBits & (1 << Bit)) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
						.OnCheckStateChanged_Lambda([this, Bit](ECheckBoxState State) { Mode->VisibleBits = State == ECheckBoxState::Checked ? uint8(Mode->VisibleBits | (1 << Bit)) : uint8(Mode->VisibleBits & ~(1 << Bit)); })]
					+ SHorizontalBox::Slot().FillWidth(1)[SNew(SButton)
						.ButtonColorAndOpacity_Lambda([this, Bit] { return MT2AreaPaint::BitColor(Bit) * (Mode->Bit == Bit ? 1.0f : .4f); })
						.Text(MT2AreaPaint::BitName(Bit))
						.OnClicked_Lambda([this, Bit] { Mode->Bit = Bit; return FReply::Handled(); })]];
			}
			Rows->AddSlot().AutoHeight().Padding(4)[SNew(STextBlock).Text(FText::FromString(TEXT("Brush radius (world units; 0 = one cell)")))];
			Rows->AddSlot().AutoHeight().Padding(4)[SNew(SNumericEntryBox<double>)
				.MinValue(0).MaxValue(10000).Value_Lambda([this] { return TOptional<double>(Mode->Radius); })
				.OnValueChanged_Lambda([this](double Value) { Mode->Radius = FMath::Clamp(Value, 0.0, 10000.0); })];
			Rows->AddSlot().AutoHeight().Padding(4)[SNew(SCheckBox)
				.IsChecked_Lambda([this] { return Mode->bErase ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([this](ECheckBoxState State) { Mode->bErase = State == ECheckBoxState::Checked; })
				[SNew(STextBlock).Text(FText::FromString(TEXT("Erase selected bit")))]];
			Rows->AddSlot().AutoHeight().Padding(4)[SNew(SCheckBox)
				.IsChecked_Lambda([this] { return Mode->bShowOverlay ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([this](ECheckBoxState State) { Mode->bShowOverlay = State == ECheckBoxState::Checked; })
				[SNew(STextBlock).Text(FText::FromString(TEXT("Show colored overlay")))]];
			Rows->AddSlot().AutoHeight().Padding(4)[SNew(SButton).Text(FText::FromString(TEXT("Refresh terrain overlay")))
				.OnClicked_Lambda([this] { Mode->RefreshPreview(); return FReply::Handled(); })];
			Rows->AddSlot().AutoHeight().Padding(4)[SNew(STextBlock).AutoWrapText(true)
				.Text(FText::FromString(TEXT("Overlap colors mix. Close views show individual cells; distant preview aggregates only its display, never the saved grid. Reserved bits have no invented gameplay behavior. Importing attributes again replaces authored changes.")))];
			Content = SNew(SScrollBox) + SScrollBox::Slot()[Rows];
			FModeToolkit::Init(Host);
		}
	private:
		FMT2AreaPaintEdMode* Mode;
		TSharedPtr<SWidget> Content;
	};
}

void FMT2AreaPaintEdMode::Enter()
{
	FEdMode::Enter();
	Toolkit = MakeShared<FAreaPaintToolkit>(this);
	Toolkit->Init(Owner->GetToolkitHost());
	CreatePreviewMaterial();
}
void FMT2AreaPaintEdMode::Exit()
{
	FinishStroke();
	if (Toolkit.IsValid()) { FToolkitManager::Get().CloseToolkit(Toolkit.ToSharedRef()); Toolkit.Reset(); }
	Preview.Reset(); Target.Reset(); Landscape.Reset(); PreviewMaterial = nullptr;
	FEdMode::Exit();
}
void FMT2AreaPaintEdMode::AddReferencedObjects(FReferenceCollector& Collector)
{
	FEdMode::AddReferencedObjects(Collector);
	Collector.AddReferencedObject(PreviewMaterial);
}
void FMT2AreaPaintEdMode::CreatePreviewMaterial()
{
	PreviewMaterial = NewObject<UMaterial>(GetTransientPackage(), NAME_None, RF_Transient);
	PreviewMaterial->BlendMode = BLEND_Translucent;
	// PDI view meshes are submitted in the standard translucency pass, not After DOF.
	PreviewMaterial->TranslucencyPass = MTP_BeforeDOF;
	PreviewMaterial->bUsedWithEditorCompositing = true;
	PreviewMaterial->TwoSided = true;
	PreviewMaterial->SetShadingModel(MSM_Unlit);
	UMaterialExpressionVertexColor* Color = NewObject<UMaterialExpressionVertexColor>(PreviewMaterial);
	UMaterialExpressionConstant* Opacity = NewObject<UMaterialExpressionConstant>(PreviewMaterial);
	Opacity->R = .45f;
	UMaterialEditorOnlyData* Data = PreviewMaterial->GetEditorOnlyData();
	Data->ExpressionCollection.AddExpression(Color);
	Data->ExpressionCollection.AddExpression(Opacity);
	Data->EmissiveColor.Expression = Color;
	Data->Opacity.Expression = Opacity;
	PreviewMaterial->PostEditChange();
}

void FMT2AreaPaintEdMode::UpdateCursor(FEditorViewportClient* Client, FViewport* Viewport)
{
	bCursorValid = false;
	if (!GEditor || GEditor->PlayWorld || !Client || !GetWorld() || Client->GetWorld() != GetWorld() || GetWorld()->WorldType != EWorldType::Editor) { return; }
	const FViewportCursorLocation Ray = Client->GetCursorWorldLocationFromMousePos();
	TArray<FHitResult> Hits;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(MT2AreaPaint), true);
	GetWorld()->LineTraceMultiByObjectType(Hits, Ray.GetOrigin(), Ray.GetOrigin() + Ray.GetDirection() * 10000000,
		FCollisionObjectQueryParams(ECC_WorldStatic), Params);
	for (const FHitResult& Hit : Hits)
	{
		ALandscapeProxy* HitLandscape = Cast<ALandscapeProxy>(Hit.GetActor());
		if (!HitLandscape) { continue; }
		for (TActorIterator<AMT2MapPresentationActor> It(GetWorld()); It; ++It)
		{
			uint8 Flags;
			if (!MT2AreaPaint::IsValidGrid(*It) || !It->IsEditable() ||
				!It->Attributes.Query(Hit.ImpactPoint, It->WorldMin, It->WorldMax, Flags)) { continue; }
			if (Target != *It || Landscape != HitLandscape) { RefreshPreview(); }
			Target = *It; Landscape = HitLandscape; Cursor = Hit.ImpactPoint; bCursorValid = true;
			return;
		}
	}
}
bool FMT2AreaPaintEdMode::MouseMove(FEditorViewportClient* Client, FViewport* Viewport, int32, int32)
{
	UpdateCursor(Client, Viewport);
	if (Transaction.IsValid()) { Paint(); }
	Client->Invalidate();
	return Transaction.IsValid();
}
bool FMT2AreaPaintEdMode::CapturedMouseMove(FEditorViewportClient* Client, FViewport* Viewport, int32 X, int32 Y)
{
	return MouseMove(Client, Viewport, X, Y);
}
bool FMT2AreaPaintEdMode::InputKey(FEditorViewportClient* Client, FViewport* Viewport, FKey Key, EInputEvent Event)
{
	if (Transaction.IsValid() && Event == IE_Pressed && (Key == EKeys::Escape ||
		Viewport->KeyState(EKeys::LeftControl) || Viewport->KeyState(EKeys::RightControl))) { FinishStroke(); }
	if (Key == EKeys::LeftMouseButton && Event == IE_Released)
	{
		const bool bHadStroke = Transaction.IsValid(); FinishStroke(); return bHadStroke;
	}
	if (Key == EKeys::LeftMouseButton && Event == IE_Pressed &&
		!Viewport->KeyState(EKeys::LeftAlt) && !Viewport->KeyState(EKeys::RightAlt) &&
		!Viewport->KeyState(EKeys::LeftControl) && !Viewport->KeyState(EKeys::RightControl))
	{
		UpdateCursor(Client, Viewport);
		if (!bCursorValid) { return false; }
		FinishStroke();
		StrokeTarget = Target; StrokeSize = Target->Attributes.Size; StrokeBit = Bit;
		bStrokeErase = bErase || Viewport->KeyState(EKeys::LeftShift) || Viewport->KeyState(EKeys::RightShift);
		LastPaint = FVector2D(Cursor);
		Transaction = MakeUnique<FScopedTransaction>(NSLOCTEXT("MT2Areas", "Paint", "Paint Metin2 areas"));
		if (!GUndo)
		{
			UE_LOG(LogTemp, Warning, TEXT("Metin2 area painting requires an active editor undo transaction."));
			FinishStroke(); return true;
		}
		Paint(); return true;
	}
	return FEdMode::InputKey(Client, Viewport, Key, Event);
}
void FMT2AreaPaintEdMode::Paint()
{
	AMT2MapPresentationActor* Map = StrokeTarget.Get();
	if (!Transaction.IsValid()) { return; }
	if (!Map || !GEditor || GEditor->PlayWorld || !bCursorValid || Target != Map || Map->Attributes.Size != StrokeSize)
	{
		FinishStroke(); return;
	}
	if (MT2AreaPaint::PaintSegment(*Map, LastPaint, FVector2D(Cursor), Radius, StrokeBit, bStrokeErase, Before)) { bPreviewFlagsDirty = true; }
	LastPaint = FVector2D(Cursor);
}
void FMT2AreaPaintEdMode::FinishStroke()
{
	if (!Transaction.IsValid()) { return; }
	AMT2MapPresentationActor* Map = StrokeTarget.Get();
	if (Map && Map->Attributes.Size == StrokeSize && GUndo)
	{
		TUniquePtr<FMT2AreaPaintChange> Change = MakeUnique<FMT2AreaPaintChange>(*Map, StrokeBit, Before);
		if (!Change->IsEmpty()) { GUndo->StoreUndo(Map, MoveTemp(Change)); }
		else { Transaction->Cancel(); }
	}
	else { Transaction->Cancel(); }
	Transaction.Reset(); Before.Reset(); StrokeTarget.Reset();
}
bool FMT2AreaPaintEdMode::LostFocus(FEditorViewportClient* Client, FViewport* Viewport)
{
	FinishStroke(); return FEdMode::LostFocus(Client, Viewport);
}
void FMT2AreaPaintEdMode::Tick(FEditorViewportClient* Client, float DeltaTime)
{
	FEdMode::Tick(Client, DeltaTime);
	if (GEditor && GEditor->PlayWorld) { FinishStroke(); bCursorValid = false; }
	if (Transaction.IsValid() && Client && Client->Viewport && !Client->Viewport->KeyState(EKeys::LeftMouseButton)) { FinishStroke(); }
}
FText FMT2AreaPaintEdMode::Status() const
{
	const AMT2MapPresentationActor* Map = Target.Get();
	if (!MT2AreaPaint::IsValidGrid(Map)) { return FText::FromString(TEXT("No valid area grid selected. Import map attributes first, then hover loaded landscape.")); }
	const FVector2D Cell = (Map->WorldMax - Map->WorldMin) / FVector2D(Map->Attributes.Size);
	uint8 Flags = 0;
	Map->Attributes.Query(Cursor, Map->WorldMin, Map->WorldMax, Flags);
	return FText::FromString(FString::Printf(TEXT("%s | %d x %d cells | %.2f x %.2f world units\nCursor flags: 0x%02X | %d cells touched in stroke\nPreview: %d terrain quads%s"),
		*Map->MapId, Map->Attributes.Size.X, Map->Attributes.Size.Y, Cell.X, Cell.Y, Flags, Before.Num(), Preview.Num(),
		bPreviewBuilt && Preview.IsEmpty() ? TEXT(" (no terrain samples; load terrain and refresh)") : TEXT("")));
}

void FMT2AreaPaintEdMode::Render(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI)
{
	FEdMode::Render(View, Viewport, PDI);
	AMT2MapPresentationActor* Map = Target.Get();
	ALandscapeProxy* Terrain = Landscape.Get();
	if (!GEditor || GEditor->PlayWorld || !GetWorld() || !bShowOverlay || !PreviewMaterial) { return; }
	auto TerrainBounds = [](ALandscapeProxy* Proxy)
	{
		TArray<FBox> Boxes;
		ULandscapeInfo* Info = Proxy ? Proxy->GetLandscapeInfo() : nullptr;
		if (Info)
		{
			for (const auto& Pair : Info->XYtoComponentMap)
			{
				if (IsValid(Pair.Value) && Pair.Value->IsRegistered()) { Boxes.Add(Pair.Value->Bounds.GetBox()); }
			}
		}
		return Boxes;
	};
	// Choose a visible map even before the first mouse hover. Cursor picking remains independent.
	if (!MT2AreaPaint::IsValidGrid(Map) || !Terrain)
	{
		Terrain = nullptr;
		for (TActorIterator<AMT2MapPresentationActor> It(GetWorld()); It && !Terrain; ++It)
		{
			if (!MT2AreaPaint::IsValidGrid(*It)) { continue; }
			for (TActorIterator<ALandscapeProxy> Proxy(GetWorld()); Proxy; ++Proxy)
			{
				const FIntRect Visible = MT2AreaPaint::VisibleGridRect(**It, TerrainBounds(*Proxy), View->ViewFrustum);
				if (Visible.Width() <= 0 || Visible.Height() <= 0) { continue; }
				Map = *It; Terrain = *Proxy; Target = Map; Landscape = Terrain; RefreshPreview(); break;
			}
		}
	}
	if (!MT2AreaPaint::IsValidGrid(Map) || !Terrain) { return; }
	const FIntPoint Size = Map->Attributes.Size;
	const FVector2D Cell = (Map->WorldMax - Map->WorldMin) / FVector2D(Size);
	// Cover every loaded terrain component intersecting this view, without a cursor-radius cap.
	const TArray<FBox> Boxes = TerrainBounds(Terrain);
	const FIntRect VisibleRect = MT2AreaPaint::VisibleGridRect(*Map, Boxes, View->ViewFrustum);
	if (VisibleRect.Width() <= 0 || VisibleRect.Height() <= 0) { RefreshPreview(); return; }
	// Quantized coverage contains the entire view and avoids resampling for tiny camera moves.
	const FIntRect Rect(VisibleRect.Min.X / 16 * 16, VisibleRect.Min.Y / 16 * 16,
		FMath::Min((VisibleRect.Max.X + 15) / 16 * 16, Size.X), FMath::Min((VisibleRect.Max.Y + 15) / 16 * 16, Size.Y));
	uint32 TerrainHash = 0;
	for (const FBox& Box : Boxes) { TerrainHash = HashCombineFast(TerrainHash, HashCombineFast(GetTypeHash(Box.Min), GetTypeHash(Box.Max))); }
	const int32 MinX = Rect.Min.X, MinY = Rect.Min.Y, MaxX = Rect.Max.X, MaxY = Rect.Max.Y;
	const int32 Step = FMath::Max(1, FMath::CeilToInt(FMath::Sqrt(double(Rect.Width()) * Rect.Height() / 16384.0)));
	if (!bPreviewBuilt || Rect != PreviewRect || Step != PreviewStep || Size != PreviewSize || TerrainHash != PreviewTerrainHash)
	{
		Preview.Reset(); PreviewRect = Rect; PreviewStep = Step; PreviewSize = Size; PreviewTerrainHash = TerrainHash; bPreviewBuilt = true; bPreviewFlagsDirty = true;
		for (int32 Y = MinY; Y < MaxY; Y += Step)
		{
			for (int32 X = MinX; X < MaxX; X += Step)
			{
				FPreviewCell Quad;
				Quad.Cells = FIntRect(X, Y, FMath::Min(X + Step, MaxX), FMath::Min(Y + Step, MaxY));
				const FIntPoint Corners[] = {{X, Y}, {Quad.Cells.Max.X, Y}, Quad.Cells.Max, {X, Quad.Cells.Max.Y}};
				bool bValid = true;
				for (int32 I = 0; I < 4; ++I)
				{
					const FVector2D XY = Map->WorldMax - FVector2D(Corners[I]) * Cell;
					const TOptional<float> Height = Terrain->GetHeightAtLocation(FVector(XY, 0));
					if (!Height.IsSet()) { bValid = false; break; }
					Quad.Corners[I] = FVector(XY, Height.GetValue() + 4.0f);
				}
				if (bValid) { Preview.Add(Quad); }
			}
		}
	}
	FDynamicMeshBuilder Mesh(View->GetFeatureLevel());
	bool bHasTriangles = false;
	for (FPreviewCell& Quad : Preview)
	{
		if (bPreviewFlagsDirty)
		{
			Quad.Flags = 0;
			for (int32 Y = Quad.Cells.Min.Y; Y < Quad.Cells.Max.Y; ++Y)
				for (int32 X = Quad.Cells.Min.X; X < Quad.Cells.Max.X; ++X) { Quad.Flags |= Map->Attributes.Flags[Y * Size.X + X]; }
		}
		const uint8 Flags = Quad.Flags & VisibleBits;
		if (!Flags) { continue; }
		FLinearColor Color(0, 0, 0, 0); int32 Count = 0;
		for (int32 BitIndex = 0; BitIndex < 8; ++BitIndex) { if (Flags & (1 << BitIndex)) { Color += MT2AreaPaint::BitColor(BitIndex); ++Count; } }
		const FColor VertexColor = (Color / Count).ToFColor(true);
		const int32 V = Mesh.AddVertex(FVector3f(Quad.Corners[0]), FVector2f::ZeroVector, FVector3f(1, 0, 0), FVector3f(0, 1, 0), FVector3f(0, 0, 1), VertexColor);
		for (int32 I = 1; I < 4; ++I) { Mesh.AddVertex(FVector3f(Quad.Corners[I]), FVector2f::ZeroVector, FVector3f(1, 0, 0), FVector3f(0, 1, 0), FVector3f(0, 0, 1), VertexColor); }
		Mesh.AddTriangle(V, V + 1, V + 2); Mesh.AddTriangle(V, V + 2, V + 3); bHasTriangles = true;
	}
	bPreviewFlagsDirty = false;
	if (bHasTriangles) { Mesh.Draw(PDI, FMatrix::Identity, PreviewMaterial->GetRenderProxy(), SDPG_World, true, false); }
	if (bCursorValid)
	{
		DrawCircle(PDI, Cursor + FVector(0, 0, 6), FVector::XAxisVector, FVector::YAxisVector,
			MT2AreaPaint::BitColor(Bit), FMath::Max(Radius, FMath::Min(Cell.X, Cell.Y) * .5), 64, SDPG_World);
	}
}
