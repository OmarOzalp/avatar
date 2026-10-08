#pragma once

#include "CoreMinimal.h"
#include "BendingTypes.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "BendingInputConfig.generated.h"

class UInputAction;

USTRUCT(BlueprintType)
struct FBendingInputAction
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<const UInputAction> InputAction = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input", meta = (Categories = "Input.Bending"))
	FGameplayTag InputTag;
};

USTRUCT(BlueprintType)
struct FBendingStanceInputAction
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<const UInputAction> InputAction = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	EBendingElement Element = EBendingElement::None;
};

/** Maps Enhanced Input actions to bending input tags and stance switches. */
UCLASS(BlueprintType, Const)
class BENDING_API UBendingInputConfig : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input", meta = (TitleProperty = "InputTag"))
	TArray<FBendingInputAction> MoveInputs;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input", meta = (TitleProperty = "Element"))
	TArray<FBendingStanceInputAction> StanceInputs;
};
