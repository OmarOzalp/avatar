#include "AvatarGameMode.h"

#include "AvatarCharacter.h"
#include "AvatarHUD.h"
#include "Engine/World.h"
#include "GameFramework/PlayerStart.h"
#include "Sandbox/BendingSandboxArena.h"

AAvatarGameMode::AAvatarGameMode()
{
	// Asset-free defaults; a Blueprint game mode can point these at a character with a real mesh and input assets.
	DefaultPawnClass = AAvatarCharacter::StaticClass();
	HUDClass = AAvatarHUD::StaticClass();
}

void AAvatarGameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
	Super::InitGame(MapName, Options, ErrorMessage);

	UWorld* World = GetWorld();
	if (!bSpawnSandboxArena || !World || ABendingSandboxArena::Find(World))
	{
		return;
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	if (ABendingSandboxArena* Arena = World->SpawnActor<ABendingSandboxArena>(ABendingSandboxArena::StaticClass(), FTransform::Identity, Params))
	{
		Arena->BuildArena();
	}
}

AActor* AAvatarGameMode::ChoosePlayerStart_Implementation(AController* Player)
{
	if (ABendingSandboxArena* Arena = ABendingSandboxArena::Find(GetWorld()))
	{
		// A hand-placed arena may not have reached BeginPlay yet.
		Arena->BuildArena();
		if (APlayerStart* Start = Arena->GetPlayerStart())
		{
			return Start;
		}
	}
	return Super::ChoosePlayerStart_Implementation(Player);
}
