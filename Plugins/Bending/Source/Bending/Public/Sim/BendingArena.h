#pragma once

#include "Sim/BendingCombustion.h"
#include "Sim/BendingSimTypes.h"
#include "Sim/BendingTerrain.h"

/**
 * The training ground: one world definition (terrain shape, ponds, props) that every engine builds, so the
 * browser sandbox and the Unreal level are the same place. Positions are cm with Z up; prop Z is filled in from
 * the generated terrain.
 */
namespace BendingSim
{
	enum class EArenaProp : unsigned char
	{
		/** 6 kg granite stone. */
		Stone,
		/** 60 kg granite rock. */
		Rock,
		/** 600 kg granite boulder. */
		Boulder,
		/** 100 kg ball of loose soil: soaks up water and turns to mud. */
		Clod,
		/** 60 kg wooden training dummy (upright capsule). */
		Dummy,
		/** Stone pedestal with a flame kept burning on it; water puts it out, fire relights it. */
		Brazier,
		/** 40 kg block of ice frozen to the ground; fire melts it (about 14 MJ). */
		IceBlock,
		/** Cloth banner on a wooden pole, in an element's colours: fire burns the cloth away, water puts it out. */
		Banner,
		/** Stone lantern: fire lights its wick, water puts it out. */
		Lantern,
		/** Bale of straw: catches easily and burns big, setting what stands near it alight. */
		StrawBale,
		/** Wooden crate: smashes when hit hard, burns. */
		Crate,
		/** Barrel of water: bursts when hit hard and soaks everything around it; a waterbender can draw from it. */
		WaterBarrel,

		Count
	};

	struct FArenaPropSpec
	{
		const char* Name = "";
		ESubstance Substance = ESubstance::Earth;
		double MassKg = 1.0;
		double DensityKgM3 = 2600.0;
		double Porosity = 0.0;
		double TemperatureK = 288.15;
		/** Sphere radius, or capsule radius when HalfHeightCm > 0 (upright capsule). */
		double RadiusCm = 10.0;
		double HalfHeightCm = 0.0;
		/** Height of the volume's centre above the ground it stands on. */
		double CenterHeightCm = 10.0;
		/** Does not move (pedestals, ice frozen to the ground). */
		bool bStatic = false;
		/** How it burns (cloth, straw, wood, a wick); not flammable by default. */
		FCombustionSpec Combustion;
		/** A hit that changes its speed by more than this smashes it (m/s); 0: unbreakable. */
		double BreakSpeedMs = 0.0;
		/** Water it holds (kg): spilled when it breaks, and drawable by a waterbender. */
		double WaterKg = 0.0;
		/** Where its flame burns, above the ground it stands on (banners: the bottom of the cloth). */
		double FlameHeightCm = 0.0;
	};

	struct FArenaPropPlacement
	{
		EArenaProp Kind = EArenaProp::Stone;
		FVec3 LocationCm;
		/** Facing (banners face the ring; crates sit askew). */
		double YawDeg = 0.0;
		/** Banners: the element they show (ETechniqueElement order, 1..4). */
		int Variant = 0;
	};

	struct FArenaPond
	{
		FVec3 CenterCm;
		double RadiusCm = 400.0;
		/** Water surface height (cm). */
		double SurfaceHeightCm = -15.0;
		double WaterDepthCm = 80.0;
		/** Water available to draw (kg): the pond's real volume of water. */
		double WaterKg = 0.0;
	};

	struct FArenaLayout
	{
		static constexpr int MaxProps = 80;
		/** Radius of the sand ring at the centre of the training ground. */
		static constexpr double RingRadiusCm = 820.0;
		static constexpr int MaxPonds = 4;

		int SamplesPerSide = 241;
		double CellSizeCm = 40.0;
		FVec3 OriginCm = FVec3(-4800.0, -4800.0, 0.0);
		FVec3 PlayerStartCm;
		double PlayerStartYawDeg = 0.0;

		FArenaPond Ponds[MaxPonds];
		int NumPonds = 0;
		FArenaPropPlacement Props[MaxProps];
		int NumProps = 0;
	};

	BENDINGSIM_API const FArenaPropSpec& GetArenaPropSpec(EArenaProp Kind);
	/** The volume a prop puts into the simulation, standing on the ground at GroundCm. */
	BENDINGSIM_API FVolume MakeArenaPropVolume(EArenaProp Kind, const FVec3& GroundCm);

	/** The training ground (positions only; call GenerateArenaTerrain, then read prop heights from the terrain). */
	BENDINGSIM_API const FArenaLayout& GetTrainingGroundLayout();
	/** Shapes the terrain: plaza, hills, pond basins and a boundary ridge. */
	BENDINGSIM_API void GenerateArenaTerrain(const FArenaLayout& Layout, FTerrain& Terrain);
	/** Banners: the cloth hangs from this height down to BannerClothBottomCm. */
	inline constexpr double BannerClothTopCm = 405.0;
	inline constexpr double BannerClothBottomCm = 175.0;
	inline constexpr double BannerClothWidthCm = 95.0;

	/** Nearest pond whose surface lies within RangeCm of a point; returns its index or -1. OutSourceCm = closest surface point. */
	BENDINGSIM_API int FindWaterSource(const FArenaLayout& Layout, const FVec3& FromCm, double RangeCm, FVec3& OutSourceCm);
}
