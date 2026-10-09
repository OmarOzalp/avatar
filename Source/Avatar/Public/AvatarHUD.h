#pragma once

#include "CoreMinimal.h"
#include "BendingTypes.h"
#include "GameFramework/HUD.h"
#include "Interaction/ElementalVolumeTypes.h"
#include "Sandbox/BendingSparringPartner.h"
#include "AvatarHUD.generated.h"

class ABendingPropActor;
class UBendingComponent;
class UBendingInteractionSubsystem;
class UBendingMoveDefinition;
class UFont;

/**
 * Canvas HUD for the bending sandbox (no UMG assets): crosshair, health, chi and stamina, the stance's five techniques,
 * the current move's Startup / Active / Recovery frames as they elapse, reactions between the elements as they
 * happen, what each technique did, ground traction on mud, the guard, the sparring partner's health, duel scoreboard
 * and callouts, and a controls panel (H).
 */
UCLASS()
class AVATAR_API AAvatarHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;

	void ToggleControlsPanel() { bShowControls = !bShowControls; }

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Seconds a reaction or technique line stays on screen (it fades over the last second). */
	UPROPERTY(EditAnywhere, Category = "HUD", meta = (ClampMin = 0.5))
	float MessageSeconds = 4.f;

private:
	struct FReactionLine
	{
		EElementalReactionType Type = EElementalReactionType::None;
		double MassKg = 0.0;
		double EnergyJ = 0.0;
		double LastTimeSeconds = 0.0;
	};

	void HandleReaction(const FElementalReactionEvent& Event);
	void HandlePropHit(const ABendingPropActor* Prop, const FVector& Location, double Damage, bool bKnockout);
	void HandleSparringCallout(EBendingSparringCallout Callout, const FVector& Location, double Amount);

	/** A damage number or a callout (K.O., PARRY, DUEL) floating up from where it happened. */
	struct FHitNumber
	{
		FVector Location = FVector::ZeroVector;
		FString Text;
		FLinearColor Color = FLinearColor::White;
		float Scale = 1.f;
		/** How long it rises before it is gone (it fades over the last second). */
		float Seconds = 1.5f;
		double TimeSeconds = 0.0;
	};
	/** A blow's damage number; real blows chain the combo counter. */
	void AddDamageNumber(const FVector& Location, double Damage);
	void AddCallout(const FVector& Location, const FString& Text, const FLinearColor& Color, float Scale, float Seconds);
	void DrawHits();
	/** The sparring partner's health over its head, its invitation while it waits, and the duel scoreboard. */
	void DrawSparring(const APawn* Pawn);

	void DrawCrosshair();
	void DrawResources(const APawn* Pawn);
	void DrawStance(const UBendingComponent* Bending);
	void DrawFrameBar(const UBendingComponent* Bending);
	void DrawFeed(const APawn* Pawn);
	void DrawStatus(const APawn* Pawn);
	void DrawControls();
	void DrawLabel(const FString& Text, float X, float Y, const FLinearColor& Color, const UFont* Font, float Scale = 1.f);
	void DrawBar(float X, float Y, float Width, float Height, float Fraction, const FLinearColor& Fill, const FString& Label);
	float GetTextWidth(const FString& Text, const UFont* Font, float Scale) const;
	double GetTimeSeconds() const;

	TWeakObjectPtr<UBendingInteractionSubsystem> Interaction;
	FDelegateHandle ReactionHandle;
	FDelegateHandle PropHitHandle;
	FDelegateHandle SparringHandle;
	TArray<FHitNumber> HitNumbers;
	/** Hits in a row (within ComboSeconds of each other) and their total damage. */
	int32 ComboCount = 0;
	double ComboDamage = 0.0;
	double LastHitSeconds = -100.0;
	/** When the last duel ended: the scoreboard stays up a few seconds after. */
	double LastDuelEndSeconds = -100.0;
	TArray<FReactionLine> Reactions;

	/** The last move stays on the frame bar briefly after it ends. */
	TWeakObjectPtr<const UBendingMoveDefinition> LastMove;
	EBendingPhase LastPhase = EBendingPhase::None;
	int32 LastPhaseFrame = 0;
	double LastMoveSeenSeconds = -100.0;

	/** UI scale for the current canvas height (1 at 1080 p). */
	float UIScale = 1.f;
	bool bShowControls = true;
};
