"""High-poly surface detail (displacement) for the fiddler crab, pure numpy.

Every function returns a displacement height in metres along the vertex
normal. Detail: carapace grooves, rim, granulation; claw tubercles, keels and
teeth; leg joint grooves, ridges and spines.
"""
import numpy as np
import crab_lib as L
from crab_lib import fbm, cellular, smoothstep


def seg_dist(px, py, a, b):
    ax, ay = a
    bx, by = b
    dx, dy = bx - ax, by - ay
    t = np.clip(((px - ax) * dx + (py - ay) * dy) / (dx * dx + dy * dy), 0, 1)
    return np.hypot(px - (ax + t * dx), py - (ay + t * dy))


def poly_dist(px, py, pts):
    d = None
    for a, b in zip(pts[:-1], pts[1:]):
        s = seg_dist(px, py, a, b)
        d = s if d is None else np.minimum(d, s)
    return d


def groove(d, depth, width):
    return -depth * np.exp(-(d / width) ** 2)


def local_coords(P, spec):
    R, O, sc = L.claw_frame(spec)
    return ((P - O) @ R) / sc, R, sc


def disp_carapace(P, N):
    X, Y, Z = P[:, 0], P[:, 1], P[:, 2] - L.ZC
    az = N[:, 2]
    top = smoothstep(0.30, 0.80, az)
    side = 1 - top
    ay = np.abs(Y)
    h = np.zeros(len(P), np.float32)
    # cervical (transverse) groove, the H of the gastric region and the branchial grooves (all gently curved)
    d_c = np.abs(X - (0.05 - 0.55 * Y ** 2 + 0.9 * Y ** 4))
    g = groove(d_c, 0.0070, 0.0105) * (1 - smoothstep(0.30, 0.40, ay))
    for sg in (-1, 1):
        g += groove(poly_dist(X, Y, [(0.03, sg * 0.105), (0.12, sg * 0.128), (0.22, sg * 0.150), (0.32, sg * 0.185)]), 0.0058, 0.0090)
        g += groove(poly_dist(X, Y, [(-0.28, sg * 0.235), (-0.16, sg * 0.262), (-0.05, sg * 0.275), (0.03, sg * 0.30)]), 0.0050, 0.0085)
        g += groove(poly_dist(X, Y, [(0.24, sg * 0.30), (0.30, sg * 0.335), (0.34, sg * 0.385)]), 0.0038, 0.0065)   # front corner
        g += groove(poly_dist(X, Y, [(-0.30, sg * 0.10), (-0.22, sg * 0.14), (-0.14, sg * 0.15)]), 0.0030, 0.0070)  # cardiac flank
    g += groove(np.hypot(X + 0.13, Y / 1.3), 0.0045, 0.035)                                  # cardiac dimple
    h += g * top
    # raised rim where the shell turns down
    rim = np.exp(-((az - 0.55) / 0.15) ** 2)
    h += 0.0045 * rim
    # front margin lip
    h += 0.0035 * np.exp(-((X - 0.362) / 0.010) ** 2) * (1 - smoothstep(0.42, 0.49, ay)) * top
    # granulation
    F1, cid = cellular(P / 0.0145, seed=3)
    gran = np.clip(1 - (F1 / 0.5) ** 2, 0, 1) * (cid > 0.22)
    mask = np.clip(0.30 + side * 1.1 + smoothstep(0.16, 0.42, ay) * 0.8 + 0.4 * smoothstep(-0.36, -0.2, -X), 0, 1)
    h += 0.0015 * gran * mask
    F2, cid2 = cellular(P / 0.0055, seed=11)
    h += 0.0007 * np.clip(1 - (F2 / 0.55) ** 2, 0, 1) * (cid2 > 0.45)
    h += 0.0018 * fbm(P / 0.07, 3, seed=5) + 0.0004 * fbm(P / 0.010, 2, seed=8)
    return h


def disp_hand(P, N, spec, part):
    Pl, R, sc = local_coords(P, spec)
    Nl = N @ R
    x = Pl[:, 0]
    cs = part.cs_hi
    cth, sth = cs[:, 0], cs[:, 1]
    outer = Nl[:, 1] * spec["outward"]
    h = np.zeros(len(P), np.float32)
    pal = 1 - smoothstep(0.50, 0.68, x)
    # tubercles on the outer face of the palm, smaller on fingers
    F1, cid = cellular(Pl / 0.052, seed=21)
    tub = np.clip(1 - (F1 / 0.42) ** 2, 0, 1) * (cid > 0.15)
    om = smoothstep(-0.1, 0.5, outer)
    h += sc * 0.0030 * tub * (0.20 + 0.8 * om) * (0.35 + 0.65 * pal) * smoothstep(0.02, 0.14, x)
    F2, cid2 = cellular(Pl / 0.016, seed=23)
    h += sc * 0.0009 * np.clip(1 - (F2 / 0.5) ** 2, 0, 1) * (cid2 > 0.3)
    # keels: lower edge (continues into the fixed finger) and upper palm ridge
    bot = np.exp(-((1 + cth) / 0.05))
    h += sc * 0.0055 * bot * smoothstep(0.03, 0.15, x) * (1 - 0.6 * smoothstep(0.9, 1.06, x))
    upr = np.exp(-((1 - cth) / 0.04))
    h += sc * 0.0045 * upr * (1 - smoothstep(0.42, 0.56, x)) * smoothstep(0.04, 0.16, x)
    # fine serrations on the fixed finger's cutting edge (upper edge, x>0.6)
    ser = L.tooth_wave(x, 0.030, 0.0042 * 1.0, 0.62, 1.04, 1.5)
    h += sc * ser * smoothstep(0.55, 0.9, cth)
    # wrist wrinkles
    h += sc * 0.0025 * np.sin(x * 190) * (1 - smoothstep(0.0, 0.08, x)) * 0.5
    h += sc * 0.0012 * fbm(Pl / 0.07, 3, seed=27)
    return h


def disp_dactyl(P, N, spec, part):
    Pl, R, sc = local_coords(P, spec)
    Nl = N @ R
    cth = part.cs_hi[:, 0]
    s = part.sp_hi    # local arc length (path is built in claw-local units)
    outer = Nl[:, 1] * spec["outward"]
    h = np.zeros(len(P), np.float32)
    F1, cid = cellular(Pl / 0.048, seed=31)
    tub = np.clip(1 - (F1 / 0.4) ** 2, 0, 1) * (cid > 0.2)
    h += sc * 0.0026 * tub * (0.2 + 0.8 * smoothstep(-0.1, 0.5, outer)) * (1 - smoothstep(0.35, 0.8, s))
    h += sc * 0.0011 * fbm(Pl / 0.07, 3, seed=33)
    up = np.exp(-((1 - cth) / 0.05))
    h += sc * 0.0045 * up * (1 - smoothstep(0.5, 0.95, s))
    ser = L.tooth_wave(s, 0.030, 0.0040, 0.20, 0.95, 1.5)
    h += sc * ser * smoothstep(0.55, 0.9, -cth)
    return h


def disp_arm(P, N, spec, part):
    h = np.zeros(len(P), np.float32)
    sc = spec["scale"]
    F1, cid = cellular(P / (0.05 * sc + 0.01), seed=41)
    h += 0.0022 * max(sc, 0.4) * np.clip(1 - (F1 / 0.45) ** 2, 0, 1) * (cid > 0.2)
    h += 0.0020 * max(sc, 0.4) * fbm(P / (0.09 * sc + 0.02), 3, seed=43)
    up = np.exp(-((1 - part.cs_hi[:, 0]) / 0.06))
    h += 0.004 * max(sc, 0.4) * up
    return h


def disp_leg(P, N, part):
    js = part.info["js"]
    s = part.sp_hi
    cth, sth = part.cs_hi[:, 0], part.cs_hi[:, 1]
    th = np.arctan2(sth, cth)
    h = np.zeros(len(P), np.float32)
    # joint grooves
    for k in (1, 2, 3):
        h += groove(s - js[k], 0.0030, 0.008)
    # longitudinal ridges (rounded triangular section on the merus, keeled propodus)
    mer = smoothstep(js[1], js[1] + 0.06, s) * (1 - smoothstep(js[2] - 0.05, js[2], s))
    prop = smoothstep(js[3], js[3] + 0.05, s) * (1 - smoothstep(js[4] - 0.08, js[4] - 0.02, s))
    h += 0.0016 * np.cos(3 * th + 0.5) * mer
    h += 0.0014 * np.cos(2 * th) * prop
    # spine rows on the propodus edges
    edge = smoothstep(0.78, 0.96, np.abs(cth))
    h += L.tooth_wave(s, 0.052, 0.0026, js[3] + 0.03, js[4] - 0.08, 1.4) * edge
    # granulation
    F1, cid = cellular(P / 0.010, seed=51)
    h += 0.0012 * np.clip(1 - (F1 / 0.5) ** 2, 0, 1) * (cid > 0.25)
    h += 0.0012 * fbm(P / 0.05, 2, seed=53)
    return h


def disp_eye(P, N, part):
    s = part.sp_hi / part.info["L"]
    h = np.zeros(len(P), np.float32)
    h += 0.0007 * fbm(P / 0.02, 2, seed=61)
    h += 0.0025 * np.exp(-((s - 0.15) / 0.03) ** 2)                       # collar ring
    F1, cid = cellular(P / 0.0075, seed=63)
    h += 0.0004 * np.clip(1 - (F1 / 0.6) ** 2, 0, 1) * smoothstep(0.84, 0.90, s)   # cornea facets
    return h


def disp_mouth(P, N, part):
    s = part.sp_hi
    th = np.arctan2(part.cs_hi[:, 1], part.cs_hi[:, 0])
    h = 0.0009 * np.sin(th * 9) + 0.0008 * fbm(P / 0.02, 2, seed=71)
    return h.astype(np.float32)


def displace(part, P, N):
    k = part.info.get("kind", part.name)
    if part.name == "carapace":
        return disp_carapace(P, N)
    if k == "hand":
        return disp_hand(P, N, part.info["spec"], part)
    if k == "dactyl":
        return disp_dactyl(P, N, part.info["spec"], part)
    if k == "arm":
        return disp_arm(P, N, part.info["spec"], part)
    if k == "leg":
        return disp_leg(P, N, part)
    if k == "eye":
        return disp_eye(P, N, part)
    if k == "mouth":
        return disp_mouth(P, N, part)
    return np.zeros(len(P), np.float32)
