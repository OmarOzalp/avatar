#include "Sim/BendingTerrain.h"

namespace BendingSim
{
	namespace
	{
		/** Horizontal distance from (X, Y) to segment [A, B]. */
		double DistanceToSegment2D(double X, double Y, const FVec3& A, const FVec3& B)
		{
			const double AbX = B.X - A.X;
			const double AbY = B.Y - A.Y;
			const double LengthSquared = AbX * AbX + AbY * AbY;
			double T = 0.0;
			if (LengthSquared > SmallNumber)
			{
				T = KClamp(((X - A.X) * AbX + (Y - A.Y) * AbY) / LengthSquared, 0.0, 1.0);
			}
			const double Dx = X - (A.X + AbX * T);
			const double Dy = Y - (A.Y + AbY * T);
			return KSqrt(Dx * Dx + Dy * Dy);
		}

		int ClampInt(int V, int Lo, int Hi) { return V < Lo ? Lo : (V > Hi ? Hi : V); }

		/** Remaining volume below this fraction of the request ends the redistribution passes. */
		constexpr double VolumeTolerance = 1e-12;
		constexpr int MaxRedistributionPasses = 4;
	}

	// ---------------------------------------------------------------------------------------------------- Brush

	FTerrainBrush FTerrainBrush::Disc(const FVec3& InCenterCm, double InRadiusCm, double InFalloffCm)
	{
		return Ring(InCenterCm, 0.0, InRadiusCm, InFalloffCm);
	}

	FTerrainBrush FTerrainBrush::Ring(const FVec3& InCenterCm, double InInnerRadiusCm, double InOuterRadiusCm, double InFalloffCm)
	{
		return Band(InCenterCm, InCenterCm, InInnerRadiusCm, InOuterRadiusCm, InFalloffCm);
	}

	FTerrainBrush FTerrainBrush::Band(const FVec3& InStartCm, const FVec3& InEndCm, double InInnerCm, double InOuterCm, double InFalloffCm)
	{
		FTerrainBrush Brush;
		Brush.StartCm = InStartCm;
		Brush.EndCm = InEndCm;
		Brush.InnerCm = KMax(InInnerCm, 0.0);
		Brush.OuterCm = KMax(InOuterCm, Brush.InnerCm);
		Brush.FalloffCm = KMax(InFalloffCm, 0.0);
		return Brush;
	}

	double FTerrainBrush::GetWeight(double XCm, double YCm) const
	{
		const double D = DistanceToSegment2D(XCm, YCm, StartCm, EndCm);
		if (D > OuterCm)
		{
			return 1.0 - KSmoothStep(OuterCm, OuterCm + FalloffCm, D);
		}
		if (D < InnerCm)
		{
			return KSmoothStep(KMax(InnerCm - FalloffCm, 0.0), InnerCm, D);
		}
		return 1.0;
	}

	// ---------------------------------------------------------------------------------------------------- Field

	bool FTerrain::Init(int InSamplesX, int InSamplesY, double InCellSizeCm, const FVec3& InOriginCm, double InBaseHeightCm)
	{
		if (InSamplesX < 2 || InSamplesY < 2 || InSamplesX > MaxSamplesPerSide || InSamplesY > MaxSamplesPerSide || InCellSizeCm <= 0.0)
		{
			return false;
		}
		SamplesX = InSamplesX;
		SamplesY = InSamplesY;
		CellSizeCm = InCellSizeCm;
		OriginCm = InOriginCm;
		BaseHeightCm = InBaseHeightCm;
		for (int Index = 0; Index < SamplesX * SamplesY; ++Index)
		{
			Heights[Index] = BaseHeightCm;
		}
		MarkAllDirty();
		return true;
	}

	void FTerrain::SetSampleHeight(int X, int Y, double HeightCm)
	{
		if (X < 0 || Y < 0 || X >= SamplesX || Y >= SamplesY)
		{
			return;
		}
		Heights[Y * SamplesX + X] = KClamp(HeightCm, GetBedrockHeightCm(), BaseHeightCm + MaxRaiseCm);
		MarkDirty(X, Y);
	}

	FVec3 FTerrain::GetSampleLocation(int X, int Y) const
	{
		return FVec3(OriginCm.X + X * CellSizeCm, OriginCm.Y + Y * CellSizeCm, GetSampleHeight(X, Y));
	}

	double FTerrain::SampleClamped(int X, int Y) const
	{
		return Heights[ClampInt(Y, 0, SamplesY - 1) * SamplesX + ClampInt(X, 0, SamplesX - 1)];
	}

	bool FTerrain::IsInside(double XCm, double YCm) const
	{
		const double U = (XCm - OriginCm.X) / CellSizeCm;
		const double V = (YCm - OriginCm.Y) / CellSizeCm;
		return U >= 0.0 && V >= 0.0 && U <= SamplesX - 1 && V <= SamplesY - 1;
	}

	double FTerrain::GetHeightAt(double XCm, double YCm) const
	{
		if (SamplesX < 2)
		{
			return BaseHeightCm;
		}
		const double U = KClamp((XCm - OriginCm.X) / CellSizeCm, 0.0, static_cast<double>(SamplesX - 1));
		const double V = KClamp((YCm - OriginCm.Y) / CellSizeCm, 0.0, static_cast<double>(SamplesY - 1));
		const int X0 = ClampInt(static_cast<int>(U), 0, SamplesX - 2);
		const int Y0 = ClampInt(static_cast<int>(V), 0, SamplesY - 2);
		const double Fx = U - X0;
		const double Fy = V - Y0;

		const double H00 = GetSampleHeight(X0, Y0);
		const double H10 = GetSampleHeight(X0 + 1, Y0);
		const double H01 = GetSampleHeight(X0, Y0 + 1);
		const double H11 = GetSampleHeight(X0 + 1, Y0 + 1);
		// Diagonal (X0, Y0) -> (X0 + 1, Y0 + 1).
		if (Fx >= Fy)
		{
			return H00 + (H10 - H00) * Fx + (H11 - H10) * Fy;
		}
		return H00 + (H11 - H01) * Fx + (H01 - H00) * Fy;
	}

	FVec3 FTerrain::GetNormalAt(double XCm, double YCm) const
	{
		const double D = CellSizeCm;
		const double DhDx = (GetHeightAt(XCm + D, YCm) - GetHeightAt(XCm - D, YCm)) / (2.0 * D);
		const double DhDy = (GetHeightAt(XCm, YCm + D) - GetHeightAt(XCm, YCm - D)) / (2.0 * D);
		return FVec3(-DhDx, -DhDy, 1.0).GetSafeNormal();
	}

	bool FTerrain::Raycast(const FVec3& StartCm, const FVec3& DirectionUnit, double MaxDistanceCm, FVec3& OutHitCm) const
	{
		auto Clearance = [this, &StartCm, &DirectionUnit](double T)
		{
			const FVec3 P = StartCm + DirectionUnit * T;
			return P.Z - GetHeightAt(P.X, P.Y);
		};

		if (Clearance(0.0) <= 0.0)
		{
			OutHitCm = StartCm;
			return true;
		}
		const double StepCm = 0.5 * CellSizeCm;
		double Previous = 0.0;
		for (double T = StepCm; Previous < MaxDistanceCm; T += StepCm)
		{
			const double Current = KMin(T, MaxDistanceCm);
			if (Clearance(Current) <= 0.0)
			{
				double Lo = Previous;
				double Hi = Current;
				for (int Iteration = 0; Iteration < 20; ++Iteration)
				{
					const double Mid = 0.5 * (Lo + Hi);
					(Clearance(Mid) > 0.0 ? Lo : Hi) = Mid;
				}
				OutHitCm = StartCm + DirectionUnit * Hi;
				return true;
			}
			Previous = Current;
		}
		return false;
	}

	// ---------------------------------------------------------------------------------------------------- Edits

	void FTerrain::GetBrushSampleRange(const FTerrainBrush& Brush, int& MinX, int& MinY, int& MaxX, int& MaxY) const
	{
		const double Reach = Brush.GetReachCm();
		const double Left = KMin(Brush.StartCm.X, Brush.EndCm.X) - Reach;
		const double Right = KMax(Brush.StartCm.X, Brush.EndCm.X) + Reach;
		const double Bottom = KMin(Brush.StartCm.Y, Brush.EndCm.Y) - Reach;
		const double Top = KMax(Brush.StartCm.Y, Brush.EndCm.Y) + Reach;
		MinX = ClampInt(static_cast<int>(KFloor((Left - OriginCm.X) / CellSizeCm)), 0, SamplesX - 1);
		MaxX = ClampInt(static_cast<int>(KFloor((Right - OriginCm.X) / CellSizeCm)) + 1, 0, SamplesX - 1);
		MinY = ClampInt(static_cast<int>(KFloor((Bottom - OriginCm.Y) / CellSizeCm)), 0, SamplesY - 1);
		MaxY = ClampInt(static_cast<int>(KFloor((Top - OriginCm.Y) / CellSizeCm)) + 1, 0, SamplesY - 1);
	}

	void FTerrain::SetHeightTracked(int Index, double NewHeightCm, FTerrainEdit& Edit)
	{
		const double Floor = GetBedrockHeightCm();
		const double OldM = CmToM(Heights[Index] - Floor);
		const double NewM = CmToM(NewHeightCm - Floor);
		// Potential energy of a soil column of height h over its cell: rho * g * A * h^2 / 2.
		Edit.PotentialEnergyChangeJ += SoilDensityKgM3 * GravityMs2 * GetCellAreaM2() * 0.5 * (NewM * NewM - OldM * OldM);
		Heights[Index] = NewHeightCm;
		MarkDirty(Index % SamplesX, Index / SamplesX);
	}

	double FTerrain::RemoveWeighted(const FTerrainBrush& Brush, double VolumeM3, FTerrainEdit& Edit)
	{
		if (VolumeM3 <= 0.0 || SamplesX < 2)
		{
			return 0.0;
		}
		int MinX, MinY, MaxX, MaxY;
		GetBrushSampleRange(Brush, MinX, MinY, MaxX, MaxY);
		const double Floor = GetBedrockHeightCm();
		const double CellArea = GetCellAreaM2();

		double Remaining = VolumeM3;
		for (int Pass = 0; Pass < MaxRedistributionPasses && Remaining > VolumeM3 * VolumeTolerance; ++Pass)
		{
			double SumWeights = 0.0;
			for (int Y = MinY; Y <= MaxY; ++Y)
			{
				for (int X = MinX; X <= MaxX; ++X)
				{
					if (Heights[Y * SamplesX + X] > Floor)
					{
						SumWeights += Brush.GetWeight(OriginCm.X + X * CellSizeCm, OriginCm.Y + Y * CellSizeCm);
					}
				}
			}
			if (SumWeights <= SmallNumber)
			{
				break;
			}
			const double DepthPerWeightCm = MToCm(Remaining / (SumWeights * CellArea));
			double Removed = 0.0;
			for (int Y = MinY; Y <= MaxY; ++Y)
			{
				for (int X = MinX; X <= MaxX; ++X)
				{
					const int Index = Y * SamplesX + X;
					const double Available = Heights[Index] - Floor;
					if (Available <= 0.0)
					{
						continue;
					}
					const double Weight = Brush.GetWeight(OriginCm.X + X * CellSizeCm, OriginCm.Y + Y * CellSizeCm);
					const double TakeCm = KMin(Weight * DepthPerWeightCm, Available);
					if (TakeCm > 0.0)
					{
						SetHeightTracked(Index, Heights[Index] - TakeCm, Edit);
						Removed += CmToM(TakeCm) * CellArea;
					}
				}
			}
			Remaining -= Removed;
		}
		return VolumeM3 - KMax(Remaining, 0.0);
	}

	double FTerrain::AddWeighted(const FTerrainBrush& Brush, double VolumeM3, FTerrainEdit& Edit)
	{
		if (VolumeM3 <= 0.0 || SamplesX < 2)
		{
			return 0.0;
		}
		int MinX, MinY, MaxX, MaxY;
		GetBrushSampleRange(Brush, MinX, MinY, MaxX, MaxY);
		const double Ceiling = BaseHeightCm + MaxRaiseCm;
		const double CellArea = GetCellAreaM2();

		double Remaining = VolumeM3;
		for (int Pass = 0; Pass < MaxRedistributionPasses && Remaining > VolumeM3 * VolumeTolerance; ++Pass)
		{
			double SumWeights = 0.0;
			for (int Y = MinY; Y <= MaxY; ++Y)
			{
				for (int X = MinX; X <= MaxX; ++X)
				{
					if (Heights[Y * SamplesX + X] < Ceiling)
					{
						SumWeights += Brush.GetWeight(OriginCm.X + X * CellSizeCm, OriginCm.Y + Y * CellSizeCm);
					}
				}
			}
			if (SumWeights <= SmallNumber)
			{
				break;
			}
			const double DepthPerWeightCm = MToCm(Remaining / (SumWeights * CellArea));
			double Added = 0.0;
			for (int Y = MinY; Y <= MaxY; ++Y)
			{
				for (int X = MinX; X <= MaxX; ++X)
				{
					const int Index = Y * SamplesX + X;
					const double Room = Ceiling - Heights[Index];
					if (Room <= 0.0)
					{
						continue;
					}
					const double Weight = Brush.GetWeight(OriginCm.X + X * CellSizeCm, OriginCm.Y + Y * CellSizeCm);
					const double PutCm = KMin(Weight * DepthPerWeightCm, Room);
					if (PutCm > 0.0)
					{
						SetHeightTracked(Index, Heights[Index] + PutCm, Edit);
						Added += CmToM(PutCm) * CellArea;
					}
				}
			}
			Remaining -= Added;
		}
		return VolumeM3 - KMax(Remaining, 0.0);
	}

	FTerrainEdit FTerrain::MoveSoil(const FTerrainBrush& Source, const FTerrainBrush& Target, double VolumeM3)
	{
		FTerrainEdit Edit;
		const double Removed = RemoveWeighted(Source, VolumeM3, Edit);
		const double Added = AddWeighted(Target, Removed, Edit);
		if (Added < Removed)
		{
			// Target hit the height limit: what did not fit goes back where it came from.
			AddWeighted(Source, Removed - Added, Edit);
		}
		Edit.VolumeM3 = Added;
		Edit.MassKg = Added * SoilDensityKgM3;
		Edit.bLimited = Added < VolumeM3 * (1.0 - 1e-9);
		return Edit;
	}

	FTerrainEdit FTerrain::RemoveSoil(const FTerrainBrush& Brush, double VolumeM3)
	{
		FTerrainEdit Edit;
		Edit.VolumeM3 = RemoveWeighted(Brush, VolumeM3, Edit);
		Edit.MassKg = Edit.VolumeM3 * SoilDensityKgM3;
		Edit.bLimited = Edit.VolumeM3 < VolumeM3 * (1.0 - 1e-9);
		return Edit;
	}

	FTerrainEdit FTerrain::AddSoil(const FTerrainBrush& Brush, double VolumeM3)
	{
		FTerrainEdit Edit;
		Edit.VolumeM3 = AddWeighted(Brush, VolumeM3, Edit);
		Edit.MassKg = Edit.VolumeM3 * SoilDensityKgM3;
		Edit.bLimited = Edit.VolumeM3 < VolumeM3 * (1.0 - 1e-9);
		return Edit;
	}

	double FTerrain::GetBrushAreaM2(const FTerrainBrush& Brush) const
	{
		int MinX, MinY, MaxX, MaxY;
		GetBrushSampleRange(Brush, MinX, MinY, MaxX, MaxY);
		double SumWeights = 0.0;
		for (int Y = MinY; Y <= MaxY; ++Y)
		{
			for (int X = MinX; X <= MaxX; ++X)
			{
				SumWeights += Brush.GetWeight(OriginCm.X + X * CellSizeCm, OriginCm.Y + Y * CellSizeCm);
			}
		}
		return SumWeights * GetCellAreaM2();
	}

	double FTerrain::GetBrushMeanHeightCm(const FTerrainBrush& Brush) const
	{
		int MinX, MinY, MaxX, MaxY;
		GetBrushSampleRange(Brush, MinX, MinY, MaxX, MaxY);
		double SumWeights = 0.0;
		double SumHeights = 0.0;
		for (int Y = MinY; Y <= MaxY; ++Y)
		{
			for (int X = MinX; X <= MaxX; ++X)
			{
				const double Weight = Brush.GetWeight(OriginCm.X + X * CellSizeCm, OriginCm.Y + Y * CellSizeCm);
				SumWeights += Weight;
				SumHeights += Weight * Heights[Y * SamplesX + X];
			}
		}
		return SumWeights > SmallNumber ? SumHeights / SumWeights : GetHeightAt(Brush.StartCm.X, Brush.StartCm.Y);
	}

	double FTerrain::GetTotalSoilVolumeM3() const
	{
		const double Floor = GetBedrockHeightCm();
		double SumCm = 0.0;
		for (int Index = 0; Index < SamplesX * SamplesY; ++Index)
		{
			SumCm += Heights[Index] - Floor;
		}
		return CmToM(SumCm) * GetCellAreaM2();
	}

	double FTerrain::GetTotalPotentialEnergyJ() const
	{
		const double Floor = GetBedrockHeightCm();
		double SumM2 = 0.0;
		for (int Index = 0; Index < SamplesX * SamplesY; ++Index)
		{
			const double H = CmToM(Heights[Index] - Floor);
			SumM2 += H * H;
		}
		return SoilDensityKgM3 * GravityMs2 * GetCellAreaM2() * 0.5 * SumM2;
	}

	// ---------------------------------------------------------------------------------------------------- Dirty tracking

	void FTerrain::MarkDirty(int X, int Y)
	{
		if (DirtyMaxX < DirtyMinX)
		{
			DirtyMinX = DirtyMaxX = X;
			DirtyMinY = DirtyMaxY = Y;
			return;
		}
		DirtyMinX = X < DirtyMinX ? X : DirtyMinX;
		DirtyMaxX = X > DirtyMaxX ? X : DirtyMaxX;
		DirtyMinY = Y < DirtyMinY ? Y : DirtyMinY;
		DirtyMaxY = Y > DirtyMaxY ? Y : DirtyMaxY;
	}

	void FTerrain::MarkAllDirty()
	{
		DirtyMinX = 0;
		DirtyMinY = 0;
		DirtyMaxX = SamplesX - 1;
		DirtyMaxY = SamplesY - 1;
	}

	bool FTerrain::ConsumeDirtyRegion(int& OutMinX, int& OutMinY, int& OutMaxX, int& OutMaxY)
	{
		if (DirtyMaxX < DirtyMinX)
		{
			return false;
		}
		OutMinX = DirtyMinX;
		OutMinY = DirtyMinY;
		OutMaxX = DirtyMaxX;
		OutMaxY = DirtyMaxY;
		DirtyMinX = 0;
		DirtyMinY = 0;
		DirtyMaxX = -1;
		DirtyMaxY = -1;
		return true;
	}
}
