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

ABendingWaterWhipActor::ABendingWaterWhipActor()
{
	PrimaryActorTick.bCanEverTick = true;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;

	WaterInstances = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("WaterInstances"));
	IceInstances = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("IceInstances"));
	for (UInstancedStaticMeshComponent* Instances : { WaterInstances.Get(), IceInstances.Get() })
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
	UBendingSandboxLibrary::SetMeshColor(WaterInstances, UBendingSandboxLibrary::FromSRGB(45, 120, 230));
	UBendingSandboxLibrary::SetMeshColor(IceInstances, UBendingSandboxLibrary::FromSRGB(215, 240, 255));

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

	Whip.SetHoldExtended(BenderComponent->ShouldHoldWhipExtended());
	Whip.SetControl(BendingUnits::ToSim(BenderComponent->GetHandLocation()), BendingUnits::ToSim(BenderComponent->GetHandVelocity()),
		BendingUnits::ToSim(BenderComponent->GetAimPoint()));
	const ABendingSandboxArena* ArenaActor = Arena.Get();
	Whip.PreStep(*SimWorld, ArenaActor ? ArenaActor->GetTerrain() : nullptr, DeltaSeconds);
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

bool ABendingWaterWhipActor::Lash()
{
	return Whip.Lash();
}

double ABendingWaterWhipActor::TransferHeat(double HeatJ)
{
	BendingSim::FSimWorld* SimWorld = GetSimWorld();
	return SimWorld ? Whip.TransferHeat(*SimWorld, HeatJ) : 0.0;
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

	if (const BendingSim::FSimWorld* SimWorld = GetSimWorld())
	{
		const int32 NumSegments = Whip.GetNumSegments();
		for (int32 Segment = 0; Segment < NumSegments; ++Segment)
		{
			const BendingSim::FHandle Handle = Whip.GetSegmentHandle(Segment);
			const BendingSim::FVolume* SegmentVolume = SimWorld->GetVolume(Handle);
			if (!SegmentVolume || SimWorld->IsDepleted(Handle))
			{
				continue;
			}
			TArray<FTransform>& Target = SegmentVolume->Substance == BendingSim::ESubstance::Ice ? IceTransforms : WaterTransforms;
			const FVector A = BendingUnits::ToEngine(Whip.GetPoint(Segment));
			const FVector B = BendingUnits::ToEngine(Whip.GetPoint(Segment + 1));
			const double Radius = FMath::Max(Whip.GetSegmentRadiusCm(Segment) * VisualRadiusScale, static_cast<double>(MinVisualRadiusCm));
			const FVector Axis = B - A;
			const double HalfLength = 0.5 * Axis.Size();
			const FQuat Along = HalfLength > UE_KINDA_SMALL_NUMBER ? FRotationMatrix::MakeFromX(Axis).ToQuat() : FQuat::Identity;

			// A sphere on each chain point and an ellipsoid along each segment: one continuous stream.
			Target.Emplace(FQuat::Identity, A, FVector(Radius / 50.0));
			Target.Emplace(Along, 0.5 * (A + B), FVector((HalfLength + 0.5 * Radius) / 50.0, Radius / 50.0, Radius / 50.0));
			if (Segment == NumSegments - 1)
			{
				Target.Emplace(FQuat::Identity, B, FVector(Radius / 50.0));
			}
		}
	}

	UBendingSandboxLibrary::SetInstanceTransforms(WaterInstances, WaterTransforms);
	UBendingSandboxLibrary::SetInstanceTransforms(IceInstances, IceTransforms);
}
