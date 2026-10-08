#pragma once

#include "CoreMinimal.h"
#include "Sim/BendingSimTypes.h"
#include "ElementalSubstance.generated.h"

/**
 * Physical matter a bender moves. Distinct from EBendingElement (the discipline): waterbending works with
 * Water, Ice and Steam, and reactions are keyed by substance so Fire+Ice (melting) and Fire+Water
 * (evaporation) differ. Reflected mirror of BendingSim::ESubstance; real-world properties live in the kernel
 * (BendingSim::GetSubstanceProperties).
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

static_assert(static_cast<int>(EElementalSubstance::Earth) == static_cast<int>(BendingSim::ESubstance::Earth), "Substance mirror out of sync");
static_assert(static_cast<int>(EElementalSubstance::Water) == static_cast<int>(BendingSim::ESubstance::Water), "Substance mirror out of sync");
static_assert(static_cast<int>(EElementalSubstance::Ice) == static_cast<int>(BendingSim::ESubstance::Ice), "Substance mirror out of sync");
static_assert(static_cast<int>(EElementalSubstance::Steam) == static_cast<int>(BendingSim::ESubstance::Steam), "Substance mirror out of sync");
static_assert(static_cast<int>(EElementalSubstance::Fire) == static_cast<int>(BendingSim::ESubstance::Fire), "Substance mirror out of sync");
static_assert(static_cast<int>(EElementalSubstance::Air) == static_cast<int>(BendingSim::ESubstance::Air), "Substance mirror out of sync");
static_assert(static_cast<int>(EElementalSubstance::Count) == static_cast<int>(BendingSim::ESubstance::Count), "Substance mirror out of sync");

namespace ElementalSubstance
{
	[[nodiscard]] constexpr BendingSim::ESubstance ToSim(EElementalSubstance Substance)
	{
		return static_cast<BendingSim::ESubstance>(Substance);
	}

	[[nodiscard]] constexpr EElementalSubstance FromSim(BendingSim::ESubstance Substance)
	{
		return static_cast<EElementalSubstance>(Substance);
	}

	[[nodiscard]] constexpr uint32 ToMask(EElementalSubstance Substance)
	{
		return BendingSim::SubstanceMask(ToSim(Substance));
	}

	inline constexpr uint32 AllMask = BendingSim::AllSubstancesMask;
}
