# -*- coding: utf-8 -*-
"""地形セル画像のサンプルを生成する（要 Pillow）。

    python tools/gen_terrain_gfx.py

assets/gfx/terrain/*.png を作る。terrain.def の `image =` から参照される。

**すべて「真上から見た地面の模様」として描くこと。**
斜め見下ろし表示では画像が縦0.6倍に潰れる。地面の模様なら潰れても自然に見える
（奥行きで縮んでいるだけに見える）が、木や建物を横から見た形で描くと上下に
潰れて不自然になる。森は樹冠を、街は屋根を、山は岩肌を、それぞれ真上から
見た形で描いている。

描画側は六角形の内側にクリップして貼るので、正方形PNGで全面を塗ればよい。
基調色は terrain.def の color と合わせてある（タイル側面＝崖はその color で
塗られるため、ずらすと天面と側面が食い違って見える）。
"""
import math
import os
import random

from PIL import Image, ImageDraw, ImageFilter

OUT = os.path.join(os.path.dirname(__file__), "..", "assets", "gfx", "terrain")
S = 192  # 1辺（正方形）


def canvas(base):
    img = Image.new("RGBA", (S, S), base + (255,))
    return img, ImageDraw.Draw(img, "RGBA")


def save(img, name):
    os.makedirs(OUT, exist_ok=True)
    img.save(os.path.join(OUT, name + ".png"))
    print(name)


def grain(d, col, n, rmin, rmax, seed):
    """細かい粒を撒いて単色っぽさを消す（真上から見た地面のざらつき）。"""
    rnd = random.Random(seed)
    for _ in range(n):
        x, y = rnd.uniform(0, S), rnd.uniform(0, S)
        r = rnd.uniform(rmin, rmax)
        d.ellipse([x - r, y - r, x + r, y + r], fill=col)


def plain():
    img, d = canvas((156, 191, 107))
    grain(d, (170, 203, 120, 190), 240, 2, 6, 1)
    grain(d, (138, 174, 94, 150), 190, 2, 5, 2)
    grain(d, (182, 210, 132, 120), 90, 1, 3, 3)     # 草の穂
    save(img, "plain")


def road():
    img, d = canvas((156, 191, 107))
    grain(d, (138, 174, 94, 150), 130, 2, 5, 4)
    d.polygon([(0, S * 0.34), (S, S * 0.28), (S, S * 0.72), (0, S * 0.66)],
              fill=(201, 183, 156, 255))            # 舗装（真上）
    grain(d, (188, 170, 144, 120), 60, 2, 5, 5)
    for i in range(5):                              # センターライン
        x = S * (0.08 + i * 0.2)
        d.rounded_rectangle([x, S * 0.46, x + S * 0.1, S * 0.51], 4,
                            fill=(228, 217, 194, 255))
    save(img, "road")


def forest():
    """樹冠を真上から見た形。幹は描かない（横から見た形にしないため）。"""
    img, d = canvas((78, 112, 66))                  # 林床（影）
    grain(d, (68, 100, 58, 170), 150, 4, 9, 6)
    rnd = random.Random(7)
    blobs = []
    for _ in range(15):
        blobs.append((rnd.uniform(14, S - 14), rnd.uniform(14, S - 14),
                      rnd.uniform(22, 34)))
    for cx, cy, r in blobs:                         # 影を先に（重なりを出す）
        d.ellipse([cx - r, cy - r * 0.94, cx + r, cy + r], fill=(58, 88, 52, 255))
    for cx, cy, r in blobs:                         # 樹冠
        d.ellipse([cx - r * 0.86, cy - r * 0.86, cx + r * 0.78, cy + r * 0.78],
                  fill=(90, 138, 82, 255))
        d.ellipse([cx - r * 0.5, cy - r * 0.62, cx + r * 0.24, cy + r * 0.06],
                  fill=(108, 158, 96, 255))         # 陽の当たる側
    save(img, "forest")


def mountain():
    """岩肌を真上から見た形。稜線は面の色差で表す（横向きの三角は描かない）。"""
    img, d = canvas((141, 133, 120))
    rnd = random.Random(8)
    for _ in range(16):                             # 岩の面（多角形）
        cx, cy = rnd.uniform(0, S), rnd.uniform(0, S)
        r = rnd.uniform(26, 52)
        n = rnd.randint(4, 6)
        a0 = rnd.uniform(0, 6.28)
        pts = [(cx + r * math.cos(a0 + 6.283 * i / n) * rnd.uniform(0.6, 1.0),
                cy + r * math.sin(a0 + 6.283 * i / n) * rnd.uniform(0.6, 1.0))
               for i in range(n)]
        g = rnd.randint(-18, 20)
        d.polygon(pts, fill=(141 + g, 133 + g, 120 + g, 255))
    for _ in range(5):                              # 残雪（真上から見た斑）
        cx, cy = rnd.uniform(20, S - 20), rnd.uniform(20, S - 20)
        r = rnd.uniform(14, 26)
        n = rnd.randint(5, 7)
        pts = [(cx + r * math.cos(6.283 * i / n) * rnd.uniform(0.55, 1.0),
                cy + r * math.sin(6.283 * i / n) * rnd.uniform(0.55, 1.0))
               for i in range(n)]
        d.polygon(pts, fill=(228, 230, 230, 235))
    grain(d, (120, 113, 102, 130), 120, 2, 5, 9)    # 砂礫
    save(img, "mountain")


def hill():
    """乾いた草地を真上から。等高線状のむらで起伏を示す。"""
    img, d = canvas((168, 162, 118))
    rnd = random.Random(10)
    for k in range(4):                              # 等高線（同心の崩れた輪）
        r = S * (0.46 - k * 0.09)
        pts = []
        for i in range(24):
            a = 6.283 * i / 24
            rr = r * rnd.uniform(0.88, 1.12)
            pts.append((S / 2 + rr * math.cos(a), S / 2 + rr * math.sin(a) * 0.96))
        d.polygon(pts, fill=(168 + 8 * (k + 1), 162 + 8 * (k + 1),
                             118 + 7 * (k + 1), 255))
    grain(d, (152, 147, 105, 150), 180, 2, 6, 11)
    grain(d, (190, 184, 138, 110), 90, 1, 4, 12)
    save(img, "hill")


def water(base, light, seed, name):
    img, d = canvas(base)
    grain(d, light + (80,), 130, 6, 16, seed)
    rnd = random.Random(seed + 1)
    for _ in range(18):                             # さざ波（真上）
        x, y = rnd.uniform(4, S - 46), rnd.uniform(4, S - 8)
        w = rnd.uniform(24, 46)
        d.arc([x, y, x + w, y + 11], 200, 340, fill=light + (190,), width=3)
    img = img.filter(ImageFilter.GaussianBlur(0.7))
    save(img, name)


def city():
    """屋根を真上から。壁や窓は描かない（横から見た形にしないため）。"""
    img, d = canvas((150, 148, 142))                # 路面
    grain(d, (138, 136, 130, 150), 110, 3, 7, 13)
    rnd = random.Random(14)
    roofs = [(0.06, 0.06, 0.40, 0.40), (0.52, 0.04, 0.42, 0.34),
             (0.05, 0.54, 0.34, 0.40), (0.46, 0.46, 0.30, 0.28),
             (0.80, 0.44, 0.18, 0.50), (0.44, 0.80, 0.34, 0.18)]
    for rx, ry, rw, rh in roofs:
        x0, y0 = S * rx, S * ry
        x1, y1 = x0 + S * rw, y0 + S * rh
        base = rnd.choice([(196, 194, 186), (208, 206, 198), (182, 180, 174)])
        d.rectangle([x0, y0, x1, y1], fill=base + (255,))
        d.rectangle([x0, y0, x1, y1], outline=(126, 124, 120, 255), width=2)
        for _ in range(2):                          # 屋上の設備
            ux = rnd.uniform(x0 + 6, max(x0 + 7, x1 - 18))
            uy = rnd.uniform(y0 + 6, max(y0 + 7, y1 - 18))
            d.rectangle([ux, uy, ux + 12, uy + 12], fill=(160, 158, 152, 255))
    save(img, "city")


def factory():
    """のこぎり屋根を真上から見た平行の稜線として描く。"""
    img, d = canvas((150, 138, 126))
    grain(d, (138, 127, 116, 150), 100, 3, 7, 15)
    d.rectangle([S * 0.06, S * 0.16, S * 0.94, S * 0.74], fill=(178, 166, 152, 255))
    d.rectangle([S * 0.06, S * 0.16, S * 0.94, S * 0.74],
                outline=(120, 110, 100, 255), width=2)
    for i in range(6):                              # 屋根の稜線
        y = S * (0.19 + i * 0.093)
        d.rectangle([S * 0.07, y, S * 0.93, y + S * 0.028],
                    fill=(200, 190, 176, 255))
        d.rectangle([S * 0.07, y + S * 0.028, S * 0.93, y + S * 0.052],
                    fill=(150, 140, 128, 255))
    for cx in (0.20, 0.44):                         # タンク（真上＝円）
        d.ellipse([S * cx, S * 0.80, S * (cx + 0.15), S * 0.95],
                  fill=(166, 158, 148, 255), outline=(120, 112, 104, 255), width=2)
    save(img, "factory")


def airport():
    img, d = canvas((150, 152, 160))
    grain(d, (140, 142, 150, 150), 110, 3, 7, 16)
    d.polygon([(0, S * 0.30), (S, S * 0.25), (S, S * 0.75), (0, S * 0.70)],
              fill=(178, 180, 190, 255))            # 滑走路（真上）
    for i in range(6):                              # 中心線
        x = S * (0.05 + i * 0.16)
        d.rectangle([x, S * 0.49, x + S * 0.09, S * 0.53],
                    fill=(236, 238, 244, 255))
    d.rectangle([0, S * 0.30, S, S * 0.325], fill=(206, 208, 216, 255))
    d.rectangle([0, S * 0.675, S, S * 0.70], fill=(206, 208, 216, 255))
    save(img, "airport")


def port():
    img, d = canvas((92, 138, 176))                 # 海面
    grain(d, (120, 164, 200, 110), 90, 5, 13, 17)
    d.polygon([(0, 0), (S, 0), (S, S * 0.44), (0, S * 0.52)],
              fill=(168, 172, 178, 255))            # 岸壁（真上）
    grain(d, (156, 160, 166, 130), 70, 3, 7, 18)
    d.polygon([(0, S * 0.44), (S, S * 0.37), (S, S * 0.44), (0, S * 0.52)],
              fill=(132, 136, 142, 255))            # 岸のふち
    for i in range(4):                              # 係船柱（真上＝円）
        x = S * (0.10 + i * 0.25)
        y = S * (0.34 - i * 0.018)
        d.ellipse([x, y, x + 13, y + 13], fill=(96, 100, 106, 255))
    save(img, "port")


def hq():
    """大きな屋根と中庭を真上から。"""
    img, d = canvas((198, 182, 92))
    grain(d, (186, 170, 84, 150), 100, 3, 7, 19)
    d.rectangle([S * 0.10, S * 0.12, S * 0.90, S * 0.88], fill=(224, 208, 118, 255))
    d.rectangle([S * 0.10, S * 0.12, S * 0.90, S * 0.88],
                outline=(150, 136, 62, 255), width=3)
    d.rectangle([S * 0.34, S * 0.36, S * 0.66, S * 0.64],
                fill=(186, 170, 84, 255))           # 中庭
    r = S * 0.11                                     # 中庭の星章
    cx, cy = S * 0.5, S * 0.5
    pts = []
    for i in range(10):
        rr = r if i % 2 == 0 else r * 0.45
        a = math.radians(-90 + i * 36)
        pts.append((cx + rr * math.cos(a), cy + rr * math.sin(a)))
    d.polygon(pts, fill=(240, 228, 150, 255))
    save(img, "hq")


if __name__ == "__main__":
    plain(); road(); forest(); mountain(); hill()
    water((74, 127, 181), (126, 176, 220), 20, "sea")
    water((127, 178, 217), (176, 214, 240), 30, "river")
    city(); factory(); airport(); port(); hq()
