#pragma once

#include "CoreMinimal.h"
#include "Interaction/ElementalReaction.h"
#include "ElementalReaction_Oxygenation.generated.h"

/**
 * Air + Fire -> Oxygenation.
 *
 * Air flowing into a flame is entrained (mass, momentum and sensible heat conserved) and its oxygen
 * burns with the bender's fuel, releasing ~3 MJ per kg of air (Thornton's rule). The hotter, heavier
 * flame expands by the ideal-gas law, so range and damage grow; the air's momentum pushes it along.
 */
UCLASS(meta = (DisplayName = "Oxygenation (Air feeds Fire)"))
class BENDING_API UElementalReaction_Oxygenation : public UElementalReaction
{
	GENERATED_BODY()

public:
	UElementalReaction_Oxygenation();

	virtual void React(FElementalReactionContext& Context) const override;

	/** Share of the combustion heat kept by the flame (the rest radiates or goes to incomplete mixing). */
	UPROPERTY(EditAnywhere, Category = "Combustion", meta = (ClampMin = 0.0, ClampMax = 1.0))
	float CombustionEfficiency = 0.35f;

	/**
	 * Fuel limit: most air (kg) a flame can burn per second per kg of flame. Without it a strong gust
	 * would grow the fire without bound; with it, growth is exponential at most at this rate.
	 */
	UPROPERTY(EditAnywhere, Category = "Combustion", meta = (ClampMin = 0.0))
	float MaxEntrainmentPerFlameKgPerS = 1.5f;

	/** Entrainment speed (m/s) from turbulent mixing even when the air is not flowing into the flame. */
	UPROPERTY(EditAnywhere, Category = "Combustion", meta = (ClampMin = 0.0))
	float MixingEntrainmentSpeedMs = 0.5f;
};
