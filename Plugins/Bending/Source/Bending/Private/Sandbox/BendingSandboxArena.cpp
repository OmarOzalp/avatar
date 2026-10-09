#include "Sandbox/BendingSandboxArena.h"

#include "BendingLog.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerStart.h"
#include "Interaction/BendingInteractionSubsystem.h"
#include "Physics/BendingUnits.h"
#include "Sandbox/BendableTerrain.h"
#include "Sandbox/BendingPropActor.h"
#include "Sandbox/BendingSandboxLibrary.h"

namespace
{
	/** The character drops onto the ground from this high above the start. */
	constexpr double PlayerStartHeightCm = 100.0;
	/** Water discs reach a little past the pond radius, where the basin rises above the surface. */
	constexpr double PondSurfaceRadiusScale = 1.06;
	constexpr double PondSurfaceThicknessCm = 4.0;
	constexpr double MinSteamVisualRadiusCm = 10.0;
	/** Mud discs float this far above the ground to stay visible on it. */
	constexpr double MudLiftCm = 2.0;
	constexpr double MudThicknessCm = 2.0;
}

ABendingSandboxArena::ABendingSandboxArena()
{
	PrimaryActorTick.bCanEverTick = true;
	// Draws the simulation's state after everything else has changed it this frame.
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;

	SteamInstances = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("SteamInstances"));
	MoistureInstances = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("MoistureInstances"));
	for (UInstancedStaticMeshComponent* Instances : { SteamInstances.Get(), MoistureInstances.Get() })
	{
		Instances->SetupAttachment(Root);
		Instances->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
		Instances->SetGenerateOverlapEvents(false);
		Instances->SetCanEverAffectNavigation(false);
		Instances->SetCastShadow(false);
	}
}

ABendingSandboxArena* ABendingSandboxArena::Find(const UWorld* World)
{
	if (!World)
	{
		return nullptr;
	}
	for (TActorIterator<ABendingSandboxArena> It(World); It; ++It)
	{
		if (IsValid(*It))
		{
			return *It;
		}
	}
	return nullptr;
}

ABendableTerrain* ABendingSandboxArena::GetTerrainActor() const
{
	return TerrainActor;
}

APlayerStart* ABendingSandboxArena::GetPlayerStart() const
{
	return PlayerStart;
}

BendingSim::FTerrain* ABendingSandboxArena::GetTerrain() const
{
	return TerrainActor ? TerrainActor->GetTerrain() : nullptr;
}

void ABendingSandboxArena::BeginPlay()
{
	Super::BeginPlay();
	BuildArena();
}

// ---------------------------------------------------------------------------------------------------- Build

void ABendingSandboxArena::BuildArena()
{
	if (bBuilt || !GetWorld())
	{
		return;
	}
	bBuilt = true;
	Layout = BendingSim::GetTrainingGroundLayout();

	SpawnTerrain();
	SpawnPonds();
	SpawnProps();
	SpawnPlayerStart();
	if (bSpawnLighting)
	{
		SpawnLighting();
	}

	SteamInstances->SetStaticMesh(UBendingSandboxLibrary::LoadBasicShape(TEXT("Sphere")));
	UBendingSandboxLibrary::SetMeshColor(SteamInstances, UBendingSandboxLibrary::FromSRGB(226, 229, 233));
	MoistureInstances->SetStaticMesh(UBendingSandboxLibrary::LoadBasicShape(TEXT("Cylinder")));
	UBendingSandboxLibrary::SetMeshColor(MoistureInstances, UBendingSandboxLibrary::FromSRGB(72, 52, 34));

	UE_LOG(LogBending, Log, TEXT("Training ground built: %d props, %d ponds."), Layout.NumProps, Layout.NumPonds);
}

void ABendingSandboxArena::SpawnTerrain()
{
	FActorSpawnParameters Params;
	Params.Owner = this;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	TerrainActor = GetWorld()->SpawnActor<ABendableTerrain>(ABendableTerrain::StaticClass(), FTransform::Identity, Params);
	if (TerrainActor)
	{
		TerrainActor->BuildFromLayout(Layout);
	}
	else
	{
		UE_LOG(LogBending, Error, TEXT("%s could not spawn its terrain."), *GetName());
	}
}

void ABendingSandboxArena::SpawnPonds()
{
	UStaticMesh* Cylinder = UBendingSandboxLibrary::LoadBasicShape(TEXT("Cylinder"));
	for (int32 Index = 0; Index < Layout.NumPonds; ++Index)
	{
		const BendingSim::FArenaPond& Pond = Layout.Ponds[Index];
		UStaticMeshComponent* Surface = NewObject<UStaticMeshComponent>(this);
		Surface->SetStaticMesh(Cylinder);
		Surface->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
		Surface->SetGenerateOverlapEvents(false);
		Surface->SetCanEverAffectNavigation(false);
		Surface->SetCastShadow(false);
		Surface->SetupAttachment(Root);
		Surface->RegisterComponent();

		// A flat disc of water whose top is the pond's surface.
		const double RadiusCm = PondSurfaceRadiusScale * Pond.RadiusCm;
		Surface->SetWorldLocationAndRotation(FVector(Pond.CenterCm.X, Pond.CenterCm.Y, Pond.SurfaceHeightCm - 0.5 * PondSurfaceThicknessCm), FRotator::ZeroRotator);
		Surface->SetWorldScale3D(FVector(RadiusCm / 50.0, RadiusCm / 50.0, PondSurfaceThicknessCm / 100.0));
		UBendingSandboxLibrary::SetMeshColor(Surface, UBendingSandboxLibrary::FromSRGB(40, 105, 175));
		PondSurfaces.Add(Surface);
	}
}

void ABendingSandboxArena::SpawnProps()
{
	FActorSpawnParameters Params;
	Params.Owner = this;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	for (int32 Index = 0; Index < Layout.NumProps; ++Index)
	{
		const BendingSim::FArenaPropPlacement& Placement = Layout.Props[Index];
		FVector Ground(Placement.LocationCm.X, Placement.LocationCm.Y, 0.0);
		Ground.Z = GetGroundHeightAt(Ground);
		ABendingPropActor* Prop = GetWorld()->SpawnActor<ABendingPropActor>(ABendingPropActor::StaticClass(), FTransform(Ground), Params);
		if (Prop)
		{
			Prop->InitArenaProp(Placement.Kind, Ground, Placement.YawDeg, Placement.Variant);
			Props.Add(Prop);
		}
	}
}

void ABendingSandboxArena::SpawnPlayerStart()
{
	FActorSpawnParameters Params;
	Params.Owner = this;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	FVector Start(Layout.PlayerStartCm.X, Layout.PlayerStartCm.Y, 0.0);
	Start.Z = GetGroundHeightAt(Start) + PlayerStartHeightCm;
	PlayerStart = GetWorld()->SpawnActor<APlayerStart>(APlayerStart::StaticClass(), FTransform(FRotator(0.0, Layout.PlayerStartYawDeg, 0.0), Start), Params);
}

void ABendingSandboxArena::SpawnLighting()
{
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		if (It->FindComponentByClass<UDirectionalLightComponent>())
		{
			// The level brings its own lighting.
			return;
		}
	}

	// Mid-afternoon sun, 42 degrees up.
	UDirectionalLightComponent* Sun = NewObject<UDirectionalLightComponent>(this, TEXT("Sun"));
	Sun->SetMobility(EComponentMobility::Movable);
	Sun->SetupAttachment(Root);
	Sun->SetRelativeRotation(FRotator(-42.0, 35.0, 0.0));
	Sun->SetIntensity(8.f);
	Sun->SetLightColor(FLinearColor(1.f, 0.95f, 0.88f));
	Sun->SetAtmosphereSunLight(true);
	Sun->RegisterComponent();

	// Captures the atmosphere continuously, so nothing has to be baked.
	USkyLightComponent* SkyLight = NewObject<USkyLightComponent>(this, TEXT("SkyLight"));
	SkyLight->SetMobility(EComponentMobility::Movable);
	SkyLight->bRealTimeCapture = true;
	SkyLight->SetupAttachment(Root);
	SkyLight->RegisterComponent();

	USkyAtmosphereComponent* Atmosphere = NewObject<USkyAtmosphereComponent>(this, TEXT("SkyAtmosphere"));
	Atmosphere->SetupAttachment(Root);
	Atmosphere->RegisterComponent();

	UExponentialHeightFogComponent* Fog = NewObject<UExponentialHeightFogComponent>(this, TEXT("HeightFog"));
	Fog->SetupAttachment(Root);
	Fog->SetFogDensity(0.008f);
	Fog->RegisterComponent();
}

// ---------------------------------------------------------------------------------------------------- Ponds and ground

double ABendingSandboxArena::DrawPondWater(int32 PondIndex, double RequestedKg)
{
	if (PondIndex < 0 || PondIndex >= Layout.NumPonds)
	{
		return 0.0;
	}
	BendingSim::FArenaPond& Pond = Layout.Ponds[PondIndex];
	const double DrawnKg = FMath::Clamp(RequestedKg, 0.0, Pond.WaterKg);
	Pond.WaterKg -= DrawnKg;
	return DrawnKg;
}

void ABendingSandboxArena::ReturnPondWater(int32 PondIndex, double WaterKg)
{
	if (PondIndex >= 0 && PondIndex < Layout.NumPonds && WaterKg > 0.0)
	{
		Layout.Ponds[PondIndex].WaterKg += WaterKg;
	}
}

int32 ABendingSandboxArena::FindPondAt(const FVector& LocationCm) const
{
	for (int32 Index = 0; Index < Layout.NumPonds; ++Index)
	{
		const BendingSim::FArenaPond& Pond = Layout.Ponds[Index];
		if (FVector::Dist2D(LocationCm, FVector(Pond.CenterCm.X, Pond.CenterCm.Y, 0.0)) <= Pond.RadiusCm)
		{
			return Index;
		}
	}
	return INDEX_NONE;
}

double ABendingSandboxArena::GetGroundHeightAt(const FVector& LocationCm) const
{
	return TerrainActor ? TerrainActor->GetHeightAt(LocationCm) : 0.0;
}

FVector ABendingSandboxArena::GetGroundNormalAt(const FVector& LocationCm) const
{
	return TerrainActor ? TerrainActor->GetNormalAt(LocationCm) : FVector::UpVector;
}

float ABendingSandboxArena::GetSoilPorosity() const
{
	const BendingSim::FTerrain* Terrain = GetTerrain();
	return Terrain ? static_cast<float>(Terrain->SoilPorosity) : DefaultSoilPorosity;
}

// ---------------------------------------------------------------------------------------------------- Frame

void ABendingSandboxArena::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bBuilt)
	{
		return;
	}
	if (const UBendingInteractionSubsystem* Interaction = GetWorld()->GetSubsystem<UBendingInteractionSubsystem>())
	{
		UpdateSteam(*Interaction);
		UpdateMoisture(*Interaction);
	}
}

void ABendingSandboxArena::UpdateSteam(const UBendingInteractionSubsystem& Interaction)
{
	InstanceScratch.Reset();
	if (const BendingSim::FSimWorld* SimWorld = Interaction.GetSimWorld())
	{
		// Steam clouds are the simulation's free volumes: no actor owns them.
		for (int32 Index = 0; Index < SimWorld->GetSlotLimit(); ++Index)
		{
			const BendingSim::FHandle Handle = SimWorld->GetHandleAt(Index);
			if (!Handle.IsSet() || SimWorld->IsOwned(Handle) || SimWorld->IsDepleted(Handle))
			{
				continue;
			}
			const BendingSim::FVolume* Volume = SimWorld->GetVolume(Handle);
			if (!Volume || Volume->Substance != BendingSim::ESubstance::Steam)
			{
				continue;
			}
			const double RadiusCm = FMath::Max(Volume->RadiusCm * SteamVisualScale, MinSteamVisualRadiusCm);
			InstanceScratch.Emplace(FQuat::Identity, BendingUnits::ToEngine(Volume->LocationCm), FVector(RadiusCm / 50.0));
		}
	}
	UBendingSandboxLibrary::SetInstanceTransforms(SteamInstances, InstanceScratch);
}

void ABendingSandboxArena::UpdateMoisture(const UBendingInteractionSubsystem& Interaction)
{
	InstanceScratch.Reset();
	if (const BendingSim::FSimWorld* SimWorld = Interaction.GetSimWorld())
	{
		for (int32 Index = 0; Index < SimWorld->GetNumMoisturePatches(); ++Index)
		{
			const BendingSim::FMoisturePatch& Patch = SimWorld->GetMoisturePatch(Index);
			if (Patch.GetSaturation(SimWorld->Settings.MoistureSoilDepthM) < MinVisibleSaturation)
			{
				continue;
			}
			FVector Center = BendingUnits::ToEngine(Patch.CenterCm);
			Center.Z = GetGroundHeightAt(Center) + MudLiftCm;
			InstanceScratch.Emplace(FQuat::Identity, Center, FVector(Patch.RadiusCm / 50.0, Patch.RadiusCm / 50.0, MudThicknessCm / 100.0));
		}
	}
	UBendingSandboxLibrary::SetInstanceTransforms(MoistureInstances, InstanceScratch);
}
