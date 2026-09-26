# Crab Sim

A crab simulator game. You are a crab. Scuttle, dig, pinch, molt, survive the tide.

Unreal Engine 5.8, C++. Design brief in `GAME.md`.

## Status

Playable: an animated fiddler crab on a sloped 3D beach with a tide that rises and falls, translucent water with
foam, burrows that flood, grip that the surge drains, and a dance. Food, gulls and pinching are not built. The
crab, sand and water are real assets (`Art/README.md`). Rocks, shells and plants still wait on CC0 environment
assets that need network access to fetch.

## Controls

- Left click: walk to that spot.
- Hold left: follow the cursor.
- Left click a burrow: walk there and dig in. Click elsewhere to come out.
- Left click the crab: dance.
- Right click: dash toward the cursor.

## Layout

- `Source/CrabSim/`: game module
  - `CrabMovementMath.h`, `CrabTerrainMath.h`, `CrabTide.h`, `CrabSurvivalMath.h`: the rules, pure functions
  - `CrabPawn`: the crab, its camera, dance, burrow and grip, and its shape-built stand-in visual
  - `CrabPlayerController`: pointer input
  - `CrabBeach`: the terrain and water meshes, the tide clock, burrows, rocks and props
  - `CrabHUD`: tide gauge, grip bar, messages
  - `CrabSimGameMode`: wires the above together
  - `Tests/`: automation tests (rules, beach, pawn, controller, game mode)
- `Config/`: project ini files
- `Scripts/`: build, test, play, live test, screenshot
- `Art/`: Blender and Unreal Python that build the assets, plus the art contract
- `Content/`: generated assets (LFS, see `.gitattributes`)

## Commands

```
Scripts/build.sh          build the editor target
Scripts/test.sh           headless automation tests (filter as argument)
Scripts/play.sh           start the game for a person to play
Scripts/live-test.sh      drive the real game with a virtual mouse and check the log
Scripts/shot.sh out.png   screenshot the running game window
```

`live-test.sh` opens a window and moves the real pointer for about a minute.
Leave the machine alone while it runs. Needs X11 and write access to `/dev/uinput`.

Console variables and commands:

- `CrabSim.StateLog 1`: log the crab's state every 0.2 s (the live test reads it)
- `CrabSim.TideSpeed <n>`: tide clock multiplier. 0 freezes it at low tide, 10 makes a whole tide take 18 s
- `CrabSim.TideTime <s>`: jump the tide clock. 0 is low tide rising, 90 is high tide
- `CrabSim.CameraDistance <uu>`, `CrabSim.CameraPitch <deg>`: camera. 7000 shows the whole map
