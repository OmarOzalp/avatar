#pragma once

#include "Sim/BendingSimTypes.h"
#include "Sim/BendingTerrain.h"

/**
 * The bending techniques of the sandbox, defined once for every engine that runs it (Unreal, the browser build,
 * the tests): which element and input each uses, its frame data, its flat cost, and the physical recipe of the
 * matter it creates. Engines supply bodies, rendering and input; what a technique does to matter lives here.
 */
namespace BendingSim
{
	/** Values match EBendingElement in the Unreal plugin. */
	enum class ETechniqueElement : unsigned char
	{
		None,
		Earth,
		Water,
		Fire,
		Air
	};

	/** Shared inputs, switched by stance. Unreal maps them to Input.Bending.Light / Heavy / Special / Utility. */
	enum class ETechniqueSlot : unsigned char
	{
		/** Left mouse. */
		Primary,
		/** Right mouse. */
		Secondary,
		/** Q. */
		Special,
		/** E. */
		Utility,
		/** F: each element's signature move. */
		Signature,

		Count
	};

	enum class ETechnique : unsigned char
	{
		None,
		WaterWhip,
		WaterFreeze,
		WaterRelease,
		WaterBlast,
		RockThrow,
		EarthWall,
		RaiseGround,
		LowerGround,
		FireBlast,
		FlameStream,
		GroundFlame,
		AirBlast,
		AirGust,
		AirJump,
		FireJet,
		AirScooter,
		IceDaggers,
		Earthquake,
		FireRing,
		Tornado,

		Count
	};

	struct FTechniqueInfo
	{
		ETechnique Technique = ETechnique::None;
		const char* Name = "";
		const char* Description = "";
		ETechniqueElement Element = ETechniqueElement::None;
		ETechniqueSlot Slot = ETechniqueSlot::Primary;
		/** 60 Hz frame data. For held techniques Active is the minimum; it lasts while the input is held. */
		int StartupFrames = 10;
		int ActiveFrames = 3;
		int RecoveryFrames = 15;
		/** Flat chi and stamina at activation. Physical work is billed separately, in joules. */
		double ChiCost = 5.0;
		double StaminaCost = 5.0;
		bool bHold = false;
	};

	BENDINGSIM_API const FTechniqueInfo& GetTechniqueInfo(ETechnique Technique);
	/** The technique a stance maps an input to (None if the slot is empty in that stance). */
	BENDINGSIM_API ETechnique FindTechnique(ETechniqueElement Element, ETechniqueSlot Slot);

	/** Physical parameters of every technique (real units). */
	struct FTechniqueTuning
	{
		// ---------------------------------------------------------------- Chi exchange (gameplay rates)
		/** Mechanical work (J) one point of chi buys: accelerating and lifting matter. */
		double KineticJoulesPerChi = 5000.0;
		/** Heat (J) one point of chi buys. */
		double ThermalJoulesPerChi = 250000.0;

		// ---------------------------------------------------------------- Water
		double WhipWaterKg = 20.0;
		/** Farthest a water source may be from the bender's hand (cm). */
		double WhipDrawRangeCm = 1500.0;
		/** Spread of the droplet cloud a whip flings off at the snap (cm): spray has far more surface than a ball. */
		double WhipSprayRadiusCm = 30.0;
		/** Time for spray to lose most of its speed to air drag (s). */
		double WhipSprayDragTimeS = 0.25;
		double WaterBlastSpeedMs = 22.0;

		// ---------------------------------------------------------------- Earth
		/** Compacted earth of a thrown rock (kg/m^3); its mass is the soil pulled out of the ground. */
		double RockDensityKgM3 = 2400.0;
		double RockMassKg = 320.0;
		double RockLaunchSpeedMs = 20.0;
		/** Height above the ground the rock is lifted to during startup (cm). */
		double RockHoldHeightCm = 130.0;
		double WallLengthCm = 450.0;
		double WallHeightCm = 180.0;
		double WallThicknessCm = 50.0;
		/** Time for the wall to rise (s). */
		double WallRiseSeconds = 0.3;
		/** Soil moved per second while raising or lowering ground (m^3/s). */
		double TerraformRateM3S = 1.6;
		double PillarRadiusCm = 70.0;
		double PitRadiusCm = 90.0;
		double EarthbendRangeCm = 2500.0;

		// ---------------------------------------------------------------- Fire
		double FireBlastMassKg = 0.6;
		double FireBlastTemperatureK = 1500.0;
		double FireBlastSpeedMs = 18.0;
		double FlameStreamMassKg = 0.12;
		double FlameStreamTemperatureK = 1400.0;
		double FlameStreamSpeedMs = 15.0;
		double FlameStreamRateHz = 14.0;
		double GroundFlameMassKg = 0.8;
		double GroundFlameTemperatureK = 1300.0;
		/** Heat a bender pours into a ground flame while holding it (W). */
		double GroundFlamePowerW = 1.5e6;
		/** Once released, a ground flame burns the ground it lit: this much heat (J) at GroundFlameFuelPowerW (W). */
		double GroundFlameFuelJ = 1.6e6;
		double GroundFlameFuelPowerW = 3.5e5;

		// ---------------------------------------------------------------- Air
		double AirBlastRadiusCm = 100.0;
		/** Density of the bent air relative to ambient. */
		double AirBlastCompression = 8.0;
		double AirBlastSpeedMs = 40.0;
		double GustRadiusCm = 60.0;
		double GustCompression = 3.0;
		double GustSpeedMs = 28.0;
		double GustRateHz = 12.0;
		/** Bent air loses speed to the still air around it with this time constant (s); it dies after the lifetime. */
		double AirDecayTimeS = 0.6;
		double AirLifetimeS = 0.9;
		double AirJumpSpeedMs = 8.5;

		// ---------------------------------------------------------------- Signature and mobility moves
		/** Ice Daggers: water taken from the whip and frozen into this many shards. */
		int IceDaggerCount = 5;
		double IceDaggerMassKg = 0.5;
		double IceDaggerSpeedMs = 30.0;
		double IceDaggerSpreadDeg = 5.0;
		/** Earthquake: kinetic energy (J) the stomp gives a body at its feet (less farther out), the work of shaking
		 *  the ground itself, the fastest it throws anything, and how far it reaches. */
		double EarthquakeEnergyJ = 3000.0;
		double EarthquakeGroundJ = 20000.0;
		double EarthquakeMaxSpeedMs = 8.0;
		double EarthquakeRadiusCm = 850.0;
		/** Ring of Fire: flames thrown out in every direction. */
		int FireRingCount = 16;
		double FireRingMassKg = 0.2;
		double FireRingSpeedMs = 9.0;
		double FireRingTemperatureK = 1350.0;
		/** Jet Dash: fire jets from the feet push the bender this fast for this long. */
		double FireJetSpeedMs = 13.0;
		double FireJetLiftMs = 3.5;
		double FireJetSeconds = 0.35;
		double FireJetFlameMassKg = 0.1;
		/** Air Scooter: top speed on the ball of air; holding it costs the drag work times this (a spinning ball, not a sail). */
		double AirScooterSpeedMs = 14.0;
		double AirScooterUpkeepScale = 12.0;
		/** Tornado: a vortex at the aim point. */
		double TornadoRangeCm = 1400.0;
		double TornadoRadiusCm = 280.0;
		double TornadoSeconds = 4.0;
		/** Swirl speed it gives what it catches (m/s), inward pull and lift (m/s^2, lift shared out by mass). */
		double TornadoSwirlMs = 8.0;
		double TornadoPullMs2 = 16.0;
		double TornadoLiftMs2 = 24.0;
		double TornadoEnergyJ = 45000.0;

		// ---------------------------------------------------------------- Shared
		/** Bent fire and air live at most this long (s). */
		double ProjectileLifetimeS = 3.0;
	};

	BENDINGSIM_API const FTechniqueTuning& GetDefaultTechniqueTuning();

	// ---------------------------------------------------------------- Recipes

	BENDINGSIM_API double KineticEnergyJ(double MassKg, double SpeedMs);

	/** A bent flame: ideal-gas radius (it expands as it heats). */
	BENDINGSIM_API FVolume MakeFlame(double MassKg, double TemperatureK, const FVec3& LocationCm, const FVec3& VelocityCmS);
	/** Compressed air: density = Compression * ambient, filling a sphere of RadiusCm. */
	BENDINGSIM_API FVolume MakeBentAir(double RadiusCm, double Compression, double AmbientDensityKgM3, const FVec3& LocationCm, const FVec3& VelocityCmS);
	BENDINGSIM_API FVolume MakeWaterBall(double MassKg, double TemperatureK, const FVec3& LocationCm, const FVec3& VelocityCmS);
	/** An ice dagger: a shard of ice just below freezing. */
	BENDINGSIM_API FVolume MakeIceShard(double MassKg, const FVec3& LocationCm, const FVec3& VelocityCmS);
	/** Heat (J) to take out of MassKg of water at TemperatureK to make ice at IceTemperatureK. */
	BENDINGSIM_API double HeatToMakeIce(double MassKg, double TemperatureK, double IceTemperatureK);
	/** Spray: water spread into droplets over a cloud of RadiusCm, so it exchanges heat over far more surface than a ball. */
	BENDINGSIM_API FVolume MakeWaterSpray(double MassKg, double TemperatureK, const FVec3& LocationCm, const FVec3& VelocityCmS, double RadiusCm);
	/** A rock of compacted earth (porosity 0.25: it soaks a little, it can turn to mud). */
	BENDINGSIM_API FVolume MakeRock(double MassKg, double DensityKgM3, const FVec3& LocationCm, const FVec3& VelocityCmS);

	/** Ground a rock is pulled out of, centred under where it rises. */
	BENDINGSIM_API FTerrainBrush RockQuarryBrush(const FVec3& GroundCm);
	/** Wall core and the trenches its soil comes from. Facing is the direction the wall faces (it runs across it). */
	BENDINGSIM_API void EarthWallBrushes(const FVec3& CenterCm, const FVec3& FacingUnit, const FTechniqueTuning& Tuning,
		FTerrainBrush& OutCore, FTerrainBrush& OutTrench);
	/** Pillar at the centre, soil from the ring around it. */
	BENDINGSIM_API void RaiseGroundBrushes(const FVec3& CenterCm, const FTechniqueTuning& Tuning, FTerrainBrush& OutTarget, FTerrainBrush& OutSource);
	/** Pit at the centre, soil to a rim around it. */
	BENDINGSIM_API void LowerGroundBrushes(const FVec3& CenterCm, const FTechniqueTuning& Tuning, FTerrainBrush& OutTarget, FTerrainBrush& OutSource);
}
