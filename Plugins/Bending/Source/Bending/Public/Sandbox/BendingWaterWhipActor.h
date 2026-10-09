#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Sim/BendingWaterWhip.h"
#include "BendingWaterWhipActor.generated.h"

class ABendingSandboxArena;
class UBendingInteractionSubsystem;
class UBendingTechniqueComponent;
class UInstancedStaticMeshComponent;

/**
 * A water whip held by a bender: BendingSim::FWaterWhip living in the interaction subsystem's FSimWorld. Its
 * segments are owned Water capsule volumes the whip adds to the world itself, so it boils against fire, soaks
 * into soil, shoves boulders, and freezes into ice like any other matter.
 *
 * The water circles the bender's chest until a lash: drawn back, snapped out to the aim, then flowing back on its
 * own. At the snap the tip flings spray (a water projectile that slows in the air); fast water sheds droplets and
 * splashes where it slaps the ground. The stream is drawn as overlapping spheres along a smooth curve through the
 * simulated points, the droplets as small stretched spheres.
 *
 * Each frame: PostStep (what last frame's world step did to the water) -> SetControl from the bender's hand and
 * aim -> PreStep. The interaction subsystem then advances the world at the end of the frame.
 */
UCLASS()
class BENDING_API ABendingWaterWhipActor : public AActor
{
	GENERATED_BODY()

public:
	ABendingWaterWhipActor();

	/** Draws WaterKg from a source point (a pond surface) toward the bender's hand. */
	bool InitWhip(UBendingTechniqueComponent* InBender, const FVector& SourceCm, double WaterKg, double TemperatureK);

	/** Starts a lash: WindupS drawing the stream back, StrikeS snapping it out. False while forming or mid-strike. */
	bool Lash(double WindupS, double StrikeS);

	/** Heat into (+) or out of (-) the water, segment by segment from the hand outward. Returns the heat moved (J). */
	double TransferHeat(double HeatJ);
	double GetHeatToFreeze() const;
	double GetHeatToMelt() const;
	bool IsFrozen() const;
	bool IsAnyFrozen() const;
	bool IsForming() const;
	/** Too little water is left to hold together; the actor then lets it fall on its own. */
	bool ShouldCollapse() const;
	double GetMassKg() const;
	int32 GetNumSegments() const { return Whip.GetNumSegments(); }
	int32 GetNumFrozenSegments() const;

	/** Lets go of the water and destroys this actor; each parcel comes back with its location, velocity and phase. */
	int32 ReleaseWater(TArray<BendingSim::FWhipDrop>& OutDrops);

protected:
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(VisibleAnywhere, Category = "Water Whip")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, Category = "Water Whip")
	TObjectPtr<UInstancedStaticMeshComponent> WaterInstances;

	UPROPERTY(VisibleAnywhere, Category = "Water Whip")
	TObjectPtr<UInstancedStaticMeshComponent> IceInstances;

	UPROPERTY(VisibleAnywhere, Category = "Water Whip")
	TObjectPtr<UInstancedStaticMeshComponent> DropletInstances;

	/** The stream is drawn thicker than its simulated radius so it reads at a distance. */
	UPROPERTY(EditAnywhere, Category = "Water Whip", meta = (ClampMin = 0.1))
	float VisualRadiusScale = 1.9f;

	UPROPERTY(EditAnywhere, Category = "Water Whip", meta = (ClampMin = 0.0, Units = "cm"))
	float MinVisualRadiusCm = 2.5f;

	/** The water circles the bender's chest, this far above the bender actor's origin (a character's capsule centre). */
	UPROPERTY(EditAnywhere, Category = "Water Whip", meta = (Units = "cm"))
	float ChestHeightCm = 45.f;

	/** Most droplets alive at once (spray, shedding, splashes). */
	UPROPERTY(EditAnywhere, Category = "Water Whip", meta = (ClampMin = 0))
	int32 MaxDroplets = 500;

private:
	/** A droplet of presentation only: the simulation holds the water's mass; droplets show its motion. */
	struct FDroplet
	{
		FVector LocationCm;
		FVector VelocityCmS;
		float RadiusCm = 1.f;
		float LifeS = 1.f;
		float DragTimeS = 0.8f;
	};

	BendingSim::FSimWorld* GetSimWorld() const;
	/** The water can no longer be held (bender gone, boiled or soaked away): it falls as water balls. */
	void Collapse();
	/** Spray the whip flung off at the snap: a projectile for the simulation, a cone of droplets for the eye. */
	void FlingSpray();
	void SpawnDroplet(const FVector& LocationCm, const FVector& VelocityCmS, float RadiusCm, float LifeS, float DragTimeS);
	/** Sheds droplets off fast water, splashes where the stream slaps the ground, and moves every droplet. */
	void UpdateDroplets(float DeltaSeconds);
	void UpdateVisuals();

	BendingSim::FWaterWhip Whip;

	TWeakObjectPtr<UBendingTechniqueComponent> Bender;
	TWeakObjectPtr<UBendingInteractionSubsystem> Interaction;
	TWeakObjectPtr<ABendingSandboxArena> Arena;

	TArray<FTransform> WaterTransforms;
	TArray<FTransform> IceTransforms;
	TArray<FTransform> DropletTransforms;
	TArray<FDroplet> Droplets;
	/** Per chain point: time until it may splash on the ground again. */
	float SlapCooldownS[BendingSim::FWaterWhip::MaxPoints] = {};
};
