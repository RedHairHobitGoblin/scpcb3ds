"""Extract per-room door and item spawns from FillRoom() in MapSystem.bb.

FillRoom runs before the room is rotated, with r\\x, r\\y, r\\z at the room
origin, so evaluating the position expressions with those set to 0 gives
room-local coordinates in Blitz room units scaled by RoomScale.

Calls whose arguments depend on runtime state (loop variables, Rand, other
entities) are skipped and counted.
"""

import math
import re

ROOM_SCALE = 8.0 / 2048.0


def _split_args(s):
    args, depth, cur, quote = [], 0, "", False
    for ch in s:
        if ch == '"':
            quote = not quote
        if not quote:
            if ch == "(":
                depth += 1
            elif ch == ")":
                if depth == 0:
                    break
                depth -= 1
            elif ch == "," and depth == 0:
                args.append(cur.strip())
                cur = ""
                continue
        cur += ch
    args.append(cur.strip())
    return args


_ENV = {
    "RoomScale": ROOM_SCALE, "True": 1, "False": 0, "Null": 0,
    "Sin": lambda d: math.sin(math.radians(d)), "Cos": lambda d: math.cos(math.radians(d)),
    "Float": float, "Int": lambda v: int(round(v)), "Abs": abs,
    "Min": min, "Max": max, "Floor": math.floor, "Ceil": math.ceil,
}


def _eval(expr):
    e = expr.strip()
    if not e:
        return None
    if e.startswith('"') and e.endswith('"'):
        return e[1:-1]
    e = re.sub(r"\br\\(x|y|z|angle)\b", "0", e)
    e = re.sub(r"\br\\zone\b", "0", e)
    e = e.replace("#", "").replace("%", "")
    if "\\" in e or "Rand" in e or "Rnd" in e:
        return None
    e = re.sub(r"\bAnd\b", " and ", e)
    e = re.sub(r"\bOr\b", " or ", e)
    e = re.sub(r"\bNot\b", " not ", e)
    try:
        return eval(e, {"__builtins__": {}}, dict(_ENV))
    except Exception:
        return None


def _statements(line):
    """Split a Blitz line on ':' separators outside strings, dropping comments."""
    out, cur, quote = [], "", False
    for ch in line:
        if ch == '"':
            quote = not quote
        if not quote and ch == ";":
            break
        if not quote and ch == ":":
            out.append(cur)
            cur = ""
            continue
        cur += ch
    out.append(cur)
    return [s.strip() for s in out if s.strip()]


def extract(mapsystem_path):
    with open(mapsystem_path, encoding="latin1") as f:
        lines = f.read().split("\n")
    start = next(i for i, l in enumerate(lines) if l.startswith("Function FillRoom("))
    end = next(i for i in range(start + 1, len(lines)) if lines[i].startswith("End Function"))

    rooms = {}
    current = []
    doors_by_var = {}
    skipped = 0
    loop_depth = 0
    for raw in lines[start:end]:
        m = re.match(r'^\t\tCase (".*)$', raw.split(";")[0].rstrip())
        if m:
            current = re.findall(r'"([^"]+)"', m.group(1))
            for n in current:
                rooms.setdefault(n.lower(), {"doors": [], "items": []})
            doors_by_var = {}
            loop_depth = 0
            continue
        if not current:
            continue
        for st in _statements(raw):
            low = st.lower()
            if re.match(r"^(for|while|repeat)\b", low):
                loop_depth += 1
                continue
            if re.match(r"^(next|wend|until|forever)\b", low):
                loop_depth = max(0, loop_depth - 1)
                continue

            m = re.match(r"^([\w\\\[\]]+)\s*=\s*CreateDoor\((.*)$", st, re.I)
            if m:
                if loop_depth:
                    skipped += 1
                    continue
                a = _split_args(m.group(2))
                vals = [_eval(x) for x in a[1:5]]
                if any(v is None for v in vals):
                    skipped += 1
                    continue
                opt = lambda i, d: (_eval(a[i]) if len(a) > i and _eval(a[i]) is not None else d)
                door = {
                    "x": vals[0], "y": vals[1], "z": vals[2], "angle": vals[3],
                    "open": int(bool(opt(6, 0))), "big": int(opt(7, 0) or 0),
                    "keycard": int(opt(8, 0) or 0), "code": str(opt(9, "") or ""), "locked": 0,
                    "roomDoor": -1,
                }
                rd = re.match(r"^r\\RoomDoors\[(\d+)\]$", m.group(1), re.I)
                if rd:
                    door["roomDoor"] = int(rd.group(1))
                for n in current:
                    rooms[n.lower()]["doors"].append(door)
                doors_by_var[m.group(1)] = door
                continue

            # r\RoomDoors[n] = d: a door created into a variable, then kept by the room
            m = re.match(r"^r\\RoomDoors\[(\d+)\]\s*=\s*(\w+)$", st, re.I)
            if m and m.group(2) in doors_by_var:
                doors_by_var[m.group(2)]["roomDoor"] = int(m.group(1))
                doors_by_var["r\\RoomDoors[%s]" % m.group(1)] = doors_by_var[m.group(2)]
                continue

            m = re.match(r"^([\w\\\[\]]+)\\(open|locked)\s*=\s*(\w+)$", st, re.I)
            if m and m.group(1) in doors_by_var:
                v = _eval(m.group(3))
                if v is not None:
                    doors_by_var[m.group(1)][m.group(2).lower()] = int(bool(v))
                continue

            # PositionEntity r\Objects[i], x, y, z: pivots and markers that events use
            m = re.match(r"^PositionEntity\s*\(?\s*r\\Objects\[(\d+)\]\s*,(.*)$", st, re.I)
            if m and not loop_depth:
                body = m.group(2).strip()
                if body.endswith(")") and body.count(")") > body.count("("):
                    body = body[:-1]
                expr = []
                for x in _split_args(body)[:3]:
                    x = re.sub(r"Entity[XYZ]\(r\\obj(\s*,\s*True)?\)", "0", x)
                    expr.append(x)
                vals = [_eval(x) for x in expr]
                if len(vals) == 3 and all(isinstance(v, (int, float)) for v in vals):
                    for n in current:
                        rooms[n.lower()].setdefault("objects", {})[int(m.group(1))] = (vals[0], vals[1], vals[2])
                continue

            m = re.search(r"CreateItem\((.*)$", st, re.I)
            if m:
                if loop_depth:
                    skipped += 1
                    continue
                a = _split_args(m.group(1))
                if len(a) < 5:
                    skipped += 1
                    continue
                vals = [_eval(x) for x in a[:5]]
                if any(v is None for v in vals) or not isinstance(vals[0], str):
                    skipped += 1
                    continue
                item = {"name": vals[0], "tempname": vals[1], "x": vals[2], "y": vals[3], "z": vals[4]}
                for n in current:
                    rooms[n.lower()]["items"].append(item)
    return rooms, skipped


def parse_item_templates(items_path):
    """CreateItemTemplate(name, tempname, objpath, invimgpath, imgpath, scale, texturepath)."""
    out = []
    with open(items_path, encoding="latin1") as f:
        for line in f:
            for st in _statements(line):
                m = re.search(r"CreateItemTemplate\((.*)$", st)
                if not m:
                    continue
                a = _split_args(m.group(1))
                vals = [_eval(x) for x in a]
                if len(vals) < 6 or not all(isinstance(v, str) for v in vals[:5]) or vals[5] is None:
                    continue
                out.append({
                    "name": vals[0], "tempname": vals[1], "obj": vals[2].replace("\\", "/"),
                    "inv": vals[3].replace("\\", "/"), "img": vals[4].replace("\\", "/"),
                    "scale": float(vals[5]),
                    "tex": (vals[6] or "").replace("\\", "/") if len(vals) > 6 and isinstance(vals[6], str) else "",
                })
    return out


if __name__ == "__main__":
    import sys
    rooms, skipped = extract(sys.argv[1])
    nd = sum(len(r["doors"]) for r in rooms.values())
    ni = sum(len(r["items"]) for r in rooms.values())
    print(f"{len(rooms)} rooms, {nd} doors, {ni} items, {skipped} skipped")
    if len(sys.argv) > 2:
        t = parse_item_templates(sys.argv[2])
        print(f"{len(t)} item templates")
