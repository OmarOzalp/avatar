#include "Sim/BendingCombustion.h"

#include "Sim/BendingThermo.h"

namespace BendingSim
{
	namespace
	{
		constexpr int MaxQueried = 48;
		/** Water this close to nothing does not wet anything. */
		constexpr double MinWettingKg = 0.005;
	}

	double FCombustible::MeasureHeatingW(const FSimWorld& World, const FCombustionSpec& Spec, const FVec3& CenterCm, FHandle Ignore)
	{
		FHandle Handles[MaxQueried];
		const int Count = World.QueryVolumes(CenterCm, Spec.ReachCm, SubstanceMask(ESubstance::Fire), Handles, MaxQueried);
		const double AmbientK = World.Settings.AmbientTemperatureK;
		double PowerW = 0.0;
		for (int Index = 0; Index < Count; ++Index)
		{
			if (Handles[Index] == Ignore || World.IsDepleted(Handles[Index]))
			{
				continue;
			}
			const FVolume* Fire = World.GetVolume(Handles[Index]);
			if (!Fire || Fire->MassKg <= 0.0)
			{
				continue;
			}
			const double Size = KMin(1.0, Fire->MassKg / KMax(Spec.FullFlameKg, 1e-6));
			PowerW += Spec.HeatingWPerK * KMax(Fire->TemperatureK - AmbientK, 0.0) * Size;
		}
		return PowerW;
	}

	double FCombustible::MeasureWaterKg(const FSimWorld& World, const FVec3& CenterCm, double ReachCm)
	{
		FHandle Handles[MaxQueried];
		const int Count = World.QueryVolumes(CenterCm, ReachCm, SubstanceMask(ESubstance::Water), Handles, MaxQueried);
		double WaterKg = 0.0;
		for (int Index = 0; Index < Count; ++Index)
		{
			const FVolume* Water = World.IsDepleted(Handles[Index]) ? nullptr : World.GetVolume(Handles[Index]);
			WaterKg += Water ? KMax(Water->MassKg, 0.0) : 0.0;
		}
		return WaterKg;
	}

	double FCombustible::GetDousingWaterKg(double PowerW, double AmbientK)
	{
		using namespace Thermo::Constants;
		const double BoilJPerKg = SpecificHeatWater * KMax(WaterBoilingPointK - AmbientK, 0.0) + LatentHeatVaporization;
		return KMax(2.0 * PowerW / BoilJPerKg, 0.02);
	}

	bool FCombustible::Ignite(FSimWorld& World, const FCombustionSpec& Spec, const FVec3& FlameCm)
	{
		if (!Spec.IsFlammable() || bBurntOut || bBurning || WetS > 0.0)
		{
			return false;
		}
		FVolume Fire = FVolume::MakeDefault(ESubstance::Fire, Spec.FlameMassKg);
		Fire.TemperatureK = Spec.FlameTemperatureK;
		Fire.LocationCm = FlameCm;
		Fire.UpdateDerivedRadius();
		Flame = World.AddVolume(Fire, true);
		bBurning = Flame.IsSet();
		HeatJ = 0.0;
		return bBurning;
	}

	void FCombustible::RemoveFlame(FSimWorld& World)
	{
		if (Flame.IsSet())
		{
			World.RemoveVolume(Flame);
		}
		Flame = FHandle();
		bBurning = false;
	}

	double FCombustible::GetBurntFraction(const FCombustionSpec& Spec) const
	{
		return bBurntOut ? 1.0 : (Spec.BurnSeconds > 0.0 ? KClamp(BurntS / Spec.BurnSeconds, 0.0, 1.0) : 0.0);
	}

	double FCombustible::GetHeatFraction(const FCombustionSpec& Spec) const
	{
		return bBurning ? 1.0 : (Spec.IgnitionJ > 0.0 ? KClamp(HeatJ / Spec.IgnitionJ, 0.0, 1.0) : 0.0);
	}

	ECombustionEvent FCombustible::Update(FSimWorld& World, const FCombustionSpec& Spec, const FVec3& CenterCm, const FVec3& FlameCm, double Dt)
	{
		if (!Spec.IsFlammable() || bBurntOut || Dt <= 0.0)
		{
			return ECombustionEvent::None;
		}
		const double AmbientK = World.Settings.AmbientTemperatureK;
		const double WaterKg = MeasureWaterKg(World, CenterCm, Spec.ReachCm);
		if (WaterKg >= MinWettingKg)
		{
			WetS = Spec.WetSeconds;
			HeatJ = 0.0;
		}
		else
		{
			WetS = KMax(WetS - Dt, 0.0);
		}

		if (bBurning)
		{
			FVolume* Fire = World.IsDepleted(Flame) ? nullptr : World.GetVolume(Flame);
			const bool bFlameLost = !Fire || Fire->Substance != ESubstance::Fire;
			if (WaterKg >= GetDousingWaterKg(Spec.MaxBurnPowerW, AmbientK) || (bFlameLost && WaterKg >= MinWettingKg))
			{
				RemoveFlame(World);
				WetS = KMax(WetS, Spec.WetSeconds);
				return ECombustionEvent::Extinguished;
			}
			if (bFlameLost)
			{
				// Its flame merged into a passing fire (or cooled out) with no water about: the fuel is still
				// burning, so it flares up again.
				RemoveFlame(World);
				FVolume Fresh = FVolume::MakeDefault(ESubstance::Fire, Spec.FlameMassKg);
				Fresh.TemperatureK = Spec.FlameTemperatureK;
				Fresh.LocationCm = FlameCm;
				Fresh.UpdateDerivedRadius();
				Flame = World.AddVolume(Fresh, true);
				bBurning = true;
				++FlareUps;
				Fire = World.GetVolume(Flame);
				if (!Fire)
				{
					bBurning = false;
					return ECombustionEvent::Extinguished;
				}
			}
			Fire->LocationCm = FlameCm;
			Fire->VelocityCmS = FVec3();
			// The fuel holds its flame at temperature against everything that cools it, up to what it can release.
			const double NeedJ = KMax(Fire->GetHeatCapacityJPerK() * (Spec.FlameTemperatureK - Fire->TemperatureK), 0.0);
			const double BurnJ = KMin(NeedJ, Spec.MaxBurnPowerW * Dt);
			if (BurnJ > 0.0)
			{
				World.TransferHeat(Flame, BurnJ);
				ReleasedJ += BurnJ;
			}
			BurntS += Dt;
			if (BurntS >= Spec.BurnSeconds)
			{
				RemoveFlame(World);
				bBurntOut = true;
				return ECombustionEvent::BurntOut;
			}
			return ECombustionEvent::None;
		}

		const double HeatingW = WetS > 0.0 ? 0.0 : MeasureHeatingW(World, Spec, CenterCm, FHandle());
		HeatJ = HeatJ * KExp(-Dt / KMax(Spec.CoolingTimeS, 0.05)) + HeatingW * Dt;
		if (HeatJ >= Spec.IgnitionJ && Ignite(World, Spec, FlameCm))
		{
			return ECombustionEvent::Ignited;
		}
		return ECombustionEvent::None;
	}
}
