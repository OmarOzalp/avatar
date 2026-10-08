#include "Abilities/BendingGameplayAbility.h"

#include "Abilities/BendingGameplayEffects.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Abilities/Tasks/AbilityTask_WaitGameplayEvent.h"
#include "AbilitySystemComponent.h"
#include "Animation/AnimMontage.h"
#include "Attributes/BendingAttributeSet.h"
#include "BendingGameplayTags.h"
#include "BendingLog.h"
#include "Components/BendingComponent.h"
#include "Data/BendingMoveDefinition.h"
#include "Engine/World.h"

UBendingGameplayAbility::UBendingGameplayAbility(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;
}

const UBendingMoveDefinition* UBendingGameplayAbility::FindMoveForSpec(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo)
{
	UAbilitySystemComponent* AbilitySystem = ActorInfo ? ActorInfo->AbilitySystemComponent.Get() : nullptr;
	if (!AbilitySystem)
	{
		return nullptr;
	}
	const FGameplayAbilitySpec* Spec = AbilitySystem->FindAbilitySpecFromHandle(Handle);
	return Spec ? Cast<UBendingMoveDefinition>(Spec->SourceObject.Get()) : nullptr;
}

const UBendingMoveDefinition* UBendingGameplayAbility::GetMoveDefinition() const
{
	return Cast<UBendingMoveDefinition>(GetCurrentSourceObject());
}

UBendingComponent* UBendingGameplayAbility::GetBendingComponent() const
{
	return UBendingComponent::FindBendingComponent(GetAvatarActorFromActorInfo());
}

EBendingPhase UBendingGameplayAbility::GetCurrentPhase() const
{
	const UBendingComponent* Bending = GetBendingComponent();
	return Bending && Bending->GetCurrentAbility() == this ? Bending->GetCurrentPhase() : EBendingPhase::None;
}

float UBendingGameplayAbility::GetInputHeldSeconds() const
{
	const UWorld* World = GetWorld();
	if (!World || !IsActive())
	{
		return 0.f;
	}
	const double End = InputReleaseTimeSeconds >= 0.0 ? InputReleaseTimeSeconds : World->GetTimeSeconds();
	return static_cast<float>(FMath::Max(End - ActivationTimeSeconds, 0.0));
}

bool UBendingGameplayAbility::CheckCost(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, FGameplayTagContainer* OptionalRelevantTags) const
{
	if (!Super::CheckCost(Handle, ActorInfo, OptionalRelevantTags))
	{
		return false;
	}

	const UBendingMoveDefinition* Move = FindMoveForSpec(Handle, ActorInfo);
	const UAbilitySystemComponent* AbilitySystem = ActorInfo ? ActorInfo->AbilitySystemComponent.Get() : nullptr;
	if (!Move || !AbilitySystem)
	{
		return true;
	}

	bool bFound = false;
	const float Chi = AbilitySystem->GetGameplayAttributeValue(UBendingAttributeSet::GetChiAttribute(), bFound);
	if (bFound && Chi < Move->ChiCost)
	{
		return false;
	}
	const float Stamina = AbilitySystem->GetGameplayAttributeValue(UBendingAttributeSet::GetStaminaAttribute(), bFound);
	if (bFound && Stamina < Move->StaminaCost)
	{
		return false;
	}
	return true;
}

void UBendingGameplayAbility::ApplyCost(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo) const
{
	Super::ApplyCost(Handle, ActorInfo, ActivationInfo);

	const UBendingMoveDefinition* Move = FindMoveForSpec(Handle, ActorInfo);
	if (!Move || (Move->ChiCost <= 0.f && Move->StaminaCost <= 0.f))
	{
		return;
	}

	const FGameplayEffectSpecHandle SpecHandle = MakeOutgoingGameplayEffectSpec(Handle, ActorInfo, ActivationInfo,
		UBendingCostEffect::StaticClass(), GetAbilityLevel(Handle, ActorInfo));
	if (!SpecHandle.IsValid())
	{
		return;
	}
	SpecHandle.Data->SetSetByCallerMagnitude(BendingTags::SetByCaller_Bending_Chi, -Move->ChiCost);
	SpecHandle.Data->SetSetByCallerMagnitude(BendingTags::SetByCaller_Bending_Stamina, -Move->StaminaCost);
	ApplyGameplayEffectSpecToOwner(Handle, ActorInfo, ActivationInfo, SpecHandle);
}

void UBendingGameplayAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	const UBendingMoveDefinition* Move = GetMoveDefinition();
	UBendingComponent* Bending = GetBendingComponent();
	if (!Move || !Bending)
	{
		UE_LOG(LogBending, Warning, TEXT("%s activated without a move definition or bending component."), *GetName());
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	ActivationTimeSeconds = GetWorld()->GetTimeSeconds();
	InputReleaseTimeSeconds = -1.0;

	// Listen before anything can send: OnMoveStarted below immediately enters Startup.
	UAbilityTask_WaitGameplayEvent* PhaseTask = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(
		this, BendingTags::Event_Bending_Phase, nullptr, /*OnlyTriggerOnce*/ false, /*OnlyMatchExact*/ false);
	PhaseTask->EventReceived.AddDynamic(this, &ThisClass::HandlePhaseEvent);
	PhaseTask->ReadyForActivation();

	UAnimMontage* Montage = Move->Montage.Get();
	if (!Montage && !Move->Montage.IsNull())
	{
		UE_LOG(LogBending, Verbose, TEXT("Montage for %s was not preloaded; loading synchronously."), *Move->GetName());
		Montage = Move->Montage.LoadSynchronous();
	}

	bHasMontage = Montage != nullptr;
	if (bHasMontage)
	{
		UAbilityTask_PlayMontageAndWait* MontageTask = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
			this, NAME_None, Montage, /*Rate*/ 1.f, /*StartSection*/ NAME_None, /*bStopWhenAbilityEnds*/ true);
		MontageTask->OnCompleted.AddDynamic(this, &ThisClass::HandleMontageFinished);
		MontageTask->OnBlendOut.AddDynamic(this, &ThisClass::HandleMontageFinished);
		MontageTask->OnInterrupted.AddDynamic(this, &ThisClass::HandleMontageAborted);
		MontageTask->OnCancelled.AddDynamic(this, &ThisClass::HandleMontageAborted);
		MontageTask->ReadyForActivation();
	}

	Bending->OnMoveStarted(this, Move, Montage);
}

void UBendingGameplayAbility::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	if (IsEndAbilityValid(Handle, ActorInfo))
	{
		if (UBendingComponent* Bending = GetBendingComponent())
		{
			Bending->OnMoveEnded(this, bWasCancelled);
		}
	}
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void UBendingGameplayAbility::InputReleased(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo)
{
	Super::InputReleased(Handle, ActorInfo, ActivationInfo);

	if (IsActive() && InputReleaseTimeSeconds < 0.0)
	{
		InputReleaseTimeSeconds = GetWorld()->GetTimeSeconds();
		OnInputReleasedDuringMove(GetInputHeldSeconds());
	}
}

void UBendingGameplayAbility::HandlePhaseEvent(FGameplayEventData Payload)
{
	if (Payload.OptionalObject && Payload.OptionalObject != GetMoveDefinition())
	{
		return;
	}

	const FGameplayTag& EventTag = Payload.EventTag;
	if (EventTag.MatchesTagExact(BendingTags::Event_Bending_Phase_Startup))
	{
		OnStartupBegin();
	}
	else if (EventTag.MatchesTagExact(BendingTags::Event_Bending_Phase_Active))
	{
		OnActiveBegin();
	}
	else if (EventTag.MatchesTagExact(BendingTags::Event_Bending_Phase_Recovery))
	{
		OnRecoveryBegin();
	}
	else if (EventTag.MatchesTagExact(BendingTags::Event_Bending_Phase_End) && !bHasMontage)
	{
		// Montage-driven moves end on blend-out instead.
		EndAbility(GetCurrentAbilitySpecHandle(), GetCurrentActorInfo(), GetCurrentActivationInfo(), true, false);
	}
}

void UBendingGameplayAbility::HandleMontageFinished()
{
	EndAbility(GetCurrentAbilitySpecHandle(), GetCurrentActorInfo(), GetCurrentActivationInfo(), true, false);
}

void UBendingGameplayAbility::HandleMontageAborted()
{
	EndAbility(GetCurrentAbilitySpecHandle(), GetCurrentActorInfo(), GetCurrentActivationInfo(), true, true);
}

void UBendingGameplayAbility::OnStartupBegin_Implementation()
{
}

void UBendingGameplayAbility::OnActiveBegin_Implementation()
{
}

void UBendingGameplayAbility::OnRecoveryBegin_Implementation()
{
}

void UBendingGameplayAbility::OnInputReleasedDuringMove_Implementation(float HeldSeconds)
{
}
