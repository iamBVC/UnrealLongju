#pragma once
#if WITH_DEV_AUTOMATION_TESTS
#include "World/MT2MapPresentationActor.h"
#include "Config/MT2GameplaySettings.h"
#include "ProceduralMeshComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "MaterialShared.h"
#include "Misc/AutomationTest.h"
#include "Misc/App.h"
#include "Misc/Crc.h"
#include "Misc/ScopeExit.h"
#include "RenderingThread.h"
#include "SceneInterface.h"
#include "RHICommandList.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMT2WaterVisibilityTest, "Metin2.World.Water.RenderedVisibility",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMT2WaterVisibilityTest::RunTest(const FString& Parameters)
{
	if (!FApp::CanEverRender()) { AddWarning(TEXT("Water GPU visibility requires a real RHI; skipped under NullRHI.")); return true; }
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("Capture world"), World)) { return false; }
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	UMT2GameplaySettings* Settings = GetMutableDefault<UMT2GameplaySettings>();
	const auto PreviousMaterial = Settings->WaterMaterial;
	ON_SCOPE_EXIT { Settings->WaterMaterial = PreviousMaterial; GEngine->DestroyWorldContext(World); World->DestroyWorld(false); FlushRenderingCommands(); };
	TStrongObjectPtr<UMaterial> Material(NewObject<UMaterial>(GetTransientPackage()));
	Material->TwoSided = false; Material->SetShadingModel(MSM_Unlit);
	UMaterialExpressionConstant3Vector* Color = NewObject<UMaterialExpressionConstant3Vector>(Material.Get());
	Color->Constant = FLinearColor::Green;
	Material->GetExpressionCollection().AddExpression(Color);
	Material->GetEditorOnlyData()->EmissiveColor.Expression = Color;
	Material->PostEditChange();
	FMaterialResource* Resource = Material->GetMaterialResource(GMaxRHIShaderPlatform);
	if (!TestNotNull(TEXT("Test material resource"), Resource)) { return false; }
	Resource->FinishCompilation();
	Settings->WaterMaterial = Material.Get();
	AMT2MapPresentationActor* Map = World->SpawnActor<AMT2MapPresentationActor>();
	Map->WorldMin = FVector2D(0, -100); Map->WorldMax = FVector2D(100, 0);
	Map->Attributes.Size = FIntPoint(1, 1); Map->Attributes.Flags = {MT2MapAttribute::Water};
	Map->WaterGridSize = Map->Attributes.Size;
	Map->WaterAttributeCRC = FCrc::MemCrc32(Map->Attributes.Flags.GetData(), Map->Attributes.Flags.Num());
	FMT2WaterRectangle Rect; Rect.Size = FIntPoint(1, 1); Map->WaterRectangles.Add(Rect);
	Map->RefreshWaterRendering();
	TArray<UProceduralMeshComponent*> Meshes; Map->GetComponents(Meshes);
	if (!TestEqual(TEXT("Generated water chunk"), Meshes.Num(), 1)) { return false; }
	TStrongObjectPtr<UTextureRenderTarget2D> Target(NewObject<UTextureRenderTarget2D>());
	Target->ClearColor = FLinearColor::Black; Target->InitCustomFormat(32, 32, PF_FloatRGBA, true);
	Target->UpdateResourceImmediate(true);
	AActor* Camera = World->SpawnActor<AActor>();
	USceneCaptureComponent2D* Capture = NewObject<USceneCaptureComponent2D>(Camera);
	Camera->SetRootComponent(Capture);
	Capture->bCaptureEveryFrame = false; Capture->bCaptureOnMovement = false;
	Capture->TextureTarget = Target.Get(); Capture->CaptureSource = SCS_SceneDepth;
	Capture->ProjectionType = ECameraProjectionMode::Perspective; Capture->FOVAngle = 45.f;
	Capture->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
	Capture->ShowFlags.SetPostProcessing(false);
	Capture->ShowFlags.SetFog(false);
	Capture->ShowOnlyComponent(Meshes[0]);
	Capture->RegisterComponent();
	World->SendAllEndOfFrameUpdates();
	FlushRenderingCommands();
	ENQUEUE_RENDER_COMMAND(UpdateWaterCapturePrimitives)([Scene = World->Scene](FRHICommandListImmediate& RHICmdList)
	{
		Scene->UpdateAllPrimitiveSceneInfos(RHICmdList);
	});
	FlushRenderingCommands();
	auto Sample = [&](float Z, float Pitch)
	{
		Capture->SetWorldLocationAndRotation(FVector(50, -50, Z), FRotator(Pitch, 0, 0));
		Capture->CaptureScene(); FlushRenderingCommands();
		TArray<FLinearColor> Pixels;
		const bool bRead = Target->GameThread_GetRenderTargetResource()->ReadLinearColorPixels(Pixels);
		TestTrue(TEXT("Actual GPU pixels read back"), bRead && Pixels.Num() == 1024);
		return Pixels.Num() == 1024 ? Pixels[16 * 32 + 16] : FLinearColor::Black;
	};
	const FLinearColor Above = Sample(200.f, -90.f);
	const FLinearColor Below = Sample(-200.f, 90.f);
	Material->TwoSided = true; Material->PostEditChange();
	Material->GetMaterialResource(GMaxRHIShaderPlatform)->FinishCompilation();
	const FLinearColor Control = Sample(200.f, -90.f);
	AddInfo(FString::Printf(TEXT("Capture control=%s bounds=%s visible=%d registered=%d hidden=%d sceneproxy=%d capturevisible=%d camera=%s"),
		*Control.ToString(), *Meshes[0]->Bounds.GetBox().ToString(), Meshes[0]->IsVisible(), Meshes[0]->IsRegistered(),
		Map->IsHidden(), Meshes[0]->SceneProxy != nullptr, Capture->IsVisible(), *Capture->GetComponentTransform().ToHumanReadableString()));
	TestTrue(TEXT("Two-sided control verifies capture fixture"), Control.R > 100.f && Control.R < 250.f);
	TestTrue(FString::Printf(TEXT("One-sided water visible from above: depth %s"), *Above.ToString()), Above.R > 100.f && Above.R < 250.f);
	TestTrue(FString::Printf(TEXT("Underside culled: depth %s"), *Below.ToString()), Below.R > 1000.f);
	return true;
}
#endif
