#pragma once

#include "Sim/BendingArena.h"
#include "Sim/BendingSparring.h"
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
		/** Guard held (C): blocks the sparring partner's attacks, parries them in its first moments. */
		bool bGuard = false;
		/** Dodge (V): a roll the way you are moving (back from the camera with no move input). */
		bool bDodge = false;
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
		ThrownRock,
		/** The sparring partner's body (FRival): a training dummy's capsule, the size and weight of a person. */
		Rival
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
		/** Banners, straw, crates, dummies, lanterns: heat soaked up, its fire, what is left to burn. */
		FCombustible Burn;
		/** Water it holds (barrels). */
		double WaterKg = 0.0;
		/** Facing (rad) and variant from the layout (a banner's element). */
		double YawRad = 0.0;
		int Variant = 0;
		/** Hit hard enough to smash this frame. */
		bool bBreakPending = false;
		/** Training dummies: health, damage taken this frame (reported as one hit), time left knocked out. */
		double Health = 100.0;
		double FrameDamage = 0.0;
		/** Damage gathered into one hit: a blow lands over several frames (a blast heats, a rock shoves). */
		double PendingDamage = 0.0;
		double PendingAgeS = 0.0;
		double QuietS = 0.0;
		double KnockoutS = 0.0;
		/** Just back up: cannot be hurt for this long. */
		double ProtectS = 0.0;
		/** Which way it fell (unit, horizontal) and where it stands again when it gets back up. */
		FVec3 FallDirection;
		FVec3 HomeCm;
		/** Thrown rocks: the sparring partner has seen it coming and decided what to do. */
		bool bThreatChecked = false;
	};

	enum class EProjectileKind : unsigned char
	{
		Fire,
		Air,
		Water,
		/** Flame resting on the ground, fed by a bender while held. */
		GroundFlame,
		/** Water flung off a whip's tip at the snap: a cloud of droplets that falls like water but slows quickly. */
		Spray,
		/** An ice dagger: flies nearly straight, strikes what it hits, shatters. */
		IceShard
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
		/** A released ground flame's fuel (J): the ground it lit, burning on its own. */
		double FuelJ = 0.0;
		/** Who bent it: 0 the player, 1 the sparring partner (a parried blast changes hands). */
		int Owner = 0;
		/** It struck a fighter (once is all a blast hurts). */
		bool bStruck = false;
		/** The sparring partner has seen it coming and decided what to do. */
		bool bThreatChecked = false;
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
		/** Jet Dash: time left and the velocity the fire jets hold. */
		double DashTimeS = 0.0;
		double DashEmitTimerS = 0.0;
		FVec3 DashVelocityCmS;
		/** Riding an air scooter. */
		bool bScooter = false;

		/** Health: only the sparring partner's attacks hurt. */
		double Health = 100.0;
		double MaxHealth = 100.0;
		/** Guard held, and for how long (a parry in its first moments); a released guard can be raised again after GuardCooldownS. */
		bool bGuarding = false;
		double GuardTimeS = 0.0;
		double GuardCooldownS = 0.0;
		/** Rocked by a hit (slowed), knocked down (no control), just back up (cannot be hurt). */
		double FlinchS = 0.0;
		double DownS = 0.0;
		double ProtectS = 0.0;
		FVec3 FallDirection = FVec3(1.0, 0.0, 0.0);
		/** Rolling: time left, the roll's velocity, and when the next roll is allowed. Flames pass through a roll. */
		double DodgeS = 0.0;
		double DodgeCooldownS = 0.0;
		FVec3 DodgeVelocityCmS;
	};

	/** The sparring partner's states and attacks: its mind is the kernel's FSparringBrain, shared with Unreal. */
	using ERivalState = ESparringState;
	using ERivalAttack = ESparringAttack;

	/** A firebender who spars with the player: its body in the simulation, its mind an FSparringBrain. */
	struct FRival
	{
		bool bEnabled = true;
		/** Its body in Bodies (EBodyKind::Rival), or -1. */
		int Body = -1;
		ETechniqueElement Element = ETechniqueElement::Fire;
		FSparringBrain Brain;
		/** The hand it attacks from, and its walk cycle, for the renderer. */
		FVec3 HandCm;
		double StridePhase = 0.0;
	};


	/** A tornado spun up by an airbender. */
	struct FTornado
	{
		FVec3 CenterCm;
		double AgeS = 0.0;
		double LifetimeS = 0.0;
		double EmitTimerS = 0.0;
		double EmitAngleRad = 0.0;
		/** Flame caught in it (kg): a fire tornado. */
		double FireKg = 0.0;
		bool bActive = false;
	};

	/** Presentation-only events (a technique's moment), alongside the simulation's reactions. */
	enum class ESandboxEffect : unsigned char
	{
		None,
		IceShatter,
		Quake,
		FireRing,
		FireJet,
		TornadoEnd,
		/** A prop caught fire (MassKg: its EArenaProp). */
		Ignite,
		/** Water put a burning prop out (MassKg: its EArenaProp). */
		Douse,
		/** A prop burnt away (MassKg: its EArenaProp). */
		BurntOut,
		/** A prop smashed (MassKg: its EArenaProp; EnergyJ: water it spilled, kg). */
		Smash,
		/** A dummy took damage (EnergyJ: damage this frame). */
		Hit,
		/** A dummy's health ran out: it falls over. */
		Knockout,
		/** A knocked-out dummy stands up again, whole. */
		Respawn,
		/** The sparring partner's attack struck the player (EnergyJ: damage). */
		PlayerHit,
		/** The player's guard took an attack (EnergyJ: damage let through). */
		Blocked,
		/** A perfect guard sent an attack back. */
		Parried,
		/** The player was knocked down. */
		PlayerDown,
		/** A duel began. */
		DuelStart,
		/** A duel ended (MassKg: 1 the player won, 2 the sparring partner won, 0 called off). */
		DuelEnd,
		/** The sparring partner began an attack (MassKg: its ERivalAttack). */
		RivalWindUp,
		/** The sparring partner guarded or dodged (MassKg: 1 guard, 2 dodge). */
		RivalEvade,
		/** The player rolled out of the way. */
		PlayerDodge
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
		ESandboxEffect Effect = ESandboxEffect::None;
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
		FRival Rival;
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
		bool bPreviousDodge = false;
		/** This move created the whip (its active frame does not lash yet). */
		bool bWhipCreatedThisMove = false;
	public:
		FTornado Tornado;
	private:
		void UpdateTornado(double Dt);
		void AddEffect(ESandboxEffect Effect, const FVec3& LocationCm, double EnergyJ);
		/** Pushes a body as if struck: J (kg*cm/s) through its simulated volume, so every kind of body reacts its own way. */
		void PushBody(int Index, const FVec3& ImpulseKgCmS);
		/** Props catching fire, burning, being put out, burning away. */
		void UpdateCombustion(double Dt);
		/** Smashes what was hit hard enough this frame. */
		void ProcessBreaks();
		void SmashBody(int Index);
		/** Where a prop is heated and doused, and where its flame burns. */
		void GetCombustionPoints(const FBody& Body, FVec3& OutCenterCm, FVec3& OutFlameCm) const;
		void AddPropEffect(ESandboxEffect Effect, const FBody& Body, const FVec3& LocationCm, double EnergyJ);
		/** A barrel within reach holding enough water for a whip, or -1. */
		int FindWaterBarrel(const FVec3& FromCm, double MinWaterKg, FVec3& OutSourceCm) const;
	public:
		static constexpr double DummyMaxHealth = 100.0;
		/** Damage from a hit, per m/s it changes a dummy's speed. */
		static constexpr double DamagePerMs = 15.0;
		static constexpr double KnockoutSeconds = 6.0;
		/**
		 * Damages a training dummy or the sparring partner (others ignore it); FromDirection is the way the blow
		 * travelled. bKnockOut knocks it out whatever its health (burnt through, thrown off the field).
		 */
		void DamageBody(int Index, double Amount, const FVec3& FromDirection, bool bKnockOut = false);
		static constexpr double PlayerDownSeconds = 3.0;
		/** A guard raised this recently parries: the attack flies back at whoever sent it. */
		static constexpr double ParryWindowS = 0.2;
		/** A guard lets this share of an attack's damage through, and each block costs stamina. */
		static constexpr double BlockDamageScale = 0.2;
		static constexpr double BlockStamina = 10.0;
		/** Damage from a flame striking a fighter: FlameStrikeDamage * (mass / 0.6 kg)^1.5. */
		static constexpr double FlameStrikeDamage = 12.0;
		/** A roll lasts this long, covers about DodgeSpeedCmS * DodgeSeconds, costs stamina, and flames pass through it. */
		static constexpr double DodgeSeconds = 0.36;
		static constexpr double DodgeSpeedCmS = 850.0;
		static constexpr double DodgeStamina = 20.0;
		static constexpr double DodgeCooldownSeconds = 0.55;
		/** True while a duel is on (from the bow to a knockout). */
		bool IsDuelActive() const;
	private:
		/** Reports the frame's damage as hits, and stands knocked-out dummies back up. */
		void UpdateDummies(double Dt);
		void SpawnRival();
		/** The sparring partner: what it sees goes to its brain, and its orders move its body and bend its flames. */
		void UpdateRival(double Dt);
		/** Flames striking fighters: the sparring partner's at the player (guard, parry), the player's at the sparring partner. */
		void UpdateFighterHits();
		void UpdateGuard(const FInput& Input, double Dt);
		void RivalShoot(const FSparringShot& Shot);
		bool RivalHasClearShot(const FVec3& FeetCm) const;
		/** Messages and effects for what the sparring partner's brain reports. */
		void HandleRivalEvents(const FSparringEvents& Events);
		void HurtPlayer(double Damage, const FVec3& Direction, double KnockbackCmS, bool bBlocked = false);
		FVec3 GetRivalFeetCm() const;
		FVec3 GetRivalChestCm() const;
		/** Ground flame fed by the current hold. */
		int HoldProjectile = -1;
		/** Where Raise / Lower Ground works, fixed when the hold starts. */
		FVec3 HoldCenterCm;
		double EarthWorkMovedKg = 0.0;
		double EarthWorkJ = 0.0;
		double LastNoChiMessageS = -10.0;
	};
}
