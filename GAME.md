# Crab Sim: design brief

## Pitch

You are a crab on a tidal shoreline. Real-time, short rounds. Find food, avoid gulls, hold territory, molt to grow.

## Core loop (draft)

1. Low tide: forage the exposed flats, dig in, fight other crabs for burrows.
2. Incoming tide: retreat or get swept; pick a hole and hold it.
3. High tide: rest, molt if fed enough. Bigger shell, bigger claws, slower.
4. Repeat. Round ends when you are eaten, or when you molt N times.

## Input model

Pointer-first. Nothing requires a key, the wheel, or a timing window.

- Left button, quick click: walk to that spot and stop.
- Left button, held: walk toward the cursor and keep following it.
- Right click: dash toward the cursor, 1.2 s cooldown.
- Camera is fixed-angle and follows the crab. No rotation, no zoom.

The dash is on the right button, not a drag, because a held left button already
means "follow the cursor". Drag and hold are the same gesture there.

Pinch is not built yet. It needs a button, so it will get one that is not left
click.

## Movement (decided)

Free movement with a scuttle bonus. The crab can head anywhere, but it turns so a
side faces its heading, and side-on travel is faster (450 uu/s) than forward
or backward travel (250 uu/s). It never turns more than 90 degrees to do it.
Rules live in `Source/CrabSim/CrabMovementMath.h`, with unit tests.

## Open questions

- Engine: UE 5.8, C++ project, Keel to be vendored once the loop needs it.
- Tone: deadpan nature documentary, or full chaos?
