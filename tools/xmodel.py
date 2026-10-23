"""Minimal reader for the text DirectX .x models SCP:CB uses (doors, buttons).

Handles both dialects in the game files: the 3ds Max exporter (inline
materials, nested frames) and 3D World Studio (top-level materials
referenced by name). Returns triangles grouped by texture, in Blitz3D
model space with frame transforms applied.
"""

import re

_TOKEN = re.compile(r'"[^"]*"|[{}]|<[^>]*>|[-+]?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?|[A-Za-z_][\w\-]*')


class Block:
    def __init__(self, kind, name):
        self.kind = kind
        self.name = name
        self.nums = []
        self.strings = []
        self.refs = []
        self.children = []


def _tokens(text):
    text = re.sub(r"//[^\n]*|#[^\n]*", "", text)
    return _TOKEN.findall(text[text.find("\n") + 1:])  # skip "xof" header line


def _parse(tokens, i, block):
    while i < len(tokens):
        t = tokens[i]
        if t == "}":
            return i + 1
        if t == "{":
            # reference: { name }
            j = i + 1
            while j < len(tokens) and tokens[j] != "}":
                if not tokens[j].startswith("<"):
                    block.refs.append(tokens[j])
                j += 1
            i = j + 1
        elif t.startswith('"'):
            block.strings.append(t[1:-1])
            i += 1
        elif t.startswith("<"):
            i += 1
        elif t[0].isalpha() or t[0] == "_":
            name = None
            j = i + 1
            if j < len(tokens) and tokens[j] != "{":
                name = tokens[j]
                j += 1
            if j < len(tokens) and tokens[j] == "{":
                child = Block(t, name)
                i = _parse(tokens, j + 1, child)
                if t != "template":
                    block.children.append(child)
            else:
                i += 1
        else:
            block.nums.append(float(t))
            i += 1
    return i


def _matmul(a, b):
    return [sum(a[r * 4 + k] * b[k * 4 + c] for k in range(4)) for r in range(4) for c in range(4)]


IDENTITY = [1.0, 0, 0, 0, 0, 1.0, 0, 0, 0, 0, 1.0, 0, 0, 0, 0, 1.0]


def _material_texture(mat):
    for c in mat.children:
        if c.kind == "TextureFilename" and c.strings:
            return c.strings[0].replace("\\", "/").split("/")[-1]
    return None


def _mesh(block, matrix, materials, out):
    n = block.nums
    nv = int(n[0])
    verts = []
    for v in range(nv):
        x, y, z = n[1 + v * 3: 4 + v * 3]
        verts.append((
            x * matrix[0] + y * matrix[4] + z * matrix[8] + matrix[12],
            x * matrix[1] + y * matrix[5] + z * matrix[9] + matrix[13],
            x * matrix[2] + y * matrix[6] + z * matrix[10] + matrix[14],
        ))
    p = 1 + nv * 3
    nf = int(n[p]); p += 1
    faces = []
    for _ in range(nf):
        k = int(n[p]); p += 1
        faces.append([int(i) for i in n[p:p + k]])
        p += k

    uvs = [(0.0, 0.0)] * nv
    face_mat = [0] * nf
    mats = [None]
    for c in block.children:
        if c.kind == "MeshTextureCoords":
            cnt = int(c.nums[0])
            uvs = [(c.nums[1 + i * 2], c.nums[2 + i * 2]) for i in range(cnt)]
        elif c.kind == "MeshMaterialList":
            nm, nfi = int(c.nums[0]), int(c.nums[1])
            idx = [int(x) for x in c.nums[2:2 + nfi]]
            face_mat = [idx[min(i, nfi - 1)] if nfi else 0 for i in range(nf)]
            mats = [_material_texture(m) for m in c.children if m.kind == "Material"]
            mats += [_material_texture(materials[r]) if r in materials else None for r in c.refs]
            if not mats:
                mats = [None] * max(nm, 1)

    for f, face in enumerate(faces):
        tex = mats[face_mat[f]] if face_mat[f] < len(mats) else None
        for k in range(1, len(face) - 1):
            tri = [face[0], face[k], face[k + 1]]
            out.setdefault(tex, []).append([(verts[i], uvs[i] if i < len(uvs) else (0, 0)) for i in tri])


def _frame(block, parent, materials, out):
    matrix = parent
    for c in block.children:
        if c.kind == "FrameTransformMatrix":
            matrix = _matmul(c.nums[:16], parent)
    for c in block.children:
        if c.kind == "Mesh":
            _mesh(c, matrix, materials, out)
        elif c.kind == "Frame":
            _frame(c, matrix, materials, out)


def load_x(path):
    """Returns {texture_name_or_None: [triangle, ...]}, triangle = 3 x ((x, y, z), (u, v))."""
    with open(path, encoding="latin1") as f:
        root = Block("root", None)
        _parse(_tokens(f.read()), 0, root)
    materials = {c.name: c for c in root.children if c.kind == "Material" and c.name}
    out = {}
    _frame(root, IDENTITY, materials, out)
    return out


def bounds(model):
    pts = [v[0] for tris in model.values() for tri in tris for v in tri]
    lo = [min(p[i] for p in pts) for i in range(3)]
    hi = [max(p[i] for p in pts) for i in range(3)]
    return lo, hi
