# Bending Architecture

Physically simulated elemental bending for an open-world action game. Unreal Engine **5.8**, single player for now
(designed so the GAS layer can go networked later), real-world units throughout.

## The one rule

**Gameplay truth lives in a CPU-side physical model. Niagara only draws it.**

Every bent thing (a boulder, a water whip segment, a fireball, an air wave, a steam cloud) is an *elemental volume*:
mass (kg), temperature (K), velocity, shape, plus substance-specific state. Reactions between elements are exchanges
of conserved quantities (mass, momentum, heat) between volumes, not scripted "if fire touches water" outcomes.
Niagara reads the volumes through parameter bindings and never decides a hit, because GPU particles cannot report
back to gameplay without frames of latency.

```
 Enhanced Input ──► AAvatarCharacter ──► UBendingComponent ──► UAbilitySystemComponent (GAS)
                     (Source/Avatar)      stance, input buffer,   costs, cooldowns, attributes,
                                          frame-data phases,      ability activation
                                          cancel windows,                 │
                                          motion-warp targeting           ▼
                                                 ▲              UBendingGameplayAbility
                     UAnimNotifyState_ ──────────┘              montage + phase events
                     BendingPhase (montage)                     OnStartupBegin / OnActiveBegin / ...
                                                                          │ spawns / drives
                                                                          ▼
                                                     Element actors (Sandbox/: water whip, projectiles,
                                                     props, bendable terrain), each with a
                                                     UElementalVolumeComponent or owned kernel volumes
                                                                          │ registers
                                                                          ▼
                                                     UBendingInteractionSubsystem (fixed 60 Hz)
                                                     broadphase ─► contacts ─► UElementalReaction rules
                                                     ambient heat, buoyancy, mud patches, depletion
                                                                          │ events (aggregated, with rates)
                                                                          ▼
                                                     Presentation: Niagara / audio / decals (listeners)
```

## Engine choices

| Choice | Why |
|---|---|
| UE 5.8 | Latest release and Epic's last planned major UE5 version, so it stays a stable target until UE6 (early access targeted for end of 2027). MegaLights is production ready, which makes many short-lived fire lights affordable. |
| Character Movement Component | Mover is still experimental. Root motion from montages drives the body during moves. |
| Gameplay Ability System | Resources, costs, cooldowns, tags, montage tasks, and a path to networking later. |
| Motion Warping | Root motion alone cannot connect with a moving target. Warping re-targets the strike during startup. |
| World subsystem instead of a placed manager actor | Nothing to place in each World Partition cell, lifetime tied to the world, `GetWorld()->GetSubsystem<>()` from anywhere. |
| Plugin (`Plugins/Bending`) + game module (`Source/Avatar`) | The bending system stays self-contained and reusable; game-specific classes stay out of it. |

## Units

`Physics/BendingUnits.h` is the single place conversions happen.

- **Engine units** for positions, velocities and impulses, in the kernel too, so nothing is converted on the way to `AddImpulse`: cm, cm/s, kg·cm/s. Field names carry `Cm`, `CmS`, `KgCmS`.
- **SI** for every physical-model quantity: kg, m, m/s, K, J, W, Pa, N.
- Physical constants are code. Designer-facing knobs are explicit multipliers layered on top: `Strength` per reaction, `HeatTransferScale`, `DryingTimeScale`, chi exchange rates.

## Casting pipeline (GAS layer)

### Data
- **`UBendingMoveDefinition`** (PrimaryDataAsset): element, input tag, priority, ability class, montage (soft reference, preloaded when the discipline is granted), `FBendingFrameData`, warp settings, chi and stamina cost, element payload (actor class, socket, mass kg, launch speed m/s).
- **`UBendingDiscipline`** (PrimaryDataAsset): element, move list, and physical limits (`MaxBendingForceN`, `MaxControlledMassKg`, `MaxBendingRangeCm`). The Avatar is simply granted all four disciplines.
- **`UBendingInputConfig`**: input actions → `Input.Bending.*` tags, plus stance-select actions.

### Flow
1. Input press → `UBendingComponent::HandleInputPressed(tag)`.
2. Any active move bound to that tag receives the press (`AbilitySpecInputPressed`), which drives follow-ups and charge moves.
3. If the bender is free, or the current move is in a **cancel window**, the highest-priority move for `(stance, tag)` that passes cost, cooldown and tag checks is activated. The current move is cancelled only once a successor is known to be able to start, so an unaffordable press never skips recovery frames.
4. Otherwise the press is **buffered** for `InputBufferFrames` (60 Hz frames) and fires when a cancel window opens.
5. The ability commits cost (a SetByCaller instant GE, so no per-move cost assets are needed), plays the montage, and calls `OnMoveStarted`, which enters **Startup**.

### Frame data
`FBendingFrameData` holds Startup / Active / Recovery frame counts at 60 Hz, the recovery cancel frame, cancel-on-hit, and `bFitAnimationToFrameData`.

- **Notify-driven**: when the montage carries `Bending Phase` notify states, each window's start fires the phase. Animators own *where* a phase sits in the clip.
- **Play-rate fitting**: at each window start, the montage play rate is set to `segment length ÷ (frames / 60)`, so the phase lasts exactly its authored frame count. Root motion keeps its distance and changes speed.
- **Timer-driven fallback**: a montage without phase notifies (or no montage at all) runs phases on frame-data timers. This lets you prototype moves before animation exists.
- Phases only move forward, so a stray notify from a montage that is blending out cannot rewind the current move.

| Phase | Loose tag on the ability system | Gameplay event |
|---|---|---|
| Startup | `State.Bending.Startup` | `Event.Bending.Phase.Startup` |
| Active | `State.Bending.Active` | `Event.Bending.Phase.Active` |
| Recovery | `State.Bending.Recovery` | `Event.Bending.Phase.Recovery` |
| (done) | removed | `Event.Bending.Phase.End` |
| cancel window open | `State.Bending.CancelWindow` | — |
| hit confirmed | — | `Event.Bending.Hit` |

`UBendingGameplayAbility` turns the events into `OnStartupBegin` / `OnActiveBegin` / `OnRecoveryBegin` / `OnInputReleasedDuringMove`, all BlueprintNativeEvents. Element abilities spawn and launch matter in `OnActiveBegin`.

### Targeting
On activation the component picks a soft target: pawns in range and inside the aim cone, scored by angle first and distance second. With a target, the Motion Warping target is placed at striking range (`WarpStandoffCm`), clamped to `MaxWarpDistanceCm` from where the move began, and updated every tick during startup so the strike tracks a moving opponent. Without a target, the warp target is removed (authored root motion is untouched) and the body turns toward the aim at `StartupTurnRateDegPerSec`.

### Chi is priced in joules
On top of the flat activation cost, element actors pay for the physical work they do:

```
granted_J = UBendingComponent::SpendChiForEnergy(requested_J, Kinetic | Thermal)
```

The function returns the joules actually granted: everything requested if affordable, otherwise whatever the remaining chi buys. The caller scales the effect by the result, so an exhausted bender physically cannot lift the boulder.
- Kinetic: a 1.4 t boulder launched at 20 m/s is 280 kJ, which is 56 chi at 5 kJ/chi.
- Thermal: freezing 10 kg of water from 15 °C is 4.0 MJ, which is 16 chi at 250 kJ/chi.

## Interaction simulation

### Two layers: kernel and adapter

```
 Public/Sim/  namespace BendingSim          no engine headers, no STL, no libc, no allocation
   BendingSimMath.h       FVec3, segment closest points, deterministic exp / cbrt
   BendingThermo.h        sensible + latent heat, phase walk, equilibrium-capped heat flow
   BendingSimTypes.h      substances and their real properties, FVolume, contacts, events, settings
   BendingSimReactions.h  reaction rules (plain functions + parameter structs)
   BendingSimWorld.h      FSimWorld: fixed-capacity slots, sweep-and-prune, fixed step, events, moisture

 Interaction/ (UObject adapter)
   UBendingInteractionSubsystem   owns one FSimWorld, syncs components in/out once per frame,
                                  converts events to USTRUCTs, broadcasts delegates, debug draw
   UElementalVolumeComponent      pushes transform/velocity, receives impulses, substance changes, depletion
   UElementalReaction_*           editable UPROPERTY copies of the kernel parameter structs, forward to React*
```

The kernel holds all of the physics. It compiles with the plugin, with any desktop compiler on its own, and to
freestanding WebAssembly, and the three builds produce bit-identical results. That is what makes it testable without
the editor (see **Verification** below), and it is the same code that runs in the browser lab.

- Capacity is fixed (`MaxVolumes` = 1024 slots). Handles are `{Index, Serial}`, so a stale handle to a reused slot is rejected.
- Results are deterministic: same inputs, same bits. Floating-point contraction is disabled in the stand-alone builds.
- Custom rules: subclass `UElementalReaction`, override `React(BendingSim::FReactionContext&)`, and add it to a
  `UElementalReactionSet`. The context exposes both volumes, the contact, `EmitEvent` and `SpawnFreeVolume`.
  A rule whose physics is not symmetric overrides `GetRequiredSubstanceA` (drag and oxygenation need Air as A,
  saturation needs Water), and registration puts the pair in that order whichever way it was authored.
- Kernel functions and types are exported from the Bending module (`BENDINGSIM_API`), so the game module and
  other plugins can call them directly.

### Volumes
`BendingSim::FVolume` (mirrored for Blueprint as `FElementalVolumeState`) holds substance (Earth, Water, Ice, Steam, Fire, Air), mass, temperature, banked latent heat, porosity and saturation (earth), drag coefficient, shape (sphere or capsule), location, and velocity.
- Gases can derive their radius from the ideal-gas law, so a flame **expands as it heats** and shrinks as it dies.
- `UElementalVolumeComponent` attaches a volume to any actor. Every frame it pushes the component's transform and velocity into the simulation (physics body, finite difference, or manual). It receives back the accumulated impulse (applied to the attached simulating body), substance changes, and depletion. Activation registers and deactivation unregisters, so it works with pooled actors.

### Fixed step (`InteractionTickRateHz`, default 60)
1. Recompute derived radii.
2. **Broadphase**: sweep-and-prune along the axis of greatest spread, skipping substance pairs that have no reaction.
3. **Narrowphase**: closest points between core segments give penetration, the overlap midpoint, the A→B normal, **exchange area** (the smaller volume's surface lying inside the larger, continuous from 0 at first touch to 4πr² at full immersion), and immersion fractions.
4. **Reactions** (data-driven, `UElementalReactionSet` in Project Settings, or the built-in set).
5. Ambient heat exchange (Newton cooling, integrated exactly; phase plateaus use the conductive flux).
6. Moisture patches dry; flames standing on wet ground boil it off.
7. Impulses integrate into velocities. Free gas parcels get buoyancy `a = g(ρ_air/ρ − 1)` plus drag.
8. Spawned free volumes merge into nearby ones of the same substance, conserving mass, momentum and heat, up to `MaxFreeVolumes`.
9. Depletion: mass below the minimum, or a flame below `FireExtinguishTemperatureK`.

Owners sync once per frame. Nothing inside the step touches UObjects (the kernel cannot see them), and every outward
callback is deferred until the step completes. Continuous events are aggregated per pair and flushed every
`ReactionEventIntervalS`; discrete ones are flushed every frame.

### Thermodynamics kernel
`Sim/BendingThermo.h` covers sensible heat, latent heat of fusion (334 kJ/kg) and vaporization (2.257 MJ/kg), the ice → water → steam walk, flash vaporization, equilibrium-limited heat flow, and mixing.
- A volume is one phase. Melting and freezing **bank latent heat** until the whole volume converts, like a block of ice sitting at 0 °C.
- Boiling and condensation shed mass, because the vapor leaves.
- Heat put into a flame (a firebender sustaining it, or combustion) is capped at the adiabatic flame temperature
  (`MaxFlameTemperatureK`, 2300 K). `TransferHeat` reports the heat actually accepted, and chi is billed on that.

### The required reactions

| Pair | Reaction class | Physics |
|---|---|---|
| **Fire + Water → Evaporation** | `UElementalReaction_HeatExchange` | `Q = h·A·ΔT·dt` (h = 2×10⁴ W/m²K, the nucleate-boiling range), capped so the gradient cannot invert. 60% of the heat flash-boils the contact layer into **steam**, spawned as a free volume. The water loses that mass, the flame loses that heat (and dies below 700 K). Inelastic momentum coupling drags the flame to the water's velocity: its kinetic force is damped. |
| **Air + Fire → Oxygenation** | `UElementalReaction_Oxygenation` | Air inflow `ṁ = ρ·A·v_n` is entrained (mass, momentum and heat conserved) and burns at 3.03 MJ per kg of air (Thornton's rule) × efficiency (0.6: a luminous flame radiates the rest). This is capped by a fuel limit and by the adiabatic flame temperature. The flame gets hotter and heavier, expands by the ideal-gas law, and is carried by the air's momentum: more range, more damage. |
| **Water + Earth → Mud** | `UElementalReaction_Saturation` | The water's momentum shoves the earth. Pore absorption is `ρ_w·A·(K_infiltration + k·v_impact)`, limited by the pore volume (porosity × volume). The earth gets heavier and wetter, and passes a mud threshold if it is porous enough (`MinMudPorosity`: wet granite stays granite). Terrain keeps `FSurfaceMoisturePatch`es (landscape cannot hold per-location physical materials at runtime), and `GetSurfaceTractionMultiplierAt` scales character friction, braking and acceleration. |
| **Air + Earth → Deflection / Erosion** | `UElementalReaction_AeroDrag` | Quadratic drag `½ρC_dA|v|v` from the relative wind over the immersed part of the body. A bender's compressed air is denser, so it carries proportionally more momentum. Impulse `J`, `Δv = J/m`: the same blast reverses a pebble and barely slows a boulder. The impulse is capped at a full inelastic merge so light bodies never overshoot the wind. Loose, dry earth erodes above a critical dynamic pressure. Newton's third law: the air loses the momentum. |

The built-in set also registers Fire + Ice (melting), Steam + Ice, Fire + Earth, and Air + Water / Ice / Steam (wind pushes water and clears steam clouds).

### Events → presentation
`OnReaction` carries both kinds of event:
- **Continuous** events (evaporation, oxygenation, saturation, deflection, erosion, condensation) are aggregated per pair every `ReactionEventIntervalS` and carry `MassRateKgS` and `PowerW`. Bind those straight to Niagara spawn rates.
- **Discrete** events (melting, freezing, mud formed, extinguished) arrive at frame end.

`OnFreeVolumeSpawned` / `OnFreeVolumeRemoved` announce ownerless steam clouds so presentation can attach a pooled Niagara component to each.

Debug: `Bending.Interaction.DebugDraw 1` draws every volume colored by substance, plus the moisture patches.

## Training ground

The playable sandbox is the kernel plus four more engine-free pieces in `Sim/`. Both front ends use them: the
browser build (`Tools/Sandbox3D`, WebAssembly + three.js) and the Unreal build (`Sandbox/` in the plugin,
`Source/Avatar`). A technique is defined once and behaves the same in both.

| Kernel piece | What it holds |
|---|---|
| `BendingTerrain.h` — `FTerrain` | A heightfield of soil over bedrock (`BedrockDepthCm`, 300 cm). Earthbending is a brush edit (disc, ring or band with a smooth falloff) that **moves soil**: every raise takes its volume from somewhere else, so total soil volume is conserved exactly. The work billed is the change in the soil's potential energy. `RemoveSoil` / `AddSoil` exchange soil with rocks pulled out of or crumbled back into the ground. `Raycast`, `GetHeightAt` and `GetNormalAt` follow the same triangulation the renderers draw (diagonal (x,y)→(x+1,y+1)), so what you see is what you stand on. A dirty rectangle tells renderers which cells changed. |
| `BendingWaterWhip.h` — `FWaterWhip` | A position-based-dynamics chain of 16 points (550 cm, 20 kg). Each segment is an owned Water **capsule volume in the `FSimWorld`**, so the whip boils against fire, soaks soil, shoves rocks and freezes like any other water. The hand drives it with a spring-damper whose acceleration is capped (160 m/s²): the bender's force budget is physical. A lash is a straightening wave that runs from the hand to the tip toward the aim in 0.35 s, and holding keeps the whip extended. Freezing extracts `m(cΔT + L_f)` segment by segment from the hand outward, so running short of chi freezes part of it. Release removes the segments and hands each parcel (position, velocity, mass, temperature) to the owner. Per frame: `PreStep` → world step → `PostStep`. |
| `BendingTechniques.h` | The 14 techniques: element, input slot, Startup / Active / Recovery frames, flat chi and stamina, whether held. `FTechniqueTuning` holds every physical parameter (masses, speeds, temperatures, brush sizes, chi exchange rates). Recipe functions build the matter (`MakeFlame`, `MakeBentAir`, `MakeWaterBall`, `MakeRock`) and the brushes (`EarthWallBrushes`, `RaiseGroundBrushes`, …). |
| `BendingArena.h` | The training ground: a 96 × 96 m terrain (241² samples, 40 cm cells) with a plaza, hills, two pond basins and a boundary ridge, the prop placements (stones, rocks, boulders, soil clods, dummies, braziers, ice blocks), each prop's physical volume, and the player start. |

### Unreal sandbox (`Plugins/Bending/.../Sandbox`, `Source/Avatar`)

Nothing in it needs an asset. Meshes are engine basic shapes tinted through `BasicShapeMaterial`, the terrain is a
procedural mesh, input actions and mapping context are created at runtime, moves are created from the technique
table, and the HUD draws on the canvas.

| Class | Role |
|---|---|
| `AAvatarGameMode` | Spawns and builds an `ABendingSandboxArena` in `InitGame` when the level has none, and picks its player start. The default map is the engine's empty `Entry` map. |
| `ABendingSandboxArena` | Builds the layout: terrain, pond water, props, player start, and sun, sky and fog when the level has no directional light. Draws steam clouds (free gas volumes) and wet ground (moisture patches) with instanced meshes. Ponds give and take back water. |
| `ABendableTerrain` | Owns the `FTerrain`. Renders it as 32 × 32-cell procedural mesh chunks with vertex colours (grass, earth, rock, sand, and fresh soil wherever it was bent). Rebuilds only dirty chunks each tick. Collision lives in a hidden second section, re-cooked at most every 0.1 s. Wakes rigid bodies whose ground moved. |
| `AAvatarCharacter` | A basic-shape body (about 22 parts) with procedural walk, jump and casting poses, tinted by stance. Builds its Enhanced Input objects at runtime, grants the four sandbox disciplines, and scales friction, braking and acceleration by the ground's traction (mud). |
| `UBendingTechniqueMove` / `UBendingTechniqueAbility` | One move per technique, built at runtime (`UBendingSandboxLibrary::CreateTechniqueDisciplines`), and one ability class for all of them. GAS and `UBendingComponent` run them like any move (costs, frame-data phases, input buffer, cancel windows), and the ability forwards each phase to the technique component. |
| `UBendingTechniqueComponent` | Does the physics of each technique: aim (camera trace, falling back to a terrain raycast), the whip, rocks pulled out of the ground (at most 10 thrown rocks; the oldest crumbles back into the soil), walls, pillars and pits, fire, air, the air jump. Every joule goes through `SpendChiForEnergy`, and the effect is scaled by what was granted. |
| `ABendingWaterWhipActor` | Owns an `FWaterWhip` whose segments live in the interaction subsystem's `FSimWorld`. Per frame: `PostStep` → `SetControl` from hand and aim → `PreStep`; the subsystem steps the world at the end of the frame. Draws water and ice with instanced spheres and ellipsoids. |
| `ABendingProjectile` | Bent fire, air or water in flight with a `UElementalVolumeComponent`. Applies reaction impulses as `Δv = J/m`. On landing a flame burns on the ground, a water ball soaks in (mud) or rejoins a pond, and air flows along the ground. |
| `ABendingPropActor` | Props with their kernel volume. Rocks, clods and dummies simulate physics and receive reaction impulses. Braziers keep a flame burning (water puts it out, flame relights it). Ice blocks melt into the soil. |
| `AAvatarHUD` | Crosshair, chi and stamina, the stance's four techniques, a Startup / Active / Recovery frame bar with the cancel window, a feed of what each technique did and every reaction, mud traction, and the controls panel. |

The interaction subsystem tells the two kinds of owned volume apart. A slot whose owner pointer is explicitly null
belongs to a native owner (the whip), which consumes its own results. A *stale* pointer means a component died
without unregistering, and that volume is removed.

## Verification

`Tests/run_all.sh` builds the kernel with clang++ (`-Wall -Wextra -Werror -Wshadow`) and runs five suites, then
builds both WebAssembly modules and checks them against the native results.

- **Thermodynamics & kernel math (39 checks):** heat capacities, latent-heat walks, flash boiling, equilibrium caps,
  ideal-gas radius, `KExp`/`KCbrt`/`KSin`/`KCos`/`KAtan2` against libm, and segment closest points against brute force.
- **Reaction scenarios (47 checks):** conservation unit tests (mass, momentum, heat, Δv ratios), then each scenario
  driven by `Tools/SimDemo` the way an element actor would drive it. Two more checks cover determinism and performance.
- **Terrain & earthbending (28 checks):** interpolation and raycasts on the shared triangulation, conservation (300
  random pillars, walls and pits move 684 m³ of soil and the total does not change), work equals the potential
  energy gained, the bedrock floor, and dirty regions.
- **Water whip (30 checks):** drawing and holding, lash reach, the force budget (control weaker than gravity cannot
  hold the water up), whip against fire, freeze and thaw, partial freezing when short of chi, release, following a
  sprinting bender, and determinism.
- **3D sandbox (47 checks):** the training ground driven through `Tools/Sandbox3D` with player inputs: walking,
  jumping, every technique, chi, determinism and cost.
- **WebAssembly parity:** both freestanding wasm builds replay their sessions and must match the native digests exactly.

| Scenario | What happens (measured) |
|---|---|
| Evaporation | An 18 m/s fire blast into a held water wall: 82.5 g of water boils into steam, the blast slows to 11.9 m/s and goes out at 0.43 s, and the steam rises at 3.5 m/s. |
| Oxygenation | At 0.9 s the fed flame is 1106 K against 748 K unfed. Its radius grows from 71 to 116 cm and it lasts 3.4 s instead of 1.1 s. It burns 1.42 kg of air, exactly the mass it gained. |
| Mud | A splash on a soil clod: the soil absorbs 16.9 kg of water (saturation 0.64), turns to mud and is shoved 56 cm. Granite absorbs 0.38 kg. Ground traction drops to 0.35. |
| Deflection | A compressed air jet reverses a 0.3 kg pebble from −15 to +9.6 m/s. A 1.4 t boulder only slows from −15 to −14.64 m/s. The bender pays 545 kJ (109 chi). |
| Freeze | Freezing a 12 kg water whip costs exactly `m(cΔT + L_f)` = 4.76 MJ (19 chi). A fire blast then gets the ice only 4.1% of the way to melting and goes out. |
| Drying | A sustained flame dries 18 kg of mud: traction recovers from 0.39 to 1.00 by 3.7 s, for 55.7 MJ (223 chi), and the flame never exceeds 2300 K. |
| Performance | 900 interacting volumes step in about 0.4 ms (3,212 reacting contacts per step). |

| Training ground | What happens (measured) |
|---|---|
| Moving | Walk 5.0 m/s, sprint 8.0 m/s. Jump apex 114 cm (`v²/2g` = 117 cm). |
| Earth Wall | A wall 180 cm tall from 11.2 t of soil for 118 kJ. A player walking into it is stopped. |
| Rock Throw | A 320 kg rock pulled out of the ground leaves at 20.2 m/s and comes to rest 51.9 m away. |
| Raise / Lower Ground | 2 s of Raise Ground lifts a 104 cm pillar; 1.5 s of Lower Ground digs a 53 cm pit. |
| Water Whip | The lash tip peaks at 35.4 m/s. Held in a brazier, the whip puts it out in 1.62 s, losing 0.18 kg to steam. |
| Freeze | The 20 kg whip freezes for 7.94 MJ (32 chi). |
| Water Blast | Where it lands on soil, traction drops to 0.35. |
| Air Blast | A 60 kg dummy is shoved 40 cm. A 600 kg boulder does not move. |
| Ground Flame | Melts a 40 kg ice block in 20 s; the bender pours in 32.9 MJ. |
| Cost | A 922-frame scripted session runs at 0.016 ms per frame. |

**Bending Physics Lab.** `Tools/SimDemo/build_sandbox.sh` writes `Tools/SimDemo/build/BendingLab.html`, a single
self-contained page that embeds the kernel as WebAssembly (about 120 KB). It provides:
- the scenarios, with pause, frame step, restart and slow motion, plus a sandbox where you drag to throw fire, water, earth and air, and freeze or heat what you hit;
- live per-volume state, a chart of each scenario's key quantity, a reaction log with totals, the ground traction gauge and the bender's chi bill;
- sliders for the reaction parameters.

**Bending Training Ground.** `Tools/Sandbox3D/build_web.sh` writes `Tools/Sandbox3D/build/BendingTrainingGround.html`,
the 3D sandbox in a browser: the same arena, controls and techniques as the Unreal build.

## Open world

- Per-world subsystem: nothing to place in World Partition cells, and nothing leaks across worlds or PIE sessions.
- Cost scales with *active* volumes (O(n log n) sweep), never with world size. Volumes are transient and live around the player.
- Positions are doubles end to end (Large World Coordinates).
- Montages load asynchronously when a discipline is granted, not at activation.
- Commit assets through Git LFS (`.gitattributes` is set up).
- The sandbox terrain is one 96 m heightfield. An open world needs it streamed in tiles around the player, with the
  landscape as the source of the soil heights and bedrock.

## Multiplayer notes (single player today)

The GAS layer is network-shaped: abilities are `LocalPredicted`, the ability system replicates in `Mixed` mode, and attributes replicate. What still has to change before online play:
- Granted moves and disciplines are only tracked with authority. Owning clients would need to rebuild `GrantedMoves` from replicated ability specs, since each spec's source object is its move.
- Input reaches abilities locally only. Server-side charge timing would need replicated input events.
- `SpendChiForEnergy` returns 0 without authority. Element physics is meant to run on the server.
- Terrain edits and the whip are local. They would need replicated brush edits (deterministic, so the edit list is enough) and a server-owned whip.

## Next milestones

1. **First Unreal build** of the sandbox (see below), then playtest the tuning in engine.
2. **Animation**: replace the basic-shape body and procedural poses with a skeletal mesh and montages carrying `Bending Phase` notifies. The frame data and abilities already support both.
3. **Presentation subsystem**: map reaction events and volumes to Niagara user parameters (a ribbon skin for the whip, flame and steam systems, mud decals). Gameplay never reads them back.
4. **Earthbending on real ground**: drive `FTerrain` from landscape tiles, physical material → density and porosity, rocks as pre-fractured Geometry Collections.
5. **Pooling**: `UBendingPoolSubsystem` for projectiles and props. Niagara uses its own component pooling (`ENCPoolMethod::AutoRelease`).
6. **Airbending**: Chaos field velocity for loose bodies, a point wind source for cloth, and a render-target imprint for foliage.

## First build

This code was written without compiling against the engine. Expect a short round of fixes on the first build.

1. Install UE 5.8, right-click `Avatar.uproject` → *Generate project files*, then build `AvatarEditor` (Development Editor).
2. Open the project and press Play. The arena builds itself; no assets or Blueprints are needed.
3. Run `Tests/run_all.sh` any time the kernel changes.

What to check first if something looks wrong:
- **Compile errors** most likely come from engine APIs whose exact signatures could not be checked here: the
  `FCanvasTextItem` and font measuring calls in `AAvatarHUD`, `UInstancedStaticMeshComponent::BatchUpdateInstancesTransforms`,
  the `FColor` overload of `UProceduralMeshComponent::CreateMeshSection`, and `UInputMappingContext::MapKey` plus
  its modifiers.
- **Ground invisible from above** (visible from below): the terrain's triangle winding is reversed for this engine
  version. Reverse the index order in the one `Chunk.Triangles.Append` line in `ABendableTerrain::CreateChunks`.
- **Everything grey or untinted**: the sandbox tints `/Engine/BasicShapes/BasicShapeMaterial` through its `Color`
  vector parameter, and the terrain uses `/Engine/EngineDebugMaterials/VertexColorMaterial` (falling back to a flat
  green tint). The vertex-colour material may render unlit. If either differs in 5.8, change the path or parameter
  name in `UBendingSandboxLibrary` / `ABendableTerrain`, or give the terrain a lit material that reads vertex colour.
- **Looking up and down feels inverted**: flip the `Negate` modifier on the mouse Y axis in `AAvatarCharacter`.
- **Packaged build missing meshes**: `Config/DefaultGame.ini` cooks `/Engine/BasicShapes` and
  `/Engine/EngineDebugMaterials`; add any other engine content the sandbox loads by path. Steam, wet ground and the
  whip are instanced meshes tinted with `BasicShapeMaterial`. If that material is not flagged for instanced static
  meshes, the editor compiles the permutation on the fly, but a packaged build draws them with the default material.
  The fix is a project copy of the material with the flag set.
- **CommonUI warnings at startup**: the plugin is still enabled in `Avatar.uproject`, but the sandbox does not use it
  and no longer sets its viewport client. Disable the plugin if its warnings get in the way.

To build a character from assets instead, the earlier path still works: create input actions and a mapping context,
a `BendingInputConfig`, one `BendingMoveDefinition` per move, a `BendingDiscipline`, and `BP_AvatarCharacter` with
mesh, anim BP (root motion from montages) and `DefaultDisciplines`. On each montage, add `Bending Phase` notify states
and a Motion Warping window (Warp Target Name = `BendingTarget`).
