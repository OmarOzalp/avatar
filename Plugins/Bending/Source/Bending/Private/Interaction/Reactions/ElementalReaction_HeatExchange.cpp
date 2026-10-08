#include "Interaction/Reactions/ElementalReaction_HeatExchange.h"

UElementalReaction_HeatExchange::UElementalReaction_HeatExchange()
{
	SubstanceA = EElementalSubstance::Fire;
	SubstanceB = EElementalSubstance::Water;

	const BendingSim::FHeatExchangeParams Defaults;
	HeatTransferCoefficient = static_cast<float>(Defaults.HeatTransferCoefficient);
	SurfaceFlashFraction = static_cast<float>(Defaults.SurfaceFlashFraction);
	MomentumCouplingTimeS = static_cast<float>(Defaults.MomentumCouplingTimeS);
}

void UElementalReaction_HeatExchange::React(BendingSim::FReactionContext& Context) const
{
	BendingSim::FHeatExchangeParams Params;
	Params.HeatTransferCoefficient = HeatTransferCoefficient;
	Params.SurfaceFlashFraction = SurfaceFlashFraction;
	Params.MomentumCouplingTimeS = MomentumCouplingTimeS;
	Params.Strength = Strength;
	BendingSim::ReactHeatExchange(Params, Context);
}
