#pragma once

#include "Sim/BendingSimMath.h"

/**
 * Deformable ground for earthbending: a heightfield of soil over bedrock.
 *
 * Earthbending moves soil; it never creates or destroys it. Raising a pillar digs the ring around it, a wall
 * comes out of the trench beside it, a boulder pulled from the ground leaves its crater. The work a bender does
 * is the change in the soil's gravitational potential energy, so tall walls cost more than low ones, and digging
 * soil out to a rim at the same level costs almost nothing.
 *
 * Triangulation (shared by every renderer and by GetHeightAt): cell (X, Y) is split along the diagonal from
 * sample (X, Y) to sample (X + 1, Y + 1).
 */
namespace BendingSim
{
	/**
	 * Where an edit acts, by horizontal distance from a core point (disc, ring) or segment (wall, trench):
	 * full strength between InnerCm and OuterCm, smooth falloff over FalloffCm outside that band.
	 */
	struct BENDINGSIM_API FTerrainBrush
	{
		FVec3 StartCm;
		/** Equal to StartCm for discs and rings. */
		FVec3 EndCm;
		double InnerCm = 0.0;
		double OuterCm = 100.0;
		double FalloffCm = 50.0;

		static FTerrainBrush Disc(const FVec3& CenterCm, double RadiusCm, double FalloffCm);
		static FTerrainBrush Ring(const FVec3& CenterCm, double InnerRadiusCm, double OuterRadiusCm, double FalloffCm);
		/** Band around a segment: InnerCm = 0 is a wall core, InnerCm > 0 the trenches on both sides of it. */
		static FTerrainBrush Band(const FVec3& StartCm, const FVec3& EndCm, double InnerCm, double OuterCm, double FalloffCm);

		double GetWeight(double XCm, double YCm) const;
		double GetReachCm() const { return OuterCm + FalloffCm; }
	};

	/** What one edit did. */
	struct FTerrainEdit
	{
		double VolumeM3 = 0.0;
		double MassKg = 0.0;
		/** Change of the soil's gravitational potential energy (J). Positive = work a bender must supply. */
		double PotentialEnergyChangeJ = 0.0;
		/** Less than requested was moved: bedrock under the source or the height limit over the target. */
		bool bLimited = false;
	};

	class BENDINGSIM_API FTerrain
	{
	public:
		static constexpr int MaxSamplesPerSide = 257;

		/** Loose soil, compacted, kg/m^3. */
		double SoilDensityKgM3 = 1600.0;
		/** Pore fraction used for moisture (mud) on this ground. */
		double SoilPorosity = 0.35;
		/** Soil depth (cm) over bedrock below the base height: how deep a bender can dig. */
		double BedrockDepthCm = 300.0;
		/** Highest the ground can be raised above the base height (cm). */
		double MaxRaiseCm = 1200.0;
		double GravityMs2 = 9.80665;

		/** Flat ground at BaseHeightCm. SamplesX/Y in [2, MaxSamplesPerSide]. */
		bool Init(int InSamplesX, int InSamplesY, double InCellSizeCm, const FVec3& InOriginCm, double InBaseHeightCm);

		int GetSamplesX() const { return SamplesX; }
		int GetSamplesY() const { return SamplesY; }
		double GetCellSizeCm() const { return CellSizeCm; }
		/** World position of sample (0, 0). */
		const FVec3& GetOriginCm() const { return OriginCm; }
		double GetBaseHeightCm() const { return BaseHeightCm; }
		double GetBedrockHeightCm() const { return BaseHeightCm - BedrockDepthCm; }
		double GetSizeXCm() const { return (SamplesX - 1) * CellSizeCm; }
		double GetSizeYCm() const { return (SamplesY - 1) * CellSizeCm; }

		double GetSampleHeight(int X, int Y) const { return Heights[Y * SamplesX + X]; }
		/** Direct write for world generation; not conserving, not billed. */
		void SetSampleHeight(int X, int Y, double HeightCm);
		FVec3 GetSampleLocation(int X, int Y) const;
		/** Contiguous heights (cm), row-major, SamplesX per row: renderers read this directly. */
		const double* GetHeightData() const { return Heights; }

		/** Ground height (cm) under a point, on the shared triangulation. Clamped to the edge outside the field. */
		double GetHeightAt(double XCm, double YCm) const;
		/** Smooth surface normal (central differences). */
		FVec3 GetNormalAt(double XCm, double YCm) const;
		bool IsInside(double XCm, double YCm) const;
		/** First hit of a ray (DirectionUnit normalized) with the ground within MaxDistanceCm. */
		bool Raycast(const FVec3& StartCm, const FVec3& DirectionUnit, double MaxDistanceCm, FVec3& OutHitCm) const;

		// ---------------------------------------------------------------- Earthbending (volume-conserving)

		/** Moves up to VolumeM3 of soil from under Source to under Target. */
		FTerrainEdit MoveSoil(const FTerrainBrush& Source, const FTerrainBrush& Target, double VolumeM3);
		/** Takes up to VolumeM3 out of the ground (a boulder being pulled up). The edit reports what came out. */
		FTerrainEdit RemoveSoil(const FTerrainBrush& Brush, double VolumeM3);
		/** Puts VolumeM3 into the ground (a boulder merging back, a wall collapsing). */
		FTerrainEdit AddSoil(const FTerrainBrush& Brush, double VolumeM3);

		/** Weighted ground area under a brush (m^2): raising it by H metres takes H * area of soil. */
		double GetBrushAreaM2(const FTerrainBrush& Brush) const;
		/** Mean ground height under a brush, weighted by it (cm). */
		double GetBrushMeanHeightCm(const FTerrainBrush& Brush) const;

		/** Soil above bedrock (m^3) and its potential energy (J); conserved / accounted by every edit. */
		double GetTotalSoilVolumeM3() const;
		double GetTotalPotentialEnergyJ() const;
		double GetCellAreaM2() const { return CmToM(CellSizeCm) * CmToM(CellSizeCm); }

		/** Sample rectangle changed since the last call (inclusive). False if nothing changed. */
		bool ConsumeDirtyRegion(int& OutMinX, int& OutMinY, int& OutMaxX, int& OutMaxY);
		void MarkAllDirty();

	private:
		void GetBrushSampleRange(const FTerrainBrush& Brush, int& MinX, int& MinY, int& MaxX, int& MaxY) const;
		/** Removes VolumeM3 spread by brush weight, never below bedrock. Returns the volume actually removed. */
		double RemoveWeighted(const FTerrainBrush& Brush, double VolumeM3, FTerrainEdit& Edit);
		/** Adds VolumeM3 spread by brush weight, never above the height limit. Returns the volume actually added. */
		double AddWeighted(const FTerrainBrush& Brush, double VolumeM3, FTerrainEdit& Edit);
		void SetHeightTracked(int Index, double NewHeightCm, FTerrainEdit& Edit);
		void MarkDirty(int X, int Y);
		double SampleClamped(int X, int Y) const;

		int SamplesX = 0;
		int SamplesY = 0;
		double CellSizeCm = 100.0;
		FVec3 OriginCm;
		double BaseHeightCm = 0.0;

		int DirtyMinX = 0;
		int DirtyMinY = 0;
		int DirtyMaxX = -1;
		int DirtyMaxY = -1;

		double Heights[MaxSamplesPerSide * MaxSamplesPerSide] = {};
	};
}
