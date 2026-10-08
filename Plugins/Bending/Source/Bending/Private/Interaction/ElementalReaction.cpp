#include "Interaction/ElementalReaction.h"

#include "BendingLog.h"
#include "UObject/Class.h"

namespace
{
	void CallElementalReaction(const void* UserData, BendingSim::FReactionContext& Context)
	{
		static_cast<const UElementalReaction*>(UserData)->React(Context);
	}
}

bool UElementalReaction::MakeSimEntry(BendingSim::FReactionEntry& OutEntry) const
{
	EElementalSubstance First = SubstanceA;
	EElementalSubstance Second = SubstanceB;
	const EElementalSubstance Required = GetRequiredSubstanceA();
	if (Required != EElementalSubstance::None && First != Required)
	{
		if (Second != Required)
		{
			UE_LOG(LogBending, Warning, TEXT("%s: needs %s as one of its substances; not registered."),
				*GetPathName(), *UEnum::GetValueAsString(Required));
			return false;
		}
		Swap(First, Second);
	}

	OutEntry.SubstanceA = ElementalSubstance::ToSim(First);
	OutEntry.SubstanceB = ElementalSubstance::ToSim(Second);
	OutEntry.Function = &CallElementalReaction;
	OutEntry.UserData = this;
	return true;
}
