#pragma once

#include "CoreMinimal.h"
#include "ElementalSubstance.generated.h"

/**
 * Physical matter a bender moves. Distinct from EBendingElement (the discipline): waterbending works
 * with Water, Ice and Steam, and reactions are keyed by substance so Fire+Ice (melting) and
 * Fire+Water (evaporation) can differ.
 */
UENUM(BlueprintType)
enum class EElementalSubstance : uint8
{
	None,
	Earth,
	Water,
	Ice,
	Steam,
	Fire,
	Air,

	Count UMETA(Hidden)
};

namespace ElementalSubstance
{
	[[nodiscard]] constexpr uint32 ToMask(EElementalSubstance Substance)
	{
		return 1u << static_cast<uint32>(Substance);
	}

	inline constexpr uint32 AllMask = (1u << static_cast<uint32>(EElementalSubstance::Count)) - 1u;

	/** Order-independent key for a substance pair. */
	[[nodiscard]] constexpr uint16 MakePairKey(EElementalSubstance A, EElementalSubstance B)
	{
		const uint16 X = static_cast<uint16>(A);
		const uint16 Y = static_cast<uint16>(B);
		return X < Y ? static_cast<uint16>((X << 8) | Y) : static_cast<uint16>((Y << 8) | X);
	}
}

/** Real-world reference properties of a substance. */
struct BENDING_API FElementalSubstanceProperties
{
	/** Reference density (kg/m^3). Gases with a derived radius use the ideal-gas law instead. */
	double DensityKgM3 = 1000.0;

	/** Specific heat capacity (J/(kg*K)). */
	double SpecificHeat = 1000.0;

	/** Temperature a freshly bent volume starts at (K). */
	double DefaultTemperatureK = 288.15;

	/** Effective convective + radiative coefficient to the surrounding air (W/(m^2*K)). */
	double AmbientHeatTransferCoefficient = 10.0;

	/** Drag coefficient of a free blob of this substance (sphere ~0.47). */
	double DragCoefficient = 0.47;

	/** Specific gas constant (J/(kg*K)); > 0 only for gases. */
	double SpecificGasConstant = 0.0;

	/** Typical pore fraction (0..1). Only meaningful for Earth; overridden per volume from its physical material. */
	double DefaultPorosity = 0.0;

	bool bIsGas = false;

	[[nodiscard]] static const FElementalSubstanceProperties& Get(EElementalSubstance Substance);

	/** Density at a temperature: ideal gas for gases, reference density otherwise. */
	[[nodiscard]] double GetDensityAt(double TemperatureK) const;
};
