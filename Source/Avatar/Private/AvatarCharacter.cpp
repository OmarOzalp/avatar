#include "AvatarCharacter.h"

#include "AbilitySystemComponent.h"
#include "Attributes/BendingAttributeSet.h"
#include "AvatarHUD.h"
#include "BendingGameplayTags.h"
#include "Camera/CameraComponent.h"
#include "Components/BendingComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Data/BendingDiscipline.h"
#include "Data/BendingInputConfig.h"
#include "Engine/CollisionProfile.h"
#include "Engine/LocalPlayer.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EnhancedActionKeyMapping.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "InputCoreTypes.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "Interaction/BendingInteractionSubsystem.h"
#include "MotionWarpingComponent.h"
#include "Sandbox/BendingSandboxLibrary.h"
#include "Sandbox/BendingTechniqueComponent.h"
#include "Sandbox/BendingTechniqueMove.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	/** Maps a key onto an action. A 1D key moved to Y (swizzle) drives forward/back; negate flips its sign. */
	void MapRuntimeKey(UInputMappingContext* Context, const UInputAction* Action, const FKey& Key, bool bSwizzleToY = false, bool bNegate = false)
	{
		FEnhancedActionKeyMapping& Mapping = Context->MapKey(Action, Key);
		if (bSwizzleToY)
		{
			Mapping.Modifiers.Add(NewObject<UInputModifierSwizzleAxis>(Context));
		}
		if (bNegate)
		{
			Mapping.Modifiers.Add(NewObject<UInputModifierNegate>(Context));
		}
	}

	FAvatarBodyPose LerpPose(const FAvatarBodyPose& A, const FAvatarBodyPose& B, float Alpha)
	{
		FAvatarBodyPose Out;
		Out.LegL = FMath::Lerp(A.LegL, B.LegL, Alpha);
		Out.LegR = FMath::Lerp(A.LegR, B.LegR, Alpha);
		Out.ArmL = FMath::Lerp(A.ArmL, B.ArmL, Alpha);
		Out.ArmR = FMath::Lerp(A.ArmR, B.ArmR, Alpha);
		Out.ArmRollL = FMath::Lerp(A.ArmRollL, B.ArmRollL, Alpha);
		Out.ArmRollR = FMath::Lerp(A.ArmRollR, B.ArmRollR, Alpha);
		Out.SpinePitch = FMath::Lerp(A.SpinePitch, B.SpinePitch, Alpha);
		Out.SpineYaw = FMath::Lerp(A.SpineYaw, B.SpineYaw, Alpha);
		Out.BobCm = FMath::Lerp(A.BobCm, B.BobCm, Alpha);
		return Out;
	}

	FAvatarBodyPose InterpPose(const FAvatarBodyPose& Current, const FAvatarBodyPose& Target, float DeltaSeconds, float Speed)
	{
		// Frame-rate independent exponential approach, shared by every joint.
		return LerpPose(Current, Target, FMath::Clamp(DeltaSeconds * Speed, 0.f, 1.f));
	}

	bool IsTwoHanded(EBendingTechnique Technique)
	{
		return Technique == EBendingTechnique::RockThrow || Technique == EBendingTechnique::EarthWall
			|| Technique == EBendingTechnique::RaiseGround || Technique == EBendingTechnique::LowerGround;
	}

	/** Full-body poses of the signature moves, Jet Dash and the air scooter. False for every other technique. */
	bool ComputeSignaturePose(EBendingTechnique Technique, EBendingPhase Phase, float Ease, FAvatarBodyPose& Pose)
	{
		const bool bStartup = Phase == EBendingPhase::Startup;
		switch (Technique)
		{
		case EBendingTechnique::Earthquake:
			// Both arms rise high, then slam down with the stomp.
			Pose.ArmL = Pose.ArmR = bStartup ? FMath::Lerp(10.f, 160.f, Ease) : -15.f;
			Pose.ArmRollL = 15.f;
			Pose.ArmRollR = -15.f;
			Pose.SpinePitch = bStartup ? -8.f * Ease : 20.f;
			Pose.LegL = bStartup ? 25.f * Ease : 10.f;
			Pose.BobCm = bStartup ? 4.f * Ease : -14.f;
			return true;

		case EBendingTechnique::FireRing:
			// Arms sweep out wide and the body twists into the spin kick.
			Pose.ArmL = Pose.ArmR = 0.f;
			Pose.ArmRollL = FMath::Lerp(10.f, 85.f, bStartup ? Ease : 1.f);
			Pose.ArmRollR = -Pose.ArmRollL;
			Pose.SpineYaw = bStartup ? -35.f * Ease : 40.f;
			Pose.LegR = bStartup ? 0.f : 55.f;
			return true;

		case EBendingTechnique::Tornado:
			// Arms circle up over the head.
			Pose.ArmL = Pose.ArmR = bStartup ? FMath::Lerp(40.f, 165.f, Ease) : 165.f;
			Pose.ArmRollL = 25.f;
			Pose.ArmRollR = -25.f;
			Pose.SpineYaw = bStartup ? 30.f * FMath::Sin(Ease * 2.f * UE_PI) : 0.f;
			return true;

		case EBendingTechnique::FireJet:
			// Crouch, then shoot forward with the arms swept back.
			Pose.ArmL = Pose.ArmR = bStartup ? -20.f * Ease : -60.f;
			Pose.ArmRollL = 20.f;
			Pose.ArmRollR = -20.f;
			Pose.SpinePitch = bStartup ? -18.f * Ease : -32.f;
			Pose.LegL = bStartup ? 20.f * Ease : 35.f;
			Pose.LegR = bStartup ? -10.f * Ease : -35.f;
			Pose.BobCm = bStartup ? -10.f * Ease : 0.f;
			return true;

		case EBendingTechnique::AirScooter:
			// Balancing on the ball: knees soft, arms out a little.
			Pose.ArmL = Pose.ArmR = 25.f;
			Pose.ArmRollL = 40.f;
			Pose.ArmRollR = -40.f;
			Pose.SpinePitch = -12.f;
			Pose.LegL = 12.f;
			Pose.LegR = -8.f;
			return true;

		default:
			return false;
		}
	}
}

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
	Techniques = CreateDefaultSubobject<UBendingTechniqueComponent>(TEXT("Techniques"));
	MotionWarping = CreateDefaultSubobject<UMotionWarpingComponent>(TEXT("MotionWarping"));

	GetCapsuleComponent()->InitCapsuleSize(34.f, 88.f);

	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(RootComponent);
	CameraBoom->TargetArmLength = 380.f;
	// Over the right shoulder, so the crosshair is not hidden behind the body.
	CameraBoom->SocketOffset = FVector(0.0, 50.0, 70.0);
	CameraBoom->bUsePawnControlRotation = true;

	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	FollowCamera->bUsePawnControlRotation = false;

	bUseControllerRotationYaw = false;
	UCharacterMovementComponent* Movement = GetCharacterMovement();
	Movement->bOrientRotationToMovement = true;
	Movement->RotationRate = FRotator(0.0, 720.0, 0.0);
	Movement->MaxWalkSpeed = WalkSpeed;
	Movement->JumpZVelocity = 480.f;
	Movement->AirControl = 0.35f;

	// The blocky body below replaces the skeletal mesh.
	GetMesh()->SetHiddenInGame(true);
	GetMesh()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	GetMesh()->PrimaryComponentTick.bStartWithTickEnabled = false;

	// ---------------------------------------------------------------- Low-poly body (capsule: radius 34, half height 88)

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereFinder(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderFinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	UStaticMesh* Cube = CubeFinder.Object;
	UStaticMesh* Sphere = SphereFinder.Object;
	UStaticMesh* Cylinder = CylinderFinder.Object;

	auto MakePivot = [this](const TCHAR* Name, USceneComponent* Parent, const FVector& Location)
	{
		USceneComponent* Pivot = CreateDefaultSubobject<USceneComponent>(Name);
		Pivot->SetupAttachment(Parent);
		Pivot->SetRelativeLocation(Location);
		return Pivot;
	};
	// Basic shapes are 100 cm across with the pivot at their centre.
	auto MakePart = [this](const TCHAR* Name, USceneComponent* Parent, UStaticMesh* ShapeMesh, const FVector& Location, const FVector& SizeCm)
	{
		UStaticMeshComponent* Part = CreateDefaultSubobject<UStaticMeshComponent>(Name);
		Part->SetupAttachment(Parent);
		Part->SetStaticMesh(ShapeMesh);
		Part->SetRelativeLocation(Location);
		Part->SetRelativeScale3D(SizeCm / 100.0);
		Part->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
		Part->SetGenerateOverlapEvents(false);
		Part->SetCanEverAffectNavigation(false);
		Part->CanCharacterStepUpOn = ECB_No;
		return Part;
	};
	auto AddPart = [this](UStaticMeshComponent* Part, const FLinearColor& Color)
	{
		BodyParts.Add(Part);
		BodyPartColors.Add(Color);
	};

	const FLinearColor Skin = UBendingSandboxLibrary::FromSRGB(226, 176, 130);
	const FLinearColor Trousers = UBendingSandboxLibrary::FromSRGB(64, 56, 48);
	const FLinearColor Boots = UBendingSandboxLibrary::FromSRGB(40, 32, 28);
	const FLinearColor Hair = UBendingSandboxLibrary::FromSRGB(46, 32, 22);
	const FLinearColor Eyes = UBendingSandboxLibrary::FromSRGB(20, 20, 20);

	BodyRoot = MakePivot(TEXT("BodyRoot"), GetCapsuleComponent(), FVector::ZeroVector);

	// Legs hang from the hips and reach the bottom of the capsule.
	HipL = MakePivot(TEXT("HipL"), BodyRoot, FVector(0.0, -11.0, -6.0));
	HipR = MakePivot(TEXT("HipR"), BodyRoot, FVector(0.0, 11.0, -6.0));
	AddPart(MakePart(TEXT("LegL"), HipL, Cylinder, FVector(0.0, 0.0, -41.0), FVector(16.0, 16.0, 82.0)), Trousers);
	AddPart(MakePart(TEXT("LegR"), HipR, Cylinder, FVector(0.0, 0.0, -41.0), FVector(16.0, 16.0, 82.0)), Trousers);
	AddPart(MakePart(TEXT("FootL"), HipL, Cube, FVector(6.0, 0.0, -84.0), FVector(24.0, 12.0, 8.0)), Boots);
	AddPart(MakePart(TEXT("FootR"), HipR, Cube, FVector(6.0, 0.0, -84.0), FVector(24.0, 12.0, 8.0)), Boots);
	AddPart(MakePart(TEXT("Pelvis"), BodyRoot, Cube, FVector(0.0, 0.0, -2.0), FVector(22.0, 34.0, 16.0)), Trousers);

	// Upper body turns and leans around the waist.
	Spine = MakePivot(TEXT("Spine"), BodyRoot, FVector(0.0, 0.0, 4.0));
	Torso = MakePart(TEXT("Torso"), Spine, Cube, FVector(0.0, 0.0, 24.0), FVector(22.0, 38.0, 44.0));
	Sash = MakePart(TEXT("Sash"), Spine, Cube, FVector(0.0, 0.0, 3.0), FVector(24.0, 40.0, 8.0));
	AddPart(MakePart(TEXT("Head"), Spine, Sphere, FVector(0.0, 0.0, 68.0), FVector(26.0)), Skin);
	AddPart(MakePart(TEXT("Hair"), Spine, Sphere, FVector(-2.0, 0.0, 74.0), FVector(27.0, 27.0, 18.0)), Hair);
	// Eyes show which way the body faces.
	AddPart(MakePart(TEXT("EyeL"), Spine, Cube, FVector(12.0, -5.0, 70.0), FVector(3.0)), Eyes);
	AddPart(MakePart(TEXT("EyeR"), Spine, Cube, FVector(12.0, 5.0, 70.0), FVector(3.0)), Eyes);

	// Arms hang from the shoulders; positive pitch swings them forward.
	ShoulderL = MakePivot(TEXT("ShoulderL"), Spine, FVector(0.0, -24.0, 44.0));
	ShoulderR = MakePivot(TEXT("ShoulderR"), Spine, FVector(0.0, 24.0, 44.0));
	AddPart(MakePart(TEXT("ArmL"), ShoulderL, Cylinder, FVector(0.0, 0.0, -30.0), FVector(12.0, 12.0, 60.0)), Skin);
	AddPart(MakePart(TEXT("ArmR"), ShoulderR, Cylinder, FVector(0.0, 0.0, -30.0), FVector(12.0, 12.0, 60.0)), Skin);
	AddPart(MakePart(TEXT("FistL"), ShoulderL, Sphere, FVector(0.0, 0.0, -62.0), FVector(14.0)), Skin);
	AddPart(MakePart(TEXT("FistR"), ShoulderR, Sphere, FVector(0.0, 0.0, -62.0), FVector(14.0)), Skin);
	HandR = MakePivot(TEXT("HandR"), ShoulderR, FVector(0.0, 0.0, -64.0));
}

UAbilitySystemComponent* AAvatarCharacter::GetAbilitySystemComponent() const
{
	return AbilitySystem;
}

void AAvatarCharacter::BeginPlay()
{
	// Before Super::BeginPlay so UBendingComponent::BeginPlay finds an initialized ability system.
	AbilitySystem->InitAbilityActorInfo(this, this);
	Techniques->SetHandComponent(HandR);

	UCharacterMovementComponent* Movement = GetCharacterMovement();
	Movement->MaxWalkSpeed = WalkSpeed;
	BaseGroundFriction = Movement->GroundFriction;
	BaseBrakingDecelerationWalking = Movement->BrakingDecelerationWalking;
	BaseMaxAcceleration = Movement->MaxAcceleration;

	Super::BeginPlay();

	GrantSandboxDisciplines();
	for (int32 Index = 0; Index < BodyParts.Num() && Index < BodyPartColors.Num(); ++Index)
	{
		UBendingSandboxLibrary::SetMeshColor(BodyParts[Index], BodyPartColors[Index]);
	}
	UpdateElementTint();
}

void AAvatarCharacter::GrantSandboxDisciplines()
{
	const bool bHasAnyDiscipline = Bending->HasDiscipline(EBendingElement::Water) || Bending->HasDiscipline(EBendingElement::Earth)
		|| Bending->HasDiscipline(EBendingElement::Fire) || Bending->HasDiscipline(EBendingElement::Air);
	if (!bHasAnyDiscipline && HasAuthority())
	{
		for (UBendingDiscipline* Discipline : UBendingSandboxLibrary::CreateTechniqueDisciplines(this))
		{
			SandboxDisciplines.Add(Discipline);
			Bending->GrantDiscipline(Discipline);
		}
	}
	Bending->SetActiveElement(EBendingElement::Water);
}

void AAvatarCharacter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);
	AbilitySystem->RefreshAbilityActorInfo();
}

// ---------------------------------------------------------------------------------------------------- Input

void AAvatarCharacter::EnsureRuntimeInput()
{
	if (DefaultMappingContext)
	{
		return;
	}
	UInputMappingContext* Context = NewObject<UInputMappingContext>(this);

	auto MakeAction = [this](EInputActionValueType ValueType)
	{
		UInputAction* Action = NewObject<UInputAction>(this);
		Action->ValueType = ValueType;
		return Action;
	};

	if (!MoveAction)
	{
		// Input_Move reads forward from Y and right from X.
		MoveAction = MakeAction(EInputActionValueType::Axis2D);
		MapRuntimeKey(Context, MoveAction, EKeys::W, /*bSwizzleToY*/ true);
		MapRuntimeKey(Context, MoveAction, EKeys::S, /*bSwizzleToY*/ true, /*bNegate*/ true);
		MapRuntimeKey(Context, MoveAction, EKeys::A, /*bSwizzleToY*/ false, /*bNegate*/ true);
		MapRuntimeKey(Context, MoveAction, EKeys::D);
	}
	if (!LookAction)
	{
		LookAction = MakeAction(EInputActionValueType::Axis2D);
		FEnhancedActionKeyMapping& Mapping = Context->MapKey(LookAction, EKeys::Mouse2D);
		// Mouse up is +Y; the controller's pitch input runs the other way.
		UInputModifierNegate* InvertPitch = NewObject<UInputModifierNegate>(Context);
		InvertPitch->bX = false;
		InvertPitch->bZ = false;
		Mapping.Modifiers.Add(InvertPitch);
	}
	if (!JumpAction)
	{
		JumpAction = MakeAction(EInputActionValueType::Boolean);
		MapRuntimeKey(Context, JumpAction, EKeys::SpaceBar);
	}
	if (!SprintAction)
	{
		SprintAction = MakeAction(EInputActionValueType::Boolean);
		MapRuntimeKey(Context, SprintAction, EKeys::LeftShift);
	}
	if (!ToggleHelpAction)
	{
		ToggleHelpAction = MakeAction(EInputActionValueType::Boolean);
		MapRuntimeKey(Context, ToggleHelpAction, EKeys::H);
	}
	if (!BendingInputConfig)
	{
		UBendingInputConfig* Config = NewObject<UBendingInputConfig>(this);

		struct FMoveKey
		{
			FGameplayTag Tag;
			FKey Key;
		};
		const FMoveKey MoveKeys[] = {
			{ BendingTags::Input_Bending_Light, EKeys::LeftMouseButton },
			{ BendingTags::Input_Bending_Heavy, EKeys::RightMouseButton },
			{ BendingTags::Input_Bending_Special, EKeys::Q },
			{ BendingTags::Input_Bending_Utility, EKeys::E },
			{ BendingTags::Input_Bending_Signature, EKeys::F },
		};
		for (const FMoveKey& MoveKey : MoveKeys)
		{
			UInputAction* Action = MakeAction(EInputActionValueType::Boolean);
			MapRuntimeKey(Context, Action, MoveKey.Key);
			FBendingInputAction& Binding = Config->MoveInputs.AddDefaulted_GetRef();
			Binding.InputAction = Action;
			Binding.InputTag = MoveKey.Tag;
		}

		struct FStanceKey
		{
			EBendingElement Element;
			FKey Key;
		};
		const FStanceKey StanceKeys[] = {
			{ EBendingElement::Water, EKeys::One },
			{ EBendingElement::Earth, EKeys::Two },
			{ EBendingElement::Fire, EKeys::Three },
			{ EBendingElement::Air, EKeys::Four },
		};
		for (const FStanceKey& StanceKey : StanceKeys)
		{
			UInputAction* Action = MakeAction(EInputActionValueType::Boolean);
			MapRuntimeKey(Context, Action, StanceKey.Key);
			FBendingStanceInputAction& Binding = Config->StanceInputs.AddDefaulted_GetRef();
			Binding.InputAction = Action;
			Binding.Element = StanceKey.Element;
		}
		BendingInputConfig = Config;
	}
	DefaultMappingContext = Context;
}

void AAvatarCharacter::NotifyControllerChanged()
{
	Super::NotifyControllerChanged();

	const APlayerController* PlayerController = Cast<APlayerController>(Controller);
	if (!PlayerController)
	{
		return;
	}
	EnsureRuntimeInput();
	if (!DefaultMappingContext)
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
	EnsureRuntimeInput();

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
	if (SprintAction)
	{
		Input->BindAction(SprintAction, ETriggerEvent::Started, this, &ThisClass::Input_SprintStarted);
		Input->BindAction(SprintAction, ETriggerEvent::Completed, this, &ThisClass::Input_SprintCompleted);
	}
	if (ToggleHelpAction)
	{
		Input->BindAction(ToggleHelpAction, ETriggerEvent::Started, this, &ThisClass::Input_ToggleHelp);
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

void AAvatarCharacter::Input_SprintStarted()
{
	bSprinting = true;
}

void AAvatarCharacter::Input_SprintCompleted()
{
	bSprinting = false;
}

void AAvatarCharacter::Input_ToggleHelp()
{
	if (const APlayerController* PlayerController = Cast<APlayerController>(Controller))
	{
		if (AAvatarHUD* HUD = PlayerController->GetHUD<AAvatarHUD>())
		{
			HUD->ToggleControlsPanel();
		}
	}
}

// ---------------------------------------------------------------------------------------------------- Frame

void AAvatarCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UpdateGroundTraction();
	UpdateMovementMode();
	UpdateElementTint();
	UpdateBodyAnimation(DeltaSeconds);
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

void AAvatarCharacter::UpdateMovementMode()
{
	UCharacterMovementComponent* Movement = GetCharacterMovement();
	const EBendingPhase Phase = Bending->GetCurrentPhase();
	const bool bCasting = Phase == EBendingPhase::Startup || Phase == EBendingPhase::Active || Techniques->IsSustaining();
	const bool bAiming = Bending->IsMoveActive() || Techniques->IsSustaining() || Techniques->HasWaterWhip() || Techniques->IsHoldingRock();

	// While bending the body turns to the aim and strafes; otherwise it turns to where it runs.
	Movement->bOrientRotationToMovement = !bAiming;
	Movement->bUseControllerDesiredRotation = bAiming;
	Movement->MaxWalkSpeed = (bSprinting ? SprintSpeed : WalkSpeed) * (bCasting ? CastingSpeedScale : 1.f);

	// On an air scooter the bender rides fast and faces where it goes.
	if (Techniques->IsRidingAirScooter())
	{
		Movement->MaxWalkSpeed = Techniques->GetAirScooterSpeedCmS();
		Movement->bOrientRotationToMovement = true;
		Movement->bUseControllerDesiredRotation = false;
	}
}

void AAvatarCharacter::UpdateElementTint()
{
	const EBendingElement Element = Bending->GetActiveElement();
	if (bBodyTinted && Element == TintedElement)
	{
		return;
	}
	bBodyTinted = true;
	TintedElement = Element;
	const FLinearColor Shirt = UBendingSandboxLibrary::GetElementColor(Element);
	FLinearColor SashColor = Shirt * 0.5f;
	SashColor.A = 1.f;
	UBendingSandboxLibrary::SetMeshColor(Torso, Shirt);
	UBendingSandboxLibrary::SetMeshColor(Sash, SashColor);
}

float AAvatarCharacter::GetAimPitchDegrees() const
{
	const FVector ToAim = Techniques->GetAimPoint() - ShoulderR->GetComponentLocation();
	const float PitchRadians = FMath::Atan2(static_cast<float>(ToAim.Z), static_cast<float>(ToAim.Size2D()));
	return FMath::Clamp(FMath::RadiansToDegrees(PitchRadians), -70.f, 75.f);
}

float AAvatarCharacter::ComputeCastPose(FAvatarBodyPose& Pose) const
{
	const UBendingTechniqueMove* Move = Cast<UBendingTechniqueMove>(Bending->GetCurrentMove());
	EBendingPhase Phase = Bending->GetCurrentPhase();
	EBendingTechnique Technique = Move ? Move->Technique : EBendingTechnique::None;
	float Progress = 1.f;
	if (Move && Phase != EBendingPhase::None)
	{
		const int32 PhaseFrames = Move->FrameData.GetPhaseFrames(Phase);
		Progress = PhaseFrames > 0 ? FMath::Clamp(static_cast<float>(Bending->GetCurrentPhaseFrame()) / static_cast<float>(PhaseFrames), 0.f, 1.f) : 1.f;
	}
	else if (Techniques->IsSustaining())
	{
		Technique = Techniques->GetSustainedTechnique();
		Phase = EBendingPhase::Active;
	}
	else if (Techniques->HasWaterWhip())
	{
		// Holding the water up in front.
		Pose.ArmR = 55.f;
		Pose.ArmRollR = -10.f;
		Pose.ArmL = FMath::Max(Pose.ArmL, 15.f);
		return 1.f;
	}
	else
	{
		return 0.f;
	}

	const float Ease = Progress * Progress * (3.f - 2.f * Progress);
	const float Reach = 90.f + GetAimPitchDegrees();

	if (ComputeSignaturePose(Technique, Phase, Ease, Pose))
	{
		return Phase == EBendingPhase::Recovery ? 1.f - Ease : 1.f;
	}

	if (Phase == EBendingPhase::Startup)
	{
		if (IsTwoHanded(Technique))
		{
			// Gathering the earth: both hands rise from low, palms up.
			const float Arms = FMath::Lerp(10.f, Technique == EBendingTechnique::RockThrow ? 115.f : 40.f, Ease);
			Pose.ArmL = Arms;
			Pose.ArmR = Arms;
			Pose.ArmRollL = 10.f;
			Pose.ArmRollR = -10.f;
			Pose.SpinePitch = -10.f * Ease;
		}
		else if (Technique == EBendingTechnique::AirJump)
		{
			Pose.ArmL = Pose.ArmR = 60.f * Ease;
			Pose.SpinePitch = -12.f * Ease;
		}
		else
		{
			// Wind-up: the casting arm draws back and the body twists away from the aim.
			Pose.ArmR = FMath::Lerp(20.f, -55.f, Ease);
			Pose.ArmRollR = -20.f;
			Pose.ArmL = 40.f;
			Pose.ArmRollL = 10.f;
			Pose.SpineYaw = -25.f * Ease;
		}
		return 1.f;
	}

	if (Phase == EBendingPhase::Active || Phase == EBendingPhase::Recovery)
	{
		if (IsTwoHanded(Technique))
		{
			// Thrust toward the aim (rock), heave up (wall, pillar) or press down (pit).
			float Arms = Reach;
			if (Technique == EBendingTechnique::EarthWall || Technique == EBendingTechnique::RaiseGround)
			{
				Arms = 150.f;
			}
			else if (Technique == EBendingTechnique::LowerGround)
			{
				Arms = 40.f;
			}
			Pose.ArmL = Arms;
			Pose.ArmR = Arms;
			Pose.ArmRollL = 8.f;
			Pose.ArmRollR = -8.f;
			Pose.SpinePitch = -6.f;
		}
		else if (Technique == EBendingTechnique::AirJump)
		{
			Pose.ArmL = Pose.ArmR = -25.f;
			Pose.ArmRollL = 45.f;
			Pose.ArmRollR = -45.f;
		}
		else
		{
			// Strike: the arm extends toward the aim, the body turns into it.
			Pose.ArmR = Reach;
			Pose.ArmRollR = 0.f;
			Pose.ArmL = 15.f;
			Pose.ArmRollL = 10.f;
			Pose.SpineYaw = 18.f;
		}
		// Recovery eases back to the locomotion pose over its frames.
		return Phase == EBendingPhase::Recovery ? 1.f - Ease : 1.f;
	}
	return 0.f;
}

void AAvatarCharacter::UpdateBodyAnimation(float DeltaSeconds)
{
	const UCharacterMovementComponent* Movement = GetCharacterMovement();
	const float Speed = static_cast<float>(GetVelocity().Size2D());
	const bool bAirborne = Movement->IsFalling();
	const float Gait = FMath::Clamp(Speed / 500.f, 0.f, 1.6f);

	// Walk and run: legs swing opposite each other, each arm with the opposite leg; longer strides when running.
	if (!bAirborne)
	{
		const float StrideCm = 110.f + 0.06f * Speed;
		WalkPhase = FMath::Fmod(WalkPhase + DeltaSeconds * Speed / StrideCm * UE_PI, 2.f * UE_PI);
	}
	const float Swing = FMath::Sin(WalkPhase);
	const float LegAmplitude = 38.f * FMath::Min(Gait, 1.2f);

	FAvatarBodyPose Locomotion;
	Locomotion.LegL = Swing * LegAmplitude;
	Locomotion.LegR = -Swing * LegAmplitude;
	Locomotion.ArmL = -0.8f * Swing * LegAmplitude;
	Locomotion.ArmR = 0.8f * Swing * LegAmplitude;
	Locomotion.ArmRollL = 6.f;
	Locomotion.ArmRollR = -6.f;
	Locomotion.SpinePitch = -7.f * FMath::Min(Gait, 1.4f);
	Locomotion.SpineYaw = 6.f * Swing * FMath::Min(Gait, 1.f);
	Locomotion.BobCm = -3.f * FMath::Abs(Swing) * FMath::Min(Gait, 1.f);
	if (bAirborne)
	{
		Locomotion.LegL = 32.f;
		Locomotion.LegR = -18.f;
		Locomotion.ArmL = 30.f;
		Locomotion.ArmR = 30.f;
		Locomotion.ArmRollL = 50.f;
		Locomotion.ArmRollR = -50.f;
		Locomotion.SpinePitch = -4.f;
		Locomotion.SpineYaw = 0.f;
		Locomotion.BobCm = 0.f;
	}

	FAvatarBodyPose Casting = Locomotion;
	const float CastTarget = ComputeCastPose(Casting);
	CastWeight = FMath::FInterpTo(CastWeight, CastTarget, DeltaSeconds, 20.f);
	CurrentPose = InterpPose(CurrentPose, LerpPose(Locomotion, Casting, CastWeight), DeltaSeconds, 18.f);

	// Perched on the air scooter's ball.
	ScooterLiftCm = FMath::FInterpTo(ScooterLiftCm, Techniques->IsRidingAirScooter() ? 55.f : 0.f, DeltaSeconds, 14.f);
	BodyRoot->SetRelativeLocation(FVector(0.0, 0.0, CurrentPose.BobCm + ScooterLiftCm));
	Spine->SetRelativeRotation(FRotator(CurrentPose.SpinePitch, CurrentPose.SpineYaw, 0.f));
	HipL->SetRelativeRotation(FRotator(CurrentPose.LegL, 0.f, 0.f));
	HipR->SetRelativeRotation(FRotator(CurrentPose.LegR, 0.f, 0.f));
	ShoulderL->SetRelativeRotation(FRotator(CurrentPose.ArmL, 0.f, CurrentPose.ArmRollL));
	ShoulderR->SetRelativeRotation(FRotator(CurrentPose.ArmR, 0.f, CurrentPose.ArmRollR));
}
