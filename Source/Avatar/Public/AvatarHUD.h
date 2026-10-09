#pragma once

#include "CoreMinimal.h"
#include "BendingTypes.h"
#include "GameFramework/HUD.h"
#include "Interaction/ElementalVolumeTypes.h"
#include "AvatarHUD.generated.h"

class UBendingComponent;
class UBendingInteractionSubsystem;
class UBendingMoveDefinition;
class UFont;

/**
 * Canvas HUD for the bending sandbox (no UMG assets): crosshair, chi and stamina, the stance's five techniques,
 * the current move's Startup / Active / Recovery frames as they elapse, reactions between the elements as they
 * happen, what each technique did, ground traction on mud, and a controls panel (H).
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
