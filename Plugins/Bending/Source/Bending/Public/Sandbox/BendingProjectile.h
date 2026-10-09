#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Interaction/ElementalVolumeTypes.h"
#include "BendingProjectile.generated.h"

class ABendingSandboxArena;
class UBendingInteractionSubsystem;
class UElementalVolumeComponent;
class UMaterialInstanceDynamic;
class UPointLightComponent;
class UStaticMeshComponent;

UENUM(BlueprintType)
enum class EBendingProjectileKind : uint8
{
	/** Bent flame: flies straight, expands and cools; settles on the ground and burns there until it goes out. */
	Fire,
	/** Compressed air: flies straight, loses speed to the still air around it, dies after the kernel's air lifetime. */
	Air,
	/** A ball of water: falls; soaks into the soil where it lands (mud), or rejoins a pond. */
	Water,
	/** An ice dagger: flies nearly straight, strikes the first body it meets with all its momentum, and shatters. */
	IceShard
};

/**
 * Bent matter in flight. The UElementalVolumeComponent is its body in the interaction simulation; this actor
 * integrates the motion (the kernel leaves owned volumes to their owners) and applies the reaction impulses it
 * receives as dv = J / m, so a water ball that hits a boulder slows down and the boulder is shoved.
 */
UCLASS()
class BENDING_API ABendingProjectile : public AActor
{
	GENERATED_BODY()

public:
	ABendingProjectile();

	/** Spawns bent matter at SimVolume's location, moving with SimVolume's velocity. */
	static ABendingProjectile* SpawnProjectile(UWorld* World, EBendingProjectileKind InKind, const BendingSim::FVolume& SimVolume, AActor* InstigatorActor, bool bWithLight);

	void InitProjectile(EBendingProjectileKind InKind, const BendingSim::FVolume& SimVolume, bool bWithLight);

	/** Rests on the ground instead of flying (a ground flame, or a flame that has landed). */
	void SetGrounded(bool bInGrounded);

	/** Stays alive at least this much longer (a bender keeps feeding it). */
	void KeepAlive(double Seconds);

	/** Heat into the volume (J); returns what it accepted (flame is capped at the adiabatic temperature). */
	double AddHeat(double HeatJ);

	/** Moves a grounded flame along the ground toward a point (a bender dragging it). */
	void DragAlongGround(const FVector& TargetCm, double MaxStepCm);

	/** Water as spray: it loses its speed to the air over DragTimeS and draws no ball (the whip draws its droplets). */
	void MakeSpray(double DragTimeS);

	/** A released ground flame burns the ground it lit: FuelJ of heat at PowerW, unless water puts it out first. */
	void SetFuel(double InFuelJ, double PowerW);

	EBendingProjectileKind GetKind() const { return Kind; }
	FVector GetVelocityCmS() const { return VelocityCmS; }
	/** Sets the velocity of matter in flight (a tornado steering it); grounded matter stays put. */
	void SetVelocityCmS(const FVector& InVelocityCmS);
	/** Simulated mass (kg), 0 once gone. */
	double GetMassKg() const;
	bool IsGrounded() const { return bGrounded; }

protected:
	virtual void Tick(float DeltaSeconds) override;

	UPROPERTY(VisibleAnywhere, Category = "Projectile")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, Category = "Projectile")
	TObjectPtr<UStaticMeshComponent> Mesh;

	UPROPERTY(VisibleAnywhere, Category = "Projectile")
	TObjectPtr<UElementalVolumeComponent> Volume;

	UPROPERTY(VisibleAnywhere, Category = "Projectile")
	TObjectPtr<UPointLightComponent> Light;

	/** Peak brightness of a hot flame's light (cd). */
	UPROPERTY(EditAnywhere, Category = "Projectile", meta = (ClampMin = 0.0))
	float FlameLightCandelas = 120.f;

	/** Water balls are removed after this long even if they never land. */
	UPROPERTY(EditAnywhere, Category = "Projectile", meta = (ClampMin = 0.1, Units = "s"))
	float WaterLifetimeS = 8.f;

	/** Ice daggers that hit nothing shatter after this long. */
	UPROPERTY(EditAnywhere, Category = "Projectile", meta = (ClampMin = 0.1, Units = "s"))
	float IceShardLifetimeS = 3.f;

	/** Damage an ice dagger does to a training dummy on top of its momentum. */
	UPROPERTY(EditAnywhere, Category = "Projectile", meta = (ClampMin = 0.0))
	float IceShardDamage = 15.f;

	/** Share of gravity an ice dagger feels (the bender carries it). */
	UPROPERTY(EditAnywhere, Category = "Projectile", meta = (ClampMin = 0.0, ClampMax = 1.0))
	float IceShardGravityScale = 0.3f;

private:
	UFUNCTION()
	void HandleImpulse(FVector ImpulseKgCmS);

	UFUNCTION()
	void HandleDepleted(UElementalVolumeComponent* DepletedVolume);

	/** Ice dagger: strikes a physics body between the last position and this one. True when it hit (and shattered). */
	bool StrikeAlongPath(const FVector& From, const FVector& To, const FElementalVolumeState& State);

	/** Lands, splashes or dies on contact with the ground or a pond. True when the actor was destroyed. */
	bool HandleGroundContact(FVector& Location, const FElementalVolumeState& State);
	double GetGroundHeight(const FVector& LocationCm) const;
	void UpdateVisuals(const FElementalVolumeState& State);

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> Material;

	TWeakObjectPtr<ABendingSandboxArena> Arena;
	TWeakObjectPtr<UBendingInteractionSubsystem> Interaction;

	EBendingProjectileKind Kind = EBendingProjectileKind::Fire;
	FVector VelocityCmS = FVector::ZeroVector;
	FVector PreviousLocation = FVector::ZeroVector;
	double AgeSeconds = 0.0;
	double LifetimeSeconds = 3.0;
	float LastColorTemperatureK = 0.f;
	/** Spray only: time for air drag to take most of its speed (s); 0 = no drag. */
	double SprayDragTimeS = 0.0;
	/** Released ground flame: heat left to burn (J) and the rate it burns at (W). */
	double FuelJ = 0.0;
	double FuelPowerW = 0.0;
	bool bGrounded = false;
	bool bWithFlameLight = false;
	bool bInitialized = false;
};
