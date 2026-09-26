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

Four holes from the dune foot down to the low flats, and up to three more the crab digs itself (see Digging).
Click one to walk there and dig in. Inside, the crab is safe from the surge and recovers grip fast. Water more
than 50 cm over a hole floods it and forces the crab out. Higher burrows flood later, so the choice is between a
low burrow near the food and a high one that is safe all tide. The highest of the four never floods, and it is
where the sea washes the crab up.

## Food (built)

Fiddler crabs sift mud with the small claw for algae and detritus. Seven food patches lie on the exposed flats,
in the same place every round: two poor ones high up (richness 0.40 and 0.45, dry until late in the rise), three
in the middle (0.60 to 0.80) and two low ones (0.90 and 1.00) that are the richest and flood first, about 25 s in.
On the ground a patch is a flat green disc of algae mats and pellets, 130 uu across from the middle. They dull
and thin as the patch is eaten.

- Click a patch (any click within 200 uu of its middle): the crab walks to the middle and feeds by itself. Holding
  the button on the patch it is already sifting keeps it sifting. The crab itself beats the patch under it: click
  the crab and it dances instead.
- Feeding moves richness from the patch into the crab's Food store (0 to 1, starts at 0.25) at about 0.025 food a
  second from a full patch, easing to under a third of that as it runs out, so a full patch lasts a little over a
  minute and a poor one (0.4) about 40 s. Sifting is slow on purpose: it is what makes food matter, and the tide,
  not the food, then sets the pace of a round (see Molting).
- It stops when the patch is bare ("Patch empty"), when the crab is full ("Fed", at 0.98), when the crab is told
  to go anywhere else, dashes, dances or digs in, and when the surge takes hold.
- A patch that has had more than 10 cm of water over it is fresh again, back to its own full richness, once the
  water has left it. The tide refreshes the flats, even the high ones.
- Food drains by 0.004 a second, always: a whole tide costs about 0.7, so foraging has to go on. Low food does not
  hurt the crab. It gates digging (0.30) and molting (0.80). The Food bar sits beside the grip bar, with a tick at
  0.30, where a burrow becomes affordable, and one at 0.80, where a molt does.

Until the skeletal crab has a feeding clip, the crab dips toward the mud and its small claw scoops to its mouth
twice a second, as a stand-in.

## Digging (built)

The dig button is on the HUD, bottom right: 140 px square at 720p and it grows with the window. It is labelled DIG
with its cost, greyed out when it cannot be used, and a line above it says why.

- Click it and the crab digs a new burrow where it stands: four seconds of standing still, with a progress bar
  along the foot of the button. It costs 0.30 food, paid when the hole is finished.
- It needs Food of at least 0.30, dry sand (no water at all over the spot, and no surge), not from inside a
  burrow, at least 250 uu from the middle of any burrow or food patch, and no more than three dug burrows alive.
  Otherwise the button says why and the message line says "Cannot dig: too close to a burrow", "...not enough
  food" and so on.
- Any movement command cancels it for free: a walk, a follow, a dash, a click on a burrow or a patch, a dance.
  So does the water reaching the spot.
- A click on the button is only ever a click on the button: it never also walks the crab to the ground behind it.
- The new hole is an ordinary burrow: click it to dig in, water more than 50 cm over it floods it, grip comes back
  as fast. Dug holes stay for the round. Dig high on the dunes and the hole is safe all tide. Dig low, next to the
  food, and it floods sooner. That is the point.

## Molting (built)

A fiddler crab molts hidden in a burrow, soft and vulnerable. The molt button is on the HUD, straight above the dig
button and the same size (140 px square at 720p, growing with the window). It is labelled MOLT with its cost, greyed
out when it cannot be used, and a line above it says why.

- It is enabled only when the crab is inside a burrow (any, authored or dug), has Food of at least 0.80, and no molt
  is under way. Otherwise a click on it does nothing but say why on the message line: "Molt needs a burrow" or
  "Molt needs more food".
- Click it and the crab stays in the burrow for ten seconds. A ring fills round the button with the seconds left in
  the middle. The stand-in crab settles half out of its hole and pulses. It costs 0.80 food, paid when the molt is
  done, not before.
- Done: Molts goes up by one ("Molted (1 of 3)", and a pip beside the food bar fills), grip is refilled to full, and
  the crab grows 8 percent (24 percent after three). The growth is how the crab looks, not how it moves: the mesh
  scales, the capsule, the speed and every click radius stay as they were. The crab swells over a second and stays in
  view for a moment so it can be seen, then sinks out of sight.
- Leaving cancels it, for free: any click elsewhere, a dash, a new order. The water flooding the burrow mid-molt
  cancels it too, and leaves the crab soft for 30 s: its grip cannot rise above half, shown as a SOFT tag with the
  seconds left and a mark on the grip bar. A flood that catches the crab out of a molt does not.
- A click on the molt button is only ever a click on the button: it never walks the crab, and never digs it out.

Balance. A good round is meant to take about 8 to 10 minutes at the default tide speed, so about one molt a tide,
and food is never the only limit. A molt is 0.80 food, about half a minute of sifting at the slow feed rate, then ten
undisturbed seconds in a burrow that will not flood. The first low water is enough for one molt. Then the water takes
the flats and floods the low burrows, the crab has to sit out the high tide in a high one, and the next molts wait
for the flats to come back. Burrow choice is the game: a low burrow next to the food is a gamble (it floods first),
a high one is safe but a long walk from the food, and a molt that the sea interrupts leaves the crab soft. Every
number that sets the pace is in one place, `CrabMolt::Tuning` in `Source/CrabSim/CrabMoltMath.h`, and the feed rate
(0.025) in `CrabFoodMath.h`.

## Round (built)

The round is won at three molts: the crab is fully grown.

- A centered panel says "Fully grown!" and shows the time taken, the best time this session (kept in memory, no save
  file), molts, burrows dug, and food eaten, over a big NEW ROUND button.
- While the panel is up the tide stands still, hunger stops, and feeding, digging, dancing, dashing and walking are
  ignored. Only the button does anything.
- NEW ROUND resets the world: the tide is back at low water and running, food is back at 0.25, every patch is full and
  fresh, the dug burrows are gone, Molts is 0, grip is full and the crab is back at the start, facing the sea.
- There is no losing state in this pass. Gulls come later.

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
- Left click a food patch: walk there and feed until it is bare, the crab is full, or you send it elsewhere.
- Left click the HUD's dig button: dig a new burrow where the crab stands. A big target, no key, no timing.
- Left click the HUD's molt button, in a burrow: molt for ten seconds. Same size, above the dig button. Click
  elsewhere to leave and cancel.
- Left click NEW ROUND on the results panel: start again.
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

## Loop (built, first pass)

1. Low tide: forage the flats, dig or claim a burrow, molt if the store is at 0.80 and the burrow will stay dry for ten
   seconds.
2. Rising tide: retreat, choose a hole, hold it. A flood that catches a molt leaves the crab soft.
3. High tide: sit it out in a high burrow, rest, dance, molt if fed enough.
4. Repeat until the third molt. The round is won at three: the results panel, best time, NEW ROUND. Nothing eats
   the crab yet, so there is no losing state.

## Open questions

- What does dancing do? What eats you (gulls)?
- Hunger has no teeth. Low food gates digging and molting. Should it also slow the crab or weaken its grip?
- Pinch: a button that is not left click. Right click is the dash.
- Tone: deadpan nature documentary, or full chaos?
