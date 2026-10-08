#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "AvatarGameMode.generated.h"

/**
 * Plays the bending sandbox in any level: the blocky AAvatarCharacter, the canvas AAvatarHUD, and, when the level
 * has no ABendingSandboxArena, one generated at InitGame so its player start exists before the pawn spawns.
 */
UCLASS()
class AVATAR_API AAvatarGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AAvatarGameMode();

	virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;
	virtual AActor* ChoosePlayerStart_Implementation(AController* Player) override;

protected:
	/** Generate the training ground (terrain, ponds, props, lighting) when the level has none. */
	UPROPERTY(EditDefaultsOnly, Category = "Sandbox")
	bool bSpawnSandboxArena = true;
};
