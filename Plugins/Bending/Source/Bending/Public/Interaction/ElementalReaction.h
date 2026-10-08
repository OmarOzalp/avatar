#pragma once

#include "CoreMinimal.h"
#include "Interaction/ElementalVolumeTypes.h"
#include "UObject/Object.h"
#include "ElementalReaction.generated.h"

class UBendingInteractionSubsystem;
class UBendingSettings;

/**
 * Everything a reaction may read or change for one overlapping pair during one fixed step.
 * A and B are ordered to match the reaction's SubstanceA / SubstanceB; Contact.NormalAB points A -> B.
 */
struct BENDING_API FElementalReactionContext
{
	FElementalReactionContext(UBendingInteractionSubsystem& InSubsystem, const UBendingSettings& InSettings,
		FElementalVolumeHandle InHandleA, FElementalVolumeHandle InHandleB,
		FElementalVolumeState& InA, FElementalVolumeState& InB, const FElementalContact& InContact, double InDeltaSeconds);

	UBendingInteractionSubsystem& Subsystem;
	const UBendingSettings& Settings;
	const FElementalVolumeHandle HandleA;
	const FElementalVolumeHandle HandleB;
	FElementalVolumeState& A;
	FElementalVolumeState& B;
	const FElementalContact& Contact;
	const double DeltaSeconds;

	/** Records a continuous reaction; it is aggregated per pair and broadcast with rates. */
	void EmitEvent(EElementalReactionType Type, double MassKg, double EnergyJ) const;

	/** Queues an ownerless gas volume (steam); merged into nearby volumes of the same substance after the step. */
	void SpawnFreeVolume(const FElementalVolumeState& State) const;
};

/**
 * One physical interaction rule between two substances, evaluated every fixed step while they overlap.
 *
 * Reactions exchange conserved quantities (mass, momentum, heat) instead of scripting outcomes, so
 * results scale with the matter involved: a candle cannot boil a wave, a palm strike cannot turn a boulder.
 * Strength is the explicit gameplay multiplier on top of the physical rate.
 */
UCLASS(Abstract, EditInlineNew, DefaultToInstanced, CollapseCategories, BlueprintType)
class BENDING_API UElementalReaction : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Reaction")
	EElementalSubstance SubstanceA = EElementalSubstance::None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Reaction")
	EElementalSubstance SubstanceB = EElementalSubstance::None;

	/** Gameplay multiplier on the physical rate. 1 = physical. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Reaction", meta = (ClampMin = 0.0))
	float Strength = 1.f;

	/** Runs once per fixed step for each overlapping pair of SubstanceA / SubstanceB. */
	virtual void React(FElementalReactionContext& Context) const
	{
	}
};
