# gen_anim_sample.py - 戦闘アニメ動画のサンプル（アニメーションGIF）を生成
# 実際の動画に差し替える前の「動作確認用プレースホルダ」。要 Pillow。
from PIL import Image, ImageDraw
import os, math

BASE = os.path.join(os.path.dirname(__file__), "..")
OUT = os.path.join(BASE, "assets", "gfx", "anim")
os.makedirs(OUT, exist_ok=True)

W, H = 640, 360
FRAMES = 24
DARK = (24, 30, 38)
GROUND = (58, 72, 54)


def make(name, muzzle_x, color, ground=GROUND, label=""):
    frames = []
    for i in range(FRAMES):
        img = Image.new("RGB", (W, H), DARK)
        d = ImageDraw.Draw(img)
        # 地面（海マップ用は海面色）
        d.rectangle((0, H - 90, W, H), fill=ground)
        # 砲身（左側の車体から）
        d.rectangle((60, H - 150, 190, H - 100), fill=(70, 80, 66),
                    outline=(20, 24, 20), width=3)
        d.rectangle((185, H - 135, muzzle_x, H - 125), fill=(60, 68, 58))
        # 発射炎（最初の数フレーム）
        if i < 5:
            r = 26 - i * 4
            d.ellipse((muzzle_x - r, H - 130 - r, muzzle_x + r, H - 130 + r),
                      fill=(255, 226, 120))
        # 弾道
        if 3 <= i < 12:
            t = (i - 3) / 9.0
            bx = muzzle_x + (W - 130 - muzzle_x) * t
            by = (H - 130) - 70 * math.sin(math.pi * t)
            d.ellipse((bx - 5, by - 5, bx + 5, by + 5), fill=(250, 240, 200))
        # 着弾の爆発
        if i >= 12:
            k = i - 12
            r = 8 + k * 9
            d.ellipse((W - 130 - r, H - 130 - r, W - 130 + r, H - 130 + r),
                      fill=(240, max(60, 200 - k * 12), 60))
            d.ellipse((W - 130 - r // 2, H - 130 - r // 2,
                       W - 130 + r // 2, H - 130 + r // 2),
                      fill=(255, 230, 160))
        d.text((16, 12), f"SAMPLE BATTLE ANIMATION  ({label or name})", fill=color)
        frames.append(img.convert("P", palette=Image.ADAPTIVE))
    path = os.path.join(OUT, f"{name}.gif")
    frames[0].save(path, save_all=True, append_images=frames[1:],
                   duration=60, loop=0)
    print(f"{name}.gif  {W}x{H} {FRAMES}frames")


# ユニット毎に別の動画を指定できることを示すための2種類のサンプル
make("sample_land", 250, (230, 230, 220), GROUND, "LAND UNIT")
make("sample_sea", 230, (200, 230, 255), (44, 78, 120), "SEA UNIT")
print("OK")
