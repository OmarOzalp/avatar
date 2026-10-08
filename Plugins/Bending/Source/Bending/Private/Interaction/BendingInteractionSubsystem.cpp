#include "Interaction/BendingInteractionSubsystem.h"

#include "BendingLog.h"
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
#include "Physics/ElementalPhysics.h"
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
	/** Conserves mass, momentum and heat when a spawned free volume joins an existing one of the same substance. */
	void MergeFreeVolume(FElementalVolumeState& Target, const FElementalVolumeState& Source)
	{
		const double TotalMass = Target.MassKg + Source.MassKg;
		if (TotalMass <= UE_SMALL_NUMBER)
		{
			return;
		}
		const double TargetCapacity = Target.GetHeatCapacityJPerK();
		const double SourceCapacity = Source.GetHeatCapacityJPerK();
		if (TargetCapacity + SourceCapacity > UE_SMALL_NUMBER)
		{
			Target.TemperatureK = (TargetCapacity * Target.TemperatureK + SourceCapacity * Source.TemperatureK) / (TargetCapacity + SourceCapacity);
		}
		Target.VelocityCmS = (Target.MassKg * Target.VelocityCmS + Source.MassKg * Source.VelocityCmS) / TotalMass;
		Target.LocationCm = (Target.MassKg * Target.LocationCm + Source.MassKg * Source.LocationCm) / TotalMass;
		Target.MassKg = TotalMass;
		Target.UpdateDerivedRadius();
	}

	FColor DebugColorForSubstance(EElementalSubstance Substance)
	{
		switch (Substance)
		{
		case EElementalSubstance::Earth: return FColor(139, 90, 43);
		case EElementalSubstance::Water: return FColor(30, 120, 255);
		case EElementalSubstance::Ice:   return FColor(170, 230, 255);
		case EElementalSubstance::Steam: return FColor(220, 220, 220);
		case EElementalSubstance::Fire:  return FColor(255, 90, 0);
		case EElementalSubstance::Air:   return FColor(200, 255, 200);
		default:                         return FColor::Magenta;
		}
	}
}

// ---------------------------------------------------------------------------------------------------- Moisture patch

double FSurfaceMoisturePatch::GetCapacityKg(double SoilDepthM) const
{
	const double RadiusM = BendingUnits::CmToM(RadiusCm);
	return BendingPhysics::WaterDensity * Porosity * SoilDepthM * UE_DOUBLE_PI * RadiusM * RadiusM;
}

double FSurfaceMoisturePatch::GetSaturation(double SoilDepthM) const
{
	const double Capacity = GetCapacityKg(SoilDepthM);
	return Capacity > UE_SMALL_NUMBER ? FMath::Clamp(WaterKg / Capacity, 0.0, 1.0) : 0.0;
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
	Volumes.Empty();
	ReactionTable.Empty();
	Reactions.Empty();
	PendingFreeVolumes.Empty();
	PendingEvents.Empty();
	DiscreteEvents.Empty();
	MoisturePatches.Empty();

	Super::Deinitialize();
}

TStatId UBendingInteractionSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UBendingInteractionSubsystem, STATGROUP_Tickables);
}

void UBendingInteractionSubsystem::RegisterBuiltInReactions()
{
	auto Add = [this](UElementalReaction* Reaction, EElementalSubstance A, EElementalSubstance B)
	{
		Reaction->SubstanceA = A;
		Reaction->SubstanceB = B;
		AddReaction(Reaction);
	};

	// Evaporation, melting, and flame against rock.
	Add(NewObject<UElementalReaction_HeatExchange>(this), EElementalSubstance::Fire, EElementalSubstance::Water);
	Add(NewObject<UElementalReaction_HeatExchange>(this), EElementalSubstance::Fire, EElementalSubstance::Ice);
	Add(NewObject<UElementalReaction_HeatExchange>(this), EElementalSubstance::Steam, EElementalSubstance::Ice);
	Add(NewObject<UElementalReaction_HeatExchange>(this), EElementalSubstance::Fire, EElementalSubstance::Earth);
	// Oxygenation.
	Add(NewObject<UElementalReaction_Oxygenation>(this), EElementalSubstance::Air, EElementalSubstance::Fire);
	// Mud.
	Add(NewObject<UElementalReaction_Saturation>(this), EElementalSubstance::Water, EElementalSubstance::Earth);
	// Deflection and erosion; wind also pushes water, ice and steam clouds.
	Add(NewObject<UElementalReaction_AeroDrag>(this), EElementalSubstance::Air, EElementalSubstance::Earth);
	Add(NewObject<UElementalReaction_AeroDrag>(this), EElementalSubstance::Air, EElementalSubstance::Water);
	Add(NewObject<UElementalReaction_AeroDrag>(this), EElementalSubstance::Air, EElementalSubstance::Ice);
	Add(NewObject<UElementalReaction_AeroDrag>(this), EElementalSubstance::Air, EElementalSubstance::Steam);
}

void UBendingInteractionSubsystem::SetReactionSet(UElementalReactionSet* InReactionSet)
{
	ReactionSet = InReactionSet;
	Reactions.Reset();
	ReactionTable.Reset();
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
	if (!Reaction || Reaction->SubstanceA == EElementalSubstance::None || Reaction->SubstanceB == EElementalSubstance::None)
	{
		return;
	}
	Reactions.Add(Reaction);
	ReactionTable.FindOrAdd(ElementalSubstance::MakePairKey(Reaction->SubstanceA, Reaction->SubstanceB)).Add(Reaction);
}

// ---------------------------------------------------------------------------------------------------- Volumes

FElementalVolumeHandle UBendingInteractionSubsystem::RegisterVolume(const FElementalVolumeState& InitialState, UElementalVolumeComponent* Owner)
{
	FVolumeSlot Slot;
	Slot.State = InitialState;
	Slot.State.PendingImpulseKgCmS = FVector::ZeroVector;
	Slot.State.UpdateDerivedRadius();
	Slot.Owner = Owner;
	Slot.bOwned = Owner != nullptr;
	Slot.Serial = NextSerial++;
	Slot.SubstanceAtFrameStart = InitialState.Substance;
	if (NextSerial == 0)
	{
		NextSerial = 1;
	}

	const uint32 Serial = Slot.Serial;
	const bool bOwned = Slot.bOwned;
	const int32 Index = Volumes.Add(MoveTemp(Slot));
	if (!bOwned)
	{
		++NumFreeVolumes;
	}
	return FElementalVolumeHandle(Index, Serial);
}

void UBendingInteractionSubsystem::UnregisterVolume(FElementalVolumeHandle Handle)
{
	if (const FVolumeSlot* Slot = FindSlot(Handle))
	{
		if (!Slot->bOwned)
		{
			--NumFreeVolumes;
		}
		Volumes.RemoveAt(Handle.GetIndex());
	}
}

UBendingInteractionSubsystem::FVolumeSlot* UBendingInteractionSubsystem::FindSlot(FElementalVolumeHandle Handle)
{
	if (!Handle.IsSet() || !Volumes.IsValidIndex(Handle.GetIndex()))
	{
		return nullptr;
	}
	FVolumeSlot& Slot = Volumes[Handle.GetIndex()];
	return Slot.Serial == Handle.GetSerial() ? &Slot : nullptr;
}

const UBendingInteractionSubsystem::FVolumeSlot* UBendingInteractionSubsystem::FindSlot(FElementalVolumeHandle Handle) const
{
	return const_cast<UBendingInteractionSubsystem*>(this)->FindSlot(Handle);
}

FElementalVolumeHandle UBendingInteractionSubsystem::MakeHandle(int32 Index) const
{
	return Volumes.IsValidIndex(Index) ? FElementalVolumeHandle(Index, Volumes[Index].Serial) : FElementalVolumeHandle();
}

bool UBendingInteractionSubsystem::IsValidVolume(FElementalVolumeHandle Handle) const
{
	return FindSlot(Handle) != nullptr;
}

FElementalVolumeState* UBendingInteractionSubsystem::GetVolume(FElementalVolumeHandle Handle)
{
	FVolumeSlot* Slot = FindSlot(Handle);
	return Slot ? &Slot->State : nullptr;
}

const FElementalVolumeState* UBendingInteractionSubsystem::GetVolume(FElementalVolumeHandle Handle) const
{
	const FVolumeSlot* Slot = FindSlot(Handle);
	return Slot ? &Slot->State : nullptr;
}

UElementalVolumeComponent* UBendingInteractionSubsystem::GetVolumeOwner(FElementalVolumeHandle Handle) const
{
	const FVolumeSlot* Slot = FindSlot(Handle);
	return Slot ? Slot->Owner.Get() : nullptr;
}

bool UBendingInteractionSubsystem::GetVolumeState(FElementalVolumeHandle Handle, FElementalVolumeState& OutState) const
{
	if (const FElementalVolumeState* State = GetVolume(Handle))
	{
		OutState = *State;
		return true;
	}
	return false;
}

void UBendingInteractionSubsystem::QueryVolumes(const FVector& CenterCm, double RadiusCm, uint32 SubstanceMask, TArray<FElementalVolumeHandle>& OutHandles) const
{
	for (auto It = Volumes.CreateConstIterator(); It; ++It)
	{
		const FVolumeSlot& Slot = *It;
		if (Slot.bDepleted || (SubstanceMask & ElementalSubstance::ToMask(Slot.State.Substance)) == 0)
		{
			continue;
		}
		const FVector Closest = FMath::ClosestPointOnSegment(CenterCm, Slot.State.GetSegmentStartCm(), Slot.State.GetSegmentEndCm());
		if (FVector::DistSquared(Closest, CenterCm) <= FMath::Square(RadiusCm + Slot.State.RadiusCm))
		{
			OutHandles.Add(MakeHandle(It.GetIndex()));
		}
	}
}

FElementalPhaseChange UBendingInteractionSubsystem::TransferHeat(FElementalVolumeHandle Handle, double HeatJ)
{
	FVolumeSlot* Slot = FindSlot(Handle);
	if (!Slot || Slot->bDepleted)
	{
		return FElementalPhaseChange();
	}
	const FElementalPhaseChange Change = ElementalPhysics::AddHeat(Slot->State, HeatJ);
	RecordPhaseChange(Handle.GetIndex(), Change);
	return Change;
}

void UBendingInteractionSubsystem::AddImpulse(FElementalVolumeHandle Handle, FVector ImpulseKgCmS)
{
	if (FVolumeSlot* Slot = FindSlot(Handle))
	{
		Slot->State.PendingImpulseKgCmS += ImpulseKgCmS;
	}
}

// ---------------------------------------------------------------------------------------------------- Frame

void UBendingInteractionSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (Volumes.Num() == 0 && MoisturePatches.Num() == 0 && PendingFreeVolumes.Num() == 0)
	{
		StepAccumulator = 0.0;
		FlushEvents(DeltaTime);
		return;
	}

	const UBendingSettings& Settings = UBendingSettings::Get();
	const double FixedStep = 1.0 / FMath::Max(static_cast<double>(Settings.InteractionTickRateHz), 1.0);

	SyncFromOwners(DeltaTime);

	StepAccumulator += DeltaTime;
	int32 Steps = 0;
	while (StepAccumulator >= FixedStep && Steps < Settings.MaxSubstepsPerFrame)
	{
		StepSimulation(FixedStep);
		StepAccumulator -= FixedStep;
		++Steps;
	}
	if (Steps >= Settings.MaxSubstepsPerFrame)
	{
		// Hitch: drop the backlog rather than spiral.
		StepAccumulator = FMath::Min(StepAccumulator, FixedStep);
	}

	SyncToOwners();
	FlushEvents(DeltaTime);

	if (BendingInteractionCVars::CVarDebugDraw.GetValueOnGameThread())
	{
		DrawDebug();
	}
}

void UBendingInteractionSubsystem::StepSimulation(double DeltaSeconds)
{
	UpdateDerivedShapes();
	FindAndResolveContacts(DeltaSeconds);
	ExchangeHeatWithAmbient(DeltaSeconds);
	UpdateMoisturePatches(DeltaSeconds);
	IntegrateImpulses();
	IntegrateFreeVolumes(DeltaSeconds);
	ProcessFreeVolumeSpawns();
	ResolveDepletion();
}

void UBendingInteractionSubsystem::SyncFromOwners(double FrameSeconds)
{
	for (FVolumeSlot& Slot : Volumes)
	{
		Slot.SubstanceAtFrameStart = Slot.State.Substance;
		if (Slot.bOwned && !Slot.bDepleted)
		{
			if (UElementalVolumeComponent* Owner = Slot.Owner.Get())
			{
				Owner->PreSimulationSync(Slot.State, FrameSeconds);
			}
		}
	}
}

void UBendingInteractionSubsystem::SyncToOwners()
{
	struct FOwnerUpdate
	{
		TWeakObjectPtr<UElementalVolumeComponent> Owner;
		FElementalVolumeState State;
		FVector ImpulseKgCmS = FVector::ZeroVector;
		EElementalSubstance PreviousSubstance = EElementalSubstance::None;
		bool bNewlyDepleted = false;
	};

	TArray<FOwnerUpdate, TInlineAllocator<32>> Updates;
	TArray<int32, TInlineAllocator<8>> Orphans;

	for (auto It = Volumes.CreateIterator(); It; ++It)
	{
		FVolumeSlot& Slot = *It;
		if (!Slot.bOwned)
		{
			continue;
		}
		if (!Slot.Owner.IsValid())
		{
			// Owner destroyed without unregistering.
			Orphans.Add(It.GetIndex());
			continue;
		}

		FOwnerUpdate& Update = Updates.AddDefaulted_GetRef();
		Update.Owner = Slot.Owner;
		Update.State = Slot.State;
		Update.ImpulseKgCmS = Slot.FrameImpulseKgCmS;
		Update.PreviousSubstance = Slot.SubstanceAtFrameStart;
		Update.bNewlyDepleted = Slot.bDepleted && !Slot.bDepletionReported;

		Slot.FrameImpulseKgCmS = FVector::ZeroVector;
		Slot.SubstanceAtFrameStart = Slot.State.Substance;
		Slot.bDepletionReported = Slot.bDepletionReported || Slot.bDepleted;
	}

	for (const int32 Index : Orphans)
	{
		Volumes.RemoveAt(Index);
	}

	// Outside the iteration: owners may unregister (and so mutate Volumes) from these callbacks.
	for (const FOwnerUpdate& Update : Updates)
	{
		if (UElementalVolumeComponent* Owner = Update.Owner.Get())
		{
			Owner->PostSimulationSync(Update.State, Update.ImpulseKgCmS, Update.PreviousSubstance, Update.bNewlyDepleted);
		}
	}
}

// ---------------------------------------------------------------------------------------------------- Step stages

void UBendingInteractionSubsystem::UpdateDerivedShapes()
{
	for (FVolumeSlot& Slot : Volumes)
	{
		if (!Slot.bDepleted)
		{
			Slot.State.UpdateDerivedRadius();
		}
	}
}

void UBendingInteractionSubsystem::FindAndResolveContacts(double DeltaSeconds)
{
	if (ReactionTable.Num() == 0)
	{
		return;
	}

	// Sweep along the axis with the largest spread of centers: fewest false overlaps.
	FVector Sum = FVector::ZeroVector;
	FVector SumSquares = FVector::ZeroVector;
	int32 Count = 0;
	for (const FVolumeSlot& Slot : Volumes)
	{
		if (!Slot.bDepleted)
		{
			Sum += Slot.State.LocationCm;
			SumSquares += Slot.State.LocationCm * Slot.State.LocationCm;
			++Count;
		}
	}
	if (Count < 2)
	{
		return;
	}
	const FVector Mean = Sum / Count;
	const FVector Variance = SumSquares / Count - Mean * Mean;
	const int32 Axis = Variance.X >= Variance.Y ? (Variance.X >= Variance.Z ? 0 : 2) : (Variance.Y >= Variance.Z ? 1 : 2);

	BroadphaseScratch.Reset();
	for (auto It = Volumes.CreateConstIterator(); It; ++It)
	{
		const FVolumeSlot& Slot = *It;
		if (Slot.bDepleted)
		{
			continue;
		}
		const double Center = Slot.State.LocationCm[Axis];
		const double Extent = Slot.State.GetBoundingRadiusCm();
		BroadphaseScratch.Add({ It.GetIndex(), Center - Extent, Center + Extent });
	}
	BroadphaseScratch.Sort([](const FBroadphaseEntry& L, const FBroadphaseEntry& R) { return L.Min < R.Min; });

	for (int32 I = 0; I < BroadphaseScratch.Num(); ++I)
	{
		const FBroadphaseEntry& EntryA = BroadphaseScratch[I];
		for (int32 J = I + 1; J < BroadphaseScratch.Num() && BroadphaseScratch[J].Min <= EntryA.Max; ++J)
		{
			const FVolumeSlot& SlotA = Volumes[EntryA.Index];
			const FVolumeSlot& SlotB = Volumes[BroadphaseScratch[J].Index];
			if (SlotA.State.MassKg <= 0.0 || SlotB.State.MassKg <= 0.0)
			{
				continue;
			}
			if (!ReactionTable.Contains(ElementalSubstance::MakePairKey(SlotA.State.Substance, SlotB.State.Substance)))
			{
				continue;
			}
			FElementalContact Contact;
			if (ComputeContact(SlotA.State, SlotB.State, Contact))
			{
				DispatchReactions(EntryA.Index, BroadphaseScratch[J].Index, Contact, DeltaSeconds);
			}
		}
	}
}

bool UBendingInteractionSubsystem::ComputeContact(const FElementalVolumeState& A, const FElementalVolumeState& B, FElementalContact& OutContact) const
{
	const double RadiusA = A.RadiusCm;
	const double RadiusB = B.RadiusCm;
	if (RadiusA <= 0.0 || RadiusB <= 0.0)
	{
		return false;
	}

	// Closest points between the core segments; spheres are degenerate segments.
	const bool bCapsuleA = A.Shape == EElementalVolumeShape::Capsule;
	const bool bCapsuleB = B.Shape == EElementalVolumeShape::Capsule;
	FVector PointA = A.LocationCm;
	FVector PointB = B.LocationCm;
	if (bCapsuleA && bCapsuleB)
	{
		FMath::SegmentDistToSegmentSafe(A.GetSegmentStartCm(), A.GetSegmentEndCm(), B.GetSegmentStartCm(), B.GetSegmentEndCm(), PointA, PointB);
	}
	else if (bCapsuleA)
	{
		PointA = FMath::ClosestPointOnSegment(PointB, A.GetSegmentStartCm(), A.GetSegmentEndCm());
	}
	else if (bCapsuleB)
	{
		PointB = FMath::ClosestPointOnSegment(PointA, B.GetSegmentStartCm(), B.GetSegmentEndCm());
	}

	const FVector Delta = PointB - PointA;
	const double Distance = Delta.Size();
	const double Penetration = RadiusA + RadiusB - Distance;
	if (Penetration <= 0.0)
	{
		return false;
	}

	OutContact.NormalAB = Distance > UE_KINDA_SMALL_NUMBER ? Delta / Distance : FVector::UpVector;
	OutContact.PenetrationCm = Penetration;
	OutContact.PointCm = PointA + OutContact.NormalAB * (RadiusA - 0.5 * Penetration);

	const double RadiusAM = BendingUnits::CmToM(RadiusA);
	const double RadiusBM = BendingUnits::CmToM(RadiusB);
	const double DistanceM = BendingUnits::CmToM(Distance);
	OutContact.ExchangeAreaM2 = BendingKernel::ContactExchangeArea(RadiusAM, RadiusBM, DistanceM);
	OutContact.ImmersionA = BendingKernel::ImmersionFraction(RadiusAM, RadiusBM, DistanceM);
	OutContact.ImmersionB = BendingKernel::ImmersionFraction(RadiusBM, RadiusAM, DistanceM);
	OutContact.OverlapFraction = FMath::Clamp(Penetration / (2.0 * FMath::Min(RadiusA, RadiusB)), 0.0, 1.0);
	return true;
}

void UBendingInteractionSubsystem::DispatchReactions(int32 IndexA, int32 IndexB, const FElementalContact& Contact, double DeltaSeconds)
{
	FVolumeSlot& SlotA = Volumes[IndexA];
	FVolumeSlot& SlotB = Volumes[IndexB];
	const TArray<UElementalReaction*, TInlineAllocator<2>>* Handlers =
		ReactionTable.Find(ElementalSubstance::MakePairKey(SlotA.State.Substance, SlotB.State.Substance));
	if (!Handlers)
	{
		return;
	}

	// Copy: a reaction can change a substance (ice melting), which changes the table lookup mid-loop.
	const TArray<UElementalReaction*, TInlineAllocator<2>> PairReactions = *Handlers;
	const UBendingSettings& Settings = UBendingSettings::Get();
	const FElementalVolumeHandle HandleA = MakeHandle(IndexA);
	const FElementalVolumeHandle HandleB = MakeHandle(IndexB);

	for (const UElementalReaction* Reaction : PairReactions)
	{
		if (!Reaction || SlotA.State.MassKg <= 0.0 || SlotB.State.MassKg <= 0.0)
		{
			continue;
		}
		const EElementalSubstance SubstanceA = SlotA.State.Substance;
		const EElementalSubstance SubstanceB = SlotB.State.Substance;
		if (SubstanceA == Reaction->SubstanceA && SubstanceB == Reaction->SubstanceB)
		{
			FElementalReactionContext Context(*this, Settings, HandleA, HandleB, SlotA.State, SlotB.State, Contact, DeltaSeconds);
			Reaction->React(Context);
		}
		else if (SubstanceA == Reaction->SubstanceB && SubstanceB == Reaction->SubstanceA)
		{
			const FElementalContact Flipped = Contact.Flipped();
			FElementalReactionContext Context(*this, Settings, HandleB, HandleA, SlotB.State, SlotA.State, Flipped, DeltaSeconds);
			Reaction->React(Context);
		}
	}
}

void UBendingInteractionSubsystem::ExchangeHeatWithAmbient(double DeltaSeconds)
{
	const double AmbientK = UBendingSettings::Get().AmbientTemperatureK;
	for (auto It = Volumes.CreateIterator(); It; ++It)
	{
		FVolumeSlot& Slot = *It;
		if (Slot.bDepleted || Slot.State.MassKg <= 0.0)
		{
			continue;
		}
		const FElementalPhaseChange Change = ElementalPhysics::ExchangeWithAmbient(Slot.State, AmbientK, DeltaSeconds);
		RecordPhaseChange(It.GetIndex(), Change);
	}
}

void UBendingInteractionSubsystem::UpdateMoisturePatches(double DeltaSeconds)
{
	if (MoisturePatches.Num() == 0)
	{
		return;
	}

	using namespace BendingKernel::Constants;
	const UBendingSettings& Settings = UBendingSettings::Get();
	const double AmbientK = Settings.AmbientTemperatureK;
	const double HeatPerKgEvaporated = SpecificHeatWater * FMath::Max(WaterBoilingPointK - AmbientK, 0.0) + LatentHeatVaporization;

	// Natural drying.
	for (FSurfaceMoisturePatch& Patch : MoisturePatches)
	{
		const double RadiusM = BendingUnits::CmToM(Patch.RadiusCm);
		Patch.WaterKg -= Settings.DryingRateKgM2S * Settings.DryingTimeScale * UE_DOUBLE_PI * RadiusM * RadiusM * DeltaSeconds;
	}

	// Flames resting on wet ground boil it dry, and pay for it in heat.
	for (auto It = Volumes.CreateIterator(); It; ++It)
	{
		FVolumeSlot& Slot = *It;
		FElementalVolumeState& Fire = Slot.State;
		if (Slot.bDepleted || Fire.Substance != EElementalSubstance::Fire || Fire.TemperatureK <= WaterBoilingPointK)
		{
			continue;
		}
		for (FSurfaceMoisturePatch& Patch : MoisturePatches)
		{
			if (Patch.WaterKg <= 0.0
				|| FVector::Dist2D(Fire.LocationCm, Patch.CenterCm) > Patch.RadiusCm + Fire.RadiusCm
				|| FMath::Abs(Fire.LocationCm.Z - Patch.CenterCm.Z) > Fire.RadiusCm)
			{
				continue;
			}
			const double OverlapRadiusM = BendingUnits::CmToM(FMath::Min(Patch.RadiusCm, Fire.RadiusCm));
			double HeatJ = Settings.FireGroundHeatTransferCoefficient * UE_DOUBLE_PI * OverlapRadiusM * OverlapRadiusM
				* (Fire.TemperatureK - WaterBoilingPointK) * DeltaSeconds * Settings.HeatTransferScale;
			HeatJ = FMath::Min(HeatJ, Fire.GetHeatCapacityJPerK() * (Fire.TemperatureK - WaterBoilingPointK));

			const double EvaporatedKg = FMath::Min(HeatJ / HeatPerKgEvaporated, Patch.WaterKg);
			if (EvaporatedKg <= 0.0)
			{
				continue;
			}
			Patch.WaterKg -= EvaporatedKg;
			ElementalPhysics::AddHeat(Fire, -EvaporatedKg * HeatPerKgEvaporated);

			const FVector SteamLocation(Patch.CenterCm.X, Patch.CenterCm.Y, Patch.CenterCm.Z + 20.0);
			FElementalVolumeState Steam = FElementalVolumeState::MakeDefault(EElementalSubstance::Steam, EvaporatedKg);
			Steam.LocationCm = SteamLocation;
			QueueFreeVolume(Steam);

			FReactionEventKey Key;
			Key.A = MakeHandle(It.GetIndex());
			Key.Type = EElementalReactionType::Evaporation;
			AccumulateEvent(Key, EElementalSubstance::Fire, EElementalSubstance::Water, SteamLocation, FVector::UpVector,
				EvaporatedKg, EvaporatedKg * HeatPerKgEvaporated);
		}
	}

	MoisturePatches.RemoveAll([](const FSurfaceMoisturePatch& Patch) { return Patch.WaterKg <= 0.01; });
}

void UBendingInteractionSubsystem::IntegrateImpulses()
{
	for (FVolumeSlot& Slot : Volumes)
	{
		FElementalVolumeState& State = Slot.State;
		if (State.PendingImpulseKgCmS.IsZero())
		{
			continue;
		}
		State.VelocityCmS += State.PendingImpulseKgCmS * State.GetInverseMass();
		Slot.FrameImpulseKgCmS += State.PendingImpulseKgCmS;
		State.PendingImpulseKgCmS = FVector::ZeroVector;
	}
}

void UBendingInteractionSubsystem::IntegrateFreeVolumes(double DeltaSeconds)
{
	const UBendingSettings& Settings = UBendingSettings::Get();
	const UWorld* World = GetWorld();
	const FVector GravityCmS2(0.0, 0.0, World ? World->GetGravityZ() : -980.665);
	const double Damping = FMath::Exp(-DeltaSeconds / FMath::Max(static_cast<double>(Settings.FreeGasDampingTimeS), 0.01));
	const double AmbientDensity = Settings.AmbientAirDensityKgM3;

	for (FVolumeSlot& Slot : Volumes)
	{
		if (Slot.bOwned || Slot.bDepleted)
		{
			continue;
		}
		FElementalVolumeState& State = Slot.State;
		if (State.IsGas())
		{
			// Buoyancy a = g * (rho_air / rho_gas - 1), opposite gravity; capped so very hot, light parcels stay sane.
			const double Density = State.GetDensityKgM3();
			const double BuoyancyRatio = Density > UE_SMALL_NUMBER ? FMath::Clamp(AmbientDensity / Density - 1.0, -1.0, 3.0) : 0.0;
			State.VelocityCmS -= GravityCmS2 * (BuoyancyRatio * DeltaSeconds);
			// Drag toward the still surrounding air.
			State.VelocityCmS *= Damping;
		}
		else
		{
			State.VelocityCmS += GravityCmS2 * DeltaSeconds;
		}
		State.LocationCm += State.VelocityCmS * DeltaSeconds;
	}
}

void UBendingInteractionSubsystem::QueueFreeVolume(const FElementalVolumeState& State)
{
	if (State.MassKg > 0.0)
	{
		PendingFreeVolumes.Add(State);
	}
}

void UBendingInteractionSubsystem::ProcessFreeVolumeSpawns()
{
	if (PendingFreeVolumes.Num() == 0)
	{
		return;
	}

	const UBendingSettings& Settings = UBendingSettings::Get();
	TArray<FElementalVolumeState> Requests = MoveTemp(PendingFreeVolumes);
	PendingFreeVolumes.Reset();

	for (const FElementalVolumeState& Request : Requests)
	{
		if (!Request.IsGas())
		{
			UE_LOG(LogBending, Verbose, TEXT("Ignoring free %s volume: only gases may float ownerless."), *UEnum::GetValueAsString(Request.Substance));
			continue;
		}

		// Join the nearest free volume of the same substance in range, or anywhere once at capacity.
		const bool bAtCapacity = NumFreeVolumes >= Settings.MaxFreeVolumes;
		int32 MergeIndex = INDEX_NONE;
		double BestDistanceSq = TNumericLimits<double>::Max();
		for (auto It = Volumes.CreateConstIterator(); It; ++It)
		{
			const FVolumeSlot& Slot = *It;
			if (Slot.bOwned || Slot.bDepleted || Slot.State.Substance != Request.Substance)
			{
				continue;
			}
			const double DistanceSq = FVector::DistSquared(Slot.State.LocationCm, Request.LocationCm);
			const double MergeRange = Settings.FreeVolumeMergeRadiusCm + Slot.State.RadiusCm;
			if ((bAtCapacity || DistanceSq <= MergeRange * MergeRange) && DistanceSq < BestDistanceSq)
			{
				BestDistanceSq = DistanceSq;
				MergeIndex = It.GetIndex();
			}
		}

		if (MergeIndex != INDEX_NONE)
		{
			MergeFreeVolume(Volumes[MergeIndex].State, Request);
			continue;
		}
		if (bAtCapacity)
		{
			continue;
		}

		FElementalVolumeState State = Request;
		State.Shape = EElementalVolumeShape::Sphere;
		State.bDeriveRadiusFromMass = true;
		const FElementalVolumeHandle Handle = RegisterVolume(State, nullptr);
		SpawnedFreeVolumes.Emplace(Handle, State.Substance);
	}
}

void UBendingInteractionSubsystem::ResolveDepletion()
{
	const UBendingSettings& Settings = UBendingSettings::Get();
	for (auto It = Volumes.CreateIterator(); It; ++It)
	{
		FVolumeSlot& Slot = *It;
		if (Slot.bDepleted)
		{
			continue;
		}
		const bool bTooLight = Slot.State.MassKg < Settings.MinVolumeMassKg;
		const bool bExtinguished = Slot.State.Substance == EElementalSubstance::Fire && Slot.State.TemperatureK < Settings.FireExtinguishTemperatureK;
		if (!bTooLight && !bExtinguished)
		{
			continue;
		}

		Slot.bDepleted = true;
		if (bExtinguished)
		{
			const double ResidualHeatJ = Slot.State.GetHeatCapacityJPerK() * FMath::Max(Slot.State.TemperatureK - Settings.AmbientTemperatureK, 0.0);
			AddDiscreteEvent(EElementalReactionType::Extinguished, It.GetIndex(), Slot.State.MassKg, ResidualHeatJ);
		}
		if (!Slot.bOwned)
		{
			RemovedFreeVolumes.Emplace(MakeHandle(It.GetIndex()), Slot.State.Substance);
			--NumFreeVolumes;
			It.RemoveCurrent();
		}
	}
}

// ---------------------------------------------------------------------------------------------------- Events

void UBendingInteractionSubsystem::RecordPhaseChange(int32 Index, const FElementalPhaseChange& Change)
{
	if (!Volumes.IsValidIndex(Index))
	{
		return;
	}
	const FElementalVolumeState& State = Volumes[Index].State;
	using namespace BendingKernel::Constants;

	if (Change.MeltedKg > 0.0)
	{
		AddDiscreteEvent(EElementalReactionType::Melting, Index, Change.MeltedKg, Change.MeltedKg * LatentHeatFusion);
	}
	if (Change.FrozenKg > 0.0)
	{
		AddDiscreteEvent(EElementalReactionType::Freezing, Index, Change.FrozenKg, Change.FrozenKg * LatentHeatFusion);
	}
	if (Change.VaporizedKg > 0.0)
	{
		FElementalVolumeState Steam = FElementalVolumeState::MakeDefault(EElementalSubstance::Steam, Change.VaporizedKg);
		Steam.LocationCm = State.LocationCm + FVector(0.0, 0.0, State.RadiusCm);
		Steam.VelocityCmS = State.VelocityCmS;
		QueueFreeVolume(Steam);

		FReactionEventKey Key;
		Key.A = MakeHandle(Index);
		Key.Type = EElementalReactionType::Evaporation;
		AccumulateEvent(Key, State.Substance, EElementalSubstance::Steam, Steam.LocationCm, FVector::UpVector,
			Change.VaporizedKg, Change.VaporizedKg * LatentHeatVaporization);
	}
	if (Change.CondensedKg > 0.0)
	{
		FReactionEventKey Key;
		Key.A = MakeHandle(Index);
		Key.Type = EElementalReactionType::Condensation;
		AccumulateEvent(Key, State.Substance, EElementalSubstance::Water, State.LocationCm, FVector::DownVector,
			Change.CondensedKg, Change.CondensedKg * LatentHeatVaporization);
	}
}

void UBendingInteractionSubsystem::EmitReactionEvent(const FElementalReactionContext& Context, EElementalReactionType Type, double MassKg, double EnergyJ)
{
	FReactionEventKey Key;
	Key.A = Context.HandleA;
	Key.B = Context.HandleB;
	Key.Type = Type;
	AccumulateEvent(Key, Context.A.Substance, Context.B.Substance, Context.Contact.PointCm, Context.Contact.NormalAB, MassKg, EnergyJ);
}

void UBendingInteractionSubsystem::AccumulateEvent(const FReactionEventKey& Key, EElementalSubstance SubstanceA, EElementalSubstance SubstanceB,
	const FVector& LocationCm, const FVector& Normal, double MassKg, double EnergyJ)
{
	FElementalReactionEvent& Event = PendingEvents.FindOrAdd(Key);
	if (Event.Type == EElementalReactionType::None)
	{
		Event.Type = Key.Type;
		Event.SubstanceA = SubstanceA;
		Event.SubstanceB = SubstanceB;
		Event.VolumeA = Key.A;
		Event.VolumeB = Key.B;
		Event.LocationCm = LocationCm;
	}

	// Mass-weighted location over the window; massless events (deflection) track the latest contact.
	const double Weight = FMath::Max(MassKg, 0.0);
	if (Event.MassKg + Weight > UE_SMALL_NUMBER)
	{
		Event.LocationCm = (Event.LocationCm * Event.MassKg + LocationCm * Weight) / (Event.MassKg + Weight);
	}
	else
	{
		Event.LocationCm = LocationCm;
	}
	Event.Normal = Normal;
	Event.MassKg += MassKg;
	Event.EnergyJ += EnergyJ;
}

void UBendingInteractionSubsystem::AddDiscreteEvent(EElementalReactionType Type, int32 Index, double MassKg, double EnergyJ)
{
	const FVolumeSlot& Slot = Volumes[Index];
	FElementalReactionEvent& Event = DiscreteEvents.AddDefaulted_GetRef();
	Event.Type = Type;
	Event.SubstanceA = Slot.State.Substance;
	Event.VolumeA = MakeHandle(Index);
	Event.LocationCm = Slot.State.LocationCm;
	Event.MassKg = MassKg;
	Event.EnergyJ = EnergyJ;
}

void UBendingInteractionSubsystem::FlushEvents(double FrameSeconds)
{
	// Move everything out first: listeners may register volumes or trigger new events while we broadcast.
	const TArray<TPair<FElementalVolumeHandle, EElementalSubstance>> Spawned = MoveTemp(SpawnedFreeVolumes);
	const TArray<TPair<FElementalVolumeHandle, EElementalSubstance>> Removed = MoveTemp(RemovedFreeVolumes);
	const TArray<FElementalReactionEvent> Discrete = MoveTemp(DiscreteEvents);
	SpawnedFreeVolumes.Reset();
	RemovedFreeVolumes.Reset();
	DiscreteEvents.Reset();

	for (const TPair<FElementalVolumeHandle, EElementalSubstance>& Entry : Spawned)
	{
		OnFreeVolumeSpawned.Broadcast(Entry.Key, Entry.Value);
	}
	for (const TPair<FElementalVolumeHandle, EElementalSubstance>& Entry : Removed)
	{
		OnFreeVolumeRemoved.Broadcast(Entry.Key, Entry.Value);
	}
	for (const FElementalReactionEvent& Event : Discrete)
	{
		OnReactionNative.Broadcast(Event);
		OnReaction.Broadcast(Event);
	}

	if (PendingEvents.Num() == 0)
	{
		EventWindowSeconds = 0.0;
		return;
	}
	EventWindowSeconds += FrameSeconds;
	if (EventWindowSeconds < UBendingSettings::Get().ReactionEventIntervalS)
	{
		return;
	}

	const double Window = FMath::Max(EventWindowSeconds, UE_SMALL_NUMBER);
	TMap<FReactionEventKey, FElementalReactionEvent> Aggregated = MoveTemp(PendingEvents);
	PendingEvents.Reset();
	EventWindowSeconds = 0.0;

	for (auto& Entry : Aggregated)
	{
		FElementalReactionEvent& Event = Entry.Value;
		Event.WindowSeconds = static_cast<float>(Window);
		Event.MassRateKgS = Event.MassKg / Window;
		Event.PowerW = Event.EnergyJ / Window;
		OnReactionNative.Broadcast(Event);
		OnReaction.Broadcast(Event);
	}
}

// ---------------------------------------------------------------------------------------------------- Surfaces

void UBendingInteractionSubsystem::DepositWaterOnSurface(FVector LocationCm, double WaterMassKg, float SoilPorosity)
{
	// Rock and pavement shed water instead of holding it.
	if (WaterMassKg <= 0.0 || SoilPorosity < 0.01f)
	{
		return;
	}

	const UBendingSettings& Settings = UBendingSettings::Get();
	int32 TargetIndex = INDEX_NONE;
	double BestDistanceSq = TNumericLimits<double>::Max();
	for (int32 Index = 0; Index < MoisturePatches.Num(); ++Index)
	{
		const FSurfaceMoisturePatch& Patch = MoisturePatches[Index];
		const double DistanceSq = FVector::DistSquared(Patch.CenterCm, LocationCm);
		const bool bInside = DistanceSq <= FMath::Square(static_cast<double>(Patch.RadiusCm));
		const bool bAtCapacity = MoisturePatches.Num() >= Settings.MaxMoisturePatches;
		if ((bInside || bAtCapacity) && DistanceSq < BestDistanceSq)
		{
			BestDistanceSq = DistanceSq;
			TargetIndex = Index;
		}
	}

	if (TargetIndex == INDEX_NONE)
	{
		FSurfaceMoisturePatch& NewPatch = MoisturePatches.AddDefaulted_GetRef();
		NewPatch.CenterCm = LocationCm;
		NewPatch.RadiusCm = Settings.MinMoisturePatchRadiusCm;
		NewPatch.Porosity = SoilPorosity;
		TargetIndex = MoisturePatches.Num() - 1;
	}

	FSurfaceMoisturePatch& Patch = MoisturePatches[TargetIndex];
	const double NewWaterKg = Patch.WaterKg + WaterMassKg;
	// Water-weighted center: repeated splashes drag the patch toward where the water lands.
	Patch.CenterCm = (Patch.CenterCm * Patch.WaterKg + LocationCm * WaterMassKg) / NewWaterKg;
	Patch.WaterKg = NewWaterKg;

	// Oversaturated soil spreads the water outward; beyond the maximum size the excess runs off.
	const double DepthM = Settings.MoistureSoilDepthM;
	if (Patch.WaterKg > Patch.GetCapacityKg(DepthM))
	{
		const double RequiredAreaM2 = Patch.WaterKg / (BendingPhysics::WaterDensity * Patch.Porosity * DepthM);
		const double RequiredRadiusCm = BendingUnits::MToCm(FMath::Sqrt(RequiredAreaM2 / UE_DOUBLE_PI));
		Patch.RadiusCm = static_cast<float>(FMath::Min(RequiredRadiusCm, static_cast<double>(Settings.MaxMoisturePatchRadiusCm)));
		Patch.WaterKg = FMath::Min(Patch.WaterKg, Patch.GetCapacityKg(DepthM));
	}
}

float UBendingInteractionSubsystem::GetSurfaceSaturationAt(FVector LocationCm) const
{
	const double DepthM = UBendingSettings::Get().MoistureSoilDepthM;
	double Best = 0.0;
	for (const FSurfaceMoisturePatch& Patch : MoisturePatches)
	{
		const double Distance = FVector::Dist2D(Patch.CenterCm, LocationCm);
		if (Distance > Patch.RadiusCm || FMath::Abs(Patch.CenterCm.Z - LocationCm.Z) > Patch.RadiusCm)
		{
			continue;
		}
		const double EdgeFalloff = 1.0 - FMath::SmoothStep(0.7 * Patch.RadiusCm, static_cast<double>(Patch.RadiusCm), Distance);
		Best = FMath::Max(Best, Patch.GetSaturation(DepthM) * EdgeFalloff);
	}
	return static_cast<float>(Best);
}

float UBendingInteractionSubsystem::GetSurfaceTractionMultiplierAt(FVector LocationCm) const
{
	return GetTractionMultiplierForSaturation(GetSurfaceSaturationAt(LocationCm));
}

float UBendingInteractionSubsystem::GetTractionMultiplierForSaturation(float Saturation)
{
	const UBendingSettings& Settings = UBendingSettings::Get();
	const float MudAmount = FMath::SmoothStep(Settings.MudOnsetSaturation, 1.f, Saturation);
	return FMath::Lerp(1.f, Settings.MudFrictionRatio, MudAmount);
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
	for (const FVolumeSlot& Slot : Volumes)
	{
		const FElementalVolumeState& State = Slot.State;
		const FColor Color = Slot.bDepleted ? FColor::Black : DebugColorForSubstance(State.Substance);
		if (State.Shape == EElementalVolumeShape::Capsule && !State.CapsuleHalfAxisCm.IsNearlyZero())
		{
			const FQuat Rotation = FRotationMatrix::MakeFromZ(State.CapsuleHalfAxisCm).ToQuat();
			DrawDebugCapsule(World, State.LocationCm, static_cast<float>(State.CapsuleHalfAxisCm.Size() + State.RadiusCm), State.RadiusCm, Rotation, Color);
		}
		else
		{
			DrawDebugSphere(World, State.LocationCm, State.RadiusCm, 16, Color);
		}
	}
	for (const FSurfaceMoisturePatch& Patch : MoisturePatches)
	{
		DrawDebugCircle(World, Patch.CenterCm, Patch.RadiusCm, 32, FColor(90, 60, 30), false, -1.f, 0, 2.f,
			FVector(1.0, 0.0, 0.0), FVector(0.0, 1.0, 0.0), false);
	}
#endif
}
