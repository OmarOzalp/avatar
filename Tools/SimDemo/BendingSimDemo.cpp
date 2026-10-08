#include "BendingSimDemo.h"

namespace BendingSimDemo
{
	namespace
	{
		void CopyLabel(char* Destination, const char* Source)
		{
			int Index = 0;
			for (; Source && Source[Index] && Index < 39; ++Index)
			{
				Destination[Index] = Source[Index];
			}
			Destination[Index] = 0;
		}

		bool LabelsEqual(const char* A, const char* B)
		{
			int Index = 0;
			for (; A[Index] && B[Index]; ++Index)
			{
				if (A[Index] != B[Index])
				{
					return false;
				}
			}
			return A[Index] == B[Index];
		}

		bool Crossed(double FromS, double ToS, double AtS)
		{
			return FromS < AtS && AtS <= ToS;
		}

		const FScenarioInfo GScenarios[NumScenarios] = {
			{ "Fire vs water: evaporation",
				"A 0.6 kg, 1500 K fire blast hits a 40 kg water wall. The flame flash-boils the contact layer into steam, "
				"loses its heat, is dragged to a stop by the water, and goes out. The steam rises and condenses.", 6.0 },
			{ "Air feeds fire: oxygenation",
				"Two identical 0.4 kg flames. The left one burns alone and cools out in about a second. The right one "
				"is fed by an airbender's gusts: entrained air burns at 3 MJ/kg, so it grows, stays hot, and is pushed downwind.", 4.0 },
			{ "Water on earth: mud",
				"Two 30 kg water blobs are thrown at two 100 kg rocks: porous soil (0.42) and granite (0.01). The soil "
				"soaks the water up and turns to mud; the granite is only shoved. Spilled water soaks the ground and lowers traction.", 4.0 },
			{ "Air jet vs stone: deflection",
				"Two stones fly at the airbender at 15 m/s: a 0.3 kg pebble (low) and a 1400 kg boulder (high). Real air is light, "
				"so the airbender answers with a sustained jet compressed 4x at 60 m/s. Drag is the same per area; dv = J/m: the "
				"pebble is turned around, the boulder only slows.", 3.0 },
			{ "Freeze the whip, then fire",
				"A waterbender holds a 12 kg water whip, then extracts the 4.8 MJ it takes to freeze it solid. A 0.6 kg "
				"fire blast later hits the ice: far too little heat to melt 12 kg, so the ice banks latent heat and the flame dies.", 5.0 },
			{ "Firebender dries mud",
				"18 kg of water soaks the ground (traction falls to ~40%). A firebender channels 7 MW into a flame jet "
				"aimed at the mud: the water boils off as steam and traction recovers. Drying mud is expensive, about 2.6 MJ per kg.", 9.0 },
			{ "Sandbox",
				"Empty arena over soil. Spawn fire, water, earth and air and watch them interact.", 1e9 },
		};
	}

	const FScenarioInfo& GetScenarioInfo(int ScenarioId)
	{
		return GScenarios[KClamp(ScenarioId, 0, NumScenarios - 1)];
	}

	// ---------------------------------------------------------------------------------------------------- Volume helpers

	FVolume MakeFire(double MassKg, double TemperatureK, const FVec3& LocationCm, const FVec3& VelocityCmS)
	{
		FVolume Fire = FVolume::MakeDefault(ESubstance::Fire, MassKg);
		Fire.TemperatureK = TemperatureK;
		Fire.LocationCm = LocationCm;
		Fire.VelocityCmS = VelocityCmS;
		Fire.UpdateDerivedRadius();
		return Fire;
	}

	FVolume MakeWater(double MassKg, const FVec3& LocationCm, const FVec3& VelocityCmS)
	{
		FVolume Water = FVolume::MakeDefault(ESubstance::Water, MassKg);
		Water.LocationCm = LocationCm;
		Water.VelocityCmS = VelocityCmS;
		return Water;
	}

	FVolume MakeEarth(double MassKg, double DensityKgM3, double Porosity, const FVec3& LocationCm, const FVec3& VelocityCmS)
	{
		FVolume Earth = FVolume::MakeDefault(ESubstance::Earth, MassKg);
		Earth.Porosity = Porosity;
		Earth.RadiusCm = MToCm(SphereRadiusFromVolumeM(MassKg / DensityKgM3));
		Earth.LocationCm = LocationCm;
		Earth.VelocityCmS = VelocityCmS;
		return Earth;
	}

	FVolume MakeAirWave(double RadiusCm, double CompressionRatio, const FVec3& LocationCm, const FVec3& VelocityCmS)
	{
		FVolume Air = FVolume::MakeDefault(ESubstance::Air, 1.0);
		Air.bDeriveRadiusFromMass = false;
		Air.RadiusCm = RadiusCm;
		Air.MassKg = CompressionRatio * AirDensitySeaLevel * SphereVolumeM3(CmToM(RadiusCm));
		Air.LocationCm = LocationCm;
		Air.VelocityCmS = VelocityCmS;
		return Air;
	}

	// ---------------------------------------------------------------------------------------------------- Demo

	void FDemo::Init()
	{
		World.Reset();
		World.ClearReactions();
		World.Settings = FSimSettings();
		Params = FDefaultReactionParams();
		AddBuiltInReactions(World, Params);
		for (FActor& Actor : Actors)
		{
			Actor = FActor();
		}
		NumActors = 0;
		TimeS = 0.0;
		GroundPorosity = 0.35;
		BenderThermalWorkJ = 0.0;
		BenderKineticWorkJ = 0.0;
	}

	int FDemo::AddActor(const FVolume& Volume, EBehavior Behavior, const char* Label)
	{
		int Slot = -1;
		for (int Index = 0; Index < NumActors; ++Index)
		{
			if (!Actors[Index].bAlive)
			{
				Slot = Index;
				break;
			}
		}
		if (Slot < 0)
		{
			if (NumActors >= MaxActors)
			{
				return -1;
			}
			Slot = NumActors++;
		}
		const FHandle Handle = World.AddVolume(Volume, true);
		if (!Handle.IsSet())
		{
			return -1;
		}
		FActor& Actor = Actors[Slot];
		Actor = FActor();
		Actor.Handle = Handle;
		Actor.Behavior = Behavior;
		Actor.AnchorCm = Volume.LocationCm;
		Actor.bSoaksIntoGround = Volume.Substance == ESubstance::Water && Behavior == EBehavior::Ballistic;
		Actor.bAlive = true;
		CopyLabel(Actor.Label, Label);
		return Slot;
	}

	int FDemo::FindActor(FHandle Handle) const
	{
		for (int Index = 0; Index < NumActors; ++Index)
		{
			if (Actors[Index].bAlive && Actors[Index].Handle == Handle)
			{
				return Index;
			}
		}
		return -1;
	}

	FActor* FDemo::FindActorByLabel(const char* Label)
	{
		for (int Index = 0; Index < NumActors; ++Index)
		{
			if (Actors[Index].bAlive && LabelsEqual(Actors[Index].Label, Label))
			{
				return &Actors[Index];
			}
		}
		return nullptr;
	}

	int FDemo::LaunchActor(const FVolume& Volume, EBehavior Behavior, const char* Label)
	{
		const int Index = AddActor(Volume, Behavior, Label);
		if (Index >= 0)
		{
			BenderKineticWorkJ += Volume.GetKineticEnergyJ();
		}
		return Index;
	}

	Thermo::FPhaseChangeResult FDemo::BenderTransferHeat(FHandle Handle, double HeatJ)
	{
		double AcceptedHeatJ = 0.0;
		const Thermo::FPhaseChangeResult Change = World.TransferHeat(Handle, HeatJ, &AcceptedHeatJ);
		BenderThermalWorkJ += KAbs(AcceptedHeatJ);
		return Change;
	}

	int FDemo::SpawnPreset(int Preset, const FVec3& LocationCm, const FVec3& VelocityCmS)
	{
		switch (Preset)
		{
		case 0: return LaunchActor(MakeFire(0.6, 1500.0, LocationCm, VelocityCmS), EBehavior::Projectile, "Fire blast");
		case 1: return LaunchActor(MakeWater(20.0, LocationCm, VelocityCmS), EBehavior::Ballistic, "Water blob");
		case 2: return LaunchActor(MakeEarth(120.0, 1600.0, 0.42, LocationCm, VelocityCmS), EBehavior::Ballistic, "Soil boulder");
		case 3: return LaunchActor(MakeEarth(300.0, 2650.0, 0.01, LocationCm, VelocityCmS), EBehavior::Ballistic, "Granite boulder");
		case 4:
		{
			FVolume Jet = MakeAirWave(60.0, 4.0, LocationCm, VelocityCmS);
			const double Speed = VelocityCmS.Size();
			if (Speed > SmallNumber)
			{
				Jet.Shape = EShape::Capsule;
				Jet.CapsuleHalfAxisCm = VelocityCmS * (80.0 / Speed);
				Jet.MassKg = 4.0 * AirDensitySeaLevel * Jet.GetGeometricVolumeM3();
			}
			return LaunchActor(Jet, EBehavior::Projectile, "Air jet");
		}
		case 5: return AddActor(MakeWater(40.0, LocationCm, FVec3()), EBehavior::Held, "Water wall");
		case 6:
		{
			const int Index = AddActor(MakeFire(0.5, 1400.0, LocationCm, FVec3()), EBehavior::Held, "Held flame");
			if (Index >= 0)
			{
				Actors[Index].SustainPowerW = 1.5e6;
			}
			return Index;
		}
		case 7: return LaunchActor(MakeEarth(0.3, 2650.0, 0.01, LocationCm, VelocityCmS), EBehavior::Projectile, "Pebble");
		default: return -1;
		}
	}

	double FDemo::BenderFreezeNearest(const FVec3& LocationCm, double RadiusCm)
	{
		FHandle Found[32];
		const int Count = World.QueryVolumes(LocationCm, RadiusCm, SubstanceMask(ESubstance::Water), Found, 32);
		double BestDistanceSq = 1e300;
		FHandle Best;
		for (int Index = 0; Index < Count; ++Index)
		{
			const double DistanceSq = DistanceSquared(World.GetVolume(Found[Index])->LocationCm, LocationCm);
			if (DistanceSq < BestDistanceSq)
			{
				BestDistanceSq = DistanceSq;
				Best = Found[Index];
			}
		}
		const FVolume* Water = World.GetVolume(Best);
		if (!Water)
		{
			return 0.0;
		}
		const double HeatJ = HeatToFreeze(*Water);
		BenderTransferHeat(Best, -HeatJ);
		return HeatJ;
	}

	double FDemo::BenderHeatNearest(const FVec3& LocationCm, double RadiusCm, double HeatJ)
	{
		FHandle Found[32];
		const int Count = World.QueryVolumes(LocationCm, RadiusCm, AllSubstancesMask & ~SubstanceMask(ESubstance::Air), Found, 32);
		double BestDistanceSq = 1e300;
		FHandle Best;
		for (int Index = 0; Index < Count; ++Index)
		{
			const double DistanceSq = DistanceSquared(World.GetVolume(Found[Index])->LocationCm, LocationCm);
			if (DistanceSq < BestDistanceSq)
			{
				BestDistanceSq = DistanceSq;
				Best = Found[Index];
			}
		}
		if (!Best.IsSet())
		{
			return 0.0;
		}
		const double Before = BenderThermalWorkJ;
		BenderTransferHeat(Best, HeatJ);
		return BenderThermalWorkJ - Before;
	}

	void FDemo::Load(int InScenarioId)
	{
		Init();
		ScenarioId = KClamp(InScenarioId, 0, NumScenarios - 1);

		switch (ScenarioId)
		{
		case 0: // Evaporation
			AddActor(MakeWater(40.0, FVec3(250.0, 0.0, 150.0), FVec3()), EBehavior::Held, "Water wall");
			LaunchActor(MakeFire(0.6, 1500.0, FVec3(-500.0, 0.0, 150.0), FVec3(1800.0, 0.0, 0.0)), EBehavior::Projectile, "Fire blast");
			break;

		case 1: // Oxygenation
			AddActor(MakeFire(0.4, 1300.0, FVec3(-450.0, 0.0, 150.0), FVec3()), EBehavior::Held, "Fire alone");
			AddActor(MakeFire(0.4, 1300.0, FVec3(350.0, 0.0, 150.0), FVec3()), EBehavior::Held, "Fire fed by air");
			break;

		case 2: // Mud
		{
			const FVolume Soil = MakeEarth(100.0, 1600.0, 0.42, FVec3(), FVec3());
			const FVolume Granite = MakeEarth(100.0, 2650.0, 0.01, FVec3(), FVec3());
			AddActor(MakeEarth(100.0, 1600.0, 0.42, FVec3(-250.0, 0.0, Soil.RadiusCm), FVec3()), EBehavior::Ballistic, "Soil clod");
			AddActor(MakeEarth(100.0, 2650.0, 0.01, FVec3(350.0, 0.0, Granite.RadiusCm), FVec3()), EBehavior::Ballistic, "Granite");
			LaunchActor(MakeWater(30.0, FVec3(-640.0, 0.0, 60.0), FVec3(900.0, 0.0, 150.0)), EBehavior::Ballistic, "Water to soil");
			LaunchActor(MakeWater(30.0, FVec3(-50.0, 0.0, 60.0), FVec3(900.0, 0.0, 150.0)), EBehavior::Ballistic, "Water to granite");
			break;
		}

		case 3: // Deflection
			LaunchActor(MakeEarth(0.3, 2650.0, 0.01, FVec3(900.0, 0.0, 150.0), FVec3(-1500.0, 0.0, 0.0)), EBehavior::Projectile, "Pebble");
			LaunchActor(MakeEarth(1400.0, 2650.0, 0.01, FVec3(900.0, 0.0, 500.0), FVec3(-1500.0, 0.0, 0.0)), EBehavior::Projectile, "Boulder");
			break;

		case 4: // Freeze
		{
			FVolume Whip = MakeWater(12.0, FVec3(0.0, 0.0, 200.0), FVec3());
			Whip.Shape = EShape::Capsule;
			Whip.CapsuleHalfAxisCm = FVec3(130.0, 0.0, 0.0);
			Whip.RadiusCm = 4.0;
			AddActor(Whip, EBehavior::Held, "Water whip");
			break;
		}

		case 5: // Drying
			GroundPorosity = 0.35;
			World.Settings.FireGroundHeatTransferCoefficient = 5000.0; // A directed flame jet, not a resting fire.
			World.DepositWaterOnSurface(FVec3(0.0, 0.0, 0.0), 18.0, GroundPorosity);
			{
				const int Index = AddActor(MakeFire(0.5, 1400.0, FVec3(0.0, 0.0, 45.0), FVec3()), EBehavior::Held, "Flame jet");
				if (Index >= 0)
				{
					Actors[Index].SustainPowerW = 7.0e6;
				}
			}
			break;

		default: // Sandbox
			break;
		}
	}

	void FDemo::RunScript(double FromS, double ToS)
	{
		switch (ScenarioId)
		{
		case 1: // An airbender's gust: six puffs of mildly compressed air.
			for (int Puff = 0; Puff < 6; ++Puff)
			{
				if (Crossed(FromS, ToS, 0.2 + 0.25 * Puff))
				{
					LaunchActor(MakeAirWave(70.0, 1.2, FVec3(-60.0, 0.0, 150.0), FVec3(1600.0, 0.0, 0.0)), EBehavior::Projectile, "Air gust");
				}
			}
			break;

		case 2: // A second splash on the soil pushes it past the mud threshold.
			if (Crossed(FromS, ToS, 0.6))
			{
				LaunchActor(MakeWater(30.0, FVec3(-640.0, 0.0, 60.0), FVec3(900.0, 0.0, 150.0)), EBehavior::Ballistic, "Water to soil");
			}
			break;

		case 3: // The airbender answers both stones with identical sustained jets of air compressed 4x at 60 m/s.
			for (int Puff = 0; Puff < 15; ++Puff)
			{
				if (Crossed(FromS, ToS, 0.1 + 0.04 * Puff))
				{
					const double Lanes[2] = { 150.0, 500.0 };
					for (const double LaneZ : Lanes)
					{
						FVolume Jet = MakeAirWave(50.0, 4.0, FVec3(-500.0, 0.0, LaneZ), FVec3(6000.0, 0.0, 0.0));
						Jet.Shape = EShape::Capsule;
						Jet.CapsuleHalfAxisCm = FVec3(60.0, 0.0, 0.0);
						Jet.MassKg = 4.0 * AirDensitySeaLevel * Jet.GetGeometricVolumeM3();
						LaunchActor(Jet, EBehavior::Projectile, LaneZ < 300.0 ? "Air jet (low)" : "Air jet (high)");
					}
				}
			}
			break;

		case 4:
			if (Crossed(FromS, ToS, 1.0))
			{
				if (FActor* Whip = FindActorByLabel("Water whip"))
				{
					if (const FVolume* Volume = World.GetVolume(Whip->Handle))
					{
						BenderTransferHeat(Whip->Handle, -HeatToFreeze(*Volume));
					}
				}
			}
			if (Crossed(FromS, ToS, 2.0))
			{
				LaunchActor(MakeFire(0.6, 1500.0, FVec3(-700.0, 0.0, 200.0), FVec3(1800.0, 0.0, 0.0)), EBehavior::Projectile, "Fire blast");
			}
			break;

		default:
			break;
		}
	}

	void FDemo::Advance(double FrameSeconds)
	{
		if (FrameSeconds <= 0.0)
		{
			return;
		}
		const double FromS = TimeS;
		World.Advance(FrameSeconds);
		TimeS += FrameSeconds;
		MoveActors(FrameSeconds);
		RunScript(FromS, TimeS);
	}

	void FDemo::MoveActors(double DeltaSeconds)
	{
		const double GravityZ = World.Settings.GravityZCmS2;
		for (int Index = 0; Index < NumActors; ++Index)
		{
			FActor& Actor = Actors[Index];
			if (!Actor.bAlive)
			{
				continue;
			}
			FVolume* Volume = World.GetVolume(Actor.Handle);
			if (!Volume)
			{
				Actor.bAlive = false;
				continue;
			}

			// What a game actor does with its owner update: the world already applied the impulse to the velocity.
			FOwnerUpdate Update;
			World.ConsumeOwnerUpdate(Actor.Handle, Update);
			if (World.IsDepleted(Actor.Handle))
			{
				World.RemoveVolume(Actor.Handle);
				Actor.bAlive = false;
				continue;
			}
			if (Actor.SustainPowerW != 0.0)
			{
				BenderTransferHeat(Actor.Handle, Actor.SustainPowerW * DeltaSeconds);
			}

			FVec3& Location = Volume->LocationCm;
			FVec3& Velocity = Volume->VelocityCmS;
			switch (Actor.Behavior)
			{
			case EBehavior::Held:
			{
				// Critically damped spring: the bender holds the matter but it still gives under impacts.
				constexpr double Stiffness = 80.0;
				const double Damping = 2.0 * KSqrt(Stiffness);
				Velocity += ((Actor.AnchorCm - Location) * Stiffness - Velocity * Damping) * DeltaSeconds;
				Location += Velocity * DeltaSeconds;
				break;
			}
			case EBehavior::Projectile:
				Location += Velocity * DeltaSeconds;
				break;
			case EBehavior::Ballistic:
				if (!Volume->IsGas())
				{
					Velocity.Z += GravityZ * DeltaSeconds;
				}
				Location += Velocity * DeltaSeconds;
				break;
			}

			if (Actor.Behavior == EBehavior::Ballistic && !Volume->IsGas() && Location.Z - Volume->RadiusCm < 0.0)
			{
				if (Actor.bSoaksIntoGround)
				{
					World.DepositWaterOnSurface(FVec3(Location.X, Location.Y, 0.0), Volume->MassKg, GroundPorosity);
					World.RemoveVolume(Actor.Handle);
					Actor.bAlive = false;
					continue;
				}
				Location.Z = Volume->RadiusCm;
				if (Velocity.Z < 0.0)
				{
					Velocity.Z *= -0.2;
				}
				const double Friction = KExp(-6.0 * DeltaSeconds);
				Velocity.X *= Friction;
				Velocity.Y *= Friction;
			}

			if (KAbs(Location.X) > 3000.0 || Location.Z > 3000.0 || Location.Z < -500.0)
			{
				World.RemoveVolume(Actor.Handle);
				Actor.bAlive = false;
			}
		}
	}
}
