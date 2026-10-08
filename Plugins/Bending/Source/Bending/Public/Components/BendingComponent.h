#pragma once

#include "CoreMinimal.h"
#include "ActiveGameplayEffectHandle.h"
#include "BendingTypes.h"
#include "Components/ActorComponent.h"
#include "Engine/EngineTypes.h"
#include "Engine/StreamableManager.h"
#include "GameplayAbilitySpecHandle.h"
#include "GameplayTagContainer.h"
#include "BendingComponent.generated.h"

class UAbilitySystemComponent;
class UAnimMontage;
class UAnimSequenceBase;
class UBendingDiscipline;
class UBendingGameplayAbility;
class UBendingMoveDefinition;
class UGameplayEffect;
class UMotionWarpingComponent;
class USkeletalMeshComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FBendingPhaseChangedSignature, UBendingComponent*, BendingComponent, EBendingPhase, Phase, const UBendingMoveDefinition*, Move);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FBendingStanceChangedSignature, EBendingElement, OldElement, EBendingElement, NewElement);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FBendingMoveHitSignature, const UBendingMoveDefinition*, Move, AActor*, Victim);

USTRUCT()
struct FBendingGrantedMove
{
	GENERATED_BODY()

	UPROPERTY()
	TObjectPtr<const UBendingMoveDefinition> Move = nullptr;

	UPROPERTY()
	TObjectPtr<const UBendingDiscipline> Discipline = nullptr;

	FGameplayAbilitySpecHandle Handle;
};

/**
 * The bender's casting brain. Sits beside the owner's UAbilitySystemComponent and owns everything GAS
 * does not model on its own:
 *
 *  - Stance: which element's moves the shared inputs map to.
 *  - Input: press/release routing, a single-slot input buffer measured in 60 Hz frames.
 *  - Frame data: Startup / Active / Recovery phases (notify- or timer-driven), loose state tags,
 *    phase gameplay events, recovery and hit-confirm cancel windows, per-phase montage re-timing.
 *  - Targeting: soft lock-on feeding Motion Warping during startup.
 *  - Resources: chi priced in joules of physical work (SpendChiForEnergy).
 *
 * Cost, cooldowns, attributes and activation stay in GAS; element physics lives in the element actors
 * and UBendingInteractionSubsystem.
 */
UCLASS(ClassGroup = (Bending), meta = (BlueprintSpawnableComponent))
class BENDING_API UBendingComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UBendingComponent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	UFUNCTION(BlueprintPure, Category = "Bending", meta = (DefaultToSelf = "Actor"))
	static UBendingComponent* FindBendingComponent(const AActor* Actor);

	// ---------------------------------------------------------------- Setup

	/**
	 * Binds to an ability system, starts resource regeneration and grants DefaultDisciplines.
	 * Runs automatically on BeginPlay when the owner exposes an ability system.
	 */
	UFUNCTION(BlueprintCallable, Category = "Bending")
	void InitializeWithAbilitySystem(UAbilitySystemComponent* InAbilitySystem);

	/** Grants one ability spec per move (the move is the spec's source object) and preloads the montages. */
	UFUNCTION(BlueprintCallable, Category = "Bending")
	void GrantDiscipline(UBendingDiscipline* Discipline);

	UFUNCTION(BlueprintCallable, Category = "Bending")
	void RemoveDiscipline(UBendingDiscipline* Discipline);

	UFUNCTION(BlueprintPure, Category = "Bending")
	bool HasDiscipline(EBendingElement Element) const;

	UFUNCTION(BlueprintPure, Category = "Bending")
	const UBendingDiscipline* GetDiscipline(EBendingElement Element) const;

	// ---------------------------------------------------------------- Stance

	/** Switches which element the shared move inputs trigger. Fails if no discipline of that element is granted. */
	UFUNCTION(BlueprintCallable, Category = "Bending")
	bool SetActiveElement(EBendingElement Element);

	UFUNCTION(BlueprintPure, Category = "Bending")
	EBendingElement GetActiveElement() const { return ActiveElement; }

	// ---------------------------------------------------------------- Input

	UFUNCTION(BlueprintCallable, Category = "Bending|Input")
	void HandleInputPressed(FGameplayTag InputTag);

	UFUNCTION(BlueprintCallable, Category = "Bending|Input")
	void HandleInputReleased(FGameplayTag InputTag);

	UFUNCTION(BlueprintPure, Category = "Bending|Input")
	bool IsInputHeld(FGameplayTag InputTag) const;

	// ---------------------------------------------------------------- Move state

	UFUNCTION(BlueprintPure, Category = "Bending|Move")
	EBendingPhase GetCurrentPhase() const { return CurrentPhase; }

	/** 60 Hz frames elapsed in the current phase. */
	UFUNCTION(BlueprintPure, Category = "Bending|Move")
	int32 GetCurrentPhaseFrame() const;

	UFUNCTION(BlueprintPure, Category = "Bending|Move")
	const UBendingMoveDefinition* GetCurrentMove() const { return CurrentMove; }

	UBendingGameplayAbility* GetCurrentAbility() const { return CurrentAbility.Get(); }

	UFUNCTION(BlueprintPure, Category = "Bending|Move")
	bool IsMoveActive() const;

	/** True when a new move may interrupt the current one (recovery cancel frame reached, or hit confirmed). */
	UFUNCTION(BlueprintPure, Category = "Bending|Move")
	bool IsInCancelWindow() const;

	/** Element actors call this when the current move connects; opens hit-cancel windows. */
	UFUNCTION(BlueprintCallable, Category = "Bending|Move")
	void NotifyMoveHit(AActor* Victim);

	// ---------------------------------------------------------------- Targeting

	/** Picks the pawn best matching the aim direction within range and cone. */
	UFUNCTION(BlueprintCallable, Category = "Bending|Targeting")
	AActor* AcquireSoftTarget();

	UFUNCTION(BlueprintPure, Category = "Bending|Targeting")
	AActor* GetSoftTarget() const { return SoftTarget.Get(); }

	/** Horizontal aim: control rotation for possessed pawns, actor forward otherwise. */
	UFUNCTION(BlueprintPure, Category = "Bending|Targeting")
	FVector GetAimDirection() const;

	// ---------------------------------------------------------------- Resources

	/**
	 * Pays chi for physical work and returns the joules actually granted: the full request when
	 * affordable, otherwise what the remaining chi buys. Callers scale the physical effect by the result,
	 * so an exhausted bender genuinely cannot lift the boulder or freeze the wave.
	 */
	UFUNCTION(BlueprintCallable, Category = "Bending|Resources")
	double SpendChiForEnergy(double RequestedEnergyJ, EBendingEnergyKind Kind);

	// ---------------------------------------------------------------- Called by abilities and notifies

	void OnMoveStarted(UBendingGameplayAbility* Ability, const UBendingMoveDefinition* Move, UAnimMontage* Montage);
	void OnMoveEnded(UBendingGameplayAbility* Ability, bool bWasCancelled);
	void NotifyPhaseBegin(EBendingPhase Phase, float SegmentSeconds, USkeletalMeshComponent* MeshComp, const UAnimSequenceBase* Animation);
	void NotifyPhaseEnd(EBendingPhase Phase, USkeletalMeshComponent* MeshComp, const UAnimSequenceBase* Animation);

	// ---------------------------------------------------------------- Events

	UPROPERTY(BlueprintAssignable, Category = "Bending")
	FBendingPhaseChangedSignature OnPhaseChanged;

	UPROPERTY(BlueprintAssignable, Category = "Bending")
	FBendingStanceChangedSignature OnStanceChanged;

	UPROPERTY(BlueprintAssignable, Category = "Bending")
	FBendingMoveHitSignature OnMoveHit;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	UPROPERTY(EditAnywhere, Category = "Bending")
	TArray<TObjectPtr<UBendingDiscipline>> DefaultDisciplines;

	/** Starting stance. None = the first granted discipline's element. */
	UPROPERTY(EditAnywhere, Category = "Bending")
	EBendingElement DefaultElement = EBendingElement::None;

	/** How long (60 Hz frames) a press made during a non-cancellable phase stays queued. */
	UPROPERTY(EditAnywhere, Category = "Bending|Input", meta = (ClampMin = 0, ClampMax = 30))
	int32 InputBufferFrames = 8;

	UPROPERTY(EditAnywhere, Category = "Bending|Resources")
	TSubclassOf<UGameplayEffect> ResourceRegenEffect;

	UPROPERTY(EditAnywhere, Category = "Bending|Targeting", meta = (ClampMin = 0.0, Units = "cm"))
	float SoftTargetRangeCm = 1500.f;

	UPROPERTY(EditAnywhere, Category = "Bending|Targeting", meta = (ClampMin = 0.0, ClampMax = 180.0))
	float SoftTargetMaxAngleDeg = 50.f;

	UPROPERTY(EditAnywhere, Category = "Bending|Targeting")
	TEnumAsByte<ECollisionChannel> SoftTargetChannel = ECC_Pawn;

	/** Yaw rate (deg/s) used to face the target or aim during startup when Motion Warping is not doing it. */
	UPROPERTY(EditAnywhere, Category = "Bending|Targeting", meta = (ClampMin = 0.0))
	float StartupTurnRateDegPerSec = 1080.f;

private:
	struct FBufferedInput
	{
		FGameplayTag InputTag;
		double TimeSeconds = 0.0;
	};

	bool CanStartNewMove() const;
	bool TryStartMove(const FGameplayTag& InputTag);
	bool PassesPreActivationChecks(const FBendingGrantedMove& Granted) const;
	void EnterPhase(EBendingPhase NewPhase);
	void AdvanceTimedPhases();
	void UpdateCancelWindowTag();
	void ConsumeBufferedInput();
	void UpdateWarpTarget();
	void TurnTowardDesiredFacing(float DeltaTime);
	void FitMontageToPhase(EBendingPhase Phase, float SegmentSeconds, USkeletalMeshComponent* MeshComp) const;
	void ResetMontagePlayRate(USkeletalMeshComponent* MeshComp) const;
	void SetLooseTag(const FGameplayTag& Tag, bool bPresent);
	void RefreshTickEnabled();
	double GetWorldTime() const;
	bool IsCurrentMontage(const UAnimSequenceBase* Animation) const;

	UPROPERTY(Transient)
	TArray<FBendingGrantedMove> GrantedMoves;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UBendingDiscipline>> GrantedDisciplines;

	UPROPERTY(Transient)
	TObjectPtr<const UBendingMoveDefinition> CurrentMove;

	TWeakObjectPtr<UAbilitySystemComponent> AbilitySystem;
	TWeakObjectPtr<UBendingGameplayAbility> CurrentAbility;
	TWeakObjectPtr<UAnimMontage> CurrentMontage;
	TWeakObjectPtr<UMotionWarpingComponent> MotionWarping;
	TWeakObjectPtr<AActor> SoftTarget;

	TSet<FGameplayTag> HeldInputs;
	TOptional<FBufferedInput> BufferedInput;
	TArray<TSharedPtr<FStreamableHandle>> PreloadHandles;
	FActiveGameplayEffectHandle RegenEffectHandle;

	EBendingElement ActiveElement = EBendingElement::None;
	EBendingPhase CurrentPhase = EBendingPhase::None;
	double PhaseStartTime = 0.0;
	FVector MoveStartLocation = FVector::ZeroVector;
	TOptional<double> DesiredFacingYaw;

	/** The montage carries Bending Phase notifies; otherwise phases run on frame-data timers. */
	bool bPhasesFromNotifies = false;
	/** Recovery finished; the montage may still be blending out. */
	bool bPhasesCompleted = false;
	bool bHitConfirmed = false;
	bool bCancelWindowTagApplied = false;
};
