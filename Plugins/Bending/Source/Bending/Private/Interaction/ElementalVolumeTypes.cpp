#include "Interaction/ElementalVolumeTypes.h"

#include "Physics/BendingUnits.h"

FElementalVolumeState FElementalVolumeState::MakeDefault(EElementalSubstance InSubstance, double InMassKg)
{
	const FElementalSubstanceProperties& Props = FElementalSubstanceProperties::Get(InSubstance);

	FElementalVolumeState State;
	State.Substance = InSubstance;
	State.MassKg = FMath::Max(InMassKg, 0.0);
	State.TemperatureK = Props.DefaultTemperatureK;
	State.DragCoefficient = static_cast<float>(Props.DragCoefficient);
	State.Porosity = static_cast<float>(Props.DefaultPorosity);
	State.bDeriveRadiusFromMass = Props.bIsGas;

	// Start as a sphere whose size matches the mass at reference density.
	const double Density = Props.GetDensityAt(State.TemperatureK);
	const double VolumeM3 = Density > 0.0 ? State.MassKg / Density : 0.0;
	State.RadiusCm = static_cast<float>(BendingUnits::MToCm(BendingPhysics::SphereRadiusFromVolumeM(VolumeM3)));
	return State;
}

const FElementalSubstanceProperties& FElementalVolumeState::GetProperties() const
{
	return FElementalSubstanceProperties::Get(Substance);
}

bool FElementalVolumeState::IsGas() const
{
	return GetProperties().bIsGas;
}

double FElementalVolumeState::GetSpecificHeat() const
{
	switch (Substance)
	{
	case EElementalSubstance::Ice:
	case EElementalSubstance::Water:
	case EElementalSubstance::Steam:
		// Water phases always use their physical constants.
		return GetProperties().SpecificHeat;
	default:
		return SpecificHeatOverride > 0.0 ? SpecificHeatOverride : GetProperties().SpecificHeat;
	}
}

double FElementalVolumeState::GetHeatCapacityJPerK() const
{
	return MassKg * GetSpecificHeat();
}

double FElementalVolumeState::GetGeometricVolumeM3() const
{
	const double RadiusM = BendingUnits::CmToM(RadiusCm);
	double VolumeM3 = BendingPhysics::SphereVolumeM3(RadiusM);
	if (Shape == EElementalVolumeShape::Capsule)
	{
		VolumeM3 += UE_DOUBLE_PI * RadiusM * RadiusM * BendingUnits::CmToM(2.0 * CapsuleHalfAxisCm.Size());
	}
	return VolumeM3;
}

double FElementalVolumeState::GetDensityKgM3() const
{
	const FElementalSubstanceProperties& Props = GetProperties();
	if (Props.bIsGas && !bDeriveRadiusFromMass)
	{
		// Owner-shaped gas (an airbender's compressed pressure wave): density follows how much air was packed in.
		const double VolumeM3 = GetGeometricVolumeM3();
		if (VolumeM3 > UE_SMALL_NUMBER)
		{
			return MassKg / VolumeM3;
		}
	}
	return Props.GetDensityAt(TemperatureK);
}

double FElementalVolumeState::GetInverseMass() const
{
	return (bImmovable || MassKg <= UE_SMALL_NUMBER) ? 0.0 : 1.0 / MassKg;
}

double FElementalVolumeState::GetSurfaceAreaM2() const
{
	const double RadiusM = BendingUnits::CmToM(RadiusCm);
	double AreaM2 = 4.0 * UE_DOUBLE_PI * RadiusM * RadiusM;
	if (Shape == EElementalVolumeShape::Capsule)
	{
		AreaM2 += 2.0 * UE_DOUBLE_PI * RadiusM * BendingUnits::CmToM(2.0 * CapsuleHalfAxisCm.Size());
	}
	return AreaM2;
}

double FElementalVolumeState::GetFrontalAreaM2() const
{
	const double RadiusM = BendingUnits::CmToM(RadiusCm);
	double AreaM2 = UE_DOUBLE_PI * RadiusM * RadiusM;
	if (Shape == EElementalVolumeShape::Capsule)
	{
		AreaM2 += 2.0 * RadiusM * BendingUnits::CmToM(2.0 * CapsuleHalfAxisCm.Size());
	}
	return AreaM2;
}

double FElementalVolumeState::GetKineticEnergyJ() const
{
	return 0.5 * MassKg * BendingUnits::CmToM(VelocityCmS).SizeSquared();
}

FVector FElementalVolumeState::GetSegmentStartCm() const
{
	return Shape == EElementalVolumeShape::Capsule ? LocationCm - CapsuleHalfAxisCm : LocationCm;
}

FVector FElementalVolumeState::GetSegmentEndCm() const
{
	return Shape == EElementalVolumeShape::Capsule ? LocationCm + CapsuleHalfAxisCm : LocationCm;
}

double FElementalVolumeState::GetBoundingRadiusCm() const
{
	return RadiusCm + (Shape == EElementalVolumeShape::Capsule ? CapsuleHalfAxisCm.Size() : 0.0);
}

void FElementalVolumeState::UpdateDerivedRadius()
{
	if (!bDeriveRadiusFromMass || Shape != EElementalVolumeShape::Sphere)
	{
		return;
	}
	const double Density = GetProperties().GetDensityAt(TemperatureK);
	if (Density <= UE_SMALL_NUMBER)
	{
		return;
	}
	const double RadiusM = BendingPhysics::SphereRadiusFromVolumeM(FMath::Max(MassKg, 0.0) / Density);
	RadiusCm = static_cast<float>(BendingUnits::MToCm(RadiusM));
}
