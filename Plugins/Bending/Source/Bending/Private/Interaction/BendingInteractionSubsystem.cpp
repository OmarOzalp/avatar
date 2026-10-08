#include "Interaction/BendingInteractionSubsystem.h"

#include "BendingSettings.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Interaction/ElementalReaction.h"
#include "Interaction/ElementalReactionSet.h"
#include "Interaction/ElementalVolumeComponent.h"
#include "Interaction/Reactions/ElementalReaction_AeroDrag.h"
#include "Interaction/Reactions/ElementalReaction_HeatExchange.h"
#include "Interaction/Reactions/ElementalReaction_Oxygenation.h"
#include "Interaction/Reactions/ElementalReaction_Saturation.h"
#include "Physics/BendingUnits.h"
#include "Stats/Stats.h"

namespace BendingInteractionCVars
{
	static TAutoConsoleVariable<bool> CVarDebugDraw(
		TEXT("Bending.Interaction.DebugDraw"),
		false,
		TEXT("Draw elemental volumes (colored by substance) and moisture patches."));
}

namespace
{
	FColor DebugColorForSubstance(BendingSim::ESubstance Substance)
	{
		switch (Substance)
		{
		case BendingSim::ESubstance::Earth: return FColor(139, 90, 43);
		case BendingSim::ESubstance::Water: return FColor(30, 120, 255);
		case BendingSim::ESubstance::Ice:   return FColor(170, 230, 255);
		case BendingSim::ESubstance::Steam: return FColor(220, 220, 220);
		case BendingSim::ESubstance::Fire:  return FColor(255, 90, 0);
		case BendingSim::ESubstance::Air:   return FColor(200, 255, 200);
		default:                            return FColor::Magenta;
		}
	}

	constexpr int32 MaxEventsPerFrame = 512;
}

// ---------------------------------------------------------------------------------------------------- Lifecycle

bool UBendingInteractionSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	if (!Super::ShouldCreateSubsystem(Outer))
	{
		return false;
	}
	const UWorld* World = Cast<UWorld>(Outer);
	return World && World->IsGameWorld();
}

void UBendingInteractionSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	SimWorld = MakeUnique<BendingSim::FSimWorld>();
	OwnerBySlot.SetNum(BendingSim::FSimWorld::MaxVolumes);
	ApplySettings();

	if (UElementalReactionSet* ConfiguredSet = UBendingSettings::Get().ReactionSet.LoadSynchronous())
	{
		SetReactionSet(ConfiguredSet);
	}
	else
	{
		RegisterBuiltInReactions();
	}
}

void UBendingInteractionSubsystem::Deinitialize()
{
	SimWorld.Reset();
	OwnerBySlot.Empty();
	Reactions.Empty();
	ReactionSet = nullptr;
	Super::Deinitialize();
}

TStatId UBendingInteractionSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UBendingInteractionSubsystem, STATGROUP_Tickables);
}

void UBendingInteractionSubsystem::ApplySettings()
{
	const UBendingSettings& Settings = UBendingSettings::Get();
	BendingSim::FSimSettings& Sim = SimWorld->Settings;
	Sim.TickRateHz = Settings.InteractionTickRateHz;
	Sim.MaxSubstepsPerFrame = Settings.MaxSubstepsPerFrame;
	Sim.ReactionEventIntervalS = Settings.ReactionEventIntervalS;
	Sim.MinVolumeMassKg = Settings.MinVolumeMassKg;
	Sim.MaxFreeVolumes = Settings.MaxFreeVolumes;
	Sim.FreeVolumeMergeRadiusCm = Settings.FreeVolumeMergeRadiusCm;
	Sim.FreeGasDampingTimeS = Settings.FreeGasDampingTimeS;
	Sim.AmbientTemperatureK = Settings.AmbientTemperatureK;
	Sim.AmbientAirDensityKgM3 = Settings.AmbientAirDensityKgM3;
	Sim.HeatTransferScale = Settings.HeatTransferScale;
	Sim.FireExtinguishTemperatureK = Settings.FireExtinguishTemperatureK;
	Sim.MaxFlameTemperatureK = Settings.MaxFlameTemperatureK;
	Sim.MaxMoisturePatches = Settings.MaxMoisturePatches;
	Sim.MoistureSoilDepthM = Settings.MoistureSoilDepthM;
	Sim.MinMoisturePatchRadiusCm = Settings.MinMoisturePatchRadiusCm;
	Sim.MaxMoisturePatchRadiusCm = Settings.MaxMoisturePatchRadiusCm;
	Sim.DryingRateKgM2S = Settings.DryingRateKgM2S;
	Sim.DryingTimeScale = Settings.DryingTimeScale;
	Sim.MudOnsetSaturation = Settings.MudOnsetSaturation;
	Sim.MudFrictionRatio = Settings.MudFrictionRatio;
	Sim.FireGroundHeatTransferCoefficient = Settings.FireGroundHeatTransferCoefficient;
	if (const UWorld* World = GetWorld())
	{
		Sim.GravityZCmS2 = World->GetGravityZ();
	}
}

void UBendingInteractionSubsystem::RegisterBuiltInReactions()
{
	auto Add = [this](UElementalReaction* Reaction, EElementalSubstance A, EElementalSubstance B)
	{
		Reaction->SubstanceA = A;
		Reaction->SubstanceB = B;
		AddReaction(Reaction);
	};

	// Same rule set as BendingSim::AddBuiltInReactions, but as editable objects.
	Add(NewObject<UElementalReaction_HeatExchange>(this), EElementalSubstance::Fire, EElementalSubstance::Water);
	Add(NewObject<UElementalReaction_HeatExchange>(this), EElementalSubstance::Fire, EElementalSubstance::Ice);
	Add(NewObject<UElementalReaction_HeatExchange>(this), EElementalSubstance::Steam, EElementalSubstance::Ice);
	Add(NewObject<UElementalReaction_HeatExchange>(this), EElementalSubstance::Fire, EElementalSubstance::Earth);
	Add(NewObject<UElementalReaction_Oxygenation>(this), EElementalSubstance::Air, EElementalSubstance::Fire);
	Add(NewObject<UElementalReaction_Saturation>(this), EElementalSubstance::Water, EElementalSubstance::Earth);
	Add(NewObject<UElementalReaction_AeroDrag>(this), EElementalSubstance::Air, EElementalSubstance::Earth);
	Add(NewObject<UElementalReaction_AeroDrag>(this), EElementalSubstance::Air, EElementalSubstance::Water);
	Add(NewObject<UElementalReaction_AeroDrag>(this), EElementalSubstance::Air, EElementalSubstance::Ice);
	Add(NewObject<UElementalReaction_AeroDrag>(this), EElementalSubstance::Air, EElementalSubstance::Steam);
}

void UBendingInteractionSubsystem::SetReactionSet(UElementalReactionSet* InReactionSet)
{
	ReactionSet = InReactionSet;
	Reactions.Reset();
	SimWorld->ClearReactions();
	if (InReactionSet)
	{
		for (UElementalReaction* Reaction : InReactionSet->Reactions)
		{
			AddReaction(Reaction);
		}
	}
}

void UBendingInteractionSubsystem::AddReaction(UElementalReaction* Reaction)
{
	if (Reaction && SimWorld->AddReaction(Reaction->MakeSimEntry()))
	{
		Reactions.Add(Reaction);
	}
}

// ---------------------------------------------------------------------------------------------------- Volumes

FElementalVolumeHandle UBendingInteractionSubsystem::RegisterVolume(const FElementalVolumeState& InitialState, UElementalVolumeComponent* Owner)
{
	const BendingSim::FHandle Handle = SimWorld->AddVolume(InitialState.ToSim(), Owner != nullptr);
	if (Handle.IsSet())
	{
		OwnerBySlot[Handle.Index] = Owner;
	}
	return FElementalVolumeHandle::FromSim(Handle);
}

void UBendingInteractionSubsystem::UnregisterVolume(FElementalVolumeHandle Handle)
{
	const BendingSim::FHandle SimHandle = Handle.ToSim();
	if (SimWorld && SimWorld->IsValid(SimHandle))
	{
		OwnerBySlot[SimHandle.Index] = nullptr;
		SimWorld->RemoveVolume(SimHandle);
	}
}

bool UBendingInteractionSubsystem::IsValidVolume(FElementalVolumeHandle Handle) const
{
	return SimWorld && SimWorld->IsValid(Handle.ToSim());
}

bool UBendingInteractionSubsystem::IsVolumeDepleted(FElementalVolumeHandle Handle) const
{
	return SimWorld && SimWorld->IsDepleted(Handle.ToSim());
}

BendingSim::FVolume* UBendingInteractionSubsystem::GetVolume(FElementalVolumeHandle Handle)
{
	return SimWorld ? SimWorld->GetVolume(Handle.ToSim()) : nullptr;
}

const BendingSim::FVolume* UBendingInteractionSubsystem::GetVolume(FElementalVolumeHandle Handle) const
{
	return SimWorld ? SimWorld->GetVolume(Handle.ToSim()) : nullptr;
}

UElementalVolumeComponent* UBendingInteractionSubsystem::GetVolumeOwner(FElementalVolumeHandle Handle) const
{
	return IsValidVolume(Handle) ? OwnerBySlot[Handle.GetIndex()].Get() : nullptr;
}

bool UBendingInteractionSubsystem::GetVolumeState(FElementalVolumeHandle Handle, FElementalVolumeState& OutState) const
{
	if (const BendingSim::FVolume* Volume = GetVolume(Handle))
	{
		OutState = FElementalVolumeState::FromSim(*Volume);
		return true;
	}
	return false;
}

int32 UBendingInteractionSubsystem::GetNumVolumes() const
{
	return SimWorld ? SimWorld->GetNumVolumes() : 0;
}

void UBendingInteractionSubsystem::QueryVolumes(const FVector& CenterCm, double RadiusCm, uint32 SubstanceMask, TArray<FElementalVolumeHandle>& OutHandles) const
{
	BendingSim::FHandle Found[256];
	const int32 Count = SimWorld->QueryVolumes(BendingUnits::ToSim(CenterCm), RadiusCm, SubstanceMask, Found, UE_ARRAY_COUNT(Found));
	for (int32 Index = 0; Index < Count; ++Index)
	{
		OutHandles.Add(FElementalVolumeHandle::FromSim(Found[Index]));
	}
}

FElementalPhaseChange UBendingInteractionSubsystem::TransferHeat(FElementalVolumeHandle Handle, double HeatJ, double& OutAcceptedHeatJ)
{
	OutAcceptedHeatJ = 0.0;
	return FElementalPhaseChange::FromSim(SimWorld->TransferHeat(Handle.ToSim(), HeatJ, &OutAcceptedHeatJ));
}

void UBendingInteractionSubsystem::AddImpulse(FElementalVolumeHandle Handle, FVector ImpulseKgCmS)
{
	SimWorld->AddImpulse(Handle.ToSim(), BendingUnits::ToSim(ImpulseKgCmS));
}

// ---------------------------------------------------------------------------------------------------- Surfaces

void UBendingInteractionSubsystem::DepositWaterOnSurface(FVector LocationCm, double WaterMassKg, float SoilPorosity)
{
	SimWorld->DepositWaterOnSurface(BendingUnits::ToSim(LocationCm), WaterMassKg, SoilPorosity);
}

float UBendingInteractionSubsystem::GetSurfaceSaturationAt(FVector LocationCm) const
{
	return SimWorld ? static_cast<float>(SimWorld->GetSurfaceSaturationAt(BendingUnits::ToSim(LocationCm))) : 0.f;
}

float UBendingInteractionSubsystem::GetSurfaceTractionMultiplierAt(FVector LocationCm) const
{
	return SimWorld ? static_cast<float>(SimWorld->GetSurfaceTractionMultiplierAt(BendingUnits::ToSim(LocationCm))) : 1.f;
}

TArray<FSurfaceMoisturePatch> UBendingInteractionSubsystem::GetMoisturePatches() const
{
	TArray<FSurfaceMoisturePatch> Patches;
	for (int32 Index = 0; SimWorld && Index < SimWorld->GetNumMoisturePatches(); ++Index)
	{
		const BendingSim::FMoisturePatch& Patch = SimWorld->GetMoisturePatch(Index);
		FSurfaceMoisturePatch& Out = Patches.AddDefaulted_GetRef();
		Out.CenterCm = BendingUnits::ToEngine(Patch.CenterCm);
		Out.RadiusCm = static_cast<float>(Patch.RadiusCm);
		Out.WaterKg = Patch.WaterKg;
		Out.Porosity = static_cast<float>(Patch.Porosity);
		Out.Saturation = static_cast<float>(Patch.GetSaturation(SimWorld->Settings.MoistureSoilDepthM));
	}
	return Patches;
}

// ---------------------------------------------------------------------------------------------------- Frame

void UBendingInteractionSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (!SimWorld)
	{
		return;
	}

	ApplySettings();
	SyncFromOwners(DeltaTime);
	SimWorld->Advance(DeltaTime);
	SyncToOwners();
	BroadcastEvents(DeltaTime);

	if (BendingInteractionCVars::CVarDebugDraw.GetValueOnGameThread())
	{
		DrawDebug();
	}
}

void UBendingInteractionSubsystem::SyncFromOwners(double FrameSeconds)
{
	for (int32 Index = 0; Index < SimWorld->GetSlotLimit(); ++Index)
	{
		const BendingSim::FHandle Handle = SimWorld->GetHandleAt(Index);
		if (!Handle.IsSet() || !SimWorld->IsOwned(Handle) || SimWorld->IsDepleted(Handle))
		{
			continue;
		}
		if (UElementalVolumeComponent* Owner = OwnerBySlot[Index].Get())
		{
			Owner->PreSimulationSync(*SimWorld->GetVolume(Handle), FrameSeconds);
		}
	}
}

void UBendingInteractionSubsystem::SyncToOwners()
{
	struct FPendingOwnerUpdate
	{
		TWeakObjectPtr<UElementalVolumeComponent> Owner;
		BendingSim::FVolume Volume;
		BendingSim::FOwnerUpdate Update;
	};
	TArray<FPendingOwnerUpdate, TInlineAllocator<32>> Pending;

	for (int32 Index = 0; Index < SimWorld->GetSlotLimit(); ++Index)
	{
		const BendingSim::FHandle Handle = SimWorld->GetHandleAt(Index);
		if (!Handle.IsSet() || !SimWorld->IsOwned(Handle))
		{
			continue;
		}
		if (!OwnerBySlot[Index].IsValid())
		{
			// Owner destroyed without unregistering.
			SimWorld->RemoveVolume(Handle);
			continue;
		}
		FPendingOwnerUpdate& Entry = Pending.AddDefaulted_GetRef();
		Entry.Owner = OwnerBySlot[Index];
		Entry.Volume = *SimWorld->GetVolume(Handle);
		SimWorld->ConsumeOwnerUpdate(Handle, Entry.Update);
	}

	// Outside the loop: owners may unregister (and so change the slots) from these callbacks.
	for (const FPendingOwnerUpdate& Entry : Pending)
	{
		if (UElementalVolumeComponent* Owner = Entry.Owner.Get())
		{
			Owner->PostSimulationSync(Entry.Volume, BendingUnits::ToEngine(Entry.Update.FrameImpulseKgCmS),
				ElementalSubstance::FromSim(Entry.Update.PreviousSubstance), Entry.Update.bNewlyDepleted);
		}
	}
}

void UBendingInteractionSubsystem::BroadcastEvents(double FrameSeconds)
{
	BendingSim::FFreeVolumeNotice Notices[BendingSim::FSimWorld::MaxNotices];
	const int32 NumNotices = SimWorld->ConsumeFreeVolumeNotices(Notices, UE_ARRAY_COUNT(Notices));

	TArray<BendingSim::FReactionEvent> Events;
	Events.SetNum(MaxEventsPerFrame);
	Events.SetNum(SimWorld->FlushEvents(FrameSeconds, Events.GetData(), MaxEventsPerFrame));

	// Everything is copied out before broadcasting: listeners may register volumes or apply heat.
	for (int32 Index = 0; Index < NumNotices; ++Index)
	{
		const FElementalVolumeHandle Handle = FElementalVolumeHandle::FromSim(Notices[Index].Handle);
		const EElementalSubstance Substance = ElementalSubstance::FromSim(Notices[Index].Substance);
		if (Notices[Index].bSpawned)
		{
			OnFreeVolumeSpawned.Broadcast(Handle, Substance);
		}
		else
		{
			OnFreeVolumeRemoved.Broadcast(Handle, Substance);
		}
	}
	for (const BendingSim::FReactionEvent& SimEvent : Events)
	{
		const FElementalReactionEvent Event = FElementalReactionEvent::FromSim(SimEvent);
		OnReactionNative.Broadcast(Event);
		OnReaction.Broadcast(Event);
	}
}

// ---------------------------------------------------------------------------------------------------- Debug

void UBendingInteractionSubsystem::DrawDebug() const
{
#if ENABLE_DRAW_DEBUG
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	for (int32 Index = 0; Index < SimWorld->GetSlotLimit(); ++Index)
	{
		const BendingSim::FHandle Handle = SimWorld->GetHandleAt(Index);
		const BendingSim::FVolume* Volume = SimWorld->GetVolume(Handle);
		if (!Volume)
		{
			continue;
		}
		const FColor Color = SimWorld->IsDepleted(Handle) ? FColor::Black : DebugColorForSubstance(Volume->Substance);
		const FVector Location = BendingUnits::ToEngine(Volume->LocationCm);
		const float Radius = static_cast<float>(Volume->RadiusCm);
		if (Volume->Shape == BendingSim::EShape::Capsule && !Volume->CapsuleHalfAxisCm.IsNearlyZero())
		{
			const FVector HalfAxis = BendingUnits::ToEngine(Volume->CapsuleHalfAxisCm);
			DrawDebugCapsule(World, Location, static_cast<float>(HalfAxis.Size() + Volume->RadiusCm), Radius,
				FRotationMatrix::MakeFromZ(HalfAxis).ToQuat(), Color);
		}
		else
		{
			DrawDebugSphere(World, Location, Radius, 16, Color);
		}
	}
	for (int32 Index = 0; Index < SimWorld->GetNumMoisturePatches(); ++Index)
	{
		const BendingSim::FMoisturePatch& Patch = SimWorld->GetMoisturePatch(Index);
		DrawDebugCircle(World, BendingUnits::ToEngine(Patch.CenterCm), static_cast<float>(Patch.RadiusCm), 32, FColor(90, 60, 30), false, -1.f, 0, 2.f,
			FVector(1.0, 0.0, 0.0), FVector(0.0, 1.0, 0.0), false);
	}
#endif
}
