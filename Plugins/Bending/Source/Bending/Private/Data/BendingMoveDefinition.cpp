#include "Data/BendingMoveDefinition.h"

#include "Abilities/BendingGameplayAbility.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

#define LOCTEXT_NAMESPACE "BendingMoveDefinition"

const FPrimaryAssetType UBendingMoveDefinition::PrimaryAssetType(TEXT("BendingMove"));

FPrimaryAssetId UBendingMoveDefinition::GetPrimaryAssetId() const
{
	return FPrimaryAssetId(PrimaryAssetType, GetFName());
}

#if WITH_EDITOR
EDataValidationResult UBendingMoveDefinition::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);

	if (!AbilityClass)
	{
		Context.AddError(LOCTEXT("MissingAbility", "Move has no AbilityClass."));
		Result = EDataValidationResult::Invalid;
	}
	if (!InputTag.IsValid())
	{
		Context.AddError(LOCTEXT("MissingInput", "Move has no InputTag; it can never be triggered."));
		Result = EDataValidationResult::Invalid;
	}
	if (FrameData.RecoveryCancelFrame > FrameData.RecoveryFrames)
	{
		Context.AddWarning(LOCTEXT("CancelPastRecovery", "RecoveryCancelFrame is past the last recovery frame; the move can only be cancelled on hit."));
	}
	if (Montage.IsNull())
	{
		Context.AddWarning(LOCTEXT("NoMontage", "No montage: phases will run on frame-data timers only."));
	}
	return Result;
}
#endif

#undef LOCTEXT_NAMESPACE
