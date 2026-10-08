// 3D sandbox checks: the character, every technique, and how they play together, driven by scripted input the
// way the browser drives it. Build and run with Tests/run_all.sh.

#include "SimTestHarness.h"
#include "BendingSandbox3D.h"
#include "SandboxScript.h"

#include <chrono>
#include <cmath>
#include <cstdio>

using namespace BendingSim;
using namespace BendingSandbox3D;

namespace
{
	FSandbox GSandbox;
	constexpr double Dt = 1.0 / 60.0;

	FInput LookAt(const FVec3& TargetCm)
	{
		FInput Input;
		Input.CameraLocationCm = GSandbox.Player.LocationCm + FVec3(0.0, 0.0, 260.0);
		Input.CameraForward = (TargetCm - Input.CameraLocationCm).GetSafeNormal();
		return Input;
	}

	void Run(const FInput& Input, double Seconds)
	{
		const int Frames = static_cast<int>(Seconds * 60.0 + 0.5);
		for (int Frame = 0; Frame < Frames; ++Frame)
		{
			GSandbox.Advance(Input, Dt);
		}
	}

	/** Presses a slot for one frame (edge), then keeps aiming for Seconds. */
	void Press(ETechniqueSlot Slot, const FVec3& TargetCm, double Seconds)
	{
		FInput Input = LookAt(TargetCm);
		Input.bSlotHeld[static_cast<int>(Slot)] = true;
		GSandbox.Advance(Input, Dt);
		Input.bSlotHeld[static_cast<int>(Slot)] = false;
		Run(Input, Seconds);
	}

	void Hold(ETechniqueSlot Slot, const FVec3& TargetCm, double Seconds)
	{
		FInput Input = LookAt(TargetCm);
		Input.bSlotHeld[static_cast<int>(Slot)] = true;
		Run(Input, Seconds);
		Input.bSlotHeld[static_cast<int>(Slot)] = false;
		GSandbox.Advance(Input, Dt);
	}

	void SetStance(ETechniqueElement Element)
	{
		FInput Input = LookAt(GSandbox.Player.LocationCm + FVec3(1000.0, 0.0, 0.0));
		Input.StanceRequest = static_cast<int>(Element);
		GSandbox.Advance(Input, Dt);
	}

	void Teleport(double X, double Y, double YawDeg)
	{
		GSandbox.Player.LocationCm = FVec3(X, Y, GSandbox.Terrain.GetHeightAt(X, Y));
		GSandbox.Player.VelocityCmS = FVec3();
		GSandbox.Player.YawRad = YawDeg * Pi / 180.0;
		GSandbox.Player.bGrounded = true;
	}

	FVec3 Ground(double X, double Y)
	{
		return FVec3(X, Y, GSandbox.Terrain.GetHeightAt(X, Y));
	}

	int FindProp(EArenaProp Kind, int Nth = 0)
	{
		for (int Index = 0; Index < GSandbox.NumBodies; ++Index)
		{
			if (GSandbox.Bodies[Index].bAlive && GSandbox.Bodies[Index].Kind == EBodyKind::Prop && GSandbox.Bodies[Index].Prop == Kind && Nth-- == 0)
			{
				return Index;
			}
		}
		return -1;
	}

	bool HasMessage(const char* Fragment)
	{
		for (int Index = 0; Index < GSandbox.NumMessages; ++Index)
		{
			const char* Text = GSandbox.Messages[Index].Text;
			for (const char* Start = Text; *Start; ++Start)
			{
				const char* A = Start;
				const char* B = Fragment;
				while (*A && *B && *A == *B)
				{
					++A;
					++B;
				}
				if (!*B)
				{
					return true;
				}
			}
		}
		return false;
	}

	void TheTrainingGroundLoads()
	{
		SimTest::Section("Training ground");
		GSandbox.Init();
		ExpectTrue("player stands on the ground", std::fabs(GSandbox.Player.LocationCm.Z - GSandbox.Terrain.GetHeightAt(GSandbox.Player.LocationCm.X, GSandbox.Player.LocationCm.Y)) < 1e-6);
		int Props = 0;
		for (int Index = 0; Index < GSandbox.NumBodies; ++Index)
		{
			Props += GSandbox.Bodies[Index].bAlive ? 1 : 0;
		}
		ExpectTrue("every prop of the layout is in the world", Props == GSandbox.Layout.NumProps);
		ExpectTrue("three braziers burn", GSandbox.Bodies[FindProp(EArenaProp::Brazier, 0)].bLit && GSandbox.Bodies[FindProp(EArenaProp::Brazier, 2)].bLit);
		Run(LookAt(GSandbox.Player.LocationCm + FVec3(1000.0, 0.0, 0.0)), 2.0);
		const FBody& Stone = GSandbox.Bodies[FindProp(EArenaProp::Stone)];
		ExpectTrue("loose props settle on the ground", Stone.VelocityCmS.Size() < 5.0
			&& std::fabs(Stone.LocationCm.Z - Stone.RadiusCm - GSandbox.Terrain.GetHeightAt(Stone.LocationCm.X, Stone.LocationCm.Y)) < 3.0);
		ExpectTrue("braziers keep burning on their own", GSandbox.Bodies[FindProp(EArenaProp::Brazier)].bLit);
	}

	void WalkRunJump()
	{
		SimTest::Section("Walk, sprint, jump");
		GSandbox.Init();
		Teleport(-500.0, -300.0, 0.0);
		FInput Input = LookAt(GSandbox.Player.LocationCm + FVec3(2000.0, 0.0, 0.0));
		Input.MoveForward = 1.0;
		Run(Input, 1.0);
		const double Walk = GSandbox.Player.VelocityCmS.Size2D();
		Input.bSprint = true;
		Run(Input, 1.0);
		const double Sprint = GSandbox.Player.VelocityCmS.Size2D();
		std::printf("    walk %.1f m/s, sprint %.1f m/s\n", Walk / 100.0, Sprint / 100.0);
		ExpectNear("walks at 5 m/s (cm/s)", Walk, 500.0, 5.0);
		ExpectNear("sprints at 8 m/s (cm/s)", Sprint, 800.0, 5.0);
		ExpectTrue("faces where it runs", std::fabs(GSandbox.Player.YawRad) < 0.05);

		Input = LookAt(GSandbox.Player.LocationCm + FVec3(2000.0, 0.0, 0.0));
		Run(Input, 1.0);
		const double Start = GSandbox.Player.LocationCm.Z;
		Input.bJump = true;
		double Peak = Start;
		bool bLeft = false;
		for (int Frame = 0; Frame < 90; ++Frame)
		{
			GSandbox.Advance(Input, Dt);
			Input.bJump = false;
			Peak = KMax(Peak, GSandbox.Player.LocationCm.Z);
			bLeft |= !GSandbox.Player.bGrounded;
		}
		std::printf("    jump apex %.0f cm (v^2 / 2g = %.0f cm)\n", Peak - Start, 480.0 * 480.0 / (2.0 * 980.665));
		ExpectTrue("leaves the ground", bLeft);
		ExpectNear("apex = v^2 / 2g (cm)", Peak - Start, 480.0 * 480.0 / (2.0 * 980.665), 6.0);
		ExpectTrue("lands again", GSandbox.Player.bGrounded);
	}

	void EarthWallBlocksAndConserves()
	{
		SimTest::Section("Earth wall");
		GSandbox.Init();
		Teleport(-600.0, -300.0, 0.0);
		SetStance(ETechniqueElement::Earth);
		const double SoilBefore = GSandbox.Terrain.GetTotalSoilVolumeM3();
		const double ChiBefore = GSandbox.Player.Chi;
		const FVec3 Spot = Ground(-100.0, -300.0);
		Press(ETechniqueSlot::Secondary, Spot, 1.2);
		const double Top = GSandbox.Terrain.GetHeightAt(-100.0, -300.0) - Spot.Z;
		std::printf("    wall top %.0f cm above the ground; %s\n", Top, GSandbox.NumMessages ? GSandbox.Messages[GSandbox.NumMessages - 1].Text : "");
		ExpectNear("wall rises to its 180 cm height (cm)", Top, 180.0, 30.0);
		ExpectTrue("soil was moved, not made (relative 1e-12)", std::fabs(GSandbox.Terrain.GetTotalSoilVolumeM3() - SoilBefore) / SoilBefore < 1e-12);
		ExpectTrue("trench dug beside it", GSandbox.Terrain.GetHeightAt(-100.0 - 110.0, -300.0) < Spot.Z - 5.0);
		ExpectTrue("cost chi (work + flat cost)", GSandbox.Player.Chi < ChiBefore - 10.0);

		// Walk into it.
		FInput Input = LookAt(GSandbox.Player.LocationCm + FVec3(2000.0, 0.0, 0.0));
		Input.MoveForward = 1.0;
		Run(Input, 3.0);
		std::printf("    walking at the wall for 3 s stops at x = %.0f cm (wall at -100)\n", GSandbox.Player.LocationCm.X);
		ExpectTrue("the wall blocks the way", GSandbox.Player.LocationCm.X < -100.0 - 30.0);
	}

	void RockThrowLeavesACrater()
	{
		SimTest::Section("Rock throw");
		GSandbox.Init();
		Teleport(-600.0, 300.0, 0.0);
		SetStance(ETechniqueElement::Earth);
		const double SoilBefore = GSandbox.Terrain.GetTotalSoilVolumeM3();
		const FVec3 Target = Ground(1400.0, 300.0) + FVec3(0.0, 0.0, 100.0);
		Press(ETechniqueSlot::Primary, Target, 0.2);
		int Rock = -1;
		for (int Index = 0; Index < GSandbox.NumBodies; ++Index)
		{
			Rock = GSandbox.Bodies[Index].bAlive && GSandbox.Bodies[Index].Kind == EBodyKind::ThrownRock ? Index : Rock;
		}
		ExpectTrue("a rock rose out of the ground", Rock >= 0);
		if (Rock < 0)
		{
			return;
		}
		const double RockMass = GSandbox.Bodies[Rock].MassKg;
		const double Removed = (SoilBefore - GSandbox.Terrain.GetTotalSoilVolumeM3()) * GSandbox.Terrain.SoilDensityKgM3;
		ExpectNear("rock mass = soil taken from the ground (kg)", RockMass, Removed, 1e-6);
		ExpectTrue("the quarry is a crater", GSandbox.Terrain.GetHeightAt(-600.0 + 220.0, 300.0) < Ground(-600.0, 300.0).Z - 5.0);
		Run(LookAt(Target), 0.3);
		const double LaunchSpeed = GSandbox.Bodies[Rock].VelocityCmS.Size();
		Run(LookAt(Target), 3.0);
		const double Travel = GSandbox.Bodies[Rock].LocationCm.X - (-380.0);
		std::printf("    %.0f kg rock launched at %.1f m/s, came to rest %.1f m away\n", RockMass, LaunchSpeed / 100.0, Travel / 100.0);
		ExpectTrue("thrown at ~20 m/s", LaunchSpeed > 1500.0 && LaunchSpeed < 2100.0);
		ExpectTrue("flew toward the target (> 10 m)", Travel > 1000.0);
	}

	void TerraformRaisesAndDigs()
	{
		SimTest::Section("Raise and lower ground");
		GSandbox.Init();
		Teleport(-600.0, -300.0, 0.0);
		SetStance(ETechniqueElement::Earth);
		const FVec3 Spot = Ground(0.0, 600.0);
		const double SoilBefore = GSandbox.Terrain.GetTotalSoilVolumeM3();
		Hold(ETechniqueSlot::Special, Spot, 2.0);
		const double Raised = GSandbox.Terrain.GetHeightAt(Spot.X, Spot.Y) - Spot.Z;
		const FVec3 Pit = Ground(-900.0, 600.0);
		Hold(ETechniqueSlot::Utility, Pit, 1.5);
		const double Dug = Pit.Z - GSandbox.Terrain.GetHeightAt(Pit.X, Pit.Y);
		std::printf("    2 s of Raise Ground: pillar %.0f cm; 1.5 s of Lower Ground: pit %.0f cm deep\n", Raised, Dug);
		ExpectTrue("holding Q raises a pillar (> 1 m)", Raised > 100.0);
		ExpectTrue("holding E digs a pit (> 50 cm)", Dug > 50.0);
		ExpectTrue("soil conserved through both", std::fabs(GSandbox.Terrain.GetTotalSoilVolumeM3() - SoilBefore) / SoilBefore < 1e-12);
	}

	void WhipFromThePond()
	{
		SimTest::Section("Water whip from the pond");
		GSandbox.Init();
		Teleport(700.0, 450.0, 90.0);
		SetStance(ETechniqueElement::Water);
		const double PondBefore = GSandbox.Layout.Ponds[0].WaterKg;
		const FVec3 Ahead = Ground(700.0, -400.0) + FVec3(0.0, 0.0, 100.0);
		Press(ETechniqueSlot::Primary, Ahead, 1.5);
		ExpectTrue("whip drawn", GSandbox.Whip.IsActive());
		ExpectNear("the pond lost exactly the whip's water (kg)", PondBefore - GSandbox.Layout.Ponds[0].WaterKg, GSandbox.Tuning.WhipWaterKg, 1e-9);
		ExpectTrue("whip formed in the hand", GSandbox.Whip.GetState() == EWhipState::Holding);
		double PeakTip = 0.0;
		FInput Input = LookAt(Ahead);
		Input.bSlotHeld[0] = true;
		GSandbox.Advance(Input, Dt);
		Input.bSlotHeld[0] = false;
		for (int Frame = 0; Frame < 40; ++Frame)
		{
			GSandbox.Advance(Input, Dt);
			PeakTip = KMax(PeakTip, GSandbox.Whip.GetTipSpeedCmS());
		}
		std::printf("    lash tip peaked at %.1f m/s\n", PeakTip / 100.0);
		ExpectTrue("second press lashes (tip > 15 m/s)", PeakTip > 1500.0);

		Teleport(-600.0, -300.0, 0.0);
		Run(LookAt(Ground(400.0, -300.0)), 0.5);
		Press(ETechniqueSlot::Primary, Ground(400.0, -300.0), 0.5);
		ExpectTrue("away from the pond, the whip comes along", GSandbox.Whip.IsActive());

		GSandbox.Init();
		Teleport(-2000.0, -2000.0, 0.0);
		SetStance(ETechniqueElement::Water);
		Press(ETechniqueSlot::Primary, Ground(-1000.0, -2000.0), 0.5);
		ExpectTrue("far from water there is nothing to bend", !GSandbox.Whip.IsActive() && HasMessage("No water"));
	}

	void WhipPutsOutABrazierFireRelightsIt()
	{
		SimTest::Section("Whip vs brazier, fire relights it");
		GSandbox.Init();
		const int Brazier = FindProp(EArenaProp::Brazier, 0);
		const FVec3 Anchor = GSandbox.Bodies[Brazier].AnchorCm;
		const FVec3 Flame = Anchor + FVec3(0.0, 0.0, 150.0);
		// Draw at the pond, then walk next to the brazier with the whip.
		Teleport(700.0, 450.0, 90.0);
		SetStance(ETechniqueElement::Water);
		Press(ETechniqueSlot::Primary, Ground(700.0, -400.0), 1.2);
		Teleport(Anchor.X - 420.0, Anchor.Y, 0.0);
		Run(LookAt(Flame), 0.5);
		// Lash, then keep holding the button: the whip stays stretched into the fire.
		FInput HoldLash = LookAt(Flame);
		HoldLash.bSlotHeld[0] = true;
		double HeldS = 0.0;
		for (; HeldS < 4.0 && GSandbox.Bodies[Brazier].bLit; HeldS += Dt)
		{
			GSandbox.Advance(HoldLash, Dt);
		}
		HoldLash.bSlotHeld[0] = false;
		GSandbox.Advance(HoldLash, Dt);
		std::printf("    whip held in the brazier: out after %.2f s\n", HeldS);
		ExpectTrue("lashing water puts the brazier out", !GSandbox.Bodies[Brazier].bLit);
		ExpectTrue("whip lost water to steam", GSandbox.Whip.IsActive() && GSandbox.Whip.GetMassKg(GSandbox.World) < GSandbox.Tuning.WhipWaterKg);
		std::printf("    whip mass after the fight: %.2f kg\n", GSandbox.Whip.GetMassKg(GSandbox.World));

		Press(ETechniqueSlot::Special, Flame, 1.0);
		ExpectTrue("Q releases the whip", !GSandbox.Whip.IsActive());
		SetStance(ETechniqueElement::Fire);
		Run(LookAt(Flame), 1.0);
		Press(ETechniqueSlot::Primary, Flame, 1.0);
		ExpectTrue("a fire blast relights it", GSandbox.Bodies[Brazier].bLit);
	}

	void FreezeCostsLatentHeat()
	{
		SimTest::Section("Freeze the whip");
		GSandbox.Init();
		Teleport(700.0, 450.0, 90.0);
		SetStance(ETechniqueElement::Water);
		Press(ETechniqueSlot::Primary, Ground(700.0, -400.0), 1.2);
		Run(LookAt(Ground(700.0, -400.0)), 2.0);
		const double Need = GSandbox.Whip.GetHeatToFreeze(GSandbox.World);
		const double ThermalBefore = GSandbox.ThermalWorkJ;
		Press(ETechniqueSlot::Secondary, Ground(700.0, -400.0), 0.6);
		const double Paid = GSandbox.ThermalWorkJ - ThermalBefore;
		std::printf("    froze %.1f kg: %.2f MJ = %.0f chi\n", GSandbox.Whip.GetMassKg(GSandbox.World), Paid / 1e6, Paid / GSandbox.Tuning.ThermalJoulesPerChi);
		ExpectTrue("the whip is ice", GSandbox.Whip.IsFrozen(GSandbox.World));
		ExpectNear("paid exactly the heat to freeze (MJ)", Paid / 1e6, Need / 1e6, 1e-6);
		Press(ETechniqueSlot::Special, Ground(700.0, -400.0), 0.5);
		ExpectTrue("an ice whip cannot be poured out", GSandbox.Whip.IsActive() && HasMessage("Thaw"));
	}

	void WaterBlastMakesMud()
	{
		SimTest::Section("Water blast makes mud");
		GSandbox.Init();
		Teleport(700.0, 450.0, 90.0);
		SetStance(ETechniqueElement::Water);
		Press(ETechniqueSlot::Primary, Ground(700.0, -400.0), 1.2);
		const FVec3 Spot = Ground(700.0, -500.0);
		Run(LookAt(Spot), 0.5);
		Press(ETechniqueSlot::Utility, Spot, 2.0);
		ExpectTrue("the whip became a water ball and is gone", !GSandbox.Whip.IsActive());
		double Lowest = 1.0;
		for (double X = 500.0; X <= 900.0; X += 25.0)
		{
			for (double Y = -900.0; Y <= 300.0; Y += 25.0)
			{
				Lowest = KMin(Lowest, GSandbox.World.GetSurfaceTractionMultiplierAt(Ground(X, Y)));
			}
		}
		std::printf("    lowest traction where it landed: %.2f\n", Lowest);
		ExpectTrue("20 kg of water turned the ground to mud (traction < 0.6)", Lowest < 0.6);
	}

	void AirThrowsDummiesNotBoulders()
	{
		SimTest::Section("Air blast: dummies fly, boulders don't");
		GSandbox.Init();
		const int Dummy = FindProp(EArenaProp::Dummy, 0);
		const FVec3 DummyAt = GSandbox.Bodies[Dummy].LocationCm;
		Teleport(DummyAt.X - 600.0, DummyAt.Y, 0.0);
		SetStance(ETechniqueElement::Air);
		Press(ETechniqueSlot::Primary, DummyAt, 0.6);
		const double DummySpeed = GSandbox.Bodies[Dummy].VelocityCmS.Size2D();
		const double DummyMoved = Distance2D(GSandbox.Bodies[Dummy].LocationCm, DummyAt);
		const int Boulder = FindProp(EArenaProp::Boulder, 0);
		const FVec3 BoulderAt = GSandbox.Bodies[Boulder].LocationCm;
		Teleport(BoulderAt.X - 500.0, BoulderAt.Y, 0.0);
		Run(LookAt(BoulderAt), 2.0);
		Press(ETechniqueSlot::Primary, BoulderAt, 0.6);
		const double BoulderSpeed = GSandbox.Bodies[Boulder].VelocityCmS.Size2D();
		std::printf("    60 kg dummy: %.1f m/s (moved %.0f cm); 600 kg boulder: %.2f m/s\n", DummySpeed / 100.0, DummyMoved, BoulderSpeed / 100.0);
		ExpectTrue("the dummy is knocked back", DummyMoved > 30.0);
		ExpectTrue("the boulder barely notices (< 0.3 m/s)", BoulderSpeed < 30.0);
	}

	void FireMeltsIce()
	{
		SimTest::Section("Ground flame melts an ice block");
		GSandbox.Init();
		const int Ice = FindProp(EArenaProp::IceBlock, 0);
		const FVec3 IceAt = GSandbox.Bodies[Ice].AnchorCm;
		Teleport(IceAt.X - 500.0, IceAt.Y, 0.0);
		SetStance(ETechniqueElement::Fire);
		const double ThermalBefore = GSandbox.ThermalWorkJ;
		double HeldFor = 0.0;
		while (GSandbox.Bodies[Ice].bAlive && HeldFor < 40.0)
		{
			Hold(ETechniqueSlot::Utility, IceAt, 4.0);
			HeldFor += 4.0;
			Run(LookAt(IceAt), 2.0); // let chi recover
		}
		std::printf("    melted after %.0f s of ground flame; bender poured %.1f MJ\n", HeldFor, (GSandbox.ThermalWorkJ - ThermalBefore) / 1e6);
		ExpectTrue("the ice block melted", !GSandbox.Bodies[Ice].bAlive);
		ExpectTrue("its meltwater soaked the ground", GSandbox.World.GetSurfaceSaturationAt(IceAt) > 0.05);
	}

	void ChiIsNeverOverdrawn()
	{
		SimTest::Section("Chi");
		GSandbox.Init();
		Teleport(-600.0, -300.0, 0.0);
		SetStance(ETechniqueElement::Earth);
		double Lowest = 1e9;
		FInput Input = LookAt(Ground(0.0, -300.0));
		for (int Frame = 0; Frame < 600; ++Frame)
		{
			Input.bSlotHeld[2] = true; // hold Raise Ground for 10 s
			GSandbox.Advance(Input, Dt);
			Lowest = KMin(Lowest, GSandbox.Player.Chi);
		}
		ExpectTrue("chi never goes negative", Lowest >= 0.0);
		ExpectTrue("terraforming drains chi over time", Lowest < 50.0);
	}

	unsigned long long Checksum()
	{
		unsigned long long Hash = 1469598103934665603ull;
		auto Mix = [&Hash](double V)
		{
			const unsigned long long Bits = __builtin_bit_cast(unsigned long long, V);
			Hash = (Hash ^ Bits) * 1099511628211ull;
		};
		Mix(GSandbox.Player.LocationCm.X);
		Mix(GSandbox.Player.LocationCm.Y);
		Mix(GSandbox.Player.Chi);
		for (int Index = 0; Index < GSandbox.NumBodies; ++Index)
		{
			Mix(GSandbox.Bodies[Index].LocationCm.X);
			Mix(GSandbox.Bodies[Index].LocationCm.Z);
		}
		for (int Index = 0; Index < GSandbox.Terrain.GetSamplesX() * GSandbox.Terrain.GetSamplesY(); Index += 7)
		{
			Mix(GSandbox.Terrain.GetHeightData()[Index]);
		}
		return Hash;
	}

	void DeterministicAndFast(const char* DigestPath)
	{
		SimTest::Section("Determinism and cost");
		unsigned long long Hashes[2];
		double Milliseconds = 0.0;
		int Frames = 0;
		for (int Run = 0; Run < 2; ++Run)
		{
			GSandbox.Init();
			const auto Start = std::chrono::steady_clock::now();
			const double Before = GSandbox.TimeS;
			RunScriptedSession(GSandbox);
			Frames = static_cast<int>((GSandbox.TimeS - Before) * 60.0 + 0.5);
			Milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - Start).count();
			Hashes[Run] = Checksum();
		}
		std::printf("    scripted session: %d frames in %.1f ms = %.3f ms per frame (%d volumes at the end)\n", Frames, Milliseconds,
			Milliseconds / Frames, GSandbox.World.GetNumVolumes());
		ExpectTrue("two runs of the same session are bit-identical", Hashes[0] == Hashes[1]);
		ExpectTrue("a frame costs under 2 ms", Milliseconds / Frames < 2.0);
		if (DigestPath)
		{
			if (FILE* File = std::fopen(DigestPath, "w"))
			{
				std::fprintf(File, "{ \"scripted_session\": %.17g }\n", SandboxDigest(GSandbox));
				std::fclose(File);
			}
		}
	}
}

int main(int ArgCount, char** Args)
{
	TheTrainingGroundLoads();
	WalkRunJump();
	EarthWallBlocksAndConserves();
	RockThrowLeavesACrater();
	TerraformRaisesAndDigs();
	WhipFromThePond();
	WhipPutsOutABrazierFireRelightsIt();
	FreezeCostsLatentHeat();
	WaterBlastMakesMud();
	AirThrowsDummiesNotBoulders();
	FireMeltsIce();
	ChiIsNeverOverdrawn();
	DeterministicAndFast(ArgCount > 1 ? Args[1] : nullptr);
	return SimTest::Finish("3D sandbox");
}
