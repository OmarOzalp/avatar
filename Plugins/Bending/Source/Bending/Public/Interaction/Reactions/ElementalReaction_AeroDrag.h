#pragma once

#include "CoreMinimal.h"
#include "Interaction/ElementalReaction.h"
#include "ElementalReaction_AeroDrag.generated.h"

/**
 * Air + X -> Deflection / Erosion (BendingSim::ReactAeroDrag). SubstanceA must be Air.
 *
 * Quadratic drag 1/2 * rho * Cd * A * |v| * v from the relative wind over the immersed part of the body. A bender's
 * compressed air carries more momentum per volume. dv = J / m decides deflection: the same jet turns a pebble around
 * and barely slows a boulder (dv scales with area / mass). The impulse is capped at a full inelastic merge, so drag
 * never pushes a body past the wind and never creates energy. Loose, dry earth erodes above a critical dynamic
 * pressure. The air loses exactly the momentum the body gains.
 */
UCLASS(meta = (DisplayName = "Aerodynamic Drag (Deflection, Erosion)"))
class BENDING_API UElementalReaction_AeroDrag : public UElementalReaction
{
	GENERATED_BODY()

public:
	UElementalReaction_AeroDrag();

	virtual void React(BendingSim::FReactionContext& Context) const override;
	virtual EElementalSubstance GetRequiredSubstanceA() const override { return EElementalSubstance::Air; }

	/** Earth with at least this porosity is loose enough to erode (sand, soil; not rock). */
	UPROPERTY(EditAnywhere, Category = "Erosion", meta = (ClampMin = 0.0, ClampMax = 1.0))
	float ErodiblePorosity;

	/** Wet soil holds together: no erosion above this saturation. */
	UPROPERTY(EditAnywhere, Category = "Erosion", meta = (ClampMin = 0.0, ClampMax = 1.0))
	float MaxErodibleSaturation;

	/** Dynamic pressure (Pa) at which loose grains start moving. ~150 Pa is a ~15 m/s wind. */
	UPROPERTY(EditAnywhere, Category = "Erosion", meta = (ClampMin = 0.0))
	float CriticalErosionPressurePa;

	/** Eroded mass per second per m^2 per Pa of excess dynamic pressure. */
	UPROPERTY(EditAnywhere, Category = "Erosion", meta = (ClampMin = 0.0))
	float ErosionCoefficient;
};
