# Roadmap: from sandbox demo to game

Where the bending sandbox stands, what would make it better, in what order, and what you can do to move it along.

## Where it stands

**Playable today (browser build):** a third-person training ground with four elements and five techniques each, all
running on the shared C++ physics kernel (247 tests, bit-exact WebAssembly).

| Area | What works |
|---|---|
| Bending | Water whip from ponds or barrels (lash, freeze, release, blast, ice daggers), rock throw, walls, pillars, pits, earthquake, fire blast, flame stream, ground flame, jet dash, ring of fire, air blast, gust, air jump, air scooter, tornado and fire tornado. |
| The field | Fire burns banners, straw, crates and dummies and spreads between them; water puts it out; lanterns light; crates smash; barrels burst and soak the ground; ice melts; mud is slippery. |
| Fights | Dummies with health; damage numbers, health bars, hit flash, hit-stop, combos, knockouts and respawns. |
| Look | Cel-shaded cartoon style, a character in each nation's clothes, spring-driven animation, stylized fire, water and wind. |
| Onboarding | Eight training goals with toasts; a controls card; a technique table with frame data. |
| Sound | Procedural (no audio files): whooshes, the whip's crack, splashes, steam hiss, fire crackle that grows with the fires near you, wind for tornadoes, stone thuds, smashing wood, hits, K.O. and goal chimes. M mutes. |

**Unreal build:** the same techniques, field and dummy health, drawn with engine basic shapes and an on-canvas HUD.
It has never been compiled (no engine where it was written); two read-through reviews fixed what they could find.

## The biggest gaps (honest list)

1. **Sound is procedural and browser-only.** It covers every action, but real recorded or designed sounds (and the
   Unreal side) would be a big step up.
2. **Nothing fights back.** Dummies take hits but never attack, so there is no defending, dodging or reading an
   opponent: the heart of Avatar fights.
3. **No defence or mobility moves** beyond jumps and dashes: no blocks, shields, parries or dodge.
4. **The Unreal build looks like a prototype**: basic shapes, no materials, no Niagara effects, no skeletal animation.
5. **One small arena** with no goals beyond the training checklist.

## Next milestones, in order

1. **Sound in Unreal.** The browser's cues (done, procedural) as MetaSounds or recorded sounds, triggered by the same
   simulation events.
2. **First Unreal build.** Compile, fix what the engine reports, play the arena. (Needs you: see below.)
3. **Defence and mobility.** One block per element with a parry window on its first frames (earth wall, water
   shield, fire burst, air deflection), and a dodge roll. The frame-data system already supports cancel windows.
4. **A sparring partner.** An AI bender that circles, attacks with a few techniques, blocks, and has health: a real
   duel in the training ground. Starts as a state machine, later a behaviour tree in Unreal.
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
