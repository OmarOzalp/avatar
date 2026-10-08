#pragma once

#include "CoreMinimal.h"
#include "BendingTypes.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "BendingMoveDefinition.generated.h"

class UAnimMontage;
class UBendingGameplayAbility;

/**
 * One bending move: animation, frame data, cost, targeting and the element payload.
 * Designers author these; UBendingComponent grants one ability spec per move with the move as its source object.
 */
UCLASS(BlueprintType, Const)
class BENDING_API UBendingMoveDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	static const FPrimaryAssetType PrimaryAssetType;

	virtual FPrimaryAssetId GetPrimaryAssetId() const override;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif

	// ---------------------------------------------------------------- Identity

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Move")
	FText DisplayName;

	/** Element stance this move belongs to. None = available in every stance (dodges, guards). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Move")
	EBendingElement Element = EBendingElement::None;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Move", meta = (Categories = "Input.Bending"))
	FGameplayTag InputTag;

	/** When several moves share an input in the same stance, the highest priority that can activate wins (e.g. airborne variants). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Move")
	int32 Priority = 0;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Move")
	TSubclassOf<UBendingGameplayAbility> AbilityClass;

	// ---------------------------------------------------------------- Animation & frame data

	/** Root-motion montage. Mark phases with "Bending Phase" notify states and the warp window with a Motion Warping notify. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Animation")
	TSoftObjectPtr<UAnimMontage> Montage;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Animation")
	FBendingFrameData FrameData;

	// ---------------------------------------------------------------- Targeting

	/** Close distance and face the soft target during startup via Motion Warping. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Targeting")
	bool bUseMotionWarping = true;

	/** Must match the Warp Target Name on the montage's Motion Warping notify. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Targeting", meta = (EditCondition = "bUseMotionWarping"))
	FName WarpTargetName = TEXT("BendingTarget");

	/** Furthest the warp may displace the root toward the target. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Targeting", meta = (EditCondition = "bUseMotionWarping", ClampMin = 0.0, Units = "cm"))
	float MaxWarpDistanceCm = 250.f;

	/** Distance kept from the target's root at the end of the warp (striking range). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Targeting", meta = (EditCondition = "bUseMotionWarping", ClampMin = 0.0, Units = "cm"))
	float WarpStandoffCm = 130.f;

	/** Keep updating the warp target through startup so the move tracks a moving opponent. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Targeting", meta = (EditCondition = "bUseMotionWarping"))
	bool bTrackTargetDuringStartup = true;

	// ---------------------------------------------------------------- Cost

	/** Flat chi committed at activation. Physical work done by the element is billed separately in joules. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Cost", meta = (ClampMin = 0.0))
	float ChiCost = 10.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Cost", meta = (ClampMin = 0.0))
	float StaminaCost = 5.f;

	// ---------------------------------------------------------------- Element payload (read by the ability subclass)

	/** Pooled element actor (boulder, water body, fire blast, air wave) the active frames spawn or take control of. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Element")
	TSoftClassPtr<AActor> ElementActorClass;

	/** Skeletal socket the element is emitted from (hand, foot). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Element")
	FName EmitSocketName = TEXT("hand_r");

	/** Mass of matter the move bends (kg): boulder size, water drawn, flame mass. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Element", meta = (ClampMin = 0.0, Units = "kg"))
	float ElementMassKg = 50.f;

	/** Launch speed at release (m/s). Kinetic energy 1/2 m v^2 is billed as chi on top of ChiCost. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Element", meta = (ClampMin = 0.0))
	float LaunchSpeedMs = 20.f;
};
