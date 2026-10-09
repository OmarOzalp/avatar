#include "Sandbox/BendingPropActor.h"

#include "BendingSettings.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/World.h"
#include "Interaction/BendingInteractionSubsystem.h"
#include "Interaction/ElementalVolumeComponent.h"
#include "Physics/BendingUnits.h"
#include "Sandbox/BendingSandboxArena.h"
#include "Sandbox/BendingSandboxLibrary.h"
#include "Sim/BendingTechniques.h"
#include "Sim/BendingThermo.h"

namespace
{
	constexpr double BrazierPedestalRadiusCm = 30.0;
	constexpr double BrazierPedestalHeightCm = 100.0;
	/** Share of the flame's simulated radius drawn as its bright core. */
	constexpr double FlameVisualFraction = 0.6;
	/** Spawn solid props this far above the ground so they never start inside it. */
	constexpr double SpawnClearanceCm = 1.0;

	FLinearColor GetPropColor(BendingSim::EArenaProp Kind)
	{
		using BendingSim::EArenaProp;
		switch (Kind)
		{
		case EArenaProp::Stone:    return UBendingSandboxLibrary::FromSRGB(132, 132, 128);
		case EArenaProp::Rock:     return UBendingSandboxLibrary::FromSRGB(110, 108, 104);
		case EArenaProp::Boulder:  return UBendingSandboxLibrary::FromSRGB(86, 84, 82);
		case EArenaProp::Clod:     return UBendingSandboxLibrary::FromSRGB(124, 88, 56);
		case EArenaProp::Dummy:    return UBendingSandboxLibrary::FromSRGB(198, 160, 108);
		case EArenaProp::Brazier:  return UBendingSandboxLibrary::FromSRGB(118, 116, 110);
		case EArenaProp::IceBlock: return UBendingSandboxLibrary::FromSRGB(190, 235, 250);
		default:                   return FLinearColor::White;
		}
	}
}

ABendingPropActor::ABendingPropActor()
{
	PrimaryActorTick.bCanEverTick = true;

	Body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body"));
	Body->SetMobility(EComponentMobility::Movable);
	Body->SetCanEverAffectNavigation(false);
	RootComponent = Body;

	Detail = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Detail"));
	Accent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Accent"));
	for (UStaticMeshComponent* Part : { Detail.Get(), Accent.Get() })
	{
		Part->SetupAttachment(Body);
		Part->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
		Part->SetGenerateOverlapEvents(false);
		Part->SetCanEverAffectNavigation(false);
		Part->SetVisibility(false);
	}

	Light = CreateDefaultSubobject<UPointLightComponent>(TEXT("Light"));
	Light->SetupAttachment(Body);
	Light->SetMobility(EComponentMobility::Movable);
	Light->IntensityUnits = ELightUnits::Candelas;
	Light->SetCastShadows(false);
	Light->SetVisibility(false);

	// Registered by the Init functions once the prop's matter is known.
	Volume = CreateDefaultSubobject<UElementalVolumeComponent>(TEXT("Volume"));
	Volume->SetupAttachment(Body);
	Volume->bAutoActivate = false;
}

// ---------------------------------------------------------------------------------------------------- Setup

void ABendingPropActor::InitArenaProp(BendingSim::EArenaProp InKind, const FVector& InGroundCm)
{
	using BendingSim::EArenaProp;

	Kind = InKind;
	bArenaProp = true;
	GroundCm = InGroundCm;

	const BendingSim::FArenaPropSpec& Spec = BendingSim::GetArenaPropSpec(Kind);
	const BendingSim::FVolume SimVolume = BendingSim::MakeArenaPropVolume(Kind, BendingUnits::ToSim(GroundCm));
	const FVector Center = BendingUnits::ToEngine(SimVolume.LocationCm);
	const FVector Lifted = Center + FVector(0.0, 0.0, SpawnClearanceCm);
	RadiusCm = Spec.RadiusCm;
	SoilMassKg = Spec.MassKg;

	switch (Kind)
	{
	case EArenaProp::Stone:
	case EArenaProp::Rock:
	case EArenaProp::Boulder:
	case EArenaProp::Clod:
		SetupBody(TEXT("Sphere"), Lifted, FVector(2.0 * Spec.RadiusCm), GetPropColor(Kind));
		EnablePhysics(Spec.MassKg);
		ConfigureVolume(SimVolume, Lifted, false);
		break;

	case EArenaProp::Dummy:
	{
		// Upright capsule of wood: a post, a head and a cross-bar for arms.
		const double HeightCm = 2.0 * (Spec.HalfHeightCm + Spec.RadiusCm);
		const double HeadRadiusCm = 1.15 * Spec.RadiusCm;
		SetupBody(TEXT("Cylinder"), Lifted, FVector(2.0 * Spec.RadiusCm, 2.0 * Spec.RadiusCm, HeightCm), GetPropColor(Kind));
		SetupDetail(Detail, TEXT("Sphere"), Lifted + FVector(0.0, 0.0, 0.5 * HeightCm + 0.7 * HeadRadiusCm), FVector(2.0 * HeadRadiusCm),
			UBendingSandboxLibrary::FromSRGB(180, 138, 92));
		SetupDetail(Accent, TEXT("Cube"), Lifted + FVector(0.0, 0.0, 0.25 * HeightCm), FVector(8.0, 4.5 * Spec.RadiusCm, 8.0),
			UBendingSandboxLibrary::FromSRGB(150, 112, 72));
		EnablePhysics(Spec.MassKg);
		ConfigureVolume(SimVolume, Lifted, false);
		break;
	}

	case EArenaProp::Brazier:
		SetupBody(TEXT("Cylinder"), GroundCm + FVector(0.0, 0.0, 0.5 * BrazierPedestalHeightCm),
			FVector(2.0 * BrazierPedestalRadiusCm, 2.0 * BrazierPedestalRadiusCm, BrazierPedestalHeightCm), GetPropColor(Kind));
		Body->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
		SetupDetail(Detail, TEXT("Sphere"), Center, FVector(2.0 * FlameVisualFraction * SimVolume.RadiusCm), UBendingSandboxLibrary::FromSRGB(255, 140, 40));
		Detail->SetCastShadow(false);
		Light->SetWorldLocation(Center);
		Light->SetLightColor(FLinearColor(1.f, 0.55f, 0.2f));
		Light->SetAttenuationRadius(1200.f);
		// The flame stays on the pedestal: its position is pushed every frame and it does not move.
		ConfigureVolume(SimVolume, Center, true);
		SetFlameLit(true);
		break;

	case EArenaProp::IceBlock:
		// A cube holding the block's volume of ice, frozen to the ground.
		IceSideCm = BendingSim::MToCm(BendingSim::KCbrt(Spec.MassKg / FMath::Max(Spec.DensityKgM3, 1.0)));
		SetupBody(TEXT("Cube"), GroundCm + FVector(0.0, 0.0, 0.5 * IceSideCm), FVector(IceSideCm), GetPropColor(Kind));
		Body->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
		ConfigureVolume(SimVolume, Center, true);
		break;

	default:
		break;
	}
}

void ABendingPropActor::InitThrownRock(double MassKg, double DensityKgM3, const FVector& LocationCm)
{
	bThrownRock = true;
	bHeld = true;
	SoilMassKg = MassKg;

	const BendingSim::FVolume Rock = BendingSim::MakeRock(MassKg, DensityKgM3, BendingUnits::ToSim(LocationCm), BendingSim::FVec3());
	RadiusCm = Rock.RadiusCm;
	SetupBody(TEXT("Sphere"), LocationCm, FVector(2.0 * RadiusCm), UBendingSandboxLibrary::FromSRGB(128, 100, 72));
	Body->SetCollisionProfileName(UCollisionProfile::PhysicsActor_ProfileName);
	// Rising in front of the bender, it must not shove the bender around.
	Body->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	Body->SetMassOverrideInKg(NAME_None, static_cast<float>(MassKg), true);
	Body->SetAngularDamping(0.4f);
	ConfigureVolume(Rock, LocationCm, false);
}

void ABendingPropActor::SetupBody(const TCHAR* ShapeName, const FVector& CenterCm, const FVector& SizeCm, const FLinearColor& Color)
{
	Body->SetStaticMesh(UBendingSandboxLibrary::LoadBasicShape(ShapeName));
	SetActorLocation(CenterCm, false, nullptr, ETeleportType::TeleportPhysics);
	Body->SetWorldScale3D(SizeCm / 100.0);
	BaseColor = Color;
	UBendingSandboxLibrary::SetMeshColor(Body, Color);
}

void ABendingPropActor::SetupDetail(UStaticMeshComponent* Part, const TCHAR* ShapeName, const FVector& CenterCm, const FVector& SizeCm, const FLinearColor& Color)
{
	// World placement while the body is upright; from then on the part follows the body rigidly.
	Part->SetStaticMesh(UBendingSandboxLibrary::LoadBasicShape(ShapeName));
	Part->SetWorldLocation(CenterCm);
	Part->SetWorldScale3D(SizeCm / 100.0);
	UBendingSandboxLibrary::SetMeshColor(Part, Color);
	Part->SetVisibility(true);
}

void ABendingPropActor::EnablePhysics(double MassKg)
{
	Body->SetCollisionProfileName(UCollisionProfile::PhysicsActor_ProfileName);
	Body->SetSimulatePhysics(true);
	Body->SetMassOverrideInKg(NAME_None, static_cast<float>(MassKg), true);
	// Round stones would otherwise roll down every slope forever.
	Body->SetAngularDamping(0.4f);
}

void ABendingPropActor::ConfigureVolume(const BendingSim::FVolume& SimVolume, const FVector& CenterCm, bool bManualVelocity)
{
	Volume->SetWorldLocation(CenterCm);
	Volume->InitialState = FElementalVolumeState::FromSim(SimVolume);
	Volume->VelocitySource = bManualVelocity ? EElementalVelocitySource::Manual : EElementalVelocitySource::AttachedPhysicsBody;
	Volume->SetManualVelocity(FVector::ZeroVector);
	Volume->OnSubstanceChanged.AddUniqueDynamic(this, &ABendingPropActor::HandleSubstanceChanged);
	Volume->OnDepleted.AddUniqueDynamic(this, &ABendingPropActor::HandleDepleted);
	// Registers now during play; when built before BeginPlay (by the game mode), registers at BeginPlay.
	Volume->Activate(true);
}

// ---------------------------------------------------------------------------------------------------- Thrown rocks

void ABendingPropActor::SetHeldLocation(const FVector& LocationCm)
{
	if (bHeld)
	{
		SetActorLocation(LocationCm, false, nullptr, ETeleportType::TeleportPhysics);
	}
}

void ABendingPropActor::Launch(const FVector& VelocityCmS)
{
	bHeld = false;
	Body->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
	Body->SetSimulatePhysics(true);
	Body->SetPhysicsLinearVelocity(VelocityCmS);
}

void ABendingPropActor::CrumbleIntoGround(BendingSim::FTerrain& Terrain)
{
	const FVector Location = GetActorLocation();
	const BendingSim::FVec3 Ground(Location.X, Location.Y, Terrain.GetHeightAt(Location.X, Location.Y));
	const double VolumeM3 = SoilMassKg / FMath::Max(Terrain.SoilDensityKgM3, 1.0);
	Terrain.AddSoil(BendingSim::FTerrainBrush::Disc(Ground, FMath::Max(RadiusCm, 30.0), 40.0), VolumeM3);
	Destroy();
}

bool ABendingPropActor::IsLoose() const
{
	return !bHeld && Body && Body->IsSimulatingPhysics();
}

void ABendingPropActor::AddVelocity(const FVector& DeltaVCmS)
{
	if (IsLoose())
	{
		Body->AddImpulse(DeltaVCmS, NAME_None, /*bVelChange*/ true);
	}
}

double ABendingPropActor::GetMassKg() const
{
	FElementalVolumeState State;
	return Volume->GetSimulatedState(State) ? State.MassKg : SoilMassKg;
}

float ABendingPropActor::GetSoilPorosity() const
{
	const ABendingSandboxArena* Arena = ABendingSandboxArena::Find(GetWorld());
	return Arena ? Arena->GetSoilPorosity() : ABendingSandboxArena::DefaultSoilPorosity;
}

// ---------------------------------------------------------------------------------------------------- Frame

void ABendingPropActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (bArenaProp && Kind == BendingSim::EArenaProp::Brazier)
	{
		TickBrazier(DeltaSeconds);
	}
	else if (bArenaProp && Kind == BendingSim::EArenaProp::IceBlock)
	{
		TickIce();
	}
	else
	{
		TickSoakedEarth();
	}
}

void ABendingPropActor::TickBrazier(float DeltaSeconds)
{
	if (bFlameLit)
	{
		// Fuel: the kernel caps the flame at the adiabatic temperature, so surplus heat is simply not absorbed.
		double AcceptedJ = 0.0;
		Volume->AddHeat(BrazierPowerW * DeltaSeconds, AcceptedJ);

		FElementalVolumeState State;
		if (Volume->GetSimulatedState(State))
		{
			Detail->SetWorldScale3D(FVector(2.0 * FlameVisualFraction * State.RadiusCm / 100.0));
			const float Heat = FMath::Clamp((static_cast<float>(State.TemperatureK) - UBendingSettings::Get().FireExtinguishTemperatureK) / 800.f, 0.f, 1.f);
			Light->SetIntensity(BrazierLightCandelas * (0.3f + 0.7f * Heat));
		}
		return;
	}

	RelightCheckSeconds -= DeltaSeconds;
	if (RelightCheckSeconds > 0.0)
	{
		return;
	}
	RelightCheckSeconds = 0.2;

	const UBendingInteractionSubsystem* Interaction = GetWorld()->GetSubsystem<UBendingInteractionSubsystem>();
	if (!Interaction)
	{
		return;
	}
	TArray<FElementalVolumeHandle> Flames;
	Interaction->QueryVolumes(Volume->GetComponentLocation(), RelightRadiusCm, ElementalSubstance::ToMask(EElementalSubstance::Fire), Flames);
	if (Flames.Num() > 0)
	{
		// A fresh flame from the prop's recipe.
		Volume->RegisterVolume();
		SetFlameLit(Volume->IsVolumeRegistered());
	}
}

void ABendingPropActor::TickIce()
{
	FElementalVolumeState State;
	if (!Volume->GetSimulatedState(State) || State.MassKg <= 0.0)
	{
		return;
	}
	// Latent heat banked toward melting shows as the block turning to water.
	const float MeltFraction = FMath::Clamp(static_cast<float>(State.LatentHeatJ / (State.MassKg * BendingSim::Thermo::Constants::LatentHeatFusion)), 0.f, 1.f);
	if (FMath::Abs(MeltFraction - ShownMeltFraction) > 0.02f)
	{
		ShownMeltFraction = MeltFraction;
		UBendingSandboxLibrary::SetMeshColor(Body, UBendingSandboxLibrary::LerpColor(BaseColor, UBendingSandboxLibrary::FromSRGB(60, 130, 220), 0.8f * MeltFraction));
	}
}

void ABendingPropActor::TickSoakedEarth()
{
	FElementalVolumeState State;
	if (!Volume->GetSimulatedState(State) || State.Porosity <= 0.f)
	{
		return;
	}
	// Soaked soil darkens toward mud.
	if (FMath::Abs(State.Saturation - ShownSaturation) > 0.02f)
	{
		ShownSaturation = State.Saturation;
		UBendingSandboxLibrary::SetMeshColor(Body, UBendingSandboxLibrary::LerpColor(BaseColor, UBendingSandboxLibrary::FromSRGB(66, 46, 30), 0.85f * State.Saturation));
	}
}

void ABendingPropActor::SetFlameLit(bool bLit)
{
	bFlameLit = bLit;
	Detail->SetVisibility(bLit);
	Light->SetVisibility(bLit);
}

void ABendingPropActor::HandleSubstanceChanged(EElementalSubstance OldSubstance, EElementalSubstance NewSubstance)
{
	if (!bArenaProp || Kind != BendingSim::EArenaProp::IceBlock || bMelted || NewSubstance != EElementalSubstance::Water)
	{
		return;
	}
	// Fully melted: the water runs into the soil (mud) and the block is gone.
	bMelted = true;
	FElementalVolumeState State;
	const double WaterKg = Volume->GetSimulatedState(State) ? State.MassKg : SoilMassKg;
	if (UBendingInteractionSubsystem* Interaction = GetWorld()->GetSubsystem<UBendingInteractionSubsystem>())
	{
		Interaction->DepositWaterOnSurface(GroundCm, WaterKg, GetSoilPorosity());
	}
	Destroy();
}

void ABendingPropActor::HandleDepleted(UElementalVolumeComponent* DepletedVolume)
{
	if (!bArenaProp || Kind != BendingSim::EArenaProp::Brazier || !bFlameLit)
	{
		return;
	}
	// Put out (water cooled it below its sustain temperature). It stays out until a flame comes close.
	Volume->UnregisterVolume();
	SetFlameLit(false);
	RelightCheckSeconds = 0.5;
}
