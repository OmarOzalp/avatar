#pragma once

#include "CoreMinimal.h"
#include "Interaction/ElementalVolumeTypes.h"
#include "Sim/BendingSimWorld.h"
#include "Subsystems/WorldSubsystem.h"
#include "Templates/UniquePtr.h"
#include "BendingInteractionSubsystem.generated.h"

class UElementalReaction;
class UElementalReactionSet;
class UElementalVolumeComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FElementalReactionSignature, const FElementalReactionEvent&, Event);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FElementalFreeVolumeSignature, FElementalVolumeHandle, Handle, EElementalSubstance, Substance);
DECLARE_MULTICAST_DELEGATE_OneParam(FElementalReactionNativeSignature, const FElementalReactionEvent&);

/** Wet ground (Blueprint view of BendingSim::FMoisturePatch). */
USTRUCT(BlueprintType)
struct BENDING_API FSurfaceMoisturePatch
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Moisture")
	FVector CenterCm = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Moisture")
	float RadiusCm = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Moisture")
	double WaterKg = 0.0;

	UPROPERTY(BlueprintReadOnly, Category = "Moisture")
	float Porosity = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Moisture")
	float Saturation = 0.f;
};

/**
 * Cross-element interaction simulation for one world: an engine adapter over BendingSim::FSimWorld.
 *
 * The physics (fixed-step broadphase, contacts, reactions, steam, moisture, depletion) lives in the
 * engine-independent kernel under Sim/, which is compiled and tested outside Unreal (Tests/run_all.sh) and also
 * runs in the WebAssembly sandbox. This class only:
 *   - feeds the kernel settings from Project Settings and the world's gravity,
 *   - syncs UElementalVolumeComponent owners in before the step and out after (impulses, phase changes, depletion);
 *     native owners that add owned volumes to GetSimWorld() directly (FWaterWhip) sync and consume their own,
 *   - registers designer reaction rules, and
 *   - converts kernel events into Blueprint-visible delegates.
 */
UCLASS()
class BENDING_API UBendingInteractionSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	//~ USubsystem
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ FTickableGameObject
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	// ---------------------------------------------------------------- Volumes

	FElementalVolumeHandle RegisterVolume(const FElementalVolumeState& InitialState, UElementalVolumeComponent* Owner = nullptr);
	void UnregisterVolume(FElementalVolumeHandle Handle);

	[[nodiscard]] bool IsValidVolume(FElementalVolumeHandle Handle) const;
	/** Registered but spent (mass ran out, flame went out); waiting for its owner to unregister or re-register. */
	[[nodiscard]] bool IsVolumeDepleted(FElementalVolumeHandle Handle) const;
	/** Direct access to the simulated body for native owners that drive matter themselves. */
	[[nodiscard]] BendingSim::FVolume* GetVolume(FElementalVolumeHandle Handle);
	[[nodiscard]] const BendingSim::FVolume* GetVolume(FElementalVolumeHandle Handle) const;
	[[nodiscard]] UElementalVolumeComponent* GetVolumeOwner(FElementalVolumeHandle Handle) const;

	UFUNCTION(BlueprintPure, Category = "Bending|Interaction")
	bool GetVolumeState(FElementalVolumeHandle Handle, FElementalVolumeState& OutState) const;

	UFUNCTION(BlueprintPure, Category = "Bending|Interaction")
	int32 GetNumVolumes() const;

	/** Volumes whose shape comes within RadiusCm of CenterCm, filtered by ElementalSubstance::ToMask bits. */
	void QueryVolumes(const FVector& CenterCm, double RadiusCm, uint32 SubstanceMask, TArray<FElementalVolumeHandle>& OutHandles) const;

	/** Native access to the kernel world, for tools and tests. Null outside Initialize..Deinitialize. */
	[[nodiscard]] BendingSim::FSimWorld* GetSimWorld() { return SimWorld.Get(); }
	[[nodiscard]] const BendingSim::FSimWorld* GetSimWorld() const { return SimWorld.Get(); }

	// ---------------------------------------------------------------- Bender commands

	/**
	 * Adds heat (J); negative extracts it (freezing a water whip). Phase changes are reported as events.
	 * Flame cannot be heated past MaxFlameTemperatureK; OutAcceptedHeatJ is what a bender should pay chi for.
	 */
	UFUNCTION(BlueprintCallable, Category = "Bending|Interaction")
	FElementalPhaseChange TransferHeat(FElementalVolumeHandle Handle, double HeatJ, double& OutAcceptedHeatJ);

	/** Impulse (kg*cm/s) applied on the next step and forwarded to the owner like any reaction impulse. */
	UFUNCTION(BlueprintCallable, Category = "Bending|Interaction")
	void AddImpulse(FElementalVolumeHandle Handle, FVector ImpulseKgCmS);

	// ---------------------------------------------------------------- Surfaces

	/** Water hitting soil. Porosity comes from the hit physical material; rock (< 0.01) sheds water. */
	UFUNCTION(BlueprintCallable, Category = "Bending|Surfaces")
	void DepositWaterOnSurface(FVector LocationCm, double WaterMassKg, float SoilPorosity);

	UFUNCTION(BlueprintPure, Category = "Bending|Surfaces")
	float GetSurfaceSaturationAt(FVector LocationCm) const;

	/** Ground friction multiplier at a location (1 = dry, MudFrictionRatio = saturated mud). */
	UFUNCTION(BlueprintPure, Category = "Bending|Surfaces")
	float GetSurfaceTractionMultiplierAt(FVector LocationCm) const;

	UFUNCTION(BlueprintPure, Category = "Bending|Surfaces")
	TArray<FSurfaceMoisturePatch> GetMoisturePatches() const;

	// ---------------------------------------------------------------- Reactions

	void SetReactionSet(UElementalReactionSet* InReactionSet);
	void AddReaction(UElementalReaction* Reaction);

	// ---------------------------------------------------------------- Events

	/** Continuous reactions arrive aggregated every ReactionEventIntervalS; discrete ones (melting, extinguished) every frame. */
	UPROPERTY(BlueprintAssignable, Category = "Bending|Interaction")
	FElementalReactionSignature OnReaction;

	FElementalReactionNativeSignature OnReactionNative;

	/** Ownerless gas volumes (steam clouds) appearing and disappearing: hook presentation here. */
	UPROPERTY(BlueprintAssignable, Category = "Bending|Interaction")
	FElementalFreeVolumeSignature OnFreeVolumeSpawned;

	UPROPERTY(BlueprintAssignable, Category = "Bending|Interaction")
	FElementalFreeVolumeSignature OnFreeVolumeRemoved;

private:
	void ApplySettings();
	void RegisterBuiltInReactions();
	/** Re-registers every rule from ReactionSet, or the built-in set when there is none. */
	void RebuildReactions();
	void SyncFromOwners(double FrameSeconds);
	void SyncToOwners();
	void BroadcastEvents(double FrameSeconds);
	void DrawDebug() const;

	TUniquePtr<BendingSim::FSimWorld> SimWorld;

	/**
	 * Owner per kernel slot index. Explicitly null for free volumes and for volumes owned natively (FWaterWhip
	 * segments); stale only when the component died without unregistering. Every removal resets the entry to null,
	 * so a reused slot never looks stale.
	 */
	TArray<TWeakObjectPtr<UElementalVolumeComponent>> OwnerBySlot;

	UPROPERTY(Transient)
	TObjectPtr<UElementalReactionSet> ReactionSet;

	/**
	 * Keeps reaction objects alive; the kernel holds raw pointers to them. If one is destroyed anyway (asset
	 * force-deleted or reloaded during PIE), GC nulls its entry here and the next Tick rebuilds before stepping.
	 */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UElementalReaction>> Reactions;

	/** Reused every frame for FlushEvents. */
	TArray<BendingSim::FReactionEvent> EventScratch;
};
