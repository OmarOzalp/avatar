#include "Sim/BendingSparring.h"

namespace BendingSim
{
	namespace
	{
		FVec3 Horizontal(const FVec3& V) { return FVec3(V.X, V.Y, 0.0); }

		double WrapRadians(double Angle)
		{
			while (Angle > Pi)
			{
				Angle -= 2.0 * Pi;
			}
			while (Angle < -Pi)
			{
				Angle += 2.0 * Pi;
			}
			return Angle;
		}

		/** A long blow (a blast that keeps pushing) is still one blow after this long. */
		constexpr double MaxBlowS = 0.5;
	}

	void FSparringBrain::Reset(const FVec3& InPostCm, double InYawRad)
	{
		const FSparringTuning KeptTuning = Tuning;
		*this = FSparringBrain();
		Tuning = KeptTuning;
		PostCm = InPostCm;
		YawRad = InYawRad;
		Health = Tuning.MaxHealth;
	}

	double FSparringBrain::Random()
	{
		unsigned int X = Seed;
		X ^= X << 13;
		X ^= X >> 17;
		X ^= X << 5;
		Seed = X ? X : 0x2545F491u;
		return static_cast<double>(X & 0xFFFFFFu) / 16777216.0;
	}

	bool FSparringBrain::IsDuelActive() const
	{
		return State != ESparringState::Waiting && State != ESparringState::Returning && State != ESparringState::Down;
	}

	double FSparringBrain::GetStateProgress() const
	{
		return StateDurationS > 0.0 ? KClamp(StateTimeS / StateDurationS, 0.0, 1.0) : 0.0;
	}

	void FSparringBrain::SetState(ESparringState NewState, double DurationS)
	{
		State = NewState;
		StateTimeS = 0.0;
		StateDurationS = DurationS;
	}

	void FSparringBrain::StartDuel(FSparringEvents& Out)
	{
		ChallengeDamage = 0.0;
		ShotsFired = 0;
		Attack = ESparringAttack::None;
		Health = Tuning.MaxHealth;
		BlowDamage = 0.0;
		// It bows first.
		SetState(ESparringState::Ready, Tuning.ReadyS);
		Out.bDuelStarted = true;
	}

	void FSparringBrain::EndDuel(int Winner, FSparringEvents& Out)
	{
		if (Winner == 1)
		{
			++PlayerWins;
		}
		else if (Winner == 2)
		{
			++PartnerWins;
		}
		Attack = ESparringAttack::None;
		ShotsLeft = 0;
		SetState(Winner == 1 ? ESparringState::Down : ESparringState::Returning, 0.0);
		Out.DuelEnded = Winner;
	}

	void FSparringBrain::OnPlayerDown(FSparringEvents& Out)
	{
		if (IsDuelActive())
		{
			EndDuel(2, Out);
		}
	}

	void FSparringBrain::OnBlocked()
	{
		StrafeSign = -StrafeSign;
	}

	double FSparringBrain::TakeDamage(double Amount, bool bKnockOut, FSparringEvents& Out)
	{
		if (Amount <= 0.0 || State == ESparringState::Down)
		{
			return 0.0;
		}
		if (bKnockOut)
		{
			Amount = Health;
		}
		else
		{
			// Waiting at its post, a real hit is a challenge, not damage.
			if (State == ESparringState::Waiting)
			{
				ChallengeDamage += Amount;
				if (ChallengeDamage >= Tuning.ChallengeDamage)
				{
					StartDuel(Out);
				}
				return 0.0;
			}
			if (State == ESparringState::Returning || State == ESparringState::Ready || ProtectS > 0.0)
			{
				return 0.0;
			}
			if (State == ESparringState::Guarding)
			{
				Amount *= Tuning.GuardDamageScale;
			}
			Amount = KMin(Amount, KMax(Tuning.BlowCap - BlowDamage, 0.0));
		}
		Amount = KMin(Amount, Health);
		if (Amount <= 0.0)
		{
			return 0.0;
		}
		Health -= Amount;
		BlowAgeS = BlowDamage > 0.0 ? BlowAgeS : 0.0;
		BlowDamage += Amount;
		BlowQuietS = 0.0;
		FrameHurt += Amount;
		if (Health > 1e-6)
		{
			return Amount;
		}
		Health = 0.0;
		DownS = Tuning.DownS;
		Out.bKnockedOut = true;
		EndDuel(1, Out);
		return Amount;
	}

	bool FSparringBrain::IsThreatening(const FSparringThreat& Threat, const FVec3& CenterCm) const
	{
		const FVec3 Relative = Threat.LocationCm - CenterCm;
		const double SpeedSquared = Threat.VelocityCmS.SizeSquared();
		const double ArriveS = SpeedSquared > 1.0 ? -Relative.Dot(Threat.VelocityCmS) / SpeedSquared : -1.0;
		if (ArriveS <= 0.0 || ArriveS > Tuning.ReactWindowS)
		{
			return false;
		}
		return (Relative + Threat.VelocityCmS * ArriveS).Size() <= Tuning.ThreatReachCm + 32.0;
	}

	void FSparringBrain::React(const FSparringThreat& Threat, const FVec3& CenterCm, FSparringEvents& Out)
	{
		if (!CanReact())
		{
			return;
		}
		const FVec3 Relative = Threat.LocationCm - CenterCm;
		const double SpeedSquared = Threat.VelocityCmS.SizeSquared();
		const double ArriveS = SpeedSquared > 1.0 ? -Relative.Dot(Threat.VelocityCmS) / SpeedSquared : 0.0;
		const FVec3 Passing = Relative + Threat.VelocityCmS * KMax(ArriveS, 0.0);
		const double Roll = Random();
		const double Guard = Threat.bHeavy ? Tuning.HeavyGuardChance : Tuning.GuardChance;
		const double Dodge = Threat.bHeavy ? Tuning.HeavyDodgeChance : Tuning.DodgeChance;
		if (Roll < Guard)
		{
			SetState(ESparringState::Guarding, Tuning.GuardS);
			Out.Evaded = 1;
		}
		else if (Roll < Guard + Dodge)
		{
			// Across the line of the throw, away from where it will pass.
			const FVec3 Across = FVec3(-Threat.VelocityCmS.Y, Threat.VelocityCmS.X, 0.0).GetSafeNormal();
			const FVec3 PassingFlat = Horizontal(Passing);
			const double Lean = PassingFlat.Size() > 10.0 ? -Across.Dot(PassingFlat) : StrafeSign;
			DodgeDirection = Lean >= 0.0 ? Across : Across * -1.0;
			SetState(ESparringState::Dodging, Tuning.DodgeS);
			Out.Evaded = 2;
		}
	}

	void FSparringBrain::Update(const FSparringSenses& Senses, double Dt, FSparringOrders& Out)
	{
		Out.DesiredVelocityCmS = FVec3();
		Out.bControl = Senses.bGrounded;
		Out.bShoot = false;
		Out.Shot = FSparringShot();
		if (Dt <= 0.0)
		{
			return;
		}
		StateTimeS += Dt;
		CooldownS = KMax(CooldownS - Dt, 0.0);
		StrafeTimerS -= Dt;
		ProtectS = KMax(ProtectS - Dt, 0.0);
		BlowQuietS += Dt;
		BlowAgeS += BlowDamage > 0.0 ? Dt : 0.0;
		if (BlowQuietS >= Tuning.BlowQuietS || BlowAgeS >= MaxBlowS)
		{
			BlowDamage = 0.0;
			BlowAgeS = 0.0;
		}
		const bool bTimeUp = StateDurationS > 0.0 && StateTimeS >= StateDurationS;
		const FVec3 ToPlayer = Horizontal(Senses.PlayerFeetCm - Senses.FeetCm);
		const double PlayerDistance = ToPlayer.Size();
		const FVec3 Forward(KCos(YawRad), KSin(YawRad), 0.0);
		const FVec3 ToPlayerDir = PlayerDistance > 1.0 ? ToPlayer / PlayerDistance : Forward;
		const FVec3 Side(-ToPlayerDir.Y, ToPlayerDir.X, 0.0);

		// A big hit rocks it, interrupting whatever it was doing (a guard holds).
		PoiseS = KMax(PoiseS - Dt, 0.0);
		const double Hurt = FrameHurt;
		FrameHurt = 0.0;
		if (Hurt >= Tuning.StaggerDamage && PoiseS <= 0.0 && (State == ESparringState::Moving || State == ESparringState::WindUp
			|| State == ESparringState::Attacking || State == ESparringState::Recovering || State == ESparringState::Dodging))
		{
			Attack = ESparringAttack::None;
			ShotsLeft = 0;
			PoiseS = Tuning.PoiseS;
			SetState(ESparringState::Staggered, Tuning.StaggerS);
		}

		FVec3 Desired;
		bool bFacePlayer = true;
		switch (State)
		{
		case ESparringState::Waiting:
		{
			const FVec3 ToPost = Horizontal(PostCm - Senses.FeetCm);
			if (ToPost.Size() > 40.0)
			{
				Desired = ToPost.GetSafeNormal() * Tuning.WalkCmS;
			}
			bFacePlayer = PlayerDistance < 1800.0;
			Health = KMin(Health + Tuning.RestHealPerS * Dt, Tuning.MaxHealth);
			break;
		}
		case ESparringState::Ready:
			if (bTimeUp)
			{
				SetState(ESparringState::Moving, 0.0);
				CooldownS = 0.4 + 0.5 * Random();
			}
			break;
		case ESparringState::Moving:
		{
			// Keep its distance: close in from far, back off from near, circle in between.
			if (PlayerDistance > Tuning.FarCm)
			{
				Desired = ToPlayerDir * (PlayerDistance > 1.6 * Tuning.FarCm ? Tuning.RunCmS : Tuning.WalkCmS);
			}
			else if (PlayerDistance < Tuning.NearCm)
			{
				Desired = ToPlayerDir * -Tuning.WalkCmS + Side * (StrafeSign * 150.0);
			}
			else
			{
				Desired = Side * (StrafeSign * 260.0);
			}
			if (StrafeTimerS <= 0.0)
			{
				StrafeSign = Random() < 0.5 ? -1.0 : 1.0;
				StrafeTimerS = 1.0 + 1.5 * Random();
			}
			if (Horizontal(Senses.PlayerFeetCm - PostCm).Size() > Tuning.LeashCm)
			{
				EndDuel(0, Out.Events);
				break;
			}
			if (CooldownS > 0.0 || PlayerDistance > Tuning.AttackRangeCm || Senses.bPlayerDown)
			{
				break;
			}
			// Only with a clear line to the player: something in the way makes it work round.
			if (!Senses.bClearShot)
			{
				CooldownS = 0.3;
				Desired = Desired + ToPlayerDir * 200.0;
				break;
			}
			Attack = PlayerDistance < Tuning.BurstRangeCm ? ESparringAttack::Burst : (Random() < Tuning.ComboChance ? ESparringAttack::Combo : ESparringAttack::Blast);
			SetState(ESparringState::WindUp, Attack == ESparringAttack::Burst ? Tuning.BurstWindUpS : (Attack == ESparringAttack::Combo ? Tuning.ComboWindUpS : Tuning.BlastWindUpS));
			Out.Events.bWindUp = true;
			break;
		}
		case ESparringState::WindUp:
			if (bTimeUp)
			{
				SetState(ESparringState::Attacking, 0.0);
				ShotsLeft = Attack == ESparringAttack::Combo ? 3 : 1;
				ShotTimerS = 0.0;
			}
			break;
		case ESparringState::Attacking:
			ShotTimerS -= Dt;
			if (ShotsLeft > 0 && ShotTimerS <= 0.0)
			{
				Out.bShoot = true;
				Out.Shot.Attack = Attack;
				Out.Shot.Side = ShotsFired % 2 == 0 ? 1.0 : -1.0;
				if (Attack == ESparringAttack::Combo)
				{
					Out.Shot.MassKg = Tuning.ComboMassKg;
					Out.Shot.SpeedMs = Tuning.ComboSpeedMs;
					Out.Shot.SpreadRad = (ShotsLeft - 2) * Tuning.ComboSpreadRad;
				}
				else if (Attack == ESparringAttack::Burst)
				{
					Out.Shot.MassKg = Tuning.BurstMassKg;
					Out.Shot.SpeedMs = Tuning.BurstSpeedMs;
				}
				else
				{
					Out.Shot.MassKg = Tuning.BlastMassKg;
					Out.Shot.SpeedMs = Tuning.BlastSpeedMs;
				}
				--ShotsLeft;
				++ShotsFired;
				ShotTimerS = Tuning.ShotIntervalS;
			}
			if (ShotsLeft <= 0 && ShotTimerS <= Tuning.ShotIntervalS * 0.5)
			{
				SetState(ESparringState::Recovering, Attack == ESparringAttack::Combo ? Tuning.ComboRecoveryS
					: (Attack == ESparringAttack::Burst ? Tuning.BurstRecoveryS : Tuning.BlastRecoveryS));
			}
			break;
		case ESparringState::Recovering:
			if (bTimeUp)
			{
				SetState(ESparringState::Moving, 0.0);
				Attack = ESparringAttack::None;
				CooldownS = Tuning.CooldownS + Tuning.CooldownSpreadS * Random();
			}
			break;
		case ESparringState::Guarding:
			if (bTimeUp)
			{
				// Out of a guard it answers quickly.
				SetState(ESparringState::Moving, 0.0);
				CooldownS = KMin(CooldownS, 0.3);
			}
			break;
		case ESparringState::Dodging:
			Desired = DodgeDirection * Tuning.DodgeCmS;
			if (bTimeUp)
			{
				SetState(ESparringState::Moving, 0.0);
			}
			break;
		case ESparringState::Staggered:
			Out.bControl = false;
			if (bTimeUp)
			{
				SetState(ESparringState::Moving, 0.0);
				CooldownS = KMax(CooldownS, 0.4);
			}
			break;
		case ESparringState::Down:
			Out.bControl = false;
			bFacePlayer = false;
			DownS -= Dt;
			if (DownS <= 0.0)
			{
				// Up again, whole; it bows and walks back to its post.
				DownS = 0.0;
				Health = Tuning.MaxHealth;
				ProtectS = Tuning.ProtectS;
				SetState(ESparringState::Returning, 0.0);
				Out.Events.bGotUp = true;
			}
			break;
		case ESparringState::Returning:
			if (StateTimeS > Tuning.BowS)
			{
				const FVec3 ToPost = Horizontal(PostCm - Senses.FeetCm);
				if (ToPost.Size() > 40.0)
				{
					Desired = ToPost.GetSafeNormal() * Tuning.WalkCmS;
					bFacePlayer = false;
				}
				else
				{
					SetState(ESparringState::Waiting, 0.0);
				}
			}
			Health = KMin(Health + Tuning.RestHealPerS * Dt, Tuning.MaxHealth);
			break;
		}
		Out.DesiredVelocityCmS = Desired;

		if (State != ESparringState::Down && State != ESparringState::Staggered)
		{
			const double TargetYaw = bFacePlayer ? KAtan2(ToPlayerDir.Y, ToPlayerDir.X) : (Desired.Size() > 10.0 ? KAtan2(Desired.Y, Desired.X) : YawRad);
			const double MaxTurn = (State == ESparringState::WindUp ? 14.0 : 8.0) * Dt;
			YawRad = WrapRadians(YawRad + KClamp(WrapRadians(TargetYaw - YawRad), -MaxTurn, MaxTurn));
		}
	}
}
