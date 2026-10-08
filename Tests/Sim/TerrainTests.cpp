// Deformable terrain (earthbending) checks. Build and run with Tests/run_all.sh.

#include "SimTestHarness.h"
#include "Sim/BendingTerrain.h"

#include <cmath>
#include <cstdio>

using namespace BendingSim;

namespace
{
	FTerrain GTerrain;

	void ResetFlat()
	{
		GTerrain = FTerrain();
		GTerrain.Init(121, 121, 40.0, FVec3(-2400.0, -2400.0, 0.0), 0.0);
	}

	void FlatGroundSamplesFlat()
	{
		SimTest::Section("Flat ground");
		ResetFlat();
		ExpectNear("height anywhere", GTerrain.GetHeightAt(123.4, -987.6), 0.0, 1e-12);
		const FVec3 N = GTerrain.GetNormalAt(10.0, 20.0);
		ExpectNear("normal is up", N.Z, 1.0, 1e-12);
		ExpectNear("soil above bedrock = area * depth", GTerrain.GetTotalSoilVolumeM3(), 121.0 * 121.0 * 0.16 * 3.0, 1e-6);
	}

	void PlanesAreReproducedExactly()
	{
		SimTest::Section("Interpolation follows the shared triangulation");
		ResetFlat();
		// Any plane is reproduced exactly by linear triangles, whatever the diagonal.
		for (int Y = 0; Y < GTerrain.GetSamplesY(); ++Y)
		{
			for (int X = 0; X < GTerrain.GetSamplesX(); ++X)
			{
				const FVec3 P = GTerrain.GetSampleLocation(X, Y);
				GTerrain.SetSampleHeight(X, Y, 0.05 * P.X - 0.03 * P.Y + 30.0);
			}
		}
		double Worst = 0.0;
		for (double X = -2300.0; X < 2300.0; X += 37.3)
		{
			for (double Y = -2300.0; Y < 2300.0; Y += 41.9)
			{
				Worst = KMax(Worst, std::fabs(GTerrain.GetHeightAt(X, Y) - (0.05 * X - 0.03 * Y + 30.0)));
			}
		}
		ExpectTrue("plane reproduced to < 1e-9 cm", Worst < 1e-9);

		// A single raised sample: the triangle split runs from (x, y) to (x + 1, y + 1).
		ResetFlat();
		GTerrain.SetSampleHeight(60, 60, 100.0);
		const FVec3 S = GTerrain.GetSampleLocation(60, 60);
		ExpectNear("at the raised sample", GTerrain.GetHeightAt(S.X, S.Y), 100.0, 1e-9);
		// Point inside cell (59, 59), upper triangle: only (60, 60) of its corners is raised -> along the diagonal it ramps.
		ExpectNear("halfway along the diagonal", GTerrain.GetHeightAt(S.X - 20.0, S.Y - 20.0), 50.0, 1e-9);
		// Inside cell (60, 59) off-diagonal corner (61, 59) is flat: point near (61, 59) stays low.
		ExpectNear("near a flat corner", GTerrain.GetHeightAt(S.X + 38.0, S.Y - 38.0), 0.0, 1e-9);
	}

	void RaycastFindsTheSurface()
	{
		SimTest::Section("Raycast");
		ResetFlat();
		for (int Y = 0; Y < GTerrain.GetSamplesY(); ++Y)
		{
			for (int X = 0; X < GTerrain.GetSamplesX(); ++X)
			{
				const FVec3 P = GTerrain.GetSampleLocation(X, Y);
				GTerrain.SetSampleHeight(X, Y, 0.2 * P.X);
			}
		}
		FVec3 Hit;
		const FVec3 Start(-500.0, 0.0, 800.0);
		const FVec3 Direction = FVec3(1.0, 0.2, -0.6).GetSafeNormal();
		const bool bHit = GTerrain.Raycast(Start, Direction, 5000.0, Hit);
		ExpectTrue("ray hits the slope", bHit);
		ExpectNear("hit lies on the surface (cm)", Hit.Z - GTerrain.GetHeightAt(Hit.X, Hit.Y), 0.0, 1e-3);
		// Analytic: z0 + t*dz = 0.2*(x0 + t*dx).
		const double T = (Start.Z - 0.2 * Start.X) / (0.2 * Direction.X - Direction.Z);
		ExpectNear("hit distance matches the analytic plane (cm)", Distance(Start, Hit), T, 1e-3);
		FVec3 Miss;
		ExpectTrue("ray pointing up misses", !GTerrain.Raycast(Start, FVec3(0.0, 0.0, 1.0), 5000.0, Miss));
	}

	void EarthbendingConservesSoil()
	{
		SimTest::Section("Earthbending conserves soil");
		ResetFlat();
		const double Before = GTerrain.GetTotalSoilVolumeM3();
		double Moved = 0.0;
		unsigned int Seed = 12345;
		auto Random = [&Seed](double Lo, double Hi)
		{
			Seed = Seed * 1664525u + 1013904223u;
			return Lo + (Hi - Lo) * ((Seed >> 8) / 16777216.0);
		};
		for (int Op = 0; Op < 300; ++Op)
		{
			const FVec3 C(Random(-2000.0, 2000.0), Random(-2000.0, 2000.0), 0.0);
			switch (Op % 3)
			{
			case 0: // pillar out of the ring around it
				Moved += GTerrain.MoveSoil(FTerrainBrush::Ring(C, 160.0, 260.0, 60.0), FTerrainBrush::Disc(C, 80.0, 60.0), Random(0.1, 3.0)).VolumeM3;
				break;
			case 1: // wall out of the trenches beside it
			{
				const FVec3 E = C + FVec3(Random(-300.0, 300.0), Random(-300.0, 300.0), 0.0);
				Moved += GTerrain.MoveSoil(FTerrainBrush::Band(C, E, 70.0, 150.0, 30.0), FTerrainBrush::Band(C, E, 0.0, 30.0, 20.0), Random(0.5, 6.0)).VolumeM3;
				break;
			}
			default: // dig, soil to the rim
				Moved += GTerrain.MoveSoil(FTerrainBrush::Disc(C, 100.0, 60.0), FTerrainBrush::Ring(C, 170.0, 280.0, 60.0), Random(0.1, 4.0)).VolumeM3;
				break;
			}
		}
		const double After = GTerrain.GetTotalSoilVolumeM3();
		std::printf("    300 random pillars, walls and pits moved %.1f m^3; soil volume %.9f -> %.9f m^3\n", Moved, Before, After);
		ExpectTrue("total soil volume unchanged (relative < 1e-12)", std::fabs(After - Before) / Before < 1e-12);
		double Lowest = 1e300;
		double Highest = -1e300;
		for (int Y = 0; Y < GTerrain.GetSamplesY(); ++Y)
		{
			for (int X = 0; X < GTerrain.GetSamplesX(); ++X)
			{
				Lowest = KMin(Lowest, GTerrain.GetSampleHeight(X, Y));
				Highest = KMax(Highest, GTerrain.GetSampleHeight(X, Y));
			}
		}
		ExpectTrue("never dug below bedrock", Lowest >= GTerrain.GetBedrockHeightCm() - 1e-9);
		ExpectTrue("never raised above the limit", Highest <= GTerrain.GetBaseHeightCm() + GTerrain.MaxRaiseCm + 1e-9);
	}

	void PillarCostsItsPotentialEnergy()
	{
		SimTest::Section("Raising a pillar costs its potential energy");
		ResetFlat();
		const FVec3 C(0.0, 0.0, 0.0);
		const double EnergyBefore = GTerrain.GetTotalPotentialEnergyJ();
		const FTerrainEdit Edit = GTerrain.MoveSoil(FTerrainBrush::Ring(C, 160.0, 260.0, 60.0), FTerrainBrush::Disc(C, 80.0, 60.0), 2.0);
		const double EnergyAfter = GTerrain.GetTotalPotentialEnergyJ();
		std::printf("    2 m^3 (%.0f kg) moved: pillar top %.0f cm, ring %.0f cm, work %.1f kJ\n", Edit.MassKg, GTerrain.GetHeightAt(0.0, 0.0),
			GTerrain.GetHeightAt(210.0, 0.0), Edit.PotentialEnergyChangeJ / 1000.0);
		ExpectNear("all 2 m^3 moved", Edit.VolumeM3, 2.0, 1e-9);
		ExpectNear("mass = volume * soil density", Edit.MassKg, 3200.0, 1e-6);
		ExpectNear("edit reports the exact potential-energy change (J)", Edit.PotentialEnergyChangeJ, EnergyAfter - EnergyBefore, 1e-6 * (EnergyAfter - EnergyBefore));
		ExpectTrue("pillar rose", GTerrain.GetHeightAt(0.0, 0.0) > 50.0);
		ExpectTrue("ring sank", GTerrain.GetHeightAt(210.0, 0.0) < -5.0);
		// 3.2 t whose centre of mass rises a few tens of cm: m * g * 0.15..0.5 m.
		ExpectTrue("work = m * g * (a few tens of cm)", Edit.PotentialEnergyChangeJ > 3200.0 * 9.81 * 0.15 && Edit.PotentialEnergyChangeJ < 3200.0 * 9.81 * 0.5);

		// Digging out to a rim at the same level costs far less per m^3 than raising.
		ResetFlat();
		const FTerrainEdit Dig = GTerrain.MoveSoil(FTerrainBrush::Disc(C, 100.0, 60.0), FTerrainBrush::Ring(C, 170.0, 280.0, 60.0), 2.0);
		std::printf("    digging the same 2 m^3 out to a rim: %.1f kJ\n", Dig.PotentialEnergyChangeJ / 1000.0);
		ExpectTrue("digging costs less than raising", Dig.PotentialEnergyChangeJ < Edit.PotentialEnergyChangeJ);
	}

	void BedrockLimitsDigging()
	{
		SimTest::Section("Bedrock");
		ResetFlat();
		const FVec3 C(0.0, 0.0, 0.0);
		const FTerrainEdit Edit = GTerrain.RemoveSoil(FTerrainBrush::Disc(C, 50.0, 0.0), 1000.0);
		ExpectTrue("asking for 1000 m^3 from a small disc is limited", Edit.bLimited);
		ExpectNear("the disc is dug down to bedrock", GTerrain.GetHeightAt(0.0, 0.0), GTerrain.GetBedrockHeightCm(), 1e-9);
		ExpectTrue("removed what was above bedrock under the disc (a few m^3)", Edit.VolumeM3 > 1.0 && Edit.VolumeM3 < 10.0);
	}

	void DirtyRegionCoversTheEdit()
	{
		SimTest::Section("Dirty region");
		ResetFlat();
		int MinX, MinY, MaxX, MaxY;
		GTerrain.ConsumeDirtyRegion(MinX, MinY, MaxX, MaxY);
		ExpectTrue("nothing dirty after consuming", !GTerrain.ConsumeDirtyRegion(MinX, MinY, MaxX, MaxY));
		const FVec3 C(400.0, -400.0, 0.0);
		GTerrain.MoveSoil(FTerrainBrush::Ring(C, 160.0, 260.0, 60.0), FTerrainBrush::Disc(C, 80.0, 60.0), 1.0);
		const bool bDirty = GTerrain.ConsumeDirtyRegion(MinX, MinY, MaxX, MaxY);
		ExpectTrue("edit marks a region", bDirty);
		const double Reach = 320.0;
		const FVec3 Lo = GTerrain.GetSampleLocation(MinX, MinY);
		const FVec3 Hi = GTerrain.GetSampleLocation(MaxX, MaxY);
		ExpectTrue("region stays within the brush reach", Lo.X >= C.X - Reach - 40.0 && Hi.X <= C.X + Reach + 40.0 && Lo.Y >= C.Y - Reach - 40.0 && Hi.Y <= C.Y + Reach + 40.0);
		ExpectTrue("region contains the pillar", Lo.X <= C.X && Hi.X >= C.X && Lo.Y <= C.Y && Hi.Y >= C.Y);
	}
}

int main()
{
	FlatGroundSamplesFlat();
	PlanesAreReproducedExactly();
	RaycastFindsTheSurface();
	EarthbendingConservesSoil();
	PillarCostsItsPotentialEnergy();
	BedrockLimitsDigging();
	DirtyRegionCoversTheEdit();
	return SimTest::Finish("Terrain & earthbending");
}
