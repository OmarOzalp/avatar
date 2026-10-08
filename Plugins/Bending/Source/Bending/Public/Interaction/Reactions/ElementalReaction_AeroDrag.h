#pragma once

#include "CoreMinimal.h"
#include "Interaction/ElementalReaction.h"
#include "ElementalReaction_AeroDrag.generated.h"

/**
 * Air + X -> Deflection / Erosion. SubstanceA must be Air.
 *
 * Two forces act on the body inside an air volume:
 *  - Quadratic drag from the relative wind: F = 1/2 * rho * Cd * A * |v| * v.
 *  - Overpressure of compressed air (a bender's pressure wave, rho > ambient) while its front sweeps across
 *    the body. Uniform pressure on all sides cancels, so this peaks when the body is half immersed.
 *
 * Whether an incoming boulder is deflected falls out of the impulse it receives against its momentum:
 * dv = J / m. A gust scatters pebbles and barely nudges a slab. Loose, dry earth also erodes when the
 * dynamic pressure exceeds a threshold. Newton's third law: the air loses the same momentum.
 */
UCLASS(meta = (DisplayName = "Aerodynamic Drag (Deflection, Erosion)"))
class BENDING_API UElementalReaction_AeroDrag : public UElementalReaction
{
	GENERATED_BODY()

public:
	UElementalReaction_AeroDrag();

	virtual void React(FElementalReactionContext& Context) const override;

	/** Share of overpressure * area delivered as force by a passing front. */
	UPROPERTY(EditAnywhere, Category = "Force", meta = (ClampMin = 0.0))
	float PressureCoupling = 1.f;

	/** Earth with at least this porosity is loose enough to erode (sand, soil; not rock). */
	UPROPERTY(EditAnywhere, Category = "Erosion", meta = (ClampMin = 0.0, ClampMax = 1.0))
	float ErodiblePorosity = 0.2f;

	/** Wet soil holds together: no erosion above this saturation. */
	UPROPERTY(EditAnywhere, Category = "Erosion", meta = (ClampMin = 0.0, ClampMax = 1.0))
	float MaxErodibleSaturation = 0.3f;

	/** Dynamic pressure (Pa) at which loose grains start moving. ~150 Pa is a ~15 m/s wind. */
	UPROPERTY(EditAnywhere, Category = "Erosion", meta = (ClampMin = 0.0))
	float CriticalErosionPressurePa = 150.f;

	/** Eroded mass per second per m^2 per Pa of excess dynamic pressure. */
	UPROPERTY(EditAnywhere, Category = "Erosion", meta = (ClampMin = 0.0))
	float ErosionCoefficient = 2e-4f;
};
