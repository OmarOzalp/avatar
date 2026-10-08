#pragma once

#include "Sim/BendingArena.h"
#include "Sim/BendingTechniques.h"
#include "Sim/BendingWaterWhip.h"

/**
 * The 3D training ground without an engine: the game side of the bending sandbox for the browser build and the
 * native tests. It plays the parts Unreal plays in the real game (character movement, rigid bodies, the move
 * system with frame data, chi) around the same kernel: FSimWorld for reactions, FTerrain for earthbending,
 * FWaterWhip for waterbending, and the shared technique table and arena layout.
 *
 * Coordinates are the kernel's: cm, Z up. Fixed-capacity, no allocation, no standard library, so it also builds
 * as freestanding WebAssembly.
 */
namespace BendingSandbox3D
{
	using namespace BendingSim;

	enum class EPhase : unsigned char
	{
		None,
		Startup,
		Active,
		Recovery
	};

	/** Player input for one frame. */
	struct FInput
	{
		/** -1..1 relative to the camera's yaw. */
		double MoveForward = 0.0;
		double MoveRight = 0.0;
		FVec3 CameraLocationCm;
		/** Unit view direction; its yaw orients movement. */
		FVec3 CameraForward = FVec3(1.0, 0.0, 0.0);
		bool bJump = false;
		bool bSprint = false;
		/** Held state of LMB, RMB, Q, E. */
		bool bSlotHeld[static_cast<int>(ETechniqueSlot::Count)] = {};
		/** 0 = no change, else an ETechniqueElement. */
		int StanceRequest = 0;
	};

	enum class EBodyKind : unsigned char
	{
		/** From the arena layout (EArenaProp). */
		Prop,
		/** Pulled out of the ground by Rock Throw. */
		ThrownRock
	};

	/** A solid thing with a simulated volume: stones, boulders, clods, dummies, ice blocks, brazier flames, thrown rocks. */
	struct FBody
	{
		EBodyKind Kind = EBodyKind::Prop;
		EArenaProp Prop = EArenaProp::Stone;
		FHandle Volume;
		FVec3 LocationCm;
		FVec3 VelocityCmS;
		/** Orientation for rendering (rolling spheres), quaternion x, y, z, w. */
		double Rotation[4] = { 0.0, 0.0, 0.0, 1.0 };
		/** Dummies: visual tilt (rad) about X and Y and its rate; they rock back like weighted training dummies. */
		double Tilt[2] = {};
		double TiltRate[2] = {};
		double RadiusCm = 10.0;
		double HalfHeightCm = 0.0;
		double MassKg = 1.0;
		/** Where it stands (braziers, ice blocks). */
		FVec3 AnchorCm;
		bool bStatic = false;
		/** Thrown rock being lifted by the bender (kinematic until launched). */
		bool bHeld = false;
		/** Brazier: flame burning. */
		bool bLit = true;
		bool bAlive = false;
	};

	enum class EProjectileKind : unsigned char
	{
		Fire,
		Air,
		Water,
		/** Flame resting on the ground, fed by a bender while held. */
		GroundFlame
	};

	struct FProjectile
	{
		EProjectileKind Kind = EProjectileKind::Fire;
		FHandle Volume;
		FVec3 LocationCm;
		FVec3 VelocityCmS;
		double AgeS = 0.0;
		/** Heat a bender pours in (W) while holding a ground flame. */
		double SustainPowerW = 0.0;
		bool bLanded = false;
		bool bAlive = false;
	};

	struct FPlayer
	{
		/** Feet. */
		FVec3 LocationCm;
		FVec3 VelocityCmS;
		double YawRad = 0.0;
		bool bGrounded = true;
		ETechniqueElement Stance = ETechniqueElement::Water;

		ETechnique Move = ETechnique::None;
		EPhase Phase = EPhase::None;
		double PhaseTimeS = 0.0;
		ETechnique BufferedMove = ETechnique::None;
		double BufferedTimeS = 0.0;
		/** A held technique keeps running while its input is held. */
		ETechnique HoldTechnique = ETechnique::None;
		ETechniqueSlot HoldSlot = ETechniqueSlot::Primary;
		double HoldEmitTimerS = 0.0;

		double Chi = 100.0;
		double MaxChi = 100.0;
		double ChiRegenPerS = 8.0;
		double Stamina = 100.0;
		double MaxStamina = 100.0;
		double StaminaRegenPerS = 15.0;
		double ChiSpentTotal = 0.0;

		FVec3 AimPointCm;
		bool bAimOnSurface = false;
		FVec3 HandCm;
		FVec3 PreviousHandCm;
		/** Walk cycle phase (rad), for the renderer. */
		double StridePhase = 0.0;
		double Traction = 1.0;
	};

	/** Earthbending in progress (wall rising, rock lifting). */
	struct FEarthWork
	{
		ETechnique Technique = ETechnique::None;
		FTerrainBrush Source;
		FTerrainBrush Target;
		double RemainingM3 = 0.0;
		double RateM3S = 0.0;
		/** Rock throw: the rock being lifted. */
		int RockBody = -1;
		FVec3 RockStartCm;
		FVec3 RockHoldCm;
	};

	struct FMessage
	{
		char Text[96] = {};
		double TimeS = 0.0;
	};

	struct FSandboxEvent
	{
		EReactionType Type = EReactionType::None;
		FVec3 LocationCm;
		double MassKg = 0.0;
		double EnergyJ = 0.0;
		bool bDiscrete = false;
	};

	class FSandbox
	{
	public:
		static constexpr int MaxBodies = 96;
		static constexpr int MaxProjectiles = 160;
		static constexpr int MaxMessages = 16;
		static constexpr int MaxFrameEvents = 128;
		static constexpr double PlayerRadiusCm = 35.0;
		static constexpr double PlayerHeightCm = 180.0;
		static constexpr double PlayerMassKg = 70.0;

		FSimWorld World;
		FTerrain Terrain;
		FDefaultReactionParams ReactionParams;
		FTechniqueTuning Tuning;
		FArenaLayout Layout;
		FWaterWhip Whip;
		FPlayer Player;
		FBody Bodies[MaxBodies];
		int NumBodies = 0;
		FProjectile Projectiles[MaxProjectiles];
		FEarthWork EarthWork;
		/** Generated heights, to tell bent soil from untouched ground. */
		double OriginalHeights[FTerrain::MaxSamplesPerSide * FTerrain::MaxSamplesPerSide] = {};

		FMessage Messages[MaxMessages];
		int NumMessages = 0;
		int MessageSerial = 0;
		FSandboxEvent FrameEvents[MaxFrameEvents];
		int NumFrameEvents = 0;

		double TimeS = 0.0;
		/** Heat moved and work done by the bender, for the HUD and tests (J). */
		double ThermalWorkJ = 0.0;
		double KineticWorkJ = 0.0;

		/** Builds the training ground and puts the player at its start. */
		void Init();
		void Advance(const FInput& Input, double FrameSeconds);

		/** Info for renderers and tests. */
		double GetPhaseProgress() const;
		bool IsInCancelWindow() const;
		int CountProjectiles(EProjectileKind Kind) const;
		const FBody* FindBodyByVolume(FHandle Handle) const;
		bool IsWhipVolume(FHandle Handle) const;
		int FindProjectileByVolume(FHandle Handle) const;

		/** Pays chi for physical work; returns the joules granted (all, or what the remaining chi buys). */
		double SpendChiForEnergy(double EnergyJ, bool bThermal);

	private:
		void AddMessage(const char* Text);
		void AddMessageNumber(const char* Prefix, double Value, const char* Suffix);

		void UpdateStance(const FInput& Input);
		void UpdateAim(const FInput& Input);
		void UpdateMoves(const FInput& Input, double Dt);
		bool TryStartMove(ETechnique Technique);
		void EnterPhase(EPhase Phase);
		void OnStartup(ETechnique Technique);
		void OnActive(ETechnique Technique);
		void UpdateHold(const FInput& Input, double Dt);
		void UpdatePlayer(const FInput& Input, double Dt);
		void UpdateHand(double Dt);

		void UpdateEarthWork(double Dt);
		void UpdateWhip(double Dt);
		void UpdateBodies(double Dt);
		void UpdateProjectiles(double Dt);
		void UpdateBraziers(double Dt);
		void SyncVolumesIn();
		void SyncVolumesOut();
		void CollectEvents(double FrameSeconds);

		int AddBody(const FBody& Body, const FVolume& Volume);
		int SpawnProjectile(EProjectileKind Kind, const FVolume& Volume);
		void RemoveProjectile(int Index);
		void RemoveBody(int Index);
		void ReleaseWhip(bool bAsBlast);
		FVec3 GetFacing() const;
		FVec3 GetChestCm() const;
		FVec3 GetEmitOriginCm() const;
		FVec3 ClampToRange(const FVec3& PointCm, double RangeCm) const;
		int FindPondAt(const FVec3& LocationCm) const;

		bool PreviousSlotHeld[static_cast<int>(ETechniqueSlot::Count)] = {};
		bool bPreviousJump = false;
		/** This move created the whip (its active frame does not lash yet). */
		bool bWhipCreatedThisMove = false;
		/** Ground flame fed by the current hold. */
		int HoldProjectile = -1;
		/** Where Raise / Lower Ground works, fixed when the hold starts. */
		FVec3 HoldCenterCm;
		double EarthWorkMovedKg = 0.0;
		double EarthWorkJ = 0.0;
		double LastNoChiMessageS = -10.0;
	};
}
