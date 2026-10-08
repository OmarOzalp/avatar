#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Physics/ElementalSubstance.h"
#include "Sim/BendingArena.h"
#include "BendingPropActor.generated.h"

class ABendingSandboxArena;
class UElementalVolumeComponent;
class UPointLightComponent;
class UStaticMeshComponent;

/**
 * A solid thing in the training ground, built from engine basic shapes, with its body in the interaction
 * simulation (BendingSim::MakeArenaPropVolume):
 *
 *  - Stones, rocks, boulders, soil clods and dummies simulate physics; reaction impulses (air blasts, water
 *    splashes) are applied to the rigid body, and porous earth darkens as it soaks up water.
 *  - Ice blocks are frozen to the ground; when fire melts one, its water soaks into the soil and it is gone.
 *  - Braziers keep a flame burning on a stone pedestal. Water puts it out; a flame brought close relights it.
 *  - Thrown rocks are soil pulled out of the terrain: kinematic while the bender lifts them, then launched.
 */
UCLASS()
class BENDING_API ABendingPropActor : public AActor
{
	GENERATED_BODY()

public:
	ABendingPropActor();

	/** Builds an arena prop standing on the ground at GroundCm. */
	void InitArenaProp(BendingSim::EArenaProp InKind, const FVector& InGroundCm);

	/** A rock of compacted soil (BendingSim::MakeRock), held kinematic by the bender until Launch. */
	void InitThrownRock(double MassKg, double DensityKgM3, const FVector& LocationCm);

	/** Moves a held rock (teleport; it is not simulating). */
	void SetHeldLocation(const FVector& LocationCm);

	/** Lets a held rock go with this velocity; it simulates from now on. */
	void Launch(const FVector& VelocityCmS);

	/** Returns a thrown rock's soil to the ground under it and destroys it: earthbending never destroys soil. */
	void CrumbleIntoGround(BendingSim::FTerrain& Terrain);

	bool IsHeld() const { return bHeld; }
	bool IsThrownRock() const { return bThrownRock; }
	double GetMassKg() const;
	double GetRadiusCm() const { return RadiusCm; }

protected:
	virtual void Tick(float DeltaSeconds) override;

	UPROPERTY(VisibleAnywhere, Category = "Prop")
	TObjectPtr<UStaticMeshComponent> Body;

	/** Dummy head, brazier flame. */
	UPROPERTY(VisibleAnywhere, Category = "Prop")
	TObjectPtr<UStaticMeshComponent> Detail;

	/** Dummy cross-bar. */
	UPROPERTY(VisibleAnywhere, Category = "Prop")
	TObjectPtr<UStaticMeshComponent> Accent;

	UPROPERTY(VisibleAnywhere, Category = "Prop")
	TObjectPtr<UPointLightComponent> Light;

	UPROPERTY(VisibleAnywhere, Category = "Prop")
	TObjectPtr<UElementalVolumeComponent> Volume;

	/** Brazier fuel: what a 0.5 kg, 1300 K flame loses to the air, so it burns steadily until water cools it. */
	UPROPERTY(EditAnywhere, Category = "Prop", meta = (ClampMin = 0.0))
	float BrazierPowerW = 6.0e5f;

	/** An unlit brazier relights when a flame comes this close to it. */
	UPROPERTY(EditAnywhere, Category = "Prop", meta = (ClampMin = 0.0, Units = "cm"))
	float RelightRadiusCm = 150.f;

	UPROPERTY(EditAnywhere, Category = "Prop", meta = (ClampMin = 0.0))
	float BrazierLightCandelas = 150.f;

private:
	UFUNCTION()
	void HandleSubstanceChanged(EElementalSubstance OldSubstance, EElementalSubstance NewSubstance);

	UFUNCTION()
	void HandleDepleted(UElementalVolumeComponent* DepletedVolume);

	void SetupBody(const TCHAR* ShapeName, const FVector& CenterCm, const FVector& SizeCm, const FLinearColor& Color);
	void SetupDetail(UStaticMeshComponent* Part, const TCHAR* ShapeName, const FVector& CenterCm, const FVector& SizeCm, const FLinearColor& Color);
	void EnablePhysics(double MassKg);
	void ConfigureVolume(const BendingSim::FVolume& SimVolume, const FVector& CenterCm, bool bManualVelocity);
	void SetFlameLit(bool bLit);
	void TickBrazier(float DeltaSeconds);
	void TickIce();
	void TickSoakedEarth();
	float GetSoilPorosity() const;

	BendingSim::EArenaProp Kind = BendingSim::EArenaProp::Stone;
	FLinearColor BaseColor = FLinearColor::White;
	FVector GroundCm = FVector::ZeroVector;
	/** Soil a thrown rock was made of (kg): what returns to the ground when it crumbles. */
	double SoilMassKg = 0.0;
	double RadiusCm = 10.0;
	double IceSideCm = 0.0;
	double RelightCheckSeconds = 0.0;
	float ShownSaturation = 0.f;
	float ShownMeltFraction = 0.f;
	bool bArenaProp = false;
	bool bThrownRock = false;
	bool bHeld = false;
	bool bFlameLit = false;
	bool bMelted = false;
};
