#include "Sim/BendingWaterWhip.h"

namespace BendingSim
{
	namespace
	{
		constexpr double MinSegmentRadiusCm = 1.0;
		constexpr double MaxSegmentRadiusCm = 45.0;
		constexpr int MaxSubstepsPerFrame = 16;
		/** Below this share of the water drawn, the whip can no longer hold together. */
		constexpr double CollapseMassFraction = 0.15;
		/** The intended motion never asks the water to move faster than this (cm/s). */
		constexpr double MaxTargetSpeedCmS = 6000.0;
		/** Nothing in the chain moves faster than this (cm/s): a guard against a bad frame, not a gameplay limit. */
		constexpr double MaxPointSpeedCmS = 12000.0;
		/** Samples of a shape before it is resampled at even arc length. */
		constexpr int ShapeSamples = 97;

		/** Capsule radius holding MassKg at DensityKgM3 over a core of LengthCm (cylinder approximation). */
		double SegmentRadiusFor(double MassKg, double DensityKgM3, double LengthCm)
		{
			const double LengthM = CmToM(KMax(LengthCm, 1.0));
			const double RadiusM = KSqrt(KMax(MassKg, 0.0) / (KMax(DensityKgM3, 1.0) * Pi * LengthM));
			return KClamp(MToCm(RadiusM), MinSegmentRadiusCm, MaxSegmentRadiusCm);
		}

		/** Places Count points at even arc length along a sampled curve, extending past its end along the last chord. */
		void ResampleByArcLength(const FVec3* Samples, int NumSamples, double SpacingCm, FVec3* OutPoints, int Count)
		{
			double Cumulative[ShapeSamples];
			Cumulative[0] = 0.0;
			for (int Index = 1; Index < NumSamples; ++Index)
			{
				Cumulative[Index] = Cumulative[Index - 1] + Distance(Samples[Index], Samples[Index - 1]);
			}
			int Segment = 0;
			for (int Point = 0; Point < Count; ++Point)
			{
				const double Along = Point * SpacingCm;
				while (Segment + 2 < NumSamples && Cumulative[Segment + 1] < Along)
				{
					++Segment;
				}
				const double Length = Cumulative[Segment + 1] - Cumulative[Segment];
				const double Alpha = Length > SmallNumber ? (Along - Cumulative[Segment]) / Length : 0.0;
				OutPoints[Point] = Samples[Segment] + (Samples[Segment + 1] - Samples[Segment]) * Alpha;
			}
		}

		/** How far the rolling loop has travelled down the stream, from where the windup left it: it speeds up toward the snap. */
		double StrikeFront(double Alpha, double Start)
		{
			const double U = KClamp(Alpha, 0.0, 1.0);
			return Start + (1.0 - Start) * U * (0.85 + 0.15 * U);
		}
	}

	bool FWaterWhip::Create(FSimWorld& World, const FVec3& InSourceCm, const FVec3& InHandCm, double WaterMassKg, double TemperatureK)
	{
		if (IsActive() || WaterMassKg <= 0.0)
		{
			return false;
		}
		NumPoints = Settings.NumPoints < 3 ? 3 : (Settings.NumPoints > MaxPoints ? MaxPoints : Settings.NumPoints);
		HandCm = InHandCm;
		PreviousHandCm = InHandCm;
		HandVelocityCmS = FVec3();
		SourceCm = InSourceCm;

		const FVec3 Away = InSourceCm - InHandCm;
		const FVec3 Flat(Away.X, Away.Y, 0.0);
		Forward = Flat.Size() > 1.0 ? Flat.GetSafeNormal() : FVec3(1.0, 0.0, 0.0);
		Right = Forward.Cross(FVec3(0.0, 0.0, 1.0));
		AimDirection = Forward;
		StrikeDirection = Forward;
		AimPointCm = InHandCm + Forward * Settings.LengthCm;

		// The stream tapers like a whip: segment mass follows the square of the radius profile.
		const int NumSegments = NumPoints - 1;
		double Weights[MaxSegments];
		double WeightSum = 0.0;
		for (int Segment = 0; Segment < NumSegments; ++Segment)
		{
			const double S = (Segment + 0.5) / NumSegments;
			const double Radius = 1.0 - (1.0 - KClamp(Settings.TipRadiusFraction, 0.05, 1.0)) * S;
			Weights[Segment] = Radius * Radius;
			WeightSum += Weights[Segment];
		}

		InitialSpacingCm = Away.Size() / NumSegments;
		for (int Index = 0; Index < NumPoints; ++Index)
		{
			Points[Index] = InHandCm + Away * (static_cast<double>(Index) / NumSegments);
			Velocities[Index] = FVec3();
			BendRestCm[Index] = 0.0;
			LashStartOffsets[Index] = FVec3();
		}
		for (int Segment = 0; Segment < NumSegments; ++Segment)
		{
			const double SegmentMassKg = WaterMassKg * Weights[Segment] / WeightSum;
			FVolume Volume = FVolume::MakeDefault(ESubstance::Water, SegmentMassKg);
			Volume.TemperatureK = TemperatureK;
			Volume.Shape = EShape::Capsule;
			Volume.bDeriveRadiusFromMass = false;
			Volume.LocationCm = (Points[Segment] + Points[Segment + 1]) * 0.5;
			Volume.CapsuleHalfAxisCm = (Points[Segment + 1] - Points[Segment]) * 0.5;
			Volume.RadiusCm = SegmentRadiusFor(SegmentMassKg, WaterDensity, (Points[Segment + 1] - Points[Segment]).Size());
			SegmentRadiiCm[Segment] = Volume.RadiusCm;
			Segments[Segment] = World.AddVolume(Volume, true);
			if (!Segments[Segment].IsSet())
			{
				for (int Added = 0; Added < Segment; ++Added)
				{
					World.RemoveVolume(Segments[Added]);
				}
				NumPoints = 0;
				return false;
			}
		}

		InitialMassKg = WaterMassKg;
		NumPendingSpray = 0;
		bHasPreviousTargets = false;
		EnterState(EWhipState::Forming);
		return true;
	}

	void FWaterWhip::Destroy(FSimWorld& World)
	{
		for (int Segment = 0; Segment < GetNumSegments(); ++Segment)
		{
			World.RemoveVolume(Segments[Segment]);
			Segments[Segment] = FHandle();
		}
		State = EWhipState::Inactive;
		NumPoints = 0;
		bHasPreviousTargets = false;
	}

	void FWaterWhip::SetControl(const FVec3& InHandCm, const FVec3& InHandVelocityCmS, const FVec3& InAimPointCm)
	{
		HandCm = InHandCm;
		HandVelocityCmS = InHandVelocityCmS;
		AimPointCm = InAimPointCm;
		const FVec3 ToAim = InAimPointCm - InHandCm;
		if (ToAim.Size() > 1.0)
		{
			AimDirection = ToAim.GetSafeNormal();
			const FVec3 Flat(AimDirection.X, AimDirection.Y, 0.0);
			if (Flat.Size() > 0.05)
			{
				Forward = Flat.GetSafeNormal();
				Right = Forward.Cross(FVec3(0.0, 0.0, 1.0));
			}
		}
	}

	void FWaterWhip::SetBodyCenter(const FVec3& InBodyCenterCm)
	{
		BodyCenterCm = InBodyCenterCm;
		bHasBodyCenter = true;
	}

	bool FWaterWhip::Lash()
	{
		if (State != EWhipState::Holding && State != EWhipState::Returning && State != EWhipState::Extended)
		{
			return false;
		}
		// The windup blends out of wherever the water is now, so lashes chain without a jump.
		for (int Index = 0; Index < NumPoints; ++Index)
		{
			LashStartOffsets[Index] = Points[Index] - HandCm;
		}
		EnterState(EWhipState::Windup);
		return true;
	}

	void FWaterWhip::EnterState(EWhipState NewState)
	{
		State = NewState;
		StateTimeS = 0.0;
		if (NewState == EWhipState::Striking)
		{
			PeakTipVelocityCmS = FVec3();
			PeakTipForwardCmS = 0.0;
		}
	}

	double FWaterWhip::GetFormProgress() const
	{
		if (State != EWhipState::Forming)
		{
			return State == EWhipState::Inactive ? 0.0 : 1.0;
		}
		return KClamp(StateTimeS / KMax(Settings.FormSeconds, 0.01), 0.0, 1.0);
	}

	FWaterWhip::FFrame FWaterWhip::MakeFrame() const
	{
		FFrame Frame;
		Frame.Hand = HandCm;
		Frame.Forward = Forward;
		Frame.Right = Right;
		Frame.Up = FVec3(0.0, 0.0, 1.0);
		// Without a body, assume the hand is held out in front of the chest and to the side.
		Frame.Body = bHasBodyCenter ? BodyCenterCm : HandCm - Forward * 45.0 - Right * 22.0;
		return Frame;
	}

	void FWaterWhip::BuildReadyShape(const FFrame& Frame, FVec3* OutShape) const
	{
		// A loose loop around the chest, starting at the hand and sweeping across the front, round the far side and
		// behind. It sits a little low in front (out of the line of sight) and rises behind; slow waves run along it.
		const double LoopRadius = KMax(Settings.ReadyRadiusCm, 30.0);
		const FVec3 Center = Frame.Body - Frame.Up * Settings.ReadyDropCm;
		const FVec3 FromCenter = Frame.Hand - Center;
		const double HandX = FromCenter.Dot(Frame.Forward);
		const double HandY = FromCenter.Dot(Frame.Right);
		const double HandRadius = KMax(KSqrt(HandX * HandX + HandY * HandY), 5.0);
		const double HandAngle = KAtan2(HandY, HandX);
		const double HandZ = Frame.Hand.Dot(Frame.Up);
		const double CenterZ = Center.Dot(Frame.Up);
		const FVec3 CenterFlat = Center - Frame.Up * CenterZ;
		const double T = ElapsedS;

		FVec3 Samples[ShapeSamples];
		double Span = Settings.LengthCm / LoopRadius;
		for (int Pass = 0; Pass < 3; ++Pass)
		{
			double Length = 0.0;
			for (int Index = 0; Index < ShapeSamples; ++Index)
			{
				const double P = static_cast<double>(Index) / (ShapeSamples - 1);
				const double Out = KSmoothStep(0.0, 0.2, P);
				const double Angle = HandAngle - Span * P;
				const double Radius = HandRadius + (LoopRadius - HandRadius) * Out + 6.0 * Out * KSin(2.0 * Pi * (2.0 * P - 0.35 * T));
				const double LoopZ = CenterZ + Settings.ReadyDropCm * (1.0 - KCos(Angle));
				const double Z = KLerp(HandZ, LoopZ, Out) + 7.0 * Out * KSin(2.0 * Pi * (1.5 * P - 0.5 * T));
				Samples[Index] = CenterFlat + Frame.Forward * (Radius * KCos(Angle)) + Frame.Right * (Radius * KSin(Angle)) + Frame.Up * Z;
				if (Index > 0)
				{
					Length += Distance(Samples[Index], Samples[Index - 1]);
				}
			}
			// Scale the sweep so the loop holds exactly the stream's length.
			Span *= Settings.LengthCm / KMax(Length, 1.0);
		}
		ResampleByArcLength(Samples, ShapeSamples, Settings.LengthCm / (NumPoints - 1), OutShape, NumPoints);
	}

	FVec3 FWaterWhip::GetFoldPoint(const FFrame& Frame, double S, double Front) const
	{
		// The stream laid out from the hand toward the strike for the part the loop has passed (s <= front); past the
		// loop it folds back over itself, the far leg rising slightly behind. At front = 0 this is the windup (the
		// whole stream folded back over the shoulder; the windup stops at WindupFront); at front = 1 the stream is
		// straight out to full reach.
		const double L = Settings.LengthCm;
		const double Rho = KMax(Settings.LoopRadiusCm, 1.0);
		const FVec3& D = State == EWhipState::Windup ? AimDirection : StrikeDirection;
		FVec3 UpAcross = Frame.Up - D * Frame.Up.Dot(D);
		UpAcross = UpAcross.Size() > 0.1 ? UpAcross.GetSafeNormal() : Frame.Forward * -1.0;
		const FVec3 SideAcross = D.Cross(UpAcross);
		const double Lean = Settings.StrikeLeanDeg * Pi / 180.0;
		// SideAcross is the bender's Right when the strike is level; leaning toward it puts the loop over the hand side.
		const FVec3 LoopUp = (UpAcross * KCos(Lean) + SideAcross * (KSin(Lean) * (SideAcross.Dot(Frame.Right) >= 0.0 ? 1.0 : -1.0))).GetSafeNormal();

		if (S <= Front)
		{
			return Frame.Hand + D * (S * L);
		}
		const FVec3 Fold = Frame.Hand + D * (Front * L);
		const double Past = (S - Front) * L;
		const double Arc = Pi * Rho;
		if (Past < Arc)
		{
			const double Angle = Past / Rho;
			return Fold + D * (Rho * KSin(Angle)) + LoopUp * (Rho * (1.0 - KCos(Angle)));
		}
		// The folded-back leg angles away from the strike line as it goes back, so the drawn-back stream stays clear
		// of the bender's head and of the camera behind them.
		const double Back = Past - Arc;
		const FVec3 BackDirection = (LoopUp * 0.45 - D).GetSafeNormal();
		return Fold + LoopUp * (2.0 * Rho) + BackDirection * Back;
	}

	void FWaterWhip::ComputeTargets(FVec3* OutTargets) const
	{
		const FFrame Frame = MakeFrame();
		const double Den = static_cast<double>(NumPoints - 1);
		FVec3 Ready[MaxPoints];
		const bool bNeedsReady = State == EWhipState::Forming || State == EWhipState::Holding || State == EWhipState::Returning;
		if (bNeedsReady)
		{
			BuildReadyShape(Frame, Ready);
		}

		for (int Index = 0; Index < NumPoints; ++Index)
		{
			const double S = Index / Den;
			switch (State)
			{
			case EWhipState::Forming:
			{
				// Root first: the water nearest the hand arrives first; the tail arcs up out of the pond after it.
				const double U = GetFormProgress();
				const double W = KSmoothStep(0.0, 1.0, (U - 0.45 * S) / 0.55);
				const FVec3 Strung = Frame.Hand + (SourceCm - Frame.Hand) * S;
				OutTargets[Index] = Strung + (Ready[Index] - Strung) * W + Frame.Up * (90.0 * KSin(Pi * W) * S);
				break;
			}
			case EWhipState::Holding:
				OutTargets[Index] = Ready[Index];
				break;
			case EWhipState::Windup:
			{
				const double W = KSmoothStep(0.0, 1.0, StateTimeS / KMax(Settings.WindupS, 0.01));
				const FVec3 From = Frame.Hand + LashStartOffsets[Index];
				OutTargets[Index] = From + (GetFoldPoint(Frame, S, KClamp(Settings.WindupFront, 0.0, 0.9)) - From) * W;
				break;
			}
			case EWhipState::Striking:
				OutTargets[Index] = GetFoldPoint(Frame, S, StrikeFront(StateTimeS / KMax(Settings.StrikeS, 0.01), KClamp(Settings.WindupFront, 0.0, 0.9)));
				break;
			case EWhipState::Extended:
				OutTargets[Index] = GetFoldPoint(Frame, S, 1.0);
				break;
			case EWhipState::Returning:
			{
				// Root first again; the tail is drawn back last, arcing up over the stream as it comes.
				const double U = StateTimeS / KMax(Settings.ReturnS, 0.01);
				const double W = KSmoothStep(0.0, 1.0, (U - 0.4 * S) / 0.6);
				const FVec3 Out = GetFoldPoint(Frame, S, 1.0);
				OutTargets[Index] = Out + (Ready[Index] - Out) * W + Frame.Up * (70.0 * KSin(Pi * W) * (0.3 + 0.7 * S));
				break;
			}
			default:
				OutTargets[Index] = Points[Index];
				break;
			}
		}
		OutTargets[0] = Frame.Hand;
	}

	double FWaterWhip::GetPointStiffness(int Index) const
	{
		double Stiffness = Settings.ReadyStiffness;
		switch (State)
		{
		case EWhipState::Forming:
			Stiffness = Settings.ReadyStiffness * (0.5 + 0.5 * GetFormProgress());
			break;
		case EWhipState::Windup:
		case EWhipState::Striking:
		case EWhipState::Extended:
			Stiffness = Settings.LashStiffness;
			break;
		case EWhipState::Returning:
			Stiffness = KLerp(Settings.LashStiffness, Settings.ReadyStiffness, KSmoothStep(0.0, 1.0, StateTimeS / KMax(Settings.ReturnS, 0.01)));
			break;
		default:
			break;
		}
		// Ice is moved as a rigid piece by its frozen joints, not pulled point by point.
		const bool bFrozen = (Index >= 1 && BendRestCm[Index - 1] > 0.0) || (Index >= 2 && BendRestCm[Index - 2] > 0.0);
		return bFrozen ? Stiffness * 0.3 : Stiffness;
	}

	double FWaterWhip::GetPointDampingRatio() const
	{
		switch (State)
		{
		case EWhipState::Windup:
		case EWhipState::Striking:
		case EWhipState::Extended:
		case EWhipState::Returning:
			return Settings.LashDampingRatio;
		default:
			return Settings.ReadyDampingRatio;
		}
	}

	void FWaterWhip::PreStep(FSimWorld& World, const FTerrain* Terrain, double DeltaSeconds)
	{
		if (!IsActive() || DeltaSeconds <= 0.0)
		{
			return;
		}
		ElapsedS += DeltaSeconds;
		StateTimeS += DeltaSeconds;
		switch (State)
		{
		case EWhipState::Forming:
			if (StateTimeS >= Settings.FormSeconds)
			{
				EnterState(EWhipState::Holding);
			}
			break;
		case EWhipState::Windup:
			if (StateTimeS >= Settings.WindupS)
			{
				// The strike line is fixed now, so the lash lands where it was aimed.
				StrikeDirection = AimDirection;
				EnterState(EWhipState::Striking);
			}
			break;
		case EWhipState::Striking:
			if (StateTimeS >= Settings.StrikeS)
			{
				FlingSpray(World);
				EnterState(EWhipState::Extended);
			}
			break;
		case EWhipState::Extended:
			if (StateTimeS >= Settings.ExtendedS)
			{
				EnterState(EWhipState::Returning);
			}
			break;
		case EWhipState::Returning:
			if (StateTimeS >= Settings.ReturnS)
			{
				EnterState(EWhipState::Holding);
			}
			break;
		default:
			break;
		}

		FVec3 Targets[MaxPoints];
		ComputeTargets(Targets);
		if (!bHasPreviousTargets)
		{
			for (int Index = 0; Index < NumPoints; ++Index)
			{
				PreviousTargets[Index] = Targets[Index];
			}
		}
		FVec3 TargetVelocities[MaxPoints];
		double Stiffness[MaxPoints] = {};
		double Damping[MaxPoints] = {};
		const double DampingRatio = GetPointDampingRatio();
		for (int Index = 1; Index < NumPoints; ++Index)
		{
			FVec3 Velocity = (Targets[Index] - PreviousTargets[Index]) / DeltaSeconds;
			const double Speed = Velocity.Size();
			if (Speed > MaxTargetSpeedCmS)
			{
				Velocity *= MaxTargetSpeedCmS / Speed;
			}
			TargetVelocities[Index] = Velocity;
			Stiffness[Index] = GetPointStiffness(Index);
			Damping[Index] = 2.0 * DampingRatio * KSqrt(Stiffness[Index]);
		}

		const double FormProgress = GetFormProgress();
		const double SpacingCm = Settings.LengthCm / (NumPoints - 1);
		const double RestCm = State == EWhipState::Forming ? KLerp(InitialSpacingCm, SpacingCm, FormProgress) : SpacingCm;
		const double MaxAccelerationCmS2 = MToCm(Settings.MaxControlAccelerationMs2);
		const FVec3 Gravity(0.0, 0.0, World.Settings.GravityZCmS2);

		int NumSubsteps = static_cast<int>(DeltaSeconds * KMax(Settings.SubstepHz, 30.0)) + 1;
		NumSubsteps = NumSubsteps > MaxSubstepsPerFrame ? MaxSubstepsPerFrame : NumSubsteps;
		const double H = DeltaSeconds / NumSubsteps;

		FVec3 Predicted[MaxPoints];
		for (int Substep = 0; Substep < NumSubsteps; ++Substep)
		{
			const double Alpha = static_cast<double>(Substep + 1) / NumSubsteps;
			Predicted[0] = PreviousHandCm + (HandCm - PreviousHandCm) * Alpha;
			for (int Index = 1; Index < NumPoints; ++Index)
			{
				// The bender's force: a spring-damper along the intended motion (position and velocity), gravity
				// cancelled, all of it capped at what the bender can exert. Gravity then acts in full.
				const FVec3 Target = PreviousTargets[Index] + (Targets[Index] - PreviousTargets[Index]) * Alpha;
				FVec3 Control = (Target - Points[Index]) * Stiffness[Index] + (TargetVelocities[Index] - Velocities[Index]) * Damping[Index] - Gravity;
				const double ControlSize = Control.Size();
				if (ControlSize > MaxAccelerationCmS2)
				{
					Control *= MaxAccelerationCmS2 / ControlSize;
				}
				Velocities[Index] += (Control + Gravity) * H;
				Predicted[Index] = Points[Index] + Velocities[Index] * H;
			}

			for (int Iteration = 0; Iteration < Settings.ConstraintIterations; ++Iteration)
			{
				for (int Segment = 0; Segment + 1 < NumPoints; ++Segment)
				{
					const FVec3 Delta = Predicted[Segment + 1] - Predicted[Segment];
					const double Length = Delta.Size();
					if (Length <= SmallNumber)
					{
						continue;
					}
					const FVec3 Correction = Delta * ((Length - RestCm) / Length);
					if (Segment == 0)
					{
						Predicted[1] -= Correction;
					}
					else
					{
						Predicted[Segment] += Correction * 0.5;
						Predicted[Segment + 1] -= Correction * 0.5;
					}
				}
				// Frozen pairs keep their angle: ice is rigid.
				for (int Index = 0; Index + 2 < NumPoints; ++Index)
				{
					if (BendRestCm[Index] <= 0.0)
					{
						continue;
					}
					const FVec3 Delta = Predicted[Index + 2] - Predicted[Index];
					const double Length = Delta.Size();
					if (Length <= SmallNumber)
					{
						continue;
					}
					const FVec3 Correction = Delta * ((Length - BendRestCm[Index]) / Length);
					if (Index == 0)
					{
						Predicted[2] -= Correction;
					}
					else
					{
						Predicted[Index] += Correction * 0.5;
						Predicted[Index + 2] -= Correction * 0.5;
					}
				}
			}

			// Water stretches only so far: from the hand out, no segment may exceed MaxStretch of its rest length.
			// A stream pulled tight therefore stops dead instead of stretching, which is the whip's snap.
			const double MaxLengthCm = RestCm * KMax(Settings.MaxStretch, 1.0);
			for (int Segment = 0; Segment + 1 < NumPoints; ++Segment)
			{
				const FVec3 Delta = Predicted[Segment + 1] - Predicted[Segment];
				const double Length = Delta.Size();
				if (Length > MaxLengthCm)
				{
					Predicted[Segment + 1] = Predicted[Segment] + Delta * (MaxLengthCm / Length);
				}
			}

			if (Terrain)
			{
				for (int Index = 1; Index < NumPoints; ++Index)
				{
					const double Radius = SegmentRadiiCm[Index - 1];
					const double Floor = Terrain->GetHeightAt(Predicted[Index].X, Predicted[Index].Y) + Radius;
					if (Predicted[Index].Z < Floor)
					{
						Predicted[Index].Z = Floor;
					}
				}
			}

			for (int Index = 0; Index < NumPoints; ++Index)
			{
				FVec3 Velocity = (Predicted[Index] - Points[Index]) / H;
				const double Speed = Velocity.Size();
				if (Speed > MaxPointSpeedCmS)
				{
					Velocity *= MaxPointSpeedCmS / Speed;
				}
				Velocities[Index] = Velocity;
				Points[Index] = Predicted[Index];
			}
		}
		for (int Index = 0; Index < NumPoints; ++Index)
		{
			PreviousTargets[Index] = Targets[Index];
		}
		bHasPreviousTargets = true;
		PreviousHandCm = HandCm;
		SyncVolumes(World);

		if (State == EWhipState::Striking)
		{
			// Track the crack: the moment the last quarter of the stream moves fastest along the strike.
			const int First = (GetNumSegments() * 3) / 4 + 1;
			FVec3 Sum;
			for (int Index = First; Index < NumPoints; ++Index)
			{
				Sum += Velocities[Index];
			}
			const FVec3 Average = Sum / KMax(NumPoints - First, 1);
			const double Along = Average.Dot(StrikeDirection);
			if (Along > PeakTipForwardCmS)
			{
				PeakTipForwardCmS = Along;
				PeakTipVelocityCmS = Average;
			}
		}
	}

	void FWaterWhip::FlingSpray(FSimWorld& World)
	{
		if (Settings.SprayFraction <= 0.0 || NumPendingSpray >= MaxPendingSpray)
		{
			return;
		}
		// The tip's water keeps going when the stream stops: a share of the last quarter breaks away at the crack and
		// flies on at the speed it had then, carrying its heat. Ice does not spray.
		const int First = (GetNumSegments() * 3) / 4;
		double MassKg = 0.0;
		double HeatWeighted = 0.0;
		for (int Segment = First; Segment < GetNumSegments(); ++Segment)
		{
			FVolume* Volume = World.GetVolume(Segments[Segment]);
			if (!Volume || World.IsDepleted(Segments[Segment]) || Volume->Substance != ESubstance::Water)
			{
				continue;
			}
			const double Shed = Volume->MassKg * KClamp(Settings.SprayFraction, 0.0, 0.5);
			Volume->MassKg -= Shed;
			MassKg += Shed;
			HeatWeighted += Shed * Volume->TemperatureK;
		}
		if (MassKg <= 1e-6)
		{
			return;
		}
		FWhipDrop& Drop = PendingSpray[NumPendingSpray++];
		Drop.MassKg = MassKg;
		Drop.TemperatureK = HeatWeighted / MassKg;
		Drop.VelocityCmS = PeakTipVelocityCmS;
		Drop.LocationCm = Points[NumPoints - 1];
		Drop.Substance = ESubstance::Water;
	}

	int FWaterWhip::ConsumeSpray(FWhipDrop* OutDrops, int MaxDrops)
	{
		int Count = 0;
		for (int Index = 0; Index < NumPendingSpray && Count < MaxDrops; ++Index)
		{
			OutDrops[Count++] = PendingSpray[Index];
		}
		NumPendingSpray = 0;
		return Count;
	}

	void FWaterWhip::SyncVolumes(FSimWorld& World)
	{
		for (int Segment = 0; Segment < GetNumSegments(); ++Segment)
		{
			FVolume* Volume = World.GetVolume(Segments[Segment]);
			if (!Volume)
			{
				SegmentRadiiCm[Segment] = 0.0;
				continue;
			}
			const FVec3& A = Points[Segment];
			const FVec3& B = Points[Segment + 1];
			Volume->Shape = EShape::Capsule;
			Volume->LocationCm = (A + B) * 0.5;
			Volume->CapsuleHalfAxisCm = (B - A) * 0.5;
			Volume->VelocityCmS = (Velocities[Segment] + Velocities[Segment + 1]) * 0.5;
			Volume->RadiusCm = SegmentRadiusFor(Volume->MassKg, Volume->GetProperties().DensityKgM3, (B - A).Size());
			SegmentRadiiCm[Segment] = Volume->RadiusCm;
		}
	}

	void FWaterWhip::PostStep(FSimWorld& World)
	{
		if (!IsActive())
		{
			return;
		}
		for (int Segment = 0; Segment < GetNumSegments(); ++Segment)
		{
			const FVolume* Volume = World.GetVolume(Segments[Segment]);
			if (!Volume)
			{
				continue;
			}
			FOwnerUpdate Update;
			World.ConsumeOwnerUpdate(Segments[Segment], Update);
			if (Volume->MassKg > 1e-6 && !Update.FrameImpulseKgCmS.IsZero())
			{
				// The segment's momentum changes by the impulse when both of its points change velocity by J / m.
				const FVec3 DeltaV = Update.FrameImpulseKgCmS / Volume->MassKg;
				if (Segment > 0)
				{
					Velocities[Segment] += DeltaV;
				}
				Velocities[Segment + 1] += DeltaV;
			}
		}

		// Rigid ice: a pair of frozen segments keeps the angle it froze at.
		for (int Index = 0; Index + 2 < NumPoints; ++Index)
		{
			const FVolume* First = World.GetVolume(Segments[Index]);
			const FVolume* Second = World.GetVolume(Segments[Index + 1]);
			const bool bFrozen = First && Second && First->Substance == ESubstance::Ice && Second->Substance == ESubstance::Ice;
			if (!bFrozen)
			{
				BendRestCm[Index] = 0.0;
			}
			else if (BendRestCm[Index] <= 0.0)
			{
				BendRestCm[Index] = KMax((Points[Index + 2] - Points[Index]).Size(), 1.0);
			}
		}
	}

	double FWaterWhip::GetSegmentMassKg(const FSimWorld& World, int Segment) const
	{
		const FVolume* Volume = World.GetVolume(Segments[Segment]);
		return Volume && !World.IsDepleted(Segments[Segment]) ? Volume->MassKg : 0.0;
	}

	double FWaterWhip::GetMassKg(const FSimWorld& World) const
	{
		double Total = 0.0;
		for (int Segment = 0; Segment < GetNumSegments(); ++Segment)
		{
			Total += GetSegmentMassKg(World, Segment);
		}
		return Total;
	}

	bool FWaterWhip::ShouldCollapse(const FSimWorld& World) const
	{
		return IsActive() && GetMassKg(World) < CollapseMassFraction * InitialMassKg;
	}

	bool FWaterWhip::IsFrozen(const FSimWorld& World) const
	{
		int Frozen = 0;
		int Live = 0;
		for (int Segment = 0; Segment < GetNumSegments(); ++Segment)
		{
			const FVolume* Volume = World.GetVolume(Segments[Segment]);
			if (Volume && !World.IsDepleted(Segments[Segment]))
			{
				++Live;
				Frozen += Volume->Substance == ESubstance::Ice ? 1 : 0;
			}
		}
		return Live > 0 && Frozen == Live;
	}

	bool FWaterWhip::IsAnyFrozen(const FSimWorld& World) const
	{
		for (int Segment = 0; Segment < GetNumSegments(); ++Segment)
		{
			const FVolume* Volume = World.GetVolume(Segments[Segment]);
			if (Volume && Volume->Substance == ESubstance::Ice)
			{
				return true;
			}
		}
		return false;
	}

	double FWaterWhip::GetHeatToFreeze(const FSimWorld& World) const
	{
		double Total = 0.0;
		for (int Segment = 0; Segment < GetNumSegments(); ++Segment)
		{
			if (const FVolume* Volume = World.GetVolume(Segments[Segment]))
			{
				Total += HeatToFreeze(*Volume);
			}
		}
		return Total;
	}

	double FWaterWhip::GetHeatToMelt(const FSimWorld& World) const
	{
		double Total = 0.0;
		for (int Segment = 0; Segment < GetNumSegments(); ++Segment)
		{
			if (const FVolume* Volume = World.GetVolume(Segments[Segment]))
			{
				Total += HeatToMelt(*Volume);
			}
		}
		return Total;
	}

	double FWaterWhip::TransferHeat(FSimWorld& World, double HeatJ)
	{
		if (!IsActive() || HeatJ == 0.0)
		{
			return 0.0;
		}
		// The bender works along the stream from the hand: each segment changes phase completely before the next
		// one starts, so a bender short of chi freezes part of the whip rather than none of it. Heat beyond every
		// phase change is spread by mass.
		const double Sign = HeatJ < 0.0 ? -1.0 : 1.0;
		double Remaining = Sign * HeatJ;
		double Accepted = 0.0;
		for (int Segment = 0; Segment < GetNumSegments() && Remaining > 0.0; ++Segment)
		{
			const FVolume* Volume = World.GetVolume(Segments[Segment]);
			if (!Volume || World.IsDepleted(Segments[Segment]))
			{
				continue;
			}
			const double Need = Sign < 0.0 ? HeatToFreeze(*Volume) : HeatToMelt(*Volume);
			const double Portion = KMin(Need, Remaining);
			if (Portion > 0.0)
			{
				double SegmentAccepted = 0.0;
				World.TransferHeat(Segments[Segment], Sign * Portion, &SegmentAccepted);
				Accepted += SegmentAccepted;
				Remaining -= Portion;
			}
		}
		if (Remaining > 0.0)
		{
			double TotalMass = 0.0;
			for (int Segment = 0; Segment < GetNumSegments(); ++Segment)
			{
				TotalMass += GetSegmentMassKg(World, Segment);
			}
			for (int Segment = 0; Segment < GetNumSegments() && TotalMass > 0.0; ++Segment)
			{
				const double MassKg = GetSegmentMassKg(World, Segment);
				if (MassKg <= 0.0)
				{
					continue;
				}
				double SegmentAccepted = 0.0;
				World.TransferHeat(Segments[Segment], Sign * Remaining * MassKg / TotalMass, &SegmentAccepted);
				Accepted += SegmentAccepted;
			}
		}
		return Accepted;
	}

	int FWaterWhip::Release(FSimWorld& World, FWhipDrop* OutDrops, int MaxDrops)
	{
		int Count = 0;
		// Spray not yet collected by the owner goes out with the rest.
		for (int Index = 0; Index < NumPendingSpray && Count < MaxDrops; ++Index)
		{
			OutDrops[Count++] = PendingSpray[Index];
		}
		NumPendingSpray = 0;
		for (int Segment = 0; Segment < GetNumSegments(); ++Segment)
		{
			const FVolume* Volume = World.GetVolume(Segments[Segment]);
			if (Volume && !World.IsDepleted(Segments[Segment]) && Count < MaxDrops)
			{
				FWhipDrop& Drop = OutDrops[Count++];
				Drop.LocationCm = Volume->LocationCm;
				Drop.VelocityCmS = (Velocities[Segment] + Velocities[Segment + 1]) * 0.5;
				Drop.MassKg = Volume->MassKg;
				Drop.TemperatureK = Volume->TemperatureK;
				Drop.Substance = Volume->Substance;
			}
		}
		Destroy(World);
		return Count;
	}
}
