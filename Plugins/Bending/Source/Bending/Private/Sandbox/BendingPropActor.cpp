#include "Sandbox/BendingPropActor.h"

#include "BendingSettings.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/HitResult.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Interaction/BendingInteractionSubsystem.h"
#include "Interaction/ElementalVolumeComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Physics/BendingUnits.h"
#include "Sandbox/BendingProjectile.h"
#include "Sandbox/BendingSandboxArena.h"
#include "Sandbox/BendingSandboxLibrary.h"
#include "Sandbox/BendingTechniqueComponent.h"
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
	/** Below this speed change a physics contact is a nudge, not a blow (resting on the ground, a gentle push). */
	constexpr double MinBlowCmS = 150.0;
	/** Banners: where the cloth hangs, and how wide it is. */
	constexpr double BannerPoleHeightCm = 440.0;
	/** A barrel's water top above its base. */
	constexpr double BarrelHeightCm = 90.0;
	/** A waterbender's whip can be drawn from a barrel this close. */
	constexpr int32 SpillParcels = 6;
	constexpr int32 DebrisPieces = 7;

	FLinearColor GetBannerColor(int32 Element)
	{
		switch (Element)
		{
		case 1:  return UBendingSandboxLibrary::FromSRGB(70, 140, 60);   // earth
		case 2:  return UBendingSandboxLibrary::FromSRGB(50, 120, 210);  // water
		case 3:  return UBendingSandboxLibrary::FromSRGB(200, 50, 36);   // fire
		default: return UBendingSandboxLibrary::FromSRGB(235, 200, 90);  // air
		}
	}

	const FLinearColor CharColor = FLinearColor(0.02f, 0.015f, 0.012f);

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
		case EArenaProp::Banner:   return UBendingSandboxLibrary::FromSRGB(120, 86, 58);
		case EArenaProp::Lantern:  return UBendingSandboxLibrary::FromSRGB(170, 168, 176);
		case EArenaProp::StrawBale: return UBendingSandboxLibrary::FromSRGB(222, 190, 110);
		case EArenaProp::Crate:    return UBendingSandboxLibrary::FromSRGB(176, 128, 76);
		case EArenaProp::WaterBarrel: return UBendingSandboxLibrary::FromSRGB(130, 90, 56);
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

	FlameMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("FlameMesh"));
	FlameMesh->SetupAttachment(Body);
	FlameMesh->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	FlameMesh->SetGenerateOverlapEvents(false);
	FlameMesh->SetCanEverAffectNavigation(false);
	FlameMesh->SetCastShadow(false);
	FlameMesh->SetVisibility(false);
}

ABendingPropActor::FOnPropHit ABendingPropActor::OnPropHit;

// ---------------------------------------------------------------------------------------------------- Setup

void ABendingPropActor::InitArenaProp(BendingSim::EArenaProp InKind, const FVector& InGroundCm, double YawDeg, int32 Variant)
{
	using BendingSim::EArenaProp;

	Kind = InKind;
	bArenaProp = true;
	GroundCm = InGroundCm;
	FacingYawDeg = YawDeg;

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

	case EArenaProp::Banner:
	{
		// A pole with a cross-bar; the cloth hangs facing the ring (its face is the actor's Y axis).
		SetActorRotation(FRotator(0.0, YawDeg - 90.0, 0.0));
		SetupBody(TEXT("Cylinder"), GroundCm + FVector(0.0, 0.0, 0.5 * BannerPoleHeightCm), FVector(14.0, 14.0, BannerPoleHeightCm), GetPropColor(Kind));
		Body->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
		const FVector Out = FRotator(0.0, YawDeg, 0.0).Vector() * 6.0;
		const double ClothHeight = BendingSim::BannerClothTopCm - BendingSim::BannerClothBottomCm;
		SetupDetail(Detail, TEXT("Cube"), GroundCm + Out + FVector(0.0, 0.0, BendingSim::BannerClothBottomCm + 0.5 * ClothHeight),
			FVector(BendingSim::BannerClothWidthCm, 3.0, ClothHeight), GetBannerColor(Variant));
		Detail->SetWorldRotation(GetActorRotation());
		SetupDetail(Accent, TEXT("Cylinder"), GroundCm + FVector(0.0, 0.0, BendingSim::BannerClothTopCm + 5.0), FVector(7.0, 7.0, 115.0), GetPropColor(Kind));
		Accent->SetWorldRotation(GetActorRotation() + FRotator(0.0, 0.0, 90.0));
		ConfigureVolume(SimVolume, Center, true);
		break;
	}

	case EArenaProp::Lantern:
		// Stone post, a lamp box (it glows when lit), and a pointed roof.
		SetupBody(TEXT("Cylinder"), GroundCm + FVector(0.0, 0.0, 45.0), FVector(24.0, 24.0, 90.0), GetPropColor(Kind));
		Body->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
		SetupDetail(Detail, TEXT("Cube"), GroundCm + FVector(0.0, 0.0, 105.0), FVector(28.0, 28.0, 28.0), UBendingSandboxLibrary::FromSRGB(80, 64, 48));
		SetupDetail(Accent, TEXT("Cone"), GroundCm + FVector(0.0, 0.0, 135.0), FVector(46.0, 46.0, 30.0), GetPropColor(Kind));
		Light->SetLightColor(FLinearColor(1.f, 0.62f, 0.28f));
		Light->SetAttenuationRadius(700.f);
		ConfigureVolume(SimVolume, Center, true);
		break;

	case EArenaProp::StrawBale:
		SetActorRotation(FRotator(0.0, YawDeg, 0.0));
		SetupBody(TEXT("Cube"), GroundCm + FVector(0.0, 0.0, 28.0), FVector(100.0, 58.0, 55.0), GetPropColor(Kind));
		Body->SetWorldRotation(GetActorRotation());
		Body->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
		ConfigureVolume(SimVolume, Center, true);
		break;

	case EArenaProp::Crate:
		SetActorRotation(FRotator(0.0, YawDeg, 0.0));
		SetupBody(TEXT("Cube"), GroundCm + FVector(0.0, 0.0, 25.0 + SpawnClearanceCm), FVector(50.0), GetPropColor(Kind));
		EnablePhysics(Spec.MassKg);
		ConfigureVolume(SimVolume, GroundCm + FVector(0.0, 0.0, 25.0 + SpawnClearanceCm), false);
		break;

	case EArenaProp::WaterBarrel:
		SetupBody(TEXT("Cylinder"), GroundCm + FVector(0.0, 0.0, 0.5 * BarrelHeightCm + SpawnClearanceCm), FVector(60.0, 60.0, BarrelHeightCm), GetPropColor(Kind));
		SetupDetail(Detail, TEXT("Cylinder"), GroundCm + FVector(0.0, 0.0, BarrelHeightCm - 2.0), FVector(50.0, 50.0, 2.0), UBendingSandboxLibrary::FromSRGB(45, 115, 225));
		EnablePhysics(Spec.MassKg);
		WaterKg = Spec.WaterKg;
		ConfigureVolume(SimVolume, GroundCm + FVector(0.0, 0.0, 0.5 * BarrelHeightCm + SpawnClearanceCm), false);
		break;

	default:
		break;
	}

	HomeLocation = GetActorLocation();
	HomeRotation = GetActorRotation();
	Health = DummyMaxHealth;
	if (Spec.Combustion.IsFlammable())
	{
		FlameMesh->SetStaticMesh(UBendingSandboxLibrary::LoadBasicShape(TEXT("Sphere")));
		UBendingSandboxLibrary::SetMeshColor(FlameMesh, UBendingSandboxLibrary::FromSRGB(255, 150, 40));
		Light->SetLightColor(FLinearColor(1.f, 0.55f, 0.2f));
		Light->SetAttenuationRadius(Kind == EArenaProp::Lantern ? 700.f : 1000.f);
	}
	if (Body->IsSimulatingPhysics() && (IsDummy() || Spec.BreakSpeedMs > 0.0))
	{
		// Blows from other bodies (thrown rocks, a crate thrown into a dummy) hurt and smash.
		Body->SetNotifyRigidBodyCollision(true);
		Body->OnComponentHit.AddUniqueDynamic(this, &ABendingPropActor::HandleBodyHit);
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
	Volume->OnImpulseReceived.AddUniqueDynamic(this, &ABendingPropActor::HandleImpulse);
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
		ReactToBlow(DeltaVCmS);
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

	if (bSmashPending)
	{
		Smash();
		return;
	}
	if (bArenaProp && BendingSim::GetArenaPropSpec(Kind).Combustion.IsFlammable())
	{
		TickCombustion(DeltaSeconds);
	}
	if (IsDummy())
	{
		TickDummy(DeltaSeconds);
	}

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

// ---------------------------------------------------------------------------------------------------- Burning

void ABendingPropActor::GetCombustionPoints(FVector& OutCenterCm, FVector& OutFlameCm) const
{
	const BendingSim::FArenaPropSpec& Spec = BendingSim::GetArenaPropSpec(Kind);
	if (Kind == BendingSim::EArenaProp::Banner)
	{
		// The cloth burns from its bottom edge up.
		const FVector Out = FRotator(0.0, FacingYawDeg, 0.0).Vector() * 6.0;
		OutCenterCm = GroundCm + Out + FVector(0.0, 0.0, 0.5 * (BendingSim::BannerClothTopCm + BendingSim::BannerClothBottomCm));
		const double Burnt = FMath::Min(Burn.GetBurntFraction(Spec.Combustion), 0.92);
		OutFlameCm = GroundCm + Out + FVector(0.0, 0.0, BendingSim::BannerClothBottomCm + (BendingSim::BannerClothTopCm - BendingSim::BannerClothBottomCm) * Burnt + 25.0);
		return;
	}
	if (!Body->IsSimulatingPhysics())
	{
		OutFlameCm = GroundCm + FVector(0.0, 0.0, Spec.FlameHeightCm);
		OutCenterCm = Kind == BendingSim::EArenaProp::Lantern ? OutFlameCm : Volume->GetComponentLocation();
		return;
	}
	OutCenterCm = Volume->GetComponentLocation();
	OutFlameCm = OutCenterCm + FVector(0.0, 0.0, Spec.FlameHeightCm - Spec.CenterHeightCm);
}

void ABendingPropActor::TickCombustion(float DeltaSeconds)
{
	UBendingInteractionSubsystem* Interaction = GetWorld()->GetSubsystem<UBendingInteractionSubsystem>();
	BendingSim::FSimWorld* SimWorld = Interaction ? Interaction->GetSimWorld() : nullptr;
	if (!SimWorld)
	{
		return;
	}
	const BendingSim::FArenaPropSpec& Spec = BendingSim::GetArenaPropSpec(Kind);
	FVector Center;
	FVector FlameSpot;
	GetCombustionPoints(Center, FlameSpot);
	const double HeatBefore = Burn.HeatJ;
	const BendingSim::ECombustionEvent Event = Burn.Update(*SimWorld, Spec.Combustion, BendingUnits::ToSim(Center), BendingUnits::ToSim(FlameSpot), DeltaSeconds);

	if (IsDummy())
	{
		// Fire hurts a dummy as it heats it, and keeps hurting while it burns.
		double Soaked = Event == BendingSim::ECombustionEvent::Ignited ? FMath::Max(Spec.Combustion.IgnitionJ - HeatBefore, 0.0) : FMath::Max(Burn.HeatJ - HeatBefore, 0.0);
		if (Burn.bBurning && Event != BendingSim::ECombustionEvent::Ignited)
		{
			Soaked += BendingSim::FCombustible::MeasureHeatingW(*SimWorld, Spec.Combustion, BendingUnits::ToSim(Center), Burn.Flame) * DeltaSeconds;
		}
		TakeHit(Soaked / 2500.0 + (Burn.bBurning ? 7.0 * DeltaSeconds : 0.0), FVector::ZeroVector);
	}

	const FString Name = FString(UTF8_TO_TCHAR(Spec.Name));
	const bool bLantern = Kind == BendingSim::EArenaProp::Lantern;
	switch (Event)
	{
	case BendingSim::ECombustionEvent::Ignited:
		Announce(bLantern ? FString(TEXT("Lantern lit")) : Name + TEXT(" caught fire!"));
		break;
	case BendingSim::ECombustionEvent::Extinguished:
		Announce(bLantern ? FString(TEXT("Lantern put out")) : TEXT("Water put out the ") + Name);
		break;
	case BendingSim::ECombustionEvent::BurntOut:
		Announce(Name + TEXT(" burnt away"));
		if (Kind == BendingSim::EArenaProp::Crate)
		{
			// A burnt crate falls apart.
			RequestSmash();
		}
		else if (IsDummy())
		{
			// Burnt down: it lies there charred for a while, then comes back whole.
			if (KnockoutS > 0.0)
			{
				KnockoutS = KnockoutSeconds;
			}
			else
			{
				TakeHit(Health + 1.0, FVector::ZeroVector);
			}
		}
		break;
	default:
		break;
	}

	ShowBurning(Burn.GetBurntFraction(Spec.Combustion), Burn.bBurning, Burn.bBurntOut);
	if (Burn.bBurning)
	{
		const BendingSim::FVolume* FlameVolume = SimWorld->GetVolume(Burn.Flame);
		const double FlameRadius = FlameVolume ? FlameVolume->RadiusCm : 40.0;
		FlameMesh->SetWorldLocation(FlameSpot);
		FlameMesh->SetWorldScale3D(FVector(bLantern ? 0.12 : 1.2 * FlameRadius / 100.0));
		Light->SetWorldLocation(FlameSpot);
		Light->SetIntensity((bLantern ? 0.6f : 1.f) * BrazierLightCandelas * (0.85f + 0.15f * FMath::Sin(static_cast<float>(GetWorld()->GetTimeSeconds()) * 21.f)));
	}
}

void ABendingPropActor::ShowBurning(double BurntFraction, bool bBurning, bool bBurntOut)
{
	const bool bLantern = Kind == BendingSim::EArenaProp::Lantern;
	FlameMesh->SetVisibility(bBurning && !bLantern);
	Light->SetVisibility(bBurning);
	if (bLantern)
	{
		// The lamp box glows while its wick burns.
		if (FMath::Abs((bBurning ? 1.0 : 0.0) - LastShownBurnt) > 0.5)
		{
			LastShownBurnt = bBurning ? 1.0 : 0.0;
			UBendingSandboxLibrary::SetMeshColor(Detail, bBurning ? UBendingSandboxLibrary::FromSRGB(255, 214, 120) : UBendingSandboxLibrary::FromSRGB(80, 64, 48));
		}
		return;
	}
	if (FMath::Abs(BurntFraction - LastShownBurnt) < 0.02 && !(bBurntOut && LastShownBurnt < 1.0))
	{
		return;
	}
	LastShownBurnt = bBurntOut ? 1.0 : BurntFraction;
	const float Char = static_cast<float>(FMath::Clamp(BurntFraction * 1.1, 0.0, 1.0));
	switch (Kind)
	{
	case BendingSim::EArenaProp::Banner:
	{
		// What is left of the cloth hangs from the bar; it darkens as it chars.
		const double ClothHeight = BendingSim::BannerClothTopCm - BendingSim::BannerClothBottomCm;
		const double Left = bBurntOut ? 0.06 : FMath::Max(1.0 - BurntFraction, 0.06);
		const FVector Out = FRotator(0.0, FacingYawDeg, 0.0).Vector() * 6.0;
		Detail->SetWorldLocation(GroundCm + Out + FVector(0.0, 0.0, BendingSim::BannerClothTopCm - 0.5 * ClothHeight * Left));
		Detail->SetWorldScale3D(FVector(BendingSim::BannerClothWidthCm, 3.0, ClothHeight * Left) / 100.0);
		UBendingSandboxLibrary::SetMeshColor(Body, UBendingSandboxLibrary::LerpColor(BaseColor, CharColor, 0.5f * Char));
		break;
	}
	case BendingSim::EArenaProp::StrawBale:
		// It slumps as it burns, down to a flat heap of ash.
		Body->SetWorldScale3D(FVector(1.0, 0.58, 0.55 * FMath::Max(1.0 - 0.8 * BurntFraction, 0.2)));
		Body->SetWorldLocation(GroundCm + FVector(0.0, 0.0, 27.5 * FMath::Max(1.0 - 0.8 * BurntFraction, 0.2)));
		UBendingSandboxLibrary::SetMeshColor(Body, UBendingSandboxLibrary::LerpColor(BaseColor, CharColor, Char));
		break;
	default:
		UBendingSandboxLibrary::SetMeshColor(Body, UBendingSandboxLibrary::LerpColor(BaseColor, CharColor, Char));
		if (IsDummy())
		{
			UBendingSandboxLibrary::SetMeshColor(Detail, UBendingSandboxLibrary::LerpColor(UBendingSandboxLibrary::FromSRGB(180, 138, 92), CharColor, Char));
		}
		break;
	}
}

// ---------------------------------------------------------------------------------------------------- Blows, smashing

void ABendingPropActor::HandleImpulse(FVector ImpulseKgCmS)
{
	ReactToBlow(ImpulseKgCmS / FMath::Max(GetMassKg(), 0.01));
}

void ABendingPropActor::HandleBodyHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit)
{
	ReactToBlow(NormalImpulse / FMath::Max(GetMassKg(), 0.01));
}

void ABendingPropActor::ReactToBlow(const FVector& DeltaVCmS)
{
	if (!bArenaProp)
	{
		return;
	}
	const double SpeedCmS = DeltaVCmS.Size();
	const double BreakSpeedMs = BendingSim::GetArenaPropSpec(Kind).BreakSpeedMs;
	if (BreakSpeedMs > 0.0 && SpeedCmS > BendingUnits::MToCm(BreakSpeedMs))
	{
		RequestSmash();
	}
	if (IsDummy() && SpeedCmS > MinBlowCmS)
	{
		TakeHit(DamagePerMs * BendingUnits::CmToM(SpeedCmS), DeltaVCmS);
	}
}

void ABendingPropActor::Smash()
{
	bSmashPending = false;
	UWorld* World = GetWorld();
	const FVector Spot = GetActorLocation();
	const BendingSim::FArenaPropSpec& Spec = BendingSim::GetArenaPropSpec(Kind);

	// Boards (or staves) fly.
	const bool bCharred = Burn.bBurning || Burn.bBurntOut;
	const FVector PieceScale = Kind == BendingSim::EArenaProp::WaterBarrel ? FVector(0.1, 0.03, 0.85) : FVector(0.5, 0.12, 0.03);
	for (int32 Piece = 0; Piece < DebrisPieces; ++Piece)
	{
		const FTransform At(FRotator(FMath::FRandRange(0.f, 360.f), FMath::FRandRange(0.f, 360.f), 0.f), Spot + FVector(0.0, 0.0, 20.0), PieceScale);
		AStaticMeshActor* Board = World->SpawnActorDeferred<AStaticMeshActor>(AStaticMeshActor::StaticClass(), At);
		if (!Board)
		{
			continue;
		}
		UStaticMeshComponent* Mesh = Board->GetStaticMeshComponent();
		Mesh->SetMobility(EComponentMobility::Movable);
		Mesh->SetStaticMesh(UBendingSandboxLibrary::LoadBasicShape(TEXT("Cube")));
		Mesh->SetCollisionProfileName(UCollisionProfile::PhysicsActor_ProfileName);
		Mesh->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
		Board->FinishSpawning(At);
		UBendingSandboxLibrary::SetMeshColor(Mesh, bCharred ? CharColor : BaseColor);
		Mesh->SetSimulatePhysics(true);
		Mesh->SetMassOverrideInKg(NAME_None, 1.5f, true);
		const float Angle = FMath::FRandRange(0.f, 2.f * UE_PI);
		Mesh->SetPhysicsLinearVelocity(FVector(FMath::Cos(Angle) * 300.f, FMath::Sin(Angle) * 300.f, FMath::FRandRange(250.f, 550.f)));
		Mesh->SetPhysicsAngularVelocityInDegrees(FVector(FMath::FRandRange(-720.f, 720.f), FMath::FRandRange(-720.f, 720.f), FMath::FRandRange(-720.f, 720.f)));
		Board->SetLifeSpan(4.f);
	}

	// A barrel's water flies out in a ring and soaks everything round it.
	if (WaterKg > 0.5)
	{
		for (int32 Parcel = 0; Parcel < SpillParcels; ++Parcel)
		{
			const double Angle = 2.0 * UE_DOUBLE_PI * Parcel / SpillParcels + 0.4;
			const FVector Out(FMath::Cos(Angle), FMath::Sin(Angle), 0.0);
			ABendingProjectile::SpawnProjectile(World, EBendingProjectileKind::Water,
				BendingSim::MakeWaterBall(WaterKg / SpillParcels, UBendingSettings::Get().AmbientTemperatureK, BendingUnits::ToSim(Spot + Out * 30.0 + FVector(0.0, 0.0, 30.0)),
					BendingUnits::ToSim(Out * 320.0 + FVector(0.0, 0.0, 260.0))), this, false);
		}
		Announce(FString::Printf(TEXT("The barrel burst: %.0f kg of water"), WaterKg));
		WaterKg = 0.0;
	}
	else
	{
		Announce(FString(UTF8_TO_TCHAR(Spec.Name)) + TEXT(" smashed"));
	}

	// A burning crate scatters its fire on the ground.
	if (Burn.bBurning)
	{
		const BendingSim::FTechniqueTuning& Tuning = BendingSim::GetDefaultTechniqueTuning();
		const ABendingSandboxArena* Arena = ABendingSandboxArena::Find(World);
		const double GroundZ = Arena ? Arena->GetGroundHeightAt(Spot) : Spot.Z - 25.0;
		BendingSim::FVolume Fire = BendingSim::MakeFlame(Spec.Combustion.FlameMassKg, Spec.Combustion.FlameTemperatureK, BendingUnits::ToSim(FVector(Spot.X, Spot.Y, GroundZ)), BendingSim::FVec3());
		if (ABendingProjectile* Dropped = ABendingProjectile::SpawnProjectile(World, EBendingProjectileKind::Fire, Fire, this, true))
		{
			Dropped->SetGrounded(true);
			Dropped->SetFuel(Tuning.GroundFlameFuelJ, Tuning.GroundFlameFuelPowerW);
			Dropped->KeepAlive(Tuning.ProjectileLifetimeS + Tuning.GroundFlameFuelJ / FMath::Max(Tuning.GroundFlameFuelPowerW, 1.0));
		}
	}
	if (UBendingInteractionSubsystem* Interaction = World->GetSubsystem<UBendingInteractionSubsystem>())
	{
		if (BendingSim::FSimWorld* SimWorld = Interaction->GetSimWorld())
		{
			Burn.RemoveFlame(*SimWorld);
		}
	}
	Destroy();
}

double ABendingPropActor::TakeWater(double MassKg)
{
	const double Taken = FMath::Clamp(MassKg, 0.0, WaterKg);
	WaterKg -= Taken;
	if (WaterKg <= 0.5)
	{
		Detail->SetVisibility(false);
	}
	else
	{
		Detail->SetWorldLocation(GroundCm + FVector(0.0, 0.0, 10.0 + (BarrelHeightCm - 12.0) * WaterKg / FMath::Max(BendingSim::GetArenaPropSpec(Kind).WaterKg, 1.0)));
	}
	return Taken;
}

// ---------------------------------------------------------------------------------------------------- Training dummies

void ABendingPropActor::TakeHit(double Damage, const FVector& FromDirection)
{
	if (!IsDummy() || Damage <= 0.0 || KnockoutS > 0.0 || ProtectS > 0.0)
	{
		return;
	}
	Health -= Damage;
	PendingDamage += Damage;
	QuietS = 0.0;
	if (Health > 0.0)
	{
		return;
	}
	Health = 0.0;
	KnockoutS = KnockoutSeconds;
	OnPropHit.Broadcast(this, GetActorLocation() + FVector(0.0, 0.0, 80.0), PendingDamage, false);
	OnPropHit.Broadcast(this, GetActorLocation() + FVector(0.0, 0.0, 110.0), 0.0, true);
	PendingDamage = 0.0;
	PendingAgeS = 0.0;
	// It topples over the way it was hit.
	FVector Fall(FromDirection.X, FromDirection.Y, 0.0);
	Fall = Fall.IsNearlyZero() ? GetActorForwardVector() : Fall.GetSafeNormal();
	if (Body->IsSimulatingPhysics())
	{
		Body->SetPhysicsAngularVelocityInRadians(FVector::CrossProduct(FVector::UpVector, Fall) * 4.0);
	}
	Announce(TEXT("Dummy knocked out!"));
}

void ABendingPropActor::TickDummy(float DeltaSeconds)
{
	ProtectS = FMath::Max(ProtectS - DeltaSeconds, 0.0);
	// A blow that lands over several frames is one hit, reported once it has gone quiet (or every half second).
	if (PendingDamage > 0.0)
	{
		QuietS += DeltaSeconds;
		PendingAgeS += DeltaSeconds;
		if (QuietS >= 0.12 || PendingAgeS >= 0.5)
		{
			if (PendingDamage >= 1.0)
			{
				OnPropHit.Broadcast(this, GetActorLocation() + FVector(0.0, 0.0, 80.0), PendingDamage, false);
			}
			PendingDamage = 0.0;
			PendingAgeS = 0.0;
		}
	}
	if (KnockoutS <= 0.0)
	{
		return;
	}
	// Still burning, it stays down until the fire is out.
	KnockoutS = Burn.bBurning ? FMath::Max(KnockoutS - DeltaSeconds, 1.0) : KnockoutS - DeltaSeconds;
	if (KnockoutS <= 0.0)
	{
		Respawn();
	}
}

void ABendingPropActor::Respawn()
{
	// Back on its feet where it first stood, whole again.
	KnockoutS = 0.0;
	Health = DummyMaxHealth;
	PendingDamage = 0.0;
	ProtectS = 1.5;
	if (UBendingInteractionSubsystem* Interaction = GetWorld()->GetSubsystem<UBendingInteractionSubsystem>())
	{
		if (BendingSim::FSimWorld* SimWorld = Interaction->GetSimWorld())
		{
			Burn.RemoveFlame(*SimWorld);
		}
	}
	Burn = BendingSim::FCombustible();
	LastShownBurnt = -1.0;
	ShowBurning(0.0, false, false);
	SetActorLocationAndRotation(HomeLocation, HomeRotation, false, nullptr, ETeleportType::TeleportPhysics);
	if (Body->IsSimulatingPhysics())
	{
		Body->SetPhysicsLinearVelocity(FVector::ZeroVector);
		Body->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
	}
}

void ABendingPropActor::Announce(const FString& Text) const
{
	const APawn* Pawn = UGameplayStatics::GetPlayerPawn(GetWorld(), 0);
	if (UBendingTechniqueComponent* Techniques = Pawn ? Pawn->FindComponentByClass<UBendingTechniqueComponent>() : nullptr)
	{
		Techniques->AddMessage(Text);
	}
}

