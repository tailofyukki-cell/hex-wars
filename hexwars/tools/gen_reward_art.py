# gen_reward_art.py - クリア報酬の「ご褒美画像」を生成（Pillow）
# 作戦ごとに勝利を祝う記章風のイラストを手続き生成。差し替え自由。
from PIL import Image, ImageDraw
import os, math, random

BASE = os.path.join(os.path.dirname(__file__), "..")
OUT = os.path.join(BASE, "assets", "gfx", "reward")
os.makedirs(OUT, exist_ok=True)

W, H = 640, 480
BLUE = (58, 110, 200)
GOLD = (222, 184, 74)
GOLD_D = (170, 138, 50)
DARK = (28, 34, 44)
CREAM = (238, 232, 210)

# 各作戦のテーマ色（背景グラデ上）と勲章の形
THEMES = {
    "m01": ((70, 96, 60),  "star"),
    "m02": ((54, 80, 110), "anchor"),
    "m03": ((78, 96, 66),  "star"),
    "m04": ((48, 78, 120), "anchor"),
    "m05": ((110, 96, 64), "shield"),
    "m06": ((96, 88, 76),  "shield"),
    "m07": ((60, 84, 108), "wing"),
    "m08": ((86, 86, 78),  "star"),
    "m09": ((84, 96, 58),  "star"),
    "m10": ((120, 96, 60), "laurel"),
}


def vgrad(top, bottom):
    img = Image.new("RGBA", (W, H))
    px = img.load()
    for y in range(H):
        t = y / H
        c = tuple(int(top[i] * (1 - t) + bottom[i] * t) for i in range(3))
        for x in range(W):
            px[x, y] = c + (255,)
    return img


def rays(d, cx, cy):
    for i in range(24):
        a = math.radians(i * 15)
        x2 = cx + 600 * math.cos(a)
        y2 = cy + 600 * math.sin(a)
        col = (255, 255, 255, 16) if i % 2 == 0 else (255, 255, 255, 8)
        d.polygon([(cx, cy),
                   (cx + 600 * math.cos(a - 0.1), cy + 600 * math.sin(a - 0.1)),
                   (x2, y2)], fill=col)


def star(d, cx, cy, r, fill, outline):
    pts = []
    for i in range(10):
        a = math.radians(-90 + i * 36)
        rr = r if i % 2 == 0 else r * 0.42
        pts.append((cx + rr * math.cos(a), cy + rr * math.sin(a)))
    d.polygon(pts, fill=fill, outline=outline, width=5)


def medal(d, cx, cy, shape):
    # リボン
    d.polygon([(cx - 46, cy - 10), (cx - 20, cy - 10), (cx - 30, cy + 120),
               (cx - 60, cy + 110)], fill=BLUE + (255,))
    d.polygon([(cx + 46, cy - 10), (cx + 20, cy - 10), (cx + 30, cy + 120),
               (cx + 60, cy + 110)], fill=(180, 60, 54, 255))
    # 台座の円
    d.ellipse((cx - 74, cy - 74, cx + 74, cy + 74), fill=GOLD_D + (255,))
    d.ellipse((cx - 66, cy - 66, cx + 66, cy + 66), fill=GOLD + (255,))
    if shape == "star":
        star(d, cx, cy, 52, CREAM + (255,), GOLD_D + (255,))
    elif shape == "anchor":
        d.line([(cx, cy - 40), (cx, cy + 44)], fill=DARK + (255,), width=9)
        d.ellipse((cx - 12, cy - 52, cx + 12, cy - 28), outline=DARK, width=9)
        d.arc((cx - 40, cy - 4, cx + 40, cy + 60), 20, 160, fill=DARK, width=9)
        d.line([(cx - 34, cy + 10), (cx + 34, cy + 10)], fill=DARK + (255,), width=9)
    elif shape == "shield":
        d.polygon([(cx - 40, cy - 44), (cx + 40, cy - 44), (cx + 40, cy + 8),
                   (cx, cy + 52), (cx - 40, cy + 8)],
                  fill=CREAM + (255,), outline=DARK, width=6)
        d.line([(cx, cy - 40), (cx, cy + 46)], fill=DARK + (255,), width=6)
        d.line([(cx - 36, cy - 8), (cx + 36, cy - 8)], fill=DARK + (255,), width=6)
    elif shape == "wing":
        for s in (-1, 1):
            for k in range(4):
                d.ellipse((cx + s * (10 + k * 12) - 8, cy - 16 + k * 6,
                           cx + s * (10 + k * 12) + 18, cy + 20 + k * 6),
                          fill=CREAM + (255,), outline=DARK, width=3)
        star(d, cx, cy, 20, GOLD + (255,), DARK + (255,))
    elif shape == "laurel":
        for s in (-1, 1):
            for k in range(6):
                a = math.radians(120 - k * 24) * s
                lx = cx + s * (18 + k * 6)
                ly = cy + 40 - k * 14
                d.ellipse((lx - 10, ly - 5, lx + 10, ly + 5),
                          fill=(120, 170, 90, 255), outline=DARK, width=2)
        star(d, cx, cy - 6, 30, GOLD + (255,), DARK + (255,))


for key, (base, shape) in THEMES.items():
    img = vgrad(tuple(min(255, c + 30) for c in base), DARK)
    d = ImageDraw.Draw(img, "RGBA")
    rays(d, W // 2, 200)
    medal(d, W // 2, 200, shape)
    img.save(os.path.join(OUT, f"{key}.png"))
    print(key)

# フリー対戦の汎用勝利画像（トロフィー風の月桂＋星章）
img = vgrad((92, 82, 52), DARK)
d = ImageDraw.Draw(img, "RGBA")
rays(d, W // 2, 200)
medal(d, W // 2, 200, "laurel")
img.save(os.path.join(OUT, "free_win.png"))
print("free_win")
print("OK")
