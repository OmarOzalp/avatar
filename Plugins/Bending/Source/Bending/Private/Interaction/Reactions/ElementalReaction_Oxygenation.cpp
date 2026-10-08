#include "Interaction/Reactions/ElementalReaction_Oxygenation.h"

#include "BendingSettings.h"
#include "Physics/BendingUnits.h"
#include "Physics/ElementalPhysics.h"

UElementalReaction_Oxygenation::UElementalReaction_Oxygenation()
{
	SubstanceA = EElementalSubstance::Air;
	SubstanceB = EElementalSubstance::Fire;
}

void UElementalReaction_Oxygenation::React(FElementalReactionContext& Context) const
{
	FElementalVolumeState& Air = Context.A;
	FElementalVolumeState& Fire = Context.B;
	const FElementalContact& Contact = Context.Contact;
	const double DeltaSeconds = Context.DeltaSeconds;

	// Air entering the flame through the contact surface: normal component of the relative flow (normal points air -> fire).
	const FVector RelativeFlowMs = BendingUnits::CmToM(Air.VelocityCmS - Fire.VelocityCmS);
	const double InflowSpeedMs = FMath::Max(FVector::DotProduct(RelativeFlowMs, Contact.NormalAB), 0.0) + MixingEntrainmentSpeedMs;

	double AirMassKg = Air.GetDensityKgM3() * Contact.ExchangeAreaM2 * InflowSpeedMs * DeltaSeconds * Strength;
	AirMassKg = FMath::Min(AirMassKg, MaxEntrainmentPerFlameKgPerS * Fire.MassKg * DeltaSeconds);
	AirMassKg = FMath::Min(AirMassKg, Air.MassKg * Contact.OverlapFraction);
	if (AirMassKg <= 0.0)
	{
		return;
	}

	// Entrained air becomes combustion gas: mass, momentum and sensible heat move with it...
	const double AirTemperatureK = Air.TemperatureK;
	const double AirSpecificHeat = Air.GetSpecificHeat();
	const FVector AirVelocityCmS = Air.VelocityCmS;
	Air.MassKg -= AirMassKg;
	ElementalPhysics::EntrainMass(Fire, AirMassKg, AirTemperatureK, AirSpecificHeat, AirVelocityCmS);

	// ...and its oxygen burns, capped at the adiabatic flame temperature.
	const double CombustionHeatJ = AirMassKg * BendingPhysics::CombustionHeatPerKgAir * CombustionEfficiency;
	ElementalPhysics::AddHeat(Fire, CombustionHeatJ);
	Fire.TemperatureK = FMath::Min(Fire.TemperatureK, static_cast<double>(Context.Settings.MaxFlameTemperatureK));

	Context.EmitEvent(EElementalReactionType::Oxygenation, AirMassKg, CombustionHeatJ);
}
