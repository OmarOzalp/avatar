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

		void AddProp(FArenaLayout& Layout, EArenaProp Kind, double X, double Y)
		{
			if (Layout.NumProps < FArenaLayout::MaxProps)
			{
				Layout.Props[Layout.NumProps++] = { Kind, FVec3(X, Y, 0.0) };
			}
		}

		FArenaLayout BuildTrainingGround()
		{
			FArenaLayout Layout;
			Layout.PlayerStartCm = FVec3(0.0, -300.0, 0.0);
			Layout.PlayerStartYawDeg = 0.0;

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
