#pragma once

#include "BendingTypes.h"
#include "NativeGameplayTags.h"

namespace BendingTags
{
	// Input: Enhanced Input actions map onto these; moves declare which one triggers them.
	BENDING_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Input_Bending);
	BENDING_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Input_Bending_Light);
	BENDING_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Input_Bending_Heavy);
	BENDING_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Input_Bending_Special);
	BENDING_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Input_Bending_Utility);
	BENDING_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Input_Bending_Signature);

	// Classification of every bending move ability.
	BENDING_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_Bending);

	// Loose tags on the owner's ability system while a move runs.
	BENDING_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Bending_Startup);
	BENDING_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Bending_Active);
	BENDING_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Bending_Recovery);
	BENDING_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Bending_CancelWindow);

	// Gameplay events sent to the owner's ability system.
	BENDING_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Bending_Phase);
	BENDING_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Bending_Phase_Startup);
	BENDING_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Bending_Phase_Active);
	BENDING_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Bending_Phase_Recovery);
	BENDING_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Bending_Phase_End);
	BENDING_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Bending_Hit);

	// SetByCaller magnitudes for the resource cost effect.
	BENDING_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(SetByCaller_Bending_Chi);
	BENDING_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(SetByCaller_Bending_Stamina);

	BENDING_API FGameplayTag GetPhaseStateTag(EBendingPhase Phase);
	BENDING_API FGameplayTag GetPhaseEventTag(EBendingPhase Phase);
}
