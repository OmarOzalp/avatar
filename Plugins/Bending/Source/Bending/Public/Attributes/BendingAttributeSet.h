#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemComponent.h"
#include "AttributeSet.h"
#include "BendingAttributeSet.generated.h"

#define BENDING_ATTRIBUTE_ACCESSORS(ClassName, PropertyName) \
	GAMEPLAYATTRIBUTE_PROPERTY_GETTER(ClassName, PropertyName) \
	GAMEPLAYATTRIBUTE_VALUE_GETTER(PropertyName) \
	GAMEPLAYATTRIBUTE_VALUE_SETTER(PropertyName) \
	GAMEPLAYATTRIBUTE_VALUE_INITTER(PropertyName)

/**
 * Vital and bending resources. Chi pays for bending, both as flat move costs and as joules of physical
 * work (see UBendingComponent::SpendChiForEnergy). Stamina pays for the martial-arts body mechanics.
 */
UCLASS()
class BENDING_API UBendingAttributeSet : public UAttributeSet
{
	GENERATED_BODY()

public:
	UBendingAttributeSet();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue) override;
	virtual void PostGameplayEffectExecute(const FGameplayEffectModCallbackData& Data) override;

	UPROPERTY(BlueprintReadOnly, Category = "Vitals", ReplicatedUsing = OnRep_Health)
	FGameplayAttributeData Health;
	BENDING_ATTRIBUTE_ACCESSORS(UBendingAttributeSet, Health)

	UPROPERTY(BlueprintReadOnly, Category = "Vitals", ReplicatedUsing = OnRep_MaxHealth)
	FGameplayAttributeData MaxHealth;
	BENDING_ATTRIBUTE_ACCESSORS(UBendingAttributeSet, MaxHealth)

	UPROPERTY(BlueprintReadOnly, Category = "Chi", ReplicatedUsing = OnRep_Chi)
	FGameplayAttributeData Chi;
	BENDING_ATTRIBUTE_ACCESSORS(UBendingAttributeSet, Chi)

	UPROPERTY(BlueprintReadOnly, Category = "Chi", ReplicatedUsing = OnRep_MaxChi)
	FGameplayAttributeData MaxChi;
	BENDING_ATTRIBUTE_ACCESSORS(UBendingAttributeSet, MaxChi)

	/** Chi recovered per second. */
	UPROPERTY(BlueprintReadOnly, Category = "Chi", ReplicatedUsing = OnRep_ChiRegenRate)
	FGameplayAttributeData ChiRegenRate;
	BENDING_ATTRIBUTE_ACCESSORS(UBendingAttributeSet, ChiRegenRate)

	UPROPERTY(BlueprintReadOnly, Category = "Stamina", ReplicatedUsing = OnRep_Stamina)
	FGameplayAttributeData Stamina;
	BENDING_ATTRIBUTE_ACCESSORS(UBendingAttributeSet, Stamina)

	UPROPERTY(BlueprintReadOnly, Category = "Stamina", ReplicatedUsing = OnRep_MaxStamina)
	FGameplayAttributeData MaxStamina;
	BENDING_ATTRIBUTE_ACCESSORS(UBendingAttributeSet, MaxStamina)

	/** Stamina recovered per second. */
	UPROPERTY(BlueprintReadOnly, Category = "Stamina", ReplicatedUsing = OnRep_StaminaRegenRate)
	FGameplayAttributeData StaminaRegenRate;
	BENDING_ATTRIBUTE_ACCESSORS(UBendingAttributeSet, StaminaRegenRate)

protected:
	UFUNCTION()
	void OnRep_Health(const FGameplayAttributeData& OldValue);

	UFUNCTION()
	void OnRep_MaxHealth(const FGameplayAttributeData& OldValue);

	UFUNCTION()
	void OnRep_Chi(const FGameplayAttributeData& OldValue);

	UFUNCTION()
	void OnRep_MaxChi(const FGameplayAttributeData& OldValue);

	UFUNCTION()
	void OnRep_ChiRegenRate(const FGameplayAttributeData& OldValue);

	UFUNCTION()
	void OnRep_Stamina(const FGameplayAttributeData& OldValue);

	UFUNCTION()
	void OnRep_MaxStamina(const FGameplayAttributeData& OldValue);

	UFUNCTION()
	void OnRep_StaminaRegenRate(const FGameplayAttributeData& OldValue);

private:
	/** Clamps a resource's current value into [0, Max]. */
	void ClampResource(const FGameplayAttribute& Attribute, float& NewValue) const;
};
