// Stand-alone checks for the engine-independent physics kernel. Build and run with Tests/PhysicsKernel/run.sh.

#include "Physics/ElementalThermoKernel.h"

#include <cmath>
#include <cstdio>

using namespace BendingKernel;
using namespace BendingKernel::Constants;

namespace
{
	int GFailures = 0;

	void ExpectNear(const char* What, double Actual, double Expected, double Tolerance)
	{
		const bool bOk = std::fabs(Actual - Expected) <= Tolerance;
		std::printf("[%s] %-58s actual=%.6f expected=%.6f\n", bOk ? " OK " : "FAIL", What, Actual, Expected);
		GFailures += bOk ? 0 : 1;
	}

	void ExpectTrue(const char* What, bool bCondition)
	{
		std::printf("[%s] %s\n", bCondition ? " OK " : "FAIL", What);
		GFailures += bCondition ? 0 : 1;
	}

	FThermoMatter Make(EThermoPhase Phase, double MassKg, double TemperatureK)
	{
		FThermoMatter M;
		M.Phase = Phase;
		M.MassKg = MassKg;
		M.TemperatureK = TemperatureK;
		return M;
	}

	void IceToSteamWalksEveryPhase()
	{
		FThermoMatter M = Make(EThermoPhase::Ice, 1.0, 263.15);
		const double Q = SpecificHeatIce * 10.0 + LatentHeatFusion + SpecificHeatWater * 100.0 + 0.5 * LatentHeatVaporization;
		const FPhaseChangeResult R = AddHeat(M, Q);
		ExpectTrue("ice heated past boiling ends as water", M.Phase == EThermoPhase::Water);
		ExpectNear("melted mass", R.MeltedKg, 1.0, 1e-9);
		ExpectNear("vaporized mass", R.VaporizedKg, 0.5, 1e-9);
		ExpectNear("remaining liquid mass", M.MassKg, 0.5, 1e-9);
		ExpectNear("liquid sits at boiling point", M.TemperatureK, WaterBoilingPointK, 1e-9);
	}

	void FreezeThenMeltRoundTrips()
	{
		FThermoMatter M = Make(EThermoPhase::Water, 2.0, 293.15);
		const double ToFreeze = HeatToFreeze(M);
		ExpectNear("HeatToFreeze closed form", ToFreeze, 2.0 * (SpecificHeatWater * 20.0 + LatentHeatFusion), 1e-6);

		const FPhaseChangeResult Frozen = AddHeat(M, -ToFreeze);
		ExpectTrue("extracting HeatToFreeze yields ice", M.Phase == EThermoPhase::Ice);
		ExpectNear("frozen mass", Frozen.FrozenKg, 2.0, 1e-9);
		ExpectNear("ice at freezing point", M.TemperatureK, WaterFreezingPointK, 1e-9);

		AddHeat(M, ToFreeze);
		ExpectTrue("adding it back yields water", M.Phase == EThermoPhase::Water);
		ExpectNear("water back at start temperature", M.TemperatureK, 293.15, 1e-6);
		ExpectNear("no latent heat left banked", M.LatentHeatJ, 0.0, 1e-6);
	}

	void PartialMeltBanksLatentHeat()
	{
		FThermoMatter M = Make(EThermoPhase::Ice, 1.0, WaterFreezingPointK);
		AddHeat(M, 0.5 * LatentHeatFusion);
		ExpectTrue("half-melted block is still ice", M.Phase == EThermoPhase::Ice);
		ExpectNear("temperature pinned at 0 C", M.TemperatureK, WaterFreezingPointK, 1e-9);
		ExpectNear("latent heat banked", M.LatentHeatJ, 0.5 * LatentHeatFusion, 1e-6);
		ExpectNear("remaining heat to melt", HeatToMelt(M), 0.5 * LatentHeatFusion, 1e-6);

		AddHeat(M, -0.25 * LatentHeatFusion);
		ExpectNear("cooling refreezes before the solid cools", M.LatentHeatJ, 0.25 * LatentHeatFusion, 1e-6);
		ExpectNear("temperature still pinned", M.TemperatureK, WaterFreezingPointK, 1e-9);
	}

	void SteamCondenses()
	{
		FThermoMatter M = Make(EThermoPhase::Steam, 1.0, 400.0);
		const double Q = SpecificHeatSteam * (400.0 - WaterBoilingPointK) + 0.3 * LatentHeatVaporization;
		const FPhaseChangeResult R = AddHeat(M, -Q);
		ExpectNear("condensed mass", R.CondensedKg, 0.3, 1e-9);
		ExpectNear("steam mass left", M.MassKg, 0.7, 1e-9);
	}

	void FlashVaporizeLeavesBulkTemperature()
	{
		FThermoMatter M = Make(EThermoPhase::Water, 10.0, 288.15);
		const double PerKg = SpecificHeatWater * (WaterBoilingPointK - 288.15) + LatentHeatVaporization;
		const double Vaporized = FlashVaporize(M, 0.2 * PerKg);
		ExpectNear("flash-vaporized mass", Vaporized, 0.2, 1e-9);
		ExpectNear("bulk temperature unchanged", M.TemperatureK, 288.15, 1e-12);
	}

	void HeatFlowNeverInvertsGradient()
	{
		FThermoMatter Fire;
		Fire.MassKg = 0.5;
		Fire.TemperatureK = 1400.0;
		Fire.InertSpecificHeat = 1200.0;
		FThermoMatter Rock;
		Rock.MassKg = 1.0;
		Rock.TemperatureK = 288.15;
		Rock.InertSpecificHeat = 790.0;

		const double Limit = MaxHeatFlow(Fire, Rock);
		AddHeat(Fire, -Limit);
		AddHeat(Rock, Limit);
		ExpectNear("sensible-only bodies meet at equilibrium", Fire.TemperatureK, Rock.TemperatureK, 1e-6);

		FThermoMatter Boiling = Make(EThermoPhase::Water, 1.0, WaterBoilingPointK);
		FThermoMatter Flame;
		Flame.MassKg = 0.5;
		Flame.TemperatureK = 1400.0;
		Flame.InertSpecificHeat = 1200.0;
		ExpectNear("boiling water is an infinite sink: hot side may cool to 100 C",
			MaxHeatFlow(Flame, Boiling), 0.5 * 1200.0 * (1400.0 - WaterBoilingPointK), 1e-6);
	}

	void MixingConservesSensibleHeat()
	{
		FThermoMatter Fire;
		Fire.MassKg = 0.5;
		Fire.TemperatureK = 1400.0;
		Fire.InertSpecificHeat = 1200.0;
		const double Before = HeatCapacity(Fire) * Fire.TemperatureK + 0.2 * 1005.0 * 288.15;
		MixInInert(Fire, 0.2, 288.15, 1005.0);
		ExpectNear("mass adds up", Fire.MassKg, 0.7, 1e-12);
		ExpectNear("sensible heat conserved by mixing", HeatCapacity(Fire) * Fire.TemperatureK, Before, 1e-6);
	}

	void ContactAreaIsContinuous()
	{
		const double R = 0.5;
		const double BigR = 2.0;
		ExpectNear("separated spheres exchange nothing", ContactExchangeArea(R, BigR, 3.0), 0.0, 1e-12);
		ExpectNear("external tangency -> 0", ContactExchangeArea(R, BigR, R + BigR), 0.0, 1e-12);
		ExpectNear("internal tangency -> full surface", ContactExchangeArea(R, BigR, BigR - R), 4.0 * Pi * R * R, 1e-9);
		ExpectNear("just past internal tangency stays continuous",
			ContactExchangeArea(R, BigR, BigR - R + 1e-6), 4.0 * Pi * R * R, 1e-4);
		ExpectNear("immersion fraction halfway", ImmersionFraction(R, 1e6, 1e6), 0.5, 1e-3);
	}
}

int main()
{
	IceToSteamWalksEveryPhase();
	FreezeThenMeltRoundTrips();
	PartialMeltBanksLatentHeat();
	SteamCondenses();
	FlashVaporizeLeavesBulkTemperature();
	HeatFlowNeverInvertsGradient();
	MixingConservesSensibleHeat();
	ContactAreaIsContinuous();

	std::printf("\n%s (%d failure%s)\n", GFailures == 0 ? "ALL PASSED" : "FAILED", GFailures, GFailures == 1 ? "" : "s");
	return GFailures == 0 ? 0 : 1;
}
