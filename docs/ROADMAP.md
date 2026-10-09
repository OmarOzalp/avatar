# Roadmap: from sandbox demo to game

Where the bending sandbox stands, what would make it better, in what order, and what you can do to move it along.

## Where it stands

**Playable today (browser build):** a third-person training ground with four elements and five techniques each, all
running on the shared C++ physics kernel (272 tests, bit-exact WebAssembly).

| Area | What works |
|---|---|
| Bending | Water whip from ponds or barrels (lash, freeze, release, blast, ice daggers), rock throw, walls, pillars, pits, earthquake, fire blast, flame stream, ground flame, jet dash, ring of fire, air blast, gust, air jump, air scooter, tornado and fire tornado. |
| The field | Fire burns banners, straw, crates and dummies and spreads between them; water puts it out; lanterns light; crates smash; barrels burst and soak the ground; ice melts; mud is slippery. |
| Fights | A sparring partner who fights back: it circles, telegraphs, throws blasts, combos and rings of fire, guards, sidesteps, staggers and can be knocked out. A guard (C) and a parry that sends a blast back. Player health and knockdowns. Dummies with health; damage numbers, health bars, hit flash, hit-stop, combos, a duel scoreboard. |
| Look | Cel-shaded cartoon style, a character in each nation's clothes, spring-driven animation, stylized fire, water and wind. |
| Onboarding | Ten training goals with toasts (including a parry and a duel win); a controls card; a technique table with frame data. |
| Sound | Procedural (no audio files): whooshes, the whip's crack, splashes, steam hiss, fire crackle that grows with the fires near you, wind for tornadoes, stone thuds, smashing wood, hits, K.O. and goal chimes. M mutes. |

**Unreal build:** the same techniques, field and dummy health, drawn with engine basic shapes and an on-canvas HUD.
It has never been compiled (no engine where it was written); two read-through reviews fixed what they could find.

## The biggest gaps (honest list)

1. **Sound is procedural and browser-only.** It covers every action, but real recorded or designed sounds (and the
   Unreal side) would be a big step up.
2. **One opponent, one element.** The sparring partner is a firebender with three attacks; there is no earth, water
   or air opponent yet, and no difficulty setting. It lives in the browser build only (not yet in Unreal).
3. **Defence is one guard.** No dodge roll, and the guard is the same for every element (only its look changes).
4. **The Unreal build looks like a prototype**: basic shapes, no materials, no Niagara effects, no skeletal animation.
5. **One small arena** with no goals beyond the training checklist.

## Next milestones, in order

1. **Sound in Unreal.** The browser's cues (done, procedural) as MetaSounds or recorded sounds, triggered by the same
   simulation events.
2. **First Unreal build.** Compile, fix what the engine reports, play the arena. (Needs you: see below.)
3. **The sparring partner in Unreal.** Move its state machine into the engine-free kernel (one brain for both
   builds, tested once), then an Unreal actor that feeds it what it sees and acts on its orders, with the guard,
   parry and player health in the technique component and HUD.
4. **More of a duel.** Opponents of the other three elements (a waterbender who needs a pond, an earthbender who
   raises walls), a difficulty setting, element-specific guards (an earth slab that really blocks, water that
   douses) and a dodge roll.
5. **Unreal presentation.** Toon post-process and materials matching the browser, Niagara systems for fire, water
   and air driven by the simulation's events, a skeletal character with montages carrying the existing Bending
   Phase notifies.
6. **More of the world reacting.** Trees and grass that catch fire, stone pillars that crack, dug pits that fill
   with water, wind that bends grass and banners everywhere.
7. **Modes.** Target practice against the clock, waves of dummies, an element-restricted challenge, then a second
   arena (a rooftop, a frozen lake).
8. **Later:** online duels (the ability layer is already network-shaped; the physics would run on the server).

## What you can do

- **Build it in Unreal 5.8** and send the first compile errors (copy them from the Output Log or the Error List).
  That unblocks everything on the Unreal side. Screenshots of what you see after pressing Play help too.
- **Play the browser build and be specific**: which move feels slow or weak, where the camera gets in the way,
  what you expected to happen and did not. Short clips or screenshots are ideal.
- **Choose the art direction.** Send two or three reference images of the look you want (the show's style, a
  Fortnite-like style, something else). For Unreal, decide whether to use free assets (Lyra's character and
  animations ship with the engine; Mixamo animations; Fab's free monthly packs) or buy a stylized character and a
  VFX pack.
- **Decide the core of the game**: a sandbox to play in, a duel game, or an adventure. That decides whether the
  sparring partner, more arenas or a story comes first.
- **Playtest with a friend**: watch which goals they finish, where they get stuck, and what they try that the game
  does not support yet. Those are the next features.
- **Merge when ready.** Everything is on the `claude/bending-core-foundation` branch; a pull request into the main
  branch is a good checkpoint once the Unreal build compiles.
