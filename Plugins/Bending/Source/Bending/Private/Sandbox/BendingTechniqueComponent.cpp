#include "Sandbox/BendingTechniqueComponent.h"

#include "Abilities/BendingGameplayEffects.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Attributes/BendingAttributeSet.h"
#include "BendingGameplayTags.h"
#include "BendingLog.h"
#include "BendingSettings.h"
#include "CollisionQueryParams.h"
#include "Components/BendingComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/HitResult.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "Physics/BendingUnits.h"
#include "Sandbox/BendingProjectile.h"
#include "Sandbox/BendingPropActor.h"
#include "Sandbox/BendingQuakeActor.h"
#include "Sandbox/BendingSandboxArena.h"
#include "Sandbox/BendingSandboxLibrary.h"
#include "Sandbox/BendingSparringPartner.h"
#include "Sandbox/BendingTechniqueMove.h"
#include "Sandbox/BendingTornadoActor.h"
#include "Sandbox/BendingWaterWhipActor.h"
#include "Sim/BendingArena.h"
#include "Sim/BendingTechniques.h"

namespace
{
	constexpr int32 MaxMessages = 6;
	/** Stream puffs owed after a hitch beyond this are dropped rather than fired in one burst. */
	constexpr double MaxOwedPuffs = 3.0;
	/** A stream stops when the bender cannot pay for half a puff; a single blast still goes out weaker. */
	constexpr double StreamMinChiFraction = 0.5;
	constexpr double BlastMinChiFraction = 0.1;
	/** A waterbender can draw a whip from a water barrel this close to the hand. */
	constexpr double BarrelDrawRangeCm = 500.0;
	/** Air Jump: the column of air under the feet, pushed down as the bender rises. */
	constexpr double AirJumpPuffRadiusCm = 50.0;
	constexpr double AirJumpPuffCompression = 2.0;
	constexpr double AirJumpPuffSpeedCmS = 1200.0;
	/** Below this aim distance the camera direction is used instead of the direction to the aim point. */
	constexpr double MinAimDistanceCm = 50.0;
	constexpr double MaxHandSpeedCmS = 3000.0;
	/** Ice daggers are frozen to this temperature. */
	constexpr double IceDaggerTemperatureK = 263.15;
	/** Jet Dash: a puff of flame leaves the feet this often, this hot. */
	constexpr double JetFlameIntervalS = 0.05;
	constexpr double JetFlameTemperatureK = 1300.0;
	/** Air Scooter: the ball under the feet (radius) and how high it lifts the body. */
	constexpr double ScooterBallRadiusCm = 35.0;
	/** A clean hit rocks the bender (slowed) this long; a broken guard longer, and it cannot go up again for a while. */
	constexpr double HitFlinchS = 0.3;
	constexpr double GuardBreakFlinchS = 0.8;
	constexpr double GuardBreakCooldownS = 1.0;
	/** Back on their feet, the bender cannot be hurt for this long. */
	constexpr double GetUpProtectS = 1.5;
	/** Knocked down: thrown back and up. */
	constexpr double KnockdownSpeedCmS = 380.0;
	constexpr double KnockdownLiftCmS = 260.0;
	/** A blocked flame shoves a third as hard as a clean hit. */
	constexpr double BlockedKnockbackScale = 0.35;
	/** Chi a perfect guard gives back. */
	constexpr double ParryChi = 10.0;

	const FLinearColor InfoColor(0.85f, 0.92f, 1.f);
	const FLinearColor WarningColor(1.f, 0.72f, 0.3f);

	const BendingSim::FTechniqueTuning& GetTuning()
	{
		return BendingSim::GetDefaultTechniqueTuning();
	}
}

UBendingTechniqueComponent::UBendingTechniqueComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

void UBendingTechniqueComponent::BeginPlay()
{
	Super::BeginPlay();

	AActor* Owner = GetOwner();
	CachedBending = UBendingComponent::FindBendingComponent(Owner);
	if (!Hand.IsValid())
	{
		TInlineComponentArray<USceneComponent*> SceneComponents(Owner);
		for (USceneComponent* Component : SceneComponents)
		{
			if (Component && Component->GetFName() == HandComponentName)
			{
				Hand = Component;
				break;
			}
		}
	}
	// The hand is read after the owner has posed its body this frame.
	AddTickPrerequisiteActor(Owner);
	AimPoint = Owner->GetActorLocation() + Owner->GetActorForwardVector() * FallbackAimDistanceCm;
	ViewDirection = Owner->GetActorForwardVector();
}

void UBendingTechniqueComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// A whip notices its bender is gone and lets its water fall.
	if (EndPlayReason == EEndPlayReason::Destroyed)
	{
		DropHeldRock();
	}
	Holds.Reset();
	Walls.Reset();
	SetAirScooter(false);
	Super::EndPlay(EndPlayReason);
}

void UBendingTechniqueComponent::SetHandComponent(USceneComponent* InHand)
{
	Hand = InHand;
}

// ---------------------------------------------------------------------------------------------------- Phases

void UBendingTechniqueComponent::OnTechniqueStartup(const UBendingTechniqueMove* Move)
{
	if (!Move)
	{
		return;
	}
	switch (Move->Technique)
	{
	case EBendingTechnique::WaterWhip:
		// Without a whip, the move draws one from a pond. With one, the lash starts now: the startup frames draw the
		// stream back over the shoulder, the active frames are the strike, and the water then flows back on its own.
		if (HasWaterWhip())
		{
			LashWaterWhip(Move->FrameData.GetPhaseSeconds(EBendingPhase::Startup), Move->FrameData.GetPhaseSeconds(EBendingPhase::Active));
		}
		else
		{
			BeginWaterWhip();
		}
		break;

	case EBendingTechnique::RockThrow:
		BeginRockThrow(Move->FrameData.GetPhaseSeconds(EBendingPhase::Startup));
		break;

	default:
		break;
	}
}

void UBendingTechniqueComponent::OnTechniqueActive(const UBendingTechniqueMove* Move)
{
	if (!Move)
	{
		return;
	}
	if (BendingTechnique::GetInfo(Move->Technique).bHold)
	{
		StartHold(Move);
		return;
	}

	const BendingSim::FTechniqueTuning& Tuning = GetTuning();
	switch (Move->Technique)
	{
	case EBendingTechnique::WaterFreeze:
		FreezeOrThawWhip();
		break;

	case EBendingTechnique::WaterRelease:
		ReleaseWhip(false);
		break;

	case EBendingTechnique::WaterBlast:
		ReleaseWhip(true);
		break;

	case EBendingTechnique::RockThrow:
		LaunchRock();
		break;

	case EBendingTechnique::EarthWall:
		BeginEarthWall();
		break;

	case EBendingTechnique::FireBlast:
		ShootFlame(Tuning.FireBlastMassKg, Tuning.FireBlastTemperatureK, Tuning.FireBlastSpeedMs, true, BlastMinChiFraction);
		break;

	case EBendingTechnique::AirBlast:
	{
		const FVector Origin = GetHandLocation();
		ShootAir(Tuning.AirBlastRadiusCm, Tuning.AirBlastCompression, Tuning.AirBlastSpeedMs, Origin, GetAimDirectionFrom(Origin), BlastMinChiFraction);
		break;
	}

	case EBendingTechnique::AirJump:
		DoAirJump();
		break;

	case EBendingTechnique::IceDaggers:
		ThrowIceDaggers();
		break;

	case EBendingTechnique::Earthquake:
		DoEarthquake();
		break;

	case EBendingTechnique::FireRing:
		DoFireRing();
		break;

	case EBendingTechnique::FireJet:
		DoFireJet();
		break;

	case EBendingTechnique::Tornado:
		SpawnTornado();
		break;

	default:
		break;
	}
}

void UBendingTechniqueComponent::OnTechniqueRecovery(const UBendingTechniqueMove* Move)
{
	// A rock is never left hanging once the throw's active frames are over.
	if (Move && Move->Technique == EBendingTechnique::RockThrow)
	{
		DropHeldRock();
	}
}

void UBendingTechniqueComponent::OnTechniqueInputReleased(const UBendingTechniqueMove* Move, float HeldSeconds)
{
	if (!Move)
	{
		return;
	}
	// Holds also poll their input every tick; this ends one on the release itself once its minimum has run.
	const double Now = GetWorldTime();
	for (int32 Index = Holds.Num() - 1; Index >= 0; --Index)
	{
		if (Holds[Index].Technique == Move->Technique && Now - Holds[Index].StartTimeSeconds >= Holds[Index].MinSeconds)
		{
			const FTechniqueHold Ended = Holds[Index];
			Holds.RemoveAt(Index);
			EndHold(Ended);
		}
	}
}

void UBendingTechniqueComponent::OnTechniqueEnded(const UBendingTechniqueMove* Move, bool bWasCancelled)
{
	if (!Move)
	{
		return;
	}
	if (Move->Technique == EBendingTechnique::RockThrow)
	{
		// Interrupted before the throw: the rock drops.
		DropHeldRock();
	}
}

// ---------------------------------------------------------------------------------------------------- Water

void UBendingTechniqueComponent::BeginWaterWhip()
{
	const BendingSim::FTechniqueTuning& Tuning = GetTuning();
	const FString NoWater = FString::Printf(TEXT("No water within %.0f m: go to a pond"), BendingUnits::CmToM(Tuning.WhipDrawRangeCm));
	ABendingSandboxArena* Arena = GetArena();
	if (!Arena)
	{
		AddMessage(NoWater, WarningColor);
		return;
	}

	const FVector HandLocation = GetHandLocation();
	BendingSim::FVec3 Source;
	int32 Pond = BendingSim::FindWaterSource(Arena->GetLayout(), BendingUnits::ToSim(HandLocation), Tuning.WhipDrawRangeCm, Source);
	if (Pond >= 0 && Arena->GetLayout().Ponds[Pond].WaterKg < Tuning.WhipWaterKg)
	{
		Pond = INDEX_NONE;
	}
	// No pond in reach: a water barrel close by will do.
	ABendingPropActor* Barrel = nullptr;
	if (Pond < 0)
	{
		double BestDistance = BarrelDrawRangeCm;
		for (TActorIterator<ABendingPropActor> It(GetWorld()); It; ++It)
		{
			ABendingPropActor* Prop = *It;
			if (Prop && Prop->GetKind() == BendingSim::EArenaProp::WaterBarrel && Prop->IsArenaProp() && Prop->GetWaterKg() >= Tuning.WhipWaterKg)
			{
				const FVector Top = Prop->GetActorLocation() + FVector(0.0, 0.0, 40.0);
				const double Distance = FVector::Dist(Top, HandLocation);
				if (Distance < BestDistance)
				{
					BestDistance = Distance;
					Barrel = Prop;
					Source = BendingUnits::ToSim(Top);
				}
			}
		}
	}
	if (Pond < 0 && !Barrel)
	{
		AddMessage(NoWater + TEXT(" or a water barrel"), WarningColor);
		return;
	}

	const double WaterKg = Barrel ? Barrel->TakeWater(Tuning.WhipWaterKg) : Arena->DrawPondWater(Pond, Tuning.WhipWaterKg);
	FActorSpawnParameters Params;
	Params.Owner = GetOwner();
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ABendingWaterWhipActor* NewWhip = GetWorld()->SpawnActor<ABendingWaterWhipActor>(ABendingWaterWhipActor::StaticClass(), FTransform(HandLocation), Params);
	if (!NewWhip || !NewWhip->InitWhip(this, BendingUnits::ToEngine(Source), WaterKg, UBendingSettings::Get().AmbientTemperatureK))
	{
		if (Barrel)
		{
			Barrel->ReturnWater(WaterKg);
		}
		else if (Pond >= 0)
		{
			Arena->ReturnPondWater(Pond, WaterKg);
		}
		if (NewWhip)
		{
			NewWhip->Destroy();
		}
		AddMessage(TEXT("The water would not come"), WarningColor);
		return;
	}
	Whip = NewWhip;

	// Work: lifting the water from the pond surface to the hand.
	const double LiftM = FMath::Max(BendingUnits::CmToM(HandLocation.Z - Source.Z), 0.0);
	SpendEnergy(WaterKg * GetGravityMs2() * LiftM, EBendingEnergyKind::Kinetic);
	AddMessage(FString::Printf(TEXT("Drew %.0f kg of water from the %s"), WaterKg, Barrel ? TEXT("barrel") : TEXT("pond")), InfoColor);
}

void UBendingTechniqueComponent::LashWaterWhip(double WindupS, double StrikeS)
{
	ABendingWaterWhipActor* WhipActor = Whip.Get();
	if (!WhipActor)
	{
		return;
	}
	if (!WhipActor->Lash(WindupS, StrikeS))
	{
		if (WhipActor->IsForming())
		{
			AddMessage(TEXT("The water is still streaming in"), InfoColor);
		}
		return;
	}
	SpendEnergy(BendingSim::KineticEnergyJ(WhipActor->GetMassKg(), WhipLashSpeedMs), EBendingEnergyKind::Kinetic);
}

void UBendingTechniqueComponent::FreezeOrThawWhip()
{
	ABendingWaterWhipActor* WhipActor = Whip.Get();
	if (!WhipActor)
	{
		AddMessage(TEXT("Nothing to freeze: draw a water whip near a pond first (LMB)"), WarningColor);
		return;
	}

	// Freezing extracts the water's heat (latent heat of fusion included); thawing puts it back. Too little chi
	// changes part of the whip, from the hand outward.
	const bool bThaw = WhipActor->IsAnyFrozen();
	const double NeedJ = bThaw ? WhipActor->GetHeatToMelt() : WhipActor->GetHeatToFreeze();
	const double GrantedJ = SpendEnergy(NeedJ, EBendingEnergyKind::Thermal);
	const double MovedJ = FMath::Abs(WhipActor->TransferHeat(bThaw ? GrantedJ : -GrantedJ));
	const bool bPartial = GrantedJ < 0.999 * NeedJ;
	AddMessage(FString::Printf(TEXT("%s the whip: %.2f MJ of heat (%.0f chi)%s"), bThaw ? TEXT("Thawed") : TEXT("Froze"), MovedJ / 1.0e6,
		GrantedJ / FMath::Max(UBendingSettings::Get().ThermalJoulesPerChi, 1.f), bPartial ? TEXT(", partly: out of chi") : TEXT("")),
		bPartial ? WarningColor : InfoColor);
}

void UBendingTechniqueComponent::ReleaseWhip(bool bAsBlast)
{
	ABendingWaterWhipActor* WhipActor = Whip.Get();
	if (!WhipActor)
	{
		AddMessage(TEXT("No water whip to let go of"), WarningColor);
		return;
	}
	if (WhipActor->IsAnyFrozen())
	{
		AddMessage(TEXT("Thaw the whip first (RMB)"), WarningColor);
		return;
	}

	TArray<BendingSim::FWhipDrop> Drops;
	WhipActor->ReleaseWater(Drops);
	Whip.Reset();
	if (Drops.Num() == 0)
	{
		return;
	}

	if (!bAsBlast)
	{
		// Every parcel keeps its own motion and falls: where it lands, soil turns to mud.
		double TotalKg = 0.0;
		for (const BendingSim::FWhipDrop& Drop : Drops)
		{
			SpawnWaterBall(Drop.MassKg, Drop.TemperatureK, BendingUnits::ToEngine(Drop.LocationCm), BendingUnits::ToEngine(Drop.VelocityCmS));
			TotalKg += Drop.MassKg;
		}
		AddMessage(FString::Printf(TEXT("Released %.1f kg of water"), TotalKg), InfoColor);
		return;
	}

	double MassKg = 0.0;
	double MassTemperature = 0.0;
	for (const BendingSim::FWhipDrop& Drop : Drops)
	{
		MassKg += Drop.MassKg;
		MassTemperature += Drop.MassKg * Drop.TemperatureK;
	}
	const BendingSim::FTechniqueTuning& Tuning = GetTuning();
	const FVector Origin = GetHandLocation();
	const FVector Direction = GetAimDirectionFrom(Origin);
	const double RequestedJ = BendingSim::KineticEnergyJ(MassKg, Tuning.WaterBlastSpeedMs);
	const double GrantedJ = SpendEnergy(RequestedJ, EBendingEnergyKind::Kinetic);
	const double SpeedMs = Tuning.WaterBlastSpeedMs * BendingSim::KSqrt(GrantedJ / FMath::Max(RequestedJ, 1.0));
	const double TemperatureK = MassTemperature / FMath::Max(MassKg, 1e-6);
	const BendingSim::FVolume Ball = BendingSim::MakeWaterBall(MassKg, TemperatureK, BendingUnits::ToSim(Origin), BendingSim::FVec3());
	SpawnWaterBall(MassKg, TemperatureK, Origin + Direction * (Ball.RadiusCm + 20.0), Direction * BendingUnits::MToCm(SpeedMs));
	AddMessage(FString::Printf(TEXT("Water blast: %.1f kg at %.0f m/s"), MassKg, SpeedMs), InfoColor);
}

ABendingProjectile* UBendingTechniqueComponent::SpawnWaterBall(double MassKg, double TemperatureK, const FVector& LocationCm, const FVector& VelocityCmS)
{
	return ABendingProjectile::SpawnProjectile(GetWorld(), EBendingProjectileKind::Water,
		BendingSim::MakeWaterBall(MassKg, TemperatureK, BendingUnits::ToSim(LocationCm), BendingUnits::ToSim(VelocityCmS)), GetOwner(), false);
}

// ---------------------------------------------------------------------------------------------------- Earth

void UBendingTechniqueComponent::BeginRockThrow(double StartupSeconds)
{
	DropHeldRock();
	BendingSim::FTerrain* Terrain = GetTerrain();
	if (!Terrain)
	{
		AddMessage(TEXT("No bendable ground here"), WarningColor);
		return;
	}

	// The rock is the soil pulled out of the ground in front of the bender: it leaves a crater.
	const BendingSim::FTechniqueTuning& Tuning = GetTuning();
	FVector Spot = GetOwner()->GetActorLocation() + GetFlatAimDirection() * RockPullDistanceCm;
	Spot.Z = Terrain->GetHeightAt(Spot.X, Spot.Y);
	const BendingSim::FTerrainBrush Quarry = BendingSim::RockQuarryBrush(BendingUnits::ToSim(Spot));
	const BendingSim::FTerrainEdit Edit = Terrain->RemoveSoil(Quarry, Tuning.RockMassKg / FMath::Max(Terrain->SoilDensityKgM3, 1.0));
	if (Edit.MassKg < 10.0)
	{
		Terrain->AddSoil(Quarry, Edit.VolumeM3);
		AddMessage(TEXT("No soil to pull up here"), WarningColor);
		return;
	}

	const BendingSim::FVolume Shape = BendingSim::MakeRock(Edit.MassKg, Tuning.RockDensityKgM3, BendingUnits::ToSim(Spot), BendingSim::FVec3());
	RockRiseFrom = Spot - FVector(0.0, 0.0, 0.3 * Shape.RadiusCm);
	RockRiseTo = Spot + FVector(0.0, 0.0, Tuning.RockHoldHeightCm);
	RockRiseStartSeconds = GetWorldTime();
	RockRiseSeconds = FMath::Max(StartupSeconds, 0.05);

	FActorSpawnParameters Params;
	Params.Owner = GetOwner();
	Params.Instigator = Cast<APawn>(GetOwner());
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ABendingPropActor* Rock = GetWorld()->SpawnActor<ABendingPropActor>(ABendingPropActor::StaticClass(), FTransform(RockRiseFrom), Params);
	if (!Rock)
	{
		Terrain->AddSoil(Quarry, Edit.VolumeM3);
		return;
	}
	Rock->InitThrownRock(Edit.MassKg, Tuning.RockDensityKgM3, RockRiseFrom);
	HeldRock = Rock;
	ThrownRocks.Add(Rock);
	TrimThrownRocks();
	AddMessage(FString::Printf(TEXT("Pulled %.0f kg of soil out of the ground"), Edit.MassKg), InfoColor);
}

void UBendingTechniqueComponent::LaunchRock()
{
	ABendingPropActor* Rock = HeldRock.Get();
	HeldRock.Reset();
	if (!Rock || !Rock->IsHeld())
	{
		return;
	}

	// Work: lifting it (m g h) and throwing it (1/2 m v^2). Short of chi, the throw is slower.
	const BendingSim::FTechniqueTuning& Tuning = GetTuning();
	const double MassKg = FMath::Max(Rock->GetMassKg(), 1.0);
	const FVector From = Rock->GetActorLocation();
	const double LiftJ = MassKg * GetGravityMs2() * FMath::Max(BendingUnits::CmToM(From.Z - RockRiseFrom.Z), 0.0);
	const double LaunchJ = BendingSim::KineticEnergyJ(MassKg, Tuning.RockLaunchSpeedMs);
	const double GrantedJ = SpendEnergy(LiftJ + LaunchJ, EBendingEnergyKind::Kinetic);
	const double SpeedMs = FMath::Min(BendingSim::KSqrt(2.0 * FMath::Max(GrantedJ - LiftJ, 0.0) / MassKg), Tuning.RockLaunchSpeedMs);
	Rock->Launch(GetAimDirectionFrom(From) * BendingUnits::MToCm(SpeedMs));

	const bool bWeak = SpeedMs < 0.9 * Tuning.RockLaunchSpeedMs;
	AddMessage(FString::Printf(TEXT("Rock thrown at %.0f m/s%s"), SpeedMs, bWeak ? TEXT(" (out of chi)") : TEXT("")), bWeak ? WarningColor : InfoColor);
}

void UBendingTechniqueComponent::DropHeldRock()
{
	if (ABendingPropActor* Rock = HeldRock.Get(); Rock && Rock->IsHeld())
	{
		Rock->Launch(FVector::ZeroVector);
	}
	HeldRock.Reset();
}

void UBendingTechniqueComponent::TrimThrownRocks()
{
	ThrownRocks.RemoveAll([](const TWeakObjectPtr<ABendingPropActor>& Rock) { return !Rock.IsValid(); });
	BendingSim::FTerrain* Terrain = GetTerrain();
	while (ThrownRocks.Num() > MaxThrownRocks)
	{
		ABendingPropActor* Oldest = ThrownRocks[0].Get();
		ThrownRocks.RemoveAt(0);
		if (!Oldest || Oldest == HeldRock.Get())
		{
			continue;
		}
		if (Terrain)
		{
			Oldest->CrumbleIntoGround(*Terrain);
		}
		else
		{
			Oldest->Destroy();
		}
	}
}

void UBendingTechniqueComponent::TickHeldRock()
{
	ABendingPropActor* Rock = HeldRock.Get();
	if (!Rock || !Rock->IsHeld())
	{
		return;
	}
	// Rises out of its crater over the throw's startup frames.
	const double Alpha = FMath::Clamp((GetWorldTime() - RockRiseStartSeconds) / RockRiseSeconds, 0.0, 1.0);
	Rock->SetHeldLocation(FMath::Lerp(RockRiseFrom, RockRiseTo, Alpha * Alpha * (3.0 - 2.0 * Alpha)));
}

void UBendingTechniqueComponent::BeginEarthWall()
{
	BendingSim::FTerrain* Terrain = GetTerrain();
	if (!Terrain)
	{
		AddMessage(TEXT("No bendable ground here"), WarningColor);
		return;
	}
	const BendingSim::FTechniqueTuning& Tuning = GetTuning();
	const FVector Center = GetAimGroundPoint(Tuning.EarthbendRangeCm, MinWallDistanceCm);
	const FVector Bender = GetOwner()->GetActorLocation();
	const FVector Facing = FVector(Bender.X - Center.X, Bender.Y - Center.Y, 0.0).GetSafeNormal();

	// The wall's soil comes out of the trenches beside it, raised to full height over WallRiseSeconds.
	FRisingWall& Wall = Walls.AddDefaulted_GetRef();
	BendingSim::EarthWallBrushes(BendingUnits::ToSim(Center), BendingUnits::ToSim(Facing), Tuning, Wall.Core, Wall.Trench);
	Wall.RemainingM3 = BendingUnits::CmToM(Tuning.WallHeightCm) * Terrain->GetBrushAreaM2(Wall.Core);
	Wall.RateM3S = Wall.RemainingM3 / FMath::Max(Tuning.WallRiseSeconds, 0.05);
}

void UBendingTechniqueComponent::TickWalls(float DeltaSeconds)
{
	BendingSim::FTerrain* Terrain = GetTerrain();
	for (int32 Index = Walls.Num() - 1; Index >= 0; --Index)
	{
		FRisingWall& Wall = Walls[Index];
		bool bDone = Terrain == nullptr;
		if (Terrain)
		{
			const double StepM3 = FMath::Min(Wall.RateM3S * DeltaSeconds, Wall.RemainingM3);
			const BendingSim::FTerrainEdit Edit = Terrain->MoveSoil(Wall.Trench, Wall.Core, StepM3);
			Wall.RemainingM3 -= StepM3;
			Wall.MovedKg += Edit.MassKg;
			const double WorkJ = FMath::Max(Edit.PotentialEnergyChangeJ, 0.0);
			Wall.WorkJ += WorkJ;
			const bool bPaid = PayForWork(WorkJ, EBendingEnergyKind::Kinetic);
			const bool bNoSoilLeft = StepM3 > 0.0 && Edit.VolumeM3 <= 1e-9;
			bDone = !bPaid || bNoSoilLeft || Wall.RemainingM3 <= 1e-6;
			if (bDone)
			{
				AddMessage(FString::Printf(TEXT("Wall raised: %.1f t of soil, %.0f kJ of work"), Wall.MovedKg / 1000.0, Wall.WorkJ / 1000.0), InfoColor);
			}
		}
		if (bDone)
		{
			Walls.RemoveAtSwap(Index);
		}
	}
}

// ---------------------------------------------------------------------------------------------------- Fire and air

bool UBendingTechniqueComponent::ShootFlame(double MassKg, double TemperatureK, double SpeedMs, bool bWithLight, double MinChiFraction)
{
	const FVector Origin = GetHandLocation();
	const FVector Direction = GetAimDirectionFrom(Origin);

	// The heat the flame carries above ambient is what the bender pays for.
	const BendingSim::FVolume Full = BendingSim::MakeFlame(MassKg, TemperatureK, BendingUnits::ToSim(Origin), BendingSim::FVec3());
	const double HeatJ = Full.GetHeatCapacityJPerK() * FMath::Max(TemperatureK - UBendingSettings::Get().AmbientTemperatureK, 0.0);
	const double GrantedJ = SpendEnergy(HeatJ, EBendingEnergyKind::Thermal);
	const double Fraction = HeatJ > 0.0 ? GrantedJ / HeatJ : 1.0;
	if (Fraction < MinChiFraction)
	{
		AddOutOfChiMessage();
		return false;
	}

	BendingSim::FVolume Flame = BendingSim::MakeFlame(MassKg * FMath::Min(Fraction, 1.0), TemperatureK, BendingUnits::ToSim(Origin),
		BendingUnits::ToSim(Direction * BendingUnits::MToCm(SpeedMs)));
	SpendEnergy(BendingSim::KineticEnergyJ(Flame.MassKg, SpeedMs), EBendingEnergyKind::Kinetic);
	Flame.LocationCm = BendingUnits::ToSim(Origin + Direction * (0.6 * Flame.RadiusCm));
	return ABendingProjectile::SpawnProjectile(GetWorld(), EBendingProjectileKind::Fire, Flame, GetOwner(), bWithLight) != nullptr;
}

bool UBendingTechniqueComponent::ShootAir(double RadiusCm, double Compression, double SpeedMs, const FVector& Origin, const FVector& Direction, double MinChiFraction)
{
	BendingSim::FVolume Air = BendingSim::MakeBentAir(RadiusCm, Compression, UBendingSettings::Get().AmbientAirDensityKgM3,
		BendingUnits::ToSim(Origin), BendingSim::FVec3());
	const double RequestedJ = BendingSim::KineticEnergyJ(Air.MassKg, SpeedMs);
	const double GrantedJ = SpendEnergy(RequestedJ, EBendingEnergyKind::Kinetic);
	if (GrantedJ < MinChiFraction * RequestedJ)
	{
		AddOutOfChiMessage();
		return false;
	}
	const double ActualSpeedMs = SpeedMs * BendingSim::KSqrt(GrantedJ / FMath::Max(RequestedJ, 1.0));
	Air.LocationCm = BendingUnits::ToSim(Origin + Direction * Air.RadiusCm);
	Air.VelocityCmS = BendingUnits::ToSim(Direction * BendingUnits::MToCm(ActualSpeedMs));
	return ABendingProjectile::SpawnProjectile(GetWorld(), EBendingProjectileKind::Air, Air, GetOwner(), false) != nullptr;
}

void UBendingTechniqueComponent::DoAirJump()
{
	ACharacter* Character = Cast<ACharacter>(GetOwner());
	if (!Character)
	{
		return;
	}
	const BendingSim::FTechniqueTuning& Tuning = GetTuning();
	const double GrantedJ = SpendEnergy(BendingSim::KineticEnergyJ(BenderMassKg, Tuning.AirJumpSpeedMs), EBendingEnergyKind::Kinetic);
	const double SpeedMs = BendingSim::KSqrt(2.0 * GrantedJ / BenderMassKg);
	if (SpeedMs < 0.5 * Tuning.AirJumpSpeedMs)
	{
		AddOutOfChiMessage();
	}
	Character->LaunchCharacter(FVector(0.0, 0.0, BendingUnits::MToCm(SpeedMs)), false, true);

	// The column of air that carries the bender is pushed down under the feet.
	const FVector Feet = Character->GetActorLocation() - FVector(0.0, 0.0, Character->GetSimpleCollisionHalfHeight() - 30.0);
	const BendingSim::FVolume Puff = BendingSim::MakeBentAir(AirJumpPuffRadiusCm, AirJumpPuffCompression, UBendingSettings::Get().AmbientAirDensityKgM3,
		BendingUnits::ToSim(Feet), BendingSim::FVec3(0.0, 0.0, -AirJumpPuffSpeedCmS));
	ABendingProjectile::SpawnProjectile(GetWorld(), EBendingProjectileKind::Air, Puff, Character, false);
}

// ---------------------------------------------------------------------------------------------------- Signature techniques

void UBendingTechniqueComponent::ThrowIceDaggers()
{
	ABendingWaterWhipActor* WhipActor = Whip.Get();
	if (!WhipActor)
	{
		AddMessage(TEXT("Ice Daggers need water: draw a whip near a pond first (LMB)"), WarningColor);
		return;
	}
	if (WhipActor->IsAnyFrozen())
	{
		AddMessage(TEXT("Thaw the whip first (RMB)"), WarningColor);
		return;
	}

	// Water bent off the end of the whip and frozen into daggers: the cold is billed like any freezing.
	const BendingSim::FTechniqueTuning& Tuning = GetTuning();
	double WaterTemperatureK = UBendingSettings::Get().AmbientTemperatureK;
	const double TakenKg = WhipActor->TakeWater(Tuning.IceDaggerCount * Tuning.IceDaggerMassKg, WaterTemperatureK);
	if (TakenKg < 0.5 * Tuning.IceDaggerMassKg)
	{
		AddMessage(TEXT("Too little water left in the whip"), WarningColor);
		return;
	}
	const double NeedJ = BendingSim::HeatToMakeIce(TakenKg, WaterTemperatureK, IceDaggerTemperatureK);
	const double GrantedJ = SpendEnergy(NeedJ, EBendingEnergyKind::Thermal);
	const int32 MaxCount = FMath::Max(Tuning.IceDaggerCount, 1);
	const int32 Count = FMath::Clamp(FMath::RoundToInt(static_cast<float>(MaxCount * GrantedJ / FMath::Max(NeedJ, 1.0))), 1, MaxCount);
	const double ShardKg = TakenKg / Count;
	SpendEnergy(BendingSim::KineticEnergyJ(TakenKg, Tuning.IceDaggerSpeedMs), EBendingEnergyKind::Kinetic);

	// A tight fan: one straight at the aim, the rest just around it.
	const FVector Origin = GetHandLocation();
	const FVector Aim = GetAimDirectionFrom(Origin);
	FVector Side = FVector::CrossProduct(Aim, FVector::UpVector);
	Side = Side.Size() > 0.1 ? Side.GetSafeNormal() : FVector::RightVector;
	const FVector Lift = FVector::CrossProduct(Side, Aim).GetSafeNormal();
	const double Spread = FMath::DegreesToRadians(Tuning.IceDaggerSpreadDeg);
	static constexpr double Offsets[5][2] = { { 0.0, 0.0 }, { 1.0, 0.25 }, { -1.0, 0.25 }, { 0.5, -0.7 }, { -0.5, -0.7 } };
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const double* Offset = Offsets[Index % 5];
		const FVector Direction = (Aim + Side * (Offset[0] * Spread) + Lift * (Offset[1] * Spread)).GetSafeNormal();
		const FVector Location = Origin + Direction * 25.0 + Side * (Offset[0] * 12.0) + Lift * (Offset[1] * 12.0);
		const BendingSim::FVolume Shard = BendingSim::MakeIceShard(ShardKg, BendingUnits::ToSim(Location),
			BendingUnits::ToSim(Direction * BendingUnits::MToCm(Tuning.IceDaggerSpeedMs)));
		ABendingProjectile::SpawnProjectile(GetWorld(), EBendingProjectileKind::IceShard, Shard, GetOwner(), false);
	}
	AddMessage(FString::Printf(TEXT("Ice daggers: %d shards, %.0f kJ of cold"), Count, GrantedJ / 1000.0), InfoColor);
}

void UBendingTechniqueComponent::DoEarthquake()
{
	// A stomp: the ground shakes outward. Each loose body within reach gets kinetic energy that falls off with
	// distance; equal energy moves a heavy body less (dv = sqrt(2 E / m)).
	const BendingSim::FTechniqueTuning& Tuning = GetTuning();
	const FVector Feet = GetFeetLocation();
	const double Radius = FMath::Max(Tuning.EarthquakeRadiusCm, 1.0);
	TArray<TPair<TWeakObjectPtr<ABendingPropActor>, double>> Shares;
	double RequestedJ = Tuning.EarthquakeGroundJ;
	for (TActorIterator<ABendingPropActor> It(GetWorld()); It; ++It)
	{
		ABendingPropActor* Prop = *It;
		if (!Prop || !Prop->IsLoose())
		{
			continue;
		}
		const FVector Offset = Prop->GetActorLocation() - Feet;
		const double Distance = FVector(Offset.X, Offset.Y, 0.0).Size();
		if (Distance < Radius)
		{
			const double Falloff = 1.0 - Distance / Radius;
			const double ShareJ = Tuning.EarthquakeEnergyJ * Falloff * Falloff;
			Shares.Emplace(Prop, ShareJ);
			RequestedJ += ShareJ;
		}
	}
	const double Scale = SpendEnergy(RequestedJ, EBendingEnergyKind::Kinetic) / FMath::Max(RequestedJ, 1.0);
	int32 Thrown = 0;
	for (const TPair<TWeakObjectPtr<ABendingPropActor>, double>& Share : Shares)
	{
		ABendingPropActor* Prop = Share.Key.Get();
		if (!Prop)
		{
			continue;
		}
		FVector Out = Prop->GetActorLocation() - Feet;
		Out.Z = 0.0;
		Out = Out.Size() > 1.0 ? Out.GetSafeNormal() : GetFlatAimDirection();
		const double SpeedCmS = FMath::Min(BendingUnits::MToCm(BendingSim::KSqrt(2.0 * Share.Value * Scale / FMath::Max(Prop->GetMassKg(), 0.1))),
			BendingUnits::MToCm(Tuning.EarthquakeMaxSpeedMs));
		Prop->AddVelocity((Out * 0.75 + FVector(0.0, 0.0, 0.66)).GetSafeNormal() * SpeedCmS);
		++Thrown;
	}

	FActorSpawnParameters Params;
	Params.Owner = GetOwner();
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	if (ABendingQuakeActor* Quake = GetWorld()->SpawnActor<ABendingQuakeActor>(ABendingQuakeActor::StaticClass(), FTransform(Feet), Params))
	{
		Quake->InitQuake(Feet);
	}
	AddMessage(FString::Printf(TEXT("Earthquake: %d thing%s thrown"), Thrown, Thrown == 1 ? TEXT("") : TEXT("s")), InfoColor);
}

void UBendingTechniqueComponent::DoFireRing()
{
	// A spin kick throws flame out in every direction: the heat of every flame is paid for, then its motion.
	const BendingSim::FTechniqueTuning& Tuning = GetTuning();
	const int32 Count = FMath::Max(Tuning.FireRingCount, 1);
	const double Ambient = UBendingSettings::Get().AmbientTemperatureK;
	const BendingSim::FVolume Sample = BendingSim::MakeFlame(Tuning.FireRingMassKg, Tuning.FireRingTemperatureK, BendingSim::FVec3(), BendingSim::FVec3());
	const double HeatEachJ = Sample.GetHeatCapacityJPerK() * FMath::Max(Tuning.FireRingTemperatureK - Ambient, 0.0);
	const double GrantedJ = SpendEnergy(HeatEachJ * Count, EBendingEnergyKind::Thermal);
	const double MassKg = Tuning.FireRingMassKg * GrantedJ / FMath::Max(HeatEachJ * Count, 1.0);
	if (MassKg < 0.02)
	{
		AddOutOfChiMessage();
		return;
	}
	SpendEnergy(Count * BendingSim::KineticEnergyJ(MassKg, Tuning.FireRingSpeedMs), EBendingEnergyKind::Kinetic);
	const FVector Center = GetFeetLocation() + FVector(0.0, 0.0, 75.0);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const double Angle = 2.0 * UE_DOUBLE_PI * Index / Count;
		const FVector Out(FMath::Cos(Angle), FMath::Sin(Angle), 0.03);
		const BendingSim::FVolume Flame = BendingSim::MakeFlame(MassKg, Tuning.FireRingTemperatureK, BendingUnits::ToSim(Center + Out * 60.0),
			BendingUnits::ToSim(Out * BendingUnits::MToCm(Tuning.FireRingSpeedMs)));
		// A light on every fourth flame is enough to light the ring.
		ABendingProjectile::SpawnProjectile(GetWorld(), EBendingProjectileKind::Fire, Flame, GetOwner(), Index % 4 == 0);
	}
	AddMessage(FString::Printf(TEXT("Ring of Fire: %d flames, %.1f MJ"), Count, GrantedJ / 1.0e6), InfoColor);
}

void UBendingTechniqueComponent::DoFireJet()
{
	ACharacter* Character = Cast<ACharacter>(GetOwner());
	if (!Character)
	{
		return;
	}
	// Fire jets from the feet: the bender's own kinetic energy is the work.
	const BendingSim::FTechniqueTuning& Tuning = GetTuning();
	const double RequestedJ = BendingSim::KineticEnergyJ(BenderMassKg, Tuning.FireJetSpeedMs);
	const double GrantedJ = SpendEnergy(RequestedJ, EBendingEnergyKind::Kinetic);
	const double SpeedMs = Tuning.FireJetSpeedMs * BendingSim::KSqrt(GrantedJ / FMath::Max(RequestedJ, 1.0));
	if (SpeedMs < 0.5 * Tuning.FireJetSpeedMs)
	{
		AddOutOfChiMessage();
	}
	DashVelocityCmS = GetFlatAimDirection() * BendingUnits::MToCm(SpeedMs);
	DashRemainingS = Tuning.FireJetSeconds;
	DashEmitTimerS = 0.0;
	Character->LaunchCharacter(DashVelocityCmS + FVector(0.0, 0.0, BendingUnits::MToCm(Tuning.FireJetLiftMs)), true, true);
}

void UBendingTechniqueComponent::TickFireJet(float DeltaSeconds)
{
	if (DashRemainingS <= 0.0)
	{
		return;
	}
	DashRemainingS -= DeltaSeconds;
	ACharacter* Character = Cast<ACharacter>(GetOwner());
	if (UCharacterMovementComponent* Movement = Character ? Character->GetCharacterMovement() : nullptr)
	{
		// The jets hold the dash speed while they burn.
		Movement->Velocity.X = DashVelocityCmS.X;
		Movement->Velocity.Y = DashVelocityCmS.Y;
	}

	// Each puff of flame from the feet is paid for as heat.
	DashEmitTimerS -= DeltaSeconds;
	if (DashEmitTimerS > 0.0)
	{
		return;
	}
	DashEmitTimerS = JetFlameIntervalS;
	const BendingSim::FTechniqueTuning& Tuning = GetTuning();
	const FVector Back = -DashVelocityCmS.GetSafeNormal();
	const FVector Feet = GetFeetLocation() + FVector(0.0, 0.0, 20.0);
	BendingSim::FVolume Flame = BendingSim::MakeFlame(Tuning.FireJetFlameMassKg, JetFlameTemperatureK, BendingUnits::ToSim(Feet),
		BendingUnits::ToSim(Back * 400.0 - FVector(0.0, 0.0, 150.0)));
	const double HeatJ = Flame.GetHeatCapacityJPerK() * FMath::Max(JetFlameTemperatureK - UBendingSettings::Get().AmbientTemperatureK, 0.0);
	const double GrantedJ = SpendEnergy(HeatJ, EBendingEnergyKind::Thermal);
	if (GrantedJ < 0.2 * HeatJ)
	{
		return;
	}
	Flame = BendingSim::MakeFlame(Tuning.FireJetFlameMassKg * GrantedJ / FMath::Max(HeatJ, 1.0), JetFlameTemperatureK, Flame.LocationCm, Flame.VelocityCmS);
	ABendingProjectile::SpawnProjectile(GetWorld(), EBendingProjectileKind::Fire, Flame, GetOwner(), false);
}

void UBendingTechniqueComponent::SpawnTornado()
{
	const BendingSim::FTechniqueTuning& Tuning = GetTuning();
	const double GrantedJ = SpendEnergy(Tuning.TornadoEnergyJ, EBendingEnergyKind::Kinetic);
	if (GrantedJ < 0.5 * Tuning.TornadoEnergyJ)
	{
		AddMessage(TEXT("Not enough chi to spin up a tornado"), WarningColor);
		return;
	}
	// One at a time: a new one replaces the last.
	if (ABendingTornadoActor* Previous = Tornado.Get())
	{
		Previous->Destroy();
	}
	const FVector Spot = GetAimGroundPoint(Tuning.TornadoRangeCm, 0.0);
	FActorSpawnParameters Params;
	Params.Owner = GetOwner();
	Params.Instigator = Cast<APawn>(GetOwner());
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ABendingTornadoActor* NewTornado = GetWorld()->SpawnActor<ABendingTornadoActor>(ABendingTornadoActor::StaticClass(), FTransform(Spot), Params);
	if (!NewTornado)
	{
		return;
	}
	NewTornado->InitTornado(Spot, Tuning.TornadoSeconds * GrantedJ / Tuning.TornadoEnergyJ);
	Tornado = NewTornado;
	AddMessage(TEXT("Tornado! Throw fire into it for a fire tornado"), InfoColor);
}

void UBendingTechniqueComponent::SetAirScooter(bool bRiding)
{
	bAirScooter = bRiding;
	AActor* Owner = GetOwner();
	if (bRiding && !ScooterBall && Owner && Owner->GetRootComponent())
	{
		ScooterBall = NewObject<UStaticMeshComponent>(Owner, TEXT("AirScooterBall"));
		ScooterBall->SetStaticMesh(UBendingSandboxLibrary::LoadBasicShape(TEXT("Sphere")));
		ScooterBall->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
		ScooterBall->SetGenerateOverlapEvents(false);
		ScooterBall->SetCanEverAffectNavigation(false);
		ScooterBall->SetCastShadow(false);
		ScooterBall->SetupAttachment(Owner->GetRootComponent());
		ScooterBall->RegisterComponent();
		UBendingSandboxLibrary::SetMeshColor(ScooterBall, UBendingSandboxLibrary::FromSRGB(222, 238, 255));
	}
	if (ScooterBall)
	{
		// The ball spins under the feet; the body perches on top of it (AAvatarCharacter lifts it).
		const double HalfHeight = Owner ? Owner->GetSimpleCollisionHalfHeight() : 90.0;
		ScooterBall->SetRelativeLocation(FVector(0.0, 0.0, -HalfHeight + ScooterBallRadiusCm - 15.0));
		ScooterBall->SetRelativeScale3D(FVector(ScooterBallRadiusCm / 50.0));
		ScooterBall->SetVisibility(bRiding);
	}
}

float UBendingTechniqueComponent::GetAirScooterSpeedCmS() const
{
	return static_cast<float>(BendingUnits::MToCm(GetTuning().AirScooterSpeedMs));
}

FVector UBendingTechniqueComponent::GetFeetLocation() const
{
	const AActor* Owner = GetOwner();
	return Owner->GetActorLocation() - FVector(0.0, 0.0, Owner->GetSimpleCollisionHalfHeight());
}

// ---------------------------------------------------------------------------------------------------- Held techniques

void UBendingTechniqueComponent::StartHold(const UBendingTechniqueMove* Move)
{
	Holds.RemoveAll([Move](const FTechniqueHold& Existing) { return Existing.Technique == Move->Technique; });

	FTechniqueHold Hold;
	Hold.Technique = Move->Technique;
	Hold.InputTag = Move->InputTag;
	Hold.StartTimeSeconds = GetWorldTime();
	Hold.MinSeconds = Move->FrameData.GetPhaseSeconds(EBendingPhase::Active);

	const BendingSim::FTechniqueTuning& Tuning = GetTuning();
	switch (Move->Technique)
	{
	case EBendingTechnique::RaiseGround:
	case EBendingTechnique::LowerGround:
		// Fixed for the whole hold: re-aiming every frame, the rising pillar would catch the aim ray and walk the
		// centre toward the bender, smearing it into a low mound.
		Hold.GroundCenter = GetAimGroundPoint(Tuning.EarthbendRangeCm, 0.0);
		break;

	case EBendingTechnique::FlameStream:
		// The first puff leaves on the active frame.
		if (!ShootFlame(Tuning.FlameStreamMassKg, Tuning.FlameStreamTemperatureK, Tuning.FlameStreamSpeedMs, false, StreamMinChiFraction))
		{
			return;
		}
		break;

	case EBendingTechnique::AirGust:
	{
		const FVector Origin = GetHandLocation();
		if (!ShootAir(Tuning.GustRadiusCm, Tuning.GustCompression, Tuning.GustSpeedMs, Origin, GetAimDirectionFrom(Origin), StreamMinChiFraction))
		{
			return;
		}
		break;
	}

	case EBendingTechnique::AirScooter:
		SetAirScooter(true);
		AddMessage(TEXT("Air scooter: hold Q to ride"), InfoColor);
		break;

	case EBendingTechnique::GroundFlame:
	{
		const FVector Spot = GetAimGroundPoint(Tuning.EarthbendRangeCm, 0.0);
		BendingSim::FVolume Flame = BendingSim::MakeFlame(Tuning.GroundFlameMassKg, Tuning.GroundFlameTemperatureK, BendingUnits::ToSim(Spot), BendingSim::FVec3());
		const double HeatJ = Flame.GetHeatCapacityJPerK() * FMath::Max(Tuning.GroundFlameTemperatureK - UBendingSettings::Get().AmbientTemperatureK, 0.0);
		if (SpendEnergy(HeatJ, EBendingEnergyKind::Thermal) < 0.5 * HeatJ)
		{
			AddOutOfChiMessage();
			return;
		}
		Flame.LocationCm.Z = Spot.Z + 0.55 * Flame.RadiusCm;
		ABendingProjectile* Projectile = ABendingProjectile::SpawnProjectile(GetWorld(), EBendingProjectileKind::Fire, Flame, GetOwner(), true);
		if (!Projectile)
		{
			return;
		}
		Projectile->SetGrounded(true);
		Projectile->KeepAlive(Tuning.ProjectileLifetimeS);
		Hold.Flame = Projectile;
		break;
	}

	default:
		break;
	}
	Holds.Add(Hold);
}

void UBendingTechniqueComponent::TickHolds(float DeltaSeconds)
{
	const UBendingComponent* Bending = GetBendingComponent();
	const double Now = GetWorldTime();
	for (int32 Index = Holds.Num() - 1; Index >= 0; --Index)
	{
		// Knocked down, the bender lets go of everything.
		const bool bHeld = DownS <= 0.0 && Bending && Bending->IsInputHeld(Holds[Index].InputTag);
		const bool bTap = DownS <= 0.0 && Now - Holds[Index].StartTimeSeconds < Holds[Index].MinSeconds;
		if ((bHeld || bTap) && SustainHold(Holds[Index], DeltaSeconds))
		{
			continue;
		}
		const FTechniqueHold Ended = Holds[Index];
		Holds.RemoveAt(Index);
		EndHold(Ended);
	}
}

bool UBendingTechniqueComponent::SustainHold(FTechniqueHold& Hold, float DeltaSeconds)
{
	const BendingSim::FTechniqueTuning& Tuning = GetTuning();
	switch (Hold.Technique)
	{
	case EBendingTechnique::RaiseGround:
	case EBendingTechnique::LowerGround:
		return SustainTerraform(Hold, DeltaSeconds);

	case EBendingTechnique::GroundFlame:
		return SustainGroundFlame(Hold, DeltaSeconds);

	case EBendingTechnique::AirScooter:
	{
		// Keeping the ball spinning: the drag work at speed, scaled up (a spinning ball, not a sail), plus a little
		// to hover.
		const double SpeedMs = BendingUnits::CmToM(GetOwner()->GetVelocity().Size2D());
		const double DragW = 0.5 * UBendingSettings::Get().AmbientAirDensityKgM3 * 0.8 * 0.6 * SpeedMs * SpeedMs * SpeedMs;
		return PayForWork((DragW * Tuning.AirScooterUpkeepScale + 2500.0) * DeltaSeconds, EBendingEnergyKind::Kinetic);
	}

	case EBendingTechnique::FlameStream:
	case EBendingTechnique::AirGust:
	{
		const bool bFire = Hold.Technique == EBendingTechnique::FlameStream;
		const double RateHz = bFire ? Tuning.FlameStreamRateHz : Tuning.GustRateHz;
		Hold.EmitAccumulator = FMath::Min(Hold.EmitAccumulator + DeltaSeconds * RateHz, MaxOwedPuffs);
		while (Hold.EmitAccumulator >= 1.0)
		{
			Hold.EmitAccumulator -= 1.0;
			const FVector Origin = GetHandLocation();
			const bool bEmitted = bFire
				? ShootFlame(Tuning.FlameStreamMassKg, Tuning.FlameStreamTemperatureK, Tuning.FlameStreamSpeedMs, false, StreamMinChiFraction)
				: ShootAir(Tuning.GustRadiusCm, Tuning.GustCompression, Tuning.GustSpeedMs, Origin, GetAimDirectionFrom(Origin), StreamMinChiFraction);
			if (!bEmitted)
			{
				return false;
			}
		}
		return true;
	}

	default:
		return false;
	}
}

bool UBendingTechniqueComponent::SustainTerraform(FTechniqueHold& Hold, float DeltaSeconds)
{
	BendingSim::FTerrain* Terrain = GetTerrain();
	if (!Terrain)
	{
		return false;
	}
	const BendingSim::FTechniqueTuning& Tuning = GetTuning();
	BendingSim::FTerrainBrush Target;
	BendingSim::FTerrainBrush Source;
	if (Hold.Technique == EBendingTechnique::RaiseGround)
	{
		BendingSim::RaiseGroundBrushes(BendingUnits::ToSim(Hold.GroundCenter), Tuning, Target, Source);
	}
	else
	{
		BendingSim::LowerGroundBrushes(BendingUnits::ToSim(Hold.GroundCenter), Tuning, Target, Source);
	}
	// Soil is moved, never made: the pillar's soil comes from the ring around it, the pit's goes to its rim.
	const BendingSim::FTerrainEdit Edit = Terrain->MoveSoil(Source, Target, Tuning.TerraformRateM3S * DeltaSeconds);
	Hold.MovedKg += Edit.MassKg;
	const double WorkJ = FMath::Max(Edit.PotentialEnergyChangeJ, 0.0);
	Hold.WorkJ += WorkJ;
	return PayForWork(WorkJ, EBendingEnergyKind::Kinetic);
}

bool UBendingTechniqueComponent::SustainGroundFlame(FTechniqueHold& Hold, float DeltaSeconds)
{
	ABendingProjectile* Flame = Hold.Flame.Get();
	if (!Flame)
	{
		return false;
	}
	const BendingSim::FTechniqueTuning& Tuning = GetTuning();
	const double AcceptedJ = Flame->AddHeat(Tuning.GroundFlamePowerW * DeltaSeconds);
	Flame->KeepAlive(Tuning.ProjectileLifetimeS);
	// The bender drags the flame along the ground toward the aim.
	Flame->DragAlongGround(GetAimGroundPoint(Tuning.EarthbendRangeCm, 0.0), GroundFlameFollowCmS * DeltaSeconds);
	return PayForWork(AcceptedJ, EBendingEnergyKind::Thermal);
}

void UBendingTechniqueComponent::EndHold(const FTechniqueHold& Hold)
{
	switch (Hold.Technique)
	{
	case EBendingTechnique::RaiseGround:
	case EBendingTechnique::LowerGround:
		if (Hold.MovedKg > 0.0)
		{
			AddMessage(FString::Printf(TEXT("%s %.1f t of soil for %.1f kJ"), Hold.Technique == EBendingTechnique::RaiseGround ? TEXT("Raised") : TEXT("Dug"),
				Hold.MovedKg / 1000.0, Hold.WorkJ / 1000.0), InfoColor);
		}
		break;

	case EBendingTechnique::AirScooter:
		SetAirScooter(false);
		break;

	case EBendingTechnique::GroundFlame:
		// No longer fed by the bender, the flame burns the ground it lit, then cools and goes out on its own.
		if (ABendingProjectile* Flame = Hold.Flame.Get())
		{
			const BendingSim::FTechniqueTuning& Tuning = GetTuning();
			Flame->SetFuel(Tuning.GroundFlameFuelJ, Tuning.GroundFlameFuelPowerW);
			Flame->KeepAlive(Tuning.ProjectileLifetimeS + Tuning.GroundFlameFuelJ / FMath::Max(Tuning.GroundFlameFuelPowerW, 1.0));
		}
		break;

	default:
		break;
	}
}

// ---------------------------------------------------------------------------------------------------- Per-frame

void UBendingTechniqueComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	UpdateGuard(DeltaTime);
	UpdateAim();
	UpdateHand(DeltaTime);
	TickFireJet(DeltaTime);
	TickHeldRock();
	TickWalls(DeltaTime);
	TickHolds(DeltaTime);
}

void UBendingTechniqueComponent::UpdateAim()
{
	const APawn* Pawn = Cast<APawn>(GetOwner());
	const UWorld* World = GetWorld();
	if (!Pawn || !World)
	{
		return;
	}

	FVector ViewLocation = FVector::ZeroVector;
	FRotator ViewRotation = FRotator::ZeroRotator;
	if (const AController* Controller = Pawn->GetController())
	{
		Controller->GetPlayerViewPoint(ViewLocation, ViewRotation);
	}
	else
	{
		Pawn->GetActorEyesViewPoint(ViewLocation, ViewRotation);
	}
	ViewDirection = ViewRotation.Vector();

	// Start level with the bender, not at the camera, so nothing between the camera and the bender takes the aim.
	const double SkipCm = FMath::Max(FVector::DotProduct(Pawn->GetActorLocation() - ViewLocation, ViewDirection), 0.0);
	const FVector Start = ViewLocation + ViewDirection * SkipCm;
	const FVector End = ViewLocation + ViewDirection * AimRangeCm;

	FCollisionQueryParams Params(SCENE_QUERY_STAT(BendingTechniqueAim), false, Pawn);
	if (const ABendingPropActor* Rock = HeldRock.Get())
	{
		Params.AddIgnoredActor(Rock);
	}
	if (const ABendingWaterWhipActor* WhipActor = Whip.Get())
	{
		Params.AddIgnoredActor(WhipActor);
	}
	FHitResult Hit;
	if (World->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params))
	{
		AimPoint = Hit.ImpactPoint;
		return;
	}

	// The ground's collision can lag a fresh edit by a few frames; the heightfield itself never does.
	if (const BendingSim::FTerrain* Terrain = GetTerrain())
	{
		BendingSim::FVec3 TerrainHit;
		if (Terrain->Raycast(BendingUnits::ToSim(Start), BendingUnits::ToSim(ViewDirection), AimRangeCm - SkipCm, TerrainHit))
		{
			AimPoint = BendingUnits::ToEngine(TerrainHit);
			return;
		}
	}
	AimPoint = ViewLocation + ViewDirection * FallbackAimDistanceCm;
}

void UBendingTechniqueComponent::UpdateHand(float DeltaSeconds)
{
	const FVector Location = GetHandLocation();
	if (bHasLastHandLocation && DeltaSeconds > UE_SMALL_NUMBER)
	{
		HandVelocityCmS = ((Location - LastHandLocation) / DeltaSeconds).GetClampedToMaxSize(MaxHandSpeedCmS);
	}
	LastHandLocation = Location;
	bHasLastHandLocation = true;
}

// ---------------------------------------------------------------------------------------------------- Queries

FVector UBendingTechniqueComponent::GetHandLocation() const
{
	if (const USceneComponent* HandComponent = Hand.Get())
	{
		return HandComponent->GetComponentLocation();
	}
	const AActor* Owner = GetOwner();
	return Owner ? Owner->GetActorLocation() + Owner->GetActorForwardVector() * 40.0 + FVector(0.0, 0.0, 40.0) : FVector::ZeroVector;
}

bool UBendingTechniqueComponent::HasWaterWhip() const
{
	return Whip.IsValid();
}

ABendingWaterWhipActor* UBendingTechniqueComponent::GetWaterWhip() const
{
	return Whip.Get();
}

bool UBendingTechniqueComponent::IsHoldingRock() const
{
	const ABendingPropActor* Rock = HeldRock.Get();
	return Rock && Rock->IsHeld();
}

EBendingTechnique UBendingTechniqueComponent::GetSustainedTechnique() const
{
	return Holds.Num() > 0 ? Holds.Last().Technique : EBendingTechnique::None;
}

FString UBendingTechniqueComponent::GetStatusText() const
{
	TArray<FString> Parts;
	if (const ABendingWaterWhipActor* WhipActor = Whip.Get())
	{
		const int32 Frozen = WhipActor->GetNumFrozenSegments();
		if (Frozen > 0)
		{
			Parts.Add(FString::Printf(TEXT("Water whip %.1f kg (%d of %d segments ice)"), WhipActor->GetMassKg(), Frozen, WhipActor->GetNumSegments()));
		}
		else
		{
			Parts.Add(FString::Printf(TEXT("Water whip %.1f kg%s"), WhipActor->GetMassKg(), WhipActor->IsForming() ? TEXT(" (drawing)") : TEXT("")));
		}
	}
	if (const ABendingPropActor* Rock = HeldRock.Get())
	{
		Parts.Add(FString::Printf(TEXT("Lifting %.0f kg of rock"), Rock->GetMassKg()));
	}
	for (const FTechniqueHold& Hold : Holds)
	{
		Parts.Add(FString::Printf(TEXT("Holding %s"), *BendingTechnique::GetDisplayName(Hold.Technique)));
	}
	if (Walls.Num() > 0)
	{
		Parts.Add(TEXT("Wall rising"));
	}
	if (const ABendingTornadoActor* TornadoActor = Tornado.Get())
	{
		Parts.Add(TornadoActor->GetFireKg() > 0.0 ? FString::Printf(TEXT("Fire tornado (%.1f kg of flame)"), TornadoActor->GetFireKg()) : FString(TEXT("Tornado spinning")));
	}
	// A waterbender needs a source: say how far the nearest water is.
	const UBendingComponent* Bending = GetBendingComponent();
	const ABendingSandboxArena* Arena = GetArena();
	if (!Whip.IsValid() && Bending && Arena && Bending->GetActiveElement() == EBendingElement::Water)
	{
		BendingSim::FVec3 Source;
		if (BendingSim::FindWaterSource(Arena->GetLayout(), BendingUnits::ToSim(GetHandLocation()), UE_BIG_NUMBER, Source) >= 0)
		{
			const double DistanceM = BendingUnits::CmToM(FVector::Dist(GetHandLocation(), BendingUnits::ToEngine(Source)));
			const double RangeM = BendingUnits::CmToM(GetTuning().WhipDrawRangeCm);
			Parts.Add(DistanceM <= RangeM
				? FString::Printf(TEXT("Water in reach (%.0f m): left-click to draw"), DistanceM)
				: FString::Printf(TEXT("No water in reach: nearest pond %.0f m away (%.0f m needed)"), DistanceM, RangeM));
		}
	}
	return FString::Join(Parts, TEXT("   |   "));
}

void UBendingTechniqueComponent::AddMessage(const FString& Text, const FLinearColor& Color)
{
	const double Now = GetWorldTime();
	if (Messages.Num() > 0 && Messages.Last().Text == Text)
	{
		Messages.Last().TimeSeconds = Now;
		return;
	}
	FBendingTechniqueMessage Message;
	Message.Text = Text;
	Message.Color = Color;
	Message.TimeSeconds = Now;
	Messages.Add(MoveTemp(Message));
	if (Messages.Num() > MaxMessages)
	{
		Messages.RemoveAt(0);
	}
	UE_LOG(LogBending, Verbose, TEXT("%s: %s"), *GetNameSafe(GetOwner()), *Text);
}

void UBendingTechniqueComponent::AddOutOfChiMessage()
{
	AddMessage(TEXT("Out of chi"), WarningColor);
}

// ---------------------------------------------------------------------------------------------------- Health, guard and parry

void UBendingTechniqueComponent::SetGuardHeld(bool bPressed)
{
	bGuardHeld = bPressed;
}

float UBendingTechniqueComponent::GetMoveSpeedScale() const
{
	return static_cast<float>((bGuarding ? GuardSpeedScale : 1.0) * (FlinchS > 0.0 ? 0.5 : 1.0));
}

void UBendingTechniqueComponent::UpdateGuard(float DeltaSeconds)
{
	FlinchS = FMath::Max(FlinchS - DeltaSeconds, 0.0);
	ProtectS = FMath::Max(ProtectS - DeltaSeconds, 0.0);
	GuardCooldownS = FMath::Max(GuardCooldownS - DeltaSeconds, 0.0);
	if (DownS > 0.0)
	{
		bGuarding = false;
		DownS -= DeltaSeconds;
		if (DownS <= 0.0)
		{
			// Back on their feet, whole, and safe for a moment.
			DownS = 0.0;
			Health = MaxHealth;
			ProtectS = GetUpProtectS;
			AddMessage(TEXT("Back on your feet"), InfoColor);
		}
		return;
	}

	// The guard goes up only between moves: not while casting, holding, riding or dashing, and not out of stamina.
	const UBendingComponent* Bending = GetBendingComponent();
	const bool bFree = !(Bending && Bending->IsMoveActive()) && Holds.Num() == 0 && !bAirScooter && DashRemainingS <= 0.0 && GetStamina() > 0.0;
	if (bGuardHeld && bFree && (bGuarding || GuardCooldownS <= 0.0))
	{
		GuardTimeS = bGuarding ? GuardTimeS + DeltaSeconds : 0.0;
		bGuarding = true;
	}
	else if (bGuarding)
	{
		bGuarding = false;
		GuardCooldownS = GuardRecastS;
	}

	// Out of a duel, health comes back.
	const ABendingSparringPartner* Partner = GetSparringPartner();
	if (!(Partner && Partner->IsDuelActive()))
	{
		Health = FMath::Min(Health + RestHealPerS * DeltaSeconds, MaxHealth);
	}
}

EBendingFlameHit UBendingTechniqueComponent::TakeFlameHit(const FVector& LocationCm, double Damage, const FVector& Direction)
{
	if (DownS > 0.0 || ProtectS > 0.0)
	{
		return EBendingFlameHit::Ignored;
	}
	// A guard only works facing the flame.
	const FVector Forward = GetOwner()->GetActorForwardVector();
	const FVector Facing = FVector(Forward.X, Forward.Y, 0.0).GetSafeNormal();
	const bool bFacing = FVector::DotProduct(Facing, -Direction) > 0.2;
	if (bGuarding && bFacing && GuardTimeS <= ParryWindowS)
	{
		// A perfect guard: the flame flies back at whoever sent it (ABendingProjectile turns it), and chi flows back.
		AddResources(ParryChi, 0.0);
		ABendingSparringPartner::OnSparringCallout.Broadcast(EBendingSparringCallout::Parried, LocationCm, 0.0);
		AddMessage(TEXT("Perfect guard! The blast flies back"), InfoColor);
		return EBendingFlameHit::Parried;
	}
	if (bGuarding && bFacing)
	{
		// Blocked: the flame splashes off the guard, a little gets through, and it costs stamina.
		const double Through = Damage * BlockDamageScale;
		AddResources(0.0, -BlockStamina);
		ABendingSparringPartner::OnSparringCallout.Broadcast(EBendingSparringCallout::Blocked, LocationCm, Through);
		HurtBender(Through, Direction, BlockedKnockbackScale * FlameKnockbackCmS, true);
		if (bGuarding && GetStamina() <= 0.0)
		{
			bGuarding = false;
			GuardCooldownS = GuardBreakCooldownS;
			FlinchS = GuardBreakFlinchS;
			AddMessage(TEXT("Guard broken: out of stamina"), WarningColor);
		}
		return EBendingFlameHit::Blocked;
	}
	// A clean hit: the flame bursts against them.
	HurtBender(Damage, Direction, FlameKnockbackCmS, false);
	return EBendingFlameHit::Hit;
}

void UBendingTechniqueComponent::HurtBender(double Damage, const FVector& Direction, double KnockbackCmS, bool bBlocked)
{
	if (DownS > 0.0 || ProtectS > 0.0 || Damage <= 0.0)
	{
		return;
	}
	AActor* Owner = GetOwner();
	ACharacter* Character = Cast<ACharacter>(Owner);
	const double Dealt = FMath::Min(Damage, Health);
	Health -= Dealt;
	const FVector Push = FVector(Direction.X, Direction.Y, 0.0).GetSafeNormal();
	if (Character && KnockbackCmS > 0.0 && !Push.IsNearlyZero())
	{
		Character->LaunchCharacter(Push * KnockbackCmS, false, false);
	}
	// A blocked hit is reported as the block; only a clean hit rocks them.
	if (!bBlocked)
	{
		FlinchS = FMath::Max(FlinchS, HitFlinchS);
		ABendingSparringPartner::OnSparringCallout.Broadcast(EBendingSparringCallout::PlayerHit, Owner->GetActorLocation() + FVector(0.0, 0.0, 50.0), Dealt);
	}
	if (Health > 1e-6)
	{
		return;
	}

	// Knocked down: flat on their back for a few seconds, then up again whole. Held techniques let go (TickHolds).
	Health = 0.0;
	DownS = PlayerDownSeconds;
	bGuarding = false;
	DashRemainingS = 0.0;
	if (Push.IsNearlyZero())
	{
		const FVector Forward = Owner->GetActorForwardVector();
		FallDirection = -FVector(Forward.X, Forward.Y, 0.0).GetSafeNormal();
	}
	else
	{
		FallDirection = Push;
	}
	if (Character)
	{
		Character->LaunchCharacter(FallDirection * KnockdownSpeedCmS + FVector(0.0, 0.0, KnockdownLiftCmS), true, true);
	}
	ABendingSparringPartner::OnSparringCallout.Broadcast(EBendingSparringCallout::PlayerDown, Owner->GetActorLocation(), 0.0);
	if (ABendingSparringPartner* Partner = GetSparringPartner(); Partner && Partner->IsDuelActive())
	{
		Partner->OnPlayerKnockedDown();
	}
}

void UBendingTechniqueComponent::AddResources(double ChiDelta, double StaminaDelta)
{
	// The same instant effect move costs use, so the attribute set clamps the result.
	UAbilitySystemComponent* OwnerAbilities = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(GetOwner());
	if (!OwnerAbilities || !GetOwner()->HasAuthority())
	{
		return;
	}
	FGameplayEffectContextHandle Context = OwnerAbilities->MakeEffectContext();
	Context.AddSourceObject(this);
	const FGameplayEffectSpecHandle Spec = OwnerAbilities->MakeOutgoingSpec(UBendingCostEffect::StaticClass(), 1.f, Context);
	if (!Spec.IsValid())
	{
		return;
	}
	Spec.Data->SetSetByCallerMagnitude(BendingTags::SetByCaller_Bending_Chi, static_cast<float>(ChiDelta));
	Spec.Data->SetSetByCallerMagnitude(BendingTags::SetByCaller_Bending_Stamina, static_cast<float>(StaminaDelta));
	OwnerAbilities->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
}

double UBendingTechniqueComponent::GetStamina() const
{
	const UAbilitySystemComponent* OwnerAbilities = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(GetOwner());
	bool bFound = false;
	const float StaminaLeft = OwnerAbilities ? OwnerAbilities->GetGameplayAttributeValue(UBendingAttributeSet::GetStaminaAttribute(), bFound) : 0.f;
	// No stamina pool on this owner: the guard never tires.
	return bFound ? static_cast<double>(StaminaLeft) : UE_BIG_NUMBER;
}

ABendingSparringPartner* UBendingTechniqueComponent::GetSparringPartner() const
{
	if (!CachedPartner.IsValid())
	{
		CachedPartner = ABendingSparringPartner::Find(GetWorld());
	}
	return CachedPartner.Get();
}

// ---------------------------------------------------------------------------------------------------- Helpers

UBendingComponent* UBendingTechniqueComponent::GetBendingComponent() const
{
	return CachedBending.Get();
}

ABendingSandboxArena* UBendingTechniqueComponent::GetArena() const
{
	if (!CachedArena.IsValid())
	{
		CachedArena = ABendingSandboxArena::Find(GetWorld());
	}
	return CachedArena.Get();
}

BendingSim::FTerrain* UBendingTechniqueComponent::GetTerrain() const
{
	const ABendingSandboxArena* Arena = GetArena();
	return Arena ? Arena->GetTerrain() : nullptr;
}

double UBendingTechniqueComponent::SpendEnergy(double RequestedJ, EBendingEnergyKind Kind)
{
	if (RequestedJ <= 0.0)
	{
		return 0.0;
	}
	UBendingComponent* Bending = GetBendingComponent();
	return Bending ? Bending->SpendChiForEnergy(RequestedJ, Kind) : RequestedJ;
}

bool UBendingTechniqueComponent::PayForWork(double RequestedJ, EBendingEnergyKind Kind)
{
	// A joule or less per frame is not worth a gameplay effect.
	if (RequestedJ <= 1.0)
	{
		return true;
	}
	if (SpendEnergy(RequestedJ, Kind) >= 0.5 * RequestedJ)
	{
		return true;
	}
	AddOutOfChiMessage();
	return false;
}

FVector UBendingTechniqueComponent::GetAimDirectionFrom(const FVector& Origin) const
{
	const FVector ToAim = AimPoint - Origin;
	return ToAim.SizeSquared() > FMath::Square(MinAimDistanceCm) ? ToAim.GetSafeNormal() : ViewDirection;
}

FVector UBendingTechniqueComponent::GetFlatAimDirection() const
{
	const AActor* Owner = GetOwner();
	FVector Flat = AimPoint - Owner->GetActorLocation();
	Flat.Z = 0.0;
	if (Flat.SizeSquared() < FMath::Square(100.0))
	{
		Flat = FVector(ViewDirection.X, ViewDirection.Y, 0.0);
	}
	if (Flat.SizeSquared() < UE_KINDA_SMALL_NUMBER)
	{
		Flat = FVector(Owner->GetActorForwardVector().X, Owner->GetActorForwardVector().Y, 0.0);
	}
	return Flat.GetSafeNormal();
}

FVector UBendingTechniqueComponent::GetAimGroundPoint(double MaxRangeCm, double MinRangeCm) const
{
	const FVector Bender = GetOwner()->GetActorLocation();
	const FVector Offset(AimPoint.X - Bender.X, AimPoint.Y - Bender.Y, 0.0);
	const double Distance = Offset.Size();
	const FVector Direction = Distance > 1.0 ? Offset / Distance : GetFlatAimDirection();
	FVector Point = Bender + Direction * FMath::Clamp(Distance, MinRangeCm, FMath::Max(MaxRangeCm, MinRangeCm));
	Point.Z = GetGroundHeight(Point);
	return Point;
}

double UBendingTechniqueComponent::GetGroundHeight(const FVector& LocationCm) const
{
	const ABendingSandboxArena* Arena = GetArena();
	return Arena ? Arena->GetGroundHeightAt(LocationCm) : LocationCm.Z;
}

double UBendingTechniqueComponent::GetGravityMs2() const
{
	const UWorld* World = GetWorld();
	return World ? -BendingUnits::CmToM(static_cast<double>(World->GetGravityZ())) : 9.80665;
}

double UBendingTechniqueComponent::GetWorldTime() const
{
	const UWorld* World = GetWorld();
	return World ? World->GetTimeSeconds() : 0.0;
}
