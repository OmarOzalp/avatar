#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "BendingTypes.h"
#include "AnimNotifyState_BendingPhase.generated.h"

/**
 * Marks a Startup, Active or Recovery window on a bending montage's notify track.
 *
 * Animators own where each phase sits in the clip; the move's frame data owns how long it lasts. With
 * FBendingFrameData::bFitAnimationToFrameData, the montage play rate is set at each window's start so
 * the window plays in exactly its authored frame count.
 */
UCLASS(meta = (DisplayName = "Bending Phase"))
class BENDING_API UAnimNotifyState_BendingPhase : public UAnimNotifyState
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bending")
	EBendingPhase Phase = EBendingPhase::Active;

	virtual void NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float TotalDuration,
		const FAnimNotifyEventReference& EventReference) override;
	virtual void NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
		const FAnimNotifyEventReference& EventReference) override;
	virtual FString GetNotifyName_Implementation() const override;
};
