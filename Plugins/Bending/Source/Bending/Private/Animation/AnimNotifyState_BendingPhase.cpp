#include "Animation/AnimNotifyState_BendingPhase.h"

#include "Components/BendingComponent.h"
#include "Components/SkeletalMeshComponent.h"

void UAnimNotifyState_BendingPhase::NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float TotalDuration,
	const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyBegin(MeshComp, Animation, TotalDuration, EventReference);

	// Editor previews have no bending component; FindBendingComponent returns null there.
	if (UBendingComponent* Bending = MeshComp ? UBendingComponent::FindBendingComponent(MeshComp->GetOwner()) : nullptr)
	{
		Bending->NotifyPhaseBegin(Phase, TotalDuration, MeshComp, Animation);
	}
}

void UAnimNotifyState_BendingPhase::NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
	const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyEnd(MeshComp, Animation, EventReference);

	if (UBendingComponent* Bending = MeshComp ? UBendingComponent::FindBendingComponent(MeshComp->GetOwner()) : nullptr)
	{
		Bending->NotifyPhaseEnd(Phase, MeshComp, Animation);
	}
}

FString UAnimNotifyState_BendingPhase::GetNotifyName_Implementation() const
{
	switch (Phase)
	{
	case EBendingPhase::Startup:  return TEXT("Bending: Startup");
	case EBendingPhase::Active:   return TEXT("Bending: Active");
	case EBendingPhase::Recovery: return TEXT("Bending: Recovery");
	default:                      return TEXT("Bending: None");
	}
}
