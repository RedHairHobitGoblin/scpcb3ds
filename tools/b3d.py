"""Static reader for Blitz3D .b3d models (items, SCP models).

Flattens the node hierarchy (bind pose, no animation) and returns
triangles grouped by texture in the same shape as xmodel.load_x, so the
converter can treat both formats alike.
"""

import struct


class _R:
    def __init__(self, data, pos=0, end=None):
        self.d = data
        self.p = pos
        self.end = len(data) if end is None else end

    def left(self):
        return self.end - self.p

    def i(self):
        v = struct.unpack_from("<i", self.d, self.p)[0]
        self.p += 4
        return v

    def f(self, n=1):
        v = struct.unpack_from(f"<{n}f", self.d, self.p)
        self.p += 4 * n
        return v if n > 1 else v[0]

    def s(self):
        e = self.d.index(b"\0", self.p)
        v = self.d[self.p:e].decode("latin1")
        self.p = e + 1
        return v

    def chunks(self):
        while self.left() >= 8:
            tag = self.d[self.p:self.p + 4].decode("latin1")
            size = struct.unpack_from("<i", self.d, self.p + 4)[0]
            start = self.p + 8
            self.p = start + size
            yield tag, _R(self.d, start, start + size)


def _quat_rot(q, v):
    # B3D quaternions are left-handed (Blitz3D): conjugate for the standard formula
    w, x, y, z = q[0], -q[1], -q[2], -q[3]
    # v' = q v q*
    tx = 2 * (y * v[2] - z * v[1])
    ty = 2 * (z * v[0] - x * v[2])
    tz = 2 * (x * v[1] - y * v[0])
    return (v[0] + w * tx + (y * tz - z * ty),
            v[1] + w * ty + (z * tx - x * tz),
            v[2] + w * tz + (x * ty - y * tx))


def load_b3d(path):
    with open(path, "rb") as fh:
        data = fh.read()
    top = _R(data)
    textures, brushes = [], []
    out = {}

    def node(r, parent_xf):
        r.s()  # name
        pos = r.f(3)
        scale = r.f(3)
        rot = r.f(4)  # w, x, y, z

        def xf(v):
            v = (v[0] * scale[0], v[1] * scale[1], v[2] * scale[2])
            v = _quat_rot(rot, v)
            v = (v[0] + pos[0], v[1] + pos[1], v[2] + pos[2])
            return parent_xf(v)

        for tag, c in r.chunks():
            if tag == "MESH":
                mesh(c, xf)
            elif tag == "NODE":
                node(c, xf)

    def mesh(r, xf):
        mesh_brush = r.i()
        verts = []
        for tag, c in r.chunks():
            if tag == "VRTS":
                flags, sets, size = c.i(), c.i(), c.i()
                stride = 3 + (3 if flags & 1 else 0) + (4 if flags & 2 else 0) + sets * size
                while c.left() >= stride * 4:
                    vals = c.f(stride)
                    p = xf(vals[0:3])
                    k = 3 + (3 if flags & 1 else 0) + (4 if flags & 2 else 0)
                    uv = (vals[k], vals[k + 1]) if sets and size >= 2 else (0.0, 0.0)
                    uv1 = (vals[k + size], vals[k + size + 1]) if sets >= 2 and size >= 2 else uv
                    verts.append((p, uv, uv1))
            elif tag == "TRIS":
                brush = c.i()
                if brush < 0:
                    brush = mesh_brush
                tex = None
                if 0 <= brush < len(brushes) and brushes[brush]:
                    names = [textures[t] for t in brushes[brush] if 0 <= t < len(textures)]
                    lms = [n for n in names if "_lm" in n.lower()]
                    diffs = [n for n in names if "_lm" not in n.lower()]
                    if lms:
                        tex = (diffs[0] if diffs else None, lms[0])
                    elif diffs:
                        tex = diffs[0]
                tris = out.setdefault(tex, [])
                while c.left() >= 12:
                    a, b, cc = c.i(), c.i(), c.i()
                    if max(a, b, cc) < len(verts):
                        tris.append([verts[a], verts[b], verts[cc]])

    for tag, r in top.chunks():
        if tag != "BB3D":
            continue
        r.i()  # version
        for t2, c in r.chunks():
            if t2 == "TEXS":
                while c.left() > 0:
                    name = c.s().replace("\\", "/").split("/")[-1]
                    c.i(); c.i(); c.f(5)
                    textures.append(name)
            elif t2 == "BRUS":
                n = c.i()
                while c.left() > 0:
                    c.s(); c.f(4); c.f(); c.i(); c.i()
                    brushes.append([c.i() for _ in range(n)])
            elif t2 == "NODE":
                node(c, lambda v: v)
    return out
