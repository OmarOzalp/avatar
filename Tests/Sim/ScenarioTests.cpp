// Behavioral tests of the bending simulation: conservation laws on isolated reactions, then every demo scenario
// run end to end with physical assertions. Also writes a digest of each scenario's end state so the
// WebAssembly build can be checked for identical results (Tests/Sim/wasm_parity.mjs).

#include "BendingSimDemo.h"
#include "SimTestHarness.h"

#include <chrono>
#include <cstdio>
#include <cstring>

using namespace BendingSimDemo;

namespace
{
	FDemo GDemo;
	FReactionEvent GEvents[512];

	constexpr double FrameSeconds = 1.0 / 60.0;

	struct FRunLog
	{
		double TotalMass[static_cast<int>(EReactionType::Count)] = {};
		double TotalEnergy[static_cast<int>(EReactionType::Count)] = {};
		double FirstTime[static_cast<int>(EReactionType::Count)] = {};
		int Count[static_cast<int>(EReactionType::Count)] = {};
		FReactionEvent Last[static_cast<int>(EReactionType::Count)];

		void Record(const FReactionEvent& Event, double TimeS)
		{
			const int Type = static_cast<int>(Event.Type);
			if (Count[Type] == 0)
			{
				FirstTime[Type] = TimeS;
			}
			++Count[Type];
			TotalMass[Type] += Event.MassKg;
			TotalEnergy[Type] += Event.EnergyJ;
			Last[Type] = Event;
		}

		double Mass(EReactionType Type) const { return TotalMass[static_cast<int>(Type)]; }
		int Num(EReactionType Type) const { return Count[static_cast<int>(Type)]; }
		double First(EReactionType Type) const { return FirstTime[static_cast<int>(Type)]; }
	};

	/** Advances the demo one frame and logs its events. */
	void Frame(FRunLog& Log)
	{
		GDemo.Advance(FrameSeconds);
		const int Count = GDemo.World.FlushEvents(FrameSeconds, GEvents, 512);
		for (int Index = 0; Index < Count; ++Index)
		{
			Log.Record(GEvents[Index], GDemo.TimeS);
		}
	}

	const FVolume* ActorVolume(const char* Label)
	{
		FActor* Actor = GDemo.FindActorByLabel(Label);
		return Actor ? GDemo.World.GetVolume(Actor->Handle) : nullptr;
	}

	double FreeGasMass(ESubstance Substance, double* OutMeanVerticalSpeed = nullptr)
	{
		double Mass = 0.0;
		double Momentum = 0.0;
		for (int Index = 0; Index < GDemo.World.GetSlotLimit(); ++Index)
		{
			const FHandle Handle = GDemo.World.GetHandleAt(Index);
			const FVolume* Volume = GDemo.World.GetVolume(Handle);
			if (Volume && !GDemo.World.IsOwned(Handle) && Volume->Substance == Substance)
			{
				Mass += Volume->MassKg;
				Momentum += Volume->MassKg * Volume->VelocityCmS.Z;
			}
		}
		if (OutMeanVerticalSpeed)
		{
			*OutMeanVerticalSpeed = Mass > 0.0 ? Momentum / Mass / 100.0 : 0.0;
		}
		return Mass;
	}

	// ------------------------------------------------------------------------------------------------ Isolated reactions

	FSimWorld GWorld;
	FDefaultReactionParams GParams;

	void ResetIsolatedWorld()
	{
		GWorld.Reset();
		GWorld.ClearReactions();
		GWorld.Settings = FSimSettings();
		AddBuiltInReactions(GWorld, GParams);
	}

	FVec3 TotalMomentum()
	{
		FVec3 Momentum;
		for (int Index = 0; Index < GWorld.GetSlotLimit(); ++Index)
		{
			if (const FVolume* Volume = GWorld.GetVolume(GWorld.GetHandleAt(Index)))
			{
				Momentum += Volume->GetMomentumKgCmS();
			}
		}
		return Momentum;
	}

	double FreeGasMassIn(const FSimWorld& World)
	{
		double Mass = 0.0;
		for (int Index = 0; Index < World.GetSlotLimit(); ++Index)
		{
			const FHandle Handle = World.GetHandleAt(Index);
			if (const FVolume* Volume = World.GetVolume(Handle); Volume && !World.IsOwned(Handle))
			{
				Mass += Volume->MassKg;
			}
		}
		return Mass;
	}

	void AeroDragConservesMomentumAndRespectsMass()
	{
		SimTest::Section("Air drag: momentum conservation and dv = J/m");
		ResetIsolatedWorld();
		FVolume Air = MakeAirWave(100.0, 3.0, FVec3(0, 0, 0), FVec3(4000, 0, 0));
		FVolume Pebble = MakeEarth(0.3, 2650.0, 0.01, FVec3(30, 0, 0), FVec3(-1500, 0, 0));
		const FHandle AirHandle = GWorld.AddVolume(Air, true);
		const FHandle PebbleHandle = GWorld.AddVolume(Pebble, true);
		const FVec3 Before = TotalMomentum();
		GWorld.Step(1.0 / 60.0);
		const FVec3 After = TotalMomentum();
		ExpectNear("total momentum after one step (kg*cm/s) equals before", After.X, Before.X, 1e-9 * KAbs(Before.X));

		const double PebbleDv = GWorld.GetVolume(PebbleHandle)->VelocityCmS.X + 1500.0;
		ExpectTrue("pebble pushed along the wind", PebbleDv > 0.0);
		const double RelativeBefore = 4000.0 + 1500.0;
		const double RelativeAfter = GWorld.GetVolume(AirHandle)->VelocityCmS.X - GWorld.GetVolume(PebbleHandle)->VelocityCmS.X;
		ExpectTrue("drag never overshoots: relative wind keeps its sign", RelativeAfter > 0.0 && RelativeAfter < RelativeBefore);

		// Same air, a boulder instead: identical drag per area, tiny dv because of mass.
		ResetIsolatedWorld();
		const FHandle Air2 = GWorld.AddVolume(Air, true);
		const FHandle BoulderHandle = GWorld.AddVolume(MakeEarth(1400.0, 2650.0, 0.01, FVec3(30, 0, 0), FVec3(-1500, 0, 0)), true);
		GWorld.Step(1.0 / 60.0);
		const double BoulderDv = GWorld.GetVolume(BoulderHandle)->VelocityCmS.X + 1500.0;
		(void)Air2;
		// Drag force scales with frontal area, so dv = F*dt/m scales with area/mass (the ballistic coefficient).
		const FVolume PebbleShape = MakeEarth(0.3, 2650.0, 0.01, FVec3(), FVec3());
		const FVolume BoulderShape = MakeEarth(1400.0, 2650.0, 0.01, FVec3(), FVec3());
		const double ExpectedRatio = (PebbleShape.GetFrontalAreaM2() / PebbleShape.MassKg) / (BoulderShape.GetFrontalAreaM2() / BoulderShape.MassKg);
		std::printf("    pebble dv = %.3f m/s, boulder dv = %.4f m/s in one 1/60 s step (ratio %.1f, area/mass ratio %.1f)\n",
			PebbleDv / 100.0, BoulderDv / 100.0, PebbleDv / BoulderDv, ExpectedRatio);
		ExpectNear("dv ratio matches the area-to-mass ratio", PebbleDv / BoulderDv, ExpectedRatio, 0.05 * ExpectedRatio);
	}

	void HeatExchangeConservesEnergyAndMass()
	{
		SimTest::Section("Fire + water: energy, mass and momentum conservation in one step");
		ResetIsolatedWorld();
		GWorld.Settings.AmbientTemperatureK = 0.0; // isolate the pair: no exchange with the surroundings
		FVolume Fire = MakeFire(0.6, 1500.0, FVec3(0, 0, 0), FVec3(1800, 0, 0));
		FVolume Water = MakeWater(40.0, FVec3(70, 0, 0), FVec3(0, 0, 0));
		const FHandle FireHandle = GWorld.AddVolume(Fire, true);
		const FHandle WaterHandle = GWorld.AddVolume(Water, true);

		// Run the reaction alone (no ambient step) through the public reaction function.
		FVolume& F = *GWorld.GetVolume(FireHandle);
		FVolume& W = *GWorld.GetVolume(WaterHandle);
		FContact Contact;
		ExpectTrue("fire and water overlap", ComputeContact(F, W, Contact));
		const double FireHeatBefore = F.GetHeatCapacityJPerK() * F.TemperatureK;
		const double WaterMassBefore = W.MassKg;
		const double WaterTBefore = W.TemperatureK;
		const FVec3 MomentumBefore = F.GetMomentumKgCmS() + W.GetMomentumKgCmS();
		FReactionContext Context{ GWorld, F, W, FireHandle, WaterHandle, Contact, 1.0 / 60.0, GWorld.Settings };
		ReactHeatExchange(GParams.HeatExchange, Context);

		const double FireHeatLost = FireHeatBefore - F.GetHeatCapacityJPerK() * F.TemperatureK;
		const double VaporizedKg = WaterMassBefore - W.MassKg;
		using namespace Thermo::Constants;
		const double WaterHeatGained = VaporizedKg * (SpecificHeatWater * (WaterBoilingPointK - WaterTBefore) + LatentHeatVaporization)
			+ W.MassKg * SpecificHeatWater * (W.TemperatureK - WaterTBefore);
		std::printf("    heat out of flame %.1f kJ, into water %.1f kJ, steam %.2f g\n", FireHeatLost / 1e3, WaterHeatGained / 1e3, VaporizedKg * 1e3);
		ExpectTrue("some water boiled off", VaporizedKg > 0.0);
		ExpectNear("heat lost by flame == heat gained by water (J)", WaterHeatGained, FireHeatLost, 1e-6 * FireHeatLost);

		// Momentum: the coupling impulses are equal and opposite; steam carries the momentum of the water it came from.
		const FVec3 PendingSum = F.PendingImpulseKgCmS + W.PendingImpulseKgCmS;
		ExpectNear("coupling impulses cancel (kg*cm/s)", PendingSum.X, 0.0, 1e-9 * MomentumBefore.X);
		// Spawn the queued steam without letting the pair react again.
		GWorld.RemoveVolume(FireHandle);
		GWorld.Settings.AmbientTemperatureK = 288.15;
		GWorld.Step(1.0 / 60.0);
		ExpectNear("steam volume holds exactly the boiled mass (kg)", FreeGasMassIn(GWorld), VaporizedKg, 1e-6);
	}

	void IceBanksLatentHeat()
	{
		SimTest::Section("Fire + ice: latent heat banks until the whole block melts");
		ResetIsolatedWorld();
		FVolume Ice = MakeWater(5.0, FVec3(0, 0, 0), FVec3());
		Ice.Substance = ESubstance::Ice;
		Ice.TemperatureK = 273.15;
		const FHandle IceHandle = GWorld.AddVolume(Ice, true);
		const double NeededJ = HeatToMelt(*GWorld.GetVolume(IceHandle));
		GWorld.TransferHeat(IceHandle, 0.5 * NeededJ);
		ExpectTrue("half the melting heat leaves it ice", GWorld.GetVolume(IceHandle)->Substance == ESubstance::Ice);
		ExpectNear("temperature pinned at 0 C", GWorld.GetVolume(IceHandle)->TemperatureK, 273.15, 1e-9);
		GWorld.TransferHeat(IceHandle, 0.5 * NeededJ + 1.0);
		ExpectTrue("the rest melts it", GWorld.GetVolume(IceHandle)->Substance == ESubstance::Water);
		FReactionEvent Events[8];
		const int Count = GWorld.FlushEvents(1.0 / 60.0, Events, 8);
		ExpectTrue("a Melting event is reported", Count == 1 && Events[0].Type == EReactionType::Melting);
	}

	void FlameCannotExceedAdiabaticLimit()
	{
		SimTest::Section("Firebending is capped at the adiabatic flame temperature");
		ResetIsolatedWorld();
		const FHandle Fire = GWorld.AddVolume(MakeFire(0.5, 1400.0, FVec3(), FVec3()), true);
		double Accepted = 0.0;
		GWorld.TransferHeat(Fire, 1e9, &Accepted);
		ExpectNear("temperature clamps at MaxFlameTemperatureK", GWorld.GetVolume(Fire)->TemperatureK, GWorld.Settings.MaxFlameTemperatureK, 1e-6);
		ExpectNear("only the absorbable heat is accepted (J)", Accepted, 0.5 * 1200.0 * (2300.0 - 1400.0), 1e-3);
	}

	// ------------------------------------------------------------------------------------------------ Scenarios

	void EvaporationScenario()
	{
		SimTest::Section("Scenario: fire blast vs water wall (evaporation)");
		GDemo.Load(0);
		FRunLog Log;
		const double FireSpeedAtImpact = ActorVolume("Fire blast")->VelocityCmS.Size() / 100.0;
		double LastFireSpeed = 0.0;
		double SteamRiseSpeed = 0.0;
		while (GDemo.TimeS < 6.0)
		{
			Frame(Log);
			if (const FVolume* Fire = ActorVolume("Fire blast"))
			{
				LastFireSpeed = Fire->VelocityCmS.Size() / 100.0;
			}
			if (GDemo.TimeS > 1.5 && GDemo.TimeS < 1.52)
			{
				FreeGasMass(ESubstance::Steam, &SteamRiseSpeed);
			}
		}
		const FVolume* Water = ActorVolume("Water wall");
		const double Evaporated = Log.Mass(EReactionType::Evaporation);
		std::printf("    steam produced %.1f g, fire speed %.1f -> %.1f m/s before going out at t=%.2f s, steam rising at %.2f m/s\n",
			Evaporated * 1e3, FireSpeedAtImpact, LastFireSpeed, Log.First(EReactionType::Extinguished), SteamRiseSpeed);
		ExpectTrue("the fire blast is extinguished", Log.Num(EReactionType::Extinguished) == 1);
		ExpectTrue("steam was produced", Evaporated > 0.02);
		ExpectNear("water lost exactly the mass that boiled (kg)", 40.0 - Water->MassKg, Evaporated, 1e-9);
		ExpectTrue("the water slowed the flame before it died", LastFireSpeed < 0.8 * FireSpeedAtImpact);
		ExpectTrue("steam rises (buoyancy)", SteamRiseSpeed > 0.1);
		const double SteamLeft = FreeGasMass(ESubstance::Steam);
		ExpectNear("steam inventory: produced - condensed - airborne ~ 0 (kg)", Evaporated - Log.Mass(EReactionType::Condensation) - SteamLeft, 0.0,
			GDemo.World.Settings.MinVolumeMassKg * 4.0);
	}

	void OxygenationScenario()
	{
		SimTest::Section("Scenario: air feeds fire (oxygenation)");
		GDemo.Load(1);
		FRunLog Log;
		double ControlT = 0.0;
		double FedT = 0.0;
		double FedPeakRadius = 0.0;
		double FedFinalMass = 0.0;
		double FedInitialRadius = ActorVolume("Fire fed by air")->RadiusCm;
		double ControlOutAt = -1.0;
		double FedOutAt = -1.0;
		while (GDemo.TimeS < 4.0)
		{
			Frame(Log);
			const FVolume* Control = ActorVolume("Fire alone");
			const FVolume* Fed = ActorVolume("Fire fed by air");
			if (GDemo.TimeS > 0.9 && GDemo.TimeS < 0.92)
			{
				ControlT = Control ? Control->TemperatureK : 0.0;
				FedT = Fed ? Fed->TemperatureK : 0.0;
			}
			if (!Control && ControlOutAt < 0.0)
			{
				ControlOutAt = GDemo.TimeS;
			}
			if (Fed)
			{
				FedPeakRadius = KMax(FedPeakRadius, Fed->RadiusCm);
				FedFinalMass = Fed->MassKg;
			}
			else if (FedOutAt < 0.0)
			{
				FedOutAt = GDemo.TimeS;
			}
		}
		const double AirBurned = Log.Mass(EReactionType::Oxygenation);
		std::printf("    at t=0.9 s: alone %.0f K, fed %.0f K | radius %.0f -> %.0f cm | alone out at %.2f s, fed out at %.2f s | %.2f kg air burned, %.2f MJ released\n",
			ControlT, FedT, FedInitialRadius, FedPeakRadius, ControlOutAt, FedOutAt, AirBurned, Log.TotalEnergy[static_cast<int>(EReactionType::Oxygenation)] / 1e6);
		ExpectTrue("fed flame is hotter than the lone flame at t=0.9 s", FedT > ControlT + 150.0);
		ExpectTrue("fed flame expands by more than 40%", FedPeakRadius > 1.4 * FedInitialRadius);
		ExpectTrue("fed flame outlives the lone flame by over a second", FedOutAt - ControlOutAt > 1.0);
		ExpectNear("flame mass gained == air burned (kg)", FedFinalMass - 0.4, AirBurned, 1e-9);
	}

	void MudScenario()
	{
		SimTest::Section("Scenario: water on soil vs granite (mud)");
		GDemo.Load(2);
		FRunLog Log;
		const FHandle Soil = GDemo.FindActorByLabel("Soil clod")->Handle;
		const FHandle Granite = GDemo.FindActorByLabel("Granite")->Handle;
		const double SoilX0 = GDemo.World.GetVolume(Soil)->LocationCm.X;
		while (GDemo.TimeS < 4.0)
		{
			Frame(Log);
		}
		const FHandle MudVolume = Log.Last[static_cast<int>(EReactionType::MudFormed)].VolumeB;
		const FVolume* SoilVolume = GDemo.World.GetVolume(Soil);
		const FVolume* GraniteVolume = GDemo.World.GetVolume(Granite);
		double MinTraction = 1.0;
		for (int Index = 0; Index < GDemo.World.GetNumMoisturePatches(); ++Index)
		{
			MinTraction = KMin(MinTraction, GDemo.World.GetSurfaceTractionMultiplierAt(GDemo.World.GetMoisturePatch(Index).CenterCm));
		}
		std::printf("    soil: +%.2f kg water, saturation %.2f, shoved %.0f cm | granite: +%.2f kg | %d wet patches, min traction %.2f\n",
			SoilVolume->MassKg - 100.0, SoilVolume->Saturation, SoilVolume->LocationCm.X - SoilX0, GraniteVolume->MassKg - 100.0,
			GDemo.World.GetNumMoisturePatches(), MinTraction);
		ExpectTrue("soil crosses the mud threshold", SoilVolume->Saturation >= 0.6);
		ExpectTrue("exactly one MudFormed event, and it is the soil", Log.Num(EReactionType::MudFormed) == 1 && MudVolume == Soil);
		ExpectTrue("granite takes almost nothing (< 1 kg)", GraniteVolume->MassKg - 100.0 < 1.0);
		ExpectNear("water absorbed by both rocks == Saturation events (kg)", (SoilVolume->MassKg - 100.0) + (GraniteVolume->MassKg - 100.0),
			Log.Mass(EReactionType::Saturation), 1e-9);
		ExpectTrue("the splash shoved the soil clod", SoilVolume->LocationCm.X - SoilX0 > 20.0);
		ExpectTrue("spilled water left wet ground with reduced traction", GDemo.World.GetNumMoisturePatches() > 0 && MinTraction < 0.95);
	}

	void DeflectionScenario()
	{
		SimTest::Section("Scenario: air jet vs pebble and boulder (deflection)");
		GDemo.Load(3);
		FRunLog Log;
		while (GDemo.TimeS < 1.2)
		{
			Frame(Log);
		}
		const FVolume* Pebble = ActorVolume("Pebble");
		const FVolume* Boulder = ActorVolume("Boulder");
		const double PebbleV = Pebble ? Pebble->VelocityCmS.X / 100.0 : 0.0;
		const double BoulderV = Boulder ? Boulder->VelocityCmS.X / 100.0 : 0.0;
		std::printf("    pebble %.1f -> %.1f m/s | boulder %.1f -> %.2f m/s | airbender spent %.0f kJ of kinetic work (%.0f chi)\n",
			-15.0, PebbleV, -15.0, BoulderV, GDemo.BenderKineticWorkJ / 1e3, GDemo.BenderKineticWorkJ / 5000.0);
		ExpectTrue("pebble is turned around", Pebble && PebbleV > 0.0);
		ExpectTrue("boulder keeps coming (still faster than 13 m/s toward the bender)", Boulder && BoulderV < -13.0);
		ExpectTrue("but the boulder did slow down", Boulder && BoulderV > -15.0);
	}

	void FreezeScenario()
	{
		SimTest::Section("Scenario: freeze the whip, then fire");
		GDemo.Load(4);
		FRunLog Log;
		while (GDemo.TimeS < 1.5)
		{
			Frame(Log);
		}
		const FVolume* Whip = ActorVolume("Water whip");
		using namespace Thermo::Constants;
		const double Expected = 12.0 * (SpecificHeatWater * (288.15 - WaterFreezingPointK) + LatentHeatFusion);
		std::printf("    freezing 12 kg cost %.2f MJ = %.1f chi at %.0f J/chi\n", GDemo.BenderThermalWorkJ / 1e6, GDemo.BenderThermalWorkJ / 100000.0, 100000.0);
		ExpectTrue("the whip is ice", Whip && Whip->Substance == ESubstance::Ice);
		ExpectTrue("a Freezing event fired", Log.Num(EReactionType::Freezing) == 1);
		ExpectNear("bender paid m*(c*dT + L_f) (J)", GDemo.BenderThermalWorkJ, Expected, 1e-3 * Expected);
		while (GDemo.TimeS < 5.0)
		{
			Frame(Log);
		}
		Whip = ActorVolume("Water whip");
		const double MeltProgress = Whip ? Whip->LatentHeatJ / (Whip->MassKg * LatentHeatFusion) : 0.0;
		std::printf("    after the fire blast: still %s, %.1f%% of the way to melting\n", Whip ? GetSubstanceName(Whip->Substance) : "gone", MeltProgress * 100.0);
		ExpectTrue("a 0.6 kg flame cannot melt 12 kg of ice", Whip && Whip->Substance == ESubstance::Ice);
		ExpectTrue("the ice banked some latent heat", MeltProgress > 0.0 && MeltProgress < 1.0);
		ExpectTrue("the flame went out", Log.Num(EReactionType::Extinguished) == 1);
	}

	void DryingScenario()
	{
		SimTest::Section("Scenario: firebender dries mud");
		GDemo.Load(5);
		FRunLog Log;
		Frame(Log);
		const double TractionBefore = GDemo.World.GetSurfaceTractionMultiplierAt(FVec3());
		double MaxFlameT = 0.0;
		double RecoveredAt = -1.0;
		while (GDemo.TimeS < 9.0)
		{
			Frame(Log);
			if (const FVolume* Flame = ActorVolume("Flame jet"))
			{
				MaxFlameT = KMax(MaxFlameT, Flame->TemperatureK);
			}
			if (RecoveredAt < 0.0 && GDemo.World.GetSurfaceTractionMultiplierAt(FVec3()) > 0.95)
			{
				RecoveredAt = GDemo.TimeS;
			}
		}
		const double Evaporated = Log.Mass(EReactionType::Evaporation);
		std::printf("    traction %.2f -> %.2f (back above 0.95 at t=%.1f s) | %.1f kg boiled off | bender paid %.1f MJ = %.0f chi | flame peak %.0f K\n",
			TractionBefore, GDemo.World.GetSurfaceTractionMultiplierAt(FVec3()), RecoveredAt, Evaporated, GDemo.BenderThermalWorkJ / 1e6,
			GDemo.BenderThermalWorkJ / 100000.0, MaxFlameT);
		ExpectTrue("mud starts slippery (traction < 0.5)", TractionBefore < 0.5);
		ExpectTrue("traction fully recovers", GDemo.World.GetSurfaceTractionMultiplierAt(FVec3()) > 0.99);
		ExpectTrue("the water left as steam (> 15 kg)", Evaporated > 15.0);
		using namespace Thermo::Constants;
		ExpectTrue("bender paid at least the latent heat of what boiled", GDemo.BenderThermalWorkJ > Evaporated * LatentHeatVaporization);
		ExpectTrue("flame never exceeds 2300 K", MaxFlameT <= 2300.0 + 1e-6);
	}

	void Determinism()
	{
		SimTest::Section("Determinism");
		double Digests[2] = {};
		for (double& Digest : Digests)
		{
			GDemo.Load(0);
			FRunLog Log;
			while (GDemo.TimeS < 3.0)
			{
				Frame(Log);
			}
			for (int Index = 0; Index < GDemo.World.GetSlotLimit(); ++Index)
			{
				if (const FVolume* Volume = GDemo.World.GetVolume(GDemo.World.GetHandleAt(Index)))
				{
					Digest += Volume->MassKg * 1e3 + Volume->TemperatureK + Volume->LocationCm.Z;
				}
			}
		}
		ExpectTrue("two runs of the same scenario are bit-identical", Digests[0] == Digests[1]);
	}

	void Performance()
	{
		SimTest::Section("Performance (900 interacting volumes)");
		ResetIsolatedWorld();
		// Nine clusters of fire, water and air, all overlapping their neighbours and moving.
		int Added = 0;
		for (int I = 0; I < 300; ++I)
		{
			const double X = -6000.0 + 40.0 * I;
			const double Z = 200.0 + 30.0 * (I % 7);
			Added += GWorld.AddVolume(MakeFire(0.4, 1300.0, FVec3(X, 0, Z), FVec3(300, 0, 0)), true).IsSet();
			Added += GWorld.AddVolume(MakeWater(15.0, FVec3(X + 20.0, 0, Z + 10.0), FVec3(-200, 0, 0)), true).IsSet();
			Added += GWorld.AddVolume(MakeAirWave(60.0, 2.0, FVec3(X - 20.0, 0, Z - 10.0), FVec3(900, 0, 0)), true).IsSet();
		}
		int MaxContacts = 0;
		const auto Start = std::chrono::steady_clock::now();
		for (int Step = 0; Step < 120; ++Step)
		{
			GWorld.Step(1.0 / 60.0);
			MaxContacts = KMax(MaxContacts, GWorld.GetStats().ContactsLastStep);
		}
		const double Ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - Start).count();
		std::printf("    %d volumes, 120 steps in %.1f ms = %.3f ms/step (peak %d reacting contacts per step)\n", Added, Ms, Ms / 120.0, MaxContacts);
		ExpectTrue("900 volumes step in under 2 ms (12% of a 60 Hz frame)", Added == 900 && Ms / 120.0 < 2.0);
	}

	void WriteDigest(const char* Path)
	{
		std::FILE* File = std::fopen(Path, "w");
		if (!File)
		{
			return;
		}
		std::fprintf(File, "{\n");
		for (int Scenario = 0; Scenario < NumScenarios - 1; ++Scenario)
		{
			GDemo.Load(Scenario);
			FRunLog Log;
			const double Duration = KMin(GetScenarioInfo(Scenario).DurationS, 6.0);
			int Frames = 0;
			while (GDemo.TimeS < Duration)
			{
				Frame(Log);
				++Frames;
			}
			std::fprintf(File, "  \"%d\": {\"frames\": %d, \"thermal\": %.17g, \"kinetic\": %.17g, \"volumes\": [", Scenario, Frames, GDemo.BenderThermalWorkJ, GDemo.BenderKineticWorkJ);
			bool bFirst = true;
			for (int Index = 0; Index < GDemo.World.GetSlotLimit(); ++Index)
			{
				if (const FVolume* Volume = GDemo.World.GetVolume(GDemo.World.GetHandleAt(Index)))
				{
					std::fprintf(File, "%s[%d, %.17g, %.17g, %.17g, %.17g, %.17g]", bFirst ? "" : ", ", static_cast<int>(Volume->Substance),
						Volume->MassKg, Volume->TemperatureK, Volume->LocationCm.X, Volume->LocationCm.Z, Volume->VelocityCmS.X);
					bFirst = false;
				}
			}
			std::fprintf(File, "]}%s\n", Scenario < NumScenarios - 2 ? "," : "");
		}
		std::fprintf(File, "}\n");
		std::fclose(File);
	}
}

int main(int Argc, char** Argv)
{
	AeroDragConservesMomentumAndRespectsMass();
	HeatExchangeConservesEnergyAndMass();
	IceBanksLatentHeat();
	FlameCannotExceedAdiabaticLimit();
	EvaporationScenario();
	OxygenationScenario();
	MudScenario();
	DeflectionScenario();
	FreezeScenario();
	DryingScenario();
	Determinism();
	Performance();
	if (Argc > 1)
	{
		WriteDigest(Argv[1]);
		std::printf("\nWrote native digest to %s\n", Argv[1]);
	}
	return SimTest::Finish("Simulation scenarios");
}
