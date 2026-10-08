#pragma once

#include "CoreMinimal.h"
#include "Interaction/ElementalReaction.h"
#include "ElementalReaction_Oxygenation.generated.h"

/**
 * Air + Fire -> Oxygenation (BendingSim::ReactOxygenation).
 *
 * Air flowing into a flame is entrained (mass, momentum and sensible heat conserved) and its oxygen burns with
 * the bender's fuel at ~3 MJ per kg of air (Thornton's rule). The hotter, heavier flame expands by the ideal-gas
 * law, so range and damage grow, and it rides the air's momentum.
 */
UCLASS(meta = (DisplayName = "Oxygenation (Air feeds Fire)"))
class BENDING_API UElementalReaction_Oxygenation : public UElementalReaction
{
	GENERATED_BODY()

public:
	UElementalReaction_Oxygenation();

	virtual void React(BendingSim::FReactionContext& Context) const override;
	virtual EElementalSubstance GetRequiredSubstanceA() const override { return EElementalSubstance::Air; }

	/** Share of the combustion heat kept by the flame; luminous flames radiate away ~30-40%. */
	UPROPERTY(EditAnywhere, Category = "Combustion", meta = (ClampMin = 0.0, ClampMax = 1.0))
	float CombustionEfficiency;

	/** Fuel limit: most air (kg) a flame can burn per second per kg of flame. */
	UPROPERTY(EditAnywhere, Category = "Combustion", meta = (ClampMin = 0.0))
	float MaxEntrainmentPerFlameKgPerS;

	/** Entrainment speed (m/s) from turbulent mixing even when the air is not flowing into the flame. */
	UPROPERTY(EditAnywhere, Category = "Combustion", meta = (ClampMin = 0.0))
	float MixingEntrainmentSpeedMs;
};
