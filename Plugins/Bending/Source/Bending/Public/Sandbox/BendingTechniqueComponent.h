#pragma once

#include "CoreMinimal.h"
#include "BendingTypes.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "Sandbox/BendingTechniqueTypes.h"
#include "Sim/BendingTerrain.h"
#include "BendingTechniqueComponent.generated.h"

class ABendingProjectile;
class ABendingPropActor;
class ABendingSandboxArena;
class ABendingWaterWhipActor;
class UBendingComponent;
class UBendingTechniqueMove;
class USceneComponent;

/** A line for the HUD: what a technique did or why it could not. */
struct FBendingTechniqueMessage
{
	FString Text;
	FLinearColor Color = FLinearColor::White;
	double TimeSeconds = 0.0;
};

/**
 * Executes the kernel's sandbox techniques for a bender. UBendingTechniqueAbility forwards each move phase here;
 * this component turns it into matter: it draws and drives the water whip, pulls rocks out of the terrain, moves
 * soil, launches bent fire and air, and bills every joule of work through UBendingComponent::SpendChiForEnergy
 * (scaling the effect down when the bender runs short of chi).
 *
 * Held techniques (Raise / Lower Ground, Flame Stream, Gust, Ground Flame) start on their active frame and keep
 * going while the move's input stays held, independent of when the ability itself ends.
 */
UCLASS(ClassGroup = (Bending), meta = (BlueprintSpawnableComponent))
class BENDING_API UBendingTechniqueComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UBendingTechniqueComponent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	/** Where water, fire and air leave the body. Defaults to the owner's scene component named HandComponentName. */
	UFUNCTION(BlueprintCallable, Category = "Bending|Techniques")
	void SetHandComponent(USceneComponent* InHand);

	// ---------------------------------------------------------------- Called by UBendingTechniqueAbility

	void OnTechniqueStartup(const UBendingTechniqueMove* Move);
	void OnTechniqueActive(const UBendingTechniqueMove* Move);
	void OnTechniqueRecovery(const UBendingTechniqueMove* Move);
	void OnTechniqueInputReleased(const UBendingTechniqueMove* Move, float HeldSeconds);
	void OnTechniqueEnded(const UBendingTechniqueMove* Move, bool bWasCancelled);

	// ---------------------------------------------------------------- State for the body, the whip and the HUD

	UFUNCTION(BlueprintPure, Category = "Bending|Techniques")
	FVector GetHandLocation() const;

	UFUNCTION(BlueprintPure, Category = "Bending|Techniques")
	FVector GetHandVelocity() const { return HandVelocityCmS; }

	/** What the bender is looking at: the camera ray's hit, else a point straight ahead. */
	UFUNCTION(BlueprintPure, Category = "Bending|Techniques")
	FVector GetAimPoint() const { return AimPoint; }

	UFUNCTION(BlueprintPure, Category = "Bending|Techniques")
	bool HasWaterWhip() const;

	ABendingWaterWhipActor* GetWaterWhip() const;

	/** The whip stays stretched toward the aim while the Water Whip input is held in the water stance. */
	bool ShouldHoldWhipExtended() const;

	UFUNCTION(BlueprintPure, Category = "Bending|Techniques")
	bool IsHoldingRock() const;

	/** A held technique is running (its input is still down). */
	UFUNCTION(BlueprintPure, Category = "Bending|Techniques")
	bool IsSustaining() const { return Holds.Num() > 0; }

	/** The most recently started technique that is still being held, or None. */
	UFUNCTION(BlueprintPure, Category = "Bending|Techniques")
	EBendingTechnique GetSustainedTechnique() const;

	/** One line about matter under control: the whip's water, a lifted rock, held techniques. */
	FString GetStatusText() const;

	const TArray<FBendingTechniqueMessage>& GetMessages() const { return Messages; }
	void AddMessage(const FString& Text, const FLinearColor& Color = FLinearColor::White);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** Name of the owner's scene component used as the hand when none was set. */
	UPROPERTY(EditAnywhere, Category = "Bending|Techniques")
	FName HandComponentName = TEXT("HandR");

	/** Longest aim trace from the camera. */
	UPROPERTY(EditAnywhere, Category = "Bending|Techniques", meta = (ClampMin = 100.0, Units = "cm"))
	float AimRangeCm = 6000.f;

	/** Aim point used when the camera ray hits nothing. */
	UPROPERTY(EditAnywhere, Category = "Bending|Techniques", meta = (ClampMin = 100.0, Units = "cm"))
	float FallbackAimDistanceCm = 3000.f;

	/** Body mass launched by Air Jump. */
	UPROPERTY(EditAnywhere, Category = "Bending|Techniques", meta = (ClampMin = 1.0, Units = "kg"))
	float BenderMassKg = 70.f;

	/** Horizontal distance in front of the bender a thrown rock is pulled out of the ground. */
	UPROPERTY(EditAnywhere, Category = "Bending|Techniques", meta = (ClampMin = 50.0, Units = "cm"))
	float RockPullDistanceCm = 220.f;

	/** An earth wall never rises closer than this to the bender. */
	UPROPERTY(EditAnywhere, Category = "Bending|Techniques", meta = (ClampMin = 0.0, Units = "cm"))
	float MinWallDistanceCm = 260.f;

	/** Speed at which a held ground flame follows the aim along the ground. */
	UPROPERTY(EditAnywhere, Category = "Bending|Techniques", meta = (ClampMin = 0.0))
	float GroundFlameFollowCmS = 400.f;

	/** Tip speed (m/s) billed as the whip's kinetic energy when it lashes. */
	UPROPERTY(EditAnywhere, Category = "Bending|Techniques", meta = (ClampMin = 0.0))
	float WhipLashSpeedMs = 12.f;

	/** Thrown rocks beyond this many crumble back into the ground, oldest first (their soil returns to the terrain). */
	UPROPERTY(EditAnywhere, Category = "Bending|Techniques", meta = (ClampMin = 1))
	int32 MaxThrownRocks = 10;

private:
	struct FTechniqueHold
	{
		EBendingTechnique Technique = EBendingTechnique::None;
		FGameplayTag InputTag;
		double StartTimeSeconds = 0.0;
		/** A quick tap still runs the technique for its active frames. */
		double MinSeconds = 0.0;
		/** Fractional puffs owed (streams). */
		double EmitAccumulator = 0.0;
		/** Raise / Lower Ground work here, fixed when the hold starts. */
		FVector GroundCenter = FVector::ZeroVector;
		double MovedKg = 0.0;
		double WorkJ = 0.0;
		TWeakObjectPtr<ABendingProjectile> Flame;
	};

	struct FRisingWall
	{
		BendingSim::FTerrainBrush Core;
		BendingSim::FTerrainBrush Trench;
		double RemainingM3 = 0.0;
		double RateM3S = 0.0;
		double MovedKg = 0.0;
		double WorkJ = 0.0;
	};

	// ---------------------------------------------------------------- Techniques
	void BeginWaterWhip();
	void LashWaterWhip();
	void FreezeOrThawWhip();
	void ReleaseWhip(bool bAsBlast);
	void BeginRockThrow(double StartupSeconds);
	void LaunchRock();
	void DropHeldRock();
	void BeginEarthWall();
	void DoAirJump();
	void StartHold(const UBendingTechniqueMove* Move);
	/** False when the hold must stop (out of chi, its flame went out). */
	bool SustainHold(FTechniqueHold& Hold, float DeltaSeconds);
	void EndHold(const FTechniqueHold& Hold);
	bool SustainTerraform(FTechniqueHold& Hold, float DeltaSeconds);
	bool SustainGroundFlame(FTechniqueHold& Hold, float DeltaSeconds);

	/**
	 * Spawns bent flame from the hand toward the aim. The heat it carries is paid in chi: less chi, less flame, and
	 * nothing (false) below MinChiFraction of the full flame.
	 */
	bool ShootFlame(double MassKg, double TemperatureK, double SpeedMs, bool bWithLight, double MinChiFraction);
	/** Spawns bent air from Origin. Its kinetic energy is paid in chi: less chi, slower air; nothing below MinChiFraction. */
	bool ShootAir(double RadiusCm, double Compression, double SpeedMs, const FVector& Origin, const FVector& Direction, double MinChiFraction);
	ABendingProjectile* SpawnWaterBall(double MassKg, double TemperatureK, const FVector& LocationCm, const FVector& VelocityCmS);

	// ---------------------------------------------------------------- Per-frame
	void UpdateAim();
	void UpdateHand(float DeltaSeconds);
	void TickHeldRock();
	void TickWalls(float DeltaSeconds);
	void TickHolds(float DeltaSeconds);
	void TrimThrownRocks();

	// ---------------------------------------------------------------- Helpers
	UBendingComponent* GetBendingComponent() const;
	ABendingSandboxArena* GetArena() const;
	BendingSim::FTerrain* GetTerrain() const;
	/** Pays chi for physical work and returns the joules granted (all of it with no chi pool). */
	double SpendEnergy(double RequestedJ, EBendingEnergyKind Kind);
	/** Pays for continuing work; false (with a message) when less than half of it could be paid. */
	bool PayForWork(double RequestedJ, EBendingEnergyKind Kind);
	FVector GetAimDirectionFrom(const FVector& Origin) const;
	FVector GetFlatAimDirection() const;
	/** Ground under the aim, its horizontal distance from the bender clamped to [MinRangeCm, MaxRangeCm]. */
	FVector GetAimGroundPoint(double MaxRangeCm, double MinRangeCm) const;
	double GetGroundHeight(const FVector& LocationCm) const;
	double GetGravityMs2() const;
	double GetWorldTime() const;
	void AddOutOfChiMessage();

	TWeakObjectPtr<USceneComponent> Hand;
	TWeakObjectPtr<UBendingComponent> CachedBending;
	mutable TWeakObjectPtr<ABendingSandboxArena> CachedArena;
	TWeakObjectPtr<ABendingWaterWhipActor> Whip;
	TWeakObjectPtr<ABendingPropActor> HeldRock;
	TArray<TWeakObjectPtr<ABendingPropActor>> ThrownRocks;

	TArray<FTechniqueHold> Holds;
	TArray<FRisingWall> Walls;
	TArray<FBendingTechniqueMessage> Messages;

	FVector AimPoint = FVector::ZeroVector;
	FVector ViewDirection = FVector::ForwardVector;
	FVector LastHandLocation = FVector::ZeroVector;
	FVector HandVelocityCmS = FVector::ZeroVector;
	bool bHasLastHandLocation = false;

	FVector RockRiseFrom = FVector::ZeroVector;
	FVector RockRiseTo = FVector::ZeroVector;
	double RockRiseStartSeconds = 0.0;
	double RockRiseSeconds = 0.3;
	/** The whip was drawn by the current Water Whip move: its active frame must not lash yet. */
	bool bWhipDrawnThisMove = false;
};
