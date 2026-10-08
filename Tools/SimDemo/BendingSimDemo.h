#pragma once

#include "Sim/BendingSimWorld.h"

/**
 * Headless stand-in for the game side of the bending simulation, shared by the native scenario tests and the
 * WebAssembly sandbox so both run exactly the same setups.
 *
 * In Unreal, owned volumes are moved by their actors (a rigid-body boulder, a PD-controlled water whip, a fire
 * projectile). Here FDemo plays those actors: held matter springs to its anchor, projectiles fly straight,
 * ballistic matter falls and lands, water that hits the ground soaks into it, and scripted benders pay for heat
 * they add or extract.
 */
namespace BendingSimDemo
{
	using namespace BendingSim;

	enum class EBehavior : unsigned char
	{
		/** Held in place by a bender (critically damped spring to its anchor). */
		Held,
		/** Bent projectile: straight line, no gravity; reaction impulses still act. */
		Projectile,
		/** Thrown or resting matter: gravity and ground contact. */
		Ballistic
	};

	struct FActor
	{
		FHandle Handle;
		EBehavior Behavior = EBehavior::Held;
		FVec3 AnchorCm;
		/** Heat a bender pours in every second (W); negative extracts. */
		double SustainPowerW = 0.0;
		/** Liquid that soaks into the ground on landing (a water blob splashing). */
		bool bSoaksIntoGround = false;
		bool bAlive = false;
		char Label[40] = {};
	};

	struct FScenarioInfo
	{
		const char* Name;
		const char* Summary;
		double DurationS;
	};

	inline constexpr int NumScenarios = 7;
	const FScenarioInfo& GetScenarioInfo(int ScenarioId);

	class FDemo
	{
	public:
		static constexpr int MaxActors = 128;

		FSimWorld World;
		FDefaultReactionParams Params;
		FActor Actors[MaxActors];
		int NumActors = 0;
		int ScenarioId = 0;
		double TimeS = 0.0;
		/** Ground plane at Z = 0 with this porosity (0 = rock). */
		double GroundPorosity = 0.35;
		/** Heat benders added or extracted on command (J): what chi pays for at ThermalJoulesPerChi. */
		double BenderThermalWorkJ = 0.0;
		/** Kinetic energy benders gave matter they launched (J): what chi pays for at KineticJoulesPerChi. */
		double BenderKineticWorkJ = 0.0;

		/** Fresh world with the built-in reactions and default settings. */
		void Init();
		void Load(int InScenarioId);
		void Advance(double FrameSeconds);

		int AddActor(const FVolume& Volume, EBehavior Behavior, const char* Label);
		/** AddActor for matter a bender launches: its kinetic energy is billed as bender work. */
		int LaunchActor(const FVolume& Volume, EBehavior Behavior, const char* Label);
		int FindActor(FHandle Handle) const;
		FActor* FindActorByLabel(const char* Label);
		/** Bender command: add (positive) or extract (negative) heat, and pay for it. */
		Thermo::FPhaseChangeResult BenderTransferHeat(FHandle Handle, double HeatJ);

		/** Sandbox presets: 0 fire blast, 1 water blob, 2 soil boulder, 3 granite boulder, 4 air jet, 5 held water wall, 6 held flame, 7 pebble. */
		int SpawnPreset(int Preset, const FVec3& LocationCm, const FVec3& VelocityCmS);
		static constexpr int NumPresets = 8;
		/** Waterbender: freeze the nearest liquid water within RadiusCm. Returns the heat extracted (J). */
		double BenderFreezeNearest(const FVec3& LocationCm, double RadiusCm);
		/** Firebender: pour HeatJ into the nearest volume within RadiusCm (melts ice, boils water). Returns heat accepted (J). */
		double BenderHeatNearest(const FVec3& LocationCm, double RadiusCm, double HeatJ);

	private:
		void RunScript(double FromS, double ToS);
		void MoveActors(double DeltaSeconds);
	};

	/** Volume helpers used by the scenarios. */
	FVolume MakeFire(double MassKg, double TemperatureK, const FVec3& LocationCm, const FVec3& VelocityCmS);
	FVolume MakeWater(double MassKg, const FVec3& LocationCm, const FVec3& VelocityCmS);
	FVolume MakeEarth(double MassKg, double DensityKgM3, double Porosity, const FVec3& LocationCm, const FVec3& VelocityCmS);
	/** Compressed air: density = CompressionRatio * ambient, filling a sphere of RadiusCm. */
	FVolume MakeAirWave(double RadiusCm, double CompressionRatio, const FVec3& LocationCm, const FVec3& VelocityCmS);
}
