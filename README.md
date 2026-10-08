# Avatar

A physically simulated elemental-bending sandbox for an open-world action game, built on Unreal Engine 5.8.

Bending is real physics with martial-arts timing:
- Moves run on fighting-game frame data (Startup / Active / Recovery at 60 Hz) on top of the Gameplay Ability System.
- The elements are simulated as matter with mass, momentum and temperature, in real-world units.
- When elements meet, they exchange conserved quantities. Fire boils water into steam, wind feeds flame, water turns soil to mud, and pressure waves deflect stone, all in proportion to the matter involved.

## Layout

```
Avatar.uproject
Config/                      Engine, game and input settings
Source/Avatar/               Game module: AAvatarCharacter, AAvatarGameMode
Plugins/Bending/             The bending system
  Source/Bending/Public/
    BendingTypes.h           Elements, phases, frame data
    BendingGameplayTags.h    Input / State / Event / SetByCaller tags
    BendingSettings.h        Project Settings > Game > Bending
    Components/              UBendingComponent: stance, input buffer, phases, cancels, warping, chi pricing
    Abilities/               UBendingGameplayAbility, cost and regen effects
    Attributes/              Health, Chi, Stamina
    Animation/               "Bending Phase" anim notify state
    Data/                    Move, discipline and input-config data assets
    Interaction/             UBendingInteractionSubsystem, volumes, reaction rules
    Physics/                 Units, substances, thermodynamics kernel
Tests/PhysicsKernel/         Stand-alone tests for the thermodynamics kernel
docs/ARCHITECTURE.md         Design, equations, roadmap
```

## Getting started

1. Install Unreal Engine 5.8 and Git LFS (`git lfs install`).
2. Generate project files from `Avatar.uproject` and build the `AvatarEditor` target.
3. Follow the checklist at the end of [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) to create the input, move and character assets.

Physics kernel tests, with no engine required:

```
Tests/PhysicsKernel/run.sh
```

Debug view in game: `Bending.Interaction.DebugDraw 1`.
