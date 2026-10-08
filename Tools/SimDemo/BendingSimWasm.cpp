// WebAssembly bindings for the bending simulation demo: one global FDemo, flat exports for JavaScript.
// Built freestanding (no libc, no libc++) by Tools/SimDemo/build_wasm.sh.

#include "BendingSimDemo.h"

using namespace BendingSimDemo;

#define BS_EXPORT(Name) extern "C" __attribute__((export_name(#Name)))

namespace
{
	constexpr int VolumeStride = 20;
	constexpr int EventStride = 12;
	constexpr int PatchStride = 6;
	constexpr int MaxFrameEvents = 512;

	// Zero-initialized storage, constructed on first use, keeps the demo's preallocated arrays out of the module's
	// data segment (the module would otherwise carry ~400 KB of default-initialized slots).
	union FDemoStorage
	{
		FDemo Demo;
		char Unused;
		constexpr FDemoStorage() : Unused(0) {}
	};
	static_assert(__is_trivially_copyable(FDemo), "Demo() relies on assignment to start the union member's lifetime");
	FDemoStorage GStorage;
	bool bDemoConstructed = false;

	FDemo& Demo()
	{
		if (!bDemoConstructed)
		{
			GStorage.Demo = FDemo(); // FDemo is trivially copyable: assignment begins the member's lifetime.
			bDemoConstructed = true;
		}
		return GStorage.Demo;
	}

	FReactionEvent GEvents[MaxFrameEvents];
	double GVolumeBuffer[FSimWorld::MaxVolumes * VolumeStride];
	double GEventBuffer[MaxFrameEvents * EventStride];
	double GPatchBuffer[FSimWorld::MaxMoisturePatches * PatchStride];
	int GActorForSlot[FSimWorld::MaxVolumes];
}

BS_EXPORT(bs_scenario_count) int BsScenarioCount() { return NumScenarios; }
BS_EXPORT(bs_scenario_name) const char* BsScenarioName(int Id) { return GetScenarioInfo(Id).Name; }
BS_EXPORT(bs_scenario_summary) const char* BsScenarioSummary(int Id) { return GetScenarioInfo(Id).Summary; }
BS_EXPORT(bs_scenario_duration) double BsScenarioDuration(int Id) { return GetScenarioInfo(Id).DurationS; }

BS_EXPORT(bs_load) void BsLoad(int Id) { Demo().Load(Id); }
BS_EXPORT(bs_time) double BsTime() { return Demo().TimeS; }

/** Advances the demo and returns the number of events flushed this frame into bs_event_buffer. */
BS_EXPORT(bs_advance) int BsAdvance(double FrameSeconds)
{
	Demo().Advance(FrameSeconds);
	const int Count = Demo().World.FlushEvents(FrameSeconds, GEvents, MaxFrameEvents);
	for (int Index = 0; Index < Count; ++Index)
	{
		const FReactionEvent& Event = GEvents[Index];
		double* Out = &GEventBuffer[Index * EventStride];
		Out[0] = static_cast<double>(Event.Type);
		Out[1] = static_cast<double>(Event.SubstanceA);
		Out[2] = static_cast<double>(Event.SubstanceB);
		Out[3] = Event.LocationCm.X;
		Out[4] = Event.LocationCm.Y;
		Out[5] = Event.LocationCm.Z;
		Out[6] = Event.MassKg;
		Out[7] = Event.EnergyJ;
		Out[8] = Event.MassRateKgS;
		Out[9] = Event.PowerW;
		Out[10] = Event.bDiscrete ? 1.0 : 0.0;
		Out[11] = Event.WindowSeconds;
	}
	return Count;
}

BS_EXPORT(bs_event_buffer) double* BsEventBuffer() { return GEventBuffer; }

/** Packs live volumes into bs_volume_buffer and returns how many. */
BS_EXPORT(bs_pack_volumes) int BsPackVolumes()
{
	for (int& Actor : GActorForSlot)
	{
		Actor = -1;
	}
	for (int ActorIndex = 0; ActorIndex < Demo().NumActors; ++ActorIndex)
	{
		const FActor& Actor = Demo().Actors[ActorIndex];
		if (Actor.bAlive && Actor.Handle.Index >= 0)
		{
			GActorForSlot[Actor.Handle.Index] = ActorIndex;
		}
	}

	int Count = 0;
	for (int Index = 0; Index < Demo().World.GetSlotLimit(); ++Index)
	{
		const FHandle Handle = Demo().World.GetHandleAt(Index);
		const FVolume* Volume = Demo().World.GetVolume(Handle);
		if (!Volume)
		{
			continue;
		}
		double* Out = &GVolumeBuffer[Count * VolumeStride];
		const double LatentScale = Volume->MassKg * Thermo::Constants::LatentHeatFusion;
		Out[0] = Index;
		Out[1] = GActorForSlot[Index];
		Out[2] = static_cast<double>(Volume->Substance);
		Out[3] = Volume->MassKg;
		Out[4] = Volume->TemperatureK;
		Out[5] = Volume->RadiusCm;
		Out[6] = Volume->LocationCm.X;
		Out[7] = Volume->LocationCm.Y;
		Out[8] = Volume->LocationCm.Z;
		Out[9] = Volume->VelocityCmS.X;
		Out[10] = Volume->VelocityCmS.Y;
		Out[11] = Volume->VelocityCmS.Z;
		Out[12] = Volume->Saturation;
		Out[13] = Volume->Porosity;
		Out[14] = Demo().World.IsOwned(Handle) ? 1.0 : 0.0;
		Out[15] = static_cast<double>(Volume->Shape);
		Out[16] = Volume->CapsuleHalfAxisCm.X;
		Out[17] = Volume->CapsuleHalfAxisCm.Y;
		Out[18] = Volume->CapsuleHalfAxisCm.Z;
		Out[19] = LatentScale > 0.0 ? Volume->LatentHeatJ / LatentScale : 0.0;
		++Count;
	}
	return Count;
}

BS_EXPORT(bs_volume_buffer) double* BsVolumeBuffer() { return GVolumeBuffer; }
BS_EXPORT(bs_actor_label) const char* BsActorLabel(int ActorIndex)
{
	return ActorIndex >= 0 && ActorIndex < Demo().NumActors ? Demo().Actors[ActorIndex].Label : "";
}

BS_EXPORT(bs_pack_patches) int BsPackPatches()
{
	const int Count = Demo().World.GetNumMoisturePatches();
	for (int Index = 0; Index < Count; ++Index)
	{
		const FMoisturePatch& Patch = Demo().World.GetMoisturePatch(Index);
		double* Out = &GPatchBuffer[Index * PatchStride];
		Out[0] = Patch.CenterCm.X;
		Out[1] = Patch.CenterCm.Y;
		Out[2] = Patch.CenterCm.Z;
		Out[3] = Patch.RadiusCm;
		Out[4] = Patch.WaterKg;
		Out[5] = Patch.GetSaturation(Demo().World.Settings.MoistureSoilDepthM);
	}
	return Count;
}

BS_EXPORT(bs_patch_buffer) double* BsPatchBuffer() { return GPatchBuffer; }
BS_EXPORT(bs_traction_at) double BsTractionAt(double X, double Z) { return Demo().World.GetSurfaceTractionMultiplierAt(FVec3(X, 0.0, Z)); }
BS_EXPORT(bs_bender_thermal_work) double BsBenderThermalWork() { return Demo().BenderThermalWorkJ; }
BS_EXPORT(bs_bender_kinetic_work) double BsBenderKineticWork() { return Demo().BenderKineticWorkJ; }
BS_EXPORT(bs_contacts) int BsContacts() { return Demo().World.GetStats().ContactsLastStep; }
BS_EXPORT(bs_steps) double BsSteps() { return static_cast<double>(Demo().World.GetStats().Steps); }

BS_EXPORT(bs_spawn) int BsSpawn(int Preset, double X, double Z, double VX, double VZ)
{
	return Demo().SpawnPreset(Preset, FVec3(X, 0.0, Z), FVec3(VX, 0.0, VZ));
}
BS_EXPORT(bs_freeze_near) double BsFreezeNear(double X, double Z, double Radius) { return Demo().BenderFreezeNearest(FVec3(X, 0.0, Z), Radius); }
BS_EXPORT(bs_heat_near) double BsHeatNear(double X, double Z, double Radius, double HeatJ) { return Demo().BenderHeatNearest(FVec3(X, 0.0, Z), Radius, HeatJ); }
BS_EXPORT(bs_deposit_water) void BsDepositWater(double X, double Kg) { Demo().World.DepositWaterOnSurface(FVec3(X, 0.0, 0.0), Kg, Demo().GroundPorosity); }

/** Tunables exposed to the sandbox UI. Load() resets them, so the page re-applies its sliders after loading. */
BS_EXPORT(bs_set_param) void BsSetParam(int Id, double Value)
{
	FSimSettings& Settings = Demo().World.Settings;
	FDefaultReactionParams& Params = Demo().Params;
	switch (Id)
	{
	case 0: Settings.AmbientTemperatureK = Value; break;
	case 1: Settings.HeatTransferScale = Value; break;
	case 2: Params.HeatExchange.SurfaceFlashFraction = Value; break;
	case 3: Params.Oxygenation.CombustionEfficiency = Value; break;
	case 4: Params.Saturation.ImpactAbsorptionFactor = Value; break;
	case 5: Params.AeroDrag.Strength = Value; break;
	case 6: Settings.DryingTimeScale = Value; break;
	case 7: Settings.MudFrictionRatio = Value; break;
	case 8: Demo().GroundPorosity = Value; break;
	default: break;
	}
}

BS_EXPORT(bs_get_param) double BsGetParam(int Id)
{
	const FSimSettings& Settings = Demo().World.Settings;
	const FDefaultReactionParams& Params = Demo().Params;
	switch (Id)
	{
	case 0: return Settings.AmbientTemperatureK;
	case 1: return Settings.HeatTransferScale;
	case 2: return Params.HeatExchange.SurfaceFlashFraction;
	case 3: return Params.Oxygenation.CombustionEfficiency;
	case 4: return Params.Saturation.ImpactAbsorptionFactor;
	case 5: return Params.AeroDrag.Strength;
	case 6: return Settings.DryingTimeScale;
	case 7: return Settings.MudFrictionRatio;
	case 8: return Demo().GroundPorosity;
	default: return 0.0;
	}
}
