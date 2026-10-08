#pragma once

/**
 * Math for the engine-independent bending simulation kernel.
 *
 * The kernel uses no Unreal headers and no standard library, so one set of sources builds into the plugin,
 * the native test suite and a freestanding WebAssembly module (Tools/SimWasm). exp and cbrt are implemented
 * here instead of coming from libm, which also makes results identical across platforms.
 */

#if defined(_MSC_VER) && !defined(__clang__)
#include <cmath>
#endif

// Exported from the Bending module in engine builds, so other modules can call the kernel; empty stand-alone.
#if defined(BENDING_API)
#define BENDINGSIM_API BENDING_API
#else
#define BENDINGSIM_API
#endif

namespace BendingSim
{
	// ---------------------------------------------------------------- Scalars

	template <typename T> constexpr T KMin(T A, T B) { return A < B ? A : B; }
	template <typename T> constexpr T KMax(T A, T B) { return A > B ? A : B; }
	template <typename T> constexpr T KClamp(T V, T Lo, T Hi) { return V < Lo ? Lo : (V > Hi ? Hi : V); }
	template <typename T> constexpr T KAbs(T V) { return V < T(0) ? -V : V; }
	constexpr double KLerp(double A, double B, double Alpha) { return A + (B - A) * Alpha; }

	inline constexpr double Pi = 3.14159265358979323846;
	inline constexpr double SmallNumber = 1e-8;

	inline double KSqrt(double X)
	{
		if (X <= 0.0)
		{
			return 0.0;
		}
#if defined(_MSC_VER) && !defined(__clang__)
		return std::sqrt(X);
#else
		return __builtin_sqrt(X);
#endif
	}

	/** e^X. Range reduction to |r| <= ln2/2, then a degree-13 Taylor series (error below 1e-16 relative). */
	inline double KExp(double X)
	{
		if (X < -700.0)
		{
			return 0.0;
		}
		X = KMin(X, 709.0);
		constexpr double Ln2 = 0.69314718055994530942;
		const double Scaled = X / Ln2;
		const long long N = static_cast<long long>(Scaled + (Scaled >= 0.0 ? 0.5 : -0.5));
		const double R = X - static_cast<double>(N) * Ln2;

		double Term = 1.0;
		double Sum = 1.0;
		for (int Order = 1; Order <= 13; ++Order)
		{
			Term *= R / Order;
			Sum += Term;
		}
		const unsigned long long Bits = static_cast<unsigned long long>(N + 1023) << 52;
		return Sum * __builtin_bit_cast(double, Bits);
	}

	/** Cube root: exponent-division initial guess refined by Newton iterations. */
	inline double KCbrt(double X)
	{
		if (X == 0.0)
		{
			return 0.0;
		}
		const bool bNegative = X < 0.0;
		const double A = bNegative ? -X : X;
		unsigned long long Bits = __builtin_bit_cast(unsigned long long, A);
		Bits = Bits / 3 + 0x2A9F7893782DA1CEull;
		double Y = __builtin_bit_cast(double, Bits);
		for (int Iteration = 0; Iteration < 6; ++Iteration)
		{
			Y = (2.0 * Y + A / (Y * Y)) / 3.0;
		}
		return bNegative ? -Y : Y;
	}

	/** Hermite smoothstep between Edge0 and Edge1. */
	constexpr double KSmoothStep(double Edge0, double Edge1, double X)
	{
		if (Edge1 <= Edge0)
		{
			return X < Edge0 ? 0.0 : 1.0;
		}
		const double T = KClamp((X - Edge0) / (Edge1 - Edge0), 0.0, 1.0);
		return T * T * (3.0 - 2.0 * T);
	}

	// ---------------------------------------------------------------- Vector

	struct FVec3
	{
		double X = 0.0;
		double Y = 0.0;
		double Z = 0.0;

		constexpr FVec3() = default;
		constexpr FVec3(double InX, double InY, double InZ) : X(InX), Y(InY), Z(InZ) {}

		constexpr FVec3 operator+(const FVec3& O) const { return FVec3(X + O.X, Y + O.Y, Z + O.Z); }
		constexpr FVec3 operator-(const FVec3& O) const { return FVec3(X - O.X, Y - O.Y, Z - O.Z); }
		constexpr FVec3 operator-() const { return FVec3(-X, -Y, -Z); }
		constexpr FVec3 operator*(double S) const { return FVec3(X * S, Y * S, Z * S); }
		constexpr FVec3 operator/(double S) const { return FVec3(X / S, Y / S, Z / S); }
		FVec3& operator+=(const FVec3& O) { X += O.X; Y += O.Y; Z += O.Z; return *this; }
		FVec3& operator-=(const FVec3& O) { X -= O.X; Y -= O.Y; Z -= O.Z; return *this; }
		FVec3& operator*=(double S) { X *= S; Y *= S; Z *= S; return *this; }

		constexpr double Dot(const FVec3& O) const { return X * O.X + Y * O.Y + Z * O.Z; }
		constexpr double SizeSquared() const { return Dot(*this); }
		double Size() const { return KSqrt(SizeSquared()); }
		double Size2D() const { return KSqrt(X * X + Y * Y); }
		constexpr bool IsZero() const { return X == 0.0 && Y == 0.0 && Z == 0.0; }
		constexpr bool IsNearlyZero(double Tolerance = 1e-8) const { return KAbs(X) <= Tolerance && KAbs(Y) <= Tolerance && KAbs(Z) <= Tolerance; }

		constexpr double operator[](int Axis) const { return Axis == 0 ? X : (Axis == 1 ? Y : Z); }

		FVec3 GetSafeNormal() const
		{
			const double Length = Size();
			return Length > SmallNumber ? *this / Length : FVec3();
		}

		/** Componentwise product. */
		constexpr FVec3 Mul(const FVec3& O) const { return FVec3(X * O.X, Y * O.Y, Z * O.Z); }
	};

	constexpr FVec3 operator*(double S, const FVec3& V) { return V * S; }

	inline double Distance(const FVec3& A, const FVec3& B) { return (B - A).Size(); }
	constexpr double DistanceSquared(const FVec3& A, const FVec3& B) { return (B - A).SizeSquared(); }
	inline double Distance2D(const FVec3& A, const FVec3& B) { return (B - A).Size2D(); }

	/** Closest point on segment [A, B] to P. */
	inline FVec3 ClosestPointOnSegment(const FVec3& P, const FVec3& A, const FVec3& B)
	{
		const FVec3 AB = B - A;
		const double LengthSquared = AB.SizeSquared();
		if (LengthSquared <= SmallNumber)
		{
			return A;
		}
		const double T = KClamp((P - A).Dot(AB) / LengthSquared, 0.0, 1.0);
		return A + AB * T;
	}

	/** Closest points between segments [P0, P1] and [Q0, Q1]; robust for parallel and degenerate segments. */
	inline void ClosestPointsBetweenSegments(const FVec3& P0, const FVec3& P1, const FVec3& Q0, const FVec3& Q1, FVec3& OutP, FVec3& OutQ)
	{
		const FVec3 D1 = P1 - P0;
		const FVec3 D2 = Q1 - Q0;
		const FVec3 R = P0 - Q0;
		const double A = D1.SizeSquared();
		const double E = D2.SizeSquared();
		const double F = D2.Dot(R);

		double S = 0.0;
		double T = 0.0;
		if (A <= SmallNumber && E <= SmallNumber)
		{
			OutP = P0;
			OutQ = Q0;
			return;
		}
		if (A <= SmallNumber)
		{
			T = KClamp(F / E, 0.0, 1.0);
		}
		else
		{
			const double C = D1.Dot(R);
			if (E <= SmallNumber)
			{
				S = KClamp(-C / A, 0.0, 1.0);
			}
			else
			{
				const double B = D1.Dot(D2);
				const double Denominator = A * E - B * B;
				S = Denominator > SmallNumber ? KClamp((B * F - C * E) / Denominator, 0.0, 1.0) : 0.0;
				T = (B * S + F) / E;
				if (T < 0.0)
				{
					T = 0.0;
					S = KClamp(-C / A, 0.0, 1.0);
				}
				else if (T > 1.0)
				{
					T = 1.0;
					S = KClamp((B - C) / A, 0.0, 1.0);
				}
			}
		}
		OutP = P0 + D1 * S;
		OutQ = Q0 + D2 * T;
	}

	// ---------------------------------------------------------------- Units
	// Engine-facing quantities (positions, velocities, impulses) are cm, cm/s, kg*cm/s.
	// Physical-model quantities are SI. Convert only through these.

	inline constexpr double CmPerM = 100.0;
	inline constexpr double MPerCm = 0.01;
	constexpr double CmToM(double Cm) { return Cm * MPerCm; }
	constexpr double MToCm(double M) { return M * CmPerM; }
	constexpr FVec3 CmToM(const FVec3& Cm) { return Cm * MPerCm; }
	constexpr FVec3 MToCm(const FVec3& M) { return M * CmPerM; }
	/** N*s (kg*m/s) -> kg*cm/s. */
	constexpr FVec3 ImpulseSIToEngine(const FVec3& NewtonSeconds) { return NewtonSeconds * CmPerM; }
	/** kg*cm/s -> N*s. */
	constexpr FVec3 ImpulseEngineToSI(const FVec3& KgCmS) { return KgCmS * MPerCm; }

	// ---------------------------------------------------------------- Physical constants (SI)

	inline constexpr double StandardPressurePa = 101325.0;
	inline constexpr double SpecificGasConstantAir = 287.05;  // J/(kg*K)
	inline constexpr double SpecificGasConstantSteam = 461.5; // J/(kg*K)
	inline constexpr double AirDensitySeaLevel = 1.225;       // kg/m^3 at 288.15 K
	inline constexpr double WaterDensity = 1000.0;            // kg/m^3
	/** Heat released per kg of air consumed by combustion: Thornton's rule, ~13.1 MJ/kg O2, air is 23.1% O2. */
	inline constexpr double CombustionHeatPerKgAir = 3.03e6;

	inline double SphereVolumeM3(double RadiusM) { return (4.0 / 3.0) * Pi * RadiusM * RadiusM * RadiusM; }
	inline double SphereRadiusFromVolumeM(double VolumeM3) { return VolumeM3 > 0.0 ? KCbrt(VolumeM3 * 3.0 / (4.0 * Pi)) : 0.0; }
	inline double IdealGasDensity(double TemperatureK, double SpecificGasConstant, double PressurePa = StandardPressurePa)
	{
		return PressurePa / (SpecificGasConstant * KMax(TemperatureK, 1.0));
	}
	/** q = 1/2 * rho * v^2 (Pa). */
	constexpr double DynamicPressure(double DensityKgM3, double SpeedMs) { return 0.5 * DensityKgM3 * SpeedMs * SpeedMs; }
}
