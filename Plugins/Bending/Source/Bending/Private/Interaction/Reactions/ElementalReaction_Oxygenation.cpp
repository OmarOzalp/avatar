#include "Interaction/Reactions/ElementalReaction_Oxygenation.h"

UElementalReaction_Oxygenation::UElementalReaction_Oxygenation()
{
	SubstanceA = EElementalSubstance::Air;
	SubstanceB = EElementalSubstance::Fire;

	const BendingSim::FOxygenationParams Defaults;
	CombustionEfficiency = static_cast<float>(Defaults.CombustionEfficiency);
	MaxEntrainmentPerFlameKgPerS = static_cast<float>(Defaults.MaxEntrainmentPerFlameKgPerS);
	MixingEntrainmentSpeedMs = static_cast<float>(Defaults.MixingEntrainmentSpeedMs);
}

void UElementalReaction_Oxygenation::React(BendingSim::FReactionContext& Context) const
{
	BendingSim::FOxygenationParams Params;
	Params.CombustionEfficiency = CombustionEfficiency;
	Params.MaxEntrainmentPerFlameKgPerS = MaxEntrainmentPerFlameKgPerS;
	Params.MixingEntrainmentSpeedMs = MixingEntrainmentSpeedMs;
	Params.Strength = Strength;
	BendingSim::ReactOxygenation(Params, Context);
}
