# add_airports.py - 各マップに空港を増設する
# 方針: 各陣営が空港2つ以上（HQ近くに増設）+ マップ中央に中立の前線空港2つ。
# 空戦マップ c08 は既に空港が多いため対象外。
import os, re

MAPS_DIR = os.path.join(os.path.dirname(__file__), "..", "data", "maps")
SKIP = {"c08_airfields.map"}

NB_EVEN = [(1, 0), (0, -1), (-1, -1), (-1, 0), (-1, 1), (0, 1)]
NB_ODD  = [(1, 0), (1, -1), (0, -1), (-1, 0), (0, 1), (1, 1)]

def neighbors(x, y):
    tbl = NB_ODD if (y & 1) else NB_EVEN
    return [(x + dx, y + dy) for dx, dy in tbl]

def parse(path):
    with open(path, encoding="utf-8") as f:
        lines = f.read().split("\n")
    rows, owners, units, sec = [], [], [], None
    for ln in lines:
        s = ln.split("#")[0].strip()
        m = re.match(r"\[(\w+)\]", s)
        if m:
            sec = m.group(1)
            continue
        if not s:
            continue
        k, _, v = s.partition("=")
        k, v = k.strip(), v.strip()
        if sec == "terrain" and k == "row":
            rows.append(list(v))
        elif sec == "owners" and k == "own":
            x, y, o = [int(t) for t in v.split(",")]
            owners.append([x, y, o])
        elif sec == "units" and k == "unit":
            p = v.split(",")
            units.append((int(p[2]), int(p[3])))
    return lines, rows, owners, units

def process(path):
    fname = os.path.basename(path)
    if fname in SKIP:
        print(f"{fname}: skip（空戦マップ）")
        return
    lines, rows, owners, units = parse(path)
    h = len(rows)
    w = len(rows[0])
    unit_pos = set(units)
    own_map = {(x, y): o for x, y, o in owners}

    def in_b(x, y):
        return 0 <= x < w and 0 <= y < h

    airports = [(x, y) for y in range(h) for x in range(w) if rows[y][x] == "a"]

    def near_airport(x, y, d=3):
        return any(abs(ax - x) + abs(ay - y) <= d for ax, ay in airports)

    def bfs_place(sx, sy, min_d=0):
        """(sx,sy) 近傍の適地（平地・ユニットなし・既存空港から距離をとる）"""
        seen = {(sx, sy)}
        q = [(sx, sy, 0)]
        while q:
            x, y, d = q.pop(0)
            if (d >= min_d and in_b(x, y) and rows[y][x] == "." and
                    (x, y) not in unit_pos and not near_airport(x, y)):
                return x, y
            for nx, ny in neighbors(x, y):
                if in_b(nx, ny) and (nx, ny) not in seen:
                    seen.add((nx, ny))
                    q.append((nx, ny, d + 1))
        return None

    added = []

    # 1) 各陣営: 空港2つ以上（HQ 近くに増設）
    for p in (0, 1):
        have = sum(1 for a in airports if own_map.get(a) == p)
        hq = next(((x, y) for y in range(h) for x in range(w)
                   if rows[y][x] == "H" and own_map.get((x, y)) == p), None)
        if not hq:
            continue
        while have < 2:
            spot = bfs_place(hq[0], hq[1], min_d=2)
            if not spot:
                break
            x, y = spot
            rows[y][x] = "a"
            airports.append((x, y))
            owners.append([x, y, p])
            own_map[(x, y)] = p
            added.append((x, y, p))
            have += 1

    # 2) 中立の前線空港2つ（中央付近。争奪ポイント）
    neutral = sum(1 for a in airports if own_map.get(a) is None)
    targets = [(w // 2, h // 3), (w // 2, h * 2 // 3)]
    for tx, ty in targets:
        if neutral >= 2:
            break
        spot = bfs_place(tx, ty)
        if not spot:
            continue
        x, y = spot
        rows[y][x] = "a"
        airports.append((x, y))
        added.append((x, y, -1))
        neutral += 1

    if not added:
        print(f"{fname}: 追加なし（既に十分）")
        return

    # ファイル書き戻し: terrain 行の置換 + owners への追記
    out = []
    row_i = 0
    for ln in lines:
        s = ln.split("#")[0].strip()
        if s.startswith("row ") or s.startswith("row="):
            out.append("row = " + "".join(rows[row_i]))
            row_i += 1
        elif re.match(r"\[units\]", s):
            # owners セクションは units の直前にあるので、その直前に追記済み行を挿入
            for x, y, o in added:
                if o >= 0:
                    out.append(f"own = {x},{y},{o}")
            out.append(ln)
        else:
            out.append(ln)
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(out))
    print(f"{fname}: 空港+{len(added)} {[(x, y, o) for x, y, o in added]}")

for fn in sorted(os.listdir(MAPS_DIR)):
    if fn.endswith(".map"):
        process(os.path.join(MAPS_DIR, fn))
print("OK")
