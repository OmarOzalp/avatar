#include "Sim/BendingWaterWhip.h"

namespace BendingSim
{
	namespace
	{
		constexpr double MinSegmentRadiusCm = 2.0;
		constexpr double MaxSegmentRadiusCm = 45.0;
		constexpr int MaxSubstepsPerFrame = 16;
		/** Below this share of the water drawn, the whip can no longer hold together. */
		constexpr double CollapseMassFraction = 0.15;

		/** Capsule radius holding MassKg at DensityKgM3 over a core of LengthCm (cylinder approximation). */
		double SegmentRadiusFor(double MassKg, double DensityKgM3, double LengthCm)
		{
			const double LengthM = CmToM(KMax(LengthCm, 1.0));
			const double RadiusM = KSqrt(KMax(MassKg, 0.0) / (KMax(DensityKgM3, 1.0) * Pi * LengthM));
			return KClamp(MToCm(RadiusM), MinSegmentRadiusCm, MaxSegmentRadiusCm);
		}
	}

	bool FWaterWhip::Create(FSimWorld& World, const FVec3& SourceCm, const FVec3& InHandCm, double WaterMassKg, double TemperatureK)
	{
		if (IsActive() || WaterMassKg <= 0.0)
		{
			return false;
		}
		NumPoints = Settings.NumPoints < 3 ? 3 : (Settings.NumPoints > MaxPoints ? MaxPoints : Settings.NumPoints);
		HandCm = InHandCm;
		PreviousHandCm = InHandCm;
		HandVelocityCmS = FVec3();

		const FVec3 Away = SourceCm - InHandCm;
		const FVec3 Flat(Away.X, Away.Y, 0.0);
		Forward = Flat.Size() > 1.0 ? Flat.GetSafeNormal() : FVec3(1.0, 0.0, 0.0);
		Right = Forward.Cross(FVec3(0.0, 0.0, 1.0));
		AimDirection = Forward;
		AimPointCm = InHandCm + Forward * Settings.LengthCm;

		const int NumSegments = NumPoints - 1;
		InitialSpacingCm = Away.Size() / NumSegments;
		const double SegmentMassKg = WaterMassKg / NumSegments;
		for (int Index = 0; Index < NumPoints; ++Index)
		{
			Points[Index] = InHandCm + Away * (static_cast<double>(Index) / NumSegments);
			Velocities[Index] = FVec3();
			BendRestCm[Index] = 0.0;
		}
		for (int Segment = 0; Segment < NumSegments; ++Segment)
		{
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
		State = EWhipState::Forming;
		StateTimeS = 0.0;
		LashRecoverRemainingS = 0.0;
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

	bool FWaterWhip::Lash()
	{
		if (State != EWhipState::Holding && State != EWhipState::Lashing)
		{
			return false;
		}
		// A whip that is still stretched out stays out while the new lash runs down it.
		LashCarry = GetLashWeight(NumPoints - 1);
		State = EWhipState::Lashing;
		StateTimeS = 0.0;
		LashRecoverRemainingS = 0.0;
		return true;
	}

	double FWaterWhip::GetFormProgress() const
	{
		if (State != EWhipState::Forming)
		{
			return State == EWhipState::Inactive ? 0.0 : 1.0;
		}
		return KClamp(StateTimeS / KMax(Settings.FormSeconds, 0.01), 0.0, 1.0);
	}

	double FWaterWhip::GetLashWeight(int Index) const
	{
		const double S = static_cast<double>(Index) / (NumPoints - 1);
		if (State == EWhipState::Lashing)
		{
			// The straightening wave runs from the root (s = 0) to the tip (s = 1) and passes it at 80% of the lash.
			const double Front = 1.25 * StateTimeS / KMax(Settings.LashDurationS, 0.01);
			return KMax(KClamp((Front - S) / 0.2, 0.0, 1.0), LashCarry);
		}
		return LashRecoverRemainingS > 0.0 ? LashRecoverRemainingS / KMax(Settings.LashRecoverS, 0.01) : 0.0;
	}

	FVec3 FWaterWhip::GetTarget(int Index, double LashBlend) const
	{
		const double S = static_cast<double>(Index) / (NumPoints - 1);
		const double L = Settings.LengthCm;
		const FVec3 Up(0.0, 0.0, 1.0);

		// Held: a coil in front of the bender, flowing slightly so it reads as liquid.
		const double Ahead = L * 0.36 * KSin(0.85 * Pi * S);
		double Lift = L * 0.20 * KSin(Pi * S) * (1.0 - 0.35 * S) + L * 0.025 * KSin(2.3 * ElapsedS + 5.1 * S);
		double Side = L * 0.16 * KSin(1.5 * Pi * S) + L * 0.025 * KCos(1.9 * ElapsedS + 4.3 * S);
		const FVec3 Held = HandCm + Forward * Ahead + Up * Lift + Right * Side;
		if (LashBlend <= 0.0)
		{
			return Held;
		}
		// Lashed: a straight line from the hand to the aim.
		const FVec3 Lashed = HandCm + AimDirection * (S * L);
		return Held + (Lashed - Held) * LashBlend;
	}

	void FWaterWhip::PreStep(FSimWorld& World, const FTerrain* Terrain, double DeltaSeconds)
	{
		if (!IsActive() || DeltaSeconds <= 0.0)
		{
			return;
		}
		ElapsedS += DeltaSeconds;
		StateTimeS += DeltaSeconds;
		LashRecoverRemainingS = KMax(LashRecoverRemainingS - DeltaSeconds, 0.0);
		if (State == EWhipState::Forming && StateTimeS >= Settings.FormSeconds)
		{
			State = EWhipState::Holding;
			StateTimeS = 0.0;
		}
		else if (State == EWhipState::Lashing && StateTimeS >= Settings.LashDurationS && !bHoldExtended)
		{
			State = EWhipState::Holding;
			StateTimeS = 0.0;
			LashRecoverRemainingS = Settings.LashRecoverS;
		}

		const double FormProgress = GetFormProgress();
		const double SpacingCm = Settings.LengthCm / (NumPoints - 1);
		const double RestCm = State == EWhipState::Forming ? KLerp(InitialSpacingCm, SpacingCm, FormProgress) : SpacingCm;
		const double GainScale = State == EWhipState::Forming ? 0.25 + 0.75 * FormProgress : 1.0;
		const double MaxAccelerationCmS2 = MToCm(Settings.MaxControlAccelerationMs2);
		const FVec3 Gravity(0.0, 0.0, World.Settings.GravityZCmS2);

		int NumSubsteps = static_cast<int>(DeltaSeconds * KMax(Settings.SubstepHz, 30.0)) + 1;
		NumSubsteps = NumSubsteps > MaxSubstepsPerFrame ? MaxSubstepsPerFrame : NumSubsteps;
		const double H = DeltaSeconds / NumSubsteps;

		FVec3 Targets[MaxPoints];
		double Gains[MaxPoints] = {};
		for (int Index = 1; Index < NumPoints; ++Index)
		{
			const double Lash = GetLashWeight(Index);
			Targets[Index] = GetTarget(Index, Lash);
			Gains[Index] = Settings.ShapeStiffness * GainScale * (1.0 + (Settings.LashStiffnessScale - 1.0) * Lash);
		}

		FVec3 Predicted[MaxPoints];
		for (int Substep = 0; Substep < NumSubsteps; ++Substep)
		{
			const double Alpha = static_cast<double>(Substep + 1) / NumSubsteps;
			const FVec3 Hand = PreviousHandCm + (HandCm - PreviousHandCm) * Alpha;
			const FVec3 HandOffset = Hand - HandCm;

			Predicted[0] = Hand;
			for (int Index = 1; Index < NumPoints; ++Index)
			{
				// The bender's force: spring toward the intended shape, damping relative to the hand, gravity
				// cancelled; all of it capped at what the bender can exert. Gravity then acts in full.
				const FVec3 Error = Targets[Index] + HandOffset - Points[Index];
				FVec3 Control = Error * Gains[Index] - (Velocities[Index] - HandVelocityCmS) * Settings.ShapeDamping - Gravity;
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
				Velocities[Index] = (Predicted[Index] - Points[Index]) / H;
				Points[Index] = Predicted[Index];
			}
		}
		PreviousHandCm = HandCm;
		SyncVolumes(World);
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
		double TotalMass = 0.0;
		for (int Segment = 0; Segment < GetNumSegments() && Remaining > 0.0; ++Segment)
		{
			const FVolume* Volume = World.GetVolume(Segments[Segment]);
			if (!Volume || World.IsDepleted(Segments[Segment]))
			{
				continue;
			}
			TotalMass += Volume->MassKg;
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
			TotalMass = 0.0;
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
