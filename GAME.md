# Crab Sim: design brief

## Pitch

You are a crab on a tidal shoreline. Real-time, short rounds. Find food, avoid gulls, hold territory, molt to grow.

## Core loop (draft)

1. Low tide: forage the exposed flats, dig in, fight other crabs for burrows.
2. Incoming tide: retreat or get swept; pick a hole and hold it.
3. High tide: rest, molt if fed enough. Bigger shell, bigger claws, slower.
4. Repeat. Round ends when you are eaten, or when you molt N times.

## Input model

Pointer-first. Click or hold to move, click to pinch, drag for a sidestep burst.
No scroll wheel, no cue-timed windows, no key combos required.
Camera follows automatically. Drag bar for any slider.

## Open questions

- Engine: UE 5.8, C++ project, Keel to be vendored once the loop needs it.
- Sideways-only movement as a mechanic, or free movement with a sideways speed bonus?
- Tone: deadpan nature documentary, or full chaos?
