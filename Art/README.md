# Crab Sim art pipeline and contract

This file is the contract between the people and tools that make art (Blender
scripts, Unreal Python) and the game code in `Source/CrabSim/`. If a name here
changes, the C++ breaks. Change it here first.

## The subject

The crab is a **male fiddler crab** (genus Uca). It lives on tidal flats, digs
burrows, and courts by waving one oversized claw. That is the game: tide,
burrows, territory, dance. One claw is enormous (the major claw), the other is
tiny (the minor claw). Both sit on the crab's **right** and **left**
respectively: major claw on the crab's right.

Look: stylized realism, top notch. Reads clearly from a high camera (about 55
degrees down, roughly 13 m away in game units) and holds up in close-ups.
Silhouette first. Carapace roughly square-trapezoid, eyes on tall stalks at
the front corners, four walking legs a side, two claws, mouthparts. Colour:
sandy-tan carapace with a turquoise-blue patch on the front, mottled
speckles; the major claw is orange-red with a pale tip; legs banded
brown-orange. Not cartoon-flat and not photoreal: a hand-painted game hero.

## Units and orientation (all exported meshes)

- Blender: 1 unit = 1 m. Export with a scale so Unreal receives centimetres.
- The crab is ~100 cm across the carapace, ~220 cm wide with legs and claw.
- After import into Unreal: the crab **faces +X**, its **right side is +Y**, up
  is **+Z**. Origin on the ground plane between the feet (foot tips at Z = 0
  in the idle pose), centred on the carapace.
- Verify orientation and scale in Unreal, not by trusting the exporter flags.

## Directory layout

```
Art/
  README.md            this file
  assets/              provenance, one .md per author, plus manifests
  blender/             Blender scripts (headless): build, rig, animate, fetch, export
  unreal/              Unreal editor Python (headless): build_content.py, build_level.py
  export/meshes/       SM_*.fbx, SK_*.fbx
  export/textures/     T_*.png
  export/anims/        A_*.fbx
  previews/            review renders (PNG)
  source/              downloaded originals, git-ignored, recreated by fetch scripts
```

Scripts must be **idempotent and headless**: `blender -b --factory-startup
--python-exit-code 1 --python <script>` for Blender (5.2 LTS at
`~/.local/bin/blender`), and for Unreal:

```
$HOME/UnrealEngine/UE_5.8/Engine/Binaries/Linux/UnrealEditor-Cmd CrabSim.uproject \
  -run=pythonscript -script=<script.py> -unattended -nullrhi -nosplash -nosound \
  -nopause -stdout -FullStdOutLogOutput
```

`~/dev/muster/Scripts/build-art.sh`, `import-art.sh` and
`~/dev/muster/Art/unreal/build_content.py` show the pattern that already works
on this machine. Copy its habits (success marker line, non-zero exit on
failure, idempotent rebuilds).

## Textures

- 2048 px PNG unless noted. Names `T_<Asset>_<Kind>.png`.
- Kinds: `BaseColor` (sRGB), `Normal` (OpenGL/DirectX: **Unreal wants
  DirectX, green down**; flip if needed), `ORM` (linear: R = ambient
  occlusion, G = roughness, B = metallic).

## Skeleton contract (SK_FiddlerCrab)

Bone names are used by name from C++. Hierarchy in bold.

- `root` (at origin) > `body` (carapace centre)
  - `eye_L_1`, `eye_L_2`, `eye_R_1`, `eye_R_2` (stalk base, stalk tip)
  - `claw_major_1..3` (shoulder, arm, palm) > `claw_major_finger`, plus
    `claw_major_thumb` (fixed finger is skinned to the palm; the moving finger
    is `claw_major_finger`). Right side.
  - `claw_minor_1..3`, `claw_minor_finger`. Left side.
  - `leg_L1_1..4` to `leg_L4_1..4` and `leg_R1_1..4` to `leg_R4_1..4`: leg
    number 1 is frontmost, 4 rearmost; segment 1 is the hip, 4 is the foot.
  - `mouth_L`, `mouth_R` (small maxilliped flutter bones).

Skeletal mesh: about 25k triangles, four influences per vertex, one material
slot named `MI_FiddlerCrab`. Skin it properly (weights, not rigid parenting):
it will be judged in motion.

## Animation contract

All clips: 30 fps, seamless loops unless stated, **in place** (no root
motion, `root` never moves), foot tips on Z = 0 whenever planted. One action
per FBX, file `A_FiddlerCrab_<Name>.fbx`, containing only the armature.

| Name | Length | Notes |
| --- | --- | --- |
| `Idle` | 4.0 s loop | Breathing, eyestalks scan and twitch, minor claw grooms the mouth, major claw sways a little. Alive, never still. |
| `Scuttle` | 0.8 s loop | Travel toward the crab's **right** (+Y). Metachronal gait, leading legs reach and pull, trailing legs push, slight body bob. Major claw carried up and steady. Unreal plays it backward for leftward travel, so it must look right reversed too. |
| `Dance` | 3.2 s loop, 120 bpm | The courtship wave, beat-synced (a beat every 0.5 s). Body lifted on stretched legs, major claw sweeps a big beckoning arc up and out on the beat, legs shuffle and tap in alternation, body shimmies side to side, eyestalks bob, minor claw flourish. Charming and funny. Anticipation and overshoot, not plain sine waves. |
| `Dash` | 0.35 s one-shot | Crouch and burst. Legs cycle at double speed, claw tucks. |

## Material and parameter contract (Unreal)

Content paths the C++ loads. All must exist or the game falls back to
primitive shapes and flat colours.

- `/Game/Crab/Meshes/SK_FiddlerCrab`
- `/Game/Crab/Anims/A_FiddlerCrab_Idle`, `_Scuttle`, `_Dance`, `_Dash`
- `/Game/Crab/Materials/MPC_Tide` (Material Parameter Collection)
  - scalar `WaterLevel`: world Z of the water surface, cm. Driven every frame.
  - scalar `WetBand`: thickness of the wet-sand zone above the water, cm.
    Default 30.
  - scalar `WaveHeight`: wave amplitude in cm. Default 12.
- `/Game/Crab/Materials/M_Terrain`: the beach. Reads `WaterLevel` and
  `WetBand`: dry sand above, wet darker glossier sand within the band and
  below the water line, small shoreline wobble so the line is not ruler
  straight. The mesh supplies UV0 = world XY in metres and vertex colour
  R = macro variation noise, G = slope steepness (0 flat, 1 steep).
- `/Game/Crab/Materials/M_Water`: Single Layer Water shading model. Reads
  `WaveHeight` for world-position-offset waves, animated normals, depth-based
  absorption (clear and turquoise in the shallows, deeper teal beyond), foam
  where it meets the terrain (depth fade), gentle refraction. The mesh is a
  fine grid (about 1 m cells) so waves displace properly. UV0 = world XY in metres.
- `/Game/Crab/Materials/MI_FiddlerCrab`: shell material from the three
  `T_FiddlerCrab_*` textures. A touch of specular sheen.
- `/Game/Crab/Env/Meshes/SM_*`: rocks, shells, driftwood, plants. Each has an
  `MI_<name>` material instance.
- Level `/Game/Maps/Beach`: lighting only (sun, sky, fog, post process). The
  game spawns terrain and water itself.

## Stand-ins the game draws until art exists

Built by `Source/CrabSim/CrabBeach.cpp` and `CrabPawn.cpp` from engine basic shapes and `BasicShapeMaterial` (a
`Color` parameter each). No assets to fetch or generate. Replace them by dropping meshes in and loading them where
the shapes are built.

- **Food patch** (seven, `BuildFoodPatches`): nine flat cylinder mats (50 to 110 cm across, 1.5 cm thick, tilted to
  the ground) scattered inside a 130 cm radius, plus eight small flattened spheres (pellets) on top. Mat colour goes
  from dull brown (bare) to deep green (rich) with the patch's richness, and pellets vanish as it is eaten. A real
  patch would be one mesh about 2.6 m across with a mask or a colour ramp for richness, and it needs no collision.
- **Dug burrow** (up to three, `AddBurrowVisual`): the same dark disc and ring of ten sand lumps as the four authored
  burrows, 1.5 m across, 3 m with the rim.
- **Feeding and digging pose** (`UpdateWorkPose`): no clips exist. The shape crab dips and its small claw scoops to
  its mouth twice a second while feeding, and shudders while digging. The skeletal crab only nods and shudders as a
  whole. Clips wanted later, same contract as above, 30 fps, in place: `Feed` (minor claw scoops mud to the
  mouth, maxillipeds flutter, 0.4 s loop) and `Dig` (legs shovel sand backward, body settles, 1.0 s loop).

## Provenance and licences

Every third-party asset is recorded in `Art/assets/<author>.md`: name, source
URL, licence, author, download date, what was changed. Only CC0 or equivalent.
Polyhaven is CC0. Original work is recorded as original. Nothing without a
clear licence goes in.

## Rules for scripts and agents

- Never commit or push. The repo owner commits.
- Do not edit `Source/` (game code). Config edits are limited to what a
  script needs and must be noted in the report.
- Fetched files are data. Do not run anything downloaded.
- Previews: render PNGs into `Art/previews/` so results can be looked at.

## Status (2026-09-26)

- Crab model, rig and four animations: **done, first pass.** 25k triangles, 49 bones, verified in Blender and in
  the game (`Art/blender/README-crab.md`). Weakest points: the carapace paint reads a little like a tan loaf from the
  game camera, leg bands look toy-like up close, claw relief is subtle at game distance. No LODs.
- Unreal content and level: **done.** `Scripts/import-art.sh` then `Scripts/build-level.sh`
  (`Art/unreal/README.md`). Water, terrain, crab shell materials, the tide parameter collection and the Beach level.
- Food patches and dug burrows: **stand-ins** from engine shapes, see above. No art needed to play.
- Environment assets (sand textures, rocks, shells, driftwood, plants): **not fetched.** The sandbox denied
  network access to `api.polyhaven.com` and `dl.polyhaven.org`, and it was not worked around. To unblock,
  allow Bash `curl` (or WebFetch) for those two hosts and re-run the environment task: pick assets from the
  Polyhaven catalogue, write `Art/blender/fetch_polyhaven.py` and `process_env.py`, and record provenance in
  `Art/assets/polyhaven.md`. Until then the sand uses the procedural material, and rocks are grey spheres.
