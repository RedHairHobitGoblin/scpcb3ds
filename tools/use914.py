"""Turn Use914() (Main.bb) into a recipe table for the 3DS port.

Output lines (tab separated):
    N|T  <item name or tempname, lower case>  <setting>  keep | destroy | name|tempname;name|tempname...
N = matched by item name, T = matched by tempname (the "Default" Select). When a
setting has several CreateItem calls (Rand branches, difficulty variants), the 3DS
picks one of them at random. Combinations (SCP-148 + gas mask), cup colours and
other special cases are simplified.
"""

import re
import sys

SETTINGS = ["rough", "coarse", "1:1", "fine", "very fine"]


def strip(line):
    out, q = "", False
    for ch in line:
        if ch == '"':
            q = not q
        if ch == ";" and not q:
            break
        out += ch
    return out.strip()


def extract(path):
    lines = open(path, encoding="latin1").read().split("\n")
    start = next(i for i, l in enumerate(lines) if l.startswith("Function Use914("))
    end = next(i for i in range(start, len(lines)) if lines[i].startswith("End Function"))
    body = [strip(l) for l in lines[start + 1:end]]

    recipes = []  # (kind, key, setting, outcome)
    selects = []  # stack of select subjects
    items = None  # (kind, [keys]) of the current item case
    item_depth = -1
    settings = None
    found = None  # {"create": [...], "keep": bool}

    def flush():
        nonlocal found
        if items and settings and found is not None:
            outcome = (";".join(f"{n}|{t}" for n, t in found["create"]) if found["create"]
                       else ("keep" if found["keep"] else "destroy"))
            for k in items[1]:
                for st in settings:
                    recipes.append((items[0], k.lower(), st, outcome))
        found = None

    for l in body:
        low = l.lower()
        m = re.match(r"^select\s+(.*)$", low)
        if m:
            selects.append(m.group(1).strip())
            continue
        if low == "end select":
            if selects and selects[-1] == "setting":
                flush()
                settings = None
            selects.pop()
            continue
        if not selects:
            continue
        subj = selects[-1]
        m = re.match(r'^case\s+(".*)$', l, re.I)
        if m and subj in ("item\\itemtemplate\\name", "item\\itemtemplate\\tempname") and "setting" not in selects:
            flush()
            items = ("N" if subj.endswith("name") and not subj.endswith("tempname") else "T",
                     re.findall(r'"([^"]+)"', m.group(1)))
            item_depth = len(selects)
            settings = None
            continue
        if low == "default" and subj == "item\\itemtemplate\\name":
            items = None
            continue
        if subj == "setting":
            m = re.match(r'^case\s+(".*)$', l, re.I)
            if m:
                flush()
                settings = [s for s in re.findall(r'"([^"]+)"', m.group(1)) if s in SETTINGS]
                found = {"create": [], "keep": False}
                continue
        if found is not None:
            for m in re.finditer(r'CreateItem\("([^"]+)",\s*"([^"]+)"', l):
                if (m.group(1), m.group(2)) not in found["create"]:
                    found["create"].append((m.group(1), m.group(2)))
            if re.search(r"PositionEntity\(item\\collider", l, re.I):
                found["keep"] = True
    return recipes


if __name__ == "__main__":
    rec = extract(sys.argv[1])
    with open(sys.argv[2], "w") as f:
        for r in rec:
            f.write("\t".join(r) + "\n")
    print(f"{len(rec)} recipes, {len({(r[0], r[1]) for r in rec})} items")
