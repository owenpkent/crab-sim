# Crab Sim

A crab simulator game. You are a crab. Scuttle, dig, pinch, molt, survive the tide.

Unreal Engine 5.8, C++. Design brief in `GAME.md`.

## Status

Playable slice: a crab on a blockout beach. Walk, scuttle and dash with the mouse.
No tide, food or threats yet.

## Controls

- Left click: walk to that spot.
- Hold left: follow the cursor.
- Right click: dash toward the cursor.

## Layout

- `Source/CrabSim/`: game module
  - `CrabMovementMath.h`: scuttle rules, pure functions
  - `CrabPawn`: the crab, its camera, and its shape-built visual
  - `CrabPlayerController`: pointer input
  - `CrabBeach`: procedural sand, dunes, rocks, burrows
  - `CrabSimGameMode`: wires the above together
  - `Tests/`: automation tests
- `Config/`: project ini files
- `Scripts/`: build, test, play and live test
- `Content/`: assets (LFS, see `.gitattributes`). Empty: everything is built in code so far.

## Commands

```
Scripts/build.sh          build the editor target
Scripts/test.sh           headless automation tests (filter as argument)
Scripts/play.sh           start the game for a person to play
Scripts/live-test.sh      drive the real game with a virtual mouse and check the log
```

`live-test.sh` opens a window and moves the real pointer for about a minute.
Leave the machine alone while it runs. Needs X11 and write access to `/dev/uinput`.

Console variables: `CrabSim.StateLog 1` logs the crab's state every 0.2 s,
`CrabSim.CameraDistance <uu>` overrides the camera distance (7000 shows the whole map).
