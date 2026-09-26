"""Build the Crab Sim content: materials, textures, meshes, skeleton and animations.

Runs inside the Unreal editor (headless, via Scripts/import-art.sh). Everything is
idempotent and degrades gracefully: master materials are rebuilt from scratch on every
run, instances and the parameter collection are updated in place, and imported assets are
replaced. Art that has not landed yet is skipped with a logged warning, and materials
fall back to constant colours when a texture is missing.

Contract (asset paths, parameter names): Art/README.md. Usage notes: Art/unreal/README.md.

Environment:
  CRABSIM_EXPORT_DIR    folder holding meshes/, textures/, anims/ (default Art/export)
  CRABSIM_IMPORT_SCALE  force the FBX import scale (default: auto-detect from the mesh size)
"""

import glob
import math
import os
import re
import traceback

import unreal

# ---------------------------------------------------------------- paths

CRAB_ROOT = "/Game/Crab"
MAT_ROOT = CRAB_ROOT + "/Materials"
TEX_ROOT = CRAB_ROOT + "/Textures"
SK_ROOT = CRAB_ROOT + "/Meshes"
ANIM_ROOT = CRAB_ROOT + "/Anims"
ENV_MESH_ROOT = CRAB_ROOT + "/Env/Meshes"
ENV_TEX_ROOT = CRAB_ROOT + "/Env/Textures"
ENV_MAT_ROOT = CRAB_ROOT + "/Env/Materials"
TEST_ROOT = CRAB_ROOT + "/Test"

MPC_PATH = MAT_ROOT + "/MPC_Tide"
SK_NAME = "SK_FiddlerCrab"
CRAB_MI = "MI_FiddlerCrab"

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, "..", ".."))
EXPORT_DIR = os.environ.get("CRABSIM_EXPORT_DIR") or os.path.join(REPO, "Art", "export")
MESH_DIR = os.path.join(EXPORT_DIR, "meshes")
TEX_DIR = os.path.join(EXPORT_DIR, "textures")
ANIM_DIR = os.path.join(EXPORT_DIR, "anims")
FORCED_SCALE = os.environ.get("CRABSIM_IMPORT_SCALE")

# Expected loop lengths from Art/README.md (seconds), used to sanity-check imports.
ANIM_LENGTHS = {"Idle": 4.0, "Scuttle": 0.8, "Dance": 3.2, "Dash": 0.35}
ONE_SHOT = {"Dash"}
CRAB_BONES = ["root", "body", "eye_L_1", "eye_L_2", "eye_R_1", "eye_R_2",
              "claw_major_1", "claw_major_2", "claw_major_3", "claw_major_finger", "claw_major_thumb",
              "claw_minor_1", "claw_minor_2", "claw_minor_3", "claw_minor_finger",
              "mouth_L", "mouth_R"] + \
             [f"leg_{s}{n}_{i}" for s in "LR" for n in range(1, 5) for i in range(1, 5)]

MEL = unreal.MaterialEditingLibrary
TOOLS = unreal.AssetToolsHelpers.get_asset_tools()
EAL = unreal.EditorAssetLibrary

summary = {"materials": [], "instances": [], "textures": [], "meshes": [], "anims": [], "notes": [], "warnings": []}


def log(msg):
    unreal.log("[build_content] " + msg)


def note(msg):
    summary["notes"].append(msg)
    log(msg)


def warn(msg):
    summary["warnings"].append(msg)
    unreal.log_warning("[build_content] " + msg)


# ---------------------------------------------------------------- colour helpers

def srgb(hex_str):
    h = hex_str.lstrip("#")
    return tuple(int(h[i:i + 2], 16) / 255.0 for i in (0, 2, 4))


def linear(hex_str, a=1.0):
    """Hex (sRGB) to a linear LinearColor, which is what material colour parameters expect."""
    def dec(c):
        return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4
    r, g, b = (dec(c) for c in srgb(hex_str))
    return unreal.LinearColor(r, g, b, a)


# ---------------------------------------------------------------- asset and graph helpers

def get_or_create(path, name, cls, factory):
    full = f"{path}/{name}"
    if EAL.does_asset_exist(full):
        asset = EAL.load_asset(full)
        if asset is not None and isinstance(asset, cls):
            return asset
        EAL.delete_asset(full)
    asset = TOOLS.create_asset(name, path, cls, factory)
    if asset is None:
        raise RuntimeError(f"could not create {full}")
    return asset


def fresh_material(path, name):
    """Create or reset a material so the graph is rebuilt from nothing."""
    mat = get_or_create(path, name, unreal.Material, unreal.MaterialFactoryNew())
    # delete_all_material_expressions removes only part of the graph per call (it walks a list
    # it is editing), so repeat until nothing is left. Stale nodes matter: Single Layer Water
    # allows one output node, and leftover Custom nodes bloat the shader.
    for _ in range(30):
        if MEL.get_num_material_expressions(mat) == 0:
            break
        MEL.delete_all_material_expressions(mat)
    if MEL.get_num_material_expressions(mat) != 0:
        raise RuntimeError(f"could not empty {name}")
    return mat


def set_props(obj, **props):
    for k, v in props.items():
        try:
            obj.set_editor_property(k, v)
        except Exception as e:  # property renamed between engine versions
            warn(f"{obj.get_name()}: could not set {k} ({e})")


def expr(mat, cls, x, y, **props):
    e = MEL.create_material_expression(mat, cls, x, y)
    set_props(e, **props)
    return e


def vparam(mat, name, color, x, y, group="Crab"):
    return expr(mat, unreal.MaterialExpressionVectorParameter, x, y,
                parameter_name=name, default_value=color, group=group)


def sparam(mat, name, value, x, y, group="Crab"):
    return expr(mat, unreal.MaterialExpressionScalarParameter, x, y,
                parameter_name=name, default_value=value, group=group)


def const(mat, value, x, y):
    return expr(mat, unreal.MaterialExpressionConstant, x, y, r=value)


def const3(mat, r, g, b, x, y):
    return expr(mat, unreal.MaterialExpressionConstant3Vector, x, y, constant=unreal.LinearColor(r, g, b, 1.0))


def link(src, src_out, dst, dst_in):
    if not MEL.connect_material_expressions(src, src_out, dst, dst_in):
        try:
            names = [str(n) for n in MEL.get_material_expression_input_names(dst)]
        except Exception:
            names = []
        raise RuntimeError(f"connect {src.get_name()}.{src_out} -> {dst.get_name()}.{dst_in} failed (inputs: {names})")


def out(src, prop, src_out=""):
    if not MEL.connect_material_property(src, src_out, prop):
        raise RuntimeError(f"connect {src.get_name()}.{src_out} -> {prop} failed")


def mask(mat, src, x, y, r=False, g=False, b=False, a=False, src_out=""):
    m = expr(mat, unreal.MaterialExpressionComponentMask, x, y, r=r, g=g, b=b, a=a)
    link(src, src_out, m, "")
    return m


def lerp_node(mat, a, b, alpha, x, y, a_out="", b_out="", alpha_out=""):
    n = expr(mat, unreal.MaterialExpressionLinearInterpolate, x, y)
    link(a, a_out, n, "A")
    link(b, b_out, n, "B")
    link(alpha, alpha_out, n, "Alpha")
    return n


def mul(mat, a, b, x, y, a_out="", b_out=""):
    n = expr(mat, unreal.MaterialExpressionMultiply, x, y)
    link(a, a_out, n, "A")
    link(b, b_out, n, "B")
    return n


def custom(mat, x, y, code, inputs, out_type=unreal.CustomMaterialOutputType.CMOT_FLOAT4,
           desc="Custom", extra=None):
    """Custom HLSL node. `inputs` is a list of (name, expression, output_name);
    `extra` is a list of (output_name, CustomMaterialOutputType) additional outputs."""
    node = expr(mat, unreal.MaterialExpressionCustom, x, y,
                code=code, output_type=out_type, description=desc)
    pins = []
    for n, _, _ in inputs:
        ci = unreal.CustomInput()
        ci.set_editor_property("input_name", n)
        pins.append(ci)
    node.set_editor_property("inputs", pins)
    if extra:
        outs = []
        for n, t in extra:
            co = unreal.CustomOutput()
            co.set_editor_property("output_name", n)
            co.set_editor_property("output_type", t)
            outs.append(co)
        node.set_editor_property("additional_outputs", outs)
    for n, src, src_out in inputs:
        link(src, src_out, node, n)
    return node


def finish(mat):
    MEL.layout_material_expressions(mat)
    MEL.recompile_material(mat)
    EAL.save_loaded_asset(mat, False)
    summary["materials"].append(mat.get_path_name().split(".")[0])


def load_engine_tex(path):
    t = unreal.load_asset(path)
    if t is None:
        warn(f"engine texture {path} not found")
    return t


# ---------------------------------------------------------------- HLSL emitter

class Gen:
    """Emits HLSL statements for Custom nodes. A Custom node body cannot declare
    functions, so hash and value-noise code is expanded inline from here, with unique
    variable names. Each helper returns the name of the variable holding the result."""

    def __init__(self):
        self.lines = []
        self.k = 0

    def u(self, base="v"):
        self.k += 1
        return f"{base}{self.k}"

    def L(self, s):
        self.lines.append(s)

    def code(self):
        return "\n".join(self.lines) + "\n"

    def hash(self, p):
        """p: name of a float2 variable. Returns a float in [0,1) (hash without sine)."""
        q, h = self.u("q"), self.u("h")
        self.L(f"float3 {q} = frac(float3({p}.x, {p}.y, {p}.x) * 0.1031);")
        self.L(f"{q} += dot({q}, {q}.yzx + 33.33);")
        self.L(f"float {h} = frac(({q}.x + {q}.y) * {q}.z);")
        return h

    def _lattice(self, p_expr):
        p, i, f = self.u("p"), self.u("i"), self.u("f")
        self.L(f"float2 {p} = {p_expr};")
        self.L(f"float2 {i} = floor({p});")
        self.L(f"float2 {f} = {p} - {i};")
        corners = []
        for dx, dy in ((0, 0), (1, 0), (0, 1), (1, 1)):
            c = self.u("c")
            self.L(f"float2 {c} = {i} + float2({dx}.0, {dy}.0);")
            corners.append(self.hash(c))
        return f, corners

    def noise(self, p_expr):
        """Value noise with quintic smoothing, float in [0,1]."""
        f, (a, b, c, d) = self._lattice(p_expr)
        u, n = self.u("u"), self.u("n")
        self.L(f"float2 {u} = {f} * {f} * {f} * ({f} * ({f} * 6.0 - 15.0) + 10.0);")
        self.L(f"float {n} = lerp(lerp({a}, {b}, {u}.x), lerp({c}, {d}, {u}.x), {u}.y);")
        return n

    def noised(self, p_expr):
        """Value noise and its analytic derivative wrt p: float3(value, d/dx, d/dy)."""
        f, (a, b, c, d) = self._lattice(p_expr)
        u, du, n = self.u("u"), self.u("du"), self.u("nd")
        self.L(f"float2 {u} = {f} * {f} * {f} * ({f} * ({f} * 6.0 - 15.0) + 10.0);")
        self.L(f"float2 {du} = 30.0 * {f} * {f} * ({f} - 1.0) * ({f} - 1.0);")
        self.L(f"float3 {n} = float3({a} + ({b} - {a}) * {u}.x + ({c} - {a}) * {u}.y + ({a} - {b} - {c} + {d}) * {u}.x * {u}.y,"
               f" {du}.x * (({b} - {a}) + ({a} - {b} - {c} + {d}) * {u}.y),"
               f" {du}.y * (({c} - {a}) + ({a} - {b} - {c} + {d}) * {u}.x));")
        return n


F1 = unreal.CustomMaterialOutputType.CMOT_FLOAT1
F2 = unreal.CustomMaterialOutputType.CMOT_FLOAT2
F3 = unreal.CustomMaterialOutputType.CMOT_FLOAT3
F4 = unreal.CustomMaterialOutputType.CMOT_FLOAT4


# ---------------------------------------------------------------- parameter collection

def build_mpc():
    """MPC_Tide: WaterLevel (world Z of the sea surface, cm), WetBand (cm), WaveHeight (cm)."""
    EAL.make_directory(MAT_ROOT)
    mpc = get_or_create(MAT_ROOT, "MPC_Tide", unreal.MaterialParameterCollection,
                        unreal.MaterialParameterCollectionFactoryNew())
    params = []
    for name, value in (("WaterLevel", 0.0), ("WetBand", 30.0), ("WaveHeight", 12.0)):
        p = unreal.CollectionScalarParameter()
        p.set_editor_property("parameter_name", name)
        p.set_editor_property("default_value", value)
        params.append(p)
    mpc.set_editor_property("scalar_parameters", params)
    EAL.save_loaded_asset(mpc, False)
    summary["materials"].append(MPC_PATH)
    return mpc


def mpc_param(mat, mpc, name, x, y):
    return expr(mat, unreal.MaterialExpressionCollectionParameter, x, y, collection=mpc, parameter_name=name)


# ---------------------------------------------------------------- M_Terrain

# Palette (sRGB). Dry sand is a warm gold; the second tone is browner and carries the
# macro variation. Steep ground goes cooler and darker (compacted, damp, gritty).
SAND_DRY_A = "#D6C6A2"
SAND_DRY_B = "#C1AA84"
SAND_STEEP = "#8E7E68"


def terrain_hlsl():
    """One Custom node: sand albedo, roughness, normal, specular and AO. Sections are
    numbered in the comments; every cm value is in Unreal units. The shoreline sits at
    WaterLevel + WetBand (wobbled), wet sand is darker, glossier and more saturated."""
    g = Gen()
    L = g.L
    L("// 1. frame of reference: metres are not used, everything is world cm")
    L("float2 xy = WP.xy;")
    L("float hgt = WP.z - WL;")
    L("float wb = max(WB, 1.0);")
    L("float nearF = saturate(1.0 - PD / 2200.0);   // grain and pebbles fade out with distance")
    L("float midF = saturate(1.0 - PD / 7000.0);    // ripples fade out further away")
    L("// 2. shoreline: three scales of wobble plus a slow swash, so the line is never ruler straight")
    na = g.noise("xy / 520.0 + float2(T * 0.006, -T * 0.004)")
    nb = g.noise("xy / 140.0 + float2(-T * 0.010, T * 0.007) + 17.3")
    nc = g.noise("xy / 38.0 + 41.7")
    L(f"float wob = (({na} - 0.5) * 0.55 + ({nb} - 0.5) * 0.28 + ({nc} - 0.5) * 0.09) * wb * Wobble;")
    L(f"float swash = sin(T * 0.55 + {na} * 6.2832) * 0.05 * wb;")
    L("float top = max(wb + wob + swash, wb * 0.25);")
    L("float wet = 1.0 - smoothstep(top * 0.86, top, hgt);")
    L(f"float damp = (1.0 - smoothstep(top, top * 2.6 + 6.0, hgt)) * (0.12 + 0.26 * {nb});")
    L("float rim = exp(-pow((hgt - top * 0.93) / (top * 0.05 + 0.5), 2.0));   // darker damp line at the edge")
    L("float halo = smoothstep(top * 0.92, top * 1.08, hgt) * exp(-max(hgt - top, 0.0) / (top * 0.30 + 1.0));   // dried salt line above it")
    L("float wt = saturate(wet + damp);")
    L("// 3. dry sand colour: vertex R (macro noise) plus two noise scales pick between two tones")
    L("// white in both R and G means the mesh has no vertex colours: fall back to the geometry")
    L("bool noVC = VC.r > 0.99 && VC.g > 0.99;")
    L("float macro = noVC ? 0.0 : saturate(VC.r) - 0.5;")
    nmid = g.noise("xy / 85.0 + 7.7")
    nlow = g.noise("xy / 1400.0 + 3.1")
    L(f"float tone = saturate(0.5 + macro * 0.9 + ({nmid} - 0.5) * 0.55 + ({nlow} - 0.5) * 0.5);")
    L("float3 col = lerp(DryA, DryB, tone);")
    L(f"col *= 1.0 + ({nmid} - 0.5) * 0.10;")
    L("// 4. grain (analytic gradient reused for the normal) and pebble or shell flecks")
    gd = g.noised("xy / 2.3 + 91.1")
    gb = g.noise("xy / 0.85 + 17.9")
    L(f"col *= 1.0 + (({gd}.x - 0.5) * 0.16 + ({gb} - 0.5) * 0.12) * Grain * nearF;")
    L("float2 cp = xy / 13.0;")
    L("float2 cell = floor(cp);")
    L("float2 loc = cp - cell;")
    h1 = g.hash("cell")
    L("float2 cellB = cell + float2(31.7, 17.3);")
    h2 = g.hash("cellB")
    L("float2 cellC = cell + float2(5.3, 91.7);")
    h3 = g.hash("cellC")
    L("float2 cellD = cell + float2(71.1, 3.9);")
    h4 = g.hash("cellD")
    L(f"float2 ctr = 0.25 + 0.5 * float2({h2}, {h3});")
    L(f"float peb = step({h1}, Pebbles) * (1.0 - smoothstep(0.05 + 0.08 * {h4}, 0.07 + 0.10 * {h4}, length(loc - ctr))) * (0.35 + 0.65 * nearF);")
    L(f"col = lerp(col, col * lerp(0.48, 1.40, frac({h4} * 7.13)), peb * 0.85);")
    L("// 5. steep ground: darker, cooler, less golden (vertex G)")
    L("float slope = noVC ? saturate((1.0 - NW.z) * 4.0 - 0.08) : saturate(VC.g);")
    L("col = lerp(col, Steep * (0.75 + 0.5 * tone), slope * 0.75);")
    L("// 6. wet sand: darker, more saturated (raise to a power), then the edge details")
    L("float3 wetc = pow(saturate(col), float3(1.76, 1.88, 2.00)) * 0.80;")
    L("col = lerp(col, wetc, wt);")
    L("col *= 1.0 - 0.16 * rim;")
    L("col *= 1.0 + 0.10 * halo;")
    L("// 7. ripples run parallel to the shore: phase follows the downhill direction of the vertex normal")
    L("float slen = length(NW.xy);")
    L("float2 dir = normalize(lerp(float2(0.8, 0.6), NW.xy / max(slen, 1e-4), saturate(slen * 30.0)));")
    L("float s = dot(xy, dir);")
    npatch = g.noise("xy / 230.0 + 5.5")
    L(f"float warp = ({nb} - 0.5) * 2.6 + ({nlow} - 0.5) * 5.0;")
    L("float lam = lerp(95.0, 58.0, wt);")
    L("float ph = 6.2832 * s / lam + warp;")
    L("float rip = sin(ph) + 0.28 * sin(2.0 * ph + 1.1);")
    L("float drip = (6.2832 / lam) * (cos(ph) + 0.56 * cos(2.0 * ph + 1.1));")
    L(f"float ramp = lerp(0.50, 0.26, wt) * Ripple * smoothstep(0.30, 0.70, {npatch}) * midF;")
    L("float2 grad = dir * (ramp * drip) + " + f"{gd}.yz / 2.3 * 0.55 * Grain * nearF;")
    L("col *= 1.0 + 0.045 * rip * ramp;")
    L("NormalOut = normalize(NW + float3(-grad.x, -grad.y, 0.0));")
    L("// 8. roughness, specular, AO")
    nr = g.noise("xy / 23.0 + 61.3")
    L(f"float rDry = 0.86 + ({gd}.x - 0.5) * 0.12;")
    L(f"float rWet = lerp(0.10, 0.34, {nr}) * lerp(0.7, 1.0, smoothstep(0.0, top * 0.6, hgt));")
    L("Rough = lerp(rDry, rWet, wt);")
    L("SpecOut = lerp(0.42, 0.58, wt);")
    L("AOOut = lerp(1.0, 0.78, slope * 0.6) * (1.0 - 0.25 * peb);")
    L("return col;")
    return g.code()


def sand_two_scale_uvs():
    """Custom node for the textured path: two world-space UV sets (scale and rotation
    differ) plus a noise mask that picks between them, which hides tiling."""
    g = Gen()
    g.L("float2 xy = WP.xy;")
    g.L("float2 uv1 = xy / max(Tile, 1.0);")
    g.L("float2 r = float2(xy.x * 0.6018 - xy.y * 0.7986, xy.x * 0.7986 + xy.y * 0.6018);")
    g.L("UV2 = r / (max(Tile, 1.0) * 0.63) + 0.37;")
    n = g.noise("xy / 900.0 + 12.3")
    g.L(f"Mix = smoothstep(0.30, 0.70, {n});")
    g.L("return uv1;")
    return g.code()


def find_sand_textures():
    """Return {'dry': {...}, 'wet': {...}} of imported texture asset paths, or None entries."""
    def look(names):
        found = {}
        for kind in ("BaseColor", "Normal", "ORM"):
            for n in names:
                p = f"{ENV_TEX_ROOT}/T_{n}_{kind}"
                if EAL.does_asset_exist(p):
                    found[kind] = p
                    break
        return found
    dry = look(["SandDry", "Sand", "BeachSand", "Beach"])
    wet = look(["SandWet", "WetSand"])
    return dry, wet


def build_terrain(mpc):
    mat = fresh_material(MAT_ROOT, "M_Terrain")
    set_props(mat, material_domain=unreal.MaterialDomain.MD_SURFACE,
              blend_mode=unreal.BlendMode.BLEND_OPAQUE,
              shading_model=unreal.MaterialShadingModel.MSM_DEFAULT_LIT,
              tangent_space_normal=False,     # the node outputs a world-space normal
              used_with_static_mesh=True)

    wp = expr(mat, unreal.MaterialExpressionWorldPosition, -1400, 0)
    vc = expr(mat, unreal.MaterialExpressionVertexColor, -1400, 150)
    nw = expr(mat, unreal.MaterialExpressionVertexNormalWS, -1400, 300)
    pd = expr(mat, unreal.MaterialExpressionPixelDepth, -1400, 450)
    tm = expr(mat, unreal.MaterialExpressionTime, -1400, 550)
    wl = mpc_param(mat, mpc, "WaterLevel", -1400, 650)
    wb = mpc_param(mat, mpc, "WetBand", -1400, 750)
    dry_a = vparam(mat, "DryColorA", linear(SAND_DRY_A), -1400, 850)
    dry_b = vparam(mat, "DryColorB", linear(SAND_DRY_B), -1400, 950)
    steep = vparam(mat, "SteepColor", linear(SAND_STEEP), -1400, 1050)
    wobble = sparam(mat, "ShoreWobble", 1.0, -1400, 1150)
    grain = sparam(mat, "GrainStrength", 1.0, -1400, 1250)
    pebbles = sparam(mat, "PebbleAmount", 0.14, -1400, 1350)
    ripple = sparam(mat, "RippleStrength", 1.0, -1400, 1450)

    code = terrain_hlsl()
    inputs = [("WP", wp, ""), ("VC", vc, ""), ("NW", nw, ""), ("PD", pd, ""), ("T", tm, ""),
              ("WL", wl, ""), ("WB", wb, ""), ("DryA", dry_a, ""), ("DryB", dry_b, ""), ("Steep", steep, ""),
              ("Wobble", wobble, ""), ("Grain", grain, ""), ("Pebbles", pebbles, ""), ("Ripple", ripple, "")]

    # Optional textured sand (Art/export/textures/T_Sand*_*.png). Without it the procedural
    # sand above is the look. With it, the textures replace the flat tones (and the fine grain
    # noise), sampled twice at different scales and rotations and blended by a noise mask so the
    # tiling never shows. The wet/dry logic, macro variation and ripples stay on top.
    dry, _wet = find_sand_textures()
    if "BaseColor" in dry:
        note("M_Terrain uses the sand textures " + ", ".join(sorted(dry.values())) + " (two-scale blend)")
        tile = sparam(mat, "SandTileCm", 220.0, -1400, 1550)
        uvn = custom(mat, -1100, 1550, sand_two_scale_uvs(),
                     [("WP", wp, ""), ("Tile", tile, "")], F2, "SandUV", extra=[("UV2", F2), ("Mix", F1)])

        def two_scale(kind, param, sampler, y):
            samples = []
            for i, out_name in enumerate(("", "UV2")):
                t = expr(mat, unreal.MaterialExpressionTextureSampleParameter2D, -800, y + i * 250,
                         parameter_name=param, group="Crab", texture=EAL.load_asset(dry[kind]),
                         sampler_type=sampler)
                link(uvn, out_name, t, "UVs")
                samples.append(t)
            return lerp_node(mat, samples[0], samples[1], uvn, -500, y, a_out="RGB", b_out="RGB", alpha_out="Mix")

        tex_color = two_scale("BaseColor", "SandBaseColor", unreal.MaterialSamplerType.SAMPLERTYPE_COLOR, 1500)
        code = code.replace("float3 col = lerp(DryA, DryB, tone);", "float3 col = TexCol * lerp(1.06, 0.90, tone);")
        inputs.append(("TexCol", tex_color, ""))
        if "Normal" in dry:
            tex_n = two_scale("Normal", "SandNormal", unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL, 2100)
            code = code.replace('NormalOut = normalize(NW + float3(-grad.x, -grad.y, 0.0));',
                                'grad -= TexN.xy / max(TexN.z, 0.25) * 0.8;\nNormalOut = normalize(NW + float3(-grad.x, -grad.y, 0.0));')
            inputs.append(("TexN", tex_n, ""))
        if "ORM" in dry:
            tex_orm = two_scale("ORM", "SandORM", unreal.MaterialSamplerType.SAMPLERTYPE_MASKS, 2700)
            code = code.replace("Rough = lerp(rDry, rWet, wt);", "Rough = lerp(TexORM.g, rWet, wt);")
            code = code.replace("AOOut = lerp(1.0, 0.78, slope * 0.6)", "AOOut = TexORM.r * lerp(1.0, 0.78, slope * 0.6)")
            inputs.append(("TexORM", tex_orm, ""))

    node = custom(mat, -900, 300, code, inputs, F3, "SandShading",
                  extra=[("Rough", F1), ("NormalOut", F3), ("SpecOut", F1), ("AOOut", F1)])
    out(node, unreal.MaterialProperty.MP_BASE_COLOR)
    out(node, unreal.MaterialProperty.MP_ROUGHNESS, "Rough")
    out(node, unreal.MaterialProperty.MP_NORMAL, "NormalOut")
    out(node, unreal.MaterialProperty.MP_SPECULAR, "SpecOut")
    out(node, unreal.MaterialProperty.MP_AMBIENT_OCCLUSION, "AOOut")
    out(const(mat, 0.0, -300, 800), unreal.MaterialProperty.MP_METALLIC)
    finish(mat)
    return mat


# ---------------------------------------------------------------- M_Water

# Three swells: direction, wavelength (cm), phase speed (cm/s), share of WaveHeight.
# The grid is about 1 m, so the shortest wavelength stays above about 5 m.
WAVES = [((0.9578, 0.2873), 1150.0, 70.0, 0.50),
         ((-0.4000, 0.9165), 760.0, 55.0, 0.30),
         ((0.2000, -0.9798), 520.0, 48.0, 0.20)]


def wave_terms():
    """HLSL for the swell sum: returns (height, gradient) statements over xy, T, WH."""
    lines = ["float wz = 0.0;", "float2 wg = float2(0.0, 0.0);"]
    for i, ((dx, dy), lam, spd, share) in enumerate(WAVES):
        k = 2.0 * math.pi / lam
        lines.append(f"{{ float ph = {k:.6f} * dot(xy, float2({dx}, {dy})) - {k * spd:.6f} * T + {i * 1.7:.2f};"
                     f" wz += WH * {share} * sin(ph);"
                     f" wg += WH * {share} * {k:.6f} * cos(ph) * float2({dx}, {dy}); }}")
    return "\n".join(lines) + "\n"


def water_wpo_hlsl():
    return ("float2 xy = WP.xy;\n" + wave_terms() +
            "// swells fade out with distance: far vertices are on coarse cells and would alias\n"
            "wz *= 1.0 - smoothstep(5000.0, 11000.0, length(WP.xy - CamPos.xy));\n"
            "// pin the surface to the collection's WaterLevel wherever the actor happens to sit\n"
            "return float3(0.0, 0.0, (WL - WP.z) * Snap + wz);\n")


def water_surface_hlsl():
    """Pixel side: analytic swell slope plus two scrolling detail normal layers, and the
    shoreline foam mask from the depth of water over the terrain."""
    g = Gen()
    L = g.L
    L("float2 xy = WP.xy;")
    L(wave_terms().strip())
    L("float farF = lerp(1.0, 0.25, smoothstep(2500.0, 12000.0, PD));")
    L("float glitF = 1.0 - smoothstep(600.0, 2600.0, PD);")
    L("// two scrolling normal layers, two octaves each, plus a fine glitter octave near the camera")
    a1 = g.noised("xy / 62.0 + float2(T * 0.22, T * 0.07)")
    a2 = g.noised("xy / 31.0 + float2(T * 0.31, -T * 0.05) + 7.1")
    b1 = g.noised("xy / 41.0 + float2(-T * 0.13, T * 0.19) + 31.7")
    b2 = g.noised("xy / 19.0 + float2(-T * 0.21, T * 0.26) + 53.9")
    c1 = g.noised("xy / 9.0 + float2(T * 0.40, T * 0.33) + 77.7")
    L(f"float2 dg = {a1}.yz * (0.60 / 62.0) + {a2}.yz * (0.30 / 31.0) + {b1}.yz * (0.50 / 41.0) + {b2}.yz * (0.25 / 19.0);")
    L(f"dg += {c1}.yz * (0.10 / 9.0) * glitF;")
    L("dg *= 5.5 * Strength * farF;")
    L("NormalOut = normalize(float3(-(wg.x + dg.x), -(wg.y + dg.y), 1.0));")
    L("// shoreline foam: irregular lace at the waterline, lapping bands behind it. The depth is")
    L("// roughened by noise so the edge follows neither the mesh grid nor a straight line.")
    L("float vd = max(SD - PD, 0.0) * abs(CV.z);")
    nf = g.noise("xy / 31.0 + float2(T * 0.05, -T * 0.03) + 3.3")
    nf2 = g.noise("xy / 9.5 + 13.1")
    nf3 = g.noise("xy / 3.4 + T * 0.2 + 5.5")
    L(f"float w = max(FoamWidth, 0.5) * (0.6 + 0.8 * {nf});")
    L(f"float vdn = vd + ({nf2} - 0.5) * w * 0.9 + ({nf} - 0.5) * w * 0.7;")
    L("float edge = 1.0 - smoothstep(0.0, w, vdn);")
    L(f"float band = smoothstep(0.62, 0.95, 0.5 + 0.5 * sin(vdn / max(FoamWidth, 0.5) * 5.5 - T * 1.5 + {nf} * 4.0))"
      " * (1.0 - smoothstep(w * 0.9, w * 3.6, vdn)) * smoothstep(w * 0.30, w * 0.8, vdn);")
    L(f"float f = edge * (0.45 + 0.90 * {nf3}) + band * 0.6 * (0.3 + {nf2});")
    L(f"FoamOut = smoothstep(0.40, 0.80, f) * FoamAmount * (0.72 + 0.28 * {nf3});")
    L("RoughOut = lerp(WR + smoothstep(3500.0, 20000.0, PD) * 0.22, 0.85, FoamOut);")
    L("return float3(0.0, 0.0, 0.0);")
    return g.code()


def build_water(mpc):
    mat = fresh_material(MAT_ROOT, "M_Water")
    set_props(mat, material_domain=unreal.MaterialDomain.MD_SURFACE,
              blend_mode=unreal.BlendMode.BLEND_OPAQUE,          # Single Layer Water must be opaque
              shading_model=unreal.MaterialShadingModel.MSM_SINGLE_LAYER_WATER,
              tangent_space_normal=False,
              used_with_static_mesh=True,
              max_world_position_offset_displacement=1500.0)

    wp = expr(mat, unreal.MaterialExpressionWorldPosition, -1500, 0)
    tm = expr(mat, unreal.MaterialExpressionTime, -1500, 150)
    pd = expr(mat, unreal.MaterialExpressionPixelDepth, -1500, 250)
    cv = expr(mat, unreal.MaterialExpressionCameraVectorWS, -1500, 350)
    sdw = expr(mat, unreal.MaterialExpressionSceneDepthWithoutWater, -1500, 450)
    wl = mpc_param(mat, mpc, "WaterLevel", -1500, 600)
    wh = mpc_param(mat, mpc, "WaveHeight", -1500, 700)
    snap = sparam(mat, "SnapToWaterLevel", 1.0, -1500, 800)
    strength = sparam(mat, "DetailNormalStrength", 1.0, -1500, 900)
    foam_w = sparam(mat, "FoamWidth", 6.0, -1500, 1000)
    foam_a = sparam(mat, "FoamAmount", 1.0, -1500, 1100)
    foam_col = vparam(mat, "FoamColor", linear("#F3EFE4"), -1500, 1200)
    scat = vparam(mat, "ScatteringColor", unreal.LinearColor(0.0022, 0.0052, 0.0062, 1.0), -1500, 1300)
    absorb = vparam(mat, "AbsorptionColor", unreal.LinearColor(0.0210, 0.0058, 0.0032, 1.0), -1500, 1400)
    scat_s = sparam(mat, "ScatteringScale", 1.0, -1500, 1500)
    absorb_s = sparam(mat, "AbsorptionScale", 1.0, -1500, 1600)
    rough = sparam(mat, "WaterRoughness", 0.06, -1500, 1700)
    ior = sparam(mat, "Refraction", 1.12, -1500, 1800)

    # World-position offset: the swells.
    cam = expr(mat, unreal.MaterialExpressionCameraPositionWS, -1500, 1900)
    wpo = custom(mat, -1000, 0, water_wpo_hlsl(),
                 [("WP", wp, ""), ("T", tm, ""), ("WL", wl, ""), ("WH", wh, ""), ("Snap", snap, ""),
                  ("CamPos", cam, "")],
                 F3, "Swells")
    out(wpo, unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)

    # Pixel normal and foam.
    surf = custom(mat, -1000, 400, water_surface_hlsl(),
                  [("WP", wp, ""), ("T", tm, ""), ("PD", pd, ""), ("CV", cv, ""), ("SD", sdw, ""),
                   ("WH", wh, ""), ("Strength", strength, ""), ("FoamWidth", foam_w, ""),
                   ("FoamAmount", foam_a, ""), ("WR", rough, "")],
                  F3, "WaterSurface", extra=[("NormalOut", F3), ("FoamOut", F1), ("RoughOut", F1)])
    out(surf, unreal.MaterialProperty.MP_NORMAL, "NormalOut")

    # Foam rides on the surface layer: base colour and opacity; clear water has opacity 0.
    out(foam_col, unreal.MaterialProperty.MP_BASE_COLOR)
    out(surf, unreal.MaterialProperty.MP_OPACITY, "FoamOut")
    out(surf, unreal.MaterialProperty.MP_ROUGHNESS, "RoughOut")
    out(const(mat, 0.5, -700, 1000), unreal.MaterialProperty.MP_SPECULAR)
    out(const(mat, 0.0, -700, 1100), unreal.MaterialProperty.MP_METALLIC)
    out(ior, unreal.MaterialProperty.MP_REFRACTION)

    # Volume: depth-based absorption and in-scatter. Coefficients are per cm of water.
    slw = expr(mat, unreal.MaterialExpressionSingleLayerWaterMaterialOutput, -300, 1300)
    link(mul(mat, scat, scat_s, -700, 1300), "", slw, "ScatteringCoefficients")
    link(mul(mat, absorb, absorb_s, -700, 1450), "", slw, "AbsorptionCoefficients")
    link(const(mat, 0.25, -700, 1600), "", slw, "PhaseG")
    finish(mat)
    return mat


# ---------------------------------------------------------------- shell and prop masters

def texture_param(mat, name, default_tex, sampler, x, y):
    return expr(mat, unreal.MaterialExpressionTextureSampleParameter2D, x, y,
                parameter_name=name, group="Crab", texture=default_tex, sampler_type=sampler)


def static_switch(mat, name, a, b, x, y, a_out="", b_out="", default=False):
    sw = expr(mat, unreal.MaterialExpressionStaticSwitchParameter, x, y,
              parameter_name=name, default_value=default, group="Crab")
    link(a, a_out, sw, "True")
    link(b, b_out, sw, "False")
    return sw


def build_crab_shell():
    """M_CrabShell: BaseColor/Normal/ORM from textures (static switch UseTextures), a
    sandy-tan constant otherwise. The sheen is a Fresnel-driven drop in roughness plus a
    raised specular, so the shell catches a soft highlight at grazing angles."""
    mat = fresh_material(MAT_ROOT, "M_CrabShell")
    set_props(mat, material_domain=unreal.MaterialDomain.MD_SURFACE,
              blend_mode=unreal.BlendMode.BLEND_OPAQUE,
              shading_model=unreal.MaterialShadingModel.MSM_DEFAULT_LIT,
              used_with_skeletal_mesh=True, used_with_morph_targets=True, used_with_static_mesh=True)

    bc_tex = texture_param(mat, "BaseColorTex", load_engine_tex("/Engine/EngineMaterials/DefaultDiffuse"),
                           unreal.MaterialSamplerType.SAMPLERTYPE_COLOR, -1200, -200)
    n_tex = texture_param(mat, "NormalTex", load_engine_tex("/Engine/EngineMaterials/DefaultNormal"),
                          unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL, -1200, 100)
    orm_tex = texture_param(mat, "ORMTex", load_engine_tex("/Engine/EngineMaterials/DefaultDiffuse_TC_Masks"),
                            unreal.MaterialSamplerType.SAMPLERTYPE_MASKS, -1200, 400)
    tint = vparam(mat, "Tint", unreal.LinearColor(1.0, 1.0, 1.0, 1.0), -1200, 650)
    fallback = vparam(mat, "FallbackColor", linear("#C9A574"), -1200, 750)
    rough_scale = sparam(mat, "RoughnessScale", 0.85, -1200, 850)
    sheen = sparam(mat, "Sheen", 0.6, -1200, 950)

    # base colour
    tinted = mul(mat, bc_tex, tint, -900, -200, a_out="RGB")
    base = static_switch(mat, "UseTextures", tinted, fallback, -600, -200)
    out(base, unreal.MaterialProperty.MP_BASE_COLOR)

    # normal
    flat = const3(mat, 0.0, 0.0, 1.0, -900, 100)
    nrm = static_switch(mat, "UseTextures", n_tex, flat, -600, 100, a_out="RGB")
    out(nrm, unreal.MaterialProperty.MP_NORMAL)

    # roughness with sheen
    orm_r = mask(mat, orm_tex, -900, 300, r=True, src_out="RGB")
    orm_g = mask(mat, orm_tex, -900, 400, g=True, src_out="RGB")
    orm_b = mask(mat, orm_tex, -900, 500, b=True, src_out="RGB")
    rough_tex = mul(mat, orm_g, rough_scale, -700, 400)
    rough_sw = static_switch(mat, "UseTextures", rough_tex, const(mat, 0.55, -700, 500), -500, 400)
    fres = expr(mat, unreal.MaterialExpressionFresnel, -900, 800, exponent=3.0, base_reflect_fraction=0.0)
    fres_s = mul(mat, fres, sheen, -700, 800)
    keep = expr(mat, unreal.MaterialExpressionOneMinus, -550, 800)
    link(mul(mat, fres_s, const(mat, 0.5, -700, 900), -600, 850), "", keep, "")
    out(mul(mat, rough_sw, keep, -300, 500), unreal.MaterialProperty.MP_ROUGHNESS)
    out(lerp_node(mat, const(mat, 0.5, -700, 1000), const(mat, 0.75, -700, 1050), sheen, -300, 900),
        unreal.MaterialProperty.MP_SPECULAR)

    # metallic and AO
    out(static_switch(mat, "UseTextures", orm_b, const(mat, 0.0, -700, 600), -500, 600),
        unreal.MaterialProperty.MP_METALLIC)
    out(static_switch(mat, "UseTextures", orm_r, const(mat, 1.0, -700, 700), -500, 700),
        unreal.MaterialProperty.MP_AMBIENT_OCCLUSION)
    finish(mat)
    return mat


# Category defaults for the props: sRGB fallback colour and roughness.
PROP_KINDS = {
    "Rock": ("#7A736A", 0.85),
    "Shell": ("#E4D8C3", 0.45),
    "Driftwood": ("#8B7A68", 0.78),
    "Plant": ("#5F7C3B", 0.7),
    "Bone": ("#DDD3C0", 0.6),
}
PROP_KEYWORDS = [("Shell", ("shell", "clam", "oyster", "whelk", "cockle", "periwinkle", "mussel", "scallop", "snail")),
                 ("Driftwood", ("wood", "log", "stick", "branch", "twig", "plank")),
                 ("Plant", ("plant", "grass", "weed", "kelp", "reed", "marram", "sedge", "cord", "flora", "seaweed")),
                 ("Bone", ("bone", "skull", "shard")),
                 ("Rock", ("rock", "stone", "boulder", "pebble", "cobble", "slab"))]


def prop_kind(name):
    n = name.lower()
    for kind, words in PROP_KEYWORDS:
        if any(w in n for w in words):
            return kind
    return "Rock"


WET_CODE = ("float wb = max(WB, 1.0);\n"
            "return 1.0 - smoothstep(0.0, wb * 0.9, WP.z - WL);\n")


def build_prop_master(mpc):
    """M_EnvProp: shared master for rocks, shells, driftwood and plants. Textures optional
    (static switch UseTextures). Anything near the waterline is wet: darker and glossier."""
    mat = fresh_material(MAT_ROOT, "M_EnvProp")
    set_props(mat, material_domain=unreal.MaterialDomain.MD_SURFACE,
              blend_mode=unreal.BlendMode.BLEND_OPAQUE,
              shading_model=unreal.MaterialShadingModel.MSM_DEFAULT_LIT,
              used_with_static_mesh=True, used_with_instanced_static_meshes=True)
    bc_tex = texture_param(mat, "BaseColorTex", load_engine_tex("/Engine/EngineMaterials/DefaultDiffuse"),
                           unreal.MaterialSamplerType.SAMPLERTYPE_COLOR, -1200, -200)
    n_tex = texture_param(mat, "NormalTex", load_engine_tex("/Engine/EngineMaterials/DefaultNormal"),
                          unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL, -1200, 100)
    orm_tex = texture_param(mat, "ORMTex", load_engine_tex("/Engine/EngineMaterials/DefaultDiffuse_TC_Masks"),
                            unreal.MaterialSamplerType.SAMPLERTYPE_MASKS, -1200, 400)
    tint = vparam(mat, "Tint", unreal.LinearColor(1.0, 1.0, 1.0, 1.0), -1200, 650, group="Prop")
    fallback = vparam(mat, "FallbackColor", linear(PROP_KINDS["Rock"][0]), -1200, 750, group="Prop")
    fb_rough = sparam(mat, "FallbackRoughness", PROP_KINDS["Rock"][1], -1200, 850, group="Prop")
    wet_amt = sparam(mat, "WetDarkening", 0.38, -1200, 950, group="Prop")
    wp = expr(mat, unreal.MaterialExpressionWorldPosition, -1200, 1050)
    wl = mpc_param(mat, mpc, "WaterLevel", -1200, 1150)
    wb = mpc_param(mat, mpc, "WetBand", -1200, 1250)
    wet = custom(mat, -900, 1150, WET_CODE, [("WP", wp, ""), ("WL", wl, ""), ("WB", wb, "")], F1, "Wetness")

    tinted = mul(mat, bc_tex, tint, -900, -200, a_out="RGB")
    base = static_switch(mat, "UseTextures", tinted, fallback, -600, -200)
    darken = expr(mat, unreal.MaterialExpressionOneMinus, -600, 1000)
    link(mul(mat, wet, wet_amt, -750, 1000), "", darken, "")
    out(mul(mat, base, darken, -300, -100), unreal.MaterialProperty.MP_BASE_COLOR)
    out(static_switch(mat, "UseTextures", n_tex, const3(mat, 0.0, 0.0, 1.0, -900, 100), -600, 100, a_out="RGB"),
        unreal.MaterialProperty.MP_NORMAL)

    orm_r = mask(mat, orm_tex, -900, 300, r=True, src_out="RGB")
    orm_g = mask(mat, orm_tex, -900, 400, g=True, src_out="RGB")
    orm_b = mask(mat, orm_tex, -900, 500, b=True, src_out="RGB")
    rough = static_switch(mat, "UseTextures", orm_g, fb_rough, -600, 400)
    out(lerp_node(mat, rough, const(mat, 0.25, -600, 500), mul(mat, wet, const(mat, 0.85, -750, 550), -500, 520),
                  -300, 450), unreal.MaterialProperty.MP_ROUGHNESS)
    out(static_switch(mat, "UseTextures", orm_b, const(mat, 0.0, -700, 600), -500, 600),
        unreal.MaterialProperty.MP_METALLIC)
    out(static_switch(mat, "UseTextures", orm_r, const(mat, 1.0, -700, 700), -500, 700),
        unreal.MaterialProperty.MP_AMBIENT_OCCLUSION)
    finish(mat)
    return mat


# ---------------------------------------------------------------- material instances

def make_instance(path, name, parent, scalars=None, vectors=None, textures=None, switches=None):
    mi = get_or_create(path, name, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    MEL.set_material_instance_parent(mi, parent)
    MEL.clear_all_material_instance_parameters(mi)
    for k, v in (scalars or {}).items():
        MEL.set_material_instance_scalar_parameter_value(mi, k, v)
    for k, v in (vectors or {}).items():
        MEL.set_material_instance_vector_parameter_value(mi, k, v)
    for k, v in (textures or {}).items():
        tex = EAL.load_asset(v)
        if tex is not None:
            MEL.set_material_instance_texture_parameter_value(mi, k, tex)
    for k, v in (switches or {}).items():
        MEL.set_material_instance_static_switch_parameter_value(mi, k, v)
    MEL.update_material_instance(mi)
    EAL.save_loaded_asset(mi, False)
    summary["instances"].append(f"{path}/{name}")
    return mi


def tex_path(root, base, kind):
    p = f"{root}/{base}_{kind}"
    return p if EAL.does_asset_exist(p) else None


def texture_set(root, base):
    """{'BaseColorTex':..., 'NormalTex':..., 'ORMTex':...} for whichever of the three exist."""
    found = {}
    for param, kind in (("BaseColorTex", "BaseColor"), ("NormalTex", "Normal"), ("ORMTex", "ORM")):
        p = tex_path(root, base, kind)
        if p:
            found[param] = p
    return found


def build_crab_instance(shell):
    tex = texture_set(TEX_ROOT, "T_FiddlerCrab")
    have_all = len(tex) == 3
    if not have_all:
        missing = {"BaseColorTex", "NormalTex", "ORMTex"} - set(tex)
        warn(f"{CRAB_MI}: T_FiddlerCrab textures missing ({', '.join(sorted(missing))}); using the constant sandy-tan shell")
    make_instance(MAT_ROOT, CRAB_MI, shell, scalars={"Sheen": 0.6}, textures=tex,
                  switches={"UseTextures": have_all})


def build_prop_defaults(prop):
    """One instance per prop category so meshes always have something sensible."""
    for kind, (hex_str, rough) in PROP_KINDS.items():
        make_instance(ENV_MAT_ROOT, f"MI_{kind}", prop, scalars={"FallbackRoughness": rough},
                      vectors={"FallbackColor": linear(hex_str)})


# ---------------------------------------------------------------- texture import

def texture_kind(name):
    for kind in ("BaseColor", "Normal", "ORM"):
        if name.endswith("_" + kind):
            return kind
    return "Other"


def import_textures():
    files = []
    for pat in ("T_*.png", "T_*.PNG", "T_*.tga", "T_*.jpg"):
        files += glob.glob(os.path.join(TEX_DIR, pat))
    files = sorted(set(files))
    if not files:
        log(f"no textures in {TEX_DIR}, skipping texture import")
        return
    EAL.make_directory(TEX_ROOT)
    EAL.make_directory(ENV_TEX_ROOT)
    tasks = []
    for f in files:
        name = os.path.splitext(os.path.basename(f))[0]
        dest = TEX_ROOT if name.startswith("T_FiddlerCrab_") else ENV_TEX_ROOT
        t = unreal.AssetImportTask()
        t.set_editor_property("filename", f)
        t.set_editor_property("destination_path", dest)
        t.set_editor_property("destination_name", name)
        t.set_editor_property("replace_existing", True)
        t.set_editor_property("replace_existing_settings", True)
        t.set_editor_property("automated", True)
        t.set_editor_property("save", False)
        tasks.append((t, name, dest))
    TOOLS.import_asset_tasks([t for t, _, _ in tasks])
    for t, name, dest in tasks:
        tex = EAL.load_asset(f"{dest}/{name}")
        if not isinstance(tex, unreal.Texture2D):
            warn(f"texture import of {name} produced nothing")
            continue
        kind = texture_kind(name)
        char = dest == TEX_ROOT
        if kind == "BaseColor":
            set_props(tex, srgb=True, compression_settings=unreal.TextureCompressionSettings.TC_DEFAULT,
                      lod_group=unreal.TextureGroup.TEXTUREGROUP_CHARACTER if char else unreal.TextureGroup.TEXTUREGROUP_WORLD)
        elif kind == "Normal":
            set_props(tex, srgb=False, compression_settings=unreal.TextureCompressionSettings.TC_NORMALMAP,
                      lod_group=unreal.TextureGroup.TEXTUREGROUP_CHARACTER_NORMAL_MAP if char else unreal.TextureGroup.TEXTUREGROUP_WORLD_NORMAL_MAP)
        elif kind == "ORM":
            set_props(tex, srgb=False, compression_settings=unreal.TextureCompressionSettings.TC_MASKS,
                      lod_group=unreal.TextureGroup.TEXTUREGROUP_CHARACTER_SPECULAR if char else unreal.TextureGroup.TEXTUREGROUP_WORLD_SPECULAR)
        else:
            warn(f"{name}: unknown texture kind, imported with default settings")
        EAL.save_loaded_asset(tex, False)
        summary["textures"].append(f"{dest}/{name} ({kind}, {tex.blueprint_get_size_x()}x{tex.blueprint_get_size_y()})")


# ---------------------------------------------------------------- FBX import

def fbx_ui(kind, scale=1.0, skeleton=None):
    ui = unreal.FbxImportUI()
    ui.set_editor_property("import_mesh", kind != "anim")
    ui.set_editor_property("import_as_skeletal", kind in ("skeletal", "anim"))
    ui.set_editor_property("import_materials", False)
    ui.set_editor_property("import_textures", False)
    ui.set_editor_property("import_animations", kind == "anim")
    ui.set_editor_property("create_physics_asset", False)
    ui.set_editor_property("automated_import_should_detect_type", False)
    ui.set_editor_property("mesh_type_to_import", {
        "static": unreal.FBXImportType.FBXIT_STATIC_MESH,
        "skeletal": unreal.FBXImportType.FBXIT_SKELETAL_MESH,
        "anim": unreal.FBXImportType.FBXIT_ANIMATION}[kind])
    if skeleton is not None:
        ui.set_editor_property("skeleton", skeleton)
    if kind == "static":
        d = ui.get_editor_property("static_mesh_import_data")
        set_props(d, combine_meshes=True, generate_lightmap_u_vs=False, auto_generate_collision=True,
                  import_uniform_scale=scale, build_nanite=False,
                  normal_import_method=unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS)
    elif kind == "skeletal":
        d = ui.get_editor_property("skeletal_mesh_import_data")
        set_props(d, import_uniform_scale=scale, import_morph_targets=False,
                  normal_import_method=unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS)
    else:
        d = ui.get_editor_property("anim_sequence_import_data")
        set_props(d, import_uniform_scale=scale, use_default_sample_rate=False, custom_sample_rate=30,
                  animation_length=unreal.FBXAnimationLengthImportType.FBXALIT_EXPORTED_TIME,
                  import_bone_tracks=True)
    return ui


def run_import(filename, dest, name, ui):
    t = unreal.AssetImportTask()
    t.set_editor_property("filename", filename)
    t.set_editor_property("destination_path", dest)
    t.set_editor_property("destination_name", name)
    t.set_editor_property("replace_existing", True)
    t.set_editor_property("replace_existing_settings", True)
    t.set_editor_property("automated", True)
    t.set_editor_property("save", False)
    t.set_editor_property("options", ui)
    TOOLS.import_asset_tasks([t])
    return [EAL.load_asset(p) for p in (t.get_editor_property("imported_object_paths") or [])]


def uasset_file(path):
    return os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_content_dir()),
                        path[len("/Game/"):] + ".uasset")


def on_disk(path):
    return os.path.isfile(uasset_file(path))


def delete_if_exists(path):
    """Delete an asset so the next import starts from nothing: an import over an existing asset
    keeps that asset's stored settings (notably its import scale), whatever options are passed.
    EditorAssetLibrary.delete_asset reports success but does nothing while a saved level still
    references the asset (MaterialTest holds the crab, for one), so referenced assets are removed
    from disk directly, before anything loads them. The level is rebuilt by build_level.py and
    references by path, so it picks the new asset up."""
    if not (EAL.does_asset_exist(path) or on_disk(path)):
        return
    refs = [r for r in EAL.find_package_referencers_for_asset(path, False) if r != path]
    if refs and on_disk(path):
        os.remove(uasset_file(path))
        log(f"removed {path} from disk (referenced by {', '.join(refs)})")
    else:
        if not EAL.delete_asset(path):
            raise RuntimeError(f"could not delete existing {path} before reimport")
        unreal.SystemLibrary.collect_garbage()
    if on_disk(path):
        raise RuntimeError(f"{path} still exists after deleting it")


def applied_scale(asset):
    """The import scale the asset actually carries (an import over an existing asset ignores the
    options passed in and keeps this), or None when it cannot be read."""
    try:
        return float(asset.get_editor_property("asset_import_data").get_editor_property("import_uniform_scale"))
    except Exception:
        return None


def scale_fix(extent_max, lo, hi):
    """Blender exports 1 unit = 1 m; a wrong exporter scale shows up as a factor of 100.
    Returns the import scale that would bring the largest extent into [lo, hi] cm."""
    if FORCED_SCALE:
        return float(FORCED_SCALE)
    if extent_max <= 0:
        return 1.0
    if extent_max < lo and lo <= extent_max * 100.0 <= hi * 4:
        return 100.0
    if extent_max > hi * 30 and lo / 4 <= extent_max * 0.01 <= hi:
        return 0.01
    return 1.0


def import_static_meshes():
    files = sorted(glob.glob(os.path.join(MESH_DIR, "SM_*.fbx")) + glob.glob(os.path.join(MESH_DIR, "SM_*.FBX")))
    if not files:
        log(f"no SM_*.fbx in {MESH_DIR}, skipping static mesh import")
        return []
    EAL.make_directory(ENV_MESH_ROOT)
    EAL.make_directory(ENV_MAT_ROOT)
    unreal.SystemLibrary.execute_console_command(None, "Interchange.FeatureFlags.Import.FBX 0")
    done = []
    for f in files:
        name = os.path.splitext(os.path.basename(f))[0]
        full = f"{ENV_MESH_ROOT}/{name}"
        scale = float(FORCED_SCALE) if FORCED_SCALE else 1.0
        mesh = None
        for attempt in range(3):
            delete_if_exists(full)
            meshes = [m for m in run_import(f, ENV_MESH_ROOT, name, fbx_ui("static", scale)) if isinstance(m, unreal.StaticMesh)]
            if not meshes:
                break
            mesh = meshes[0]
            box = mesh.get_bounding_box()
            ext = max(box.max.x - box.min.x, box.max.y - box.min.y, box.max.z - box.min.z)
            used = applied_scale(mesh) or scale
            want = float(FORCED_SCALE) if FORCED_SCALE else scale_fix(ext / used, 1.5, 400.0)
            if abs(used - want) < 1e-9:
                break
            warn(f"{name}: extent {ext:.3f} cm at import scale x{used:g} looks wrong, reimporting at x{want:g}")
            scale = want
        if mesh is None:
            warn(f"import of {name}.fbx produced no StaticMesh")
            continue
        done.append((mesh, name, box))
    for mesh, name, box in done:
        base = name[3:]
        kind = prop_kind(base)
        mi_name = f"MI_{base}"
        tex = texture_set(ENV_TEX_ROOT, f"T_{base}")
        if not tex:
            stem = re.sub(r"[_]?\d+[A-Za-z]?$", "", base)
            tex = texture_set(ENV_TEX_ROOT, f"T_{stem}") if stem else {}
        parent = EAL.load_asset(f"{MAT_ROOT}/M_EnvProp")
        hex_str, rough = PROP_KINDS[kind]
        mi = make_instance(ENV_MAT_ROOT, mi_name, parent, scalars={"FallbackRoughness": rough},
                           vectors={"FallbackColor": linear(hex_str)}, textures=tex,
                           switches={"UseTextures": len(tex) == 3})
        if tex and len(tex) < 3:
            warn(f"{mi_name}: only {sorted(tex)} found, need BaseColor, Normal and ORM together; using the {kind} colour")
        for i in range(len(mesh.get_editor_property("static_materials"))):
            mesh.set_material(i, mi)
        EAL.save_loaded_asset(mesh, False)
        size = (box.max.x - box.min.x, box.max.y - box.min.y, box.max.z - box.min.z)
        summary["meshes"].append(f"{ENV_MESH_ROOT}/{name} size=({size[0]:.0f}, {size[1]:.0f}, {size[2]:.0f}) cm "
                                 f"zmin={box.min.z:.1f} -> {mi_name} ({kind})")
    return [d[1] for d in done]


def bone_world(pose, name):
    try:
        return pose.get_bone_pose(name, unreal.AnimPoseSpaces.WORLD).translation
    except Exception:
        return None


def check_crab(sk):
    """Report scale, orientation and contract bones of the imported skeletal mesh."""
    box = sk.get_bounds()
    size = [box.box_extent.x * 2, box.box_extent.y * 2, box.box_extent.z * 2]
    note(f"{SK_NAME} bounds: {size[0]:.0f} x {size[1]:.0f} x {size[2]:.0f} cm (X x Y x Z), "
         f"centre ({box.origin.x:.0f}, {box.origin.y:.0f}, {box.origin.z:.0f})")
    skel = sk.get_editor_property("skeleton")
    pose = skel.get_reference_pose()
    names = [str(n) for n in pose.get_bone_names()]
    note(f"{SK_NAME}: {len(names)} bones, root bone '{names[0] if names else '?'}'")
    if names and names[0] != "root":
        warn(f"{SK_NAME}: root bone is '{names[0]}', the contract says 'root' (an exporter node above the armature?)")
    missing = [b for b in CRAB_BONES if b not in names]
    if missing:
        warn(f"{SK_NAME}: contract bones missing: {', '.join(missing[:12])}{' ...' if len(missing) > 12 else ''} ({len(missing)} total)")
    body = bone_world(pose, "body")
    eyes = [bone_world(pose, n) for n in ("eye_L_1", "eye_R_1")]
    eyes = [e for e in eyes if e is not None]
    claw = bone_world(pose, "claw_major_1")
    if body is not None and eyes:
        fx = sum(e.x for e in eyes) / len(eyes) - body.x
        fy = sum(e.y for e in eyes) / len(eyes) - body.y
        axis = ("+X" if fx > 0 else "-X") if abs(fx) >= abs(fy) else ("+Y" if fy > 0 else "-Y")
        note(f"{SK_NAME} faces {axis} (eyes minus body: {fx:.1f}, {fy:.1f} cm)")
        if axis != "+X":
            warn(f"{SK_NAME} faces {axis} in Unreal but the contract says +X. "
                 "With the default Blender FBX axes (forward -Z, up Y) Blender +X becomes Unreal +X and "
                 "Blender Y is mirrored to Unreal -Y, so the crab should face Blender +X.")
    if body is not None and claw is not None:
        side = "+Y" if claw.y - body.y > 0 else "-Y"
        note(f"major claw is on {side} ({claw.y - body.y:.1f} cm from body); the contract says +Y (the crab's right)")
        if side != "+Y":
            warn("major claw is not on +Y")
    zmin = box.origin.z - box.box_extent.z
    note(f"{SK_NAME} lowest point in the reference pose: Z = {zmin:.1f} cm (the contract wants the foot tips at Z = 0)")
    if abs(zmin) > 3.0:
        warn(f"{SK_NAME}: lowest point is at Z = {zmin:.1f} cm, feet should rest on Z = 0")
    slots = [str(m.get_editor_property("material_slot_name")) for m in sk.get_editor_property("materials")]
    note(f"{SK_NAME} material slots: {slots} (contract: one slot named {CRAB_MI})")
    if len(slots) != 1 or slots[0] != CRAB_MI:
        warn(f"{SK_NAME}: expected exactly one material slot named {CRAB_MI}, got {slots}")


def import_crab():
    src = os.path.join(MESH_DIR, SK_NAME + ".fbx")
    if not os.path.isfile(src):
        log(f"{src} not found, skipping the skeletal mesh (existing assets are left alone)")
        return None
    EAL.make_directory(SK_ROOT)
    unreal.SystemLibrary.execute_console_command(None, "Interchange.FeatureFlags.Import.FBX 0")
    # A fresh skeleton every run (deleted inside the loop below), so re-imports never inherit stale
    # settings. The animations are re-imported after this, so nothing is left pointing at the old one.
    scale = float(FORCED_SCALE) if FORCED_SCALE else 1.0
    sk = None
    for attempt in range(3):
        for p in (f"{SK_ROOT}/{SK_NAME}", f"{SK_ROOT}/{SK_NAME}_Skeleton", f"{SK_ROOT}/{SK_NAME}_PhysicsAsset"):
            delete_if_exists(p)
        res = [m for m in run_import(src, SK_ROOT, SK_NAME, fbx_ui("skeletal", scale)) if isinstance(m, unreal.SkeletalMesh)]
        if not res:
            warn(f"import of {SK_NAME}.fbx produced no SkeletalMesh")
            return None
        sk = res[0]
        box = sk.get_bounds()
        ext = max(box.box_extent.x, box.box_extent.y, box.box_extent.z) * 2
        used = applied_scale(sk) or scale
        want = float(FORCED_SCALE) if FORCED_SCALE else scale_fix(ext / used, 20.0, 600.0)
        if abs(used - want) < 1e-9:
            scale = used
            break
        warn(f"{SK_NAME}: largest extent {ext:.3f} cm at import scale x{used:g}, contract says about 220 cm wide; reimporting at x{want:g}")
        scale = want
    mi = EAL.load_asset(f"{MAT_ROOT}/{CRAB_MI}")
    if mi is not None:
        slots = list(sk.get_editor_property("materials"))
        for slot in slots:
            slot.set_editor_property("material_interface", mi)
        sk.set_editor_property("materials", slots)
    EAL.save_loaded_asset(sk, False)
    # The importer creates the skeleton as a separate asset that nothing else saves. Without it on
    # disk the mesh has no skeleton and every animation reports "Invalid USkeleton" in a new session.
    skeleton = sk.get_editor_property("skeleton")
    if skeleton is None:
        raise RuntimeError(f"{SK_NAME} imported without a skeleton")
    EAL.save_loaded_asset(skeleton, False)
    summary["meshes"].append(f"{SK_ROOT}/{SK_NAME} (import scale x{scale:g}), skeleton {skeleton.get_path_name().split('.')[0]}")
    check_crab(sk)
    return sk, scale


def check_anim(anim, name):
    length = anim.get_editor_property("sequence_length")
    frames = unreal.AnimationLibrary.get_num_frames(anim)
    key = name.split("A_FiddlerCrab_")[-1]
    line = f"{name}: {length:.3f} s, {frames} frames"
    want = ANIM_LENGTHS.get(key)
    if want is not None and abs(length - want) > 1.0 / 30.0:
        warn(f"{name}: length {length:.3f} s, contract says {want} s")
    try:
        r0 = unreal.AnimationLibrary.get_bone_pose_for_time(anim, "root", 0.0, False)
        r1 = unreal.AnimationLibrary.get_bone_pose_for_time(anim, "root", length, False)
        move = (r1.translation - r0.translation).length()
        line += f", root drift {move:.2f} cm"
        if move > 0.5:
            warn(f"{name}: root moves {move:.1f} cm; clips must be in place")
    except Exception as e:
        line += f" (root check skipped: {e})"
    if key not in ONE_SHOT:
        try:
            worst = 0.0
            for bone in ("body", "claw_major_1", "leg_R1_4", "eye_L_2"):
                if not unreal.AnimationLibrary.does_bone_name_exist(anim, bone):
                    continue
                a = unreal.AnimationLibrary.get_bone_pose_for_time(anim, bone, 0.0, False)
                b = unreal.AnimationLibrary.get_bone_pose_for_time(anim, bone, length, False)
                worst = max(worst, (b.translation - a.translation).length())
            line += f", loop seam {worst:.2f} cm"
            if worst > 0.5:
                warn(f"{name}: the last pose differs from the first by {worst:.1f} cm, so the loop will pop; "
                     "end the clip on a copy of its first frame")
        except Exception as e:
            line += f" (loop check skipped: {e})"
    summary["anims"].append(line)
    log(line)


def clip_track_keys(anim, bone):
    """(positions, rotations, scales) of a bone's local track, sampled at every key of the clip."""
    keys = max(unreal.AnimationLibrary.get_num_keys(anim), 1)
    length = anim.get_editor_property("sequence_length")
    pos, rot, sc = [], [], []
    for i in range(keys):
        t = length * i / (keys - 1) if keys > 1 else 0.0
        tr = unreal.AnimationLibrary.get_bone_pose_for_time(anim, bone, t, False)
        pos.append(tr.translation)
        rot.append(tr.rotation)
        sc.append(tr.scale3d)
    return pos, rot, sc


def match_root_to_reference(anim, skeleton, name):
    """Make a clip agree with the skeleton about scale.

    Blender's FBX exporter (Apply Scalings: FBX_SCALE_NONE) keeps the m to cm factor on the
    Armature node. The skeletal mesh import turns that into a root bone at scale 100 with bone
    translations still in metres; the animation import drops the node, so a clip has root scale
    1. Played back, the skin matrices then shrink the crab to 1 percent. Two cases are fixed,
    told apart by the clip's body bone offset against the skeleton's:
      * same units (clip in metres, only the root scale differs): write the skeleton's root
        transform into the clip as a constant key;
      * clip in cm (offset larger by the root scale): also scale every translation key back to
        the skeleton's own units, so root scale times translation lands where it should.
    Anything else is reported and left alone: fix that at the source."""
    ref = skeleton.get_reference_pose()
    ref_root = ref.get_bone_pose("root", unreal.AnimPoseSpaces.LOCAL)
    cur = unreal.AnimationLibrary.get_bone_pose_for_time(anim, "root", 0.0, False)
    diff = (abs(cur.scale3d.x - ref_root.scale3d.x) + abs(cur.scale3d.y - ref_root.scale3d.y)
            + abs(cur.scale3d.z - ref_root.scale3d.z))
    if diff < 1e-3:
        return
    ref_len = ref.get_bone_pose("body", unreal.AnimPoseSpaces.LOCAL).translation.length()
    clip_len = unreal.AnimationLibrary.get_bone_pose_for_time(anim, "body", 0.0, False).translation.length()
    ratio = clip_len / ref_len if ref_len > 1e-6 else 0.0
    factor = 1.0
    if 0.5 < ratio < 2.0:
        what = "in the skeleton's units"
    elif abs(ratio / ref_root.scale3d.x - 1.0) < 0.2 and cur.scale3d.x < 1.5:
        factor = 1.0 / ref_root.scale3d.x
        what = f"in cm (offset x{ratio:.0f}), translations scaled by {factor:g}"
    else:
        warn(f"{name}: root scale is {cur.scale3d.x:g} in the clip and {ref_root.scale3d.x:g} in the skeleton, and the "
             f"body bone offset differs by x{ratio:g} ({clip_len:.3f} vs {ref_len:.3f}); the clip and the mesh were "
             "exported at different scales. Left unpatched: the crab will play back at the wrong size.")
        return
    ctrl = anim.get_editor_property("controller")
    ctrl.open_bracket("match the clip to the skeleton's scale", False)
    ok = True
    if factor != 1.0:
        for bone in anim.get_editor_property("data_model_interface").get_bone_track_names():
            pos, rot, sc = clip_track_keys(anim, bone)
            pos = [unreal.Vector(p.x * factor, p.y * factor, p.z * factor) for p in pos]
            ok = ctrl.set_bone_track_keys(bone, pos, rot, sc, False) and ok
    ok = ctrl.set_bone_track_keys("root", [ref_root.translation], [ref_root.rotation], [ref_root.scale3d], False) and ok
    ctrl.close_bracket(False)
    if not ok:
        warn(f"{name}: could not patch the clip (root scale {cur.scale3d.x:g}, skeleton {ref_root.scale3d.x:g})")
        return
    note(f"{name}: clip root scale was {cur.scale3d.x:g} but the skeleton's is {ref_root.scale3d.x:g}, clip {what}; "
         "wrote the skeleton's root transform into the clip (see Art/unreal/README.md, 'FBX scale')")


def import_anims(sk_result):
    files = sorted(glob.glob(os.path.join(ANIM_DIR, "A_FiddlerCrab_*.fbx")))
    if not files:
        log(f"no A_FiddlerCrab_*.fbx in {ANIM_DIR}, skipping animation import")
        return
    skeleton, scale = None, 1.0
    if sk_result is not None:
        skeleton = sk_result[0].get_editor_property("skeleton")
        scale = sk_result[1]
    else:
        existing = EAL.load_asset(f"{SK_ROOT}/{SK_NAME}")
        if existing is not None:
            skeleton = existing.get_editor_property("skeleton")
    if skeleton is None:
        warn("animations found but no imported skeleton to attach them to; import the skeletal mesh first")
        return
    if FORCED_SCALE:
        scale = float(FORCED_SCALE)
    EAL.make_directory(ANIM_ROOT)
    for f in files:
        name = os.path.splitext(os.path.basename(f))[0]
        delete_if_exists(f"{ANIM_ROOT}/{name}")
        res = [a for a in run_import(f, ANIM_ROOT, name, fbx_ui("anim", scale, skeleton)) if isinstance(a, unreal.AnimSequence)]
        if not res:
            warn(f"import of {name}.fbx produced no AnimSequence")
            continue
        anim = res[0]
        match_root_to_reference(anim, skeleton, name)
        key = name.split("A_FiddlerCrab_")[-1]
        if key not in ONE_SHOT:
            try:
                anim.set_editor_property("loop", True)
            except Exception:
                pass  # older engines have no loop flag on the asset; the game loops in the player
        EAL.save_loaded_asset(anim, False)
        check_anim(anim, name)


# ---------------------------------------------------------------- main

def main():
    for p in (MAT_ROOT, ENV_MAT_ROOT):
        EAL.make_directory(p)
    log(f"export dir {EXPORT_DIR}")

    import_textures()                       # first: materials look for the textures
    mpc = build_mpc()
    build_terrain(mpc)
    build_water(mpc)
    shell = build_crab_shell()
    prop = build_prop_master(mpc)
    build_crab_instance(shell)
    build_prop_defaults(prop)

    import_static_meshes()
    crab = import_crab()
    import_anims(crab)

    # Anything the importers created but nobody saved (skeleton, physics asset) goes to disk too.
    EAL.save_directory(CRAB_ROOT, True, True)

    log("===== summary =====")
    for k in ("materials", "instances", "textures", "meshes", "anims"):
        log(f"{k} ({len(summary[k])}):")
        for item in summary[k]:
            log(f"  {item}")
    for n in summary["notes"]:
        log(f"note: {n}")
    for w in summary["warnings"]:
        unreal.log_warning(f"[build_content] warning: {w}")
    unreal.log("[build_content] CRABSIM_BUILD_CONTENT_OK")


try:
    main()
except Exception:
    unreal.log_error("[build_content] FAILED\n" + traceback.format_exc())
    raise
