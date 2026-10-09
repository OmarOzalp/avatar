#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Physics/ElementalSubstance.h"
#include "Sim/BendingArena.h"
#include "Sim/BendingCombustion.h"
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
 *  - Banners, straw bales, crates, dummies and lanterns burn (BendingSim::FCombustible): they soak up heat from
 *    flames near them, catch, keep a flame of their own burning in the simulation, char, and burn away; water
 *    puts them out. Lanterns burn for as long as they are left lit.
 *  - Crates and water barrels smash when hit hard (a thrown rock, an earthquake, a hard landing); a barrel's water
 *    spills and soaks the ground, and a waterbender can draw a whip from it.
 *  - Training dummies have health: blows, ice and fire hurt them, and at zero they topple and get up again later.
 */
UCLASS()
class BENDING_API ABendingPropActor : public AActor
{
	GENERATED_BODY()

public:
	ABendingPropActor();

	/** Builds an arena prop standing on the ground at GroundCm, facing YawDeg; Variant is a banner's element. */
	void InitArenaProp(BendingSim::EArenaProp InKind, const FVector& InGroundCm, double YawDeg = 0.0, int32 Variant = 0);

	/** A rock of compacted soil (BendingSim::MakeRock), held kinematic by the bender until Launch. */
	void InitThrownRock(double MassKg, double DensityKgM3, const FVector& LocationCm);

	/** Moves a held rock (teleport; it is not simulating). */
	void SetHeldLocation(const FVector& LocationCm);

	/** Lets a held rock go with this velocity; it simulates from now on. */
	void Launch(const FVector& VelocityCmS);

	/** Returns a thrown rock's soil to the ground under it and destroys it: earthbending never destroys soil. */
	void CrumbleIntoGround(BendingSim::FTerrain& Terrain);

	bool IsHeld() const { return bHeld; }
	/** A loose physics body (not held, not anchored like braziers and ice blocks): quakes and tornadoes can throw it. */
	bool IsLoose() const;
	/** Changes its velocity (cm/s) as a shove would, whatever its mass. */
	void AddVelocity(const FVector& DeltaVCmS);
	bool IsThrownRock() const { return bThrownRock; }
	double GetMassKg() const;
	double GetRadiusCm() const { return RadiusCm; }
	BendingSim::EArenaProp GetKind() const { return Kind; }
	bool IsArenaProp() const { return bArenaProp; }

	/** Training dummies: damage (others ignore it); FromDirection is the way the blow travelled. */
	void TakeHit(double Damage, const FVector& FromDirection);
	bool IsDummy() const { return bArenaProp && Kind == BendingSim::EArenaProp::Dummy; }
	double GetHealthFraction() const { return Health / DummyMaxHealth; }
	bool IsKnockedOut() const { return KnockoutS > 0.0; }
	bool IsBurning() const { return Burn.bBurning; }

	/** Water barrels: water left, and taking some (a waterbender's whip); returns what was taken. */
	double GetWaterKg() const { return WaterKg; }
	double TakeWater(double MassKg);
	/** Puts back water that was taken (a whip that could not form). */
	void ReturnWater(double MassKg) { WaterKg += FMath::Max(MassKg, 0.0); }

	/** Smashes a crate or barrel next frame: boards fly, a barrel's water spills, a burning crate drops its fire. */
	void RequestSmash() { bSmashPending = true; }

	/** Every dummy hit (damage, or 0 with bKnockout for the knockout) for the HUD's numbers. */
	DECLARE_MULTICAST_DELEGATE_FourParams(FOnPropHit, const ABendingPropActor* /*Prop*/, const FVector& /*Location*/, double /*Damage*/, bool /*bKnockout*/);
	static FOnPropHit OnPropHit;

	static constexpr double DummyMaxHealth = 100.0;
	/** Damage per m/s a blow changes a dummy's speed. */
	static constexpr double DamagePerMs = 15.0;
	static constexpr double KnockoutSeconds = 6.0;

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

	/** The fire of a burning prop (its flame volume lives in the interaction simulation). */
	UPROPERTY(VisibleAnywhere, Category = "Prop")
	TObjectPtr<UStaticMeshComponent> FlameMesh;

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

	UFUNCTION()
	void HandleImpulse(FVector ImpulseKgCmS);

	UFUNCTION()
	void HandleBodyHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit);

	/** A blow changed its speed by DeltaVCmS: dummies are hurt, crates and barrels may smash. */
	void ReactToBlow(const FVector& DeltaVCmS);
	void TickCombustion(float DeltaSeconds);
	void TickDummy(float DeltaSeconds);
	void GetCombustionPoints(FVector& OutCenterCm, FVector& OutFlameCm) const;
	void ShowBurning(double BurntFraction, bool bBurning, bool bBurntOut);
	void Smash();
	void Respawn();
	/** A line in the bender's message feed. */
	void Announce(const FString& Text) const;

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
	BendingSim::FCombustible Burn;
	double LastShownBurnt = -1.0;
	double WaterKg = 0.0;
	double Health = DummyMaxHealth;
	double KnockoutS = 0.0;
	double ProtectS = 0.0;
	double PendingDamage = 0.0;
	double PendingAgeS = 0.0;
	double QuietS = 0.0;
	FVector HomeLocation = FVector::ZeroVector;
	FRotator HomeRotation = FRotator::ZeroRotator;
	double FacingYawDeg = 0.0;
	bool bSmashPending = false;
	bool bArenaProp = false;
	bool bThrownRock = false;
	bool bHeld = false;
	bool bFlameLit = false;
	bool bMelted = false;
};
