"""The four fiddler crab clips as motion functions for crab_anim.Animator.bake."""
import math
import numpy as np
import crab_lib as L
import crab_skel as K
import crab_anim as A
from crab_anim import sstep, ease_io, back_out, pulse, Chain, LEG_NAMES, TAU

PIVOT = np.array([0.0, 0.0, L.ZC])


def cr_periodic(knots, x):
    """Periodic non-uniform Catmull-Rom through (x_i, v_i) knots, x in [0,1)."""
    xs = np.array([k[0] for k in knots], dtype=np.float64)
    vs = np.array([k[1] for k in knots], dtype=np.float64)
    n = len(xs)
    xe = np.concatenate([xs - 1.0, xs, xs + 1.0])
    ve = np.tile(vs, 3)
    x = x % 1.0
    i = n + int(np.searchsorted(xs, x, side="right")) - 1
    x0, x1, x2, x3 = xe[i - 1], xe[i], xe[i + 1], xe[i + 2]
    v0, v1, v2, v3 = ve[i - 1], ve[i], ve[i + 1], ve[i + 2]
    h = x2 - x1
    t = (x - x1) / h
    m1 = (v2 - v0) / (x2 - x0) * h
    m2 = (v3 - v1) / (x3 - x1) * h
    t2, t3 = t * t, t * t * t
    return (2 * t3 - 3 * t2 + 1) * v1 + (t3 - 2 * t2 + t) * m1 + (-2 * t3 + 3 * t2) * v2 + (t3 - t2) * m2


def body_xform(loc, yaw, pitch, roll):
    R = K.Rz(yaw) @ K.Ry(pitch) @ K.Rx(roll)

    def f(P):
        return PIVOT + R @ (np.asarray(P) - PIVOT) + loc
    return f


class Motions:
    def __init__(self):
        self.A = A.Animator()
        sk = self.A.sk
        self.sk = sk
        self.f0 = self.A.foot0
        self.rest = self.A.rest
        self.maj = Chain(sk, ["claw_major_1", "claw_major_2", "claw_major_3", "claw_major_finger"],
                         [('x', 'y', 'z'), ('x', 'y'), ('x',), ()], "body",
                         limits=[(-1.4, 1.4)] * 3 + [(-1.7, 1.7), (-0.9, 0.9)] + [(-1.3, 1.3)],
                         weights=[0.5, 0.5, 0.8, 0.5, 0.8, 0.6])
        self.mnr = Chain(sk, ["claw_minor_1", "claw_minor_2", "claw_minor_3", "claw_minor_finger"],
                         [('x', 'y', 'z'), ('x', 'y'), ('x',), ()], "body",
                         limits=[(-1.4, 1.4)] * 3 + [(-1.7, 1.7), (-0.9, 0.9)] + [(-1.3, 1.3)],
                         weights=[0.4, 0.4, 0.6, 0.4, 0.6, 0.5])
        self.tip_maj0 = self.rest["claw_major_finger"][2].copy()
        self.tip_mnr0 = self.rest["claw_minor_finger"][2].copy()
        self.state = {"maj": np.zeros(self.maj.n), "mnr": np.zeros(self.mnr.n)}
        self.mouth_target = np.array([0.43, 0.035, 0.235])

    # ------------------------------------------------------------- helpers --
    def solve_claw(self, key, chain, pose, target_arm, ref=None):
        full = self.sk.pose(pose.Rb, pose.Lb)
        th = chain.solve(full["body"], target_arm, self.state[key], theta_ref=ref, mu=4e-3, iters=300, tol=0.0, dtol=1e-7)
        self.state[key] = th
        for b, R in zip(chain.bones, chain.basis(th)):
            pose.Rb[b] = R
        return th

    def set_eyes(self, pose, l1, l2, r1, r2):
        """each argument is (rx, rz): rx tilts sideways (+ = toward -Y), rz tilts backward (+ = toward -X)."""
        for b, (rx, rz) in (("eye_L_1", l1), ("eye_L_2", l2), ("eye_R_1", r1), ("eye_R_2", r2)):
            pose.set_rot_local(b, rx=rx, rz=rz)

    # ---------------------------------------------------------------- idle --
    def idle(self, t):
        T = 4.0
        w = TAU / T
        b = math.sin(2 * w * t - 0.4)            # 2 breaths per loop
        loc = np.array([0.0, 0.016 * math.sin(w * t + 0.7), 0.011 * b])
        roll = 0.022 * math.sin(w * t + 1.9)
        pitch = 0.012 * b + 0.007 * math.sin(w * t)
        yaw = 0.020 * math.sin(w * t + 0.2)
        feet = {lg: self.f0[lg].copy() for lg in LEG_NAMES}
        # a toe tap and a small shuffle so the legs are not statues
        tap = pulse(t, 1.35, 0.30)
        feet["L4"] += np.array([-0.02 * tap, 0.02 * tap, 0.035 * tap])
        tap2 = pulse(t, 2.85, 0.34)
        feet["R2"] += np.array([0.015 * tap2, -0.03 * tap2, 0.04 * tap2])
        xf = body_xform(loc, yaw, pitch, roll)

        def fk(pose, tt):
            # eyes scan and twitch
            fl = lambda tt0, a: A.decay_osc(tt, tt0, 6.0, 0.08, a)
            l1 = (0.17 * math.sin(w * tt + 0.3) + fl(1.10, 0.16), 0.08 * math.sin(w * tt + 1.0))
            l2 = (0.17 * math.sin(w * tt - 0.3) + fl(1.14, 0.24), 0.07 * math.sin(w * tt + 0.4))
            r1 = (0.17 * math.sin(w * tt + 2.5) + fl(2.70, -0.15), 0.08 * math.sin(w * tt + 3.1))
            r2 = (0.17 * math.sin(w * tt + 1.9) + fl(2.74, -0.22), 0.07 * math.sin(w * tt + 2.2))
            self.set_eyes(pose, l1, l2, r1, r2)
            # major claw carried, gentle sway
            flex = float(A.pulse(tt, 2.3, 1.2))
            pose.set_rot_local("claw_major_1", rx=0.05 * math.sin(w * tt + 0.5), ry=0.07 * math.sin(w * tt) + 0.16 * flex, rz=0.03 * math.sin(2 * w * tt))
            pose.set_rot_local("claw_major_2", rx=0.05 * math.sin(w * tt - 0.4) - 0.10 * flex)
            pose.set_rot_local("claw_major_3", rx=0.03 * math.sin(w * tt - 0.9))
            pose.set_rot_local("claw_major_finger", rx=0.05 + 0.05 * math.sin(2 * w * tt + 1.0) - 0.30 * flex)
            # minor claw grooms the mouth: two visits with nibbling
            g = float(A.pulse(tt, 0.55, 1.5)) ** 0.6
            g2 = float(A.pulse(tt, 2.55, 1.1)) ** 0.6
            gg = max(g, 0.85 * g2)
            nib = math.sin(TAU * 4.0 * (tt - 0.55)) * gg * (1 if tt < 2.2 else 0) + math.sin(TAU * 4.5 * (tt - 2.55)) * gg * (1 if tt >= 2.2 else 0)
            tgt = xf(self.tip_mnr0 + (self.mouth_target - self.tip_mnr0) * gg + np.array([0.0, 0.0, 0.012 * nib]))
            self.solve_claw("mnr", self.mnr, pose, tgt)
            pose.set_rot_local("claw_minor_finger", rx=0.15 + 0.32 * nib * gg - 0.05)
            # mouthparts flutter
            pose.set_rot_local("mouth_L", rx=0.10 * math.sin(TAU * 3.0 * tt / 1.0) + 0.04 * math.sin(TAU * 7.0 * tt / 1.0))
            pose.set_rot_local("mouth_R", rx=-0.10 * math.sin(TAU * 3.0 * tt / 1.0 + 0.9) - 0.04 * math.sin(TAU * 7.0 * tt / 1.0 + 0.5))
        return dict(body=(loc, yaw, pitch, roll), feet=feet, fk=fk)

    # ------------------------------------------------------------- scuttle --
    def scuttle(self, t):
        T = 0.8
        pc = (t / T) % 1.0
        Aamp, H, beta = 0.16, 0.11, 0.6
        feet = {}
        for lg in LEG_NAMES:
            k = int(lg[1])
            off = (k - 1) * 0.2 + (0.5 if lg[0] == "L" else 0.0)
            ph = (pc - off) % 1.0
            if ph < beta:
                u = ph / beta
                y, z, x = -Aamp + 2 * Aamp * u, 0.0, 0.0
            else:
                u = (ph - beta) / (1 - beta)
                e = ease_io(u)
                y, z = Aamp - 2 * Aamp * e, H * math.sin(math.pi * u)
                x = 0.0
            p = self.f0[lg].copy()
            p += np.array([x, y, z])
            feet[lg] = p
        w = TAU * pc
        loc = np.array([0.0, 0.012 * math.sin(w + 0.4), 0.014 * math.cos(2 * w + 0.3)])
        roll = 0.030 * math.sin(w + 0.4)
        pitch = 0.012 * math.sin(2 * w + 1.1)
        yaw = 0.018 * math.sin(2 * w + 2.0)
        xf = body_xform(loc, yaw, pitch, roll)

        def fk(pose, tt):
            ww = TAU * ((tt / T) % 1.0)
            fl = lambda ph_: (0.05 * math.sin(ww + ph_), 0.06 * math.sin(2 * ww + ph_ + 0.6))
            self.set_eyes(pose, fl(0.5), fl(0.1), fl(2.4), fl(2.0))
            # major claw carried up, steady (counter-rolls the body sway)
            pose.set_rot_local("claw_major_1", rx=-0.30 * math.sin(ww + 0.4) * 0.3, ry=0.16, rz=0.0)
            pose.set_rot_local("claw_major_2", rx=0.04 * math.sin(2 * ww + 0.8), ry=0.0)
            pose.set_rot_local("claw_major_3", rx=0.03 * math.sin(2 * ww + 1.4))
            pose.set_rot_local("claw_major_finger", rx=0.10)
            pose.set_rot_local("claw_minor_1", rx=0.06 * math.sin(2 * ww), ry=0.05 * math.sin(ww))
            pose.set_rot_local("claw_minor_2", rx=0.08 * math.sin(2 * ww + 1.0))
            pose.set_rot_local("claw_minor_finger", rx=0.10)
            pose.set_rot_local("mouth_L", rx=0.16 * math.sin(TAU * 5 * tt / T))
            pose.set_rot_local("mouth_R", rx=-0.16 * math.sin(TAU * 5 * tt / T + 0.7))
        return dict(body=(loc, yaw, pitch, roll), feet=feet, fk=fk)

    # ---------------------------------------------------------------- dash --
    def dash(self, t):
        T = 10 / 30.0
        crouch = float(ease_io(sstep(0.0, 0.11, t)) * (1 - 0.65 * ease_io(sstep(0.13, 0.30, t))))
        loc = np.array([0.0, -0.02 * float(sstep(0.08, 0.2, t)), -0.115 * crouch])
        roll = 0.11 * float(ease_io(sstep(0.05, 0.16, t)))
        pitch = 0.06 * crouch
        yaw = 0.0
        wburst = float(sstep(0.07, 0.15, t))
        cyc = 0.4
        Aamp, H, beta = 0.22, 0.13, 0.55
        feet = {}
        for lg in LEG_NAMES:
            k = int(lg[1])
            off = (k - 1) * 0.18 + (0.5 if lg[0] == "L" else 0.0)
            ph = (((t - 0.06) / cyc) + off + beta / 2) % 1.0
            if ph < beta:
                y, z = -Aamp + 2 * Aamp * ph / beta, 0.0
            else:
                u = (ph - beta) / (1 - beta)
                y, z = Aamp - 2 * Aamp * ease_io(u), H * math.sin(math.pi * u)
            p = self.f0[lg].copy()
            sgn = 1.0 if lg[0] == "L" else -1.0
            p[1] += sgn * 0.05 * crouch          # wider stance in the crouch
            p += np.array([0.0, y * wburst, z * wburst])
            feet[lg] = p
        xf = body_xform(loc, yaw, pitch, roll)

        def fk(pose, tt):
            tk = float(ease_io(sstep(0.0, 0.10, tt)))
            back = 0.55 * tk
            self.set_eyes(pose, (0.0, back), (0.0, back * 0.6), (0.0, back), (0.0, back * 0.6))
            # major claw tucks in and down, finger closed
            tgt = xf(self.tip_maj0 + np.array([-0.35, 0.28, -0.22]) * tk)
            self.solve_claw("maj", self.maj, pose, tgt)
            pose.set_rot_local("claw_major_finger", rx=0.10 + 0.12 * tk)
            pose.set_rot_local("claw_minor_1", rx=0.25 * tk)
            pose.set_rot_local("claw_minor_finger", rx=0.2 * tk)
            pose.set_rot_local("mouth_L", rx=0.2 * math.sin(TAU * 9 * tt))
            pose.set_rot_local("mouth_R", rx=-0.2 * math.sin(TAU * 9 * tt + 0.7))
        return dict(body=(loc, yaw, pitch, roll), feet=feet, fk=fk)

    # --------------------------------------------------------------- dance --
    BEATS = [0.0, 0.5, 1.0, 1.5, 2.0, 2.5, 3.2]

    def beat(self, t):
        t = t % 3.2
        for k in range(6):
            a, b = self.BEATS[k], self.BEATS[k + 1]
            if a <= t < b:
                return k, (t - a) / (b - a)
        return 5, 1.0

    U_KNOTS = [(0.0, 1.00), (0.07, 1.13), (0.17, 1.00), (0.42, 0.30), (0.60, -0.30), (0.83, 0.55)]
    Z_KNOTS = [(0.0, 0.8), (0.10, 1.0), (0.32, 0.1), (0.60, -1.0), (0.88, -0.1)]

    def dance(self, t):
        k, beta = self.beat(t)
        u = float(cr_periodic(self.U_KNOTS, beta))
        zb = float(cr_periodic(self.Z_KNOTS, beta))
        sgn = 1.0 if k % 2 == 0 else -1.0
        lift = 0.10
        loc = np.array([0.0, 0.048 * sgn * math.sin(math.pi * beta) ** 1.0, lift + 0.034 * zb])
        roll = -0.10 * sgn * math.sin(math.pi * beta)
        pitch = -0.05 * zb
        yaw = 0.05 * sgn * math.sin(math.pi * beta + 0.4) * math.sin(math.pi * beta)
        feet = {}
        grpA = {"L1", "L3", "R2", "R4"}
        for lg in LEG_NAMES:
            p = self.f0[lg].copy()
            sg = 1.0 if lg[0] == "L" else -1.0
            p[1] += sg * 0.045                                  # stretched, slightly wider
            inA = lg in grpA
            # lift group A during odd intervals, group B during even ones, landing on the next beat
            active = (inA and k % 2 == 1) or ((not inA) and k % 2 == 0)
            if active and beta > 0.45:
                q = (beta - 0.45) / 0.55
                lift_h = 0.075 * math.sin(math.pi * q) ** 1.2
                # tap: small press below the ground line is impossible, so overshoot as a fast final drop
                p[2] += lift_h
                p[1] += sg * 0.04 * math.sin(math.pi * q)
                p[0] += 0.03 * math.sin(math.pi * q) * (1 if lg[1] in "13" else -1)
            feet[lg] = p
        xf = body_xform(loc, yaw, pitch, roll)
        # claw path
        P0 = self.tip_maj0
        Ppk = np.array([1.02, -0.92, 1.22])
        D = Ppk - P0
        B = np.array([0.10, -0.15, 0.30])
        tgt_local = P0 + u * D + (u * (1 - u)) * B
        opn = float(sstep(0.55, 0.98, beta) * (1 - sstep(0.02, 0.10, beta)))
        ku = float(cr_periodic([(0.0, 0.0), (0.2, 0.0), (0.5, 0.2), (0.7, -0.3), (0.85, 0.5)], beta))

        def fk(pose, tt):
            self.solve_claw("maj", self.maj, pose, xf(tgt_local))
            pose.set_rot_local("claw_major_finger", rx=0.05 - 0.75 * opn)
            # minor claw flourish on the off-beat
            fb = float(cr_periodic([(0.0, 0.0), (0.25, 0.9), (0.5, 0.1), (0.75, 1.0)], beta))
            pose.set_rot_local("claw_minor_1", rx=-0.20 * fb, ry=0.55 * fb, rz=0.2 * sgn * fb)
            pose.set_rot_local("claw_minor_2", rx=0.35 * fb - 0.1)
            pose.set_rot_local("claw_minor_finger", rx=0.05 - 0.4 * fb)
            # eyestalks bob
            nod = 0.22 * float(cr_periodic([(0.0, 0.6), (0.15, 1.0), (0.45, 0.0), (0.65, -0.8), (0.9, 0.2)], beta))
            self.set_eyes(pose, (0.10 * sgn, nod), (0.12 * sgn, nod * 1.3), (-0.10 * sgn, nod), (-0.12 * sgn, nod * 1.3))
            pose.set_rot_local("mouth_L", rx=0.25 * math.sin(TAU * 13 * tt / 3.2))
            pose.set_rot_local("mouth_R", rx=-0.25 * math.sin(TAU * 13 * tt / 3.2 + 0.8))
        return dict(body=(loc, yaw, pitch, roll), feet=feet, fk=fk)


CLIPS = {"Idle": (120, "idle"), "Scuttle": (24, "scuttle"), "Dance": (96, "dance"), "Dash": (10, "dash")}
