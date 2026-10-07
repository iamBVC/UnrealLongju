#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "World/MT2MapWater.h"
#include "World/MT2MapAttributes.h"
#include "World/MT2MapPresentationActor.h"
#include "Config/MT2GameplaySettings.h"
#include "ProceduralMeshComponent.h"
#include "Materials/Material.h"
#include "Engine/World.h"
#include "Misc/Crc.h"
#include "Misc/ScopeExit.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMT2WaterBakeTest, "Metin2.World.Water.Bake",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMT2WaterBakeTest::RunTest(const FString& Parameters)
{
	FMT2MapAttributes Attributes; Attributes.Size = FIntPoint(4, 3);
	Attributes.Flags = {2, 2, 0, 2, 2, 6, 0, 2, 0, 2, 0, 0};
	TArray<FMT2WaterRectangle> Rects; FString Error;
	TestTrue(TEXT("Water bit including safezone overlap"), MT2MapWater::Bake(Attributes,
		[](int32 X, int32 Y, float& Z) { Z = X == 3 ? 200.f : 100.f; return true; }, Rects, Error));
	TArray<int32> Coverage; Coverage.Init(0, Attributes.Flags.Num());
	for (const auto& Rect : Rects)
	{
		for (int32 Y = Rect.Cell.Y; Y < Rect.Cell.Y + Rect.Size.Y; ++Y)
			for (int32 X = Rect.Cell.X; X < Rect.Cell.X + Rect.Size.X; ++X) { ++Coverage[Y * Attributes.Size.X + X]; }
	}
	for (int32 Index = 0; Index < Coverage.Num(); ++Index)
	{
		TestEqual(TEXT("Exactly one quad covering each water cell; no dry cells"), Coverage[Index], (Attributes.Flags[Index] & 2) ? 1 : 0);
	}
	TestEqual(TEXT("Vertical runs merged"), Rects.Num(), 3);
	TestEqual(TEXT("Separate water level preserved"), Rects[1].Height, 200.f);
	const int32 PreviousCount = Rects.Num();
	TestFalse(TEXT("Missing height rejected atomically"), MT2MapWater::Bake(Attributes,
		[](int32, int32, float&) { return false; }, Rects, Error));
	TestEqual(TEXT("Previous bake preserved on failure"), Rects.Num(), PreviousCount);
	Attributes.Size = FIntPoint(129, 1); Attributes.Flags.Init(2, 129);
	TestTrue(TEXT("Large water surface"), MT2MapWater::Bake(Attributes,
		[](int32, int32, float& Z) { Z = 10; return true; }, Rects, Error));
	TestEqual(TEXT("Never merge across chunk boundary"), Rects.Num(), 2);
	TestEqual(TEXT("Full-resolution final cell"), Rects[1].Size.X, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMT2WaterGeometryTest, "Metin2.World.Water.Geometry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMT2WaterGeometryTest::RunTest(const FString& Parameters)
{
	FMT2WaterRectangle Rect; Rect.Cell = FIntPoint(1, 2); Rect.Size = FIntPoint(1, 1); Rect.Height = 200.f;
	TArray<FVector> V; TArray<int32> I; TArray<FVector2D> UV;
	MT2MapWater::AppendQuad(Rect, FIntPoint(4, 4), FVector2D(0, -200), FVector2D(200, 0), FVector::ZeroVector, 1.f, 100.f, V, I, UV);
	TestEqual(TEXT("Mirrored cell origin matches attribute query"), V[0], FVector(150, -100, 201));
	TestEqual(TEXT("Exact 50-unit cell footprint"), V[2], FVector(100, -150, 201));
	TestTrue(TEXT("Triangle winding matches Unreal's upward-facing procedural grid"),
		FVector::CrossProduct(V[I[1]] - V[I[0]], V[I[2]] - V[I[0]]).Z < 0);
	TestEqual(TEXT("World-continuous UVs"), UV[0], FVector2D(1.5, 1));
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMT2WaterComponentTest, "Metin2.World.Water.Component",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMT2WaterComponentTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("World"), World)) { return false; }
	UMT2GameplaySettings* Settings = GetMutableDefault<UMT2GameplaySettings>();
	const auto PreviousMaterial = Settings->WaterMaterial;
	ON_SCOPE_EXIT { Settings->WaterMaterial = PreviousMaterial; World->DestroyWorld(false); };
	UMaterial* Material = NewObject<UMaterial>(GetTransientPackage());
	Settings->WaterMaterial = Material;
	AMT2MapPresentationActor* Map = World->SpawnActor<AMT2MapPresentationActor>();
	if (!TestNotNull(TEXT("Map presentation"), Map)) { return false; }
	Map->Attributes.Size = FIntPoint(129, 1); Map->Attributes.Flags.Init(2, 129);
	Map->WaterGridSize = Map->Attributes.Size; Map->WorldMax = FVector2D(6450, 50);
	Map->WaterAttributeCRC = FCrc::MemCrc32(Map->Attributes.Flags.GetData(), Map->Attributes.Flags.Num());
	FString Error;
	MT2MapWater::Bake(Map->Attributes, [](int32, int32, float& Z) { Z = 100.f; return true; }, Map->WaterRectangles, Error);
	Map->SetActorLocation(FVector(1000, 1000, 200));
	for (int32 Attempt = 0; Attempt < 2; ++Attempt)
	{
		Map->RefreshWaterRendering();
		TArray<UProceduralMeshComponent*> Meshes; Map->GetComponents(Meshes);
		TestEqual(TEXT("Rebuild replaces rather than leaks culling chunks"), Meshes.Num(), 2);
		for (UProceduralMeshComponent* Mesh : Meshes)
		{
			TestEqual(TEXT("Configured material applied"), Mesh->GetMaterial(0), static_cast<UMaterialInterface*>(Material));
			TestEqual(TEXT("No collision"), Mesh->GetCollisionEnabled(), ECollisionEnabled::NoCollision);
			TestFalse(TEXT("No network replication"), Mesh->GetIsReplicated());
			const FProcMeshSection* Section = Mesh->GetProcMeshSection(0);
			if (TestNotNull(TEXT("Mesh section exists"), Section))
			{
				TestEqual(TEXT("Mesh does not move twice with presentation actor"),
					Mesh->GetComponentTransform().TransformPosition(FVector(Section->ProcVertexBuffer[0].Position)).Z,
					double(100.f + Settings->WaterSurfaceOffset));
			}
		}
	}
	Map->WaterRectangles.Reset(); Map->RefreshWaterRendering();
	TArray<UProceduralMeshComponent*> Meshes; Map->GetComponents(Meshes);
	TestEqual(TEXT("Dry map clears generated components"), Meshes.Num(), 0);
	return true;
}
#if WITH_EDITOR
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMT2WaterEditorRefreshTest, "Metin2.World.Water.EditorRefresh",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMT2WaterEditorRefreshTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Editor, false);
	if (!TestNotNull(TEXT("Editor world"), World)) { return false; }
	UMT2GameplaySettings* Settings = GetMutableDefault<UMT2GameplaySettings>();
	const auto Previous = Settings->WaterMaterial;
	ON_SCOPE_EXIT { World->DestroyWorld(false); Settings->WaterMaterial = Previous; };
	UMaterial* Material = NewObject<UMaterial>(GetTransientPackage()); Settings->WaterMaterial = Material;
	AMT2MapPresentationActor* Map = World->SpawnActor<AMT2MapPresentationActor>();
	Map->Attributes.Size = FIntPoint(1, 1); Map->Attributes.Flags = {2}; Map->WaterGridSize = Map->Attributes.Size;
	Map->WorldMin = FVector2D(0, -100); Map->WorldMax = FVector2D(100, 0);
	Map->WaterAttributeCRC = FCrc::MemCrc32(Map->Attributes.Flags.GetData(), Map->Attributes.Flags.Num());
	FMT2WaterRectangle Rect; Rect.Size = FIntPoint(1, 1); Map->WaterRectangles.Add(Rect);
	UMT2MapWaterComponent* Water = Map->FindComponentByClass<UMT2MapWaterComponent>();
	Water->UnregisterComponent(); Water->RegisterComponent();
	TArray<UProceduralMeshComponent*> Meshes; Map->GetComponents(Meshes);
	TestEqual(TEXT("Re-registration restores loaded-map preview without OnConstruction"), Meshes.Num(), 1);
	UMaterial* Replacement = NewObject<UMaterial>(GetTransientPackage()); Settings->WaterMaterial = Replacement;
	FPropertyChangedEvent Event(FindFProperty<FProperty>(UMT2GameplaySettings::StaticClass(),
		GET_MEMBER_NAME_CHECKED(UMT2GameplaySettings, WaterMaterial)));
	Settings->OnSettingChanged().Broadcast(Settings, Event);
	Meshes.Reset(); Map->GetComponents(Meshes);
	if (TestEqual(TEXT("Project setting edit rebuilds existing preview"), Meshes.Num(), 1))
	{
		TestEqual(TEXT("New material is applied immediately"), Meshes[0]->GetMaterial(0), static_cast<UMaterialInterface*>(Replacement));
	}
	Water->UnregisterComponent(); Settings->OnSettingChanged().Broadcast(Settings, Event);
	Meshes.Reset(); Map->GetComponents(Meshes);
	TestEqual(TEXT("Unregister clears chunks and removes the settings callback"), Meshes.Num(), 0);
	return true;
}
#endif
#endif
