#include "MT2VisualWaterPaintEdMode.h"
#include "World/MT2MapPresentationActor.h"
#include "Config/MT2GameplaySettings.h"
#include "Editor.h"
#include "EditorModeManager.h"
#include "EditorViewportClient.h"
#include "Editor/Transactor.h"
#include "Engine/Selection.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "LandscapeProxy.h"
#include "SceneManagement.h"
#include "Toolkits/BaseToolkit.h"
#include "Toolkits/ToolkitManager.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Text/STextBlock.h"

const FEditorModeID FMT2VisualWaterPaintEdMode::ModeId = TEXT("EM_MT2VisualWaterPaint");

namespace
{
	class FVisualWaterToolkit : public FModeToolkit
	{
	public:
		explicit FVisualWaterToolkit(FMT2VisualWaterPaintEdMode* InMode) : Mode(InMode) {}
		virtual FName GetToolkitFName() const override { return TEXT("MT2VisualWaterPaint"); }
		virtual FText GetBaseToolkitName() const override { return FText::FromString(TEXT("Metin2 Visual Water")); }
		virtual FEdMode* GetEditorMode() const override { return Mode; }
		virtual TSharedPtr<SWidget> GetInlineContent() const override { return Content; }
		virtual void Init(const TSharedPtr<IToolkitHost>& Host) override
		{
			TSharedRef<SVerticalBox> Rows = SNew(SVerticalBox);
			Rows->AddSlot().AutoHeight().Padding(4)[SNew(STextBlock).AutoWrapText(true).Text(FText::FromString(TEXT("LMB adds water or sets height; Shift+LMB erases. E samples existing water height. Alt+mouse navigates; Ctrl+Z/Y undo/redo. Save changed actors normally. Server flags are not changed.")))];
			Rows->AddSlot().AutoHeight().Padding(4)[SNew(STextBlock).AutoWrapText(true).Text_Lambda([this] { return Mode->Status(); })];
			Rows->AddSlot().AutoHeight().Padding(4)[SNew(SButton).Text(FText::FromString(TEXT("Use selected map presentation"))).OnClicked_Lambda([this] { Mode->UseSelectedMap(); return FReply::Handled(); })];
			Rows->AddSlot().AutoHeight().Padding(4)[SNew(STextBlock).Text(FText::FromString(TEXT("Water height (world Z, cm; before global offset)")))];
			Rows->AddSlot().AutoHeight().Padding(4)[SNew(SNumericEntryBox<float>).MinValue(-1000000).MaxValue(1000000)
				.Value_Lambda([this] { return TOptional<float>(Mode->Height); })
				.OnValueChanged_Lambda([this](float Value) { if (FMath::IsFinite(Value)) { Mode->Height = FMath::Clamp(Value, -1000000.f, 1000000.f); } })];
			Rows->AddSlot().AutoHeight().Padding(4)[SNew(SButton).Text(FText::FromString(TEXT("Sample height at last cursor (E)"))).OnClicked_Lambda([this] { Mode->PickHeight(); return FReply::Handled(); })];
			Rows->AddSlot().AutoHeight().Padding(4)[SNew(STextBlock).Text(FText::FromString(TEXT("Brush radius (cm; 0 = one cell)")))];
			Rows->AddSlot().AutoHeight().Padding(4)[SNew(SNumericEntryBox<double>).MinValue(0).MaxValue(10000)
				.Value_Lambda([this] { return TOptional<double>(Mode->Radius); })
				.OnValueChanged_Lambda([this](double Value) { if (FMath::IsFinite(Value)) { Mode->Radius = FMath::Clamp(Value, 0.0, 10000.0); } })];
			Rows->AddSlot().AutoHeight().Padding(4)[SNew(SCheckBox)
				.IsChecked_Lambda([this] { return Mode->bErase ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([this](ECheckBoxState State) { Mode->bErase = State == ECheckBoxState::Checked; })
				[SNew(STextBlock).Text(FText::FromString(TEXT("Erase water")))]];
			Rows->AddSlot().AutoHeight().Padding(4)[SNew(SCheckBox)
				.IsChecked_Lambda([this] { return Mode->bWaterPlane ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([this](ECheckBoxState State) { Mode->bWaterPlane = State == ECheckBoxState::Checked; })
				[SNew(STextBlock).Text(FText::FromString(TEXT("Pick on water-height plane (works under bridges)")))]];
			Rows->AddSlot().AutoHeight().Padding(4)[SNew(SButton).Text(FText::FromString(TEXT("Reload water preview"))).OnClicked_Lambda([this] { Mode->Refresh(); return FReply::Handled(); })];
			Rows->AddSlot().AutoHeight().Padding(4)[SNew(STextBlock).AutoWrapText(true).Text(FText::FromString(TEXT("Native visual grid is preserved. Paint beneath banks; terrain hides submerged surfaces. Water Material is set in Project Settings > Metin2 Gameplay. Reimporting or rebaking visual water replaces these edits.")))];
			Content = SNew(SScrollBox) + SScrollBox::Slot()[Rows]; FModeToolkit::Init(Host);
		}
	private:
		FMT2VisualWaterPaintEdMode* Mode;
		TSharedPtr<SWidget> Content;
	};
}

void FMT2VisualWaterPaintEdMode::Enter()
{
	FEdMode::Enter(); Toolkit = MakeShared<FVisualWaterToolkit>(this); Toolkit->Init(Owner->GetToolkitHost());
	UseSelectedMap();
	if (!Target.IsValid()) { for (TActorIterator<AMT2MapPresentationActor> It(GetWorld()); It; ++It) { if (It->IsEditable()) { SelectTarget(*It); break; } } }
}
void FMT2VisualWaterPaintEdMode::Exit()
{
	FinishStroke();
	if (Toolkit.IsValid()) { FToolkitManager::Get().CloseToolkit(Toolkit.ToSharedRef()); Toolkit.Reset(); }
	Target.Reset(); Grid = FMT2VisualWaterGrid(); FEdMode::Exit();
}
void FMT2VisualWaterPaintEdMode::SelectTarget(AMT2MapPresentationActor* Map)
{
	FinishStroke(); Target = Map; bCursorValid = false; Grid = FMT2VisualWaterGrid(); Error.Reset();
	if (Map && Grid.Load(*Map, Error) && !Map->WaterRectangles.IsEmpty()) { Height = Map->WaterRectangles[0].Height; }
}
void FMT2VisualWaterPaintEdMode::UseSelectedMap()
{
	if (!GEditor || GEditor->PlayWorld) { return; }
	for (FSelectionIterator It(*GEditor->GetSelectedActors()); It; ++It)
		if (auto* Map = Cast<AMT2MapPresentationActor>(*It); Map && Map->GetWorld() == GetWorld() && Map->IsEditable()) { SelectTarget(Map); return; }
}
void FMT2VisualWaterPaintEdMode::UpdateCursor(FEditorViewportClient* Client)
{
	bCursorValid = false;
	if (!GEditor || GEditor->PlayWorld || !Client || !GetWorld() || Client->GetWorld() != GetWorld() || GetWorld()->WorldType != EWorldType::Editor || !Target.IsValid() || Grid.Size.X <= 0) { return; }
	const FViewportCursorLocation Ray = Client->GetCursorWorldLocationFromMousePos();
	const float Z = (Transaction.IsValid() ? StrokeHeight : Height) + GetDefault<UMT2GameplaySettings>()->WaterSurfaceOffset;
	if (bWaterPlane)
	{
		if (FMath::Abs(Ray.GetDirection().Z) < SMALL_NUMBER) { return; }
		const double T = (Z - Ray.GetOrigin().Z) / Ray.GetDirection().Z;
		if (T < 0 || T > 10000000) { return; }
		Cursor = Ray.GetOrigin() + Ray.GetDirection() * T; bCursorValid = Grid.IndexAt(FVector2D(Cursor)) != INDEX_NONE;
	}
	else
	{
		TArray<FHitResult> Hits;
		GetWorld()->LineTraceMultiByObjectType(Hits, Ray.GetOrigin(), Ray.GetOrigin() + Ray.GetDirection() * 10000000,
			FCollisionObjectQueryParams(ECC_WorldStatic), FCollisionQueryParams(SCENE_QUERY_STAT(MT2VisualWaterPaint), true));
		for (const FHitResult& Hit : Hits)
			if (Cast<ALandscapeProxy>(Hit.GetActor()) && Grid.IndexAt(FVector2D(Hit.ImpactPoint)) != INDEX_NONE)
			{ Cursor = FVector(Hit.ImpactPoint.X, Hit.ImpactPoint.Y, Z); bCursorValid = true; break; }
	}
}
bool FMT2VisualWaterPaintEdMode::MouseMove(FEditorViewportClient* Client, FViewport*, int32, int32)
{
	UpdateCursor(Client); if (Transaction.IsValid()) { Paint(); } Client->Invalidate(); return Transaction.IsValid();
}
bool FMT2VisualWaterPaintEdMode::CapturedMouseMove(FEditorViewportClient* Client, FViewport* Viewport, int32 X, int32 Y) { return MouseMove(Client, Viewport, X, Y); }
bool FMT2VisualWaterPaintEdMode::InputKey(FEditorViewportClient* Client, FViewport* Viewport, FKey Key, EInputEvent Event)
{
	const bool bControl = Viewport->KeyState(EKeys::LeftControl) || Viewport->KeyState(EKeys::RightControl);
	const bool bAlt = Viewport->KeyState(EKeys::LeftAlt) || Viewport->KeyState(EKeys::RightAlt);
	if (Transaction.IsValid() && Event == IE_Pressed && (Key == EKeys::Escape || bControl || bAlt)) { FinishStroke(); }
	if (Key == EKeys::E && Event == IE_Pressed && !bControl && !bAlt && !Transaction.IsValid()) { PickHeight(); Client->Invalidate(); return true; }
	if (Key == EKeys::LeftMouseButton && Event == IE_Released) { const bool Had = Transaction.IsValid(); FinishStroke(); return Had; }
	if (Key != EKeys::LeftMouseButton || Event != IE_Pressed || bControl || bAlt) { return false; }
	FinishStroke(); UpdateCursor(Client);
	if (!bCursorValid || !Target->IsEditable()) { return false; }
	// Reload at stroke boundaries to include external actor edits without per-frame grid decoding.
	if (!Grid.Load(*Target, Error)) { return true; }
	bStrokeErase = bErase || Viewport->KeyState(EKeys::LeftShift) || Viewport->KeyState(EKeys::RightShift);
	StrokeHeight = Height; StrokeRadius = Radius; InitialGridSize = Target->WaterGridSize;
	LastPaint = FVector2D(Cursor); LastFlushTime = 0;
	Transaction = MakeUnique<FScopedTransaction>(NSLOCTEXT("MT2VisualWater", "Paint", "Paint Metin2 visual water"));
	if (!GUndo) { Transaction->Cancel(); Transaction.Reset(); UE_LOG(LogTemp, Warning, TEXT("Visual water painting requires editor undo.")); return true; }
	Paint(); return true;
}
void FMT2VisualWaterPaintEdMode::Paint()
{
	if (!bCursorValid || !Transaction.IsValid()) { return; }
	FIntRect Dirty;
	if (Grid.Paint(LastPaint, FVector2D(Cursor), StrokeRadius, StrokeHeight, bStrokeErase, Before, Dirty))
	{
		if (PendingDirty.Width() <= 0) { PendingDirty = Dirty; }
		else { PendingDirty.Min = PendingDirty.Min.ComponentMin(Dirty.Min); PendingDirty.Max = PendingDirty.Max.ComponentMax(Dirty.Max); }
	}
	LastPaint = FVector2D(Cursor);
	if (FPlatformTime::Seconds() - LastFlushTime >= .1 && !Flush()) { FinishStroke(); }
}
bool FMT2VisualWaterPaintEdMode::Flush()
{
	LastFlushTime = FPlatformTime::Seconds();
	if (PendingDirty.Width() <= 0) { return true; }
	const bool Success = Target.IsValid() && Grid.Commit(*Target, PendingDirty, Error);
	PendingDirty = FIntRect(0, 0, 0, 0);
	if (!Success)
	{
		UE_LOG(LogTemp, Error, TEXT("Visual water paint could not save grid: %s"), *Error);
		// Retain only successfully committed cells when recording the stroke's undo change.
		FString LoadError; if (Target.IsValid()) { Grid.Load(*Target, LoadError); }
	}
	return Success;
}
void FMT2VisualWaterPaintEdMode::FinishStroke()
{
	if (!Transaction.IsValid()) { return; }
	Flush();
	if (Target.IsValid() && GUndo)
	{
		auto Change = MakeUnique<FMT2VisualWaterChange>(Grid, InitialGridSize, Before);
		if (!Change->IsEmpty()) { GUndo->StoreUndo(Target.Get(), MoveTemp(Change)); } else { Transaction->Cancel(); }
	}
	else { Transaction->Cancel(); }
	Transaction.Reset(); Before.Reset(); PendingDirty = FIntRect(0, 0, 0, 0);
}
bool FMT2VisualWaterPaintEdMode::LostFocus(FEditorViewportClient*, FViewport*) { FinishStroke(); return false; }
void FMT2VisualWaterPaintEdMode::Tick(FEditorViewportClient* Client, float Delta)
{
	FEdMode::Tick(Client, Delta);
	if (Transaction.IsValid())
	{
		if (!Target.IsValid() || (GEditor && GEditor->PlayWorld) || !Client->Viewport || !Client->Viewport->KeyState(EKeys::LeftMouseButton)) { FinishStroke(); }
		else if (FPlatformTime::Seconds() - LastFlushTime >= .1 && !Flush()) { FinishStroke(); }
	}
}
void FMT2VisualWaterPaintEdMode::PickHeight()
{
	if (!bCursorValid || Transaction.IsValid()) { return; }
	const int32 Index = Grid.IndexAt(FVector2D(Cursor));
	if (Grid.Heights.IsValidIndex(Index) && Grid.Heights[Index] != FMT2VisualWaterGrid::Empty) { Height = Grid.Heights[Index]; }
}
void FMT2VisualWaterPaintEdMode::Refresh()
{
	FinishStroke(); bCursorValid = false;
	if (Target.IsValid() && Grid.Load(*Target, Error)) { Target->RefreshWaterRendering(); }
}
FText FMT2VisualWaterPaintEdMode::Status() const
{
	if (!Error.IsEmpty()) { return FText::FromString(Error); }
	if (!Target.IsValid() || Grid.Size.X <= 0) { return FText::FromString(TEXT("Select a map-presentation actor and click Use selected map presentation.")); }
	const FVector2D Step = (Grid.WorldMax - Grid.WorldMin) / FVector2D(Grid.Size);
	return FText::FromString(FString::Printf(TEXT("%s | %d x %d cells | %.2f x %.2f cm\n%d cells touched in stroke"),
		*Target->GetActorLabel(), Grid.Size.X, Grid.Size.Y, Step.X, Step.Y, Before.Num()));
}
void FMT2VisualWaterPaintEdMode::Render(const FSceneView*, FViewport*, FPrimitiveDrawInterface* PDI)
{
	if (!bCursorValid || (GEditor && GEditor->PlayWorld)) { return; }
	const FVector2D Step = (Grid.WorldMax - Grid.WorldMin) / FVector2D(Grid.Size);
	const FVector Center(Cursor.X, Cursor.Y, (Transaction.IsValid() ? StrokeHeight : Height) + GetDefault<UMT2GameplaySettings>()->WaterSurfaceOffset + 2);
	DrawCircle(PDI, Center, FVector::ForwardVector, FVector::RightVector,
		(bErase || (Transaction.IsValid() && bStrokeErase)) ? FLinearColor::Red : FLinearColor(0, .6f, 1),
		FMath::Max(Transaction.IsValid() ? StrokeRadius : Radius, FMath::Max(Step.X, Step.Y) * .5), 64, SDPG_Foreground);
}
