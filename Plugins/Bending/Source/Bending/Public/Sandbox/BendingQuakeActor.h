#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BendingQuakeActor.generated.h"

class UInstancedStaticMeshComponent;

/**
 * What an earthquake stomp looks like: a wave races out over the ground and two rings of rock punch up as it
 * passes, overshoot, and sink back. Presentation only (the throw itself is UBendingTechniqueComponent's); it
 * removes itself when the last rock is back under the ground.
 */
UCLASS()
class BENDING_API ABendingQuakeActor : public AActor
{
	GENERATED_BODY()

public:
	ABendingQuakeActor();

	void InitQuake(const FVector& GroundCm);

protected:
	virtual void Tick(float DeltaSeconds) override;

	UPROPERTY(VisibleAnywhere, Category = "Quake")
	TObjectPtr<UInstancedStaticMeshComponent> Rocks;

	/** How fast the wave runs out over the ground. */
	UPROPERTY(EditAnywhere, Category = "Quake", meta = (ClampMin = 100.0, Units = "cm/s"))
	float WaveSpeedCmS = 1700.f;

private:
	struct FQuakeRock
	{
		double AngleRad = 0.0;
		double RadiusCm = 0.0;
		double SizeCm = 30.0;
		FRotator Tilt = FRotator::ZeroRotator;
		double GroundZ = 0.0;
	};

	TArray<FQuakeRock> RockSpecs;
	TArray<FTransform> Scratch;
	double AgeSeconds = 0.0;
};
