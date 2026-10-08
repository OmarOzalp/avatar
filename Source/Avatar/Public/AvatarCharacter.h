#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemInterface.h"
#include "BendingTypes.h"
#include "GameFramework/Character.h"
#include "GameplayTagContainer.h"
#include "AvatarCharacter.generated.h"

class UAbilitySystemComponent;
class UBendingAttributeSet;
class UBendingComponent;
class UBendingInputConfig;
class UCameraComponent;
class UInputAction;
class UInputMappingContext;
class UMotionWarpingComponent;
class USpringArmComponent;
struct FInputActionValue;

/**
 * Playable bender. Owns its ability system (single player: the pawn is the owner and avatar), the
 * bending component, Motion Warping for strike alignment, and a third-person camera.
 *
 * Root motion drives the body during moves; ground traction drops on mud from waterbending.
 */
UCLASS(Config = Game)
class AVATAR_API AAvatarCharacter : public ACharacter, public IAbilitySystemInterface
{
	GENERATED_BODY()

public:
	AAvatarCharacter(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;

	UBendingComponent* GetBendingComponent() const { return Bending; }

protected:
	virtual void BeginPlay() override;
	virtual void PossessedBy(AController* NewController) override;
	virtual void NotifyControllerChanged() override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	virtual void Tick(float DeltaSeconds) override;

	void Input_Move(const FInputActionValue& Value);
	void Input_Look(const FInputActionValue& Value);
	void Input_BendingPressed(FGameplayTag InputTag);
	void Input_BendingReleased(FGameplayTag InputTag);
	void Input_SelectStance(EBendingElement Element);

	/** Scales ground friction, braking and acceleration by the mud under the feet. */
	void UpdateGroundTraction();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Bending")
	TObjectPtr<UAbilitySystemComponent> AbilitySystem;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Bending")
	TObjectPtr<UBendingAttributeSet> Attributes;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Bending")
	TObjectPtr<UBendingComponent> Bending;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Bending")
	TObjectPtr<UMotionWarpingComponent> MotionWarping;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
	TObjectPtr<USpringArmComponent> CameraBoom;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
	TObjectPtr<UCameraComponent> FollowCamera;

	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputMappingContext> DefaultMappingContext;

	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputAction> MoveAction;

	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputAction> LookAction;

	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputAction> JumpAction;

	/** Move inputs (Light/Heavy/Special/Utility) and stance selection. */
	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UBendingInputConfig> BendingInputConfig;

private:
	float BaseGroundFriction = 8.f;
	float BaseBrakingDecelerationWalking = 2048.f;
	float BaseMaxAcceleration = 2048.f;
};
