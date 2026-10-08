#include "Sandbox/BendingTechniqueAbility.h"

#include "GameFramework/Actor.h"
#include "Sandbox/BendingTechniqueComponent.h"
#include "Sandbox/BendingTechniqueMove.h"

const UBendingTechniqueMove* UBendingTechniqueAbility::GetTechniqueMove() const
{
	return Cast<UBendingTechniqueMove>(GetMoveDefinition());
}

UBendingTechniqueComponent* UBendingTechniqueAbility::GetTechniqueComponent() const
{
	const AActor* Avatar = GetAvatarActorFromActorInfo();
	return Avatar ? Avatar->FindComponentByClass<UBendingTechniqueComponent>() : nullptr;
}

void UBendingTechniqueAbility::OnStartupBegin_Implementation()
{
	UBendingTechniqueComponent* Techniques = GetTechniqueComponent();
	if (const UBendingTechniqueMove* Move = GetTechniqueMove(); Techniques && Move)
	{
		Techniques->OnTechniqueStartup(Move);
	}
}

void UBendingTechniqueAbility::OnActiveBegin_Implementation()
{
	UBendingTechniqueComponent* Techniques = GetTechniqueComponent();
	if (const UBendingTechniqueMove* Move = GetTechniqueMove(); Techniques && Move)
	{
		Techniques->OnTechniqueActive(Move);
	}
}

void UBendingTechniqueAbility::OnRecoveryBegin_Implementation()
{
	UBendingTechniqueComponent* Techniques = GetTechniqueComponent();
	if (const UBendingTechniqueMove* Move = GetTechniqueMove(); Techniques && Move)
	{
		Techniques->OnTechniqueRecovery(Move);
	}
}

void UBendingTechniqueAbility::OnInputReleasedDuringMove_Implementation(float HeldSeconds)
{
	UBendingTechniqueComponent* Techniques = GetTechniqueComponent();
	if (const UBendingTechniqueMove* Move = GetTechniqueMove(); Techniques && Move)
	{
		Techniques->OnTechniqueInputReleased(Move, HeldSeconds);
	}
}

void UBendingTechniqueAbility::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	// Read the move while the ability is still active; the component hears about the end after GAS has finished it.
	UBendingTechniqueComponent* Techniques = nullptr;
	const UBendingTechniqueMove* Move = nullptr;
	if (IsEndAbilityValid(Handle, ActorInfo))
	{
		Techniques = GetTechniqueComponent();
		Move = GetTechniqueMove();
	}
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
	if (Techniques && Move)
	{
		Techniques->OnTechniqueEnded(Move, bWasCancelled);
	}
}
