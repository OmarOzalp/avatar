#include "Physics/ElementalSubstance.h"

#include "Physics/BendingUnits.h"
#include "Physics/ElementalThermoKernel.h"

namespace
{
	FElementalSubstanceProperties MakeSubstanceProperties(double Density, double SpecificHeat, double TemperatureK, double AmbientH,
		double SpecificGasConstant = 0.0, double Porosity = 0.0)
	{
		FElementalSubstanceProperties P;
		P.DensityKgM3 = Density;
		P.SpecificHeat = SpecificHeat;
		P.DefaultTemperatureK = TemperatureK;
		P.AmbientHeatTransferCoefficient = AmbientH;
		P.SpecificGasConstant = SpecificGasConstant;
		P.DefaultPorosity = Porosity;
		P.bIsGas = SpecificGasConstant > 0.0;
		return P;
	}

	struct FElementalSubstanceTable
	{
		FElementalSubstanceProperties Entries[static_cast<int32>(EElementalSubstance::Count)];

		FElementalSubstanceTable()
		{
			using namespace BendingKernel::Constants;
			using namespace BendingPhysics;

			Entries[static_cast<int32>(EElementalSubstance::None)] = MakeSubstanceProperties(1000.0, 1000.0, 288.15, 10.0);
			// Granite ~2700, packed soil ~1600-2000. Per-volume porosity distinguishes rock from soil.
			Entries[static_cast<int32>(EElementalSubstance::Earth)] = MakeSubstanceProperties(2650.0, 790.0, 288.15, 15.0, 0.0, 0.3);
			Entries[static_cast<int32>(EElementalSubstance::Water)] = MakeSubstanceProperties(WaterDensity, SpecificHeatWater, 288.15, 25.0);
			Entries[static_cast<int32>(EElementalSubstance::Ice)] = MakeSubstanceProperties(917.0, SpecificHeatIce, 263.15, 25.0);
			// Steam mixes and condenses into the surrounding air quickly: high effective coefficient.
			Entries[static_cast<int32>(EElementalSubstance::Steam)] = MakeSubstanceProperties(0.598, SpecificHeatSteam, WaterBoilingPointK, 150.0, SpecificGasConstantSteam);
			// Hot combustion gas: ~1400 K mean flame temperature; radiation + entrainment shed heat in ~1 s without fuel.
			Entries[static_cast<int32>(EElementalSubstance::Fire)] = MakeSubstanceProperties(0.25, 1200.0, 1400.0, 80.0, SpecificGasConstantAir);
			Entries[static_cast<int32>(EElementalSubstance::Air)] = MakeSubstanceProperties(AirDensitySeaLevel, 1005.0, 288.15, 10.0, SpecificGasConstantAir);
		}
	};
}

const FElementalSubstanceProperties& FElementalSubstanceProperties::Get(EElementalSubstance Substance)
{
	static const FElementalSubstanceTable Table;
	const int32 Index = static_cast<int32>(Substance);
	return Table.Entries[Index >= 0 && Index < static_cast<int32>(EElementalSubstance::Count) ? Index : 0];
}

double FElementalSubstanceProperties::GetDensityAt(double TemperatureK) const
{
	return bIsGas ? BendingPhysics::IdealGasDensity(TemperatureK, SpecificGasConstant) : DensityKgM3;
}
