"""Procedural animation for the fiddler crab (pure numpy).

Legs are solved with a small damped least squares IK on the exact bone FK
(crab_skel.Skel), so planted feet stay on Z = 0 and the exported clips are
plain FK keys. Everything else (claws, eyes, mouth, body) is authored as
joint-angle curves with anticipation and overshoot.
"""
import math
import numpy as np
import crab_lib as L
import crab_skel as K

TAU = math.tau
FPS = 30


# ------------------------------------------------------------- easing ------
def sstep(a, b, x):
    t = np.clip((x - a) / (b - a), 0.0, 1.0)
    return t * t * (3 - 2 * t)


def ease_io(t):
    t = np.clip(t, 0.0, 1.0)
    return 0.5 - 0.5 * np.cos(np.pi * t)


def back_out(t, s=1.9):
    """0->1 with overshoot past 1 then settle (easeOutBack)."""
    t = np.clip(t, 0.0, 1.0) - 1
    return 1 + t * t * ((s + 1) * t + s)


def back_in(t, s=1.9):
    t = np.clip(t, 0.0, 1.0)
    return t * t * ((s + 1) * t - s)


def pulse(t, t0, dur):
    """Smooth 0->1->0 bump on [t0, t0+dur]."""
    u = np.clip((t - t0) / dur, 0.0, 1.0)
    return np.sin(np.pi * u) ** 2


def wrap_time(t, T):
    return t % T


def decay_osc(t, t0, freq, tau, amp=1.0):
    """Damped oscillation triggered at t0 (zero before)."""
    d = t - t0
    if d < 0:
        return 0.0
    return amp * math.exp(-d / tau) * math.sin(TAU * freq * d)


# ------------------------------------------------------------- rig model ---
LEG_NAMES = [f"{sd}{i}" for sd in "LR" for i in range(1, 5)]


class Chain:
    """A serial chain of bones with DOF definitions, solved by damped least squares."""

    def __init__(self, skel, bones, dofs, root_parent, limits=None, weights=None):
        self.sk = skel
        self.bones = bones
        self.dofs = dofs                      # list per bone of tuples like ('az',), ('x',), ('az','x')
        self.parent0 = root_parent
        self.idx = []
        for bi, d in enumerate(dofs):
            for kind in d:
                self.idx.append((bi, kind))
        self.n = len(self.idx)
        self.lim = np.array(limits) if limits is not None else np.tile([-2.4, 2.4], (self.n, 1))
        self.w = np.array(weights) if weights is not None else np.ones(self.n)

    def basis(self, theta):
        """Return per-bone basis rotation from the flat theta."""
        Rb = []
        k = 0
        for bi, d in enumerate(self.dofs):
            R = np.eye(3)
            b = self.bones[bi]
            for kind in d:
                a = theta[k]
                k += 1
                if kind == 'x':
                    R = R @ K.Rx(a)
                elif kind == 'z':
                    R = R @ K.Rz(a)
                elif kind == 'y':
                    R = R @ K.Ry(a)
                elif kind == 'az':      # rotation about the armature Z axis, expressed in the bone's local frame
                    R = self.sk.basis_from_arm_rot(b, K.Rz(a)) @ R
            Rb.append(R)
        return Rb

    def fk(self, parent_pose, theta, upto=None):
        """parent_pose: (R, head, tail) of the parent bone. Returns list of (R, head, tail) per chain bone."""
        sk = self.sk
        Rpp, hpp = parent_pose[0], parent_pose[1]
        prev = self.parent0
        Rb = self.basis(theta)
        out = []
        for bi, b in enumerate(self.bones):
            rel = sk.R[prev].T @ sk.R[b]
            d = sk.R[prev].T @ (sk.head[b] - sk.head[prev])
            Rn = Rpp @ rel @ Rb[bi]
            hn = hpp + Rpp @ d
            tn = hn + Rn[:, 1] * sk.len[b]
            out.append((Rn, hn, tn))
            Rpp, hpp, prev = Rn, hn, b
        return out

    def tip(self, parent_pose, theta):
        return self.fk(parent_pose, theta)[-1][2]

    def solve(self, parent_pose, target, theta0, theta_ref=None, mu=2e-4, iters=30, tol=1.5e-4, dtol=2e-5):
        th = np.array(theta0, dtype=np.float64)
        ref = np.zeros(self.n) if theta_ref is None else np.asarray(theta_ref)
        for _ in range(iters):
            p = self.tip(parent_pose, th)
            e = target - p
            if np.linalg.norm(e) < tol:
                break
            J = np.zeros((3, self.n))
            for j in range(self.n):
                t2 = th.copy()
                t2[j] += 1e-5
                J[:, j] = (self.tip(parent_pose, t2) - p) / 1e-5
            W = np.diag(mu * self.w)
            A = J.T @ J + W
            g = J.T @ e - W @ (th - ref)
            d = np.linalg.solve(A, g)
            m = np.abs(d).max()
            if m > 0.35:
                d *= 0.35 / m
            th = np.clip(th + d, self.lim[:, 0], self.lim[:, 1])
            if m < dtol:
                break
        return th


def make_leg_chain(skel, leg):
    bones = [f"leg_{leg}_{k}" for k in (1, 2, 3, 4)]
    dofs = [('az', 'x'), ('x',), ('x',), ('x',)]
    lim = [(-0.9, 0.9), (-0.9, 0.9), (-1.5, 1.5), (-1.7, 1.7), (-1.9, 1.9)]
    w = [0.15, 0.6, 1.0, 1.0, 1.0]
    return Chain(skel, bones, dofs, "body", lim, w)


# ----------------------------------------------------------- pose builder --
class Pose:
    """One frame of animation as basis rotations/translations for every bone."""

    def __init__(self, skel):
        self.sk = skel
        self.Rb = {}
        self.Lb = {}

    def set_rot_local(self, bone, rx=0.0, ry=0.0, rz=0.0):
        self.Rb[bone] = K.Rx(rx) @ K.Ry(ry) @ K.Rz(rz)

    def add_rot_local(self, bone, rx=0.0, ry=0.0, rz=0.0):
        cur = self.Rb.get(bone, np.eye(3))
        self.Rb[bone] = cur @ K.Rx(rx) @ K.Ry(ry) @ K.Rz(rz)

    def body(self, loc, yaw=0.0, pitch=0.0, roll=0.0):
        R = K.Rz(yaw) @ K.Ry(pitch) @ K.Rx(roll)
        self.Rb["body"] = self.sk.basis_from_arm_rot("body", R)
        self.Lb["body"] = self.sk.basis_loc_from_arm("body", loc)


class Animator:
    def __init__(self):
        self.sk = K.Skel()
        self.legs = {lg: make_leg_chain(self.sk, lg) for lg in LEG_NAMES}
        rest = self.sk.pose({})
        self.rest = rest
        self.foot0 = {lg: rest[f"leg_{lg}_4"][2].copy() for lg in LEG_NAMES}
        self.theta = {lg: np.zeros(self.legs[lg].n) for lg in LEG_NAMES}

    # -- run a clip: motion(t) -> dict(body=(loc,yaw,pitch,roll), feet={leg: pos}, fk=callable(pose))
    def bake(self, motion, nframes, loop=True, warmup=1):
        sk = self.sk
        frames = []
        theta = {lg: np.zeros(self.legs[lg].n) for lg in LEG_NAMES}
        total = nframes + 1 if not loop else nframes
        passes = (warmup + 1) if loop else 1
        for pss in range(passes):
            frames = []
            for f in range(nframes + 1):
                t = f / FPS
                m = motion(t)
                pose = Pose(sk)
                bl, by, bp, br = m["body"]
                pose.body(bl, by, bp, br)
                m["fk"](pose, t)
                full = sk.pose(pose.Rb, pose.Lb)
                err = 0.0
                for lg in LEG_NAMES:
                    ch = self.legs[lg]
                    th = ch.solve(full["body"], m["feet"][lg], theta[lg])
                    theta[lg] = th
                    Rb = ch.basis(th)
                    for b, R in zip(ch.bones, Rb):
                        pose.Rb[b] = R
                    tip = ch.tip(full["body"], th)
                    err = max(err, float(np.linalg.norm(tip - m["feet"][lg])))
                frames.append(dict(Rb=dict(pose.Rb), Lb=dict(pose.Lb), err=err))
        return frames


def foot_rest(A, lg):
    return A.foot0[lg].copy()
