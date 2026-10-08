#pragma once

#include "CoreMinimal.h"
#include "Interaction/ElementalVolumeTypes.h"
#include "Physics/ElementalThermoKernel.h"

/**
 * Engine-facing physics helpers on FElementalVolumeState. Thermodynamics delegates to the tested,
 * engine-independent BendingKernel; this layer only maps substances and units.
 */
namespace ElementalPhysics
{
	[[nodiscard]] BENDING_API BendingKernel::FThermoMatter ToMatter(const FElementalVolumeState& State);
	BENDING_API void ApplyMatter(FElementalVolumeState& State, const BendingKernel::FThermoMatter& Matter);

	/** Adds heat (J; negative extracts), walking ice <-> water <-> steam. Updates Substance on full transitions. */
	BENDING_API FElementalPhaseChange AddHeat(FElementalVolumeState& State, double HeatJ);

	/** Boils the contact layer of liquid water without warming the bulk. Returns kg vaporized. */
	BENDING_API double FlashVaporize(FElementalVolumeState& State, double HeatJ);

	/** Largest heat flow Hot -> Cold this step that cannot invert the temperature gradient. */
	[[nodiscard]] BENDING_API double MaxHeatFlow(const FElementalVolumeState& Hot, const FElementalVolumeState& Cold);

	/** Heat (J) to extract to freeze this water completely; 0 if not liquid water. */
	[[nodiscard]] BENDING_API double HeatToFreeze(const FElementalVolumeState& State);

	/** Heat (J) to add to melt this ice completely; 0 if not ice. */
	[[nodiscard]] BENDING_API double HeatToMelt(const FElementalVolumeState& State);

	/**
	 * Entrains inert mass (air into flame): conserves sensible heat exactly and adds the incoming
	 * momentum as a pending impulse, so the result moves at the mass-weighted velocity.
	 */
	BENDING_API void EntrainMass(FElementalVolumeState& State, double MassKg, double TemperatureK, double SpecificHeat, const FVector& VelocityCmS);

	/** Newton cooling toward ambient over Dt, integrated exactly (stable for any step). Phase changes included. */
	BENDING_API FElementalPhaseChange ExchangeWithAmbient(FElementalVolumeState& State, double AmbientTemperatureK, double DeltaSeconds);

	/** Quadratic drag (N) on a body seeing flow at RelativeFlowMs: F = 1/2 * rho * Cd * A * |v| * v. */
	[[nodiscard]] BENDING_API FVector DragForceN(const FVector& RelativeFlowMs, double FluidDensityKgM3, double DragCoefficient, double FrontalAreaM2);

	/**
	 * Inelastic coupling: impulse on A (kg*cm/s) that closes CouplingFraction (0..1) of the velocity gap
	 * between A and B. Apply the negative to B. Momentum is conserved; immovable volumes absorb it.
	 */
	[[nodiscard]] BENDING_API FVector MomentumCouplingImpulse(const FElementalVolumeState& A, const FElementalVolumeState& B, double CouplingFraction);

	/** Velocity both volumes would share after a perfectly inelastic merge (cm/s). */
	[[nodiscard]] BENDING_API FVector MixtureVelocityCmS(const FElementalVolumeState& A, const FElementalVolumeState& B);
}
