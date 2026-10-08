#include "Abilities/BendingGameplayEffects.h"

#include "Attributes/BendingAttributeSet.h"
#include "BendingGameplayTags.h"

namespace
{
	void AddSetByCallerModifier(TArray<FGameplayModifierInfo>& Modifiers, const FGameplayAttribute& Attribute, const FGameplayTag& DataTag)
	{
		FSetByCallerFloat SetByCaller;
		SetByCaller.DataTag = DataTag;

		FGameplayModifierInfo& Modifier = Modifiers.AddDefaulted_GetRef();
		Modifier.Attribute = Attribute;
		Modifier.ModifierOp = EGameplayModOp::AddBase;
		Modifier.ModifierMagnitude = FGameplayEffectModifierMagnitude(SetByCaller);
	}

	void AddRegenModifier(TArray<FGameplayModifierInfo>& Modifiers, const FGameplayAttribute& Attribute, const FGameplayAttribute& RateAttribute, float TickSeconds)
	{
		// Amount per tick = RegenRate (per second) * tick length.
		FAttributeBasedFloat PerTick;
		PerTick.Coefficient = FScalableFloat(TickSeconds);
		PerTick.BackingAttribute = FGameplayEffectAttributeCaptureDefinition(RateAttribute, EGameplayEffectAttributeCaptureSource::Target, false);

		FGameplayModifierInfo& Modifier = Modifiers.AddDefaulted_GetRef();
		Modifier.Attribute = Attribute;
		Modifier.ModifierOp = EGameplayModOp::AddBase;
		Modifier.ModifierMagnitude = FGameplayEffectModifierMagnitude(PerTick);
	}
}

UBendingCostEffect::UBendingCostEffect()
{
	DurationPolicy = EGameplayEffectDurationType::Instant;
	AddSetByCallerModifier(Modifiers, UBendingAttributeSet::GetChiAttribute(), BendingTags::SetByCaller_Bending_Chi);
	AddSetByCallerModifier(Modifiers, UBendingAttributeSet::GetStaminaAttribute(), BendingTags::SetByCaller_Bending_Stamina);
}

UBendingResourceRegenEffect::UBendingResourceRegenEffect()
{
	DurationPolicy = EGameplayEffectDurationType::Infinite;
	Period = FScalableFloat(TickSeconds);
	bExecutePeriodicEffectOnApplication = false;
	AddRegenModifier(Modifiers, UBendingAttributeSet::GetChiAttribute(), UBendingAttributeSet::GetChiRegenRateAttribute(), TickSeconds);
	AddRegenModifier(Modifiers, UBendingAttributeSet::GetStaminaAttribute(), UBendingAttributeSet::GetStaminaRegenRateAttribute(), TickSeconds);
}
