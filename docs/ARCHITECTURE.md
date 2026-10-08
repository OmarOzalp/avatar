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
                                                     Element actors (next milestone), each with a
                                                     UElementalVolumeComponent
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

- **Engine units** for anything passed straight to engine APIs: cm, cm/s, kg·cm/s (the unit `AddImpulse` expects). Field names carry `Cm`, `CmS`, `KgCmS`.
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
- Thermal: freezing 10 kg of water from 15 °C is 4.0 MJ, which is 40 chi at 100 kJ/chi.

## Interaction simulation

### Volumes
`FElementalVolumeState` holds substance (Earth, Water, Ice, Steam, Fire, Air), mass, temperature, banked latent heat, porosity and saturation (earth), drag coefficient, shape (sphere or capsule), location, and velocity.
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

Owners sync once per frame. Nothing inside the step touches UObjects, and every outward callback is deferred until the step completes.

### Thermodynamics kernel
`Physics/ElementalThermoKernel.h` is engine-independent and unit-tested (`Tests/PhysicsKernel/run.sh`). It covers sensible heat, latent heat of fusion (334 kJ/kg) and vaporization (2.257 MJ/kg), the ice → water → steam walk, flash vaporization, equilibrium-limited heat flow, and mixing.
- A volume is one phase. Melting and freezing **bank latent heat** until the whole volume converts, like a block of ice sitting at 0 °C.
- Boiling and condensation shed mass, because the vapor leaves.

### The required reactions

| Pair | Reaction class | Physics |
|---|---|---|
| **Fire + Water → Evaporation** | `UElementalReaction_HeatExchange` | `Q = h·A·ΔT·dt` (h = 2×10⁴ W/m²K, the nucleate-boiling range), capped so the gradient cannot invert. 60% of the heat flash-boils the contact layer into **steam**, spawned as a free volume. The water loses that mass, the flame loses that heat (and dies below 700 K). Inelastic momentum coupling drags the flame to the water's velocity: its kinetic force is damped. |
| **Air + Fire → Oxygenation** | `UElementalReaction_Oxygenation` | Air inflow `ṁ = ρ·A·v_n` is entrained (mass, momentum and heat conserved) and burns at 3.03 MJ per kg of air (Thornton's rule) × efficiency. This is capped by a fuel limit and by the adiabatic flame temperature. The flame gets hotter and heavier, expands by the ideal-gas law, and is carried by the air's momentum: more range, more damage. |
| **Water + Earth → Mud** | `UElementalReaction_Saturation` | The water's momentum shoves the earth. Pore absorption is `ρ_w·A·(K_infiltration + k·v_impact)`, limited by the pore volume (porosity × volume). The earth gets heavier and wetter, and passes a mud threshold. Terrain keeps `FSurfaceMoisturePatch`es (landscape cannot hold per-location physical materials at runtime), and `GetSurfaceTractionMultiplierAt` scales character friction, braking and acceleration. |
| **Air + Earth → Deflection / Erosion** | `UElementalReaction_AeroDrag` | Drag `½ρC_dA|v|v` plus the **overpressure** of compressed air `(ρ/ρ₀ − 1)·P₀·A`, weighted to peak while the wave front crosses the body. Impulse `J`, `Δv = J/m`: a gust scatters pebbles and barely nudges a slab. The impulse is capped at a full inelastic merge so light bodies never overshoot the wind. Loose, dry earth erodes above a critical dynamic pressure. Newton's third law: the air loses the momentum. |

The built-in set also registers Fire + Ice (melting), Steam + Ice, Fire + Earth, and Air + Water / Ice / Steam (wind pushes water and clears steam clouds).

### Events → presentation
`OnReaction` carries both kinds of event:
- **Continuous** events (evaporation, oxygenation, saturation, deflection, erosion, condensation) are aggregated per pair every `ReactionEventIntervalS` and carry `MassRateKgS` and `PowerW`. Bind those straight to Niagara spawn rates.
- **Discrete** events (melting, freezing, mud formed, extinguished) arrive at frame end.

`OnFreeVolumeSpawned` / `OnFreeVolumeRemoved` announce ownerless steam clouds so presentation can attach a pooled Niagara component to each.

Debug: `Bending.Interaction.DebugDraw 1` draws every volume colored by substance, plus the moisture patches.

## Open world

- Per-world subsystem: nothing to place in World Partition cells, and nothing leaks across worlds or PIE sessions.
- Cost scales with *active* volumes (O(n log n) sweep), never with world size. Volumes are transient and live around the player.
- Positions are doubles end to end (Large World Coordinates).
- Montages load asynchronously when a discipline is granted, not at activation.
- Commit assets through Git LFS (`.gitattributes` is set up).

## Multiplayer notes (single player today)

The GAS layer is network-shaped: abilities are `LocalPredicted`, the ability system replicates in `Mixed` mode, and attributes replicate. What still has to change before online play:
- Granted moves and disciplines are only tracked with authority. Owning clients would need to rebuild `GrantedMoves` from replicated ability specs, since each spec's source object is its move.
- Input reaches abilities locally only. Server-side charge timing would need replicated input events.
- `SpendChiForEnergy` returns 0 without authority. Element physics is meant to run on the server.

## Next milestones

1. **Pooling**: `UBendingPoolSubsystem` for element actors. Niagara uses its own component pooling (`ENCPoolMethod::AutoRelease`).
2. **Earthbending**: landscape and static-mesh query (physical material → density, porosity, boulder set); a pre-fractured Geometry Collection pool rising from a crater decal; a PD controller (`F = Kp·e − Kd·v`, capped by `MaxBendingForceN`) so mass is felt; impact impulses on ragdolls and destructibles.
3. **Waterbending**: a position-based-dynamics chain (16–32 capsule volumes) as the whip body, a Niagara ribbon or mesh skin on top, and freezing via `HeatToFreeze` → dynamic-mesh collision → rigid ice.
4. **Firebending**: a projectile or stream feeding heat into its volume, Niagara Simulation Stage visuals, and an ignition/dissipation heat model on targets (GE-driven burn).
5. **Airbending**: an expanding compressed-air shell volume, Chaos `UFieldSystemComponent` linear-velocity fields, a transient point wind source for cloth, and a render-target imprint for foliage.
6. **Presentation subsystem**: maps reaction events and volumes to Niagara user parameters.

## First build checklist

This code was written without compiling against the engine. Expect a short round of fixes on the first build.

1. Install UE 5.8, right-click `Avatar.uproject` → *Generate project files*, then build `AvatarEditor` (Development Editor).
2. Run `Tests/PhysicsKernel/run.sh` any time the kernel changes.
3. In the editor, create:
   - input actions and a mapping context;
   - a `BendingInputConfig`;
   - one `BendingMoveDefinition` per move;
   - a `BendingDiscipline`;
   - `BP_AvatarCharacter` with mesh, anim BP (root motion from montages), input assets and `DefaultDisciplines`;
   - a Blueprint game mode using it.
4. On each montage, add `Bending Phase` notify states and a Motion Warping window (Warp Target Name = `BendingTarget`).
