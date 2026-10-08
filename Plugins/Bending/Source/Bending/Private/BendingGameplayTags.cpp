#include "BendingGameplayTags.h"

namespace BendingTags
{
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Input_Bending, "Input.Bending", "Parent of all bending input tags.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Input_Bending_Light, "Input.Bending.Light", "Fast strike input.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Input_Bending_Heavy, "Input.Bending.Heavy", "Heavy / chargeable strike input.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Input_Bending_Special, "Input.Bending.Special", "Element special input.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Input_Bending_Utility, "Input.Bending.Utility", "Defensive / mobility input.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Ability_Bending, "Ability.Bending", "Every bending move ability.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_Bending_Startup, "State.Bending.Startup", "A move is in its startup frames.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_Bending_Active, "State.Bending.Active", "A move is in its active frames.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_Bending_Recovery, "State.Bending.Recovery", "A move is in its recovery frames.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_Bending_CancelWindow, "State.Bending.CancelWindow", "The current move may be cancelled into another.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Event_Bending_Phase, "Event.Bending.Phase", "Parent of move phase events.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Event_Bending_Phase_Startup, "Event.Bending.Phase.Startup", "Move entered startup.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Event_Bending_Phase_Active, "Event.Bending.Phase.Active", "Move entered its active frames.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Event_Bending_Phase_Recovery, "Event.Bending.Phase.Recovery", "Move entered recovery.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Event_Bending_Phase_End, "Event.Bending.Phase.End", "Move finished its last recovery frame.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Event_Bending_Hit, "Event.Bending.Hit", "The current move connected.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(SetByCaller_Bending_Chi, "SetByCaller.Bending.Chi", "Chi delta applied by the bending cost effect.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(SetByCaller_Bending_Stamina, "SetByCaller.Bending.Stamina", "Stamina delta applied by the bending cost effect.");

	FGameplayTag GetPhaseStateTag(EBendingPhase Phase)
	{
		switch (Phase)
		{
		case EBendingPhase::Startup:  return State_Bending_Startup;
		case EBendingPhase::Active:   return State_Bending_Active;
		case EBendingPhase::Recovery: return State_Bending_Recovery;
		default:                      return FGameplayTag();
		}
	}

	FGameplayTag GetPhaseEventTag(EBendingPhase Phase)
	{
		switch (Phase)
		{
		case EBendingPhase::Startup:  return Event_Bending_Phase_Startup;
		case EBendingPhase::Active:   return Event_Bending_Phase_Active;
		case EBendingPhase::Recovery: return Event_Bending_Phase_Recovery;
		default:                      return Event_Bending_Phase_End;
		}
	}
}
