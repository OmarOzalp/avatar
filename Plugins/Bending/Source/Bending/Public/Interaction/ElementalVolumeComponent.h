#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "Interaction/ElementalVolumeTypes.h"
#include "ElementalVolumeComponent.generated.h"

class UBendingInteractionSubsystem;
class UPrimitiveComponent;

UENUM(BlueprintType)
enum class EElementalVelocitySource : uint8
{
	/** Velocity of the simulating primitive this component is attached to; falls back to FiniteDifference. */
	AttachedPhysicsBody,
	/** Frame-to-frame displacement of the component. */
	FiniteDifference,
	/** Set by the owner every frame with SetManualVelocity (custom projectile integrators). */
	Manual
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FElementalSubstanceChangedSignature, EElementalSubstance, OldSubstance, EElementalSubstance, NewSubstance);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FElementalImpulseSignature, FVector, ImpulseKgCmS);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FElementalDepletedSignature, UElementalVolumeComponent*, Volume);

/**
 * Gives an actor an elemental body in UBendingInteractionSubsystem. Attach it to the boulder's geometry
 * collection, a water-whip segment, a fire projectile or an air wave front.
 *
 * Each frame the component pushes its transform and velocity into the simulation, then receives the
 * accumulated reaction impulse (applied to the attached simulating body when there is one), substance
 * changes (water froze, ice melted) and depletion (flame extinguished, water boiled away).
 * Activation registers it and deactivation unregisters it, so it works with pooled actors.
 */
UCLASS(ClassGroup = (Bending), meta = (BlueprintSpawnableComponent))
class BENDING_API UElementalVolumeComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UElementalVolumeComponent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	/** Matter and shape to register with. Location and velocity are filled from the component every frame. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Elemental")
	FElementalVolumeState InitialState;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Elemental")
	EElementalVelocitySource VelocitySource = EElementalVelocitySource::AttachedPhysicsBody;

	/** Apply reaction impulses to the simulating primitive this component is attached to. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Elemental")
	bool bApplyImpulsesToAttachedBody = true;

	/**
	 * Start from the attached body's mass and push simulated mass changes back to it
	 * (erosion lightens a boulder, absorbed water makes it heavier).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Elemental")
	bool bSyncMassWithAttachedBody = false;

	UFUNCTION(BlueprintCallable, Category = "Elemental")
	void RegisterVolume();

	UFUNCTION(BlueprintCallable, Category = "Elemental")
	void UnregisterVolume();

	UFUNCTION(BlueprintPure, Category = "Elemental")
	bool IsVolumeRegistered() const;

	UFUNCTION(BlueprintPure, Category = "Elemental")
	FElementalVolumeHandle GetVolumeHandle() const { return Handle; }

	/** Current simulated state (mass, temperature, substance...). False when not registered. */
	UFUNCTION(BlueprintPure, Category = "Elemental")
	bool GetSimulatedState(FElementalVolumeState& OutState) const;

	/** Direct access to the simulated body for owners that drive matter themselves (a firebender feeding a stream). */
	BendingSim::FVolume* GetMutableSimulatedState();

	UFUNCTION(BlueprintCallable, Category = "Elemental")
	void SetManualVelocity(FVector VelocityCmS);

	/**
	 * Adds heat (J); negative extracts it. Freezing a water whip is AddHeat(-BendingSim::HeatToFreeze(volume)).
	 * OutAcceptedHeatJ is what was actually absorbed (flame is capped at the adiabatic temperature): bill chi on that.
	 */
	UFUNCTION(BlueprintCallable, Category = "Elemental")
	FElementalPhaseChange AddHeat(double HeatJ, double& OutAcceptedHeatJ);

	UPROPERTY(BlueprintAssignable, Category = "Elemental")
	FElementalSubstanceChangedSignature OnSubstanceChanged;

	/** Reaction impulse (kg*cm/s) received this frame. Owners without a simulating body apply it themselves. */
	UPROPERTY(BlueprintAssignable, Category = "Elemental")
	FElementalImpulseSignature OnImpulseReceived;

	/** Mass ran out or the flame went out. The owner usually deactivates and returns to its pool. */
	UPROPERTY(BlueprintAssignable, Category = "Elemental")
	FElementalDepletedSignature OnDepleted;

	//~ Called by UBendingInteractionSubsystem
	void PreSimulationSync(BendingSim::FVolume& Volume, double FrameSeconds);
	void PostSimulationSync(const BendingSim::FVolume& Volume, const FVector& FrameImpulseKgCmS, EElementalSubstance PreviousSubstance,
		bool bDepleted, bool bNewlyDepleted);

	//~ UActorComponent
	virtual void Activate(bool bReset = false) override;
	virtual void Deactivate() override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

private:
	UPrimitiveComponent* GetSimulatingParent() const;
	UBendingInteractionSubsystem* GetInteractionSubsystem() const;
	FVector GetWorldCapsuleHalfAxis() const;

	FElementalVolumeHandle Handle;
	FVector LastLocation = FVector::ZeroVector;
	FVector ManualVelocityCmS = FVector::ZeroVector;
	bool bHasLastLocation = false;
};
