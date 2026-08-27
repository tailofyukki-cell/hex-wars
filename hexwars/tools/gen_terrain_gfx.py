# -*- coding: utf-8 -*-
"""地形セル画像のサンプルを生成する（要 Pillow）。

    python tools/gen_terrain_gfx.py

assets/gfx/terrain/*.png を作る。terrain.def の `image =` から参照される。
描画側は**六角形の内側にクリップして**貼るので、画像は正方形で全面を塗り、
六角形からはみ出す四隅は見えなくなる前提で描いてよい。
差し替えるときは同じサイズ・正方形の PNG を上書きすればよい（再ビルド不要）。
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
    """細かい粒を撒いて単色っぽさを消す。"""
    rnd = random.Random(seed)
    for _ in range(n):
        x, y = rnd.uniform(0, S), rnd.uniform(0, S)
        r = rnd.uniform(rmin, rmax)
        d.ellipse([x - r, y - r, x + r, y + r], fill=col)


def plain():
    img, d = canvas((156, 191, 107))
    grain(d, (168, 201, 118, 190), 260, 2, 6, 1)
    grain(d, (140, 176, 96, 150), 180, 2, 5, 2)
    save(img, "plain")


def road():
    img, d = canvas((156, 191, 107))
    grain(d, (140, 176, 96, 150), 120, 2, 5, 3)
    d.polygon([(0, S * 0.36), (S, S * 0.30), (S, S * 0.70), (0, S * 0.64)],
              fill=(201, 183, 156, 255))
    for i in range(5):                       # センターライン
        x = S * (0.08 + i * 0.2)
        d.rounded_rectangle([x, S * 0.47, x + S * 0.1, S * 0.52], 4,
                            fill=(226, 214, 190, 255))
    save(img, "road")


def forest():
    img, d = canvas((110, 150, 84))
    grain(d, (98, 138, 74, 160), 140, 3, 7, 4)
    rnd = random.Random(5)
    for _ in range(11):                      # 樹冠
        cx, cy = rnd.uniform(20, S - 20), rnd.uniform(24, S - 16)
        r = rnd.uniform(18, 27)
        d.ellipse([cx - r * 0.5, cy - 4, cx + r * 0.5, cy + r * 0.7],
                  fill=(74, 58, 42, 255))    # 幹
        d.polygon([(cx, cy - r), (cx + r * 0.8, cy + r * 0.4),
                   (cx - r * 0.8, cy + r * 0.4)], fill=(85, 136, 79, 255))
        d.polygon([(cx, cy - r * 0.6), (cx + r * 0.62, cy + r * 0.5),
                   (cx - r * 0.62, cy + r * 0.5)], fill=(99, 154, 90, 255))
    save(img, "forest")


def mountain():
    img, d = canvas((141, 133, 120))
    grain(d, (128, 121, 110, 170), 150, 3, 7, 6)
    d.polygon([(S * 0.5, 26), (S * 0.94, S - 22), (S * 0.06, S - 22)],
              fill=(122, 115, 104, 255))
    d.polygon([(S * 0.5, 26), (S * 0.72, S - 22), (S * 0.28, S - 22)],
              fill=(158, 150, 137, 255))
    d.polygon([(S * 0.5, 26), (S * 0.63, S * 0.44), (S * 0.37, S * 0.44)],
              fill=(232, 232, 228, 255))     # 冠雪
    save(img, "mountain")


def hill():
    img, d = canvas((168, 162, 118))
    grain(d, (154, 149, 106, 160), 170, 3, 7, 7)
    d.ellipse([S * 0.02, S * 0.34, S * 0.62, S * 0.98], fill=(182, 176, 130, 255))
    d.ellipse([S * 0.40, S * 0.46, S * 0.99, S * 1.02], fill=(160, 154, 112, 255))
    save(img, "hill")


def water(base, light, seed, name):
    img, d = canvas(base)
    grain(d, light + (90,), 120, 6, 16, seed)
    rnd = random.Random(seed + 1)
    for _ in range(16):                      # さざ波
        x, y = rnd.uniform(8, S - 48), rnd.uniform(8, S - 8)
        w = rnd.uniform(22, 44)
        d.arc([x, y, x + w, y + 12], 200, 340, fill=light + (200,), width=3)
    img = img.filter(ImageFilter.GaussianBlur(0.6))
    save(img, name)


def city():
    img, d = canvas((196, 196, 188))
    grain(d, (182, 182, 174, 150), 120, 3, 7, 9)
    rnd = random.Random(10)
    for _ in range(7):                       # 建物ブロック
        w = rnd.uniform(26, 42)
        h = rnd.uniform(30, 56)
        x = rnd.uniform(10, S - w - 10)
        y = rnd.uniform(20, S - h - 12)
        d.rectangle([x, y, x + w, y + h], fill=(168, 168, 162, 255))
        d.rectangle([x, y, x + w, y + 7], fill=(210, 210, 202, 255))
        for r in range(2):                   # 窓
            for c in range(2):
                wx = x + 7 + c * (w * 0.45)
                wy = y + 14 + r * (h * 0.38)
                d.rectangle([wx, wy, wx + 8, wy + 9], fill=(120, 126, 134, 255))
    save(img, "city")


def factory():
    img, d = canvas((176, 162, 148))
    grain(d, (162, 149, 136, 150), 110, 3, 7, 11)
    d.rectangle([S * 0.12, S * 0.44, S * 0.88, S * 0.86], fill=(150, 138, 126, 255))
    for i in range(3):                       # のこぎり屋根
        x = S * (0.14 + i * 0.25)
        d.polygon([(x, S * 0.44), (x + S * 0.22, S * 0.44),
                   (x + S * 0.22, S * 0.28)], fill=(186, 174, 160, 255))
    d.rectangle([S * 0.70, S * 0.14, S * 0.80, S * 0.46], fill=(132, 120, 110, 255))
    save(img, "factory")


def airport():
    img, d = canvas((186, 186, 198))
    grain(d, (172, 172, 184, 150), 110, 3, 7, 12)
    d.polygon([(0, S * 0.30), (S, S * 0.24), (S, S * 0.76), (0, S * 0.70)],
              fill=(160, 160, 172, 255))     # 滑走路
    for i in range(6):
        x = S * (0.06 + i * 0.16)
        d.rectangle([x, S * 0.48, x + S * 0.08, S * 0.53],
                    fill=(232, 232, 238, 255))
    save(img, "airport")


def port():
    img, d = canvas((120, 156, 180))
    grain(d, (108, 144, 168, 150), 110, 4, 10, 13)
    d.rectangle([0, 0, S, S * 0.46], fill=(168, 178, 186, 255))    # 岸壁
    d.rectangle([0, S * 0.42, S, S * 0.50], fill=(140, 148, 156, 255))
    for i in range(4):                        # 係船柱
        x = S * (0.12 + i * 0.24)
        d.ellipse([x, S * 0.30, x + 14, S * 0.30 + 14], fill=(96, 102, 110, 255))
    save(img, "port")


def hq():
    img, d = canvas((214, 199, 102))
    grain(d, (200, 186, 92, 150), 110, 3, 7, 14)
    d.rectangle([S * 0.18, S * 0.40, S * 0.82, S * 0.86], fill=(196, 180, 84, 255))
    d.polygon([(S * 0.5, S * 0.18), (S * 0.86, S * 0.42), (S * 0.14, S * 0.42)],
              fill=(228, 214, 128, 255))
    d.rectangle([S * 0.44, S * 0.58, S * 0.56, S * 0.86], fill=(150, 136, 60, 255))
    save(img, "hq")


if __name__ == "__main__":
    plain(); road(); forest(); mountain(); hill()
    water((74, 127, 181), (126, 176, 220), 20, "sea")
    water((127, 178, 217), (176, 214, 240), 30, "river")
    city(); factory(); airport(); port(); hq()
