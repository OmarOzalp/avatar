#pragma once

#include "CoreMinimal.h"
#include "Sim/BendingSimReactions.h"
#include "Physics/ElementalSubstance.h"
#include "UObject/Object.h"
#include "ElementalReaction.generated.h"

/**
 * Designer-facing reaction rule. The physics runs in the engine-independent kernel (Sim/BendingSimReactions.h);
 * subclasses expose its parameters as properties and forward React to the kernel function, so the game runs the
 * same reaction code that Tests/run_all.sh verifies (results can differ in the last bits: float properties and the
 * engine's floating-point compiler settings).
 *
 * Reactions exchange conserved quantities (mass, momentum, heat) instead of scripting outcomes, so results scale
 * with the matter involved. Strength is the explicit gameplay multiplier on top of the physical rate.
 * Custom rules can override React and work on the context directly.
 */
UCLASS(Abstract, EditInlineNew, DefaultToInstanced, CollapseCategories, BlueprintType)
class BENDING_API UElementalReaction : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Reaction")
	EElementalSubstance SubstanceA = EElementalSubstance::None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Reaction")
	EElementalSubstance SubstanceB = EElementalSubstance::None;

	/** Gameplay multiplier on the physical rate. 1 = physical. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Reaction", meta = (ClampMin = 0.0))
	float Strength = 1.f;

	/**
	 * Runs once per fixed step for each overlapping pair. Context A has GetRequiredSubstanceA() when the rule has one
	 * (the pair is reordered at registration), otherwise SubstanceA.
	 */
	virtual void React(BendingSim::FReactionContext& Context) const
	{
	}

	/** Substance this rule's physics treats as A (the air in drag, the water in saturation); None if either order works. */
	virtual EElementalSubstance GetRequiredSubstanceA() const { return EElementalSubstance::None; }

	/**
	 * Kernel registration entry that calls back into React, with the pair ordered as the rule needs.
	 * False when neither substance is the required one. The reaction must outlive the simulation world.
	 */
	[[nodiscard]] bool MakeSimEntry(BendingSim::FReactionEntry& OutEntry) const;
};
