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
Click one, or press BURROW (see Go-to buttons), to walk there and dig in. Inside, the crab is safe from the surge
and recovers grip fast. Water more than 50 cm over a hole floods it and forces the crab out. Higher burrows flood
later, so the choice is between a low burrow near the food and a high one that is safe all tide. The highest of the
four never floods, and it is where the sea washes the crab up.

A crab dug in does not vanish. The model sinks out of sight, and a stand-in shows over the edge of the hole: eye
stalks with cream eyes and dark pupils, the top of the shell, and the tips of the two claws (the big one on the
left as it faces the camera). It comes up in under a second, bobs a little, grows with each molt, and is drawn about
30 percent bigger than the crab so it reads at a glance from the camera's distance. It looks at the camera whichever
way the crab was heading. It is not shown while the crab molts (the molt pose, half out of the hole and pulsing,
takes over) and goes the moment the crab comes out.

## Food (built)

Fiddler crabs sift mud with the small claw for algae and detritus. Seven food patches lie on the exposed flats,
in the same place every round: two poor ones high up (richness 0.40 and 0.45, dry until late in the rise), three
in the middle (0.60 to 0.80) and two low ones (0.90 and 1.00) that are the richest and flood first, about 25 s in.
On the ground a patch is a flat green disc of algae mats and pellets, 130 uu across from the middle. They dull
and thin as the patch is eaten.

- Click a patch (any click within 200 uu of its middle, or inside its on-screen zone, see Go-to buttons and click
  zones), or press FOOD: the crab walks to the middle and feeds by itself. Holding the button on the patch it is
  already sifting keeps it sifting. The crab itself beats the patch under it: click the crab and it dances instead.
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
with its cost, greyed out when it cannot be used, and a line above it says why. The message line does not say it
again.

- Click it and the crab digs a new burrow where it stands: four seconds of standing still, with a progress bar
  along the foot of the button. It costs 0.30 food, paid when the hole is finished.
- It needs Food of at least 0.30, dry sand (no water at all over the spot, and no surge), not from inside a
  burrow, at least 250 uu from the middle of any burrow or food patch, and no more than three dug burrows alive.
  Otherwise the button says why ("Too close to a burrow", "Not enough food" and so on), once: the message line
  leaves out a refusal that the reason line above the button already shows.
- Any movement command cancels it for free: a walk, a follow, a dash, a click on a burrow or a patch, a dance.
  So does the water reaching the spot.
- A click on the button is only ever a click on the button: it never also walks the crab to the ground behind it.
- The new hole is an ordinary burrow: click it to dig in, water more than 50 cm over it floods it, grip comes back
  as fast. Dug holes stay for the round. Dig high on the dunes and the hole is safe all tide. Dig low, next to the
  food, and it floods sooner. That is the point.

## Go-to buttons and click zones (built)

Two more big buttons on the HUD, in a column left of MOLT and DIG: FOOD above BURROW. Each is 170 by 100 px at 720p
(120 by 80 is the least at any size), grows with the window, has the same 10 px hit margin as the others, and clears
the tide gauge, the food bar, the molts pips, the grip bar, MOLT, DIG and the help panel. There are 110 px between
that column and MOLT and DIG, for their reason lines. A press on either is only ever a press on the button: it never
also walks the crab to the ground behind it, and holding on it does not follow.

- FOOD: the same as clicking the best food patch that has richness left, is not under water (over 10 cm, the soak
  depth) and will not be soaked before the crab could get there and feed for 8 s (the walk is timed at 350 uu/s, under
  its side speed). The best, not the nearest: a patch scores its richness over (1 + distance / 1500 uu), so a rich
  patch a walk away beats a poor one at hand, and a patch holding under 0.25 is passed over unless nothing richer is
  usable. It is for one click when the food is off screen. Out of a burrow it comes out first. Greyed, with a line
  above it, when there is none ("No dry food left") or the crab is full ("Not hungry").
- BURROW: the same as clicking the safest burrow the crab can reach: of the burrows that are not flooded and stay dry
  until the crab is in (3 s to spare) and within 3500 uu, the one with the highest floor (floors within 10 uu count as
  level, and the nearer wins), otherwise the nearest dry one at any distance. Greyed ("Already dug in") when the crab
  is in a burrow, and ("No dry burrow near") when none is dry.
- Each shows what it does under its name ("best patch", "safest burrow"). The rules are in `CrabGotoMath.h`, and the
  two numbers that shape the FOOD pick (`DistanceFalloff`, `MinRichness`) are in `CrabGoto::Tuning`.

A burrow or patch is easy to click at any camera range. Its click zone is its world radius (burrow 100 uu, patch
200 uu) or an ellipse on screen at least 90 px wide and 60 px tall at 720p (it grows with the window), whichever is
larger, on each axis. The zone is an ellipse, not a box, so the ground between two targets still walks. It never
reaches under a HUD button: a press inside a button's hit area is the button's, and a burrow or patch under a button
is reached with FOOD or BURROW. Priority is unchanged: burrow, then the crab (dance), then patch, then ground.
Holding on a zone keeps the order. The rules are in `CrabPickMath.h`.

## Molting (built)

A fiddler crab molts hidden in a burrow, soft and vulnerable. The molt button is on the HUD, straight above the dig
button and the same size (140 px square at 720p, growing with the window). It is labelled MOLT with its cost, greyed
out when it cannot be used, and a line above it says why.

- It is enabled only when the crab is inside a burrow (any, authored or dug), has Food of at least 0.80, and no molt
  is under way. Otherwise a click on it does nothing but say why, on the line above the button: "Molt needs a burrow" or
  "Molt needs more food". The message line does not say it again.
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
undisturbed seconds in a burrow that will not flood. The first low water is not enough for one. The crab starts at
0.25, the low, rich patches are under water about 25 s in, and the poor high ones (0.40 and 0.45) hold little: a full
round played by a bot peaked at food 0.735 in the first low water. A patch only refills once the sea has soaked it
and left, so the first molt comes in the first tide cycle after the high water, about 200 s in (227 s in the latest
playtest), and then about one a tide (417 s and 596 s there, a round of 9:56 with the FOOD button choosing the best
patch; when it chose the nearest, poor patches and bare ones stretched the same bot's round to 14:41). With gulls on
and answered, the same bot's round is 10:03 to 10:04 (see Gulls). In between the water takes the flats and floods the low
burrows, the crab has to sit out the high tide in a high one, and the next molts wait for the flats to come back.
Burrow choice is the game: a low burrow next to the food is a gamble (it floods first), a high one is safe but a long
walk from the food, and a molt that the sea interrupts leaves the crab soft. Every number that sets the pace is in one
place, `CrabMolt::Tuning` in `Source/CrabSim/CrabMoltMath.h`, and the feed rate (0.025) in `CrabFoodMath.h`.

## Round (built)

The round is won at three molts: the crab is fully grown. It is lost when a gull catches the crab (see Gulls).

- A centered panel says "Fully grown!" and shows the time taken, the best time this session (kept in memory, no save
  file), molts, burrows dug, and food eaten, over a big NEW ROUND button.
- While the panel is up the tide stands still, hunger stops, and feeding, digging, dancing, dashing and walking are
  ignored. Only the button does anything.
- NEW ROUND resets the world: the tide is back at low water and running, food is back at 0.25, every patch is full and
  fresh, the dug burrows are gone, Molts is 0, grip is full and the crab is back at the start, facing the sea.
- A gull's catch ends the round the same way: the panel is titled "Eaten by a gull" (in a warm red, with a dry line
  under it), shows the same stats (time, best time, molts so far, burrows dug, food eaten) over the same NEW ROUND
  button, and the tide, hunger and every order stand still. The best time is kept only for fully grown rounds: an
  eaten round sets none and shows "none yet" until the first win.

## Dance (built)

Click the crab. It turns to face the camera and does the claw-wave dance until you click it again or click
away. It cannot dance in the surge, in a burrow, or mid-dash. Until the skeletal crab exists the stand-in
crab bounces and alternates its claws on a 120 bpm beat.

What dancing is for: it scares a gull. A crab that dances while a stalking gull is within 900 uu sends it off after
4 s (see Gulls). While dancing the crab stands still, so a gull within 150 uu catches it: dancing near a very close gull
is a gamble.

## Gulls (built)

Tone: deadpan nature documentary. A gull is a threat that is slow to arrive, loudly announced and answered with one
click. It is never a twitch: from the first circle to the earliest possible catch there are at least 25 s (25.6 s as
the numbers stand), and a crab that is moving, or in a burrow, is never caught. The rules are a pure state machine in
`Source/CrabSim/CrabGullMath.h`, deterministic given a seed (the beach's own gull seed and the round's number), with
every number in `CrabGull::Tuning`. The actor that runs it and draws it is `CrabGull`.

- When. A gull begins circling when the crab has been out of a burrow and not molting, in water under 20 uu deep, for
  25 s in a row, and no gull has been about for the last 45 s (60 s after one was scared off). Never in the first 60 s
  of a round, never two at once, never with the results panel up.
- Circling (12 s, no threat). A loop 400 uu across, 900 to 1300 uu from the crab toward the sea, 350 uu up, with its
  shadow on the ground. The HUD announces it.
- Landing (13 s). It glides down for 4 s and lands 1400 to 1800 uu from the crab: on a food patch that is free (holds
  food, dry) if one lies in that range, else on open dry flats. It then eats where it stands for 9 s: a patch loses
  0.04 richness a second, 0.36 in all, so the gull costs the crab food as well as time. A ring shows under it once it
  is down, so it can be seen on the sand.
- Stalking. It walks at the crab at 130 uu/s. The crab walks at 250 to 450, so a crab that moves always outruns it.
- Lunge and catch. Within 150 uu of a crab that is outside a burrow and standing still (speed at most 40 uu/s), or in
  water over 60 uu deep, where its speed is cut, the gull lunges for 0.6 s and, if the crab is still in reach and
  still not moving, catches it. A lunge that finds the crab walking, or dug in, is off. A moving crab and a crab in a
  burrow are never caught.
- It goes. It gives up after 40 s on the hunt, or when the crab has been in a burrow for 5 s while it is down, or when
  scared, and flies off for 6 s. The next gull waits for the cooldown.

The answers, neither of which needs speed:

- BURROW. One click: the crab walks to the safest burrow (moving all the way, so it cannot be caught) and digs in, and
  five seconds later the gull gives up. Press it when the gull has landed, or feed on while it is still far.
- Dance. A crab that dances while a stalking gull is within 900 uu scares it: after 4 s of dance the gull flies off and
  does not come back for 60 s. The gull keeps walking while the crab dances and a dancing crab stands still, so the
  dance must be under way early: it works if the gull has 4 s of walking left before it is within 150 uu, that is,
  begun while the gull is still more than about 700 uu away. Begun later, the gull reaches the crab first and catches it.

The warning. When a gull begins circling the HUD shows a "Gull!" banner, top left, with a line for what it is doing ("It
is circling.", "It has landed.", "It is coming. Press BURROW."), and a big arrow at the edge of the view pointing toward
it with its distance in metres. It works when the gull is off screen, which it mostly is: the camera shows about 640 uu
to each side and 1000 ahead. The arrow keeps clear of the tide gauge, the food bar, the pips, the buttons and the help
line (a pure function, `CrabHud::GullArrowBox`), clear of the gull when it is on screen, and is not clickable. The
help line says what to do ("Gull! Press BURROW, or dance.", "Gull outside. Stay in until it goes."). There is no sound
yet: no sound asset exists, and nothing was fetched.

Look. Stand-ins built from engine shapes, like the crab: a white body, head and tail, grey wings with dark tips that
flap in the air and fold on the ground, an orange bill and legs, eyes, a soft shadow on the ground (smaller the higher
it flies) and the ring. It walks with a bob, leans in to stalk and drops its head to lunge. About 240 uu long, so it
reads at 1280 by 720.

Logs and switches. Events `gull_circling`, `gull_landed`, `gull_stalking`, `gull_scared`, `gull_left` (with a reason)
and `gull_catch`, then `round_eaten`, and a `CRABSIM_GULL` line while one is about. `CrabSim.Gulls 0` keeps gulls away
(the tours and the older live scenarios run with it); `CrabSim.GullForce 1` (test only, off by default) sends a gull as
soon as there is none and the crab is out of its burrow.

Balance. The aim: a crab that answers its gulls is caught rarely, one that ignores them usually, and answering costs
little pace. Measured two ways, with the numbers as they stand in `CrabGull::Tuning`:

- A fast headless simulation of the real state machine against a crab that plays like the playtest bot
  (`Tests/CrabGullBalanceTests.cpp`, 400 rounds of 600 s each): a crab that presses BURROW when a gull lands (feeding on
  with a far gull half the time, until it is within 700 uu) was caught in 0 rounds, meeting about 5.6 gulls a round and
  spending about 84 s of it answering; one that ignored them was caught in all 400, on average at 166 s.
- The live playtest bot at the default tide, no cheats. Answering with BURROW: two full rounds, each 4 gulls met and 4
  answered, no catch, fully grown in 10:03 and 10:04 (9:56 with no gulls: the answer overlaps the time the crab
  sits out the high water anyway). Ignoring gulls, `PLAYTEST_GULLS=ignore PLAYTEST_ROUNDS=6`: eaten in 6 rounds of 6,
  every time by the first gull, 2:50 to 3:10 into the round (the first circle comes about 2:19 in: the crab is in a
  burrow for the first high water, and needs 25 s of open low water first).

The numbers that pace it: `LandEatSeconds` (9 s, and with the circle and the glide it makes the 25 s of warning even
when the crab walks right up to a landed gull), `StalkSpeed` (130), `GiveUpSeconds` (40), the spawn timers
(`FirstMinute`, `OutsideSeconds`, `Cooldown`) and `EatRate` (what a gull costs the crab in food).

## Input model

Pointer-first. Nothing requires a key, the wheel, or a timing window. An optional one-stick scheme for a
player with no other input is built on top of it: see "One-stick mode" below.

- Left click the ground: walk there and stop.
- Hold left: walk toward the cursor and keep following it.
- Left click a burrow: walk there and dig in. Click your own hole to stay in, click elsewhere to come out.
- Left click a food patch: walk there and feed until it is bare, the crab is full, or you send it elsewhere.
- Left click the HUD's FOOD button: the same, for the best patch (rich, not too far). It needs no aim and works when
  the patch is off screen. BURROW: the same, for the safest burrow. Big targets, left of MOLT and DIG.
- Burrows and patches are ellipses at least 90 by 60 px on screen at any camera range, as well as their world radius.
- Left click the HUD's dig button: dig a new burrow where the crab stands. A big target, no key, no timing.
- Left click the HUD's molt button, in a burrow: molt for ten seconds. Same size, above the dig button. Click
  elsewhere to leave and cancel.
- Left click NEW ROUND on the results panel: start again (after a win, or after being eaten).
- Left click the crab: dance on or off. Begun early enough, a dance scares off a gull.
- A gull: no input of its own. The BURROW button is the answer, a dance begun in time the second. The banner and arrow
  are drawn only: neither takes a click, and a click under the arrow is an ordinary click.
- Right click: dash toward the cursor, 1.2 s cooldown.
- Camera is fixed-angle and follows the crab. No rotation, no zoom.
- The help is a dark panel, bottom left, in two lines at least 16 px tall ("Click: walk. Hold: follow." and
  "Right click: dash. Click crab: dance."). After the first minute of a round it gives way to one short hint for what
  the crab is doing ("Click elsewhere to come out.", "Molting. Click elsewhere to cancel.", "Click: walk. Right
  click: dash.", and with a gull down "Gull! Press BURROW, or dance." or, in a burrow, "Gull outside. Stay in until it goes."). It stays clear of the grip and food bars and the molts pips, and is not shown while the results panel is up.

The dash is on the right button, not a drag, because a held left button already means "follow the cursor".
Drag and hold are the same gesture there. Pinch is not built yet and will get a button that is not left click.

## One-stick mode (built)

An optional control scheme for a player whose only input is one analog stick (a wheelchair joystick, arriving
as a virtual gamepad's left stick) with a left mouse click as an alternative "tap". No other buttons, and no
timing window a stick with tremor or limited travel cannot meet: every threshold below is a CVar. Toggle it with
`CrabSim.OneStick 1`, the F2 key (one key, for a player who types on an on-screen keyboard), or the "ONE STICK"
HUD button (top right, clickable whichever way the game is being played). The choice is saved to
GameUserSettings.ini (`[CrabSim] OneStick`) and read back on startup, so it does not need setting again every
launch. Automated runs (anything launched with -unattended: the tests, live tests and recordings) neither read nor
write it. While on, the help line at the bottom left talks about the stick, not the mouse. Off, the game is exactly as the rest of this document describes and the stick is ignored. On, left
clicks that are not on a HUD button stop doing ground clicks (follow, dance, burrow, patch) and become taps
instead; the HUD buttons and right-click dash are unchanged.

Two modes:

- **MENU.** A vertical column on the HUD, large text, a highlighted cursor box: MOVE, FOOD, BURROW, DIG, MOLT,
  DANCE, DASH, in that order, wrapping at both ends. An up or down tap moves the cursor. A left or right tap, or
  a lone left click, selects the highlighted item. FOOD, BURROW, DIG and MOLT do exactly what their HUD buttons
  do and stay in MENU; DANCE toggles the dance and stays in MENU; DASH dashes in the crab's facing direction (a
  point ahead of it, into the same `TryDash` a right click uses) and stays in MENU; MOVE enters STEER. Once the
  round is over (won or eaten, whenever the results panel's own NEW ROUND button shows), the list falls back to
  that one item, NEW ROUND, in place of the seven above (`CrabStick::ActiveItemCount`/`ActiveItemAt`): selecting
  it does exactly what the results panel's button does, so a stick-only player can start the next round without
  a mouse. The round ending or a new one starting always snaps the cursor to the first item of whichever list is
  now showing and forces MENU, so the cursor is never left pointing at an item that just vanished.
- **STEER.** The stick's analog XY drives the crab directly and screen-relative (stick up is up on screen: the
  camera looks along world +X with +Y on its right, so world X takes the stick's Y and world Y takes its X),
  through the same movement path "hold left to follow the cursor" uses: a point some distance ahead of the crab,
  reset every tick, so it never arrives and stops on its own. Walk speed scales with how far the stick is pushed,
  `CrabStick::SteerSpeedMultiplier`: `CrabSim.StickMinSpeed` (default 0.35) at Inner deflection, ramping linearly
  to 1 at full deflection, monotonic and clamped at both ends. It is a plain multiplier on `CrabPawn`'s existing
  walking speed (alignment to the travel direction still sets the base, as it always did), not a change to that
  model. `SetMoveTarget` and `ClearMoveTarget` both reset it to 1, so it can never leak into a mouse walk, a
  go-to walk, a dash (which never reads it at all), or anything the tide or a gull does to the crab; leaving
  STEER, and turning one-stick off, both go through `ClearMoveTarget` or reset it directly.

A triple tap toggles MENU and STEER from either side. From MENU it resumes STEER; from STEER it returns to MENU
and the crab stops.

Tap and chain rules, pure and unit tested in `Source/CrabSim/CrabStickMath.h` (`FStickTapDetector`,
`FClickTapDetector`, `FMenuState`):

- A stick tap: the stick's magnitude rises above `CrabSim.StickTapOuter` (default 0.5) and falls back below
  `CrabSim.StickTapInner` (default 0.2, so hysteresis keeps a tremor wobbling round one threshold from ever
  making a tap) within `CrabSim.StickTapMax` seconds (default 0.4). Direction is the dominant axis at the peak
  magnitude. Held longer than the max, it is not a tap: in STEER that is just steering, in MENU it does nothing
  (no auto-repeat).
- A click tap: a left press and release within the same max, not on any HUD button. Directionless. A longer
  press is not a tap and has no meaning of its own: the player's own drag tool latches on a stationary press
  around half a second, and a long press must never fight that gesture.
- Taps whose gap, one's end to the next one's start, is at most `CrabSim.StickTapGap` seconds (default 0.6) chain
  together, stick and click taps alike. The third tap of a chain always fires the toggle above and resets the
  chain, whichever mode it started in.
- Reversibility in MENU: an up or down move applies at once; if the chain reaches three, the cursor is put back
  where it stood before the chain's first tap, undoing every move the chain made. A select is irreversible, so
  it is held pending and only committed when the chain closes with fewer than three taps in it (the last pending
  select wins, if there was more than one in the chain); if the chain reaches three the pending select is
  dropped instead. A tap taken in STEER only ever counts toward that third tap: there is no cursor or pending
  select there to undo.

HUD, while it is on: the action column (only in MENU: it would sit in the way of the view in STEER; once the
round is over it is just the one NEW ROUND item), a mode banner ("MENU: up/down, left/right picks, triple-tap
to steer", "STEER: triple-tap for menu", or "Left/right or click: NEW ROUND"), and chain pips under it ("N of
3") so the gesture building can be seen. Laid out in `CrabHudMath.h` alongside the rest of the HUD's layout,
with its own unit tests. No mouse-wheel dependence anywhere.

With `CrabSim.StateLog 1`, one-stick events log in the existing `CRABSIM_EVENT` format: `onestick_on`,
`onestick_off`, `mode` (detail `mode=menu` or `mode=steer`), `cursor` (`cursor=<item>`), `select` (`select=<item>`,
logged once it commits, `select=NEW_ROUND` included), `tap` (`dir=up`, `down`, `left`, `right` or `click`), and
`toggle` for the triple tap itself (separate from `mode`, so a plain MOVE select entering STEER is not mistaken
for one). The `onestick` scenario in `Scripts/live-test.sh` drives a virtual gamepad's stick (`Scripts/live/gamepad.py`,
uinput, the same way the live test's virtual mouse works) through the menu, STEER at full and partial deflection,
and the toggle button; forcing a round to end without the tide or a gull is not cheap to script live, so
NEW ROUND is unit tested instead. `RECORD_TOUR=onestick Scripts/record.sh` records a tour of the same ground.

The camera is fixed, so there is no camera steer yet: STEER only ever drives the crab.

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
   seconds. (The first low water only gets the store to about 0.7: the first molt is in the second one.)
2. Rising tide: retreat, choose a hole, hold it. A flood that catches a molt leaves the crab soft.
3. High tide: sit it out in a high burrow, rest, dance, molt if fed enough.
4. All the while, a gull: when the crab has been out in the open at low water long enough one circles, lands (and eats
   a patch) and stalks. Hide in a burrow (BURROW) or dance early and it goes. A crab that stands still in the open
   with the gull at its feet is eaten and the round is lost.
5. Repeat until the third molt. The round is won at three: the results panel, best time, NEW ROUND.

## Open questions

- Hunger has no teeth. Low food gates digging and molting. Should it also slow the crab or weaken its grip?
- Pinch: a button that is not left click. Right click is the dash.
- Gulls have no voice or art yet: a screech when one starts circling, and a real animated gull, wait for assets.
