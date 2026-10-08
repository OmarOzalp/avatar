#pragma once

#include "CoreMinimal.h"
#include "Interaction/ElementalVolumeTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "BendingInteractionSubsystem.generated.h"

class UBendingSettings;
class UElementalReaction;
class UElementalReactionSet;
class UElementalVolumeComponent;
struct FElementalReactionContext;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FElementalReactionSignature, const FElementalReactionEvent&, Event);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FElementalFreeVolumeSignature, FElementalVolumeHandle, Handle, EElementalSubstance, Substance);
DECLARE_MULTICAST_DELEGATE_OneParam(FElementalReactionNativeSignature, const FElementalReactionEvent&);

/**
 * Wet ground. Landscape cannot carry per-location physical materials at runtime, so moisture lives here
 * and traction is queried by position (characters scale ground friction, bodies swap physical materials).
 */
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
	float Porosity = 0.3f;

	/** Water (kg) the soil under this patch can hold: rho_water * porosity * depth * area. */
	[[nodiscard]] double GetCapacityKg(double SoilDepthM) const;
	[[nodiscard]] double GetSaturation(double SoilDepthM) const;
};

/**
 * Cross-element interaction simulation for one world.
 *
 * Bent matter registers as elemental volumes (UElementalVolumeComponent, or ownerless gas such as steam).
 * Each fixed step (60 Hz by default):
 *   1. sweep-and-prune broadphase along the axis of greatest spread, filtered by the reaction table;
 *   2. sphere/capsule narrowphase producing an exchange area, immersion and normal per pair;
 *   3. data-driven reactions exchange mass, momentum and heat;
 *   4. ambient heat exchange, buoyancy of free gas, moisture drying, depletion.
 * Owners sync in once per frame before stepping and receive accumulated impulses after.
 *
 * Volumes are POD slots in a sparse array, addressed by serial-checked handles; nothing in the step
 * touches UObjects, and every outward callback is deferred until the step completes.
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
	[[nodiscard]] FElementalVolumeState* GetVolume(FElementalVolumeHandle Handle);
	[[nodiscard]] const FElementalVolumeState* GetVolume(FElementalVolumeHandle Handle) const;
	[[nodiscard]] UElementalVolumeComponent* GetVolumeOwner(FElementalVolumeHandle Handle) const;

	UFUNCTION(BlueprintPure, Category = "Bending|Interaction")
	bool GetVolumeState(FElementalVolumeHandle Handle, FElementalVolumeState& OutState) const;

	UFUNCTION(BlueprintPure, Category = "Bending|Interaction")
	int32 GetNumVolumes() const { return Volumes.Num(); }

	/** Volumes whose shape comes within RadiusCm of CenterCm, filtered by ElementalSubstance::ToMask bits. */
	void QueryVolumes(const FVector& CenterCm, double RadiusCm, uint32 SubstanceMask, TArray<FElementalVolumeHandle>& OutHandles) const;

	// ---------------------------------------------------------------- Bender commands

	/** Adds heat (J); negative extracts it (freezing a water whip). Phase changes are reported as events. */
	UFUNCTION(BlueprintCallable, Category = "Bending|Interaction")
	FElementalPhaseChange TransferHeat(FElementalVolumeHandle Handle, double HeatJ);

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
	static float GetTractionMultiplierForSaturation(float Saturation);

	const TArray<FSurfaceMoisturePatch>& GetMoisturePatches() const { return MoisturePatches; }

	// ---------------------------------------------------------------- Reactions

	void SetReactionSet(UElementalReactionSet* InReactionSet);
	void AddReaction(UElementalReaction* Reaction);

	/** Called through FElementalReactionContext. */
	void EmitReactionEvent(const FElementalReactionContext& Context, EElementalReactionType Type, double MassKg, double EnergyJ);
	void QueueFreeVolume(const FElementalVolumeState& State);

	// ---------------------------------------------------------------- Events

	/** Continuous reactions arrive aggregated every ReactionEventIntervalS; discrete ones (melting, extinguished) at frame end. */
	UPROPERTY(BlueprintAssignable, Category = "Bending|Interaction")
	FElementalReactionSignature OnReaction;

	FElementalReactionNativeSignature OnReactionNative;

	/** Ownerless gas volumes (steam clouds) appearing and disappearing: hook presentation here. */
	UPROPERTY(BlueprintAssignable, Category = "Bending|Interaction")
	FElementalFreeVolumeSignature OnFreeVolumeSpawned;

	UPROPERTY(BlueprintAssignable, Category = "Bending|Interaction")
	FElementalFreeVolumeSignature OnFreeVolumeRemoved;

private:
	struct FVolumeSlot
	{
		FElementalVolumeState State;
		TWeakObjectPtr<UElementalVolumeComponent> Owner;
		/** Impulse accumulated over this frame's steps, handed to the owner after stepping (kg*cm/s). */
		FVector FrameImpulseKgCmS = FVector::ZeroVector;
		EElementalSubstance SubstanceAtFrameStart = EElementalSubstance::None;
		uint32 Serial = 0;
		bool bOwned = false;
		bool bDepleted = false;
		bool bDepletionReported = false;
	};

	struct FBroadphaseEntry
	{
		int32 Index = INDEX_NONE;
		double Min = 0.0;
		double Max = 0.0;
	};

	struct FReactionEventKey
	{
		FElementalVolumeHandle A;
		FElementalVolumeHandle B;
		EElementalReactionType Type = EElementalReactionType::None;

		bool operator==(const FReactionEventKey& Other) const { return A == Other.A && B == Other.B && Type == Other.Type; }

		friend uint32 GetTypeHash(const FReactionEventKey& Key)
		{
			return HashCombine(HashCombine(GetTypeHash(Key.A), GetTypeHash(Key.B)), ::GetTypeHash(static_cast<uint8>(Key.Type)));
		}
	};

	void StepSimulation(double DeltaSeconds);
	void SyncFromOwners(double FrameSeconds);
	void SyncToOwners();
	void UpdateDerivedShapes();
	void FindAndResolveContacts(double DeltaSeconds);
	bool ComputeContact(const FElementalVolumeState& A, const FElementalVolumeState& B, FElementalContact& OutContact) const;
	void DispatchReactions(int32 IndexA, int32 IndexB, const FElementalContact& Contact, double DeltaSeconds);
	void ExchangeHeatWithAmbient(double DeltaSeconds);
	void UpdateMoisturePatches(double DeltaSeconds);
	void IntegrateImpulses();
	void IntegrateFreeVolumes(double DeltaSeconds);
	void ProcessFreeVolumeSpawns();
	void ResolveDepletion();
	void FlushEvents(double FrameSeconds);
	void RecordPhaseChange(int32 Index, const FElementalPhaseChange& Change);
	void AccumulateEvent(const FReactionEventKey& Key, EElementalSubstance SubstanceA, EElementalSubstance SubstanceB,
		const FVector& LocationCm, const FVector& Normal, double MassKg, double EnergyJ);
	void AddDiscreteEvent(EElementalReactionType Type, int32 Index, double MassKg, double EnergyJ);
	void RegisterBuiltInReactions();
	void DrawDebug() const;

	[[nodiscard]] FElementalVolumeHandle MakeHandle(int32 Index) const;
	[[nodiscard]] FVolumeSlot* FindSlot(FElementalVolumeHandle Handle);
	[[nodiscard]] const FVolumeSlot* FindSlot(FElementalVolumeHandle Handle) const;

	TSparseArray<FVolumeSlot> Volumes;
	uint32 NextSerial = 1;
	int32 NumFreeVolumes = 0;

	UPROPERTY(Transient)
	TObjectPtr<UElementalReactionSet> ReactionSet;

	/** Keeps reactions alive; ReactionTable holds raw pointers into it keyed by substance pair. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UElementalReaction>> Reactions;

	TMap<uint16, TArray<UElementalReaction*, TInlineAllocator<2>>> ReactionTable;

	TArray<FBroadphaseEntry> BroadphaseScratch;
	TArray<FElementalVolumeState> PendingFreeVolumes;
	TMap<FReactionEventKey, FElementalReactionEvent> PendingEvents;
	TArray<FElementalReactionEvent> DiscreteEvents;
	TArray<TPair<FElementalVolumeHandle, EElementalSubstance>> SpawnedFreeVolumes;
	TArray<TPair<FElementalVolumeHandle, EElementalSubstance>> RemovedFreeVolumes;
	TArray<FSurfaceMoisturePatch> MoisturePatches;

	double StepAccumulator = 0.0;
	double EventWindowSeconds = 0.0;
};
