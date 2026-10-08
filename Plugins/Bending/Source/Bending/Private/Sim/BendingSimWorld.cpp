#include "Sim/BendingSimWorld.h"

namespace BendingSim
{
	namespace
	{
		/** Conserves mass, momentum and heat when a spawned free volume joins an existing one of the same substance. */
		void MergeSimFreeVolume(FVolume& Target, const FVolume& Source)
		{
			const double TotalMass = Target.MassKg + Source.MassKg;
			if (TotalMass <= SmallNumber)
			{
				return;
			}
			const double TargetCapacity = Target.GetHeatCapacityJPerK();
			const double SourceCapacity = Source.GetHeatCapacityJPerK();
			if (TargetCapacity + SourceCapacity > SmallNumber)
			{
				Target.TemperatureK = (TargetCapacity * Target.TemperatureK + SourceCapacity * Source.TemperatureK) / (TargetCapacity + SourceCapacity);
			}
			Target.VelocityCmS = (Target.VelocityCmS * Target.MassKg + Source.VelocityCmS * Source.MassKg) / TotalMass;
			Target.LocationCm = (Target.LocationCm * Target.MassKg + Source.LocationCm * Source.MassKg) / TotalMass;
			Target.MassKg = TotalMass;
			Target.UpdateDerivedRadius();
		}
	}

	// ---------------------------------------------------------------------------------------------------- Volumes

	void FSimWorld::Reset()
	{
		for (int Index = 0; Index < MaxVolumes; ++Index)
		{
			Slots[Index] = FSlot();
		}
		NumFreeIndices = 0;
		HighWater = 0;
		NumAlive = 0;
		NumFree = 0;
		NumOrdered = 0;
		NumPendingSpawns = 0;
		NumPendingEvents = 0;
		NumDiscreteEvents = 0;
		EventWindowSeconds = 0.0;
		NumNotices = 0;
		NumPatches = 0;
		StepAccumulator = 0.0;
		SimTimeSeconds = 0.0;
		Stats = FWorldStats();
	}

	FSimWorld::FSlot* FSimWorld::FindSlot(FHandle Handle)
	{
		if (Handle.Index < 0 || Handle.Index >= HighWater)
		{
			return nullptr;
		}
		FSlot& Slot = Slots[Handle.Index];
		return Slot.bAlive && Slot.Serial == Handle.Serial ? &Slot : nullptr;
	}

	const FSimWorld::FSlot* FSimWorld::FindSlot(FHandle Handle) const
	{
		return const_cast<FSimWorld*>(this)->FindSlot(Handle);
	}

	FHandle FSimWorld::MakeHandle(int Index) const
	{
		FHandle Handle;
		if (Index >= 0 && Index < HighWater && Slots[Index].bAlive)
		{
			Handle.Index = Index;
			Handle.Serial = Slots[Index].Serial;
		}
		return Handle;
	}

	FHandle FSimWorld::GetHandleAt(int Index) const
	{
		return MakeHandle(Index);
	}

	FHandle FSimWorld::AddVolume(const FVolume& Volume, bool bOwned)
	{
		int Index = -1;
		if (NumFreeIndices > 0)
		{
			Index = FreeIndices[--NumFreeIndices];
		}
		else if (HighWater < MaxVolumes)
		{
			Index = HighWater++;
		}
		if (Index < 0)
		{
			return FHandle();
		}

		FSlot& Slot = Slots[Index];
		Slot = FSlot();
		Slot.Volume = Volume;
		Slot.Volume.PendingImpulseKgCmS = FVec3();
		Slot.Volume.UpdateDerivedRadius();
		Slot.SubstanceAtFrameStart = Volume.Substance;
		Slot.Serial = NextSerial++;
		if (NextSerial == 0)
		{
			NextSerial = 1;
		}
		Slot.bAlive = true;
		Slot.bOwned = bOwned;
		++NumAlive;
		if (!bOwned)
		{
			++NumFree;
		}
		return MakeHandle(Index);
	}

	void FSimWorld::RemoveVolume(FHandle Handle)
	{
		FSlot* Slot = FindSlot(Handle);
		if (!Slot)
		{
			return;
		}
		if (!Slot->bOwned)
		{
			--NumFree;
		}
		--NumAlive;
		Slot->bAlive = false;
		FreeIndices[NumFreeIndices++] = Handle.Index;
	}

	bool FSimWorld::IsDepleted(FHandle Handle) const
	{
		const FSlot* Slot = FindSlot(Handle);
		return Slot && Slot->bDepleted;
	}

	bool FSimWorld::IsOwned(FHandle Handle) const
	{
		const FSlot* Slot = FindSlot(Handle);
		return Slot && Slot->bOwned;
	}

	FVolume* FSimWorld::GetVolume(FHandle Handle)
	{
		FSlot* Slot = FindSlot(Handle);
		return Slot ? &Slot->Volume : nullptr;
	}

	const FVolume* FSimWorld::GetVolume(FHandle Handle) const
	{
		const FSlot* Slot = FindSlot(Handle);
		return Slot ? &Slot->Volume : nullptr;
	}

	int FSimWorld::QueryVolumes(const FVec3& CenterCm, double RadiusCm, unsigned int Mask, FHandle* OutHandles, int MaxHandles) const
	{
		int Count = 0;
		for (int Index = 0; Index < HighWater && Count < MaxHandles; ++Index)
		{
			const FSlot& Slot = Slots[Index];
			if (!Slot.bAlive || Slot.bDepleted || (Mask & SubstanceMask(Slot.Volume.Substance)) == 0)
			{
				continue;
			}
			const FVolume& Volume = Slot.Volume;
			const FVec3 Closest = ClosestPointOnSegment(CenterCm, Volume.GetSegmentStartCm(), Volume.GetSegmentEndCm());
			const double Reach = RadiusCm + Volume.RadiusCm;
			if (DistanceSquared(Closest, CenterCm) <= Reach * Reach)
			{
				OutHandles[Count++] = MakeHandle(Index);
			}
		}
		return Count;
	}

	bool FSimWorld::ConsumeOwnerUpdate(FHandle Handle, FOwnerUpdate& OutUpdate)
	{
		FSlot* Slot = FindSlot(Handle);
		if (!Slot)
		{
			return false;
		}
		OutUpdate.FrameImpulseKgCmS = Slot->FrameImpulseKgCmS;
		OutUpdate.PreviousSubstance = Slot->SubstanceAtFrameStart;
		OutUpdate.bNewlyDepleted = Slot->bDepleted && !Slot->bDepletionReported;

		// The substance baseline resets only here, so changes made between ticks (a bender freezing water) still report.
		Slot->FrameImpulseKgCmS = FVec3();
		Slot->SubstanceAtFrameStart = Slot->Volume.Substance;
		Slot->bDepletionReported = Slot->bDepletionReported || Slot->bDepleted;
		return true;
	}

	Thermo::FPhaseChangeResult FSimWorld::TransferHeat(FHandle Handle, double HeatJ, double* OutAcceptedHeatJ)
	{
		if (OutAcceptedHeatJ)
		{
			*OutAcceptedHeatJ = 0.0;
		}
		FSlot* Slot = FindSlot(Handle);
		if (!Slot || Slot->bDepleted)
		{
			return Thermo::FPhaseChangeResult();
		}
		if (Slot->Volume.Substance == ESubstance::Fire && HeatJ > 0.0)
		{
			// Combustion has a ceiling: heat beyond the adiabatic flame temperature is not absorbed.
			HeatJ = KMin(HeatJ, Slot->Volume.GetHeatCapacityJPerK() * KMax(Settings.MaxFlameTemperatureK - Slot->Volume.TemperatureK, 0.0));
		}
		if (OutAcceptedHeatJ)
		{
			*OutAcceptedHeatJ = HeatJ;
		}
		const Thermo::FPhaseChangeResult Change = AddHeat(Slot->Volume, HeatJ);
		RecordPhaseChange(Handle.Index, Change);
		return Change;
	}

	void FSimWorld::AddImpulse(FHandle Handle, const FVec3& ImpulseKgCmS)
	{
		if (FSlot* Slot = FindSlot(Handle))
		{
			Slot->Volume.PendingImpulseKgCmS += ImpulseKgCmS;
		}
	}

	// ---------------------------------------------------------------------------------------------------- Reactions

	bool FSimWorld::AddReaction(const FReactionEntry& Entry)
	{
		if (NumReactions >= MaxReactions || !Entry.Function || Entry.SubstanceA == ESubstance::None || Entry.SubstanceB == ESubstance::None
			|| Entry.SubstanceA >= ESubstance::Count || Entry.SubstanceB >= ESubstance::Count)
		{
			return false;
		}
		Reactions[NumReactions++] = Entry;
		const int A = static_cast<int>(Entry.SubstanceA);
		const int B = static_cast<int>(Entry.SubstanceB);
		PairHasReaction[A][B] = true;
		PairHasReaction[B][A] = true;
		return true;
	}

	void FSimWorld::ClearReactions()
	{
		NumReactions = 0;
		for (auto& Row : PairHasReaction)
		{
			for (bool& bHas : Row)
			{
				bHas = false;
			}
		}
	}

	bool FSimWorld::HasReactionFor(ESubstance A, ESubstance B) const
	{
		return A < ESubstance::Count && B < ESubstance::Count && PairHasReaction[static_cast<int>(A)][static_cast<int>(B)];
	}

	// ---------------------------------------------------------------------------------------------------- Time

	int FSimWorld::Advance(double FrameSeconds)
	{
		if (FrameSeconds <= 0.0)
		{
			return 0;
		}
		const double FixedStep = 1.0 / KMax(Settings.TickRateHz, 1.0);
		StepAccumulator += FrameSeconds;
		int Steps = 0;
		while (StepAccumulator >= FixedStep && Steps < Settings.MaxSubstepsPerFrame)
		{
			Step(FixedStep);
			StepAccumulator -= FixedStep;
			++Steps;
		}
		if (Steps >= Settings.MaxSubstepsPerFrame)
		{
			// Hitch: drop the backlog rather than spiral.
			StepAccumulator = KMin(StepAccumulator, FixedStep);
		}
		return Steps;
	}

	void FSimWorld::Step(double DeltaSeconds)
	{
		++Stats.Steps;
		Stats.ContactsLastStep = 0;
		Stats.ReactionCallsLastStep = 0;

		UpdateDerivedShapes();
		FindAndResolveContacts(DeltaSeconds);
		ExchangeHeatWithAmbient(DeltaSeconds);
		UpdateMoisturePatches(DeltaSeconds);
		IntegrateImpulses();
		IntegrateFreeVolumes(DeltaSeconds);
		ProcessFreeVolumeSpawns();
		ResolveDepletion();
		SimTimeSeconds += DeltaSeconds;
	}

	void FSimWorld::UpdateDerivedShapes()
	{
		for (int Index = 0; Index < HighWater; ++Index)
		{
			FSlot& Slot = Slots[Index];
			if (Slot.bAlive && !Slot.bDepleted)
			{
				Slot.Volume.UpdateDerivedRadius();
			}
		}
	}

	void FSimWorld::FindAndResolveContacts(double DeltaSeconds)
	{
		if (NumReactions == 0)
		{
			return;
		}

		// Sweep along the axis of greatest spread; keep the current axis unless another is clearly better,
		// so the insertion sort below stays nearly sorted from step to step.
		FVec3 Sum;
		FVec3 SumSquares;
		int Count = 0;
		for (int Index = 0; Index < HighWater; ++Index)
		{
			const FSlot& Slot = Slots[Index];
			if (Slot.bAlive && !Slot.bDepleted)
			{
				Sum += Slot.Volume.LocationCm;
				SumSquares += Slot.Volume.LocationCm.Mul(Slot.Volume.LocationCm);
				++Count;
			}
		}
		if (Count < 2)
		{
			NumOrdered = 0;
			return;
		}
		const FVec3 Mean = Sum / Count;
		const FVec3 Variance = SumSquares / Count - Mean.Mul(Mean);
		int BestAxis = Variance.X >= Variance.Y ? (Variance.X >= Variance.Z ? 0 : 2) : (Variance.Y >= Variance.Z ? 1 : 2);
		if (BestAxis != SweepAxis && Variance[BestAxis] < 2.0 * Variance[SweepAxis])
		{
			BestAxis = SweepAxis;
		}
		SweepAxis = BestAxis;

		// Rebuild entries in last step's order, then append newcomers.
		++StepStamp;
		int NumEntries = 0;
		auto AddEntry = [this, &NumEntries](int Index)
		{
			FSlot& Slot = Slots[Index];
			const double Center = Slot.Volume.LocationCm[SweepAxis];
			const double Extent = Slot.Volume.GetBoundingRadiusCm();
			Broadphase[NumEntries].Index = Index;
			Broadphase[NumEntries].Min = Center - Extent;
			Broadphase[NumEntries].Max = Center + Extent;
			++NumEntries;
			Slot.SeenStamp = StepStamp;
		};
		for (int OrderIndex = 0; OrderIndex < NumOrdered; ++OrderIndex)
		{
			const int Index = Order[OrderIndex];
			if (Index < HighWater && Slots[Index].bAlive && !Slots[Index].bDepleted && Slots[Index].SeenStamp != StepStamp)
			{
				AddEntry(Index);
			}
		}
		for (int Index = 0; Index < HighWater; ++Index)
		{
			if (Slots[Index].bAlive && !Slots[Index].bDepleted && Slots[Index].SeenStamp != StepStamp)
			{
				AddEntry(Index);
			}
		}

		// Insertion sort: O(n) for the nearly sorted order that coherent motion produces.
		for (int I = 1; I < NumEntries; ++I)
		{
			const FBroadphaseEntry Entry = Broadphase[I];
			int J = I - 1;
			while (J >= 0 && Broadphase[J].Min > Entry.Min)
			{
				Broadphase[J + 1] = Broadphase[J];
				--J;
			}
			Broadphase[J + 1] = Entry;
		}
		for (int I = 0; I < NumEntries; ++I)
		{
			Order[I] = Broadphase[I].Index;
		}
		NumOrdered = NumEntries;

		for (int I = 0; I < NumEntries; ++I)
		{
			const FBroadphaseEntry& EntryA = Broadphase[I];
			for (int J = I + 1; J < NumEntries && Broadphase[J].Min <= EntryA.Max; ++J)
			{
				const FSlot& SlotA = Slots[EntryA.Index];
				const FSlot& SlotB = Slots[Broadphase[J].Index];
				if (SlotA.bDepleted || SlotB.bDepleted || SlotA.Volume.MassKg <= 0.0 || SlotB.Volume.MassKg <= 0.0)
				{
					continue;
				}
				if (!HasReactionFor(SlotA.Volume.Substance, SlotB.Volume.Substance))
				{
					continue;
				}
				FContact Contact;
				if (ComputeContact(SlotA.Volume, SlotB.Volume, Contact))
				{
					++Stats.ContactsLastStep;
					DispatchReactions(EntryA.Index, Broadphase[J].Index, Contact, DeltaSeconds);
				}
			}
		}
	}

	void FSimWorld::DispatchReactions(int IndexA, int IndexB, const FContact& Contact, double DeltaSeconds)
	{
		FSlot& SlotA = Slots[IndexA];
		FSlot& SlotB = Slots[IndexB];
		const FHandle HandleA = MakeHandle(IndexA);
		const FHandle HandleB = MakeHandle(IndexB);

		for (int ReactionIndex = 0; ReactionIndex < NumReactions; ++ReactionIndex)
		{
			const FReactionEntry& Entry = Reactions[ReactionIndex];
			if (SlotA.Volume.MassKg <= 0.0 || SlotB.Volume.MassKg <= 0.0)
			{
				return;
			}
			// Substances can change mid-loop (ice melting), so match against the current ones every time.
			const ESubstance SubstanceA = SlotA.Volume.Substance;
			const ESubstance SubstanceB = SlotB.Volume.Substance;
			if (SubstanceA == Entry.SubstanceA && SubstanceB == Entry.SubstanceB)
			{
				FReactionContext Context{ *this, SlotA.Volume, SlotB.Volume, HandleA, HandleB, Contact, DeltaSeconds, Settings };
				Entry.Function(Entry.UserData, Context);
				++Stats.ReactionCallsLastStep;
			}
			else if (SubstanceA == Entry.SubstanceB && SubstanceB == Entry.SubstanceA)
			{
				const FContact Flipped = Contact.Flipped();
				FReactionContext Context{ *this, SlotB.Volume, SlotA.Volume, HandleB, HandleA, Flipped, DeltaSeconds, Settings };
				Entry.Function(Entry.UserData, Context);
				++Stats.ReactionCallsLastStep;
			}
		}
	}

	void FSimWorld::ExchangeHeatWithAmbient(double DeltaSeconds)
	{
		for (int Index = 0; Index < HighWater; ++Index)
		{
			FSlot& Slot = Slots[Index];
			if (!Slot.bAlive || Slot.bDepleted || Slot.Volume.MassKg <= 0.0)
			{
				continue;
			}
			const Thermo::FPhaseChangeResult Change = ExchangeWithAmbient(Slot.Volume, Settings.AmbientTemperatureK, DeltaSeconds);
			RecordPhaseChange(Index, Change);
		}
	}

	void FSimWorld::UpdateMoisturePatches(double DeltaSeconds)
	{
		if (NumPatches == 0)
		{
			return;
		}
		using namespace Thermo::Constants;
		const double HeatPerKgEvaporated = SpecificHeatWater * KMax(WaterBoilingPointK - Settings.AmbientTemperatureK, 0.0) + LatentHeatVaporization;

		// Natural drying.
		for (int PatchIndex = 0; PatchIndex < NumPatches; ++PatchIndex)
		{
			FMoisturePatch& Patch = Patches[PatchIndex];
			const double RadiusM = CmToM(Patch.RadiusCm);
			Patch.WaterKg -= Settings.DryingRateKgM2S * Settings.DryingTimeScale * Pi * RadiusM * RadiusM * DeltaSeconds;
		}

		// Flames resting on wet ground boil it dry, and pay for it in heat.
		for (int Index = 0; Index < HighWater; ++Index)
		{
			FSlot& Slot = Slots[Index];
			FVolume& Fire = Slot.Volume;
			if (!Slot.bAlive || Slot.bDepleted || Fire.Substance != ESubstance::Fire || Fire.TemperatureK <= WaterBoilingPointK)
			{
				continue;
			}
			for (int PatchIndex = 0; PatchIndex < NumPatches; ++PatchIndex)
			{
				FMoisturePatch& Patch = Patches[PatchIndex];
				if (Patch.WaterKg <= 0.0
					|| Distance2D(Fire.LocationCm, Patch.CenterCm) > Patch.RadiusCm + Fire.RadiusCm
					|| KAbs(Fire.LocationCm.Z - Patch.CenterCm.Z) > Fire.RadiusCm)
				{
					continue;
				}
				const double OverlapRadiusM = CmToM(KMin(Patch.RadiusCm, Fire.RadiusCm));
				double HeatJ = Settings.FireGroundHeatTransferCoefficient * Pi * OverlapRadiusM * OverlapRadiusM
					* (Fire.TemperatureK - WaterBoilingPointK) * DeltaSeconds * Settings.HeatTransferScale;
				HeatJ = KMin(HeatJ, Fire.GetHeatCapacityJPerK() * (Fire.TemperatureK - WaterBoilingPointK));

				const double EvaporatedKg = KMin(HeatJ / HeatPerKgEvaporated, Patch.WaterKg);
				if (EvaporatedKg <= 0.0)
				{
					continue;
				}
				Patch.WaterKg -= EvaporatedKg;
				AddHeat(Fire, -EvaporatedKg * HeatPerKgEvaporated);

				const FVec3 SteamLocation(Patch.CenterCm.X, Patch.CenterCm.Y, Patch.CenterCm.Z + 20.0);
				FVolume Steam = FVolume::MakeDefault(ESubstance::Steam, EvaporatedKg);
				Steam.LocationCm = SteamLocation;
				QueueFreeVolume(Steam);
				AccumulateEvent(MakeHandle(Index), FHandle(), EReactionType::Evaporation, ESubstance::Fire, ESubstance::Water,
					SteamLocation, FVec3(0.0, 0.0, 1.0), EvaporatedKg, EvaporatedKg * HeatPerKgEvaporated);
			}
		}

		// Compact away dried patches.
		int Kept = 0;
		for (int PatchIndex = 0; PatchIndex < NumPatches; ++PatchIndex)
		{
			if (Patches[PatchIndex].WaterKg > 0.01)
			{
				Patches[Kept++] = Patches[PatchIndex];
			}
		}
		NumPatches = Kept;
	}

	void FSimWorld::IntegrateImpulses()
	{
		for (int Index = 0; Index < HighWater; ++Index)
		{
			FSlot& Slot = Slots[Index];
			FVolume& Volume = Slot.Volume;
			if (!Slot.bAlive || Volume.PendingImpulseKgCmS.IsZero())
			{
				continue;
			}
			Volume.VelocityCmS += Volume.PendingImpulseKgCmS * Volume.GetInverseMass();
			Slot.FrameImpulseKgCmS += Volume.PendingImpulseKgCmS;
			Volume.PendingImpulseKgCmS = FVec3();
		}
	}

	void FSimWorld::IntegrateFreeVolumes(double DeltaSeconds)
	{
		const FVec3 GravityCmS2(0.0, 0.0, Settings.GravityZCmS2);
		const double Damping = KExp(-DeltaSeconds / KMax(Settings.FreeGasDampingTimeS, 0.01));

		for (int Index = 0; Index < HighWater; ++Index)
		{
			FSlot& Slot = Slots[Index];
			if (!Slot.bAlive || Slot.bOwned || Slot.bDepleted)
			{
				continue;
			}
			FVolume& Volume = Slot.Volume;
			if (Volume.IsGas())
			{
				// Buoyancy a = g * (rho_air / rho_gas - 1) against gravity, capped so very hot, light parcels stay sane;
				// then drag toward the still surrounding air.
				const double Density = Volume.GetDensityKgM3();
				const double BuoyancyRatio = Density > SmallNumber ? KClamp(Settings.AmbientAirDensityKgM3 / Density - 1.0, -1.0, 3.0) : 0.0;
				Volume.VelocityCmS -= GravityCmS2 * (BuoyancyRatio * DeltaSeconds);
				Volume.VelocityCmS *= Damping;
			}
			else
			{
				Volume.VelocityCmS += GravityCmS2 * DeltaSeconds;
			}
			Volume.LocationCm += Volume.VelocityCmS * DeltaSeconds;
		}
	}

	void FSimWorld::QueueFreeVolume(const FVolume& Volume)
	{
		if (Volume.MassKg <= 0.0)
		{
			return;
		}
		if (NumPendingSpawns >= MaxPendingSpawns)
		{
			++Stats.DroppedSpawns;
			return;
		}
		PendingSpawns[NumPendingSpawns++] = Volume;
	}

	void FSimWorld::ProcessFreeVolumeSpawns()
	{
		const int NumRequests = NumPendingSpawns;
		NumPendingSpawns = 0;

		for (int RequestIndex = 0; RequestIndex < NumRequests; ++RequestIndex)
		{
			const FVolume& Request = PendingSpawns[RequestIndex];
			if (!Request.IsGas())
			{
				// Only gases may float ownerless.
				continue;
			}

			// Join the nearest free volume of the same substance in range, or anywhere once at capacity.
			const bool bAtCapacity = NumFree >= Settings.MaxFreeVolumes;
			int MergeIndex = -1;
			double BestDistanceSq = 1e300;
			for (int Index = 0; Index < HighWater; ++Index)
			{
				const FSlot& Slot = Slots[Index];
				if (!Slot.bAlive || Slot.bOwned || Slot.bDepleted || Slot.Volume.Substance != Request.Substance)
				{
					continue;
				}
				const double DistanceSq = DistanceSquared(Slot.Volume.LocationCm, Request.LocationCm);
				const double MergeRange = Settings.FreeVolumeMergeRadiusCm + Slot.Volume.RadiusCm;
				if ((bAtCapacity || DistanceSq <= MergeRange * MergeRange) && DistanceSq < BestDistanceSq)
				{
					BestDistanceSq = DistanceSq;
					MergeIndex = Index;
				}
			}

			if (MergeIndex >= 0)
			{
				MergeSimFreeVolume(Slots[MergeIndex].Volume, Request);
				continue;
			}
			if (bAtCapacity)
			{
				++Stats.DroppedSpawns;
				continue;
			}

			FVolume Volume = Request;
			Volume.Shape = EShape::Sphere;
			Volume.bDeriveRadiusFromMass = true;
			const FHandle Handle = AddVolume(Volume, false);
			if (Handle.IsSet())
			{
				AddNotice(Handle, Volume.Substance, true);
			}
		}
	}

	void FSimWorld::ResolveDepletion()
	{
		for (int Index = 0; Index < HighWater; ++Index)
		{
			FSlot& Slot = Slots[Index];
			if (!Slot.bAlive || Slot.bDepleted)
			{
				continue;
			}
			const bool bTooLight = Slot.Volume.MassKg < Settings.MinVolumeMassKg;
			const bool bExtinguished = Slot.Volume.Substance == ESubstance::Fire && Slot.Volume.TemperatureK < Settings.FireExtinguishTemperatureK;
			if (!bTooLight && !bExtinguished)
			{
				continue;
			}

			Slot.bDepleted = true;
			if (bExtinguished)
			{
				const double ResidualHeatJ = Slot.Volume.GetHeatCapacityJPerK() * KMax(Slot.Volume.TemperatureK - Settings.AmbientTemperatureK, 0.0);
				AddDiscreteEvent(EReactionType::Extinguished, Index, Slot.Volume.MassKg, ResidualHeatJ);
			}
			if (!Slot.bOwned)
			{
				AddNotice(MakeHandle(Index), Slot.Volume.Substance, false);
				RemoveVolume(MakeHandle(Index));
			}
		}
	}

	// ---------------------------------------------------------------------------------------------------- Events

	void FSimWorld::RecordPhaseChange(int Index, const Thermo::FPhaseChangeResult& Change)
	{
		const FVolume& Volume = Slots[Index].Volume;
		using namespace Thermo::Constants;

		if (Change.MeltedKg > 0.0)
		{
			AddDiscreteEvent(EReactionType::Melting, Index, Change.MeltedKg, Change.MeltedKg * LatentHeatFusion);
		}
		if (Change.FrozenKg > 0.0)
		{
			AddDiscreteEvent(EReactionType::Freezing, Index, Change.FrozenKg, Change.FrozenKg * LatentHeatFusion);
		}
		if (Change.VaporizedKg > 0.0)
		{
			FVolume Steam = FVolume::MakeDefault(ESubstance::Steam, Change.VaporizedKg);
			Steam.LocationCm = Volume.LocationCm + FVec3(0.0, 0.0, Volume.RadiusCm);
			Steam.VelocityCmS = Volume.VelocityCmS;
			QueueFreeVolume(Steam);
			AccumulateEvent(MakeHandle(Index), FHandle(), EReactionType::Evaporation, Volume.Substance, ESubstance::Steam,
				Steam.LocationCm, FVec3(0.0, 0.0, 1.0), Change.VaporizedKg, Change.VaporizedKg * LatentHeatVaporization);
		}
		if (Change.CondensedKg > 0.0)
		{
			AccumulateEvent(MakeHandle(Index), FHandle(), EReactionType::Condensation, Volume.Substance, ESubstance::Water,
				Volume.LocationCm, FVec3(0.0, 0.0, -1.0), Change.CondensedKg, Change.CondensedKg * LatentHeatVaporization);
		}
	}

	void FSimWorld::EmitPairEvent(const FReactionContext& Context, EReactionType Type, double MassKg, double EnergyJ)
	{
		AccumulateEvent(Context.HandleA, Context.HandleB, Type, Context.A.Substance, Context.B.Substance,
			Context.Contact.PointCm, Context.Contact.NormalAB, MassKg, EnergyJ);
	}

	void FSimWorld::AccumulateEvent(FHandle A, FHandle B, EReactionType Type, ESubstance SubstanceA, ESubstance SubstanceB,
		const FVec3& LocationCm, const FVec3& Normal, double MassKg, double EnergyJ)
	{
		FPendingEvent* Pending = nullptr;
		for (int Index = 0; Index < NumPendingEvents; ++Index)
		{
			FPendingEvent& Candidate = PendingEvents[Index];
			if (Candidate.Type == Type && Candidate.A == A && Candidate.B == B)
			{
				Pending = &Candidate;
				break;
			}
		}
		if (!Pending)
		{
			if (NumPendingEvents >= MaxPendingEvents)
			{
				++Stats.DroppedEvents;
				return;
			}
			Pending = &PendingEvents[NumPendingEvents++];
			*Pending = FPendingEvent();
			Pending->A = A;
			Pending->B = B;
			Pending->Type = Type;
			Pending->Event.Type = Type;
			Pending->Event.SubstanceA = SubstanceA;
			Pending->Event.SubstanceB = SubstanceB;
			Pending->Event.VolumeA = A;
			Pending->Event.VolumeB = B;
			Pending->Event.LocationCm = LocationCm;
		}

		// Mass-weighted location over the window; massless events (deflection) track the latest contact.
		FReactionEvent& Event = Pending->Event;
		const double Weight = KMax(MassKg, 0.0);
		Event.LocationCm = Event.MassKg + Weight > SmallNumber
			? (Event.LocationCm * Event.MassKg + LocationCm * Weight) / (Event.MassKg + Weight)
			: LocationCm;
		Event.Normal = Normal;
		Event.MassKg += MassKg;
		Event.EnergyJ += EnergyJ;
	}

	void FSimWorld::AddDiscreteEvent(EReactionType Type, int Index, double MassKg, double EnergyJ)
	{
		if (NumDiscreteEvents >= MaxDiscreteEvents)
		{
			++Stats.DroppedEvents;
			return;
		}
		const FVolume& Volume = Slots[Index].Volume;
		FReactionEvent& Event = DiscreteEvents[NumDiscreteEvents++];
		Event = FReactionEvent();
		Event.Type = Type;
		Event.SubstanceA = Volume.Substance;
		Event.VolumeA = MakeHandle(Index);
		Event.LocationCm = Volume.LocationCm;
		Event.MassKg = MassKg;
		Event.EnergyJ = EnergyJ;
		Event.bDiscrete = true;
	}

	void FSimWorld::AddNotice(FHandle Handle, ESubstance Substance, bool bSpawned)
	{
		if (NumNotices >= MaxNotices)
		{
			return;
		}
		FFreeVolumeNotice& Notice = Notices[NumNotices++];
		Notice.Handle = Handle;
		Notice.Substance = Substance;
		Notice.bSpawned = bSpawned;
	}

	int FSimWorld::FlushEvents(double FrameSeconds, FReactionEvent* OutEvents, int MaxEvents)
	{
		int Count = 0;
		for (int Index = 0; Index < NumDiscreteEvents && Count < MaxEvents; ++Index)
		{
			OutEvents[Count++] = DiscreteEvents[Index];
		}
		NumDiscreteEvents = 0;

		if (NumPendingEvents == 0)
		{
			EventWindowSeconds = 0.0;
			return Count;
		}
		EventWindowSeconds += FrameSeconds;
		if (EventWindowSeconds < Settings.ReactionEventIntervalS)
		{
			return Count;
		}

		const double Window = KMax(EventWindowSeconds, SmallNumber);
		for (int Index = 0; Index < NumPendingEvents && Count < MaxEvents; ++Index)
		{
			FReactionEvent Event = PendingEvents[Index].Event;
			Event.WindowSeconds = Window;
			Event.MassRateKgS = Event.MassKg / Window;
			Event.PowerW = Event.EnergyJ / Window;
			OutEvents[Count++] = Event;
		}
		NumPendingEvents = 0;
		EventWindowSeconds = 0.0;
		return Count;
	}

	int FSimWorld::ConsumeFreeVolumeNotices(FFreeVolumeNotice* OutNotices, int MaxCount)
	{
		const int Count = KMin(NumNotices, MaxCount);
		for (int Index = 0; Index < Count; ++Index)
		{
			OutNotices[Index] = Notices[Index];
		}
		NumNotices = 0;
		return Count;
	}

	// ---------------------------------------------------------------------------------------------------- Surfaces

	void FSimWorld::DepositWaterOnSurface(const FVec3& LocationCm, double WaterKg, double Porosity)
	{
		// Rock and pavement shed water instead of holding it.
		if (WaterKg <= 0.0 || Porosity < 0.01)
		{
			return;
		}

		const int PatchLimit = KMin(Settings.MaxMoisturePatches, MaxMoisturePatches);
		const bool bAtCapacity = NumPatches >= PatchLimit;
		int TargetIndex = -1;
		double BestDistanceSq = 1e300;
		for (int Index = 0; Index < NumPatches; ++Index)
		{
			const FMoisturePatch& Patch = Patches[Index];
			const double DistanceSq = DistanceSquared(Patch.CenterCm, LocationCm);
			const bool bInside = DistanceSq <= Patch.RadiusCm * Patch.RadiusCm;
			if ((bInside || bAtCapacity) && DistanceSq < BestDistanceSq)
			{
				BestDistanceSq = DistanceSq;
				TargetIndex = Index;
			}
		}
		if (TargetIndex < 0)
		{
			if (bAtCapacity)
			{
				return;
			}
			TargetIndex = NumPatches++;
			Patches[TargetIndex] = FMoisturePatch();
			Patches[TargetIndex].CenterCm = LocationCm;
			Patches[TargetIndex].RadiusCm = Settings.MinMoisturePatchRadiusCm;
			Patches[TargetIndex].Porosity = Porosity;
		}

		FMoisturePatch& Patch = Patches[TargetIndex];
		const double NewWaterKg = Patch.WaterKg + WaterKg;
		// Water-weighted center: repeated splashes drag the patch toward where the water lands.
		Patch.CenterCm = (Patch.CenterCm * Patch.WaterKg + LocationCm * WaterKg) / NewWaterKg;
		Patch.WaterKg = NewWaterKg;

		// Oversaturated soil spreads outward; beyond the maximum size the excess runs off.
		const double DepthM = Settings.MoistureSoilDepthM;
		if (Patch.WaterKg > Patch.GetCapacityKg(DepthM))
		{
			const double RequiredAreaM2 = Patch.WaterKg / (WaterDensity * Patch.Porosity * DepthM);
			Patch.RadiusCm = KMin(MToCm(KSqrt(RequiredAreaM2 / Pi)), Settings.MaxMoisturePatchRadiusCm);
			Patch.WaterKg = KMin(Patch.WaterKg, Patch.GetCapacityKg(DepthM));
		}
	}

	double FSimWorld::GetSurfaceSaturationAt(const FVec3& LocationCm) const
	{
		double Best = 0.0;
		for (int Index = 0; Index < NumPatches; ++Index)
		{
			const FMoisturePatch& Patch = Patches[Index];
			const double DistanceCm = Distance2D(Patch.CenterCm, LocationCm);
			if (DistanceCm > Patch.RadiusCm || KAbs(Patch.CenterCm.Z - LocationCm.Z) > Patch.RadiusCm)
			{
				continue;
			}
			const double EdgeFalloff = 1.0 - KSmoothStep(0.7 * Patch.RadiusCm, Patch.RadiusCm, DistanceCm);
			Best = KMax(Best, Patch.GetSaturation(Settings.MoistureSoilDepthM) * EdgeFalloff);
		}
		return Best;
	}

	double FSimWorld::GetTractionMultiplierForSaturation(double Saturation) const
	{
		const double MudAmount = KSmoothStep(Settings.MudOnsetSaturation, 1.0, Saturation);
		return KLerp(1.0, Settings.MudFrictionRatio, MudAmount);
	}

	double FSimWorld::GetSurfaceTractionMultiplierAt(const FVec3& LocationCm) const
	{
		return GetTractionMultiplierForSaturation(GetSurfaceSaturationAt(LocationCm));
	}
}
