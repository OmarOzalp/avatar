#include "Sandbox/BendableTerrain.h"

#include "BendingLog.h"
#include "CollisionQueryParams.h"
#include "CollisionShape.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Physics/BendingUnits.h"
#include "ProceduralMeshComponent.h"
#include "Sandbox/BendingSandboxLibrary.h"

namespace
{
	constexpr int32 ChunkCells = 32;
	/** Section 0 is drawn; section 1 is the same surface, hidden, and carries the collision. */
	constexpr int32 VisualSection = 0;
	constexpr int32 CollisionSection = 1;
	/** Bodies resting on a re-cooked chunk are woken once the new collision is in place. */
	constexpr double WakeDelaySeconds = 0.15;
	const TCHAR* VertexColorMaterialPath = TEXT("/Engine/EngineDebugMaterials/VertexColorMaterial.VertexColorMaterial");
	const TCHAR* FlatTerrainMaterialPath = TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial");
}

ABendableTerrain::ABendableTerrain()
{
	PrimaryActorTick.bCanEverTick = true;
	// After every technique has moved soil this frame.
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;
}

// ---------------------------------------------------------------------------------------------------- Build

void ABendableTerrain::BuildFromLayout(const BendingSim::FArenaLayout& Layout)
{
	// Vertices are written in world space.
	SetActorLocationAndRotation(FVector::ZeroVector, FRotator::ZeroRotator);

	Terrain = MakeUnique<BendingSim::FTerrain>();
	BendingSim::GenerateArenaTerrain(Layout, *Terrain);

	const int32 NumSamples = Terrain->GetSamplesX() * Terrain->GetSamplesY();
	GeneratedHeights.SetNumUninitialized(NumSamples);
	FMemory::Memcpy(GeneratedHeights.GetData(), Terrain->GetHeightData(), NumSamples * sizeof(double));

	Ponds.Reset();
	for (int32 Index = 0; Index < Layout.NumPonds; ++Index)
	{
		Ponds.Add(Layout.Ponds[Index]);
	}

	Grass = FLinearColor::FromSRGBColor(GrassColor);
	Slope = FLinearColor::FromSRGBColor(SlopeColor);
	Ridge = FLinearColor::FromSRGBColor(RidgeColor);
	Sand = FLinearColor::FromSRGBColor(SandColor);
	PondBed = FLinearColor::FromSRGBColor(PondBedColor);
	BentSoil = FLinearColor::FromSRGBColor(BentSoilColor);

	CreateChunks();

	// Every chunk was just built from the generated heights.
	int32 MinX = 0;
	int32 MinY = 0;
	int32 MaxX = 0;
	int32 MaxY = 0;
	Terrain->ConsumeDirtyRegion(MinX, MinY, MaxX, MaxY);
}

UMaterialInterface* ABendableTerrain::ResolveMaterial()
{
	if (!MaterialOverride.IsNull())
	{
		if (UMaterialInterface* Override = MaterialOverride.LoadSynchronous())
		{
			return Override;
		}
		UE_LOG(LogBending, Warning, TEXT("%s: MaterialOverride %s could not be loaded."), *GetName(), *MaterialOverride.ToString());
	}
	if (UMaterialInterface* VertexColor = LoadObject<UMaterialInterface>(nullptr, VertexColorMaterialPath))
	{
		return VertexColor;
	}
	UE_LOG(LogBending, Warning, TEXT("%s not found; the terrain is drawn in one flat colour."), VertexColorMaterialPath);
	if (UMaterialInterface* BasicShapeMaterial = LoadObject<UMaterialInterface>(nullptr, FlatTerrainMaterialPath))
	{
		FallbackMaterial = UMaterialInstanceDynamic::Create(BasicShapeMaterial, this);
		FallbackMaterial->SetVectorParameterValue(TEXT("Color"), Grass);
		return FallbackMaterial.Get();
	}
	return nullptr;
}

void ABendableTerrain::CreateChunks()
{
	for (UProceduralMeshComponent* Mesh : ChunkMeshes)
	{
		if (Mesh)
		{
			Mesh->DestroyComponent();
		}
	}
	ChunkMeshes.Reset();
	Chunks.Reset();
	PendingWakes.Reset();

	const int32 CellsX = Terrain->GetSamplesX() - 1;
	const int32 CellsY = Terrain->GetSamplesY() - 1;
	ChunksX = FMath::DivideAndRoundUp(CellsX, ChunkCells);
	ChunksY = FMath::DivideAndRoundUp(CellsY, ChunkCells);
	UMaterialInterface* Material = ResolveMaterial();

	for (int32 ChunkY = 0; ChunkY < ChunksY; ++ChunkY)
	{
		for (int32 ChunkX = 0; ChunkX < ChunksX; ++ChunkX)
		{
			const int32 Index = Chunks.Num();
			FChunk& Chunk = Chunks.AddDefaulted_GetRef();
			Chunk.X0 = ChunkX * ChunkCells;
			Chunk.Y0 = ChunkY * ChunkCells;
			Chunk.CellsX = FMath::Min(ChunkCells, CellsX - Chunk.X0);
			Chunk.CellsY = FMath::Min(ChunkCells, CellsY - Chunk.Y0);

			// Each cell splits along the kernel's diagonal, sample (X, Y) to (X + 1, Y + 1). Unreal's front face is
			// the one whose normal is (P2 - P0) x (P1 - P0), so these triangles face up (+Z).
			const int32 RowLength = Chunk.CellsX + 1;
			Chunk.Triangles.Reserve(Chunk.CellsX * Chunk.CellsY * 6);
			for (int32 Y = 0; Y < Chunk.CellsY; ++Y)
			{
				for (int32 X = 0; X < Chunk.CellsX; ++X)
				{
					const int32 I00 = Y * RowLength + X;
					const int32 I10 = I00 + 1;
					const int32 I01 = I00 + RowLength;
					const int32 I11 = I01 + 1;
					Chunk.Triangles.Append({ I00, I11, I10, I00, I01, I11 });
				}
			}

			UProceduralMeshComponent* Mesh = NewObject<UProceduralMeshComponent>(this);
			Mesh->SetupAttachment(Root);
			Mesh->bUseComplexAsSimpleCollision = true;
			// The ground must exist before anything spawns on it: the first cook is synchronous.
			Mesh->bUseAsyncCooking = false;
			Mesh->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
			Mesh->SetCanEverAffectNavigation(false);
			Mesh->RegisterComponent();
			ChunkMeshes.Add(Mesh);

			UpdateChunkRender(Index, /*bCreate*/ true);
			UpdateChunkCollision(Index, -1.0);
			Mesh->SetMaterial(VisualSection, Material);
			Mesh->SetMaterial(CollisionSection, Material);
			Mesh->bUseAsyncCooking = true;
		}
	}
	// Nothing rests on the ground yet.
	PendingWakes.Reset();
}

// ---------------------------------------------------------------------------------------------------- Queries

double ABendableTerrain::GetHeightAt(const FVector& LocationCm) const
{
	return Terrain ? Terrain->GetHeightAt(LocationCm.X, LocationCm.Y) : 0.0;
}

FVector ABendableTerrain::GetNormalAt(const FVector& LocationCm) const
{
	return Terrain ? BendingUnits::ToEngine(Terrain->GetNormalAt(LocationCm.X, LocationCm.Y)) : FVector::UpVector;
}

double ABendableTerrain::SampleHeightClamped(int32 X, int32 Y) const
{
	return Terrain->GetSampleHeight(FMath::Clamp(X, 0, Terrain->GetSamplesX() - 1), FMath::Clamp(Y, 0, Terrain->GetSamplesY() - 1));
}

// ---------------------------------------------------------------------------------------------------- Chunks

void ABendableTerrain::MarkChunksDirty(int32 MinX, int32 MinY, int32 MaxX, int32 MaxY)
{
	// Chunk C spans samples [C * ChunkCells, (C + 1) * ChunkCells]; neighbours share their edge samples.
	const int32 FirstX = FMath::Clamp((MinX - 1) / ChunkCells, 0, ChunksX - 1);
	const int32 FirstY = FMath::Clamp((MinY - 1) / ChunkCells, 0, ChunksY - 1);
	const int32 LastX = FMath::Clamp(MaxX / ChunkCells, 0, ChunksX - 1);
	const int32 LastY = FMath::Clamp(MaxY / ChunkCells, 0, ChunksY - 1);
	for (int32 ChunkY = FirstY; ChunkY <= LastY; ++ChunkY)
	{
		for (int32 ChunkX = FirstX; ChunkX <= LastX; ++ChunkX)
		{
			FChunk& Chunk = Chunks[ChunkY * ChunksX + ChunkX];
			Chunk.bRenderDirty = true;
			Chunk.bCollisionDirty = true;
		}
	}
}

void ABendableTerrain::BuildChunkPositions(const FChunk& Chunk, TArray<FVector>& OutPositions) const
{
	OutPositions.Reset((Chunk.CellsX + 1) * (Chunk.CellsY + 1));
	for (int32 Y = Chunk.Y0; Y <= Chunk.Y0 + Chunk.CellsY; ++Y)
	{
		for (int32 X = Chunk.X0; X <= Chunk.X0 + Chunk.CellsX; ++X)
		{
			OutPositions.Add(BendingUnits::ToEngine(Terrain->GetSampleLocation(X, Y)));
		}
	}
}

void ABendableTerrain::BuildChunkShading(const FChunk& Chunk)
{
	const int32 NumVertices = (Chunk.CellsX + 1) * (Chunk.CellsY + 1);
	const double CellCm = Terrain->GetCellSizeCm();
	ScratchNormals.Reset(NumVertices);
	ScratchUVs.Reset(NumVertices);
	ScratchColors.Reset(NumVertices);
	for (int32 Y = Chunk.Y0; Y <= Chunk.Y0 + Chunk.CellsY; ++Y)
	{
		for (int32 X = Chunk.X0; X <= Chunk.X0 + Chunk.CellsX; ++X)
		{
			// Central differences, matching BendingSim::FTerrain::GetNormalAt.
			const double SlopeX = (SampleHeightClamped(X + 1, Y) - SampleHeightClamped(X - 1, Y)) / (2.0 * CellCm);
			const double SlopeY = (SampleHeightClamped(X, Y + 1) - SampleHeightClamped(X, Y - 1)) / (2.0 * CellCm);
			const FVector Normal = FVector(-SlopeX, -SlopeY, 1.0).GetSafeNormal();
			ScratchNormals.Add(Normal);
			ScratchUVs.Add(FVector2D(X * CellCm / 400.0, Y * CellCm / 400.0));
			ScratchColors.Add(ComputeVertexColor(X, Y, Normal));
		}
	}
}

FColor ABendableTerrain::ComputeVertexColor(int32 X, int32 Y, const FVector& Normal) const
{
	const double HeightCm = Terrain->GetSampleHeight(X, Y);
	const double GeneratedCm = GeneratedHeights[Y * Terrain->GetSamplesX() + X];
	const BendingSim::FVec3 Sample = Terrain->GetSampleLocation(X, Y);

	// Bare earth on slopes, rock on the high boundary ridge.
	FLinearColor Color = UBendingSandboxLibrary::LerpColor(Grass, Slope, FMath::SmoothStep(0.04f, 0.22f, 1.f - static_cast<float>(Normal.Z)));
	Color = UBendingSandboxLibrary::LerpColor(Color, Ridge, FMath::SmoothStep(260.f, 420.f, static_cast<float>(GeneratedCm)));

	// Sand at the water's edge, a muddy bed under the water.
	for (const BendingSim::FArenaPond& Pond : Ponds)
	{
		const double Distance = BendingSim::Distance2D(Sample, Pond.CenterCm);
		const float Shore = 1.f - FMath::SmoothStep(static_cast<float>(1.1 * Pond.RadiusCm), static_cast<float>(1.45 * Pond.RadiusCm), static_cast<float>(Distance));
		Color = UBendingSandboxLibrary::LerpColor(Color, Sand, Shore);
		if (HeightCm < Pond.SurfaceHeightCm)
		{
			Color = UBendingSandboxLibrary::LerpColor(Color, PondBed, FMath::SmoothStep(0.f, 30.f, static_cast<float>(Pond.SurfaceHeightCm - HeightCm)));
		}
	}

	// Soil an earthbender raised or dug.
	Color = UBendingSandboxLibrary::LerpColor(Color, BentSoil, FMath::SmoothStep(2.f, 25.f, static_cast<float>(FMath::Abs(HeightCm - GeneratedCm))));

	// Low-poly speckle: a slight per-vertex brightness change, so movement over flat ground reads.
	const uint32 Hash = (static_cast<uint32>(X) * 73856093u) ^ (static_cast<uint32>(Y) * 19349663u);
	const float Speckle = 1.f + 0.06f * (static_cast<float>(Hash % 1024u) / 1023.f - 0.5f);
	Color *= Speckle;
	Color.A = 1.f;

	// The material reads vertex colour as stored, so store linear values.
	return Color.ToFColor(false);
}

void ABendableTerrain::UpdateChunkRender(int32 Index, bool bCreate)
{
	UProceduralMeshComponent* Mesh = ChunkMeshes[Index];
	if (!Mesh)
	{
		return;
	}
	const FChunk& Chunk = Chunks[Index];
	BuildChunkPositions(Chunk, ScratchPositions);
	BuildChunkShading(Chunk);
	const TArray<FProcMeshTangent> NoTangents;
	if (bCreate)
	{
		Mesh->CreateMeshSection(VisualSection, ScratchPositions, Chunk.Triangles, ScratchNormals, ScratchUVs, ScratchColors, NoTangents, /*bCreateCollision*/ false);
	}
	else
	{
		Mesh->UpdateMeshSection(VisualSection, ScratchPositions, ScratchNormals, ScratchUVs, ScratchColors, NoTangents);
	}
}

void ABendableTerrain::UpdateChunkCollision(int32 Index, double NowSeconds)
{
	UProceduralMeshComponent* Mesh = ChunkMeshes[Index];
	if (!Mesh)
	{
		return;
	}
	FChunk& Chunk = Chunks[Index];
	BuildChunkPositions(Chunk, ScratchPositions);
	// Re-creating the hidden section re-cooks the chunk's collision (asynchronously after the first build).
	Mesh->CreateMeshSection(CollisionSection, ScratchPositions, Chunk.Triangles, TArray<FVector>(), TArray<FVector2D>(), TArray<FColor>(),
		TArray<FProcMeshTangent>(), /*bCreateCollision*/ true);
	Mesh->SetMeshSectionVisible(CollisionSection, false);
	Chunk.LastCollisionSeconds = NowSeconds;
	Chunk.bCollisionDirty = false;

	// Bodies asleep on this ground would otherwise float over a dug pit or sink into a raised mound.
	FPendingWake& Wake = PendingWakes.AddDefaulted_GetRef();
	Wake.Box = Mesh->Bounds.GetBox().ExpandBy(FVector(50.0, 50.0, 300.0));
	Wake.TimeSeconds = NowSeconds + WakeDelaySeconds;
}

void ABendableTerrain::WakeBodies(const FBox& Box) const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	TArray<FOverlapResult> Overlaps;
	World->OverlapMultiByObjectType(Overlaps, Box.GetCenter(), FQuat::Identity, FCollisionObjectQueryParams(ECC_PhysicsBody),
		FCollisionShape::MakeBox(Box.GetExtent()), FCollisionQueryParams(SCENE_QUERY_STAT(BendableTerrainWake), false, this));
	for (const FOverlapResult& Overlap : Overlaps)
	{
		UPrimitiveComponent* Component = Overlap.GetComponent();
		if (Component && Component->IsSimulatingPhysics())
		{
			Component->WakeAllRigidBodies();
		}
	}
}

// ---------------------------------------------------------------------------------------------------- Frame

void ABendableTerrain::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!Terrain)
	{
		return;
	}

	int32 MinX = 0;
	int32 MinY = 0;
	int32 MaxX = 0;
	int32 MaxY = 0;
	if (Terrain->ConsumeDirtyRegion(MinX, MinY, MaxX, MaxY))
	{
		// Normals and colours also read the neighbouring samples.
		MarkChunksDirty(MinX - 1, MinY - 1, MaxX + 1, MaxY + 1);
	}

	const double Now = GetWorld()->GetTimeSeconds();
	for (int32 Index = 0; Index < Chunks.Num(); ++Index)
	{
		if (Chunks[Index].bRenderDirty)
		{
			Chunks[Index].bRenderDirty = false;
			UpdateChunkRender(Index, /*bCreate*/ false);
		}
		if (Chunks[Index].bCollisionDirty && Now - Chunks[Index].LastCollisionSeconds >= CollisionUpdateIntervalS)
		{
			UpdateChunkCollision(Index, Now);
		}
	}

	for (int32 Index = PendingWakes.Num() - 1; Index >= 0; --Index)
	{
		if (Now >= PendingWakes[Index].TimeSeconds)
		{
			WakeBodies(PendingWakes[Index].Box);
			PendingWakes.RemoveAtSwap(Index);
		}
	}
}
