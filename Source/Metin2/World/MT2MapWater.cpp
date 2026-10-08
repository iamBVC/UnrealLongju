#include "World/MT2MapWater.h"
#include "World/MT2MapPresentationActor.h"
#include "Config/MT2GameplaySettings.h"
#include "Materials/MaterialInterface.h"
#include "ProceduralMeshComponent.h"
#include "Engine/World.h"
#include "UObject/UnrealType.h"

DEFINE_LOG_CATEGORY_STATIC(LogMT2Water, Log, All);

bool MT2MapWater::Bake(const FMT2MapAttributes& Attributes,
	TFunctionRef<bool(int32, int32, float&)> HeightAt, TArray<FMT2WaterRectangle>& Out, FString& Error)
{
	const FIntPoint Grid = Attributes.Size;
	if (Grid.X <= 0 || Grid.Y <= 0 || int64(Grid.X) * Grid.Y != Attributes.Flags.Num())
	{
		Error = TEXT("Invalid water attribute grid"); return false;
	}
	TArray<FMT2WaterRectangle> Result;
	// Merge equal-height row runs vertically, but never across culling-chunk boundaries.
	TMap<FIntPoint, int32> Previous;
	for (int32 Y = 0; Y < Grid.Y; ++Y)
	{
		TMap<FIntPoint, int32> Current;
		if (Y % ChunkCells == 0) { Previous.Reset(); }
		for (int32 X = 0; X < Grid.X;)
		{
			if (!(Attributes.Flags[Y * Grid.X + X] & MT2MapAttribute::Water)) { ++X; continue; }
			float Height = 0.f;
			if (!HeightAt(X, Y, Height) || !FMath::IsFinite(Height))
			{
				Error = FString::Printf(TEXT("Water cell (%d,%d) has no valid water.wtr surface height"), X, Y);
				return false;
			}
			const int32 Start = X++;
			while (X < Grid.X && X % ChunkCells != 0 && (Attributes.Flags[Y * Grid.X + X] & MT2MapAttribute::Water))
			{
				float NextHeight = 0.f;
				if (!HeightAt(X, Y, NextHeight) || NextHeight != Height) { break; }
				++X;
			}
			const FIntPoint Key(Start, X - Start);
			const int32* Existing = Previous.Find(Key);
			int32 Index;
			if (Existing && Result[*Existing].Height == Height)
			{
				Index = *Existing; ++Result[Index].Size.Y;
			}
			else
			{
				FMT2WaterRectangle Rect; Rect.Cell = FIntPoint(Start, Y); Rect.Size = FIntPoint(X - Start, 1); Rect.Height = Height;
				Index = Result.Add(Rect);
			}
			Current.Add(Key, Index);
		}
		Previous = MoveTemp(Current);
	}
	Out = MoveTemp(Result); Error.Reset(); return true;
}

void MT2MapWater::AppendQuad(const FMT2WaterRectangle& Rect, FIntPoint GridSize,
	FVector2D WorldMin, FVector2D WorldMax, FVector Origin, float Offset, float UVTileSize,
	TArray<FVector>& Vertices, TArray<int32>& Indices, TArray<FVector2D>& UVs)
{
	const FVector2D Step = (WorldMax - WorldMin) / FVector2D(GridSize);
	const FVector2D A = WorldMax - FVector2D(Rect.Cell) * Step;
	const FVector2D B = WorldMax - FVector2D(Rect.Cell + Rect.Size) * Step;
	const int32 Base = Vertices.Num();
	for (const FVector2D XY : {A, FVector2D(B.X, A.Y), B, FVector2D(A.X, B.Y)})
	{
		Vertices.Add(FVector(XY, Rect.Height + Offset) - Origin);
		UVs.Add((XY - WorldMin) / UVTileSize);
	}
	// Match UE's upward-facing CreateGridMeshWelded winding, not a right-handed cross-product test.
	Indices.Append({Base, Base + 2, Base + 1, Base, Base + 3, Base + 2});
}

UMT2MapWaterComponent::UMT2MapWaterComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(false);
}

void UMT2MapWaterComponent::ClearChunks()
{
#if WITH_EDITOR
	// Derived, transient render components must not serialize their owner into editor undo.
	TGuardValue<decltype(GUndo)> UndoGuard(GUndo, nullptr);
#endif
	for (UProceduralMeshComponent* Chunk : Chunks) { if (IsValid(Chunk)) { Chunk->DestroyComponent(); } }
	Chunks.Reset();
	ChunkLookup.Reset();
}

void UMT2MapWaterComponent::OnRegister()
{
	Super::OnRegister();
#if WITH_EDITOR
	if (GetWorld() && !GetWorld()->IsGameWorld())
	{
		GetMutableDefault<UMT2GameplaySettings>()->OnSettingChanged().AddUObject(this, &UMT2MapWaterComponent::OnWaterSettingsChanged);
		if (const AMT2MapPresentationActor* Map = Cast<AMT2MapPresentationActor>(GetOwner())) { Rebuild(*Map); }
	}
#endif
}

void UMT2MapWaterComponent::OnUnregister()
{
#if WITH_EDITOR
	GetMutableDefault<UMT2GameplaySettings>()->OnSettingChanged().RemoveAll(this);
#endif
	ClearChunks();
	Super::OnUnregister();
}

#if WITH_EDITOR
void UMT2MapWaterComponent::OnWaterSettingsChanged(UObject* Settings, FPropertyChangedEvent& Event)
{
	const FName Name = Event.GetMemberPropertyName();
	if (Name == GET_MEMBER_NAME_CHECKED(UMT2GameplaySettings, WaterMaterial) ||
		Name == GET_MEMBER_NAME_CHECKED(UMT2GameplaySettings, WaterUVTileSize) ||
		Name == GET_MEMBER_NAME_CHECKED(UMT2GameplaySettings, WaterSurfaceOffset))
	{
		if (const AMT2MapPresentationActor* Map = Cast<AMT2MapPresentationActor>(GetOwner())) { Rebuild(*Map); }
	}
}
#endif

void UMT2MapWaterComponent::Rebuild(const AMT2MapPresentationActor& Map, const FIntRect* UpdatedCells)
{
#if WITH_EDITOR
	TGuardValue<decltype(GUndo)> UndoGuard(GUndo, nullptr);
#endif
	if (!UpdatedCells) { ClearChunks(); }
	if (!IsRegistered() || Map.GetNetMode() == NM_DedicatedServer || Map.WaterRectangles.IsEmpty()) { ClearChunks(); return; }
	const FVector2D Extent = Map.WorldMax - Map.WorldMin;
	const bool bValidSource = Map.MapCells.X > 0 && Map.MapCells.Y > 0 &&
		int64(Map.MapCells.X) * 128 == Map.WaterGridSize.X && int64(Map.MapCells.Y) * 128 == Map.WaterGridSize.Y;
	if (!bValidSource || Map.WaterGridSize.X <= 0 || Map.WaterGridSize.Y <= 0 ||
		!FMath::IsFinite(Extent.X) || !FMath::IsFinite(Extent.Y) || Extent.X <= 0 || Extent.Y <= 0 ||
		!FMath::IsFinite(Map.WorldMin.X) || !FMath::IsFinite(Map.WorldMin.Y))
	{
		ClearChunks();
		UE_LOG(LogMT2Water, Warning, TEXT("%s: invalid visual water grid or map bounds; re-bake water from the source water layers."), *Map.MapId);
		return;
	}
	const UMT2GameplaySettings& Settings = UMT2GameplaySettings::Get();
	if (Settings.WaterMaterial.IsNull())
	{
		ClearChunks();
		UE_LOG(LogMT2Water, Warning, TEXT("%s: assign Water Material in Project Settings > Metin2 > Metin2 Gameplay > Water."), *Map.MapId);
		return;
	}
	UMaterialInterface* Material = Settings.WaterMaterial.LoadSynchronous();
	if (!Material) { ClearChunks(); UE_LOG(LogMT2Water, Error, TEXT("Cannot load configured water material: %s"), *Settings.WaterMaterial.ToString()); return; }
	FIntRect ChunkRegion(FIntPoint::ZeroValue, FIntPoint::ZeroValue);
	if (UpdatedCells)
	{
		const FIntRect Cells(UpdatedCells->Min.ComponentMax(FIntPoint::ZeroValue), UpdatedCells->Max.ComponentMin(Map.WaterGridSize));
		if (Cells.Width() <= 0 || Cells.Height() <= 0) { return; }
		ChunkRegion = FIntRect(Cells.Min / MT2MapWater::ChunkCells, (Cells.Max - FIntPoint(1, 1)) / MT2MapWater::ChunkCells + FIntPoint(1, 1));
		for (int32 Y = ChunkRegion.Min.Y; Y < ChunkRegion.Max.Y; ++Y)
			for (int32 X = ChunkRegion.Min.X; X < ChunkRegion.Max.X; ++X)
			{
				const FIntPoint Key(X, Y);
				if (auto* Existing = ChunkLookup.Find(Key))
				{
					if (UProceduralMeshComponent* Mesh = Existing->Get()) { Chunks.Remove(Mesh); Mesh->DestroyComponent(); }
					ChunkLookup.Remove(Key);
				}
			}
	}
	TMap<FIntPoint, TArray<const FMT2WaterRectangle*>> Groups;
	for (const FMT2WaterRectangle& Rect : Map.WaterRectangles)
	{
		if (Rect.Cell.X < 0 || Rect.Cell.Y < 0 || Rect.Size.X <= 0 || Rect.Size.Y <= 0 ||
			int64(Rect.Cell.X) + Rect.Size.X > Map.WaterGridSize.X || int64(Rect.Cell.Y) + Rect.Size.Y > Map.WaterGridSize.Y ||
			!FMath::IsFinite(Rect.Height))
		{
			ClearChunks(); UE_LOG(LogMT2Water, Error, TEXT("%s: invalid baked water rectangle."), *Map.MapId); return;
		}
		const FIntPoint Key = Rect.Cell / MT2MapWater::ChunkCells;
		if (!UpdatedCells || ChunkRegion.Contains(Key)) { Groups.FindOrAdd(Key).Add(&Rect); }
	}
	for (const auto& Group : Groups)
	{
		const FVector2D XY = Map.WorldMax - FVector2D(Group.Key * MT2MapWater::ChunkCells) * Extent / FVector2D(Map.WaterGridSize);
		const FVector Origin(XY, Group.Value[0]->Height);
		TArray<FVector> Vertices; TArray<int32> Indices; TArray<FVector2D> UVs;
		Vertices.Reserve(Group.Value.Num() * 4); Indices.Reserve(Group.Value.Num() * 6); UVs.Reserve(Group.Value.Num() * 4);
		for (const FMT2WaterRectangle* Rect : Group.Value)
		{
			MT2MapWater::AppendQuad(*Rect, Map.WaterGridSize, Map.WorldMin, Map.WorldMax, Origin,
				Settings.WaterSurfaceOffset, FMath::Max(1.f, Settings.WaterUVTileSize), Vertices, Indices, UVs);
		}
		UProceduralMeshComponent* Chunk = NewObject<UProceduralMeshComponent>(GetOwner(), NAME_None, RF_Transient | RF_DuplicateTransient);
		Chunks.Add(Chunk);
		ChunkLookup.Add(Group.Key, Chunk);
		Chunk->SetupAttachment(this);
		Chunk->SetAbsolute(true, true, true);
		Chunk->SetWorldLocation(Origin);
		Chunk->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Chunk->SetCanEverAffectNavigation(false);
		Chunk->SetCastShadow(false);
		Chunk->bUseAsyncCooking = false;
		Chunk->RegisterComponent();
		TArray<FVector> Normals; Normals.Init(FVector::UpVector, Vertices.Num());
		TArray<FProcMeshTangent> Tangents; Tangents.Init(FProcMeshTangent(FVector::ForwardVector, false), Vertices.Num());
		Chunk->CreateMeshSection_LinearColor(0, Vertices, Indices, Normals, UVs, {}, Tangents, false);
		Chunk->SetMaterial(0, Material);
	}
	UE_CLOG(!UpdatedCells, LogMT2Water, Display, TEXT("%s: built %d water chunks from %d rectangles using %s."),
		*Map.MapId, Chunks.Num(), Map.WaterRectangles.Num(), *Material->GetPathName());
}
