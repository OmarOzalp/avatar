#pragma once

#include "CoreMinimal.h"
#include "Abilities/BendingGameplayAbility.h"
#include "BendingTechniqueAbility.generated.h"

class UBendingTechniqueComponent;
class UBendingTechniqueMove;

/**
 * The one ability class behind every sandbox technique. GAS and UBendingComponent run it like any bending move
 * (cost, frame-data phases, cancel windows); each phase is forwarded with its UBendingTechniqueMove to the owner's
 * UBendingTechniqueComponent, which does the physics.
 */
UCLASS()
class BENDING_API UBendingTechniqueAbility : public UBendingGameplayAbility
{
	GENERATED_BODY()

public:
	//~ UGameplayAbility
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;
	//~ End UGameplayAbility

protected:
	virtual void OnStartupBegin_Implementation() override;
	virtual void OnActiveBegin_Implementation() override;
	virtual void OnRecoveryBegin_Implementation() override;
	virtual void OnInputReleasedDuringMove_Implementation(float HeldSeconds) override;

private:
	const UBendingTechniqueMove* GetTechniqueMove() const;
	UBendingTechniqueComponent* GetTechniqueComponent() const;
};
