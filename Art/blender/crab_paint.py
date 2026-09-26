"""Texture-space hand painting for the fiddler crab (pure numpy).

Input: data maps baked from the game mesh (object position, normal, part id,
arc/around params, displacement height, AO). Every texel is painted from its 3D
surface position, so colour is continuous across UV seams and independent of
UV stretch.
"""
import numpy as np
import crab_lib as L
from crab_lib import fbm, cellular, smoothstep

f32 = np.float32


def S(a, b, x):
    return smoothstep(a, b, x).astype(f32)


def col(r, g, b):
    return np.array([r, g, b], dtype=f32)


def mixc(a, b, t):
    a = np.asarray(a, dtype=f32)
    b = np.asarray(b, dtype=f32)
    t = np.asarray(t, dtype=f32)
    if a.ndim == 1:
        a = a[None, :]
    return a + (b - a) * t[:, None]


def dots(P, scale, seed, thr, cidmin=0.4, soft=0.25, var=0.8):
    F1, cid = cellular(P / scale, seed)
    r = thr * (1 - var * 0.5 + var * cid)
    m = 1 - S(r * (1 - soft), r * (1 + soft), F1)
    return (m * (cid > cidmin)).astype(f32)


def norm01(x):
    return x * 0.5 + 0.5


class Ctx:
    """Per-group texel subset."""

    def __init__(self, P, N, PD, h, ao):
        self.P, self.N, self.PD, self.h, self.ao = P, N, PD, h, ao
        self.s = PD[:, 0]
        self.cth = PD[:, 1]
        self.sth = PD[:, 2]


TAN_L, TAN_M, TAN_D = col(.84, .71, .49), col(.69, .55, .36), col(.46, .34, .22)
TQ_D, TQ_M, TQ_L = col(.03, .40, .47), col(.06, .60, .64), col(.34, .84, .82)
RED_D, RED, ORG, IVORY = col(.58, .09, .04), col(.88, .24, .06), col(.99, .52, .14), col(.97, .93, .80)
BROWN, ORANGE, DARK, CREAM = col(.48, .27, .10), col(.91, .53, .14), col(.22, .12, .06), col(.88, .80, .64)


def paint_carapace(c):
    P, N, h, ao = c.P, c.N, c.h, c.ao
    X, Y, Z = P[:, 0], P[:, 1], P[:, 2]
    top = S(0.25, 0.75, N[:, 2])
    under = S(0.15, 0.55, -N[:, 2])
    side = np.clip(1 - top - under, 0, 1)
    m1 = fbm(P / 0.24, 3, seed=1) * 0.5 + 0.5
    m2 = fbm(P / 0.07, 4, seed=2) * 0.5 + 0.5
    m3 = fbm(P / 0.13, 3, seed=4) * 0.5 + 0.5
    SAND, OCHRE, UMBER, OLIVE = col(.86, .72, .47), col(.76, .46, .20), col(.42, .27, .14), col(.56, .50, .28)
    col_ = mixc(SAND, TAN_L, S(0.40, 0.85, m1))
    col_ = mixc(col_, OCHRE, S(0.42, 0.80, m3) * 0.95)
    col_ = mixc(col_, OLIVE, S(0.55, 0.90, m2) * 0.65)
    col_ = mixc(col_, UMBER, S(0.58, 0.92, fbm(P / 0.05, 3, seed=6) * 0.5 + 0.5) * 0.70)
    # broad dark blotches and a pale cardiac patch: readable from far away
    marb = fbm(P / 0.16, 3, seed=8) * 0.5 + 0.5
    col_ = mixc(col_, col(.52, .33, .17), S(0.52, 0.82, marb) * 0.65 * (1 - under))
    cardiac = np.exp(-(((X + 0.10) / 0.16) ** 2 + (Y / 0.16) ** 2)).astype(f32)
    col_ = mixc(col_, col(.95, .86, .62), cardiac * 0.55 * top)
    col_ = mixc(col_, col(.80, .52, .28), side * 0.4)
    rim = np.exp(-((N[:, 2] - 0.62) / 0.16) ** 2).astype(f32)
    col_ = mixc(col_, col(.96, .88, .66), rim * 0.45)
    edge = np.exp(-((N[:, 2] - 0.15) / 0.20) ** 2).astype(f32)
    col_ = mixc(col_, col(.40, .25, .13), edge * 0.45 * (1 - under))
    col_ = mixc(col_, TAN_L * 1.08, S(0.06, 0.34, h) * 0.5)
    groove = S(-0.06, -0.40, h)
    col_ = mixc(col_, UMBER * 0.70, groove * 0.62)
    col_ = mixc(col_, col(.30, .20, .12), dots(P, 0.030, 101, 0.19, 0.55) * 0.62)
    col_ = mixc(col_, col(.95, .88, .70), dots(P, 0.040, 103, 0.22, 0.55) * 0.55)
    col_ = mixc(col_, col(.40, .27, .16), dots(P, 0.012, 107, 0.28, 0.6) * 0.35)
    # turquoise front patch
    wob = 0.035 * fbm(P / 0.075, 3, seed=9)
    xb = 0.10 + 0.20 * (Y / 0.42) ** 2
    inside = S(-0.010, 0.030, (X - xb) + wob) * S(-0.02, 0.05, 0.42 - np.abs(Y) + wob * 0.7) * (1 - under)
    tq = mixc(TQ_M, TQ_L, S(0.25, 0.9, fbm(P / 0.07, 3, seed=12) * 0.5 + 0.5))
    tq = mixc(tq, TQ_D, S(0.05, -0.35, h) * 0.6 + S(0.6, 1.0, fbm(P / 0.04, 2, seed=14) * 0.5 + 0.5) * 0.30)
    tq = mixc(tq, col(.04, .28, .34), dots(P, 0.026, 111, 0.22, 0.55) * 0.55)
    tq = mixc(tq, col(.72, .95, .93), dots(P, 0.036, 113, 0.20, 0.65) * 0.5)
    col_ = mixc(col_, tq, inside * 0.97)
    fr = dots(P, 0.034, 121, 0.20, 0.6) * S(0.05, 0.25, (X - xb + 0.12)) * (1 - inside) * (1 - under)
    col_ = mixc(col_, TQ_M, fr * 0.55)
    col_ = mixc(col_, CREAM * (0.92 + 0.08 * m2[:, None]), under * 0.95)
    # soft socket shadows where limbs enter the shell (AO for the shell ignores the limbs, see build_crab.stage_bake)
    sock = np.zeros(len(P), dtype=np.float64)
    for sd in "LR":
        for i in range(1, 5):
            j0 = L.leg_joints(sd, i)[0]
            sock += np.exp(-np.sum((P - j0) ** 2, axis=1) / (2 * 0.075 ** 2))
    for spec in (L.MAJOR, L.MINOR):
        sock += np.exp(-np.sum((P - spec["J1"]) ** 2, axis=1) / (2 * 0.075 ** 2))
    for sd in "LR":
        sock += 0.7 * np.exp(-np.sum((P - L.eye_points(sd)[0]) ** 2, axis=1) / (2 * 0.06 ** 2))
    ao = ao * (1 - 0.30 * np.clip(sock, 0, 1)).astype(f32)
    c.ao = ao
    col_ = col_ * (0.70 + 0.30 * ao)[:, None]
    rough = 0.55 - 0.16 * inside + 0.22 * S(-0.1, -0.5, h) - 0.06 * S(0.1, 0.4, h) + 0.08 * under
    return col_, rough


def claw_common(c, spec, part_kind, L_part=None, minor=False):
    Pl, R, sc = _local(c.P, spec)
    Nl = c.N @ R
    out = Nl[:, 1] * spec["outward"]
    return Pl, Nl, out, sc


def _local(P, spec):
    R, O, sc = L.claw_frame(spec)
    return ((P - O) @ R) / sc, R, sc


def paint_hand(c, spec, minor=False):
    P, h, ao = c.P, c.h, c.ao
    Pl, Nl, out, sc = claw_common(c, spec, "hand")
    x, z = Pl[:, 0], Pl[:, 2]
    cth = c.cth
    if minor:
        red, red_d, org = col(.90, .55, .24), col(.66, .32, .14), col(.99, .72, .38)
    else:
        red, red_d, org = RED, RED_D, ORG
    n1 = fbm(Pl / 0.20, 3, seed=201) * 0.5 + 0.5
    col_ = mixc(red, org, S(0.35, 0.85, n1) * 0.65)
    col_ = mixc(col_, red_d, S(0.22, 0.52, z) * 0.5 * (1 - S(0.5, 0.64, x)))
    col_ = mixc(col_, red_d, (1 - S(0.0, 0.14, x)) * 0.75)
    col_ = mixc(col_, col(1.0, .68, .30), S(0.08, 0.30, h) * 0.6)
    col_ = mixc(col_, red_d * 0.85, S(-0.02, -0.18, h) * 0.55)
    inner = S(0.05, 0.55, -out)
    col_ = mixc(col_, col(.96, .68, .46), inner * 0.6)
    col_ = mixc(col_, col(.99, .78, .36), dots(Pl, 0.045, 211, 0.24, 0.55) * 0.22 * (1 - inner))
    col_ = mixc(col_, red_d * 0.7, dots(Pl, 0.030, 213, 0.26, 0.5) * 0.35)
    tip = S(0.83, 0.96, x)
    col_ = mixc(col_, IVORY, tip)
    edge = S(0.55, 0.9, cth) * S(0.58, 0.68, x) * (0.55 + 0.45 * S(-0.02, 0.2, h))
    col_ = mixc(col_, IVORY * 0.97, edge * 0.8)
    col_ = col_ * (0.72 + 0.28 * ao)[:, None]
    rough = 0.40 - 0.05 * S(0.1, 0.3, h) + 0.15 * S(-0.05, -0.3, h) - 0.06 * tip
    return col_, rough


def paint_dactyl(c, spec, part, minor=False):
    P, h, ao = c.P, c.h, c.ao
    Pl, Nl, out, sc = claw_common(c, spec, "dactyl")
    s = c.s / part.info["L"]
    cth = c.cth
    if minor:
        red, red_d, org = col(.90, .55, .24), col(.66, .32, .14), col(.99, .72, .38)
    else:
        red, red_d, org = RED, RED_D, ORG
    n1 = fbm(Pl / 0.20, 3, seed=221) * 0.5 + 0.5
    col_ = mixc(red, org, S(0.35, 0.85, n1) * 0.65)
    col_ = mixc(col_, red_d, (1 - S(0.0, 0.25, s)) * 0.55)
    col_ = mixc(col_, col(1.0, .68, .30), S(0.08, 0.30, h) * 0.6)
    col_ = mixc(col_, red_d * 0.85, S(-0.02, -0.18, h) * 0.55)
    inner = S(0.05, 0.55, -out)
    col_ = mixc(col_, col(.96, .68, .46), inner * 0.6)
    col_ = mixc(col_, red_d * 0.7, dots(Pl, 0.028, 223, 0.30, 0.4) * 0.45)
    tip = S(0.66, 0.84, s)
    col_ = mixc(col_, IVORY, tip)
    edge = S(0.55, 0.9, -cth) * S(0.18, 0.28, s) * (0.55 + 0.45 * S(-0.02, 0.2, h))
    col_ = mixc(col_, IVORY * 0.97, edge * 0.8)
    col_ = col_ * (0.72 + 0.28 * ao)[:, None]
    rough = 0.40 - 0.05 * S(0.1, 0.3, h) + 0.15 * S(-0.05, -0.3, h) - 0.06 * tip
    return col_, rough


def paint_arm(c, spec, part, minor=False):
    P, h, ao = c.P, c.h, c.ao
    u = c.s / part.info["L"]
    if minor:
        base, dk = col(.92, .60, .30), col(.62, .34, .16)
    else:
        base, dk = col(.93, .40, .11), col(.60, .13, .06)
    n1 = fbm(P / 0.12, 3, seed=231) * 0.5 + 0.5
    col_ = mixc(base, ORG, S(0.4, 0.9, n1) * 0.5)
    col_ = mixc(col_, dk, S(-0.03, -0.2, h) * 0.6 + bumpu(u, 0.35, 0.03) * 0.5)
    col_ = mixc(col_, col(1.0, .68, .30), S(0.08, 0.3, h) * 0.5)
    col_ = mixc(col_, TAN_M, S(0.20, 0.06, u) * 0.85)
    col_ = mixc(col_, dk * 0.9, dots(P, 0.03, 233, 0.3, 0.4) * 0.4)
    col_ = col_ * (0.72 + 0.28 * ao)[:, None]
    rough = 0.42 + 0.14 * S(-0.05, -0.3, h)
    return col_, rough


def bumpu(u, c, w):
    return np.exp(-0.5 * ((u - c) / w) ** 2).astype(f32)


def paint_leg(c, part):
    P, N, h, ao = c.P, c.N, c.h, c.ao
    js = part.info["js"]
    s = c.s
    ph = (s - js[1]) / 0.135
    w = S(-0.55, 0.55, np.sin(L.TAU * ph + 0.6 * fbm(P / 0.1, 2, seed=299)))
    n1 = fbm(P / 0.08, 3, seed=301) * 0.5 + 0.5
    col_ = mixc(BROWN * 1.08, ORANGE, w)
    col_ = mixc(col_, col_ * 1.10, S(0.5, 0.9, n1) * 0.6)
    col_ = mixc(col_, CREAM * 0.9, S(js[1] + 0.05, js[1] - 0.03, s) * 0.9)          # coxa near the body
    for k in (1, 2, 3):
        col_ = mixc(col_, DARK, bumpu(s, js[k], 0.012) * 0.55)
    col_ = mixc(col_, col(.62, .38, .18), S(js[2] - 0.02, js[2] + 0.03, s) * S(js[3] + 0.03, js[3] - 0.03, s) * 0.5)   # carpus
    col_ = mixc(col_, DARK, S(js[4] - 0.11, js[4] - 0.035, s) * 0.92)                                     # dark foot tip
    under = S(0.05, 0.55, -N[:, 2])
    col_ = mixc(col_, CREAM * 0.85, under * 0.55)
    col_ = mixc(col_, col(1.0, .72, .34), S(0.08, 0.30, h) * 0.5)
    col_ = mixc(col_, DARK * 0.8, S(-0.04, -0.2, h) * 0.5)
    col_ = mixc(col_, col(.20, .11, .06), dots(P, 0.026, 311, 0.24, 0.5) * 0.40)
    col_ = mixc(col_, col(.98, .84, .56), dots(P, 0.034, 313, 0.20, 0.6) * 0.22)
    col_ = col_ * (0.70 + 0.30 * ao)[:, None]
    rough = 0.50 + 0.15 * S(-0.05, -0.3, h) - 0.08 * w
    return col_, rough


def paint_eye(c, part):
    P, N, h, ao = c.P, c.N, c.h, c.ao
    u = c.s / part.info["L"]
    cth, sth = c.cth, c.sth
    stalk = mixc(col(.83, .73, .53), col(.70, .55, .36), S(0.4, 0.9, fbm(P / 0.05, 3, seed=401) * 0.5 + 0.5))
    stalk = mixc(stalk, col(.5, .36, .2), S(0.0, 1.0, np.sin(L.TAU * u * 7) * 0.5 + 0.5) * 0.25 * S(0.2, 0.3, u))
    stalk = mixc(stalk, TQ_M, S(0.14, 0.05, u) * 0.65)
    stalk = mixc(stalk, col(.35, .24, .14), S(0.13, 0.18, u) * S(0.23, 0.17, u) * 0.6)
    bulb = S(0.785, 0.82, u)
    col_ = mixc(stalk, col(.045, .035, .03), bulb)
    ring = bumpu(u, 0.815, 0.010) * bulb
    col_ = mixc(col_, TQ_L, ring * 0.9)
    gold = bumpu(u, 0.96, 0.02) * S(0.9, 0.95, u)
    col_ = mixc(col_, col(.55, .42, .12), gold * 0.5)
    hl = np.exp(-(((u - 0.915) / 0.022) ** 2 + (sth / 0.38) ** 2)) * (cth > 0)
    col_ = mixc(col_, col(.98, .98, .95), hl.astype(f32) * bulb)
    col_ = col_ * (0.75 + 0.25 * ao)[:, None]
    rough = np.where(bulb > 0.5, 0.12, 0.55).astype(f32)
    return col_, rough


def paint_mouth(c, part):
    P, h, ao = c.P, c.h, c.ao
    th = np.arctan2(c.sth, c.cth)
    col_ = mixc(col(.87, .79, .63), col(.76, .62, .44), S(0.4, 0.9, fbm(P / 0.03, 3, seed=501) * 0.5 + 0.5) * 0.7)
    col_ = mixc(col_, col(.45, .30, .18), S(0.55, 0.95, np.abs(c.sth)) * 0.7)
    col_ = mixc(col_, col(.55, .40, .25), (np.sin(th * 9) * 0.5 + 0.5) * 0.25)
    col_ = col_ * (0.7 + 0.3 * ao)[:, None]
    return col_, np.full(len(P), 0.62, dtype=f32)


def dilate(img, valid, iters=10):
    img = img.copy()
    valid = valid.copy()
    for _ in range(iters):
        acc = np.zeros_like(img)
        cnt = np.zeros(valid.shape, dtype=np.float32)
        for dy, dx in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            v = np.roll(valid, (dy, dx), (0, 1))
            acc += np.roll(img, (dy, dx), (0, 1)) * v[..., None]
            cnt += v
        new = (~valid) & (cnt > 0)
        img[new] = acc[new] / cnt[new][:, None]
        valid = valid | new
    return img


def paint_all(maps, parts):
    n = maps["pid"].shape[0]
    pid = np.rint(maps["pid"][..., 0]).astype(np.int32).ravel()
    P = maps["pos"].reshape(-1, 3)
    N = maps["nrm"].reshape(-1, 3)
    PD = maps["pd"].reshape(-1, 3)
    h = maps["h"].ravel()
    ao = np.clip(maps["ao"].ravel(), 0, 1)
    color = np.zeros((n * n, 3), dtype=f32)
    rough = np.zeros(n * n, dtype=f32)
    by_pid = {p.pid: p for p in parts}

    def run(fn, pids, *args, **kw):
        idx = np.flatnonzero(np.isin(pid, pids))
        if len(idx) == 0:
            return
        ctx = Ctx(P[idx], N[idx], PD[idx], h[idx], ao[idx])
        cc, rr = fn(ctx, *args, **kw)
        color[idx] = cc
        rough[idx] = rr
        ao[idx] = ctx.ao

    for p in parts:
        k = p.info.get("kind", p.name)
        if p.name == "carapace":
            run(paint_carapace, [p.pid])
        elif k == "hand":
            run(paint_hand, [p.pid], p.info["spec"], minor=("minor" in p.name))
        elif k == "dactyl":
            run(paint_dactyl, [p.pid], p.info["spec"], p, minor=("minor" in p.name))
        elif k == "arm":
            run(paint_arm, [p.pid], p.info["spec"], p, minor=("minor" in p.name))
        elif k == "leg":
            run(paint_leg, [p.pid], p)
        elif k == "eye":
            run(paint_eye, [p.pid], p)
        elif k == "mouth":
            run(paint_mouth, [p.pid], p)
        print("   painted", p.name)
    valid = (pid > 0).reshape(n, n)
    color = np.clip(color, 0, 1).reshape(n, n, 3)
    fill = color[valid].mean(0)
    color[~valid] = fill
    color = dilate(color, valid, 12)
    rough = rough.reshape(n, n)
    rough[~valid] = 0.6
    aoi = ao.reshape(n, n).copy()
    aoi[~valid] = 1.0
    return color, np.clip(rough, 0.04, 1), aoi, valid
