#include "AvatarCharacter.h"

#include "AbilitySystemComponent.h"
#include "Attributes/BendingAttributeSet.h"
#include "Camera/CameraComponent.h"
#include "Components/BendingComponent.h"
#include "Components/CapsuleComponent.h"
#include "Data/BendingInputConfig.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "InputActionValue.h"
#include "Interaction/BendingInteractionSubsystem.h"
#include "MotionWarpingComponent.h"

AAvatarCharacter::AAvatarCharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryActorTick.bCanEverTick = true;

	AbilitySystem = CreateDefaultSubobject<UAbilitySystemComponent>(TEXT("AbilitySystem"));
	AbilitySystem->SetIsReplicated(true);
	AbilitySystem->SetReplicationMode(EGameplayEffectReplicationMode::Mixed);

	// Attribute sets that are default subobjects of the owner register with its ability system automatically.
	Attributes = CreateDefaultSubobject<UBendingAttributeSet>(TEXT("Attributes"));

	Bending = CreateDefaultSubobject<UBendingComponent>(TEXT("Bending"));
	MotionWarping = CreateDefaultSubobject<UMotionWarpingComponent>(TEXT("MotionWarping"));

	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(RootComponent);
	CameraBoom->TargetArmLength = 420.f;
	CameraBoom->bUsePawnControlRotation = true;

	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	FollowCamera->bUsePawnControlRotation = false;

	bUseControllerRotationYaw = false;
	UCharacterMovementComponent* Movement = GetCharacterMovement();
	Movement->bOrientRotationToMovement = true;
	Movement->RotationRate = FRotator(0.0, 720.0, 0.0);
}

UAbilitySystemComponent* AAvatarCharacter::GetAbilitySystemComponent() const
{
	return AbilitySystem;
}

void AAvatarCharacter::BeginPlay()
{
	// Before Super::BeginPlay so UBendingComponent::BeginPlay finds an initialized ability system.
	AbilitySystem->InitAbilityActorInfo(this, this);

	const UCharacterMovementComponent* Movement = GetCharacterMovement();
	BaseGroundFriction = Movement->GroundFriction;
	BaseBrakingDecelerationWalking = Movement->BrakingDecelerationWalking;
	BaseMaxAcceleration = Movement->MaxAcceleration;

	Super::BeginPlay();
}

void AAvatarCharacter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);
	AbilitySystem->RefreshAbilityActorInfo();
}

void AAvatarCharacter::NotifyControllerChanged()
{
	Super::NotifyControllerChanged();

	const APlayerController* PlayerController = Cast<APlayerController>(Controller);
	if (!PlayerController || !DefaultMappingContext)
	{
		return;
	}
	if (UEnhancedInputLocalPlayerSubsystem* InputSubsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PlayerController->GetLocalPlayer()))
	{
		InputSubsystem->AddMappingContext(DefaultMappingContext, 0);
	}
}

void AAvatarCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	UEnhancedInputComponent* Input = Cast<UEnhancedInputComponent>(PlayerInputComponent);
	if (!Input)
	{
		return;
	}

	if (MoveAction)
	{
		Input->BindAction(MoveAction, ETriggerEvent::Triggered, this, &ThisClass::Input_Move);
	}
	if (LookAction)
	{
		Input->BindAction(LookAction, ETriggerEvent::Triggered, this, &ThisClass::Input_Look);
	}
	if (JumpAction)
	{
		Input->BindAction(JumpAction, ETriggerEvent::Started, this, &ACharacter::Jump);
		Input->BindAction(JumpAction, ETriggerEvent::Completed, this, &ACharacter::StopJumping);
	}

	if (BendingInputConfig)
	{
		for (const FBendingInputAction& Binding : BendingInputConfig->MoveInputs)
		{
			if (Binding.InputAction && Binding.InputTag.IsValid())
			{
				Input->BindAction(Binding.InputAction, ETriggerEvent::Started, this, &ThisClass::Input_BendingPressed, Binding.InputTag);
				Input->BindAction(Binding.InputAction, ETriggerEvent::Completed, this, &ThisClass::Input_BendingReleased, Binding.InputTag);
			}
		}
		for (const FBendingStanceInputAction& Binding : BendingInputConfig->StanceInputs)
		{
			if (Binding.InputAction)
			{
				Input->BindAction(Binding.InputAction, ETriggerEvent::Started, this, &ThisClass::Input_SelectStance, Binding.Element);
			}
		}
	}
}

void AAvatarCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UpdateGroundTraction();
}

void AAvatarCharacter::Input_Move(const FInputActionValue& Value)
{
	const FVector2D Axis = Value.Get<FVector2D>();
	if (!Controller)
	{
		return;
	}
	const FRotator YawRotation(0.0, Controller->GetControlRotation().Yaw, 0.0);
	AddMovementInput(FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X), Axis.Y);
	AddMovementInput(FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y), Axis.X);
}

void AAvatarCharacter::Input_Look(const FInputActionValue& Value)
{
	const FVector2D Axis = Value.Get<FVector2D>();
	AddControllerYawInput(Axis.X);
	AddControllerPitchInput(Axis.Y);
}

void AAvatarCharacter::Input_BendingPressed(FGameplayTag InputTag)
{
	Bending->HandleInputPressed(InputTag);
}

void AAvatarCharacter::Input_BendingReleased(FGameplayTag InputTag)
{
	Bending->HandleInputReleased(InputTag);
}

void AAvatarCharacter::Input_SelectStance(EBendingElement Element)
{
	Bending->SetActiveElement(Element);
}

void AAvatarCharacter::UpdateGroundTraction()
{
	UCharacterMovementComponent* Movement = GetCharacterMovement();
	const UBendingInteractionSubsystem* Interaction = GetWorld()->GetSubsystem<UBendingInteractionSubsystem>();
	if (!Movement || !Interaction)
	{
		return;
	}

	float Traction = 1.f;
	if (Movement->IsMovingOnGround())
	{
		const FVector Feet = GetActorLocation() - FVector(0.0, 0.0, GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
		Traction = Interaction->GetSurfaceTractionMultiplierAt(Feet);
	}

	Movement->GroundFriction = BaseGroundFriction * Traction;
	Movement->BrakingDecelerationWalking = BaseBrakingDecelerationWalking * Traction;
	// Mud costs push-off too, but less than it costs braking: you can still run, you just can't stop.
	Movement->MaxAcceleration = BaseMaxAcceleration * FMath::Lerp(1.f, Traction, 0.5f);
}
