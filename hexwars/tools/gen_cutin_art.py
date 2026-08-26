# -*- coding: utf-8 -*-
"""攻撃カットインのプレースホルダ画像を生成する（要 Pillow）。

    python tools/gen_cutin_art.py

assets/gfx/cutin/*.png を作る。実際のイラストに差し替える前提の仮絵。
units.def の `cutin =` / commanders.def の `cutin =` から参照する。
"""
import math
import os

from PIL import Image, ImageDraw, ImageFilter

OUT = os.path.join(os.path.dirname(__file__), "..", "assets", "gfx", "cutin")
W, H = 520, 900          # 縦長（画面左下に立てて出す）
SCALE = 1.08             # シルエットの大きさ（枠に収まる範囲で大きく）

# name -> (主色, 副色, 兵科アイコン風の形)
SHEETS = {
    "tank":     ((90, 120, 90),  (230, 210, 120), "tank"),
    "infantry": ((110, 120, 150), (240, 235, 220), "soldier"),
    "air":      ((90, 130, 170), (255, 250, 230), "plane"),
    "ship":     ((70, 100, 130), (220, 235, 245), "ship"),
    "co_west":  ((70, 96, 150),  (245, 240, 225), "soldier"),
    "co_east":  ((150, 70, 62),  (245, 235, 225), "soldier"),
}


class _Scaled:
    """中心(cx,cy)を基準に一律拡大して描くための薄いラッパ。"""

    def __init__(self, d, cx, cy, k):
        self._d, self._cx, self._cy, self._k = d, cx, cy, k

    def _p(self, box):
        return [self._cx + (v - self._cx) * self._k if i % 2 == 0
                else self._cy + (v - self._cy) * self._k
                for i, v in enumerate(box)]

    def _pts(self, pts):
        return [(self._cx + (x - self._cx) * self._k,
                 self._cy + (y - self._cy) * self._k) for x, y in pts]

    def rounded_rectangle(self, box, r, **kw):
        self._d.rounded_rectangle(self._p(box), int(r * self._k), **kw)

    def rectangle(self, box, **kw):
        self._d.rectangle(self._p(box), **kw)

    def ellipse(self, box, **kw):
        self._d.ellipse(self._p(box), **kw)

    def polygon(self, pts, **kw):
        self._d.polygon(self._pts(pts), **kw)


def speed_lines(d, col):
    """集中線。勢いを出すための背景。"""
    cx, cy = W * 0.52, H * 0.42
    for i in range(46):
        a = math.radians(i * 360.0 / 46 + 7)
        r0, r1 = 190, 1250
        w = 2 if i % 3 else 5
        d.line([(cx + r0 * math.cos(a), cy + r0 * math.sin(a)),
                (cx + r1 * math.cos(a), cy + r1 * math.sin(a))],
               fill=col, width=w)


def silhouette(d, kind, col):
    """兵科ごとのざっくりしたシルエット（枠いっぱい・やや下寄り）。"""
    cx, cy = W * 0.50, H * 0.50
    d = _Scaled(d, cx, cy, SCALE)
    if kind == "tank":
        d.rounded_rectangle([cx - 165, cy + 10, cx + 165, cy + 110], 18, fill=col)
        d.rounded_rectangle([cx - 95, cy - 60, cx + 75, cy + 15], 14, fill=col)
        d.rectangle([cx + 60, cy - 34, cx + 210, cy - 16], fill=col)
        for i in range(6):
            d.ellipse([cx - 150 + i * 55, cy + 66, cx - 108 + i * 55, cy + 116],
                      fill=col)
    elif kind == "plane":
        d.polygon([(cx - 200, cy + 30), (cx + 30, cy - 10), (cx + 200, cy + 20),
                   (cx + 30, cy + 46)], fill=col)
        d.polygon([(cx - 20, cy - 130), (cx + 26, cy - 130), (cx + 60, cy + 30),
                   (cx - 40, cy + 30)], fill=col)
    elif kind == "ship":
        d.polygon([(cx - 210, cy + 40), (cx + 210, cy + 40),
                   (cx + 150, cy + 120), (cx - 150, cy + 120)], fill=col)
        d.rounded_rectangle([cx - 70, cy - 70, cx + 40, cy + 40], 10, fill=col)
        d.rectangle([cx - 14, cy - 165, cx + 6, cy - 60], fill=col)
    else:  # soldier
        d.ellipse([cx - 62, cy - 205, cx + 62, cy - 80], fill=col)
        d.rounded_rectangle([cx - 96, cy - 96, cx + 96, cy + 130], 40, fill=col)
        d.rounded_rectangle([cx + 60, cy - 70, cx + 128, cy + 40], 22, fill=col)


def make(name, main, accent, kind):
    img = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)

    # 斜めに切り取った帯を土台にする（カットインらしい形）
    d.polygon([(0, 90), (W, 0), (W, H - 70), (0, H)], fill=main + (238,))
    speed_lines(d, accent + (46,))
    d.polygon([(0, 90), (W, 0), (W, 26), (0, 116)], fill=accent + (230,))
    d.polygon([(0, H - 26), (W, H - 96), (W, H - 70), (0, H)], fill=accent + (230,))

    # シルエットは少しぼかして「奥にいる」感じにする
    sil = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    silhouette(ImageDraw.Draw(sil), kind, (0, 0, 0, 150))
    img.alpha_composite(sil.filter(ImageFilter.GaussianBlur(2)))

    sil2 = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    ImageDraw.Draw(sil2).rectangle([0, 0, 0, 0])
    silhouette(ImageDraw.Draw(sil2), kind, accent + (255,))
    sil2 = sil2.transform(sil2.size, Image.AFFINE, (1, 0, -7, 0, 1, -9))
    img.alpha_composite(sil2)

    os.makedirs(OUT, exist_ok=True)
    p = os.path.join(OUT, name + ".png")
    img.save(p)
    print("wrote", os.path.normpath(p))


if __name__ == "__main__":
    for n, (m, a, k) in SHEETS.items():
        make(n, m, a, k)
