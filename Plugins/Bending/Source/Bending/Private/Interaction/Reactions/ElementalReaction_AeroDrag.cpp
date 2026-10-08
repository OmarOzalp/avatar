#include "Interaction/Reactions/ElementalReaction_AeroDrag.h"

UElementalReaction_AeroDrag::UElementalReaction_AeroDrag()
{
	SubstanceA = EElementalSubstance::Air;
	SubstanceB = EElementalSubstance::Earth;

	const BendingSim::FAeroDragParams Defaults;
	ErodiblePorosity = static_cast<float>(Defaults.ErodiblePorosity);
	MaxErodibleSaturation = static_cast<float>(Defaults.MaxErodibleSaturation);
	CriticalErosionPressurePa = static_cast<float>(Defaults.CriticalErosionPressurePa);
	ErosionCoefficient = static_cast<float>(Defaults.ErosionCoefficient);
}

void UElementalReaction_AeroDrag::React(BendingSim::FReactionContext& Context) const
{
	BendingSim::FAeroDragParams Params;
	Params.ErodiblePorosity = ErodiblePorosity;
	Params.MaxErodibleSaturation = MaxErodibleSaturation;
	Params.CriticalErosionPressurePa = CriticalErosionPressurePa;
	Params.ErosionCoefficient = ErosionCoefficient;
	Params.Strength = Strength;
	BendingSim::ReactAeroDrag(Params, Context);
}
