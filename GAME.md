# Crab Sim: design brief

## Pitch

You are a male fiddler crab on a tidal flat. One claw is enormous and the other is tiny. You forage on the
exposed mud at low tide, dig burrows, hold territory, and wave the big claw at passing crabs. Then the tide
comes in. Real time, short rounds.

Why a fiddler crab: they live exactly where this game happens (tidal flats, burrows, tide), and the claw wave
is a real courtship dance, so "crab dance" is the species' own behaviour, not a bolted-on emote.

## The tide (built)

A 180 second cycle, low to high to low. The sea is ahead of the crab (world +X, the camera looks that way),
the dunes behind it. A swell rides on the tide so the water line breathes in and out.

- Low tide: the flats are open. Nothing costs anything.
- Rising tide: the shoreline sweeps inland. The start point floods about 50 s in.
- Water deeper than 45 cm (the surge) makes the crab lose grip and shoves it: a rising tide pushes it up the
  beach, a falling tide drags it out to sea. Deeper than 60 cm it moves at 60 percent speed.
- Grip is 0 to 1. At 0 the sea carries the crab away and it washes up by the dunes with half its grip back.
- Grip refills on dry sand, and faster inside a burrow.

## Burrows (built)

Four holes from the dune foot down to the low flats. Click one to walk there and dig in. Inside, the crab is
safe from the surge and recovers grip fast. Water more than 50 cm over a hole floods it and forces the crab
out. Higher burrows flood later, so the choice is between a low burrow near the food and a high one that is
safe all tide. The highest never floods.

## Dance (built)

Click the crab. It turns to face the camera and does the claw-wave dance until you click it again or click
away. It cannot dance in the surge, in a burrow, or mid-dash. Until the skeletal crab exists the stand-in
crab bounces and alternates its claws on a 120 bpm beat.

Not built: what dancing does. Ideas: it draws a mate, it stakes a burrow, it faces down a rival.

## Input model

Pointer-first. Nothing requires a key, the wheel, or a timing window.

- Left click the ground: walk there and stop.
- Hold left: walk toward the cursor and keep following it.
- Left click a burrow: walk there and dig in. Click your own hole to stay in, click elsewhere to come out.
- Left click the crab: dance on or off.
- Right click: dash toward the cursor, 1.2 s cooldown.
- Camera is fixed-angle and follows the crab. No rotation, no zoom.

The dash is on the right button, not a drag, because a held left button already means "follow the cursor".
Drag and hold are the same gesture there. Pinch is not built yet and will get a button that is not left click.

## Movement

Free movement with a scuttle bonus. The crab can head anywhere, but it turns so a side faces its heading, and
side-on travel is faster (450 uu/s) than forward or backward travel (250 uu/s). It never turns more than 90
degrees to do it. Rules live in `Source/CrabSim/CrabMovementMath.h`.

## Look

Stylized realism, top notch: a hand-painted hero crab (sandy carapace, turquoise front patch, orange-red major
claw) on a real 3D beach with relief, a tidal creek, translucent water with foam where it meets the sand, wet
sand that darkens at the waterline. Contract and pipeline in `Art/README.md`.

## Loop (draft)

1. Low tide: forage the flats, claim a burrow.
2. Rising tide: retreat, choose a hole, hold it.
3. High tide: rest, dance, molt if fed enough.
4. Repeat. The round ends when you are eaten, or after N molts.

## Open questions

- What does dancing do? What is the food, and what eats you (gulls)?
- Pinch: a button that is not left click. Right click is the dash.
- Engine is decided: UE 5.8, C++. Keel to be vendored once the loop needs it.
- Tone: deadpan nature documentary, or full chaos?
