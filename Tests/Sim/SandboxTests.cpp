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
			Props += GSandbox.Bodies[Index].bAlive && GSandbox.Bodies[Index].Kind == EBodyKind::Prop ? 1 : 0;
		}
		ExpectTrue("every prop of the layout is in the world", Props == GSandbox.Layout.NumProps);
		ExpectTrue("the sparring partner waits at its post", GSandbox.Rival.Body >= 0 && GSandbox.Rival.State == ERivalState::Waiting);
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
		Run(Input, 0.6);
		ExpectTrue("the whip comes back to circle the bender", GSandbox.Whip.GetState() == EWhipState::Holding);

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
		// Lash it: each strike drives the stream through the flame and flings spray into it.
		int Lashes = 0;
		while (Lashes < 6 && GSandbox.Bodies[Brazier].bLit)
		{
			Press(ETechniqueSlot::Primary, Flame, 0.7);
			++Lashes;
		}
		std::printf("    brazier out after %d lash%s\n", Lashes, Lashes == 1 ? "" : "es");
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

	int CountGroundFlames()
	{
		int Count = 0;
		for (int Index = 0; Index < FSandbox::MaxProjectiles; ++Index)
		{
			Count += GSandbox.Projectiles[Index].bAlive && GSandbox.Projectiles[Index].Kind == EProjectileKind::GroundFlame ? 1 : 0;
		}
		return Count;
	}

	/** Draws a whip at the near pond, then lights a ground flame 3.5 m away; true when both are there. */
	bool SetUpWhipAndFlame(const FVec3& Spot)
	{
		GSandbox.Init();
		Teleport(700.0, 450.0, 90.0);
		SetStance(ETechniqueElement::Water);
		Press(ETechniqueSlot::Primary, Ground(700.0, -400.0), 1.2);
		SetStance(ETechniqueElement::Fire);
		Hold(ETechniqueSlot::Utility, Spot, 0.6);
		SetStance(ETechniqueElement::Water);
		return GSandbox.Whip.IsActive() && CountGroundFlames() > 0;
	}

	void WaterPutsOutFire()
	{
		SimTest::Section("Water puts out fire");
		const FVec3 Spot = Ground(700.0, 100.0);
		const FVec3 Target = Spot + FVec3(0.0, 0.0, 40.0);

		// Left alone, the flame keeps burning for a while.
		ExpectTrue("a whip drawn and a ground flame lit", SetUpWhipAndFlame(Spot));
		Run(LookAt(Target), 2.8);
		const bool bBurnsAlone = CountGroundFlames() > 0;
		ExpectTrue("left alone, the flame is still burning after 2.8 s", bBurnsAlone);

		// Lashed, it goes out.
		SetUpWhipAndFlame(Spot);
		int Lashes = 0;
		while (Lashes < 4 && CountGroundFlames() > 0)
		{
			Press(ETechniqueSlot::Primary, Target, 0.7);
			++Lashes;
		}
		std::printf("    the whip put a ground flame out in %d lash%s (2.8 s alone did not)\n", Lashes, Lashes == 1 ? "" : "es");
		ExpectTrue("lashing the flame puts it out", CountGroundFlames() == 0);
	}

	double BodySpeed(int Index) { return Index >= 0 ? GSandbox.Bodies[Index].VelocityCmS.Size() : 0.0; }
	FVec3 Flat(const FVec3& V) { return FVec3(V.X, V.Y, 0.0); }

	void IceDaggersStrike()
	{
		SimTest::Section("Ice Daggers");
		GSandbox.Init();
		const int Dummy = FindProp(EArenaProp::Dummy, 0);
		const FVec3 DummyAt = GSandbox.Bodies[Dummy].LocationCm;
		Teleport(700.0, 450.0, 90.0);
		SetStance(ETechniqueElement::Water);
		Press(ETechniqueSlot::Primary, Ground(700.0, -400.0), 1.2);
		Teleport(DummyAt.X - 700.0, DummyAt.Y, 0.0);
		Run(LookAt(DummyAt), 0.6);
		const double WhipBefore = GSandbox.Whip.GetMassKg(GSandbox.World);
		const double ColdBefore = GSandbox.ThermalWorkJ;
		FInput Input = LookAt(DummyAt);
		Input.bSlotHeld[static_cast<int>(ETechniqueSlot::Signature)] = true;
		GSandbox.Advance(Input, Dt);
		Input.bSlotHeld[static_cast<int>(ETechniqueSlot::Signature)] = false;
		int Shards = 0;
		for (int Frame = 0; Frame < 20 && Shards == 0; ++Frame)
		{
			GSandbox.Advance(Input, Dt);
			Shards = GSandbox.CountProjectiles(EProjectileKind::IceShard);
		}
		const double Taken = WhipBefore - GSandbox.Whip.GetMassKg(GSandbox.World);
		const double Cold = GSandbox.ThermalWorkJ - ColdBefore;
		std::printf("    %d daggers from %.2f kg of the whip's water; %.0f kJ of cold\n", Shards, Taken, Cold / 1000.0);
		ExpectTrue("five daggers fly", Shards == 5);
		ExpectNear("their water came out of the whip (kg)", Taken, 2.5, 0.01);
		// The whip's water sits near 15 C (it trades heat with the air), hence 1%.
		ExpectNear("freezing them cost m (c dT + L_f + c_ice dT) (kJ)", Cold / 1000.0, HeatToMakeIce(Taken, 288.15, 263.15) / 1000.0, 10.0);
		double Peak = 0.0;
		for (int Frame = 0; Frame < 40; ++Frame)
		{
			GSandbox.Advance(Input, Dt);
			Peak = KMax(Peak, BodySpeed(Dummy) + 30.0 * (std::fabs(GSandbox.Bodies[Dummy].TiltRate[0]) + std::fabs(GSandbox.Bodies[Dummy].TiltRate[1])));
		}
		ExpectTrue("they strike the dummy", Peak > 20.0);
		ExpectTrue("and shatter", GSandbox.CountProjectiles(EProjectileKind::IceShard) == 0);
	}

	void EarthquakeThrowsByMass()
	{
		SimTest::Section("Earthquake");
		GSandbox.Init();
		Teleport(580.0, -330.0, 0.0);
		SetStance(ETechniqueElement::Earth);
		const int Stone = FindProp(EArenaProp::Stone, 0);
		const int FarDummy = FindProp(EArenaProp::Dummy, 4);
		Press(ETechniqueSlot::Signature, Ground(1500.0, -330.0), 0.0);
		double StoneUp = 0.0, StoneSpeed = 0.0;
		for (int Frame = 0; Frame < 30; ++Frame)
		{
			GSandbox.Advance(LookAt(Ground(1500.0, -330.0)), Dt);
			StoneUp = KMax(StoneUp, GSandbox.Bodies[Stone].VelocityCmS.Z);
			StoneSpeed = KMax(StoneSpeed, BodySpeed(Stone));
		}
		std::printf("    stones thrown at up to %.1f m/s; a dummy 25 m away: %.2f m/s\n", StoneSpeed / 100.0, BodySpeed(FarDummy) / 100.0);
		ExpectTrue("the stomp throws nearby stones up and out (> 4 m/s)", StoneUp > 200.0 && StoneSpeed > 400.0);
		ExpectTrue("nothing outside its reach moves", BodySpeed(FarDummy) < 1.0);
		ExpectTrue("it says so", HasMessage("Earthquake"));
	}

	void FireRingAndJetDash()
	{
		SimTest::Section("Ring of Fire and Jet Dash");
		GSandbox.Init();
		Teleport(-600.0, -300.0, 0.0);
		SetStance(ETechniqueElement::Fire);
		Press(ETechniqueSlot::Signature, Ground(400.0, -300.0), 0.25);
		const int Flames = GSandbox.CountProjectiles(EProjectileKind::Fire);
		ExpectTrue("sixteen flames burst out in a ring", Flames == 16);
		Run(LookAt(Ground(400.0, -300.0)), 1.0);
		const FVec3 Start = GSandbox.Player.LocationCm;
		const int FlamesBefore = GSandbox.CountProjectiles(EProjectileKind::Fire);
		Press(ETechniqueSlot::Special, Ground(2000.0, -300.0), 0.5);
		const double Moved = Flat(GSandbox.Player.LocationCm - Start).Size();
		std::printf("    ring: %d flames; jet dash carried the bender %.1f m in 0.5 s\n", Flames, Moved / 100.0);
		ExpectTrue("the jets carry the bender forward (> 3.5 m in 0.5 s)", Moved > 350.0);
		ExpectTrue("leaving a trail of flame", GSandbox.CountProjectiles(EProjectileKind::Fire) > FlamesBefore);
	}

	void AirScooterAndTornado()
	{
		SimTest::Section("Air Scooter and Tornado");
		GSandbox.Init();
		Teleport(-600.0, -300.0, 0.0);
		SetStance(ETechniqueElement::Air);
		FInput Ride = LookAt(GSandbox.Player.LocationCm + FVec3(3000.0, 0.0, 0.0));
		Ride.bSlotHeld[static_cast<int>(ETechniqueSlot::Special)] = true;
		Ride.MoveForward = 1.0;
		Run(Ride, 1.6);
		const double RideSpeed = Flat(GSandbox.Player.VelocityCmS).Size();
		Ride.bSlotHeld[static_cast<int>(ETechniqueSlot::Special)] = false;
		Run(Ride, 1.0);
		const double AfterSpeed = Flat(GSandbox.Player.VelocityCmS).Size();
		std::printf("    air scooter: %.1f m/s (running: %.1f m/s after letting go)\n", RideSpeed / 100.0, AfterSpeed / 100.0);
		ExpectTrue("riding the air scooter is fast (> 12 m/s)", RideSpeed > 1200.0);
		ExpectTrue("letting go returns to running speed", AfterSpeed < 600.0);

		// A tornado over the stones lifts them; a fire blast thrown into it makes a fire tornado.
		GSandbox.Init();
		Teleport(-300.0, -500.0, 0.0);
		SetStance(ETechniqueElement::Air);
		const int Stone = FindProp(EArenaProp::Stone, 0);
		const double StoneStartZ = GSandbox.Bodies[Stone].LocationCm.Z;
		Press(ETechniqueSlot::Signature, Ground(570.0, -500.0), 0.0);
		double Highest = StoneStartZ;
		for (int Frame = 0; Frame < 120; ++Frame)
		{
			GSandbox.Advance(LookAt(Ground(570.0, -500.0)), Dt);
			Highest = KMax(Highest, GSandbox.Bodies[Stone].LocationCm.Z);
		}
		ExpectTrue("a tornado spins up", GSandbox.Tornado.bActive);
		SetStance(ETechniqueElement::Fire);
		Press(ETechniqueSlot::Primary, GSandbox.Tornado.CenterCm + FVec3(0.0, 0.0, 120.0), 0.6);
		const double FireKg = GSandbox.Tornado.FireKg;
		std::printf("    tornado lifted a stone %.1f m; %.2f kg of flame caught in it\n", (Highest - StoneStartZ) / 100.0, FireKg);
		ExpectTrue("it lifts the stones (> 1 m)", Highest - StoneStartZ > 100.0);
		ExpectTrue("fire thrown in makes a fire tornado", FireKg > 0.05);
	}


	// ---------------------------------------------------------------------------------------------------- Interactive props

	/** Stands Distance cm from a prop, between it and the ring's centre, facing it. */
	void StandBefore(const FBody& Body, double DistanceCm)
	{
		const FVec3 Flat2(Body.AnchorCm.X, Body.AnchorCm.Y, 0.0);
		const FVec3 Toward = Flat2.Size() > 1.0 ? Flat2.GetSafeNormal() : FVec3(1.0, 0.0, 0.0);
		const FVec3 Spot = Flat2 - Toward * DistanceCm;
		Teleport(Spot.X, Spot.Y, KAtan2(Toward.Y, Toward.X) * 180.0 / Pi);
	}

	FVec3 BannerCloth(const FBody& Banner)
	{
		return Banner.AnchorCm + FVec3(0.0, 0.0, 0.5 * (BannerClothTopCm + BannerClothBottomCm));
	}

	/** Draws a whip at the near pond (the whip follows the bender wherever it is teleported). */
	void DrawWhipAtPond()
	{
		Teleport(700.0, 450.0, 90.0);
		SetStance(ETechniqueElement::Water);
		Press(ETechniqueSlot::Primary, Ground(700.0, -400.0), 1.2);
	}

	void BannersBurnAway()
	{
		SimTest::Section("Banners burn; water saves them");
		GSandbox.Init();
		int Banners = 0;
		while (FindProp(EArenaProp::Banner, Banners) >= 0)
		{
			++Banners;
		}
		ExpectTrue("banners stand round the ring", Banners >= 5);
		const int Banner = FindProp(EArenaProp::Banner, 0);
		StandBefore(GSandbox.Bodies[Banner], 700.0);
		SetStance(ETechniqueElement::Fire);
		const FVec3 Cloth = BannerCloth(GSandbox.Bodies[Banner]);
		Run(LookAt(Cloth), 0.6);
		Press(ETechniqueSlot::Primary, Cloth, 0.8);
		ExpectTrue("one fire blast sets the cloth alight", GSandbox.Bodies[Banner].Burn.bBurning);
		ExpectTrue("and says so", HasMessage("Banner caught fire"));
		Run(LookAt(Cloth), 8.0);
		const FBody& Burnt = GSandbox.Bodies[Banner];
		std::printf("    banner burnt away after 7 s, releasing %.1f MJ\n", Burnt.Burn.ReleasedJ / 1.0e6);
		ExpectTrue("left burning, the cloth burns away", Burnt.bAlive && Burnt.Burn.bBurntOut);

		// Another banner, lit, then lashed with water before it is gone. (Light it first: a fire blast thrown
		// through a circling water whip boils away before it gets anywhere.)
		const int Second = FindProp(EArenaProp::Banner, 1);
		StandBefore(GSandbox.Bodies[Second], 700.0);
		const FVec3 Cloth2 = BannerCloth(GSandbox.Bodies[Second]);
		Run(LookAt(Cloth2), 0.6);
		Press(ETechniqueSlot::Primary, Cloth2, 0.6);
		ExpectTrue("the second banner catches", GSandbox.Bodies[Second].Burn.bBurning);
		DrawWhipAtPond();
		StandBefore(GSandbox.Bodies[Second], 420.0);
		Run(LookAt(Cloth2), 0.3);
		int Lashes = 0;
		while (Lashes < 4 && GSandbox.Bodies[Second].Burn.bBurning)
		{
			Press(ETechniqueSlot::Primary, Cloth2 - FVec3(0.0, 0.0, 60.0), 0.7);
			++Lashes;
		}
		const double Saved = 1.0 - GSandbox.Bodies[Second].Burn.GetBurntFraction(GetArenaPropSpec(EArenaProp::Banner).Combustion);
		std::printf("    put out in %d lash%s with %.0f%% of the cloth left\n", Lashes, Lashes == 1 ? "" : "es", Saved * 100.0);
		ExpectTrue("lashing it with water puts it out", !GSandbox.Bodies[Second].Burn.bBurning && !GSandbox.Bodies[Second].Burn.bBurntOut);
		ExpectTrue("the message says water did it", HasMessage("Water put out the Banner"));
	}

	void FireSpreadsThroughTheYard()
	{
		SimTest::Section("Fire spreads from straw to crates");
		GSandbox.Init();
		const int Bale = FindProp(EArenaProp::StrawBale, 0);
		const FVec3 BaleAt = GSandbox.Bodies[Bale].LocationCm;
		Teleport(BaleAt.X - 650.0, BaleAt.Y + 250.0, -20.0);
		SetStance(ETechniqueElement::Fire);
		Run(LookAt(BaleAt), 0.5);
		Press(ETechniqueSlot::Primary, BaleAt, 0.8);
		ExpectTrue("a fire blast lights the straw", GSandbox.Bodies[Bale].Burn.bBurning);
		Run(LookAt(BaleAt), 9.0);
		int Caught = 0;
		int Yard = 0;
		for (int Index = 0; Index < GSandbox.NumBodies; ++Index)
		{
			const FBody& Body = GSandbox.Bodies[Index];
			const bool bYard = (Body.Prop == EArenaProp::StrawBale || Body.Prop == EArenaProp::Crate) && Body.Kind == EBodyKind::Prop
				&& Distance(Body.AnchorCm, BaleAt) < 600.0;
			if (bYard)
			{
				++Yard;
				// A crate that burnt away is gone (not alive) but it did catch.
				Caught += (Body.Burn.bBurning || Body.Burn.bBurntOut || !Body.bAlive) ? 1 : 0;
			}
		}
		std::printf("    one blast: %d of the yard's %d straw bales and crates caught within 9 s\n", Caught, Yard);
		ExpectTrue("the fire spreads to its neighbours", Caught >= 3);
	}

	void CratesSmashBarrelsBurst()
	{
		SimTest::Section("Crates smash, barrels burst");
		GSandbox.Init();
		int Crates = 0;
		while (FindProp(EArenaProp::Crate, Crates) >= 0)
		{
			++Crates;
		}
		const int Crate = FindProp(EArenaProp::Crate, 3);
		const FVec3 CrateAt = GSandbox.Bodies[Crate].LocationCm;
		Teleport(CrateAt.X - 620.0, CrateAt.Y + 60.0, 0.0);
		SetStance(ETechniqueElement::Earth);
		Run(LookAt(CrateAt), 0.4);
		Press(ETechniqueSlot::Primary, CrateAt + FVec3(0.0, 0.0, 20.0), 2.0);
		int After = 0;
		while (FindProp(EArenaProp::Crate, After) >= 0)
		{
			++After;
		}
		ExpectTrue("a thrown rock smashes a crate", After < Crates && HasMessage("Crate smashed"));
		// An earthquake in the fire yard smashes the crates there.
		const FVec3 Pile = GSandbox.Bodies[FindProp(EArenaProp::Crate, 0)].LocationCm;
		Teleport(Pile.X - 120.0, Pile.Y + 40.0, 0.0);
		Run(LookAt(Pile), 0.3);
		Press(ETechniqueSlot::Signature, Pile, 1.5);
		int AfterQuake = 0;
		while (FindProp(EArenaProp::Crate, AfterQuake) >= 0)
		{
			++AfterQuake;
		}
		std::printf("    crates: %d -> %d after a rock -> %d after an earthquake\n", Crates, After, AfterQuake);
		ExpectTrue("an earthquake smashes the crates beside it", AfterQuake < After);

		// A fresh field for the barrel (a rock that smashed the crates may have rolled on into one).
		GSandbox.Init();
		const int Barrel = FindProp(EArenaProp::WaterBarrel, 0);
		const FVec3 BarrelAt = GSandbox.Bodies[Barrel].LocationCm;
		const FVec3 BarrelGround = Ground(BarrelAt.X, BarrelAt.Y);
		const auto WettestRound = [&]()
		{
			double Wettest = 0.0;
			for (int Step = 0; Step < 24; ++Step)
			{
				for (double Radius = 0.0; Radius <= 250.0; Radius += 50.0)
				{
					const double Angle = 2.0 * Pi * Step / 24.0;
					Wettest = KMax(Wettest, GSandbox.World.GetSurfaceSaturationAt(Ground(BarrelGround.X + KCos(Angle) * Radius, BarrelGround.Y + KSin(Angle) * Radius)));
				}
			}
			return Wettest;
		};
		const double DryBefore = WettestRound();
		Teleport(BarrelAt.X - 500.0, BarrelAt.Y + 80.0, 0.0);
		SetStance(ETechniqueElement::Earth);
		Run(LookAt(BarrelAt), 0.4);
		Press(ETechniqueSlot::Primary, BarrelAt + FVec3(0.0, 0.0, 20.0), 2.5);
		const double WetAfter = WettestRound();
		std::printf("    a thrown rock burst the barrel: ground saturation %.2f -> %.2f\n", DryBefore, WetAfter);
		ExpectTrue("a thrown rock bursts a water barrel", FindProp(EArenaProp::WaterBarrel, 2) < 0 && HasMessage("The barrel burst"));
		ExpectTrue("its water soaks the ground round it", WetAfter > DryBefore + 0.1);
	}

	void LanternsAndBarrels()
	{
		SimTest::Section("Lanterns light; barrels are a water source");
		GSandbox.Init();
		const int Lantern = FindProp(EArenaProp::Lantern, 0);
		ExpectTrue("lanterns stand unlit", Lantern >= 0 && !GSandbox.Bodies[Lantern].Burn.bBurning);
		const FVec3 Wick = GSandbox.Bodies[Lantern].AnchorCm + FVec3(0.0, 0.0, GetArenaPropSpec(EArenaProp::Lantern).FlameHeightCm);
		StandBefore(GSandbox.Bodies[Lantern], -420.0);
		SetStance(ETechniqueElement::Fire);
		Run(LookAt(Wick), 0.4);
		Press(ETechniqueSlot::Primary, Wick, 0.6);
		ExpectTrue("a fire blast lights a lantern", GSandbox.Bodies[Lantern].Burn.bBurning && HasMessage("Lantern lit"));
		Run(LookAt(Wick), 20.0);
		ExpectTrue("and it stays lit", GSandbox.Bodies[Lantern].Burn.bBurning);

		// Far from both ponds, a barrel gives a waterbender enough for a whip.
		const int Barrel = FindProp(EArenaProp::WaterBarrel, 2);
		const FVec3 BarrelAt = GSandbox.Bodies[Barrel].LocationCm;
		Teleport(BarrelAt.X + 250.0, BarrelAt.Y, 180.0);
		SetStance(ETechniqueElement::Water);
		Press(ETechniqueSlot::Primary, BarrelAt + FVec3(-800.0, 0.0, 0.0), 1.2);
		std::printf("    whip drawn from a barrel: %.0f kg left in it\n", GSandbox.Bodies[Barrel].WaterKg);
		ExpectTrue("a whip can be drawn from a water barrel", GSandbox.Whip.IsActive() && HasMessage("from the barrel"));
		ExpectNear("the barrel gives up 20 kg", GSandbox.Bodies[Barrel].WaterKg, 40.0, 0.01);
	}

	void DummiesBurnAndChar()
	{
		SimTest::Section("Dummies burn and char");
		GSandbox.Init();
		const int Dummy = FindProp(EArenaProp::Dummy, 0);
		const FVec3 DummyAt = GSandbox.Bodies[Dummy].LocationCm;
		Teleport(DummyAt.X - 700.0, DummyAt.Y, 0.0);
		SetStance(ETechniqueElement::Fire);
		Run(LookAt(DummyAt), 0.4);
		int Blasts = 0;
		while (Blasts < 6 && !GSandbox.Bodies[Dummy].Burn.bBurning)
		{
			Press(ETechniqueSlot::Primary, DummyAt, 0.55);
			++Blasts;
		}
		std::printf("    the dummy caught after %d fire blast%s\n", Blasts, Blasts == 1 ? "" : "s");
		ExpectTrue("fire blasts set a dummy alight", GSandbox.Bodies[Dummy].Burn.bBurning);
		Run(LookAt(DummyAt), 13.0);
		ExpectTrue("it burns down to char, knocked out", GSandbox.Bodies[Dummy].bAlive && GSandbox.Bodies[Dummy].Burn.bBurntOut && GSandbox.Bodies[Dummy].KnockoutS > 0.0);
		Run(LookAt(DummyAt), FSandbox::KnockoutSeconds + 0.5);
		ExpectTrue("then gets up again, whole", GSandbox.Bodies[Dummy].KnockoutS <= 0.0 && !GSandbox.Bodies[Dummy].Burn.bBurntOut);
	}


	void DummiesTakeHitsAndGetUp()
	{
		SimTest::Section("Dummies: health, knockout, back on their feet");
		GSandbox.Init();
		const int Dummy = FindProp(EArenaProp::Dummy, 0);
		const FVec3 Home = GSandbox.Bodies[Dummy].LocationCm;
		Teleport(Home.X - 800.0, Home.Y, 0.0);
		SetStance(ETechniqueElement::Air);
		Run(LookAt(Home), 0.4);
		Press(ETechniqueSlot::Primary, Home, 1.0);
		const double AfterAir = GSandbox.Bodies[Dummy].Health;
		std::printf("    an air blast: %.0f damage\n", FSandbox::DummyMaxHealth - AfterAir);
		ExpectTrue("an air blast hurts a little", AfterAir < FSandbox::DummyMaxHealth && AfterAir > 60.0);

		SetStance(ETechniqueElement::Earth);
		Run(LookAt(Home), 1.2);
		Press(ETechniqueSlot::Primary, GSandbox.Bodies[Dummy].LocationCm + FVec3(0.0, 0.0, 30.0), 1.6);
		ExpectTrue("a thrown rock knocks it out", GSandbox.Bodies[Dummy].KnockoutS > 0.0 && GSandbox.Bodies[Dummy].Health <= 0.0);
		ExpectTrue("and says so", HasMessage("Dummy knocked out"));
		const double Tilt = KAbs(GSandbox.Bodies[Dummy].Tilt[0]) + KAbs(GSandbox.Bodies[Dummy].Tilt[1]);
		ExpectTrue("knocked out, it falls over", Tilt > 1.0);
		Run(LookAt(Home), FSandbox::KnockoutSeconds + 0.5);
		const FBody& Back = GSandbox.Bodies[Dummy];
		ExpectTrue("after a few seconds it stands up again, whole", Back.KnockoutS <= 0.0 && Back.Health == FSandbox::DummyMaxHealth
			&& Distance(Back.LocationCm, Home) < 400.0);

		// Ice daggers cut: five of them take most of a dummy's health.
		DrawWhipAtPond();
		const int Target = FindProp(EArenaProp::Dummy, 1);
		const FVec3 TargetAt = GSandbox.Bodies[Target].LocationCm;
		Teleport(TargetAt.X - 450.0, TargetAt.Y, 0.0);
		Run(LookAt(TargetAt), 0.5);
		Press(ETechniqueSlot::Signature, TargetAt + FVec3(0.0, 0.0, 20.0), 1.2);
		const double Cut = FSandbox::DummyMaxHealth - GSandbox.Bodies[Target].Health + (GSandbox.Bodies[Target].KnockoutS > 0.0 ? 100.0 : 0.0);
		std::printf("    five ice daggers: %.0f damage\n", Cut);
		ExpectTrue("ice daggers do real damage (> 50)", Cut > 50.0);
	}

	FVec3 RivalChest()
	{
		return GSandbox.Bodies[GSandbox.Rival.Body].LocationCm + FVec3(0.0, 0.0, 20.0);
	}

	int CountRivalFlames()
	{
		int Count = 0;
		for (const FProjectile& Projectile : GSandbox.Projectiles)
		{
			Count += Projectile.bAlive && Projectile.Owner == 1 ? 1 : 0;
		}
		return Count;
	}

	/** Challenges the sparring partner with a fire blast from where the player starts and waits for the bow. */
	bool ChallengeRival()
	{
		SetStance(ETechniqueElement::Fire);
		Run(LookAt(RivalChest()), 0.5);
		Press(ETechniqueSlot::Primary, RivalChest(), 0.1);
		for (int Frame = 0; Frame < 180 && GSandbox.Rival.State == ERivalState::Waiting; ++Frame)
		{
			GSandbox.Advance(LookAt(RivalChest()), Dt);
		}
		return GSandbox.Rival.State != ERivalState::Waiting;
	}

	/** True when one of the sparring partner's flames will reach the player within Seconds. */
	bool FlameIncoming(double Seconds)
	{
		const FVec3 Chest = GSandbox.Player.LocationCm + FVec3(0.0, 0.0, 110.0);
		for (const FProjectile& Projectile : GSandbox.Projectiles)
		{
			if (!Projectile.bAlive || Projectile.Owner != 1 || Projectile.bStruck || Projectile.bLanded)
			{
				continue;
			}
			const FVec3 Ahead = Projectile.LocationCm + Projectile.VelocityCmS * Seconds;
			if (Distance(ClosestPointOnSegment(Chest, Projectile.LocationCm, Ahead), Chest) < 90.0)
			{
				return true;
			}
		}
		return false;
	}

	void SparringPartnerDuels()
	{
		SimTest::Section("Sparring partner: waits, is challenged, fights");
		GSandbox.Init();
		const FVec3 Post = GSandbox.Bodies[GSandbox.Rival.Body].LocationCm;
		Run(LookAt(GSandbox.Player.LocationCm + FVec3(0.0, -1000.0, 0.0)), 3.0);
		ExpectTrue("left alone, it waits at its post", GSandbox.Rival.State == ERivalState::Waiting && CountRivalFlames() == 0
			&& Distance(GSandbox.Bodies[GSandbox.Rival.Body].LocationCm, Post) < 50.0);
		ExpectTrue("a fire blast challenges it", ChallengeRival());
		ExpectTrue("and the duel is announced", HasMessage("Duel!"));
		ExpectTrue("the challenge does not hurt it", GSandbox.Bodies[GSandbox.Rival.Body].Health == FSandbox::DummyMaxHealth);
		bool bAttacked = false;
		for (int Frame = 0; Frame < 60 * 8 && !bAttacked; ++Frame)
		{
			GSandbox.Advance(LookAt(RivalChest()), Dt);
			bAttacked = CountRivalFlames() > 0;
		}
		ExpectTrue("it attacks with fire", bAttacked);
		Run(LookAt(RivalChest()), 15.0);
		std::printf("    standing still for 15 s: %.0f health left\n", GSandbox.Player.Health);
		ExpectTrue("its fire hurts", GSandbox.Player.Health < GSandbox.Player.MaxHealth);
		ExpectTrue("it circles instead of standing still", Distance(GSandbox.Bodies[GSandbox.Rival.Body].LocationCm, Post) > 100.0);
	}

	void GuardBlocksParryReturns()
	{
		SimTest::Section("Guard blocks, a timed guard parries");
		GSandbox.Init();
		ChallengeRival();
		// Guard held the whole time: blocks soak most of each flame and cost stamina.
		int Blocks = 0;
		double Lost = 0.0;
		for (int Frame = 0; Frame < 60 * 12; ++Frame)
		{
			FInput Input = LookAt(RivalChest());
			Input.bGuard = true;
			const double Before = GSandbox.Player.Health;
			GSandbox.Advance(Input, Dt);
			for (int Event = 0; Event < GSandbox.NumFrameEvents; ++Event)
			{
				Blocks += GSandbox.FrameEvents[Event].Effect == ESandboxEffect::Blocked ? 1 : 0;
			}
			Lost += KMax(Before - GSandbox.Player.Health, 0.0);
		}
		std::printf("    guarding 12 s: %d blocks, %.1f health lost\n", Blocks, Lost);
		ExpectTrue("the guard blocks its flames", Blocks > 0);
		ExpectTrue("a block lets little through", Lost <= Blocks * FSandbox::FlameStrikeDamage * FSandbox::BlockDamageScale + 1e-6);
		ExpectTrue("blocking costs stamina", GSandbox.Player.Stamina < GSandbox.Player.MaxStamina || Blocks == 0);

		// Guard raised just as a flame arrives: it flies back and hurts the sender.
		Run(LookAt(RivalChest()), 0.5);
		bool bParried = false;
		bool bGuard = false;
		const double RivalBefore = GSandbox.Bodies[GSandbox.Rival.Body].Health;
		for (int Frame = 0; Frame < 60 * 20 && !bParried; ++Frame)
		{
			if (!bGuard && FlameIncoming(0.08))
			{
				bGuard = true;
			}
			FInput Input = LookAt(RivalChest());
			Input.bGuard = bGuard;
			GSandbox.Advance(Input, Dt);
			for (int Event = 0; Event < GSandbox.NumFrameEvents; ++Event)
			{
				bParried = bParried || GSandbox.FrameEvents[Event].Effect == ESandboxEffect::Parried;
			}
			if (bGuard && !GSandbox.Player.bGuarding && GSandbox.Player.GuardCooldownS <= 0.0)
			{
				bGuard = false;
			}
			if (bGuard && GSandbox.Player.GuardTimeS > 0.3)
			{
				bGuard = false;
			}
		}
		ExpectTrue("a guard raised as the flame arrives parries it", bParried);
		ExpectTrue("and says so", HasMessage("Perfect guard"));
		bool bReturned = false;
		for (const FProjectile& Projectile : GSandbox.Projectiles)
		{
			bReturned = bReturned || (Projectile.bAlive && Projectile.Owner == 0 && Projectile.Kind == EProjectileKind::Fire);
		}
		ExpectTrue("the parried flame now flies for the player", bReturned);
		Run(LookAt(RivalChest()), 1.0);
		std::printf("    sparring partner after the parry: %.0f health (was %.0f)\n", GSandbox.Bodies[GSandbox.Rival.Body].Health, RivalBefore);
		ExpectTrue("it strikes the sparring partner (or its guard)", GSandbox.Bodies[GSandbox.Rival.Body].Health < RivalBefore
			|| GSandbox.Rival.State == ERivalState::Guarding || GSandbox.Rival.State == ERivalState::Dodging);
	}

	void DuelsAreWonAndLost()
	{
		SimTest::Section("Duels are won and lost; both get back up");
		GSandbox.Init();
		ChallengeRival();
		SetStance(ETechniqueElement::Air);
		int Blasts = 0;
		for (int Frame = 0; Frame < 60 * 40 && GSandbox.Rival.State != ERivalState::Down; ++Frame)
		{
			FInput Input = LookAt(RivalChest());
			// An air blast whenever ready.
			Input.bSlotHeld[0] = Frame % 30 == 0;
			Blasts += Input.bSlotHeld[0] ? 1 : 0;
			GSandbox.Advance(Input, Dt);
		}
		std::printf("    knocked out with %d air blasts\n", Blasts);
		ExpectTrue("air blasts knock the sparring partner out", GSandbox.Rival.State == ERivalState::Down && GSandbox.Rival.PlayerWins == 1);
		ExpectTrue("you win the duel", HasMessage("You win the duel"));
		Run(LookAt(RivalChest()), FSandbox::RivalDownSeconds + 6.0);
		ExpectTrue("it gets up and walks back to its post", GSandbox.Rival.State == ERivalState::Waiting);
		ExpectTrue("whole again", GSandbox.Bodies[GSandbox.Rival.Body].Health == FSandbox::DummyMaxHealth);

		// Standing still without guarding loses.
		GSandbox.Init();
		ChallengeRival();
		for (int Frame = 0; Frame < 60 * 120 && GSandbox.Player.DownS <= 0.0; ++Frame)
		{
			GSandbox.Advance(LookAt(RivalChest()), Dt);
		}
		std::printf("    knocked down after %.0f s of standing still\n", GSandbox.TimeS);
		ExpectTrue("standing still, the player is knocked down", GSandbox.Player.DownS > 0.0 && GSandbox.Rival.RivalWins == 1);
		ExpectTrue("and the duel is over", !GSandbox.IsDuelActive());
		// Thrown back by the blow, then flat on the ground: pushing forward does nothing.
		Run(LookAt(RivalChest()), 0.8);
		FInput Push = LookAt(RivalChest());
		Push.MoveForward = 1.0;
		const FVec3 Lying = GSandbox.Player.LocationCm;
		Run(Push, 1.0);
		ExpectTrue("down, the player cannot move", Distance(Flat(GSandbox.Player.LocationCm), Flat(Lying)) < 60.0);
		Run(LookAt(RivalChest()), FSandbox::PlayerDownSeconds - 1.7);
		ExpectTrue("up again with full health", GSandbox.Player.DownS <= 0.0 && GSandbox.Player.Health == GSandbox.Player.MaxHealth);
		Run(LookAt(RivalChest()), 8.0);
		ExpectTrue("the sparring partner goes back to its post", GSandbox.Rival.State == ERivalState::Waiting);
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
	WaterPutsOutFire();
	IceDaggersStrike();
	EarthquakeThrowsByMass();
	FireRingAndJetDash();
	AirScooterAndTornado();
	BannersBurnAway();
	FireSpreadsThroughTheYard();
	CratesSmashBarrelsBurst();
	LanternsAndBarrels();
	DummiesBurnAndChar();
	DummiesTakeHitsAndGetUp();
	SparringPartnerDuels();
	GuardBlocksParryReturns();
	DuelsAreWonAndLost();
	FreezeCostsLatentHeat();
	WaterBlastMakesMud();
	AirThrowsDummiesNotBoulders();
	FireMeltsIce();
	ChiIsNeverOverdrawn();
	DeterministicAndFast(ArgCount > 1 ? Args[1] : nullptr);
	return SimTest::Finish("3D sandbox");
}
