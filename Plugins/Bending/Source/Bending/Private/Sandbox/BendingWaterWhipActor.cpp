#include "Sandbox/BendingWaterWhipActor.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/World.h"
#include "Interaction/BendingInteractionSubsystem.h"
#include "Physics/BendingUnits.h"
#include "Sandbox/BendingProjectile.h"
#include "Sandbox/BendingSandboxArena.h"
#include "Sandbox/BendingSandboxLibrary.h"
#include "Sandbox/BendingTechniqueComponent.h"
#include "Sim/BendingTechniques.h"

namespace
{
	/** Spheres drawn per simulated segment along the smooth curve. */
	constexpr int32 SamplesPerSegment = 3;
	/** Below this speed (cm/s) water holds together; above it, it sheds droplets. */
	constexpr double ShedSpeedCmS = 500.0;

	FVector CatmullRom(const FVector& P0, const FVector& P1, const FVector& P2, const FVector& P3, double T)
	{
		const double T2 = T * T;
		const double T3 = T2 * T;
		return 0.5 * ((2.0 * P1) + (P2 - P0) * T + (2.0 * P0 - 5.0 * P1 + 4.0 * P2 - P3) * T2 + (3.0 * P1 - P0 - 3.0 * P2 + P3) * T3);
	}
}

ABendingWaterWhipActor::ABendingWaterWhipActor()
{
	PrimaryActorTick.bCanEverTick = true;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;

	WaterInstances = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("WaterInstances"));
	IceInstances = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("IceInstances"));
	DropletInstances = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("DropletInstances"));
	DropletInstances->SetCastShadow(false);
	for (UInstancedStaticMeshComponent* Instances : { WaterInstances.Get(), IceInstances.Get(), DropletInstances.Get() })
	{
		Instances->SetupAttachment(Root);
		Instances->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
		Instances->SetGenerateOverlapEvents(false);
		Instances->SetCanEverAffectNavigation(false);
	}
}

bool ABendingWaterWhipActor::InitWhip(UBendingTechniqueComponent* InBender, const FVector& SourceCm, double WaterKg, double TemperatureK)
{
	UWorld* World = GetWorld();
	Bender = InBender;
	Interaction = World ? World->GetSubsystem<UBendingInteractionSubsystem>() : nullptr;
	Arena = ABendingSandboxArena::Find(World);

	BendingSim::FSimWorld* SimWorld = GetSimWorld();
	if (!InBender || !SimWorld
		|| !Whip.Create(*SimWorld, BendingUnits::ToSim(SourceCm), BendingUnits::ToSim(InBender->GetHandLocation()), WaterKg, TemperatureK))
	{
		return false;
	}

	UStaticMesh* Sphere = UBendingSandboxLibrary::LoadBasicShape(TEXT("Sphere"));
	WaterInstances->SetStaticMesh(Sphere);
	IceInstances->SetStaticMesh(Sphere);
	DropletInstances->SetStaticMesh(Sphere);
	UBendingSandboxLibrary::SetMeshColor(WaterInstances, UBendingSandboxLibrary::FromSRGB(30, 110, 170));
	UBendingSandboxLibrary::SetMeshColor(IceInstances, UBendingSandboxLibrary::FromSRGB(215, 240, 255));
	UBendingSandboxLibrary::SetMeshColor(DropletInstances, UBendingSandboxLibrary::FromSRGB(120, 185, 225));
	Droplets.Reserve(MaxDroplets);

	// The whip follows the hand and aim: read them after the bender has updated them this frame.
	AddTickPrerequisiteComponent(InBender);
	UpdateVisuals();
	return true;
}

BendingSim::FSimWorld* ABendingWaterWhipActor::GetSimWorld() const
{
	UBendingInteractionSubsystem* Subsystem = Interaction.Get();
	return Subsystem ? Subsystem->GetSimWorld() : nullptr;
}

void ABendingWaterWhipActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	BendingSim::FSimWorld* SimWorld = GetSimWorld();
	if (!SimWorld || !Whip.IsActive())
	{
		Destroy();
		return;
	}

	// What reactions did to the water during last frame's world step.
	Whip.PostStep(*SimWorld);

	const UBendingTechniqueComponent* BenderComponent = Bender.Get();
	if (!BenderComponent || Whip.ShouldCollapse(*SimWorld))
	{
		Collapse();
		return;
	}

	if (const AActor* BenderActor = BenderComponent->GetOwner())
	{
		Whip.SetBodyCenter(BendingUnits::ToSim(BenderActor->GetActorLocation() + FVector(0.0, 0.0, ChestHeightCm)));
	}
	Whip.SetControl(BendingUnits::ToSim(BenderComponent->GetHandLocation()), BendingUnits::ToSim(BenderComponent->GetHandVelocity()),
		BendingUnits::ToSim(BenderComponent->GetAimPoint()));
	const ABendingSandboxArena* ArenaActor = Arena.Get();
	Whip.PreStep(*SimWorld, ArenaActor ? ArenaActor->GetTerrain() : nullptr, DeltaSeconds);
	FlingSpray();
	UpdateDroplets(DeltaSeconds);
	UpdateVisuals();
}

void ABendingWaterWhipActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (Whip.IsActive())
	{
		if (BendingSim::FSimWorld* SimWorld = GetSimWorld())
		{
			Whip.Destroy(*SimWorld);
		}
	}
	Super::EndPlay(EndPlayReason);
}

bool ABendingWaterWhipActor::Lash(double WindupS, double StrikeS)
{
	Whip.Settings.WindupS = FMath::Max(WindupS, 1.0 / 60.0);
	Whip.Settings.StrikeS = FMath::Max(StrikeS, 2.0 / 60.0);
	return Whip.Lash();
}

void ABendingWaterWhipActor::FlingSpray()
{
	BendingSim::FWhipDrop Spray[BendingSim::FWaterWhip::MaxPendingSpray];
	const int32 Count = Whip.ConsumeSpray(Spray, BendingSim::FWaterWhip::MaxPendingSpray);
	if (Count == 0)
	{
		return;
	}
	const BendingSim::FTechniqueTuning& Tuning = BendingSim::GetDefaultTechniqueTuning();
	const UBendingTechniqueComponent* BenderComponent = Bender.Get();
	AActor* InstigatorActor = BenderComponent ? BenderComponent->GetOwner() : nullptr;
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const BendingSim::FWhipDrop& Drop = Spray[Index];
		if (ABendingProjectile* Projectile = ABendingProjectile::SpawnProjectile(GetWorld(), EBendingProjectileKind::Water,
			BendingSim::MakeWaterSpray(Drop.MassKg, Drop.TemperatureK, Drop.LocationCm, Drop.VelocityCmS, Tuning.WhipSprayRadiusCm), InstigatorActor, false))
		{
			Projectile->MakeSpray(Tuning.WhipSprayDragTimeS);
		}
		// What the eye sees: a cone of droplets flying on from the tip.
		const FVector Location = BendingUnits::ToEngine(Drop.LocationCm);
		const FVector Velocity = BendingUnits::ToEngine(Drop.VelocityCmS);
		const double Spread = 0.16 * Velocity.Size();
		for (int32 Droplet = 0; Droplet < 70; ++Droplet)
		{
			SpawnDroplet(Location + FMath::VRand() * 10.0, Velocity * FMath::FRandRange(0.55, 1.05) + FMath::VRand() * Spread,
				FMath::FRandRange(0.5f, 1.8f), 2.f, 0.45f);
		}
	}
}

void ABendingWaterWhipActor::SpawnDroplet(const FVector& LocationCm, const FVector& VelocityCmS, float RadiusCm, float LifeS, float DragTimeS)
{
	if (MaxDroplets <= 0)
	{
		return;
	}
	if (Droplets.Num() >= MaxDroplets)
	{
		Droplets.RemoveAtSwap(0);
	}
	FDroplet& Droplet = Droplets.AddDefaulted_GetRef();
	Droplet.LocationCm = LocationCm;
	Droplet.VelocityCmS = VelocityCmS;
	Droplet.RadiusCm = RadiusCm;
	Droplet.LifeS = LifeS;
	Droplet.DragTimeS = DragTimeS;
}

void ABendingWaterWhipActor::UpdateDroplets(float DeltaSeconds)
{
	const ABendingSandboxArena* ArenaActor = Arena.Get();
	const BendingSim::FSimWorld* SimWorld = GetSimWorld();

	// Fast water sheds droplets (more the faster it goes); the stream splashes where it slaps the ground.
	int32 Budget = 48;
	for (int32 Index = 1; SimWorld && Index < Whip.GetNumPoints(); ++Index)
	{
		const int32 Segment = FMath::Min(Index, Whip.GetNumSegments() - 1);
		const BendingSim::FVolume* SegmentVolume = SimWorld->GetVolume(Whip.GetSegmentHandle(Segment));
		if (!SegmentVolume || SegmentVolume->Substance != BendingSim::ESubstance::Water)
		{
			continue;
		}
		const FVector Point = BendingUnits::ToEngine(Whip.GetPoint(Index));
		const FVector Velocity = BendingUnits::ToEngine(Whip.GetPointVelocity(Index));
		const double Speed = Velocity.Size();
		const double Radius = Whip.GetSegmentRadiusCm(Segment) * VisualRadiusScale;
		if (Speed > ShedSpeedCmS && Budget > 0)
		{
			const double Expected = DeltaSeconds * 26.0 * FMath::Square((Speed - ShedSpeedCmS) / 700.0);
			const int32 Count = FMath::Min3(6, Budget, FMath::FloorToInt32(Expected) + (FMath::FRand() < FMath::Frac(Expected) ? 1 : 0));
			for (int32 Droplet = 0; Droplet < Count; ++Droplet)
			{
				SpawnDroplet(Point + FMath::VRand() * Radius, Velocity * FMath::FRandRange(0.7, 1.0) + FMath::VRand() * 140.0, FMath::FRandRange(0.4f, 1.4f), 1.6f, 0.75f);
			}
			Budget -= Count;
		}
		SlapCooldownS[Index] = FMath::Max(SlapCooldownS[Index] - DeltaSeconds, 0.f);
		const double Ground = ArenaActor ? ArenaActor->GetGroundHeightAt(Point) : -UE_BIG_NUMBER;
		if (SlapCooldownS[Index] <= 0.f && Speed > 250.0 && Point.Z - Ground < Radius + 4.0)
		{
			SlapCooldownS[Index] = 0.3f;
			for (int32 Droplet = 0; Droplet < 7; ++Droplet)
			{
				const FVector Out = FVector(FMath::FRandRange(-1.0, 1.0), FMath::FRandRange(-1.0, 1.0), 0.0).GetSafeNormal();
				SpawnDroplet(FVector(Point.X, Point.Y, Ground + 2.0), Out * FMath::FRandRange(40.0, 160.0) + FVector(0.0, 0.0, FMath::FRandRange(100.0, 260.0)),
					FMath::FRandRange(0.5f, 1.2f), 1.4f, 0.5f);
			}
		}
	}

	const double GravityZ = GetWorld()->GetGravityZ();
	for (int32 Index = Droplets.Num() - 1; Index >= 0; --Index)
	{
		FDroplet& Droplet = Droplets[Index];
		Droplet.LifeS -= DeltaSeconds;
		Droplet.VelocityCmS *= BendingSim::KExp(-DeltaSeconds / FMath::Max(Droplet.DragTimeS, 0.02f));
		Droplet.VelocityCmS.Z += GravityZ * DeltaSeconds;
		Droplet.LocationCm += Droplet.VelocityCmS * DeltaSeconds;
		double Floor = -UE_BIG_NUMBER;
		if (ArenaActor)
		{
			const int32 Pond = ArenaActor->FindPondAt(Droplet.LocationCm);
			Floor = Pond != INDEX_NONE ? ArenaActor->GetLayout().Ponds[Pond].SurfaceHeightCm : ArenaActor->GetGroundHeightAt(Droplet.LocationCm);
		}
		if (Droplet.LifeS <= 0.f || Droplet.LocationCm.Z < Floor)
		{
			Droplets.RemoveAtSwap(Index);
		}
	}
}

double ABendingWaterWhipActor::TransferHeat(double HeatJ)
{
	BendingSim::FSimWorld* SimWorld = GetSimWorld();
	return SimWorld ? Whip.TransferHeat(*SimWorld, HeatJ) : 0.0;
}

double ABendingWaterWhipActor::TakeWater(double MassKg, double& OutTemperatureK)
{
	BendingSim::FSimWorld* SimWorld = GetSimWorld();
	return SimWorld ? Whip.TakeWater(*SimWorld, MassKg, OutTemperatureK) : 0.0;
}

double ABendingWaterWhipActor::GetHeatToFreeze() const
{
	const BendingSim::FSimWorld* SimWorld = GetSimWorld();
	return SimWorld ? Whip.GetHeatToFreeze(*SimWorld) : 0.0;
}

double ABendingWaterWhipActor::GetHeatToMelt() const
{
	const BendingSim::FSimWorld* SimWorld = GetSimWorld();
	return SimWorld ? Whip.GetHeatToMelt(*SimWorld) : 0.0;
}

bool ABendingWaterWhipActor::IsFrozen() const
{
	const BendingSim::FSimWorld* SimWorld = GetSimWorld();
	return SimWorld && Whip.IsFrozen(*SimWorld);
}

bool ABendingWaterWhipActor::IsAnyFrozen() const
{
	const BendingSim::FSimWorld* SimWorld = GetSimWorld();
	return SimWorld && Whip.IsAnyFrozen(*SimWorld);
}

bool ABendingWaterWhipActor::IsForming() const
{
	return Whip.GetState() == BendingSim::EWhipState::Forming;
}

bool ABendingWaterWhipActor::ShouldCollapse() const
{
	const BendingSim::FSimWorld* SimWorld = GetSimWorld();
	return SimWorld && Whip.ShouldCollapse(*SimWorld);
}

double ABendingWaterWhipActor::GetMassKg() const
{
	const BendingSim::FSimWorld* SimWorld = GetSimWorld();
	return SimWorld ? Whip.GetMassKg(*SimWorld) : 0.0;
}

int32 ABendingWaterWhipActor::GetNumFrozenSegments() const
{
	const BendingSim::FSimWorld* SimWorld = GetSimWorld();
	int32 Frozen = 0;
	for (int32 Segment = 0; SimWorld && Segment < Whip.GetNumSegments(); ++Segment)
	{
		const BendingSim::FHandle Handle = Whip.GetSegmentHandle(Segment);
		const BendingSim::FVolume* SegmentVolume = SimWorld->GetVolume(Handle);
		Frozen += SegmentVolume && !SimWorld->IsDepleted(Handle) && SegmentVolume->Substance == BendingSim::ESubstance::Ice ? 1 : 0;
	}
	return Frozen;
}

int32 ABendingWaterWhipActor::ReleaseWater(TArray<BendingSim::FWhipDrop>& OutDrops)
{
	OutDrops.SetNum(BendingSim::FWaterWhip::MaxSegments);
	int32 Count = 0;
	if (BendingSim::FSimWorld* SimWorld = GetSimWorld())
	{
		Count = Whip.Release(*SimWorld, OutDrops.GetData(), OutDrops.Num());
	}
	OutDrops.SetNum(Count);
	Destroy();
	return Count;
}

void ABendingWaterWhipActor::Collapse()
{
	UWorld* World = GetWorld();
	const UBendingTechniqueComponent* BenderComponent = Bender.Get();
	AActor* InstigatorActor = BenderComponent ? BenderComponent->GetOwner() : nullptr;

	TArray<BendingSim::FWhipDrop> Drops;
	ReleaseWater(Drops);
	for (const BendingSim::FWhipDrop& Drop : Drops)
	{
		ABendingProjectile::SpawnProjectile(World, EBendingProjectileKind::Water,
			BendingSim::MakeWaterBall(Drop.MassKg, Drop.TemperatureK, Drop.LocationCm, Drop.VelocityCmS), InstigatorActor, false);
	}
}

void ABendingWaterWhipActor::UpdateVisuals()
{
	WaterTransforms.Reset();
	IceTransforms.Reset();
	DropletTransforms.Reset();

	// The basic sphere mesh has a 50 cm radius.
	constexpr double SphereRadiusCm = 50.0;
	if (const BendingSim::FSimWorld* SimWorld = GetSimWorld())
	{
		// Small overlapping spheres along a Catmull-Rom curve through the chain points: a smooth, tapering stream.
		const int32 NumPoints = Whip.GetNumPoints();
		const int32 NumSegments = Whip.GetNumSegments();
		const auto PointAt = [this, NumPoints](int32 Index) { return BendingUnits::ToEngine(Whip.GetPoint(FMath::Clamp(Index, 0, NumPoints - 1))); };
		for (int32 Segment = 0; Segment < NumSegments; ++Segment)
		{
			const BendingSim::FHandle Handle = Whip.GetSegmentHandle(Segment);
			const BendingSim::FVolume* SegmentVolume = SimWorld->GetVolume(Handle);
			if (!SegmentVolume || SimWorld->IsDepleted(Handle))
			{
				continue;
			}
			TArray<FTransform>& Target = SegmentVolume->Substance == BendingSim::ESubstance::Ice ? IceTransforms : WaterTransforms;
			const double Radius = FMath::Max(Whip.GetSegmentRadiusCm(Segment) * VisualRadiusScale, static_cast<double>(MinVisualRadiusCm));
			const FVector P0 = PointAt(Segment - 1), P1 = PointAt(Segment), P2 = PointAt(Segment + 1), P3 = PointAt(Segment + 2);
			for (int32 Sample = 0; Sample < SamplesPerSegment; ++Sample)
			{
				const double T = static_cast<double>(Sample) / SamplesPerSegment;
				// Thin where the water leaves the hand, rounded off at the tip.
				const double Along = (Segment + T) / NumSegments;
				const double Taper = FMath::Lerp(0.45, 1.0, FMath::SmoothStep(0.0, 0.035, Along)) * (Along > 0.955 ? FMath::Sqrt(FMath::Max(0.03, 1.0 - FMath::Square((Along - 0.955) / 0.045))) : 1.0);
				Target.Emplace(FQuat::Identity, CatmullRom(P0, P1, P2, P3, T), FVector(Radius * Taper / SphereRadiusCm));
			}
		}
	}
	for (const FDroplet& Droplet : Droplets)
	{
		// Fast droplets stretch along their motion.
		const double Speed = Droplet.VelocityCmS.Size();
		const FQuat Along = Speed > 1.0 ? FRotationMatrix::MakeFromX(Droplet.VelocityCmS).ToQuat() : FQuat::Identity;
		const double Stretch = 1.0 + FMath::Min(Speed * 0.0006, 2.2);
		DropletTransforms.Emplace(Along, Droplet.LocationCm, FVector(Droplet.RadiusCm * Stretch, Droplet.RadiusCm, Droplet.RadiusCm) / SphereRadiusCm);
	}

	UBendingSandboxLibrary::SetInstanceTransforms(WaterInstances, WaterTransforms);
	UBendingSandboxLibrary::SetInstanceTransforms(IceInstances, IceTransforms);
	UBendingSandboxLibrary::SetInstanceTransforms(DropletInstances, DropletTransforms);
}
