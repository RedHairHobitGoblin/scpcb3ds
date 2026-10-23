"""Convert SCP - Containment Breach room data into the compact 3DS format.

Reads the .rmesh rooms listed in Data/rooms.ini plus the textures they use,
and writes them to romfs/ for the 3DS build:

  romfs/rooms.txt          one "name file" pair per line
  romfs/rooms/<name>.scr   room geometry, collision and spawn point
  romfs/tex/<name>.t3d     textures, shrunk and pre-swizzled for the PICA200

Usage: py -3 tools/convert.py [upstream_dir] [romfs_dir]
"""

import math
import os
import shutil
import re
import struct
import sys

import numpy as np
from PIL import Image, ImageChops

import b3d
import b3danim
import fillroom
import xmodel

ROMFS = None  # set in main()

ROOM_SCALE = 8.0 / 2048.0  # MapSystem.bb: Global RoomScale# = 8.0 / 2048.0

# "Toned down" texture budget: diffuse textures and lightmaps are shrunk to
# these sizes (the PC game uses 512-1024 px textures).
MAX_DIFFUSE = 128
MAX_LIGHTMAP = 256

FMT_RGB565 = 0
FMT_RGBA4 = 1

SURF_ALPHA = 1
SURF_LIGHTMAP = 2


def read_str(f):
    n = struct.unpack("<i", f.read(4))[0]
    return f.read(n).decode("latin1")


def read_i(f):
    return struct.unpack("<i", f.read(4))[0]


def read_f(f):
    return struct.unpack("<f", f.read(4))[0]


def find_ci(directory, name):
    """Case-insensitive lookup of a file inside directory."""
    path = os.path.join(directory, name)
    if os.path.exists(path):
        return path
    low = name.lower()
    for entry in os.listdir(directory):
        if entry.lower() == low:
            return os.path.join(directory, entry)
    return None


def tex_key(name):
    base = os.path.splitext(os.path.basename(name))[0].lower()
    return re.sub(r"[^a-z0-9_]", "_", base)


# --- texture conversion -----------------------------------------------------

def _morton_table():
    xs, ys = [], []
    for i in range(64):
        xs.append((i & 1) | ((i >> 1) & 2) | ((i >> 2) & 4))
        ys.append(((i >> 1) & 1) | ((i >> 2) & 2) | ((i >> 3) & 4))
    return np.array(xs), np.array(ys)


MORTON_X, MORTON_Y = _morton_table()


def pow2_at_most(n, limit):
    p = 8
    while p * 2 <= min(n, limit):
        p *= 2
    return p


def swizzle(pixels, flip=True):
    """Reorder an (h, w) array into PICA200 8x8 Morton tiles.

    The PICA200 samples memory row 0 at t=1, so images are stored bottom row
    first: Blitz3D's v=0 (image top) then lands at t=0 and UVs pass through
    unchanged. 2D screen images (citro2d) are stored top row first instead
    (flip=False), as citro2d reads a sub-texture with top < bottom as rotated.
    """
    if flip:
        pixels = pixels[::-1]
    h, w = pixels.shape
    tiles = pixels.reshape(h // 8, 8, w // 8, 8).transpose(0, 2, 1, 3)
    return tiles[:, :, MORTON_Y, MORTON_X].reshape(-1)


def convert_texture(src, dst, limit, alpha, flip=True, size=None):
    img = Image.open(src)
    img = img.convert("RGBA" if alpha else "RGB")
    w, h = size or (pow2_at_most(img.width, limit), pow2_at_most(img.height, limit))
    img = img.resize((w, h), Image.LANCZOS)
    a = np.asarray(img).astype(np.uint16)
    if alpha:
        packed = ((a[..., 0] >> 4) << 12) | ((a[..., 1] >> 4) << 8) | ((a[..., 2] >> 4) << 4) | (a[..., 3] >> 4)
        fmt = FMT_RGBA4
    else:
        packed = ((a[..., 0] >> 3) << 11) | ((a[..., 1] >> 2) << 5) | (a[..., 2] >> 3)
        fmt = FMT_RGB565
    data = swizzle(packed, flip).astype("<u2").tobytes()
    with open(dst, "wb") as f:
        f.write(b"T3D1")
        f.write(struct.pack("<HHBxxx", w, h, fmt))
        f.write(data)
    return len(data)


# --- room conversion --------------------------------------------------------

class Surface:
    def __init__(self):
        self.flags = 0
        self.diffuse = None
        self.lightmap = None
        self.verts = b""
        self.nverts = 0
        self.indices = []


def to_3ds(x, y, z):
    # Blitz3D is left-handed; citro3d is right-handed, so mirror Z.
    return x * ROOM_SCALE, y * ROOM_SCALE, -z * ROOM_SCALE


def load_rmesh(path):
    surfaces = []
    collision = []  # list of 9-float triangles
    start = None
    props = []  # (file, pos, rot, scale) in Blitz room units
    hidden = []  # invisible collision: blocks movement, not sight (it isn't pickable in Blitz3D)
    with open(path, "rb") as f:
        header = read_str(f)
        if header not in ("RoomMesh", "RoomMesh.HasTriggerBox"):
            raise ValueError(f"{path}: not an rmesh ({header!r})")
        has_trigger = header == "RoomMesh.HasTriggerBox"

        for _ in range(read_i(f)):  # drawn meshes
            s = Surface()
            slots = []
            for _j in range(2):
                flag = f.read(1)[0]
                slots.append((flag, read_str(f) if flag else ""))
            # slot 0 = lightmap (UV set 1), slot 1 = diffuse (UV set 0)
            if slots[0][0] and slots[0][1]:
                s.lightmap = slots[0][1]
                s.flags |= SURF_LIGHTMAP
            if slots[1][0] and slots[1][1]:
                s.diffuse = slots[1][1]
            if slots[1][0] == 3 or slots[0][0] == 3:
                s.flags |= SURF_ALPHA
                s.flags &= ~SURF_LIGHTMAP

            nv = read_i(f)
            raw = f.read(nv * 31)
            positions = []
            out = bytearray()
            for i in range(nv):
                x, y, z, u0, v0, u1, v1 = struct.unpack_from("<7f", raw, i * 31)
                r, g, b = raw[i * 31 + 28: i * 31 + 31]
                p = to_3ds(x, y, z)
                positions.append(p)
                out += struct.pack("<7f4B", *p, u0, v0, u1, v1, r, g, b, 255)
            s.verts = bytes(out)
            s.nverts = nv

            nt = read_i(f)
            tris = struct.unpack(f"<{nt * 3}i", f.read(nt * 12))
            s.indices = list(tris)
            if not s.flags & SURF_ALPHA:
                for t in range(nt):
                    a, b, c = tris[t * 3: t * 3 + 3]
                    collision.append(positions[a] + positions[b] + positions[c])
            surfaces.append(s)

        for _ in range(read_i(f)):  # invisible collision meshes
            nv = read_i(f)
            pts = [to_3ds(*struct.unpack("<3f", f.read(12))) for _v in range(nv)]
            nt = read_i(f)
            for _t in range(nt):
                a, b, c = struct.unpack("<3i", f.read(12))
                hidden.append(pts[a] + pts[b] + pts[c])

        if has_trigger:
            for _ in range(read_i(f)):
                for _s in range(read_i(f)):
                    f.read(read_i(f) * 12)
                    f.read(read_i(f) * 12)
                read_str(f)

        for _ in range(read_i(f)):  # point entities
            kind = read_str(f)
            if kind == "screen":
                f.read(12); read_str(f)
            elif kind == "waypoint":
                f.read(12)
            elif kind == "light":
                f.read(12); f.read(4); read_str(f); f.read(4)
            elif kind == "spotlight":
                f.read(12); f.read(4); read_str(f); f.read(4); read_str(f); f.read(8)
            elif kind == "soundemitter":
                f.read(12); f.read(4); f.read(4)
            elif kind == "playerstart":
                x, y, z = struct.unpack("<3f", f.read(12))
                angles = read_str(f).split()
                yaw = float(angles[1]) if len(angles) > 1 else 0.0
                start = (*to_3ds(x, y, z), yaw)
            elif kind == "model":
                name = read_str(f)
                if name:
                    pos = struct.unpack("<3f", f.read(12))
                    rot = struct.unpack("<3f", f.read(12))
                    scl = struct.unpack("<3f", f.read(12))
                    props.append((name, pos, rot, scl))
                else:
                    f.read(12)
            else:
                raise ValueError(f"{path}: unknown entity {kind!r}")
    return surfaces, collision, start, props, hidden


def write_room(path, surfaces, collision, start, texkeys, hidden=()):
    keys = sorted({k for k in texkeys if k})
    index = {k: i for i, k in enumerate(keys)}
    with open(path, "wb") as f:
        f.write(b"SCR1")
        f.write(struct.pack("<H", len(keys)))
        for k in keys:
            kb = k.encode()
            f.write(struct.pack("<B", len(kb)) + kb)
        f.write(struct.pack("<H", len(surfaces)))
        for s in surfaces:
            f.write(struct.pack("<Bhh", s.flags, index.get(s.diffuse_key, -1), index.get(s.lightmap_key, -1)))
            f.write(struct.pack("<II", s.nverts, len(s.indices)))
            f.write(s.verts)
            f.write(struct.pack(f"<{len(s.indices)}H", *s.indices))
            if len(s.indices) % 2:
                f.write(b"\0\0")
        f.write(struct.pack("<I", len(collision) + len(hidden)))
        for tri in list(collision) + list(hidden):
            f.write(struct.pack("<9f", *tri))
        if start:
            f.write(struct.pack("<B4f", 1, *start))
        else:
            f.write(struct.pack("<B4f", 0, 0, 0, 0, 0))
        f.write(struct.pack("<I", len(collision)))  # triangles that also block sight


def split_large(surfaces):
    """PICA200 indices are 16-bit; split any surface above 65535 vertices."""
    out = []
    for s in surfaces:
        if s.nverts <= 0xFFFF:
            out.append(s)
            continue
        raise ValueError("surface with more than 65535 vertices")
    return out


SHAPES = {"room1": 1, "1": 1, "room2": 2, "2": 2, "room2c": 3, "2c": 3,
          "room3": 4, "3": 4, "room4": 5, "4": 5}


def parse_rooms_ini(path):
    """Returns [(name, mesh_path, info)], info holding the RoomTemplates fields."""
    rooms = []
    section = None
    with open(path, encoding="latin1") as f:
        for line in f:
            line = line.split(";")[0].strip()
            m = re.match(r"^\[(.+)\]$", line)
            if m:
                section = {"name": m.group(1).strip()}
                rooms.append(section)
                continue
            m = re.match(r"^([^=]+?)\s*=\s*(.*)$", line)
            if m and section is not None:
                section[m.group(1).strip().lower()] = m.group(2).strip()

    def num(v):
        try:
            return int(float(v))
        except (TypeError, ValueError):
            return 0

    out = []
    for r in rooms:
        if "mesh path" not in r:
            continue
        info = {
            "shape": SHAPES.get(r.get("shape", "").lower(), 0),
            "commonness": max(min(num(r.get("commonness")), 100), 0),
            "zones": [num(r.get(f"zone{i + 1}")) for i in range(5)],
            "large": num(r.get("large")),
        }
        out.append((r["name"], r["mesh path"].replace("\\", "/"), info))
    return out


# Door parts, from LoadEntities in Main.bb and CreateDoor:
# (output name, source file, how the game scales it)
MODELS = [
    ("door", "GFX/map/Door01.x", "door"),           # fitted to 204 x 312 x 16 room units
    ("doorframe", "GFX/map/DoorFrame.x", "room"),
    ("heavydoor1", "GFX/map/heavydoor1.x", "room"),
    ("heavydoor2", "GFX/map/heavydoor2.x", "room"),
    ("button", "GFX/map/Button.x", 0.03),
    ("buttonkey", "GFX/map/ButtonKeycard.x", 0.03),
    ("buttoncode", "GFX/map/ButtonCode.x", 0.03),
    ("buttonscanner", "GFX/map/ButtonScanner.x", 0.03),
    ("bigdoor1", "GFX/map/ContDoorLeft.x", 55.0 * ROOM_SCALE),
    ("bigdoor2", "GFX/map/ContDoorRight.x", 55.0 * ROOM_SCALE),
    ("doorcoll", "GFX/map/DoorColl.x", "room"),
    ("leverbase", "GFX/map/leverbase.x", 0.04),      # CopyEntity(LeverBaseOBJ), ScaleEntity 0.04
    ("leverhandle", "GFX/map/leverhandle.x", 0.04),
]

MODEL_LIGHT = 150  # vertex colour for models: no lightmaps, so a flat mid-grey


def convert_model(upstream, romfs, name, src, mode, done_tex):
    model = xmodel.load_x(os.path.join(upstream, src))
    lo, hi = xmodel.bounds(model)
    if mode == "door":
        sc = [204.0 * ROOM_SCALE / (hi[0] - lo[0]), 312.0 * ROOM_SCALE / (hi[1] - lo[1]),
              16.0 * ROOM_SCALE / (hi[2] - lo[2])]
    elif mode == "room":
        sc = [ROOM_SCALE] * 3
    else:
        sc = [mode] * 3

    surfaces = []
    collision = []
    src_dir = os.path.dirname(os.path.join(upstream, src))
    for tex, tris in model.items():
        s = Surface()
        s.diffuse_key = s.lightmap_key = None
        if tex:
            key = tex_key(tex)
            if key not in done_tex:
                path = find_ci(src_dir, tex)
                done_tex[key] = bool(path) and convert_texture(path, os.path.join(romfs, "tex", key + ".t3d"),
                                                               MAX_DIFFUSE, False) > 0
            if done_tex[key]:
                s.diffuse_key = key
        out = bytearray()
        for tri in tris:
            pts = []
            for (x, y, z), (u, v) in tri:
                p = (x * sc[0], y * sc[1], -z * sc[2])
                pts.append(p)
                out += struct.pack("<7f4B", *p, u, v, 0.0, 0.0, MODEL_LIGHT, MODEL_LIGHT, MODEL_LIGHT, 255)
            collision.append(pts[0] + pts[1] + pts[2])
        s.verts = bytes(out)
        s.nverts = len(tris) * 3
        s.indices = list(range(s.nverts))
        surfaces.append(s)

    if mode == "door":
        # sliding panels collide as their bounding box
        x0, y0, z0 = lo[0] * sc[0], lo[1] * sc[1], -hi[2] * sc[2]
        x1, y1, z1 = hi[0] * sc[0], hi[1] * sc[1], -lo[2] * sc[2]
        collision = box_tris(x0, y0, z0, x1, y1, z1)
    elif name.startswith("bigdoor") or (mode == "room" and name.startswith("heavydoor")):
        x0, y0, z0 = lo[0] * sc[0], lo[1] * sc[1], -hi[2] * sc[2]
        x1, y1, z1 = hi[0] * sc[0], hi[1] * sc[1], -lo[2] * sc[2]
        collision = box_tris(x0, y0, z0, x1, y1, z1)
    elif name.startswith("button"):
        collision = []
    if name == "doorcoll":
        surfaces = []

    write_room(os.path.join(romfs, "models", name + ".scr"), surfaces, collision, None,
               [s.diffuse_key for s in surfaces])
    print(f"model {name:12s} {sum(s.nverts for s in surfaces):5d} verts {len(collision):4d} col tris")


def blitz_rotate(v, pitch, yaw, roll):
    """RotateEntity pitch, yaw, roll (Blitz3D, left-handed): v' = Ry * Rx * Rz * v."""
    x, y, z = v
    c, s_ = math.cos(math.radians(roll)), math.sin(math.radians(roll))
    x, y = x * c - y * s_, x * s_ + y * c
    c, s_ = math.cos(math.radians(pitch)), math.sin(math.radians(pitch))
    y, z = y * c - z * s_, y * s_ + z * c
    c, s_ = math.cos(math.radians(yaw)), math.sin(math.radians(yaw))
    x, z = x * c - z * s_, x * s_ + z * c
    return x, y, z


def load_model(path):
    low = path.lower()
    if low.endswith(".b3d"):
        return b3d.load_b3d(path)
    if low.endswith(".x"):
        return xmodel.load_x(path)
    raise ValueError(f"unsupported model {path}")


def model_texture(done_tex, src_dir, tex, alpha=False, limit=None):
    """Converts a model texture once; returns its key or None."""
    if not tex:
        return None
    key = tex_key(tex) + ("_a" if alpha else "")
    if key not in done_tex:
        path = find_ci(src_dir, tex) if os.path.isdir(src_dir) else None
        done_tex[key] = bool(path) and convert_texture(path, os.path.join(ROMFS, "tex", key + ".t3d"),
                                                       limit or MAX_DIFFUSE, alpha) > 0
    return key if done_tex[key] else None


def model_surfaces(model, transform, done_tex, src_dir, light, tex_override=None):
    """Builds unlit surfaces and collision from a loaded model.

    transform maps a model-space vertex to 3DS space (already Z-mirrored)."""
    surfaces, collision = [], []
    for tex, tris in model.items():
        s = Surface()
        lightmap = None
        if isinstance(tex, tuple):  # b3d brush with a lightmap: (diffuse, lightmap)
            tex, lightmap = tex
        alpha = bool(tex) and tex.lower().endswith(".png") and not lightmap
        s.diffuse_key = model_texture(done_tex, os.path.dirname(tex_override) if tex_override else src_dir,
                                      os.path.basename(tex_override) if tex_override else tex, alpha)
        s.lightmap_key = model_texture(done_tex, src_dir, lightmap, False, MAX_LIGHTMAP) if lightmap else None
        if s.lightmap_key:
            s.flags |= SURF_LIGHTMAP
            light = 255  # lit by the lightmap, like room geometry
        if alpha:
            s.flags |= SURF_ALPHA
        # share identical vertices so props are indexed meshes, not triangle soup
        out = bytearray()
        index = {}
        s.indices = []
        for tri in tris:
            pts = []
            for vert in tri:
                p, uv0 = vert[0], vert[1]
                uv1 = vert[2] if len(vert) > 2 else (0.0, 0.0)
                q = transform(p)
                pts.append(q)
                key = (round(q[0], 4), round(q[1], 4), round(q[2], 4), round(uv0[0], 4), round(uv0[1], 4),
                       round(uv1[0], 4), round(uv1[1], 4))
                i = index.get(key)
                if i is None:
                    i = index[key] = len(index)
                    out += struct.pack("<7f4B", *q, *uv0, *uv1, light, light, light, 255)
                s.indices.append(i)
            if not alpha:
                collision.append(pts[0] + pts[1] + pts[2])
        s.verts = bytes(out)
        s.nverts = len(index)
        if s.nverts and s.nverts <= 0xFFFF:
            surfaces.append(s)
    return surfaces, collision


def merge_surfaces(surfaces):
    """Merges surfaces with the same textures and flags (fewer draw calls)."""
    groups = {}
    out = []
    for s in surfaces:
        key = (s.flags, s.diffuse_key, s.lightmap_key)
        g = groups.get(key)
        if g is not None and g.nverts + s.nverts <= 0xFFFF:
            base = g.nverts
            g.verts += s.verts
            g.indices += [i + base for i in s.indices]
            g.nverts += s.nverts
            continue
        m = Surface()
        m.flags, m.diffuse_key, m.lightmap_key = s.flags, s.diffuse_key, s.lightmap_key
        m.verts, m.indices, m.nverts = bytes(s.verts), list(s.indices), s.nverts
        groups[key] = m
        out.append(m)
    return out


PROP_LIGHT = 120

# Meshes FillRoom loads and parents to the room (file, Blitz room-local position in
# room units, scale): "173" = Objects[9], [10]; "start" = Objects[0] (IntroDesk).
ROOM_MESHES = {
    "173": [("GFX/map/173_2.b3d", (0, 0, 0), ROOM_SCALE), ("GFX/map/intro_labels.b3d", (0, 0, 0), ROOM_SCALE)],
    "start": [("GFX/map/IntroDesk.b3d", (272, 0, 400), ROOM_SCALE),
              ("GFX/map/IntroDrawer.b3d", (448, 0, 192), ROOM_SCALE)],
}


def bake_room_meshes(upstream, name, done_tex):
    surfaces, collision = [], []
    for src, pos, sc in ROOM_MESHES.get(name.lower(), []):
        path = os.path.join(upstream, src)
        if not os.path.exists(path):
            continue
        model = load_model(path)

        def xf(p, pos=pos, sc=sc):
            # model units * scale, placed at a room-unit offset
            return (p[0] * sc + pos[0] * ROOM_SCALE, p[1] * sc + pos[1] * ROOM_SCALE, -(p[2] * sc + pos[2] * ROOM_SCALE))
        s_, c_ = model_surfaces(model, xf, done_tex, os.path.dirname(path), PROP_LIGHT)
        surfaces += s_
        collision += c_
    return surfaces, collision


def bake_props(upstream, props, done_tex, cache={}):
    """Room props (GFX/map/Props) baked into the room as static geometry."""
    surfaces, collision = [], []
    prop_dir = os.path.join(upstream, "GFX", "map", "Props")
    for name, pos, rot, scl in props:
        path = find_ci(prop_dir, name.replace("\\", "/").split("/")[-1])
        if not path or not path.lower().endswith((".x", ".b3d")):
            continue
        if path not in cache:
            try:
                cache[path] = load_model(path)
            except Exception as e:
                print(f"  prop {name}: {e}")
                cache[path] = {}
        def xf(p, pos=pos, rot=rot, scl=scl):
            v = blitz_rotate((p[0] * scl[0], p[1] * scl[1], p[2] * scl[2]), *rot)
            return to_3ds(v[0] + pos[0], v[1] + pos[1], v[2] + pos[2])
        s, c = model_surfaces(cache[path], xf, done_tex, prop_dir, PROP_LIGHT)
        surfaces += s
        collision += c
    return surfaces, collision


def box_tris(x0, y0, z0, x1, y1, z1):
    c = [(x, y, z) for x in (x0, x1) for y in (y0, y1) for z in (z0, z1)]
    quads = [(0, 1, 3, 2), (4, 6, 7, 5), (0, 4, 5, 1), (2, 3, 7, 6), (0, 2, 6, 4), (1, 5, 7, 3)]
    tris = []
    for a, b, cc, d in quads:
        tris.append(c[a] + c[b] + c[cc])
        tris.append(c[a] + c[cc] + c[d])
    return tris


TUNNEL_PIECES = ["mt1", "mt2", "mt2c", "mt3", "mt4", "mt_elevator", "mt_generator"]


def convert_rmesh(upstream, romfs, name, mesh_path, done_tex):
    """One .rmesh (a room, or a maintenance tunnel piece) to romfs/rooms/<key>.scr."""
    nbytes = 0
    try:
        surfaces, collision, start, props, hidden = load_rmesh(mesh_path)
        surfaces = split_large(surfaces)
    except Exception as e:  # report and keep going with other rooms
        print(f"skip {name}: {e}")
        return None, 0

    mesh_dir = os.path.dirname(mesh_path)
    for s in surfaces:
        s.diffuse_key = s.lightmap_key = None
        for attr, limit in (("diffuse", MAX_DIFFUSE), ("lightmap", MAX_LIGHTMAP)):
            tex = getattr(s, attr)
            if not tex:
                continue
            key = tex_key(tex) + ("_a" if (attr == "diffuse" and s.flags & SURF_ALPHA) else "")
            if key not in done_tex:
                src = find_ci(mesh_dir, tex)
                if src is None:
                    print(f"  {name}: missing texture {tex}")
                    done_tex[key] = False
                else:
                    nbytes += convert_texture(src, os.path.join(romfs, "tex", key + ".t3d"),
                                                 limit, bool(s.flags & SURF_ALPHA) and attr == "diffuse")
                    done_tex[key] = True
            if done_tex[key]:
                setattr(s, attr + "_key", key)

    prop_surfs, prop_col = bake_props(upstream, props, done_tex)
    prop_surfs = merge_surfaces(prop_surfs)
    surfaces += prop_surfs
    collision += prop_col
    mesh_surfs, mesh_col = bake_room_meshes(upstream, name, done_tex)
    surfaces += mesh_surfs
    collision += mesh_col

    file_key = tex_key(name)
    keys = [k for s in surfaces for k in (s.diffuse_key, s.lightmap_key)]
    write_room(os.path.join(romfs, "rooms", file_key + ".scr"), surfaces, collision, start, keys, hidden)
    nv = sum(s.nverts for s in surfaces)
    print(f"{name:24s} {len(surfaces):3d} surfs {nv:6d} verts {len(collision):6d} col tris")
    return file_key, nbytes


def main():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    upstream = sys.argv[1] if len(sys.argv) > 1 else os.path.join(root, "upstream")
    romfs = sys.argv[2] if len(sys.argv) > 2 else os.path.join(root, "romfs")
    global ROMFS
    ROMFS = romfs
    os.makedirs(os.path.join(romfs, "rooms"), exist_ok=True)
    os.makedirs(os.path.join(romfs, "tex"), exist_ok=True)
    os.makedirs(os.path.join(romfs, "models"), exist_ok=True)

    done_tex = {}
    room_list = []
    tex_bytes = 0
    for name, mesh, info in parse_rooms_ini(os.path.join(upstream, "Data", "rooms.ini")):
        mesh_path = os.path.join(upstream, mesh)
        if not os.path.exists(mesh_path):
            print(f"skip {name}: {mesh} missing")
            continue
        file_key, nbytes = convert_rmesh(upstream, romfs, name, mesh_path, done_tex)
        tex_bytes += nbytes
        if file_key is None:
            continue
        room_list.append((name, file_key, info))

    # maintenance tunnel pieces (OBJTunnel(0..6), used by the "room2tunnel" event)
    for piece in TUNNEL_PIECES:
        convert_rmesh(upstream, romfs, piece, os.path.join(upstream, "GFX", "map", piece + ".rmesh"), done_tex)
    for model in MODELS:
        convert_model(upstream, romfs, *model, done_tex)
    convert_items(upstream, romfs, done_tex)
    convert_fill(upstream, romfs)
    convert_npcs(upstream, romfs, done_tex)
    convert_skinned(upstream, romfs, done_tex)
    convert_pd(upstream, romfs, done_tex)
    convert_forest(upstream, romfs, done_tex)
    convert_menu(upstream, romfs)
    # SCP-294's drink list is read as it is
    shutil.copy(os.path.join(upstream, "Data", "SCP-294.ini"), os.path.join(romfs, "294.ini"))
    # dimension 1499's chunk layouts
    shutil.copy(os.path.join(upstream, "Data", "1499chunks.INI"), os.path.join(romfs, "1499chunks.ini"))

    # key shape commonness zone1..zone5 large name
    with open(os.path.join(romfs, "rooms.txt"), "w") as f:
        for name, key, info in room_list:
            zones = " ".join(str(z) for z in info["zones"])
            f.write(f"{key} {info['shape']} {info['commonness']} {zones} {info['large']} {name.lower()}\n")
    print(f"{len(room_list)} rooms, {sum(done_tex.values())} textures ({tex_bytes // 1024} KiB)")


# --- items, room fill, NPC models ------------------------------------------

INV_SIZE = 64
DOC_SIZE = 512


def convert_inv_icon(src, dst):
    """Inventory icons use magenta (255, 0, 255) as the transparent colour."""
    img = Image.open(src).convert("RGB").resize((INV_SIZE, INV_SIZE), Image.LANCZOS)
    a = np.asarray(img).astype(np.int32)
    mask = (a[..., 0] > 200) & (a[..., 1] < 60) & (a[..., 2] > 200)
    rgba = np.dstack([a, np.where(mask, 0, 255)]).astype(np.uint8)
    Image.fromarray(rgba, "RGBA").save(dst + ".png")
    convert_texture(dst + ".png", dst, INV_SIZE, True, flip=False)
    os.remove(dst + ".png")


def convert_doc(src, dst):
    # Tall pages get a 1024-row texture so the text stays readable when zoomed.
    w, h = Image.open(src).size
    tw = pow2_at_most(w, DOC_SIZE)
    th = 1024 if h > w * 1.25 and w > 300 else pow2_at_most(h, DOC_SIZE)
    convert_texture(src, dst, DOC_SIZE, False, flip=False, size=(tw, th))
    with open(dst, "r+b") as f:
        f.seek(10)
        f.write(struct.pack("<H", round(h / w * 1000)))


def convert_items(upstream, romfs, done_tex):
    templates = fillroom.parse_item_templates(os.path.join(upstream, "Items.bb"))
    os.makedirs(os.path.join(romfs, "inv"), exist_ok=True)
    os.makedirs(os.path.join(romfs, "docs"), exist_ok=True)
    models = {}
    lines = []
    for t in templates:
        obj = t["obj"]
        mkey = "item_" + tex_key(obj) + ("_" + tex_key(t["tex"]) if t["tex"] else "")
        if mkey not in models:
            src = os.path.join(upstream, obj)
            try:
                model = load_model(src)
                sc = t["scale"]
                surfs, _ = model_surfaces(model, lambda p, sc=sc: (p[0] * sc, p[1] * sc, -p[2] * sc), done_tex,
                                          os.path.dirname(src), MODEL_LIGHT,
                                          os.path.join(upstream, t["tex"]) if t["tex"] else None)
                write_room(os.path.join(romfs, "models", mkey + ".scr"), surfs, [], None,
                           [x.diffuse_key for x in surfs])
                models[mkey] = True
            except Exception as e:
                print(f"  item model {obj}: {e}")
                models[mkey] = False
        inv = tex_key(t["inv"])
        inv_dst = os.path.join(romfs, "inv", inv + ".t3d")
        if not os.path.exists(inv_dst) and os.path.exists(os.path.join(upstream, t["inv"])):
            convert_inv_icon(os.path.join(upstream, t["inv"]), inv_dst)
        doc = tex_key(t["img"]) if t["img"] else ""
        if doc:
            doc_src = os.path.join(upstream, t["img"])
            doc_dst = os.path.join(romfs, "docs", doc + ".t3d")
            if not os.path.exists(doc_dst) and os.path.exists(doc_src):
                convert_doc(doc_src, doc_dst)
            elif not os.path.exists(doc_src):
                doc = ""
        lines.append("	".join([t["name"].lower(), t["tempname"].lower(), mkey if models[mkey] else "-",
                                 inv, doc or "-", t["name"]]))
    with open(os.path.join(romfs, "items.txt"), "w") as f:
        f.write("\n".join(lines) + "\n")
    print(f"{len(templates)} item templates, {sum(models.values())} item models")


def intro_cell_doors():
    """FillRoom "173": the cell block doors created in For loops (Blitz room units).
    Returns dicts like fillroom.extract's, plus noButtons."""
    out = []
    door = lambda x, z, angle, open_=0, locked=1, nob=0: out.append(
        {"x": x * ROOM_SCALE, "y": 0.0, "z": z * ROOM_SCALE, "angle": angle, "open": open_, "big": 0,
         "keycard": 0, "code": "", "locked": locked, "noButtons": nob})
    for ztemp in range(2):
        door(-5760, 320 + 896 * ztemp, 0)
        door(-8288, 320 + 896 * ztemp, 0, open_=1 if ztemp == 0 else 0)
        for xtemp in range(3):
            door(-(7424.0 - 512.0 * xtemp), 1008.0 - 480.0 * ztemp, 0 if ztemp else 180, nob=1)
        for xtemp in range(5):
            # xtemp = 2, ztemp = 1 is the player's cell (RoomDoors[6])
            door(-(5120.0 - 512.0 * xtemp), 1008.0 - 480.0 * ztemp, 0 if ztemp else 180, nob=1)
    return out


def convert_fill(upstream, romfs):
    rooms, skipped = fillroom.extract(os.path.join(upstream, "MapSystem.bb"))
    rooms.setdefault("173", {"doors": [], "items": []})["doors"] += intro_cell_doors()
    with open(os.path.join(romfs, "fill.txt"), "w") as f:
        for name, r in sorted(rooms.items()):
            for d in r["doors"]:
                # Blitz room-local -> 3DS room-local (mirror Z)
                f.write(f"D	{name}	{d['x']:.4f}	{d['y']:.4f}	{-d['z']:.4f}	{int(d['angle'])}	{d['open']}	"
                        f"{d['big']}	{d['keycard']}	{d['locked']}	{d['code'] or '-'}	{d.get('noButtons', 0)}	{d.get('roomDoor', -1)}\n")
            for idx, (x, y, z) in sorted(r.get("objects", {}).items()):
                f.write(f"O	{name}	{idx}	{x:.4f}	{y:.4f}	{-z:.4f}\n")
            for it in r["items"]:
                f.write(f"I	{name}	{it['x']:.4f}	{it['y']:.4f}	{-it['z']:.4f}	{it['name'].lower()}	"
                        f"{it['tempname'].lower()}\n")
    nd = sum(len(r["doors"]) for r in rooms.values())
    ni = sum(len(r["items"]) for r in rooms.values())
    print(f"fill: {nd} doors, {ni} items in {len(rooms)} rooms ({skipped} spawns skipped)")


def convert_padded(src, dst, size, canvas, alpha=False):
    """Resizes an image to `size` and pads it into a power-of-two `canvas`
    (for 2D screen images drawn 1:1 with a sub-rectangle)."""
    img = Image.open(src).convert("RGBA" if alpha else "RGB").resize(size, Image.LANCZOS)
    pad = Image.new(img.mode, canvas)
    pad.paste(img, (0, 0))
    tmp = dst + ".png"
    pad.save(tmp)
    convert_texture(tmp, dst, max(canvas), alpha, flip=False)
    os.remove(tmp)


# Pocket dimension pieces (FillRoom "pocketdimension"): static meshes with collision,
# in Blitz model space scaled by RoomScale and Z-mirrored like rooms.
PD_MESHES = [
    ("pd_hall", "GFX/map/pocketdimension2.b3d"),
    ("pd_throne", "GFX/map/pocketdimension3.b3d"),
    ("pd_pillar", "GFX/map/pocketdimension4.b3d"),
    ("pd_tower", "GFX/map/pocketdimension5.b3d"),
    ("pd_terrain", "GFX/map/pocketdimensionterrain.b3d"),
    # Gate B surface terrain (exit1 event) and the Gate A tunnel (gatea event)
    ("exit1_terrain", "GFX/map/exit1terrain.b3d"),
    ("gatea_tunnel", "GFX/map/gateatunnel.b3d"),
    # SCP-008's containment lid (room "008", r\Objects[1])
    ("008_lid", "GFX/map/008_2.b3d"),
    # the femur breaker's magnet cage (room106, r\Objects[6])
    ("room1062", "GFX/map/room1062.b3d"),
    # SCP-012's box and composition (room012 r\Objects[2], [4]); SCP-079's computer (scaled 1.3, not RoomScale)
    ("012_box", "GFX/map/room012_2.b3d"),
    ("012_comp", "GFX/map/room012_3.b3d"),
    ("079", "GFX/map/079.b3d", 1.3),
    # the rubber duck (room2offices2, room3pitduck: ScaleEntity 0.07, EntityTexture duck1.png); medibay props
    ("duck", "GFX/npcs/duck_low_res.b3d", 0.07, "GFX/npcs/duck1.png"),
    ("medibay_props", "GFX/map/medibay_props.b3d"),
    # SCP-1048 holding its drawing (room3pit1048), in its bind pose
    ("1048pp", "GFX/npcs/scp-1048pp.b3d", 0.05),
    # SCP-860's wooden door (room860 r\Objects[3]/[4], ScaleEntity 46,45,46 RoomScale) and its frame
    # (r\Objects[2], 45,45,80), and the forest's end walls (PlaceForest: DetailMesh[5])
    ("860_door", "GFX/map/forest/door.b3d", (46 * ROOM_SCALE, 45 * ROOM_SCALE, 46 * ROOM_SCALE)),
    ("860_frame", "GFX/map/forest/door_frame.b3d", (45 * ROOM_SCALE, 45 * ROOM_SCALE, 80 * ROOM_SCALE)),
    ("forest_wall", "GFX/map/forest/wall.b3d"),
    # dimension 1499: the 40 m ground plane every chunk stands on (unscaled) and the chunk objects
    ("1499plane", "GFX/map/dimension1499/1499plane.b3d", 1.0),
    ("1499obj1", "GFX/map/dimension1499/1499object1.b3d"),
    ("1499obj2", "GFX/map/dimension1499/1499object2.b3d"),
    ("1499obj3", "GFX/map/dimension1499/1499object3.b3d"),
    ("1499obj4", "GFX/map/dimension1499/1499object4.b3d"),
    ("1499obj5", "GFX/map/dimension1499/1499object5.b3d"),
    ("1499obj6", "GFX/map/dimension1499/1499object6.b3d"),
    ("1499obj7", "GFX/map/dimension1499/1499object7.b3d"),
    ("1499obj8", "GFX/map/dimension1499/1499object8.b3d"),
    ("1499obj9", "GFX/map/dimension1499/1499object9.b3d"),
    ("1499obj10", "GFX/map/dimension1499/1499object10.b3d"),
    ("1499obj11", "GFX/map/dimension1499/1499object11.b3d"),
    ("1499obj12", "GFX/map/dimension1499/1499object12.b3d"),
    ("1499obj13", "GFX/map/dimension1499/1499object13.b3d"),
    ("1499obj14", "GFX/map/dimension1499/1499object14.b3d"),
    ("1499obj15", "GFX/map/dimension1499/1499object15.b3d"),
]


def convert_pd(upstream, romfs, done_tex):
    for entry in PD_MESHES:
        name, src = entry[:2]
        sc = entry[2] if len(entry) > 2 else ROOM_SCALE
        tex_override = os.path.join(upstream, entry[3]) if len(entry) > 3 else None
        path = os.path.join(upstream, src)
        model = load_model(path)
        sx, sy, sz = sc if isinstance(sc, tuple) else (sc, sc, sc)
        surfs, col = model_surfaces(model, lambda p, sx=sx, sy=sy, sz=sz: (p[0] * sx, p[1] * sy, -p[2] * sz),
                                    done_tex, os.path.dirname(path), PROP_LIGHT, tex_override)
        surfs = merge_surfaces(surfs)
        write_room(os.path.join(romfs, "models", name + ".scr"), surfs, col, None,
                   [k for x in surfs for k in (x.diffuse_key, x.lightmap_key)])
        print(f"pd {name:10s} {sum(x.nverts for x in surfs):6d} verts {len(col):6d} col tris")


# --- SCP-860's forest (PlaceForest / load_terrain, MapSystem.bb) ----------

FOREST_TILES = [("forest1", "forest1h"), ("forest2", "forest2h"), ("forest2c", "forest2Ch"),
                ("forest3", "forest3h"), ("forest4", "forest4h")]  # ROOM1, ROOM2, ROOM2C, ROOM3, ROOM4
FOREST_TILE_SIZE = 12.0
FOREST_TREE_KEEP = 0.22  # share of the original's trees kept on the 3DS


def forest_blend_texture(forest_dir, hname, alpha_grid, out_path):
    """The ground and path layers of a tile pre-blended by the mask (the path layer's vertex alpha)."""
    size = 256
    floor = Image.open(os.path.join(forest_dir, "forestfloor.jpg")).convert("RGB").resize((size // 4, size // 4), Image.LANCZOS)
    path = Image.open(os.path.join(forest_dir, "forestpath.jpg")).convert("RGB").resize((size // 4, size // 4), Image.LANCZOS)
    tiled_f = Image.new("RGB", (size, size))
    tiled_p = Image.new("RGB", (size, size))
    for i in range(4):  # ScaleTexture t, x/4, y/4: four repeats across the tile
        for j in range(4):
            tiled_f.paste(floor, (i * size // 4, j * size // 4))
            tiled_p.paste(path, (i * size // 4, j * size // 4))
    a = Image.fromarray((np.clip(alpha_grid, 0, 1) * 255).astype(np.uint8), "L").resize((size, size), Image.BILINEAR)
    Image.composite(tiled_p, tiled_f, a).save(out_path)


def convert_forest(upstream, romfs, done_tex):
    forest_dir = os.path.join(upstream, "GFX", "map", "forest")
    detail_dir = os.path.join(forest_dir, "detail")
    trunk = load_model(os.path.join(detail_dir, "treetest4.b3d"))
    leaves = load_model(os.path.join(detail_dir, "treetest5.b3d"))
    rock = load_model(os.path.join(detail_dir, "rock.b3d"))
    rng = __import__("random").Random(860)
    for name, hname in FOREST_TILES:
        hm = Image.open(os.path.join(forest_dir, hname + ".png")).convert("L")
        hpx = np.asarray(hm).astype(np.float32)  # [y][x]
        mask = np.asarray(Image.open(os.path.join(forest_dir, hname + "_mask.png")).convert("RGBA"))[:, :, 3] / 255.0
        n = hm.width - 1  # x = ImageWidth - 1
        mw, mh = mask.shape[1], mask.shape[0]
        scale = FOREST_TILE_SIZE / n  # tempf1 = tile_size / MeshWidth
        heights = np.zeros((n + 1, n + 1), np.float32)
        alpha = np.zeros((n + 1, n + 1), np.float32)
        for lx in range(n + 1):
            for ly in range(n + 1):
                r = hpx[n - min(ly, n - 1), min(lx, n - 1)]
                heights[ly, lx] = r * 0.03
                mx = min(lx * mw / float(hm.width), mw - 1)
                my = min(mh - min(ly * mh / float(hm.height), mh - 1), mh - 1)
                def m(x, y):
                    return mask[int(min(max(y, 0), mh - 1)), int(min(max(x, 0), mw - 1))]
                al = (m(max(mx - 5, 5), max(my - 5, 5)) + m(min(mx + 5, mw - 5), min(my + 5, mh - 5)) +
                      m(max(mx - 5, 5), min(my + 5, mh - 5)) + m(min(mx + 5, mw - 5), max(my - 5, 5))) * 0.25
                alpha[ly, lx] = math.sqrt(al)
        # terrain: one surface with the blended texture
        tex_key_name = "forest_" + name
        tex_png = os.path.join(romfs, "tex", tex_key_name + ".png")
        forest_blend_texture(forest_dir, hname, alpha, tex_png)
        convert_texture(tex_png, os.path.join(romfs, "tex", tex_key_name + ".t3d"), 256, False)
        os.remove(tex_png)
        done_tex[tex_key_name] = True
        ground = Surface()
        ground.diffuse_key, ground.lightmap_key = tex_key_name, None
        out = bytearray()
        for ly in range(n + 1):
            for lx in range(n + 1):
                x, y, z = (lx - n / 2.0) * scale, heights[ly, lx] * scale, (ly - n / 2.0) * scale
                out += struct.pack("<7f4B", x, y, -z, lx / float(n), ly / float(n), 0.0, 0.0, 255, 255, 255, 255)
        ground.verts, ground.nverts = bytes(out), (n + 1) * (n + 1)
        collision = []
        idx = []
        for ly in range(n):
            for lx in range(n):
                a, b, c, d = lx + (n + 1) * ly, lx + (n + 1) * ly + (n + 1), (lx + 1) + (n + 1) * ly, (lx + 1) + (n + 1) * ly + (n + 1)
                for tri in ((a, b, c), (c, b, d)):
                    idx += tri  # Blitz order, like rooms (the cull mode accounts for the mirror)
                    collision.append(sum((struct.unpack_from("<3f", ground.verts, k * 32) for k in tri), ()))
        ground.indices = idx
        surfaces = [ground]
        # trees and rocks where the heightmap is high (GetColor lx, width - ly), thinned out
        detail = []
        tempf4 = n / float(hm.width)
        for lx in range(3, hm.width - 1):
            for ly in range(3, hm.width - 1):
                red = hpx[hm.width - ly, lx] if hm.width - ly < hm.height else 0
                if red <= rng.randint(100, 260):
                    continue
                kind = rng.randint(0, 7)
                if rng.random() > FOREST_TREE_KEEP:
                    continue
                px, pz = lx * tempf4 - n / 2.0, ly * tempf4 - n / 2.0
                if kind <= 6:
                    s_ = rng.uniform(0.25, 0.4)
                    py = red * 0.03 - rng.uniform(3.0, 3.2)
                    yaw = math.radians(rng.uniform(0, 360))
                    leaf_yaws = [math.radians(90 * i + rng.uniform(-20, 20)) for i in range(4)]
                    detail.append(("tree", px, py, pz, s_, yaw, leaf_yaws))
                else:
                    detail.append(("rock", px, red * 0.03 - 1.3, pz, 1.0, math.radians(rng.uniform(0, 360)), None))

        def place(model, sx, sy, sz, yaws, px, py, pz, alpha_tex):
            def xf(p, sx=sx, sy=sy, sz=sz, yaws=yaws, px=px, py=py, pz=pz):
                x, y, z = p[0] * sx, p[1] * sy, p[2] * sz
                for a in yaws:  # Blitz yaw (applied child first)
                    c, s2 = math.cos(a), math.sin(a)
                    x, z = x * c - z * s2, x * s2 + z * c
                x, y, z = (x + px) * scale, (y + py) * scale, (z + pz) * scale
                return (x, y, -z)
            surfs, _col = model_surfaces(model, xf, done_tex, detail_dir, 255)
            for sf in surfs:
                if alpha_tex:
                    sf.diffuse_key = model_texture(done_tex, detail_dir, "tree1.dds", True)
                    sf.flags |= SURF_ALPHA
            return surfs

        for kind, px, py, pz, s_, yaw, leaf_yaws in detail:
            if kind == "tree":
                surfaces += place(trunk, s_ * 1.1, s_, s_ * 1.1, [yaw], px, py, pz, False)
                for ly_ in leaf_yaws:
                    surfaces += place(leaves, s_ * 1.1, s_, s_ * 1.1, [ly_, yaw], px, py, pz, True)
            else:
                surfaces += place(rock, 1.0, 1.0, 1.0, [yaw], px, py, pz, False)
        surfaces = merge_surfaces(surfaces)
        keys = [k for x in surfaces for k in (x.diffuse_key, x.lightmap_key)]
        write_room(os.path.join(romfs, "models", name + ".scr"), surfaces, collision, None, keys)
        tris = sum(len(x.indices) // 3 for x in surfaces)
        print(f"forest {name:9s} {sum(x.nverts for x in surfaces):6d} verts {tris:6d} tris {len(detail):3d} details")


def convert_menu(upstream, romfs):
    """Main menu art (Menu.bb: MenuBack, Menu173, MenuText)."""
    menu = os.path.join(upstream, "GFX", "menu")
    out = os.path.join(romfs, "menu")
    os.makedirs(out, exist_ok=True)
    # The title screen, composed into one 400x240 image: the dim emblem from back.jpg behind,
    # SCP-173 bottom right, the logo (cut out of back.jpg's top left) centered at the top and
    # "Secure. Contain. Protect." centered along the bottom.
    back = Image.open(os.path.join(menu, "back.jpg")).convert("RGB")
    title = back.crop((0, 340, 1033, 960)).resize((400, 240), Image.LANCZOS)  # below the frame line
    s173 = Image.open(os.path.join(menu, "173back.jpg")).convert("RGB").resize((141, 200), Image.LANCZOS)
    title.paste(ImageChops.lighter(title.crop((259, 40, 400, 240)), s173), (259, 40))
    logo = back.crop((165, 15, 720, 245))  # "SCP", the emblem and "CONTAINMENT BREACH"
    lw = 260
    logo = logo.resize((lw, round(logo.height * lw / logo.width)), Image.LANCZOS)
    title.paste(ImageChops.lighter(title.crop((200 - lw // 2, 14, 200 - lw // 2 + lw, 14 + logo.height)), logo),
                (200 - lw // 2, 14))
    text = Image.open(os.path.join(menu, "scptext.jpg")).convert("RGB").resize((330, 36), Image.LANCZOS)
    title.paste(ImageChops.lighter(title.crop((35, 196, 365, 232)), text), (35, 196))
    title.save(os.path.join(out, "title.png"))
    convert_padded(os.path.join(out, "title.png"), os.path.join(out, "back.t3d"), (400, 240), (512, 256))
    os.remove(os.path.join(out, "title.png"))
    # GFX/endingscreen.pt is a JPEG (DrawEnding)
    convert_padded(os.path.join(upstream, "GFX", "endingscreen.pt"), os.path.join(out, "ending.t3d"), (240, 240), (256, 256))
    print("menu art converted")


NPC_MODELS = [
    # name, file, scale (NPCs.bb: 173 is scaled to 0.35 / MeshDepth... see CreateNPC)
    ("npc173", "GFX/npcs/173_2.b3d", None),
]


# Animated NPCs: name, model, ScaleEntity value from CreateNPC / NPCs.ini
SKINNED_NPCS = [
    # name, model, ScaleEntity value (NPCs.ini scale / divisor in CreateNPC), or None to size to a height
    ("106", "GFX/npcs/106_2.b3d", 0.25 / 2.2),
    ("096", "GFX/npcs/scp096.b3d", 0.6 / 3.0),
    ("049", "GFX/npcs/scp-049.b3d", None),
    ("guard", "GFX/npcs/guard.b3d", 0.29 / 2.5),
    ("classd", "GFX/npcs/classd.b3d", None),
    ("mtf", "GFX/npcs/MTF2.b3d", None),
    ("939", "GFX/npcs/scp-939.b3d", None),
    ("966", "GFX/npcs/scp-966.b3d", None),
    ("zombie", "GFX/npcs/zombie1.b3d", None),
    ("surgeon", "GFX/npcs/zombiesurgeon.b3d", None),
    ("066", "GFX/npcs/scp-066.b3d", None),
    ("372", "GFX/npcs/372.b3d", None),
    ("5131", "GFX/npcs/bll.b3d", None),
    ("1048a", "GFX/npcs/scp-1048a.b3d", None),
    ("035", "GFX/npcs/035.b3d", None),  # the host SCP-035 wears (room035)
    ("860", "GFX/npcs/forestmonster.b3d", 0.5 / 20.0),  # SCP-860-2 (NPCs.ini scale / 20)
    ("1499", "GFX/npcs/1499-1.b3d", 0.08 / 4.0),  # SCP-1499-1 (NPCs.ini scale / 4)
]
# target heights (m) for the models sized from MeshWidth/MeshHeight at runtime
NPC_HEIGHTS = {"049": 1.25, "classd": 1.25, "mtf": 1.25, "939": 1.3, "zombie": 1.2, "surgeon": 1.2,
               "066": 0.35, "372": 0.3, "5131": 1.9, "966": 1.7, "1048a": 0.35, "035": 1.25}
NPC_HEIGHT = 1.25  # fallback for models whose scale comes from MeshHeight at runtime


def convert_skinned(upstream, romfs, done_tex):
    os.makedirs(os.path.join(romfs, "npcs"), exist_ok=True)
    lines = []
    for name, src, scale in SKINNED_NPCS:
        path = os.path.join(upstream, src)
        try:
            bones, mesh, vw, last = b3danim.load_anim_b3d(path)
        except Exception as ex:
            print(f"skinned {name}: {ex}")
            continue
        lo, hi = b3danim.bounds_world(bones, mesh)
        if scale is None:
            scale = NPC_HEIGHTS.get(name, NPC_HEIGHT) / max(hi[1] - lo[1], 1e-4)
        keys = [model_texture(done_tex, os.path.dirname(path), tex) for tex, _ in mesh["surfs"]]
        b3danim.write_skn(os.path.join(romfs, "npcs", name + ".skn"), bones, mesh, vw, last, keys)
        lines.append(f"{name} {scale:.6f} {lo[1]:.4f}")
        print(f"skinned {name:6s} {len(bones):3d} bones {len(mesh['verts']):5d} verts {last:5d} frames "
              f"height {(hi[1] - lo[1]) * scale:.2f}")
    with open(os.path.join(romfs, "npcs", "npcs.txt"), "w") as f:
        f.write("\n".join(lines) + "\n")


def convert_npcs(upstream, romfs, done_tex):
    for name, src, scale in NPC_MODELS:
        path = os.path.join(upstream, src)
        model = load_model(path)
        lo, hi = xmodel.bounds(model)
        # CreateNPC (NPCtype173): the statue is about 2.2 m tall at RoomScale-ish units
        sc = scale or (1.65 / (hi[1] - lo[1]))
        surfs, _ = model_surfaces(model, lambda p, sc=sc, y0=lo[1]: (p[0] * sc, (p[1] - y0) * sc, -p[2] * sc),
                                  done_tex, os.path.dirname(path), MODEL_LIGHT)
        write_room(os.path.join(romfs, "models", name + ".scr"), surfs, [], None, [x.diffuse_key for x in surfs])
        print(f"npc {name}: {sum(x.nverts for x in surfs)} verts, scale {sc:.4f}")


if __name__ == "__main__":
    main()
