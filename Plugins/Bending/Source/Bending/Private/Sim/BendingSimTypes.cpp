#include "Sim/BendingSimTypes.h"

namespace BendingSim
{
	namespace
	{
		FSubstanceProperties MakeSimSubstance(double Density, double SpecificHeat, double TemperatureK, double AmbientH,
			double SpecificGasConstant = 0.0, double Porosity = 0.0)
		{
			FSubstanceProperties P;
			P.DensityKgM3 = Density;
			P.SpecificHeat = SpecificHeat;
			P.DefaultTemperatureK = TemperatureK;
			P.AmbientHeatTransferCoefficient = AmbientH;
			P.SpecificGasConstant = SpecificGasConstant;
			P.DefaultPorosity = Porosity;
			P.bIsGas = SpecificGasConstant > 0.0;
			return P;
		}

		struct FSimSubstanceTable
		{
			FSubstanceProperties Entries[static_cast<int>(ESubstance::Count)];

			FSimSubstanceTable()
			{
				using namespace Thermo::Constants;
				Entries[static_cast<int>(ESubstance::None)] = MakeSimSubstance(1000.0, 1000.0, 288.15, 10.0);
				// Granite ~2700, packed soil ~1600-2000; per-volume porosity distinguishes rock from soil.
				Entries[static_cast<int>(ESubstance::Earth)] = MakeSimSubstance(2650.0, 790.0, 288.15, 15.0, 0.0, 0.3);
				Entries[static_cast<int>(ESubstance::Water)] = MakeSimSubstance(WaterDensity, SpecificHeatWater, 288.15, 25.0);
				Entries[static_cast<int>(ESubstance::Ice)] = MakeSimSubstance(917.0, SpecificHeatIce, 263.15, 25.0);
				// Steam mixes into the surrounding air and condenses quickly: high effective coefficient.
				Entries[static_cast<int>(ESubstance::Steam)] = MakeSimSubstance(0.598, SpecificHeatSteam, WaterBoilingPointK, 150.0, SpecificGasConstantSteam);
				// Hot combustion gas, ~1400 K mean flame temperature; radiation + entrainment shed heat in ~1 s without fuel.
				Entries[static_cast<int>(ESubstance::Fire)] = MakeSimSubstance(0.25, 1200.0, 1400.0, 80.0, SpecificGasConstantAir);
				Entries[static_cast<int>(ESubstance::Air)] = MakeSimSubstance(AirDensitySeaLevel, 1005.0, 288.15, 10.0, SpecificGasConstantAir);
			}
		};
	}

	const FSubstanceProperties& GetSubstanceProperties(ESubstance Substance)
	{
		static const FSimSubstanceTable Table;
		const int Index = static_cast<int>(Substance);
		return Table.Entries[Index >= 0 && Index < static_cast<int>(ESubstance::Count) ? Index : 0];
	}

	const char* GetSubstanceName(ESubstance Substance)
	{
		switch (Substance)
		{
		case ESubstance::Earth: return "Earth";
		case ESubstance::Water: return "Water";
		case ESubstance::Ice:   return "Ice";
		case ESubstance::Steam: return "Steam";
		case ESubstance::Fire:  return "Fire";
		case ESubstance::Air:   return "Air";
		default:                return "None";
		}
	}

	const char* GetReactionTypeName(EReactionType Type)
	{
		switch (Type)
		{
		case EReactionType::Evaporation:  return "Evaporation";
		case EReactionType::Condensation: return "Condensation";
		case EReactionType::Melting:      return "Melting";
		case EReactionType::Freezing:     return "Freezing";
		case EReactionType::Oxygenation:  return "Oxygenation";
		case EReactionType::Saturation:   return "Saturation";
		case EReactionType::MudFormed:    return "MudFormed";
		case EReactionType::Deflection:   return "Deflection";
		case EReactionType::Erosion:      return "Erosion";
		case EReactionType::Extinguished: return "Extinguished";
		default:                          return "None";
		}
	}

	// ---------------------------------------------------------------------------------------------------- FVolume

	FVolume FVolume::MakeDefault(ESubstance InSubstance, double InMassKg)
	{
		const FSubstanceProperties& Props = GetSubstanceProperties(InSubstance);
		FVolume Volume;
		Volume.Substance = InSubstance;
		Volume.MassKg = KMax(InMassKg, 0.0);
		Volume.TemperatureK = Props.DefaultTemperatureK;
		Volume.DragCoefficient = Props.DragCoefficient;
		Volume.Porosity = Props.DefaultPorosity;
		Volume.bDeriveRadiusFromMass = Props.bIsGas;

		const double Density = Props.GetDensityAt(Volume.TemperatureK);
		Volume.RadiusCm = MToCm(SphereRadiusFromVolumeM(Density > 0.0 ? Volume.MassKg / Density : 0.0));
		return Volume;
	}

	double FVolume::GetSpecificHeat() const
	{
		switch (Substance)
		{
		case ESubstance::Ice:
		case ESubstance::Water:
		case ESubstance::Steam:
			return GetProperties().SpecificHeat;
		default:
			return SpecificHeatOverride > 0.0 ? SpecificHeatOverride : GetProperties().SpecificHeat;
		}
	}

	double FVolume::GetGeometricVolumeM3() const
	{
		const double RadiusM = CmToM(RadiusCm);
		double VolumeM3 = SphereVolumeM3(RadiusM);
		if (Shape == EShape::Capsule)
		{
			VolumeM3 += Pi * RadiusM * RadiusM * CmToM(2.0 * CapsuleHalfAxisCm.Size());
		}
		return VolumeM3;
	}

	double FVolume::GetDensityKgM3() const
	{
		const FSubstanceProperties& Props = GetProperties();
		if (Props.bIsGas && !bDeriveRadiusFromMass)
		{
			const double VolumeM3 = GetGeometricVolumeM3();
			if (VolumeM3 > SmallNumber)
			{
				return MassKg / VolumeM3;
			}
		}
		return Props.GetDensityAt(TemperatureK);
	}

	double FVolume::GetSurfaceAreaM2() const
	{
		const double RadiusM = CmToM(RadiusCm);
		double AreaM2 = 4.0 * Pi * RadiusM * RadiusM;
		if (Shape == EShape::Capsule)
		{
			AreaM2 += 2.0 * Pi * RadiusM * CmToM(2.0 * CapsuleHalfAxisCm.Size());
		}
		return AreaM2;
	}

	double FVolume::GetFrontalAreaM2() const
	{
		const double RadiusM = CmToM(RadiusCm);
		double AreaM2 = Pi * RadiusM * RadiusM;
		if (Shape == EShape::Capsule)
		{
			AreaM2 += 2.0 * RadiusM * CmToM(2.0 * CapsuleHalfAxisCm.Size());
		}
		return AreaM2;
	}

	void FVolume::UpdateDerivedRadius()
	{
		if (!bDeriveRadiusFromMass || Shape != EShape::Sphere)
		{
			return;
		}
		const double Density = GetProperties().GetDensityAt(TemperatureK);
		if (Density > SmallNumber)
		{
			RadiusCm = MToCm(SphereRadiusFromVolumeM(KMax(MassKg, 0.0) / Density));
		}
	}

	// ---------------------------------------------------------------------------------------------------- Physics

	Thermo::FThermoMatter ToMatter(const FVolume& Volume)
	{
		Thermo::FThermoMatter Matter;
		switch (Volume.Substance)
		{
		case ESubstance::Ice:   Matter.Phase = Thermo::EThermoPhase::Ice; break;
		case ESubstance::Water: Matter.Phase = Thermo::EThermoPhase::Water; break;
		case ESubstance::Steam: Matter.Phase = Thermo::EThermoPhase::Steam; break;
		default:                Matter.Phase = Thermo::EThermoPhase::Inert; break;
		}
		Matter.MassKg = Volume.MassKg;
		Matter.TemperatureK = Volume.TemperatureK;
		Matter.LatentHeatJ = Volume.LatentHeatJ;
		Matter.InertSpecificHeat = Volume.GetSpecificHeat();
		return Matter;
	}

	void ApplyMatter(FVolume& Volume, const Thermo::FThermoMatter& Matter)
	{
		switch (Matter.Phase)
		{
		case Thermo::EThermoPhase::Ice:   Volume.Substance = ESubstance::Ice; break;
		case Thermo::EThermoPhase::Water: Volume.Substance = ESubstance::Water; break;
		case Thermo::EThermoPhase::Steam: Volume.Substance = ESubstance::Steam; break;
		default:                          Volume.SpecificHeatOverride = Matter.InertSpecificHeat; break;
		}
		Volume.MassKg = Matter.MassKg;
		Volume.TemperatureK = Matter.TemperatureK;
		Volume.LatentHeatJ = Matter.LatentHeatJ;
	}

	Thermo::FPhaseChangeResult AddHeat(FVolume& Volume, double HeatJ)
	{
		Thermo::FThermoMatter Matter = ToMatter(Volume);
		const Thermo::FPhaseChangeResult Result = Thermo::AddHeat(Matter, HeatJ);
		ApplyMatter(Volume, Matter);
		return Result;
	}

	double FlashVaporize(FVolume& Volume, double HeatJ)
	{
		Thermo::FThermoMatter Matter = ToMatter(Volume);
		const double Vaporized = Thermo::FlashVaporize(Matter, HeatJ);
		ApplyMatter(Volume, Matter);
		return Vaporized;
	}

	double MaxHeatFlow(const FVolume& Hot, const FVolume& Cold)
	{
		return Thermo::MaxHeatFlow(ToMatter(Hot), ToMatter(Cold));
	}

	double HeatToFreeze(const FVolume& Volume)
	{
		return Thermo::HeatToFreeze(ToMatter(Volume));
	}

	double HeatToMelt(const FVolume& Volume)
	{
		return Thermo::HeatToMelt(ToMatter(Volume));
	}

	void EntrainMass(FVolume& Volume, double MassKg, double TemperatureK, double SpecificHeat, const FVec3& VelocityCmS)
	{
		if (MassKg <= 0.0)
		{
			return;
		}
		Thermo::FThermoMatter Matter = ToMatter(Volume);
		if (Matter.Phase != Thermo::EThermoPhase::Inert)
		{
			return;
		}
		// Integrating this impulse over the new mass lands exactly on the mass-weighted velocity.
		Volume.PendingImpulseKgCmS += (VelocityCmS - Volume.VelocityCmS) * MassKg;
		Thermo::MixInInert(Matter, MassKg, TemperatureK, SpecificHeat);
		ApplyMatter(Volume, Matter);
	}

	Thermo::FPhaseChangeResult ExchangeWithAmbient(FVolume& Volume, double AmbientTemperatureK, double DeltaSeconds)
	{
		const double Capacity = Volume.GetHeatCapacityJPerK();
		const double Conductance = Volume.GetProperties().AmbientHeatTransferCoefficient * Volume.GetSurfaceAreaM2(); // W/K
		if (Capacity <= SmallNumber || Conductance <= 0.0 || DeltaSeconds <= 0.0)
		{
			return Thermo::FPhaseChangeResult();
		}
		const Thermo::FThermoMatter Matter = ToMatter(Volume);
		double HeatJ = Thermo::AmbientExchangeHeat(Matter, AmbientTemperatureK, 1.0 - KExp(-Conductance * DeltaSeconds / Capacity));
		// On a phase plateau the temperature cannot relax, so deliver the plain conductive flux
		// (ice melting slowly in warm air, steam condensing).
		if (Thermo::IsAtPhasePlateau(Matter, AmbientTemperatureK - Volume.TemperatureK))
		{
			HeatJ = Conductance * (AmbientTemperatureK - Volume.TemperatureK) * DeltaSeconds;
		}
		return AddHeat(Volume, HeatJ);
	}

	FVec3 DragForceN(const FVec3& RelativeFlowMs, double FluidDensityKgM3, double DragCoefficient, double FrontalAreaM2)
	{
		return RelativeFlowMs * (0.5 * FluidDensityKgM3 * DragCoefficient * FrontalAreaM2 * RelativeFlowMs.Size());
	}

	FVec3 MomentumCouplingImpulse(const FVolume& A, const FVolume& B, double CouplingFraction)
	{
		const double InverseMassSum = A.GetInverseMass() + B.GetInverseMass();
		if (InverseMassSum <= 0.0 || CouplingFraction <= 0.0)
		{
			return FVec3();
		}
		return (B.VelocityCmS - A.VelocityCmS) * (KClamp(CouplingFraction, 0.0, 1.0) / InverseMassSum);
	}

	FVec3 MixtureVelocityCmS(const FVolume& A, const FVolume& B)
	{
		if (A.bImmovable || B.bImmovable)
		{
			return A.bImmovable ? A.VelocityCmS : B.VelocityCmS;
		}
		const double TotalMass = A.MassKg + B.MassKg;
		return TotalMass > SmallNumber ? (A.VelocityCmS * A.MassKg + B.VelocityCmS * B.MassKg) / TotalMass : FVec3();
	}

	// ---------------------------------------------------------------------------------------------------- Contacts

	bool ComputeContact(const FVolume& A, const FVolume& B, FContact& OutContact)
	{
		const double RadiusA = A.RadiusCm;
		const double RadiusB = B.RadiusCm;
		if (RadiusA <= 0.0 || RadiusB <= 0.0)
		{
			return false;
		}

		FVec3 PointA = A.LocationCm;
		FVec3 PointB = B.LocationCm;
		const bool bCapsuleA = A.Shape == EShape::Capsule;
		const bool bCapsuleB = B.Shape == EShape::Capsule;
		if (bCapsuleA && bCapsuleB)
		{
			ClosestPointsBetweenSegments(A.GetSegmentStartCm(), A.GetSegmentEndCm(), B.GetSegmentStartCm(), B.GetSegmentEndCm(), PointA, PointB);
		}
		else if (bCapsuleA)
		{
			PointA = ClosestPointOnSegment(PointB, A.GetSegmentStartCm(), A.GetSegmentEndCm());
		}
		else if (bCapsuleB)
		{
			PointB = ClosestPointOnSegment(PointA, B.GetSegmentStartCm(), B.GetSegmentEndCm());
		}

		const FVec3 Delta = PointB - PointA;
		const double DistanceCm = Delta.Size();
		const double Penetration = RadiusA + RadiusB - DistanceCm;
		if (Penetration <= 0.0)
		{
			return false;
		}

		OutContact.NormalAB = DistanceCm > SmallNumber ? Delta / DistanceCm : FVec3(0.0, 0.0, 1.0);
		OutContact.PenetrationCm = Penetration;
		OutContact.PointCm = PointA + OutContact.NormalAB * (RadiusA - 0.5 * Penetration);

		const double RadiusAM = CmToM(RadiusA);
		const double RadiusBM = CmToM(RadiusB);
		const double DistanceM = CmToM(DistanceCm);
		OutContact.ExchangeAreaM2 = Thermo::ContactExchangeArea(RadiusAM, RadiusBM, DistanceM);
		OutContact.ImmersionA = Thermo::ImmersionFraction(RadiusAM, RadiusBM, DistanceM);
		OutContact.ImmersionB = Thermo::ImmersionFraction(RadiusBM, RadiusAM, DistanceM);
		OutContact.OverlapFraction = KClamp(Penetration / (2.0 * KMin(RadiusA, RadiusB)), 0.0, 1.0);
		return true;
	}
}
