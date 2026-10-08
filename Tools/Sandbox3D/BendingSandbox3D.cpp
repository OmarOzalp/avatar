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
		constexpr double BrazierPedestalRadiusCm = 30.0;
		constexpr double BrazierPedestalHeightCm = 100.0;
		constexpr double MinWallDistanceCm = 260.0;
		constexpr double GroundFlameFollowCmS = 400.0;

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
		bWhipCreatedThisMove = false;
		HoldProjectile = -1;
		EarthWorkMovedKg = 0.0;
		EarthWorkJ = 0.0;
		LastNoChiMessageS = -10.0;

		Player = FPlayer();
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
			AddBody(Body, Volume);
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
			if (Bodies[Index].bAlive && Bodies[Index].Volume == Handle)
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

		UpdateStance(Input);
		UpdateAim(Input);
		UpdateMoves(Input, Dt);
		UpdateHold(Input, Dt);
		UpdatePlayer(Input, Dt);
		UpdateHand(Dt);
		UpdateEarthWork(Dt);
		UpdateBraziers(Dt);
		UpdateProjectiles(Dt);
		UpdateBodies(Dt);
		UpdateWhip(Dt);

		SyncVolumesIn();
		World.Advance(Dt);
		Whip.PostStep(World);
		SyncVolumesOut();
		CollectEvents(Dt);

		if (Whip.IsActive() && Whip.ShouldCollapse(World))
		{
			AddMessage("Too little water left: the whip falls apart");
			ReleaseWhip(false);
		}

		Player.Chi = KMin(Player.Chi + Player.ChiRegenPerS * Dt, Player.MaxChi);
		Player.Stamina = KMin(Player.Stamina + Player.StaminaRegenPerS * Dt, Player.MaxStamina);
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
		for (int Slot = 0; Slot < static_cast<int>(ETechniqueSlot::Count); ++Slot)
		{
			const bool bPressed = Input.bSlotHeld[Slot] && !PreviousSlotHeld[Slot];
			PreviousSlotHeld[Slot] = Input.bSlotHeld[Slot];
			if (!bPressed)
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
		const bool bNeedsWhip = Technique == ETechnique::WaterFreeze || Technique == ETechnique::WaterRelease || Technique == ETechnique::WaterBlast;
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
				break;
			}
			FVec3 Source;
			const int Pond = FindWaterSource(Layout, Player.HandCm, Tuning.WhipDrawRangeCm, Source);
			if (Pond < 0 || Layout.Ponds[Pond].WaterKg < Tuning.WhipWaterKg)
			{
				AddMessage("No water within 15 m: go to a pond");
				break;
			}
			if (Whip.Create(World, Source, Player.HandCm, Tuning.WhipWaterKg, 288.15))
			{
				Layout.Ponds[Pond].WaterKg -= Tuning.WhipWaterKg;
				bWhipCreatedThisMove = true;
				AddMessageNumber("Drew ", Tuning.WhipWaterKg, " kg of water from the pond");
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
		case ETechnique::WaterWhip:
			if (Whip.IsActive() && !bWhipCreatedThisMove)
			{
				Whip.Lash();
			}
			break;

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

		default:
			break;
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

		const bool bCasting = Player.Phase == EPhase::Startup || Player.Phase == EPhase::Active || Player.HoldTechnique != ETechnique::None;
		double MaxSpeed = Input.bSprint ? SprintSpeedCmS : WalkSpeedCmS;
		MaxSpeed *= bCasting ? CastingSpeedScale : 1.0;
		Player.Traction = Player.bGrounded ? World.GetSurfaceTractionMultiplierAt(Player.LocationCm) : 1.0;

		FVec3 Horizontal = Flat(Player.VelocityCmS);
		const FVec3 Target = Wish * MaxSpeed;
		if (Player.bGrounded)
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

		// Facing: toward the aim while bending, else toward motion.
		if (bCasting)
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
		Whip.SetHoldExtended(PreviousSlotHeld[static_cast<int>(ETechniqueSlot::Primary)] && Player.Stance == ETechniqueElement::Water);
		Whip.SetControl(Player.HandCm, HandVelocity, Player.AimPointCm);
		Whip.PreStep(World, &Terrain, Dt);
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

			case EProjectileKind::Water:
			{
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
				// Weighted base: the dummy rocks back upright.
				for (int Axis = 0; Axis < 2; ++Axis)
				{
					Body.TiltRate[Axis] += (-40.0 * Body.Tilt[Axis] - 4.0 * Body.TiltRate[Axis]) * Dt;
					Body.Tilt[Axis] = KClamp(Body.Tilt[Axis] + Body.TiltRate[Axis] * Dt, -1.2, 1.2);
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
					if (A.Prop == EArenaProp::Dummy)
					{
						A.TiltRate[0] -= Normal.Y * Impulse * InvA * 0.01;
						A.TiltRate[1] += Normal.X * Impulse * InvA * 0.01;
					}
					if (B.Prop == EArenaProp::Dummy)
					{
						B.TiltRate[0] += Normal.Y * Impulse * InvB * 0.01;
						B.TiltRate[1] -= Normal.X * Impulse * InvB * 0.01;
					}
				}
			}
		}

		for (int Index = 0; Index < NumBodies; ++Index)
		{
			FBody& Body = Bodies[Index];
			if (Body.bAlive && !Terrain.IsInside(Body.LocationCm.X, Body.LocationCm.Y))
			{
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
				if (Body.Prop == EArenaProp::Dummy)
				{
					// Pushed high on its body, the dummy rocks.
					Body.TiltRate[0] -= DeltaV.Y * 0.02;
					Body.TiltRate[1] += DeltaV.X * 0.02;
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
			if (Event.Type == EReactionType::Extinguished && FindProjectileByVolume(Event.VolumeA) >= 0)
			{
				continue;
			}
			if (NumFrameEvents < MaxFrameEvents)
			{
				FSandboxEvent& Out = FrameEvents[NumFrameEvents++];
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
}
