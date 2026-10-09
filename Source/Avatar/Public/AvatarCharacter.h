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
class UBendingDiscipline;
class UBendingInputConfig;
class UBendingTechniqueComponent;
class UCameraComponent;
class UInputAction;
class UInputMappingContext;
class UMotionWarpingComponent;
class USceneComponent;
class USpringArmComponent;
class UStaticMeshComponent;
struct FInputActionValue;

/** Joint angles (degrees) and vertical bob (cm) of the blocky body. */
struct FAvatarBodyPose
{
	float LegL = 0.f;
	float LegR = 0.f;
	float ArmL = 0.f;
	float ArmR = 0.f;
	/** Arms lifted out sideways (left positive, right negative). */
	float ArmRollL = 0.f;
	float ArmRollR = 0.f;
	/** Negative leans forward. */
	float SpinePitch = 0.f;
	float SpineYaw = 0.f;
	float BobCm = 0.f;
};

/**
 * Playable bender. Owns its ability system (single player: the pawn is the owner and avatar), the
 * bending component, Motion Warping for strike alignment, and a third-person camera.
 *
 * Root motion drives the body during moves; ground traction drops on mud from waterbending.
 *
 * Works without any assets: a low-poly body of engine basic shapes, animated procedurally (walk and run cycle,
 * jump pose, and a casting pose driven by the current move's Startup / Active / Recovery frames), the sandbox
 * techniques granted as disciplines, and Enhanced Input objects built at runtime when none are assigned.
 */
UCLASS(Config = Game)
class AVATAR_API AAvatarCharacter : public ACharacter, public IAbilitySystemInterface
{
	GENERATED_BODY()

public:
	AAvatarCharacter(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;

	UBendingComponent* GetBendingComponent() const { return Bending; }
	UBendingTechniqueComponent* GetTechniqueComponent() const { return Techniques; }

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
	void Input_SprintStarted();
	void Input_SprintCompleted();
	void Input_JumpStarted();
	void Input_GuardStarted();
	void Input_GuardCompleted();
	void Input_ToggleHelp();

	/** Scales ground friction, braking and acceleration by the mud under the feet. */
	void UpdateGroundTraction();

	/** Builds input actions, the bending input config and a mapping context when no mapping context is assigned. */
	void EnsureRuntimeInput();

	/** Grants one discipline per element built from the kernel's technique table, unless disciplines were assigned. */
	void GrantSandboxDisciplines();

	/**
	 * Faces the aim while bending or guarding (strafing), the direction of travel otherwise; slows down while casting
	 * or guarding, and lies still while knocked down.
	 */
	void UpdateMovementMode();

	void UpdateBodyAnimation(float DeltaSeconds);

	/** Arm and spine pose of the current move; returns how strongly it overrides locomotion (0..1). */
	float ComputeCastPose(FAvatarBodyPose& InOutPose) const;

	/** Shirt and sash take the colour of the active stance. */
	void UpdateElementTint();

	float GetAimPitchDegrees() const;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Bending")
	TObjectPtr<UAbilitySystemComponent> AbilitySystem;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Bending")
	TObjectPtr<UBendingAttributeSet> Attributes;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Bending")
	TObjectPtr<UBendingComponent> Bending;

	/** Executes the sandbox techniques (water whip, rock throw, terraforming, fire, air) for the granted moves. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Bending")
	TObjectPtr<UBendingTechniqueComponent> Techniques;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Bending")
	TObjectPtr<UMotionWarpingComponent> MotionWarping;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
	TObjectPtr<USpringArmComponent> CameraBoom;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
	TObjectPtr<UCameraComponent> FollowCamera;

	// ---------------------------------------------------------------- Body (engine basic shapes, no collision)

	UPROPERTY(VisibleAnywhere, Category = "Body")
	TObjectPtr<USceneComponent> BodyRoot;

	UPROPERTY(VisibleAnywhere, Category = "Body")
	TObjectPtr<USceneComponent> Spine;

	UPROPERTY(VisibleAnywhere, Category = "Body")
	TObjectPtr<USceneComponent> HipL;

	UPROPERTY(VisibleAnywhere, Category = "Body")
	TObjectPtr<USceneComponent> HipR;

	UPROPERTY(VisibleAnywhere, Category = "Body")
	TObjectPtr<USceneComponent> ShoulderL;

	UPROPERTY(VisibleAnywhere, Category = "Body")
	TObjectPtr<USceneComponent> ShoulderR;

	/** End of the right arm: where water, fire and air leave the body. */
	UPROPERTY(VisibleAnywhere, Category = "Body")
	TObjectPtr<USceneComponent> HandR;

	UPROPERTY(VisibleAnywhere, Category = "Body")
	TObjectPtr<UStaticMeshComponent> Torso;

	UPROPERTY(VisibleAnywhere, Category = "Body")
	TObjectPtr<UStaticMeshComponent> Sash;

	/** Every other body part, coloured once at BeginPlay. */
	UPROPERTY(VisibleAnywhere, Category = "Body")
	TArray<TObjectPtr<UStaticMeshComponent>> BodyParts;

	// ---------------------------------------------------------------- Input

	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputMappingContext> DefaultMappingContext;

	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputAction> MoveAction;

	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputAction> LookAction;

	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputAction> JumpAction;

	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputAction> SprintAction;

	/** Shows or hides the controls panel of AAvatarHUD. */
	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputAction> ToggleHelpAction;

	/** Held: guard against the sparring partner's flames (raised just as one arrives, it parries). */
	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputAction> GuardAction;

	/** Move inputs (Light/Heavy/Special/Utility/Signature) and stance selection. */
	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UBendingInputConfig> BendingInputConfig;

	// ---------------------------------------------------------------- Movement

	UPROPERTY(EditDefaultsOnly, Category = "Movement", meta = (ClampMin = 0.0))
	float WalkSpeed = 500.f;

	UPROPERTY(EditDefaultsOnly, Category = "Movement", meta = (ClampMin = 0.0))
	float SprintSpeed = 800.f;

	/** Speed multiplier during a move's startup and active frames and while holding a technique. */
	UPROPERTY(EditDefaultsOnly, Category = "Movement", meta = (ClampMin = 0.0, ClampMax = 1.0))
	float CastingSpeedScale = 0.6f;

private:
	/** Granted sandbox disciplines (runtime objects): referenced here for as long as they are granted. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UBendingDiscipline>> SandboxDisciplines;

	TArray<FLinearColor> BodyPartColors;
	FAvatarBodyPose CurrentPose;
	float WalkPhase = 0.f;
	float CastWeight = 0.f;
	/** Body lift while perched on an air scooter (cm). */
	float ScooterLiftCm = 0.f;
	/** 0 standing, 1 lying on the ground (knocked down). */
	float DownWeight = 0.f;
	EBendingElement TintedElement = EBendingElement::None;
	bool bBodyTinted = false;
	bool bSprinting = false;

	float BaseGroundFriction = 8.f;
	float BaseBrakingDecelerationWalking = 2048.f;
	float BaseMaxAcceleration = 2048.f;
};
