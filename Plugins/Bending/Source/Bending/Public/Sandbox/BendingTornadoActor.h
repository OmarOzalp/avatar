#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BendingTornadoActor.generated.h"

class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;

/**
 * A tornado spun up by an airbender (the Tornado technique). For its lifetime it swirls loose bodies round, draws
 * them in and lifts the light ones; flame that flies into it is caught in the spiral and makes a fire tornado. The
 * rules and numbers are the browser sandbox's (FSandbox::UpdateTornado), from the kernel's technique tuning.
 *
 * Drawn with engine basic shapes: strands of stretched spheres spiralling up the funnel, and debris whirling round it.
 */
UCLASS()
class BENDING_API ABendingTornadoActor : public AActor
{
	GENERATED_BODY()

public:
	ABendingTornadoActor();

	/** Starts it standing on the ground at GroundCm for LifetimeS seconds. */
	void InitTornado(const FVector& GroundCm, double LifetimeS);

	/** Flame caught in it (kg): above zero it is a fire tornado. */
	double GetFireKg() const { return FireKg; }

protected:
	virtual void Tick(float DeltaSeconds) override;

	UPROPERTY(VisibleAnywhere, Category = "Tornado")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, Category = "Tornado")
	TObjectPtr<UInstancedStaticMeshComponent> Funnel;

	UPROPERTY(VisibleAnywhere, Category = "Tornado")
	TObjectPtr<UInstancedStaticMeshComponent> Debris;

	/** Height of the drawn funnel. */
	UPROPERTY(EditAnywhere, Category = "Tornado", meta = (ClampMin = 100.0, Units = "cm"))
	float FunnelHeightCm = 900.f;

	UPROPERTY(EditAnywhere, Category = "Tornado", meta = (ClampMin = 1, ClampMax = 8))
	int32 NumStrands = 4;

	UPROPERTY(EditAnywhere, Category = "Tornado", meta = (ClampMin = 4, ClampMax = 48))
	int32 PuffsPerStrand = 22;

	UPROPERTY(EditAnywhere, Category = "Tornado", meta = (ClampMin = 0, ClampMax = 96))
	int32 NumDebris = 36;

	/** Air parcels shed round the funnel per second: they push what they meet and feed any fire. */
	UPROPERTY(EditAnywhere, Category = "Tornado", meta = (ClampMin = 0.0))
	float AirPuffsPerSecond = 8.f;

private:
	/** 0..1: builds up over its first 0.4 s, dies away over its last 0.6 s. */
	double GetStrength() const;
	void PullBodies(double Strength, float DeltaSeconds);
	void CatchProjectiles(double Strength, float DeltaSeconds);
	void ShedAir(double Strength, float DeltaSeconds);
	void UpdateVisuals(double Strength);

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> FunnelMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> DebrisMaterial;

	TArray<FTransform> Scratch;
	double AgeSeconds = 0.0;
	double LifetimeSeconds = 4.0;
	double FireKg = 0.0;
	double ShownFire = -1.0;
	double AirAccumulator = 0.0;
	double EmitAngleRad = 0.0;
	bool bInitialized = false;
};
