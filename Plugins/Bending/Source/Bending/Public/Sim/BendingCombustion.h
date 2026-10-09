#pragma once

#include "Sim/BendingSimWorld.h"

/**
 * Burning props: cloth banners, straw bales, wooden crates and dummies, a lantern's wick.
 *
 * Combustion is not a substance of its own. A prop soaks up heat from the Fire volumes near it until it catches,
 * then keeps an owned Fire volume burning on itself, fed from its own fuel. That flame is ordinary matter in the
 * FSimWorld, so everything already true of fire holds for it: water hitting it boils to steam and puts it out, wind
 * feeds it, and it heats whatever stands next to it, so fire spreads from prop to prop.
 *
 * Water douses: enough water within reach (enough to boil away two seconds of the fire's heat) puts a burning prop
 * out at once, and a wet prop will not catch again for a few seconds.
 */
namespace BendingSim
{
	struct FCombustionSpec
	{
		/** Heat it must soak up to catch fire (J). 0: it never burns. */
		double IgnitionJ = 0.0;
		/** How long its fuel burns (s). */
		double BurnSeconds = 0.0;
		/** Its flame: mass and the temperature its fuel holds it at. */
		double FlameMassKg = 0.2;
		double FlameTemperatureK = 1250.0;
		/** Most heat its fuel can release per second (W): a flame fanned or cooled past this starts to die. */
		double MaxBurnPowerW = 4.0e5;
		/** Fire within this reach of its centre heats it. */
		double ReachCm = 60.0;
		/** Heating from a flame: W per kelvin the flame is above ambient, for a flame of FullFlameKg or more. */
		double HeatingWPerK = 150.0;
		double FullFlameKg = 0.5;
		/** Soaked-up heat leaks away with this time constant. */
		double CoolingTimeS = 3.0;
		/** After water, it will not catch for this long. */
		double WetSeconds = 4.0;

		bool IsFlammable() const { return IgnitionJ > 0.0 && BurnSeconds > 0.0; }
	};

	enum class ECombustionEvent : unsigned char
	{
		None,
		Ignited,
		/** Water put it out (or its flame was blown out); unburnt fuel remains. */
		Extinguished,
		/** Its fuel is gone. */
		BurntOut
	};

	struct BENDINGSIM_API FCombustible
	{
		/** Heat soaked up toward catching (J). */
		double HeatJ = 0.0;
		/** Time it has burnt (s). */
		double BurntS = 0.0;
		/** Heat its fire has released (J). */
		double ReleasedJ = 0.0;
		/** Seconds of wetness left. */
		double WetS = 0.0;
		FHandle Flame;
		/** Times its flame was lost without water and flared up again (merged into a passing fire, cooled out). */
		int FlareUps = 0;
		bool bBurning = false;
		bool bBurntOut = false;

		/**
		 * Per frame, after the world has stepped. CenterCm is where it is heated and doused; FlameCm is where its
		 * flame burns (a banner's burning edge climbs as it burns away).
		 */
		ECombustionEvent Update(FSimWorld& World, const FCombustionSpec& Spec, const FVec3& CenterCm, const FVec3& FlameCm, double Dt);

		/** Catches fire now; false if it cannot (not flammable, burnt out, already burning, wet). */
		bool Ignite(FSimWorld& World, const FCombustionSpec& Spec, const FVec3& FlameCm);

		/** Removes its flame without counting it as put out (the prop broke or was removed). */
		void RemoveFlame(FSimWorld& World);

		/** 0..1: how much of it has burnt. */
		double GetBurntFraction(const FCombustionSpec& Spec) const;
		/** 0..1: how close it is to catching. */
		double GetHeatFraction(const FCombustionSpec& Spec) const;

		/** Heating power at a point from every flame within Spec.ReachCm except Ignore (W). */
		static double MeasureHeatingW(const FSimWorld& World, const FCombustionSpec& Spec, const FVec3& CenterCm, FHandle Ignore);
		/** Liquid water within ReachCm of a point (kg). */
		static double MeasureWaterKg(const FSimWorld& World, const FVec3& CenterCm, double ReachCm);
		/** Water that puts out a fire releasing PowerW: enough to boil away two seconds of its heat (kg). */
		static double GetDousingWaterKg(double PowerW, double AmbientK);
	};
}
