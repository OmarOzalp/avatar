# Avatar

A physically simulated elemental-bending sandbox for an open-world action game, built on Unreal Engine 5.8.

Bending is real physics with martial-arts timing:
- Moves run on fighting-game frame data (Startup / Active / Recovery at 60 Hz) on top of the Gameplay Ability System.
- The elements are simulated as matter with mass, momentum and temperature, in real-world units.
- When elements meet, they exchange conserved quantities. Fire boils water into steam, wind feeds flame, water turns soil to mud, and pressure waves deflect stone, all in proportion to the matter involved.

## Play the training ground

A low-poly third-person sandbox: walk around a small valley with two ponds, rocks, boulders, training dummies,
braziers and a block of ice, and bend all four elements. The ground is deformable, the water whip is a simulated
chain of water, and everything you throw interacts through the same physics kernel.

### In Unreal Engine 5.8

1. Install Unreal Engine 5.8.
2. Right-click `Avatar.uproject` → *Generate project files*, then build the `AvatarEditor` target (Development Editor).
   Opening the `.uproject` directly also offers to build the modules.
3. Open the project and press **Play**.

There are no assets to create. The project starts on an empty engine map, and `AAvatarGameMode` builds the arena
from code when the level starts: the terrain, ponds, props and lighting. The character and props are made of
engine basic shapes, and the HUD is drawn on the canvas.

> The Unreal code has not been compiled yet: the environment it was written in has no engine. Expect a short round
> of fixes on the first build. See [First build](docs/ARCHITECTURE.md#first-build) for the parts most likely to need them.

### In a browser

```
Tools/Sandbox3D/build_web.sh     # writes Tools/Sandbox3D/build/BendingTrainingGround.html: open it in a browser
```

The browser build is the same arena and techniques, running the same C++ kernel compiled to WebAssembly and drawn
with three.js. It is the quickest way to try a change to the physics.

### Controls

| Input | Action |
|---|---|
| W A S D, mouse | Move, look (third person) |
| Shift | Sprint |
| Space | Jump |
| 1 2 3 4 | Water, Earth, Fire, Air stance |
| Left mouse, right mouse, Q, E | The stance's techniques (below) |
| H | Show or hide the controls panel |

| Stance | Left mouse | Right mouse | Q | E |
|---|---|---|---|---|
| **1 Water** | Water Whip: draw from a pond within 15 m, then lash (hold to keep it extended) | Freeze / Thaw the whip | Release: the water soaks into the ground (mud) | Water Blast: throw the whip as one ball |
| **2 Earth** | Rock Throw: pull a rock from the ground (leaves a crater) and hurl it | Earth Wall | Raise Ground (hold) | Lower Ground (hold) |
| **3 Fire** | Fire Blast | Flame Stream (hold) | | Ground Flame (hold) |
| **4 Air** | Air Blast | Gust (hold) | | Air Jump |

The HUD shows chi and stamina, the current move's Startup / Active / Recovery frames with the cancel window, what
each technique did and what it cost, and every reaction between elements as it happens.

**Things to try**
- Lash the whip into a lit brazier and hold it there: the water boils to steam and the fire goes out. Hit the brazier with fire to relight it.
- Freeze the whip (it costs about 32 chi to freeze 20 kg of water), then thaw it again.
- Release the whip or throw a water blast onto soil, then run across the mud. It is slippery: you speed up and stop slowly until a ground flame dries it.
- Hold a ground flame on the ice block until it melts (about 20 s and 33 MJ).
- Air-blast a dummy, then a boulder.
- Raise an earth wall and try to walk through it. Dig a pit and watch its soil pile up on the rim.
- Run out of chi: techniques still fire, but they do only the work you can pay for.

## Layout

```
Avatar.uproject
Config/                      Engine, game and input settings
Source/Avatar/               Game module: AAvatarCharacter (basic-shape body, runtime input), AAvatarGameMode, AAvatarHUD
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
    Sim/                     Engine-independent physics kernel (BendingSim): volumes, thermodynamics, reactions,
                             world, deformable terrain, water whip, technique table, arena layout
    Interaction/             UBendingInteractionSubsystem, UElementalVolumeComponent, reaction assets (wrap Sim/)
    Physics/                 Unit conversion, substance enum mirror
    Sandbox/                 The training ground in Unreal: techniques, terrain, whip, projectiles, props, arena
Tests/Sim/                   Kernel tests: math, thermodynamics, scenarios, terrain, whip, 3D sandbox, wasm parity
Tools/SimDemo/               2D scenario driver + Bending Physics Lab (browser)
Tools/Sandbox3D/             3D sandbox driver + Bending Training Ground (browser)
docs/ARCHITECTURE.md         Design, equations, roadmap
```

## Tests

The physics kernel builds and runs without the engine (clang++ and Node 18+):

```
Tests/run_all.sh                 # 191 checks in five suites, plus bit-exact WebAssembly parity for both browser builds
Tools/SimDemo/build_sandbox.sh   # writes Tools/SimDemo/build/BendingLab.html (the 2D reaction lab)
```

The Bending Physics Lab shows the six reaction scenarios in 2D, side on: evaporation, oxygenation, mud, deflection,
freezing and drying. It has a sandbox where you throw elements at each other, plus live volumes, a reaction log, a
traction gauge, chi billing and tunable parameters.

Debug view in game: `Bending.Interaction.DebugDraw 1`.
