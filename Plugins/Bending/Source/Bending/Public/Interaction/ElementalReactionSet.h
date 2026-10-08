#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "ElementalReactionSet.generated.h"

class UElementalReaction;

/** The sandbox's rulebook: every substance pair that interacts, and how. Assign in Project Settings > Bending. */
UCLASS(BlueprintType, Const)
class BENDING_API UElementalReactionSet : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, Instanced, Category = "Reactions")
	TArray<TObjectPtr<UElementalReaction>> Reactions;
};
