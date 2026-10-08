#pragma once

#include "CoreMinimal.h"
#include "Data/BendingMoveDefinition.h"
#include "Sandbox/BendingTechniqueTypes.h"
#include "BendingTechniqueMove.generated.h"

/**
 * A move that runs one of the kernel's sandbox techniques. Built at runtime from the technique table
 * (UBendingSandboxLibrary::CreateTechniqueDisciplines), so no assets are needed; UBendingTechniqueAbility reads
 * Technique and hands the phases to the owner's UBendingTechniqueComponent.
 */
UCLASS(BlueprintType)
class BENDING_API UBendingTechniqueMove : public UBendingMoveDefinition
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Technique")
	EBendingTechnique Technique = EBendingTechnique::None;
};
