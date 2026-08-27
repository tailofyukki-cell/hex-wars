# -*- coding: utf-8 -*-
"""指揮官の顔絵（仮）を生成する（要 Pillow）。

    python tools/gen_co_art.py

assets/gfx/co/*.png を作る。commanders.def の `image =` から参照される。
**本物のイラストに差し替える前提のプレースホルダ**。同じ縦横比（3:4）の PNG を
上書きすれば、コードもデータも触らずに絵だけ変わる。

描画側は縦横比を保って収めるので、多少サイズが違っても崩れない。
"""
import math
import os

from PIL import Image, ImageDraw, ImageFilter

OUT = os.path.join(os.path.dirname(__file__), "..", "assets", "gfx", "co")
W, H = 480, 640

# id -> (制服色, 差し色, 階級章の形, 肌の明るさ)
COS = {
    "GRAF":  ((62, 78, 112), (214, 198, 128), "star3", 0),
    "LIESE": ((78, 96, 128), (226, 214, 176), "wing", 1),
    "BALT":  ((116, 66, 58), (226, 186, 120), "star2", 0),
    "WOLF":  ((54, 82, 96), (188, 214, 226), "anchor", 0),
    "KARLA": ((96, 74, 112), (222, 200, 226), "star1", 1),
    "EAGLE": ((70, 88, 104), (206, 220, 232), "wing", 0),
}
SKIN = [(206, 176, 150), (222, 196, 172)]


def insignia(d, cx, cy, kind, col):
    """階級章。指揮官ごとの見分けを付けるための簡単な記号。"""
    if kind.startswith("star"):
        n = int(kind[-1])
        for k in range(n):
            x = cx + (k - (n - 1) / 2) * 34
            pts = []
            for i in range(10):
                r = 15 if i % 2 == 0 else 6.5
                a = math.radians(-90 + i * 36)
                pts.append((x + r * math.cos(a), cy + r * math.sin(a)))
            d.polygon(pts, fill=col)
    elif kind == "wing":
        for s in (-1, 1):
            d.polygon([(cx, cy), (cx + s * 56, cy - 14), (cx + s * 50, cy + 6),
                       (cx + s * 18, cy + 12)], fill=col)
    elif kind == "anchor":
        d.line([(cx, cy - 18), (cx, cy + 18)], fill=col, width=7)
        d.arc([cx - 22, cy - 2, cx + 22, cy + 30], 0, 180, fill=col, width=7)
        d.line([(cx - 14, cy - 12), (cx + 14, cy - 12)], fill=col, width=6)


def make(cid, uni, accent, kind, skin):
    img = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(img, "RGBA")

    # 背景（斜めの帯。カットインと同じ雰囲気に揃える）
    d.polygon([(0, 40), (W, 0), (W, H - 40), (0, H)], fill=tuple(
        min(255, c + 26) for c in uni) + (255,))
    d.polygon([(0, 40), (W, 0), (W, 22), (0, 62)], fill=accent + (220,))
    d.polygon([(0, H - 22), (W, H - 62), (W, H - 40), (0, H)], fill=accent + (220,))

    # 肩・胴（制服）
    d.rounded_rectangle([W * 0.10, H * 0.62, W * 0.90, H], 60, fill=uni + (255,))
    # 襟
    d.polygon([(W * 0.50, H * 0.62), (W * 0.30, H * 0.70), (W * 0.42, H * 0.86)],
              fill=tuple(max(0, c - 18) for c in uni) + (255,))
    d.polygon([(W * 0.50, H * 0.62), (W * 0.70, H * 0.70), (W * 0.58, H * 0.86)],
              fill=tuple(max(0, c - 18) for c in uni) + (255,))
    # 首・頭
    d.rounded_rectangle([W * 0.42, H * 0.50, W * 0.58, H * 0.68], 16,
                        fill=SKIN[skin] + (255,))
    d.ellipse([W * 0.30, H * 0.20, W * 0.70, H * 0.62], fill=SKIN[skin] + (255,))
    # 制帽
    d.ellipse([W * 0.26, H * 0.14, W * 0.74, H * 0.36], fill=uni + (255,))
    d.rectangle([W * 0.27, H * 0.28, W * 0.73, H * 0.34],
                fill=tuple(max(0, c - 24) for c in uni) + (255,))
    d.ellipse([W * 0.44, H * 0.24, W * 0.56, H * 0.31], fill=accent + (255,))
    # 目元の影（顔は描き込まない＝差し替え前提の仮絵とわかるように）
    d.rectangle([W * 0.34, H * 0.38, W * 0.66, H * 0.43],
                fill=(0, 0, 0, 70))
    # 胸の階級章
    insignia(d, W * 0.66, H * 0.78, kind, accent + (255,))

    img = img.filter(ImageFilter.SMOOTH)
    os.makedirs(OUT, exist_ok=True)
    p = os.path.join(OUT, cid.lower() + ".png")
    img.save(p)
    print(os.path.basename(p))


if __name__ == "__main__":
    for cid, (uni, accent, kind, skin) in COS.items():
        make(cid, uni, accent, kind, skin)
