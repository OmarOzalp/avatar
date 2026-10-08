#include "Interaction/Reactions/ElementalReaction_HeatExchange.h"

#include "BendingSettings.h"
#include "Physics/ElementalPhysics.h"

UElementalReaction_HeatExchange::UElementalReaction_HeatExchange()
{
	SubstanceA = EElementalSubstance::Fire;
	SubstanceB = EElementalSubstance::Water;
}

void UElementalReaction_HeatExchange::React(FElementalReactionContext& Context) const
{
	FElementalVolumeState& A = Context.A;
	FElementalVolumeState& B = Context.B;
	const FElementalContact& Contact = Context.Contact;
	const double DeltaSeconds = Context.DeltaSeconds;

	// Momentum: interpenetrating matter drags itself toward a common velocity (water smothers a fire jet).
	const double Coupling = (1.0 - FMath::Exp(-DeltaSeconds / MomentumCouplingTimeS)) * Contact.OverlapFraction * Strength;
	const FVector ImpulseOnA = ElementalPhysics::MomentumCouplingImpulse(A, B, FMath::Min(Coupling, 1.0));
	A.PendingImpulseKgCmS += ImpulseOnA;
	B.PendingImpulseKgCmS -= ImpulseOnA;

	const double DeltaT = A.TemperatureK - B.TemperatureK;
	if (FMath::Abs(DeltaT) < 0.01)
	{
		return;
	}
	FElementalVolumeState& Hot = DeltaT > 0.0 ? A : B;
	FElementalVolumeState& Cold = DeltaT > 0.0 ? B : A;

	double HeatJ = HeatTransferCoefficient * Contact.ExchangeAreaM2 * FMath::Abs(DeltaT) * DeltaSeconds
		* Strength * Context.Settings.HeatTransferScale;
	HeatJ = FMath::Min(HeatJ, ElementalPhysics::MaxHeatFlow(Hot, Cold));
	if (HeatJ <= 0.0)
	{
		return;
	}

	// Vapor leaves with the mixture's velocity; capture it before masses change.
	const FVector VaporVelocityCmS = ElementalPhysics::MixtureVelocityCmS(A, B);

	double VaporizedKg = 0.0;
	double BulkHeatJ = HeatJ;
	if (Cold.Substance == EElementalSubstance::Water && Hot.TemperatureK > BendingKernel::Constants::WaterBoilingPointK)
	{
		const double FlashHeatJ = HeatJ * SurfaceFlashFraction;
		VaporizedKg += ElementalPhysics::FlashVaporize(Cold, FlashHeatJ);
		BulkHeatJ -= FlashHeatJ;
	}
	const FElementalPhaseChange ColdChange = ElementalPhysics::AddHeat(Cold, BulkHeatJ);
	const FElementalPhaseChange HotChange = ElementalPhysics::AddHeat(Hot, -HeatJ);
	VaporizedKg += ColdChange.VaporizedKg;

	if (VaporizedKg > 0.0)
	{
		Context.EmitEvent(EElementalReactionType::Evaporation, VaporizedKg, HeatJ);

		FElementalVolumeState Steam = FElementalVolumeState::MakeDefault(EElementalSubstance::Steam, VaporizedKg);
		Steam.LocationCm = Contact.PointCm;
		Steam.VelocityCmS = VaporVelocityCmS;
		Context.SpawnFreeVolume(Steam);
	}
	if (ColdChange.MeltedKg > 0.0)
	{
		Context.EmitEvent(EElementalReactionType::Melting, ColdChange.MeltedKg, HeatJ);
	}
	if (HotChange.CondensedKg > 0.0)
	{
		Context.EmitEvent(EElementalReactionType::Condensation, HotChange.CondensedKg, HeatJ);
	}
}
