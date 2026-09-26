"""Re-import the exported crab FBX files in Blender and check them against the art contract.

  blender -b --factory-startup --python-exit-code 1 --python Art/blender/verify_crab_exports.py

Checks: bone names and hierarchy, one material named MI_FiddlerCrab, triangle count, <=4 influences per
vertex, size in metres, clip lengths at 30 fps, root never moves, loops close, planted feet on Z = 0.
Prints CRAB_VERIFY_OK on success, exits non-zero on failure.
"""
import os
import sys
import math
import numpy as np
import bpy
from mathutils import Vector

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import crab_lib as L  # noqa: E402
import crab_skel as K  # noqa: E402

CONTRACT_LEN = {"Idle": 4.0, "Scuttle": 0.8, "Dance": 3.2, "Dash": 0.35}
LOOPS = {"Idle": True, "Scuttle": True, "Dance": True, "Dash": False}
fails = []


def check(cond, msg):
    print(("  ok   " if cond else "  FAIL ") + msg)
    if not cond:
        fails.append(msg)


def wipe():
    bpy.ops.wm.read_factory_settings(use_empty=True)


def import_fbx(path):
    wipe()
    bpy.ops.import_scene.fbx(filepath=path, automatic_bone_orientation=False, use_anim=True)




# ---------------------------------------------------------------- raw FBX --
def raw_models(path):
    """Read Model nodes (name -> {Lcl Translation, Lcl Scaling}) and the first key of translation curves straight
    from the FBX, bypassing any importer unit handling."""
    from io_scene_fbx import parse_fbx
    root, _ver = parse_fbx.parse(path)
    objs = [e for e in root.elems if e.id == b'Objects'][0]
    models = {}
    for e in objs.elems:
        if e.id == b'Model':
            name = e.props[1].decode().split("\x00")[0]
            props = {}
            for pe in [x for x in e.elems if x.id == b'Properties70'][0].elems:
                props[pe.props[0]] = pe.props[4:]
            models[e.props[0]] = (name, props)
    gs = [e for e in root.elems if e.id == b'GlobalSettings'][0]
    unit = None
    for p in [e for e in gs.elems if e.id == b'Properties70'][0].elems:
        if p.props[0] == b'UnitScaleFactor':
            unit = p.props[4]
    curves = {e.props[0]: e for e in objs.elems if e.id == b'AnimationCurve'}
    cnodes = {e.props[0] for e in objs.elems if e.id == b'AnimationCurveNode'}
    node_model, node_curves = {}, {}
    for c in [e for e in root.elems if e.id == b'Connections'][0].elems:
        if len(c.props) > 3:
            if c.props[1] in cnodes and c.props[2] in models:
                node_model[c.props[1]] = (models[c.props[2]][0], c.props[3])
            if c.props[1] in curves and c.props[2] in cnodes:
                node_curves.setdefault(c.props[2], []).append((c.props[3], c.props[1]))
    first = {}
    for nid, (mname, prop) in node_model.items():
        if prop == b'Lcl Translation':
            vec = {}
            for axis, cid in node_curves.get(nid, []):
                vec[axis.decode()[-1]] = float([x for x in curves[cid].elems if x.id == b'KeyValueFloat'][0].props[0][0])
            first[mname] = np.array([vec.get("X", 0.0), vec.get("Y", 0.0), vec.get("Z", 0.0)])
    return unit, {n: p for n, p in models.values()}, first


def check_raw_units():
    print("raw FBX units (what Unreal reads)")
    unit, sk_models, _ = raw_models(os.path.join(L.MESH_DIR, "SK_FiddlerCrab.fbx"))
    check(unit == 1.0, f"SK file unit is centimetres (UnitScaleFactor {unit})")
    bad = [n for n, p in sk_models.items() if p.get(b'Lcl Scaling') and any(abs(s - 1.0) > 1e-6 for s in p[b'Lcl Scaling'])]
    check(not bad, f"SK: no node carries a scale ({bad[:3]})")
    ref = np.array(sk_models["leg_R1_1"][b'Lcl Translation'])
    check(30 < np.linalg.norm(ref) < 60, f"SK leg_R1_1 local translation is in centimetres {tuple(np.round(ref, 2))}")
    for name in CONTRACT_LEN:
        unit, models, first = raw_models(os.path.join(L.ANIM_DIR, f"A_FiddlerCrab_{name}.fbx"))
        bad = [n for n, p in models.items() if p.get(b'Lcl Scaling') and any(abs(s - 1.0) > 1e-6 for s in p[b'Lcl Scaling'])]
        check(unit == 1.0 and not bad, f"{name}: unit cm, no node scale ({bad[:3]})")
        if "leg_R1_1" in first:
            d = np.abs(first["leg_R1_1"] - ref).max()
            check(d < 1e-2, f"{name}: leg_R1_1 track {tuple(np.round(first['leg_R1_1'], 2))} matches the SK reference pose {tuple(np.round(ref, 2))}")
        if "body" in first:
            check(20 < np.linalg.norm(first["body"]) < 60, f"{name}: body track is in centimetres {tuple(np.round(first['body'], 2))}")


expected = list(L.bone_specs().keys())

# ------------------------------------------------------------------ mesh ---
print("SK_FiddlerCrab.fbx")
import_fbx(os.path.join(L.MESH_DIR, "SK_FiddlerCrab.fbx"))
arms = [o for o in bpy.data.objects if o.type == 'ARMATURE']
meshes = [o for o in bpy.data.objects if o.type == 'MESH']
check(len(arms) == 1 and len(meshes) == 1, f"one armature and one mesh ({len(arms)}, {len(meshes)})")
arm, mesh = arms[0], meshes[0]
names = [b.name for b in arm.data.bones]
check(sorted(names) == sorted(expected), f"49 contract bones present, no extras ({len(names)} bones)")
check(all(b.name == "root" for b in arm.data.bones if b.parent is None), "root is the only root bone")
for n in expected:
    par = L.bone_specs()[n]["parent"]
    got = arm.data.bones[n].parent.name if arm.data.bones[n].parent else None
    if par != got:
        check(False, f"parent of {n}: expected {par}, got {got}")
me = mesh.data
tris = sum(len(p.vertices) - 2 for p in me.polygons)
check(23000 <= tris <= 27000, f"triangles: {tris}")
check(len(me.materials) == 1 and me.materials[0].name.startswith("MI_FiddlerCrab"), f"one material slot: {[m.name for m in me.materials]}")
vg_names = {vg.name for vg in mesh.vertex_groups}
check(vg_names <= set(expected), "vertex groups are all contract bones")
maxinf = max(len([g for g in v.groups if g.weight > 1e-4]) for v in me.vertices)
check(maxinf <= 4, f"max influences per vertex: {maxinf}")
bpy.context.view_layer.update()
mw = mesh.matrix_world
zs = [(mw @ v.co).z for v in me.vertices]
xs = [(mw @ v.co).x for v in me.vertices]
ys = [(mw @ v.co).y for v in me.vertices]
print(f"  info  bounds (Blender import, metres): x {min(xs):.2f}..{max(xs):.2f}  y {min(ys):.2f}..{max(ys):.2f}  z {min(zs):.2f}..{max(zs):.2f}")
check(abs(min(zs)) < 0.02 and 0.6 < max(zs) < 1.0, "feet at Z=0, height in metres")
check(2.0 < (max(ys) - min(ys)) < 2.4, f"width with legs {max(ys)-min(ys):.2f} m (~2.2)")
hb = arm.data.bones["claw_major_1"].head_local
hl = arm.data.bones["eye_L_2"].head_local
print(f"  info  claw_major_1 head {tuple(round(c, 3) for c in hb)}  eye_L_2 head {tuple(round(c, 3) for c in hl)}")
sk = K.Skel()
worst = 0
for n in expected:
    h = np.array(arm.matrix_world @ arm.data.bones[n].head_local)
    worst = max(worst, np.abs(h - sk.head[n]).max())
check(worst < 2e-3, f"bone heads match the authored skeleton (max diff {worst*1000:.2f} mm)")

# ------------------------------------------------------------------ anims --
for name, want in CONTRACT_LEN.items():
    print(f"A_FiddlerCrab_{name}.fbx")
    import_fbx(os.path.join(L.ANIM_DIR, f"A_FiddlerCrab_{name}.fbx"))
    arm = [o for o in bpy.data.objects if o.type == 'ARMATURE'][0]
    meshes = [o for o in bpy.data.objects if o.type == 'MESH']
    check(len(meshes) == 0, "armature only (no mesh)")
    acts = list(bpy.data.actions)
    check(len(acts) == 1, f"exactly one action ({[a.name for a in acts]})")
    act = acts[0]
    fps = bpy.context.scene.render.fps / bpy.context.scene.render.fps_base
    f0, f1 = act.frame_range
    dur = (f1 - f0) / fps
    print(f"  info  fps {fps:.2f}  frames {f0:.0f}..{f1:.0f}  length {dur:.4f}s (contract {want})")
    check(abs(fps - 30) < 1e-3, "30 fps")
    check(abs(dur - want) < 0.02 + 1e-6, f"length {dur:.3f}s vs contract {want}s")
    # sample the evaluated pose at every frame
    arm.animation_data.action = act
    sc = bpy.context.scene
    frames = list(range(int(round(f0)), int(round(f1)) + 1))
    rootpos, feet, first, last = [], [], None, None
    bnames = [b.name for b in arm.data.bones]
    check(sorted(bnames) == sorted(expected), "same 49 bones in the anim file")
    footz_min = 1e9
    planted = 0
    for f in frames:
        sc.frame_set(f)
        pose = {b.name: np.array((arm.matrix_world @ (b.matrix @ Vector((0, sk.len[b.name], 0))))) for b in arm.pose.bones}
        rootpos.append(np.array((arm.matrix_world @ arm.pose.bones["root"].head)))
        for lg in ("L1", "L2", "L3", "L4", "R1", "R2", "R3", "R4"):
            z = pose[f"leg_{lg}_4"][2]
            footz_min = min(footz_min, z)
        sig = np.concatenate([np.array(arm.matrix_world @ b.head) for b in arm.pose.bones])
        if first is None:
            first = sig
        last = sig
    if name == "Idle":
        print("  info  armature object scale", tuple(round(c, 4) for c in arm.matrix_world.to_scale()),
              "leg_L1_4 head f0", tuple(round(c, 3) for c in (arm.matrix_world @ arm.pose.bones["leg_L1_4"].head)))
    rp = np.array(rootpos)
    check(np.abs(rp - rp[0]).max() < 1e-4, "root never moves")
    check(footz_min > -0.003, f"no foot below the ground (min tip Z {footz_min*1000:.2f} mm)")
    if LOOPS[name]:
        d = np.abs(first - last).max()
        check(d < 2e-3, f"loop closes (max bone head diff first/last frame {d*1000:.3f} mm)")

check_raw_units()
print("CRAB_VERIFY_OK" if not fails else f"CRAB_VERIFY_FAILED: {len(fails)}")
if fails:
    sys.exit(1)
