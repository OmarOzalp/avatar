// Sparring partner brain checks: the duel's flow, attacks, reactions, damage rules. The brain alone, fed senses by
// hand the way the browser sandbox and the Unreal actor feed it. Build and run with Tests/run_all.sh.

#include "SimTestHarness.h"
#include "Sim/BendingSparring.h"

#include <cstdio>

using namespace BendingSim;

namespace
{
	constexpr double Dt = 1.0 / 60.0;

	/** A partner at the origin facing +X, the player 7 m away, both standing still on open ground. */
	struct FDuel
	{
		FSparringBrain Brain;
		FSparringSenses Senses;
		FSparringEvents Seen;
		int Shots = 0;
		int Bursts = 0;

		FDuel()
		{
			Brain.Reset(FVec3(), 0.0);
			Senses.FeetCm = FVec3();
			Senses.PlayerFeetCm = FVec3(700.0, 0.0, 0.0);
		}

		void Collect(const FSparringEvents& Events)
		{
			Seen.bDuelStarted = Seen.bDuelStarted || Events.bDuelStarted;
			Seen.DuelEnded = Events.DuelEnded >= 0 ? Events.DuelEnded : Seen.DuelEnded;
			Seen.bKnockedOut = Seen.bKnockedOut || Events.bKnockedOut;
			Seen.bGotUp = Seen.bGotUp || Events.bGotUp;
			Seen.bWindUp = Seen.bWindUp || Events.bWindUp;
			Seen.Evaded = Events.Evaded ? Events.Evaded : Seen.Evaded;
		}

		/** Steps the brain; its feet follow its orders (no physics here). */
		void Run(double Seconds)
		{
			const int Frames = static_cast<int>(Seconds * 60.0 + 0.5);
			for (int Frame = 0; Frame < Frames; ++Frame)
			{
				FSparringOrders Orders;
				Brain.Update(Senses, Dt, Orders);
				Collect(Orders.Events);
				if (Orders.bControl)
				{
					Senses.FeetCm += Orders.DesiredVelocityCmS * Dt;
				}
				if (Orders.bShoot)
				{
					++Shots;
					Bursts += Orders.Shot.Attack == ESparringAttack::Burst ? 1 : 0;
				}
			}
		}

		double Hit(double Amount)
		{
			FSparringEvents Events;
			const double Taken = Brain.TakeDamage(Amount, false, Events);
			Collect(Events);
			return Taken;
		}

		/** Challenges and waits out the bow. */
		void Start()
		{
			Hit(10.0);
			Run(Brain.Tuning.ReadyS + 0.05);
		}
	};

	void WaitsUntilChallenged()
	{
		SimTest::Section("Waits at its post until challenged");
		FDuel Duel;
		Duel.Run(5.0);
		ExpectTrue("left alone, it waits", Duel.Brain.State == ESparringState::Waiting && Duel.Shots == 0);
		ExpectTrue("and stays at its post", Duel.Senses.FeetCm.Size() < 1.0);
		ExpectNear("it turns to face the player", Duel.Brain.YawRad, 0.0, 1e-9);
		ExpectTrue("a scratch is not a challenge", Duel.Hit(1.0) == 0.0 && Duel.Brain.State == ESparringState::Waiting);
		ExpectTrue("a real hit is: it takes no damage, it bows", Duel.Hit(10.0) == 0.0 && Duel.Brain.State == ESparringState::Ready && Duel.Seen.bDuelStarted);
		ExpectTrue("hits during the bow do nothing", Duel.Hit(30.0) == 0.0 && Duel.Brain.Health == Duel.Brain.Tuning.MaxHealth);
		Duel.Run(Duel.Brain.Tuning.ReadyS + 0.05);
		ExpectTrue("after the bow it fights", Duel.Brain.State == ESparringState::Moving && Duel.Brain.IsDuelActive());
	}

	void AttacksWithClearShot()
	{
		SimTest::Section("Attacks when it has a clear shot, telegraphs first");
		FDuel Duel;
		Duel.Start();
		Duel.Run(4.0);
		std::printf("    in 4 s: %d flames\n", Duel.Shots);
		ExpectTrue("it winds up before it attacks", Duel.Seen.bWindUp);
		ExpectTrue("it attacks", Duel.Shots > 0);
		ExpectTrue("from 7 m: blasts, not bursts", Duel.Bursts == 0);
		const double Distance = (Duel.Senses.PlayerFeetCm - Duel.Senses.FeetCm).Size();
		ExpectTrue("it keeps its distance", Distance > Duel.Brain.Tuning.NearCm - 60.0 && Distance < Duel.Brain.Tuning.FarCm + 60.0);

		FDuel Blocked;
		Blocked.Senses.bClearShot = false;
		Blocked.Start();
		Blocked.Run(6.0);
		ExpectTrue("with something in the way it does not attack", Blocked.Shots == 0 && !Blocked.Seen.bWindUp);

		FDuel Close;
		Close.Senses.PlayerFeetCm = FVec3(250.0, 0.0, 0.0);
		Close.Start();
		for (int Frame = 0; Frame < 600 && Close.Shots == 0; ++Frame)
		{
			// Hold the player at arm's length while it backs off.
			Close.Senses.PlayerFeetCm = Close.Senses.FeetCm + FVec3(250.0, 0.0, 0.0);
			Close.Run(Dt);
		}
		ExpectTrue("up close it bursts", Close.Bursts > 0);

		FDuel Down;
		Down.Senses.bPlayerDown = true;
		Down.Start();
		Down.Run(5.0);
		ExpectTrue("it never strikes a player who is down", Down.Shots == 0);
	}

	void DamageRules()
	{
		SimTest::Section("Damage: guard, the per-blow cap, stagger and poise");
		FDuel Duel;
		Duel.Start();
		ExpectNear("one blow takes at most the cap", Duel.Hit(90.0), Duel.Brain.Tuning.BlowCap, 1e-9);
		ExpectNear("more in the same blow takes nothing", Duel.Hit(20.0), 0.0, 1e-9);
		Duel.Run(Dt);
		ExpectTrue("a big hit staggers it", Duel.Brain.State == ESparringState::Staggered);
		Duel.Run(Duel.Brain.Tuning.StaggerS + 0.1);
		Duel.Hit(12.0);
		Duel.Run(Dt);
		ExpectTrue("but not again straight away (poise)", Duel.Brain.State != ESparringState::Staggered);

		FDuel Guard;
		Guard.Start();
		FSparringThreat Threat;
		Threat.LocationCm = FVec3(300.0, 0.0, 90.0);
		Threat.VelocityCmS = FVec3(-1800.0, 0.0, 0.0);
		const FVec3 Center(0.0, 0.0, 90.0);
		ExpectTrue("a blast coming straight at it is a threat", Guard.Brain.IsThreatening(Threat, Center));
		FSparringThreat Wide = Threat;
		Wide.LocationCm.Y = 600.0;
		ExpectTrue("one passing wide is not", !Guard.Brain.IsThreatening(Wide, Center));
		FSparringThreat Away = Threat;
		Away.VelocityCmS = FVec3(1800.0, 0.0, 0.0);
		ExpectTrue("nor one flying away", !Guard.Brain.IsThreatening(Away, Center));
		int Guards = 0, Dodges = 0, Takes = 0, GuardedWrong = 0;
		for (int Trial = 0; Trial < 200; ++Trial)
		{
			FDuel Fresh;
			Fresh.Start();
			for (int Skip = 0; Skip < Trial; ++Skip)
			{
				Fresh.Brain.Random();
			}
			FSparringEvents Events;
			Fresh.Brain.React(Threat, Center, Events);
			Guards += Events.Evaded == 1 ? 1 : 0;
			Dodges += Events.Evaded == 2 ? 1 : 0;
			Takes += Events.Evaded == 0 ? 1 : 0;
			if (Events.Evaded == 1 && Fresh.Hit(20.0) != 20.0 * Fresh.Brain.Tuning.GuardDamageScale)
			{
				++GuardedWrong;
			}
		}
		ExpectTrue("guarding, it takes a quarter of the damage", Guards > 0 && GuardedWrong == 0);
		std::printf("    200 blasts: %d guarded, %d dodged, %d taken\n", Guards, Dodges, Takes);
		ExpectTrue("it guards some, dodges some, takes some", Guards > 40 && Dodges > 20 && Takes > 40);
	}

	void KnockoutAndReturn()
	{
		SimTest::Section("Knocked out, it gets up, bows and walks back");
		FDuel Duel;
		Duel.Start();
		Duel.Run(1.0);
		const FVec3 Away = Duel.Senses.FeetCm;
		for (int Blow = 0; Blow < 5 && !Duel.Brain.IsDown(); ++Blow)
		{
			Duel.Hit(30.0);
			Duel.Run(0.7);
		}
		ExpectTrue("knocked out", Duel.Seen.bKnockedOut && Duel.Seen.DuelEnded == 1 && Duel.Brain.PlayerWins == 1);
		ExpectTrue("down, it takes no more damage", Duel.Hit(30.0) == 0.0);
		Duel.Run(Duel.Brain.Tuning.DownS + 0.1);
		ExpectTrue("it gets up whole", Duel.Seen.bGotUp && Duel.Brain.Health == Duel.Brain.Tuning.MaxHealth && Duel.Brain.State == ESparringState::Returning);
		ExpectTrue("just up, it cannot be hurt", Duel.Hit(20.0) == 0.0);
		Duel.Run(8.0);
		ExpectTrue("and walks back to its post", Duel.Brain.State == ESparringState::Waiting && Duel.Senses.FeetCm.Size() < 60.0);
		std::printf("    it had wandered %.0f cm from its post\n", Away.Size());

		FDuel Lose;
		Lose.Start();
		FSparringEvents Events;
		Lose.Brain.OnPlayerDown(Events);
		ExpectTrue("the player down: the partner wins", Events.DuelEnded == 2 && Lose.Brain.PartnerWins == 1 && !Lose.Brain.IsDuelActive());

		FDuel Leave;
		Leave.Start();
		Leave.Senses.PlayerFeetCm = FVec3(Leave.Brain.Tuning.LeashCm + 500.0, 0.0, 0.0);
		Leave.Run(Dt);
		ExpectTrue("the player leaving the ring calls it off", Leave.Seen.DuelEnded == 0 && Leave.Brain.State == ESparringState::Returning);
	}

	void Deterministic()
	{
		SimTest::Section("Deterministic");
		FDuel A, B;
		A.Start();
		B.Start();
		A.Run(20.0);
		B.Run(20.0);
		ExpectTrue("two duels from the same start play out the same", A.Shots == B.Shots && A.Senses.FeetCm.X == B.Senses.FeetCm.X
			&& A.Senses.FeetCm.Y == B.Senses.FeetCm.Y && A.Brain.YawRad == B.Brain.YawRad);
	}
}

int main()
{
	WaitsUntilChallenged();
	AttacksWithClearShot();
	DamageRules();
	KnockoutAndReturn();
	Deterministic();
	return SimTest::Finish("Sparring partner");
}
