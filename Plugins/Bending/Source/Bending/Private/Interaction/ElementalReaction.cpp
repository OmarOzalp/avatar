#include "Interaction/ElementalReaction.h"

#include "Interaction/BendingInteractionSubsystem.h"

FElementalReactionContext::FElementalReactionContext(UBendingInteractionSubsystem& InSubsystem, const UBendingSettings& InSettings,
	FElementalVolumeHandle InHandleA, FElementalVolumeHandle InHandleB,
	FElementalVolumeState& InA, FElementalVolumeState& InB, const FElementalContact& InContact, double InDeltaSeconds)
	: Subsystem(InSubsystem)
	, Settings(InSettings)
	, HandleA(InHandleA)
	, HandleB(InHandleB)
	, A(InA)
	, B(InB)
	, Contact(InContact)
	, DeltaSeconds(InDeltaSeconds)
{
}

void FElementalReactionContext::EmitEvent(EElementalReactionType Type, double MassKg, double EnergyJ) const
{
	Subsystem.EmitReactionEvent(*this, Type, MassKg, EnergyJ);
}

void FElementalReactionContext::SpawnFreeVolume(const FElementalVolumeState& State) const
{
	Subsystem.QueueFreeVolume(State);
}
