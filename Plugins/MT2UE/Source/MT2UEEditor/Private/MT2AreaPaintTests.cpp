#if WITH_DEV_AUTOMATION_TESTS
#include "MT2AreaPaintData.h"
#include "MT2AreaPaintEdMode.h"
#include "Materials/Material.h"
#include "ConvexVolume.h"
#include "World/MT2MapPresentationActor.h"
#include "Engine/World.h"
#include "Editor.h"
#include "Editor/Transactor.h"
#include "ScopedTransaction.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMT2AreaBrushTest, "Metin2.Editor.AreaPaint.Brush", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMT2AreaBrushTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("World"), World)) { return false; }
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	AMT2MapPresentationActor* Map = World->SpawnActor<AMT2MapPresentationActor>();
	Map->WorldMin = FVector2D(0, -150); Map->WorldMax = FVector2D(250, 0);
	Map->Attributes.Size = FIntPoint(5, 3); Map->Attributes.Flags.Init(0x82, 15);
	TMap<int32, uint8> Before;
	const FVector2D Point(240, -10);
	TestEqual(TEXT("Zero radius changes exactly the mirrored source cell"), MT2AreaPaint::PaintSegment(*Map, Point, Point, 0, 2, false, Before), 1);
	TestEqual(TEXT("Overlapping water/object retained"), Map->Attributes.Flags[0], uint8(0x86));
	TestEqual(TEXT("Untouched cell retained"), Map->Attributes.Flags[1], uint8(0x82));
	TestEqual(TEXT("Repeated paint is idempotent"), MT2AreaPaint::PaintSegment(*Map, Point, Point, 0, 2, false, Before), 0);
	TestEqual(TEXT("Stroke retains its original byte"), Before[0], uint8(0x82));
	for (int32 Bit = 0; Bit < 8; ++Bit) { MT2AreaPaint::PaintSegment(*Map, Point, Point, 0, Bit, false, Before); }
	TestEqual(TEXT("Every byte bit is paintable"), Map->Attributes.Flags[0], uint8(0xff));
	MT2AreaPaint::PaintSegment(*Map, Point, Point, 0, 2, true, Before);
	TestEqual(TEXT("Erase only selected bit"), Map->Attributes.Flags[0], uint8(0xfb));
	Map->Attributes.Flags.Init(0, 15); Before.Reset();
	TestEqual(TEXT("Off-center thin drag visits every crossed cell"), MT2AreaPaint::PaintSegment(*Map, Point, FVector2D(10, -10), 0, 0, false, Before), 5);
	for (int32 I = 0; I < 15; ++I) { TestEqual(TEXT("Thin stroke stays in its row"), Map->Attributes.Flags[I], uint8(I < 5 ? 1 : 0)); }
	TestEqual(TEXT("Exclusive edge does not paint"), MT2AreaPaint::PaintSegment(*Map, FVector2D(0, -10), FVector2D(0, -100), 0, 0, false, Before), 0);
	Map->Attributes.Flags.Init(0, 15); Before.Reset();
	TestEqual(TEXT("Finite radius sweeps without missing intermediate cells"), MT2AreaPaint::PaintSegment(*Map, FVector2D(225, -75), FVector2D(25, -75), 20, 7, false, Before), 5);
	TestEqual(TEXT("Native resolution remains unchanged"), Map->Attributes.Size, FIntPoint(5, 3));
	Map->Attributes.Flags.Init(0, 15); Before.Reset();
	TestEqual(TEXT("Outside zero-radius drag clips to grid"), MT2AreaPaint::PaintSegment(*Map, FVector2D(300, -125), FVector2D(-50, -125), 0, 6, false, Before), 5);
	TestEqual(TEXT("Outside brush cannot mutate grid"), MT2AreaPaint::PaintSegment(*Map, FVector2D(600, 600), FVector2D(800, 800), 10, 0, false, Before), 0);
	TestEqual(TEXT("Negative radius rejected"), MT2AreaPaint::PaintSegment(*Map, Point, Point, -1, 0, false, Before), 0);
	TestEqual(TEXT("Invalid bit rejected"), MT2AreaPaint::PaintSegment(*Map, Point, Point, 1, 8, false, Before), 0);
	Map->Attributes.Size.X = 6;
	TestFalse(TEXT("Malformed grid rejected"), MT2AreaPaint::IsValidGrid(Map));
	TestEqual(TEXT("Malformed grid is not painted"), MT2AreaPaint::PaintSegment(*Map, Point, Point, 500, 0, false, Before), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMT2AreaChangeTest, "Metin2.Editor.AreaPaint.SparseUndoAndSerialization", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMT2AreaChangeTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("World"), World)) { return false; }
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	AMT2MapPresentationActor* Map = World->SpawnActor<AMT2MapPresentationActor>();
	Map->WorldMin = FVector2D(0, -100); Map->WorldMax = FVector2D(100, 0);
	Map->Attributes.Size = FIntPoint(2, 2); Map->Attributes.Flags = {0x82, 0x10, 0, 0};
	TMap<int32, uint8> Before;
	MT2AreaPaint::PaintSegment(*Map, FVector2D(75, -25), FVector2D(75, -25), 0, 2, false, Before);
	FMT2AreaPaintChange Change(*Map, 2, Before);
	TestFalse(TEXT("Changed stroke has undo data"), Change.IsEmpty());
	Map->Attributes.Flags[0] |= 0x20;
	Change.Revert(Map);
	TestEqual(TEXT("Undo preserves independently changed bits"), Map->Attributes.Flags[0], uint8(0xa2));
	Change.Apply(Map);
	TestEqual(TEXT("Redo restores only painted bit"), Map->Attributes.Flags[0], uint8(0xa6));
	TestEqual(TEXT("Undo leaves other cells untouched"), Map->Attributes.Flags[1], uint8(0x10));
	TArray<uint8> Bytes;
	FMemoryWriter Writer(Bytes);
	FMT2MapAttributes::StaticStruct()->SerializeItem(Writer, &Map->Attributes, nullptr);
	FMT2MapAttributes Loaded;
	FMemoryReader Reader(Bytes);
	FMT2MapAttributes::StaticStruct()->SerializeItem(Reader, &Loaded, nullptr);
	TestEqual(TEXT("Reflected serialization preserves native dimensions"), Loaded.Size, Map->Attributes.Size);
	TestTrue(TEXT("Reflected serialization preserves every bit"), Loaded.Flags == Map->Attributes.Flags);
	Map->Attributes.Size = FIntPoint(4, 1);
	TestTrue(TEXT("Changed dimensions expire sparse changes"), Change.HasExpired(Map));
	const TArray<uint8> Prior = Map->Attributes.Flags;
	Change.Revert(Map);
	TestTrue(TEXT("Expired change cannot corrupt a replacement grid"), Map->Attributes.Flags == Prior);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMT2AreaTransactionTest, "Metin2.Editor.AreaPaint.EditorTransaction", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMT2AreaTransactionTest::RunTest(const FString& Parameters)
{
	if (!TestNotNull(TEXT("Editor"), GEditor)) { return false; }
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("World"), World)) { return false; }
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	AMT2MapPresentationActor* Map = World->SpawnActor<AMT2MapPresentationActor>();
	Map->WorldMin = FVector2D(0, -50); Map->WorldMax = FVector2D(50, 0);
	Map->Attributes.Size = FIntPoint(1, 1); Map->Attributes.Flags = {0x82};
	{
		FScopedTransaction Transaction(FText::FromString(TEXT("Area paint automation transaction")));
		if (!TestNotNull(TEXT("Active transaction"), GUndo)) { Transaction.Cancel(); return false; }
		TMap<int32, uint8> Before;
		MT2AreaPaint::PaintSegment(*Map, FVector2D(25, -25), FVector2D(25, -25), 0, 2, false, Before);
		GUndo->StoreUndo(Map, MakeUnique<FMT2AreaPaintChange>(*Map, 2, Before));
	}
	TestEqual(TEXT("Applied editor stroke"), Map->Attributes.Flags[0], uint8(0x86));
	GEditor->UndoTransaction();
	TestEqual(TEXT("Editor undo applies sparse command"), Map->Attributes.Flags[0], uint8(0x82));
	GEditor->RedoTransaction();
	TestEqual(TEXT("Editor redo applies sparse command"), Map->Attributes.Flags[0], uint8(0x86));
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMT2AreaPreviewMaterialTest, "Metin2.Editor.AreaPaint.PreviewMaterial", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMT2AreaPreviewMaterialTest::RunTest(const FString& Parameters)
{
	FMT2AreaPaintEdMode Mode;
	Mode.CreatePreviewMaterial();
	UMaterial* Material = Mode.PreviewMaterial.Get();
	if (!TestNotNull(TEXT("Transient preview material"), Material)) { return false; }
	TestEqual(TEXT("PDI meshes must use standard translucency, not skipped After DOF"), Material->TranslucencyPass.GetValue(), MTP_BeforeDOF);
	TestTrue(TEXT("Editor compositing shaders requested"), bool(Material->bUsedWithEditorCompositing));
	TestTrue(TEXT("Preview is translucent and two-sided"), Material->BlendMode == BLEND_Translucent && Material->TwoSided);
	TestTrue(TEXT("Preview has a connected vertex-color output"), Material->GetEditorOnlyData()->EmissiveColor.Expression != nullptr);
	TestTrue(TEXT("Preview has connected opacity"), Material->GetEditorOnlyData()->Opacity.Expression != nullptr);
	TestTrue(TEXT("Preview is not a saved material asset"), Material->HasAnyFlags(RF_Transient));
	const FProperty* Size = FMT2MapAttributes::StaticStruct()->FindPropertyByName(TEXT("Size"));
	TestTrue(TEXT("Dimensions visible and read-only in Details"), Size && Size->HasAllPropertyFlags(CPF_Edit | CPF_EditConst));
	const FProperty* Flags = FMT2MapAttributes::StaticStruct()->FindPropertyByName(TEXT("Flags"));
	TestTrue(TEXT("Millions of flag entries are not expanded in Details"), Flags && !Flags->HasAnyPropertyFlags(CPF_Edit));
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMT2AreaViewportCoverageTest, "Metin2.Editor.AreaPaint.ViewportCoverage", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMT2AreaViewportCoverageTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("World"), World)) { return false; }
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	AMT2MapPresentationActor* Map = World->SpawnActor<AMT2MapPresentationActor>();
	Map->WorldMin = FVector2D(0, -150); Map->WorldMax = FVector2D(250, 0);
	Map->Attributes.Size = FIntPoint(5, 3); Map->Attributes.Flags.Init(4, 15);
	FConvexVolume Frustum;
	Frustum.Planes.Add(FPlane(1, 0, 0, 250)); Frustum.Planes.Add(FPlane(-1, 0, 0, 0));
	Frustum.Planes.Add(FPlane(0, 1, 0, 0)); Frustum.Planes.Add(FPlane(0, -1, 0, 150));
	Frustum.Planes.Add(FPlane(0, 0, 1, 1000)); Frustum.Planes.Add(FPlane(0, 0, -1, 1000));
	Frustum.Init();
	TArray<FBox> Boxes = {FBox(FVector(0, -150, -100), FVector(250, 0, 100))};
	TestTrue(TEXT("Visible terrain covers full grid without any cursor dependency"), MT2AreaPaint::VisibleGridRect(*Map, Boxes, Frustum) == FIntRect(0, 0, 5, 3));
	Boxes = {FBox(FVector(100, -100, -100), FVector(200, 0, 100)), FBox(FVector(500, 500, 0), FVector(600, 600, 100))};
	TestTrue(TEXT("Mirrored cell bounds and offscreen terrain filtering"), MT2AreaPaint::VisibleGridRect(*Map, Boxes, Frustum) == FIntRect(1, 0, 3, 2));
	Boxes = {FBox(FVector(500, 500, 0), FVector(600, 600, 100))};
	TestTrue(TEXT("No visible terrain produces an empty preview"), MT2AreaPaint::VisibleGridRect(*Map, Boxes, Frustum) == FIntRect(0, 0, 0, 0));
	Boxes = {FBox(FVector(-100, -250, -100), FVector(350, 100, 100))};
	TestTrue(TEXT("Component coverage is clipped to map bounds"), MT2AreaPaint::VisibleGridRect(*Map, Boxes, Frustum) == FIntRect(0, 0, 5, 3));
	FConvexVolume CloseView = Frustum;
	CloseView.Planes.Add(FPlane(1, 0, 0, 150)); CloseView.Planes.Add(FPlane(-1, 0, 0, -100)); CloseView.Init();
	TestTrue(TEXT("Zoomed view clips a large component to original cells"), MT2AreaPaint::VisibleGridRect(*Map, Boxes, CloseView) == FIntRect(2, 0, 3, 3));
	Map->Attributes.Size = FIntPoint(2048, 2560); Map->Attributes.Flags.Init(4, 2048 * 2560);
	Map->WorldMin = FVector2D(0, -128000); Map->WorldMax = FVector2D(102400, 0);
	FConvexVolume Unrestricted;
	Boxes = {FBox(FVector(0, -128000, -100), FVector(102400, 0, 100))};
	TestTrue(TEXT("Overview covers beyond the old 6000-unit cursor cap"), MT2AreaPaint::VisibleGridRect(*Map, Boxes, Unrestricted) == FIntRect(0, 0, 2048, 2560));
	TestEqual(TEXT("Viewport LOD never changes native resolution"), Map->Attributes.Size, FIntPoint(2048, 2560));
	return true;
}
#endif
