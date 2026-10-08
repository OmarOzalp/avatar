#include "Interaction/ElementalVolumeTypes.h"

#include "Physics/BendingUnits.h"

FElementalVolumeState FElementalVolumeState::MakeDefault(EElementalSubstance InSubstance, double InMassKg)
{
	return FromSim(BendingSim::FVolume::MakeDefault(ElementalSubstance::ToSim(InSubstance), InMassKg));
}

BendingSim::FVolume FElementalVolumeState::ToSim() const
{
	BendingSim::FVolume Volume;
	Volume.Substance = ElementalSubstance::ToSim(Substance);
	Volume.MassKg = MassKg;
	Volume.TemperatureK = TemperatureK;
	Volume.LatentHeatJ = LatentHeatJ;
	Volume.SpecificHeatOverride = SpecificHeatOverride;
	Volume.Porosity = Porosity;
	Volume.Saturation = Saturation;
	Volume.DragCoefficient = DragCoefficient;
	Volume.Shape = static_cast<BendingSim::EShape>(Shape);
	Volume.RadiusCm = RadiusCm;
	Volume.CapsuleHalfAxisCm = BendingUnits::ToSim(CapsuleHalfAxisCm);
	Volume.bDeriveRadiusFromMass = bDeriveRadiusFromMass;
	Volume.bImmovable = bImmovable;
	Volume.LocationCm = BendingUnits::ToSim(LocationCm);
	Volume.VelocityCmS = BendingUnits::ToSim(VelocityCmS);
	return Volume;
}

FElementalVolumeState FElementalVolumeState::FromSim(const BendingSim::FVolume& Volume)
{
	FElementalVolumeState State;
	State.Substance = ElementalSubstance::FromSim(Volume.Substance);
	State.MassKg = Volume.MassKg;
	State.TemperatureK = Volume.TemperatureK;
	State.LatentHeatJ = Volume.LatentHeatJ;
	State.SpecificHeatOverride = Volume.SpecificHeatOverride;
	State.Porosity = static_cast<float>(Volume.Porosity);
	State.Saturation = static_cast<float>(Volume.Saturation);
	State.DragCoefficient = static_cast<float>(Volume.DragCoefficient);
	State.Shape = static_cast<EElementalVolumeShape>(Volume.Shape);
	State.RadiusCm = static_cast<float>(Volume.RadiusCm);
	State.CapsuleHalfAxisCm = BendingUnits::ToEngine(Volume.CapsuleHalfAxisCm);
	State.bDeriveRadiusFromMass = Volume.bDeriveRadiusFromMass;
	State.bImmovable = Volume.bImmovable;
	State.LocationCm = BendingUnits::ToEngine(Volume.LocationCm);
	State.VelocityCmS = BendingUnits::ToEngine(Volume.VelocityCmS);
	return State;
}

FElementalReactionEvent FElementalReactionEvent::FromSim(const BendingSim::FReactionEvent& Event)
{
	FElementalReactionEvent Out;
	Out.Type = static_cast<EElementalReactionType>(Event.Type);
	Out.SubstanceA = ElementalSubstance::FromSim(Event.SubstanceA);
	Out.SubstanceB = ElementalSubstance::FromSim(Event.SubstanceB);
	Out.VolumeA = FElementalVolumeHandle::FromSim(Event.VolumeA);
	Out.VolumeB = FElementalVolumeHandle::FromSim(Event.VolumeB);
	Out.LocationCm = BendingUnits::ToEngine(Event.LocationCm);
	Out.Normal = BendingUnits::ToEngine(Event.Normal);
	Out.MassKg = Event.MassKg;
	Out.EnergyJ = Event.EnergyJ;
	Out.MassRateKgS = Event.MassRateKgS;
	Out.PowerW = Event.PowerW;
	Out.WindowSeconds = static_cast<float>(Event.WindowSeconds);
	Out.bDiscrete = Event.bDiscrete;
	return Out;
}
