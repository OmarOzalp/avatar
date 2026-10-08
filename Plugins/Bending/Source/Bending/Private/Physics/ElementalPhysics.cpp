#include "Physics/ElementalPhysics.h"

#include "Physics/BendingUnits.h"

namespace ElementalPhysics
{
	BendingKernel::FThermoMatter ToMatter(const FElementalVolumeState& State)
	{
		BendingKernel::FThermoMatter Matter;
		switch (State.Substance)
		{
		case EElementalSubstance::Ice:   Matter.Phase = BendingKernel::EThermoPhase::Ice; break;
		case EElementalSubstance::Water: Matter.Phase = BendingKernel::EThermoPhase::Water; break;
		case EElementalSubstance::Steam: Matter.Phase = BendingKernel::EThermoPhase::Steam; break;
		default:                         Matter.Phase = BendingKernel::EThermoPhase::Inert; break;
		}
		Matter.MassKg = State.MassKg;
		Matter.TemperatureK = State.TemperatureK;
		Matter.LatentHeatJ = State.LatentHeatJ;
		Matter.InertSpecificHeat = State.GetSpecificHeat();
		return Matter;
	}

	void ApplyMatter(FElementalVolumeState& State, const BendingKernel::FThermoMatter& Matter)
	{
		switch (Matter.Phase)
		{
		case BendingKernel::EThermoPhase::Ice:   State.Substance = EElementalSubstance::Ice; break;
		case BendingKernel::EThermoPhase::Water: State.Substance = EElementalSubstance::Water; break;
		case BendingKernel::EThermoPhase::Steam: State.Substance = EElementalSubstance::Steam; break;
		default:
			State.SpecificHeatOverride = Matter.InertSpecificHeat;
			break;
		}
		State.MassKg = Matter.MassKg;
		State.TemperatureK = Matter.TemperatureK;
		State.LatentHeatJ = Matter.LatentHeatJ;
	}

	FElementalPhaseChange AddHeat(FElementalVolumeState& State, double HeatJ)
	{
		BendingKernel::FThermoMatter Matter = ToMatter(State);
		const BendingKernel::FPhaseChangeResult Result = BendingKernel::AddHeat(Matter, HeatJ);
		ApplyMatter(State, Matter);

		FElementalPhaseChange Change;
		Change.MeltedKg = Result.MeltedKg;
		Change.FrozenKg = Result.FrozenKg;
		Change.VaporizedKg = Result.VaporizedKg;
		Change.CondensedKg = Result.CondensedKg;
		Change.bSubstanceChanged = Result.bPhaseChanged;
		return Change;
	}

	double FlashVaporize(FElementalVolumeState& State, double HeatJ)
	{
		BendingKernel::FThermoMatter Matter = ToMatter(State);
		const double Vaporized = BendingKernel::FlashVaporize(Matter, HeatJ);
		ApplyMatter(State, Matter);
		return Vaporized;
	}

	double MaxHeatFlow(const FElementalVolumeState& Hot, const FElementalVolumeState& Cold)
	{
		return BendingKernel::MaxHeatFlow(ToMatter(Hot), ToMatter(Cold));
	}

	double HeatToFreeze(const FElementalVolumeState& State)
	{
		return BendingKernel::HeatToFreeze(ToMatter(State));
	}

	double HeatToMelt(const FElementalVolumeState& State)
	{
		return BendingKernel::HeatToMelt(ToMatter(State));
	}

	void EntrainMass(FElementalVolumeState& State, double MassKg, double TemperatureK, double SpecificHeat, const FVector& VelocityCmS)
	{
		if (MassKg <= 0.0)
		{
			return;
		}
		BendingKernel::FThermoMatter Matter = ToMatter(State);
		if (Matter.Phase != BendingKernel::EThermoPhase::Inert)
		{
			return;
		}
		// Momentum of the incoming mass relative to the volume; integrating this impulse over the new mass
		// lands exactly on the mass-weighted velocity.
		State.PendingImpulseKgCmS += MassKg * (VelocityCmS - State.VelocityCmS);
		BendingKernel::MixInInert(Matter, MassKg, TemperatureK, SpecificHeat);
		ApplyMatter(State, Matter);
	}

	FElementalPhaseChange ExchangeWithAmbient(FElementalVolumeState& State, double AmbientTemperatureK, double DeltaSeconds)
	{
		const double Capacity = State.GetHeatCapacityJPerK();
		const double Conductance = State.GetProperties().AmbientHeatTransferCoefficient * State.GetSurfaceAreaM2(); // W/K
		if (Capacity <= UE_SMALL_NUMBER || Conductance <= 0.0 || DeltaSeconds <= 0.0)
		{
			return FElementalPhaseChange();
		}
		const double RelaxFraction = 1.0 - FMath::Exp(-Conductance * DeltaSeconds / Capacity);
		const BendingKernel::FThermoMatter Matter = ToMatter(State);
		double HeatJ = BendingKernel::AmbientExchangeHeat(Matter, AmbientTemperatureK, RelaxFraction);

		// On a phase plateau the temperature cannot relax, so the exact exponential does not apply:
		// deliver the plain conductive flux instead (ice melting slowly in warm air, steam condensing).
		if (BendingKernel::IsAtPhasePlateau(Matter, AmbientTemperatureK - State.TemperatureK))
		{
			HeatJ = Conductance * (AmbientTemperatureK - State.TemperatureK) * DeltaSeconds;
		}
		return AddHeat(State, HeatJ);
	}

	FVector DragForceN(const FVector& RelativeFlowMs, double FluidDensityKgM3, double DragCoefficient, double FrontalAreaM2)
	{
		const double Speed = RelativeFlowMs.Size();
		return 0.5 * FluidDensityKgM3 * DragCoefficient * FrontalAreaM2 * Speed * RelativeFlowMs;
	}

	FVector MomentumCouplingImpulse(const FElementalVolumeState& A, const FElementalVolumeState& B, double CouplingFraction)
	{
		const double InverseMassSum = A.GetInverseMass() + B.GetInverseMass();
		if (InverseMassSum <= 0.0 || CouplingFraction <= 0.0)
		{
			return FVector::ZeroVector;
		}
		const double ReducedMass = 1.0 / InverseMassSum;
		return ReducedMass * FMath::Clamp(CouplingFraction, 0.0, 1.0) * (B.VelocityCmS - A.VelocityCmS);
	}

	FVector MixtureVelocityCmS(const FElementalVolumeState& A, const FElementalVolumeState& B)
	{
		if (A.bImmovable || B.bImmovable)
		{
			return A.bImmovable ? A.VelocityCmS : B.VelocityCmS;
		}
		const double TotalMass = A.MassKg + B.MassKg;
		return TotalMass > UE_SMALL_NUMBER ? (A.MassKg * A.VelocityCmS + B.MassKg * B.VelocityCmS) / TotalMass : FVector::ZeroVector;
	}
}
