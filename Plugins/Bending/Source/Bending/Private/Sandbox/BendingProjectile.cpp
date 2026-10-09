#include "Sandbox/BendingProjectile.h"

#include "BendingSettings.h"
#include "CollisionQueryParams.h"
#include "Components/PointLightComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/HitResult.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Interaction/BendingInteractionSubsystem.h"
#include "Interaction/ElementalVolumeComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Physics/BendingUnits.h"
#include "Sandbox/BendingSandboxArena.h"
#include "Sandbox/BendingSandboxLibrary.h"
#include "Sim/BendingTechniques.h"

namespace
{
	/** Height of a resting flame's centre above the ground, in radii. */
	constexpr double GroundedFlameLift = 0.55;

	/** Share of the simulated radius drawn: flame as its bright core, air as a faint core that does not hide the view. */
	double GetVisualRadiusFraction(EBendingProjectileKind Kind)
	{
		switch (Kind)
		{
		case EBendingProjectileKind::Fire: return 0.6;
		case EBendingProjectileKind::Air:  return 0.3;
		// Drawn larger than its few centimetres of ice so it reads at range.
		case EBendingProjectileKind::IceShard: return 1.6;
		default:                           return 1.0;
		}
	}

	/** Dull red near extinction to yellow-white at the adiabatic ceiling. */
	FLinearColor GetFlameColor(double TemperatureK)
	{
		const UBendingSettings& Settings = UBendingSettings::Get();
		const float Heat = FMath::Clamp(static_cast<float>((TemperatureK - Settings.FireExtinguishTemperatureK)
			/ FMath::Max(Settings.MaxFlameTemperatureK - Settings.FireExtinguishTemperatureK, 1.f)), 0.f, 1.f);
		return UBendingSandboxLibrary::LerpColor(UBendingSandboxLibrary::FromSRGB(150, 25, 8), UBendingSandboxLibrary::FromSRGB(255, 205, 90), Heat);
	}
}

ABendingProjectile::ABendingProjectile()
{
	PrimaryActorTick.bCanEverTick = true;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(Root);
	Mesh->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	Mesh->SetGenerateOverlapEvents(false);
	Mesh->SetCanEverAffectNavigation(false);
	Mesh->SetCastShadow(false);

	// Registered by InitProjectile once its matter is known.
	Volume = CreateDefaultSubobject<UElementalVolumeComponent>(TEXT("Volume"));
	Volume->SetupAttachment(Root);
	Volume->bAutoActivate = false;
	Volume->VelocitySource = EElementalVelocitySource::Manual;
	Volume->bApplyImpulsesToAttachedBody = false;

	Light = CreateDefaultSubobject<UPointLightComponent>(TEXT("Light"));
	Light->SetupAttachment(Root);
	Light->SetMobility(EComponentMobility::Movable);
	Light->IntensityUnits = ELightUnits::Candelas;
	Light->SetCastShadows(false);
	Light->SetVisibility(false);
}

ABendingProjectile* ABendingProjectile::SpawnProjectile(UWorld* World, EBendingProjectileKind InKind, const BendingSim::FVolume& SimVolume,
	AActor* InstigatorActor, bool bWithLight)
{
	if (!World)
	{
		return nullptr;
	}
	FActorSpawnParameters Params;
	Params.Owner = InstigatorActor;
	Params.Instigator = Cast<APawn>(InstigatorActor);
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ABendingProjectile* Projectile = World->SpawnActor<ABendingProjectile>(ABendingProjectile::StaticClass(),
		FTransform(BendingUnits::ToEngine(SimVolume.LocationCm)), Params);
	if (Projectile)
	{
		Projectile->InitProjectile(InKind, SimVolume, bWithLight);
	}
	return Projectile;
}

void ABendingProjectile::InitProjectile(EBendingProjectileKind InKind, const BendingSim::FVolume& SimVolume, bool bWithLight)
{
	Kind = InKind;
	bWithFlameLight = bWithLight && Kind == EBendingProjectileKind::Fire;
	VelocityCmS = BendingUnits::ToEngine(SimVolume.VelocityCmS);
	SetActorLocation(BendingUnits::ToEngine(SimVolume.LocationCm));
	PreviousLocation = GetActorLocation();

	const BendingSim::FTechniqueTuning& Tuning = BendingSim::GetDefaultTechniqueTuning();
	switch (Kind)
	{
	case EBendingProjectileKind::Fire:  LifetimeSeconds = Tuning.ProjectileLifetimeS; break;
	case EBendingProjectileKind::Air:   LifetimeSeconds = FMath::Min(Tuning.ProjectileLifetimeS, Tuning.AirLifetimeS); break;
	case EBendingProjectileKind::Water: LifetimeSeconds = WaterLifetimeS; break;
	case EBendingProjectileKind::IceShard: LifetimeSeconds = IceShardLifetimeS; break;
	}

	UWorld* World = GetWorld();
	Interaction = World ? World->GetSubsystem<UBendingInteractionSubsystem>() : nullptr;
	Arena = ABendingSandboxArena::Find(World);

	Mesh->SetStaticMesh(UBendingSandboxLibrary::LoadBasicShape(TEXT("Sphere")));
	Mesh->SetCastShadow(Kind == EBendingProjectileKind::Water || Kind == EBendingProjectileKind::IceShard);
	switch (Kind)
	{
	case EBendingProjectileKind::Fire:  Material = UBendingSandboxLibrary::SetMeshColor(Mesh, GetFlameColor(SimVolume.TemperatureK)); break;
	case EBendingProjectileKind::Air:   Material = UBendingSandboxLibrary::SetMeshColor(Mesh, UBendingSandboxLibrary::FromSRGB(225, 240, 255)); break;
	case EBendingProjectileKind::Water: Material = UBendingSandboxLibrary::SetMeshColor(Mesh, UBendingSandboxLibrary::FromSRGB(45, 115, 225)); break;
	case EBendingProjectileKind::IceShard: Material = UBendingSandboxLibrary::SetMeshColor(Mesh, UBendingSandboxLibrary::FromSRGB(200, 240, 255)); break;
	}

	if (bWithFlameLight)
	{
		Light->SetLightColor(FLinearColor(1.f, 0.55f, 0.2f));
		Light->SetAttenuationRadius(900.f);
		Light->SetVisibility(true);
	}

	Volume->InitialState = FElementalVolumeState::FromSim(SimVolume);
	Volume->SetManualVelocity(VelocityCmS);
	Volume->OnImpulseReceived.AddUniqueDynamic(this, &ABendingProjectile::HandleImpulse);
	Volume->OnDepleted.AddUniqueDynamic(this, &ABendingProjectile::HandleDepleted);
	Volume->Activate(true);
	bInitialized = true;

	FElementalVolumeState State;
	if (Volume->GetSimulatedState(State))
	{
		UpdateVisuals(State);
	}
}

void ABendingProjectile::SetGrounded(bool bInGrounded)
{
	bGrounded = bInGrounded;
	if (bGrounded)
	{
		VelocityCmS = FVector::ZeroVector;
	}
}

void ABendingProjectile::KeepAlive(double Seconds)
{
	LifetimeSeconds = FMath::Max(LifetimeSeconds, AgeSeconds + Seconds);
}

double ABendingProjectile::AddHeat(double HeatJ)
{
	double AcceptedJ = 0.0;
	Volume->AddHeat(HeatJ, AcceptedJ);
	return AcceptedJ;
}

void ABendingProjectile::DragAlongGround(const FVector& TargetCm, double MaxStepCm)
{
	FVector Location = GetActorLocation();
	const FVector Offset(TargetCm.X - Location.X, TargetCm.Y - Location.Y, 0.0);
	const double Distance = Offset.Size();
	if (Distance <= 1.0 || MaxStepCm <= 0.0)
	{
		return;
	}
	Location += Offset * (FMath::Min(Distance, MaxStepCm) / Distance);
	SetActorLocation(Location);
}

void ABendingProjectile::MakeSpray(double DragTimeS)
{
	SprayDragTimeS = FMath::Max(DragTimeS, 0.02);
	Mesh->SetVisibility(false);
	Mesh->SetCastShadow(false);
}

void ABendingProjectile::SetVelocityCmS(const FVector& InVelocityCmS)
{
	if (!bGrounded)
	{
		VelocityCmS = InVelocityCmS;
	}
}

double ABendingProjectile::GetMassKg() const
{
	FElementalVolumeState State;
	return Volume->GetSimulatedState(State) ? State.MassKg : 0.0;
}

void ABendingProjectile::SetFuel(double InFuelJ, double PowerW)
{
	FuelJ = FMath::Max(InFuelJ, 0.0);
	FuelPowerW = FMath::Max(PowerW, 0.0);
}

double ABendingProjectile::GetGroundHeight(const FVector& LocationCm) const
{
	const ABendingSandboxArena* ArenaActor = Arena.Get();
	return ArenaActor ? ArenaActor->GetGroundHeightAt(LocationCm) : LocationCm.Z;
}

void ABendingProjectile::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bInitialized)
	{
		return;
	}

	AgeSeconds += DeltaSeconds;
	FElementalVolumeState State;
	if (AgeSeconds > LifetimeSeconds || !Volume->GetSimulatedState(State))
	{
		Destroy();
		return;
	}

	FVector Location = GetActorLocation();
	if (bGrounded)
	{
		VelocityCmS = FVector::ZeroVector;
		Location.Z = GetGroundHeight(Location) + GroundedFlameLift * State.RadiusCm;
		if (FuelJ > 0.0)
		{
			// It burns what it lit until that runs out.
			const double BurnJ = FMath::Min(FuelJ, FuelPowerW * DeltaSeconds);
			AddHeat(BurnJ);
			FuelJ -= BurnJ;
		}
	}
	else
	{
		if (Kind == EBendingProjectileKind::Water)
		{
			if (SprayDragTimeS > 0.0)
			{
				// Droplets lose their speed to the air within a few metres.
				VelocityCmS *= BendingSim::KExp(-DeltaSeconds / SprayDragTimeS);
			}
			VelocityCmS.Z += GetWorld()->GetGravityZ() * DeltaSeconds;
		}
		else if (Kind == EBendingProjectileKind::Air)
		{
			// Bent air hands its momentum to the still air around it.
			const double DecayS = FMath::Max(BendingSim::GetDefaultTechniqueTuning().AirDecayTimeS, 0.05);
			VelocityCmS *= BendingSim::KExp(-DeltaSeconds / DecayS);
		}
		else if (Kind == EBendingProjectileKind::IceShard)
		{
			// The bender carries the daggers: they drop only slowly.
			VelocityCmS.Z += IceShardGravityScale * GetWorld()->GetGravityZ() * DeltaSeconds;
		}
		const FVector From = Location;
		Location += VelocityCmS * DeltaSeconds;
		if (Kind == EBendingProjectileKind::IceShard && StrikeAlongPath(From, Location, State))
		{
			return;
		}
		if (HandleGroundContact(Location, State))
		{
			return;
		}
	}

	SetActorLocation(Location);
	Volume->SetManualVelocity(VelocityCmS);
	UpdateVisuals(State);
}

bool ABendingProjectile::StrikeAlongPath(const FVector& From, const FVector& To, const FElementalVolumeState& State)
{
	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(ECC_PhysicsBody);
	Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
	// Braziers, ice blocks and the ground shatter it too.
	Objects.AddObjectTypesToQuery(ECC_WorldStatic);
	Objects.AddObjectTypesToQuery(ECC_Pawn);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(BendingIceShard), false, this);
	if (AActor* OwnerActor = GetOwner())
	{
		Params.AddIgnoredActor(OwnerActor);
	}
	FHitResult Hit;
	if (!GetWorld()->SweepSingleByObjectType(Hit, From, To, FQuat::Identity, Objects, FCollisionShape::MakeSphere(static_cast<float>(State.RadiusCm)), Params))
	{
		return false;
	}
	// It strikes and shatters: its momentum goes into what it hit.
	if (UPrimitiveComponent* Struck = Hit.GetComponent(); Struck && Struck->IsSimulatingPhysics())
	{
		Struck->AddImpulseAtLocation(VelocityCmS * State.MassKg, Hit.ImpactPoint);
	}
	Destroy();
	return true;
}

bool ABendingProjectile::HandleGroundContact(FVector& Location, const FElementalVolumeState& State)
{
	const double Radius = State.RadiusCm;
	ABendingSandboxArena* ArenaActor = Arena.Get();

	bool bHasFloor = false;
	double FloorZ = 0.0;
	FVector FloorNormal = FVector::UpVector;
	int32 Pond = INDEX_NONE;
	if (ArenaActor)
	{
		Pond = ArenaActor->FindPondAt(Location);
		const bool bOnPond = Kind == EBendingProjectileKind::Water && Pond != INDEX_NONE;
		FloorZ = bOnPond ? ArenaActor->GetLayout().Ponds[Pond].SurfaceHeightCm : ArenaActor->GetGroundHeightAt(Location);
		FloorNormal = bOnPond ? FVector::UpVector : ArenaActor->GetGroundNormalAt(Location);
		bHasFloor = true;
	}
	else
	{
		// No generated terrain: any static geometry along the path is the floor.
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(BendingProjectileGround), false, this);
		if (const AActor* OwnerActor = GetOwner())
		{
			Params.AddIgnoredActor(OwnerActor);
		}
		const FVector Reach = FVector(0.0, 0.0, Radius);
		if (GetWorld()->LineTraceSingleByObjectType(Hit, PreviousLocation, Location - Reach, FCollisionObjectQueryParams(ECC_WorldStatic), Params))
		{
			bHasFloor = true;
			FloorZ = Hit.ImpactPoint.Z;
			FloorNormal = Hit.ImpactNormal;
		}
	}
	PreviousLocation = Location;

	const double ContactFraction = (Kind == EBendingProjectileKind::Water || Kind == EBendingProjectileKind::IceShard) ? 1.0
		: (Kind == EBendingProjectileKind::Fire ? 0.4 : 0.5);
	if (!bHasFloor || Location.Z - ContactFraction * Radius > FloorZ)
	{
		return false;
	}

	switch (Kind)
	{
	case EBendingProjectileKind::Water:
		if (ArenaActor && Pond != INDEX_NONE)
		{
			ArenaActor->ReturnPondWater(Pond, State.MassKg);
		}
		else if (UBendingInteractionSubsystem* Simulation = Interaction.Get())
		{
			Simulation->DepositWaterOnSurface(FVector(Location.X, Location.Y, FloorZ), State.MassKg,
				ArenaActor ? ArenaActor->GetSoilPorosity() : ABendingSandboxArena::DefaultSoilPorosity);
		}
		Destroy();
		return true;

	case EBendingProjectileKind::Fire:
		// Flame splashes onto the ground and burns there until it cools.
		SetGrounded(true);
		Location.Z = FloorZ + GroundedFlameLift * Radius;
		return false;

	case EBendingProjectileKind::IceShard:
		// Shatters on the ground.
		Destroy();
		return true;

	case EBendingProjectileKind::Air:
	{
		// Air meeting the ground flows along it.
		Location.Z = FloorZ + 0.5 * Radius;
		const double Into = FVector::DotProduct(VelocityCmS, FloorNormal);
		if (Into < 0.0)
		{
			VelocityCmS -= FloorNormal * Into;
		}
		return false;
	}
	}
	return false;
}

void ABendingProjectile::UpdateVisuals(const FElementalVolumeState& State)
{
	const double VisualRadius = FMath::Max(State.RadiusCm * GetVisualRadiusFraction(Kind), 4.0);
	if (Kind == EBendingProjectileKind::IceShard)
	{
		// A long crystal pointing where it flies.
		Mesh->SetRelativeScale3D(FVector(4.0, 1.0, 1.0) * (VisualRadius / 50.0));
		if (!VelocityCmS.IsNearlyZero())
		{
			SetActorRotation(VelocityCmS.Rotation());
		}
		return;
	}
	Mesh->SetRelativeScale3D(FVector(VisualRadius / 50.0));

	if (Kind != EBendingProjectileKind::Fire)
	{
		return;
	}
	const float TemperatureK = static_cast<float>(State.TemperatureK);
	if (FMath::Abs(TemperatureK - LastColorTemperatureK) > 15.f)
	{
		LastColorTemperatureK = TemperatureK;
		Material = UBendingSandboxLibrary::SetMeshColor(Mesh, GetFlameColor(State.TemperatureK));
	}
	if (bWithFlameLight)
	{
		const UBendingSettings& Settings = UBendingSettings::Get();
		const float Heat = FMath::Clamp((TemperatureK - Settings.FireExtinguishTemperatureK) / 800.f, 0.f, 1.f);
		Light->SetIntensity(FlameLightCandelas * Heat);
	}
}

void ABendingProjectile::HandleImpulse(FVector ImpulseKgCmS)
{
	if (bGrounded)
	{
		return;
	}
	FElementalVolumeState State;
	if (Volume->GetSimulatedState(State) && State.MassKg > UE_KINDA_SMALL_NUMBER)
	{
		VelocityCmS += ImpulseKgCmS / State.MassKg;
	}
}

void ABendingProjectile::HandleDepleted(UElementalVolumeComponent* DepletedVolume)
{
	// Flame extinguished, or water boiled away.
	Destroy();
}
