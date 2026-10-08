#pragma once

// Minimal assertion helpers shared by the simulation test executables.

#include <cmath>
#include <cstdio>

namespace SimTest
{
	inline int GFailures = 0;
	inline int GChecks = 0;

	inline void ExpectTrue(const char* What, bool bCondition)
	{
		++GChecks;
		std::printf("  [%s] %s\n", bCondition ? " OK " : "FAIL", What);
		GFailures += bCondition ? 0 : 1;
	}

	inline void ExpectNear(const char* What, double Actual, double Expected, double Tolerance)
	{
		++GChecks;
		const bool bOk = std::fabs(Actual - Expected) <= Tolerance;
		std::printf("  [%s] %-62s %.6g (expected %.6g +/- %.2g)\n", bOk ? " OK " : "FAIL", What, Actual, Expected, Tolerance);
		GFailures += bOk ? 0 : 1;
	}

	inline void Section(const char* Name)
	{
		std::printf("\n%s\n", Name);
	}

	inline int Finish(const char* Suite)
	{
		std::printf("\n%s: %d checks, %s (%d failure%s)\n", Suite, GChecks, GFailures == 0 ? "ALL PASSED" : "FAILED", GFailures,
			GFailures == 1 ? "" : "s");
		return GFailures == 0 ? 0 : 1;
	}
}

using SimTest::ExpectNear;
using SimTest::ExpectTrue;
