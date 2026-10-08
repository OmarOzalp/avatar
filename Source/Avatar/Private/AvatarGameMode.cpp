#include "AvatarGameMode.h"

#include "AvatarCharacter.h"

AAvatarGameMode::AAvatarGameMode()
{
	// Point a Blueprint game mode at BP_AvatarCharacter (with mesh, anim BP and input assets) for real play.
	DefaultPawnClass = AAvatarCharacter::StaticClass();
}
