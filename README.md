# Crab Sim

A crab simulator game. You are a crab. Scuttle, dig, pinch, molt, survive the tide.

Unreal Engine 5.8, C++. Design brief in `GAME.md`.

## Status

Playable: an animated fiddler crab on a sloped 3D beach with a tide that rises and falls, translucent water with
foam, burrows that flood, grip that the surge drains, a dance, food patches to sift, burrows the crab digs
itself with a big on-screen button, big FOOD and BURROW buttons that walk the crab to the best food patch and the
safest burrow with one click, a crab that peeks out of its hole, and molting: three molts in a burrow, each ten
seconds and 80% of the food, win the round. Gulls are built: one circles, lands (and eats a food patch), then stalks
the crab on foot, and catches it only if it stands still outside a burrow. BURROW is the one-click answer, a dance
begun in time scares it off, and a catch ends the round ("Eaten by a gull"). A colony of non-playable crabs lives
under burrow 0 (the dune-foot burrow, the highest, which never floods): dug in there, DIG becomes DOWN, taking the
crab into a side-cutaway "ant farm" view where FOOD becomes EAT (from the colony's store), BURROW becomes UP (back
to the beach), and DIG helps dig the colony's own tunnels, rolling sand pellets up to a mound at the mouth. Pinching
is not built. The crab, sand and water are real assets
(`Art/README.md`). Rocks, shells and plants still wait on CC0 environment
assets that need network access to fetch.

## Controls

- Left click: walk to that spot.
- Hold left: follow the cursor.
- Left click a burrow: walk there and dig in. Click elsewhere to come out.
- Left click a food patch (green mud): walk there and feed until it is bare or you are full.
  Burrows and patches are click targets at least 90 by 60 px on screen (an ellipse) at any camera range.
- Left click the FOOD button (left of MOLT and DIG): walk to the best food patch (richness over distance, dry, and
  dry long enough) and feed. BURROW, below it: walk to the safest burrow you can reach and dig in. No aim needed, and
  they work when the target is off screen.
- Left click the DIG button (bottom right): dig a new burrow where the crab stands. Four seconds of standing still,
  costs 30% food, needs dry sand away from other burrows and patches. Any walk cancels it.
- Left click the MOLT button (above the dig button): molt, in a burrow with at least 80% food. Ten seconds, costs 80%
  food. Any click elsewhere leaves the burrow and cancels it for free. The sea flooding the burrow mid-molt cancels it
  and leaves the crab soft (half grip) for 30 s. Three molts win the round.
- Left click NEW ROUND on the results panel: start again.
- Left click the crab: dance. Start it while a gull is still far off and 4 s of it scare the gull away for a minute.
- A gull comes with a "Gull!" banner (top left) and an arrow at the edge of the view toward it, with its distance. It
  is never a twitch: 25 s at least from the first circle to a possible catch, and a crab that is moving, or in a
  burrow, is never caught. Press BURROW (or dance early) and it gives up.
- Right click: dash toward the cursor.
- Hold right and drag: orbit the camera freely round the crab (sideways all the way round, up and down over it).
- Dug into burrow 0 (the highest, at the dune foot), the DIG button reads DOWN: takes the crab into the colony
  living under it, a side-cutaway view. Down there: left click walks the cutaway the same way, click the crab to
  dance (colony crabs wave back), FOOD reads EAT (eat from the colony's store), BURROW reads UP (back to the
  beach, dug in and peeking as before), and DIG reads "help dig" (walk to the tunnel being dug and dig, faster
  than a colony crab). Digging rolls a sand pellet every 40 uu the crab holds until it carries it up to the mound
  at the mouth; no dash and no camera orbit down there. See `GAME.md`, "Colony".

## Layout

- `Source/CrabSim/`: game module
  - `CrabMovementMath.h`, `CrabTerrainMath.h`, `CrabTide.h`, `CrabSurvivalMath.h`, `CrabFoodMath.h`,
    `CrabDigMath.h`, `CrabMoltMath.h` (molting, the round, and every pacing number in `CrabMolt::Tuning`),
    `CrabGotoMath.h` (what FOOD and BURROW pick), `CrabOrbitMath.h` (the right-drag camera orbit), `CrabGullMath.h` (the gull's state machine, spawn rules and every
    number that paces it, in `CrabGull::Tuning`), `CrabPickMath.h` (the on-screen click zones), `CrabHudMath.h`: the
    rules and the HUD layout, pure functions
  - `CrabPawn`: the crab, its camera, dance, burrow, grip, food, digging, molting and the round, its go-to
    buttons' logic, its shape-built stand-in visual (and the stand-in that peeks over a hole), and its trip down
    into the colony and back up (`GoDown`/`GoUp`, underground walking, digging and eating)
  - `CrabGull`: the gull actor: it runs `CrabGullMath.h` against the crab and the beach, eats a patch, ends the
    round on a catch, and draws itself from engine shapes (body, flapping wings, bill, legs, shadow, ring)
  - `CrabPlayerController`: pointer input, the right button's dash or camera orbit, and (underground) clicks and
    holds in the colony's cutaway
  - `CrabBeach`: the terrain and water meshes, the tide clock, burrows (authored and dug), food patches, rocks and props
  - `CrabColony`, `CrabColonyNpc`, `CrabColonyView`, `CrabColonyMath.h`, `CrabCarryComponent`: the colony under
    burrow 0 (its plan, jobs, digging and hatching are pure rules in `CrabColonyMath.h`), its non-playable crabs'
    bodies, the side-cutaway look, and the held-pellet visual. See `GAME.md`, "Colony"
  - `CrabHUD`: tide gauge, grip and food bars, the FOOD, BURROW, dig and molt buttons (DOWN/EAT/UP/"help dig"
    underground), molt pips, soft tag, messages, the help panel, the gull banner and arrow, the colony panel, results
    panel (fully grown, or eaten by a gull)
  - `CrabSimGameMode`: wires the above together (and puts the gull and the colony in the world)
  - `Tests/`: automation tests (rules, beach, pawn, controller, gull, colony, game mode, and a fast headless balance
    simulation of a crab that answers its gulls against one that ignores them). The module builds without unity files,
    because the test files each bring their own `TestFlags` into scope.
- `Config/`: project ini files
- `Scripts/`: build, test, play, live test, record, screenshot
- `Art/`: Blender and Unreal Python that build the assets, plus the art contract
- `Content/`: generated assets (LFS, see `.gitattributes`)

## Commands

```
Scripts/build.sh          build the editor target
Scripts/test.sh           headless automation tests (filter as argument)
Scripts/play.sh           start the game for a person to play
Scripts/live-test.sh      drive the real game with a virtual mouse and check the log
Scripts/shot.sh out.png   screenshot the running game window
Scripts/record.sh [out.mp4]   record a scripted tour of the game as video (default videos/crab-sim-tour.mp4)
RECORD_TOUR=molt Scripts/record.sh   record the molt tour instead (default videos/crab-sim-molt.mp4)
RECORD_TOUR=playtest SPEEDUP=4 CRF=30 Scripts/record.sh   a bot plays one whole round (10 to 15 min, videos/crab-sim-playtest.mp4)
RECORD_TOUR=orbit Scripts/record.sh   the free camera orbit (about 60 s, videos/crab-sim-orbit.mp4)
RECORD_TOUR=colony Scripts/record.sh   the colony under burrow 0 (about 60 s, videos/crab-sim-colony.mp4)
```

`live-test.sh` opens a window and moves the real pointer for a few minutes: one game launch per scenario (`basic`,
`tide`, `forage`, `molt`, `goto`, `gull`, `onestick`, `orbit`, `colony`). Leave the machine alone while it runs. Needs X11 and write access to `/dev/uinput`. It refuses
to start while any other CrabSim UnrealEditor is running.

The `forage` scenario (about a minute, tide frozen at low water) clicks a food patch and checks that the crab
feeds and its food rises, clicks away, clicks the HUD's dig button on dry sand clear of every burrow and patch,
checks that the click ordered no walk, waits for the new burrow and the food it cost, then clicks the new hole and
checks that the crab digs in. Run it alone with:

```
LIVE_SCENARIOS=forage Scripts/live-test.sh
```

The `molt` scenario (about a minute, tide frozen at low water, `CrabSim.FoodFloor 0.9` so it needs no foraging) clicks
the HUD's molt button in the open and checks it is refused with no walk, digs into burrow2, starts a molt, checks the
click did not walk the crab out, clicks elsewhere after 3 s and checks the molt is cancelled and no food is spent,
digs in again and molts to the end: `molt_done` after 10 s, the crab stayed put, 0.80 food paid, `molts=1`, `scale`
1.08, grip full. Run it alone with:

```
LIVE_SCENARIOS=molt Scripts/live-test.sh
```

The `goto` scenario (about a minute, tide frozen at low water) clicks the HUD's FOOD button and checks that the crab
walks to the best patch (patch3, richest for its distance from the start) and feeds there with its food rising, clicks BURROW while
it feeds and checks that it stops, walks to the highest burrow and digs in, then clicks BURROW again while dug in and
checks that the press is refused, orders no walk and leaves the crab where it is. Run it alone with:

```
LIVE_SCENARIOS=goto Scripts/live-test.sh
```

The `gull` scenario (about two minutes, tide frozen at low water, `CrabSim.GullForce 1` so a gull comes as soon as
the crab is out of a burrow) sees a gull circle and land 1400 to 1800 uu away, clicks BURROW and checks that the
crab walks to the burrow without being caught and that the gull gives up (`gull_left reason=burrow`) with the round
still on, then clicks elsewhere to come out and leaves the pointer alone: a new gull stalks and catches the crab
standing still (`gull_catch` within 150 uu, no `gull_scared`, `round_eaten`, `over=1 eaten=1`), and NEW ROUND starts
the round again. The other scenarios run with `CrabSim.Gulls 0`, so a natural gull never interrupts them. Run it alone with:

```
LIVE_SCENARIOS=gull Scripts/live-test.sh
```

The `orbit` scenario (about half a minute, tide frozen) holds the right button and drags: 300 px right turns the
camera about 90 degrees round the crab with no dash while held (`orbit_start`, `orbit_end`), a right click that does
not move still dashes on release and leaves the camera alone, 300 px left turns it back, and 100 px down then up
tilts it about 30 degrees toward overhead and back. Run it alone with:

```
LIVE_SCENARIOS=orbit Scripts/live-test.sh
```

The `colony` scenario (about a minute, tide frozen at low water, `CrabSim.StartFood 0.3`) clicks BURROW, which
reaches burrow 0 (the colony's own entrance, the safest), then clicks the DIG button (now DOWN) and checks
`colony_enter` and `under=1`; clicks FOOD (now EAT) and checks food rises from the colony's store; clicks DIG (now
"help dig") and checks it walks to the active face and digs until it carries a pellet (`carry=1`); clicks BURROW
(now UP) and checks `colony_pellet who=player`, `colony_exit` and `under=0`, back at burrow 0. Run it alone with:

```
LIVE_SCENARIOS=colony Scripts/live-test.sh
```

`record.sh` launches the game, plays a 90 second tour through the same virtual pointer (feed on a patch, dig a
burrow, dig into it, scuttle, dance, dash, then the tide rises and sweeps the crab out) and captures the window with
ffmpeg: about 2 minutes end to end, same rules as the live test (it refuses to start if another CrabSim game is running). Writes the mp4 and an events
file with times in the video. `LIMIT`, `FPS`, `CRF`, `SPEEDUP` and `RECORD_TIDE_SPEED` tune it (the tide runs at 0.75
by default, so the feeding and digging finish before the water reaches the flats), see the script
header. The recorded mp4 and events files stay local (git-ignored).

`RECORD_TOUR=molt Scripts/record.sh` plays `Scripts/live/tour_molt.py` instead and writes `videos/crab-sim-molt.mp4`
(about 85 s, 9 MB, local like the other clip), a short clip of the molt: the molt button refuses in the open, three molts in a burrow with the progress ring and the
crab settling into its hole, the crab climbing out bigger after the first two, the third molt ending the round, the
results panel and NEW ROUND. It runs with `CrabSim.FoodFloor 0.9` and a slow tide, so it needs no foraging.

`RECORD_TOUR=playtest` plays `Scripts/live/playtest.py` instead: a small reactive bot that reads the state log and plays one full
round at the default tide with left clicks only (feed the richest reachable patch, retreat to a high burrow when the sea
rises, molt when fed, dig once when the tide is low, press BURROW when a gull lands and stay in until it has gone,
sifting on with a far gull on every other one), until the results panel (fully grown, or eaten by a gull).
`PLAYTEST_GULLS=ignore` plays as if there were no gulls, and `PLAYTEST_ROUNDS=<n>` plays on with NEW ROUND after a
round a gull ended, so the balance of both can be counted. A patch or burrow that is off screen is
reached with the FOOD or BURROW button, one click, instead of a chain of ground hops; a hop is the fallback when a
button is greyed, and the way to open sand for the dig. The run folder gets `clicks.csv` (every click, with its kind
and time), `playtest.log` (its last line counts the clicks, hops, button presses, gulls met and answered, and clicks a minute) and screenshots (`PLAYTEST_SHOTS=<dir>` to move them): the first feeding, dig, molt, high tide in a burrow, the results panel, and the first gull circling, landed and near. Unlike the tours it takes as long as the
round does (`LIMIT` defaults to 1320 s for it).

`RECORD_TOUR=colony Scripts/record.sh` plays `Scripts/live/tour_colony.py` instead and writes
`videos/crab-sim-colony.mp4` (about 60 s): walk to burrow 0 (the colony's own entrance) and dig in, DOWN into the
cutaway, a beat to look round, walk to the pantry and EAT, walk to the face and DIG until the crab holds a pellet,
carry it up by clicking the top of the entrance shaft as the mound grows, back DOWN, dance near the colony's own
crabs so they wave back, then UP. It runs with `CrabSim.Gulls 0` and `CrabSim.StartFood 0.3`, tide frozen.

Console variables and commands:

- `CrabSim.StateLog 1`: log the crab's state every 0.2 s, including `food=`, `feeding=` (the patch it is on, or -1),
  `dig=` (progress 0 to 1), `dug=` (dug burrows), `molts=`, `molt=` (progress 0 to 1), `soft=` (seconds left),
  `over=` (the round is over), `scale=` (how big the crab looks), `eaten=` (a gull caught it), `under=` (down in the
  colony), `carry=` (holding a rolled pellet) and `rolled=` (pellets rolled to the mound this round), plus where the
  crab, burrows, food patches, the
  dig and molt buttons, the new round button, the FOOD and BURROW buttons (`gofood=`, `goburrow=`) and a gull that is
  coming (`gull=`) are on screen
  (the live test reads it). Events such as `goto_food`, `goto_burrow`, `goto_refused`, `food_begin`,
  `food_end` (with `amount=`), `dig_begin`, `dig_done`, `dig_cancel`, `molt_begin`, `molt_done`, `molt_cancel`,
  `molt_refused`, `soft_begin`, `soft_end`, `round_won`, `round_eaten`, `round_new`, `surge_begin`, `swept_out`,
  the gull's `gull_circling`, `gull_landed`, `gull_stalking`, `gull_scared`, `gull_left` (with `reason=`) and
  `gull_catch`, and the colony's `colony_enter`, `colony_exit`, `colony_dug`, `colony_hatch`, `colony_eat`,
  `colony_forage` and `colony_pellet` (`who=player` or `who=npc`) are logged as `CRABSIM_EVENT`. While a gull is
  about, a `CRABSIM_GULL` line every 0.2 s gives its phase, place, height, distance to the crab and the patch it
  stands on; a `CRABSIM_COLONY` line every second gives the colony's own state (population, food in store over
  capacity, the active dig edge, the mound, and each job's headcount)
- `CrabSim.TideSpeed <n>`: tide clock multiplier. 0 freezes it at low tide, 10 makes a whole tide take 18 s
- `CrabSim.TideTime <s>`: jump the tide clock. 0 is low tide rising, 90 is high tide
- `CrabSim.StartFood <0..1>` and `CrabSim.FoodFloor <0..1>`: test only, both off by default (-1). The first sets the
  crab's food once, the second keeps it from dropping below that. They let a live test or a recording molt without
  foraging. Not for play.
- `CrabSim.Gulls <0|1>`: 0 keeps every gull away (the tours and the older live scenarios run with it), 1 is the game.
- `CrabSim.GullForce 1`: test only, off by default. Sends a gull as soon as there is none and the crab is out of its
  burrow: no first minute, no 25 s wait, no cooldown. The `gull` live scenario runs with it.
- `CrabSim.OrbitDegreesPerPixel <deg>`, `CrabSim.OrbitPitchDegreesPerPixel <deg>`, `CrabSim.OrbitStartPixels <px>`:
  the right-drag orbit's yaw and pitch rates (negative flips one), and how far a right press must move before it
  orbits instead of dashing.
- `CrabSim.CameraDistance <uu>`, `CrabSim.CameraPitch <deg>`: camera. 7000 shows the whole map
