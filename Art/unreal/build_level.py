"""Build the Crab Sim maps.

Runs inside the Unreal editor (headless, via Scripts/build-level.sh). Idempotent: an
existing map is opened, emptied and rebuilt on every run. Needs the materials from
build_content.py (Scripts/import-art.sh) to exist already.

  /Game/Maps/Beach         lighting only: sun, sky atmosphere, real-time sky light,
                           height fog, an unbound post process volume. The game spawns
                           the terrain and the water itself.
  /Game/Maps/MaterialTest  the same lighting plus a sloped test beach (M_Terrain), a sea
                           (M_Water) crossing it at WaterLevel, a rock and the crab if
                           they exist, and cameras. Open it to judge the materials.

Environment:
  CRABSIM_TEST_CAMERA   which MaterialTest camera the game uses: game (default), surf,
                        shore, water, crab, top. All cameras are in the map either way.
"""

import math
import os
import random
import traceback

import unreal

MAP_DIR = "/Game/Maps"
BEACH = MAP_DIR + "/Beach"
TEST = MAP_DIR + "/MaterialTest"
MAT_ROOT = "/Game/Crab/Materials"
ENV_MAT_ROOT = "/Game/Crab/Env/Materials"
ENV_MESH_ROOT = "/Game/Crab/Env/Meshes"
SK_PATH = "/Game/Crab/Meshes/SK_FiddlerCrab"
ANIM_IDLE = "/Game/Crab/Anims/A_FiddlerCrab_Idle"

EAL = unreal.EditorAssetLibrary
ACTORS = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
LEVELS = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)

# ---- lighting (late afternoon, sun in front of and to the left of the fixed game camera,
# which looks along +X with +Y to its right). Lux and EV are a matched pair: the exposure is
# fixed so the palette reads the same in every view.
SUN_YAW = 145.0          # direction the light travels; the sun sits at yaw 325 (front left)
SUN_PITCH = -26.0        # low sun, long shadows
SUN_LUX = 6.5
SUN_COLOR = unreal.Color(r=255, g=255, b=255, a=255)   # the sky atmosphere supplies the warmth
SKY_INTENSITY = 1.8
EXPOSURE_EV = 0.0

CAM_CHOICES = ("game", "surf", "shore", "water", "crab", "top")


def log(msg):
    unreal.log("[build_level] " + msg)


def warn(msg):
    unreal.log_warning("[build_level] " + msg)


def set_props(obj, **props):
    for k, v in props.items():
        try:
            obj.set_editor_property(k, v)
        except Exception as e:
            warn(f"{obj.get_name()}: could not set {k} ({e})")


def spawn(cls, loc=(0.0, 0.0, 0.0), rot=(0.0, 0.0, 0.0), label=None, folder=None):
    """rot is (pitch, yaw, roll)."""
    a = ACTORS.spawn_actor_from_class(cls, unreal.Vector(*loc), unreal.Rotator(roll=rot[2], pitch=rot[0], yaw=rot[1]))
    if a is None:
        raise RuntimeError(f"could not spawn {cls}")
    if label:
        a.set_actor_label(label)
    if folder:
        a.set_folder_path(folder)
    return a


# ---------------------------------------------------------------- levels

def fresh_level(path):
    EAL.make_directory(MAP_DIR)
    on_disk = os.path.isfile(os.path.join(unreal.Paths.convert_relative_path_to_full(
        unreal.Paths.project_content_dir()), path[len("/Game/"):] + ".umap"))
    if on_disk or EAL.does_asset_exist(path):
        if not LEVELS.load_level(path):
            raise RuntimeError(f"could not load existing {path}")
        removed = 0
        for actor in ACTORS.get_all_level_actors():
            if isinstance(actor, (unreal.WorldSettings, unreal.Brush)) and not isinstance(actor, unreal.Volume):
                continue
            ACTORS.destroy_actor(actor)
            removed += 1
        log(f"{path}: cleared {removed} actors")
    elif not LEVELS.new_level(path, False):
        raise RuntimeError(f"new_level({path}) failed")
    return unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()


def save_level(path):
    if not LEVELS.save_current_level():
        raise RuntimeError(f"save_current_level failed for {path}")
    log(f"saved {path}")


# ---------------------------------------------------------------- lighting

def add_lighting():
    sun = spawn(unreal.DirectionalLight, (0, 0, 500), (SUN_PITCH, SUN_YAW, 0), "Sun", "Lighting")
    c = sun.light_component
    set_props(c, mobility=unreal.ComponentMobility.MOVABLE, light_color=SUN_COLOR, intensity=SUN_LUX,
              cast_shadows=True, cast_dynamic_shadows=True, atmosphere_sun_light=True,
              atmosphere_sun_light_index=0,
              # The camera sits about 13 m out and the play area is large: cascades must reach it,
              # but three cascades keep the shadow cost low.
              dynamic_shadow_distance_movable_light=9000.0, dynamic_shadow_cascades=3,
              cascade_distribution_exponent=3.0, light_source_angle=1.2)

    atm = spawn(unreal.SkyAtmosphere, (0, 0, 0), (0, 0, 0), "SkyAtmosphere", "Lighting")
    # Past the end of the map the atmosphere draws its own ground at Z = 0, and the default grey
    # shows as a flat band at the horizon. A dark teal reads as more sea.
    set_props(atm.get_component_by_class(unreal.SkyAtmosphereComponent), ground_albedo=unreal.Color(r=120, g=160, b=176, a=255))

    sky = spawn(unreal.SkyLight, (0, 0, 300), (0, 0, 0), "SkyLight", "Lighting")
    set_props(sky.light_component, mobility=unreal.ComponentMobility.MOVABLE, real_time_capture=True,
              intensity=SKY_INTENSITY, cast_shadows=False,
              # No GI in this lean config, so fake the warm bounce off the sand in the shadows.
              lower_hemisphere_is_black=False, lower_hemisphere_color=unreal.LinearColor(0.30, 0.23, 0.15, 1.0))

    fog = spawn(unreal.ExponentialHeightFog, (0, 0, 0), (0, 0, 0), "HeightFog", "Lighting")
    fc = fog.component
    set_props(fc, fog_density=0.006, fog_height_falloff=0.25, start_distance=1500.0, fog_max_opacity=0.55,
              fog_inscattering_luminance=unreal.LinearColor(0.32, 0.36, 0.42, 1.0),
              directional_inscattering_luminance=unreal.LinearColor(0.75, 0.52, 0.30, 1.0),
              directional_inscattering_exponent=18.0, directional_inscattering_start_distance=2500.0)

    ppv = spawn(unreal.PostProcessVolume, (0, 0, 0), (0, 0, 0), "PostProcess", "Lighting")
    ppv.set_editor_property("unbound", True)
    s = ppv.get_editor_property("settings")
    # Fixed exposure: EV100 0 is the manual base, the bias shifts it.
    set_props(s,
              override_auto_exposure_method=True, auto_exposure_method=unreal.AutoExposureMethod.AEM_MANUAL,
              override_auto_exposure_apply_physical_camera_exposure=True, auto_exposure_apply_physical_camera_exposure=False,
              override_auto_exposure_bias=True, auto_exposure_bias=-EXPOSURE_EV,
              # Modest bloom, and no motion blur (an accessibility choice, also set in DefaultEngine.ini).
              override_bloom_intensity=True, bloom_intensity=0.30,
              override_bloom_threshold=True, bloom_threshold=1.5,
              override_motion_blur_amount=True, motion_blur_amount=0.0,
              override_motion_blur_max=True, motion_blur_max=0.0,
              # Warm grade: a little gain toward orange, slightly richer colour, a touch more contrast.
              override_color_gain=True, color_gain=unreal.Vector4(1.02, 1.0, 0.95, 1.0),
              override_color_saturation=True, color_saturation=unreal.Vector4(1.0, 1.0, 1.0, 1.0),
              override_color_contrast=True, color_contrast=unreal.Vector4(1.05, 1.05, 1.05, 1.0),
              override_vignette_intensity=True, vignette_intensity=0.35)
    ppv.set_editor_property("settings", s)
    log("lighting: sun, sky atmosphere, real-time sky light, height fog, post process")


# ---------------------------------------------------------------- Beach

def build_beach():
    fresh_level(BEACH)
    add_lighting()
    save_level(BEACH)


# ---------------------------------------------------------------- MaterialTest geometry

# The test beach slopes toward +X. The shoreline (height 0 = the collection's default
# WaterLevel) sits near x = SHORE_X. X and Y are cm.
SHORE_X = 900.0
X_RANGE = (-3000.0, 6500.0)
Y_RANGE = (-4200.0, 4200.0)
CELL = 150.0


def vnoise(x, y):
    """Smooth 2D value noise, deterministic, 0..1."""
    def h(ix, iy):
        random.seed((ix * 73856093) ^ (iy * 19349663))
        return random.random()
    ix, iy = math.floor(x), math.floor(y)
    fx, fy = x - ix, y - iy
    ux, uy = fx * fx * fx * (fx * (fx * 6 - 15) + 10), fy * fy * fy * (fy * (fy * 6 - 15) + 10)
    a, b, c, d = h(ix, iy), h(ix + 1, iy), h(ix, iy + 1), h(ix + 1, iy + 1)
    return (a + (b - a) * ux) + ((c + (d - c) * ux) - (a + (b - a) * ux)) * uy


def fbm(x, y, octaves=3):
    v, amp, tot = 0.0, 1.0, 0.0
    for o in range(octaves):
        v += amp * vnoise(x * 2 ** o, y * 2 ** o)
        tot += amp
        amp *= 0.5
    return v / tot


def beach_height(x, y):
    """A gentle slope with a berm and dunes inland, a wobbling shoreline, a steeper drop
    once past the surf zone, and a couple of low ridges on the flats."""
    s = SHORE_X + 350.0 * math.sin(y / 900.0) + 240.0 * math.sin(y / 370.0 + 1.3) + 120.0 * (fbm(y / 500.0, 3.3) - 0.5)
    z = -(x - s) * 0.040
    if x > s + 1600.0:
        z -= (x - s - 1600.0) * 0.06
    dune = math.exp(-((x - (SHORE_X - 2500.0)) / 700.0) ** 2)
    z += 130.0 * dune * (0.6 + 0.8 * fbm(x / 600.0 + 9.0, y / 700.0))
    z += 28.0 * (fbm(x / 1400.0 + 2.0, y / 1400.0 + 5.0) - 0.5)
    z += 6.0 * math.sin(x / 260.0 + 2.0 * math.sin(y / 500.0)) * math.exp(-max(0.0, x - s) / 900.0)
    return z


def build_grid(xr, yr, cell, height_fn, color_fn=None):
    """Vertex, triangle, normal, UV and colour arrays for a regular grid (UV0 = world XY in
    metres, as the C++ terrain supplies)."""
    nx = int(round((xr[1] - xr[0]) / cell))
    ny = int(round((yr[1] - yr[0]) / cell))
    verts, uvs, cols, norms = [], [], [], []
    e = cell * 0.5
    for j in range(ny + 1):
        y = yr[0] + j * cell
        for i in range(nx + 1):
            x = xr[0] + i * cell
            z = height_fn(x, y)
            verts.append(unreal.Vector(x, y, z))
            uvs.append(unreal.Vector2D(x / 100.0, y / 100.0))
            dzx = (height_fn(x + e, y) - height_fn(x - e, y)) / (2 * e)
            dzy = (height_fn(x, y + e) - height_fn(x, y - e)) / (2 * e)
            n = unreal.Vector(-dzx, -dzy, 1.0)
            l = math.sqrt(n.x * n.x + n.y * n.y + 1.0)
            norms.append(unreal.Vector(n.x / l, n.y / l, 1.0 / l))
            cols.append(color_fn(x, y, dzx, dzy) if color_fn else unreal.LinearColor(0.5, 0.0, 0.0, 1.0))
    tris = []
    for j in range(ny):
        for i in range(nx):
            a = j * (nx + 1) + i
            b, c, d = a + 1, a + nx + 1, a + nx + 2
            tris += [a, c, b, b, c, d]     # counter-clockwise seen from above (+Z)
    return verts, tris, norms, uvs, cols


def terrain_color(x, y, dzx, dzy):
    """R = macro variation noise, G = slope steepness (0 flat, 1 steep), as the game supplies."""
    macro = fbm(x / 1800.0 + 11.0, y / 1800.0 + 3.0, 3)
    slope = math.hypot(dzx, dzy)               # rise over run
    steep = min(1.0, max(0.0, (slope - 0.05) / 0.35))
    return unreal.LinearColor(macro, steep, 0.0, 1.0)


def make_pmc_actor(label, grid, material, cast_shadow=True):
    actor = spawn(unreal.Actor, (0, 0, 0), (0, 0, 0), label, "MaterialTest")
    # Actor has no add-component call in Python, so make the component as the actor's root.
    comp = unreal.new_object(unreal.ProceduralMeshComponent, actor, label + "Mesh")
    actor.set_editor_property("root_component", comp)
    verts, tris, norms, uvs, cols = grid
    comp.create_mesh_section_linear_color(0, verts, tris, norms, uvs, [], [], [], cols, [], False, False)
    comp.set_material(0, material)
    comp.set_editor_property("cast_shadow", cast_shadow)
    log(f"{label}: {len(verts)} vertices, {len(tris) // 3} triangles")
    return actor, comp


def load_mat(path):
    m = EAL.load_asset(path)
    if m is None:
        raise RuntimeError(f"missing {path}: run Scripts/import-art.sh first")
    return m


def place_static(mesh, loc, scale=(1, 1, 1), rot=(0, 0, 0), label=None, material=None):
    a = spawn(unreal.StaticMeshActor, loc, rot, label, "MaterialTest")
    c = a.static_mesh_component
    c.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
    c.set_static_mesh(mesh)
    a.set_actor_scale3d(unreal.Vector(*scale))
    if material is not None:
        c.set_material(0, material)
    return a


def first_asset(root, prefix):
    for p in sorted(EAL.list_assets(root, recursive=False, include_folder=False)):
        name = p.split("/")[-1].split(".")[0]
        if name.startswith(prefix):
            return EAL.load_asset(p.split(".")[0])
    return None


# ---------------------------------------------------------------- cameras

def look_at(frm, to):
    d = unreal.Vector(to[0] - frm[0], to[1] - frm[1], to[2] - frm[2])
    yaw = math.degrees(math.atan2(d.y, d.x))
    pitch = math.degrees(math.atan2(d.z, math.hypot(d.x, d.y)))
    return (pitch, yaw, 0.0)


def add_camera(name, frm, to, fov, active):
    cam = spawn(unreal.CameraActor, frm, look_at(frm, to), name, "Cameras")
    cc = cam.get_component_by_class(unreal.CameraComponent)
    set_props(cc, field_of_view=fov, constrain_aspect_ratio=False)
    if active:
        cam.set_editor_property("auto_activate_for_player", unreal.AutoReceiveInput.PLAYER0)
    return cam


def add_cameras(target, wanted):
    """`target` is the crab's spot. `game` reproduces the fixed game camera: 55 degrees down,
    1300 cm along the boom, FOV 60, looking along +X."""
    tx, ty, tz = target
    pitch = math.radians(55.0)
    game = (tx - 1300.0 * math.cos(pitch), ty, tz + 1300.0 * math.sin(pitch))
    surf_t = (SHORE_X + 250.0, ty, 0.0)
    surf = (surf_t[0] - 1300.0 * math.cos(pitch), ty, surf_t[2] + 1300.0 * math.sin(pitch))
    views = {
        "game": (game, target, 60.0),
        "surf": (surf, surf_t, 60.0),
        "shore": ((tx - 500.0, ty - 380.0, tz + 130.0), (tx + 350.0, ty + 60.0, tz - 20.0), 65.0),
        "water": ((SHORE_X + 700.0, ty - 300.0, 220.0), (SHORE_X + 3500.0, ty + 300.0, 20.0), 70.0),
        "crab": ((tx - 260.0, ty - 210.0, tz + 130.0), (tx, ty, tz + 45.0), 55.0),
        "top": ((SHORE_X - 300.0, ty, 4200.0), (SHORE_X + 300.0, ty, 0.0), 60.0),
    }
    for name, (frm, to, fov) in views.items():
        add_camera(f"Cam_{name}", frm, to, fov, name == wanted)
    log(f"cameras: {', '.join(views)}; active for the game: {wanted}")


# ---------------------------------------------------------------- MaterialTest

def build_test():
    wanted = os.environ.get("CRABSIM_TEST_CAMERA", "game").strip().lower()
    if wanted not in CAM_CHOICES:
        warn(f"CRABSIM_TEST_CAMERA={wanted!r} is not one of {CAM_CHOICES}, using game")
        wanted = "game"
    terrain_mat = load_mat(MAT_ROOT + "/M_Terrain")
    water_mat = load_mat(MAT_ROOT + "/M_Water")

    world = fresh_level(TEST)
    # A plain game mode: the crab game mode would spawn its own blockout beach and pawn.
    world.get_world_settings().set_editor_property("default_game_mode", unreal.GameModeBase)
    add_lighting()

    make_pmc_actor("TestBeach", build_grid(X_RANGE, Y_RANGE, CELL, beach_height, terrain_color), terrain_mat)
    # The sea is a fine grid (1.5 m cells here; the game uses about 1 m) so the swells displace properly. It
    # sits at Z = 0 and the material pins it to the collection's WaterLevel (default 0).
    sea = build_grid((SHORE_X - 1800.0, X_RANGE[1] + 2500.0), (Y_RANGE[0] - 500.0, Y_RANGE[1] + 500.0), CELL,
                     lambda x, y: 0.0, lambda x, y, a, b: unreal.LinearColor(0, 0, 0, 1))
    make_pmc_actor("TestSea", sea, water_mat, cast_shadow=False)
    # Far sea: four coarse slabs around the fine one out to 5 km, so the horizon is water.
    fx0, fx1 = SHORE_X - 1800.0, X_RANGE[1] + 2500.0
    fy0, fy1 = Y_RANGE[0] - 500.0, Y_RANGE[1] + 500.0
    far = 500000.0
    ring = []
    for rect in (((fx1, far), (-far, far)), ((-far, fx0), (-far, far)),
                 ((fx0, fx1), (fy1, far)), ((fx0, fx1), (-far, fy0))):
        ring.append(rect)
    verts, tris, norms, uvs, cols = [], [], [], [], []
    for (xa, xb), (ya, yb) in ring:
        base = len(verts)
        for (x, y) in ((xa, ya), (xb, ya), (xa, yb), (xb, yb)):
            verts.append(unreal.Vector(x, y, 0.0))
            norms.append(unreal.Vector(0, 0, 1))
            uvs.append(unreal.Vector2D(x / 100.0, y / 100.0))
            cols.append(unreal.LinearColor(0, 0, 0, 1))
        tris += [base, base + 2, base + 1, base + 1, base + 2, base + 3]
    make_pmc_actor("TestFarSea", (verts, tris, norms, uvs, cols), water_mat, cast_shadow=False)

    # Props: a real rock if one exists, otherwise a squashed engine sphere as a stand-in.
    rock_mi = EAL.load_asset(ENV_MAT_ROOT + "/MI_Rock") if EAL.does_asset_exist(ENV_MAT_ROOT + "/MI_Rock") else None
    rock = first_asset(ENV_MESH_ROOT, "SM_Rock")
    rock_spots = [(SHORE_X - 250.0, -350.0), (SHORE_X + 60.0, 300.0), (SHORE_X - 900.0, 520.0)]
    for i, (rx, ry) in enumerate(rock_spots):
        rz = beach_height(rx, ry)
        if rock is not None:
            place_static(rock, (rx, ry, rz), (1, 1, 1), (0, i * 71.0, 0), f"Rock_{i}", None)
        else:
            sphere = EAL.load_asset("/Engine/BasicShapes/Sphere")
            place_static(sphere, (rx, ry, rz + 12.0), (0.9 - 0.15 * i, 0.7, 0.4), (0, i * 71.0, 0),
                         f"RockStandIn_{i}", rock_mi)
    if rock is None:
        log("no SM_Rock* imported yet: using squashed spheres with MI_Rock as stand-ins")

    # Every other imported prop (shells, driftwood, plants...) in a row on the dry sand.
    others = [p for p in sorted(EAL.list_assets(ENV_MESH_ROOT, recursive=False, include_folder=False))
              if not p.split("/")[-1].startswith("SM_Rock")]
    for i, p in enumerate(others[:8]):
        mesh = EAL.load_asset(p.split(".")[0])
        px, py = SHORE_X - 1000.0, (i - 3.5) * 260.0
        place_static(mesh, (px, py, beach_height(px, py)), (1, 1, 1), (0, i * 37.0, 0), f"Prop_{i}", None)
    if others:
        log(f"props placed in a row: {', '.join(p.split('/')[-1].split('.')[0] for p in others[:8])}")

    # The crab, if the skeletal mesh has been imported.
    crab_x, crab_y = SHORE_X - 420.0, 0.0
    crab_z = beach_height(crab_x, crab_y)
    sk = EAL.load_asset(SK_PATH) if EAL.does_asset_exist(SK_PATH) else None
    if sk is not None:
        a = spawn(unreal.SkeletalMeshActor, (crab_x, crab_y, crab_z), (0, 0, 0), "Crab", "MaterialTest")
        comp = a.skeletal_mesh_component
        comp.set_skeletal_mesh_asset(sk)
        anim = EAL.load_asset(ANIM_IDLE) if EAL.does_asset_exist(ANIM_IDLE) else None
        if anim is not None:
            comp.set_animation_mode(unreal.AnimationMode.ANIMATION_SINGLE_NODE)
            comp.override_animation_data(anim, True, True, 0.0, 1.0)
        log(f"crab placed at ({crab_x:.0f}, {crab_y:.0f}, {crab_z:.0f})" + (" with the Idle clip" if anim else ""))
    else:
        log(f"{SK_PATH} not imported yet: no crab in MaterialTest")

    add_cameras((crab_x, crab_y, crab_z + 40.0), wanted)
    save_level(TEST)


def main():
    build_beach()
    build_test()
    unreal.log("[build_level] CRABSIM_BUILD_LEVEL_OK")


try:
    main()
except Exception:
    unreal.log_error("[build_level] FAILED\n" + traceback.format_exc())
    raise
