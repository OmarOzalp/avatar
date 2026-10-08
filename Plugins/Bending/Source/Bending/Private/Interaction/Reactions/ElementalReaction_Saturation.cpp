#include "Interaction/Reactions/ElementalReaction_Saturation.h"

#include "Physics/BendingUnits.h"
#include "Physics/ElementalPhysics.h"

UElementalReaction_Saturation::UElementalReaction_Saturation()
{
	SubstanceA = EElementalSubstance::Water;
	SubstanceB = EElementalSubstance::Earth;
}

void UElementalReaction_Saturation::React(FElementalReactionContext& Context) const
{
	FElementalVolumeState& Water = Context.A;
	FElementalVolumeState& Earth = Context.B;
	const FElementalContact& Contact = Context.Contact;
	const double DeltaSeconds = Context.DeltaSeconds;

	// Momentum: an inelastic splash shoves the earth.
	const double Coupling = (1.0 - FMath::Exp(-DeltaSeconds / MomentumCouplingTimeS)) * Contact.OverlapFraction * Strength;
	const FVector ImpulseOnWater = ElementalPhysics::MomentumCouplingImpulse(Water, Earth, FMath::Min(Coupling, 1.0));
	Water.PendingImpulseKgCmS += ImpulseOnWater;
	Earth.PendingImpulseKgCmS -= ImpulseOnWater;

	// Absorption into the pore volume.
	const double PoreCapacityKg = BendingPhysics::WaterDensity * Earth.Porosity * Earth.GetGeometricVolumeM3();
	if (PoreCapacityKg <= UE_SMALL_NUMBER || Earth.Saturation >= 1.f)
	{
		return;
	}

	const double ImpactSpeedMs = FMath::Max(FVector::DotProduct(BendingUnits::CmToM(Water.VelocityCmS - Earth.VelocityCmS), Contact.NormalAB), 0.0);
	const double AbsorptionRateKgS = BendingPhysics::WaterDensity * Contact.ExchangeAreaM2
		* (InfiltrationRateMs + ImpactAbsorptionFactor * ImpactSpeedMs) * Strength;
	const double RoomKg = PoreCapacityKg * (1.0 - Earth.Saturation);
	const double AbsorbedKg = FMath::Min3(AbsorptionRateKgS * DeltaSeconds, RoomKg, Water.MassKg);
	if (AbsorbedKg <= 0.0)
	{
		return;
	}

	const bool bWasMud = Earth.Saturation >= MudThresholdSaturation;
	const double WaterTemperatureK = Water.TemperatureK;
	const double WaterSpecificHeat = Water.GetSpecificHeat();
	const FVector WaterVelocityCmS = Water.VelocityCmS;

	// The absorbed water's mass, momentum and heat go into the earth (wet soil is heavier and has a higher heat capacity).
	Water.MassKg -= AbsorbedKg;
	ElementalPhysics::EntrainMass(Earth, AbsorbedKg, WaterTemperatureK, WaterSpecificHeat, WaterVelocityCmS);
	Earth.Saturation = static_cast<float>(FMath::Min(Earth.Saturation + AbsorbedKg / PoreCapacityKg, 1.0));

	Context.EmitEvent(EElementalReactionType::Saturation, AbsorbedKg, 0.0);
	if (!bWasMud && Earth.Saturation >= MudThresholdSaturation)
	{
		Context.EmitEvent(EElementalReactionType::MudFormed, Earth.MassKg, 0.0);
	}
}
