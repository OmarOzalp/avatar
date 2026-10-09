// Water whip checks: shape control, lash, the bender's force budget, reactions, freezing, release.
// Build and run with Tests/run_all.sh.

#include "SimTestHarness.h"
#include "Sim/BendingWaterWhip.h"

#include <cmath>
#include <cstdio>

using namespace BendingSim;

namespace
{
	FSimWorld GWorld;
	FTerrain GTerrain;
	FDefaultReactionParams GParams;
	constexpr double Dt = 1.0 / 60.0;
	const FVec3 Hand(0.0, 0.0, 140.0);
	const FVec3 Pond(500.0, 0.0, 0.0);

	void ResetWorld()
	{
		GWorld.Reset();
		GWorld.ClearReactions();
		GWorld.Settings = FSimSettings();
		AddBuiltInReactions(GWorld, GParams);
		GTerrain = FTerrain();
		GTerrain.Init(101, 101, 40.0, FVec3(-2000.0, -2000.0, 0.0), 0.0);
	}

	void Frame(FWaterWhip& Whip, const FVec3& HandCm, const FVec3& HandVelocity, const FVec3& Aim)
	{
		Whip.SetControl(HandCm, HandVelocity, Aim);
		Whip.PreStep(GWorld, &GTerrain, Dt);
		GWorld.Advance(Dt);
		Whip.PostStep(GWorld);
	}

	double ChainLength(const FWaterWhip& Whip)
	{
		double Length = 0.0;
		for (int Index = 0; Index + 1 < Whip.GetNumPoints(); ++Index)
		{
			Length += Distance(Whip.GetPoint(Index), Whip.GetPoint(Index + 1));
		}
		return Length;
	}

	void DrawAndHold()
	{
		SimTest::Section("Draw water from a pond and hold it");
		ResetWorld();
		FWaterWhip Whip;
		const bool bCreated = Whip.Create(GWorld, Pond, Hand, 20.0, 288.15);
		ExpectTrue("whip created from 20 kg of pond water", bCreated);
		ExpectTrue("starts forming", Whip.GetState() == EWhipState::Forming);
		const FVec3 Aim(800.0, 0.0, 140.0);
		for (int Step = 0; Step < 90; ++Step)
		{
			Frame(Whip, Hand, FVec3(), Aim);
		}
		ExpectTrue("formed into the held shape after 1.5 s", Whip.GetState() == EWhipState::Holding);
		const double Length = ChainLength(Whip);
		std::printf("    chain length %.0f cm (target %.0f), tip %.0f cm from the hand, %.1f kg\n", Length, Whip.Settings.LengthCm,
			Distance(Whip.GetPoint(Whip.GetNumPoints() - 1), Hand), Whip.GetMassKg(GWorld));
		ExpectNear("chain keeps its length (cm)", Length, Whip.Settings.LengthCm, 0.05 * Whip.Settings.LengthCm);
		ExpectNear("no water lost while holding (kg)", Whip.GetMassKg(GWorld), 20.0, 1e-9);
		double Lowest = 1e300;
		double Fastest = 0.0;
		for (int Index = 0; Index < Whip.GetNumPoints(); ++Index)
		{
			Lowest = KMin(Lowest, Whip.GetPoint(Index).Z);
			Fastest = KMax(Fastest, Whip.GetPointVelocity(Index).Size());
		}
		ExpectTrue("held off the ground against gravity", Lowest > 30.0);
		ExpectTrue("held water flows gently (< 3 m/s)", Fastest < 300.0);
		ExpectTrue("segments are Water capsules in the world", GWorld.GetVolume(Whip.GetSegmentHandle(3))->Shape == EShape::Capsule
			&& GWorld.GetVolume(Whip.GetSegmentHandle(3))->Substance == ESubstance::Water);
	}

	void LashCracks()
	{
		SimTest::Section("Lash");
		ResetWorld();
		FWaterWhip Whip;
		Whip.Create(GWorld, Pond, Hand, 20.0, 288.15);
		const FVec3 Aim(900.0, 0.0, 100.0);
		for (int Step = 0; Step < 80; ++Step)
		{
			Frame(Whip, Hand, FVec3(), Aim);
		}
		ExpectTrue("lash accepted while holding", Whip.Lash());
		double PeakTip = 0.0;
		double Along = 0.0;
		double OffLine = 0.0;
		const FVec3 Line = (Aim - Hand).GetSafeNormal();
		for (int Step = 0; Step < 30; ++Step)
		{
			Frame(Whip, Hand, FVec3(), Aim);
			PeakTip = KMax(PeakTip, Whip.GetTipSpeedCmS());
			const FVec3 Tip = Whip.GetPoint(Whip.GetNumPoints() - 1);
			const double TipAlong = (Tip - Hand).Dot(Line);
			if (TipAlong > Along)
			{
				Along = TipAlong;
				OffLine = ((Tip - Hand) - Line * TipAlong).Size();
			}
		}
		std::printf("    peak tip speed %.1f m/s; at full reach the tip is %.0f cm along the aim line, %.0f cm off it\n", PeakTip / 100.0, Along, OffLine);
		ExpectTrue("tip cracks faster than 15 m/s", PeakTip > 1500.0);
		ExpectTrue("whip reaches out along the aim (> 80% of its length)", Along > 0.8 * Whip.Settings.LengthCm);
		ExpectTrue("tip ends near the aim line (< 1 m off)", OffLine < 100.0);

		FWhipDrop Spray[FWaterWhip::MaxPendingSpray];
		const int NumSpray = Whip.ConsumeSpray(Spray, FWaterWhip::MaxPendingSpray);
		ExpectTrue("the snap flings spray off the tip", NumSpray == 1 && Spray[0].MassKg > 0.05);
		if (NumSpray == 1)
		{
			std::printf("    spray: %.2f kg at %.1f m/s along the aim\n", Spray[0].MassKg, Spray[0].VelocityCmS.Dot(Line) / 100.0);
			ExpectTrue("spray flies on toward the aim (> 5 m/s)", Spray[0].VelocityCmS.Dot(Line) > 500.0);
			ExpectNear("water is conserved: whip + spray = 20 kg", Whip.GetMassKg(GWorld) + Spray[0].MassKg, 20.0, 1e-9);
		}

		// Nothing is held: the whip comes back to circle the bender by itself.
		for (int Step = 0; Step < 45; ++Step)
		{
			Frame(Whip, Hand, FVec3(), Aim);
		}
		double Farthest = 0.0;
		for (int Index = 0; Index < Whip.GetNumPoints(); ++Index)
		{
			Farthest = KMax(Farthest, Distance(Whip.GetPoint(Index), Hand));
		}
		std::printf("    1.25 s after the lash: %s, farthest water %.0f cm from the hand\n", Whip.GetState() == EWhipState::Holding ? "circling again" : "still out", Farthest);
		ExpectTrue("comes back on its own", Whip.GetState() == EWhipState::Holding);
		ExpectTrue("back around the bender (< 2.6 m from the hand)", Farthest < 260.0);
	}

	void LashesChain()
	{
		SimTest::Section("Lashes chain and the stream tapers");
		ResetWorld();
		FWaterWhip Whip;
		Whip.Create(GWorld, Pond, Hand, 20.0, 288.15);
		const FVec3 Aim(900.0, 0.0, 100.0);
		for (int Step = 0; Step < 80; ++Step)
		{
			Frame(Whip, Hand, FVec3(), Aim);
		}
		ExpectTrue("thicker at the hand than at the tip", Whip.GetSegmentRadiusCm(0) > 1.5 * Whip.GetSegmentRadiusCm(Whip.GetNumSegments() - 1));
		std::printf("    radius %.1f cm at the hand, %.1f cm at the tip\n", Whip.GetSegmentRadiusCm(0), Whip.GetSegmentRadiusCm(Whip.GetNumSegments() - 1));
		Whip.Lash();
		ExpectTrue("cannot restart a lash mid-strike", !Whip.Lash());
		int Lashes = 1;
		for (int Step = 0; Step < 90; ++Step)
		{
			if (Whip.GetState() == EWhipState::Returning && Whip.Lash())
			{
				++Lashes;
			}
			Frame(Whip, Hand, FVec3(), Aim);
		}
		ExpectTrue("a lash on the way back starts the next one", Lashes >= 3);
		ExpectNear("chain keeps its length through chained lashes (cm)", ChainLength(Whip), Whip.Settings.LengthCm, 0.08 * Whip.Settings.LengthCm);
	}

	void ForceBudgetIsPhysical()
	{
		SimTest::Section("The bender's force budget is physical");
		ResetWorld();
		FWaterWhip Whip;
		Whip.Settings.MaxControlAccelerationMs2 = 6.0; // less than gravity: cannot hold water up
		Whip.Create(GWorld, Pond, Hand, 20.0, 288.15);
		for (int Step = 0; Step < 150; ++Step)
		{
			Frame(Whip, Hand, FVec3(), FVec3(800.0, 0.0, 140.0));
		}
		double Lowest = 1e300;
		for (int Index = 1; Index < Whip.GetNumPoints(); ++Index)
		{
			Lowest = KMin(Lowest, Whip.GetPoint(Index).Z - GTerrain.GetHeightAt(Whip.GetPoint(Index).X, Whip.GetPoint(Index).Y));
		}
		std::printf("    with 6 m/s^2 of control (< g), the lowest point rests %.0f cm above the ground\n", Lowest);
		ExpectTrue("water sags to the ground when the bender cannot beat gravity", Lowest < 30.0);
		ExpectTrue("but never sinks into it", Lowest >= 0.0);
	}

	void WhipPutsOutFire()
	{
		SimTest::Section("Whip vs fire");
		ResetWorld();
		FWaterWhip Whip;
		Whip.Create(GWorld, Pond, Hand, 20.0, 288.15);
		const FVec3 Aim(500.0, 0.0, 120.0);
		for (int Step = 0; Step < 80; ++Step)
		{
			Frame(Whip, Hand, FVec3(), Aim);
		}
		FVolume Fire = FVolume::MakeDefault(ESubstance::Fire, 0.6);
		Fire.TemperatureK = 1500.0;
		Fire.LocationCm = FVec3(520.0, 0.0, 120.0);
		const FHandle FireHandle = GWorld.AddVolume(Fire, true);
		const double MassBefore = Whip.GetMassKg(GWorld);
		bool bExtinguished = false;
		double SteamKg = 0.0;
		double SprayKg = 0.0;
		FReactionEvent Events[FSimWorld::MaxFlushedEvents];
		FWhipDrop Spray[FWaterWhip::MaxPendingSpray];
		for (int Step = 0; Step < 90; ++Step)
		{
			// Two lashes, 0.75 s apart.
			if (Step == 0 || Step == 45)
			{
				Whip.Lash();
			}
			// The flame is a brazier: it stays put.
			if (FVolume* Flame = GWorld.GetVolume(FireHandle))
			{
				Flame->LocationCm = FVec3(520.0, 0.0, 120.0);
				Flame->VelocityCmS = FVec3();
			}
			Frame(Whip, Hand, FVec3(), Aim);
			const int NumSpray = Whip.ConsumeSpray(Spray, FWaterWhip::MaxPendingSpray);
			for (int Index = 0; Index < NumSpray; ++Index)
			{
				SprayKg += Spray[Index].MassKg;
			}
			const int Count = GWorld.FlushEvents(Dt, Events, FSimWorld::MaxFlushedEvents);
			for (int Index = 0; Index < Count; ++Index)
			{
				bExtinguished |= Events[Index].Type == EReactionType::Extinguished;
				SteamKg += Events[Index].Type == EReactionType::Evaporation ? Events[Index].MassKg : 0.0;
			}
		}
		const double Lost = MassBefore - Whip.GetMassKg(GWorld);
		std::printf("    two lashes boiled %.1f g into steam and flung %.0f g of spray; whip lost %.1f g; fire %s\n", SteamKg * 1000.0, SprayKg * 1000.0,
			Lost * 1000.0, bExtinguished ? "out" : "still burning");
		ExpectTrue("the fire went out", bExtinguished);
		ExpectTrue("water boiled into steam", SteamKg > 0.005);
		ExpectNear("whip lost exactly the steam it made plus its spray (g)", Lost * 1000.0, (SteamKg + SprayKg) * 1000.0, 0.01);
	}

	void FreezeAndThaw()
	{
		SimTest::Section("Freeze and thaw");
		ResetWorld();
		FWaterWhip Whip;
		Whip.Create(GWorld, Pond, Hand, 20.0, 288.15);
		for (int Step = 0; Step < 80; ++Step)
		{
			Frame(Whip, Hand, FVec3(), FVec3(800.0, 0.0, 140.0));
		}
		const double Need = Whip.GetHeatToFreeze(GWorld);
		const double Expected = 20.0 * (4186.0 * 15.0 + 334000.0);
		const double Moved = Whip.TransferHeat(GWorld, -Need);
		Frame(Whip, Hand, FVec3(), FVec3(800.0, 0.0, 140.0));
		std::printf("    freezing 20 kg took %.2f MJ = %.1f chi at 250 kJ/chi\n", -Moved / 1e6, -Moved / 2.5e5);
		ExpectNear("heat to freeze = m (c dT + L_f) (MJ)", Need / 1e6, Expected / 1e6, 0.01);
		ExpectNear("all of it was extracted (MJ)", -Moved / 1e6, Need / 1e6, 1e-9);
		ExpectTrue("the whip is ice", Whip.IsFrozen(GWorld));
		const double Before = ChainLength(Whip);
		Whip.Lash();
		for (int Step = 0; Step < 30; ++Step)
		{
			Frame(Whip, Hand, FVec3(), FVec3(800.0, 0.0, 140.0));
		}
		ExpectNear("ice keeps its length through a lash (cm)", ChainLength(Whip), Before, 0.05 * Before);
		const double Thaw = Whip.TransferHeat(GWorld, Whip.GetHeatToMelt(GWorld) * 1.01);
		Frame(Whip, Hand, FVec3(), FVec3(800.0, 0.0, 140.0));
		ExpectTrue("thawed back to water", !Whip.IsAnyFrozen(GWorld));
		ExpectTrue("thawing took the latent heat back (> 6.6 MJ)", Thaw > 20.0 * 334000.0);
	}

	void PartialFreezeStartsAtTheHand()
	{
		SimTest::Section("Short of chi: part of the whip freezes, from the hand out");
		ResetWorld();
		FWaterWhip Whip;
		Whip.Create(GWorld, Pond, Hand, 20.0, 288.15);
		for (int Step = 0; Step < 80; ++Step)
		{
			Frame(Whip, Hand, FVec3(), FVec3(800.0, 0.0, 140.0));
		}
		Whip.TransferHeat(GWorld, -0.5 * Whip.GetHeatToFreeze(GWorld));
		Frame(Whip, Hand, FVec3(), FVec3(800.0, 0.0, 140.0));
		int Frozen = 0;
		double FrozenKg = 0.0;
		double LargestSegmentKg = 0.0;
		bool bContiguous = true;
		bool bSeenWater = false;
		for (int Segment = 0; Segment < Whip.GetNumSegments(); ++Segment)
		{
			const FVolume* Volume = GWorld.GetVolume(Whip.GetSegmentHandle(Segment));
			const bool bIce = Volume->Substance == ESubstance::Ice;
			Frozen += bIce ? 1 : 0;
			FrozenKg += bIce ? Volume->MassKg : 0.0;
			LargestSegmentKg = KMax(LargestSegmentKg, Volume->MassKg);
			bContiguous &= !(bIce && bSeenWater);
			bSeenWater |= !bIce;
		}
		std::printf("    half the heat froze %.1f of 20 kg (%d of %d segments: the stream is thickest at the hand)\n", FrozenKg, Frozen, Whip.GetNumSegments());
		ExpectNear("about half the water froze (kg)", FrozenKg, 10.0, LargestSegmentKg);
		ExpectTrue("the frozen part starts at the hand", bContiguous && GWorld.GetVolume(Whip.GetSegmentHandle(0))->Substance == ESubstance::Ice);
	}

	void ReleaseConservesWater()
	{
		SimTest::Section("Release");
		ResetWorld();
		FWaterWhip Whip;
		Whip.Create(GWorld, Pond, Hand, 20.0, 288.15);
		for (int Step = 0; Step < 60; ++Step)
		{
			Frame(Whip, Hand, FVec3(), FVec3(800.0, 0.0, 140.0));
		}
		FWhipDrop Drops[FWaterWhip::MaxSegments];
		const int Count = Whip.Release(GWorld, Drops, FWaterWhip::MaxSegments);
		double Total = 0.0;
		for (int Index = 0; Index < Count; ++Index)
		{
			Total += Drops[Index].MassKg;
		}
		ExpectNear("released parcels carry all 20 kg", Total, 20.0, 1e-9);
		ExpectTrue("whip is gone", !Whip.IsActive());
		ExpectTrue("its volumes left the world", GWorld.GetNumVolumes() == 0);
	}

	void FollowsARunningBender()
	{
		SimTest::Section("Follows a running bender");
		ResetWorld();
		FWaterWhip Whip;
		Whip.Create(GWorld, Pond, Hand, 20.0, 288.15);
		FVec3 HandNow = Hand;
		const FVec3 RunVelocity(700.0, 300.0, 0.0);
		double WorstReach = 0.0;
		for (int Step = 0; Step < 180; ++Step)
		{
			HandNow += RunVelocity * Dt;
			Frame(Whip, HandNow, RunVelocity, HandNow + FVec3(800.0, 0.0, 0.0));
			if (Step > 60)
			{
				for (int Index = 0; Index < Whip.GetNumPoints(); ++Index)
				{
					WorstReach = KMax(WorstReach, Distance(Whip.GetPoint(Index), HandNow));
				}
			}
		}
		std::printf("    sprinting at 7.6 m/s for 3 s: farthest water %.0f cm from the hand\n", WorstReach);
		ExpectTrue("the water keeps up (never farther than its length)", WorstReach <= Whip.Settings.LengthCm * 1.05);
	}

	void Deterministic()
	{
		SimTest::Section("Determinism");
		FVec3 Tips[2];
		for (int Run = 0; Run < 2; ++Run)
		{
			ResetWorld();
			FWaterWhip Whip;
			Whip.Create(GWorld, Pond, Hand, 20.0, 288.15);
			for (int Step = 0; Step < 120; ++Step)
			{
				if (Step == 70)
				{
					Whip.Lash();
				}
				Frame(Whip, Hand + FVec3(Step * 2.0, 0.0, 0.0), FVec3(120.0, 0.0, 0.0), FVec3(900.0, 100.0, 120.0));
			}
			Tips[Run] = Whip.GetPoint(Whip.GetNumPoints() - 1);
		}
		ExpectTrue("two runs are bit-identical", Tips[0].X == Tips[1].X && Tips[0].Y == Tips[1].Y && Tips[0].Z == Tips[1].Z);
	}
}

int main()
{
	DrawAndHold();
	LashCracks();
	LashesChain();
	ForceBudgetIsPhysical();
	WhipPutsOutFire();
	FreezeAndThaw();
	PartialFreezeStartsAtTheHand();
	ReleaseConservesWater();
	FollowsARunningBender();
	Deterministic();
	return SimTest::Finish("Water whip");
}
