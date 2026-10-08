#pragma once

#include "Sim/BendingSimWorld.h"
#include "Sim/BendingTerrain.h"

/**
 * Water whip: a stream of water held together by a waterbender, simulated as a chain of points (position-based
 * dynamics) whose segments are Water capsule volumes in an FSimWorld.
 *
 * Because every segment is a real volume, the whip takes part in every reaction: lashing a fire boils the contact
 * layer into steam and puts the fire out, lashing a soil boulder shoves it and soaks it, an air blast pushes the
 * chain, freezing turns segments to ice. The bender controls the water by force, not by animation: each point is
 * pulled toward the intended shape by an acceleration capped at what the bender can exert, so heavier water lags.
 *
 * Per frame: SetControl -> PreStep (integrate the chain, write volumes) -> FSimWorld::Advance -> PostStep (read back
 * impulses, mass lost to steam or soil, phase changes).
 */
namespace BendingSim
{
	struct FWhipSettings
	{
		int NumPoints = 16;
		double LengthCm = 550.0;
		/** Spring gain pulling each point toward the intended shape (1/s^2). */
		double ShapeStiffness = 180.0;
		/** Damping of velocity relative to the hand (1/s). */
		double ShapeDamping = 16.0;
		/** Most acceleration the bender can give the water (m/s^2). Gravity is cancelled out of this budget. */
		double MaxControlAccelerationMs2 = 160.0;
		/** A lash straightens the whip from the root to the tip over this time. */
		double LashDurationS = 0.35;
		/** Gain multiplier on the part of the whip the lash wave has reached. */
		double LashStiffnessScale = 5.0;
		/** After a lash, time to settle back into the held shape. */
		double LashRecoverS = 0.35;
		/** Time for drawn water to stream from its source into the held shape. */
		double FormSeconds = 0.7;
		double SubstepHz = 240.0;
		int ConstraintIterations = 4;
	};

	enum class EWhipState : unsigned char
	{
		Inactive,
		/** Water streaming from the source to the hand. */
		Forming,
		Holding,
		Lashing
	};

	/** A parcel of the whip's matter when it is let go. */
	struct FWhipDrop
	{
		FVec3 LocationCm;
		FVec3 VelocityCmS;
		double MassKg = 0.0;
		double TemperatureK = 288.15;
		ESubstance Substance = ESubstance::Water;
	};

	class BENDINGSIM_API FWaterWhip
	{
	public:
		static constexpr int MaxPoints = 32;
		static constexpr int MaxSegments = MaxPoints - 1;

		FWhipSettings Settings;

		/**
		 * Draws WaterMassKg out of a source at SourceCm (a pond surface). The water starts strung between the source
		 * and the hand and streams into the held shape over FormSeconds.
		 */
		bool Create(FSimWorld& World, const FVec3& SourceCm, const FVec3& HandCm, double WaterMassKg, double TemperatureK);
		/** Removes the segment volumes without releasing anything. */
		void Destroy(FSimWorld& World);

		/** Bender input, every frame. */
		void SetControl(const FVec3& HandCm, const FVec3& HandVelocityCmS, const FVec3& AimPointCm);
		/** Lashes toward the aim point. False while forming or inactive. Lashing again while extended stays extended. */
		bool Lash();
		/** While set, a lashed whip stays stretched toward the aim instead of coiling back (sweep it, hold it in a fire). */
		void SetHoldExtended(bool bInHoldExtended) { bHoldExtended = bInHoldExtended; }

		/** Integrates the chain over DeltaSeconds and writes the segment volumes. Call before FSimWorld::Advance. */
		void PreStep(FSimWorld& World, const FTerrain* Terrain, double DeltaSeconds);
		/** Applies what reactions did to the segments: impulses, mass lost, freezing and melting. Call after Advance. */
		void PostStep(FSimWorld& World);

		/** Heat (J) to extract to freeze every liquid segment completely. */
		double GetHeatToFreeze(const FSimWorld& World) const;
		/** Heat (J) to add to melt every frozen segment completely. */
		double GetHeatToMelt(const FSimWorld& World) const;
		/**
		 * Moves heat into (positive) or out of (negative) the water. Segments change phase one at a time from the hand
		 * outward, so too little heat freezes (or thaws) part of the whip. Returns the heat moved (J): what a bender pays.
		 */
		double TransferHeat(FSimWorld& World, double HeatJ);

		/** Lets go: removes the segments and reports where each parcel is, how fast it moves and what it is. */
		int Release(FSimWorld& World, FWhipDrop* OutDrops, int MaxDrops);

		bool IsActive() const { return State != EWhipState::Inactive; }
		EWhipState GetState() const { return State; }
		bool IsFrozen(const FSimWorld& World) const;
		bool IsAnyFrozen(const FSimWorld& World) const;
		/** Too little water left to hold together (boiled away or soaked into soil). */
		bool ShouldCollapse(const FSimWorld& World) const;
		double GetMassKg(const FSimWorld& World) const;
		double GetInitialMassKg() const { return InitialMassKg; }

		int GetNumPoints() const { return NumPoints; }
		const FVec3& GetPoint(int Index) const { return Points[Index]; }
		const FVec3& GetPointVelocity(int Index) const { return Velocities[Index]; }
		int GetNumSegments() const { return NumPoints > 1 ? NumPoints - 1 : 0; }
		FHandle GetSegmentHandle(int Index) const { return Segments[Index]; }
		double GetSegmentRadiusCm(int Index) const { return SegmentRadiiCm[Index]; }
		double GetTipSpeedCmS() const { return NumPoints > 0 ? Velocities[NumPoints - 1].Size() : 0.0; }
		/** 0 while forming starts, 1 once the water reached the hand. */
		double GetFormProgress() const;

	private:
		FVec3 GetTarget(int Index, double LashBlend) const;
		double GetLashWeight(int Index) const;
		double GetSegmentMassKg(const FSimWorld& World, int Segment) const;
		void SyncVolumes(FSimWorld& World);

		EWhipState State = EWhipState::Inactive;
		int NumPoints = 0;
		FVec3 Points[MaxPoints];
		FVec3 Velocities[MaxPoints];
		FHandle Segments[MaxSegments];
		double SegmentRadiiCm[MaxSegments] = {};
		/** Rest distance of i -> i+2 when a segment pair froze (rigid ice); 0 = not frozen. */
		double BendRestCm[MaxPoints] = {};

		FVec3 HandCm;
		FVec3 PreviousHandCm;
		FVec3 HandVelocityCmS;
		FVec3 AimPointCm;
		FVec3 AimDirection = FVec3(1.0, 0.0, 0.0);
		FVec3 Forward = FVec3(1.0, 0.0, 0.0);
		FVec3 Right = FVec3(0.0, 1.0, 0.0);

		double InitialMassKg = 0.0;
		double InitialSpacingCm = 0.0;
		double ElapsedS = 0.0;
		double StateTimeS = 0.0;
		/** Counts down after a lash while the whip eases back into the held shape. */
		double LashRecoverRemainingS = 0.0;
		/** How extended the whip already was when the current lash started. */
		double LashCarry = 0.0;
		bool bHoldExtended = false;
	};
}
