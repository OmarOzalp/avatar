#include "Sim/BendingArena.h"

namespace BendingSim
{
	namespace
	{
		FArenaPropSpec MakeSpec(const char* Name, ESubstance Substance, double MassKg, double DensityKgM3, double Porosity,
			double HalfHeightCm, bool bStatic)
		{
			FArenaPropSpec Spec;
			Spec.Name = Name;
			Spec.Substance = Substance;
			Spec.MassKg = MassKg;
			Spec.DensityKgM3 = DensityKgM3;
			Spec.Porosity = Porosity;
			Spec.HalfHeightCm = HalfHeightCm;
			Spec.bStatic = bStatic;
			const double VolumeM3 = MassKg / DensityKgM3;
			if (HalfHeightCm > 0.0)
			{
				// Upright capsule holding the volume: pi r^2 (2h) + 4/3 pi r^3, solved for r by bisection.
				double Lo = 1.0;
				double Hi = 200.0;
				for (int Iteration = 0; Iteration < 60; ++Iteration)
				{
					const double R = 0.5 * (Lo + Hi);
					const double RM = CmToM(R);
					const double V = Pi * RM * RM * CmToM(2.0 * HalfHeightCm) + SphereVolumeM3(RM);
					(V > VolumeM3 ? Hi : Lo) = R;
				}
				Spec.RadiusCm = 0.5 * (Lo + Hi);
				Spec.CenterHeightCm = HalfHeightCm + Spec.RadiusCm;
			}
			else
			{
				Spec.RadiusCm = MToCm(SphereRadiusFromVolumeM(VolumeM3));
				Spec.CenterHeightCm = Spec.RadiusCm;
			}
			return Spec;
		}

		FCombustionSpec MakeCombustion(double IgnitionJ, double BurnSeconds, double FlameMassKg, double FlameTemperatureK, double ReachCm,
			double HeatingWPerK)
		{
			FCombustionSpec Combustion;
			Combustion.HeatingWPerK = HeatingWPerK;
			Combustion.IgnitionJ = IgnitionJ;
			Combustion.BurnSeconds = BurnSeconds;
			Combustion.FlameMassKg = FlameMassKg;
			Combustion.FlameTemperatureK = FlameTemperatureK;
			Combustion.ReachCm = ReachCm;
			// Enough to hold the flame at temperature in the open (a brazier's 0.5 kg flame needs about 600 kW) while the
			// prop it burns on soaks up heat from it.
			Combustion.MaxBurnPowerW = 4.0e6 * FlameMassKg + 1.0e5;
			return Combustion;
		}

		struct FSpecTable
		{
			FArenaPropSpec Specs[static_cast<int>(EArenaProp::Count)];

			FSpecTable()
			{
				Specs[static_cast<int>(EArenaProp::Stone)] = MakeSpec("Stone", ESubstance::Earth, 6.0, 2650.0, 0.02, 0.0, false);
				Specs[static_cast<int>(EArenaProp::Rock)] = MakeSpec("Rock", ESubstance::Earth, 60.0, 2650.0, 0.02, 0.0, false);
				Specs[static_cast<int>(EArenaProp::Boulder)] = MakeSpec("Boulder", ESubstance::Earth, 600.0, 2650.0, 0.02, 0.0, false);
				Specs[static_cast<int>(EArenaProp::Clod)] = MakeSpec("Soil clod", ESubstance::Earth, 100.0, 1600.0, 0.35, 0.0, false);
				// Wood (~550 kg/m^3), modelled as earth that cannot absorb water.
				Specs[static_cast<int>(EArenaProp::Dummy)] = MakeSpec("Training dummy", ESubstance::Earth, 60.0, 550.0, 0.0, 55.0, false);
				FArenaPropSpec Brazier = MakeSpec("Brazier", ESubstance::Fire, 0.5, 1.0, 0.0, 0.0, true);
				Brazier.TemperatureK = 1300.0;
				// The flame sits on a 1 m pedestal; its radius follows the ideal-gas law at runtime.
				Brazier.CenterHeightCm = 150.0;
				Specs[static_cast<int>(EArenaProp::Brazier)] = Brazier;
				FArenaPropSpec Ice = MakeSpec("Ice block", ESubstance::Ice, 40.0, 917.0, 0.0, 0.0, true);
				Ice.TemperatureK = 263.15;
				Specs[static_cast<int>(EArenaProp::IceBlock)] = Ice;

				// Dummies are wood and straw: slow to catch, then they char.
				FArenaPropSpec& Dummy = Specs[static_cast<int>(EArenaProp::Dummy)];
				Dummy.Combustion = MakeCombustion(90.0e3, 12.0, 0.3, 1250.0, 70.0, 150.0);
				Dummy.FlameHeightCm = 120.0;

				// The banner's volume is its wooden pole; the cloth is what burns.
				FArenaPropSpec Banner = MakeSpec("Banner", ESubstance::Earth, 15.0, 600.0, 0.0, 210.0, true);
				Banner.Combustion = MakeCombustion(30.0e3, 7.0, 0.12, 1200.0, 115.0, 200.0);
				Banner.FlameHeightCm = BannerClothBottomCm;
				Specs[static_cast<int>(EArenaProp::Banner)] = Banner;

				// Stone lantern with a wick that burns for as long as it is left lit.
				FArenaPropSpec Lantern = MakeSpec("Stone lantern", ESubstance::Earth, 120.0, 2400.0, 0.02, 45.0, true);
				Lantern.Combustion = MakeCombustion(5.0e3, 1.0e9, 0.02, 1100.0, 45.0, 300.0);
				Lantern.Combustion.MaxBurnPowerW = 1.0e5;
				// Its flame sits just clear of the stone (the light is drawn in the lamp box below it).
				Lantern.FlameHeightCm = 160.0;
				Specs[static_cast<int>(EArenaProp::Lantern)] = Lantern;

				// Straw: catches at a touch and burns big and long.
				FArenaPropSpec Straw = MakeSpec("Straw bale", ESubstance::Earth, 30.0, 120.0, 0.0, 0.0, true);
				Straw.Combustion = MakeCombustion(40.0e3, 14.0, 0.6, 1350.0, 90.0, 400.0);
				Straw.FlameHeightCm = 60.0;
				Specs[static_cast<int>(EArenaProp::StrawBale)] = Straw;

				// A 50 cm crate of boards (mostly air inside): smashes at 4.5 m/s, burns.
				FArenaPropSpec Crate = MakeSpec("Crate", ESubstance::Earth, 20.0, 160.0, 0.0, 0.0, false);
				Crate.Combustion = MakeCombustion(60.0e3, 12.0, 0.3, 1250.0, 60.0, 250.0);
				Crate.BreakSpeedMs = 4.5;
				Crate.FlameHeightCm = 45.0;
				Specs[static_cast<int>(EArenaProp::Crate)] = Crate;

				// 20 kg of staves round 60 kg of water: too wet to burn, bursts at 3.5 m/s.
				FArenaPropSpec Barrel = MakeSpec("Water barrel", ESubstance::Earth, 80.0, 900.0, 0.0, 22.0, false);
				Barrel.BreakSpeedMs = 3.5;
				Barrel.WaterKg = 60.0;
				Specs[static_cast<int>(EArenaProp::WaterBarrel)] = Barrel;
			}
		};

		const FSpecTable& GetSpecTable()
		{
			static const FSpecTable Table;
			return Table;
		}

		void AddPond(FArenaLayout& Layout, double X, double Y, double RadiusCm, double WaterDepthCm)
		{
			FArenaPond& Pond = Layout.Ponds[Layout.NumPonds++];
			Pond.CenterCm = FVec3(X, Y, 0.0);
			Pond.RadiusCm = RadiusCm;
			Pond.WaterDepthCm = WaterDepthCm;
			Pond.SurfaceHeightCm = -15.0;
			Pond.WaterKg = WaterDensity * Pi * CmToM(RadiusCm) * CmToM(RadiusCm) * CmToM(WaterDepthCm);
			Pond.CenterCm.Z = Pond.SurfaceHeightCm;
		}

		void AddProp(FArenaLayout& Layout, EArenaProp Kind, double X, double Y, double YawDeg = 0.0, int Variant = 0)
		{
			if (Layout.NumProps < FArenaLayout::MaxProps)
			{
				FArenaPropPlacement& Placement = Layout.Props[Layout.NumProps++];
				Placement.Kind = Kind;
				Placement.LocationCm = FVec3(X, Y, 0.0);
				Placement.YawDeg = YawDeg;
				Placement.Variant = Variant;
			}
		}

		/** Clear of ponds (by Margin pond radii) and of the props placed so far (by ClearanceCm). */
		bool IsClearSpot(const FArenaLayout& Layout, double X, double Y, double PondMargin, double ClearanceCm)
		{
			for (int Index = 0; Index < Layout.NumPonds; ++Index)
			{
				const FArenaPond& Pond = Layout.Ponds[Index];
				if (KSqrt((X - Pond.CenterCm.X) * (X - Pond.CenterCm.X) + (Y - Pond.CenterCm.Y) * (Y - Pond.CenterCm.Y)) < PondMargin * Pond.RadiusCm)
				{
					return false;
				}
			}
			for (int Index = 0; Index < Layout.NumProps; ++Index)
			{
				const FVec3& Other = Layout.Props[Index].LocationCm;
				if (KSqrt((X - Other.X) * (X - Other.X) + (Y - Other.Y) * (Y - Other.Y)) < ClearanceCm)
				{
					return false;
				}
			}
			return true;
		}

		FArenaLayout BuildTrainingGround()
		{
			FArenaLayout Layout;
			Layout.PlayerStartCm = FVec3(0.0, -300.0, 0.0);
			Layout.PlayerStartYawDeg = 0.0;
			Layout.SparringPostCm = FVec3(600.0, 250.0, 0.0);

			AddPond(Layout, 700.0, 1100.0, 380.0, 80.0);
			AddPond(Layout, -2400.0, 1900.0, 650.0, 120.0);

			AddProp(Layout, EArenaProp::Brazier, 1500.0, -900.0);
			AddProp(Layout, EArenaProp::Brazier, 1900.0, -600.0);
			AddProp(Layout, EArenaProp::Brazier, -900.0, -1700.0);
			AddProp(Layout, EArenaProp::IceBlock, 1200.0, 650.0);
			AddProp(Layout, EArenaProp::IceBlock, 1450.0, 850.0);
			AddProp(Layout, EArenaProp::Dummy, 2300.0, -250.0);
			AddProp(Layout, EArenaProp::Dummy, 2300.0, 250.0);
			AddProp(Layout, EArenaProp::Dummy, 2650.0, 0.0);
			AddProp(Layout, EArenaProp::Dummy, 3000.0, -350.0);
			AddProp(Layout, EArenaProp::Dummy, 3000.0, 350.0);
			AddProp(Layout, EArenaProp::Stone, 500.0, -500.0);
			AddProp(Layout, EArenaProp::Stone, 570.0, -430.0);
			AddProp(Layout, EArenaProp::Stone, 630.0, -560.0);
			AddProp(Layout, EArenaProp::Stone, 450.0, -630.0);
			AddProp(Layout, EArenaProp::Stone, 720.0, -470.0);
			AddProp(Layout, EArenaProp::Rock, 900.0, -1250.0);
			AddProp(Layout, EArenaProp::Rock, 1080.0, -1400.0);
			AddProp(Layout, EArenaProp::Boulder, -1300.0, -500.0);
			AddProp(Layout, EArenaProp::Boulder, -1550.0, 150.0);
			AddProp(Layout, EArenaProp::Clod, 250.0, 550.0);
			AddProp(Layout, EArenaProp::Clod, -350.0, 850.0);

			// Things to burn, smash and soak. Fire practice yard: straw and crates close enough for fire to spread.
			AddProp(Layout, EArenaProp::StrawBale, 2350.0, -950.0, 20.0);
			AddProp(Layout, EArenaProp::StrawBale, 2470.0, -1040.0, -15.0);
			AddProp(Layout, EArenaProp::StrawBale, 2600.0, -1130.0, 35.0);
			AddProp(Layout, EArenaProp::Crate, 2235.0, -1005.0, 12.0);
			AddProp(Layout, EArenaProp::Crate, 2300.0, -1075.0, -20.0);
			AddProp(Layout, EArenaProp::Crate, 2205.0, -1095.0, 40.0);
			// Crates by the rocks, to smash with them.
			AddProp(Layout, EArenaProp::Crate, 950.0, -880.0, 8.0);
			AddProp(Layout, EArenaProp::Crate, 1030.0, -950.0, -25.0);
			AddProp(Layout, EArenaProp::Crate, 960.0, -1020.0, 30.0);
			// Water barrels by the braziers: burst one and it soaks everything round it.
			AddProp(Layout, EArenaProp::WaterBarrel, 1250.0, -1080.0);
			AddProp(Layout, EArenaProp::WaterBarrel, 1730.0, -1150.0);
			AddProp(Layout, EArenaProp::WaterBarrel, -620.0, -1350.0);

			// Banners in the four elements' colours round the ring, facing its centre.
			const int BannerElements[8] = { 2, 1, 3, 4, 2, 1, 3, 4 };
			for (int Index = 0; Index < 8; ++Index)
			{
				const double Angle = Pi / 8.0 + Index * Pi / 4.0;
				const double X = KCos(Angle) * (FArenaLayout::RingRadiusCm + 200.0);
				const double Y = KSin(Angle) * (FArenaLayout::RingRadiusCm + 200.0);
				if (IsClearSpot(Layout, X, Y, 1.4, 260.0))
				{
					AddProp(Layout, EArenaProp::Banner, X, Y, Angle * 180.0 / Pi + 180.0, BannerElements[Index]);
				}
			}
			// Stone lanterns flanking the four entrances to the ring.
			for (int Entrance = 0; Entrance < 4; ++Entrance)
			{
				for (int Side = -1; Side <= 1; Side += 2)
				{
					const double Angle = Entrance * Pi / 2.0 + Side * 0.13;
					const double X = KCos(Angle) * (FArenaLayout::RingRadiusCm + 60.0);
					const double Y = KSin(Angle) * (FArenaLayout::RingRadiusCm + 60.0);
					if (IsClearSpot(Layout, X, Y, 1.3, 160.0))
					{
						AddProp(Layout, EArenaProp::Lantern, X, Y, Angle * 180.0 / Pi);
					}
				}
			}
			return Layout;
		}

		double ArenaHeightAt(const FArenaLayout& Layout, double X, double Y)
		{
			double H = 0.0;
			// Flat plaza around the start, rolling ground beyond it.
			const double FromCenter = KSqrt(X * X + Y * Y);
			const double Rolling = KSmoothStep(900.0, 1600.0, FromCenter);
			H += Rolling * (28.0 * KSin(X / 650.0 + 0.7) * KCos(Y / 820.0 - 0.4) + 14.0 * KSin((X + Y) / 410.0));
			// Two hills.
			const double Ax = X + 2600.0, Ay = Y + 2600.0;
			H += 450.0 * KExp(-(Ax * Ax + Ay * Ay) / (2.0 * 900.0 * 900.0));
			const double Bx = X - 2900.0, By = Y - 2700.0;
			H += 260.0 * KExp(-(Bx * Bx + By * By) / (2.0 * 700.0 * 700.0));
			// Pond basins.
			for (int Index = 0; Index < Layout.NumPonds; ++Index)
			{
				const FArenaPond& Pond = Layout.Ponds[Index];
				const double D = KSqrt((X - Pond.CenterCm.X) * (X - Pond.CenterCm.X) + (Y - Pond.CenterCm.Y) * (Y - Pond.CenterCm.Y));
				const double Bottom = Pond.SurfaceHeightCm - Pond.WaterDepthCm;
				const double Inside = 1.0 - KSmoothStep(0.7 * Pond.RadiusCm, 1.15 * Pond.RadiusCm, D);
				H = KLerp(H, Bottom, Inside);
			}
			// Ridge around the edge keeps everyone inside.
			const double MinX = Layout.OriginCm.X;
			const double MinY = Layout.OriginCm.Y;
			const double MaxX = MinX + (Layout.SamplesPerSide - 1) * Layout.CellSizeCm;
			const double MaxY = MinY + (Layout.SamplesPerSide - 1) * Layout.CellSizeCm;
			const double ToEdge = KMin(KMin(X - MinX, MaxX - X), KMin(Y - MinY, MaxY - Y));
			if (ToEdge < 700.0)
			{
				const double T = 1.0 - ToEdge / 700.0;
				H += 550.0 * T * T;
			}
			return H;
		}
	}

	const FArenaPropSpec& GetArenaPropSpec(EArenaProp Kind)
	{
		const int Index = static_cast<int>(Kind);
		return GetSpecTable().Specs[Index < static_cast<int>(EArenaProp::Count) ? Index : 0];
	}

	FVolume MakeArenaPropVolume(EArenaProp Kind, const FVec3& GroundCm)
	{
		const FArenaPropSpec& Spec = GetArenaPropSpec(Kind);
		FVolume Volume = FVolume::MakeDefault(Spec.Substance, Spec.MassKg);
		Volume.TemperatureK = Spec.TemperatureK;
		Volume.Porosity = Spec.Porosity;
		Volume.LocationCm = FVec3(GroundCm.X, GroundCm.Y, GroundCm.Z + Spec.CenterHeightCm);
		Volume.bImmovable = Spec.bStatic && Spec.Substance != ESubstance::Fire;
		if (Spec.Substance == ESubstance::Fire)
		{
			Volume.UpdateDerivedRadius();
		}
		else
		{
			Volume.bDeriveRadiusFromMass = false;
			Volume.RadiusCm = Spec.RadiusCm;
			if (Spec.HalfHeightCm > 0.0)
			{
				Volume.Shape = EShape::Capsule;
				Volume.CapsuleHalfAxisCm = FVec3(0.0, 0.0, Spec.HalfHeightCm);
				Volume.DragCoefficient = 1.0;
			}
		}
		return Volume;
	}

	const FArenaLayout& GetTrainingGroundLayout()
	{
		static const FArenaLayout Layout = BuildTrainingGround();
		return Layout;
	}

	void GenerateArenaTerrain(const FArenaLayout& Layout, FTerrain& Terrain)
	{
		Terrain.Init(Layout.SamplesPerSide, Layout.SamplesPerSide, Layout.CellSizeCm, Layout.OriginCm, 0.0);
		for (int Y = 0; Y < Layout.SamplesPerSide; ++Y)
		{
			for (int X = 0; X < Layout.SamplesPerSide; ++X)
			{
				const double WorldX = Layout.OriginCm.X + X * Layout.CellSizeCm;
				const double WorldY = Layout.OriginCm.Y + Y * Layout.CellSizeCm;
				Terrain.SetSampleHeight(X, Y, ArenaHeightAt(Layout, WorldX, WorldY));
			}
		}
		Terrain.MarkAllDirty();
	}

	int FindWaterSource(const FArenaLayout& Layout, const FVec3& FromCm, double RangeCm, FVec3& OutSourceCm)
	{
		int Best = -1;
		double BestDistance = RangeCm;
		for (int Index = 0; Index < Layout.NumPonds; ++Index)
		{
			const FArenaPond& Pond = Layout.Ponds[Index];
			if (Pond.WaterKg <= 0.0)
			{
				continue;
			}
			// Closest point of the water surface (kept 30 cm inside the shore).
			FVec3 Flat(FromCm.X - Pond.CenterCm.X, FromCm.Y - Pond.CenterCm.Y, 0.0);
			const double Reach = KMax(Pond.RadiusCm - 30.0, 0.0);
			if (Flat.Size() > Reach)
			{
				Flat = Flat.GetSafeNormal() * Reach;
			}
			const FVec3 Surface(Pond.CenterCm.X + Flat.X, Pond.CenterCm.Y + Flat.Y, Pond.SurfaceHeightCm);
			const double D = Distance(FromCm, Surface);
			if (D <= BestDistance)
			{
				BestDistance = D;
				Best = Index;
				OutSourceCm = Surface;
			}
		}
		return Best;
	}
}
