"""Shared geometry library for the fiddler crab (original work, no external assets).

Pure numpy geometry: parts are built as (vertices, faces, per-corner UVs in
metres, per-vertex attributes). build_crab.py turns them into Blender meshes,
rig_animate_crab.py reads the joint spec (BONES) from here so mesh and skeleton
always agree.

Frame: Blender, 1 unit = 1 m. The crab faces +X, its RIGHT side is -Y (the
major claw side), up is +Z. The FBX exporter maps Blender -Y to Unreal +Y.
"""
import math
import os
import numpy as np

TAU = math.tau
HERE = os.path.dirname(os.path.abspath(__file__))
ART = os.path.dirname(HERE)
WORK = os.path.join(HERE, "_work")
TEX_DIR = os.path.join(ART, "export", "textures")
MESH_DIR = os.path.join(ART, "export", "meshes")
ANIM_DIR = os.path.join(ART, "export", "anims")
PREV_DIR = os.path.join(ART, "previews")

# ---------------------------------------------------------------- noise ----


def _hash(ix, iy, iz, seed):
    h = (ix.astype(np.int64) & 0xFFFFFFFF).astype(np.uint32) * np.uint32(374761393)
    h = h + (iy.astype(np.int64) & 0xFFFFFFFF).astype(np.uint32) * np.uint32(668265263)
    h = h + (iz.astype(np.int64) & 0xFFFFFFFF).astype(np.uint32) * np.uint32(1274126177)
    h = h + np.uint32((seed * 2654435761) & 0xFFFFFFFF)
    h ^= h >> np.uint32(15)
    h *= np.uint32(2246822519)
    h ^= h >> np.uint32(13)
    h *= np.uint32(3266489917)
    h ^= h >> np.uint32(16)
    return h.astype(np.float32) * np.float32(1.0 / 4294967295.0)


def vnoise(P, seed=0):
    """3D value noise in [0,1]. P is (N,3)."""
    P = np.asarray(P, dtype=np.float64)
    i = np.floor(P)
    f = (P - i).astype(np.float32)
    u = f * f * f * (f * (f * 6 - 15) + 10)
    ix, iy, iz = i[:, 0], i[:, 1], i[:, 2]

    def h(dx, dy, dz):
        return _hash(ix + dx, iy + dy, iz + dz, seed)

    ux, uy, uz = u[:, 0], u[:, 1], u[:, 2]
    x00 = h(0, 0, 0) + (h(1, 0, 0) - h(0, 0, 0)) * ux
    x10 = h(0, 1, 0) + (h(1, 1, 0) - h(0, 1, 0)) * ux
    x01 = h(0, 0, 1) + (h(1, 0, 1) - h(0, 0, 1)) * ux
    x11 = h(0, 1, 1) + (h(1, 1, 1) - h(0, 1, 1)) * ux
    y0 = x00 + (x10 - x00) * uy
    y1 = x01 + (x11 - x01) * uy
    return y0 + (y1 - y0) * uz


def fbm(P, octaves=4, lac=2.0, gain=0.5, seed=0):
    """Fractal value noise, roughly in [-1,1]."""
    P = np.asarray(P, dtype=np.float64)
    tot = np.zeros(len(P), dtype=np.float32)
    amp = 1.0
    norm = 0.0
    for o in range(octaves):
        tot += amp * (vnoise(P * (lac ** o) + o * 17.3, seed + o * 31) * 2 - 1)
        norm += amp
        amp *= gain
    return tot / norm


def cellular(P, seed=0, jitter=0.95):
    """3D cellular noise. Returns (F1 distance, cell random value in [0,1])."""
    P = np.asarray(P, dtype=np.float64)
    i = np.floor(P)
    f = (P - i).astype(np.float32)
    ix, iy, iz = i[:, 0], i[:, 1], i[:, 2]
    best = np.full(len(P), 9.0, dtype=np.float32)
    cid = np.zeros(len(P), dtype=np.float32)
    for dz in (-1, 0, 1):
        for dy in (-1, 0, 1):
            for dx in (-1, 0, 1):
                cx, cy, cz = ix + dx, iy + dy, iz + dz
                rx = _hash(cx, cy, cz, seed + 1)
                ry = _hash(cx, cy, cz, seed + 2)
                rz = _hash(cx, cy, cz, seed + 3)
                px = dx + 0.5 + (rx - 0.5) * jitter - f[:, 0]
                py = dy + 0.5 + (ry - 0.5) * jitter - f[:, 1]
                pz = dz + 0.5 + (rz - 0.5) * jitter - f[:, 2]
                d = px * px + py * py + pz * pz
                m = d < best
                best = np.where(m, d, best)
                cid = np.where(m, _hash(cx, cy, cz, seed + 4), cid)
    return np.sqrt(best), cid


def smoothstep(a, b, x):
    t = np.clip((np.asarray(x, dtype=np.float64) - a) / (b - a), 0.0, 1.0)
    return t * t * (3 - 2 * t)


def smooth1d(y, k):
    if k <= 0:
        return y
    w = np.exp(-0.5 * (np.arange(-3 * k, 3 * k + 1) / max(k, 1e-6)) ** 2)
    w /= w.sum()
    pad = np.pad(y, (3 * k, 3 * k), mode="edge")
    return np.convolve(pad, w, mode="valid")


def knots(s, pts, k=1):
    """Piecewise-linear knots [(s,val),...] smoothed a little."""
    pts = np.asarray(pts, dtype=np.float64)
    y = np.interp(s, pts[:, 0], pts[:, 1])
    return smooth1d(y, k)


# ----------------------------------------------------------------- parts ---


class Part:
    def __init__(self, name):
        self.name = name
        self.pid = 0
        self.V = np.zeros((0, 3))
        self.F = []          # list of tuples of vertex indices
        self.UV = []         # list of per-face lists of (u,v) metres
        self.isl = []        # island id per face (local ints)
        self.sp = None       # arc-length param (m) per vertex
        self.cs = None       # (cos, sin) around-angle per vertex
        self.sub = 2         # subdivision level for the high poly
        self.uvw = 1.0       # texel-density weight
        self.info = {}

    def fix_winding(self):
        vol = 0.0
        V = self.V
        for f in self.F:
            for k in range(1, len(f) - 1):
                vol += np.dot(V[f[0]], np.cross(V[f[k]], V[f[k + 1]]))
        if vol < 0:
            self.F = [tuple(reversed(f)) for f in self.F]
            self.UV = [list(reversed(u)) for u in self.UV]
        return vol


def resample(pts, n):
    pts = np.asarray(pts, dtype=np.float64)
    seg = np.linalg.norm(np.diff(pts, axis=0), axis=1)
    cum = np.r_[0, np.cumsum(seg)]
    s = np.linspace(0, cum[-1], n)
    P = np.stack([np.interp(s, cum, pts[:, k]) for k in range(3)], 1)
    return P, s, cum


def _norm(v):
    n = np.linalg.norm(v, axis=-1, keepdims=True)
    return v / np.maximum(n, 1e-12)


def make_tube(name, pts, spacing, radius_fn, nsides, ref, profile_fn=None,
              smooth=5, round0=0.0, round1=0.0, flip=False, phase=0.0,
              nrings=None):
    """Closed tube along a path.

    radius_fn(s, L) -> (a, b): radius along U and along W for every ring.
    profile_fn(theta, s, ctx) -> (fx, fy) arrays (M, n): unit-ish section shape
    (default cos, sin). ref: reference direction (3,) or (M,3) defining U.
    round0/round1: metres of hemispherical rounding at each end (poles are
    single vertices at the path ends).
    """
    pts = np.asarray(pts, dtype=np.float64)
    L = float(np.linalg.norm(np.diff(pts, axis=0), axis=1).sum())
    M = nrings if nrings else max(6, int(round(L / spacing)) + 1)
    P, s, cum = resample(pts, M)
    for _ in range(smooth):
        P[1:-1] = 0.25 * P[:-2] + 0.5 * P[1:-1] + 0.25 * P[2:]
    T = _norm(np.gradient(P, axis=0))
    ref = np.asarray(ref, dtype=np.float64)
    if ref.ndim == 1:
        ref = np.tile(ref, (M, 1))
    U = ref - (ref * T).sum(1, keepdims=True) * T
    U = _norm(U)
    W = np.cross(T, U)
    a, b = radius_fn(s, L)
    a = np.array(a, dtype=np.float64) * np.ones(M)
    b = np.array(b, dtype=np.float64) * np.ones(M)
    for r, at_end in ((round0, 0), (round1, 1)):
        if r > 0:
            d = s if at_end == 0 else (L - s)
            t = np.clip(d / r, 0, 1)
            f = np.sqrt(np.maximum(1 - (1 - t) ** 2, 0.0))
            a = a * f
            b = b * f
    n = nsides
    theta = phase + TAU * np.arange(n) / n
    if profile_fn is None:
        fx = np.tile(np.cos(theta), (M, 1))
        fy = np.tile(np.sin(theta), (M, 1))
    else:
        fx, fy = profile_fn(theta, s, dict(P=P, T=T, U=U, W=W, L=L, a=a, b=b))
    ring = (P[:, None, :] + (a[:, None] * fx)[:, :, None] * U[:, None, :]
            + b[:, None, None] * fy[:, :, None] * W[:, None, :])
    r0, r1 = 1, M - 2   # rings 0 and M-1 are the pole vertices
    verts = [P[0]]
    for i in range(r0, r1 + 1):
        verts.extend(ring[i])
    verts.append(P[-1])
    V = np.array(verts)
    nring = r1 - r0 + 1

    def vi(i, j):
        return 1 + (i - r0) * n + (j % n)

    pole0, pole1 = 0, len(V) - 1
    per = np.zeros(M)
    for i in range(r0, r1 + 1):
        e = np.roll(ring[i], -1, axis=0) - ring[i]
        per[i] = np.linalg.norm(e, axis=1).sum()
    F, UV = [], []
    # every ring gets the same UV width (mean perimeter): the strip is a clean
    # rectangle for packing; painting is done in 3D so the stretch is harmless
    wuv = float(per[r0:r1 + 1].mean())
    per = np.full(M, wuv)
    for i in range(r0, r1):
        for j in range(n):
            F.append((vi(i, j), vi(i, j + 1), vi(i + 1, j + 1), vi(i + 1, j)))
            UV.append([(j / n * per[i], s[i]), ((j + 1) / n * per[i], s[i]),
                       ((j + 1) / n * per[i + 1], s[i + 1]), (j / n * per[i + 1], s[i + 1])])
    for j in range(n):
        F.append((pole0, vi(r0, j + 1), vi(r0, j)))
        UV.append([((j + 0.5) / n * per[r0], s[0]), ((j + 1) / n * per[r0], s[r0]),
                   (j / n * per[r0], s[r0])])
        F.append((pole1, vi(r1, j), vi(r1, j + 1)))
        UV.append([((j + 0.5) / n * per[r1], s[-1]), (j / n * per[r1], s[r1]),
                   ((j + 1) / n * per[r1], s[r1])])
    part = Part(name)
    part.V = V
    part.F = F
    part.UV = UV
    part.isl = [0] * len(F)
    sp = [s[0]] + [s[i] for i in range(r0, r1 + 1) for _ in range(n)] + [s[-1]]
    part.sp = np.array(sp)
    cs = [(0.0, 0.0)] + [(math.cos(theta[j]), math.sin(theta[j])) for i in range(r0, r1 + 1) for j in range(n)] + [(0.0, 0.0)]
    part.cs = np.array(cs)
    part.info["L"] = L
    part.info["path"] = P
    if flip:
        part.V = part.V * np.array([1.0, -1.0, 1.0])
        part.F = [tuple(reversed(f)) for f in part.F]
        part.UV = [list(reversed(u)) for u in part.UV]
    part.fix_winding()
    return part


def bump(s, c, w, h):
    return h * np.exp(-0.5 * ((s - c) / w) ** 2)


# -------------------------------------------------------------- spec -------
ZC = 0.31            # carapace centre height
SIDE = {"L": +1.0, "R": -1.0}   # Blender Y sign of each side (right = -Y)

LEG_HIP_X = [0.20, 0.07, -0.07, -0.20]
LEG_HIP_Y = [0.395, 0.405, 0.385, 0.345]
LEG_PHI = [math.radians(a) for a in (24, 8, -8, -24)]
LEG_SCALE = [0.93, 1.0, 0.99, 0.86]
LEG_Z0 = 0.26
# (r outward from hip, z) for hip, coxa end, knee, ankle, foot
LEG_RZ = [(0.0, 0.26), (0.10, 0.275), (0.36, 0.50), (0.52, 0.345), (0.70, 0.0)]


LEG_F = [  # fore-aft offset (m, +forward) of knee, ankle, foot relative to the hip, per leg
    (0.20, 0.22, 0.22),
    (0.10, 0.09, 0.08),
    (-0.10, -0.13, -0.14),
    (-0.20, -0.26, -0.28),
]


def leg_joints(side, i):
    """Five joint points of leg number i (1..4) on side 'L'/'R'."""
    sg = SIDE[side]
    k = i - 1
    sc = LEG_SCALE[k]
    hip = np.array([LEG_HIP_X[k], sg * LEG_HIP_Y[k]])
    fk = (0.0, 0.015 * np.sign(LEG_F[k][0])) + tuple(LEG_F[k])
    out = []
    for n, (r, z) in enumerate(LEG_RZ):
        rr = r * sc if n > 0 else 0.0
        xy = hip + np.array([fk[n], sg * rr])
        out.append((xy[0], xy[1], z if n < 4 else 0.0))
    return np.array(out)


def rot_from_x_z(ex, up=(0, 0, 1)):
    ex = _norm(np.asarray(ex, dtype=np.float64))
    up = np.asarray(up, dtype=np.float64)
    ez = _norm(up - np.dot(up, ex) * ex)
    ey = np.cross(ez, ex)
    return np.stack([ex, ey, ez], 1)   # columns = local axes in world


MAJOR = dict(
    J0=np.array([0.26, -0.37, 0.335]),
    J1=np.array([0.385, -0.515, 0.265]),
    J2=np.array([0.575, -0.665, 0.405]),
    ex=np.array([0.66, 0.68, 0.30]),
    scale=1.16,
    outward=-1.0,      # local y sign of the outer face (right claw: -y)
    roll=0.55,
)
MINOR = dict(
    J0=np.array([0.27, 0.36, 0.315]),
    J1=np.array([0.355, 0.30, 0.255]),
    J2=np.array([0.435, 0.225, 0.275]),
    ex=np.array([0.80, -0.55, 0.05]),
    scale=0.27,
    outward=+1.0,
    roll=0.45,
)

# claw local layout (metres, scale 1): wrist at local (0,0,WRIST_Z)
WRIST_Z = 0.10
HINGE = np.array([0.43, 0.0, 0.335])     # dactyl hinge
DACT_TIP = np.array([1.06, 0.0, 0.115])
THUMB_H = np.array([0.55, 0.0, 0.10])
THUMB_T = np.array([1.08, 0.0, 0.05])


def claw_frame(spec):
    roll = spec.get("roll", 0.0)      # tilt the palm so its broad outer face looks up and out (reads from the high camera)
    up = (0.0, -spec["outward"] * math.sin(roll), math.cos(roll))
    R = rot_from_x_z(spec["ex"], up)
    sc = spec["scale"]
    O = spec["J2"] - R @ (np.array([0.0, 0.0, WRIST_Z]) * sc)
    return R, O, sc


def claw_pt(spec, local):
    R, O, sc = claw_frame(spec)
    return O + R @ (np.asarray(local, dtype=np.float64) * sc)


def bone_specs():
    """name -> dict(head, tail, parent, hinge) in Blender armature space.

    hinge: axis (world) about which the bone's natural flex happens; it becomes
    the local X axis of the bone.
    """
    B = {}

    def add(name, head, tail, parent, hinge):
        B[name] = dict(head=np.asarray(head, dtype=np.float64), tail=np.asarray(tail, dtype=np.float64),
                       parent=parent, hinge=np.asarray(hinge, dtype=np.float64))

    add("root", (0, 0, 0), (0, 0, 0.12), None, (1, 0, 0))
    add("body", (0, 0, ZC), (0.22, 0, ZC), "root", (0, 1, 0))
    # eyes
    for sd in "LR":
        sg = SIDE[sd]
        b, m, t = eye_points(sd)
        add(f"eye_{sd}_1", b, m, "body", (1, 0, 0))
        add(f"eye_{sd}_2", m, t, f"eye_{sd}_1", (1, 0, 0))
    # legs
    for sd in "LR":
        sg = SIDE[sd]
        for i in range(1, 5):
            J = leg_joints(sd, i)
            hip_dir = J[4] - J[0]
            n = np.cross([0, 0, 1.0], [hip_dir[0], hip_dir[1], 0.0])
            n = _norm(n) if np.linalg.norm(n) > 1e-6 else np.array([1.0, 0, 0])
            for k in range(4):
                par = "body" if k == 0 else f"leg_{sd}{i}_{k}"
                add(f"leg_{sd}{i}_{k + 1}", J[k], J[k + 1], par, n)
    # claws
    for nm, spec in (("major", MAJOR), ("minor", MINOR)):
        R, O, sc = claw_frame(spec)
        hinge_y = R[:, 1]
        add(f"claw_{nm}_1", spec["J0"], spec["J1"], "body", hinge_y)
        add(f"claw_{nm}_2", spec["J1"], spec["J2"], f"claw_{nm}_1", hinge_y)
        F0 = claw_pt(spec, HINGE)
        add(f"claw_{nm}_3", spec["J2"], F0, f"claw_{nm}_2", hinge_y)
        add(f"claw_{nm}_finger", F0, claw_pt(spec, DACT_TIP), f"claw_{nm}_3", hinge_y)
        if nm == "major":
            add("claw_major_thumb", claw_pt(spec, THUMB_H), claw_pt(spec, THUMB_T), "claw_major_3", hinge_y)
    # mouth
    for sd in "LR":
        h, t = mouth_points(sd)
        add(f"mouth_{sd}", h, t, "body", (0, 0, 1))
    return B


def eye_points(side):
    sg = SIDE[side]
    base = np.array([0.305, sg * 0.335, 0.435])
    mid = np.array([0.315, sg * 0.362, 0.585])
    tip = np.array([0.325, sg * 0.392, 0.735])
    return base, mid, tip


def mouth_points(side):
    sg = SIDE[side]
    return np.array([0.335, sg * 0.055, 0.265]), np.array([0.455, sg * 0.105, 0.185])


# ------------------------------------------------------------ builders -----


def build_carapace(q=1.0):
    nx, ny, nz = int(34 * q), int(44 * q), int(12 * q)
    N = (nx, ny, nz)
    key2idx = {}
    coords = []

    def vid(ix, iy, iz):
        k = (ix, iy, iz)
        if k not in key2idx:
            key2idx[k] = len(coords)
            coords.append(k)
        return key2idx[k]

    F = []
    faceinfo = []   # (axis, side, ia, ib)
    for k in range(3):
        a_ax, b_ax = (k + 1) % 3, (k + 2) % 3
        for side in (0, 1):
            fixed = N[k] if side else 0
            for ia in range(N[a_ax]):
                for ib in range(N[b_ax]):
                    cs = []
                    for (da, db) in ((0, 0), (1, 0), (1, 1), (0, 1)):
                        c = [0, 0, 0]
                        c[k] = fixed
                        c[a_ax] = ia + da
                        c[b_ax] = ib + db
                        cs.append(vid(*c))
                    if side == 0:
                        cs = cs[::-1]
                    F.append(tuple(cs))
                    faceinfo.append((k, side, ia, ib))
    C = np.array(coords, dtype=np.float64)
    p = C / np.array(N) * 2 - 1
    V = carapace_shape(p)
    part = Part("carapace")
    part.V = V
    part.F = F
    part.sub = 3
    part.uvw = 1.15
    # UVs: developed grid per cube face
    UV = []
    isl = []
    face_grid = {}
    for k in range(3):
        a_ax, b_ax = (k + 1) % 3, (k + 2) % 3
        for side in (0, 1):
            Na, Nb = N[a_ax], N[b_ax]
            G = np.zeros((Na + 1, Nb + 1, 3))
            for ia in range(Na + 1):
                for ib in range(Nb + 1):
                    c = [0, 0, 0]
                    c[k] = N[k] if side else 0
                    c[a_ax] = ia
                    c[b_ax] = ib
                    G[ia, ib] = V[key2idx[tuple(c)]]
            du = np.linalg.norm(np.diff(G, axis=0), axis=2)   # (Na, Nb+1)
            dv = np.linalg.norm(np.diff(G, axis=1), axis=2)   # (Na+1, Nb)
            u = np.vstack([np.zeros((1, Nb + 1)), np.cumsum(du, axis=0)])
            v = np.hstack([np.zeros((Na + 1, 1)), np.cumsum(dv, axis=1)])
            face_grid[(k, side)] = (u, v)
    isl_id = {(k, s): k * 2 + s for k in range(3) for s in (0, 1)}
    for f, (k, side, ia, ib) in zip(F, faceinfo):
        u, v = face_grid[(k, side)]
        corners = [(ia, ib), (ia + 1, ib), (ia + 1, ib + 1), (ia, ib + 1)]
        uv = [(u[c], v[c]) for c in corners]
        if side == 0:
            uv = uv[::-1]
        UV.append(uv)
        isl.append(isl_id[(k, side)])
    part.UV = UV
    part.isl = isl
    part.sp = np.zeros(len(V))
    part.cs = np.zeros((len(V), 2))
    part.fix_winding()
    return part


def carapace_shape(p):
    """Map cube-surface lattice points (in [-1,1]) to the carapace shell."""
    r = np.array([0.42, 0.38, 0.60])
    inner = 1 - r
    c = np.clip(p, -inner, inner)
    d = (p - c) / r
    dn = d / np.maximum(np.linalg.norm(d, axis=1, keepdims=True), 1e-9)
    q = c + r * dn
    qx, qy, qz = q[:, 0], q[:, 1], q[:, 2]
    x = qx * 0.39 + 0.005
    # rounded trapezoid: widest at the anterolateral corners, convex flanks, narrower rear
    wf = smoothstep(-1.0, 0.35, qx)
    B = 0.335 + 0.165 * wf
    B = B - 0.030 * smoothstep(0.55, 1.0, qx) + 0.018 * np.exp(-((qx + 0.15) / 0.5) ** 2)
    y = qy * B
    top = qz > 0
    dome = 1 + 0.40 * (1 - qx ** 2) * (1 - (qy ** 2)) + 0.10 * np.exp(-((qx - 0.05) ** 2 + qy ** 2) / 0.08)
    Ht = 0.158 * dome
    Hb = 0.130
    z = ZC + np.where(top, qz * Ht, qz * Hb)
    up = np.clip(qz, 0.0, 1.0) ** 1.4          # zero at the equator so the shell never folds
    z = z - 0.050 * smoothstep(0.55, 1.0, qx) * up - 0.02 * smoothstep(0.5, 1.0, -qx) * up
    z = z + np.where(top, 0.0, 0.02 * (1 - qx ** 2) * (1 - qy ** 2) * (-qz))
    return np.stack([x, y, z], 1)


def build_leg(side, i, q=1.0):
    J = leg_joints(side, i)
    sg = SIDE[side]
    sc = LEG_SCALE[i - 1]
    # joint arc-length positions
    seg = np.linalg.norm(np.diff(J, axis=0), axis=1)
    js = np.r_[0, np.cumsum(seg)]

    def radius(s, L):
        # a: fore-aft width (U), b: thickness across the bend plane (W)
        a = knots(s, [(js[0], 0.070), (js[1], 0.078), (js[1] + 0.06, 0.088), (js[2] - 0.08, 0.082), (js[2], 0.070),
                      (js[3], 0.060), (js[3] + 0.05, 0.054), (js[4] - 0.16, 0.048), (js[4] - 0.07, 0.036),
                      (js[4] - 0.02, 0.014), (js[4], 0.003)], 2)
        b = a * knots(s, [(js[0], 0.85), (js[2], 0.72), (js[4], 0.7)], 1)
        kn = bump(s, js[2], 0.035, 0.024) + bump(s, js[3], 0.03, 0.014) + bump(s, js[1], 0.025, 0.014)
        return a + kn, b + kn * 0.9

    # ref: horizontal fore-aft direction so U is the flat fore-aft width
    part = make_tube(f"leg_{side}{i}", J, 0.031 / q, radius, 12, ref=(1, 0, 0), round0=0.04, round1=0.0,
                     smooth=6, phase=0.0)
    part.sub = 2
    part.uvw = 0.9
    part.info["js"] = js
    part.info["J"] = J
    part.info["kind"] = "leg"
    return part


def build_eye(side, q=1.0):
    b, m, t = eye_points(side)
    sg = SIDE[side]
    path = np.array([b - (t - b) * 0.12, b, m, t + (t - b) * 0.10])
    L = np.linalg.norm(np.diff(path, axis=0), axis=1).sum()

    def radius(s, LL):
        u = s / LL
        r = knots(u, [(0, 0.062), (0.10, 0.064), (0.17, 0.042), (0.30, 0.034), (0.68, 0.031),
                      (0.77, 0.046), (0.84, 0.076), (0.92, 0.088), (1.0, 0.05)], 2)
        return r, r * 0.97

    part = make_tube(f"eye_{side}", path, 0.021 / q, radius, 12, ref=(1, 0, 0), round0=0.05, round1=0.06, smooth=3)
    part.sub = 2
    part.uvw = 1.6
    part.info["kind"] = "eye"
    part.info["L"] = L
    cum = np.r_[0, np.cumsum(np.linalg.norm(np.diff(path, axis=0), axis=1))]
    part.info["js"] = np.array([cum[1], cum[2]])      # stalk base, stalk middle (arc length)
    return part


def build_mouth(side, q=1.0):
    sg = SIDE[side]
    h, t = mouth_points(side)
    mid = (h + t) / 2 + np.array([0.02, sg * 0.0, -0.015])
    path = np.array([h - (t - h) * 0.15, h, mid, t])

    def radius(s, L):
        u = s / L
        a = knots(u, [(0, 0.030), (0.3, 0.048), (0.7, 0.052), (1.0, 0.03)], 2)
        b = knots(u, [(0, 0.014), (0.3, 0.017), (0.7, 0.014), (1.0, 0.008)], 2)
        return a, b

    # U (wide axis) lateral-ish, W thin
    part = make_tube(f"mouth_{side}", path, 0.03 / q, radius, 10, ref=(0, sg, 0.3), round0=0.03, round1=0.04, smooth=2)
    part.sub = 2
    part.uvw = 0.8
    part.info["kind"] = "mouth"
    part.info["side"] = side
    return part


def tooth_wave(x, period, amp, start, end, sharp=1.0):
    """Sawtooth-like teeth (rounded), zero outside [start,end]."""
    ph = ((x - start) / period) % 1.0
    tri = np.where(ph < 0.7, ph / 0.7, (1 - ph) / 0.3)      # asymmetric saw
    tri = tri ** sharp
    env = smoothstep(start, start + period * 0.8, x) * (1 - smoothstep(end - period * 1.0, end, x))
    return amp * tri * env


def build_claw(nm, spec, q=1.0):
    """Returns (arm, hand, dactyl) parts."""
    R, O, sc = claw_frame(spec)
    out_sign = spec["outward"]
    major = nm == "major"
    dens = 1.0 if major else 0.6     # ring-count density of the local-space tubes

    def tw(pts):
        return O + (np.asarray(pts) * sc) @ R.T

    # ---- arm (bones 1,2): shoulder -> elbow -> wrist
    J0, J1, J2 = spec["J0"], spec["J1"], spec["J2"]
    outv = R[:, 1] * out_sign
    mid = (J1 + J2) / 2 + outv * (0.03 if major else 0.008)
    path = np.array([J0 - (J1 - J0) * 0.25, J0, J1, mid, J2])
    base = 0.125 if major else 0.024

    def arm_radius(s, L):
        u = s / L
        r = knots(u, [(0, 0.9), (0.15, 1.0), (0.3, 1.25), (0.45, 1.1), (0.7, 1.15), (0.9, 1.0), (1.0, 0.8)], 2) * base
        r = r * (1 + bump(u, 0.33, 0.04, 0.25))
        return r * 1.05, r * 0.92

    arm = make_tube(f"claw_{nm}_arm", path, (0.030 if major else 0.011) / q, arm_radius, 14 if major else 9,
                    ref=R[:, 2], round0=0.05 if major else 0.015, round1=0.0, smooth=2)
    arm.sub = 2 if major else 1
    arm.info["kind"] = "arm"
    arm.info["spec"] = spec
    arm.uvw = 1.15 if major else 1.4
    arm.info["J"] = np.array([J0, J1, J2])
    cum = np.r_[0, np.cumsum(np.linalg.norm(np.diff(path, axis=0), axis=1))]
    arm.info["js"] = np.array([cum[1], cum[2], cum[4]])   # J0, J1, J2 arc length

    # ---- hand (palm + fixed finger): straight tube along local x, centre lifted afterwards
    Lh = 1.08
    top_k = [(0.0, 0.200), (0.05, 0.34), (0.16, 0.465), (0.32, 0.505), (0.46, 0.48), (0.56, 0.34), (0.64, 0.27),
             (0.80, 0.20), (0.95, 0.135), (1.08, 0.05)]
    bot_k = [(0.0, 0.03), (0.04, 0.0), (0.55, 0.0), (0.72, 0.006), (0.90, 0.030), (1.02, 0.045), (1.08, 0.035)]
    thk_k = [(0.0, 0.24), (0.10, 0.33), (0.26, 0.43), (0.42, 0.43), (0.54, 0.31), (0.68, 0.20), (0.90, 0.13), (1.08, 0.05)]

    def hand_geom(s):
        top = knots(s, top_k, 2)
        bot = knots(s, bot_k, 2)
        thk = knots(s, thk_k, 2)
        top = top + tooth_wave(s, 0.085, 0.020, 0.60, 1.03, 1.2)
        return top, bot, thk

    def hand_radius(s, L):
        top, bot, thk = hand_geom(s)
        return (top - bot) / 2, thk / 2

    w_out = -out_sign      # W axis = -local y, so +W is local -y

    def hand_profile(theta, s, ctx):
        c = np.cos(theta)[None, :] * np.ones((len(s), 1))
        sn = np.sin(theta)[None, :] * np.ones((len(s), 1))
        e = 2.0 / 3.2
        fx = np.sign(c) * np.abs(c) ** e
        fy = np.sign(sn) * np.abs(sn) ** e
        k = np.where(np.sign(fy) * w_out >= 0, 1.0, 0.62)
        return fx, fy * k

    nh = int(Lh / (0.024 / q / dens)) + 1
    hpath = np.stack([np.linspace(0, Lh, nh), np.zeros(nh), np.zeros(nh)], 1)
    hand = make_tube(f"claw_{nm}_hand", hpath, 0.024, hand_radius, 18 if major else 12,
                     ref=(0, 0, 1), profile_fn=hand_profile, round0=0.07, round1=0.055, smooth=0, nrings=nh)
    xl = hand.V[:, 0]
    tt, bb, _ = hand_geom(np.clip(xl, 0, Lh))
    hand.V[:, 2] += (tt + bb) / 2
    hand.V = tw(hand.V)
    hand.info["kind"] = "hand"
    hand.info["spec"] = spec
    hand.sub = 3 if major else 2
    hand.uvw = 1.3 if major else 1.6
    hand.UV = [[(u * sc, v * sc) for (u, v) in f] for f in hand.UV]
    hand.fix_winding()

    # ---- dactyl (moving finger)
    dp = np.array([[0.36, 0, 0.32], [0.46, 0, 0.365], [0.60, 0, 0.375], [0.80, 0, 0.305], [0.96, 0, 0.205], [1.075, 0, 0.115]])

    def dact_radius(s, L):
        u = s / L
        a = knots(u, [(0, 0.075), (0.2, 0.082), (0.5, 0.062), (0.8, 0.036), (1.0, 0.012)], 2)
        b = knots(u, [(0, 0.115), (0.2, 0.125), (0.5, 0.085), (0.8, 0.05), (1.0, 0.018)], 2)
        return a, b

    def dact_profile(theta, s, ctx):
        c = np.cos(theta)[None, :] * np.ones((len(s), 1))
        sn = np.sin(theta)[None, :] * np.ones((len(s), 1))
        e = 2.0 / 2.6
        fx = np.sign(c) * np.abs(c) ** e
        fy = np.sign(sn) * np.abs(sn) ** e
        low = np.clip(-fx, 0, 1) ** 2
        tooth = tooth_wave(s, 0.08, 0.30, 0.20, ctx["L"] - 0.03, 1.2)[:, None]
        fx = fx - low * tooth
        k = np.where(np.sign(fy) * w_out >= 0, 1.0, 0.66)
        return fx, fy * k

    dact = make_tube(f"claw_{nm}_dactyl", dp, 0.024 / dens, dact_radius, 14 if major else 10,
                     ref=(0, 0, 1), profile_fn=dact_profile, round0=0.05, round1=0.05, smooth=1)
    dact.V = tw(dact.V)
    dact.info["kind"] = "dactyl"
    dact.info["spec"] = spec
    dact.sub = 3 if major else 2
    dact.uvw = 1.3 if major else 1.6
    dact.UV = [[(u * sc, v * sc) for (u, v) in f] for f in dact.UV]
    dact.fix_winding()
    return arm, hand, dact


QUALITY = 1.13     # ring/segment density; tuned so the game mesh lands near 25k triangles


def build_all(q=None):
    q = QUALITY if q is None else q
    parts = [build_carapace(q)]
    for sd in "LR":
        parts.append(build_eye(sd, q))
    arm, hand, dact = build_claw("major", MAJOR, q)
    parts += [arm, hand, dact]
    arm, hand, dact = build_claw("minor", MINOR, q)
    parts += [arm, hand, dact]
    for sd in "LR":
        for i in range(1, 5):
            parts.append(build_leg(sd, i, q))
    for sd in "LR":
        parts.append(build_mouth(sd, q))
    for i, p in enumerate(parts):
        p.pid = i + 1
    return parts


def tri_count(parts):
    return sum(len(f) - 2 for p in parts for f in p.F)


# ------------------------------------------------------------------- png ---
def write_png(path, arr):
    """Write (H,W,3|4) uint8 as PNG, first row = top. Pure zlib, no Blender colour management."""
    import zlib
    import struct
    arr = np.ascontiguousarray(arr)
    h, w, c = arr.shape
    raw = np.hstack([np.zeros((h, 1), np.uint8), arr.reshape(h, w * c)]).tobytes()

    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)

    png = (b"\x89PNG\r\n\x1a\n"
           + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2 if c == 3 else 6, 0, 0, 0))
           + chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))
    with open(path, "wb") as f:
        f.write(png)
