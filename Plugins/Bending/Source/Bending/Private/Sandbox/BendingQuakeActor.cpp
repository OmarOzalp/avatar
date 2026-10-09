#include "Sandbox/BendingQuakeActor.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/World.h"
#include "Sandbox/BendingSandboxArena.h"
#include "Sandbox/BendingSandboxLibrary.h"

namespace
{
	constexpr int32 InnerRocks = 12;
	constexpr int32 OuterRocks = 18;
	constexpr double InnerRadiusCm = 260.0;
	constexpr double OuterRadiusCm = 520.0;
	/** A rock is fully up this long after the wave reaches it, then sinks back over about 1.3 s. */
	constexpr double RiseSeconds = 0.12;
	constexpr double SinkRate = 0.9;
}

ABendingQuakeActor::ABendingQuakeActor()
{
	PrimaryActorTick.bCanEverTick = true;

	Rocks = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("Rocks"));
	RootComponent = Rocks;
	Rocks->SetMobility(EComponentMobility::Movable);
	Rocks->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	Rocks->SetGenerateOverlapEvents(false);
	Rocks->SetCanEverAffectNavigation(false);
}

void ABendingQuakeActor::InitQuake(const FVector& GroundCm)
{
	SetActorLocation(GroundCm);
	Rocks->SetStaticMesh(UBendingSandboxLibrary::LoadBasicShape(TEXT("Cube")));
	UBendingSandboxLibrary::SetMeshColor(Rocks, UBendingSandboxLibrary::FromSRGB(141, 127, 110));

	const ABendingSandboxArena* Arena = ABendingSandboxArena::Find(GetWorld());
	FRandomStream Random(static_cast<int32>(GroundCm.X * 7.0 + GroundCm.Y * 13.0));
	RockSpecs.Reset();
	for (int32 Index = 0; Index < InnerRocks + OuterRocks; ++Index)
	{
		const bool bOuter = Index >= InnerRocks;
		const int32 Count = bOuter ? OuterRocks : InnerRocks;
		const int32 Slot = bOuter ? Index - InnerRocks : Index;
		FQuakeRock& Rock = RockSpecs.AddDefaulted_GetRef();
		Rock.AngleRad = (Slot + (bOuter ? 0.5 : 0.0)) / Count * 2.0 * UE_DOUBLE_PI + Random.FRandRange(-0.1f, 0.1f);
		Rock.RadiusCm = (bOuter ? OuterRadiusCm : InnerRadiusCm) + Random.FRandRange(-30.f, 30.f);
		Rock.SizeCm = (bOuter ? 32.0 : 42.0) * Random.FRandRange(0.75f, 1.25f);
		Rock.Tilt = FRotator(Random.FRandRange(-25.f, 25.f), FMath::RadiansToDegrees(Rock.AngleRad), Random.FRandRange(-25.f, 25.f));
		const FVector Spot = GroundCm + FVector(FMath::Cos(Rock.AngleRad), FMath::Sin(Rock.AngleRad), 0.0) * Rock.RadiusCm;
		Rock.GroundZ = Arena ? Arena->GetGroundHeightAt(Spot) : GroundCm.Z;
	}
}

void ABendingQuakeActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	AgeSeconds += DeltaSeconds;

	const FVector Center = GetActorLocation();
	bool bAnyUp = false;
	Scratch.Reset();
	for (const FQuakeRock& Rock : RockSpecs)
	{
		// Each rock bursts up when the wave reaches it, overshoots, then sinks back into the ground.
		const double Since = AgeSeconds - Rock.RadiusCm / FMath::Max(static_cast<double>(WaveSpeedCmS), 1.0);
		double Lift = 0.0;
		if (Since > 0.0)
		{
			Lift = Since < RiseSeconds ? Since / RiseSeconds * 1.15 : FMath::Max(0.0, 1.15 - (Since - RiseSeconds) * SinkRate);
		}
		bAnyUp |= Since < 1.6;
		const double Size = Rock.SizeCm * FMath::Min(1.0, Lift * 1.4);
		const FVector Location(Center.X + FMath::Cos(Rock.AngleRad) * Rock.RadiusCm, Center.Y + FMath::Sin(Rock.AngleRad) * Rock.RadiusCm,
			Rock.GroundZ - 0.9 * Rock.SizeCm + Lift * 1.6 * Rock.SizeCm);
		Scratch.Emplace(Rock.Tilt.Quaternion(), Location, FVector(Size, Size, 1.5 * Size) / 100.0);
	}
	UBendingSandboxLibrary::SetInstanceTransforms(Rocks, Scratch);
	if (!bAnyUp)
	{
		Destroy();
	}
}
