#pragma once

#include "Sim/BendingSimReactions.h"

namespace BendingSim
{
	/**
	 * Wet ground. Landscape cannot carry per-location physical materials at runtime, so moisture lives here and
	 * traction is queried by position.
	 */
	struct FMoisturePatch
	{
		FVec3 CenterCm;
		double RadiusCm = 0.0;
		double WaterKg = 0.0;
		double Porosity = 0.3;

		/** rho_water * porosity * depth * area. */
		double GetCapacityKg(double SoilDepthM) const
		{
			const double RadiusM = CmToM(RadiusCm);
			return WaterDensity * Porosity * SoilDepthM * Pi * RadiusM * RadiusM;
		}

		double GetSaturation(double SoilDepthM) const
		{
			const double Capacity = GetCapacityKg(SoilDepthM);
			return Capacity > SmallNumber ? KClamp(WaterKg / Capacity, 0.0, 1.0) : 0.0;
		}
	};

	/** An ownerless gas volume (steam cloud) appeared or disappeared. */
	struct FFreeVolumeNotice
	{
		FHandle Handle;
		ESubstance Substance = ESubstance::None;
		bool bSpawned = false;
	};

	/** What happened to an owned volume since its owner last looked. */
	struct FOwnerUpdate
	{
		/** Impulse accumulated over this frame's steps (kg*cm/s), for the owner's physics body. */
		FVec3 FrameImpulseKgCmS;
		ESubstance PreviousSubstance = ESubstance::None;
		bool bNewlyDepleted = false;
	};

	struct FWorldStats
	{
		long long Steps = 0;
		int ContactsLastStep = 0;
		int ReactionCallsLastStep = 0;
		int DroppedEvents = 0;
		int DroppedSpawns = 0;
	};

	/**
	 * Cross-element interaction simulation for one world.
	 *
	 * Each fixed step (60 Hz by default):
	 *   1. coherent sweep-and-prune broadphase along the axis of greatest spread, filtered by the reaction table;
	 *   2. sphere/capsule narrowphase -> exchange area, immersion, normal;
	 *   3. reaction rules exchange mass, momentum and heat;
	 *   4. ambient heat exchange, moisture drying, impulse integration, buoyancy of free gas, spawn merging, depletion.
	 *
	 * Fixed capacity, no allocation, no standard library: the same object runs inside Unreal, in native tests and
	 * in WebAssembly. Engine adapters sync owners before Advance and read FOwnerUpdate / events after.
	 */
	class BENDINGSIM_API FSimWorld
	{
	public:
		static constexpr int MaxVolumes = 1024;
		static constexpr int MaxReactions = 64;
		static constexpr int MaxMoisturePatches = 128;
		static constexpr int MaxPendingSpawns = 256;
		static constexpr int MaxPendingEvents = 256;
		static constexpr int MaxDiscreteEvents = 128;
		static constexpr int MaxNotices = 256;
		/** Most events one FlushEvents call can return. */
		static constexpr int MaxFlushedEvents = MaxPendingEvents + MaxDiscreteEvents;

		FSimSettings Settings;

		/** Removes all volumes, patches and pending output. Reactions and settings are kept. */
		void Reset();

		// ---------------------------------------------------------------- Volumes

		FHandle AddVolume(const FVolume& Volume, bool bOwned = false);
		void RemoveVolume(FHandle Handle);
		bool IsValid(FHandle Handle) const { return FindSlot(Handle) != nullptr; }
		bool IsDepleted(FHandle Handle) const;
		bool IsOwned(FHandle Handle) const;
		FVolume* GetVolume(FHandle Handle);
		const FVolume* GetVolume(FHandle Handle) const;
		/** Handle of the live volume in slot Index, or an unset handle. Iterate Index in [0, GetSlotLimit()). */
		FHandle GetHandleAt(int Index) const;
		int GetSlotLimit() const { return HighWater; }
		int GetNumVolumes() const { return NumAlive; }
		int GetNumFreeVolumes() const { return NumFree; }
		/** Volumes whose shape comes within RadiusCm of CenterCm, filtered by SubstanceMask bits. */
		int QueryVolumes(const FVec3& CenterCm, double RadiusCm, unsigned int Mask, FHandle* OutHandles, int MaxHandles) const;
		/** Returns and clears the frame's impulse, substance change and depletion for an owned volume. */
		bool ConsumeOwnerUpdate(FHandle Handle, FOwnerUpdate& OutUpdate);

		// ---------------------------------------------------------------- Bender commands

		/**
		 * Adds heat (J); negative extracts it (freezing a water whip). Phase changes become events. Flame cannot be
		 * pushed past MaxFlameTemperatureK; OutAcceptedHeatJ reports what was actually taken (what a bender pays for).
		 */
		Thermo::FPhaseChangeResult TransferHeat(FHandle Handle, double HeatJ, double* OutAcceptedHeatJ = nullptr);
		/** Impulse (kg*cm/s) applied on the next step. */
		void AddImpulse(FHandle Handle, const FVec3& ImpulseKgCmS);

		// ---------------------------------------------------------------- Reactions

		bool AddReaction(const FReactionEntry& Entry);
		void ClearReactions();
		bool HasReactionFor(ESubstance A, ESubstance B) const;

		// ---------------------------------------------------------------- Surfaces

		/** Water hitting soil. Rock (porosity < 0.01) sheds water. */
		void DepositWaterOnSurface(const FVec3& LocationCm, double WaterKg, double Porosity);
		double GetSurfaceSaturationAt(const FVec3& LocationCm) const;
		/** Ground friction multiplier: 1 dry, MudFrictionRatio saturated. */
		double GetSurfaceTractionMultiplierAt(const FVec3& LocationCm) const;
		double GetTractionMultiplierForSaturation(double Saturation) const;
		int GetNumMoisturePatches() const { return NumPatches; }
		const FMoisturePatch& GetMoisturePatch(int Index) const { return Patches[Index]; }

		// ---------------------------------------------------------------- Time

		/** Runs as many fixed steps as FrameSeconds covers (capped). Returns steps taken. */
		int Advance(double FrameSeconds);
		void Step(double DeltaSeconds);
		double GetSimTimeSeconds() const { return SimTimeSeconds; }

		// ---------------------------------------------------------------- Output

		/** Discrete events, plus aggregated continuous events once ReactionEventIntervalS has elapsed. */
		int FlushEvents(double FrameSeconds, FReactionEvent* OutEvents, int MaxEvents);
		int ConsumeFreeVolumeNotices(FFreeVolumeNotice* OutNotices, int MaxCount);
		const FWorldStats& GetStats() const { return Stats; }

		// ---------------------------------------------------------------- Used by FReactionContext

		void EmitPairEvent(const FReactionContext& Context, EReactionType Type, double MassKg, double EnergyJ);
		void QueueFreeVolume(const FVolume& Volume);

	private:
		struct FSlot
		{
			FVolume Volume;
			FVec3 FrameImpulseKgCmS;
			ESubstance SubstanceAtFrameStart = ESubstance::None;
			unsigned int Serial = 0;
			unsigned int SeenStamp = 0;
			bool bAlive = false;
			bool bOwned = false;
			bool bDepleted = false;
			bool bDepletionReported = false;
		};

		struct FBroadphaseEntry
		{
			int Index = -1;
			double Min = 0.0;
			double Max = 0.0;
		};

		struct FPendingEvent
		{
			FHandle A;
			FHandle B;
			EReactionType Type = EReactionType::None;
			FReactionEvent Event;
		};

		FSlot* FindSlot(FHandle Handle);
		const FSlot* FindSlot(FHandle Handle) const;
		FHandle MakeHandle(int Index) const;

		void UpdateDerivedShapes();
		void FindAndResolveContacts(double DeltaSeconds);
		void DispatchReactions(int IndexA, int IndexB, const FContact& Contact, double DeltaSeconds);
		void ExchangeHeatWithAmbient(double DeltaSeconds);
		void UpdateMoisturePatches(double DeltaSeconds);
		void IntegrateImpulses();
		void IntegrateFreeVolumes(double DeltaSeconds);
		void ProcessFreeVolumeSpawns();
		void ResolveDepletion();
		void RecordPhaseChange(int Index, const Thermo::FPhaseChangeResult& Change);
		void AccumulateEvent(FHandle A, FHandle B, EReactionType Type, ESubstance SubstanceA, ESubstance SubstanceB,
			const FVec3& LocationCm, const FVec3& Normal, double MassKg, double EnergyJ);
		void AddDiscreteEvent(EReactionType Type, int Index, double MassKg, double EnergyJ);
		void AddNotice(FHandle Handle, ESubstance Substance, bool bSpawned);

		FSlot Slots[MaxVolumes];
		int FreeIndices[MaxVolumes] = {};
		int NumFreeIndices = 0;
		int HighWater = 0;
		int NumAlive = 0;
		int NumFree = 0;
		unsigned int NextSerial = 1;

		FReactionEntry Reactions[MaxReactions];
		int NumReactions = 0;
		bool PairHasReaction[static_cast<int>(ESubstance::Count)][static_cast<int>(ESubstance::Count)] = {};

		FBroadphaseEntry Broadphase[MaxVolumes];
		int Order[MaxVolumes] = {};
		int NumOrdered = 0;
		int SweepAxis = 0;
		unsigned int StepStamp = 0;

		FVolume PendingSpawns[MaxPendingSpawns];
		int NumPendingSpawns = 0;

		FPendingEvent PendingEvents[MaxPendingEvents];
		int NumPendingEvents = 0;
		FReactionEvent DiscreteEvents[MaxDiscreteEvents];
		int NumDiscreteEvents = 0;
		double EventWindowSeconds = 0.0;

		FFreeVolumeNotice Notices[MaxNotices];
		int NumNotices = 0;

		FMoisturePatch Patches[MaxMoisturePatches];
		int NumPatches = 0;

		double StepAccumulator = 0.0;
		double SimTimeSeconds = 0.0;
		FWorldStats Stats;
	};
}
