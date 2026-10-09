#include "Sim/BendingTechniques.h"

namespace BendingSim
{
	namespace
	{
		using TechId = ETechnique;
		using TechElem = ETechniqueElement;
		using TechSlot = ETechniqueSlot;

		// technique, name, description,
		//     element, slot, startup / active / recovery frames, chi, stamina, hold
		const FTechniqueInfo GTechniques[] = {
			{ TechId::None, "", "", TechElem::None, TechSlot::Primary, 1, 1, 0, 0.0, 0.0, false },
			{ TechId::WaterWhip, "Water Whip", "Draw water from a pond within 15 m; it circles you. Press to lash: it snaps out to the aim and flows back.",
				TechElem::Water, TechSlot::Primary, 8, 14, 16, 6.0, 4.0, false },
			{ TechId::WaterFreeze, "Freeze / Thaw", "Pull the heat out of the whip to turn it to ice, or put it back.",
				TechElem::Water, TechSlot::Secondary, 16, 3, 12, 4.0, 2.0, false },
			{ TechId::WaterRelease, "Release", "Let the water fall: it soaks into the ground and turns soil to mud.",
				TechElem::Water, TechSlot::Special, 4, 2, 8, 0.0, 0.0, false },
			{ TechId::WaterBlast, "Water Blast", "Throw the whip's water as one ball.",
				TechElem::Water, TechSlot::Utility, 10, 3, 16, 6.0, 6.0, false },
			{ TechId::RockThrow, "Rock Throw", "Pull a rock out of the ground (it leaves a crater) and hurl it.",
				TechElem::Earth, TechSlot::Primary, 20, 3, 18, 6.0, 8.0, false },
			{ TechId::EarthWall, "Earth Wall", "Raise a wall across your aim out of the trenches beside it.",
				TechElem::Earth, TechSlot::Secondary, 12, 18, 14, 6.0, 8.0, false },
			{ TechId::RaiseGround, "Raise Ground", "Hold: lift a pillar at the aim point; its soil comes from the ring around it.",
				TechElem::Earth, TechSlot::Special, 8, 6, 8, 2.0, 2.0, true },
			{ TechId::LowerGround, "Lower Ground", "Hold: dig a pit at the aim point; its soil piles up on the rim.",
				TechElem::Earth, TechSlot::Utility, 8, 6, 8, 2.0, 2.0, true },
			{ TechId::FireBlast, "Fire Blast", "A 0.6 kg, 1500 K blast of flame at 18 m/s.",
				TechElem::Fire, TechSlot::Primary, 8, 3, 14, 5.0, 5.0, false },
			{ TechId::FlameStream, "Flame Stream", "Hold: a stream of flame.",
				TechElem::Fire, TechSlot::Secondary, 6, 6, 10, 3.0, 3.0, true },
			{ TechId::GroundFlame, "Ground Flame", "Hold: keep a flame burning at the aim point (dries mud, melts ice).",
				TechElem::Fire, TechSlot::Utility, 10, 6, 10, 3.0, 2.0, true },
			{ TechId::AirBlast, "Air Blast", "41 kg of compressed air at 40 m/s: knocks dummies back, feeds fire, barely moves dense rock.",
				TechElem::Air, TechSlot::Primary, 10, 3, 16, 5.0, 5.0, false },
			{ TechId::AirGust, "Gust", "Hold: a steady stream of wind.",
				TechElem::Air, TechSlot::Secondary, 6, 6, 10, 3.0, 3.0, true },
			{ TechId::AirJump, "Air Jump", "Launch yourself upward on a column of air.",
				TechElem::Air, TechSlot::Utility, 4, 2, 12, 4.0, 6.0, false },
		};
		static_assert(sizeof(GTechniques) / sizeof(GTechniques[0]) == static_cast<int>(ETechnique::Count), "One entry per technique");

		const FTechniqueTuning GDefaultTuning;
	}

	const FTechniqueInfo& GetTechniqueInfo(ETechnique Technique)
	{
		const int Index = static_cast<int>(Technique);
		return GTechniques[Index < static_cast<int>(ETechnique::Count) ? Index : 0];
	}

	ETechnique FindTechnique(ETechniqueElement Element, ETechniqueSlot Slot)
	{
		for (const FTechniqueInfo& Info : GTechniques)
		{
			if (Info.Technique != ETechnique::None && Info.Element == Element && Info.Slot == Slot)
			{
				return Info.Technique;
			}
		}
		return ETechnique::None;
	}

	const FTechniqueTuning& GetDefaultTechniqueTuning()
	{
		return GDefaultTuning;
	}

	double KineticEnergyJ(double MassKg, double SpeedMs)
	{
		return 0.5 * MassKg * SpeedMs * SpeedMs;
	}

	FVolume MakeFlame(double MassKg, double TemperatureK, const FVec3& LocationCm, const FVec3& VelocityCmS)
	{
		FVolume Fire = FVolume::MakeDefault(ESubstance::Fire, MassKg);
		Fire.TemperatureK = TemperatureK;
		Fire.LocationCm = LocationCm;
		Fire.VelocityCmS = VelocityCmS;
		Fire.UpdateDerivedRadius();
		return Fire;
	}

	FVolume MakeBentAir(double RadiusCm, double Compression, double AmbientDensityKgM3, const FVec3& LocationCm, const FVec3& VelocityCmS)
	{
		FVolume Air = FVolume::MakeDefault(ESubstance::Air, 1.0);
		Air.bDeriveRadiusFromMass = false;
		Air.RadiusCm = RadiusCm;
		Air.MassKg = Compression * AmbientDensityKgM3 * SphereVolumeM3(CmToM(RadiusCm));
		Air.LocationCm = LocationCm;
		Air.VelocityCmS = VelocityCmS;
		return Air;
	}

	FVolume MakeWaterBall(double MassKg, double TemperatureK, const FVec3& LocationCm, const FVec3& VelocityCmS)
	{
		FVolume Water = FVolume::MakeDefault(ESubstance::Water, MassKg);
		Water.TemperatureK = TemperatureK;
		Water.LocationCm = LocationCm;
		Water.VelocityCmS = VelocityCmS;
		return Water;
	}

	FVolume MakeWaterSpray(double MassKg, double TemperatureK, const FVec3& LocationCm, const FVec3& VelocityCmS, double RadiusCm)
	{
		FVolume Spray = MakeWaterBall(MassKg, TemperatureK, LocationCm, VelocityCmS);
		Spray.bDeriveRadiusFromMass = false;
		Spray.RadiusCm = KMax(RadiusCm, Spray.RadiusCm);
		return Spray;
	}

	FVolume MakeRock(double MassKg, double DensityKgM3, const FVec3& LocationCm, const FVec3& VelocityCmS)
	{
		FVolume Rock = FVolume::MakeDefault(ESubstance::Earth, MassKg);
		Rock.Porosity = 0.25;
		Rock.RadiusCm = MToCm(SphereRadiusFromVolumeM(MassKg / KMax(DensityKgM3, 1.0)));
		Rock.LocationCm = LocationCm;
		Rock.VelocityCmS = VelocityCmS;
		return Rock;
	}

	FTerrainBrush RockQuarryBrush(const FVec3& GroundCm)
	{
		return FTerrainBrush::Disc(GroundCm, 55.0, 35.0);
	}

	void EarthWallBrushes(const FVec3& CenterCm, const FVec3& FacingUnit, const FTechniqueTuning& Tuning, FTerrainBrush& OutCore, FTerrainBrush& OutTrench)
	{
		FVec3 Facing(FacingUnit.X, FacingUnit.Y, 0.0);
		Facing = Facing.Size() > SmallNumber ? Facing.GetSafeNormal() : FVec3(1.0, 0.0, 0.0);
		const FVec3 Across = Facing.Cross(FVec3(0.0, 0.0, 1.0));
		const FVec3 Start = CenterCm - Across * (0.5 * Tuning.WallLengthCm);
		const FVec3 End = CenterCm + Across * (0.5 * Tuning.WallLengthCm);
		const double HalfThickness = 0.5 * Tuning.WallThicknessCm;
		OutCore = FTerrainBrush::Band(Start, End, 0.0, HalfThickness, 18.0);
		OutTrench = FTerrainBrush::Band(Start, End, HalfThickness + 45.0, HalfThickness + 125.0, 30.0);
	}

	void RaiseGroundBrushes(const FVec3& CenterCm, const FTechniqueTuning& Tuning, FTerrainBrush& OutTarget, FTerrainBrush& OutSource)
	{
		OutTarget = FTerrainBrush::Disc(CenterCm, Tuning.PillarRadiusCm, 50.0);
		OutSource = FTerrainBrush::Ring(CenterCm, Tuning.PillarRadiusCm + 90.0, Tuning.PillarRadiusCm + 190.0, 50.0);
	}

	void LowerGroundBrushes(const FVec3& CenterCm, const FTechniqueTuning& Tuning, FTerrainBrush& OutTarget, FTerrainBrush& OutSource)
	{
		OutSource = FTerrainBrush::Disc(CenterCm, Tuning.PitRadiusCm, 50.0);
		OutTarget = FTerrainBrush::Ring(CenterCm, Tuning.PitRadiusCm + 80.0, Tuning.PitRadiusCm + 180.0, 50.0);
	}
}
