# Unreal content and level scripts

Headless editor Python that builds everything Unreal-side from `Art/export/`, so the whole
thing is reproducible from scripts. The contract (paths, parameter names, skeleton) is
`Art/README.md`. Re-run the scripts whenever art changes: they are idempotent, and anything
that is missing is skipped with a logged warning.

```
Scripts/import-art.sh     Art/unreal/build_content.py   materials, textures, meshes, skeleton, animations
Scripts/build-level.sh    Art/unreal/build_level.py     /Game/Maps/Beach and /Game/Maps/MaterialTest
```

Run `import-art.sh` first (the maps need its materials). Each takes about a minute, prints
`CRABSIM_BUILD_CONTENT_OK` or `CRABSIM_BUILD_LEVEL_OK` on success and exits non-zero on failure.
Logs: `Saved/Logs/import-art.log`, `Saved/Logs/build-level.log`. Warnings are repeated at the
end of the log as `warning: ...`.

Environment variables:

| Variable | Effect |
| --- | --- |
| `UE_ROOT` | engine location (default `~/UnrealEngine/UE_5.8`) |
| `CRABSIM_EXPORT_DIR` | folder holding `meshes/`, `textures/`, `anims/` (default `Art/export`); for trying art from somewhere else |
| `CRABSIM_IMPORT_SCALE` | force the FBX import scale for meshes and animations (default: detect) |
| `CRABSIM_TEST_CAMERA` | `build-level.sh` only: which MaterialTest camera the game uses (`game`, `surf`, `shore`, `water`, `crab`, `top`) |

## What import-art builds

| Asset | Source |
| --- | --- |
| `/Game/Crab/Materials/MPC_Tide` | scalars `WaterLevel` 0, `WetBand` 30, `WaveHeight` 12 (updated in place, never deleted) |
| `/Game/Crab/Materials/M_Terrain`, `M_Water`, `M_CrabShell`, `M_EnvProp` | rebuilt from scratch every run |
| `/Game/Crab/Materials/MI_FiddlerCrab` | instance of `M_CrabShell`, textures from `T_FiddlerCrab_*` |
| `/Game/Crab/Env/Materials/MI_<name>` | one per `SM_<name>`, instance of `M_EnvProp`; plus category defaults `MI_Rock`, `MI_Shell`, `MI_Driftwood`, `MI_Plant`, `MI_Bone` |
| `/Game/Crab/Textures/T_FiddlerCrab_*` | `Art/export/textures/T_FiddlerCrab_*.png` |
| `/Game/Crab/Env/Textures/T_*` | every other `Art/export/textures/T_*.png` |
| `/Game/Crab/Env/Meshes/SM_*` | `Art/export/meshes/SM_*.fbx`, material slots all set to `MI_<name>` |
| `/Game/Crab/Meshes/SK_FiddlerCrab` (+ `_Skeleton`) | `Art/export/meshes/SK_FiddlerCrab.fbx`, slot set to `MI_FiddlerCrab` |
| `/Game/Crab/Anims/A_FiddlerCrab_*` | `Art/export/anims/A_FiddlerCrab_*.fbx` on that skeleton, 30 fps |

Textures are imported as `BaseColor` sRGB, `Normal` as NormalMap compression (DirectX, green
down, no flip), `ORM` as Masks without sRGB. Kind comes from the file name suffix.

Meshes, the skeleton and the clips are deleted and imported fresh each run, because an import
over an existing asset keeps that asset's old import scale. Assets a saved level references
cannot be deleted through the editor API (it reports success and does nothing), so those are
removed from disk before anything loads them; the maps reference by path and pick the new asset
up. Nothing is deleted when its source file is missing, and nothing stale is pruned when a
source file goes away.

After each import the script checks and logs: the imported size (crab about 200 by 220 by 100
cm), which way the crab faces (must be +X) and which side the major claw is on (+Y), the
contract bones, the single material slot, the lowest point of the reference pose (feet at Z = 0),
each clip's length against `Art/README.md`, root drift (must be 0) and whether the loop closes.

## Missing art

* No `T_FiddlerCrab_*` textures: `MI_FiddlerCrab` uses a constant sandy-tan shell.
* No `SM_*` meshes: only the category instances exist; `MaterialTest` uses squashed engine
  spheres with `MI_Rock` as stand-in rocks.
* No `SK_FiddlerCrab.fbx`: no crab in `MaterialTest`; existing assets are left alone.
* No sand textures: `M_Terrain` is fully procedural (see below). This is a complete look, not
  a placeholder.
* A prop needs all three of `T_<name>_BaseColor/_Normal/_ORM` to use textures (also tried:
  the name without trailing digits, so `SM_Rock01` finds `T_Rock_*`); with fewer it warns and
  uses the category colour.

## Materials

**M_Terrain** (opaque, default lit, world-space normal). One Custom HLSL node; every length
is world cm. It reads `WaterLevel` and `WetBand` from `MPC_Tide`, vertex colour R (macro
variation, 0.5 neutral) and G (slope steepness), and the vertex normal. Not read: UV0.

* Shoreline: wet above the water up to `WetBand`, with a wobble at three scales plus a slow
  swash, so the line is never straight. Wet sand is darker, more saturated, and glossy
  (roughness 0.10 to 0.34 in patches), with a darker damp rim at the edge and a faint dried
  salt line above it. Sand just above the band is slightly damp.
* Dry sand: two tones chosen by vertex R and noise, fine grain and pebble or shell flecks that
  fade with distance, ripples that run parallel to the shore (phase follows the downhill
  direction of the vertex normal) in patches, and a cooler darker tint on steep ground (vertex G).
* No vertex colours (white R and G): it falls back to the geometry's slope and no macro tone.
* Sand textures (optional): `T_SandDry_*` or `T_Sand_*` or `T_BeachSand_*` or `T_Beach_*`, each
  as `_BaseColor`, `_Normal`, `_ORM` in `Art/export/textures/`. They are sampled twice at
  different scales and rotations (`SandTileCm`, default 220) and blended by a noise mask to hide
  tiling; the wet/dry logic, macro tone and ripples stay on top. Any of the three may be absent.
* Parameters (all in the group `Crab`): `DryColorA`, `DryColorB`, `SteepColor`, `ShoreWobble`,
  `GrainStrength`, `PebbleAmount`, `RippleStrength`, `SandTileCm`.
* Cost: about nine noise evaluations per pixel. Fine detail fades with distance. If it is too heavy on the integrated GPU, lower octaves in `terrain_hlsl()`.

**M_Water** (Single Layer Water, opaque as the shading model requires). It needs
`r.Water.SingleLayer.Reflection=3` for scene reflections (see Config below).

* Waves: three swells in world-position offset, scaled by `WaveHeight` (12 cm total at the
  default), faded out beyond 50 to 110 m from the camera. The surface is pinned to
  `WaterLevel` in the material, so the actor can sit at any Z (harmless if the C++ also moves
  the actor). `SnapToWaterLevel` = 0 turns that off.
* Normals: the analytic swell slope plus two scrolling detail layers (two octaves each) and a
  fine glitter octave near the camera, all generated in the material; no texture is needed.
  `DetailNormalStrength` scales them; they fade with distance and the roughness rises.
* Volume: `ScatteringColor` and `AbsorptionColor` per cm of water (times `ScatteringScale`,
  `AbsorptionScale`). Red dies first, so the shallows are clear and turquoise and deeper water
  goes teal. `Refraction` is the IOR (1.12 is gentle).
* Foam: from the depth between the surface and the terrain behind it (`SceneDepthWithoutWater`),
  a noisy lace at the waterline plus lapping bands; `FoamWidth` (cm of water depth, default 6),
  `FoamAmount`, `FoamColor`. Foam is the surface layer (base colour and opacity); clear water
  has opacity 0.

**M_CrabShell / MI_FiddlerCrab**: default lit, used with skeletal meshes and morph targets. A
static switch `UseTextures` picks the three textures or a constant (`FallbackColor`). The sheen
is a Fresnel-driven drop in roughness with raised specular (`Sheen`, default 0.6, 0 for none).

**M_EnvProp / MI_<name>**: same switch pattern, plus a wet look near the waterline (darker and
glossier below `WetBand`, `WetDarkening`). Used with instanced static meshes.

## Using them from the C++

* Set the collection each frame: `UKismetMaterialLibrary::SetScalarParameterValue(World,
  Mpc, "WaterLevel", Z)` (same for `WetBand` and `WaveHeight`).
* Terrain: a mesh with vertex colour R (macro noise) and G (slope) and correct normals. A
  procedural mesh is fine.
* Water: a flat grid of about 1 m cells (the swells need it; the shortest is 5 m long), cast
  shadow off, at any Z. Beyond the edge of the mesh the sky atmosphere draws its own ground.
* Materials and instances are loaded by path, see `Art/README.md`.

## Maps

* `/Game/Maps/Beach`: lighting only. A movable sun (6.5 lux with a fixed EV100 0 exposure, 26 degrees up, in front
  of and to the left of the fixed camera, so shadows fall toward the camera), sky atmosphere, a
  real-time captured sky light with a warm lower hemisphere to stand in for sand bounce, light
  height fog, and an unbound post process volume: fixed exposure, a warm grade, bloom 0.3, no
  motion blur. It is `GameDefaultMap` and `EditorStartupMap`.
* `/Game/Maps/MaterialTest`: the same lighting plus a sloped test beach (a procedural mesh with
  the vertex colours the game supplies), a sea crossing it at `WaterLevel` 0, stand-in rocks
  (or real `SM_Rock*`), every other `SM_*` prop in a row, the crab with its Idle clip, and six
  cameras. It uses a plain game mode. View it with
  `UnrealEditor CrabSim.uproject /Game/Maps/MaterialTest -game -windowed -ResX=1280 -ResY=720`
  or open it in the editor. The map is about 5 MB (procedural mesh data).

## FBX scale and orientation (measured)

Blender exports metres. What Unreal receives depends on the exporter's Apply Scalings option
(tested with the crab pipeline's settings and a throwaway rig):

* `FBX_SCALE_NONE` with global scale 1 (the crab's first export): meshes import at the right size
  in cm, but the skeleton's root bone gets **scale 100** (the m to cm factor stays on the Armature
  node) while bone translations stay in metres. The animation import drops that node, so a clip
  has root scale 1, and played back unpatched the skin matrices shrink the crab to 1 percent
  (invisible in game). A root bone at scale 100 would also scale anything attached to a bone.
* `FBX_SCALE_ALL` or `FBX_SCALE_UNITS` with global scale 1: root scale stays 1 but everything is in
  metres, so it imports 100 times too small; the script detects that and reimports at scale 100
  (meshes and clips alike), which gives a clean cm skeleton with consistent clips.
* The current crab export (mesh and clips) already lands in cm with root scale 1, so it imports at
  scale 1.0 and needs no patching. If a clip ever disagrees with the skeleton about scale,
  `build_content.py` (`match_root_to_reference`) fixes two cases it can tell apart by the body
  bone offset: a clip in the skeleton's own units (writes the skeleton's root transform into the
  clip), and a clip in cm against a scale-100 root (also scales the translation keys by 0.01;
  tested by simulating cm clips on a throwaway rig). Any other mismatch is reported and left alone.
* Blender X maps to Unreal X and Blender Y is mirrored to Unreal -Y. So the crab must face
  Blender +X (major claw on Blender -Y) to face +X with the claw on +Y in Unreal.
* `SK_FiddlerCrab.fbx` imports at 197 by 221 by 98 cm, faces +X, major claw on +Y, root bone
  `root`, 49 bones, one material slot `MI_FiddlerCrab`, feet at Z = 0. The clips are 3.2, 0.333,
  4.0 and 0.8 s at 30 fps with no root drift, and each loop closes on a copy of its first frame
  (which is correct for Unreal).
* Import scale settings used: mesh 1.0, clips the same value as the mesh (1.0), 30 fps sample
  rate, exported time range. The script only changes the scale (to 100 or 0.01) when the
  resulting size is out of range, judged from the size that actually results, not the scale it
  asked for (the editor silently keeps an existing asset's old scale).

## Config edits

`Config/DefaultEngine.ini`: `GameDefaultMap` and `EditorStartupMap` are `/Game/Maps/Beach`, and
`r.Water.SingleLayer.Reflection=3` under `[SystemSettings]` (commented there). With the project's
`r.ReflectionMethod=0` the water would otherwise have no screen-space reflections. Nothing else
changed: Lumen, Nanite and virtual shadow maps stay off. Single Layer Water needs no other setting.

## Known limits

* Verified on a GTX 1070 (Vulkan) only, not on the integrated GPU.
* Shader compile of `M_Water`, `M_Terrain` and the instances takes a while on first launch.
* `MaterialTest`'s sea is 1.5 m cells with four coarse slabs out to 5 km; the game's mesh
  decides its own extent.
