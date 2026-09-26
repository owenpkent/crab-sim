"""Rig, skin, animate and export the fiddler crab (headless Blender 5.2).

  blender -b --factory-startup --python-exit-code 1 --python Art/blender/rig_animate_crab.py

Reads  Art/blender/_work/crab_lowpoly.blend (from build_crab.py)
Writes Art/export/meshes/SK_FiddlerCrab.fbx            mesh + armature, rest pose
       Art/export/anims/A_FiddlerCrab_{Idle,Scuttle,Dance,Dash}.fbx   armature only, one action each
       Art/blender/_work/crab_rigged.blend             for previews
"""
import os
import sys
import time
import math
import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bpy  # noqa: E402
from mathutils import Vector, Matrix  # noqa: E402
import crab_lib as L  # noqa: E402
import crab_skel as K  # noqa: E402

FPS = 30
# FBX export axes: crab faces +X in Blender; UE wants +X forward. Verified against Unreal, see README-crab.md.
FBX_FORWARD = os.environ.get("CRAB_FBX_FORWARD", "-Z")
FBX_UP = os.environ.get("CRAB_FBX_UP", "Y")


def smoothstep(a, b, x):
    t = np.clip((np.asarray(x, dtype=np.float64) - a) / (b - a), 0.0, 1.0)
    return t * t * (3 - 2 * t)


# --------------------------------------------------------------- armature --
CM = 100.0      # export copies are authored in centimetres so no node in the FBX carries a scale


def build_armature(skel, scale=1.0):
    arm = bpy.data.armatures.new("SK_FiddlerCrab_Skeleton")
    ob = bpy.data.objects.new("Armature", arm)
    bpy.context.scene.collection.objects.link(ob)
    bpy.context.view_layer.objects.active = ob
    ob.select_set(True)
    bpy.ops.object.mode_set(mode='EDIT')
    ebs = {}
    for n in skel.names:
        eb = arm.edit_bones.new(n)
        eb.head = Vector(skel.head[n] * scale)
        eb.tail = Vector(skel.tail[n] * scale)
        eb.align_roll(Vector(skel.R[n][:, 2]))
        eb.use_deform = True
        ebs[n] = eb
    for n in skel.names:
        p = skel.parent[n]
        if p:
            ebs[n].parent = ebs[p]
            ebs[n].use_connect = False
    bpy.ops.object.mode_set(mode='OBJECT')
    # verify roll: bone rest frames must equal the numpy frames
    worst = 0.0
    for n in skel.names:
        M = np.array(arm.bones[n].matrix_local)
        worst = max(worst, np.abs(M[:3, :3] - skel.R[n]).max(), np.abs(M[:3, 3] - skel.head[n] * scale).max() / scale)
    print(f"[rig] armature built: {len(skel.names)} bones, rest-frame mismatch vs numpy = {worst:.2e}")
    assert worst < 2e-4, "bone rest frames differ from numpy model"
    return ob


# ----------------------------------------------------------------- weights --
def compute_weights(parts, skel):
    """Per-vertex weights: {vertex_index: {bone: w}} using per-limb rules (rigid segments, narrow joint blends)."""
    W = {}
    off = 0

    def put(vi, d):
        tot = sum(d.values())
        W[vi] = {k: v / tot for k, v in d.items() if v / tot > 1e-4}

    for p in parts:
        n = len(p.V)
        kind = p.info.get("kind", "carapace")
        s = p.sp
        for i in range(n):
            vi = off + i
            if p.name == "carapace":
                put(vi, {"body": 1.0})
            elif kind == "eye":
                sd = p.name[-1]
                js = p.info["js"]
                w2 = float(smoothstep(js[1] - 0.045, js[1] + 0.045, s[i]))
                wb = 1.0 - float(smoothstep(js[0] - 0.03, js[0] + 0.09, s[i]))
                put(vi, {"body": wb, f"eye_{sd}_1": (1 - wb) * (1 - w2), f"eye_{sd}_2": (1 - wb) * w2})
            elif kind == "leg":
                sd, num = p.name[-2], int(p.name[-1])
                js = p.info["js"]
                zw = (0.03, 0.05, 0.045)
                t = [float(smoothstep(js[j] - zw[j - 1], js[j] + zw[j - 1], s[i])) for j in (1, 2, 3)]
                wb = 1.0 - float(smoothstep(0.02, 0.11, s[i]))
                base = {f"leg_{sd}{num}_1": 1 - t[0], f"leg_{sd}{num}_2": t[0] - t[1],
                        f"leg_{sd}{num}_3": t[1] - t[2], f"leg_{sd}{num}_4": t[2]}
                d = {k: v * (1 - wb) for k, v in base.items()}
                d["body"] = wb
                put(vi, d)
            elif kind == "arm":
                nm = "major" if "major" in p.name else "minor"
                sc = 1.0 if nm == "major" else 0.3
                js = p.info["js"]
                w2 = float(smoothstep(js[1] - 0.05 * sc, js[1] + 0.05 * sc, s[i]))
                wb = 1.0 - float(smoothstep(js[0] - 0.02 * sc, js[0] + 0.10 * sc, s[i]))
                w3 = float(smoothstep(js[2] - 0.05 * sc, js[2] + 0.01 * sc, s[i])) * 0.5
                d = {"body": wb, f"claw_{nm}_1": (1 - wb) * (1 - w2),
                     f"claw_{nm}_2": (1 - wb) * w2 * (1 - w3), f"claw_{nm}_3": (1 - wb) * w2 * w3}
                put(vi, d)
            elif kind == "hand":
                nm = "major" if "major" in p.name else "minor"
                x = p.V[i]  # world; use local param via sp (local x)
                w3 = float(smoothstep(0.0, 0.14, s[i]))
                put(vi, {f"claw_{nm}_2": 1 - w3, f"claw_{nm}_3": w3})
            elif kind == "dactyl":
                nm = "major" if "major" in p.name else "minor"
                wf = float(smoothstep(0.02, 0.14, s[i]))
                put(vi, {f"claw_{nm}_3": 1 - wf, f"claw_{nm}_finger": wf})
            elif kind == "mouth":
                sd = p.info["side"]
                wm = float(smoothstep(0.02, 0.09, s[i]))
                put(vi, {"body": 1 - wm, f"mouth_{sd}": wm})
            else:
                raise RuntimeError("unhandled part " + p.name)
        off += n
    return W


def apply_weights(mesh_ob, arm_ob, W, skel):
    for n in skel.names:
        mesh_ob.vertex_groups.new(name=n)
    groups = {vg.name: vg for vg in mesh_ob.vertex_groups}
    by_bone = {}
    for vi, d in W.items():
        for b, w in d.items():
            by_bone.setdefault(b, []).append((vi, w))
    for b, lst in by_bone.items():
        g = groups[b]
        # group vertices by rounded weight to keep the number of calls small
        for vi, w in lst:
            g.add([vi], float(w), 'REPLACE')
    mod = mesh_ob.modifiers.new("Armature", 'ARMATURE')
    mod.object = arm_ob
    mesh_ob.parent = arm_ob
    mesh_ob.matrix_parent_inverse = arm_ob.matrix_world.inverted()


def verify_weights(mesh_ob, arm_ob, parts, W):
    """No leg may drag a neighbour: pose each limb and check other parts do not move."""
    depsgraph = bpy.context.evaluated_depsgraph_get()
    base = np.array([v.co[:] for v in mesh_ob.data.vertices])
    offs = {}
    o = 0
    for p in parts:
        offs[p.name] = (o, o + len(p.V))
        o += len(p.V)
    worst_other = 0.0
    maxinf = max(len(d) for d in W.values())
    print(f"[weights] max influences per vertex = {maxinf}")
    tests = [f"leg_{sd}{i}_2" for sd in "LR" for i in range(1, 5)] + ["claw_major_2", "claw_minor_2", "eye_L_1", "eye_R_2", "mouth_L"]
    for bn in tests:
        pb = arm_ob.pose.bones[bn]
        pb.rotation_mode = 'QUATERNION'
        pb.rotation_quaternion = (math.cos(0.3), math.sin(0.3), 0, 0)
        bpy.context.view_layer.update()
        ev = mesh_ob.evaluated_get(bpy.context.evaluated_depsgraph_get())
        me = ev.to_mesh()
        cur = np.array([v.co[:] for v in me.vertices])
        ev.to_mesh_clear()
        moved = np.abs(cur - base).max(1) > 1e-6
        # which parts moved
        moved_parts = [nm for nm, (a, b) in offs.items() if moved[a:b].any()]
        pb.rotation_quaternion = (1, 0, 0, 0)
        expected = {"leg": lambda nm: nm == f"leg_{bn[4]}{bn[5]}", "claw_major_2": lambda nm: nm.startswith("claw_major"),
                    "claw_minor_2": lambda nm: nm.startswith("claw_minor"), "eye_L_1": lambda nm: nm == "eye_L",
                    "eye_R_2": lambda nm: nm == "eye_R", "mouth_L": lambda nm: nm == "mouth_L"}
        chk = expected["leg"] if bn.startswith("leg") else expected[bn]
        bad = [nm for nm in moved_parts if not chk(nm)]
        print(f"   pose {bn:14s} moves parts: {moved_parts}  {'OK' if not bad else 'LEAK -> ' + str(bad)}")
        assert not bad, f"{bn} drags {bad}"
    bpy.context.view_layer.update()


# --------------------------------------------------------------- actions ---
def write_action(arm_ob, name, frames, skel, loc_scale=1.0):
    """Key every bone on every frame (dense, linear) from baked basis rotations/translations."""
    act = bpy.data.actions.new(name)
    act.use_fake_user = True
    if arm_ob.animation_data is None:
        arm_ob.animation_data_create()
    arm_ob.animation_data.action = act
    for pb in arm_ob.pose.bones:
        pb.rotation_mode = 'QUATERNION'
        pb.rotation_quaternion = (1, 0, 0, 0)
        pb.location = (0, 0, 0)
    prev = {}
    for fi, fr in enumerate(frames):
        for n in skel.names:
            pb = arm_ob.pose.bones[n]
            q = K.mat_to_quat(fr["Rb"].get(n, np.eye(3)))
            if n in prev and np.dot(prev[n], q) < 0:
                q = -q
            prev[n] = q
            pb.rotation_quaternion = tuple(q)
            pb.keyframe_insert("rotation_quaternion", frame=fi, group=n)
            if n == "body":
                pb.location = tuple(np.asarray(fr["Lb"].get("body", np.zeros(3))) * loc_scale)
                pb.keyframe_insert("location", frame=fi, group=n)
    # linear interpolation (keys are dense)
    for fc in iter_fcurves(act):
        for kp in fc.keyframe_points:
            kp.interpolation = 'LINEAR'
    for pb in arm_ob.pose.bones:
        pb.rotation_quaternion = (1, 0, 0, 0)
        pb.location = (0, 0, 0)
    return act


def iter_fcurves(act):
    if hasattr(act, "fcurves"):
        try:
            yield from act.fcurves
            return
        except Exception:
            pass
    for layer in act.layers:
        for strip in layer.strips:
            for slot in act.slots:
                cb = strip.channelbag(slot)
                if cb:
                    yield from cb.fcurves


def export_fbx(path, objects, active, anim=False, nframes=0):
    bpy.ops.object.select_all(action='DESELECT')
    for o in objects:
        o.select_set(True)
    bpy.context.view_layer.objects.active = active
    os.makedirs(os.path.dirname(path), exist_ok=True)
    kw = dict(filepath=path, use_selection=True, use_active_collection=False,
              object_types={'ARMATURE'} if anim else {'ARMATURE', 'MESH'},
              global_scale=0.01, apply_unit_scale=False, apply_scale_options='FBX_SCALE_NONE',   # the exporter multiplies by 100 when apply_unit_scale is off, so 0.01 nets to exactly 1: no node scale, raw numbers stay centimetres
              bake_space_transform=False, axis_forward=FBX_FORWARD, axis_up=FBX_UP,
              add_leaf_bones=False, use_armature_deform_only=False,
              primary_bone_axis='Y', secondary_bone_axis='X', armature_nodetype='NULL',
              mesh_smooth_type='FACE', use_mesh_modifiers=True, use_tspace=False,
              path_mode='STRIP', embed_textures=False, batch_mode='OFF')
    if anim:
        kw.update(bake_anim=True, bake_anim_use_all_actions=False, bake_anim_use_nla_strips=False,
                  bake_anim_step=1.0, bake_anim_simplify_factor=0.0, bake_anim_force_startend_keying=True)
    else:
        kw.update(bake_anim=False)
    bpy.ops.export_scene.fbx(**kw)
    print(f"[export] {os.path.relpath(path, L.ART)}  ({os.path.getsize(path)//1024} KB)")


def make_cm_copies(mesh, arm_ob, skel):
    """Centimetre copies of the rig for export. The working rig stays in metres for previews.
    Exporting the metre rig with 'unit scale' puts a x100 scale on the Armature node while bone translations stay in
    metres; Unreal applies that scale to the reference pose but not to animation tracks, so clips collapse the crab.
    Authoring the export copy in centimetres leaves every FBX node at scale 1 and every translation in centimetres."""
    mesh.name, arm_ob.name = "SK_FiddlerCrab_work", "Armature_work"
    arm_cm = build_armature(skel, scale=CM)
    arm_cm.name = "Armature"
    mesh_cm = mesh.copy()
    mesh_cm.data = mesh.data.copy()
    mesh_cm.name = "SK_FiddlerCrab"
    mesh_cm.data.name = "SK_FiddlerCrab_cm"
    bpy.context.scene.collection.objects.link(mesh_cm)
    mesh_cm.data.transform(Matrix.Scale(CM, 4))
    mesh_cm.data.update()
    for mod in mesh_cm.modifiers:
        if mod.type == 'ARMATURE':
            mod.object = arm_cm
    mesh_cm.parent = arm_cm
    mesh_cm.matrix_parent_inverse = Matrix.Identity(4)
    return mesh_cm, arm_cm


def drop_cm_copies(mesh, arm_ob, mesh_cm, arm_cm):
    for ob in (mesh_cm, arm_cm):
        data = ob.data
        bpy.data.objects.remove(ob)
        if ob is arm_cm or data.users == 0:
            (bpy.data.armatures if data.__class__.__name__ == "Armature" else bpy.data.meshes).remove(data)
    for a in [a for a in bpy.data.actions if a.name.startswith("cm_")]:
        bpy.data.actions.remove(a)
    mesh.name, arm_ob.name = "SK_FiddlerCrab", "Armature"


def main():
    t0 = time.time()
    import crab_motions as M
    bpy.ops.wm.open_mainfile(filepath=os.path.join(L.WORK, "crab_lowpoly.blend"))
    parts = L.build_all()
    skel = K.Skel()
    mesh = bpy.data.objects["crab_low"]
    mesh.name = "SK_FiddlerCrab"
    mesh.data.name = "SK_FiddlerCrab"
    arm_ob = build_armature(skel)
    W = compute_weights(parts, skel)
    apply_weights(mesh, arm_ob, W, skel)
    print(f"[rig] weights applied ({time.time()-t0:.0f}s)")
    verify_weights(mesh, arm_ob, parts, W)
    sc = bpy.context.scene
    sc.render.fps = FPS
    sc.render.fps_base = 1.0
    sc.frame_start, sc.frame_end = 0, 1
    mot = M.Motions()
    baked = {}
    for name, (n, fn) in M.CLIPS.items():
        loop = name != "Dash"
        frames = mot.A.bake(getattr(mot, fn), n, loop=loop)
        baked[name] = frames
        print(f"[anim] {name}: {n} frames, ik err {max(f['err'] for f in frames)*1000:.2f} mm")
        write_action(arm_ob, f"A_FiddlerCrab_{name}", frames, skel)
    # ---- export from centimetre copies
    mesh_cm, arm_cm = make_cm_copies(mesh, arm_ob, skel)
    sc.frame_start, sc.frame_end = 0, 1
    export_fbx(os.path.join(L.MESH_DIR, "SK_FiddlerCrab.fbx"), [mesh_cm, arm_cm], arm_cm)
    for name, (n, fn) in M.CLIPS.items():
        act = write_action(arm_cm, f"cm_{name}", baked[name], skel, loc_scale=CM)
        sc.frame_start, sc.frame_end = 0, n
        arm_cm.animation_data.action = act
        export_fbx(os.path.join(L.ANIM_DIR, f"A_FiddlerCrab_{name}.fbx"), [arm_cm], arm_cm, anim=True, nframes=n)
    drop_cm_copies(mesh, arm_ob, mesh_cm, arm_cm)
    arm_ob.animation_data.action = bpy.data.actions["A_FiddlerCrab_Idle"]
    sc.frame_start, sc.frame_end = 0, 120
    sc.frame_set(0)
    bpy.ops.wm.save_as_mainfile(filepath=os.path.join(L.WORK, "crab_rigged.blend"))
    print(f"[rig] done in {time.time()-t0:.0f}s")


if __name__ == "__main__":
    main()
