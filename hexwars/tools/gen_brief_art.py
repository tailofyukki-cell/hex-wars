# gen_brief_art.py - ブリーフィング用の1枚絵をユニットスプライトから合成（Pillow）
from PIL import Image, ImageDraw
import os, math, random

BASE = os.path.join(os.path.dirname(__file__), "..")
UNITS = os.path.join(BASE, "assets", "gfx", "units")
OUT = os.path.join(BASE, "assets", "gfx", "brief")
os.makedirs(OUT, exist_ok=True)
random.seed(7)

W, H = 800, 220
BLUE = (58, 110, 200)
RED = (205, 72, 58)

# 各作戦のテーマ: (地面色, 空色, 登場ユニット, 敵ユニット)
SCENES = {
    "m01": ((110, 135, 80),  (44, 58, 74),  ["infantry", "recon"],        ["infantry"]),
    "m02": ((84, 110, 70),   (38, 50, 66),  ["at_infantry", "artillery"], ["tank", "tank"]),
    "m03": ((104, 128, 88),  (46, 62, 80),  ["tank", "infantry"],         ["at_infantry"]),
    "m04": ((74, 110, 150),  (40, 56, 76),  ["t_ship", "destroyer"],      ["fighter"]),
    "m05": ((150, 138, 100), (52, 54, 66),  ["artillery", "infantry"],    ["htank", "tank"]),
    "m06": ((130, 122, 104), (48, 58, 72),  ["at_infantry", "tank"],      ["artillery"]),
    "m07": ((100, 120, 90),  (56, 72, 92),  ["fighter", "aa_tank"],       ["bomber"]),
    "m08": ((120, 120, 110), (44, 52, 64),  ["truck", "infantry", "tank"],["recon"]),
    "m09": ((112, 128, 76),  (50, 60, 70),  ["htank", "tank", "supply"],  ["htank", "tank"]),
    "m10": ((96, 96, 88),    (36, 40, 52),  ["htank", "artillery", "bomber"], ["htank", "aa_tank"]),
}

def load_unit(name, size):
    p = os.path.join(UNITS, f"{name}.png")
    img = Image.open(p).convert("RGBA")
    return img.resize((size, size), Image.LANCZOS)

def hex_points(cx, cy, r):
    return [(cx + r * math.cos(math.radians(60 * i - 90)),
             cy + r * math.sin(math.radians(60 * i - 90))) for i in range(6)]

for key, (ground, sky, friends, foes) in SCENES.items():
    img = Image.new("RGBA", (W, H), sky + (255,))
    d = ImageDraw.Draw(img)

    # 背景のヘクス模様（うっすら）
    for i in range(14):
        cx = 30 + i * 60 + random.randint(-8, 8)
        cy = random.randint(10, 80)
        col = tuple(min(255, c + random.randint(-6, 14)) for c in sky) + (255,)
        d.polygon(hex_points(cx, cy, 26), fill=col)

    # 地面（2トーンの帯）
    d.polygon([(0, 128), (W, 112), (W, H), (0, H)], fill=ground + (255,))
    dark = tuple(int(c * 0.8) for c in ground) + (255,)
    d.polygon([(0, 176), (W, 160), (W, H), (0, H)], fill=dark)

    # 陣営の対峙: 左=西方(青) 右=東方(赤)
    x = 90
    for u in friends:
        spr = load_unit(u, 120)
        img.alpha_composite(spr, (x, 74))
        x += 96
    x = W - 200
    for u in foes:
        spr = load_unit(u, 110).transpose(Image.FLIP_LEFT_RIGHT)
        img.alpha_composite(spr, (x, 82))
        x -= 88

    # 陣営カラーの帯
    d.rectangle((0, H - 10, W // 2, H), fill=BLUE + (255,))
    d.rectangle((W // 2, H - 10, W, H), fill=RED + (255,))

    img.save(os.path.join(OUT, f"{key}.png"))
    print(key)
print("OK")
