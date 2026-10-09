#include "Sandbox/BendingTornadoActor.h"

#include "BendingSettings.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Physics/BendingUnits.h"
#include "Sandbox/BendingProjectile.h"
#include "Sandbox/BendingPropActor.h"
#include "Sandbox/BendingSandboxLibrary.h"
#include "Sim/BendingTechniques.h"

namespace
{
	/** Funnel radius at its foot, as a share of the tuned radius (it widens to the full radius at the top). */
	constexpr double FootRadiusFraction = 0.16;
	/** Bodies higher than this above its foot are out of its reach. */
	constexpr double MaxBodyHeightCm = 900.0;
	constexpr double MaxLiftHeightCm = 600.0;
	constexpr double MaxProjectileHeightCm = 1000.0;

	const BendingSim::FTechniqueTuning& GetTuning()
	{
		return BendingSim::GetDefaultTechniqueTuning();
	}

	double SmoothStep01(double Edge0, double Edge1, double X)
	{
		const double T = FMath::Clamp((X - Edge0) / FMath::Max(Edge1 - Edge0, 1e-6), 0.0, 1.0);
		return T * T * (3.0 - 2.0 * T);
	}
}

ABendingTornadoActor::ABendingTornadoActor()
{
	PrimaryActorTick.bCanEverTick = true;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;

	Funnel = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("Funnel"));
	Debris = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("Debris"));
	for (UInstancedStaticMeshComponent* Instances : { Funnel.Get(), Debris.Get() })
	{
		Instances->SetupAttachment(Root);
		Instances->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
		Instances->SetGenerateOverlapEvents(false);
		Instances->SetCanEverAffectNavigation(false);
		Instances->SetMobility(EComponentMobility::Movable);
	}
	Funnel->SetCastShadow(false);
}

void ABendingTornadoActor::InitTornado(const FVector& GroundCm, double LifetimeS)
{
	SetActorLocation(GroundCm);
	LifetimeSeconds = FMath::Max(LifetimeS, 0.5);
	AgeSeconds = 0.0;

	Funnel->SetStaticMesh(UBendingSandboxLibrary::LoadBasicShape(TEXT("Sphere")));
	Debris->SetStaticMesh(UBendingSandboxLibrary::LoadBasicShape(TEXT("Cube")));
	FunnelMaterial = UBendingSandboxLibrary::SetMeshColor(Funnel, UBendingSandboxLibrary::FromSRGB(225, 236, 248));
	DebrisMaterial = UBendingSandboxLibrary::SetMeshColor(Debris, UBendingSandboxLibrary::FromSRGB(122, 92, 62));
	bInitialized = true;
	UpdateVisuals(0.0);
}

double ABendingTornadoActor::GetStrength() const
{
	return SmoothStep01(0.0, 0.4, AgeSeconds) * (1.0 - SmoothStep01(LifetimeSeconds - 0.6, LifetimeSeconds, AgeSeconds));
}

void ABendingTornadoActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bInitialized)
	{
		return;
	}
	AgeSeconds += DeltaSeconds;
	if (AgeSeconds >= LifetimeSeconds)
	{
		Destroy();
		return;
	}
	const double Strength = GetStrength();
	PullBodies(Strength, DeltaSeconds);
	CatchProjectiles(Strength, DeltaSeconds);
	ShedAir(Strength, DeltaSeconds);
	UpdateVisuals(Strength);
}

void ABendingTornadoActor::PullBodies(double Strength, float DeltaSeconds)
{
	// What it catches swirls round, is drawn in, and (if light enough) is lifted. Equal acceleration for every
	// body, except that the lift fades for anything heavier than 150 kg: boulders only shuffle.
	const BendingSim::FTechniqueTuning& Tuning = GetTuning();
	const FVector Center = GetActorLocation();
	const double Reach = Tuning.TornadoRadiusCm * 1.8;
	const double Swirl = BendingUnits::MToCm(Tuning.TornadoSwirlMs);
	for (TActorIterator<ABendingPropActor> It(GetWorld()); It; ++It)
	{
		ABendingPropActor* Prop = *It;
		if (!Prop || !Prop->IsLoose())
		{
			continue;
		}
		const FVector Location = Prop->GetActorLocation();
		const FVector Offset(Location.X - Center.X, Location.Y - Center.Y, 0.0);
		const double Distance = Offset.Size();
		const double Height = Location.Z - Center.Z;
		if (Distance >= Reach || Height > MaxBodyHeightCm)
		{
			continue;
		}
		const double Falloff = (1.0 - Distance / Reach) * Strength;
		const FVector Inward = Distance > 1.0 ? -Offset / Distance : FVector::ZeroVector;
		const FVector Around(-Inward.Y, Inward.X, 0.0);
		const double AroundSpeed = FVector::DotProduct(Prop->GetVelocity(), Around);
		FVector DeltaV = Around * ((Swirl * Falloff - AroundSpeed) * FMath::Min(1.0, 4.0 * DeltaSeconds));
		DeltaV += Inward * (BendingUnits::MToCm(Tuning.TornadoPullMs2) * Falloff * DeltaSeconds);
		if (Height < MaxLiftHeightCm)
		{
			const double LiftScale = FMath::Min(1.0, 150.0 / FMath::Max(Prop->GetMassKg(), 1.0));
			// Gravity is the engine's job here (the sandbox's bodies feel it too): this is the lift on top of it.
			DeltaV.Z += BendingUnits::MToCm(Tuning.TornadoLiftMs2) * Falloff * LiftScale * DeltaSeconds;
		}
		Prop->AddVelocity(DeltaV);
	}
}

void ABendingTornadoActor::CatchProjectiles(double Strength, float DeltaSeconds)
{
	// Flames, water and air nearby are drawn into the spiral; flame caught in it makes a fire tornado.
	const BendingSim::FTechniqueTuning& Tuning = GetTuning();
	const FVector Center = GetActorLocation();
	const double Reach = Tuning.TornadoRadiusCm * 1.8 * 1.2;
	const double Swirl = BendingUnits::MToCm(Tuning.TornadoSwirlMs);
	FireKg = 0.0;
	for (TActorIterator<ABendingProjectile> It(GetWorld()); It; ++It)
	{
		ABendingProjectile* Projectile = *It;
		if (!Projectile || Projectile->IsGrounded() || Projectile->GetOwner() == this)
		{
			continue;
		}
		const FVector Location = Projectile->GetActorLocation();
		const FVector Offset(Location.X - Center.X, Location.Y - Center.Y, 0.0);
		const double Distance = Offset.Size();
		const double Height = Location.Z - Center.Z;
		if (Distance >= Reach || Height > MaxProjectileHeightCm)
		{
			continue;
		}
		if (Projectile->GetKind() == EBendingProjectileKind::Fire)
		{
			FireKg += Projectile->GetMassKg();
			// It burns on in the spiral instead of flying out of its lifetime.
			Projectile->KeepAlive(0.5 * Tuning.ProjectileLifetimeS);
		}
		const FVector Inward = Distance > 1.0 ? -Offset / Distance : FVector::ZeroVector;
		const FVector Around(-Inward.Y, Inward.X, 0.0);
		const double Ring = Tuning.TornadoRadiusCm * (0.5 + 0.5 * FMath::Clamp(Height / 800.0, 0.0, 1.0));
		const FVector Wanted = Around * (Swirl * Strength) + Inward * ((Distance - Ring) * 3.0) + FVector(0.0, 0.0, 180.0 * Strength);
		const FVector Velocity = Projectile->GetVelocityCmS();
		Projectile->SetVelocityCmS(Velocity + (Wanted - Velocity) * FMath::Min(1.0, 3.0 * DeltaSeconds));
	}
}

void ABendingTornadoActor::ShedAir(double Strength, float DeltaSeconds)
{
	const BendingSim::FTechniqueTuning& Tuning = GetTuning();
	AirAccumulator = FMath::Min(AirAccumulator + DeltaSeconds * AirPuffsPerSecond, 2.0);
	while (AirAccumulator >= 1.0)
	{
		AirAccumulator -= 1.0;
		EmitAngleRad += 2.1;
		const FVector Out(FMath::Cos(EmitAngleRad), FMath::Sin(EmitAngleRad), 0.0);
		const FVector Around(-Out.Y, Out.X, 0.0);
		const FVector Location = GetActorLocation() + Out * Tuning.TornadoRadiusCm + FVector(0.0, 0.0, 80.0);
		const FVector Velocity = Around * (BendingUnits::MToCm(Tuning.TornadoSwirlMs) * Strength) + FVector(0.0, 0.0, 150.0);
		const BendingSim::FVolume Air = BendingSim::MakeBentAir(45.0, 1.4, UBendingSettings::Get().AmbientAirDensityKgM3,
			BendingUnits::ToSim(Location), BendingUnits::ToSim(Velocity));
		ABendingProjectile::SpawnProjectile(GetWorld(), EBendingProjectileKind::Air, Air, this, false);
	}
}

void ABendingTornadoActor::UpdateVisuals(double Strength)
{
	const BendingSim::FTechniqueTuning& Tuning = GetTuning();
	const FVector Center = GetActorLocation();
	const double Time = AgeSeconds;
	const double Grow = 0.4 + 0.6 * Strength;
	const double TopRadius = Tuning.TornadoRadiusCm * Grow;
	const double FootRadius = Tuning.TornadoRadiusCm * FootRadiusFraction * Grow;
	const double Height = FunnelHeightCm * (0.3 + 0.7 * Strength);

	// White wind turning to flame as fire is caught in it.
	const double Fire = FMath::Clamp(FireKg / 0.3, 0.0, 1.0);
	if (FMath::Abs(Fire - ShownFire) > 0.05)
	{
		ShownFire = Fire;
		const float Alpha = static_cast<float>(Fire);
		FunnelMaterial = UBendingSandboxLibrary::SetMeshColor(Funnel, UBendingSandboxLibrary::LerpColor(
			UBendingSandboxLibrary::FromSRGB(225, 236, 248), UBendingSandboxLibrary::FromSRGB(255, 150, 40), Alpha));
		DebrisMaterial = UBendingSandboxLibrary::SetMeshColor(Debris, UBendingSandboxLibrary::LerpColor(
			UBendingSandboxLibrary::FromSRGB(122, 92, 62), UBendingSandboxLibrary::FromSRGB(255, 190, 70), Alpha));
	}

	// Strands of stretched puffs spiral up the funnel, each lying along its direction of travel; the narrow foot
	// spins fastest.
	Scratch.Reset();
	const int32 Strands = FMath::Max(NumStrands, 1);
	const int32 Puffs = FMath::Max(PuffsPerStrand, 2);
	for (int32 Strand = 0; Strand < Strands; ++Strand)
	{
		for (int32 Index = 0; Index < Puffs; ++Index)
		{
			const double U = static_cast<double>(Index) / (Puffs - 1);
			const double Z = U * Height;
			const double Radius = FMath::Lerp(FootRadius, TopRadius, U * U * 0.6 + U * 0.4);
			const double Angle = Strand * 2.0 * UE_DOUBLE_PI / Strands + U * 5.0 - Time * (9.0 - 4.0 * U);
			const FVector Out(FMath::Cos(Angle), FMath::Sin(Angle), 0.0);
			const FVector Tangent = FVector(-Out.Y, Out.X, 0.25).GetSafeNormal();
			const double Length = FMath::Max(0.55 * Radius, 30.0);
			const double Thickness = 10.0 + 0.04 * Radius;
			Scratch.Emplace(FRotationMatrix::MakeFromX(Tangent).ToQuat(), Center + Out * Radius + FVector(0.0, 0.0, Z),
				FVector(Length, Thickness, Thickness) / 100.0);
		}
	}
	UBendingSandboxLibrary::SetInstanceTransforms(Funnel, Scratch);

	// Debris climbing a spiral round the funnel.
	Scratch.Reset();
	for (int32 Index = 0; Index < NumDebris; ++Index)
	{
		const double U = FMath::Fmod(Index * 0.6180339 + Time * (0.18 + (Index % 5) * 0.03), 1.0);
		const double Z = U * Height * 0.95;
		const double Radius = FMath::Lerp(FootRadius, TopRadius, U) * (1.08 + 0.15 * FMath::Sin(Index * 7.1));
		const double Angle = Index * 2.399 + Time * (7.5 - 4.0 * U);
		const double Size = (5.0 + (Index % 4) * 2.5) * Grow;
		const FRotator Tumble(Time * 170.0 + Index * 40.0, Time * 130.0 + Index * 25.0, 0.0);
		Scratch.Emplace(Tumble.Quaternion(), Center + FVector(FMath::Cos(Angle) * Radius, FMath::Sin(Angle) * Radius, Z), FVector(Size / 100.0));
	}
	UBendingSandboxLibrary::SetInstanceTransforms(Debris, Scratch);
}
