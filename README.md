# Avatar

A physically simulated elemental-bending sandbox for an open-world action game, built on Unreal Engine 5.8.

Bending is real physics with martial-arts timing:
- Moves run on fighting-game frame data (Startup / Active / Recovery at 60 Hz) on top of the Gameplay Ability System.
- The elements are simulated as matter with mass, momentum and temperature, in real-world units.
- When elements meet, they exchange conserved quantities. Fire boils water into steam, wind feeds flame, water turns soil to mud, and pressure waves deflect stone, all in proportion to the matter involved.

## Play the training ground

A third-person sandbox: walk around a training arena in a small valley, with two ponds, rocks, boulders, straw
dummies, braziers and blocks of ice, and bend all four elements with five techniques each. The ground is deformable,
the water whip is a simulated stream of water, water puts out fire, and everything you throw interacts through the
same physics kernel. The field reacts: banners, straw, crates and dummies burn (and fire spreads between them),
lanterns light, crates smash and water barrels burst, and training dummies take damage, get knocked out and stand up
again. Eight training goals walk a new player through it.

### In Unreal Engine 5.8

1. Install Unreal Engine 5.8, and on Windows Visual Studio 2022 with the *Game development with C++* workload (on
   macOS, Xcode). This is a C++ project, so the engine has to compile its two modules once.
2. Clone this repository (branch `claude/bending-core-foundation`).
3. Right-click `Avatar.uproject` → *Generate Visual Studio project files* (macOS: *Generate Xcode project*).
4. Open `Avatar.sln`, set the configuration to **Development Editor** and the start-up project to **Avatar**, and
   build (Ctrl+Shift+B). Opening the `.uproject` directly also offers to build the modules: answer *Yes*.
5. When the editor opens, press **Play** (Alt+P). Click into the viewport so it takes the mouse.

There are no assets to create. The project starts on an empty engine map, and `AAvatarGameMode` builds the arena
from code when the level starts: the terrain, ponds, props and lighting. The character, props and effects are made of
engine basic shapes, and the HUD is drawn on the canvas. It plays the same techniques as the browser build, but it
looks much plainer: the cartoon look, shaders and particle effects exist only in the browser build so far. The
interactive field and dummy health work in both.

> The Unreal code has not been compiled yet: the environment it was written in has no engine. Expect a short round
> of fixes on the first build. See [First build](docs/ARCHITECTURE.md#first-build) for the parts most likely to need them.

### In a browser

```
Tools/Sandbox3D/build_web.sh     # writes Tools/Sandbox3D/build/BendingTrainingGround.html: open it in a browser
```

The browser build is the same arena and techniques, running the same C++ kernel compiled to WebAssembly, and it is
where the look is developed first. It is a bright, cel-shaded cartoon:
- **Bender:** a cartoon character with ink outlines and bouncy, spring-driven animation: a pose for every technique,
  squash on landing, a hop when you switch stance. Switching stance changes into that nation's clothes: a Water
  Tribe parka with fur trim, Earth Kingdom green and tan, Fire Nation red and gold with shoulder guards, or Air Nomad
  robes with the blue arrow.
- **Fights:** damage numbers, health bars, a hit flash, hit-stop and camera kick on heavy blows, a combo counter and
  K.O. callouts.
- **Arena:** toon grass and a sand arena ringed by stone, banners in the four elements' colours, lanterns, trees and
  a sky with drifting clouds.
- **Water:** a glossy, rippling whip with foam, droplets and splashes; ponds with waves and ripples, and a ring on the
  shore when you are close enough to draw water.
- **Fire:** hard-edged cartoon flames that lick and stretch as they fly, with embers, smoke and scorch marks.
- **Effects:** ice daggers with cold-light trails, a shockwave with rocks punching up, a swirling tornado that turns
  into a fire tornado, a ball of air to ride, floating callouts and camera kick.
- **The field:** cloth banners that flap in bent air and burn away from the bottom edge, charring straw and crates,
  flying boards and staves, bursting barrels, glowing lanterns, steam when a fire is doused.

It is also the quickest way to try a change to the physics. The Unreal build still draws everything with basic
shapes; its materials and Niagara effects come next.

### Controls

| Input | Action |
|---|---|
| W A S D, mouse | Move, look (third person) |
| Shift | Sprint |
| Space | Jump |
| 1 2 3 4 | Water, Earth, Fire, Air stance |
| Left mouse, right mouse, Q, E, F | The stance's techniques (below); F is its signature move |
| H | Show or hide the controls panel |

| Stance | Left mouse | Right mouse | Q | E | F (signature) |
|---|---|---|---|---|---|
| **1 Water** | Water Whip: draw from a pond within 15 m and it circles you; press to lash, and it snaps out to the aim and flows back | Freeze / Thaw the whip | Release: the water soaks into the ground (mud) | Water Blast: throw the whip as one ball | Ice Daggers: freeze water off the whip into five daggers and throw them |
| **2 Earth** | Rock Throw: pull a rock from the ground (leaves a crater) and hurl it | Earth Wall | Raise Ground (hold) | Lower Ground (hold) | Earthquake: stomp, and everything around you is thrown outward |
| **3 Fire** | Fire Blast | Flame Stream (hold) | Jet Dash: fire from your feet launches you forward | Ground Flame (hold) | Ring of Fire: a spin kick bursts flame out in every direction |
| **4 Air** | Air Blast | Gust (hold) | Air Scooter (hold): ride a spinning ball of air at 14 m/s | Air Jump | Tornado: spin one up at the aim; it pulls things in and lifts them |

The HUD shows chi and stamina, the current move's Startup / Active / Recovery frames with the cancel window, what
each technique did and what it cost, and every reaction between elements as it happens.

**Things to try**
- Water only comes from a pond: walk within 15 m (the HUD and a ring on the shore tell you), then left-click.
- Lash a lit brazier: the stream and the spray flung off its tip boil to steam, and two lashes put the fire out. Hit the brazier with fire to relight it.
- Light a ground flame (Fire, hold E), then lash it once with water: it goes out.
- Spin up a tornado next to the stones (Air, F), switch to Fire and throw a fire blast into it: a fire tornado.
- Stomp an earthquake (Earth, F) in the middle of the stones, then try it next to a boulder.
- Throw ice daggers (Water, F) at a dummy. They take water from the whip, so it gets shorter.
- One fire blast into the straw bales by the braziers sets the whole yard alight; put it out with water.
- Set a banner alight, then lash it with water before it is gone.
- Smash the crates by the rocks with a thrown rock or an earthquake. Burst a water barrel and watch the ground turn to mud.
- Far from the ponds? Draw a whip from a water barrel.
- Light the stone lanterns round the ring with fire.
- Knock out a dummy (a thrown rock does it in one). It gets up again after a few seconds.
- Lash the ground in front of you: the stream slaps it and splashes.
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
docs/ARCHITECTURE.md         Design, equations, verification
docs/ROADMAP.md              Where the demo stands, what comes next, and what you can do
```

## Tests

The physics kernel builds and runs without the engine (clang++ and Node 18+):

```
Tests/run_all.sh                 # 247 checks in five suites, plus bit-exact WebAssembly parity for both browser builds
Tools/SimDemo/build_sandbox.sh   # writes Tools/SimDemo/build/BendingLab.html (the 2D reaction lab)
```

The Bending Physics Lab shows the six reaction scenarios in 2D, side on: evaporation, oxygenation, mud, deflection,
freezing and drying. It has a sandbox where you throw elements at each other, plus live volumes, a reaction log, a
traction gauge, chi billing and tunable parameters.

Debug view in game: `Bending.Interaction.DebugDraw 1`.
