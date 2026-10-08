#pragma once

#include "CoreMinimal.h"
#include "Interaction/ElementalReaction.h"
#include "ElementalReaction_HeatExchange.generated.h"

/**
 * Convective heat exchange with phase changes and momentum coupling (BendingSim::ReactHeatExchange).
 *
 *  Fire + Water -> Evaporation: the flame flash-boils the water's contact layer into steam (spawned as a free
 *                  volume), loses that heat, and is dragged toward the water's velocity.
 *  Fire + Ice   -> Melting: latent heat banks in the ice until the whole volume turns to water.
 *  Steam + Ice, Fire + Earth -> heat flows down the gradient.
 *
 * Q = h * A_exchange * dT * dt, capped so the temperature gradient can never invert in one step.
 */
UCLASS(meta = (DisplayName = "Heat Exchange (Evaporation, Melting)"))
class BENDING_API UElementalReaction_HeatExchange : public UElementalReaction
{
	GENERATED_BODY()

public:
	UElementalReaction_HeatExchange();

	virtual void React(BendingSim::FReactionContext& Context) const override;

	/** Heat-transfer coefficient across the contact (W/(m^2*K)). 2e4 is the nucleate-boiling range: flame on a liquid. */
	UPROPERTY(EditAnywhere, Category = "Thermal", meta = (ClampMin = 0.0))
	float HeatTransferCoefficient;

	/** Share of the heat entering liquid water that flash-boils its contact layer before the bulk warms. */
	UPROPERTY(EditAnywhere, Category = "Thermal", meta = (ClampMin = 0.0, ClampMax = 1.0))
	float SurfaceFlashFraction;

	/** Time for fully mixed volumes to reach a common velocity. Small = water stops a fire jet hard. */
	UPROPERTY(EditAnywhere, Category = "Momentum", meta = (ClampMin = 0.001, Units = "s"))
	float MomentumCouplingTimeS;
};
