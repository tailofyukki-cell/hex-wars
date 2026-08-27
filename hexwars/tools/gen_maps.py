# gen_maps.py - 全12マップを「マップ毎に違うシルエット（輪郭）」で生成する
#
# 長方形のグリッドの外側を「圏外 'x'」（全ユニット進入不可・非描画）または海で
# 削り取ることで、プレイ領域そのものの形を変える。
#
#   m01 国境の丘       … 巨大な六角形の大陸（斜めの丘の尾根）
#   m02 運河の対岸     … 海に浮かぶ斜めの双子島
#   m03 群島の海戦     … 盤面の大半が海。中央の群島を巡る制海権争い
#   c11 制海権の争奪   … キャンペーン後半の海戦。東の要塞島へ上陸
#   c12 敵首都上陸     … 最終の大規模上陸戦。西の海から東岸の敵首都へ
#   c01 国境の哨戒線   … L字型の国境地帯（角で直角に折れる）
#   c02 森林の防衛線   … 十字型の森（中央交差点の防衛）
#   c03 海峡横断       … 蝶ネクタイ型の両大陸が海を挟んで向き合う
#   c04 首都攻略       … 三角形の大陸。頂点に敵首都
#   c05 河川突破       … 稲妻（Z字）型の回廊を川が横切る
#   c06 橋頭堡の死守   … 鉤（フック）型の半島
#   c07 山岳の隘路     … ドーナツ型の環状谷。中心は山塊
#   c08 空の要衝       … Y字型の三叉空域
#   c09 補給都市の攻防 … 斜めに走る帯状の市街地
#   c10 平原の決戦     … 巨大な楕円の大平原
#
# 建物・ユニットは BFS で最寄りの有効ヘクスに吸着するので形を変えても壊れない。
import os, random, math

OUT = os.path.join(os.path.dirname(__file__), "..", "data", "maps")

NB_EVEN = [(1, 0), (0, -1), (-1, -1), (-1, 0), (-1, 1), (0, 1)]
NB_ODD  = [(1, 0), (1, -1), (0, -1), (-1, 0), (0, 1), (1, 1)]

def neighbors(x, y):
    tbl = NB_ODD if (y & 1) else NB_EVEN
    return [(x + dx, y + dy) for dx, dy in tbl]

def hexdist(x0, y0, x1, y1):
    q0 = x0 - (y0 - (y0 & 1)) // 2
    q1 = x1 - (y1 - (y1 & 1)) // 2
    dq, dr = q0 - q1, y0 - y1
    return (abs(dq) + abs(dr) + abs(dq + dr)) // 2

BUILDINGS = set("cfapH")
SEA = "~"
VOID = "x"
BLOCK_ALL = {SEA, VOID}

FOOT = {"INFANTRY", "AT_INFANTRY", "MILITIA", "AA_GUN"}
AIRU = {"FIGHTER", "BOMBER", "HELI", "T_COPTER", "SUPPLY_AIR",
        "DIVE_BOMBER", "SCOUT_PLANE", "T_PLANE"}
# 海中レイヤー。高さが違うので海面の艦と同じセルに置ける
SUBU = {"SUBMARINE"}
SEAU = {"DESTROYER", "CRUISER", "T_SHIP", "SUBMARINE",
        "BATTLESHIP", "CARRIER", "GUNBOAT", "MISSILE_BOAT", "SUPPLY_SHIP"}


class M:
    def __init__(self, w, h, seed, fill=VOID):
        self.w, self.h = w, h
        self.g = [[fill] * w for _ in range(h)]
        self.owners = []
        self.units = []
        self.unit_pos = set()
        self.rng = random.Random(seed)

    def inb(self, x, y):
        return 0 <= x < self.w and 0 <= y < self.h

    def get(self, x, y):
        return self.g[y][x]

    def set(self, x, y, ch, over=None):
        if self.inb(x, y) and (over is None or self.g[y][x] in over):
            self.g[y][x] = ch

    # --- 形状 ---
    def carve(self, pred, ch="."):
        for y in range(self.h):
            for x in range(self.w):
                if pred(x, y):
                    self.g[y][x] = ch

    def disk(self, cx, cy, r, ch, over=None):
        for y in range(self.h):
            for x in range(self.w):
                if hexdist(cx, cy, x, y) <= r:
                    self.set(x, y, ch, over)

    def ring(self, cx, cy, r0, r1, ch, over=None):
        for y in range(self.h):
            for x in range(self.w):
                if r0 <= hexdist(cx, cy, x, y) <= r1:
                    self.set(x, y, ch, over)

    def stamp_path(self, pts, r, ch, over=None):
        """制御点間をヘクス歩行し、半径 r の円盤を落として太い回廊を作る"""
        for i in range(len(pts) - 1):
            x, y = pts[i]
            tx, ty = pts[i + 1]
            self.disk(x, y, r, ch, over)
            guard = 0
            while (x, y) != (tx, ty) and guard < 600:
                guard += 1
                x, y = min(neighbors(x, y),
                           key=lambda p: hexdist(p[0], p[1], tx, ty))
                self.disk(x, y, r, ch, over)

    def path(self, pts, ch, over=None):
        """細い線（道路・川用）。圏外や海は上書きしない使い方を想定"""
        for i in range(len(pts) - 1):
            x, y = pts[i]
            tx, ty = pts[i + 1]
            self.set(x, y, ch, over)
            guard = 0
            while (x, y) != (tx, ty) and guard < 600:
                guard += 1
                x, y = min(neighbors(x, y),
                           key=lambda p: hexdist(p[0], p[1], tx, ty))
                self.set(x, y, ch, over)

    def scatter(self, ch, prob, over="."):
        for y in range(self.h):
            for x in range(self.w):
                if self.g[y][x] in over and self.rng.random() < prob:
                    self.g[y][x] = ch

    def clump(self, ch, over="."):
        snap = [row[:] for row in self.g]
        for y in range(self.h):
            for x in range(self.w):
                if snap[y][x] not in over:
                    continue
                n = sum(1 for nx, ny in neighbors(x, y)
                        if self.inb(nx, ny) and snap[ny][nx] == ch)
                if n >= 3:
                    self.g[y][x] = ch

    # --- 配置 ---
    def _bfs(self, sx, sy, ok):
        sx = max(0, min(self.w - 1, sx))
        sy = max(0, min(self.h - 1, sy))
        seen = {(sx, sy)}
        q = [(sx, sy)]
        while q:
            x, y = q.pop(0)
            if ok(x, y):
                return x, y
            for nx, ny in neighbors(x, y):
                if self.inb(nx, ny) and (nx, ny) not in seen:
                    seen.add((nx, ny))
                    q.append((nx, ny))
        raise RuntimeError("placement failed")

    def bld(self, x, y, ch, owner=-1):
        if ch == "p":
            def ok(cx, cy):
                if self.g[cy][cx] in ".h":
                    return any(self.inb(nx, ny) and self.g[ny][nx] == SEA
                               for nx, ny in neighbors(cx, cy))
                return False
        else:
            def ok(cx, cy):
                return self.g[cy][cx] in ".h"
        x, y = self._bfs(x, y, ok)
        self.g[y][x] = ch
        if owner >= 0:
            self.owners.append((x, y, owner))
        return x, y

    @staticmethod
    def layer_of(tid):
        """立体化: ユニットの高さ。空/海中/地表海面 の3層（C側 unit_layer と同じ分類）。"""
        if tid in AIRU:
            return "air"
        if tid in SUBU:
            return "under"
        return "surface"

    def unit(self, owner, tid, x, y):
        layer = self.layer_of(tid)

        def ok(cx, cy):
            # 占有は (セル, 高さ) 単位。高さが違えば同じセルに重ねて置ける
            if (cx, cy, layer) in self.unit_pos:
                return False
            ch = self.g[cy][cx]
            if ch in BUILDINGS or ch == VOID:
                return False
            if tid in AIRU:
                return True
            if tid in SEAU:
                return ch == SEA
            if tid in FOOT:
                return ch != SEA
            return ch not in (SEA, "^", "r")
        x, y = self._bfs(x, y, ok)
        self.unit_pos.add((x, y, layer))
        self.units.append((owner, tid, x, y))

    def base(self, x, y, owner, n_city=3, n_fact=2, n_air=2, spread=4):
        hx, hy = self.bld(x, y, "H", owner)
        for i in range(n_fact):
            a = self.rng.uniform(0, 6.283)
            self.bld(hx + int(3 * math.cos(a)), hy + int(2.4 * math.sin(a)),
                     "f", owner)
        for i in range(n_city):
            a = self.rng.uniform(0, 6.283)
            d = self.rng.randint(2, spread)
            self.bld(hx + int(d * math.cos(a)), hy + int(d * 0.8 * math.sin(a)),
                     "c", owner)
        for i in range(n_air):
            a = self.rng.uniform(0, 6.283)
            d = self.rng.randint(2, spread)
            self.bld(hx + int(d * math.cos(a)), hy + int(d * 0.8 * math.sin(a)),
                     "a", owner)
        return hx, hy

    def write(self, fname, name, turns, f0, f1, comment,
              timeout_winner=-1, extra=None):
        lines = [f"# {comment}", "[map]", f"name   = {name}",
                 f"width  = {self.w}", f"height = {self.h}",
                 f"turns  = {turns}", f"timeout_winner = {timeout_winner}",
                 f"funds0 = {f0}", f"funds1 = {f1}"]
        if extra:
            lines += extra
        lines += ["", "[terrain]"]
        lines += ["row = " + "".join(r) for r in self.g]
        lines += ["", "[owners]"]
        lines += [f"own = {x},{y},{o}" for x, y, o in self.owners]
        lines += ["", "[units]"]
        lines += [f"unit = {o},{t},{x},{y}" for o, t, x, y in self.units]
        with open(os.path.join(OUT, fname), "w", encoding="utf-8",
                  newline="\n") as f:
            f.write("\n".join(lines) + "\n")
        play = sum(1 for row in self.g for ch in row if ch not in (VOID,))
        print(f"{fname}: {self.w}x{self.h} 可視ヘクス{play} "
              f"units={len(self.units)}")


# ------------------------------------------------------------------
def m01():  # 六角形の大陸・斜めの丘の尾根
    m = M(32, 22, 101)
    cx, cy = 16, 11
    m.carve(lambda x, y: hexdist(cx, cy, x, y) <= 10)
    # 斜めの尾根
    m.stamp_path([(22, 4), (18, 9), (14, 13), (10, 18)], 1, "h", over=".")
    m.path([(20, 6), (16, 11), (12, 16)], "^", over=".h")
    m.scatter("w", 0.09)
    m.clump("w")
    m.path([(8, 7), (13, 10), (16, 11), (20, 13), (24, 15)], "=", over=".hw")
    hx0, hy0 = m.base(9, 6, 0, spread=4)     # 六角形の北西側
    hx1, hy1 = m.base(23, 16, 1, spread=4)   # 南東側
    m.bld(16, 11, "c")   # 峠の街
    m.bld(22, 7, "c")
    m.bld(10, 15, "c")
    for t in ("INFANTRY", "INFANTRY", "RECON", "TANK", "ARTILLERY"):
        m.unit(0, t, hx0 + 2, hy0 + 1)
        m.unit(1, t, hx1 - 2, hy1 - 1)
    m.write("m01_border_hills.map", "国境の丘", 80, 1700, 1700,
            "m01 国境の丘 - 六角形の大陸。対角に走る丘の尾根が国境線")


def m02():  # 海に浮かぶ斜めの双子島
    m = M(38, 26, 102, fill=SEA)
    # 北西の島と南東の島（斜め配置）
    m.disk(10, 8, 7, ".")
    m.disk(14, 5, 4, ".")
    m.disk(27, 18, 7, ".")
    m.disk(23, 21, 4, ".")
    # 小さな中間の環礁
    m.disk(19, 12, 2, ".")
    m.scatter("w", 0.10)
    m.clump("w")
    hx0, hy0 = m.base(9, 7, 0, n_city=4, spread=4)
    hx1, hy1 = m.base(28, 19, 1, n_city=4, spread=4)
    m.bld(14, 11, "p", 0)
    m.bld(23, 15, "p", 1)
    m.bld(19, 12, "c")    # 環礁の中立都市
    m.bld(19, 11, "p")    # 環礁の中立港
    for t in ("INFANTRY", "INFANTRY", "AT_INFANTRY", "TANK", "RECON"):
        m.unit(0, t, hx0 + 2, hy0 + 1)
        m.unit(1, t, hx1 - 2, hy1 - 1)
    m.unit(0, "BATTLESHIP", 16, 13)
    m.unit(0, "DESTROYER", 15, 14)
    m.unit(0, "SUBMARINE", 12, 16)
    m.unit(0, "MISSILE_BOAT", 17, 12)
    m.unit(1, "BATTLESHIP", 22, 11)
    m.unit(1, "DESTROYER", 23, 12)
    m.unit(1, "SUBMARINE", 26, 9)
    m.unit(1, "MISSILE_BOAT", 21, 13)
    m.write("m02_channel.map", "運河の対岸", 96, 2100, 2100,
            "m02 運河の対岸 - 斜めに向き合う双子島。中間の環礁が要衝")


def m03():  # 群島の海戦 - 盤面の大半が海。小島の港を足場に制海権を奪い合う
    m = M(40, 26, 103, fill=SEA)
    # 両軍の母港島（西=P0 / 東=P1）。陸地は小さく保ち、海戦を主役にする
    m.disk(5, 13, 4, ".")
    m.disk(34, 13, 4, ".")
    # 中央の群島（north/center/south の三線。艦隊の機動と遮蔽になる）
    for ix, iy, r in ((15, 4, 2), (25, 4, 2), (20, 13, 3),
                      (15, 22, 2), (25, 22, 2), (20, 8, 1), (20, 18, 1)):
        m.disk(ix, iy, r, ".")
    m.scatter("w", 0.06)
    m.clump("w")

    hx0, hy0 = m.base(5, 13, 0, n_city=3, n_fact=1, n_air=1, spread=3)
    hx1, hy1 = m.base(34, 13, 1, n_city=3, n_fact=1, n_air=1, spread=3)
    # 母港は各2（艦艇の生産・補給の要）
    m.bld(7, 11, "p", 0); m.bld(7, 15, "p", 0)
    m.bld(32, 11, "p", 1); m.bld(32, 15, "p", 1)
    # 中央諸島の中立拠点（ここを取れば前線で艦を補給・生産できる）
    m.bld(20, 13, "p"); m.bld(20, 14, "c")   # 主島: 港＋都市
    m.bld(15, 4, "p");  m.bld(25, 4, "p")    # 北の環礁
    m.bld(15, 22, "p"); m.bld(25, 22, "p")   # 南の環礁
    m.bld(20, 8, "a");  m.bld(20, 18, "a")   # 洋上の中立飛行場

    # 初期艦隊（両軍対称）。海戦がそのまま勝敗に直結する規模で配置
    fleet = (("BATTLESHIP", 4, 0), ("CARRIER", 3, -2), ("CRUISER", 4, 2),
             ("DESTROYER", 5, -3), ("DESTROYER", 5, 3), ("SUBMARINE", 6, 0),
             ("MISSILE_BOAT", 6, -4), ("GUNBOAT", 6, 4),
             ("SUPPLY_SHIP", 2, 1), ("T_SHIP", 3, 3))
    for t, dx, dy in fleet:
        m.unit(0, t, hx0 + dx, hy0 + dy)
        m.unit(1, t, hx1 - dx, hy1 - dy)
    # 艦載機（空母運用）と上陸用の少数の陸兵
    for t in ("FIGHTER", "DIVE_BOMBER"):
        m.unit(0, t, hx0 + 1, hy0 - 1)
        m.unit(1, t, hx1 - 1, hy1 + 1)
    for t in ("INFANTRY", "INFANTRY", "AT_INFANTRY"):
        m.unit(0, t, hx0, hy0 + 1)
        m.unit(1, t, hx1, hy1 - 1)

    m.write("m03_archipelago.map", "群島の海戦", 100, 3600, 3600,
            "m03 群島の海戦 - 盤面の7割が海。中央の群島の港を奪い制海権を握る")


def m04():  # 三層の要衝 - 空・海面・海中が同じセルで噛み合う立体戦マップ
    """立体化(L5)の見せ場を作るマップ。

    中央を深い海峡が縦に貫き、その上を陸の桟橋が横切る。海峡には潜水艦が潜り、
    海面には艦が浮かび、上空を航空機が飛ぶ——同じセルに3層が同時に居られる状況を
    意図的に多く作ってある。空港と港を近接させ、どの高さで戦うかを選ばせる。
    """
    m = M(44, 28, 407, fill=".")
    # 中央の深い海峡（縦断）。ここが海面と海中の戦場になる
    m.carve(lambda x, y: 19 <= x <= 25, SEA)
    # 海峡を横切る陸の桟橋2本（陸路をつなぎ、艦の頭上を歩兵が渡る）
    m.carve(lambda x, y: 19 <= x <= 25 and 7 <= y <= 8, ".")
    m.carve(lambda x, y: 19 <= x <= 25 and 19 <= y <= 20, ".")
    # 両岸の内海（小さな入り江。潜水艦の隠れ場所）
    m.disk(9, 5, 3, SEA)
    m.disk(34, 22, 3, SEA)
    # 地形の変化
    m.ring(9, 20, 2, 4, "^", over=".")
    m.ring(34, 7, 2, 4, "^", over=".")
    m.scatter("w", 0.07)
    m.clump("w")
    m.scatter("h", 0.05)

    hx0, hy0 = m.base(5, 13, 0, n_city=4, n_fact=2, n_air=2, spread=4)
    hx1, hy1 = m.base(38, 13, 1, n_city=4, n_fact=2, n_air=2, spread=4)
    # 母港（海峡と入り江の両方に面させる）
    m.bld(15, 6, "p", 0); m.bld(15, 20, "p", 0)
    m.bld(28, 6, "p", 1); m.bld(28, 20, "p", 1)
    # 海峡の要衝（中立）。桟橋の上の拠点＝陸で取り合いつつ、下を潜水艦が通る
    m.bld(21, 7, "c");  m.bld(22, 19, "c")
    m.bld(21, 13, "p"); m.bld(22, 13, "a")   # 海峡中央の港と飛行場
    m.bld(9, 5, "p");   m.bld(34, 22, "p")   # 入り江の港

    # --- 初期配置: 同じセルに空/海面/海中を重ねて立体戦の起点を作る ---
    for t in ("INFANTRY", "INFANTRY", "AT_INFANTRY", "TANK", "AA_TANK",
              "ARTILLERY", "SUPPLY"):
        m.unit(0, t, hx0 + 2, hy0)
        m.unit(1, t, hx1 - 2, hy1)
    # 入り江の1セルに 海面・海中・上空 の3層を重ねて置く（立体化の見本）。
    # 建物セルには置けない仕様なので、港そのものではなく隣の海面を使う。
    for owner, (sx, sy) in ((0, (10, 5)), (1, (33, 22))):
        m.unit(owner, "DESTROYER", sx, sy)    # 海面
        m.unit(owner, "SUBMARINE", sx, sy)    # 海中（同じセル）
        m.unit(owner, "FIGHTER",   sx, sy)    # 上空（同じセル）
    # 残りの艦隊は海峡寄りに
    for t, dx, dy in (("CRUISER", 0, 1), ("T_SHIP", 1, 0), ("GUNBOAT", 0, -1)):
        m.unit(0, t, 15 + dx, 6 + dy)
        m.unit(1, t, 28 - dx, 20 - dy)
    for t in ("HELI",):
        m.unit(0, t, 14, 6)
        m.unit(1, t, 29, 20)
    m.unit(0, "T_PLANE", hx0, hy0 - 2)
    m.unit(1, "T_PLANE", hx1, hy1 + 2)

    m.write("m04_layers.map", "三層の要衝", 100, 3400, 3400,
            "m04 三層の要衝 - 中央の海峡で空・海面・海中が同じセルに重なる立体戦")


def c11():  # 制海権の争奪（キャンペーン用・海戦主体）。P0西 vs P1東の島。
    m = M(40, 26, 211, fill=SEA)
    # P0の母港半島（西）と P1の要塞島（東・敵首都）
    m.disk(5, 13, 4, ".")
    m.disk(35, 13, 5, ".")            # 敵は少し大きい島に籠る
    # 中央の島嶼（足がかり）
    for ix, iy, r in ((16, 6, 2), (24, 20, 2), (20, 13, 3), (16, 19, 1), (24, 6, 1)):
        m.disk(ix, iy, r, ".")
    m.scatter("w", 0.06)
    m.clump("w")

    hx0, hy0 = m.base(5, 13, 0, n_city=3, n_fact=1, n_air=1, spread=3)
    hx1, hy1 = m.base(35, 13, 1, n_city=4, n_fact=1, n_air=1, spread=4)
    m.bld(7, 11, "p", 0); m.bld(7, 15, "p", 0)      # P0母港x2
    m.bld(32, 11, "p", 1)                            # 敵港
    # 敵島の防御（上陸を阻む砲台役の陸兵）
    m.bld(20, 13, "p"); m.bld(20, 14, "c")          # 中央主島: 中立港＋都市
    m.bld(16, 6, "a"); m.bld(24, 20, "p")           # 中立飛行場・港

    # P0はやや優勢な艦隊＋上陸部隊、P1は防御的な艦隊＋島の守備隊
    p0fleet = (("BATTLESHIP", 4, 0), ("CARRIER", 3, -2), ("CRUISER", 4, 2),
               ("DESTROYER", 5, -3), ("DESTROYER", 5, 3), ("SUBMARINE", 6, -1),
               ("GUNBOAT", 6, 3), ("SUPPLY_SHIP", 2, 1),
               ("T_SHIP", 3, 2), ("T_SHIP", 3, 4))
    for t, dx, dy in p0fleet:
        m.unit(0, t, hx0 + dx, hy0 + dy)
    for t in ("FIGHTER", "DIVE_BOMBER", "FIGHTER"):
        m.unit(0, t, hx0 + 1, hy0 - 1)
    for t in ("INFANTRY", "INFANTRY", "AT_INFANTRY", "TANK", "MECH_INF", "AA_TANK"):
        m.unit(0, t, hx0, hy0 + 1)    # 上陸させる部隊
    p1fleet = (("BATTLESHIP", 5, 0), ("CRUISER", 6, -2), ("DESTROYER", 6, 2),
               ("SUBMARINE", 7, -1), ("MISSILE_BOAT", 7, 2), ("GUNBOAT", 6, 3))
    for t, dx, dy in p1fleet:
        m.unit(1, t, hx1 - dx, hy1 - dy)
    for t in ("FIGHTER", "BOMBER"):
        m.unit(1, t, hx1 - 1, hy1 + 1)
    for t in ("INFANTRY", "AT_INFANTRY", "AA_TANK", "ARTILLERY"):
        m.unit(1, t, hx1, hy1 - 1)    # 島の守備隊
    m.write("c11_seacontrol.map", "制海権の争奪", 90, 4000, 3800,
            "c11 制海権の争奪 - 東の要塞島の敵首都へ、制海権を握り上陸せよ")


def c12():  # 敵首都上陸（最終・大規模な上陸戦）。西の海から東岸の敵首都へ。
    m = M(44, 26, 212, fill=SEA)
    # 西=外洋(P0発進)、東2/3=敵本土の海岸線。中央に上陸拠点の小島。
    m.carve(lambda x, y: x >= 24, ".")               # 東側=敵本土
    m.disk(6, 13, 4, ".")                             # P0の泊地の小島
    m.disk(17, 8, 2, "."); m.disk(17, 18, 2, ".")    # 中央の橋頭堡候補
    m.scatter("w", 0.07)
    m.clump("w")
    # 海岸に沿った川と、内陸の敵首都
    m.path([(24, 2), (26, 9), (25, 16), (27, 23)], "r", over=".w")

    hx0, hy0 = m.base(6, 13, 0, n_city=2, n_fact=1, n_air=1, spread=3)
    hx1, hy1 = m.base(38, 13, 1, n_city=5, n_fact=2, n_air=2, spread=5)
    m.bld(9, 11, "p", 0); m.bld(9, 15, "p", 0)       # P0泊地の港x2
    m.bld(17, 8, "p"); m.bld(17, 18, "p")            # 中立の上陸拠点港
    m.bld(28, 7, "c"); m.bld(28, 19, "c")            # 海岸の敵都市
    m.bld(31, 13, "a", 1)                            # 敵飛行場

    # P0: 強力な上陸船団＋護衛艦隊
    p0fleet = (("BATTLESHIP", 4, 0), ("BATTLESHIP", 4, 3), ("CARRIER", 3, -2),
               ("CRUISER", 5, -3), ("DESTROYER", 5, 2), ("DESTROYER", 5, 4),
               ("GUNBOAT", 6, -1), ("GUNBOAT", 6, 1), ("SUPPLY_SHIP", 2, 1),
               ("T_SHIP", 3, -1), ("T_SHIP", 3, 3), ("T_SHIP", 4, 5))
    for t, dx, dy in p0fleet:
        m.unit(0, t, hx0 + dx, hy0 + dy)
    for t in ("FIGHTER", "FIGHTER", "DIVE_BOMBER", "BOMBER"):
        m.unit(0, t, hx0 + 1, hy0 - 2)
    for t in ("INFANTRY", "INFANTRY", "AT_INFANTRY", "MECH_INF",
              "TANK", "TANK", "AA_TANK", "ARTILLERY"):
        m.unit(0, t, hx0, hy0 + 1)    # 上陸部隊
    # P1: 沿岸防御（首都を固める陸上戦力＋少数の艦）
    for t in ("HTANK", "TANK", "AA_TANK", "ARTILLERY", "ROCKET",
              "AT_INFANTRY", "INFANTRY", "AA_GUN"):
        m.unit(1, t, hx1 - 2, hy1)
    m.unit(1, "ARTILLERY", 29, 7); m.unit(1, "ROCKET", 29, 19)  # 海岸砲
    for t in ("FIGHTER", "BOMBER", "HELI"):
        m.unit(1, t, hx1 - 1, hy1 - 2)
    for t in ("DESTROYER", "CRUISER", "MISSILE_BOAT"):
        m.unit(1, t, 33, 13)          # 沿岸の敵艦
    m.write("c12_dday.map", "敵首都上陸", 96, 4500, 4200,
            "c12 敵首都上陸 - 西の海から東岸の敵首都へ。史上最大の上陸作戦")


def c01():  # L字型の国境地帯
    m = M(26, 19, 201)
    m.carve(lambda x, y: x < 10 or y > 11)   # 縦棒 + 横棒 = L
    m.scatter("w", 0.10)
    m.clump("w")
    m.path([(4, 3), (5, 9), (7, 14), (14, 15), (21, 15)], "=", over=".w")
    hx0, hy0 = m.base(4, 3, 0, n_city=2, n_air=2, spread=3)   # Lの上端
    hx1, hy1 = m.base(22, 15, 1, n_city=2, n_air=1, spread=3) # Lの右端
    m.bld(6, 14, "c")    # Lの角（激戦地）
    m.bld(5, 15, "c")
    m.bld(13, 14, "c")
    m.bld(4, 8, "c")
    for t in ("INFANTRY", "INFANTRY", "RECON", "TANK"):
        m.unit(0, t, hx0 + 1, hy0 + 2)
    for t in ("INFANTRY", "INFANTRY", "RECON", "AT_INFANTRY"):
        m.unit(1, t, hx1 - 2, hy1)
    m.write("c01_border.map", "国境の哨戒線", 40, 1250, 1250,
            "c01 国境の哨戒線 - L字型の国境地帯。角の村を巡る遭遇戦")


def c02():  # 十字型の森
    m = M(29, 19, 202)
    cx, cy = 14, 9
    m.carve(lambda x, y: abs(x - cx) <= 4 or abs(y - cy) <= 3)
    m.scatter("w", 0.34)
    m.clump("w")
    m.disk(4, 9, 2, ".", over="w")     # 西の空き地（自軍）
    m.disk(25, 9, 2, ".", over="w")    # 東の空き地（敵）
    m.disk(cx, cy, 2, ".", over="w")   # 中央交差点
    m.path([(6, 9), (cx, cy), (23, 9)], "=", over=".w")
    hx0, hy0 = m.base(4, 9, 0, n_city=3, spread=3)
    hx1, hy1 = m.base(25, 9, 1, n_city=2, spread=3)
    m.bld(cx, cy, "c")     # 交差点の街
    m.bld(cx, 3, "c")      # 北腕の街
    m.bld(cx, 15, "c")     # 南腕の街
    for t in ("INFANTRY", "AT_INFANTRY", "ARTILLERY", "TANK"):
        m.unit(0, t, hx0 + 1, hy0)
    for t in ("TANK", "TANK", "INFANTRY", "INFANTRY", "ARTILLERY", "RECON"):
        m.unit(1, t, hx1 - 2, hy1)
    m.write("c02_forest.map", "森林の防衛線", 29, 1000, 1950,
            "c02 森林の防衛線 - 十字型の森。中央交差点を封鎖して守る",
            timeout_winner=0)


def c03():  # 蝶ネクタイ型の両大陸
    m = M(35, 22, 203, fill=SEA)
    # 左右の三角形の大陸（中央に向かって細くなる）
    for y in range(m.h):
        t = abs(y - 11) / 11.0
        wl = int(13 - 9 * (1 - t))    # 中央の行ほど幅が狭い=くびれ
        for x in range(0, max(3, wl)):
            m.set(x, y, ".")
        for x in range(m.w - max(3, wl), m.w):
            m.set(x, y, ".")
    m.disk(17, 11, 1, ".")            # くびれの間の小島
    m.scatter("w", 0.09)
    m.clump("w")
    hx0, hy0 = m.base(4, 11, 0, n_city=4, spread=4)
    hx1, hy1 = m.base(30, 11, 1, n_city=4, spread=4)
    # 母港は司令部のすぐ隣（海峡側の海岸）に南北2つずつ。
    # ここで地上部隊を輸送艦に載せて対岸へ渡す。くびれ(row11)は陸が細く
    # BFS が遠方へ流れるため、あえて海岸のある y=9/13 を指定する。
    m.bld(4, 9, "p", 0)
    m.bld(4, 13, "p", 0)
    m.bld(30, 9, "p", 1)
    m.bld(30, 13, "p", 1)
    m.bld(17, 11, "p")               # 小島の中立港
    m.bld(17, 10, "c")               # 小島の中立都市
    m.bld(5, 2, "c")
    m.bld(29, 19, "c")
    for t in ("INFANTRY", "INFANTRY", "AT_INFANTRY", "TANK"):
        m.unit(0, t, hx0 + 2, hy0)
    for t in ("INFANTRY", "INFANTRY", "TANK", "AA_TANK"):
        m.unit(1, t, hx1 - 2, hy1)
    m.unit(0, "T_SHIP", 12, 9)
    m.unit(0, "DESTROYER", 13, 14)
    m.unit(0, "GUNBOAT", 11, 11)          # 上陸支援の砲艦
    m.unit(0, "CARRIER", 10, 12)          # 艦載機を運ぶ空母
    m.unit(0, "FIGHTER", hx0 + 3, hy0 - 2)
    m.unit(1, "DESTROYER", 22, 8)
    m.unit(1, "CRUISER", 23, 9)
    m.unit(1, "FIGHTER", hx1 - 3, hy1 - 2)
    m.unit(1, "BOMBER", hx1 - 2, hy1 - 3)
    m.write("c03_channel.map", "海峡横断", 64, 2250, 2250,
            "c03 海峡横断 - 蝶ネクタイ型の両大陸。くびれの小島が足がかり")


def c04():  # 三角形の大陸・頂点に敵首都
    m = M(32, 22, 204)
    # 頂点(16,1) から下に広がる三角形
    m.carve(lambda x, y: abs(x - 16) <= 1 + int(y * 0.72))
    m.scatter("w", 0.10)
    m.clump("w")
    # 頂点の敵首都を守る山の壁（2つの門）
    m.path([(11, 8), (14, 6), (18, 6), (21, 8)], "^", over=".w")
    m.set(13, 7, ".", over="^")
    m.set(19, 7, ".", over="^")
    m.path([(3, 19), (10, 15), (13, 7)], "=", over=".w")
    m.path([(29, 19), (22, 15), (19, 7)], "=", over=".w")
    m.path([(5, 20), (16, 19), (27, 20)], "r", over=".w")   # 底辺の川
    hx0, hy0 = m.base(16, 18, 0, n_city=4, spread=5)        # 底辺の中央
    m.bld(16, 3, "H", 1)
    m.bld(15, 4, "f", 1)
    m.bld(17, 4, "f", 1)
    m.bld(16, 5, "c", 1)
    m.bld(14, 3, "c", 1)
    m.bld(18, 3, "a", 1)
    m.bld(16, 9, "a", 1)
    m.bld(9, 13, "c")     # 中立: 左斜面
    m.bld(23, 13, "c")    # 中立: 右斜面
    m.bld(16, 12, "c")    # 中立: 中央
    for t in ("INFANTRY", "INFANTRY", "RECON", "TANK", "ARTILLERY"):
        m.unit(0, t, hx0 - 3, hy0 - 1)
    for t in ("HTANK", "TANK", "AA_TANK", "ARTILLERY", "INFANTRY"):
        m.unit(1, t, 16, 7)
    m.unit(1, "FIGHTER", 14, 5)
    m.write("c04_capital.map", "首都攻略", 72, 3100, 3350,
            "c04 首都攻略 - 三角形の大陸。頂点の敵首都へ2本の登り道")


def c05():  # 稲妻(Z字)型の回廊
    m = M(32, 19, 205)
    m.stamp_path([(3, 2), (13, 3)], 2, ".")
    m.stamp_path([(13, 3), (9, 9), (18, 10)], 2, ".")
    m.stamp_path([(18, 10), (14, 16), (28, 16)], 2, ".")
    m.scatter("w", 0.10)
    m.clump("w")
    # 川が中段の回廊を横切る（橋1本）
    m.path([(13, 6), (14, 9), (13, 13)], "r", over=".w")
    m.set(14, 10, "=", over="r.")
    m.path([(10, 9), (14, 10), (17, 10)], "=", over=".w")
    hx0, hy0 = m.base(4, 2, 0, n_city=3, spread=3)     # 稲妻の上端
    hx1, hy1 = m.base(27, 16, 1, n_city=3, spread=3)   # 下端
    m.bld(12, 3, "c")     # 折れ目1
    m.bld(17, 10, "c")    # 折れ目2（中央）
    m.bld(9, 9, "c")
    m.bld(15, 16, "c")
    for t in ("INFANTRY", "INFANTRY", "TANK", "ARTILLERY", "RECON"):
        m.unit(0, t, hx0 + 2, hy0 + 1)
        m.unit(1, t, hx1 - 2, hy1 - 1)
    m.write("c05_river.map", "河川突破", 48, 1400, 1400,
            "c05 河川突破 - 稲妻型の回廊。中段の渡河点がボトルネック")


def c06():  # 鉤(フック)型の半島
    m = M(29, 19, 206, fill=SEA)
    # 南の陸塊から西→北→東へ曲がるフック
    m.carve(lambda x, y: y >= 13)                       # 南の大陸（敵）
    m.stamp_path([(5, 13), (4, 7), (7, 3), (13, 2)], 2, ".")   # フックの柄
    m.disk(15, 3, 2, ".")                               # フックの先端
    m.scatter("w", 0.10)
    m.clump("w")
    hx0, hy0 = m.base(14, 3, 0, n_city=3, spread=3)     # 先端に籠る
    hx1, hy1 = m.base(22, 16, 1, n_city=3, spread=3)    # 南の大陸
    m.bld(4, 8, "p", 0)
    m.bld(12, 2, "p", 0)
    m.bld(5, 11, "c")     # フックの付け根（中立・防衛ライン）
    m.bld(4, 5, "c")
    m.bld(10, 16, "c")
    for t in ("INFANTRY", "INFANTRY", "AT_INFANTRY", "ARTILLERY", "TANK"):
        m.unit(0, t, hx0 - 1, hy0 + 1)
    m.unit(0, "DESTROYER", 9, 7)
    for t in ("TANK", "TANK", "HTANK", "INFANTRY", "ARTILLERY"):
        m.unit(1, t, hx1 - 3, hy1 - 1)
    m.unit(1, "BOMBER", hx1, hy1 - 2)
    m.write("c06_beachhead.map", "橋頭堡の死守", 26, 1100, 2250,
            "c06 橋頭堡の死守 - 鉤型の半島。付け根を封じて先端を守り抜く",
            timeout_winner=0)


def c07():  # ドーナツ型の環状谷
    m = M(32, 19, 207)
    cx, cy = 16, 9
    m.ring(cx, cy, 5, 8, ".")
    m.disk(cx, cy, 4, "^")            # 中心の山塊
    m.scatter("w", 0.12)
    m.clump("w")
    # 環状路
    hx0, hy0 = m.base(8, 9, 0, n_city=2, spread=2)     # 西の環上
    hx1, hy1 = m.base(24, 9, 1, n_city=2, spread=2)    # 東の環上
    m.bld(16, 2, "c")    # 北の環上（中立）
    m.bld(16, 16, "c")   # 南の環上（中立）
    m.bld(12, 4, "c")
    m.bld(20, 14, "c")
    for t in ("INFANTRY", "INFANTRY", "TANK", "ARTILLERY", "AT_INFANTRY"):
        m.unit(0, t, hx0, hy0 + 2)
        m.unit(1, t, hx1, hy1 - 2)
    m.write("c07_pass.map", "山岳の隘路", 56, 1550, 1550,
            "c07 山岳の隘路 - ドーナツ型の環状谷。北回りか南回りか")


def c08():  # Y字型の三叉空域
    m = M(32, 19, 208)
    cx, cy = 16, 10
    m.stamp_path([(4, 3), (cx, cy)], 2, ".")       # 左上腕（自軍）
    m.stamp_path([(28, 3), (cx, cy)], 2, ".")      # 右上腕（敵軍）
    m.stamp_path([(16, 17), (cx, cy)], 2, ".")     # 下腕（中立）
    m.scatter("w", 0.08)
    m.clump("w")
    hx0, hy0 = m.base(5, 3, 0, n_city=2, n_air=2, spread=3)
    hx1, hy1 = m.base(27, 3, 1, n_city=2, n_air=2, spread=3)
    m.bld(cx, cy, "a")        # 三叉の中心（最重要飛行場）
    m.bld(16, 16, "a")        # 下腕の先端
    m.bld(15, 17, "c")
    m.bld(12, 7, "a")         # 左腕の中間
    m.bld(20, 7, "a")         # 右腕の中間
    for t in ("FIGHTER", "HELI", "INFANTRY", "INFANTRY", "AA_TANK"):
        m.unit(0, t, hx0 + 2, hy0 + 1)
        m.unit(1, t, hx1 - 2, hy1 + 1)
    m.write("c08_airfields.map", "空の要衝", 56, 1800, 1800,
            "c08 空の要衝 - Y字の三叉空域。中心の飛行場を制した側が空を制す")


def c09():  # 斜めに走る帯状の市街地
    m = M(35, 19, 209)
    # 左下→右上の斜め帯
    def band(x, y):
        d = (x / m.w) + ((m.h - 1 - y) / m.h)   # 対角方向の位置
        return 0.55 <= d <= 1.45
    m.carve(band)
    m.scatter("w", 0.05)
    # 帯に沿った大通りと市街ブロック
    m.path([(3, 16), (10, 13), (17, 9), (24, 6), (31, 2)], "=", over=".w")
    blocks = [(7, 14), (10, 15), (12, 11), (15, 12), (16, 8),
              (19, 9), (21, 5), (24, 7), (13, 15), (22, 9)]
    for bx, by in blocks:
        m.bld(bx, by, "c")
    hx0, hy0 = m.base(4, 17, 0, n_city=2, spread=3)    # 帯の左下端
    hx1, hy1 = m.base(30, 2, 1, n_city=2, spread=3)    # 帯の右上端
    for t in ("INFANTRY", "INFANTRY", "TRUCK", "TANK", "RECON"):
        m.unit(0, t, hx0 + 2, hy0 - 1)
        m.unit(1, t, hx1 - 2, hy1 + 1)
    p0 = sum(1 for _, _, o in m.owners if o == 0)
    m.write("c09_cities.map", "補給都市の攻防", 64, 1700, 1700,
            "c09 補給都市の攻防 - 斜めに走る帯状の市街地を端から端まで奪い合う",
            extra=[f"objective_count  = {p0 + 10}", "objective_player = 0"])


def c10():  # 巨大な楕円の大平原
    m = M(38, 22, 210)
    cx, cy = 18, 11
    m.carve(lambda x, y: ((x - cx) / 17.5) ** 2 + ((y - cy) / 9.5) ** 2 <= 1.0)
    m.scatter("w", 0.03)
    m.ring(24, 11, 3, 4, "h", over=".")   # 三日月の丘（東寄り）
    for y in range(m.h):                  # 東半分を削って三日月に
        for x in range(25, m.w):
            if m.get(x, y) == "h":
                m.set(x, y, ".")
    m.path([(3, 11), (12, 11), (20, 11), (28, 11), (34, 11)], "=", over=".hw")
    hx0, hy0 = m.base(4, 11, 0, n_city=4, spread=4)
    hx1, hy1 = m.base(33, 11, 1, n_city=4, spread=4)
    m.bld(18, 5, "c")
    m.bld(18, 17, "c")
    m.bld(24, 11, "c")
    for t in ("TANK", "TANK", "HTANK", "ARTILLERY", "INFANTRY", "INFANTRY",
              "AA_TANK", "RECON", "SUPPLY"):
        m.unit(0, t, hx0 + 2, hy0)
        m.unit(1, t, hx1 - 2, hy1)
    m.unit(0, "SUPPLY_AIR", hx0, hy0 - 3)
    m.unit(1, "SUPPLY_AIR", hx1, hy1 + 3)
    m.write("c10_plains.map", "平原の決戦", 80, 2800, 2800,
            "c10 平原の決戦 - 巨大な楕円の大平原。遮蔽は三日月の丘だけ")


ALL = (m01, m02, m03, m04, c11, c12,
       c01, c02, c03, c04, c05, c06, c07, c08, c09, c10)

if __name__ == "__main__":
    import sys
    # 引数を付けるとそのマップだけ生成する（例: python tools/gen_maps.py m04）。
    # 引数なしは全再生成。**既存マップは scale_maps.py で拡大済みなので、
    # 全再生成すると小さいサイズに戻る**（拡大したいときは gen → scale の順に実行）。
    want = set(sys.argv[1:])
    for fn in ALL:
        if not want or fn.__name__ in want:
            fn()
    print("OK")
