#pragma once

#include "CoreMinimal.h"
#include "GameplayEffect.h"
#include "BendingGameplayEffects.generated.h"

/**
 * Instant resource change driven by SetByCaller.Bending.Chi / SetByCaller.Bending.Stamina.
 * Used for move costs and for joule-priced work, so no per-move cost assets are needed.
 */
UCLASS()
class BENDING_API UBendingCostEffect : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UBendingCostEffect();
};

/** Infinite periodic regeneration of Chi and Stamina from their *RegenRate attributes. */
UCLASS()
class BENDING_API UBendingResourceRegenEffect : public UGameplayEffect
{
	GENERATED_BODY()

public:
	static constexpr float TickSeconds = 0.1f;

	UBendingResourceRegenEffect();
};
