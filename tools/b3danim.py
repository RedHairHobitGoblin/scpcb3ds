"""Animated Blitz3D .b3d models -> skinned model files for the 3DS (.skn).

Keeps the node hierarchy as a skeleton (bind pose + B3D keyframes) and the
mesh with up to four bone weights per vertex. The 3DS samples the keys and
skins on the CPU (the PICA200 doesn't have the uniform space for 50+ bones).

.skn layout (little endian):
  "SKN1", u16 nbones, u16 nsurf, u32 nverts, u32 lastFrame
  bones:  i16 parent, 3f pos, 4f rot (w x y z), 3f scale,
          u16 npos, u16 nrot, u16 nscale,
          npos * (u16 frame, 3f), nrot * (u16 frame, 4f), nscale * (u16 frame, 3f)
  verts:  3f pos (mesh space, Blitz axes), 2f uv, 4 u8 bone, 4 u8 weight (sum 255)
  surfs:  u8 keylen, key, u32 nidx, nidx * u16
Positions stay in Blitz (left-handed) space; the 3DS mirrors Z after skinning.
"""

import struct

from b3d import _R


class Bone:
    def __init__(self, name, parent, pos, rot, scale):
        self.name = name
        self.parent = parent
        self.pos, self.rot, self.scale = pos, rot, scale
        self.kpos, self.krot, self.kscale = [], [], []


def load_anim_b3d(path):
    with open(path, "rb") as fh:
        data = fh.read()
    top = _R(data)
    textures, brushes = [], []
    bones = []
    mesh = {"verts": [], "surfs": [], "bone_of_node": {}}
    weights = {}  # vertex -> [(bone, w)]
    last_frame = 0

    def node(r, parent):
        nonlocal last_frame
        name = r.s()
        pos, scale, rot = r.f(3), r.f(3), r.f(4)
        idx = len(bones)
        bones.append(Bone(name, parent, pos, rot, scale))
        for tag, c in r.chunks():
            if tag == "MESH":
                if mesh["verts"]:
                    continue  # one skinned mesh per model is all the NPCs use
                mesh["node"] = idx
                read_mesh(c)
            elif tag == "BONE":
                while c.left() >= 8:
                    vid, w = c.i(), c.f()
                    if w > 0:
                        weights.setdefault(vid, []).append((idx, w))
            elif tag == "KEYS":
                flags = c.i()
                size = 4 + (12 if flags & 1 else 0) + (12 if flags & 2 else 0) + (16 if flags & 4 else 0)
                b = bones[idx]
                while c.left() >= size:
                    frame = c.i()
                    last_frame = max(last_frame, frame)
                    if flags & 1:
                        b.kpos.append((frame, c.f(3)))
                    if flags & 2:
                        b.kscale.append((frame, c.f(3)))
                    if flags & 4:
                        b.krot.append((frame, c.f(4)))
            elif tag == "ANIM":
                c.i()
                last_frame = max(last_frame, c.i())
            elif tag == "NODE":
                node(c, idx)

    def read_mesh(r):
        mesh_brush = r.i()
        for tag, c in r.chunks():
            if tag == "VRTS":
                flags, sets, size = c.i(), c.i(), c.i()
                stride = 3 + (3 if flags & 1 else 0) + (4 if flags & 2 else 0) + sets * size
                while c.left() >= stride * 4:
                    v = c.f(stride)
                    k = 3 + (3 if flags & 1 else 0) + (4 if flags & 2 else 0)
                    uv = (v[k], v[k + 1]) if sets and size >= 2 else (0.0, 0.0)
                    mesh["verts"].append((v[0:3], uv))
            elif tag == "TRIS":
                brush = c.i()
                if brush < 0:
                    brush = mesh_brush
                tex = None
                if 0 <= brush < len(brushes) and brushes[brush]:
                    t = brushes[brush][0]
                    if 0 <= t < len(textures):
                        tex = textures[t]
                idx = []
                while c.left() >= 12:
                    idx += [c.i(), c.i(), c.i()]
                mesh["surfs"].append((tex, idx))

    for tag, r in top.chunks():
        if tag != "BB3D":
            continue
        r.i()
        for t2, c in r.chunks():
            if t2 == "TEXS":
                while c.left() > 0:
                    textures.append(c.s().replace("\\", "/").split("/")[-1])
                    c.i(); c.i(); c.f(5)
            elif t2 == "BRUS":
                n = c.i()
                while c.left() > 0:
                    c.s(); c.f(4); c.f(); c.i(); c.i()
                    brushes.append([c.i() for _ in range(n)])
            elif t2 == "NODE":
                node(c, -1)

    # vertex weights: top four, normalised to 255; unweighted vertices follow the mesh node
    vw = []
    for i in range(len(mesh["verts"])):
        ws = sorted(weights.get(i, []), key=lambda x: -x[1])[:4]
        if not ws:
            ws = [(mesh.get("node", 0), 1.0)]
        total = sum(w for _, w in ws)
        q = [int(round(w / total * 255)) for _, w in ws]
        q[0] += 255 - sum(q)
        b = [bi for bi, _ in ws]
        while len(b) < 4:
            b.append(0)
            q.append(0)
        vw.append((b, q))
    return bones, mesh, vw, last_frame


def bounds_bind(mesh):
    pts = [v[0] for v in mesh["verts"]]
    lo = [min(p[i] for p in pts) for i in range(3)]
    hi = [max(p[i] for p in pts) for i in range(3)]
    return lo, hi


def write_skn(path, bones, mesh, vw, last_frame, surf_keys):
    if len(bones) > 255:
        raise ValueError("too many bones for u8 indices")
    with open(path, "wb") as f:
        surfs = [(k, idx) for k, (_tex, idx) in zip(surf_keys, mesh["surfs"]) if idx]
        f.write(b"SKN1")
        f.write(struct.pack("<HHII", len(bones), len(surfs), len(mesh["verts"]), last_frame))
        for b in bones:
            f.write(struct.pack("<h3f4f3f", b.parent, *b.pos, *b.rot, *b.scale))
            f.write(struct.pack("<HHH", len(b.kpos), len(b.krot), len(b.kscale)))
            for fr, v in b.kpos:
                f.write(struct.pack("<H3f", fr, *v))
            for fr, v in b.krot:
                f.write(struct.pack("<H4f", fr, *v))
            for fr, v in b.kscale:
                f.write(struct.pack("<H3f", fr, *v))
        for (p, uv), (bi, w) in zip(mesh["verts"], vw):
            f.write(struct.pack("<3f2f4B4B", *p, *uv, *bi, *w))
        for key, idx in surfs:
            kb = (key or "").encode()
            f.write(struct.pack("<B", len(kb)) + kb)
            if max(idx) > 0xFFFF:
                raise ValueError("mesh too large for 16-bit indices")
            f.write(struct.pack("<I", len(idx)))
            f.write(struct.pack(f"<{len(idx)}H", *idx))


def _quat_mat(q):
    # B3D quaternions are left-handed (Blitz3D): conjugate for the standard formula
    w, x, y, z = q[0], -q[1], -q[2], -q[3]
    return [[1 - 2 * (y * y + z * z), 2 * (x * y - w * z), 2 * (x * z + w * y)],
            [2 * (x * y + w * z), 1 - 2 * (x * x + z * z), 2 * (y * z - w * x)],
            [2 * (x * z - w * y), 2 * (y * z + w * x), 1 - 2 * (x * x + y * y)]]


def _local(b):
    r = _quat_mat(b.rot)
    m = [[r[i][j] * b.scale[j] for j in range(3)] + [b.pos[i]] for i in range(3)]
    return m


def _mul(a, b):
    out = []
    for i in range(3):
        row = [sum(a[i][k] * b[k][j] for k in range(3)) for j in range(3)]
        row.append(sum(a[i][k] * b[k][3] for k in range(3)) + a[i][3])
        out.append(row)
    return out


def bind_world(bones):
    world = []
    for b in bones:
        loc = _local(b)
        world.append(loc if b.parent < 0 else _mul(world[b.parent], loc))
    return world


def bounds_world(bones, mesh):
    """Bind-pose bounds with the mesh node's transform applied (as the 3DS does)."""
    m = bind_world(bones)[mesh.get("node", 0)]
    pts = [[sum(m[i][k] * p[k] for k in range(3)) + m[i][3] for i in range(3)] for p, _ in mesh["verts"]]
    lo = [min(p[i] for p in pts) for i in range(3)]
    hi = [max(p[i] for p in pts) for i in range(3)]
    return lo, hi
