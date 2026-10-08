#include "Interaction/Reactions/ElementalReaction_Saturation.h"

UElementalReaction_Saturation::UElementalReaction_Saturation()
{
	SubstanceA = EElementalSubstance::Water;
	SubstanceB = EElementalSubstance::Earth;

	const BendingSim::FSaturationParams Defaults;
	InfiltrationRateMs = static_cast<float>(Defaults.InfiltrationRateMs);
	ImpactAbsorptionFactor = static_cast<float>(Defaults.ImpactAbsorptionFactor);
	MudThresholdSaturation = static_cast<float>(Defaults.MudThresholdSaturation);
	MinMudPorosity = static_cast<float>(Defaults.MinMudPorosity);
	MomentumCouplingTimeS = static_cast<float>(Defaults.MomentumCouplingTimeS);
}

void UElementalReaction_Saturation::React(BendingSim::FReactionContext& Context) const
{
	BendingSim::FSaturationParams Params;
	Params.InfiltrationRateMs = InfiltrationRateMs;
	Params.ImpactAbsorptionFactor = ImpactAbsorptionFactor;
	Params.MudThresholdSaturation = MudThresholdSaturation;
	Params.MinMudPorosity = MinMudPorosity;
	Params.MomentumCouplingTimeS = MomentumCouplingTimeS;
	Params.Strength = Strength;
	BendingSim::ReactSaturation(Params, Context);
}
