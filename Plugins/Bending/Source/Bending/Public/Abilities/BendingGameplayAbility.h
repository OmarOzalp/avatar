#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "BendingTypes.h"
#include "BendingGameplayAbility.generated.h"

class UBendingComponent;
class UBendingMoveDefinition;

/**
 * Base ability for every bending move. The move definition arrives as the spec's source object.
 *
 * Lifecycle: commit cost -> play the root-motion montage -> UBendingComponent drives Startup / Active /
 * Recovery (from notify states or frame-data timers) and sends phase events -> this class turns them into
 * OnStartupBegin / OnActiveBegin / OnRecoveryBegin. Element subclasses spawn and launch matter in OnActiveBegin.
 */
UCLASS()
class BENDING_API UBendingGameplayAbility : public UGameplayAbility
{
	GENERATED_BODY()

public:
	UBendingGameplayAbility(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	UFUNCTION(BlueprintPure, Category = "Bending")
	const UBendingMoveDefinition* GetMoveDefinition() const;

	UFUNCTION(BlueprintPure, Category = "Bending")
	UBendingComponent* GetBendingComponent() const;

	UFUNCTION(BlueprintPure, Category = "Bending")
	EBendingPhase GetCurrentPhase() const;

	/** How long the triggering input was (or still is) held: charge moves scale mass or speed with it. */
	UFUNCTION(BlueprintPure, Category = "Bending")
	float GetInputHeldSeconds() const;

	//~ UGameplayAbility
	virtual bool CheckCost(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;
	virtual void ApplyCost(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo) const override;
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;
	virtual void InputReleased(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo) override;
	//~ End UGameplayAbility

protected:
	/** First startup frame. Telegraph VFX, begin drawing water from a source, crack the ground. */
	UFUNCTION(BlueprintNativeEvent, Category = "Bending|Phases")
	void OnStartupBegin();

	/** First active frame: launch, strike, release. */
	UFUNCTION(BlueprintNativeEvent, Category = "Bending|Phases")
	void OnActiveBegin();

	UFUNCTION(BlueprintNativeEvent, Category = "Bending|Phases")
	void OnRecoveryBegin();

	/** The triggering input was released while the move runs (charge release, hold-to-sustain streams). */
	UFUNCTION(BlueprintNativeEvent, Category = "Bending|Phases")
	void OnInputReleasedDuringMove(float HeldSeconds);

	[[nodiscard]] static const UBendingMoveDefinition* FindMoveForSpec(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo);

private:
	UFUNCTION()
	void HandlePhaseEvent(FGameplayEventData Payload);

	UFUNCTION()
	void HandleMontageFinished();

	UFUNCTION()
	void HandleMontageAborted();

	double ActivationTimeSeconds = 0.0;
	double InputReleaseTimeSeconds = -1.0;
	bool bHasMontage = false;
};
