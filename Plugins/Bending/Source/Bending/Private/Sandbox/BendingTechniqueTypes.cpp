#include "Sandbox/BendingTechniqueTypes.h"

#include "BendingGameplayTags.h"

namespace BendingTechnique
{
	FGameplayTag GetInputTag(BendingSim::ETechniqueSlot Slot)
	{
		switch (Slot)
		{
		case BendingSim::ETechniqueSlot::Primary:   return BendingTags::Input_Bending_Light;
		case BendingSim::ETechniqueSlot::Secondary: return BendingTags::Input_Bending_Heavy;
		case BendingSim::ETechniqueSlot::Special:   return BendingTags::Input_Bending_Special;
		case BendingSim::ETechniqueSlot::Utility:   return BendingTags::Input_Bending_Utility;
		case BendingSim::ETechniqueSlot::Signature: return BendingTags::Input_Bending_Signature;
		default:                                    return FGameplayTag();
		}
	}

	const TCHAR* GetKeyLabel(BendingSim::ETechniqueSlot Slot)
	{
		switch (Slot)
		{
		case BendingSim::ETechniqueSlot::Primary:   return TEXT("LMB");
		case BendingSim::ETechniqueSlot::Secondary: return TEXT("RMB");
		case BendingSim::ETechniqueSlot::Special:   return TEXT("Q");
		case BendingSim::ETechniqueSlot::Utility:   return TEXT("E");
		case BendingSim::ETechniqueSlot::Signature: return TEXT("F");
		default:                                    return TEXT("");
		}
	}

	FString GetDisplayName(EBendingTechnique Technique)
	{
		return FString(UTF8_TO_TCHAR(GetInfo(Technique).Name));
	}

	FString GetDescription(EBendingTechnique Technique)
	{
		return FString(UTF8_TO_TCHAR(GetInfo(Technique).Description));
	}

	EBendingTechnique FindTechnique(EBendingElement Element, BendingSim::ETechniqueSlot Slot)
	{
		return FromSim(BendingSim::FindTechnique(FromElement(Element), Slot));
	}
}
