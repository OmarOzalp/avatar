#pragma once

#include "CoreMinimal.h"
#include "BendingTypes.h"
#include "GameplayTagContainer.h"
#include "Sim/BendingTechniques.h"
#include "BendingTechniqueTypes.generated.h"

/**
 * Reflected mirror of BendingSim::ETechnique. What each technique is (element, input slot, frame data, flat cost,
 * physical recipe) is defined once in the kernel (Sim/BendingTechniques.h) and shared with the browser build.
 */
UENUM(BlueprintType)
enum class EBendingTechnique : uint8
{
	None,
	WaterWhip,
	WaterFreeze,
	WaterRelease,
	WaterBlast,
	RockThrow,
	EarthWall,
	RaiseGround,
	LowerGround,
	FireBlast,
	FlameStream,
	GroundFlame,
	AirBlast,
	AirGust,
	AirJump,

	Count UMETA(Hidden)
};

static_assert(static_cast<int>(EBendingTechnique::WaterWhip) == static_cast<int>(BendingSim::ETechnique::WaterWhip), "Technique mirror out of sync");
static_assert(static_cast<int>(EBendingTechnique::RockThrow) == static_cast<int>(BendingSim::ETechnique::RockThrow), "Technique mirror out of sync");
static_assert(static_cast<int>(EBendingTechnique::FireBlast) == static_cast<int>(BendingSim::ETechnique::FireBlast), "Technique mirror out of sync");
static_assert(static_cast<int>(EBendingTechnique::AirJump) == static_cast<int>(BendingSim::ETechnique::AirJump), "Technique mirror out of sync");
static_assert(static_cast<int>(EBendingTechnique::Count) == static_cast<int>(BendingSim::ETechnique::Count), "Technique mirror out of sync");
static_assert(static_cast<int>(EBendingElement::Earth) == static_cast<int>(BendingSim::ETechniqueElement::Earth)
	&& static_cast<int>(EBendingElement::Water) == static_cast<int>(BendingSim::ETechniqueElement::Water)
	&& static_cast<int>(EBendingElement::Fire) == static_cast<int>(BendingSim::ETechniqueElement::Fire)
	&& static_cast<int>(EBendingElement::Air) == static_cast<int>(BendingSim::ETechniqueElement::Air),
	"Technique element mirror out of sync with EBendingElement");

namespace BendingTechnique
{
	[[nodiscard]] constexpr BendingSim::ETechnique ToSim(EBendingTechnique Technique)
	{
		return static_cast<BendingSim::ETechnique>(Technique);
	}

	[[nodiscard]] constexpr EBendingTechnique FromSim(BendingSim::ETechnique Technique)
	{
		return static_cast<EBendingTechnique>(Technique);
	}

	[[nodiscard]] constexpr EBendingElement ToElement(BendingSim::ETechniqueElement Element)
	{
		return static_cast<EBendingElement>(Element);
	}

	[[nodiscard]] constexpr BendingSim::ETechniqueElement FromElement(EBendingElement Element)
	{
		return static_cast<BendingSim::ETechniqueElement>(Element);
	}

	[[nodiscard]] inline const BendingSim::FTechniqueInfo& GetInfo(EBendingTechnique Technique)
	{
		return BendingSim::GetTechniqueInfo(ToSim(Technique));
	}

	/** Input.Bending.Light / Heavy / Special / Utility for the Primary (LMB) / Secondary (RMB) / Special (Q) / Utility (E) slots. */
	BENDING_API FGameplayTag GetInputTag(BendingSim::ETechniqueSlot Slot);

	/** Key label for the HUD: LMB, RMB, Q, E. */
	BENDING_API const TCHAR* GetKeyLabel(BendingSim::ETechniqueSlot Slot);

	BENDING_API FString GetDisplayName(EBendingTechnique Technique);
	BENDING_API FString GetDescription(EBendingTechnique Technique);

	/** The technique a stance maps an input slot to (None if that slot is empty in the stance). */
	BENDING_API EBendingTechnique FindTechnique(EBendingElement Element, BendingSim::ETechniqueSlot Slot);
}
