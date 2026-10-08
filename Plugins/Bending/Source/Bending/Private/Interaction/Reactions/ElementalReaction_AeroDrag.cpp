#include "Interaction/Reactions/ElementalReaction_AeroDrag.h"

#include "BendingSettings.h"
#include "Physics/BendingUnits.h"
#include "Physics/ElementalPhysics.h"

UElementalReaction_AeroDrag::UElementalReaction_AeroDrag()
{
	SubstanceA = EElementalSubstance::Air;
	SubstanceB = EElementalSubstance::Earth;
}

void UElementalReaction_AeroDrag::React(FElementalReactionContext& Context) const
{
	FElementalVolumeState& Air = Context.A;
	FElementalVolumeState& Body = Context.B;
	const FElementalContact& Contact = Context.Contact;
	const double DeltaSeconds = Context.DeltaSeconds;

	const double FullFrontalAreaM2 = Body.GetFrontalAreaM2();
	const double DragAreaM2 = FullFrontalAreaM2 * Contact.ImmersionB;
	if (FullFrontalAreaM2 <= 0.0 || Contact.ImmersionB <= 0.0)
	{
		return;
	}

	const FVector FlowMs = BendingUnits::CmToM(Air.VelocityCmS - Body.VelocityCmS);
	const double AirDensity = Air.GetDensityKgM3();

	FVector ForceN = ElementalPhysics::DragForceN(FlowMs, AirDensity, Body.DragCoefficient, DragAreaM2);

	const double OverpressurePa = FMath::Max((AirDensity / Context.Settings.AmbientAirDensityKgM3 - 1.0) * BendingPhysics::StandardPressurePa, 0.0);
	const double FrontWeight = 1.0 - FMath::Abs(2.0 * Contact.ImmersionB - 1.0);
	ForceN += Contact.NormalAB * (OverpressurePa * FullFrontalAreaM2 * FrontWeight * PressureCoupling);

	FVector ImpulseNs = ForceN * (DeltaSeconds * Strength);

	// Explicit drag must never push a light body past the wind's velocity: cap at a full inelastic merge.
	const double MaxImpulseNs = BendingUnits::ImpulseEngineToSI(ElementalPhysics::MomentumCouplingImpulse(Body, Air, 1.0)).Size();
	const double ImpulseMagnitude = ImpulseNs.Size();
	if (ImpulseMagnitude > MaxImpulseNs && ImpulseMagnitude > 0.0)
	{
		ImpulseNs *= MaxImpulseNs / ImpulseMagnitude;
	}

	const FVector ImpulseKgCmS = BendingUnits::ImpulseSIToEngine(ImpulseNs);
	Body.PendingImpulseKgCmS += ImpulseKgCmS;
	Air.PendingImpulseKgCmS -= ImpulseKgCmS;

	if (!Body.bImmovable && Body.MassKg > UE_SMALL_NUMBER && !ImpulseNs.IsNearlyZero())
	{
		// Kinetic energy handed to the body: what separates a nudge from a deflection.
		const FVector VelocityMs = BendingUnits::CmToM(Body.VelocityCmS);
		const FVector NewVelocityMs = VelocityMs + ImpulseNs / Body.MassKg;
		const double WorkJ = 0.5 * Body.MassKg * (NewVelocityMs.SizeSquared() - VelocityMs.SizeSquared());
		Context.EmitEvent(EElementalReactionType::Deflection, 0.0, FMath::Abs(WorkJ));
	}

	// Loose, dry earth erodes once the dynamic pressure mobilizes its grains.
	if (Body.Substance == EElementalSubstance::Earth && Body.Porosity >= ErodiblePorosity && Body.Saturation <= MaxErodibleSaturation)
	{
		const double DynamicPressurePa = BendingPhysics::DynamicPressure(AirDensity, FlowMs.Size());
		if (DynamicPressurePa > CriticalErosionPressurePa)
		{
			const double ErodedKg = FMath::Min(
				ErosionCoefficient * (DynamicPressurePa - CriticalErosionPressurePa) * DragAreaM2 * DeltaSeconds * Strength,
				Body.MassKg);
			Body.MassKg -= ErodedKg;
			Context.EmitEvent(EElementalReactionType::Erosion, ErodedKg, 0.0);
		}
	}
}
