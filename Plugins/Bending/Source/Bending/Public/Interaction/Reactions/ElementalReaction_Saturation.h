#pragma once

#include "CoreMinimal.h"
#include "Interaction/ElementalReaction.h"
#include "ElementalReaction_Saturation.generated.h"

/**
 * Water + Earth -> Saturation / Mud.
 *
 * Water striking earth transfers momentum (a water whip shoves a boulder), and porous earth soaks it up:
 * infiltration plus impact-driven absorption into the pore volume. The soaked earth gets heavier and its
 * Saturation rises; past MudThresholdSaturation it loses cohesion and traction (owners read Saturation
 * to lower fracture thresholds and swap to a low-friction physical material). Terrain moisture is handled
 * by UBendingInteractionSubsystem::DepositWaterOnSurface.
 */
UCLASS(meta = (DisplayName = "Saturation (Water into Earth)"))
class BENDING_API UElementalReaction_Saturation : public UElementalReaction
{
	GENERATED_BODY()

public:
	UElementalReaction_Saturation();

	virtual void React(FElementalReactionContext& Context) const override;

	/** Saturated hydraulic conductivity (m/s): ~1e-4 to 1e-3 for sandy soil, ~1e-7 for clay. */
	UPROPERTY(EditAnywhere, Category = "Absorption", meta = (ClampMin = 0.0))
	float InfiltrationRateMs = 1e-3f;

	/** Share of the impact speed that drives water into the pores. */
	UPROPERTY(EditAnywhere, Category = "Absorption", meta = (ClampMin = 0.0, ClampMax = 1.0))
	float ImpactAbsorptionFactor = 0.05f;

	/** Saturation at which soil turns to mud. */
	UPROPERTY(EditAnywhere, Category = "Absorption", meta = (ClampMin = 0.0, ClampMax = 1.0))
	float MudThresholdSaturation = 0.6f;

	UPROPERTY(EditAnywhere, Category = "Momentum", meta = (ClampMin = 0.001, Units = "s"))
	float MomentumCouplingTimeS = 0.05f;
};
