# -*- coding: utf-8 -*-
"""f14 四面楚歌・海 を生成する。

    python tools/gen_f14_seasiege.py

四面楚歌（f01）の海版。陣営0が中央の要塞島、陣営1..4が四方の島。全陣営が独立。
艦隊が主役で、航空は補助（空港は各島に1つだけ）。
中央の島は岩礁（山）の環で囲い、四方に水路を開けてある（元の山の環と門の海版）。
岩礁は艦船も潜水艦も通れず、航空機だけが越えられる。
環は4つの弧に分かれているので、歩兵が環を歩いて中央へ渡ることはできない。
"""
import io
import os

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
OUT = os.path.join(ROOT, "data", "maps", "f14_seasiege.map")
W = H = 40
C = (20, 20)


def hexdist(x0, y0, x1, y1):
    q0 = x0 - (y0 - (y0 & 1)) // 2
    q1 = x1 - (y1 - (y1 & 1)) // 2
    dq, dr = q0 - q1, y0 - y1
    return (abs(dq) + abs(dr) + abs(dq + dr)) // 2


NB_E = [(1, 0), (0, -1), (-1, -1), (-1, 0), (-1, 1), (0, 1)]
NB_O = [(1, 0), (1, -1), (0, -1), (-1, 0), (0, 1), (1, 1)]


def nbrs(x, y):
    return [(x + dx, y + dy) for dx, dy in (NB_O if (y & 1) else NB_E)
            if 0 <= x + dx < W and 0 <= y + dy < H]


grid = [["~"] * W for _ in range(H)]

# 島: (中心, 陣営, 半径)
ISLANDS = [(C, 0, 3), ((20, 4), 1, 3), ((36, 20), 2, 3), ((20, 36), 3, 3), ((4, 20), 4, 3)]
for (cx, cy), p, r in ISLANDS:
    for y in range(H):
        for x in range(W):
            if hexdist(cx, cy, x, y) <= r:
                grid[y][x] = "."

# 岩礁の環（中心から7）。四方の水路（幅2〜3）は空ける
RING = 7
gaps = [(20, 13), (27, 20), (20, 27), (13, 20)]
for y in range(H):
    for x in range(W):
        if hexdist(C[0], C[1], x, y) == RING and all(hexdist(gx, gy, x, y) > 1 for gx, gy in gaps):
            grid[y][x] = "^"

owners = {}
units = []


def land_of(cx, cy, r):
    return [(x, y) for y in range(H) for x in range(W)
            if hexdist(cx, cy, x, y) <= r and grid[y][x] == "."]


def coastal(cells):
    return [(x, y) for x, y in cells if any(grid[ny][nx] == "~" for nx, ny in nbrs(x, y))]


def put(x, y, ch, p):
    grid[y][x] = ch
    owners[(x, y)] = p


for (cx, cy), p, r in ISLANDS:
    land = land_of(cx, cy, r)
    put(cx, cy, "H", p)
    coast = coastal(land)
    # 港: 中央（外の島は中央、中央の島は東西）に向いた岸と、その反対側の岸
    if p == 0:
        p1 = min(coast, key=lambda c: hexdist(c[0], c[1], 27, 20))
        p2 = min(coast, key=lambda c: hexdist(c[0], c[1], 13, 20))
    else:
        p1 = min(coast, key=lambda c: hexdist(c[0], c[1], C[0], C[1]))
        p2 = max(coast, key=lambda c: hexdist(c[0], c[1], p1[0], p1[1]))
    put(p1[0], p1[1], "p", p)
    put(p2[0], p2[1], "p", p)
    inner = [c for c in land if c not in ((cx, cy), p1, p2) and hexdist(cx, cy, c[0], c[1]) <= 2]
    # 空港・都市2 を、首都から離れた順に散らす。工場は置かない（陸の部隊が補充され続けて
    # 決着がつかなかったため。生産は港と空港だけ。首都は仕様上、陸を作れる）
    inner.sort(key=lambda c: (-hexdist(cx, cy, c[0], c[1]), c[1], c[0]))
    picked = []
    for c in inner:
        if all(hexdist(c[0], c[1], q[0], q[1]) >= 2 for q in picked):
            picked.append(c)
        if len(picked) == 3:
            break
    for ch, c in zip(("a", "c", "c"), picked):
        put(c[0], c[1], ch, p)
    if p == 0:   # 中央は都市をもう1つ（四方より囲まれているぶん収入で補う）
        rest = [c for c in inner if grid[c[1]][c[0]] == "."]
        if rest:
            put(rest[0][0], rest[0][1], "c", p)

    # 初期部隊: 艦は港の近くの海、陸は島の空き地
    sea_near = sorted([(x, y) for y in range(H) for x in range(W) if grid[y][x] == "~"
                       and hexdist(x, y, cx, cy) <= r + 3
                       and (p != 0 or hexdist(x, y, C[0], C[1]) < RING)],
                      key=lambda c: (hexdist(c[0], c[1], p1[0], p1[1]), c[1], c[0]))
    free_land = [c for c in land_of(cx, cy, r) if c not in owners]
    if p == 0:
        fleet = ["BATTLESHIP", "CRUISER", "DESTROYER", "DESTROYER", "SUBMARINE", "SUBCHASER", "T_SHIP"]
        ground = ["INFANTRY", "INFANTRY", "AA_TANK"]
        air = ["FIGHTER"]
    else:
        fleet = ["CRUISER", "DESTROYER", "DESTROYER", "SUBMARINE", "T_SHIP"]
        ground = ["INFANTRY", "INFANTRY"]
        air = []
    used = set()
    for t in fleet:
        c = next(c for c in sea_near if c not in used)
        used.add(c); units.append((p, t, c))
    for t in ground + air:
        c = free_land.pop(0)
        units.append((p, t, c))

name_lines = [
    "# f14 四面楚歌・海 - 5陣営の乱戦の海版。陣営0が中央の要塞島、陣営1..4が四方の島。",
    "# 艦隊が主役で、航空は補助（空港は各島に1つ）。工場は無く、生産は港と空港（と首都）だけ。全陣営が独立。",
    "# 中央の島は岩礁（山）の環で囲み、四方に水路を開けてある。岩礁は艦も潜水艦も",
    "# 通れず、航空機だけが越えられる。環は4つの弧に分かれていて、歩いて中央へは渡れない。",
    "# 首都を取るには輸送艦で歩兵を運んで上陸させる。",
    "# 生成: python tools/gen_f14_seasiege.py（研究所は tools/place_labs.py で後から置く）",
    "[map]",
    "name             = 四面楚歌・海",
    "name@en = Surrounded at Sea",
    "width            = %d" % W,
    "height           = %d" % H,
    "turns            = 80",
    "timeout_winner   = -1",
    "funds0           = 5000",
    "funds1           = 3000",
    "funds2           = 3000",
    "funds3           = 3000",
    "funds4           = 3000",
    "",
    "[terrain]",
]
lines = name_lines + ["row = " + "".join(r) for r in grid]
lines += ["", "[owners]"] + ["own = %d,%d,%d" % (x, y, p) for (x, y), p in sorted(owners.items(), key=lambda kv: (kv[1], kv[0][1], kv[0][0]))]
lines += ["", "[units]"] + ["unit = %d,%s,%d,%d" % (p, t, x, y) for p, t, (x, y) in units]
io.open(OUT, "w", encoding="utf-8", newline="\n").write("\n".join(lines) + "\n")
print("wrote", OUT, "units", len(units))
