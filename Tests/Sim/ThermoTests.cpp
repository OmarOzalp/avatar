// Thermodynamics and kernel-math checks. Build and run with Tests/run_all.sh.

#include "SimTestHarness.h"
#include "Sim/BendingThermo.h"

#include <cmath>
#include <cstdio>

using namespace BendingSim;
using namespace BendingSim::Thermo;
using namespace BendingSim::Thermo::Constants;

namespace
{
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

	void ExpAndCbrtMatchLibm()
	{
		double WorstExp = 0.0;
		for (double X = -50.0; X <= 50.0; X += 0.137)
		{
			WorstExp = KMax(WorstExp, std::fabs(KExp(X) - std::exp(X)) / std::exp(X));
		}
		ExpectTrue("KExp relative error < 1e-14 over [-50, 50]", WorstExp < 1e-14);
		double WorstCbrt = 0.0;
		for (double X = 1e-9; X < 1e9; X *= 1.37)
		{
			WorstCbrt = KMax(WorstCbrt, std::fabs(KCbrt(X) - std::cbrt(X)) / std::cbrt(X));
		}
		ExpectTrue("KCbrt relative error < 1e-15 over [1e-9, 1e9]", WorstCbrt < 1e-15);
	}

	void TrigMatchesLibm()
	{
		double WorstSin = 0.0;
		double WorstCos = 0.0;
		for (double X = -100.0; X <= 100.0; X += 0.0173)
		{
			WorstSin = KMax(WorstSin, std::fabs(KSin(X) - std::sin(X)));
			WorstCos = KMax(WorstCos, std::fabs(KCos(X) - std::cos(X)));
		}
		ExpectTrue("KSin absolute error < 1e-13 over [-100, 100]", WorstSin < 1e-13);
		ExpectTrue("KCos absolute error < 1e-13 over [-100, 100]", WorstCos < 1e-13);
		double WorstAtan2 = 0.0;
		for (double A = -3.2; A <= 3.2; A += 0.0137)
		{
			const double Radii[] = { 1e-3, 1.0, 1e3 };
			for (double R : Radii)
			{
				const double Y = R * std::sin(A);
				const double X = R * std::cos(A);
				WorstAtan2 = KMax(WorstAtan2, std::fabs(KAtan2(Y, X) - std::atan2(Y, X)));
			}
		}
		ExpectTrue("KAtan2 absolute error < 1e-14 around the circle", WorstAtan2 < 1e-14);
		ExpectTrue("KFloor rounds toward -inf", KFloor(-1.5) == -2.0 && KFloor(1.5) == 1.0 && KFloor(-2.0) == -2.0 && KFloor(0.0) == 0.0);
	}

	void SegmentClosestPointsMatchBruteForce()
	{
		const FVec3 P0(0, 0, 0), P1(100, 0, 0), Q0(30, -50, 20), Q1(70, 60, 20);
		FVec3 A, B;
		ClosestPointsBetweenSegments(P0, P1, Q0, Q1, A, B);
		double Best = 1e300;
		for (int I = 0; I <= 400; ++I)
		{
			for (int J = 0; J <= 400; ++J)
			{
				const FVec3 PA = P0 + (P1 - P0) * (I / 400.0);
				const FVec3 PB = Q0 + (Q1 - Q0) * (J / 400.0);
				Best = KMin(Best, Distance(PA, PB));
			}
		}
		ExpectNear("segment-segment distance vs brute force", Distance(A, B), Best, 1e-2);
		ClosestPointsBetweenSegments(P0, P1, FVec3(0, 10, 0), FVec3(100, 10, 0), A, B);
		ExpectNear("parallel segments", Distance(A, B), 10.0, 1e-9);
	}
}

int main()
{
	ExpAndCbrtMatchLibm();
	TrigMatchesLibm();
	SegmentClosestPointsMatchBruteForce();
	IceToSteamWalksEveryPhase();
	FreezeThenMeltRoundTrips();
	PartialMeltBanksLatentHeat();
	SteamCondenses();
	FlashVaporizeLeavesBulkTemperature();
	HeatFlowNeverInvertsGradient();
	MixingConservesSensibleHeat();
	ContactAreaIsContinuous();

	return SimTest::Finish("Thermodynamics & kernel math");
}
