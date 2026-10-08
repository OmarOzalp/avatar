#include "Interaction/ElementalReaction.h"

namespace
{
	void CallElementalReaction(const void* UserData, BendingSim::FReactionContext& Context)
	{
		static_cast<const UElementalReaction*>(UserData)->React(Context);
	}
}

BendingSim::FReactionEntry UElementalReaction::MakeSimEntry() const
{
	BendingSim::FReactionEntry Entry;
	Entry.SubstanceA = ElementalSubstance::ToSim(SubstanceA);
	Entry.SubstanceB = ElementalSubstance::ToSim(SubstanceB);
	Entry.Function = &CallElementalReaction;
	Entry.UserData = this;
	return Entry;
}
