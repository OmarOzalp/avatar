#pragma once

/**
 * Engine-independent thermodynamics and contact-geometry kernel for elemental matter.
 *
 * Deliberately free of Unreal includes so Tests/PhysicsKernel can compile and verify it with a plain
 * C++ compiler (energy conservation across ice <-> water <-> steam). Everything here is SI: kg, K, J, m.
 */
namespace BendingKernel
{
	namespace Constants
	{
		inline constexpr double WaterFreezingPointK = 273.15;
		inline constexpr double WaterBoilingPointK = 373.15;
		inline constexpr double LatentHeatFusion = 3.34e5;        // J/kg
		inline constexpr double LatentHeatVaporization = 2.257e6; // J/kg
		inline constexpr double SpecificHeatIce = 2090.0;         // J/(kg*K)
		inline constexpr double SpecificHeatWater = 4186.0;       // J/(kg*K)
		inline constexpr double SpecificHeatSteam = 2010.0;       // J/(kg*K)
		inline constexpr double MinTemperatureK = 1.0;
		inline constexpr double Pi = 3.14159265358979323846;
		inline constexpr double PlateauToleranceK = 1e-3;
	}

	template <typename T> constexpr T KMin(T A, T B) { return A < B ? A : B; }
	template <typename T> constexpr T KMax(T A, T B) { return A > B ? A : B; }
	template <typename T> constexpr T KClamp(T V, T Lo, T Hi) { return V < Lo ? Lo : (V > Hi ? Hi : V); }

	/** Phase family. Inert matter (earth, fire, air) stores sensible heat only. */
	enum class EThermoPhase : unsigned char
	{
		Inert,
		Ice,
		Water,
		Steam
	};

	struct FThermoMatter
	{
		EThermoPhase Phase = EThermoPhase::Inert;
		double MassKg = 0.0;
		double TemperatureK = 288.15;
		/** Energy banked toward the pending phase transition (J). Ice: >= 0, heading to melt. Water: <= 0, heading to freeze. */
		double LatentHeatJ = 0.0;
		/** Specific heat for Inert matter (J/(kg*K)). Water phases use the constants above. */
		double InertSpecificHeat = 1000.0;
	};

	struct FPhaseChangeResult
	{
		double MeltedKg = 0.0;
		double FrozenKg = 0.0;
		double VaporizedKg = 0.0;
		double CondensedKg = 0.0;
		bool bPhaseChanged = false;

		FPhaseChangeResult& operator+=(const FPhaseChangeResult& Other)
		{
			MeltedKg += Other.MeltedKg;
			FrozenKg += Other.FrozenKg;
			VaporizedKg += Other.VaporizedKg;
			CondensedKg += Other.CondensedKg;
			bPhaseChanged = bPhaseChanged || Other.bPhaseChanged;
			return *this;
		}
	};

	inline double SpecificHeat(const FThermoMatter& M)
	{
		switch (M.Phase)
		{
		case EThermoPhase::Ice:   return Constants::SpecificHeatIce;
		case EThermoPhase::Water: return Constants::SpecificHeatWater;
		case EThermoPhase::Steam: return Constants::SpecificHeatSteam;
		default:                  return M.InertSpecificHeat;
		}
	}

	inline double HeatCapacity(const FThermoMatter& M)
	{
		return M.MassKg * SpecificHeat(M);
	}

	/** True when heat of the given sign would go into a phase transition instead of changing temperature. */
	inline bool IsAtPhasePlateau(const FThermoMatter& M, double HeatSign)
	{
		using namespace Constants;
		switch (M.Phase)
		{
		case EThermoPhase::Ice:
			return HeatSign > 0.0
				? M.TemperatureK >= WaterFreezingPointK - PlateauToleranceK
				: M.LatentHeatJ > 0.0;
		case EThermoPhase::Water:
			return HeatSign > 0.0
				? (M.TemperatureK >= WaterBoilingPointK - PlateauToleranceK || M.LatentHeatJ < 0.0)
				: M.TemperatureK <= WaterFreezingPointK + PlateauToleranceK;
		case EThermoPhase::Steam:
			return HeatSign < 0.0 && M.TemperatureK <= WaterBoilingPointK + PlateauToleranceK;
		default:
			return false;
		}
	}

	/** Heat (J, >= 0) still needed to finish the transition M is currently sitting on for the given heat sign. */
	inline double PlateauHeatRemaining(const FThermoMatter& M, double HeatSign)
	{
		using namespace Constants;
		if (!IsAtPhasePlateau(M, HeatSign))
		{
			return 0.0;
		}
		switch (M.Phase)
		{
		case EThermoPhase::Ice:
			return HeatSign > 0.0 ? M.MassKg * LatentHeatFusion - M.LatentHeatJ : M.LatentHeatJ;
		case EThermoPhase::Water:
			if (HeatSign > 0.0)
			{
				return M.LatentHeatJ < 0.0 ? -M.LatentHeatJ : M.MassKg * LatentHeatVaporization;
			}
			return M.MassKg * LatentHeatFusion + M.LatentHeatJ;
		case EThermoPhase::Steam:
			return M.MassKg * LatentHeatVaporization;
		default:
			return 0.0;
		}
	}

	/**
	 * Adds heat (J; negative removes it), walking through ice <-> water <-> steam transitions.
	 *
	 * A volume is a single phase: melting and freezing bank latent heat until the whole mass converts,
	 * like a block of ice sitting at 0 C. Boiling and condensation shed mass instead, because vapor leaves.
	 */
	inline FPhaseChangeResult AddHeat(FThermoMatter& M, double HeatJ)
	{
		using namespace Constants;
		FPhaseChangeResult Result;
		double Q = HeatJ;

		// Each pass consumes Q or performs one transition; ice -> water -> steam needs at most three.
		for (int Pass = 0; Pass < 4; ++Pass)
		{
			if (M.MassKg <= 0.0 || Q == 0.0)
			{
				break;
			}

			const double C = HeatCapacity(M);
			switch (M.Phase)
			{
			case EThermoPhase::Ice:
				if (Q > 0.0)
				{
					const double ToMeltingPoint = (WaterFreezingPointK - M.TemperatureK) * C;
					if (ToMeltingPoint > 0.0)
					{
						if (Q <= ToMeltingPoint)
						{
							M.TemperatureK += Q / C;
							Q = 0.0;
							break;
						}
						Q -= ToMeltingPoint;
						M.TemperatureK = WaterFreezingPointK;
					}

					const double LatentRemaining = M.MassKg * LatentHeatFusion - M.LatentHeatJ;
					if (Q < LatentRemaining)
					{
						M.LatentHeatJ += Q;
						Q = 0.0;
						break;
					}
					Q -= LatentRemaining;
					Result.MeltedKg += M.MassKg;
					Result.bPhaseChanged = true;
					M.Phase = EThermoPhase::Water;
					M.LatentHeatJ = 0.0;
					M.TemperatureK = WaterFreezingPointK;
				}
				else
				{
					// Refreeze any partially melted fraction before the solid cools further.
					if (M.LatentHeatJ > 0.0)
					{
						const double Refrozen = KMin(M.LatentHeatJ, -Q);
						M.LatentHeatJ -= Refrozen;
						Q += Refrozen;
						if (Q >= 0.0)
						{
							Q = 0.0;
							break;
						}
					}
					M.TemperatureK = KMax(M.TemperatureK + Q / C, MinTemperatureK);
					Q = 0.0;
				}
				break;

			case EThermoPhase::Water:
				if (Q > 0.0)
				{
					// Melt back any partially frozen fraction first.
					if (M.LatentHeatJ < 0.0)
					{
						const double Remelted = KMin(-M.LatentHeatJ, Q);
						M.LatentHeatJ += Remelted;
						Q -= Remelted;
						if (Q <= 0.0)
						{
							Q = 0.0;
							break;
						}
					}

					const double ToBoilingPoint = (WaterBoilingPointK - M.TemperatureK) * C;
					if (Q <= ToBoilingPoint)
					{
						M.TemperatureK += Q / C;
						Q = 0.0;
						break;
					}
					if (ToBoilingPoint > 0.0)
					{
						Q -= ToBoilingPoint;
					}
					M.TemperatureK = WaterBoilingPointK;

					const double Vaporized = KMin(Q / LatentHeatVaporization, M.MassKg);
					M.MassKg -= Vaporized;
					Result.VaporizedKg += Vaporized;
					// Heat beyond full vaporization leaves with the vapor.
					Q = 0.0;
				}
				else
				{
					const double ToFreezingPoint = (M.TemperatureK - WaterFreezingPointK) * C;
					if (-Q <= ToFreezingPoint)
					{
						M.TemperatureK += Q / C;
						Q = 0.0;
						break;
					}
					if (ToFreezingPoint > 0.0)
					{
						Q += ToFreezingPoint;
					}
					M.TemperatureK = WaterFreezingPointK;

					// LatentHeatJ <= 0 here: heat already extracted toward freezing.
					const double LatentRemaining = M.MassKg * LatentHeatFusion + M.LatentHeatJ;
					if (-Q < LatentRemaining)
					{
						M.LatentHeatJ += Q;
						Q = 0.0;
						break;
					}
					Q += LatentRemaining;
					Result.FrozenKg += M.MassKg;
					Result.bPhaseChanged = true;
					M.Phase = EThermoPhase::Ice;
					M.LatentHeatJ = 0.0;
				}
				break;

			case EThermoPhase::Steam:
				if (Q < 0.0)
				{
					const double ToCondensationPoint = (M.TemperatureK - WaterBoilingPointK) * C;
					if (-Q <= ToCondensationPoint)
					{
						M.TemperatureK += Q / C;
						Q = 0.0;
						break;
					}
					if (ToCondensationPoint > 0.0)
					{
						Q += ToCondensationPoint;
					}
					M.TemperatureK = WaterBoilingPointK;

					const double Condensed = KMin(-Q / LatentHeatVaporization, M.MassKg);
					M.MassKg -= Condensed;
					Result.CondensedKg += Condensed;
					Q = 0.0;
				}
				else
				{
					M.TemperatureK += Q / C;
					Q = 0.0;
				}
				break;

			default:
				M.TemperatureK = KMax(M.TemperatureK + Q / C, MinTemperatureK);
				Q = 0.0;
				break;
			}
		}
		return Result;
	}

	/**
	 * Flash-boils the contact layer of a liquid: the heat vaporizes surface mass directly and the bulk
	 * temperature is untouched (what happens when flame meets a water surface). Returns kg vaporized.
	 */
	inline double FlashVaporize(FThermoMatter& M, double HeatJ)
	{
		using namespace Constants;
		if (M.Phase != EThermoPhase::Water || HeatJ <= 0.0 || M.MassKg <= 0.0)
		{
			return 0.0;
		}
		const double HeatPerKg = SpecificHeatWater * KMax(WaterBoilingPointK - M.TemperatureK, 0.0) + LatentHeatVaporization;
		const double Vaporized = KMin(HeatJ / HeatPerKg, M.MassKg);
		M.MassKg -= Vaporized;
		return Vaporized;
	}

	/** Upper bound on heat flowing Hot -> Cold in one step without inverting the temperature gradient. */
	inline double MaxHeatFlow(const FThermoMatter& Hot, const FThermoMatter& Cold)
	{
		const double DeltaT = Hot.TemperatureK - Cold.TemperatureK;
		if (DeltaT <= 0.0)
		{
			return 0.0;
		}

		const bool bColdOnPlateau = IsAtPhasePlateau(Cold, 1.0);
		const bool bHotOnPlateau = IsAtPhasePlateau(Hot, -1.0);
		const double HotCapacity = HeatCapacity(Hot);
		const double ColdCapacity = HeatCapacity(Cold);

		if (bColdOnPlateau && bHotOnPlateau)
		{
			return KMin(PlateauHeatRemaining(Hot, -1.0), PlateauHeatRemaining(Cold, 1.0));
		}
		if (bColdOnPlateau)
		{
			return HotCapacity * DeltaT;
		}
		if (bHotOnPlateau)
		{
			return ColdCapacity * DeltaT;
		}
		const double CapacitySum = HotCapacity + ColdCapacity;
		return CapacitySum > 0.0 ? DeltaT * HotCapacity * ColdCapacity / CapacitySum : 0.0;
	}

	/** Heat (J, >= 0) that must be removed to turn liquid water fully to ice. Zero for other phases. */
	inline double HeatToFreeze(const FThermoMatter& M)
	{
		using namespace Constants;
		if (M.Phase != EThermoPhase::Water)
		{
			return 0.0;
		}
		return M.MassKg * SpecificHeatWater * KMax(M.TemperatureK - WaterFreezingPointK, 0.0)
			+ M.MassKg * LatentHeatFusion + M.LatentHeatJ;
	}

	/** Heat (J, >= 0) that must be added to turn ice fully to liquid water. Zero for other phases. */
	inline double HeatToMelt(const FThermoMatter& M)
	{
		using namespace Constants;
		if (M.Phase != EThermoPhase::Ice)
		{
			return 0.0;
		}
		return M.MassKg * SpecificHeatIce * KMax(WaterFreezingPointK - M.TemperatureK, 0.0)
			+ M.MassKg * LatentHeatFusion - M.LatentHeatJ;
	}

	/**
	 * Mixes AddedMassKg of inert matter at AddedTemperatureK into M, conserving sensible heat exactly.
	 * The specific heat becomes the mass-weighted average so the heat capacity stays consistent.
	 */
	inline void MixInInert(FThermoMatter& M, double AddedMassKg, double AddedTemperatureK, double AddedSpecificHeat)
	{
		if (AddedMassKg <= 0.0 || M.Phase != EThermoPhase::Inert)
		{
			return;
		}
		const double OldCapacity = HeatCapacity(M);
		const double AddedCapacity = AddedMassKg * AddedSpecificHeat;
		const double NewMass = M.MassKg + AddedMassKg;
		M.TemperatureK = (OldCapacity * M.TemperatureK + AddedCapacity * AddedTemperatureK) / (OldCapacity + AddedCapacity);
		M.InertSpecificHeat = (OldCapacity + AddedCapacity) / NewMass;
		M.MassKg = NewMass;
	}

	/**
	 * Heat (J) delivered to M by its surroundings over a step, given the fraction of the remaining
	 * temperature gap closed this step (1 - exp(-h*A*dt / (m*c)), computed by the caller).
	 */
	inline double AmbientExchangeHeat(const FThermoMatter& M, double AmbientTemperatureK, double RelaxFraction)
	{
		return HeatCapacity(M) * (AmbientTemperatureK - M.TemperatureK) * KClamp(RelaxFraction, 0.0, 1.0);
	}

	/**
	 * Height of the cap of sphere 1 (radius R1) lying inside sphere 2 (radius R2), centers Distance apart.
	 * Clamped to [0, 2*R1]; continuous from external tangency (0) to full immersion (2*R1).
	 */
	inline double CapHeightInside(double R1, double R2, double Distance)
	{
		if (R1 <= 0.0 || R2 <= 0.0 || Distance >= R1 + R2)
		{
			return 0.0;
		}
		const double RadiusGap = R1 > R2 ? R1 - R2 : R2 - R1;
		if (Distance <= RadiusGap)
		{
			return R1 <= R2 ? 2.0 * R1 : 0.0;
		}
		const double H = (R2 - R1 + Distance) * (R2 + R1 - Distance) / (2.0 * Distance);
		return KClamp(H, 0.0, 2.0 * R1);
	}

	/** Fraction (0..1) of sphere 1's surface lying inside sphere 2. */
	inline double ImmersionFraction(double R1, double R2, double Distance)
	{
		return R1 > 0.0 ? CapHeightInside(R1, R2, Distance) / (2.0 * R1) : 0.0;
	}

	/**
	 * Exchange area (m^2) between two overlapping spheres: the surface of the smaller one that lies
	 * inside the larger. Grows continuously from 0 at first touch to 4*pi*r^2 at full immersion.
	 */
	inline double ContactExchangeArea(double RadiusA, double RadiusB, double Distance)
	{
		const double Small = KMin(RadiusA, RadiusB);
		const double Large = KMax(RadiusA, RadiusB);
		return 2.0 * Constants::Pi * Small * CapHeightInside(Small, Large, Distance);
	}
}
