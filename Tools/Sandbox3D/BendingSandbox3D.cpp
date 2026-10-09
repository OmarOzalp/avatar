#include "BendingSandbox3D.h"

namespace BendingSandbox3D
{
	namespace
	{
		constexpr double FramesPerSecond = 60.0;
		constexpr double InputBufferS = 8.0 / FramesPerSecond;
		constexpr double GravityCmS2 = 980.665;

		// Character movement (Unreal's Character Movement defaults where they exist).
		constexpr double WalkSpeedCmS = 500.0;
		constexpr double SprintSpeedCmS = 800.0;
		constexpr double CastingSpeedScale = 0.6;
		constexpr double MaxAccelerationCmS2 = 2048.0;
		constexpr double BrakingCmS2 = 2048.0;
		constexpr double AirControl = 0.35;
		constexpr double JumpSpeedCmS = 480.0;
		constexpr double MaxStepCm = 45.0;
		constexpr double WalkableNormalZ = 0.64;
		constexpr double TurnRateRadS = 12.0;

		constexpr double ChestHeightCm = 135.0;
		constexpr double FlameSpecificHeat = 1005.0;
		/** A brazier's fuel: what a 0.5 kg flame at 1300 K loses to the air (80 W/m^2K over its ~7 m^2). */
		constexpr double BrazierPowerW = 6.0e5;
		constexpr double RelightRadiusCm = 150.0;
		/** A waterbender can draw a whip from a barrel this close to the hand. */
		constexpr double BarrelDrawRangeCm = 500.0;
		/** Top of a barrel's water above the ground. */
		constexpr double BarrelWaterHeightCm = 80.0;
		/** A smashed barrel's water flies out in this many parcels. */
		constexpr int SpillParcels = 6;
		constexpr double BrazierPedestalRadiusCm = 30.0;
		constexpr double BrazierPedestalHeightCm = 100.0;
		constexpr double MinWallDistanceCm = 260.0;
		constexpr double GroundFlameFollowCmS = 400.0;

		// The sparring partner's body: the size of the player (its behaviour and tuning are FSparringBrain's).
		constexpr double RivalRadiusCm = 32.0;
		constexpr double RivalHalfHeightCm = 58.0;
		constexpr double RivalMassKg = 70.0;
		constexpr double RivalHandHeightCm = 128.0;
		/** Lingering fire (burning ground, a ground flame, a burning prop) hurts it: J of heating per point of damage. */
		constexpr double RivalHeatJPerDamage = 25000.0;
		/** Out of a duel, health comes back this fast (per second). */
		constexpr double RestHealPerS = 25.0;
		constexpr double GuardSpeedScale = 0.35;
		/** A released guard can be raised again after this long (so it cannot be tapped into a constant parry). */
		constexpr double GuardRecastS = 0.3;
		/** A flame striking the player shoves them this hard. */
		constexpr double FlameKnockbackCmS = 330.0;

		// ---------------------------------------------------------------- Text (no libc here)

		int Append(char* Out, int Length, int Capacity, const char* Text)
		{
			while (*Text && Length < Capacity - 1)
			{
				Out[Length++] = *Text++;
			}
			Out[Length] = '\0';
			return Length;
		}

		int AppendNumber(char* Out, int Length, int Capacity, double Value, int Decimals)
		{
			if (Value < 0.0)
			{
				Length = Append(Out, Length, Capacity, "-");
				Value = -Value;
			}
			double Scale = 1.0;
			for (int Index = 0; Index < Decimals; ++Index)
			{
				Scale *= 10.0;
			}
			const long long Scaled = static_cast<long long>(Value * Scale + 0.5);
			const long long Whole = Scaled / static_cast<long long>(Scale);
			long long Fraction = Scaled % static_cast<long long>(Scale);

			char Digits[24];
			int Count = 0;
			long long W = Whole;
			do
			{
				Digits[Count++] = static_cast<char>('0' + W % 10);
				W /= 10;
			} while (W > 0 && Count < 20);
			while (Count > 0 && Length < Capacity - 1)
			{
				Out[Length++] = Digits[--Count];
			}
			if (Decimals > 0 && Length < Capacity - 1)
			{
				Out[Length++] = '.';
				char FractionDigits[12];
				for (int Index = Decimals - 1; Index >= 0; --Index)
				{
					FractionDigits[Index] = static_cast<char>('0' + Fraction % 10);
					Fraction /= 10;
				}
				for (int Index = 0; Index < Decimals && Length < Capacity - 1; ++Index)
				{
					Out[Length++] = FractionDigits[Index];
				}
			}
			Out[Length] = '\0';
			return Length;
		}

		const char* ElementName(ETechniqueElement Element)
		{
			switch (Element)
			{
			case ETechniqueElement::Earth: return "Earth";
			case ETechniqueElement::Water: return "Water";
			case ETechniqueElement::Fire:  return "Fire";
			case ETechniqueElement::Air:   return "Air";
			default:                       return "None";
			}
		}

		// ---------------------------------------------------------------- Math helpers

		FVec3 Flat(const FVec3& V) { return FVec3(V.X, V.Y, 0.0); }

		FVec3 MoveToward(const FVec3& Current, const FVec3& Target, double MaxDelta)
		{
			const FVec3 Delta = Target - Current;
			const double Length = Delta.Size();
			return Length <= MaxDelta || Length <= SmallNumber ? Target : Current + Delta * (MaxDelta / Length);
		}

		double WrapAngle(double Angle)
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

		double TurnToward(double Current, double Target, double MaxStep)
		{
			const double Delta = WrapAngle(Target - Current);
			return WrapAngle(Current + KClamp(Delta, -MaxStep, MaxStep));
		}

		/** q = q + 0.5 * (omega, 0) * q * dt, renormalized. */
		void IntegrateRotation(double* Q, const FVec3& Omega, double Dt)
		{
			const double X = Q[0], Y = Q[1], Z = Q[2], W = Q[3];
			const double Hx = 0.5 * Dt * Omega.X, Hy = 0.5 * Dt * Omega.Y, Hz = 0.5 * Dt * Omega.Z;
			Q[0] = X + Hx * W + Hy * Z - Hz * Y;
			Q[1] = Y - Hx * Z + Hy * W + Hz * X;
			Q[2] = Z + Hx * Y - Hy * X + Hz * W;
			Q[3] = W - Hx * X - Hy * Y - Hz * Z;
			const double Length = KSqrt(Q[0] * Q[0] + Q[1] * Q[1] + Q[2] * Q[2] + Q[3] * Q[3]);
			for (int Index = 0; Index < 4; ++Index)
			{
				Q[Index] /= Length > SmallNumber ? Length : 1.0;
			}
		}

		/** Closest points between two vertical-or-point cores (capsules and spheres). */
		void BodyCore(const FBody& Body, FVec3& OutA, FVec3& OutB)
		{
			if (Body.Prop == EArenaProp::Brazier && Body.Kind == EBodyKind::Prop)
			{
				const double HalfCore = 0.5 * BrazierPedestalHeightCm - BrazierPedestalRadiusCm;
				const FVec3 Center = Body.AnchorCm + FVec3(0.0, 0.0, 0.5 * BrazierPedestalHeightCm);
				OutA = Center - FVec3(0.0, 0.0, HalfCore);
				OutB = Center + FVec3(0.0, 0.0, HalfCore);
				return;
			}
			OutA = Body.LocationCm - FVec3(0.0, 0.0, Body.HalfHeightCm);
			OutB = Body.LocationCm + FVec3(0.0, 0.0, Body.HalfHeightCm);
		}

		double BodyCollisionRadius(const FBody& Body)
		{
			return Body.Prop == EArenaProp::Brazier && Body.Kind == EBodyKind::Prop ? BrazierPedestalRadiusCm : Body.RadiusCm;
		}

		bool RaySphere(const FVec3& Start, const FVec3& Direction, const FVec3& Center, double Radius, double& OutT)
		{
			const FVec3 M = Start - Center;
			const double B = M.Dot(Direction);
			const double C = M.SizeSquared() - Radius * Radius;
			if (C > 0.0 && B > 0.0)
			{
				return false;
			}
			const double Discriminant = B * B - C;
			if (Discriminant < 0.0)
			{
				return false;
			}
			OutT = KMax(-B - KSqrt(Discriminant), 0.0);
			return true;
		}
	}

	// ---------------------------------------------------------------------------------------------------- Setup

	void FSandbox::Init()
	{
		World.Reset();
		World.ClearReactions();
		World.Settings = FSimSettings();
		ReactionParams = FDefaultReactionParams();
		AddBuiltInReactions(World, ReactionParams);

		Tuning = GetDefaultTechniqueTuning();
		Layout = GetTrainingGroundLayout();
		GenerateArenaTerrain(Layout, Terrain);
		const int NumSamples = Terrain.GetSamplesX() * Terrain.GetSamplesY();
		for (int Index = 0; Index < NumSamples; ++Index)
		{
			OriginalHeights[Index] = Terrain.GetHeightData()[Index];
		}

		Whip = FWaterWhip();
		for (FBody& Body : Bodies)
		{
			Body = FBody();
		}
		NumBodies = 0;
		for (FProjectile& Projectile : Projectiles)
		{
			Projectile = FProjectile();
		}
		EarthWork = FEarthWork();
		NumMessages = 0;
		MessageSerial = 0;
		NumFrameEvents = 0;
		TimeS = 0.0;
		ThermalWorkJ = 0.0;
		KineticWorkJ = 0.0;
		for (bool& bHeld : PreviousSlotHeld)
		{
			bHeld = false;
		}
		bPreviousJump = false;
		bPreviousDodge = false;
		bWhipCreatedThisMove = false;
		HoldProjectile = -1;
		EarthWorkMovedKg = 0.0;
		EarthWorkJ = 0.0;
		LastNoChiMessageS = -10.0;

		Player = FPlayer();
		Tornado = FTornado();
		const bool bRivalEnabled = Rival.bEnabled;
		const FSparringTuning RivalTuning = Rival.Brain.Tuning;
		Rival = FRival();
		Rival.bEnabled = bRivalEnabled;
		Rival.Brain.Tuning = RivalTuning;
		Player.LocationCm = Layout.PlayerStartCm;
		Player.LocationCm.Z = Terrain.GetHeightAt(Player.LocationCm.X, Player.LocationCm.Y);
		Player.YawRad = Layout.PlayerStartYawDeg * Pi / 180.0;
		Player.AimPointCm = Player.LocationCm + GetFacing() * 800.0;
		UpdateHand(1.0 / FramesPerSecond);
		Player.PreviousHandCm = Player.HandCm;

		for (int Index = 0; Index < Layout.NumProps; ++Index)
		{
			const FArenaPropPlacement& Placement = Layout.Props[Index];
			const FArenaPropSpec& Spec = GetArenaPropSpec(Placement.Kind);
			const FVec3 Ground(Placement.LocationCm.X, Placement.LocationCm.Y, Terrain.GetHeightAt(Placement.LocationCm.X, Placement.LocationCm.Y));
			const FVolume Volume = MakeArenaPropVolume(Placement.Kind, Ground);

			FBody Body;
			Body.Kind = EBodyKind::Prop;
			Body.Prop = Placement.Kind;
			Body.LocationCm = Volume.LocationCm;
			Body.AnchorCm = Ground;
			Body.RadiusCm = Volume.RadiusCm;
			Body.HalfHeightCm = Spec.HalfHeightCm;
			Body.MassKg = Spec.MassKg;
			Body.bStatic = Spec.bStatic;
			Body.YawRad = Placement.YawDeg * Pi / 180.0;
			Body.Variant = Placement.Variant;
			Body.WaterKg = Spec.WaterKg;
			Body.Health = DummyMaxHealth;
			Body.HomeCm = Volume.LocationCm;
			// Lanterns stand unlit until a firebender lights them.
			AddBody(Body, Volume);
		}
		if (Rival.bEnabled)
		{
			SpawnRival();
		}
	}

	int FSandbox::AddBody(const FBody& Body, const FVolume& Volume)
	{
		int Slot = -1;
		for (int Index = 0; Index < MaxBodies; ++Index)
		{
			if (!Bodies[Index].bAlive)
			{
				Slot = Index;
				break;
			}
		}
		if (Slot < 0)
		{
			return -1;
		}
		const FHandle Handle = World.AddVolume(Volume, true);
		if (!Handle.IsSet())
		{
			return -1;
		}
		Bodies[Slot] = Body;
		Bodies[Slot].Volume = Handle;
		Bodies[Slot].bAlive = true;
		NumBodies = Slot + 1 > NumBodies ? Slot + 1 : NumBodies;
		return Slot;
	}

	void FSandbox::RemoveBody(int Index)
	{
		World.RemoveVolume(Bodies[Index].Volume);
		Bodies[Index].Burn.RemoveFlame(World);
		Bodies[Index].bAlive = false;
		Bodies[Index].Volume = FHandle();
	}

	int FSandbox::SpawnProjectile(EProjectileKind Kind, const FVolume& Volume)
	{
		for (int Index = 0; Index < MaxProjectiles; ++Index)
		{
			FProjectile& Projectile = Projectiles[Index];
			if (Projectile.bAlive)
			{
				continue;
			}
			const FHandle Handle = World.AddVolume(Volume, true);
			if (!Handle.IsSet())
			{
				return -1;
			}
			Projectile = FProjectile();
			Projectile.Kind = Kind;
			Projectile.Volume = Handle;
			Projectile.LocationCm = Volume.LocationCm;
			Projectile.VelocityCmS = Volume.VelocityCmS;
			Projectile.bAlive = true;
			return Index;
		}
		return -1;
	}

	void FSandbox::RemoveProjectile(int Index)
	{
		World.RemoveVolume(Projectiles[Index].Volume);
		Projectiles[Index].bAlive = false;
		Projectiles[Index].Volume = FHandle();
		if (HoldProjectile == Index)
		{
			HoldProjectile = -1;
		}
	}

	// ---------------------------------------------------------------------------------------------------- Queries

	FVec3 FSandbox::GetFacing() const
	{
		return FVec3(KCos(Player.YawRad), KSin(Player.YawRad), 0.0);
	}

	FVec3 FSandbox::GetChestCm() const
	{
		return Player.LocationCm + FVec3(0.0, 0.0, ChestHeightCm);
	}

	FVec3 FSandbox::GetEmitOriginCm() const
	{
		return Player.HandCm;
	}

	FVec3 FSandbox::ClampToRange(const FVec3& PointCm, double RangeCm) const
	{
		FVec3 Offset = Flat(PointCm - Player.LocationCm);
		if (Offset.Size() > RangeCm)
		{
			Offset = Offset.GetSafeNormal() * RangeCm;
		}
		const FVec3 Clamped = Player.LocationCm + Offset;
		return FVec3(Clamped.X, Clamped.Y, Terrain.GetHeightAt(Clamped.X, Clamped.Y));
	}

	int FSandbox::FindPondAt(const FVec3& LocationCm) const
	{
		for (int Index = 0; Index < Layout.NumPonds; ++Index)
		{
			const FArenaPond& Pond = Layout.Ponds[Index];
			if (Distance2D(LocationCm, Pond.CenterCm) < Pond.RadiusCm && LocationCm.Z < Pond.SurfaceHeightCm + 150.0)
			{
				return Index;
			}
		}
		return -1;
	}

	double FSandbox::GetPhaseProgress() const
	{
		if (Player.Phase == EPhase::None)
		{
			return 0.0;
		}
		const FTechniqueInfo& Info = GetTechniqueInfo(Player.Move);
		const int Frames = Player.Phase == EPhase::Startup ? Info.StartupFrames : (Player.Phase == EPhase::Active ? Info.ActiveFrames : Info.RecoveryFrames);
		return KClamp(Player.PhaseTimeS * FramesPerSecond / KMax(Frames, 1), 0.0, 1.0);
	}

	bool FSandbox::IsInCancelWindow() const
	{
		if (Player.Phase != EPhase::Recovery)
		{
			return false;
		}
		const FTechniqueInfo& Info = GetTechniqueInfo(Player.Move);
		const int CancelFrame = static_cast<int>(0.6 * Info.RecoveryFrames);
		return Player.PhaseTimeS * FramesPerSecond >= CancelFrame;
	}

	int FSandbox::CountProjectiles(EProjectileKind Kind) const
	{
		int Count = 0;
		for (const FProjectile& Projectile : Projectiles)
		{
			Count += Projectile.bAlive && Projectile.Kind == Kind ? 1 : 0;
		}
		return Count;
	}

	const FBody* FSandbox::FindBodyByVolume(FHandle Handle) const
	{
		for (int Index = 0; Index < NumBodies; ++Index)
		{
			// A burning prop's flame counts as part of it.
			if (Bodies[Index].bAlive && (Bodies[Index].Volume == Handle || (Handle.IsSet() && Bodies[Index].Burn.Flame == Handle)))
			{
				return &Bodies[Index];
			}
		}
		return nullptr;
	}

	int FSandbox::FindProjectileByVolume(FHandle Handle) const
	{
		for (int Index = 0; Index < MaxProjectiles; ++Index)
		{
			if (Projectiles[Index].bAlive && Projectiles[Index].Volume == Handle)
			{
				return Index;
			}
		}
		return -1;
	}

	bool FSandbox::IsWhipVolume(FHandle Handle) const
	{
		for (int Segment = 0; Whip.IsActive() && Segment < Whip.GetNumSegments(); ++Segment)
		{
			if (Whip.GetSegmentHandle(Segment) == Handle)
			{
				return true;
			}
		}
		return false;
	}

	// ---------------------------------------------------------------------------------------------------- Messages and chi

	void FSandbox::AddMessage(const char* Text)
	{
		if (NumMessages == MaxMessages)
		{
			for (int Index = 1; Index < MaxMessages; ++Index)
			{
				Messages[Index - 1] = Messages[Index];
			}
			--NumMessages;
		}
		FMessage& Message = Messages[NumMessages++];
		Append(Message.Text, 0, sizeof(Message.Text), Text);
		Message.TimeS = TimeS;
		++MessageSerial;
	}

	void FSandbox::AddMessageNumber(const char* Prefix, double Value, const char* Suffix)
	{
		char Text[96];
		int Length = Append(Text, 0, sizeof(Text), Prefix);
		Length = AppendNumber(Text, Length, sizeof(Text), Value, Value < 10.0 ? 1 : 0);
		Append(Text, Length, sizeof(Text), Suffix);
		AddMessage(Text);
	}

	double FSandbox::SpendChiForEnergy(double EnergyJ, bool bThermal)
	{
		if (EnergyJ <= 0.0)
		{
			return 0.0;
		}
		const double JoulesPerChi = bThermal ? Tuning.ThermalJoulesPerChi : Tuning.KineticJoulesPerChi;
		const double ChiSpent = KMin(EnergyJ / JoulesPerChi, Player.Chi);
		Player.Chi -= ChiSpent;
		Player.ChiSpentTotal += ChiSpent;
		const double Granted = ChiSpent * JoulesPerChi;
		(bThermal ? ThermalWorkJ : KineticWorkJ) += Granted;
		if (Granted < 0.5 * EnergyJ && TimeS - LastNoChiMessageS > 1.5)
		{
			LastNoChiMessageS = TimeS;
			AddMessage("Out of chi: the bending weakens");
		}
		return Granted;
	}

	// ---------------------------------------------------------------------------------------------------- Frame

	void FSandbox::Advance(const FInput& Input, double FrameSeconds)
	{
		const double Dt = KClamp(FrameSeconds, 0.0, 0.1);
		if (Dt <= 0.0)
		{
			return;
		}
		TimeS += Dt;
		NumFrameEvents = 0;

		// Knocked down, the player lies still: no moving, no bending, until they get up.
		FInput Effective = Input;
		if (Player.DownS > 0.0)
		{
			Effective = FInput();
			Effective.CameraLocationCm = Input.CameraLocationCm;
			Effective.CameraForward = Input.CameraForward;
		}

		UpdateStance(Effective);
		UpdateAim(Effective);
		UpdateGuard(Effective, Dt);
		UpdateMoves(Effective, Dt);
		UpdateHold(Effective, Dt);
		UpdatePlayer(Effective, Dt);
		UpdateHand(Dt);
		UpdateEarthWork(Dt);
		UpdateBraziers(Dt);
		UpdateRival(Dt);
		UpdateProjectiles(Dt);
		UpdateFighterHits();
		UpdateBodies(Dt);
		UpdateTornado(Dt);
		UpdateWhip(Dt);

		SyncVolumesIn();
		World.Advance(Dt);
		Whip.PostStep(World);
		SyncVolumesOut();
		UpdateCombustion(Dt);
		ProcessBreaks();
		UpdateDummies(Dt);
		CollectEvents(Dt);

		if (Whip.IsActive() && Whip.ShouldCollapse(World))
		{
			AddMessage("Too little water left: the whip falls apart");
			ReleaseWhip(false);
		}

		Player.Chi = KMin(Player.Chi + Player.ChiRegenPerS * Dt, Player.MaxChi);
		Player.Stamina = KMin(Player.Stamina + Player.StaminaRegenPerS * (Player.bGuarding ? 0.5 : 1.0) * Dt, Player.MaxStamina);
		if (!IsDuelActive() && Player.DownS <= 0.0)
		{
			Player.Health = KMin(Player.Health + RestHealPerS * Dt, Player.MaxHealth);
		}
	}

	void FSandbox::UpdateStance(const FInput& Input)
	{
		if (Input.StanceRequest < 1 || Input.StanceRequest > 4)
		{
			return;
		}
		const ETechniqueElement Requested = static_cast<ETechniqueElement>(Input.StanceRequest);
		if (Requested != Player.Stance)
		{
			Player.Stance = Requested;
			char Text[64];
			int Length = Append(Text, 0, sizeof(Text), "Stance: ");
			Append(Text, Length, sizeof(Text), ElementName(Requested));
			AddMessage(Text);
		}
	}

	void FSandbox::UpdateAim(const FInput& Input)
	{
		const FVec3 Direction = Input.CameraForward.GetSafeNormal();
		if (Direction.IsZero())
		{
			return;
		}
		const double MaxDistance = 6000.0;
		double BestT = MaxDistance;
		bool bHit = false;
		FVec3 TerrainHit;
		if (Terrain.Raycast(Input.CameraLocationCm, Direction, MaxDistance, TerrainHit))
		{
			BestT = Distance(Input.CameraLocationCm, TerrainHit);
			bHit = true;
		}
		for (int Index = 0; Index < NumBodies; ++Index)
		{
			const FBody& Body = Bodies[Index];
			if (!Body.bAlive || Body.bHeld)
			{
				continue;
			}
			double T = 0.0;
			const double Radius = Body.RadiusCm + (Body.HalfHeightCm > 0.0 ? 0.5 * Body.HalfHeightCm : 0.0);
			if (RaySphere(Input.CameraLocationCm, Direction, Body.LocationCm, Radius, T) && T < BestT && T > 50.0)
			{
				BestT = T;
				bHit = true;
			}
		}
		Player.AimPointCm = Input.CameraLocationCm + Direction * BestT;
		Player.bAimOnSurface = bHit;
	}

	// ---------------------------------------------------------------------------------------------------- Moves

	void FSandbox::UpdateMoves(const FInput& Input, double Dt)
	{
		// Guarding or down, no new move starts (and none is buffered).
		const bool bLocked = Player.bGuarding || Player.DownS > 0.0 || Player.DodgeS > 0.0;
		for (int Slot = 0; Slot < static_cast<int>(ETechniqueSlot::Count); ++Slot)
		{
			const bool bPressed = Input.bSlotHeld[Slot] && !PreviousSlotHeld[Slot];
			PreviousSlotHeld[Slot] = Input.bSlotHeld[Slot];
			if (!bPressed || bLocked)
			{
				continue;
			}
			const ETechnique Technique = FindTechnique(Player.Stance, static_cast<ETechniqueSlot>(Slot));
			if (Technique != ETechnique::None && !TryStartMove(Technique))
			{
				Player.BufferedMove = Technique;
				Player.BufferedTimeS = TimeS;
			}
		}

		if (bLocked)
		{
			Player.BufferedMove = ETechnique::None;
		}
		if (Player.BufferedMove != ETechnique::None)
		{
			if (TimeS - Player.BufferedTimeS > InputBufferS)
			{
				Player.BufferedMove = ETechnique::None;
			}
			else if (TryStartMove(Player.BufferedMove))
			{
				Player.BufferedMove = ETechnique::None;
			}
		}

		if (Player.Phase == EPhase::None)
		{
			return;
		}
		Player.PhaseTimeS += Dt;
		for (int Guard = 0; Guard < 4 && Player.Phase != EPhase::None; ++Guard)
		{
			const FTechniqueInfo& Info = GetTechniqueInfo(Player.Move);
			const int Frames = Player.Phase == EPhase::Startup ? Info.StartupFrames : (Player.Phase == EPhase::Active ? Info.ActiveFrames : Info.RecoveryFrames);
			const double Duration = Frames / FramesPerSecond;
			if (Player.PhaseTimeS < Duration)
			{
				break;
			}
			const double Overflow = Player.PhaseTimeS - Duration;
			EnterPhase(Player.Phase == EPhase::Startup ? EPhase::Active : (Player.Phase == EPhase::Active ? EPhase::Recovery : EPhase::None));
			Player.PhaseTimeS = Overflow;
		}
	}

	bool FSandbox::TryStartMove(ETechnique Technique)
	{
		if (Player.Phase != EPhase::None && !IsInCancelWindow())
		{
			return false;
		}
		const FTechniqueInfo& Info = GetTechniqueInfo(Technique);
		const bool bNeedsWhip = Technique == ETechnique::WaterFreeze || Technique == ETechnique::WaterRelease || Technique == ETechnique::WaterBlast
			|| Technique == ETechnique::IceDaggers;
		if (bNeedsWhip && !Whip.IsActive())
		{
			AddMessage("No water whip: draw one first (left mouse near a pond)");
			return true;
		}
		if (Player.Chi < Info.ChiCost || Player.Stamina < Info.StaminaCost)
		{
			if (TimeS - LastNoChiMessageS > 1.0)
			{
				LastNoChiMessageS = TimeS;
				AddMessage("Not enough chi");
			}
			return true;
		}
		Player.Chi -= Info.ChiCost;
		Player.ChiSpentTotal += Info.ChiCost;
		Player.Stamina -= Info.StaminaCost;
		Player.Move = Technique;
		Player.PhaseTimeS = 0.0;
		EnterPhase(EPhase::Startup);
		return true;
	}

	void FSandbox::EnterPhase(EPhase Phase)
	{
		Player.Phase = Phase;
		Player.PhaseTimeS = 0.0;
		switch (Phase)
		{
		case EPhase::Startup:
			OnStartup(Player.Move);
			break;
		case EPhase::Active:
			OnActive(Player.Move);
			break;
		case EPhase::None:
			Player.Move = ETechnique::None;
			break;
		default:
			break;
		}
	}

	void FSandbox::OnStartup(ETechnique Technique)
	{
		bWhipCreatedThisMove = false;
		switch (Technique)
		{
		case ETechnique::WaterWhip:
		{
			if (Whip.IsActive())
			{
				// The move's frame data times the lash: startup draws the stream back, active frames are the strike.
				const FTechniqueInfo& Info = GetTechniqueInfo(Technique);
				Whip.Settings.WindupS = Info.StartupFrames / FramesPerSecond;
				Whip.Settings.StrikeS = Info.ActiveFrames / FramesPerSecond;
				Whip.Lash();
				break;
			}
			FVec3 Source;
			int Pond = FindWaterSource(Layout, Player.HandCm, Tuning.WhipDrawRangeCm, Source);
			if (Pond >= 0 && Layout.Ponds[Pond].WaterKg < Tuning.WhipWaterKg)
			{
				Pond = -1;
			}
			// No pond in reach: a water barrel close by will do.
			const int Barrel = Pond < 0 ? FindWaterBarrel(Player.HandCm, Tuning.WhipWaterKg, Source) : -1;
			if (Pond < 0 && Barrel < 0)
			{
				AddMessage("No water within 15 m: go to a pond or a water barrel");
				break;
			}
			if (Whip.Create(World, Source, Player.HandCm, Tuning.WhipWaterKg, 288.15))
			{
				if (Pond >= 0)
				{
					Layout.Ponds[Pond].WaterKg -= Tuning.WhipWaterKg;
				}
				else
				{
					Bodies[Barrel].WaterKg -= Tuning.WhipWaterKg;
					Bodies[Barrel].MassKg = KMax(Bodies[Barrel].MassKg - Tuning.WhipWaterKg, 1.0);
				}
				bWhipCreatedThisMove = true;
				AddMessageNumber("Drew ", Tuning.WhipWaterKg, Pond >= 0 ? " kg of water from the pond" : " kg of water from the barrel");
			}
			break;
		}
		case ETechnique::RockThrow:
		{
			if (EarthWork.RockBody >= 0)
			{
				break;
			}
			const FVec3 Spot = Player.LocationCm + GetFacing() * 220.0;
			const FVec3 Ground(Spot.X, Spot.Y, Terrain.GetHeightAt(Spot.X, Spot.Y));
			const FTerrainEdit Edit = Terrain.RemoveSoil(RockQuarryBrush(Ground), Tuning.RockMassKg / Terrain.SoilDensityKgM3);
			if (Edit.MassKg < 10.0)
			{
				AddMessage("No soil to pull up here");
				break;
			}
			FVolume Rock = MakeRock(Edit.MassKg, Tuning.RockDensityKgM3, Ground, FVec3());
			FBody Body;
			Body.Kind = EBodyKind::ThrownRock;
			Body.Prop = EArenaProp::Rock;
			Body.RadiusCm = Rock.RadiusCm;
			Body.MassKg = Edit.MassKg;
			Body.bHeld = true;
			Body.LocationCm = Ground - FVec3(0.0, 0.0, 0.3 * Rock.RadiusCm);
			Rock.LocationCm = Body.LocationCm;
			const int Index = AddBody(Body, Rock);
			if (Index >= 0)
			{
				EarthWork.RockBody = Index;
				EarthWork.RockStartCm = Body.LocationCm;
				EarthWork.RockHoldCm = Ground + FVec3(0.0, 0.0, Tuning.RockHoldHeightCm);
				AddMessageNumber("Pulled ", Edit.MassKg, " kg of soil out of the ground");
			}
			break;
		}
		default:
			break;
		}
	}

	void FSandbox::OnActive(ETechnique Technique)
	{
		const FTechniqueInfo& Info = GetTechniqueInfo(Technique);
		const FVec3 Origin = GetEmitOriginCm();
		const FVec3 ToAim = (Player.AimPointCm - Origin).GetSafeNormal();
		const FVec3 AimDirection = ToAim.IsZero() ? GetFacing() : ToAim;
		const double Ambient = World.Settings.AmbientTemperatureK;

		if (Info.bHold)
		{
			Player.HoldTechnique = Technique;
			Player.HoldSlot = Info.Slot;
			Player.HoldEmitTimerS = 0.0;
			if (Technique == ETechnique::AirScooter)
			{
				Player.bScooter = true;
				AddMessage("Air scooter: hold to ride");
			}
			if (Technique == ETechnique::GroundFlame)
			{
				const FVec3 Spot = ClampToRange(Player.AimPointCm, Tuning.EarthbendRangeCm);
				FVolume Flame = MakeFlame(Tuning.GroundFlameMassKg, Tuning.GroundFlameTemperatureK, Spot, FVec3());
				Flame.LocationCm.Z = Spot.Z + 0.55 * Flame.RadiusCm;
				const double Heat = Flame.MassKg * FlameSpecificHeat * (Flame.TemperatureK - Ambient);
				const double Granted = SpendChiForEnergy(Heat, true);
				if (Granted < 0.5 * Heat)
				{
					Player.HoldTechnique = ETechnique::None;
					return;
				}
				HoldProjectile = SpawnProjectile(EProjectileKind::GroundFlame, Flame);
				if (HoldProjectile >= 0)
				{
					Projectiles[HoldProjectile].bLanded = true;
					Projectiles[HoldProjectile].SustainPowerW = Tuning.GroundFlamePowerW;
				}
			}
			else if (Technique == ETechnique::RaiseGround || Technique == ETechnique::LowerGround)
			{
				// The spot is fixed when the hold starts: the rising pillar would otherwise catch the aim ray and
				// walk it toward the bender.
				HoldCenterCm = ClampToRange(Player.AimPointCm, Tuning.EarthbendRangeCm);
				EarthWorkMovedKg = 0.0;
				EarthWorkJ = 0.0;
			}
			return;
		}

		switch (Technique)
		{
		case ETechnique::WaterFreeze:
		{
			const bool bThaw = Whip.IsAnyFrozen(World);
			const double Need = bThaw ? Whip.GetHeatToMelt(World) : Whip.GetHeatToFreeze(World);
			const double Granted = SpendChiForEnergy(Need, true);
			const double Moved = Whip.TransferHeat(World, bThaw ? Granted : -Granted);
			char Text[96];
			int Length = Append(Text, 0, sizeof(Text), bThaw ? "Thawed the whip: " : "Froze the whip: ");
			Length = AppendNumber(Text, Length, sizeof(Text), (Moved < 0.0 ? -Moved : Moved) / 1e6, 2);
			Length = Append(Text, Length, sizeof(Text), " MJ of heat (");
			Length = AppendNumber(Text, Length, sizeof(Text), Granted / Tuning.ThermalJoulesPerChi, 0);
			Append(Text, Length, sizeof(Text), " chi)");
			AddMessage(Text);
			break;
		}

		case ETechnique::WaterRelease:
		case ETechnique::WaterBlast:
			if (Whip.IsAnyFrozen(World))
			{
				AddMessage("Thaw the whip first (right mouse)");
				break;
			}
			ReleaseWhip(Technique == ETechnique::WaterBlast);
			break;

		case ETechnique::RockThrow:
		{
			if (EarthWork.RockBody < 0)
			{
				break;
			}
			FBody& Rock = Bodies[EarthWork.RockBody];
			EarthWork.RockBody = -1;
			if (!Rock.bAlive)
			{
				break;
			}
			const FVec3 Direction = (Player.AimPointCm - Rock.LocationCm).GetSafeNormal();
			// Work: lifting it (m g h) and throwing it (1/2 m v^2).
			const double LiftJ = Rock.MassKg * CmToM(GravityCmS2) * CmToM(KMax(Rock.LocationCm.Z - EarthWork.RockStartCm.Z, 0.0));
			const double LaunchJ = KineticEnergyJ(Rock.MassKg, Tuning.RockLaunchSpeedMs);
			const double Granted = SpendChiForEnergy(LiftJ + LaunchJ, false);
			const double SpeedMs = KMin(KSqrt(2.0 * KMax(Granted - LiftJ, 0.0) / Rock.MassKg), Tuning.RockLaunchSpeedMs);
			Rock.bHeld = false;
			Rock.VelocityCmS = (Direction.IsZero() ? GetFacing() : Direction) * MToCm(SpeedMs);
			AddMessageNumber("Rock thrown at ", SpeedMs, " m/s");
			break;
		}

		case ETechnique::EarthWall:
		{
			FVec3 Center = ClampToRange(Player.AimPointCm, Tuning.EarthbendRangeCm);
			FVec3 Away = Flat(Center - Player.LocationCm);
			if (Away.Size() < MinWallDistanceCm)
			{
				Away = (Away.Size() > 1.0 ? Away.GetSafeNormal() : GetFacing()) * MinWallDistanceCm;
				Center = Player.LocationCm + Away;
				Center.Z = Terrain.GetHeightAt(Center.X, Center.Y);
			}
			const FVec3 Facing = Flat(Player.LocationCm - Center).GetSafeNormal();
			EarthWallBrushes(Center, Facing, Tuning, EarthWork.Target, EarthWork.Source);
			EarthWork.Technique = ETechnique::EarthWall;
			EarthWork.RemainingM3 = CmToM(Tuning.WallHeightCm) * Terrain.GetBrushAreaM2(EarthWork.Target);
			EarthWork.RateM3S = EarthWork.RemainingM3 / KMax(Tuning.WallRiseSeconds, 0.05);
			EarthWorkMovedKg = 0.0;
			EarthWorkJ = 0.0;
			break;
		}

		case ETechnique::FireBlast:
		{
			const double Heat = Tuning.FireBlastMassKg * FlameSpecificHeat * (Tuning.FireBlastTemperatureK - Ambient);
			const double Granted = SpendChiForEnergy(Heat, true);
			const double MassKg = Tuning.FireBlastMassKg * Granted / KMax(Heat, 1.0);
			if (MassKg < 0.05)
			{
				break;
			}
			SpendChiForEnergy(KineticEnergyJ(MassKg, Tuning.FireBlastSpeedMs), false);
			FVolume Flame = MakeFlame(MassKg, Tuning.FireBlastTemperatureK, Origin, AimDirection * MToCm(Tuning.FireBlastSpeedMs));
			Flame.LocationCm = Origin + AimDirection * (0.6 * Flame.RadiusCm);
			SpawnProjectile(EProjectileKind::Fire, Flame);
			break;
		}

		case ETechnique::AirBlast:
		{
			FVolume Air = MakeBentAir(Tuning.AirBlastRadiusCm, Tuning.AirBlastCompression, World.Settings.AmbientAirDensityKgM3, Origin, FVec3());
			const double Requested = KineticEnergyJ(Air.MassKg, Tuning.AirBlastSpeedMs);
			const double Granted = SpendChiForEnergy(Requested, false);
			const double SpeedMs = Tuning.AirBlastSpeedMs * KSqrt(Granted / KMax(Requested, 1.0));
			Air.LocationCm = Origin + AimDirection * Air.RadiusCm;
			Air.VelocityCmS = AimDirection * MToCm(SpeedMs);
			SpawnProjectile(EProjectileKind::Air, Air);
			break;
		}

		case ETechnique::AirJump:
		{
			const double Granted = SpendChiForEnergy(KineticEnergyJ(PlayerMassKg, Tuning.AirJumpSpeedMs), false);
			const double SpeedMs = KSqrt(2.0 * Granted / PlayerMassKg);
			Player.VelocityCmS.Z = KMax(Player.VelocityCmS.Z, MToCm(SpeedMs));
			Player.bGrounded = false;
			// A puff of air pushed down under the feet.
			SpawnProjectile(EProjectileKind::Air, MakeBentAir(50.0, 2.0, World.Settings.AmbientAirDensityKgM3,
				Player.LocationCm + FVec3(0.0, 0.0, 30.0), FVec3(0.0, 0.0, -1200.0)));
			break;
		}

		case ETechnique::IceDaggers:
		{
			if (Whip.IsAnyFrozen(World))
			{
				AddMessage("Thaw the whip first (right mouse)");
				break;
			}
			// Water bent off the end of the whip, frozen into daggers: the cold is billed like any freezing.
			double WaterTemperatureK = 288.15;
			const double Taken = Whip.TakeWater(World, Tuning.IceDaggerCount * Tuning.IceDaggerMassKg, WaterTemperatureK);
			if (Taken < 0.5 * Tuning.IceDaggerMassKg)
			{
				AddMessage("Too little water left in the whip");
				break;
			}
			const double Need = HeatToMakeIce(Taken, WaterTemperatureK, 263.15);
			const double Granted = SpendChiForEnergy(Need, true);
			const int Count = KClamp(static_cast<int>(Tuning.IceDaggerCount * Granted / KMax(Need, 1.0) + 0.5), 1, Tuning.IceDaggerCount);
			const double ShardKg = Taken / Count;
			SpendChiForEnergy(KineticEnergyJ(Taken, Tuning.IceDaggerSpeedMs), false);
			FVec3 Side = AimDirection.Cross(FVec3(0.0, 0.0, 1.0));
			Side = Side.Size() > 0.1 ? Side.GetSafeNormal() : FVec3(0.0, 1.0, 0.0);
			const FVec3 Lift = Side.Cross(AimDirection).GetSafeNormal();
			const double Spread = Tuning.IceDaggerSpreadDeg * Pi / 180.0;
			const double Offsets[5][2] = { { 0.0, 0.0 }, { 1.0, 0.25 }, { -1.0, 0.25 }, { 0.5, -0.7 }, { -0.5, -0.7 } };
			// They leave in a small fan and close on the aim point, landing in a tight group round it.
			const double AimDistance = KClamp(Distance(Player.AimPointCm, Origin), 200.0, 3000.0);
			const double Group = 6.0 + AimDistance * (KSin(Spread) / KMax(KCos(Spread), 0.1)) * 0.12;
			for (int Index = 0; Index < Count; ++Index)
			{
				const double* Offset = Offsets[Index % 5];
				const FVec3 Location = Origin + AimDirection * 25.0 + Side * (Offset[0] * 12.0) + Lift * (Offset[1] * 12.0);
				const FVec3 Target = Origin + AimDirection * AimDistance + Side * (Offset[0] * Group) + Lift * (Offset[1] * Group);
				const FVec3 Direction = (Target - Location).GetSafeNormal();
				SpawnProjectile(EProjectileKind::IceShard, MakeIceShard(ShardKg, Location, Direction * MToCm(Tuning.IceDaggerSpeedMs)));
			}
			char Text[96];
			int Length = Append(Text, 0, sizeof(Text), "Ice daggers: ");
			Length = AppendNumber(Text, Length, sizeof(Text), Count, 0);
			Length = Append(Text, Length, sizeof(Text), " shards, ");
			Length = AppendNumber(Text, Length, sizeof(Text), Granted / 1000.0, 0);
			Append(Text, Length, sizeof(Text), " kJ of cold");
			AddMessage(Text);
			break;
		}

		case ETechnique::Earthquake:
		{
			// A stomp: the ground shakes outward. Each body within reach gets kinetic energy that falls off with
			// distance; equal energy moves a heavy body less (dv = sqrt(2 E / m)).
			const FVec3 Center = Player.LocationCm;
			const double Radius = KMax(Tuning.EarthquakeRadiusCm, 1.0);
			double Requested = Tuning.EarthquakeGroundJ;
			double Shares[MaxBodies] = {};
			for (int Index = 0; Index < NumBodies; ++Index)
			{
				const FBody& Body = Bodies[Index];
				const bool bMovable = Body.bAlive && !Body.bStatic && !Body.bHeld && !(Body.Prop == EArenaProp::Brazier && Body.Kind == EBodyKind::Prop);
				const double Distance = Flat(Body.LocationCm - Center).Size();
				if (bMovable && Distance < Radius)
				{
					const double Falloff = 1.0 - Distance / Radius;
					Shares[Index] = Tuning.EarthquakeEnergyJ * Falloff * Falloff;
					Requested += Shares[Index];
				}
			}
			const double Scale = SpendChiForEnergy(Requested, false) / KMax(Requested, 1.0);
			int Thrown = 0;
			for (int Index = 0; Index < NumBodies; ++Index)
			{
				if (Shares[Index] <= 0.0)
				{
					continue;
				}
				const FBody& Body = Bodies[Index];
				FVec3 Out = Flat(Body.LocationCm - Center);
				Out = Out.Size() > 1.0 ? Out.GetSafeNormal() : GetFacing();
				const double SpeedCmS = KMin(MToCm(KSqrt(2.0 * Shares[Index] * Scale / KMax(Body.MassKg, 0.1))), MToCm(Tuning.EarthquakeMaxSpeedMs));
				PushBody(Index, (Out * 0.75 + FVec3(0.0, 0.0, 0.66)).GetSafeNormal() * (SpeedCmS * Body.MassKg));
				++Thrown;
			}
			AddEffect(ESandboxEffect::Quake, Center, Requested * Scale);
			char Text[96];
			int Length = Append(Text, 0, sizeof(Text), "Earthquake: ");
			Length = AppendNumber(Text, Length, sizeof(Text), Thrown, 0);
			Append(Text, Length, sizeof(Text), Thrown == 1 ? " thing thrown" : " things thrown");
			AddMessage(Text);
			break;
		}

		case ETechnique::FireRing:
		{
			// A spin kick throws flame out in every direction.
			const int Count = KMax(Tuning.FireRingCount, 1);
			const double HeatEach = Tuning.FireRingMassKg * FlameSpecificHeat * (Tuning.FireRingTemperatureK - Ambient);
			const double Granted = SpendChiForEnergy(HeatEach * Count, true);
			const double MassKg = Tuning.FireRingMassKg * Granted / KMax(HeatEach * Count, 1.0);
			if (MassKg < 0.02)
			{
				break;
			}
			SpendChiForEnergy(Count * KineticEnergyJ(MassKg, Tuning.FireRingSpeedMs), false);
			for (int Index = 0; Index < Count; ++Index)
			{
				const double Angle = (2.0 * Pi * Index) / Count;
				const FVec3 Out(KCos(Angle), KSin(Angle), 0.03);
				FVolume Flame = MakeFlame(MassKg, Tuning.FireRingTemperatureK, Player.LocationCm + FVec3(0.0, 0.0, 75.0) + Out * 60.0, Out * MToCm(Tuning.FireRingSpeedMs));
				SpawnProjectile(EProjectileKind::Fire, Flame);
			}
			AddEffect(ESandboxEffect::FireRing, Player.LocationCm + FVec3(0.0, 0.0, 75.0), Granted);
			break;
		}

		case ETechnique::FireJet:
		{
			// Fire jets from the feet: the bender's own kinetic energy is the work.
			FVec3 Direction = Flat(AimDirection);
			Direction = Direction.Size() > 0.1 ? Direction.GetSafeNormal() : GetFacing();
			const double Requested = KineticEnergyJ(PlayerMassKg, Tuning.FireJetSpeedMs);
			const double Granted = SpendChiForEnergy(Requested, false);
			const double SpeedMs = Tuning.FireJetSpeedMs * KSqrt(Granted / KMax(Requested, 1.0));
			Player.DashTimeS = Tuning.FireJetSeconds;
			Player.DashEmitTimerS = 0.0;
			Player.DashVelocityCmS = Direction * MToCm(SpeedMs);
			Player.VelocityCmS.Z = KMax(Player.VelocityCmS.Z, MToCm(Tuning.FireJetLiftMs));
			Player.bGrounded = false;
			AddEffect(ESandboxEffect::FireJet, Player.LocationCm, Granted);
			break;
		}

		case ETechnique::Tornado:
		{
			FVec3 Spot = ClampToRange(Player.AimPointCm, Tuning.TornadoRangeCm);
			Spot.Z = Terrain.GetHeightAt(Spot.X, Spot.Y);
			const double Granted = SpendChiForEnergy(Tuning.TornadoEnergyJ, false);
			if (Granted < 0.5 * Tuning.TornadoEnergyJ)
			{
				AddMessage("Not enough chi to spin up a tornado");
				break;
			}
			Tornado = FTornado();
			Tornado.bActive = true;
			Tornado.CenterCm = Spot;
			Tornado.LifetimeS = Tuning.TornadoSeconds * Granted / Tuning.TornadoEnergyJ;
			AddMessage("Tornado!");
			break;
		}

		default:
			break;
		}
	}

	void FSandbox::AddEffect(ESandboxEffect Effect, const FVec3& LocationCm, double EnergyJ)
	{
		if (NumFrameEvents < MaxFrameEvents)
		{
			FSandboxEvent& Event = FrameEvents[NumFrameEvents++];
			Event = FSandboxEvent();
			Event.Effect = Effect;
			Event.LocationCm = LocationCm;
			Event.EnergyJ = EnergyJ;
			Event.bDiscrete = true;
		}
	}

	void FSandbox::PushBody(int Index, const FVec3& ImpulseKgCmS)
	{
		if (Index >= 0 && Index < NumBodies && Bodies[Index].bAlive)
		{
			World.AddImpulse(Bodies[Index].Volume, ImpulseKgCmS);
		}
	}

	void FSandbox::UpdateTornado(double Dt)
	{
		if (!Tornado.bActive)
		{
			return;
		}
		Tornado.AgeS += Dt;
		if (Tornado.AgeS >= Tornado.LifetimeS)
		{
			Tornado.bActive = false;
			AddEffect(ESandboxEffect::TornadoEnd, Tornado.CenterCm, 0.0);
			return;
		}
		// Strength builds up, then dies away.
		const double Strength = KSmoothStep(0.0, 0.4, Tornado.AgeS) * (1.0 - KSmoothStep(Tornado.LifetimeS - 0.6, Tornado.LifetimeS, Tornado.AgeS));
		const double Reach = Tuning.TornadoRadiusCm * 1.8;
		const FVec3 Up(0.0, 0.0, 1.0);
		const double Swirl = MToCm(Tuning.TornadoSwirlMs);

		// What it catches swirls round, is drawn in, and (if light enough) is lifted.
		for (int Index = 0; Index < NumBodies; ++Index)
		{
			const FBody& Body = Bodies[Index];
			if (!Body.bAlive || Body.bStatic || Body.bHeld || (Body.Prop == EArenaProp::Brazier && Body.Kind == EBodyKind::Prop))
			{
				continue;
			}
			const FVec3 Offset = Flat(Body.LocationCm - Tornado.CenterCm);
			const double Distance = Offset.Size();
			const double Height = Body.LocationCm.Z - Tornado.CenterCm.Z;
			if (Distance >= Reach || Height > 900.0)
			{
				continue;
			}
			const double Falloff = (1.0 - Distance / Reach) * Strength;
			const FVec3 Inward = Distance > 1.0 ? Offset * (-1.0 / Distance) : FVec3();
			const FVec3 Around(-Inward.Y, Inward.X, 0.0);
			const double AroundSpeed = Body.VelocityCmS.Dot(Around);
			FVec3 DeltaV = Around * ((Swirl * Falloff - AroundSpeed) * KMin(1.0, 4.0 * Dt));
			DeltaV += Inward * (MToCm(Tuning.TornadoPullMs2) * Falloff * Dt);
			if (Height < 600.0)
			{
				DeltaV += Up * (MToCm(Tuning.TornadoLiftMs2) * Falloff * KMin(1.0, 150.0 / KMax(Body.MassKg, 1.0)) * Dt);
			}
			PushBody(Index, DeltaV * Body.MassKg);
		}

		// Flames, water and spray nearby are drawn into the spiral; flame caught in it makes a fire tornado.
		Tornado.FireKg = 0.0;
		for (FProjectile& Projectile : Projectiles)
		{
			if (!Projectile.bAlive || Projectile.Kind == EProjectileKind::GroundFlame)
			{
				continue;
			}
			const FVec3 Offset = Flat(Projectile.LocationCm - Tornado.CenterCm);
			const double Distance = Offset.Size();
			const double Height = Projectile.LocationCm.Z - Tornado.CenterCm.Z;
			if (Distance >= Reach * 1.2 || Height > 1000.0)
			{
				continue;
			}
			if (Projectile.Kind == EProjectileKind::Fire)
			{
				Projectile.bLanded = false;
				if (const FVolume* Volume = World.GetVolume(Projectile.Volume))
				{
					Tornado.FireKg += Volume->MassKg;
				}
				Projectile.AgeS = KMin(Projectile.AgeS, 0.5 * Tuning.ProjectileLifetimeS);
			}
			const FVec3 Inward = Distance > 1.0 ? Offset * (-1.0 / Distance) : FVec3();
			const FVec3 Around(-Inward.Y, Inward.X, 0.0);
			const double Ring = Tuning.TornadoRadiusCm * (0.5 + 0.5 * KClamp(Height / 800.0, 0.0, 1.0));
			const FVec3 Wanted = Around * (Swirl * Strength) + Inward * ((Distance - Ring) * 3.0) + Up * (180.0 * Strength);
			Projectile.VelocityCmS += (Wanted - Projectile.VelocityCmS) * KMin(1.0, 3.0 * Dt);
		}

		// Its air: parcels shed around the funnel (they push, and feed any fire they meet).
		Tornado.EmitTimerS -= Dt;
		if (Tornado.EmitTimerS <= 0.0 && CountProjectiles(EProjectileKind::Air) < 24)
		{
			Tornado.EmitTimerS = 0.12;
			Tornado.EmitAngleRad += 2.1;
			const FVec3 Out(KCos(Tornado.EmitAngleRad), KSin(Tornado.EmitAngleRad), 0.0);
			const FVec3 Around(-Out.Y, Out.X, 0.0);
			SpawnProjectile(EProjectileKind::Air, MakeBentAir(45.0, 1.4, World.Settings.AmbientAirDensityKgM3,
				Tornado.CenterCm + Out * Tuning.TornadoRadiusCm + FVec3(0.0, 0.0, 80.0), Around * (Swirl * Strength) + Up * 150.0));
		}
	}

	void FSandbox::UpdateHold(const FInput& Input, double Dt)
	{
		const ETechnique Technique = Player.HoldTechnique;
		if (Technique == ETechnique::None)
		{
			return;
		}
		if (!Input.bSlotHeld[static_cast<int>(Player.HoldSlot)])
		{
			if (Technique == ETechnique::GroundFlame && HoldProjectile >= 0)
			{
				Projectiles[HoldProjectile].SustainPowerW = 0.0;
				Projectiles[HoldProjectile].FuelJ = Tuning.GroundFlameFuelJ;
			}
			if ((Technique == ETechnique::RaiseGround || Technique == ETechnique::LowerGround) && EarthWorkMovedKg > 0.0)
			{
				char Text[96];
				int Length = Append(Text, 0, sizeof(Text), Technique == ETechnique::RaiseGround ? "Raised " : "Dug ");
				Length = AppendNumber(Text, Length, sizeof(Text), EarthWorkMovedKg / 1000.0, 1);
				Length = Append(Text, Length, sizeof(Text), " t of soil for ");
				Length = AppendNumber(Text, Length, sizeof(Text), EarthWorkJ / 1000.0, 1);
				Append(Text, Length, sizeof(Text), " kJ");
				AddMessage(Text);
			}
			if (Technique == ETechnique::AirScooter)
			{
				Player.bScooter = false;
			}
			Player.HoldTechnique = ETechnique::None;
			HoldProjectile = -1;
			return;
		}

		const FVec3 Origin = GetEmitOriginCm();
		const FVec3 ToAim = (Player.AimPointCm - Origin).GetSafeNormal();
		const FVec3 AimDirection = ToAim.IsZero() ? GetFacing() : ToAim;
		const double Ambient = World.Settings.AmbientTemperatureK;

		switch (Technique)
		{
		case ETechnique::RaiseGround:
		case ETechnique::LowerGround:
		{
			const FVec3 Center = HoldCenterCm;
			FTerrainBrush Target;
			FTerrainBrush Source;
			if (Technique == ETechnique::RaiseGround)
			{
				RaiseGroundBrushes(Center, Tuning, Target, Source);
			}
			else
			{
				LowerGroundBrushes(Center, Tuning, Target, Source);
			}
			const FTerrainEdit Edit = Terrain.MoveSoil(Source, Target, Tuning.TerraformRateM3S * Dt);
			EarthWorkMovedKg += Edit.MassKg;
			const double Work = KMax(Edit.PotentialEnergyChangeJ, 0.0);
			EarthWorkJ += Work;
			if (SpendChiForEnergy(Work, false) < 0.5 * Work)
			{
				Player.HoldTechnique = ETechnique::None;
			}
			break;
		}

		case ETechnique::AirScooter:
		{
			// Keeping the ball spinning: the drag work at speed, scaled up (a spinning ball, not a sail), plus a little to hover.
			const double SpeedMs = CmToM(Flat(Player.VelocityCmS).Size());
			const double DragW = 0.5 * World.Settings.AmbientAirDensityKgM3 * 0.8 * 0.6 * SpeedMs * SpeedMs * SpeedMs;
			const double Requested = (DragW * Tuning.AirScooterUpkeepScale + 2500.0) * Dt;
			if (SpendChiForEnergy(Requested, false) < 0.5 * Requested)
			{
				Player.bScooter = false;
				Player.HoldTechnique = ETechnique::None;
				AddMessage("Out of chi: the air scooter breaks up");
			}
			break;
		}

		case ETechnique::FlameStream:
		case ETechnique::AirGust:
		{
			const bool bFire = Technique == ETechnique::FlameStream;
			const double Rate = bFire ? Tuning.FlameStreamRateHz : Tuning.GustRateHz;
			Player.HoldEmitTimerS -= Dt;
			while (Player.HoldEmitTimerS <= 0.0 && Player.HoldTechnique != ETechnique::None)
			{
				Player.HoldEmitTimerS += 1.0 / KMax(Rate, 1.0);
				if (bFire)
				{
					const double Heat = Tuning.FlameStreamMassKg * FlameSpecificHeat * (Tuning.FlameStreamTemperatureK - Ambient);
					const double Granted = SpendChiForEnergy(Heat, true);
					if (Granted < 0.5 * Heat)
					{
						Player.HoldTechnique = ETechnique::None;
						break;
					}
					FVolume Flame = MakeFlame(Tuning.FlameStreamMassKg * Granted / Heat, Tuning.FlameStreamTemperatureK, Origin,
						AimDirection * MToCm(Tuning.FlameStreamSpeedMs));
					Flame.LocationCm = Origin + AimDirection * (0.6 * Flame.RadiusCm);
					SpawnProjectile(EProjectileKind::Fire, Flame);
				}
				else
				{
					FVolume Air = MakeBentAir(Tuning.GustRadiusCm, Tuning.GustCompression, World.Settings.AmbientAirDensityKgM3, Origin, FVec3());
					const double Requested = KineticEnergyJ(Air.MassKg, Tuning.GustSpeedMs);
					const double Granted = SpendChiForEnergy(Requested, false);
					if (Granted < 0.5 * Requested)
					{
						Player.HoldTechnique = ETechnique::None;
						break;
					}
					Air.LocationCm = Origin + AimDirection * Air.RadiusCm;
					Air.VelocityCmS = AimDirection * MToCm(Tuning.GustSpeedMs);
					SpawnProjectile(EProjectileKind::Air, Air);
				}
			}
			break;
		}

		case ETechnique::GroundFlame:
		{
			if (HoldProjectile < 0 || !Projectiles[HoldProjectile].bAlive)
			{
				Player.HoldTechnique = ETechnique::None;
				break;
			}
			FProjectile& Flame = Projectiles[HoldProjectile];
			const double Requested = Tuning.GroundFlamePowerW * Dt;
			const double Granted = SpendChiForEnergy(Requested, true);
			Flame.SustainPowerW = Granted / Dt;
			// The bender drags the flame along the ground toward the aim.
			const FVec3 Target = ClampToRange(Player.AimPointCm, Tuning.EarthbendRangeCm);
			const FVec3 Step = MoveToward(Flat(Flame.LocationCm), Flat(Target), GroundFlameFollowCmS * Dt);
			Flame.VelocityCmS = (Step - Flat(Flame.LocationCm)) / Dt;
			Flame.LocationCm.X = Step.X;
			Flame.LocationCm.Y = Step.Y;
			break;
		}

		default:
			break;
		}
	}

	// ---------------------------------------------------------------------------------------------------- Player

	void FSandbox::UpdatePlayer(const FInput& Input, double Dt)
	{
		FVec3 CameraFlat = Flat(Input.CameraForward);
		CameraFlat = CameraFlat.Size() > SmallNumber ? CameraFlat.GetSafeNormal() : GetFacing();
		const FVec3 CameraRight(CameraFlat.Y, -CameraFlat.X, 0.0);
		FVec3 Wish = CameraFlat * Input.MoveForward + CameraRight * Input.MoveRight;
		if (Wish.Size() > 1.0)
		{
			Wish = Wish.GetSafeNormal();
		}

		const bool bCasting = Player.Phase == EPhase::Startup || Player.Phase == EPhase::Active
			|| (Player.HoldTechnique != ETechnique::None && Player.HoldTechnique != ETechnique::AirScooter);
		double MaxSpeed = Input.bSprint && !Player.bGuarding ? SprintSpeedCmS : WalkSpeedCmS;
		MaxSpeed *= bCasting ? CastingSpeedScale : 1.0;
		MaxSpeed *= Player.bGuarding ? GuardSpeedScale : 1.0;
		MaxSpeed *= Player.FlinchS > 0.0 ? 0.5 : 1.0;
		Player.Traction = Player.bGrounded ? World.GetSurfaceTractionMultiplierAt(Player.LocationCm) : 1.0;
		if (Player.bScooter)
		{
			// Riding air: fast, glides, and mud does not grip it.
			MaxSpeed = MToCm(Tuning.AirScooterSpeedMs);
			Player.Traction = 1.0;
		}

		FVec3 Horizontal = Flat(Player.VelocityCmS);
		const FVec3 Target = Wish * MaxSpeed;
		if (Player.DodgeS > 0.0)
		{
			// Rolling: carried at the roll's speed, easing off over its last third.
			Horizontal = Player.DodgeVelocityCmS * KMin(1.0, 0.35 + Player.DodgeS / (DodgeSeconds * 0.5));
		}
		else if (Player.DashTimeS > 0.0)
		{
			// Fire jets hold the dash velocity, trailing flame from the feet.
			Player.DashTimeS -= Dt;
			Horizontal = Player.DashVelocityCmS;
			Player.DashEmitTimerS -= Dt;
			if (Player.DashEmitTimerS <= 0.0)
			{
				Player.DashEmitTimerS = 0.05;
				const FVec3 Back = Player.DashVelocityCmS.GetSafeNormal() * -1.0;
				const double Heat = Tuning.FireJetFlameMassKg * FlameSpecificHeat * (1300.0 - World.Settings.AmbientTemperatureK);
				const double Granted = SpendChiForEnergy(Heat, true);
				if (Granted > 0.25 * Heat)
				{
					SpawnProjectile(EProjectileKind::Fire, MakeFlame(Tuning.FireJetFlameMassKg * Granted / Heat, 1300.0,
						Player.LocationCm + Back * 30.0 + FVec3(0.0, 0.0, 25.0), Back * 500.0 + FVec3(0.0, 0.0, -250.0)));
				}
			}
		}
		else if (Player.bScooter)
		{
			Horizontal = MoveToward(Horizontal, Wish.Size() > 0.01 ? Target : Horizontal * 0.985, MaxAccelerationCmS2 * 1.6 * Dt);
		}
		else if (Player.bGrounded)
		{
			if (Wish.Size() > 0.01)
			{
				// Mud costs push-off too, but less than it costs braking: you can still run, you just can't stop.
				Horizontal = MoveToward(Horizontal, Target, MaxAccelerationCmS2 * KLerp(1.0, Player.Traction, 0.5) * Dt);
			}
			else
			{
				Horizontal = MoveToward(Horizontal, FVec3(), BrakingCmS2 * Player.Traction * Dt);
			}
		}
		else if (Wish.Size() > 0.01)
		{
			Horizontal = MoveToward(Horizontal, Target, MaxAccelerationCmS2 * AirControl * Dt);
		}

		// Facing: toward the aim while bending or guarding, else toward motion.
		if (bCasting || Player.bGuarding)
		{
			const FVec3 ToAim = Flat(Player.AimPointCm - Player.LocationCm);
			if (ToAim.Size() > 1.0)
			{
				Player.YawRad = TurnToward(Player.YawRad, KAtan2(ToAim.Y, ToAim.X), TurnRateRadS * 1.5 * Dt);
			}
		}
		else if (Horizontal.Size() > 20.0)
		{
			Player.YawRad = TurnToward(Player.YawRad, KAtan2(Horizontal.Y, Horizontal.X), TurnRateRadS * Dt);
		}

		const bool bJumpPressed = Input.bJump && !bPreviousJump;
		bPreviousJump = Input.bJump;
		double VerticalSpeed = Player.VelocityCmS.Z;
		if (bJumpPressed && Player.bGrounded)
		{
			VerticalSpeed = JumpSpeedCmS;
			Player.bGrounded = false;
		}
		if (!Player.bGrounded)
		{
			VerticalSpeed -= GravityCmS2 * Dt;
		}

		FVec3 Next = Player.LocationCm + Horizontal * Dt + FVec3(0.0, 0.0, VerticalSpeed * Dt);

		// Ground too steep to stand on (a wall, a pillar) blocks walking up it; small ledges are stepped over.
		const double GroundAhead = Terrain.GetHeightAt(Next.X, Next.Y);
		const double Rise = GroundAhead - Player.LocationCm.Z;
		if ((Rise > 2.0 && Terrain.GetNormalAt(Next.X, Next.Y).Z < WalkableNormalZ) || Rise > MaxStepCm)
		{
			Next.X = Player.LocationCm.X;
			Next.Y = Player.LocationCm.Y;
			Horizontal = FVec3();
		}

		const double Ground = Terrain.GetHeightAt(Next.X, Next.Y);
		if (Player.bGrounded)
		{
			if (Next.Z <= Ground + MaxStepCm && Next.Z >= Ground - MaxStepCm)
			{
				Next.Z = Ground;
				VerticalSpeed = 0.0;
			}
			else if (Next.Z < Ground)
			{
				// Ground rose under the feet (a wall or pillar): ride it up.
				Next.Z = Ground;
				VerticalSpeed = 0.0;
			}
			else
			{
				Player.bGrounded = false;
			}
		}
		else if (Next.Z <= Ground)
		{
			Next.Z = Ground;
			VerticalSpeed = 0.0;
			Player.bGrounded = true;
		}

		// Bodies push back: static ones block, loose ones get shoved.
		for (int Index = 0; Index < NumBodies; ++Index)
		{
			FBody& Body = Bodies[Index];
			if (!Body.bAlive)
			{
				continue;
			}
			FVec3 CoreA, CoreB;
			BodyCore(Body, CoreA, CoreB);
			const FVec3 PlayerA = Next + FVec3(0.0, 0.0, PlayerRadiusCm);
			const FVec3 PlayerB = Next + FVec3(0.0, 0.0, PlayerHeightCm - PlayerRadiusCm);
			FVec3 OnPlayer, OnBody;
			ClosestPointsBetweenSegments(PlayerA, PlayerB, CoreA, CoreB, OnPlayer, OnBody);
			const double Reach = PlayerRadiusCm + BodyCollisionRadius(Body);
			const FVec3 Delta = OnPlayer - OnBody;
			const double D = Delta.Size();
			if (D >= Reach || D <= SmallNumber)
			{
				continue;
			}
			const FVec3 Normal = Delta / D;
			const double Penetration = Reach - D;
			const bool bImmovable = Body.bStatic || Body.bHeld || Body.Prop == EArenaProp::Brazier;
			const double PlayerShare = bImmovable ? 1.0 : Body.MassKg / (Body.MassKg + PlayerMassKg);
			Next += Normal * (Penetration * PlayerShare);
			if (!bImmovable)
			{
				Body.LocationCm -= Normal * (Penetration * (1.0 - PlayerShare));
				const double Approach = (Horizontal - Body.VelocityCmS).Dot(Normal);
				if (Approach < 0.0)
				{
					// Inelastic push: share the closing speed by mass.
					const double Impulse = -Approach * PlayerMassKg * Body.MassKg / (PlayerMassKg + Body.MassKg);
					Body.VelocityCmS -= Normal * (Impulse / Body.MassKg);
					Horizontal += Normal * (Impulse / PlayerMassKg);
				}
			}
			if (Normal.Z > 0.7)
			{
				// Standing on it.
				Player.bGrounded = true;
				VerticalSpeed = KMax(VerticalSpeed, 0.0);
			}
			else
			{
				const double Into = Horizontal.Dot(Normal);
				if (Into < 0.0)
				{
					Horizontal -= Flat(Normal) * Into;
				}
			}
		}

		// Stay inside the arena.
		const double MinX = Terrain.GetOriginCm().X + 100.0;
		const double MinY = Terrain.GetOriginCm().Y + 100.0;
		Next.X = KClamp(Next.X, MinX, MinX + Terrain.GetSizeXCm() - 200.0);
		Next.Y = KClamp(Next.Y, MinY, MinY + Terrain.GetSizeYCm() - 200.0);

		if (Player.bGrounded)
		{
			Player.StridePhase = WrapAngle(Player.StridePhase + Horizontal.Size() * Dt / 140.0 * Pi);
		}
		Player.LocationCm = Next;
		Player.VelocityCmS = FVec3(Horizontal.X, Horizontal.Y, VerticalSpeed);
	}

	void FSandbox::UpdateHand(double Dt)
	{
		Player.PreviousHandCm = Player.HandCm;
		const FVec3 Facing = GetFacing();
		const FVec3 Right(Facing.Y, -Facing.X, 0.0);
		const FVec3 Shoulder = GetChestCm() + Right * 22.0;
		FVec3 Reach = Facing * 35.0 + FVec3(0.0, 0.0, -10.0);
		const bool bCasting = Player.Phase == EPhase::Startup || Player.Phase == EPhase::Active || Player.HoldTechnique != ETechnique::None;
		if (bCasting || Whip.IsActive())
		{
			const FVec3 ToAim = (Player.AimPointCm - Shoulder).GetSafeNormal();
			if (!ToAim.IsZero())
			{
				// Startup draws the arm in; Active, holds and a held whip extend it toward the aim.
				Reach = ToAim * (Player.Phase == EPhase::Startup ? 20.0 : 60.0);
			}
		}
		Player.HandCm = Shoulder + Reach;
		if (Dt <= 0.0)
		{
			Player.PreviousHandCm = Player.HandCm;
		}
	}

	// ---------------------------------------------------------------------------------------------------- Earth

	void FSandbox::UpdateEarthWork(double Dt)
	{
		// The rock rises during the throw's startup.
		if (EarthWork.RockBody >= 0)
		{
			FBody& Rock = Bodies[EarthWork.RockBody];
			if (!Rock.bAlive)
			{
				EarthWork.RockBody = -1;
			}
			else if (Player.Move == ETechnique::RockThrow && Player.Phase == EPhase::Startup)
			{
				const double T = KSmoothStep(0.0, 1.0, GetPhaseProgress());
				const FVec3 Next = EarthWork.RockStartCm + (EarthWork.RockHoldCm - EarthWork.RockStartCm) * T;
				Rock.VelocityCmS = (Next - Rock.LocationCm) / Dt;
				Rock.LocationCm = Next;
			}
			else if (Player.Move != ETechnique::RockThrow)
			{
				// Interrupted: the rock drops.
				Rock.bHeld = false;
				EarthWork.RockBody = -1;
			}
		}

		if (EarthWork.Technique == ETechnique::EarthWall && EarthWork.RemainingM3 > 0.0)
		{
			const double Step = KMin(EarthWork.RateM3S * Dt, EarthWork.RemainingM3);
			const FTerrainEdit Edit = Terrain.MoveSoil(EarthWork.Source, EarthWork.Target, Step);
			EarthWork.RemainingM3 -= Step;
			EarthWorkMovedKg += Edit.MassKg;
			const double Work = KMax(Edit.PotentialEnergyChangeJ, 0.0);
			EarthWorkJ += Work;
			if (SpendChiForEnergy(Work, false) < 0.5 * Work)
			{
				EarthWork.RemainingM3 = 0.0;
			}
			if (EarthWork.RemainingM3 <= 0.0)
			{
				char Text[96];
				int Length = Append(Text, 0, sizeof(Text), "Wall raised: ");
				Length = AppendNumber(Text, Length, sizeof(Text), EarthWorkMovedKg / 1000.0, 1);
				Length = Append(Text, Length, sizeof(Text), " t of soil, ");
				Length = AppendNumber(Text, Length, sizeof(Text), EarthWorkJ / 1000.0, 0);
				Append(Text, Length, sizeof(Text), " kJ of work");
				AddMessage(Text);
				EarthWork.Technique = ETechnique::None;
			}
		}
	}

	// ---------------------------------------------------------------------------------------------------- Water

	void FSandbox::UpdateWhip(double Dt)
	{
		if (!Whip.IsActive())
		{
			return;
		}
		const FVec3 HandVelocity = (Player.HandCm - Player.PreviousHandCm) / KMax(Dt, 1e-4);
		Whip.SetBodyCenter(GetChestCm());
		Whip.SetControl(Player.HandCm, HandVelocity, Player.AimPointCm);
		Whip.PreStep(World, &Terrain, Dt);

		// Water flung off the tip at the snap flies on as spray.
		FWhipDrop Spray[FWaterWhip::MaxPendingSpray];
		const int Count = Whip.ConsumeSpray(Spray, FWaterWhip::MaxPendingSpray);
		for (int Index = 0; Index < Count; ++Index)
		{
			SpawnProjectile(EProjectileKind::Spray, MakeWaterSpray(Spray[Index].MassKg, Spray[Index].TemperatureK, Spray[Index].LocationCm,
				Spray[Index].VelocityCmS, Tuning.WhipSprayRadiusCm));
		}
	}

	void FSandbox::ReleaseWhip(bool bAsBlast)
	{
		FWhipDrop Drops[FWaterWhip::MaxSegments];
		const int Count = Whip.Release(World, Drops, FWaterWhip::MaxSegments);
		if (Count == 0)
		{
			return;
		}
		if (!bAsBlast)
		{
			double Total = 0.0;
			for (int Index = 0; Index < Count; ++Index)
			{
				SpawnProjectile(EProjectileKind::Water, MakeWaterBall(Drops[Index].MassKg, Drops[Index].TemperatureK, Drops[Index].LocationCm, Drops[Index].VelocityCmS));
				Total += Drops[Index].MassKg;
			}
			AddMessageNumber("Released ", Total, " kg of water");
			return;
		}
		double MassKg = 0.0;
		double HeatWeighted = 0.0;
		for (int Index = 0; Index < Count; ++Index)
		{
			MassKg += Drops[Index].MassKg;
			HeatWeighted += Drops[Index].MassKg * Drops[Index].TemperatureK;
		}
		const FVec3 Origin = GetEmitOriginCm();
		const FVec3 Direction = (Player.AimPointCm - Origin).GetSafeNormal();
		const double Requested = KineticEnergyJ(MassKg, Tuning.WaterBlastSpeedMs);
		const double Granted = SpendChiForEnergy(Requested, false);
		const double SpeedMs = Tuning.WaterBlastSpeedMs * KSqrt(Granted / KMax(Requested, 1.0));
		FVolume Ball = MakeWaterBall(MassKg, HeatWeighted / KMax(MassKg, 1e-6), Origin, (Direction.IsZero() ? GetFacing() : Direction) * MToCm(SpeedMs));
		Ball.LocationCm = Origin + (Direction.IsZero() ? GetFacing() : Direction) * (Ball.RadiusCm + 20.0);
		SpawnProjectile(EProjectileKind::Water, Ball);
		char Text[96];
		int Length = Append(Text, 0, sizeof(Text), "Water blast: ");
		Length = AppendNumber(Text, Length, sizeof(Text), MassKg, 1);
		Length = Append(Text, Length, sizeof(Text), " kg at ");
		Length = AppendNumber(Text, Length, sizeof(Text), SpeedMs, 0);
		Append(Text, Length, sizeof(Text), " m/s");
		AddMessage(Text);
	}

	// ---------------------------------------------------------------------------------------------------- Fire props

	void FSandbox::UpdateBraziers(double Dt)
	{
		for (int Index = 0; Index < NumBodies; ++Index)
		{
			FBody& Body = Bodies[Index];
			if (!Body.bAlive || Body.Prop != EArenaProp::Brazier || Body.Kind != EBodyKind::Prop)
			{
				continue;
			}
			if (Body.bLit)
			{
				if (World.IsDepleted(Body.Volume) || !World.IsValid(Body.Volume))
				{
					World.RemoveVolume(Body.Volume);
					Body.Volume = FHandle();
					Body.bLit = false;
					AddMessage("A brazier went out");
					continue;
				}
				World.TransferHeat(Body.Volume, BrazierPowerW * Dt);
				continue;
			}
			// Unlit: a passing flame relights it.
			const FVec3 FlameSpot = Body.AnchorCm + FVec3(0.0, 0.0, GetArenaPropSpec(EArenaProp::Brazier).CenterHeightCm);
			for (const FProjectile& Projectile : Projectiles)
			{
				if (Projectile.bAlive && (Projectile.Kind == EProjectileKind::Fire || Projectile.Kind == EProjectileKind::GroundFlame)
					&& Distance(Projectile.LocationCm, FlameSpot) < RelightRadiusCm)
				{
					Body.Volume = World.AddVolume(MakeArenaPropVolume(EArenaProp::Brazier, Body.AnchorCm), true);
					Body.bLit = Body.Volume.IsSet();
					if (Body.bLit)
					{
						AddMessage("Brazier relit");
					}
					break;
				}
			}
		}
	}

	// ---------------------------------------------------------------------------------------------------- Burning and smashing

	void FSandbox::GetCombustionPoints(const FBody& Body, FVec3& OutCenterCm, FVec3& OutFlameCm) const
	{
		const FArenaPropSpec& Spec = GetArenaPropSpec(Body.Prop);
		if (Body.Prop == EArenaProp::Banner)
		{
			// The cloth hangs off the pole's cross-bar; it burns from the bottom edge up.
			const FVec3 Offset(KCos(Body.YawRad + 0.5 * Pi) * 4.0, KSin(Body.YawRad + 0.5 * Pi) * 4.0, 0.0);
			OutCenterCm = Body.AnchorCm + Offset + FVec3(0.0, 0.0, 0.5 * (BannerClothTopCm + BannerClothBottomCm));
			const double Edge = BannerClothBottomCm + (BannerClothTopCm - BannerClothBottomCm) * KMin(Body.Burn.GetBurntFraction(Spec.Combustion), 0.92);
			OutFlameCm = Body.AnchorCm + Offset + FVec3(0.0, 0.0, Edge + 25.0);
			return;
		}
		if (Body.bStatic)
		{
			OutCenterCm = Body.LocationCm;
			OutFlameCm = Body.AnchorCm + FVec3(0.0, 0.0, Spec.FlameHeightCm);
			if (Body.Prop == EArenaProp::Lantern)
			{
				OutCenterCm = OutFlameCm;
			}
			return;
		}
		OutCenterCm = Body.LocationCm;
		OutFlameCm = Body.LocationCm + FVec3(0.0, 0.0, Spec.FlameHeightCm - Spec.CenterHeightCm);
	}

	void FSandbox::AddPropEffect(ESandboxEffect Effect, const FBody& Body, const FVec3& LocationCm, double EnergyJ)
	{
		AddEffect(Effect, LocationCm, EnergyJ);
		FrameEvents[NumFrameEvents - 1].MassKg = static_cast<double>(Body.Prop);
	}

	void FSandbox::UpdateCombustion(double Dt)
	{
		for (int Index = 0; Index < NumBodies; ++Index)
		{
			FBody& Body = Bodies[Index];
			if (!Body.bAlive || Body.Kind != EBodyKind::Prop)
			{
				continue;
			}
			const FArenaPropSpec& Spec = GetArenaPropSpec(Body.Prop);
			if (!Spec.Combustion.IsFlammable())
			{
				continue;
			}
			FVec3 Center;
			FVec3 FlameSpot;
			GetCombustionPoints(Body, Center, FlameSpot);
			const bool bLantern = Body.Prop == EArenaProp::Lantern;
			const double HeatBefore = Body.Burn.HeatJ;
			const ECombustionEvent Event = Body.Burn.Update(World, Spec.Combustion, Center, FlameSpot, Dt);
			if (Body.Prop == EArenaProp::Dummy)
			{
				// Fire hurts a dummy as it heats it (catching takes the rest of its ignition heat), and keeps hurting
				// while it burns.
				double Soaked = Event == ECombustionEvent::Ignited ? KMax(Spec.Combustion.IgnitionJ - HeatBefore, 0.0) : KMax(Body.Burn.HeatJ - HeatBefore, 0.0);
				if (Body.Burn.bBurning && Event != ECombustionEvent::Ignited)
				{
					// Already alight, it still feels every other flame that hits it.
					Soaked += FCombustible::MeasureHeatingW(World, Spec.Combustion, Center, Body.Burn.Flame) * Dt;
				}
				DamageBody(Index, Soaked / 2500.0 + (Body.Burn.bBurning ? 7.0 * Dt : 0.0), FVec3());
			}
			char Text[96];
			switch (Event)
			{
			case ECombustionEvent::Ignited:
				AddPropEffect(ESandboxEffect::Ignite, Body, FlameSpot, 0.0);
				if (bLantern)
				{
					AddMessage("Lantern lit");
				}
				else
				{
					Append(Text, Append(Text, 0, sizeof(Text), Spec.Name), sizeof(Text), " caught fire!");
					AddMessage(Text);
				}
				break;

			case ECombustionEvent::Extinguished:
				AddPropEffect(ESandboxEffect::Douse, Body, FlameSpot, 0.0);
				if (bLantern)
				{
					AddMessage("Lantern put out");
				}
				else
				{
					Append(Text, Append(Text, 0, sizeof(Text), "Water put out the "), sizeof(Text), Spec.Name);
					AddMessage(Text);
				}
				break;

			case ECombustionEvent::BurntOut:
				AddPropEffect(ESandboxEffect::BurntOut, Body, FlameSpot, Body.Burn.ReleasedJ);
				Append(Text, Append(Text, 0, sizeof(Text), Spec.Name), sizeof(Text), " burnt away");
				AddMessage(Text);
				// A burnt crate falls apart; banners keep their pole, straw leaves ash, dummies stand charred.
				if (Body.Prop == EArenaProp::Crate)
				{
					RemoveBody(Index);
				}
				break;

			default:
				break;
			}
			if (Body.Prop == EArenaProp::Dummy && Event == ECombustionEvent::BurntOut)
			{
				// Burnt down: it lies there charred for a while, then comes back whole.
				if (Body.KnockoutS > 0.0)
				{
					Body.KnockoutS = KnockoutSeconds;
				}
				else
				{
					DamageBody(Index, Body.Health, FVec3(), true);
				}
			}
		}
	}

	void FSandbox::DamageBody(int Index, double Amount, const FVec3& FromDirection, bool bKnockOut)
	{
		if (Index < 0 || Index >= NumBodies || Amount <= 0.0)
		{
			return;
		}
		FBody& Body = Bodies[Index];
		const bool bRival = Body.Kind == EBodyKind::Rival;
		const bool bDummy = Body.Kind == EBodyKind::Prop && Body.Prop == EArenaProp::Dummy;
		if (!Body.bAlive || (!bDummy && !bRival) || Body.KnockoutS > 0.0)
		{
			return;
		}
		if (bRival)
		{
			// The sparring partner's brain decides what reaches it (a challenge, a guard, the per-blow cap).
			FSparringEvents Events;
			const double Taken = Rival.Brain.TakeDamage(Amount, bKnockOut, Events);
			Body.Health = Rival.Brain.Health;
			Body.FrameDamage += Taken;
			if (Events.bKnockedOut)
			{
				Body.KnockoutS = Rival.Brain.DownS;
				const FVec3 Flat2(FromDirection.X, FromDirection.Y, 0.0);
				Body.FallDirection = Flat2.Size() > 1e-6 ? Flat2.GetSafeNormal() : FVec3(1.0, 0.0, 0.0);
				AddPropEffect(ESandboxEffect::Hit, Body, Body.LocationCm + FVec3(0.0, 0.0, 60.0), Body.PendingDamage + Body.FrameDamage);
				Body.FrameDamage = 0.0;
				Body.PendingDamage = 0.0;
				Body.PendingAgeS = 0.0;
				AddPropEffect(ESandboxEffect::Knockout, Body, Body.LocationCm + FVec3(0.0, 0.0, 60.0), 0.0);
			}
			HandleRivalEvents(Events);
			return;
		}
		if (bKnockOut)
		{
			Amount = Body.Health;
		}
		if (Body.ProtectS > 0.0 && !bKnockOut)
		{
			return;
		}
		// No one takes more than the health they have left.
		Amount = KMin(Amount, Body.Health);
		if (Amount <= 0.0)
		{
			return;
		}
		Body.Health -= Amount;
		Body.FrameDamage += Amount;
		if (Body.Health > 1e-6)
		{
			return;
		}
		Body.Health = 0.0;
		Body.KnockoutS = KnockoutSeconds;
		const FVec3 Flat2(FromDirection.X, FromDirection.Y, 0.0);
		Body.FallDirection = Flat2.Size() > 1e-6 ? Flat2.GetSafeNormal() : FVec3(1.0, 0.0, 0.0);
		AddPropEffect(ESandboxEffect::Hit, Body, Body.LocationCm + FVec3(0.0, 0.0, 60.0), Body.PendingDamage + Body.FrameDamage);
		Body.FrameDamage = 0.0;
		Body.PendingDamage = 0.0;
		Body.PendingAgeS = 0.0;
		AddPropEffect(ESandboxEffect::Knockout, Body, Body.LocationCm + FVec3(0.0, 0.0, 60.0), 0.0);
		AddMessage("Dummy knocked out!");
	}

	void FSandbox::UpdateDummies(double Dt)
	{
		for (int Index = 0; Index < NumBodies; ++Index)
		{
			FBody& Body = Bodies[Index];
			const bool bRival = Body.Kind == EBodyKind::Rival;
			if (!Body.bAlive || ((Body.Kind != EBodyKind::Prop || Body.Prop != EArenaProp::Dummy) && !bRival))
			{
				continue;
			}
			// A blow that lands over several frames is reported as one hit, once it has gone quiet (or every half
			// second for damage that keeps coming, like burning).
			if (Body.FrameDamage > 0.0)
			{
				Body.PendingDamage += Body.FrameDamage;
				Body.FrameDamage = 0.0;
				Body.QuietS = 0.0;
			}
			else
			{
				Body.QuietS += Dt;
			}
			if (Body.PendingDamage > 0.0)
			{
				Body.PendingAgeS += Dt;
				if (Body.QuietS >= 0.12 || Body.PendingAgeS >= 0.5)
				{
					if (Body.PendingDamage >= 1.0)
					{
						AddPropEffect(ESandboxEffect::Hit, Body, Body.LocationCm + FVec3(0.0, 0.0, 60.0), Body.PendingDamage);
					}
					Body.PendingDamage = 0.0;
					Body.PendingAgeS = 0.0;
				}
			}
			Body.ProtectS = KMax(Body.ProtectS - Dt, 0.0);
			// The sparring partner gets up on its own terms (UpdateRival).
			if (Body.KnockoutS <= 0.0 || bRival)
			{
				continue;
			}
			// Still burning, it stays down until the fire is out (or burnt through).
			Body.KnockoutS = Body.Burn.bBurning ? KMax(Body.KnockoutS - Dt, 1.0) : Body.KnockoutS - Dt;
			if (Body.KnockoutS > 0.0)
			{
				continue;
			}
			// Back on its feet where it first stood, whole again (unburnt, full health).
			Body.KnockoutS = 0.0;
			Body.Health = DummyMaxHealth;
			Body.FrameDamage = 0.0;
			Body.PendingDamage = 0.0;
			Body.PendingAgeS = 0.0;
			Body.Burn.RemoveFlame(World);
			Body.Burn = FCombustible();
			// Where it first stood, or the nearest clear spot (a rock may have come to rest there).
			FVec3 Spot = Body.HomeCm;
			for (int Ring = 0; Ring <= 4; ++Ring)
			{
				bool bFound = false;
				for (int Step = 0; Step < (Ring == 0 ? 1 : 8) && !bFound; ++Step)
				{
					const double Angle = Step * Pi / 4.0;
					const FVec3 Try = Body.HomeCm + FVec3(KCos(Angle), KSin(Angle), 0.0) * (Ring * 90.0);
					bool bClear = true;
					for (int Other = 0; Other < NumBodies && bClear; ++Other)
					{
						const FBody& O = Bodies[Other];
						bClear = Other == Index || !O.bAlive || Distance(FVec3(O.LocationCm.X, O.LocationCm.Y, 0.0), FVec3(Try.X, Try.Y, 0.0)) > O.RadiusCm + Body.RadiusCm + 20.0;
					}
					if (bClear)
					{
						Spot = Try;
						Spot.Z = Terrain.GetHeightAt(Try.X, Try.Y) + (Body.HomeCm.Z - Terrain.GetHeightAt(Body.HomeCm.X, Body.HomeCm.Y));
						bFound = true;
					}
				}
				if (bFound)
				{
					break;
				}
			}
			Body.LocationCm = Spot;
			Body.VelocityCmS = FVec3();
			Body.ProtectS = 1.5;
			Body.Tilt[0] = Body.Tilt[1] = 0.0;
			Body.TiltRate[0] = Body.TiltRate[1] = 0.0;
			AddPropEffect(ESandboxEffect::Respawn, Body, Body.HomeCm, 0.0);
		}
	}

	void FSandbox::ProcessBreaks()
	{
		for (int Index = 0; Index < NumBodies; ++Index)
		{
			if (Bodies[Index].bAlive && Bodies[Index].bBreakPending)
			{
				SmashBody(Index);
			}
		}
	}

	void FSandbox::SmashBody(int Index)
	{
		FBody& Body = Bodies[Index];
		const FArenaPropSpec& Spec = GetArenaPropSpec(Body.Prop);
		const FVec3 Spot = Body.LocationCm;
		const double Ground = Terrain.GetHeightAt(Spot.X, Spot.Y);

		// A barrel's water flies out in a ring and soaks everything round it.
		double SpilledKg = 0.0;
		if (Body.WaterKg > 0.5)
		{
			SpilledKg = Body.WaterKg;
			for (int Parcel = 0; Parcel < SpillParcels; ++Parcel)
			{
				const double Angle = 2.0 * Pi * Parcel / SpillParcels + 0.4;
				const FVec3 Out(KCos(Angle), KSin(Angle), 0.0);
				FVolume Water = MakeWaterBall(SpilledKg / SpillParcels, 288.15, Spot + Out * 30.0 + FVec3(0.0, 0.0, 20.0),
					Out * 320.0 + Body.VelocityCmS * 0.5 + FVec3(0.0, 0.0, 260.0));
				SpawnProjectile(EProjectileKind::Water, Water);
			}
		}
		// A burning crate scatters its fire on the ground.
		if (Body.Burn.bBurning)
		{
			FVolume Flame = MakeFlame(Spec.Combustion.FlameMassKg, Spec.Combustion.FlameTemperatureK, FVec3(Spot.X, Spot.Y, Ground), FVec3());
			Flame.LocationCm.Z = Ground + 0.55 * Flame.RadiusCm;
			const int Dropped = SpawnProjectile(EProjectileKind::GroundFlame, Flame);
			if (Dropped >= 0)
			{
				Projectiles[Dropped].bLanded = true;
				Projectiles[Dropped].FuelJ = Tuning.GroundFlameFuelJ;
			}
		}
		AddPropEffect(ESandboxEffect::Smash, Body, Spot, SpilledKg);
		if (SpilledKg > 0.0)
		{
			AddMessageNumber("The barrel burst: ", SpilledKg, " kg of water");
		}
		else
		{
			char Text[96];
			Append(Text, Append(Text, 0, sizeof(Text), Spec.Name), sizeof(Text), " smashed");
			AddMessage(Text);
		}
		RemoveBody(Index);
	}

	int FSandbox::FindWaterBarrel(const FVec3& FromCm, double MinWaterKg, FVec3& OutSourceCm) const
	{
		int Best = -1;
		double BestDistance = BarrelDrawRangeCm;
		for (int Index = 0; Index < NumBodies; ++Index)
		{
			const FBody& Body = Bodies[Index];
			if (!Body.bAlive || Body.Kind != EBodyKind::Prop || Body.Prop != EArenaProp::WaterBarrel || Body.WaterKg < MinWaterKg)
			{
				continue;
			}
			const FVec3 Top = Body.LocationCm + FVec3(0.0, 0.0, BarrelWaterHeightCm - GetArenaPropSpec(Body.Prop).CenterHeightCm);
			const double D = Distance(Top, FromCm);
			if (D < BestDistance)
			{
				BestDistance = D;
				Best = Index;
				OutSourceCm = Top;
			}
		}
		return Best;
	}

	// ---------------------------------------------------------------------------------------------------- Projectiles

	void FSandbox::UpdateProjectiles(double Dt)
	{
		for (int Index = 0; Index < MaxProjectiles; ++Index)
		{
			FProjectile& Projectile = Projectiles[Index];
			if (!Projectile.bAlive)
			{
				continue;
			}
			const FVolume* Volume = World.GetVolume(Projectile.Volume);
			if (!Volume || World.IsDepleted(Projectile.Volume))
			{
				RemoveProjectile(Index);
				continue;
			}
			Projectile.AgeS += Dt;
			const double Radius = Volume->RadiusCm;
			const double MassKg = Volume->MassKg;
			const double Ground = Terrain.GetHeightAt(Projectile.LocationCm.X, Projectile.LocationCm.Y);

			switch (Projectile.Kind)
			{
			case EProjectileKind::Fire:
				if (!Projectile.bLanded)
				{
					Projectile.LocationCm += Projectile.VelocityCmS * Dt;
					const double GroundHere = Terrain.GetHeightAt(Projectile.LocationCm.X, Projectile.LocationCm.Y);
					if (Projectile.LocationCm.Z - 0.4 * Radius <= GroundHere)
					{
						// Flame splashes onto the ground and burns there until it cools.
						Projectile.bLanded = true;
						Projectile.VelocityCmS = FVec3();
						Projectile.LocationCm.Z = GroundHere + 0.55 * Radius;
					}
				}
				else
				{
					Projectile.LocationCm.Z = Ground + 0.55 * Radius;
				}
				if (Projectile.AgeS > Tuning.ProjectileLifetimeS)
				{
					RemoveProjectile(Index);
				}
				break;

			case EProjectileKind::GroundFlame:
				Projectile.LocationCm.Z = Ground + 0.55 * Radius;
				if (Projectile.SustainPowerW > 0.0)
				{
					World.TransferHeat(Projectile.Volume, Projectile.SustainPowerW * Dt);
				}
				else if (Projectile.FuelJ > 0.0)
				{
					// Released, it burns what it lit until that runs out; water still puts it out at once.
					const double Burn = KMin(Projectile.FuelJ, Tuning.GroundFlameFuelPowerW * Dt);
					World.TransferHeat(Projectile.Volume, Burn);
					Projectile.FuelJ -= Burn;
				}
				break;

			case EProjectileKind::Air:
			{
				Projectile.VelocityCmS *= KExp(-Dt / KMax(Tuning.AirDecayTimeS, 0.05));
				Projectile.LocationCm += Projectile.VelocityCmS * Dt;
				const double GroundHere = Terrain.GetHeightAt(Projectile.LocationCm.X, Projectile.LocationCm.Y);
				if (Projectile.LocationCm.Z - 0.5 * Radius < GroundHere)
				{
					// Air meeting the ground flows along it.
					Projectile.LocationCm.Z = GroundHere + 0.5 * Radius;
					const FVec3 Normal = Terrain.GetNormalAt(Projectile.LocationCm.X, Projectile.LocationCm.Y);
					const double Into = Projectile.VelocityCmS.Dot(Normal);
					if (Into < 0.0)
					{
						Projectile.VelocityCmS -= Normal * Into;
					}
				}
				if (Projectile.AgeS > Tuning.AirLifetimeS)
				{
					RemoveProjectile(Index);
				}
				break;
			}

			case EProjectileKind::IceShard:
			{
				// A bender carries the daggers: they drop only slowly.
				Projectile.VelocityCmS.Z -= 0.3 * GravityCmS2 * Dt;
				Projectile.LocationCm += Projectile.VelocityCmS * Dt;
				bool bHit = false;
				for (int BodyIndex = 0; BodyIndex < NumBodies && !bHit; ++BodyIndex)
				{
					const FBody& Body = Bodies[BodyIndex];
					if (!Body.bAlive || Body.bHeld)
					{
						continue;
					}
					FVec3 To = Projectile.LocationCm - Body.LocationCm;
					if (Body.HalfHeightCm > 0.0)
					{
						To.Z -= KClamp(To.Z, -Body.HalfHeightCm, Body.HalfHeightCm);
					}
					if (To.Size() < Body.RadiusCm + Radius)
					{
						// It strikes and shatters: its momentum goes into what it hit.
						PushBody(BodyIndex, Projectile.VelocityCmS * MassKg);
						// A blade of ice does more than its momentum.
						DamageBody(BodyIndex, 15.0, Projectile.VelocityCmS);
						bHit = true;
					}
				}
				const double GroundHere = Terrain.GetHeightAt(Projectile.LocationCm.X, Projectile.LocationCm.Y);
				if (bHit || Projectile.LocationCm.Z - Radius <= GroundHere || Projectile.AgeS > 3.0)
				{
					AddEffect(ESandboxEffect::IceShatter, Projectile.LocationCm, KineticEnergyJ(MassKg, CmToM(Projectile.VelocityCmS.Size())));
					RemoveProjectile(Index);
				}
				break;
			}

			case EProjectileKind::Water:
			case EProjectileKind::Spray:
			{
				if (Projectile.Kind == EProjectileKind::Spray)
				{
					// Droplets lose their speed to the air within a few metres.
					Projectile.VelocityCmS *= KExp(-Dt / KMax(Tuning.WhipSprayDragTimeS, 0.02));
				}
				Projectile.VelocityCmS.Z -= GravityCmS2 * Dt;
				Projectile.LocationCm += Projectile.VelocityCmS * Dt;
				const double GroundHere = Terrain.GetHeightAt(Projectile.LocationCm.X, Projectile.LocationCm.Y);
				const int Pond = FindPondAt(Projectile.LocationCm);
				const double Floor = Pond >= 0 ? Layout.Ponds[Pond].SurfaceHeightCm : GroundHere;
				if (Projectile.LocationCm.Z - Radius <= Floor)
				{
					const FVec3 Splash(Projectile.LocationCm.X, Projectile.LocationCm.Y, Floor);
					if (Pond >= 0)
					{
						Layout.Ponds[Pond].WaterKg += MassKg;
					}
					else
					{
						World.DepositWaterOnSurface(Splash, MassKg, Terrain.SoilPorosity);
					}
					if (NumFrameEvents < MaxFrameEvents)
					{
						FSandboxEvent& Event = FrameEvents[NumFrameEvents++];
						Event = FSandboxEvent();
						Event.Type = EReactionType::Saturation;
						Event.LocationCm = Splash;
						Event.MassKg = MassKg;
						Event.bDiscrete = true;
					}
					RemoveProjectile(Index);
				}
				else if (Projectile.AgeS > 6.0)
				{
					RemoveProjectile(Index);
				}
				break;
			}
			}

			if (Projectile.bAlive && !Terrain.IsInside(Projectile.LocationCm.X, Projectile.LocationCm.Y))
			{
				RemoveProjectile(Index);
			}
		}
	}

	// ---------------------------------------------------------------------------------------------------- Bodies

	void FSandbox::UpdateBodies(double Dt)
	{
		for (int Index = 0; Index < NumBodies; ++Index)
		{
			FBody& Body = Bodies[Index];
			if (!Body.bAlive)
			{
				continue;
			}
			if (Body.Prop == EArenaProp::Dummy)
			{
				// Weighted base: the dummy rocks back upright; knocked out, it topples over the way it was hit.
				const double Target[2] = { Body.KnockoutS > 0.0 ? -Body.FallDirection.Y * 1.45 : 0.0, Body.KnockoutS > 0.0 ? Body.FallDirection.X * 1.45 : 0.0 };
				for (int Axis = 0; Axis < 2; ++Axis)
				{
					Body.TiltRate[Axis] += (-40.0 * (Body.Tilt[Axis] - Target[Axis]) - (Body.KnockoutS > 0.0 ? 9.0 : 4.0) * Body.TiltRate[Axis]) * Dt;
					Body.Tilt[Axis] = KClamp(Body.Tilt[Axis] + Body.TiltRate[Axis] * Dt, -1.5, 1.5);
				}
			}
			if (Body.bStatic || Body.bHeld || (Body.Prop == EArenaProp::Brazier && Body.Kind == EBodyKind::Prop))
			{
				continue;
			}

			Body.VelocityCmS.Z -= GravityCmS2 * Dt;
			Body.LocationCm += Body.VelocityCmS * Dt;

			const double Ground = Terrain.GetHeightAt(Body.LocationCm.X, Body.LocationCm.Y);
			const double Traction = World.GetSurfaceTractionMultiplierAt(FVec3(Body.LocationCm.X, Body.LocationCm.Y, Ground));
			if (Body.HalfHeightCm > 0.0)
			{
				// Upright capsule (dummy): stands and slides.
				const double Bottom = Body.LocationCm.Z - Body.HalfHeightCm - Body.RadiusCm;
				if (Bottom <= Ground)
				{
					Body.LocationCm.Z += Ground - Bottom;
					Body.VelocityCmS.Z = KMax(Body.VelocityCmS.Z, 0.0);
					FVec3 Horizontal = Flat(Body.VelocityCmS);
					Horizontal = MoveToward(Horizontal, FVec3(), 0.5 * Traction * GravityCmS2 * Dt);
					Body.VelocityCmS = FVec3(Horizontal.X, Horizontal.Y, Body.VelocityCmS.Z);
				}
				continue;
			}

			// Sphere on the ground: contact along the surface normal, bounce, roll.
			const FVec3 Normal = Terrain.GetNormalAt(Body.LocationCm.X, Body.LocationCm.Y);
			const double Penetration = Body.RadiusCm - (Body.LocationCm.Z - Ground) * Normal.Z;
			if (Penetration > 0.0)
			{
				Body.LocationCm += Normal * Penetration;
				const double Into = Body.VelocityCmS.Dot(Normal);
				if (Into < 0.0)
				{
					Body.VelocityCmS -= Normal * (Into * (Into < -150.0 ? 1.25 : 1.0));
					// Thrown down hard, it smashes on the ground.
					const double BreakSpeedMs = Body.Kind == EBodyKind::Prop ? GetArenaPropSpec(Body.Prop).BreakSpeedMs : 0.0;
					if (BreakSpeedMs > 0.0 && -Into > MToCm(BreakSpeedMs))
					{
						Body.bBreakPending = true;
					}
				}
				FVec3 Tangent = Body.VelocityCmS - Normal * Body.VelocityCmS.Dot(Normal);
				// Rolling resistance, higher in mud.
				const double Resistance = (0.08 + 0.25 * (1.0 - Traction)) * GravityCmS2;
				Tangent = MoveToward(Tangent, FVec3(), Resistance * Dt);
				Body.VelocityCmS = Tangent + Normal * Body.VelocityCmS.Dot(Normal);
				IntegrateRotation(Body.Rotation, Normal.Cross(Tangent) / KMax(Body.RadiusCm, 1.0), Dt);
			}
			else
			{
				IntegrateRotation(Body.Rotation, FVec3(), Dt);
			}
		}

		// Body against body.
		for (int I = 0; I < NumBodies; ++I)
		{
			FBody& A = Bodies[I];
			if (!A.bAlive)
			{
				continue;
			}
			for (int J = I + 1; J < NumBodies; ++J)
			{
				FBody& B = Bodies[J];
				if (!B.bAlive)
				{
					continue;
				}
				FVec3 A0, A1, B0, B1;
				BodyCore(A, A0, A1);
				BodyCore(B, B0, B1);
				FVec3 OnA, OnB;
				ClosestPointsBetweenSegments(A0, A1, B0, B1, OnA, OnB);
				const FVec3 Delta = OnB - OnA;
				const double D = Delta.Size();
				const double Reach = BodyCollisionRadius(A) + BodyCollisionRadius(B);
				if (D >= Reach || D <= SmallNumber)
				{
					continue;
				}
				const auto InverseMass = [](const FBody& Body)
				{
					const bool bFixed = Body.bStatic || Body.bHeld || (Body.Prop == EArenaProp::Brazier && Body.Kind == EBodyKind::Prop);
					return bFixed ? 0.0 : 1.0 / KMax(Body.MassKg, 0.01);
				};
				const double InvA = InverseMass(A);
				const double InvB = InverseMass(B);
				if (InvA + InvB <= 0.0)
				{
					continue;
				}
				const FVec3 Normal = Delta / D;
				const double Penetration = Reach - D;
				A.LocationCm -= Normal * (Penetration * InvA / (InvA + InvB));
				B.LocationCm += Normal * (Penetration * InvB / (InvA + InvB));
				const double Closing = (B.VelocityCmS - A.VelocityCmS).Dot(Normal);
				if (Closing < 0.0)
				{
					const double Impulse = -1.2 * Closing / (InvA + InvB);
					A.VelocityCmS -= Normal * (Impulse * InvA);
					B.VelocityCmS += Normal * (Impulse * InvB);
					// Struck hard enough, a crate or barrel smashes.
					FBody* const Pair[2] = { &A, &B };
					const double PairInverseMass[2] = { InvA, InvB };
					for (int Side = 0; Side < 2; ++Side)
					{
						FBody& Struck = *Pair[Side];
						const double BreakSpeedMs = Struck.Kind == EBodyKind::Prop ? GetArenaPropSpec(Struck.Prop).BreakSpeedMs : 0.0;
						if (BreakSpeedMs > 0.0 && Impulse * PairInverseMass[Side] > MToCm(BreakSpeedMs))
						{
							Struck.bBreakPending = true;
						}
					}
					if (A.Prop == EArenaProp::Dummy)
					{
						A.TiltRate[0] -= Normal.Y * Impulse * InvA * 0.01;
						A.TiltRate[1] += Normal.X * Impulse * InvA * 0.01;
						DamageBody(I, DamagePerMs * CmToM(Impulse * InvA), Normal * -1.0);
					}
					if (B.Prop == EArenaProp::Dummy)
					{
						B.TiltRate[0] += Normal.Y * Impulse * InvB * 0.01;
						B.TiltRate[1] -= Normal.X * Impulse * InvB * 0.01;
						DamageBody(J, DamagePerMs * CmToM(Impulse * InvB), Normal);
					}
				}
			}
		}

		for (int Index = 0; Index < NumBodies; ++Index)
		{
			FBody& Body = Bodies[Index];
			if (Body.bAlive && !Terrain.IsInside(Body.LocationCm.X, Body.LocationCm.Y))
			{
				if ((Body.Kind == EBodyKind::Prop && Body.Prop == EArenaProp::Dummy) || Body.Kind == EBodyKind::Rival)
				{
					// Flung off the field: it lies at the edge, out, until it gets back up where it stood.
					Body.LocationCm = FVec3(KClamp(Body.LocationCm.X, Layout.OriginCm.X + 50.0, -Layout.OriginCm.X - 50.0),
						KClamp(Body.LocationCm.Y, Layout.OriginCm.Y + 50.0, -Layout.OriginCm.Y - 50.0), Body.LocationCm.Z);
					Body.VelocityCmS = FVec3();
					if (Body.KnockoutS <= 0.0)
					{
						DamageBody(Index, Body.Health, FVec3(1.0, 0.0, 0.0), true);
					}
					continue;
				}
				RemoveBody(Index);
			}
		}
	}

	// ---------------------------------------------------------------------------------------------------- Kernel sync

	void FSandbox::SyncVolumesIn()
	{
		for (int Index = 0; Index < NumBodies; ++Index)
		{
			FBody& Body = Bodies[Index];
			FVolume* Volume = Body.bAlive ? World.GetVolume(Body.Volume) : nullptr;
			if (!Volume)
			{
				continue;
			}
			if (Body.Prop == EArenaProp::Brazier && Body.Kind == EBodyKind::Prop)
			{
				Volume->LocationCm = Body.AnchorCm + FVec3(0.0, 0.0, GetArenaPropSpec(EArenaProp::Brazier).CenterHeightCm);
				Volume->VelocityCmS = FVec3();
				continue;
			}
			Volume->LocationCm = Body.LocationCm;
			Volume->VelocityCmS = Body.bStatic ? FVec3() : Body.VelocityCmS;
		}
		for (FProjectile& Projectile : Projectiles)
		{
			if (FVolume* Volume = Projectile.bAlive ? World.GetVolume(Projectile.Volume) : nullptr)
			{
				Volume->LocationCm = Projectile.LocationCm;
				Volume->VelocityCmS = Projectile.VelocityCmS;
			}
		}
	}

	void FSandbox::SyncVolumesOut()
	{
		for (int Index = 0; Index < NumBodies; ++Index)
		{
			FBody& Body = Bodies[Index];
			const FVolume* Volume = Body.bAlive ? World.GetVolume(Body.Volume) : nullptr;
			if (!Volume)
			{
				continue;
			}
			FOwnerUpdate Update;
			World.ConsumeOwnerUpdate(Body.Volume, Update);
			Body.MassKg = Volume->MassKg > 0.0 ? Volume->MassKg : Body.MassKg;
			const bool bMovable = !Body.bStatic && !Body.bHeld && !(Body.Prop == EArenaProp::Brazier && Body.Kind == EBodyKind::Prop);
			if (bMovable && !Update.FrameImpulseKgCmS.IsZero())
			{
				const FVec3 DeltaV = Update.FrameImpulseKgCmS / KMax(Body.MassKg, 0.01);
				Body.VelocityCmS += DeltaV;
				const double BreakSpeedMs = Body.Kind == EBodyKind::Prop ? GetArenaPropSpec(Body.Prop).BreakSpeedMs : 0.0;
				if (BreakSpeedMs > 0.0 && DeltaV.Size() > MToCm(BreakSpeedMs))
				{
					Body.bBreakPending = true;
				}
				if (Body.Prop == EArenaProp::Dummy)
				{
					// Pushed high on its body, the dummy rocks; struck, it takes damage.
					Body.TiltRate[0] -= DeltaV.Y * 0.02;
					Body.TiltRate[1] += DeltaV.X * 0.02;
					DamageBody(Index, DamagePerMs * CmToM(DeltaV.Size()), DeltaV);
				}
			}
			if (Body.Prop == EArenaProp::IceBlock && Volume->Substance == ESubstance::Water)
			{
				World.DepositWaterOnSurface(Body.AnchorCm, Volume->MassKg, Terrain.SoilPorosity);
				AddMessageNumber("An ice block melted: ", Volume->MassKg, " kg of meltwater soaks the ground");
				RemoveBody(Index);
			}
		}
		for (FProjectile& Projectile : Projectiles)
		{
			const FVolume* Volume = Projectile.bAlive ? World.GetVolume(Projectile.Volume) : nullptr;
			if (!Volume)
			{
				continue;
			}
			FOwnerUpdate Update;
			World.ConsumeOwnerUpdate(Projectile.Volume, Update);
			if (!Projectile.bLanded && Volume->MassKg > 1e-6)
			{
				Projectile.VelocityCmS += Update.FrameImpulseKgCmS / Volume->MassKg;
			}
		}
	}

	void FSandbox::CollectEvents(double FrameSeconds)
	{
		FReactionEvent Events[FSimWorld::MaxFlushedEvents];
		const int Count = World.FlushEvents(FrameSeconds, Events, FSimWorld::MaxFlushedEvents);
		bool bSaid[static_cast<int>(EReactionType::Count)] = {};
		for (int Index = 0; Index < Count; ++Index)
		{
			const FReactionEvent& Event = Events[Index];
			// Bent flames burning out in flight are not news (braziers report going out themselves).
			if (Event.Type == EReactionType::Extinguished && (FindProjectileByVolume(Event.VolumeA) >= 0 || FindBodyByVolume(Event.VolumeA)))
			{
				continue;
			}
			if (NumFrameEvents < MaxFrameEvents)
			{
				FSandboxEvent& Out = FrameEvents[NumFrameEvents++];
				// A fresh event: a slot reused from an earlier frame must not keep its old effect.
				Out = FSandboxEvent();
				Out.Type = Event.Type;
				Out.LocationCm = Event.LocationCm;
				Out.MassKg = Event.MassKg;
				Out.EnergyJ = Event.EnergyJ;
				Out.bDiscrete = Event.bDiscrete;
			}
			const int Type = static_cast<int>(Event.Type);
			if (!Event.bDiscrete || bSaid[Type] || IsWhipVolume(Event.VolumeA))
			{
				continue;
			}
			bSaid[Type] = true;
			switch (Event.Type)
			{
			case EReactionType::Extinguished: AddMessage("Fire extinguished"); break;
			case EReactionType::MudFormed:    AddMessage("Soil turned to mud"); break;
			case EReactionType::Melting:      AddMessage("Ice melted"); break;
			default: break;
			}
		}
	}
	// ---------------------------------------------------------------------------------------------------- Sparring

	FVec3 FSandbox::GetRivalFeetCm() const
	{
		if (Rival.Body < 0)
		{
			return Rival.Brain.PostCm;
		}
		const FBody& Body = Bodies[Rival.Body];
		return Body.LocationCm - FVec3(0.0, 0.0, Body.HalfHeightCm + Body.RadiusCm);
	}

	FVec3 FSandbox::GetRivalChestCm() const
	{
		return GetRivalFeetCm() + FVec3(0.0, 0.0, ChestHeightCm);
	}

	bool FSandbox::IsDuelActive() const
	{
		return Rival.Body >= 0 && Rival.Brain.IsDuelActive();
	}

	void FSandbox::SpawnRival()
	{
		FVec3 Post = Layout.SparringPostCm;
		Post.Z = Terrain.GetHeightAt(Post.X, Post.Y);
		const FVec3 ToStart = Flat(Layout.PlayerStartCm - Post);
		Rival.Brain.Reset(Post, KAtan2(ToStart.Y, ToStart.X));

		// A person-sized capsule: solid, it does not soak up water.
		FVolume Volume = FVolume::MakeDefault(ESubstance::Earth, RivalMassKg);
		Volume.Porosity = 0.0;
		Volume.bDeriveRadiusFromMass = false;
		Volume.RadiusCm = RivalRadiusCm;
		Volume.Shape = EShape::Capsule;
		Volume.CapsuleHalfAxisCm = FVec3(0.0, 0.0, RivalHalfHeightCm);
		Volume.DragCoefficient = 1.0;
		Volume.LocationCm = Post + FVec3(0.0, 0.0, RivalHalfHeightCm + RivalRadiusCm);

		FBody Body;
		Body.Kind = EBodyKind::Rival;
		// Struck, it rocks and falls like a training dummy.
		Body.Prop = EArenaProp::Dummy;
		Body.LocationCm = Volume.LocationCm;
		Body.AnchorCm = Post;
		Body.RadiusCm = RivalRadiusCm;
		Body.HalfHeightCm = RivalHalfHeightCm;
		Body.MassKg = RivalMassKg;
		Body.YawRad = Rival.Brain.YawRad;
		Body.Variant = static_cast<int>(Rival.Element);
		Body.Health = Rival.Brain.Health;
		Body.HomeCm = Volume.LocationCm;
		Rival.Body = AddBody(Body, Volume);
	}

	void FSandbox::HandleRivalEvents(const FSparringEvents& Events)
	{
		if (Rival.Body < 0)
		{
			return;
		}
		FBody& Body = Bodies[Rival.Body];
		const FSparringBrain& Brain = Rival.Brain;
		const FVec3 Above = GetRivalFeetCm() + FVec3(0.0, 0.0, 210.0);
		if (Events.bDuelStarted)
		{
			Body.Health = Brain.Health;
			Body.FrameDamage = 0.0;
			Body.PendingDamage = 0.0;
			Player.Health = Player.MaxHealth;
			AddEffect(ESandboxEffect::DuelStart, Above, 0.0);
			AddMessage("Duel! Knock your sparring partner down (hold C to guard)");
		}
		if (Events.bWindUp)
		{
			AddEffect(ESandboxEffect::RivalWindUp, Rival.HandCm, 0.0);
			FrameEvents[NumFrameEvents - 1].MassKg = static_cast<double>(Brain.Attack);
		}
		if (Events.Evaded > 0)
		{
			AddEffect(ESandboxEffect::RivalEvade, Body.LocationCm, 0.0);
			FrameEvents[NumFrameEvents - 1].MassKg = Events.Evaded;
		}
		if (Events.bGotUp)
		{
			AddPropEffect(ESandboxEffect::Respawn, Body, GetRivalFeetCm(), 0.0);
		}
		if (Events.DuelEnded < 0)
		{
			return;
		}
		AddEffect(ESandboxEffect::DuelEnd, Above, 0.0);
		FrameEvents[NumFrameEvents - 1].MassKg = Events.DuelEnded;
		if (Events.DuelEnded == 0)
		{
			AddMessage("You left the ring: the duel is called off");
			return;
		}
		char Text[96];
		int Length = Append(Text, 0, sizeof(Text), Events.DuelEnded == 1 ? "You win the duel! (" : "Your sparring partner wins the duel (");
		Length = AppendNumber(Text, Length, sizeof(Text), Brain.PlayerWins, 0);
		Length = Append(Text, Length, sizeof(Text), " - ");
		Length = AppendNumber(Text, Length, sizeof(Text), Brain.PartnerWins, 0);
		Append(Text, Length, sizeof(Text), ")");
		AddMessage(Text);
	}

	void FSandbox::UpdateGuard(const FInput& Input, double Dt)
	{
		Player.FlinchS = KMax(Player.FlinchS - Dt, 0.0);
		Player.ProtectS = KMax(Player.ProtectS - Dt, 0.0);
		Player.GuardCooldownS = KMax(Player.GuardCooldownS - Dt, 0.0);
		if (Player.DownS > 0.0)
		{
			Player.bGuarding = false;
			Player.DownS -= Dt;
			if (Player.DownS <= 0.0)
			{
				// Back on their feet, whole, and safe for a moment.
				Player.DownS = 0.0;
				Player.Health = Player.MaxHealth;
				Player.ProtectS = 1.5;
				AddMessage("Back on your feet");
			}
			return;
		}
		const bool bFree = Player.Phase == EPhase::None && Player.HoldTechnique == ETechnique::None && !Player.bScooter
			&& Player.DashTimeS <= 0.0 && Player.Stamina > 0.0;

		// A roll: the way the stick points (camera-relative), or back from the camera; flames pass through it.
		const bool bDodgePressed = Input.bDodge && !bPreviousDodge;
		bPreviousDodge = Input.bDodge;
		Player.DodgeCooldownS = KMax(Player.DodgeCooldownS - Dt, 0.0);
		if (Player.DodgeS > 0.0)
		{
			Player.DodgeS = KMax(Player.DodgeS - Dt, 0.0);
		}
		else if (bDodgePressed && bFree && Player.bGrounded && Player.DodgeCooldownS <= 0.0 && Player.Stamina >= DodgeStamina)
		{
			FVec3 CameraFlat = Flat(Input.CameraForward);
			CameraFlat = CameraFlat.Size() > SmallNumber ? CameraFlat.GetSafeNormal() : GetFacing();
			const FVec3 CameraRight(CameraFlat.Y, -CameraFlat.X, 0.0);
			FVec3 Way = CameraFlat * Input.MoveForward + CameraRight * Input.MoveRight;
			// No direction: a backstep, away from where the camera looks (from the opponent you are watching).
			Way = Way.Size() > 0.1 ? Way.GetSafeNormal() : CameraFlat * -1.0;
			Player.DodgeS = DodgeSeconds;
			Player.DodgeCooldownS = DodgeCooldownSeconds;
			Player.DodgeVelocityCmS = Way * DodgeSpeedCmS;
			Player.Stamina -= DodgeStamina;
			Player.ProtectS = KMax(Player.ProtectS, DodgeSeconds);
			Player.bGuarding = false;
			AddEffect(ESandboxEffect::PlayerDodge, Player.LocationCm, 0.0);
		}
		if (Player.DodgeS > 0.0)
		{
			Player.bGuarding = false;
			return;
		}
		if (Input.bGuard && bFree && (Player.bGuarding || Player.GuardCooldownS <= 0.0))
		{
			Player.GuardTimeS = Player.bGuarding ? Player.GuardTimeS + Dt : 0.0;
			Player.bGuarding = true;
		}
		else if (Player.bGuarding)
		{
			Player.bGuarding = false;
			Player.GuardCooldownS = GuardRecastS;
		}
	}

	void FSandbox::HurtPlayer(double Damage, const FVec3& Direction, double KnockbackCmS, bool bBlocked)
	{
		if (Player.DownS > 0.0 || Player.ProtectS > 0.0 || Damage <= 0.0)
		{
			return;
		}
		const double Dealt = KMin(Damage, Player.Health);
		Player.Health -= Dealt;
		Player.VelocityCmS += Flat(Direction) * KnockbackCmS;
		// A blocked hit is reported as the block; only a clean hit rocks them.
		if (!bBlocked)
		{
			Player.FlinchS = KMax(Player.FlinchS, 0.3);
			AddEffect(ESandboxEffect::PlayerHit, GetChestCm(), Dealt);
		}
		if (Player.Health > 1e-6)
		{
			return;
		}
		// Knocked down: flat on their back for a few seconds, then up again whole.
		Player.Health = 0.0;
		Player.DownS = PlayerDownSeconds;
		Player.bGuarding = false;
		const FVec3 Away = Flat(Direction);
		Player.FallDirection = Away.Size() > 1e-6 ? Away.GetSafeNormal() : GetFacing() * -1.0;
		Player.VelocityCmS = Player.FallDirection * 380.0 + FVec3(0.0, 0.0, 260.0);
		Player.bGrounded = false;
		AddEffect(ESandboxEffect::PlayerDown, Player.LocationCm, 0.0);
		if (IsDuelActive())
		{
			FSparringEvents Events;
			Rival.Brain.OnPlayerDown(Events);
			HandleRivalEvents(Events);
		}
	}

	void FSandbox::UpdateFighterHits()
	{
		const FVec3 PlayerA = Player.LocationCm + FVec3(0.0, 0.0, PlayerRadiusCm);
		const FVec3 PlayerB = Player.LocationCm + FVec3(0.0, 0.0, PlayerHeightCm - PlayerRadiusCm);
		const FBody* RivalBody = Rival.Body >= 0 && Bodies[Rival.Body].bAlive ? &Bodies[Rival.Body] : nullptr;
		for (int Index = 0; Index < MaxProjectiles; ++Index)
		{
			FProjectile& Projectile = Projectiles[Index];
			if (!Projectile.bAlive || Projectile.bStruck || Projectile.bLanded || Projectile.Kind != EProjectileKind::Fire)
			{
				continue;
			}
			const FVolume* Volume = World.GetVolume(Projectile.Volume);
			if (!Volume || Volume->MassKg <= 0.0)
			{
				continue;
			}
			// The flame's dense core has to reach the body; the glow around it only warms.
			const double CoreCm = 0.45 * Volume->RadiusCm;
			const double Share = Volume->MassKg / 0.6;
			const double Damage = FlameStrikeDamage * Share * KSqrt(Share);
			const FVec3 Direction = Flat(Projectile.VelocityCmS).GetSafeNormal();

			if (Projectile.Owner == 0)
			{
				if (!RivalBody)
				{
					continue;
				}
				FVec3 CoreA, CoreB;
				BodyCore(*RivalBody, CoreA, CoreB);
				if (Distance(ClosestPointOnSegment(Projectile.LocationCm, CoreA, CoreB), Projectile.LocationCm) > RivalBody->RadiusCm + CoreCm)
				{
					continue;
				}
				Projectile.bStruck = true;
				DamageBody(Rival.Body, Damage, Direction);
				continue;
			}

			if (Distance(ClosestPointOnSegment(Projectile.LocationCm, PlayerA, PlayerB), Projectile.LocationCm) > PlayerRadiusCm + CoreCm)
			{
				continue;
			}
			Projectile.bStruck = true;
			if (Player.DownS > 0.0 || Player.ProtectS > 0.0)
			{
				// Rolled clean through it.
				if (Player.DodgeS > 0.0)
				{
					AddEffect(ESandboxEffect::PlayerDodge, GetChestCm(), 1.0);
				}
				continue;
			}
			const bool bFacing = GetFacing().Dot(Direction * -1.0) > 0.2;
			if (Player.bGuarding && bFacing && Player.GuardTimeS <= ParryWindowS)
			{
				// A perfect guard: the flame is turned back at whoever sent it, faster than it came.
				const FVec3 Back = (GetRivalChestCm() - Projectile.LocationCm).GetSafeNormal();
				Projectile.Owner = 0;
				Projectile.bStruck = false;
				Projectile.bThreatChecked = true;
				Projectile.AgeS = 0.0;
				Projectile.VelocityCmS = (Back.IsZero() ? GetFacing() : Back) * KMax(1.2 * Projectile.VelocityCmS.Size(), 1500.0);
				Player.Chi = KMin(Player.Chi + 10.0, Player.MaxChi);
				AddEffect(ESandboxEffect::Parried, Projectile.LocationCm, 0.0);
				AddMessage("Perfect guard! The blast flies back");
				continue;
			}
			if (Player.bGuarding && bFacing)
			{
				// Blocked: the flame splashes off the guard, a little gets through, and it costs stamina.
				const FVec3 Up(0.0, 0.0, 1.0);
				const FVec3 Across(-Direction.Y, Direction.X, 0.0);
				Projectile.VelocityCmS = (Direction * -0.3 + Up * 0.7 + Across * (Rival.Brain.Random() < 0.5 ? -0.6 : 0.6)) * 450.0;
				Player.Stamina = KMax(Player.Stamina - BlockStamina, 0.0);
				AddEffect(ESandboxEffect::Blocked, Projectile.LocationCm, Damage * BlockDamageScale);
				HurtPlayer(Damage * BlockDamageScale, Direction, 0.35 * FlameKnockbackCmS, true);
				if (Player.Stamina <= 0.0)
				{
					Player.bGuarding = false;
					Player.GuardCooldownS = 1.0;
					Player.FlinchS = 0.8;
					AddMessage("Guard broken: out of stamina");
				}
				continue;
			}
			// A clean hit: the flame bursts against them.
			HurtPlayer(Damage, Direction, FlameKnockbackCmS);
			RemoveProjectile(Index);
		}
	}

	void FSandbox::RivalShoot(const FSparringShot& Shot)
	{
		const FSparringTuning& Tune = Rival.Brain.Tuning;
		const FVec3 Feet = GetRivalFeetCm();
		const FVec3 Forward(KCos(Rival.Brain.YawRad), KSin(Rival.Brain.YawRad), 0.0);
		const FVec3 Right(Forward.Y, -Forward.X, 0.0);
		if (Shot.Attack == ESparringAttack::Burst)
		{
			// A ring of flames bursting outward from its middle.
			for (int Index = 0; Index < Tune.BurstFlames; ++Index)
			{
				const double Angle = Rival.Brain.YawRad + Index * 2.0 * Pi / Tune.BurstFlames;
				const FVec3 Out(KCos(Angle), KSin(Angle), 0.0);
				FVolume Flame = MakeFlame(Shot.MassKg, Tune.FlameTemperatureK, Feet, Out * MToCm(Shot.SpeedMs));
				Flame.LocationCm = Feet + FVec3(0.0, 0.0, 85.0) + Out * (RivalRadiusCm + Flame.RadiusCm + 10.0);
				const int Spawned = SpawnProjectile(EProjectileKind::Fire, Flame);
				if (Spawned >= 0)
				{
					Projectiles[Spawned].Owner = 1;
					Projectiles[Spawned].bThreatChecked = true;
				}
			}
			AddEffect(ESandboxEffect::FireRing, Feet + FVec3(0.0, 0.0, 85.0), 0.0);
			return;
		}
		const FVec3 Shoulder = Feet + FVec3(0.0, 0.0, RivalHandHeightCm) + Right * (Shot.Side * 18.0);
		// At the chest, leading a moving target a little.
		FVec3 Target = Player.LocationCm + FVec3(0.0, 0.0, 115.0);
		const double FlightS = Distance(Shoulder, Target) / MToCm(Shot.SpeedMs);
		Target += Flat(Player.VelocityCmS) * (0.5 * FlightS);
		FVec3 Direction = (Target - Shoulder).GetSafeNormal();
		if (Direction.IsZero())
		{
			Direction = Forward;
		}
		const double C = KCos(Shot.SpreadRad), S = KSin(Shot.SpreadRad);
		Direction = FVec3(Direction.X * C - Direction.Y * S, Direction.X * S + Direction.Y * C, Direction.Z);
		FVolume Flame = MakeFlame(Shot.MassKg, Tune.FlameTemperatureK, Shoulder, Direction * MToCm(Shot.SpeedMs));
		// Born clear of its own body, so its own fire never touches it.
		Flame.LocationCm = Shoulder + Direction * (RivalRadiusCm + Flame.RadiusCm + 10.0);
		const int Index = SpawnProjectile(EProjectileKind::Fire, Flame);
		if (Index >= 0)
		{
			Projectiles[Index].Owner = 1;
			Projectiles[Index].bThreatChecked = true;
		}
	}

	bool FSandbox::RivalHasClearShot(const FVec3& FeetCm) const
	{
		const FVec3 ToPlayer = Flat(Player.LocationCm - FeetCm);
		const FVec3 Toward = ToPlayer.Size() > 1.0 ? ToPlayer.GetSafeNormal() : FVec3(KCos(Rival.Brain.YawRad), KSin(Rival.Brain.YawRad), 0.0);
		const FVec3 Hand = FeetCm + FVec3(0.0, 0.0, RivalHandHeightCm) + Toward * 40.0;
		const FVec3 Target = Player.LocationCm + FVec3(0.0, 0.0, 110.0);
		const FVec3 Line = Target - Hand;
		const double LineCm = Line.Size();
		FVec3 Hit;
		if (LineCm > 1.0 && Terrain.Raycast(Hand, Line / LineCm, LineCm, Hit))
		{
			return false;
		}
		// Solid props in the way (lanterns, rocks, banners' poles) block it too.
		for (int Index = 0; Index < NumBodies; ++Index)
		{
			const FBody& Other = Bodies[Index];
			if (!Other.bAlive || Index == Rival.Body || Other.Kind == EBodyKind::ThrownRock)
			{
				continue;
			}
			FVec3 CoreA, CoreB, OnLine, OnBody;
			BodyCore(Other, CoreA, CoreB);
			ClosestPointsBetweenSegments(Hand, Target, CoreA, CoreB, OnLine, OnBody);
			if (Distance(OnLine, OnBody) < BodyCollisionRadius(Other) + 35.0)
			{
				return false;
			}
		}
		return true;
	}

	void FSandbox::UpdateRival(double Dt)
	{
		if (Rival.Body < 0 || !Bodies[Rival.Body].bAlive)
		{
			return;
		}
		FBody& Body = Bodies[Rival.Body];
		FSparringBrain& Brain = Rival.Brain;
		const double Ambient = World.Settings.AmbientTemperatureK;
		const FVec3 Feet = GetRivalFeetCm();
		const double Ground = Terrain.GetHeightAt(Feet.X, Feet.Y);

		// Standing in fire hurts (flames that strike it are counted in UpdateFighterHits; its own never hurt it).
		if (Brain.IsDuelActive())
		{
			FHandle Handles[48];
			const int Count = World.QueryVolumes(Body.LocationCm, Body.RadiusCm + 20.0, SubstanceMask(ESubstance::Fire), Handles, 48);
			double HeatingW = 0.0;
			for (int Index = 0; Index < Count; ++Index)
			{
				const int Projectile = FindProjectileByVolume(Handles[Index]);
				if (Projectile >= 0 && (Projectiles[Projectile].Owner == 1 || (Projectiles[Projectile].Kind == EProjectileKind::Fire && !Projectiles[Projectile].bLanded)))
				{
					continue;
				}
				const FVolume* Fire = World.IsDepleted(Handles[Index]) ? nullptr : World.GetVolume(Handles[Index]);
				if (Fire)
				{
					HeatingW += 150.0 * KMax(Fire->TemperatureK - Ambient, 0.0) * KMin(1.0, Fire->MassKg / 0.5);
				}
			}
			if (HeatingW > 0.0)
			{
				DamageBody(Rival.Body, HeatingW * Dt / RivalHeatJPerDamage, FVec3());
			}
		}

		FSparringSenses Senses;
		Senses.FeetCm = Feet;
		Senses.VelocityCmS = Body.VelocityCmS;
		Senses.bGrounded = Feet.Z <= Ground + 8.0;
		Senses.PlayerFeetCm = Player.LocationCm;
		Senses.PlayerVelocityCmS = Player.VelocityCmS;
		Senses.bPlayerDown = Player.DownS > 0.0;
		Senses.bClearShot = Brain.State != ESparringState::Moving || RivalHasClearShot(Feet);
		FSparringOrders Orders;
		Brain.Update(Senses, Dt, Orders);

		// Something thrown at it while it is free to react: guard or sidestep (or take it), decided once per throw.
		for (int Index = 0; Index < MaxProjectiles + NumBodies && Brain.CanReact(); ++Index)
		{
			FSparringThreat Threat;
			bool* Checked = nullptr;
			if (Index < MaxProjectiles)
			{
				FProjectile& Projectile = Projectiles[Index];
				if (!Projectile.bAlive || Projectile.bThreatChecked || Projectile.Owner != 0 || Projectile.bLanded || Projectile.Kind == EProjectileKind::GroundFlame)
				{
					continue;
				}
				Threat.LocationCm = Projectile.LocationCm;
				Threat.VelocityCmS = Projectile.VelocityCmS;
				Checked = &Projectile.bThreatChecked;
			}
			else
			{
				FBody& Rock = Bodies[Index - MaxProjectiles];
				if (!Rock.bAlive || Rock.Kind != EBodyKind::ThrownRock || Rock.bHeld || Rock.bThreatChecked || Rock.VelocityCmS.Size() < 600.0)
				{
					continue;
				}
				Threat.LocationCm = Rock.LocationCm;
				Threat.VelocityCmS = Rock.VelocityCmS;
				Threat.bHeavy = true;
				Checked = &Rock.bThreatChecked;
			}
			if (Brain.IsThreatening(Threat, Body.LocationCm))
			{
				*Checked = true;
				Brain.React(Threat, Body.LocationCm, Orders.Events);
				break;
			}
		}

		// It will not walk up a wall, into a pond or off the field.
		FVec3 Desired = Orders.DesiredVelocityCmS;
		if (Brain.State == ESparringState::Dodging)
		{
			Desired = Brain.DodgeDirection * Brain.Tuning.DodgeCmS;
		}
		if (!Desired.IsZero())
		{
			const FVec3 Ahead = Feet + Desired.GetSafeNormal() * 60.0;
			const int Pond = FindPondAt(FVec3(Ahead.X, Ahead.Y, Ground));
			if (!Terrain.IsInside(Ahead.X, Ahead.Y) || Terrain.GetHeightAt(Ahead.X, Ahead.Y) - Ground > 40.0 || Pond >= 0)
			{
				Desired = FVec3();
				Brain.OnBlocked();
			}
		}
		// Knocked flying, it has no footing to steer with.
		if (Orders.bControl && Flat(Body.VelocityCmS).Size() < 1200.0)
		{
			const FVec3 Horizontal = MoveToward(Flat(Body.VelocityCmS), Desired, Brain.Tuning.AccelerationCmS2 * Dt);
			Body.VelocityCmS = FVec3(Horizontal.X, Horizontal.Y, Body.VelocityCmS.Z);
		}

		Body.YawRad = Brain.YawRad;
		Body.Health = Brain.Health;
		// Down, its body lies the way it was hit (the dummy tilt), until it gets up.
		Body.KnockoutS = Brain.IsDown() ? KMax(Brain.DownS, 1e-3) : 0.0;
		Rival.StridePhase = WrapAngle(Rival.StridePhase + Flat(Body.VelocityCmS).Size() * Dt / 140.0 * Pi);
		const FVec3 Facing(KCos(Brain.YawRad), KSin(Brain.YawRad), 0.0);
		const FVec3 Right(Facing.Y, -Facing.X, 0.0);
		Rival.HandCm = Feet + FVec3(0.0, 0.0, RivalHandHeightCm) + Facing * 45.0 + Right * ((Brain.ShotsFired % 2 == 0 ? 1.0 : -1.0) * 18.0);
		if (Orders.bShoot)
		{
			RivalShoot(Orders.Shot);
		}
		HandleRivalEvents(Orders.Events);
	}
}
