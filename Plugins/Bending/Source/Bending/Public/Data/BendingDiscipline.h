#pragma once

#include "CoreMinimal.h"
#include "BendingTypes.h"
#include "Engine/DataAsset.h"
#include "BendingDiscipline.generated.h"

class UBendingMoveDefinition;

/**
 * A bending style: an element plus its move list and the physical limits of a practitioner.
 * Characters are granted disciplines; the Avatar is simply granted all four.
 */
UCLASS(BlueprintType, Const)
class BENDING_API UBendingDiscipline : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	static const FPrimaryAssetType PrimaryAssetType;

	virtual FPrimaryAssetId GetPrimaryAssetId() const override;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Discipline")
	EBendingElement Element = EBendingElement::None;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Discipline")
	TArray<TObjectPtr<UBendingMoveDefinition>> Moves;

	/**
	 * Peak force (N) the bender can exert on bent matter. Caps the PD controller that holds boulders and
	 * water, so a 2-tonne slab lags and overshoots where a pebble snaps to the hand. Elite humans push ~2 kN.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Physical Limits", meta = (ClampMin = 0.0))
	float MaxBendingForceN = 40000.f;

	/** Largest single mass (kg) the bender can take control of at all. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Physical Limits", meta = (ClampMin = 0.0, Units = "kg"))
	float MaxControlledMassKg = 3000.f;

	/** Furthest distance (cm) at which matter can be bent. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Physical Limits", meta = (ClampMin = 0.0, Units = "cm"))
	float MaxBendingRangeCm = 2500.f;
};
