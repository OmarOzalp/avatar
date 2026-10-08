#include "Components/BendingComponent.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Abilities/BendingGameplayAbility.h"
#include "Abilities/BendingGameplayEffects.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimNotifyState_BendingPhase.h"
#include "Attributes/BendingAttributeSet.h"
#include "BendingGameplayTags.h"
#include "BendingLog.h"
#include "BendingSettings.h"
#include "CollisionQueryParams.h"
#include "CollisionShape.h"
#include "Components/SkeletalMeshComponent.h"
#include "Data/BendingDiscipline.h"
#include "Data/BendingMoveDefinition.h"
#include "Engine/AssetManager.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "MotionWarpingComponent.h"

namespace
{
	bool MontageHasBendingPhaseNotifies(const UAnimMontage* Montage)
	{
		if (!Montage)
		{
			return false;
		}
		for (const FAnimNotifyEvent& Notify : Montage->Notifies)
		{
			if (Cast<UAnimNotifyState_BendingPhase>(Notify.NotifyStateClass))
			{
				return true;
			}
		}
		return false;
	}

	EBendingPhase NextBendingPhase(EBendingPhase Phase, const FBendingFrameData& FrameData)
	{
		switch (Phase)
		{
		case EBendingPhase::Startup:  return EBendingPhase::Active;
		case EBendingPhase::Active:   return FrameData.RecoveryFrames > 0 ? EBendingPhase::Recovery : EBendingPhase::None;
		default:                      return EBendingPhase::None;
		}
	}
}

UBendingComponent::UBendingComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;

	ResourceRegenEffect = UBendingResourceRegenEffect::StaticClass();
}

UBendingComponent* UBendingComponent::FindBendingComponent(const AActor* Actor)
{
	return Actor ? Actor->FindComponentByClass<UBendingComponent>() : nullptr;
}

void UBendingComponent::BeginPlay()
{
	Super::BeginPlay();

	MotionWarping = GetOwner()->FindComponentByClass<UMotionWarpingComponent>();

	if (!AbilitySystem.IsValid())
	{
		if (UAbilitySystemComponent* OwnerAbilitySystem = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(GetOwner()))
		{
			InitializeWithAbilitySystem(OwnerAbilitySystem);
		}
		else
		{
			UE_LOG(LogBending, Warning, TEXT("%s: owner %s has no ability system; bending is disabled."), *GetName(), *GetNameSafe(GetOwner()));
		}
	}
}

void UBendingComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UAbilitySystemComponent* ASC = AbilitySystem.Get())
	{
		if (RegenEffectHandle.IsValid())
		{
			ASC->RemoveActiveGameplayEffect(RegenEffectHandle);
		}
	}
	for (const TSharedPtr<FStreamableHandle>& Handle : PreloadHandles)
	{
		if (Handle.IsValid())
		{
			Handle->ReleaseHandle();
		}
	}
	PreloadHandles.Reset();

	Super::EndPlay(EndPlayReason);
}

// ---------------------------------------------------------------------------------------------------- Setup

void UBendingComponent::InitializeWithAbilitySystem(UAbilitySystemComponent* InAbilitySystem)
{
	if (!InAbilitySystem || AbilitySystem.Get() == InAbilitySystem)
	{
		return;
	}
	AbilitySystem = InAbilitySystem;

	if (ResourceRegenEffect && GetOwner()->HasAuthority())
	{
		FGameplayEffectContextHandle Context = InAbilitySystem->MakeEffectContext();
		Context.AddSourceObject(this);
		const FGameplayEffectSpecHandle Spec = InAbilitySystem->MakeOutgoingSpec(ResourceRegenEffect, 1.f, Context);
		if (Spec.IsValid())
		{
			RegenEffectHandle = InAbilitySystem->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
		}
	}

	for (UBendingDiscipline* Discipline : DefaultDisciplines)
	{
		GrantDiscipline(Discipline);
	}

	if (ActiveElement == EBendingElement::None)
	{
		const EBendingElement Initial = DefaultElement != EBendingElement::None
			? DefaultElement
			: (GrantedDisciplines.Num() > 0 ? GrantedDisciplines[0]->Element : EBendingElement::None);
		SetActiveElement(Initial);
	}
}

void UBendingComponent::GrantDiscipline(UBendingDiscipline* Discipline)
{
	UAbilitySystemComponent* ASC = AbilitySystem.Get();
	if (!Discipline || !ASC || GrantedDisciplines.Contains(Discipline))
	{
		return;
	}
	if (!GetOwner()->HasAuthority())
	{
		UE_LOG(LogBending, Warning, TEXT("GrantDiscipline must run with authority (%s)."), *GetNameSafe(GetOwner()));
		return;
	}

	GrantedDisciplines.Add(Discipline);

	TArray<FSoftObjectPath> MontagesToPreload;
	for (UBendingMoveDefinition* Move : Discipline->Moves)
	{
		if (!Move || !Move->AbilityClass)
		{
			continue;
		}
		FBendingGrantedMove& Granted = GrantedMoves.AddDefaulted_GetRef();
		Granted.Move = Move;
		Granted.Discipline = Discipline;
		Granted.Handle = ASC->GiveAbility(FGameplayAbilitySpec(Move->AbilityClass, 1, INDEX_NONE, Move));

		if (!Move->Montage.IsNull())
		{
			MontagesToPreload.Add(Move->Montage.ToSoftObjectPath());
		}
	}

	// Highest priority first, so TryStartMove can take the first candidate that activates.
	GrantedMoves.StableSort([](const FBendingGrantedMove& A, const FBendingGrantedMove& B)
	{
		return (A.Move ? A.Move->Priority : 0) > (B.Move ? B.Move->Priority : 0);
	});

	if (MontagesToPreload.Num() > 0)
	{
		TSharedPtr<FStreamableHandle> Handle = UAssetManager::GetStreamableManager().RequestAsyncLoad(MoveTemp(MontagesToPreload), FStreamableDelegate());
		if (Handle.IsValid())
		{
			PreloadHandles.Add(Handle);
		}
	}

	if (ActiveElement == EBendingElement::None)
	{
		SetActiveElement(Discipline->Element);
	}
}

void UBendingComponent::RemoveDiscipline(UBendingDiscipline* Discipline)
{
	UAbilitySystemComponent* ASC = AbilitySystem.Get();
	if (!Discipline || !ASC || !GrantedDisciplines.Contains(Discipline))
	{
		return;
	}

	for (const FBendingGrantedMove& Granted : GrantedMoves)
	{
		if (Granted.Discipline == Discipline)
		{
			ASC->ClearAbility(Granted.Handle);
		}
	}
	GrantedMoves.RemoveAll([Discipline](const FBendingGrantedMove& Granted) { return Granted.Discipline == Discipline; });
	GrantedDisciplines.Remove(Discipline);

	if (ActiveElement == Discipline->Element && !HasDiscipline(ActiveElement))
	{
		SetActiveElement(GrantedDisciplines.Num() > 0 ? GrantedDisciplines[0]->Element : EBendingElement::None);
	}
}

bool UBendingComponent::HasDiscipline(EBendingElement Element) const
{
	return GetDiscipline(Element) != nullptr;
}

const UBendingDiscipline* UBendingComponent::GetDiscipline(EBendingElement Element) const
{
	for (const UBendingDiscipline* Discipline : GrantedDisciplines)
	{
		if (Discipline && Discipline->Element == Element)
		{
			return Discipline;
		}
	}
	return nullptr;
}

bool UBendingComponent::SetActiveElement(EBendingElement Element)
{
	if (Element == ActiveElement)
	{
		return true;
	}
	if (Element != EBendingElement::None && !HasDiscipline(Element))
	{
		return false;
	}
	const EBendingElement OldElement = ActiveElement;
	ActiveElement = Element;
	OnStanceChanged.Broadcast(OldElement, ActiveElement);
	return true;
}

// ---------------------------------------------------------------------------------------------------- Input

void UBendingComponent::HandleInputPressed(FGameplayTag InputTag)
{
	UAbilitySystemComponent* ASC = AbilitySystem.Get();
	if (!InputTag.IsValid() || !ASC)
	{
		return;
	}
	HeldInputs.Add(InputTag);

	// Running moves bound to this input see the press (follow-ups, re-press handling inside the move).
	for (const FBendingGrantedMove& Granted : GrantedMoves)
	{
		if (Granted.Move && Granted.Move->InputTag.MatchesTagExact(InputTag))
		{
			FGameplayAbilitySpec* Spec = ASC->FindAbilitySpecFromHandle(Granted.Handle);
			if (Spec && Spec->IsActive())
			{
				ASC->AbilitySpecInputPressed(*Spec);
			}
		}
	}

	if (CanStartNewMove() && TryStartMove(InputTag))
	{
		BufferedInput.Reset();
		RefreshTickEnabled();
		return;
	}

	// Locked into the current move: remember the press for the next cancel window.
	if (IsMoveActive())
	{
		BufferedInput = FBufferedInput{ InputTag, GetWorldTime() };
		RefreshTickEnabled();
	}
}

void UBendingComponent::HandleInputReleased(FGameplayTag InputTag)
{
	UAbilitySystemComponent* ASC = AbilitySystem.Get();
	if (!InputTag.IsValid() || !ASC)
	{
		return;
	}
	HeldInputs.Remove(InputTag);

	for (const FBendingGrantedMove& Granted : GrantedMoves)
	{
		if (Granted.Move && Granted.Move->InputTag.MatchesTagExact(InputTag))
		{
			FGameplayAbilitySpec* Spec = ASC->FindAbilitySpecFromHandle(Granted.Handle);
			if (Spec && Spec->IsActive())
			{
				ASC->AbilitySpecInputReleased(*Spec);
			}
		}
	}
}

bool UBendingComponent::IsInputHeld(FGameplayTag InputTag) const
{
	return HeldInputs.Contains(InputTag);
}

bool UBendingComponent::CanStartNewMove() const
{
	return !IsMoveActive() || IsInCancelWindow();
}

bool UBendingComponent::PassesPreActivationChecks(const FBendingGrantedMove& Granted) const
{
	UAbilitySystemComponent* ASC = AbilitySystem.Get();
	const FGameplayAbilitySpec* Spec = ASC ? ASC->FindAbilitySpecFromHandle(Granted.Handle) : nullptr;
	if (!Spec || !Spec->Ability)
	{
		return false;
	}
	const FGameplayAbilityActorInfo* ActorInfo = ASC->AbilityActorInfo.Get();
	return Spec->Ability->CheckCost(Spec->Handle, ActorInfo)
		&& Spec->Ability->CheckCooldown(Spec->Handle, ActorInfo)
		&& Spec->Ability->DoesAbilitySatisfyTagRequirements(*ASC);
}

bool UBendingComponent::TryStartMove(const FGameplayTag& InputTag)
{
	UAbilitySystemComponent* ASC = AbilitySystem.Get();
	if (!ASC)
	{
		return false;
	}

	for (const FBendingGrantedMove& Granted : GrantedMoves)
	{
		const UBendingMoveDefinition* Move = Granted.Move;
		if (!Move || !Move->InputTag.MatchesTagExact(InputTag))
		{
			continue;
		}
		if (Move->Element != EBendingElement::None && Move->Element != ActiveElement)
		{
			continue;
		}
		// Check cost, cooldown and tags first so a move that cannot start never cancels the one in progress
		// (otherwise pressing an unaffordable move would skip recovery frames for free).
		if (!PassesPreActivationChecks(Granted))
		{
			continue;
		}

		if (UBendingGameplayAbility* Current = CurrentAbility.Get(); Current && Current->IsActive())
		{
			ASC->CancelAbilityHandle(Current->GetCurrentAbilitySpecHandle());
		}
		if (ASC->TryActivateAbility(Granted.Handle))
		{
			return true;
		}
	}
	return false;
}

void UBendingComponent::ConsumeBufferedInput()
{
	if (!BufferedInput.IsSet())
	{
		return;
	}
	const double BufferSeconds = InputBufferFrames / FBendingFrameData::FramesPerSecond;
	if (GetWorldTime() - BufferedInput->TimeSeconds > BufferSeconds)
	{
		BufferedInput.Reset();
		return;
	}
	if (!CanStartNewMove())
	{
		return;
	}
	const FGameplayTag InputTag = BufferedInput->InputTag;
	BufferedInput.Reset();
	TryStartMove(InputTag);
}

// ---------------------------------------------------------------------------------------------------- Move state

bool UBendingComponent::IsMoveActive() const
{
	const UBendingGameplayAbility* Ability = CurrentAbility.Get();
	return Ability && Ability->IsActive();
}

bool UBendingComponent::IsInCancelWindow() const
{
	if (!IsMoveActive() || !CurrentMove)
	{
		return false;
	}
	if (bPhasesCompleted)
	{
		return true;
	}

	const FBendingFrameData& FrameData = CurrentMove->FrameData;
	if (bHitConfirmed && FrameData.bCancelOnHit && (CurrentPhase == EBendingPhase::Active || CurrentPhase == EBendingPhase::Recovery))
	{
		return true;
	}
	return CurrentPhase == EBendingPhase::Recovery
		&& FrameData.RecoveryCancelFrame < FrameData.RecoveryFrames
		&& GetCurrentPhaseFrame() >= FrameData.RecoveryCancelFrame;
}

int32 UBendingComponent::GetCurrentPhaseFrame() const
{
	return CurrentPhase == EBendingPhase::None ? 0 : FBendingFrameData::SecondsToFrames(GetWorldTime() - PhaseStartTime);
}

void UBendingComponent::OnMoveStarted(UBendingGameplayAbility* Ability, const UBendingMoveDefinition* Move, UAnimMontage* Montage)
{
	if (!Ability || !Move)
	{
		return;
	}

	CurrentAbility = Ability;
	CurrentMove = Move;
	CurrentMontage = Montage;
	CurrentPhase = EBendingPhase::None;
	bPhasesFromNotifies = MontageHasBendingPhaseNotifies(Montage);
	bPhasesCompleted = false;
	bHitConfirmed = false;
	BufferedInput.Reset();
	MoveStartLocation = GetOwner()->GetActorLocation();
	DesiredFacingYaw.Reset();

	if (Move->bUseMotionWarping)
	{
		AcquireSoftTarget();
	}
	UpdateWarpTarget();

	// Startup begins on activation; a Startup notify at frame 0 only re-syncs the clock and fits the play rate.
	EnterPhase(EBendingPhase::Startup);
	RefreshTickEnabled();
}

void UBendingComponent::OnMoveEnded(UBendingGameplayAbility* Ability, bool bWasCancelled)
{
	if (!Ability || CurrentAbility.Get() != Ability)
	{
		return;
	}

	const UBendingMoveDefinition* EndedMove = CurrentMove;
	if (UMotionWarpingComponent* Warp = MotionWarping.Get(); Warp && EndedMove)
	{
		Warp->RemoveWarpTarget(EndedMove->WarpTargetName);
	}

	SetLooseTag(BendingTags::GetPhaseStateTag(CurrentPhase), false);
	if (bCancelWindowTagApplied)
	{
		SetLooseTag(BendingTags::State_Bending_CancelWindow, false);
		bCancelWindowTagApplied = false;
	}

	const bool bWasMidMove = CurrentPhase != EBendingPhase::None;
	CurrentPhase = EBendingPhase::None;
	CurrentAbility.Reset();
	CurrentMove = nullptr;
	CurrentMontage.Reset();
	DesiredFacingYaw.Reset();
	bPhasesCompleted = false;
	bHitConfirmed = false;

	if (bWasMidMove)
	{
		OnPhaseChanged.Broadcast(this, EBendingPhase::None, EndedMove);
	}

	UE_LOG(LogBending, Verbose, TEXT("%s ended%s."), *GetNameSafe(EndedMove), bWasCancelled ? TEXT(" (cancelled)") : TEXT(""));

	// A buffered successor fires on the next tick, once the ending ability has fully released its spec.
	RefreshTickEnabled();
}

void UBendingComponent::NotifyPhaseBegin(EBendingPhase Phase, float SegmentSeconds, USkeletalMeshComponent* MeshComp, const UAnimSequenceBase* Animation)
{
	if (!IsMoveActive() || !CurrentMove || !IsCurrentMontage(Animation) || bPhasesCompleted)
	{
		return;
	}
	// Phases only move forward; a stray notify from a blending-out montage cannot rewind the move.
	if (static_cast<uint8>(Phase) < static_cast<uint8>(CurrentPhase))
	{
		return;
	}
	FitMontageToPhase(Phase, SegmentSeconds, MeshComp);
	EnterPhase(Phase);
}

void UBendingComponent::NotifyPhaseEnd(EBendingPhase Phase, USkeletalMeshComponent* MeshComp, const UAnimSequenceBase* Animation)
{
	if (!IsMoveActive() || !IsCurrentMontage(Animation) || Phase != CurrentPhase)
	{
		return;
	}
	if (Phase == EBendingPhase::Recovery)
	{
		ResetMontagePlayRate(MeshComp);
		EnterPhase(EBendingPhase::None);
	}
}

void UBendingComponent::EnterPhase(EBendingPhase NewPhase)
{
	if (NewPhase == CurrentPhase)
	{
		PhaseStartTime = GetWorldTime();
		return;
	}

	SetLooseTag(BendingTags::GetPhaseStateTag(CurrentPhase), false);
	CurrentPhase = NewPhase;
	PhaseStartTime = GetWorldTime();
	SetLooseTag(BendingTags::GetPhaseStateTag(NewPhase), true);
	if (NewPhase == EBendingPhase::None)
	{
		bPhasesCompleted = true;
	}
	UpdateCancelWindowTag();

	// Callbacks below may end the ability and clear CurrentMove; keep what we broadcast.
	const UBendingMoveDefinition* Move = CurrentMove;
	if (UAbilitySystemComponent* ASC = AbilitySystem.Get())
	{
		FGameplayEventData Payload;
		Payload.EventTag = BendingTags::GetPhaseEventTag(NewPhase);
		Payload.Instigator = GetOwner();
		Payload.Target = GetOwner();
		Payload.OptionalObject = Move;
		ASC->HandleGameplayEvent(Payload.EventTag, &Payload);
	}
	OnPhaseChanged.Broadcast(this, NewPhase, Move);
}

void UBendingComponent::AdvanceTimedPhases()
{
	// A long frame can cross several phase boundaries; carry the remainder so timing never drifts.
	for (int32 Guard = 0; Guard < 3; ++Guard)
	{
		if (!CurrentMove || CurrentPhase == EBendingPhase::None || bPhasesFromNotifies)
		{
			return;
		}
		const FBendingFrameData& FrameData = CurrentMove->FrameData;
		const double PhaseStart = PhaseStartTime;
		const double PhaseDuration = FrameData.GetPhaseSeconds(CurrentPhase);
		if (GetWorldTime() - PhaseStart < PhaseDuration)
		{
			return;
		}
		EnterPhase(NextBendingPhase(CurrentPhase, FrameData));
		PhaseStartTime = PhaseStart + PhaseDuration;
	}
}

void UBendingComponent::NotifyMoveHit(AActor* Victim)
{
	if (!IsMoveActive())
	{
		return;
	}
	bHitConfirmed = true;
	UpdateCancelWindowTag();

	const UBendingMoveDefinition* Move = CurrentMove;
	if (UAbilitySystemComponent* ASC = AbilitySystem.Get())
	{
		FGameplayEventData Payload;
		Payload.EventTag = BendingTags::Event_Bending_Hit;
		Payload.Instigator = GetOwner();
		Payload.Target = Victim;
		Payload.OptionalObject = Move;
		ASC->HandleGameplayEvent(Payload.EventTag, &Payload);
	}
	OnMoveHit.Broadcast(Move, Victim);
	RefreshTickEnabled();
}

void UBendingComponent::UpdateCancelWindowTag()
{
	const bool bOpen = IsInCancelWindow();
	if (bOpen != bCancelWindowTagApplied)
	{
		SetLooseTag(BendingTags::State_Bending_CancelWindow, bOpen);
		bCancelWindowTagApplied = bOpen;
	}
}

// ---------------------------------------------------------------------------------------------------- Animation

bool UBendingComponent::IsCurrentMontage(const UAnimSequenceBase* Animation) const
{
	// Bending Phase notifies belong on the montage itself, so Animation is the montage.
	return Animation && Animation == CurrentMontage.Get();
}

void UBendingComponent::FitMontageToPhase(EBendingPhase Phase, float SegmentSeconds, USkeletalMeshComponent* MeshComp) const
{
	if (!CurrentMove || !CurrentMove->FrameData.bFitAnimationToFrameData || SegmentSeconds <= UE_KINDA_SMALL_NUMBER)
	{
		return;
	}
	const double DesiredSeconds = CurrentMove->FrameData.GetPhaseSeconds(Phase);
	UAnimInstance* AnimInstance = MeshComp ? MeshComp->GetAnimInstance() : nullptr;
	UAnimMontage* Montage = CurrentMontage.Get();
	if (DesiredSeconds <= 0.0 || !AnimInstance || !Montage)
	{
		return;
	}
	// Play the authored segment in exactly the designed frame count. Root motion keeps its distance
	// and changes speed, which is what a faster or slower strike should do.
	const float PlayRate = static_cast<float>(FMath::Clamp(SegmentSeconds / DesiredSeconds, 0.2, 5.0));
	AnimInstance->Montage_SetPlayRate(Montage, PlayRate);
}

void UBendingComponent::ResetMontagePlayRate(USkeletalMeshComponent* MeshComp) const
{
	UAnimInstance* AnimInstance = MeshComp ? MeshComp->GetAnimInstance() : nullptr;
	if (AnimInstance && CurrentMontage.IsValid() && CurrentMove && CurrentMove->FrameData.bFitAnimationToFrameData)
	{
		AnimInstance->Montage_SetPlayRate(CurrentMontage.Get(), 1.f);
	}
}

// ---------------------------------------------------------------------------------------------------- Targeting

FVector UBendingComponent::GetAimDirection() const
{
	const AActor* Owner = GetOwner();
	if (const APawn* Pawn = Cast<APawn>(Owner); Pawn && Pawn->GetController())
	{
		const FRotator ControlRotation = Pawn->GetControlRotation();
		return FRotator(0.0, ControlRotation.Yaw, 0.0).Vector();
	}
	if (Owner)
	{
		return FRotator(0.0, Owner->GetActorRotation().Yaw, 0.0).Vector();
	}
	return FVector::ForwardVector;
}

AActor* UBendingComponent::AcquireSoftTarget()
{
	SoftTarget.Reset();

	const AActor* Owner = GetOwner();
	const UWorld* World = GetWorld();
	if (!Owner || !World || SoftTargetRangeCm <= 0.f)
	{
		return nullptr;
	}

	const FVector Origin = Owner->GetActorLocation();
	TArray<FOverlapResult> Overlaps;
	const FCollisionQueryParams Params(SCENE_QUERY_STAT(BendingSoftTarget), false, Owner);
	World->OverlapMultiByObjectType(Overlaps, Origin, FQuat::Identity, FCollisionObjectQueryParams(SoftTargetChannel.GetValue()),
		FCollisionShape::MakeSphere(SoftTargetRangeCm), Params);

	const FVector Aim = GetAimDirection();
	const double CosLimit = FMath::Cos(FMath::DegreesToRadians(static_cast<double>(SoftTargetMaxAngleDeg)));
	double BestScore = TNumericLimits<double>::Max();
	AActor* Best = nullptr;

	for (const FOverlapResult& Overlap : Overlaps)
	{
		AActor* Candidate = Overlap.GetActor();
		if (!Candidate || Candidate == Owner)
		{
			continue;
		}
		FVector ToCandidate = Candidate->GetActorLocation() - Origin;
		ToCandidate.Z = 0.0;
		const double Distance = ToCandidate.Size();
		if (Distance <= UE_KINDA_SMALL_NUMBER)
		{
			continue;
		}
		const double CosAngle = FVector::DotProduct(ToCandidate / Distance, Aim);
		if (CosAngle < CosLimit)
		{
			continue;
		}
		// Aim alignment dominates; distance breaks ties between similarly aligned targets.
		const double Score = 2.0 * (1.0 - CosAngle) + Distance / SoftTargetRangeCm;
		if (Score < BestScore)
		{
			BestScore = Score;
			Best = Candidate;
		}
	}

	SoftTarget = Best;
	return Best;
}

void UBendingComponent::UpdateWarpTarget()
{
	if (!CurrentMove || !CurrentMove->bUseMotionWarping)
	{
		return;
	}

	AActor* Owner = GetOwner();
	UMotionWarpingComponent* Warp = MotionWarping.Get();
	const FVector OwnerLocation = Owner->GetActorLocation();

	if (const AActor* Target = SoftTarget.Get())
	{
		FVector ToTarget = Target->GetActorLocation() - OwnerLocation;
		ToTarget.Z = 0.0;
		const double Distance = ToTarget.Size();
		if (Distance > UE_KINDA_SMALL_NUMBER)
		{
			const FVector Direction = ToTarget / Distance;
			if (Warp)
			{
				// Close to striking range, but never further than MaxWarpDistanceCm from where the move began.
				const FVector StrikePoint = Target->GetActorLocation() - Direction * CurrentMove->WarpStandoffCm;
				FVector Offset = StrikePoint - MoveStartLocation;
				Offset.Z = 0.0;
				Offset = Offset.GetClampedToMaxSize(CurrentMove->MaxWarpDistanceCm);
				const FVector WarpLocation(MoveStartLocation.X + Offset.X, MoveStartLocation.Y + Offset.Y, OwnerLocation.Z);
				Warp->AddOrUpdateWarpTargetFromLocationAndRotation(CurrentMove->WarpTargetName, WarpLocation, Direction.Rotation());
				DesiredFacingYaw.Reset();
			}
			else
			{
				DesiredFacingYaw = Direction.Rotation().Yaw;
			}
			return;
		}
	}

	// No target: leave the authored root motion untouched and just turn toward the aim during startup.
	if (Warp)
	{
		Warp->RemoveWarpTarget(CurrentMove->WarpTargetName);
	}
	DesiredFacingYaw = GetAimDirection().Rotation().Yaw;
}

void UBendingComponent::TurnTowardDesiredFacing(float DeltaTime)
{
	if (!DesiredFacingYaw.IsSet())
	{
		return;
	}
	AActor* Owner = GetOwner();
	const FRotator Current = Owner->GetActorRotation();
	const FRotator Desired(Current.Pitch, DesiredFacingYaw.GetValue(), Current.Roll);
	const FRotator Next = FMath::RInterpConstantTo(Current, Desired, DeltaTime, StartupTurnRateDegPerSec);
	Owner->SetActorRotation(Next);
	if (Next.Equals(Desired, 0.5))
	{
		DesiredFacingYaw.Reset();
	}
}

// ---------------------------------------------------------------------------------------------------- Resources

double UBendingComponent::SpendChiForEnergy(double RequestedEnergyJ, EBendingEnergyKind Kind)
{
	UAbilitySystemComponent* ASC = AbilitySystem.Get();
	if (!ASC || RequestedEnergyJ <= 0.0)
	{
		return 0.0;
	}

	const UBendingSettings& Settings = UBendingSettings::Get();
	const double JoulesPerChi = Kind == EBendingEnergyKind::Thermal ? Settings.ThermalJoulesPerChi : Settings.KineticJoulesPerChi;

	bool bFound = false;
	const double AvailableChi = ASC->GetGameplayAttributeValue(UBendingAttributeSet::GetChiAttribute(), bFound);
	if (!bFound)
	{
		// No chi pool on this owner (scripted actors, test rigs): work is free.
		return RequestedEnergyJ;
	}

	const double ChiSpent = FMath::Min(RequestedEnergyJ / JoulesPerChi, AvailableChi);
	if (ChiSpent <= 0.0)
	{
		return 0.0;
	}

	FGameplayEffectContextHandle Context = ASC->MakeEffectContext();
	Context.AddSourceObject(this);
	const FGameplayEffectSpecHandle Spec = ASC->MakeOutgoingSpec(UBendingCostEffect::StaticClass(), 1.f, Context);
	if (!Spec.IsValid())
	{
		return 0.0;
	}
	Spec.Data->SetSetByCallerMagnitude(BendingTags::SetByCaller_Bending_Chi, static_cast<float>(-ChiSpent));
	Spec.Data->SetSetByCallerMagnitude(BendingTags::SetByCaller_Bending_Stamina, 0.f);
	ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());

	return ChiSpent * JoulesPerChi;
}

// ---------------------------------------------------------------------------------------------------- Tick

void UBendingComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (IsMoveActive())
	{
		if (!bPhasesFromNotifies)
		{
			AdvanceTimedPhases();
		}
		if (CurrentMove && CurrentPhase == EBendingPhase::Startup)
		{
			if (CurrentMove->bTrackTargetDuringStartup && SoftTarget.IsValid())
			{
				UpdateWarpTarget();
			}
			TurnTowardDesiredFacing(DeltaTime);
		}
		UpdateCancelWindowTag();
	}

	ConsumeBufferedInput();
	RefreshTickEnabled();
}

void UBendingComponent::RefreshTickEnabled()
{
	SetComponentTickEnabled(IsMoveActive() || BufferedInput.IsSet());
}

void UBendingComponent::SetLooseTag(const FGameplayTag& Tag, bool bPresent)
{
	UAbilitySystemComponent* ASC = AbilitySystem.Get();
	if (!Tag.IsValid() || !ASC)
	{
		return;
	}
	if (bPresent)
	{
		ASC->AddLooseGameplayTag(Tag);
	}
	else
	{
		ASC->RemoveLooseGameplayTag(Tag);
	}
}

double UBendingComponent::GetWorldTime() const
{
	const UWorld* World = GetWorld();
	return World ? World->GetTimeSeconds() : 0.0;
}
