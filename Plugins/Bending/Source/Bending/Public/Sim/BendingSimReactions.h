#pragma once

#include "Sim/BendingSimTypes.h"

/**
 * Cross-element reaction rules. Each runs once per fixed step for each overlapping pair of its two
 * substances and exchanges conserved quantities (mass, momentum, heat) instead of scripting an outcome,
 * so results scale with the matter involved. Strength is the explicit gameplay multiplier on the
 * physical rate (1 = physical).
 */
namespace BendingSim
{
	class FSimWorld;

	/** Everything a reaction may read or change for one pair during one step. A and B match the entry's substances. */
	struct FReactionContext
	{
		FSimWorld& World;
		FVolume& A;
		FVolume& B;
		FHandle HandleA;
		FHandle HandleB;
		/** Normal points A -> B. */
		const FContact& Contact;
		double DeltaSeconds;
		const FSimSettings& Settings;

		/** Records a continuous reaction; aggregated per pair and flushed with rates. */
		void EmitEvent(EReactionType Type, double MassKg, double EnergyJ) const;
		/** Queues an ownerless gas volume (steam); merged into nearby volumes of the same substance after the step. */
		void SpawnFreeVolume(const FVolume& Volume) const;
	};

	using FReactionFunction = void (*)(const void* UserData, FReactionContext& Context);

	/** One registered rule. UserData is passed back to Function (parameters, or an engine-side object). */
	struct FReactionEntry
	{
		ESubstance SubstanceA = ESubstance::None;
		ESubstance SubstanceB = ESubstance::None;
		FReactionFunction Function = nullptr;
		const void* UserData = nullptr;
	};

	// ---------------------------------------------------------------- Heat exchange: Fire+Water evaporation, Fire+Ice melting

	struct FHeatExchangeParams
	{
		/** W/(m^2*K). 2e4 is the nucleate-boiling range: flame on a liquid surface. */
		double HeatTransferCoefficient = 20000.0;
		/** Share of heat entering liquid water that flash-boils its contact layer before the bulk warms. */
		double SurfaceFlashFraction = 0.6;
		/** Time for fully mixed volumes to reach a common velocity (s). */
		double MomentumCouplingTimeS = 0.08;
		double Strength = 1.0;
	};

	/**
	 * Q = h * A_exchange * dT * dt, capped so the gradient cannot invert. Liquid water flash-boils into steam
	 * (spawned as a free volume); ice banks latent heat until it melts; inelastic momentum coupling drags the
	 * pair toward a common velocity (water smothers a fire jet).
	 */
	void ReactHeatExchange(const FHeatExchangeParams& Params, FReactionContext& Context);

	// ---------------------------------------------------------------- Oxygenation: Air (A) + Fire (B)

	struct FOxygenationParams
	{
		/** Share of combustion heat kept by the flame; luminous flames radiate away ~30-40%. */
		double CombustionEfficiency = 0.6;
		/** Fuel limit: most air (kg) burned per second per kg of flame. */
		double MaxEntrainmentPerFlameKgPerS = 1.5;
		/** Entrainment speed (m/s) from turbulent mixing even without inflow. */
		double MixingEntrainmentSpeedMs = 0.5;
		double Strength = 1.0;
	};

	/**
	 * Air flowing into the flame is entrained (mass, momentum, sensible heat conserved) and its oxygen burns at
	 * 3.03 MJ/kg of air * efficiency, capped by the fuel limit and the adiabatic flame temperature. The flame gets
	 * hotter and heavier, expands by the ideal-gas law, and rides the air's momentum.
	 */
	void ReactOxygenation(const FOxygenationParams& Params, FReactionContext& Context);

	// ---------------------------------------------------------------- Saturation: Water (A) + Earth (B)

	struct FSaturationParams
	{
		/** Saturated hydraulic conductivity (m/s): ~1e-4..1e-3 sandy soil, ~1e-7 clay. */
		double InfiltrationRateMs = 1e-3;
		/** Share of impact speed driving water into the pores (a bent splash forces water in). */
		double ImpactAbsorptionFactor = 0.12;
		double MudThresholdSaturation = 0.6;
		/** Only earth at least this porous can turn to mud; wet granite is just wet granite. */
		double MinMudPorosity = 0.2;
		double MomentumCouplingTimeS = 0.05;
		double Strength = 1.0;
	};

	/** Inelastic splash shoves the earth; porous earth absorbs water into its pore volume and turns to mud. */
	void ReactSaturation(const FSaturationParams& Params, FReactionContext& Context);

	// ---------------------------------------------------------------- Aerodynamic drag: Air (A) + anything (B)

	struct FAeroDragParams
	{
		/** Earth at least this porous can erode (sand, soil; not rock). */
		double ErodiblePorosity = 0.2;
		/** Wet soil holds together. */
		double MaxErodibleSaturation = 0.3;
		/** Dynamic pressure (Pa) that mobilizes loose grains (~15 m/s wind). */
		double CriticalErosionPressurePa = 150.0;
		/** kg per second per m^2 per Pa of excess dynamic pressure. */
		double ErosionCoefficient = 2e-4;
		double Strength = 1.0;
	};

	/**
	 * Quadratic drag 1/2*rho*Cd*A*|v|*v from the relative wind over the immersed part of the body. A bender's
	 * compressed air carries more momentum per volume (rho scales with compression). dv = J/m decides whether a
	 * projectile is deflected: the same blast reverses a pebble and barely slows a boulder. The impulse is capped
	 * at a full inelastic merge, so drag can never push a body past the wind or create energy. Loose, dry earth
	 * erodes above a critical dynamic pressure. The air loses exactly the momentum the body gains.
	 */
	void ReactAeroDrag(const FAeroDragParams& Params, FReactionContext& Context);

	// ---------------------------------------------------------------- Built-in rule set

	struct FDefaultReactionParams
	{
		FHeatExchangeParams HeatExchange;
		FOxygenationParams Oxygenation;
		FSaturationParams Saturation;
		FAeroDragParams AeroDrag;
	};

	/**
	 * Registers the standard rules: Fire+Water, Fire+Ice, Steam+Ice, Fire+Earth heat exchange; Air+Fire oxygenation;
	 * Water+Earth saturation; Air+Earth/Water/Ice/Steam drag. Params must outlive the world's use of them.
	 */
	void AddBuiltInReactions(FSimWorld& World, const FDefaultReactionParams& Params);

	void HeatExchangeReactionFunction(const void* UserData, FReactionContext& Context);
	void OxygenationReactionFunction(const void* UserData, FReactionContext& Context);
	void SaturationReactionFunction(const void* UserData, FReactionContext& Context);
	void AeroDragReactionFunction(const void* UserData, FReactionContext& Context);
}
