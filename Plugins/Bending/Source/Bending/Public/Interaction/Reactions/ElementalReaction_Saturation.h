#pragma once

#include "CoreMinimal.h"
#include "Interaction/ElementalReaction.h"
#include "ElementalReaction_Saturation.generated.h"

/**
 * Water + Earth -> Saturation / Mud (BendingSim::ReactSaturation).
 *
 * Water striking earth transfers momentum (a water whip shoves a boulder) and porous earth soaks it up:
 * infiltration plus impact-driven absorption into the pore volume. The soaked earth gets heavier and its
 * Saturation rises; past MudThresholdSaturation (and only if porous enough) it turns to mud. Terrain moisture is
 * handled by UBendingInteractionSubsystem::DepositWaterOnSurface.
 */
UCLASS(meta = (DisplayName = "Saturation (Water into Earth)"))
class BENDING_API UElementalReaction_Saturation : public UElementalReaction
{
	GENERATED_BODY()

public:
	UElementalReaction_Saturation();

	virtual void React(BendingSim::FReactionContext& Context) const override;

	/** Saturated hydraulic conductivity (m/s): ~1e-4 to 1e-3 for sandy soil, ~1e-7 for clay. */
	UPROPERTY(EditAnywhere, Category = "Absorption", meta = (ClampMin = 0.0))
	float InfiltrationRateMs;

	/** Share of the impact speed that drives water into the pores. */
	UPROPERTY(EditAnywhere, Category = "Absorption", meta = (ClampMin = 0.0, ClampMax = 1.0))
	float ImpactAbsorptionFactor;

	/** Saturation at which soil turns to mud. */
	UPROPERTY(EditAnywhere, Category = "Absorption", meta = (ClampMin = 0.0, ClampMax = 1.0))
	float MudThresholdSaturation;

	/** Only earth at least this porous can turn to mud; wet granite is just wet granite. */
	UPROPERTY(EditAnywhere, Category = "Absorption", meta = (ClampMin = 0.0, ClampMax = 1.0))
	float MinMudPorosity;

	UPROPERTY(EditAnywhere, Category = "Momentum", meta = (ClampMin = 0.001, Units = "s"))
	float MomentumCouplingTimeS;
};
