#pragma once

#include "CoreMinimal.h"

/**
 * Unit conventions for the Bending plugin.
 *
 *  - Anything handed straight to engine APIs stays in engine units: positions and lengths in cm,
 *    velocities in cm/s, impulses in kg*cm/s. Such fields carry a Cm / CmS / KgCmS suffix.
 *  - Every physical-model quantity is SI: kg, m, m/s, K, J, W, Pa, N.
 *  - Conversion happens only through these helpers, at the edge of a formula, never inside it.
 */
namespace BendingUnits
{
	inline constexpr double CmPerM = 100.0;
	inline constexpr double MPerCm = 0.01;

	[[nodiscard]] constexpr double CmToM(double Cm) { return Cm * MPerCm; }
	[[nodiscard]] constexpr double MToCm(double M) { return M * CmPerM; }
	[[nodiscard]] inline FVector CmToM(const FVector& Cm) { return Cm * MPerCm; }
	[[nodiscard]] inline FVector MToCm(const FVector& M) { return M * CmPerM; }

	/** N*s (kg*m/s) -> kg*cm/s, the unit UPrimitiveComponent::AddImpulse expects. */
	[[nodiscard]] inline FVector ImpulseSIToEngine(const FVector& NewtonSeconds) { return NewtonSeconds * CmPerM; }
	/** kg*cm/s -> N*s. */
	[[nodiscard]] inline FVector ImpulseEngineToSI(const FVector& KgCmS) { return KgCmS * MPerCm; }
}

namespace BendingPhysics
{
	inline constexpr double StandardPressurePa = 101325.0;
	inline constexpr double SpecificGasConstantAir = 287.05;   // J/(kg*K)
	inline constexpr double SpecificGasConstantSteam = 461.5;  // J/(kg*K)
	inline constexpr double AirDensitySeaLevel = 1.225;        // kg/m^3 at 288.15 K
	inline constexpr double WaterDensity = 1000.0;             // kg/m^3

	/**
	 * Heat released per kg of air consumed by combustion. Thornton's rule gives ~13.1 MJ per kg of O2
	 * for common fuels; air is 23.1% O2 by mass.
	 */
	inline constexpr double CombustionHeatPerKgAir = 3.03e6;

	[[nodiscard]] inline double SphereVolumeM3(double RadiusM)
	{
		return (4.0 / 3.0) * UE_DOUBLE_PI * RadiusM * RadiusM * RadiusM;
	}

	[[nodiscard]] inline double SphereRadiusFromVolumeM(double VolumeM3)
	{
		return VolumeM3 > 0.0 ? FMath::Pow(VolumeM3 * 3.0 / (4.0 * UE_DOUBLE_PI), 1.0 / 3.0) : 0.0;
	}

	/** Ideal gas: rho = P / (R_specific * T). */
	[[nodiscard]] inline double IdealGasDensity(double TemperatureK, double SpecificGasConstant, double PressurePa = StandardPressurePa)
	{
		return PressurePa / (SpecificGasConstant * FMath::Max(TemperatureK, 1.0));
	}

	/** q = 1/2 * rho * v^2 (Pa). */
	[[nodiscard]] inline double DynamicPressure(double DensityKgM3, double SpeedMs)
	{
		return 0.5 * DensityKgM3 * SpeedMs * SpeedMs;
	}
}
