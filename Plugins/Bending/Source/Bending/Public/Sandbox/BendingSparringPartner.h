#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Sim/BendingSimTypes.h"
#include "Sim/BendingSparring.h"
#include "BendingSparringPartner.generated.h"

class ABendingSandboxArena;
class APawn;
class UBendingTechniqueComponent;
class UCapsuleComponent;
class UElementalVolumeComponent;
class UPointLightComponent;
class USceneComponent;
class UStaticMeshComponent;

/** A moment in a duel worth a callout: the sparring partner's, or the player's guard against it. */
enum class EBendingSparringCallout : uint8
{
	/** A blow reached the sparring partner (Amount: its damage, gathered into one hit). */
	PartnerHit,
	/** Its health ran out: it falls. */
	PartnerKnockedOut,
	/** Its flame struck the player (Amount: damage). */
	PlayerHit,
	/** The player's guard took a flame (Amount: damage let through). */
	Blocked,
	/** A perfect guard sent a flame back. */
	Parried,
	/** The player was knocked down. */
	PlayerDown,
	DuelStarted,
	DuelWon,
	DuelLost,
	DuelCalledOff
};

/** Joint angles (degrees) of the sparring partner's blocky body. */
struct FSparringPartnerPose
{
	float LegL = 0.f;
	float LegR = 0.f;
	/** Positive swings an arm forward. */
	float ArmL = 0.f;
	float ArmR = 0.f;
	/** Arms lifted out sideways (left positive, right negative). */
	float ArmRollL = 0.f;
	float ArmRollR = 0.f;
	/** Negative leans forward. */
	float SpinePitch = 0.f;
	/** Positive turns the right shoulder back. */
	float SpineYaw = 0.f;
};

/**
 * A firebender who spars with the player in the training ground. Its mind is the kernel's BendingSim::FSparringBrain,
 * the same brain the browser sandbox runs; this actor is its body, its senses and its hands:
 *
 *  - A person-sized body of basic shapes in Fire Nation red, with a 70 kg Earth capsule in the interaction simulation,
 *    so air, water and stone shove it (dv = J / m) and a hard shove hurts. It moves itself (kinematic): it steers
 *    toward the velocity the brain asks for, falls, stands on the terrain, and will not walk up a wall, into a pond or
 *    off the field.
 *  - The player's flames that strike it (ABendingProjectile), fire it stands in, and props flung into it hurt it; the
 *    brain decides what that damage means (a challenge, a guard, a stagger, a knockout).
 *  - It bends its flames as ABendingProjectiles marked as its own: they strike the player, who can guard or parry
 *    (UBendingTechniqueComponent::TakeFlameHit).
 *  - Its messages go to the player's technique feed; OnSparringCallout feeds the HUD's numbers and callouts.
 */
UCLASS()
class BENDING_API ABendingSparringPartner : public AActor
{
	GENERATED_BODY()

public:
	ABendingSparringPartner();

	/** The sparring partner in this world, or null. */
	static ABendingSparringPartner* Find(const UWorld* World);

	/** Puts it at its post (feet on the ground at InPostCm), facing YawDeg, waiting to be challenged. */
	void InitPartner(const FVector& InPostCm, double YawDeg);

	/** A flame of the player's struck it: FlameStrikeDamage * (m / 0.6 kg)^1.5; Direction is the way it flew. */
	void TakeFlameDamage(double Damage, const FVector& Direction);
	/** Damage from a blow (an ice dagger's blade); the brain decides how much of it lands. */
	void TakeHit(double Damage, const FVector& FromDirection);
	/** Shoved (kg*cm/s): its velocity changes by J / m, and a hard shove hurts like a blow. */
	void Shove(const FVector& ImpulseKgCmS);
	/** True when a point comes within ReachCm of its body (a flame's dense core reaching it). */
	bool IsStruckBy(const FVector& LocationCm, double ReachCm) const;
	/** The player was knocked down: the duel is the sparring partner's. */
	void OnPlayerKnockedDown();

	bool IsDuelActive() const { return Brain.IsDuelActive(); }
	bool IsWaiting() const { return Brain.State == BendingSim::ESparringState::Waiting; }
	bool IsDown() const { return Brain.IsDown(); }
	BendingSim::ESparringState GetState() const { return Brain.State; }
	double GetHealthFraction() const;
	int32 GetPlayerWins() const { return Brain.PlayerWins; }
	int32 GetPartnerWins() const { return Brain.PartnerWins; }
	FVector GetFeetLocation() const;
	FVector GetChestLocation() const;
	FVector GetHeadLocation() const;
	virtual FVector GetVelocity() const override { return VelocityCmS; }

	/** Hits on it, the player's guard against it, and how each duel goes, for the HUD. */
	DECLARE_MULTICAST_DELEGATE_ThreeParams(FOnSparringCallout, EBendingSparringCallout /*Callout*/, const FVector& /*Location*/, double /*Amount*/);
	static FOnSparringCallout OnSparringCallout;

	/** The size and weight of the player (an upright capsule: radius, half the core segment). */
	static constexpr double BodyRadiusCm = 32.0;
	static constexpr double BodyHalfHeightCm = 58.0;
	static constexpr double BodyMassKg = 70.0;
	/** Damage from a flame striking a fighter: FlameStrikeDamage * (mass / 0.6 kg)^1.5. */
	static constexpr double FlameStrikeDamage = 12.0;
	/** Damage per m/s a blow changes its speed (as for training dummies). */
	static constexpr double DamagePerMs = 15.0;

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/** Its collision: the player cannot walk through it, and the aim and ice daggers find it. Moved, never simulated. */
	UPROPERTY(VisibleAnywhere, Category = "Sparring")
	TObjectPtr<UCapsuleComponent> Capsule;

	/** Its body in the interaction simulation: a 70 kg Earth capsule. */
	UPROPERTY(VisibleAnywhere, Category = "Sparring")
	TObjectPtr<UElementalVolumeComponent> Volume;

	// ---------------------------------------------------------------- Body (engine basic shapes, no collision)

	/** At the feet: knocked down, the body tips over about it. */
	UPROPERTY(VisibleAnywhere, Category = "Sparring|Body")
	TObjectPtr<USceneComponent> BodyRoot;

	UPROPERTY(VisibleAnywhere, Category = "Sparring|Body")
	TObjectPtr<USceneComponent> Spine;

	UPROPERTY(VisibleAnywhere, Category = "Sparring|Body")
	TObjectPtr<USceneComponent> HipL;

	UPROPERTY(VisibleAnywhere, Category = "Sparring|Body")
	TObjectPtr<USceneComponent> HipR;

	UPROPERTY(VisibleAnywhere, Category = "Sparring|Body")
	TObjectPtr<USceneComponent> ShoulderL;

	UPROPERTY(VisibleAnywhere, Category = "Sparring|Body")
	TObjectPtr<USceneComponent> ShoulderR;

	UPROPERTY(VisibleAnywhere, Category = "Sparring|Body")
	TObjectPtr<USceneComponent> HandL;

	UPROPERTY(VisibleAnywhere, Category = "Sparring|Body")
	TObjectPtr<USceneComponent> HandR;

	UPROPERTY(VisibleAnywhere, Category = "Sparring|Body")
	TObjectPtr<UStaticMeshComponent> Head;

	/** The telegraph: flame gathering at the fist before it strikes. */
	UPROPERTY(VisibleAnywhere, Category = "Sparring|Body")
	TObjectPtr<UStaticMeshComponent> FistFlame;

	UPROPERTY(VisibleAnywhere, Category = "Sparring|Body")
	TObjectPtr<UPointLightComponent> FistLight;

	/** Every body part, coloured at BeginPlay. */
	UPROPERTY(VisibleAnywhere, Category = "Sparring|Body")
	TArray<TObjectPtr<UStaticMeshComponent>> BodyParts;

	/** Brightness of the flame at its fist when fully gathered (cd). */
	UPROPERTY(EditAnywhere, Category = "Sparring", meta = (ClampMin = 0.0))
	float FistLightCandelas = 160.f;

private:
	UFUNCTION()
	void HandleImpulse(FVector ImpulseKgCmS);

	/** Damage the brain decides on; bKnockOut takes all its health (flung off the field). */
	void Hurt(double Damage, const FVector& FromDirection, bool bKnockOut);
	/** Messages and callouts for what the brain reports. */
	void HandleEvents(const BendingSim::FSparringEvents& Events);
	/** Fire it stands in hurts it: not flames in flight (they strike), never its own. */
	void TickBurning(float DeltaSeconds);
	/** Throws coming at it while it is free to react, each offered to the brain once. */
	void OfferThreats(BendingSim::FSparringEvents& Events);
	bool OfferThreat(AActor* Thrown, const FVector& ThrownVelocityCmS, bool bHeavy, BendingSim::FSparringEvents& Events);
	/** Thrown rocks and flung props that hit it: body meets body. */
	void TickBlows(float DeltaSeconds);
	/** Steering, gravity, the ground under it and the solid props around it. */
	void TickMovement(const FVector& DesiredCmS, bool bControl, float DeltaSeconds);
	/** A blow that lands over several frames is reported as one hit, once it has gone quiet. */
	void TickHitReports(float DeltaSeconds);
	void UpdatePose(float DeltaSeconds);
	void Shoot(const BendingSim::FSparringShot& Shot);
	void SpawnFlame(const BendingSim::FVolume& Flame, bool bWithLight);
	/** Nothing between its hand and the player's chest: terrain (an earth wall) or a solid prop. */
	bool HasClearShot(const FVector& FeetCm, const APawn* Player) const;
	/** A wall (a rise of more than MaxRise), a pond or the edge of the field ahead. */
	bool IsWayBlocked(const FVector& FeetCm, const FVector& Direction, double GroundZ) const;
	/** A line in the player's message feed. */
	void Announce(const FString& Text) const;
	ABendingSandboxArena* GetArena() const;
	APawn* GetPlayerPawn() const;
	UBendingTechniqueComponent* GetPlayerTechniques() const;

	BendingSim::FSparringBrain Brain;
	FSparringPartnerPose CurrentPose;
	TArray<FLinearColor> BodyPartColors;
	mutable TWeakObjectPtr<ABendingSandboxArena> CachedArena;
	/** Throws already offered to the brain (and its own flames, which never are). */
	TSet<TWeakObjectPtr<AActor>> ThreatsSeen;
	/** Props that struck it, and when: one contact is one blow. */
	TMap<TWeakObjectPtr<AActor>, double> RecentBlows;
	FVector VelocityCmS = FVector::ZeroVector;
	/** Which way it fell (horizontal, unit). */
	FVector FallDirection = FVector::ForwardVector;
	double PendingDamage = 0.0;
	double PendingAgeS = 0.0;
	double QuietS = 0.0;
	float StridePhase = 0.f;
	/** 0 standing, 1 lying on the ground. */
	float DownWeight = 0.f;
	bool bOnGround = true;
	bool bInitialized = false;
};
