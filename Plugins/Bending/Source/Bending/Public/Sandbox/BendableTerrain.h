#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Sim/BendingArena.h"
#include "Sim/BendingTerrain.h"
#include "Templates/UniquePtr.h"
#include "BendableTerrain.generated.h"

class UMaterialInstanceDynamic;
class UMaterialInterface;
class UProceduralMeshComponent;

/**
 * Ground earthbending can reshape: a BendingSim::FTerrain heightfield (volume-conserving soil edits) rendered as
 * low-poly procedural mesh chunks of 32 x 32 cells. Each tick the terrain's dirty region is rebuilt; only the
 * chunks it touches are updated.
 *
 * Vertex colours carry the look: grass on flat ground, darker earth on slopes, rock on the high ridge, sand at pond
 * shores, and brown wherever soil was raised or dug since generation. Each chunk keeps its collision in a hidden
 * second section that is re-cooked at most every CollisionUpdateIntervalS, so the visible ground moves smoothly
 * while a bender reshapes it.
 */
UCLASS()
class BENDING_API ABendableTerrain : public AActor
{
	GENERATED_BODY()

public:
	ABendableTerrain();

	/** Generates the arena's heightfield and builds every chunk (collision cooked synchronously, so it exists at once). */
	void BuildFromLayout(const BendingSim::FArenaLayout& Layout);

	BendingSim::FTerrain* GetTerrain() { return Terrain.Get(); }
	const BendingSim::FTerrain* GetTerrain() const { return Terrain.Get(); }

	/** Ground height (cm) under a point; 0 before the terrain is built. */
	double GetHeightAt(const FVector& LocationCm) const;
	FVector GetNormalAt(const FVector& LocationCm) const;

protected:
	virtual void Tick(float DeltaSeconds) override;

	UPROPERTY(VisibleAnywhere, Category = "Terrain")
	TObjectPtr<USceneComponent> Root;

	/** Optional material reading vertex colour. Default: the engine's VertexColorMaterial, else a flat grass tint. */
	UPROPERTY(EditAnywhere, Category = "Terrain")
	TSoftObjectPtr<UMaterialInterface> MaterialOverride;

	/** Shortest time between collision re-cooks of one chunk while it is being reshaped. */
	UPROPERTY(EditAnywhere, Category = "Terrain", meta = (ClampMin = 0.0, Units = "s"))
	float CollisionUpdateIntervalS = 0.1f;

	UPROPERTY(EditAnywhere, Category = "Terrain|Colors")
	FColor GrassColor = FColor(92, 146, 64);

	UPROPERTY(EditAnywhere, Category = "Terrain|Colors")
	FColor SlopeColor = FColor(112, 92, 64);

	UPROPERTY(EditAnywhere, Category = "Terrain|Colors")
	FColor RidgeColor = FColor(126, 124, 118);

	UPROPERTY(EditAnywhere, Category = "Terrain|Colors")
	FColor SandColor = FColor(200, 184, 134);

	UPROPERTY(EditAnywhere, Category = "Terrain|Colors")
	FColor PondBedColor = FColor(104, 98, 74);

	/** Soil raised or dug by earthbending. */
	UPROPERTY(EditAnywhere, Category = "Terrain|Colors")
	FColor BentSoilColor = FColor(134, 94, 58);

private:
	struct FChunk
	{
		int32 X0 = 0;
		int32 Y0 = 0;
		int32 CellsX = 0;
		int32 CellsY = 0;
		TArray<int32> Triangles;
		double LastCollisionSeconds = -1.0;
		bool bRenderDirty = false;
		bool bCollisionDirty = false;
	};

	struct FPendingWake
	{
		FBox Box;
		double TimeSeconds = 0.0;
	};

	UMaterialInterface* ResolveMaterial();
	void CreateChunks();
	void MarkChunksDirty(int32 MinX, int32 MinY, int32 MaxX, int32 MaxY);
	void BuildChunkPositions(const FChunk& Chunk, TArray<FVector>& OutPositions) const;
	void BuildChunkShading(const FChunk& Chunk);
	void UpdateChunkRender(int32 Index, bool bCreate);
	void UpdateChunkCollision(int32 Index, double NowSeconds);
	void WakeBodies(const FBox& Box) const;
	double SampleHeightClamped(int32 X, int32 Y) const;
	FColor ComputeVertexColor(int32 X, int32 Y, const FVector& Normal) const;

	TUniquePtr<BendingSim::FTerrain> Terrain;

	/** Heights as generated, to tell bent soil from untouched ground. */
	TArray<double> GeneratedHeights;
	TArray<BendingSim::FArenaPond> Ponds;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UProceduralMeshComponent>> ChunkMeshes;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> FallbackMaterial;

	TArray<FChunk> Chunks;
	TArray<FPendingWake> PendingWakes;
	int32 ChunksX = 0;
	int32 ChunksY = 0;

	FLinearColor Grass;
	FLinearColor Slope;
	FLinearColor Ridge;
	FLinearColor Sand;
	FLinearColor PondBed;
	FLinearColor BentSoil;

	/** Reused for every chunk rebuild. */
	TArray<FVector> ScratchPositions;
	TArray<FVector> ScratchNormals;
	TArray<FVector2D> ScratchUVs;
	TArray<FColor> ScratchColors;
};
