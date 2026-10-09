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
 * pulled along the intended motion by an acceleration capped at what the bender can exert, so heavier water lags.
 *
 * The motion follows waterbending's Tai Chi roots: continuous and circular, push and pull.
 *  - Holding: the water circles the bender's body in a loose loop, flowing, ready.
 *  - Lash: pull (Windup: the stream is drawn back over the shoulder), snap (Striking: a loop rolls down the stream
 *    toward the aim, so the tip travels at twice the loop's speed and cracks out to full length), a brief Extended
 *    beat at full reach, then pull (Returning: the stream flows back, root first, into the circling loop). Nothing
 *    has to be held; the whip always comes back.
 *  - At the snap the tip flings a little of its water forward as spray (ConsumeSpray), the way a real wet whip does.
 *
 * Per frame: SetControl -> PreStep (integrate the chain, write volumes) -> FSimWorld::Advance -> PostStep (read back
 * impulses, mass lost to steam or soil, phase changes).
 */
namespace BendingSim
{
	struct FWhipSettings
	{
		int NumPoints = 40;
		double LengthCm = 550.0;
		/** Radius at the tip relative to the root: the stream tapers like a whip (more water near the hand). */
		double TipRadiusFraction = 0.45;
		/** Most acceleration the bender can give the water (m/s^2). Gravity is cancelled out of this budget. */
		double MaxControlAccelerationMs2 = 450.0;

		// ------------------------------------------------------------------ Holding: water circling the body
		/** Radius of the loop around the bender's chest. */
		double ReadyRadiusCm = 110.0;
		/** The loop sits this far below the chest in front and rises by twice this behind the bender. */
		double ReadyDropCm = 25.0;
		/** Spring gain (1/s^2) and damping ratio while circling: soft, so the water sways and lags like liquid. */
		double ReadyStiffness = 110.0;
		double ReadyDampingRatio = 0.75;

		// ------------------------------------------------------------------ Lash
		/** Drawing the stream back over the shoulder. Match the move's startup frames. */
		double WindupS = 8.0 / 60.0;
		/** The loop rolling out to full reach. Match the move's active frames. */
		double StrikeS = 14.0 / 60.0;
		/** Share of the stream already laid toward the aim when the windup ends (the rest is folded back over it). */
		double WindupFront = 0.25;
		/** Held at full reach before the pull back. */
		double ExtendedS = 0.05;
		/** Flowing back into the circling loop. */
		double ReturnS = 0.42;
		double LashStiffness = 1600.0;
		double LashDampingRatio = 0.55;
		/** Radius of the rolling loop that carries the strike down the stream. */
		double LoopRadiusCm = 22.0;
		/**
		 * The strike's plane leans from vertical toward the bender's lead side by this angle: a three-quarter sidearm
		 * lash, so the stream is drawn back over the shoulder and sweeps through an arc instead of folding straight
		 * up and over.
		 */
		double StrikeLeanDeg = 50.0;
		/** Share of the water in the last quarter of the stream flung forward as spray at the snap. */
		double SprayFraction = 0.08;
		/** Water stretches this much at most; pulled tighter, the stream stops dead (the snap). */
		double MaxStretch = 1.08;

		/** Time for drawn water to stream from its source into the circling loop. */
		double FormSeconds = 0.8;
		double SubstepHz = 240.0;
		int ConstraintIterations = 8;
	};

	enum class EWhipState : unsigned char
	{
		Inactive,
		/** Water streaming from the source to the hand. */
		Forming,
		/** Circling the bender, ready to strike. */
		Holding,
		/** Pull: the stream drawn back over the shoulder. */
		Windup,
		/** Snap: a loop rolls down the stream toward the aim. */
		Striking,
		/** At full reach. */
		Extended,
		/** Pull: flowing back into the circling loop. */
		Returning
	};

	/** A parcel of the whip's matter when it is let go, or flung off as spray. */
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
		static constexpr int MaxPoints = 48;
		static constexpr int MaxSegments = MaxPoints - 1;
		static constexpr int MaxPendingSpray = 4;

		FWhipSettings Settings;

		/**
		 * Draws WaterMassKg out of a source at SourceCm (a pond surface). The water starts strung between the source
		 * and the hand and streams into the circling loop over FormSeconds.
		 */
		bool Create(FSimWorld& World, const FVec3& InSourceCm, const FVec3& InHandCm, double WaterMassKg, double TemperatureK);
		/** Removes the segment volumes without releasing anything. */
		void Destroy(FSimWorld& World);

		/** Bender input, every frame. */
		void SetControl(const FVec3& InHandCm, const FVec3& InHandVelocityCmS, const FVec3& InAimPointCm);
		/** The chest the water circles. Without it, a point behind and inside the hand is assumed. */
		void SetBodyCenter(const FVec3& InBodyCenterCm);
		/**
		 * Starts a lash toward the aim: windup, strike, a beat at full reach, then back into the loop. Accepted while
		 * circling, at full reach or on the way back (lashes chain); false while forming or mid-strike.
		 */
		bool Lash();

		/** Integrates the chain over DeltaSeconds and writes the segment volumes. Call before FSimWorld::Advance. */
		void PreStep(FSimWorld& World, const FTerrain* Terrain, double DeltaSeconds);
		/** Applies what reactions did to the segments: impulses, mass lost, freezing and melting. Call after Advance. */
		void PostStep(FSimWorld& World);
		/** Water flung off at the snap since the last call (already taken out of the segments). The owner spawns it. */
		int ConsumeSpray(FWhipDrop* OutDrops, int MaxDrops);
		/**
		 * Takes up to MassKg of liquid water out of the stream, tip first (the bender bends it off the end), and
		 * returns the mass taken; OutTemperatureK is its mass-weighted temperature.
		 */
		double TakeWater(FSimWorld& World, double MassKg, double& OutTemperatureK);

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
		/** Windup, strike or full reach: the lash is under way. */
		bool IsLashing() const { return State == EWhipState::Windup || State == EWhipState::Striking || State == EWhipState::Extended; }
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
		/** Seconds since the current state began. */
		double GetStateTimeS() const { return StateTimeS; }

	private:
		/** Frame the shapes are built in, refreshed every PreStep. */
		struct FFrame
		{
			FVec3 Hand;
			FVec3 Body;
			FVec3 Forward;
			FVec3 Right;
			FVec3 Up;
		};

		void ComputeTargets(FVec3* OutTargets) const;
		void BuildReadyShape(const FFrame& Frame, FVec3* OutShape) const;
		FVec3 GetFoldPoint(const FFrame& Frame, double S, double Front) const;
		double GetPointStiffness(int Index) const;
		double GetPointDampingRatio() const;
		double GetSegmentMassKg(const FSimWorld& World, int Segment) const;
		void SyncVolumes(FSimWorld& World);
		void EnterState(EWhipState NewState);
		void FlingSpray(FSimWorld& World);
		FFrame MakeFrame() const;

		EWhipState State = EWhipState::Inactive;
		int NumPoints = 0;
		FVec3 Points[MaxPoints];
		FVec3 Velocities[MaxPoints];
		FVec3 PreviousTargets[MaxPoints];
		/** The shape when the current lash began, relative to the hand: the windup blends out of it. */
		FVec3 LashStartOffsets[MaxPoints];
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
		FVec3 BodyCenterCm;
		bool bHasBodyCenter = false;
		/** Strike direction, fixed when the snap begins so the lash lands where it was aimed. */
		FVec3 StrikeDirection = FVec3(1.0, 0.0, 0.0);
		FVec3 SourceCm;

		FWhipDrop PendingSpray[MaxPendingSpray];
		int NumPendingSpray = 0;
		/** Fastest the tip's water moved along the strike during this strike: spray leaves at the crack, not after. */
		FVec3 PeakTipVelocityCmS;
		double PeakTipForwardCmS = 0.0;

		double InitialMassKg = 0.0;
		double InitialSpacingCm = 0.0;
		double ElapsedS = 0.0;
		double StateTimeS = 0.0;
		bool bHasPreviousTargets = false;
	};
}
