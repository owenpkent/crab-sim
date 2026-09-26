"""Try Blender's automatic (bone heat) weights on the game mesh and measure the problems the
procedural per-limb weights in rig_animate_crab.py fix. Diagnostic only, writes nothing.

  blender -b --factory-startup --python Art/blender/check_auto_weights.py
"""
import os
import sys
import time
import numpy as np
import bpy

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import crab_lib as L  # noqa: E402
import crab_skel as K  # noqa: E402
import rig_animate_crab as R  # noqa: E402

bpy.ops.wm.open_mainfile(filepath=os.path.join(L.WORK, "crab_lowpoly.blend"))
parts = L.build_all()
skel = K.Skel()
mesh = bpy.data.objects["crab_low"]
arm = R.build_armature(skel)
bpy.ops.object.select_all(action='DESELECT')
mesh.select_set(True)
arm.select_set(True)
bpy.context.view_layer.objects.active = arm
t0 = time.time()
bpy.ops.object.parent_set(type='ARMATURE_AUTO')
print(f"[auto] heat weights took {time.time()-t0:.1f}s")
names = {vg.index: vg.name for vg in mesh.vertex_groups}
off = 0
leak_parts = {}
unweighted = 0
for p in parts:
    kind = p.info.get("kind", "carapace")
    own = None
    if kind == "leg":
        own = {f"leg_{p.name[-2:]}_{k}" for k in (1, 2, 3, 4)}
        own = {f"leg_{p.name[-2]}{p.name[-1]}_{k}" for k in (1, 2, 3, 4)} | {"body"}
    elif kind in ("arm", "hand", "dactyl"):
        nm = "major" if "major" in p.name else "minor"
        own = {f"claw_{nm}_{s}" for s in ("1", "2", "3", "finger", "thumb")} | {"body"}
    elif kind == "eye":
        own = {f"eye_{p.name[-1]}_1", f"eye_{p.name[-1]}_2", "body"}
    elif kind == "mouth":
        own = {f"mouth_{p.name[-1]}", "body"}
    else:
        own = {"body"}
    bad = 0
    for i in range(len(p.V)):
        v = mesh.data.vertices[off + i]
        gs = [(names[g.group], g.weight) for g in v.groups if g.weight > 0.01]
        if not gs:
            unweighted += 1
        if any(n not in own for n, w in gs):
            bad += 1
    leak_parts[p.name] = bad
    off += len(p.V)
tot = sum(leak_parts.values())
print(f"[auto] vertices with >1% weight on another limb's bones: {tot} of {off}; unweighted vertices: {unweighted}")
for k, v in leak_parts.items():
    if v:
        print(f"   {k:20s} {v}")
