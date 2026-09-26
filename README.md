# Crab Sim

A crab simulator game. You are a crab. Scuttle, dig, pinch, molt, survive the tide.

Unreal Engine 5.8, C++. Design brief in `GAME.md`.

## Status

Playable: an animated fiddler crab on a sloped 3D beach with a tide that rises and falls, translucent water with
foam, burrows that flood, grip that the surge drains, a dance, food patches to sift, and burrows the crab digs
itself with a big on-screen button. Gulls and pinching are not built. The crab, sand and water are real assets
(`Art/README.md`). Rocks, shells and plants still wait on CC0 environment
assets that need network access to fetch.

## Controls

- Left click: walk to that spot.
- Hold left: follow the cursor.
- Left click a burrow: walk there and dig in. Click elsewhere to come out.
- Left click a food patch (green mud): walk there and feed until it is bare or you are full.
- Left click the DIG button (bottom right): dig a new burrow where the crab stands. Four seconds of standing still,
  costs 30% food, needs dry sand away from other burrows and patches. Any walk cancels it.
- Left click the crab: dance.
- Right click: dash toward the cursor.

## Layout

- `Source/CrabSim/`: game module
  - `CrabMovementMath.h`, `CrabTerrainMath.h`, `CrabTide.h`, `CrabSurvivalMath.h`, `CrabFoodMath.h`,
    `CrabDigMath.h`, `CrabHudMath.h`: the rules and the HUD layout, pure functions
  - `CrabPawn`: the crab, its camera, dance, burrow, grip, food and digging, and its shape-built stand-in visual
  - `CrabPlayerController`: pointer input
  - `CrabBeach`: the terrain and water meshes, the tide clock, burrows (authored and dug), food patches, rocks and props
  - `CrabHUD`: tide gauge, grip and food bars, the dig button, messages
  - `CrabSimGameMode`: wires the above together
  - `Tests/`: automation tests (rules, beach, pawn, controller, game mode)
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
```

`live-test.sh` opens a window and moves the real pointer for a few minutes: one game launch per scenario (`basic`,
`tide`, `forage`). Leave the machine alone while it runs. Needs X11 and write access to `/dev/uinput`. It refuses
to start while any other CrabSim UnrealEditor is running.

The `forage` scenario (about a minute, tide frozen at low water) clicks a food patch and checks that the crab
feeds and its food rises, clicks away, clicks the HUD's dig button on dry sand clear of every burrow and patch,
checks that the click ordered no walk, waits for the new burrow and the food it cost, then clicks the new hole and
checks that the crab digs in. Run it alone with:

```
LIVE_SCENARIOS=forage Scripts/live-test.sh
```

`record.sh` launches the game, plays a 90 second tour through the same virtual pointer (feed on a patch, dig a
burrow, dig into it, scuttle, dance, dash, then the tide rises and sweeps the crab out) and captures the window with
ffmpeg: about 2 minutes end to end, same rules as the live test (it refuses to start if another CrabSim game is running). Writes the mp4 and an events
file with times in the video. `LIMIT`, `FPS`, `CRF`, `SPEEDUP` and `RECORD_TIDE_SPEED` tune it (the tide runs at 0.75
by default, so the feeding and digging finish before the water reaches the flats), see the script
header. The recorded mp4 and events files stay local (git-ignored).

Console variables and commands:

- `CrabSim.StateLog 1`: log the crab's state every 0.2 s, including `food=`, `feeding=` (the patch it is on, or -1),
  `dig=` (progress 0 to 1) and `dug=` (dug burrows), plus where the crab, burrows, food patches and the dig button
  are on screen (the live test reads it). Events such as `food_begin`, `food_end` (with `amount=`), `dig_begin`,
  `dig_done`, `dig_cancel`, `surge_begin` and `swept_out` are logged as `CRABSIM_EVENT`
- `CrabSim.TideSpeed <n>`: tide clock multiplier. 0 freezes it at low tide, 10 makes a whole tide take 18 s
- `CrabSim.TideTime <s>`: jump the tide clock. 0 is low tide rising, 90 is high tide
- `CrabSim.CameraDistance <uu>`, `CrabSim.CameraPitch <deg>`: camera. 7000 shows the whole map
