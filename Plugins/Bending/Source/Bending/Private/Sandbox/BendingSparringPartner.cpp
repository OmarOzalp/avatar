#include "Sandbox/BendingSparringPartner.h"

#include "BendingSettings.h"
#include "CollisionQueryParams.h"
#include "CollisionShape.h"
#include "Components/CapsuleComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/HitResult.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "Interaction/BendingInteractionSubsystem.h"
#include "Interaction/ElementalVolumeComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Physics/BendingUnits.h"
#include "Physics/ElementalSubstance.h"
#include "Sandbox/BendableTerrain.h"
#include "Sandbox/BendingProjectile.h"
#include "Sandbox/BendingPropActor.h"
#include "Sandbox/BendingSandboxArena.h"
#include "Sandbox/BendingSandboxLibrary.h"
#include "Sandbox/BendingTechniqueComponent.h"
#include "Sim/BendingTechniques.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	/** Feet to the centre of its capsule. */
	constexpr double PartnerFeetOffsetCm = ABendingSparringPartner::BodyHalfHeightCm + ABendingSparringPartner::BodyRadiusCm;
	/** Where its flames leave from (a shoulder), and the chest the player's parries aim at. */
	constexpr double PartnerHandHeightCm = 128.0;
	constexpr double PartnerChestHeightCm = 135.0;
	/** Lingering fire (burning ground, a ground flame, a burning prop) hurts it: J of heating per point of damage. */
	constexpr double PartnerHeatJPerDamage = 25000.0;
	/** Slower than this a prop only stands in its way; faster, running into it is a blow. */
	constexpr double PartnerMinBlowCmS = 150.0;
	/** A prop in contact for this long is still the same blow. */
	constexpr double PartnerBlowContactS = 0.3;
	/** It will not walk up a rise steeper than this within PartnerLookAheadCm. */
	constexpr double PartnerMaxRiseCm = 40.0;
	constexpr double PartnerLookAheadCm = 60.0;
	/** Feet this close above the ground still stand on it (and snap down to it walking downhill). */
	constexpr double PartnerGroundToleranceCm = 8.0;
	/** Knocked flying faster than this, it has no footing to steer with. */
	constexpr double PartnerMaxSteerSpeedCmS = 1200.0;
	/** Knocked down, it lies this far over (rad). */
	constexpr double PartnerFallTiltRad = 1.45;

	const FLinearColor PartnerDuelColor(1.f, 0.62f, 0.38f);

	FVector PartnerMoveToward(const FVector& Current, const FVector& Target, double MaxStep)
	{
		const FVector Delta = Target - Current;
		const double DeltaSize = Delta.Size();
		return DeltaSize <= MaxStep || DeltaSize < UE_KINDA_SMALL_NUMBER ? Target : Current + Delta * (MaxStep / DeltaSize);
	}

	FVector PartnerClosestOnSegment(const FVector& Point, const FVector& A, const FVector& B)
	{
		const FVector AB = B - A;
		const double LengthSquared = AB.SizeSquared();
		const double T = LengthSquared > UE_SMALL_NUMBER ? FMath::Clamp(FVector::DotProduct(Point - A, AB) / LengthSquared, 0.0, 1.0) : 0.0;
		return A + AB * T;
	}

	FSparringPartnerPose LerpPartnerPose(const FSparringPartnerPose& A, const FSparringPartnerPose& B, float Alpha)
	{
		FSparringPartnerPose Out;
		Out.LegL = FMath::Lerp(A.LegL, B.LegL, Alpha);
		Out.LegR = FMath::Lerp(A.LegR, B.LegR, Alpha);
		Out.ArmL = FMath::Lerp(A.ArmL, B.ArmL, Alpha);
		Out.ArmR = FMath::Lerp(A.ArmR, B.ArmR, Alpha);
		Out.ArmRollL = FMath::Lerp(A.ArmRollL, B.ArmRollL, Alpha);
		Out.ArmRollR = FMath::Lerp(A.ArmRollR, B.ArmRollR, Alpha);
		Out.SpinePitch = FMath::Lerp(A.SpinePitch, B.SpinePitch, Alpha);
		Out.SpineYaw = FMath::Lerp(A.SpineYaw, B.SpineYaw, Alpha);
		return Out;
	}

	/** Fists up, the stance it fights from. */
	void PartnerFightingStance(FSparringPartnerPose& Pose)
	{
		Pose.ArmL = 75.f;
		Pose.ArmR = 60.f;
		Pose.ArmRollL = -14.f;
		Pose.ArmRollR = 14.f;
		Pose.SpinePitch = -6.f;
		Pose.SpineYaw = 0.f;
	}

	/** Waiting at its post: hands clasped behind its back. */
	void PartnerHandsBehindBack(FSparringPartnerPose& Pose)
	{
		Pose.ArmL = -24.f;
		Pose.ArmR = -24.f;
		Pose.ArmRollL = -12.f;
		Pose.ArmRollR = 12.f;
		Pose.SpinePitch = 0.f;
		Pose.SpineYaw = 0.f;
	}

	/** The bow: fist in palm before the chest, bending forward by Amount (0..1). */
	void PartnerBow(FSparringPartnerPose& Pose, float Amount)
	{
		Pose.ArmL = 55.f;
		Pose.ArmR = 55.f;
		Pose.ArmRollL = -32.f;
		Pose.ArmRollR = 32.f;
		Pose.SpinePitch = -40.f * Amount;
		Pose.SpineYaw = 0.f;
	}

	/** A straight punch with one arm, the body turned into it. */
	void PartnerPunch(FSparringPartnerPose& Pose, bool bRight)
	{
		Pose.ArmL = bRight ? 35.f : 90.f;
		Pose.ArmR = bRight ? 90.f : 35.f;
		Pose.ArmRollL = bRight ? -10.f : 0.f;
		Pose.ArmRollR = bRight ? 0.f : 10.f;
		Pose.SpinePitch = -4.f;
		Pose.SpineYaw = bRight ? -18.f : 18.f;
	}
}

ABendingSparringPartner::FOnSparringCallout ABendingSparringPartner::OnSparringCallout;

ABendingSparringPartner::ABendingSparringPartner()
{
	PrimaryActorTick.bCanEverTick = true;

	Capsule = CreateDefaultSubobject<UCapsuleComponent>(TEXT("Capsule"));
	Capsule->InitCapsuleSize(static_cast<float>(BodyRadiusCm), static_cast<float>(PartnerFeetOffsetCm));
	Capsule->SetCollisionProfileName(UCollisionProfile::Pawn_ProfileName);
	// The aim (a visibility trace) lands on it; the camera does not pull in when it stands in the way.
	Capsule->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	Capsule->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	Capsule->SetCanEverAffectNavigation(false);
	Capsule->CanCharacterStepUpOn = ECB_No;
	RootComponent = Capsule;

	// Registered by InitPartner. It moves itself, so reaction impulses come back through HandleImpulse.
	Volume = CreateDefaultSubobject<UElementalVolumeComponent>(TEXT("Volume"));
	Volume->SetupAttachment(Capsule);
	Volume->bAutoActivate = false;
	Volume->VelocitySource = EElementalVelocitySource::Manual;
	Volume->bApplyImpulsesToAttachedBody = false;

	// ---------------------------------------------------------------- Body (heights above the feet)

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereFinder(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderFinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	UStaticMesh* Cube = CubeFinder.Object;
	UStaticMesh* Sphere = SphereFinder.Object;
	UStaticMesh* Cylinder = CylinderFinder.Object;

	auto MakePivot = [this](const TCHAR* PivotName, USceneComponent* Parent, const FVector& PivotLocation)
	{
		USceneComponent* Pivot = CreateDefaultSubobject<USceneComponent>(PivotName);
		Pivot->SetupAttachment(Parent);
		Pivot->SetRelativeLocation(PivotLocation);
		return Pivot;
	};
	// Basic shapes are 100 cm across with the pivot at their centre.
	auto MakePart = [this](const TCHAR* PartName, USceneComponent* Parent, UStaticMesh* ShapeMesh, const FVector& PartLocation, const FVector& SizeCm)
	{
		UStaticMeshComponent* Part = CreateDefaultSubobject<UStaticMeshComponent>(PartName);
		Part->SetupAttachment(Parent);
		Part->SetStaticMesh(ShapeMesh);
		Part->SetRelativeLocation(PartLocation);
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

	// Fire Nation red with gold trim, dark trousers, a red headband.
	const FLinearColor Tunic = UBendingSandboxLibrary::FromSRGB(142, 31, 26);
	const FLinearColor Gold = UBendingSandboxLibrary::FromSRGB(214, 170, 62);
	const FLinearColor Trousers = UBendingSandboxLibrary::FromSRGB(46, 30, 28);
	const FLinearColor Boots = UBendingSandboxLibrary::FromSRGB(28, 22, 20);
	const FLinearColor Skin = UBendingSandboxLibrary::FromSRGB(222, 170, 124);
	const FLinearColor Hair = UBendingSandboxLibrary::FromSRGB(28, 20, 16);
	const FLinearColor Band = UBendingSandboxLibrary::FromSRGB(186, 28, 22);
	const FLinearColor Eyes = UBendingSandboxLibrary::FromSRGB(20, 20, 20);

	BodyRoot = MakePivot(TEXT("BodyRoot"), Capsule, FVector(0.0, 0.0, -PartnerFeetOffsetCm));

	// Legs hang from the hips down to the ground.
	HipL = MakePivot(TEXT("HipL"), BodyRoot, FVector(0.0, -11.0, 92.0));
	HipR = MakePivot(TEXT("HipR"), BodyRoot, FVector(0.0, 11.0, 92.0));
	AddPart(MakePart(TEXT("LegL"), HipL, Cylinder, FVector(0.0, 0.0, -42.0), FVector(17.0, 17.0, 84.0)), Trousers);
	AddPart(MakePart(TEXT("LegR"), HipR, Cylinder, FVector(0.0, 0.0, -42.0), FVector(17.0, 17.0, 84.0)), Trousers);
	AddPart(MakePart(TEXT("FootL"), HipL, Cube, FVector(6.0, 0.0, -86.0), FVector(25.0, 13.0, 8.0)), Boots);
	AddPart(MakePart(TEXT("FootR"), HipR, Cube, FVector(6.0, 0.0, -86.0), FVector(25.0, 13.0, 8.0)), Boots);
	AddPart(MakePart(TEXT("Pelvis"), BodyRoot, Cube, FVector(0.0, 0.0, 92.0), FVector(22.0, 34.0, 16.0)), Trousers);

	// Upper body: it turns, leans and bows around the waist.
	Spine = MakePivot(TEXT("Spine"), BodyRoot, FVector(0.0, 0.0, 98.0));
	AddPart(MakePart(TEXT("Torso"), Spine, Cube, FVector(0.0, 0.0, 23.0), FVector(24.0, 40.0, 46.0)), Tunic);
	AddPart(MakePart(TEXT("Skirt"), Spine, Cube, FVector(0.0, 0.0, -9.0), FVector(25.0, 38.0, 20.0)), Tunic);
	AddPart(MakePart(TEXT("Sash"), Spine, Cube, FVector(0.0, 0.0, 2.0), FVector(26.0, 42.0, 7.0)), Gold);
	AddPart(MakePart(TEXT("Collar"), Spine, Cube, FVector(0.0, 0.0, 45.0), FVector(26.0, 28.0, 5.0)), Gold);
	AddPart(MakePart(TEXT("PadL"), Spine, Cube, FVector(0.0, -25.0, 46.0), FVector(18.0, 16.0, 7.0)), Gold);
	AddPart(MakePart(TEXT("PadR"), Spine, Cube, FVector(0.0, 25.0, 46.0), FVector(18.0, 16.0, 7.0)), Gold);
	Head = MakePart(TEXT("Head"), Spine, Sphere, FVector(0.0, 0.0, 66.0), FVector(26.0));
	AddPart(Head, Skin);
	AddPart(MakePart(TEXT("Hair"), Spine, Sphere, FVector(-3.0, 0.0, 72.0), FVector(26.0, 26.0, 16.0)), Hair);
	AddPart(MakePart(TEXT("Topknot"), Spine, Sphere, FVector(-4.0, 0.0, 82.0), FVector(9.0)), Hair);
	AddPart(MakePart(TEXT("Headband"), Spine, Cylinder, FVector(0.0, 0.0, 70.0), FVector(28.0, 28.0, 5.0)), Band);
	// Eyes show which way it faces.
	AddPart(MakePart(TEXT("EyeL"), Spine, Cube, FVector(12.0, -5.0, 66.0), FVector(3.0)), Eyes);
	AddPart(MakePart(TEXT("EyeR"), Spine, Cube, FVector(12.0, 5.0, 66.0), FVector(3.0)), Eyes);

	// Arms hang from the shoulders; positive pitch swings them forward. +Y is its right.
	ShoulderL = MakePivot(TEXT("ShoulderL"), Spine, FVector(0.0, -25.0, 42.0));
	ShoulderR = MakePivot(TEXT("ShoulderR"), Spine, FVector(0.0, 25.0, 42.0));
	AddPart(MakePart(TEXT("ArmL"), ShoulderL, Cylinder, FVector(0.0, 0.0, -29.0), FVector(13.0, 13.0, 58.0)), Tunic);
	AddPart(MakePart(TEXT("ArmR"), ShoulderR, Cylinder, FVector(0.0, 0.0, -29.0), FVector(13.0, 13.0, 58.0)), Tunic);
	AddPart(MakePart(TEXT("CuffL"), ShoulderL, Cylinder, FVector(0.0, 0.0, -52.0), FVector(15.0, 15.0, 6.0)), Gold);
	AddPart(MakePart(TEXT("CuffR"), ShoulderR, Cylinder, FVector(0.0, 0.0, -52.0), FVector(15.0, 15.0, 6.0)), Gold);
	AddPart(MakePart(TEXT("FistL"), ShoulderL, Sphere, FVector(0.0, 0.0, -61.0), FVector(14.0)), Skin);
	AddPart(MakePart(TEXT("FistR"), ShoulderR, Sphere, FVector(0.0, 0.0, -61.0), FVector(14.0)), Skin);
	HandL = MakePivot(TEXT("HandL"), ShoulderL, FVector(0.0, 0.0, -63.0));
	HandR = MakePivot(TEXT("HandR"), ShoulderR, FVector(0.0, 0.0, -63.0));

	// The telegraph: placed on the striking fist each frame while it winds up and strikes.
	FistFlame = MakePart(TEXT("FistFlame"), Capsule, Sphere, FVector::ZeroVector, FVector(12.0));
	FistFlame->SetCastShadow(false);
	FistFlame->SetVisibility(false);

	FistLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("FistLight"));
	FistLight->SetupAttachment(Capsule);
	FistLight->SetMobility(EComponentMobility::Movable);
	FistLight->IntensityUnits = ELightUnits::Candelas;
	FistLight->SetCastShadows(false);
	FistLight->SetLightColor(FLinearColor(1.f, 0.55f, 0.2f));
	FistLight->SetAttenuationRadius(500.f);
	FistLight->SetVisibility(false);
}

ABendingSparringPartner* ABendingSparringPartner::Find(const UWorld* World)
{
	if (!World)
	{
		return nullptr;
	}
	for (TActorIterator<ABendingSparringPartner> It(World); It; ++It)
	{
		if (IsValid(*It))
		{
			return *It;
		}
	}
	return nullptr;
}

// ---------------------------------------------------------------------------------------------------- Setup

void ABendingSparringPartner::BeginPlay()
{
	Super::BeginPlay();

	for (int32 Index = 0; Index < BodyParts.Num() && Index < BodyPartColors.Num(); ++Index)
	{
		UBendingSandboxLibrary::SetMeshColor(BodyParts[Index], BodyPartColors[Index]);
	}
	UBendingSandboxLibrary::SetMeshColor(FistFlame, UBendingSandboxLibrary::FromSRGB(255, 160, 50));

	if (!bInitialized)
	{
		// Placed in a level by hand: it waits where it stands.
		InitPartner(GetFeetLocation(), GetActorRotation().Yaw);
	}
}

void ABendingSparringPartner::InitPartner(const FVector& InPostCm, double YawDeg)
{
	bInitialized = true;
	SetActorLocationAndRotation(InPostCm + FVector(0.0, 0.0, PartnerFeetOffsetCm), FRotator(0.0, YawDeg, 0.0), false, nullptr, ETeleportType::TeleportPhysics);
	Brain.Reset(BendingUnits::ToSim(InPostCm), FMath::DegreesToRadians(YawDeg));
	VelocityCmS = FVector::ZeroVector;

	// A person-sized capsule of solid matter: blasts shove it, it does not soak up water.
	BendingSim::FVolume SimBody = BendingSim::FVolume::MakeDefault(BendingSim::ESubstance::Earth, BodyMassKg);
	SimBody.Porosity = 0.0;
	SimBody.bDeriveRadiusFromMass = false;
	SimBody.RadiusCm = BodyRadiusCm;
	SimBody.Shape = BendingSim::EShape::Capsule;
	SimBody.CapsuleHalfAxisCm = BendingSim::FVec3(0.0, 0.0, BodyHalfHeightCm);
	SimBody.DragCoefficient = 1.0;
	SimBody.LocationCm = BendingUnits::ToSim(GetActorLocation());
	Volume->InitialState = FElementalVolumeState::FromSim(SimBody);
	Volume->SetManualVelocity(FVector::ZeroVector);
	Volume->OnImpulseReceived.AddUniqueDynamic(this, &ABendingSparringPartner::HandleImpulse);
	// Registers now during play; when built before BeginPlay (by the game mode), registers at BeginPlay.
	Volume->Activate(true);
}

// ---------------------------------------------------------------------------------------------------- Frame

void ABendingSparringPartner::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const float Dt = FMath::Min(DeltaSeconds, 0.1f);
	if (!bInitialized || Dt <= 0.f)
	{
		return;
	}

	const FVector Feet = GetFeetLocation();
	const ABendingSandboxArena* Arena = GetArena();
	const double Ground = Arena ? Arena->GetGroundHeightAt(Feet) : Feet.Z;

	TickBurning(Dt);

	// What it sees goes to its brain; its orders move its body and bend its flames.
	BendingSim::FSparringOrders Orders;
	FVector Desired = FVector::ZeroVector;
	bool bControl = Feet.Z <= Ground + PartnerGroundToleranceCm;
	if (const APawn* Player = GetPlayerPawn())
	{
		const UBendingTechniqueComponent* PlayerTechniques = Player->FindComponentByClass<UBendingTechniqueComponent>();
		BendingSim::FSparringSenses Senses;
		Senses.FeetCm = BendingUnits::ToSim(Feet);
		Senses.VelocityCmS = BendingUnits::ToSim(VelocityCmS);
		Senses.bGrounded = Feet.Z <= Ground + PartnerGroundToleranceCm;
		Senses.PlayerFeetCm = BendingUnits::ToSim(Player->GetActorLocation() - FVector(0.0, 0.0, Player->GetSimpleCollisionHalfHeight()));
		Senses.PlayerVelocityCmS = BendingUnits::ToSim(Player->GetVelocity());
		Senses.bPlayerDown = PlayerTechniques && PlayerTechniques->IsDown();
		// Only read when it wants to attack.
		Senses.bClearShot = Brain.State != BendingSim::ESparringState::Moving || HasClearShot(Feet, Player);
		Brain.Update(Senses, Dt, Orders);
		OfferThreats(Orders.Events);
		Desired = BendingUnits::ToEngine(Orders.DesiredVelocityCmS);
		bControl = Orders.bControl;
	}
	// A sidestep it has just decided on starts this frame.
	if (Brain.State == BendingSim::ESparringState::Dodging)
	{
		Desired = BendingUnits::ToEngine(Brain.DodgeDirection) * Brain.Tuning.DodgeCmS;
	}
	// It will not walk up a wall, into a pond or off the field: it turns the other way.
	if (!Desired.IsNearlyZero() && IsWayBlocked(Feet, Desired.GetSafeNormal(), Ground))
	{
		Desired = FVector::ZeroVector;
		Brain.OnBlocked();
	}

	TickMovement(Desired, bControl, Dt);
	TickBlows(Dt);
	SetActorRotation(FRotator(0.0, FMath::RadiansToDegrees(Brain.YawRad), 0.0));
	Volume->SetManualVelocity(VelocityCmS);
	TickHitReports(Dt);
	UpdatePose(Dt);

	if (Orders.bShoot)
	{
		Shoot(Orders.Shot);
	}
	HandleEvents(Orders.Events);
}

void ABendingSparringPartner::TickMovement(const FVector& DesiredCmS, bool bControl, float DeltaSeconds)
{
	UWorld* World = GetWorld();
	const ABendingSandboxArena* Arena = GetArena();
	const double GravityCmS2 = -static_cast<double>(World->GetGravityZ());

	// Knocked flying, it has no footing to steer with.
	if (bControl && VelocityCmS.Size2D() < PartnerMaxSteerSpeedCmS)
	{
		const FVector Steered = PartnerMoveToward(FVector(VelocityCmS.X, VelocityCmS.Y, 0.0), DesiredCmS, Brain.Tuning.AccelerationCmS2 * DeltaSeconds);
		VelocityCmS.X = Steered.X;
		VelocityCmS.Y = Steered.Y;
	}
	VelocityCmS.Z -= GravityCmS2 * DeltaSeconds;

	FVector Center = GetActorLocation();
	FVector Step = VelocityCmS * DeltaSeconds;

	// Solid props (braziers, lanterns, banner poles, straw) stop it, knee to head; the ground is the terrain's height.
	const FVector FlatStep(Step.X, Step.Y, 0.0);
	if (!FlatStep.IsNearlyZero())
	{
		FCollisionQueryParams Params(SCENE_QUERY_STAT(BendingSparringPartnerMove), false, this);
		if (Arena && Arena->GetTerrainActor())
		{
			Params.AddIgnoredActor(Arena->GetTerrainActor());
		}
		const FVector From = Center - FVector(0.0, 0.0, 10.0);
		FHitResult Hit;
		if (World->SweepSingleByObjectType(Hit, From, From + FlatStep, FQuat::Identity, FCollisionObjectQueryParams(ECC_WorldStatic),
			FCollisionShape::MakeCapsule(static_cast<float>(BodyRadiusCm - 4.0), 50.f), Params) && !Hit.bStartPenetrating)
		{
			Step.X = FlatStep.X * Hit.Time;
			Step.Y = FlatStep.Y * Hit.Time;
			const FVector WallNormal = FVector(Hit.ImpactNormal.X, Hit.ImpactNormal.Y, 0.0).GetSafeNormal();
			const double Into = FVector::DotProduct(VelocityCmS, WallNormal);
			if (Into < 0.0)
			{
				VelocityCmS -= WallNormal * Into;
			}
			if (bControl && !DesiredCmS.IsNearlyZero())
			{
				Brain.OnBlocked();
			}
		}
	}
	Center += Step;

	// Flung off the field: it lies at the edge, out, until it gets up.
	const BendingSim::FTerrain* Terrain = Arena ? Arena->GetTerrain() : nullptr;
	if (Terrain && !Terrain->IsInside(Center.X, Center.Y))
	{
		const BendingSim::FVec3& Origin = Arena->GetLayout().OriginCm;
		Center.X = FMath::Clamp(Center.X, Origin.X + 50.0, -Origin.X - 50.0);
		Center.Y = FMath::Clamp(Center.Y, Origin.Y + 50.0, -Origin.Y - 50.0);
		VelocityCmS = FVector::ZeroVector;
		if (!Brain.IsDown())
		{
			Hurt(1.0, -GetActorForwardVector(), true);
		}
	}

	// Standing on the ground (snapped down to it walking downhill), with friction, less on mud.
	const double Ground = Arena ? Arena->GetGroundHeightAt(Center) : Center.Z - PartnerFeetOffsetCm;
	const double FeetZ = Center.Z - PartnerFeetOffsetCm;
	bOnGround = FeetZ <= Ground || (bOnGround && VelocityCmS.Z <= 0.0 && FeetZ <= Ground + PartnerGroundToleranceCm);
	if (bOnGround)
	{
		Center.Z = Ground + PartnerFeetOffsetCm;
		VelocityCmS.Z = FMath::Max(VelocityCmS.Z, 0.0);
		const UBendingInteractionSubsystem* Interaction = World->GetSubsystem<UBendingInteractionSubsystem>();
		const double Traction = Interaction ? static_cast<double>(Interaction->GetSurfaceTractionMultiplierAt(FVector(Center.X, Center.Y, Ground))) : 1.0;
		const FVector Slowed = PartnerMoveToward(FVector(VelocityCmS.X, VelocityCmS.Y, 0.0), FVector::ZeroVector, 0.5 * Traction * GravityCmS2 * DeltaSeconds);
		VelocityCmS.X = Slowed.X;
		VelocityCmS.Y = Slowed.Y;
	}
	SetActorLocation(Center);
}

bool ABendingSparringPartner::IsWayBlocked(const FVector& FeetCm, const FVector& Direction, double GroundZ) const
{
	const ABendingSandboxArena* Arena = GetArena();
	if (!Arena)
	{
		return false;
	}
	const FVector Ahead = FeetCm + Direction * PartnerLookAheadCm;
	const BendingSim::FTerrain* Terrain = Arena->GetTerrain();
	if (Terrain && !Terrain->IsInside(Ahead.X, Ahead.Y))
	{
		return true;
	}
	return Arena->GetGroundHeightAt(Ahead) - GroundZ > PartnerMaxRiseCm || Arena->FindPondAt(Ahead) != INDEX_NONE;
}

// ---------------------------------------------------------------------------------------------------- Damage

void ABendingSparringPartner::HandleImpulse(FVector ImpulseKgCmS)
{
	Shove(ImpulseKgCmS);
}

void ABendingSparringPartner::Shove(const FVector& ImpulseKgCmS)
{
	const FVector DeltaV = ImpulseKgCmS / BodyMassKg;
	VelocityCmS += DeltaV;
	Hurt(DamagePerMs * BendingUnits::CmToM(DeltaV.Size()), DeltaV, false);
}

void ABendingSparringPartner::TakeFlameDamage(double Damage, const FVector& Direction)
{
	Hurt(Damage, Direction, false);
}

void ABendingSparringPartner::TakeHit(double Damage, const FVector& FromDirection)
{
	Hurt(Damage, FromDirection, false);
}

void ABendingSparringPartner::Hurt(double Damage, const FVector& FromDirection, bool bKnockOut)
{
	if (Damage <= 0.0 || !bInitialized)
	{
		return;
	}
	// The brain decides what reaches it: a challenge while it waits, a quarter behind its guard, at most one blow's worth.
	BendingSim::FSparringEvents Events;
	const double Taken = Brain.TakeDamage(Damage, bKnockOut, Events);
	if (Taken > 0.0)
	{
		PendingDamage += Taken;
		QuietS = 0.0;
	}
	if (Events.bKnockedOut)
	{
		// It falls the way it was hit (backward, when nothing says which way).
		const FVector Flat(FromDirection.X, FromDirection.Y, 0.0);
		FallDirection = Flat.IsNearlyZero() ? -GetActorForwardVector() : Flat.GetSafeNormal();
		OnSparringCallout.Broadcast(EBendingSparringCallout::PartnerHit, GetActorLocation() + FVector(0.0, 0.0, 60.0), PendingDamage);
		OnSparringCallout.Broadcast(EBendingSparringCallout::PartnerKnockedOut, GetActorLocation() + FVector(0.0, 0.0, 90.0), 0.0);
		PendingDamage = 0.0;
		PendingAgeS = 0.0;
	}
	HandleEvents(Events);
}

bool ABendingSparringPartner::IsStruckBy(const FVector& LocationCm, double ReachCm) const
{
	const FVector Center = GetActorLocation();
	const FVector Axis(0.0, 0.0, BodyHalfHeightCm);
	return FVector::Dist(PartnerClosestOnSegment(LocationCm, Center - Axis, Center + Axis), LocationCm) <= BodyRadiusCm + ReachCm;
}

void ABendingSparringPartner::OnPlayerKnockedDown()
{
	BendingSim::FSparringEvents Events;
	Brain.OnPlayerDown(Events);
	HandleEvents(Events);
}

void ABendingSparringPartner::TickBurning(float DeltaSeconds)
{
	if (!Brain.IsDuelActive())
	{
		return;
	}
	const UBendingInteractionSubsystem* Interaction = GetWorld()->GetSubsystem<UBendingInteractionSubsystem>();
	if (!Interaction)
	{
		return;
	}
	TArray<FElementalVolumeHandle> Fires;
	Interaction->QueryVolumes(GetActorLocation(), BodyRadiusCm + 20.0, ElementalSubstance::ToMask(EElementalSubstance::Fire), Fires);
	const double AmbientK = UBendingSettings::Get().AmbientTemperatureK;
	double HeatingW = 0.0;
	for (const FElementalVolumeHandle& Handle : Fires)
	{
		if (Interaction->IsVolumeDepleted(Handle))
		{
			continue;
		}
		// Its own flames never burn it, and a flame in flight hurts only when it strikes (ABendingProjectile).
		const UElementalVolumeComponent* FlameComponent = Interaction->GetVolumeOwner(Handle);
		const ABendingProjectile* Projectile = FlameComponent ? Cast<ABendingProjectile>(FlameComponent->GetOwner()) : nullptr;
		if (Projectile && (Projectile->IsFromSparringPartner() || !Projectile->IsGrounded()))
		{
			continue;
		}
		if (const BendingSim::FVolume* Fire = Interaction->GetVolume(Handle))
		{
			HeatingW += 150.0 * FMath::Max(Fire->TemperatureK - AmbientK, 0.0) * FMath::Min(1.0, Fire->MassKg / 0.5);
		}
	}
	if (HeatingW > 0.0)
	{
		Hurt(HeatingW * DeltaSeconds / PartnerHeatJPerDamage, FVector::ZeroVector, false);
	}
}

void ABendingSparringPartner::TickBlows(float DeltaSeconds)
{
	const double Now = GetWorld()->GetTimeSeconds();
	for (TMap<TWeakObjectPtr<AActor>, double>::TIterator It = RecentBlows.CreateIterator(); It; ++It)
	{
		if (!It->Key.IsValid() || Now - It->Value > PartnerBlowContactS)
		{
			It.RemoveCurrent();
		}
	}

	const FVector Center = GetActorLocation();
	const FVector Axis(0.0, 0.0, BodyHalfHeightCm);
	for (TActorIterator<ABendingPropActor> It(GetWorld()); It; ++It)
	{
		ABendingPropActor* Prop = *It;
		if (!Prop || !Prop->IsLoose())
		{
			continue;
		}
		// Props at rest only stand in its way (the physics keeps them out of it); it is hurt by things flung at it.
		const FVector PropVelocity = Prop->GetVelocity();
		if (PropVelocity.SizeSquared() < FMath::Square(PartnerMinBlowCmS))
		{
			continue;
		}
		const FVector PropCenter = Prop->GetActorLocation();
		const FVector Offset = PropCenter - PartnerClosestOnSegment(PropCenter, Center - Axis, Center + Axis);
		const double Distance = Offset.Size();
		const FVector Normal = Distance > 1.0 ? Offset / Distance : -PropVelocity.GetSafeNormal();
		const double Closing = FVector::DotProduct(PropVelocity - VelocityCmS, Normal);
		// Touching, or about to within the physics step that follows (the collision then bounces the prop off it).
		if (Closing > -PartnerMinBlowCmS || Distance > BodyRadiusCm + Prop->GetRadiusCm() - 1.5 * Closing * DeltaSeconds)
		{
			continue;
		}
		const TWeakObjectPtr<AActor> Key(Prop);
		if (RecentBlows.Contains(Key))
		{
			continue;
		}
		RecentBlows.Add(Key, Now);
		// Body meets body as in the browser build: an inelastic knock (restitution 0.2) shared by the two masses.
		const double PropMassKg = FMath::Max(Prop->GetMassKg(), 0.1);
		const double KnockKgCmS = -1.2 * Closing / (1.0 / BodyMassKg + 1.0 / PropMassKg);
		Shove(Normal * -KnockKgCmS);
	}
}

void ABendingSparringPartner::TickHitReports(float DeltaSeconds)
{
	if (PendingDamage <= 0.0)
	{
		return;
	}
	QuietS += DeltaSeconds;
	PendingAgeS += DeltaSeconds;
	if (QuietS >= 0.12 || PendingAgeS >= 0.5)
	{
		if (PendingDamage >= 1.0)
		{
			OnSparringCallout.Broadcast(EBendingSparringCallout::PartnerHit, GetActorLocation() + FVector(0.0, 0.0, 60.0), PendingDamage);
		}
		PendingDamage = 0.0;
		PendingAgeS = 0.0;
	}
}

// ---------------------------------------------------------------------------------------------------- Seeing and reacting

void ABendingSparringPartner::OfferThreats(BendingSim::FSparringEvents& Events)
{
	if (!Brain.CanReact())
	{
		return;
	}
	if (ThreatsSeen.Num() > 64)
	{
		for (TSet<TWeakObjectPtr<AActor>>::TIterator It = ThreatsSeen.CreateIterator(); It; ++It)
		{
			if (!It->IsValid())
			{
				It.RemoveCurrent();
			}
		}
	}
	// Something thrown at it: guard or sidestep (or take it), decided once per throw; one decision a frame.
	for (TActorIterator<ABendingProjectile> It(GetWorld()); It; ++It)
	{
		ABendingProjectile* Projectile = *It;
		if (Projectile && !Projectile->IsFromSparringPartner() && !Projectile->IsGrounded()
			&& OfferThreat(Projectile, Projectile->GetVelocityCmS(), false, Events))
		{
			return;
		}
	}
	// A thrown rock is heavy: better dodged than guarded.
	for (TActorIterator<ABendingPropActor> It(GetWorld()); It; ++It)
	{
		ABendingPropActor* Rock = *It;
		if (!Rock || !Rock->IsThrownRock() || Rock->IsHeld())
		{
			continue;
		}
		const FVector RockVelocity = Rock->GetVelocity();
		if (RockVelocity.SizeSquared() >= FMath::Square(600.0) && OfferThreat(Rock, RockVelocity, true, Events))
		{
			return;
		}
	}
}

bool ABendingSparringPartner::OfferThreat(AActor* Thrown, const FVector& ThrownVelocityCmS, bool bHeavy, BendingSim::FSparringEvents& Events)
{
	const TWeakObjectPtr<AActor> Key(Thrown);
	if (ThreatsSeen.Contains(Key))
	{
		return false;
	}
	BendingSim::FSparringThreat Threat;
	Threat.LocationCm = BendingUnits::ToSim(Thrown->GetActorLocation());
	Threat.VelocityCmS = BendingUnits::ToSim(ThrownVelocityCmS);
	Threat.bHeavy = bHeavy;
	const BendingSim::FVec3 Center = BendingUnits::ToSim(GetActorLocation());
	if (!Brain.IsThreatening(Threat, Center))
	{
		return false;
	}
	ThreatsSeen.Add(Key);
	Brain.React(Threat, Center, Events);
	return true;
}

bool ABendingSparringPartner::HasClearShot(const FVector& FeetCm, const APawn* Player) const
{
	const FVector PlayerFeet = Player->GetActorLocation() - FVector(0.0, 0.0, Player->GetSimpleCollisionHalfHeight());
	FVector Toward(PlayerFeet.X - FeetCm.X, PlayerFeet.Y - FeetCm.Y, 0.0);
	Toward = Toward.Size() > 1.0 ? Toward.GetSafeNormal() : GetActorForwardVector();
	const FVector HandCm = FeetCm + FVector(0.0, 0.0, PartnerHandHeightCm) + Toward * 40.0;
	const FVector Target = PlayerFeet + FVector(0.0, 0.0, 110.0);
	const FVector Line = Target - HandCm;
	const double LineCm = Line.Size();
	if (LineCm <= 1.0)
	{
		return true;
	}

	// Terrain in the way (an earth wall, a hill): the heightfield itself, which never lags an edit.
	const ABendingSandboxArena* Arena = GetArena();
	if (const BendingSim::FTerrain* Terrain = Arena ? Arena->GetTerrain() : nullptr)
	{
		BendingSim::FVec3 TerrainHit;
		if (Terrain->Raycast(BendingUnits::ToSim(HandCm), BendingUnits::ToSim(Line / LineCm), LineCm, TerrainHit))
		{
			return false;
		}
	}

	// Solid props in the way (lanterns, rocks, banner poles) block it too; a rock in flight does not.
	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(ECC_WorldStatic);
	Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
	Objects.AddObjectTypesToQuery(ECC_PhysicsBody);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(BendingSparringClearShot), false, this);
	Params.AddIgnoredActor(Player);
	if (Arena && Arena->GetTerrainActor())
	{
		Params.AddIgnoredActor(Arena->GetTerrainActor());
	}
	TArray<FHitResult> Hits;
	if (!GetWorld()->SweepMultiByObjectType(Hits, HandCm, Target, FQuat::Identity, Objects, FCollisionShape::MakeSphere(35.f), Params) && Hits.Num() == 0)
	{
		return true;
	}
	for (const FHitResult& Blocking : Hits)
	{
		const ABendingPropActor* Prop = Cast<ABendingPropActor>(Blocking.GetActor());
		if (!Prop || !Prop->IsThrownRock())
		{
			return false;
		}
	}
	return true;
}

// ---------------------------------------------------------------------------------------------------- Bending

void ABendingSparringPartner::Shoot(const BendingSim::FSparringShot& Shot)
{
	const BendingSim::FSparringTuning& Tune = Brain.Tuning;
	const FVector Feet = GetFeetLocation();
	const FVector Forward(FMath::Cos(Brain.YawRad), FMath::Sin(Brain.YawRad), 0.0);
	const FVector Right(-Forward.Y, Forward.X, 0.0);
	const double SpeedCmS = BendingUnits::MToCm(Shot.SpeedMs);

	if (Shot.Attack == BendingSim::ESparringAttack::Burst)
	{
		// A ring of flames bursting outward from its middle.
		const FVector Middle = Feet + FVector(0.0, 0.0, 85.0);
		const int32 Count = FMath::Max(Tune.BurstFlames, 1);
		for (int32 Index = 0; Index < Count; ++Index)
		{
			const double Angle = Brain.YawRad + Index * 2.0 * UE_DOUBLE_PI / Count;
			const FVector Out(FMath::Cos(Angle), FMath::Sin(Angle), 0.0);
			BendingSim::FVolume Flame = BendingSim::MakeFlame(Shot.MassKg, Tune.FlameTemperatureK, BendingUnits::ToSim(Middle), BendingUnits::ToSim(Out * SpeedCmS));
			Flame.LocationCm = BendingUnits::ToSim(Middle + Out * (BodyRadiusCm + Flame.RadiusCm + 10.0));
			// A light on every fourth flame is enough to light the ring.
			SpawnFlame(Flame, Index % 4 == 0);
		}
		return;
	}

	// From the shoulder of the striking hand, at the chest, leading a moving target a little.
	const FVector Shoulder = Feet + FVector(0.0, 0.0, PartnerHandHeightCm) + Right * (Shot.Side * 18.0);
	FVector Target = Shoulder + Forward * 1000.0;
	if (const APawn* Player = GetPlayerPawn())
	{
		Target = Player->GetActorLocation() - FVector(0.0, 0.0, Player->GetSimpleCollisionHalfHeight()) + FVector(0.0, 0.0, 115.0);
		const double FlightS = FVector::Dist(Shoulder, Target) / FMath::Max(SpeedCmS, 1.0);
		const FVector PlayerVelocity = Player->GetVelocity();
		Target += FVector(PlayerVelocity.X, PlayerVelocity.Y, 0.0) * (0.5 * FlightS);
	}
	FVector Direction = (Target - Shoulder).GetSafeNormal();
	if (Direction.IsNearlyZero())
	{
		Direction = Forward;
	}
	// A combo's blasts fan out a little.
	const double SpreadCos = FMath::Cos(Shot.SpreadRad);
	const double SpreadSin = FMath::Sin(Shot.SpreadRad);
	Direction = FVector(Direction.X * SpreadCos - Direction.Y * SpreadSin, Direction.X * SpreadSin + Direction.Y * SpreadCos, Direction.Z);
	BendingSim::FVolume Flame = BendingSim::MakeFlame(Shot.MassKg, Tune.FlameTemperatureK, BendingUnits::ToSim(Shoulder), BendingUnits::ToSim(Direction * SpeedCmS));
	// Born clear of its own body, so its own fire never touches it.
	Flame.LocationCm = BendingUnits::ToSim(Shoulder + Direction * (BodyRadiusCm + Flame.RadiusCm + 10.0));
	SpawnFlame(Flame, true);
}

void ABendingSparringPartner::SpawnFlame(const BendingSim::FVolume& Flame, bool bWithLight)
{
	// The same bent fire as the player's Fire Blast, marked as its own: it strikes the player, never its bender.
	ABendingProjectile* Projectile = ABendingProjectile::SpawnProjectile(GetWorld(), EBendingProjectileKind::Fire, Flame, this, bWithLight);
	if (Projectile)
	{
		Projectile->SetFromSparringPartner(true);
		// Never a threat to it, even when a parry turns it back.
		ThreatsSeen.Add(TWeakObjectPtr<AActor>(Projectile));
	}
}

// ---------------------------------------------------------------------------------------------------- Events

void ABendingSparringPartner::HandleEvents(const BendingSim::FSparringEvents& Events)
{
	const FVector Above = GetFeetLocation() + FVector(0.0, 0.0, 210.0);
	if (Events.bDuelStarted)
	{
		PendingDamage = 0.0;
		PendingAgeS = 0.0;
		// Both start the duel whole.
		if (UBendingTechniqueComponent* Techniques = GetPlayerTechniques())
		{
			Techniques->RestoreHealth();
		}
		OnSparringCallout.Broadcast(EBendingSparringCallout::DuelStarted, Above, 0.0);
		Announce(TEXT("Duel! Knock your sparring partner down (hold C to guard)"));
	}
	if (Events.DuelEnded < 0)
	{
		return;
	}
	if (Events.DuelEnded == 0)
	{
		OnSparringCallout.Broadcast(EBendingSparringCallout::DuelCalledOff, Above, 0.0);
		Announce(TEXT("You left the ring: the duel is called off"));
		return;
	}
	if (Events.DuelEnded == 1)
	{
		OnSparringCallout.Broadcast(EBendingSparringCallout::DuelWon, Above, 0.0);
		Announce(FString::Printf(TEXT("You win the duel! (%d - %d)"), Brain.PlayerWins, Brain.PartnerWins));
	}
	else
	{
		OnSparringCallout.Broadcast(EBendingSparringCallout::DuelLost, Above, 0.0);
		Announce(FString::Printf(TEXT("Your sparring partner wins the duel (%d - %d)"), Brain.PlayerWins, Brain.PartnerWins));
	}
}

void ABendingSparringPartner::Announce(const FString& Text) const
{
	if (UBendingTechniqueComponent* Techniques = GetPlayerTechniques())
	{
		Techniques->AddMessage(Text, PartnerDuelColor);
	}
}

// ---------------------------------------------------------------------------------------------------- Pose

void ABendingSparringPartner::UpdatePose(float DeltaSeconds)
{
	using BendingSim::ESparringState;

	// Walk cycle: legs swing opposite each other, longer strides the faster it goes.
	const float Speed = static_cast<float>(VelocityCmS.Size2D());
	if (bOnGround)
	{
		StridePhase = FMath::Fmod(StridePhase + DeltaSeconds * Speed / 140.f * UE_PI, 2.f * UE_PI);
	}
	const float Swing = bOnGround ? FMath::Sin(StridePhase) * 32.f * FMath::Clamp(Speed / 340.f, 0.f, 1.2f) : 0.f;
	const float Progress = static_cast<float>(Brain.GetStateProgress());
	const float Ease = Progress * Progress * (3.f - 2.f * Progress);
	// The next flame leaves the right hand on even shots; the last one left the other.
	const bool bNextRight = Brain.ShotsFired % 2 == 0;
	const bool bLastRight = Brain.ShotsFired == 0 || !bNextRight;
	const bool bBurst = Brain.Attack == BendingSim::ESparringAttack::Burst;

	FSparringPartnerPose Target;
	switch (Brain.State)
	{
	case ESparringState::Waiting:
		PartnerHandsBehindBack(Target);
		break;

	case ESparringState::Ready:
		PartnerBow(Target, FMath::Sin(Progress * UE_PI));
		break;

	case ESparringState::Returning:
	{
		// It bows, then walks back to its post.
		const float BowS = static_cast<float>(Brain.Tuning.BowS);
		const float BowProgress = BowS > 0.f ? FMath::Clamp(static_cast<float>(Brain.StateTimeS) / BowS, 0.f, 1.f) : 1.f;
		if (BowProgress < 1.f)
		{
			PartnerBow(Target, FMath::Sin(BowProgress * UE_PI));
		}
		else
		{
			PartnerHandsBehindBack(Target);
		}
		break;
	}

	case ESparringState::WindUp:
		if (bBurst)
		{
			// Gathering the fire in close, crouching into it.
			Target.ArmL = Target.ArmR = FMath::Lerp(60.f, 15.f, Ease);
			Target.ArmRollL = -35.f;
			Target.ArmRollR = 35.f;
			Target.SpinePitch = -14.f * Ease;
		}
		else
		{
			// The striking arm draws back and the body twists away: the telegraph.
			PartnerFightingStance(Target);
			if (bNextRight)
			{
				Target.ArmR = FMath::Lerp(60.f, -50.f, Ease);
				Target.ArmRollR = -18.f;
				Target.SpineYaw = 28.f * Ease;
			}
			else
			{
				Target.ArmL = FMath::Lerp(75.f, -50.f, Ease);
				Target.ArmRollL = 18.f;
				Target.SpineYaw = -28.f * Ease;
			}
		}
		break;

	case ESparringState::Attacking:
		if (bBurst)
		{
			// Arms flung out wide as the ring bursts.
			Target.ArmL = Target.ArmR = 5.f;
			Target.ArmRollL = 85.f;
			Target.ArmRollR = -85.f;
		}
		else
		{
			PartnerPunch(Target, bLastRight);
		}
		break;

	case ESparringState::Recovering:
	{
		// Committed: easing back from the strike to its stance.
		FSparringPartnerPose Strike;
		if (bBurst)
		{
			Strike.ArmL = Strike.ArmR = 5.f;
			Strike.ArmRollL = 85.f;
			Strike.ArmRollR = -85.f;
		}
		else
		{
			PartnerPunch(Strike, bLastRight);
		}
		FSparringPartnerPose Stance;
		PartnerFightingStance(Stance);
		Target = LerpPartnerPose(Strike, Stance, Ease);
		break;
	}

	case ESparringState::Guarding:
		// Forearms crossed before the face.
		Target.ArmL = Target.ArmR = 112.f;
		Target.ArmRollL = -30.f;
		Target.ArmRollR = 30.f;
		Target.SpinePitch = -4.f;
		break;

	case ESparringState::Staggered:
		// Rocked back by the blow.
		Target.ArmL = Target.ArmR = 30.f;
		Target.ArmRollL = 40.f;
		Target.ArmRollR = -40.f;
		Target.SpinePitch = 22.f;
		break;

	case ESparringState::Down:
		Target.ArmL = Target.ArmR = 10.f;
		Target.ArmRollL = 70.f;
		Target.ArmRollR = -70.f;
		break;

	case ESparringState::Dodging:
		PartnerFightingStance(Target);
		Target.SpinePitch = -10.f;
		break;

	case ESparringState::Moving:
	default:
		PartnerFightingStance(Target);
		break;
	}
	Target.LegL = Brain.IsDown() ? 8.f : Swing;
	Target.LegR = Brain.IsDown() ? -6.f : -Swing;

	// Strikes snap out; everything else eases.
	const float PoseSpeed = Brain.State == ESparringState::Attacking ? 28.f : 14.f;
	CurrentPose = LerpPartnerPose(CurrentPose, Target, FMath::Clamp(DeltaSeconds * PoseSpeed, 0.f, 1.f));

	// Knocked down, the body tips over about its feet the way it was hit, until it gets up.
	DownWeight = FMath::FInterpTo(DownWeight, Brain.IsDown() ? 1.f : 0.f, DeltaSeconds, 6.f);
	FVector LocalFall = GetActorRotation().UnrotateVector(FallDirection);
	LocalFall.Z = 0.0;
	FVector TiltAxis = FVector::CrossProduct(FVector::UpVector, LocalFall.GetSafeNormal());
	TiltAxis = TiltAxis.IsNearlyZero() ? FVector::RightVector : TiltAxis.GetSafeNormal();
	BodyRoot->SetRelativeLocationAndRotation(FVector(0.0, 0.0, -PartnerFeetOffsetCm + 14.0 * DownWeight), FQuat(TiltAxis, PartnerFallTiltRad * DownWeight));
	Spine->SetRelativeRotation(FRotator(CurrentPose.SpinePitch, CurrentPose.SpineYaw, 0.f));
	HipL->SetRelativeRotation(FRotator(CurrentPose.LegL, 0.f, 0.f));
	HipR->SetRelativeRotation(FRotator(CurrentPose.LegR, 0.f, 0.f));
	ShoulderL->SetRelativeRotation(FRotator(CurrentPose.ArmL, 0.f, CurrentPose.ArmRollL));
	ShoulderR->SetRelativeRotation(FRotator(CurrentPose.ArmR, 0.f, CurrentPose.ArmRollR));

	// The telegraph: fire gathers at the striking fist through the wind-up and flares as it strikes.
	const bool bWindUp = Brain.State == ESparringState::WindUp;
	const bool bShowFlame = bWindUp || Brain.State == ESparringState::Attacking;
	FistFlame->SetVisibility(bShowFlame);
	FistLight->SetVisibility(bShowFlame);
	if (bShowFlame)
	{
		const bool bRightFist = bWindUp ? bNextRight : bLastRight;
		const USceneComponent* Fist = bRightFist ? HandR.Get() : HandL.Get();
		const float Glow = bWindUp ? 0.35f + 0.65f * Ease : 1.f;
		const FVector FistLocation = Fist->GetComponentLocation();
		FistFlame->SetWorldLocation(FistLocation);
		FistFlame->SetWorldScale3D(FVector((8.0 + 16.0 * Glow) / 100.0));
		FistLight->SetWorldLocation(FistLocation);
		FistLight->SetIntensity(FistLightCandelas * Glow);
	}
}

// ---------------------------------------------------------------------------------------------------- Queries

double ABendingSparringPartner::GetHealthFraction() const
{
	return Brain.Tuning.MaxHealth > 0.0 ? FMath::Clamp(Brain.Health / Brain.Tuning.MaxHealth, 0.0, 1.0) : 0.0;
}

FVector ABendingSparringPartner::GetFeetLocation() const
{
	return GetActorLocation() - FVector(0.0, 0.0, PartnerFeetOffsetCm);
}

FVector ABendingSparringPartner::GetChestLocation() const
{
	return GetFeetLocation() + FVector(0.0, 0.0, PartnerChestHeightCm);
}

FVector ABendingSparringPartner::GetHeadLocation() const
{
	return Head ? Head->GetComponentLocation() : GetFeetLocation() + FVector(0.0, 0.0, 165.0);
}

ABendingSandboxArena* ABendingSparringPartner::GetArena() const
{
	if (!CachedArena.IsValid())
	{
		CachedArena = ABendingSandboxArena::Find(GetWorld());
	}
	return CachedArena.Get();
}

APawn* ABendingSparringPartner::GetPlayerPawn() const
{
	return UGameplayStatics::GetPlayerPawn(this, 0);
}

UBendingTechniqueComponent* ABendingSparringPartner::GetPlayerTechniques() const
{
	const APawn* Player = GetPlayerPawn();
	return Player ? Player->FindComponentByClass<UBendingTechniqueComponent>() : nullptr;
}
