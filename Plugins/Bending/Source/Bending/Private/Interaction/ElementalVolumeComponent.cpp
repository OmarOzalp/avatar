#include "Interaction/ElementalVolumeComponent.h"

#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"
#include "Interaction/BendingInteractionSubsystem.h"
#include "Physics/BendingUnits.h"

UElementalVolumeComponent::UElementalVolumeComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryComponentTick.bCanEverTick = false;
	bAutoActivate = true;
	InitialState = FElementalVolumeState::MakeDefault(EElementalSubstance::Earth, 100.0);
}

void UElementalVolumeComponent::BeginPlay()
{
	Super::BeginPlay();
	if (IsActive())
	{
		RegisterVolume();
	}
}

void UElementalVolumeComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UnregisterVolume();
	Super::EndPlay(EndPlayReason);
}

void UElementalVolumeComponent::Activate(bool bReset)
{
	Super::Activate(bReset);
	if (IsActive() && HasBegunPlay())
	{
		if (bReset)
		{
			UnregisterVolume();
		}
		RegisterVolume();
	}
}

void UElementalVolumeComponent::Deactivate()
{
	Super::Deactivate();
	UnregisterVolume();
}

UBendingInteractionSubsystem* UElementalVolumeComponent::GetInteractionSubsystem() const
{
	const UWorld* World = GetWorld();
	return World ? World->GetSubsystem<UBendingInteractionSubsystem>() : nullptr;
}

UPrimitiveComponent* UElementalVolumeComponent::GetSimulatingParent() const
{
	UPrimitiveComponent* Parent = Cast<UPrimitiveComponent>(GetAttachParent());
	return Parent && Parent->IsSimulatingPhysics() ? Parent : nullptr;
}

FVector UElementalVolumeComponent::GetWorldCapsuleHalfAxis() const
{
	return GetComponentTransform().TransformVectorNoScale(InitialState.CapsuleHalfAxisCm);
}

void UElementalVolumeComponent::RegisterVolume()
{
	UBendingInteractionSubsystem* Subsystem = GetInteractionSubsystem();
	if (!Subsystem)
	{
		return;
	}
	if (Subsystem->IsValidVolume(Handle))
	{
		// A spent slot from a previous life of a pooled actor is replaced by a fresh one.
		if (!Subsystem->IsVolumeDepleted(Handle))
		{
			return;
		}
		UnregisterVolume();
	}

	FElementalVolumeState State = InitialState;
	State.LocationCm = GetComponentLocation();
	if (State.Shape == EElementalVolumeShape::Capsule)
	{
		State.CapsuleHalfAxisCm = GetWorldCapsuleHalfAxis();
	}
	if (bSyncMassWithAttachedBody)
	{
		if (const UPrimitiveComponent* Body = GetSimulatingParent())
		{
			State.MassKg = Body->GetMass();
		}
	}

	bHasLastLocation = false;
	Handle = Subsystem->RegisterVolume(State, this);
}

void UElementalVolumeComponent::UnregisterVolume()
{
	if (!Handle.IsSet())
	{
		return;
	}
	if (UBendingInteractionSubsystem* Subsystem = GetInteractionSubsystem())
	{
		Subsystem->UnregisterVolume(Handle);
	}
	Handle.Reset();
}

bool UElementalVolumeComponent::IsVolumeRegistered() const
{
	const UBendingInteractionSubsystem* Subsystem = GetInteractionSubsystem();
	return Subsystem && Subsystem->IsValidVolume(Handle);
}

bool UElementalVolumeComponent::GetSimulatedState(FElementalVolumeState& OutState) const
{
	const UBendingInteractionSubsystem* Subsystem = GetInteractionSubsystem();
	return Subsystem && Subsystem->GetVolumeState(Handle, OutState);
}

BendingSim::FVolume* UElementalVolumeComponent::GetMutableSimulatedState()
{
	UBendingInteractionSubsystem* Subsystem = GetInteractionSubsystem();
	return Subsystem ? Subsystem->GetVolume(Handle) : nullptr;
}

void UElementalVolumeComponent::SetManualVelocity(FVector VelocityCmS)
{
	ManualVelocityCmS = VelocityCmS;
}

FElementalPhaseChange UElementalVolumeComponent::AddHeat(double HeatJ, double& OutAcceptedHeatJ)
{
	OutAcceptedHeatJ = 0.0;
	UBendingInteractionSubsystem* Subsystem = GetInteractionSubsystem();
	return Subsystem ? Subsystem->TransferHeat(Handle, HeatJ, OutAcceptedHeatJ) : FElementalPhaseChange();
}

void UElementalVolumeComponent::PreSimulationSync(BendingSim::FVolume& Volume, double FrameSeconds)
{
	const FVector Location = GetComponentLocation();
	Volume.LocationCm = BendingUnits::ToSim(Location);
	if (Volume.Shape == BendingSim::EShape::Capsule)
	{
		Volume.CapsuleHalfAxisCm = BendingUnits::ToSim(GetWorldCapsuleHalfAxis());
	}

	FVector Velocity = FVector::ZeroVector;
	switch (VelocitySource)
	{
	case EElementalVelocitySource::AttachedPhysicsBody:
		if (UPrimitiveComponent* Body = GetSimulatingParent())
		{
			Velocity = Body->GetPhysicsLinearVelocityAtPoint(Location);
			break;
		}
		[[fallthrough]];
	case EElementalVelocitySource::FiniteDifference:
		Velocity = bHasLastLocation && FrameSeconds > UE_SMALL_NUMBER ? (Location - LastLocation) / FrameSeconds : FVector::ZeroVector;
		break;
	case EElementalVelocitySource::Manual:
		Velocity = ManualVelocityCmS;
		break;
	}
	Volume.VelocityCmS = BendingUnits::ToSim(Velocity);

	LastLocation = Location;
	bHasLastLocation = true;
}

void UElementalVolumeComponent::PostSimulationSync(const BendingSim::FVolume& Volume, const FVector& FrameImpulseKgCmS,
	EElementalSubstance PreviousSubstance, bool bNewlyDepleted)
{
	UPrimitiveComponent* Body = GetSimulatingParent();

	if (!FrameImpulseKgCmS.IsNearlyZero())
	{
		if (bApplyImpulsesToAttachedBody && Body)
		{
			Body->AddImpulseAtLocation(FrameImpulseKgCmS, BendingUnits::ToEngine(Volume.LocationCm));
		}
		OnImpulseReceived.Broadcast(FrameImpulseKgCmS);
	}

	if (bSyncMassWithAttachedBody && Body && Volume.MassKg > 0.0)
	{
		const double BodyMassKg = Body->GetMass();
		if (FMath::Abs(BodyMassKg - Volume.MassKg) > 0.005 * Volume.MassKg)
		{
			Body->SetMassOverrideInKg(NAME_None, static_cast<float>(Volume.MassKg), true);
		}
	}

	const EElementalSubstance CurrentSubstance = ElementalSubstance::FromSim(Volume.Substance);
	if (PreviousSubstance != CurrentSubstance)
	{
		OnSubstanceChanged.Broadcast(PreviousSubstance, CurrentSubstance);
	}
	if (bNewlyDepleted)
	{
		OnDepleted.Broadcast(this);
	}
}

#if WITH_EDITOR
void UElementalVolumeComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	// Picking a substance fills in its real-world defaults; mass and shape stay as authored.
	if (PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(FElementalVolumeState, Substance))
	{
		const FElementalVolumeState Defaults = FElementalVolumeState::MakeDefault(InitialState.Substance, InitialState.MassKg);
		InitialState.TemperatureK = Defaults.TemperatureK;
		InitialState.DragCoefficient = Defaults.DragCoefficient;
		InitialState.Porosity = Defaults.Porosity;
		InitialState.bDeriveRadiusFromMass = Defaults.bDeriveRadiusFromMass;
		InitialState.RadiusCm = Defaults.RadiusCm;
		InitialState.LatentHeatJ = 0.0;
		InitialState.SpecificHeatOverride = 0.0;
	}
}
#endif
