#pragma once

#include "Sim/BendingSimMath.h"

/**
 * The sparring partner's mind: a firebender who duels the player in the training ground. Engine-free, so the
 * browser sandbox and the Unreal actor run the same brain. The owner feeds it what it sees each frame (FSparringSenses)
 * and the damage that reaches its body (TakeDamage), and carries out its orders (FSparringOrders): where to move,
 * where to face, and which flame to bend. Its body lives in the owner's physics; blasts that shove it and fire that
 * burns it are the owner's to measure.
 *
 *  Waiting   at its post, hands behind its back; a real hit is a challenge, not damage.
 *  Ready     the bow before the first exchange.
 *  Moving    keeps its distance: closes in, backs off, circles; attacks when ready and the line to the player is clear.
 *  WindUp    the telegraph (a glowing fist): the opening to strike first or raise a guard.
 *  Attacking a blast, a three-blast combo, or up close a ring of fire.
 *  Recovering committed: no guard, no dodge. The opening to punish.
 *  Guarding / Dodging  decided once per throw coming at it, and only while Moving.
 *  Staggered a big hit interrupts it; then a moment of poise so it cannot be stun-locked.
 *  Down / Returning  knocked out, it gets up whole, bows and walks back to its post.
 */
namespace BendingSim
{
	enum class ESparringState : unsigned char
	{
		Waiting,
		Ready,
		Moving,
		WindUp,
		Attacking,
		Recovering,
		Guarding,
		Dodging,
		Staggered,
		Down,
		Returning
	};

	enum class ESparringAttack : unsigned char
	{
		None,
		/** One heavy fire blast. */
		Blast,
		/** Three quick blasts, alternating hands. */
		Combo,
		/** Up close: a ring of fire bursting outward. */
		Burst
	};

	struct FSparringTuning
	{
		double MaxHealth = 100.0;
		double WalkCmS = 340.0;
		double RunCmS = 560.0;
		double DodgeCmS = 720.0;
		double AccelerationCmS2 = 2600.0;
		/** It keeps between these distances from the player, circling in between. */
		double NearCm = 480.0;
		double FarCm = 950.0;
		double AttackRangeCm = 1500.0;
		/** Closer than this it answers with a burst of fire instead of a blast. */
		double BurstRangeCm = 330.0;
		/** A duel is called off when the player goes this far from the post. */
		double LeashCm = 3500.0;
		double ReadyS = 0.9;
		/** After a duel: the bow before it walks back. */
		double BowS = 1.0;
		double BlastWindUpS = 0.5;
		double ComboWindUpS = 0.38;
		double BurstWindUpS = 0.32;
		double BlastRecoveryS = 0.45;
		double ComboRecoveryS = 0.6;
		double BurstRecoveryS = 0.55;
		double ShotIntervalS = 0.2;
		/** Between attacks: CooldownS + CooldownSpreadS * random. */
		double CooldownS = 1.1;
		double CooldownSpreadS = 1.3;
		double ComboChance = 0.35;
		/** Against a flame, a gust or water; against a thrown rock (heavy). */
		double GuardChance = 0.4;
		double DodgeChance = 0.25;
		double HeavyGuardChance = 0.15;
		double HeavyDodgeChance = 0.55;
		/** A throw is considered when it will pass within this of the body within ReactWindowS. */
		double ReactWindowS = 0.6;
		double ThreatReachCm = 130.0;
		double GuardS = 0.55;
		double DodgeS = 0.32;
		double GuardDamageScale = 0.25;
		/** Damage in one hit that staggers it, for how long, and the poise after. */
		double StaggerDamage = 8.0;
		double StaggerS = 0.45;
		double PoiseS = 1.6;
		/** At most this much damage from one blow (a boulder staggers it rather than ending the duel). */
		double BlowCap = 40.0;
		/** A blow is over when no damage has arrived for this long. */
		double BlowQuietS = 0.12;
		/** Damage while waiting that counts as a challenge. */
		double ChallengeDamage = 3.0;
		double DownS = 4.0;
		/** Back on its feet it cannot be hurt for this long. */
		double ProtectS = 1.5;
		double RestHealPerS = 25.0;
		double BlastMassKg = 0.6;
		double BlastSpeedMs = 17.0;
		double ComboMassKg = 0.4;
		double ComboSpeedMs = 19.0;
		double ComboSpreadRad = 0.06;
		double BurstMassKg = 0.3;
		double BurstSpeedMs = 11.0;
		int BurstFlames = 10;
		double FlameTemperatureK = 1450.0;
	};

	/** What it sees this frame (cm, Z up). */
	struct FSparringSenses
	{
		FVec3 FeetCm;
		FVec3 VelocityCmS;
		bool bGrounded = true;
		FVec3 PlayerFeetCm;
		FVec3 PlayerVelocityCmS;
		bool bPlayerDown = false;
		/** Nothing between its hand and the player's chest (terrain, solid props). Only read when it wants to attack. */
		bool bClearShot = true;
	};

	/** Something thrown at it, as the owner sees it. */
	struct FSparringThreat
	{
		FVec3 LocationCm;
		FVec3 VelocityCmS;
		/** A thrown rock: better dodged than guarded. */
		bool bHeavy = false;
	};

	/** One flame to bend this frame: aimed at the player's chest, turned by SpreadRad, from the hand on Side (+1 right). */
	struct FSparringShot
	{
		ESparringAttack Attack = ESparringAttack::None;
		double MassKg = 0.0;
		double SpeedMs = 0.0;
		double SpreadRad = 0.0;
		double Side = 1.0;
	};

	/** What happened, for messages and effects. */
	struct FSparringEvents
	{
		bool bDuelStarted = false;
		/** -1 none; 0 called off; 1 the player won; 2 the sparring partner won. */
		int DuelEnded = -1;
		bool bKnockedOut = false;
		bool bGotUp = false;
		bool bWindUp = false;
		/** 1 it guarded, 2 it sidestepped. */
		int Evaded = 0;
	};

	struct FSparringOrders
	{
		/** Horizontal velocity to steer toward, at Tuning.AccelerationCmS2 (when bControl). */
		FVec3 DesiredVelocityCmS;
		/** False while it is knocked about: let its body's physics carry it. */
		bool bControl = true;
		/** Bend this flame now (a burst: Tuning.BurstFlames flames in a ring). */
		bool bShoot = false;
		FSparringShot Shot;
		FSparringEvents Events;
	};

	class BENDINGSIM_API FSparringBrain
	{
	public:
		FSparringTuning Tuning;
		ESparringState State = ESparringState::Waiting;
		ESparringAttack Attack = ESparringAttack::None;
		double StateTimeS = 0.0;
		/** How long the current state lasts (0: until something happens). */
		double StateDurationS = 0.0;
		double CooldownS = 0.0;
		int ShotsLeft = 0;
		int ShotsFired = 0;
		double ShotTimerS = 0.0;
		double StrafeSign = 1.0;
		double StrafeTimerS = 0.0;
		double PoiseS = 0.0;
		double DownS = 0.0;
		double ProtectS = 0.0;
		FVec3 DodgeDirection;
		/** Where it waits (feet) and which way it faces. */
		FVec3 PostCm;
		double YawRad = 0.0;
		double Health = 100.0;
		int PlayerWins = 0;
		int PartnerWins = 0;

		void Reset(const FVec3& InPostCm, double InYawRad);

		/** Decides this frame: movement, facing, attacks. */
		void Update(const FSparringSenses& Senses, double Dt, FSparringOrders& Out);

		/** The owner found the way it wants to move blocked (a wall, a pond, the edge): it turns the other way. */
		void OnBlocked();

		/** It may react to a throw now (only while Moving); each throw should be offered once. */
		bool CanReact() const { return State == ESparringState::Moving; }
		/** True if the throw will pass close within the reaction window (offer it to React then, and mark it seen). */
		bool IsThreatening(const FSparringThreat& Threat, const FVec3& CenterCm) const;
		/** A throw is coming: guard, sidestep or take it. */
		void React(const FSparringThreat& Threat, const FVec3& CenterCm, FSparringEvents& Out);

		/** Damage reaching its body; returns what it takes (none while waiting, which challenges it, bowing, down or
		 * just up; a quarter while guarding; at most BlowCap per blow and the health left). bKnockOut takes it all. */
		double TakeDamage(double Amount, bool bKnockOut, FSparringEvents& Out);

		/** The player was knocked down: the duel is the sparring partner's. */
		void OnPlayerDown(FSparringEvents& Out);

		bool IsDuelActive() const;
		bool IsDown() const { return State == ESparringState::Down; }
		/** 0..1 through the current state (0 when it has no fixed length). */
		double GetStateProgress() const;

		/** 0..1, deterministic. */
		double Random();

	private:
		void SetState(ESparringState NewState, double DurationS);
		void StartDuel(FSparringEvents& Out);
		void EndDuel(int Winner, FSparringEvents& Out);

		double FrameHurt = 0.0;
		double BlowDamage = 0.0;
		double BlowQuietS = 0.0;
		double BlowAgeS = 0.0;
		double ChallengeDamage = 0.0;
		unsigned int Seed = 0x2545F491u;
	};
}
