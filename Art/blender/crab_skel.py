"""Skeleton rest data and forward kinematics (pure numpy) for the fiddler crab.

The FK here follows Blender's pose-bone maths exactly:
  pose = parent_pose @ (parent_rest^-1 @ rest) @ basis
so animation can be authored (and legs solved with IK) in numpy, then written
as plain pose-bone keys. rig_animate_crab.py checks this FK against Blender.
"""
import numpy as np
import crab_lib as L


def unit(v):
    v = np.asarray(v, dtype=np.float64)
    return v / np.linalg.norm(v)


def rest_frame(head, tail, hinge):
    """Bone rest rotation (columns X,Y,Z): Y along the bone, X = hinge axis (orthogonalised)."""
    y = unit(tail - head)
    x = np.asarray(hinge, dtype=np.float64)
    x = x - np.dot(x, y) * y
    if np.linalg.norm(x) < 1e-6:
        x = np.cross(y, [0, 0, 1.0]) if abs(y[2]) < 0.99 else np.array([1.0, 0, 0])
    x = unit(x)
    z = np.cross(x, y)
    return np.stack([x, y, z], 1)


def rot_axis(axis, ang):
    a = unit(axis)
    c, s = np.cos(ang), np.sin(ang)
    x, y, z = a
    K = np.array([[0, -z, y], [z, 0, -x], [-y, x, 0]])
    return np.eye(3) + s * K + (1 - c) * (K @ K)


def Rx(a):
    c, s = np.cos(a), np.sin(a)
    return np.array([[1, 0, 0], [0, c, -s], [0, s, c]])


def Ry(a):
    c, s = np.cos(a), np.sin(a)
    return np.array([[c, 0, s], [0, 1, 0], [-s, 0, c]])


def Rz(a):
    c, s = np.cos(a), np.sin(a)
    return np.array([[c, -s, 0], [s, c, 0], [0, 0, 1]])


def mat_to_quat(R):
    """3x3 rotation to (w,x,y,z)."""
    t = np.trace(R)
    if t > 0:
        s = np.sqrt(t + 1.0) * 2
        w = 0.25 * s
        x = (R[2, 1] - R[1, 2]) / s
        y = (R[0, 2] - R[2, 0]) / s
        z = (R[1, 0] - R[0, 1]) / s
    elif R[0, 0] > R[1, 1] and R[0, 0] > R[2, 2]:
        s = np.sqrt(1.0 + R[0, 0] - R[1, 1] - R[2, 2]) * 2
        w = (R[2, 1] - R[1, 2]) / s
        x = 0.25 * s
        y = (R[0, 1] + R[1, 0]) / s
        z = (R[0, 2] + R[2, 0]) / s
    elif R[1, 1] > R[2, 2]:
        s = np.sqrt(1.0 + R[1, 1] - R[0, 0] - R[2, 2]) * 2
        w = (R[0, 2] - R[2, 0]) / s
        x = (R[0, 1] + R[1, 0]) / s
        y = 0.25 * s
        z = (R[1, 2] + R[2, 1]) / s
    else:
        s = np.sqrt(1.0 + R[2, 2] - R[0, 0] - R[1, 1]) * 2
        w = (R[1, 0] - R[0, 1]) / s
        x = (R[0, 2] + R[2, 0]) / s
        y = (R[1, 2] + R[2, 1]) / s
        z = 0.25 * s
    q = np.array([w, x, y, z])
    return q / np.linalg.norm(q)


class Skel:
    def __init__(self, specs=None):
        self.specs = specs or L.bone_specs()
        self.names = list(self.specs.keys())          # parents always precede children
        self.R = {}
        self.head = {}
        self.tail = {}
        self.len = {}
        self.parent = {}
        for n, s in self.specs.items():
            self.head[n] = s["head"]
            self.tail[n] = s["tail"]
            self.R[n] = rest_frame(s["head"], s["tail"], s["hinge"])
            self.len[n] = float(np.linalg.norm(s["tail"] - s["head"]))
            self.parent[n] = s["parent"]

    def pose(self, Rb, Lb=None):
        """Rb: name -> 3x3 basis rotation (default identity); Lb: name -> basis translation (bone-local)."""
        Lb = Lb or {}
        out = {}
        for n in self.names:
            rb = Rb.get(n)
            if rb is None:
                rb = np.eye(3)
            lb = Lb.get(n)
            if lb is None:
                lb = np.zeros(3)
            p = self.parent[n]
            if p is None:
                Rn = self.R[n] @ rb
                hn = self.head[n] + self.R[n] @ lb
            else:
                Rpp, hpp = out[p][0], out[p][1]
                rel = self.R[p].T @ self.R[n]
                d = self.R[p].T @ (self.head[n] - self.head[p])
                Rn = Rpp @ rel @ rb
                hn = hpp + Rpp @ (rel @ lb + d)
            tn = hn + Rn[:, 1] * self.len[n]
            out[n] = (Rn, hn, tn)
        return out

    def basis_from_arm_rot(self, name, R_arm):
        """Local basis rotation that rotates `name` by R_arm (armature-space rotation) when its parent is at rest."""
        return self.R[name].T @ R_arm @ self.R[name]

    def basis_loc_from_arm(self, name, v_arm):
        return self.R[name].T @ np.asarray(v_arm, dtype=np.float64)


BONE_ORDER_NOTE = "specs dict order is parent-before-child"
