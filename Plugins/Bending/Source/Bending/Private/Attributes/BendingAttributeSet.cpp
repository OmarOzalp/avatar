#include "Attributes/BendingAttributeSet.h"

#include "GameplayEffectExtension.h"
#include "Net/UnrealNetwork.h"

UBendingAttributeSet::UBendingAttributeSet()
{
	InitHealth(100.f);
	InitMaxHealth(100.f);
	InitChi(100.f);
	InitMaxChi(100.f);
	InitChiRegenRate(8.f);
	InitStamina(100.f);
	InitMaxStamina(100.f);
	InitStaminaRegenRate(15.f);
}

void UBendingAttributeSet::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME_CONDITION_NOTIFY(UBendingAttributeSet, Health, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UBendingAttributeSet, MaxHealth, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UBendingAttributeSet, Chi, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UBendingAttributeSet, MaxChi, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UBendingAttributeSet, ChiRegenRate, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UBendingAttributeSet, Stamina, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UBendingAttributeSet, MaxStamina, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UBendingAttributeSet, StaminaRegenRate, COND_None, REPNOTIFY_Always);
}

void UBendingAttributeSet::PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue)
{
	Super::PreAttributeChange(Attribute, NewValue);
	ClampResource(Attribute, NewValue);
}

void UBendingAttributeSet::PostGameplayEffectExecute(const FGameplayEffectModCallbackData& Data)
{
	Super::PostGameplayEffectExecute(Data);

	// Instant and periodic effects modify base values; clamp those too.
	const FGameplayAttribute& Attribute = Data.EvaluatedData.Attribute;
	if (Attribute == GetHealthAttribute())
	{
		SetHealth(FMath::Clamp(GetHealth(), 0.f, GetMaxHealth()));
	}
	else if (Attribute == GetChiAttribute())
	{
		SetChi(FMath::Clamp(GetChi(), 0.f, GetMaxChi()));
	}
	else if (Attribute == GetStaminaAttribute())
	{
		SetStamina(FMath::Clamp(GetStamina(), 0.f, GetMaxStamina()));
	}
}

void UBendingAttributeSet::ClampResource(const FGameplayAttribute& Attribute, float& NewValue) const
{
	if (Attribute == GetHealthAttribute())
	{
		NewValue = FMath::Clamp(NewValue, 0.f, GetMaxHealth());
	}
	else if (Attribute == GetChiAttribute())
	{
		NewValue = FMath::Clamp(NewValue, 0.f, GetMaxChi());
	}
	else if (Attribute == GetStaminaAttribute())
	{
		NewValue = FMath::Clamp(NewValue, 0.f, GetMaxStamina());
	}
	else if (Attribute == GetMaxHealthAttribute() || Attribute == GetMaxChiAttribute() || Attribute == GetMaxStaminaAttribute()
		|| Attribute == GetChiRegenRateAttribute() || Attribute == GetStaminaRegenRateAttribute())
	{
		NewValue = FMath::Max(NewValue, 0.f);
	}
}

void UBendingAttributeSet::OnRep_Health(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UBendingAttributeSet, Health, OldValue);
}

void UBendingAttributeSet::OnRep_MaxHealth(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UBendingAttributeSet, MaxHealth, OldValue);
}

void UBendingAttributeSet::OnRep_Chi(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UBendingAttributeSet, Chi, OldValue);
}

void UBendingAttributeSet::OnRep_MaxChi(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UBendingAttributeSet, MaxChi, OldValue);
}

void UBendingAttributeSet::OnRep_ChiRegenRate(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UBendingAttributeSet, ChiRegenRate, OldValue);
}

void UBendingAttributeSet::OnRep_Stamina(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UBendingAttributeSet, Stamina, OldValue);
}

void UBendingAttributeSet::OnRep_MaxStamina(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UBendingAttributeSet, MaxStamina, OldValue);
}

void UBendingAttributeSet::OnRep_StaminaRegenRate(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UBendingAttributeSet, StaminaRegenRate, OldValue);
}
