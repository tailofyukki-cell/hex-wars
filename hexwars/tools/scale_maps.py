# scale_maps.py - 既存の .map をレイアウトを保ったまま拡大する
# 地形は最近傍拡大、建物・ユニットは座標をスケールして1つずつ再配置。
# 使い方: python tools/scale_maps.py [factor]   （既定 1.6）
import os, re, sys, math

MAPS_DIR = os.path.join(os.path.dirname(__file__), "..", "data", "maps")
FACTOR = float(sys.argv[1]) if len(sys.argv) > 1 else 1.6
MAX_WH = 64

BUILDINGS = set("cfapH")
SEA = "~"

# odd-r 近傍（C側 hex.c と同一）
NB_EVEN = [(1, 0), (0, -1), (-1, -1), (-1, 0), (-1, 1), (0, 1)]
NB_ODD  = [(1, 0), (1, -1), (0, -1), (-1, 0), (0, 1), (1, 1)]

def neighbors(x, y):
    tbl = NB_ODD if (y & 1) else NB_EVEN
    return [(x + dx, y + dy) for dx, dy in tbl]

def parse(path):
    sec = None
    kv = {"map": [], "owners": [], "units": []}
    rows = []
    header_comment = ""
    with open(path, encoding="utf-8") as f:
        for raw in f:
            line = raw.split("#")[0].strip()
            if raw.startswith("#") and not header_comment:
                header_comment = raw.rstrip("\n")
            if not line:
                continue
            m = re.match(r"\[(\w+)\]", line)
            if m:
                sec = m.group(1)
                continue
            k, _, v = line.partition("=")
            k, v = k.strip(), v.strip()
            if sec == "terrain" and k == "row":
                rows.append(v)
            elif sec in kv:
                kv[sec].append((k, v))
    return header_comment, kv, rows

def scale_map(path):
    header, kv, rows = parse(path)
    h0 = len(rows)
    w0 = len(rows[0])
    w1 = min(MAX_WH, round(w0 * FACTOR))
    h1 = min(MAX_WH, round(h0 * FACTOR))
    fx, fy = w1 / w0, h1 / h0

    # 1) 建物を素地（平地/海）に置換した地形で最近傍拡大
    base = []
    for y in range(h0):
        r = ""
        for x in range(w0):
            ch = rows[y][x]
            if ch in BUILDINGS:
                # 港は海沿いにあるので素地は平地でよい（海は '~' のまま）
                ch = "."
            r += ch
        base.append(r)

    grid = [[base[min(h0 - 1, int(y / fy))][min(w0 - 1, int(x / fx))]
             for x in range(w1)] for y in range(h1)]

    def in_b(x, y):
        return 0 <= x < w1 and 0 <= y < h1

    def bfs_find(sx, sy, ok):
        seen = {(sx, sy)}
        q = [(sx, sy)]
        while q:
            x, y = q.pop(0)
            if ok(x, y):
                return x, y
            for nx, ny in neighbors(x, y):
                if in_b(nx, ny) and (nx, ny) not in seen:
                    seen.add((nx, ny))
                    q.append((nx, ny))
        return sx, sy

    # 2) 建物を再配置（同座標衝突は近傍へ。港は海に隣接する位置を保証）
    placed = {}   # (old_x, old_y) -> (new_x, new_y)
    used = set()
    for y in range(h0):
        for x in range(w0):
            ch = rows[y][x]
            if ch not in BUILDINGS:
                continue
            tx = min(w1 - 1, round(x * fx))
            ty = min(h1 - 1, round(y * fy))

            def free_land(cx, cy):
                return (cx, cy) not in used and grid[cy][cx] not in (SEA,)
            def free_port(cx, cy):
                if (cx, cy) in used or grid[cy][cx] == SEA:
                    return False
                return any(in_b(nx, ny) and grid[ny][nx] == SEA
                           for nx, ny in neighbors(cx, cy))

            ok = free_port if ch == "p" else free_land
            tx, ty = bfs_find(tx, ty, ok)
            grid[ty][tx] = ch
            used.add((tx, ty))
            placed[(x, y)] = (tx, ty)

    # 3) owners を新座標に変換
    owners = []
    for k, v in kv["owners"]:
        x, y, o = [int(t) for t in v.split(",")]
        if (x, y) in placed:
            nx, ny = placed[(x, y)]
            owners.append((nx, ny, o))
        else:
            print(f"  警告: owner ({x},{y}) は建物ではない: {path}")

    # 4) ユニットを新座標へ（地形適性と重なりを解決）
    LAND_OK = set(".=wc f a p H r".replace(" ", ""))  # 陸that can stand (河は歩兵のみ… 簡易に許可)
    unit_used = set()
    units = []
    for k, v in kv["units"]:
        parts = [t.strip() for t in v.split(",")]
        o, tid, x, y = int(parts[0]), parts[1], int(parts[2]), int(parts[3])
        tx = min(w1 - 1, round(x * fx))
        ty = min(h1 - 1, round(y * fy))
        is_sea_unit = tid in ("DESTROYER", "CRUISER", "SUBMARINE", "T_SHIP",
                              "BATTLESHIP", "CARRIER", "GUNBOAT",
                              "MISSILE_BOAT", "SUPPLY_SHIP")
        is_air_unit = tid in ("FIGHTER", "BOMBER", "HELI", "T_COPTER",
                              "SUPPLY_AIR", "DIVE_BOMBER", "SCOUT_PLANE")
        is_foot = tid in ("INFANTRY", "AT_INFANTRY", "MILITIA", "AA_GUN")

        def ok_unit(cx, cy):
            if (cx, cy) in unit_used:
                return False
            ch = grid[cy][cx]
            if is_air_unit:
                return True
            if is_sea_unit:
                return ch == SEA or ch == "p"
            if is_foot:
                return ch != SEA            # 歩兵系は山・川も可
            return ch not in (SEA, "^", "r")  # 車両は海・山・川を避ける

        tx, ty = bfs_find(tx, ty, ok_unit)
        unit_used.add((tx, ty))
        rest = "," + ",".join(parts[4:]) if len(parts) > 4 else ""
        units.append(f"{o},{tid},{tx},{ty}{rest}")

    # 5) map セクション: 元の全キーを保持し、width/height を差し替え、
    #    turns/funds/objective_count のみスケールする（objective_count 等の
    #    メタデータを取りこぼさない）
    out = []
    out.append(header if header else f"# {os.path.basename(path)}")
    out.append("[map]")
    for k, v in kv["map"]:
        if k == "width":
            out.append(f"width  = {w1}")
        elif k == "height":
            out.append(f"height = {h1}")
        elif k == "turns":
            t = int(v);  out.append(f"turns  = {round(t * 1.3) if t > 0 else t}")
        elif k in ("funds0", "funds1"):
            out.append(f"{k} = {round(int(v) * 1.25 / 50) * 50}")
        elif k == "objective_count":
            out.append(f"objective_count = {max(1, round(int(v) * FACTOR))}")
        else:
            out.append(f"{k} = {v}")
    out.append("")
    out.append("[terrain]")
    for y in range(h1):
        out.append("row = " + "".join(grid[y]))
    out.append("")
    out.append("[owners]")
    for nx, ny, o in owners:
        out.append(f"own = {nx},{ny},{o}")
    out.append("")
    out.append("[units]")
    for u in units:
        out.append(f"unit = {u}")
    out.append("")

    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(out))
    print(f"{os.path.basename(path)}: {w0}x{h0} -> {w1}x{h1}")

# test_arena.map は単体テストが座標依存で参照する凍結フィクスチャなので拡大しない
SKIP = {"test_arena.map"}
for fn in sorted(os.listdir(MAPS_DIR)):
    if fn.endswith(".map") and fn not in SKIP:
        scale_map(os.path.join(MAPS_DIR, fn))
print("OK (test_arena.map は据え置き)")
