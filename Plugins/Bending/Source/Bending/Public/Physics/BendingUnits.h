#pragma once

#include "CoreMinimal.h"
#include "Sim/BendingSimMath.h"

/**
 * Unit conventions, engine side. The simulation kernel (Sim/BendingSimMath.h) defines the units and constants;
 * these are the FVector conveniences for gameplay code.
 *
 *  - Anything handed straight to engine APIs stays in engine units: cm, cm/s, kg*cm/s.
 *  - Every physical-model quantity is SI: kg, m, m/s, K, J, W, Pa, N.
 */
namespace BendingUnits
{
	using BendingSim::CmPerM;
	using BendingSim::MPerCm;

	[[nodiscard]] constexpr double CmToM(double Cm) { return BendingSim::CmToM(Cm); }
	[[nodiscard]] constexpr double MToCm(double M) { return BendingSim::MToCm(M); }
	[[nodiscard]] inline FVector CmToM(const FVector& Cm) { return Cm * MPerCm; }
	[[nodiscard]] inline FVector MToCm(const FVector& M) { return M * CmPerM; }

	/** N*s (kg*m/s) -> kg*cm/s, the unit UPrimitiveComponent::AddImpulse expects. */
	[[nodiscard]] inline FVector ImpulseSIToEngine(const FVector& NewtonSeconds) { return NewtonSeconds * CmPerM; }
	/** kg*cm/s -> N*s. */
	[[nodiscard]] inline FVector ImpulseEngineToSI(const FVector& KgCmS) { return KgCmS * MPerCm; }

	[[nodiscard]] inline BendingSim::FVec3 ToSim(const FVector& V) { return BendingSim::FVec3(V.X, V.Y, V.Z); }
	[[nodiscard]] inline FVector ToEngine(const BendingSim::FVec3& V) { return FVector(V.X, V.Y, V.Z); }
}
