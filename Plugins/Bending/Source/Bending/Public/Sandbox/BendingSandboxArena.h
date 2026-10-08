#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Sim/BendingArena.h"
#include "BendingSandboxArena.generated.h"

class ABendableTerrain;
class APlayerStart;
class UBendingInteractionSubsystem;
class UInstancedStaticMeshComponent;
class UStaticMeshComponent;

/**
 * The training ground, generated from BendingSim::GetTrainingGroundLayout() (the same place the browser sandbox
 * builds): bendable terrain, ponds, stones, boulders, soil clods, dummies, braziers, ice blocks, a player start, and
 * daylight when the level has none. It also draws what has no actor of its own: steam clouds (free gas volumes of
 * the interaction simulation) and wet ground (its moisture patches).
 *
 * AAvatarGameMode spawns and builds one when the level has none; one placed in a level builds itself on BeginPlay.
 */
UCLASS()
class BENDING_API ABendingSandboxArena : public AActor
{
	GENERATED_BODY()

public:
	/** Loose soil's pore fraction, for ground that is not (yet) the generated terrain. */
	static constexpr float DefaultSoilPorosity = 0.35f;

	ABendingSandboxArena();

	static ABendingSandboxArena* Find(const UWorld* World);

	/** Spawns everything. Does nothing when already built. */
	UFUNCTION(BlueprintCallable, Category = "Bending|Sandbox")
	void BuildArena();

	bool IsBuilt() const { return bBuilt; }

	ABendableTerrain* GetTerrainActor() const;
	BendingSim::FTerrain* GetTerrain() const;
	APlayerStart* GetPlayerStart() const;

	/** The live layout: pond water goes down as benders draw from it. */
	const BendingSim::FArenaLayout& GetLayout() const { return Layout; }

	/** Takes up to RequestedKg of water out of a pond; returns what it had. */
	double DrawPondWater(int32 PondIndex, double RequestedKg);
	void ReturnPondWater(int32 PondIndex, double WaterKg);
	/** The pond whose surface covers this point horizontally, or INDEX_NONE. */
	int32 FindPondAt(const FVector& LocationCm) const;

	double GetGroundHeightAt(const FVector& LocationCm) const;
	FVector GetGroundNormalAt(const FVector& LocationCm) const;
	/** Pore fraction of the arena's soil, for water soaking into it. */
	float GetSoilPorosity() const;

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	UPROPERTY(VisibleAnywhere, Category = "Arena")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, Category = "Arena")
	TObjectPtr<UInstancedStaticMeshComponent> SteamInstances;

	UPROPERTY(VisibleAnywhere, Category = "Arena")
	TObjectPtr<UInstancedStaticMeshComponent> MoistureInstances;

	/** Add a sun, sky light, sky atmosphere and height fog when the level has no directional light. */
	UPROPERTY(EditAnywhere, Category = "Arena")
	bool bSpawnLighting = true;

	/** Wet ground is drawn as mud from this saturation up. */
	UPROPERTY(EditAnywhere, Category = "Arena", meta = (ClampMin = 0.0, ClampMax = 1.0))
	float MinVisibleSaturation = 0.2f;

	/** Share of a steam cloud's simulated radius drawn, so clouds do not hide everything behind them. */
	UPROPERTY(EditAnywhere, Category = "Arena", meta = (ClampMin = 0.0))
	float SteamVisualScale = 0.7f;

private:
	void SpawnTerrain();
	void SpawnPonds();
	void SpawnProps();
	void SpawnPlayerStart();
	void SpawnLighting();
	void UpdateSteam(const UBendingInteractionSubsystem& Interaction);
	void UpdateMoisture(const UBendingInteractionSubsystem& Interaction);

	BendingSim::FArenaLayout Layout;

	UPROPERTY(Transient)
	TObjectPtr<ABendableTerrain> TerrainActor;

	UPROPERTY(Transient)
	TObjectPtr<APlayerStart> PlayerStart;

	UPROPERTY(Transient)
	TArray<TObjectPtr<AActor>> Props;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> PondSurfaces;

	/** Reused every frame. */
	TArray<FTransform> InstanceScratch;
	bool bBuilt = false;
};
