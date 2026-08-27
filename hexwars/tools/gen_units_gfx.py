# gen_units_gfx.py - ユニットのフラットベクタ風スプライトPNGを生成（Pillow）
# 192pxで描いて96pxへ縮小（アンチエイリアス）。透過背景。
from PIL import Image, ImageDraw
import os

OUT = os.path.join(os.path.dirname(__file__), "..", "assets", "gfx", "units")
os.makedirs(OUT, exist_ok=True)

S = 192          # 作業キャンバス
FINAL = 96       # 出力サイズ

# フラットベクタ調パレット（陣営色は本体側のチップで示すため中立色）
OLIVE   = (138, 143, 120, 255)
OLIVE_D = (91, 95, 78, 255)
OLIVE_L = (169, 173, 151, 255)
GUN     = (60, 63, 53, 255)
OUTLINE = (38, 40, 36, 255)
SKIN    = (222, 190, 160, 255)
NAVY    = (125, 136, 148, 255)
NAVY_D  = (86, 95, 105, 255)
NAVY_L  = (156, 166, 177, 255)
AIRGRAY = (183, 188, 196, 255)
AIR_D   = (132, 137, 146, 255)
GLASS   = (159, 196, 216, 255)
TIRE    = (45, 46, 44, 255)

OW = 6  # アウトライン太さ（縮小後 3px 相当）

def canvas():
    img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    return img, ImageDraw.Draw(img)

def save(img, name):
    img = img.resize((FINAL, FINAL), Image.LANCZOS)
    img.save(os.path.join(OUT, f"{name}.png"))
    print(name)

def poly(d, pts, fill):
    d.polygon(pts, fill=fill, outline=OUTLINE, width=OW)

def ell(d, box, fill):
    d.ellipse(box, fill=fill, outline=OUTLINE, width=OW)

def rect(d, box, fill, r=10):
    d.rounded_rectangle(box, radius=r, fill=fill, outline=OUTLINE, width=OW)

# ---------------- 陸上 ----------------
def infantry():
    img, d = canvas()
    d.line([(118, 78), (176, 58)], fill=GUN, width=12)      # 小銃
    ell(d, (66, 26, 126, 86), OLIVE)                        # ヘルメット
    d.ellipse((84, 62, 112, 90), fill=SKIN, outline=OUTLINE, width=4)  # 顔
    poly(d, [(60, 90), (132, 90), (140, 170), (52, 170)], OLIVE)       # 胴
    rect(d, (56, 150, 90, 184), OLIVE_D, 6)                 # 脚
    rect(d, (102, 150, 136, 184), OLIVE_D, 6)
    save(img, "infantry")

def at_infantry():
    img, d = canvas()
    rect(d, (44, 34, 178, 66), GUN, 12)                     # 対戦車火器
    d.polygon([(160, 30), (184, 50), (160, 70)], fill=OLIVE_D, outline=OUTLINE, width=4)
    ell(d, (60, 44, 116, 98), OLIVE)                        # ヘルメット
    d.ellipse((76, 76, 102, 102), fill=SKIN, outline=OUTLINE, width=4)
    poly(d, [(54, 102), (124, 102), (132, 174), (46, 174)], OLIVE)
    rect(d, (50, 156, 84, 188), OLIVE_D, 6)
    rect(d, (94, 156, 128, 188), OLIVE_D, 6)
    save(img, "at_infantry")

def wheels(d, y, xs, r=18):
    for x in xs:
        d.ellipse((x - r, y - r, x + r, y + r), fill=TIRE, outline=OUTLINE, width=4)
        d.ellipse((x - 7, y - 7, x + 7, y + 7), fill=OLIVE_L)

def recon():
    img, d = canvas()
    poly(d, [(20, 120), (44, 84), (150, 84), (176, 120), (176, 142), (20, 142)], OLIVE)
    poly(d, [(66, 84), (78, 58), (128, 58), (140, 84)], OLIVE_D)   # 銃塔
    d.line([(126, 66), (168, 58)], fill=GUN, width=8)
    d.rectangle((52, 96, 92, 112), fill=GLASS, outline=OUTLINE, width=4)
    wheels(d, 146, (52, 100, 148))
    save(img, "recon")

def tracks(d, box):
    rect(d, box, OLIVE_D, 20)
    x0, y0, x1, y1 = box
    cy = (y0 + y1) // 2
    for x in range(x0 + 22, x1 - 10, 26):
        d.ellipse((x - 9, cy - 9, x + 9, cy + 9), fill=TIRE, outline=OUTLINE, width=3)

def tank():
    img, d = canvas()
    d.line([(120, 82), (186, 74)], fill=GUN, width=11)                 # 砲身
    poly(d, [(58, 62), (128, 62), (140, 96), (46, 96)], OLIVE_L)       # 砲塔
    poly(d, [(24, 96), (170, 96), (182, 128), (14, 128)], OLIVE)       # 車体
    tracks(d, (16, 122, 180, 162))
    save(img, "tank")

def htank():
    img, d = canvas()
    d.line([(126, 70), (190, 60)], fill=GUN, width=15)
    poly(d, [(48, 46), (134, 46), (150, 88), (34, 88)], OLIVE_L)
    poly(d, [(16, 88), (178, 88), (188, 124), (8, 124)], OLIVE)
    tracks(d, (8, 118, 188, 164))
    save(img, "htank")

def artillery():
    img, d = canvas()
    d.line([(96, 96), (180, 26)], fill=GUN, width=13)                  # 仰角砲身
    poly(d, [(60, 76), (120, 76), (132, 108), (48, 108)], OLIVE_L)
    poly(d, [(26, 108), (166, 108), (176, 136), (16, 136)], OLIVE)
    tracks(d, (18, 130, 174, 166))
    save(img, "artillery")

def aa_tank():
    img, d = canvas()
    d.line([(96, 84), (140, 20)], fill=GUN, width=9)                   # 対空連装砲
    d.line([(112, 88), (156, 24)], fill=GUN, width=9)
    poly(d, [(62, 74), (126, 74), (136, 104), (52, 104)], OLIVE_L)
    poly(d, [(28, 104), (164, 104), (174, 132), (18, 132)], OLIVE)
    tracks(d, (20, 126, 172, 162))
    save(img, "aa_tank")

def truck():
    img, d = canvas()
    rect(d, (78, 62, 180, 132), OLIVE_L, 8)                            # 荷台
    poly(d, [(20, 132), (20, 92), (40, 72), (74, 72), (74, 132)], OLIVE)  # キャブ
    d.rectangle((28, 82, 62, 104), fill=GLASS, outline=OUTLINE, width=4)
    wheels(d, 140, (48, 112, 156))
    save(img, "truck")

def supply():
    img, d = canvas()
    ell(d, (80, 66, 182, 128), OLIVE_D)                                # タンク
    d.ellipse((116, 80, 146, 112), fill=OLIVE_L, outline=OUTLINE, width=4)
    poly(d, [(20, 132), (20, 92), (40, 72), (76, 72), (76, 132)], OLIVE)  # キャブ
    d.rectangle((28, 82, 62, 104), fill=GLASS, outline=OUTLINE, width=4)
    wheels(d, 140, (48, 112, 156))
    save(img, "supply")

# ---------------- 航空 ----------------
def fighter():
    img, d = canvas()
    poly(d, [(96, 26), (114, 96), (96, 170), (78, 96)], AIRGRAY)        # 胴体
    poly(d, [(20, 96), (172, 96), (150, 122), (42, 122)], AIR_D)        # 主翼
    poly(d, [(70, 158), (122, 158), (110, 176), (82, 176)], AIR_D)      # 尾翼
    d.ellipse((86, 60, 106, 92), fill=GLASS, outline=OUTLINE, width=4)  # 風防
    save(img, "fighter")

def bomber():
    img, d = canvas()
    poly(d, [(96, 18), (112, 100), (96, 178), (80, 100)], AIRGRAY)
    poly(d, [(8, 88), (184, 88), (168, 120), (24, 120)], AIR_D)         # 大きな主翼
    rect(d, (44, 84, 64, 128), GUN, 8)                                  # エンジン
    rect(d, (128, 84, 148, 128), GUN, 8)
    poly(d, [(62, 162), (130, 162), (114, 182), (78, 182)], AIR_D)
    d.ellipse((88, 44, 104, 72), fill=GLASS, outline=OUTLINE, width=4)
    save(img, "bomber")

def heli():
    img, d = canvas()
    d.line([(16, 44), (176, 44)], fill=GUN, width=8)                    # ローター
    poly(d, [(60, 64), (140, 64), (150, 104), (96, 124), (50, 104)], OLIVE)  # 機体
    poly(d, [(140, 78), (184, 96), (140, 100)], OLIVE_D)                # テイル
    d.ellipse((64, 72, 96, 100), fill=GLASS, outline=OUTLINE, width=4)
    d.line([(56, 116), (144, 116)], fill=GUN, width=8)                  # スキッド
    d.line([(52, 96), (110, 130)], fill=GUN, width=6)                   # ガンポッド
    save(img, "heli")

def t_copter():
    img, d = canvas()
    d.line([(12, 40), (180, 40)], fill=GUN, width=8)
    rect(d, (44, 58, 150, 118), OLIVE_L, 18)                            # 太い胴体
    poly(d, [(148, 70), (188, 88), (148, 96)], OLIVE_D)
    d.rectangle((54, 68, 86, 92), fill=GLASS, outline=OUTLINE, width=4)
    d.line([(48, 130), (150, 130)], fill=GUN, width=8)
    save(img, "t_copter")

# ---------------- 海上 ----------------
def hull(d, y, h, x0=14, x1=178, color=NAVY):
    poly(d, [(x0, y), (x1, y), (x1 - 26, y + h), (x0 + 26, y + h)], color)

def destroyer():
    img, d = canvas()
    hull(d, 108, 44)
    rect(d, (66, 74, 128, 112), NAVY_L, 6)                              # 艦橋
    rect(d, (88, 46, 106, 78), NAVY_D, 4)                               # 煙突
    d.line([(128, 92), (168, 84)], fill=GUN, width=8)                   # 主砲
    save(img, "destroyer")

def cruiser():
    img, d = canvas()
    hull(d, 104, 52, 8, 184)
    rect(d, (72, 62, 124, 108), NAVY_L, 6)
    rect(d, (90, 36, 108, 66), NAVY_D, 4)
    d.line([(124, 84), (172, 74)], fill=GUN, width=10)
    d.line([(72, 84), (28, 76)], fill=GUN, width=10)                    # 前後主砲
    save(img, "cruiser")

def submarine():
    img, d = canvas()
    ell(d, (10, 84, 182, 140), NAVY_D)                                  # 船体
    rect(d, (78, 52, 116, 96), NAVY, 8)                                 # セイル
    d.line([(94, 36), (94, 56)], fill=GUN, width=6)                     # 潜望鏡
    save(img, "submarine")

def t_ship():
    img, d = canvas()
    hull(d, 106, 48)
    rect(d, (40, 74, 88, 110), OLIVE_L, 4)                              # 貨物
    rect(d, (92, 62, 132, 110), OLIVE_D, 4)
    rect(d, (140, 70, 168, 110), NAVY_L, 4)                             # 艦橋
    save(img, "t_ship")

def supply_ship():
    img, d = canvas()
    hull(d, 106, 48)
    rect(d, (36, 70, 84, 112), (210, 200, 170, 255), 4)                # 物資コンテナ（明）
    rect(d, (88, 70, 128, 112), OLIVE_D, 4)                            # 物資コンテナ（暗）
    d.line([(36, 91), (128, 91)], fill=OUTLINE, width=3)               # コンテナ帯
    rect(d, (138, 66, 168, 112), NAVY_L, 4)                            # 艦橋
    d.line([(150, 44), (150, 66)], fill=GUN, width=5)                  # クレーン支柱
    d.line([(150, 50), (176, 66)], fill=GUN, width=5)                  # 補給クレーン
    save(img, "supply_ship")

def battleship():
    img, d = canvas()
    hull(d, 100, 54, 6, 186)                                            # 大型船体
    rect(d, (74, 58, 122, 104), NAVY_L, 6)                              # 艦橋（塔）
    rect(d, (90, 30, 106, 62), NAVY_D, 4)                               # マスト
    d.line([(120, 78), (176, 66)], fill=GUN, width=12)                  # 三連装主砲
    d.line([(120, 86), (176, 78)], fill=GUN, width=12)
    d.line([(72, 78), (20, 66)], fill=GUN, width=12)
    d.line([(72, 86), (20, 78)], fill=GUN, width=12)
    save(img, "battleship")

def carrier():
    img, d = canvas()
    hull(d, 104, 50, 8, 184)
    # 平らな飛行甲板
    poly(d, [(20, 78), (180, 78), (176, 100), (24, 100)], NAVY_L)
    d.line([(34, 89), (166, 89)], fill=(230, 220, 60, 255), width=4)    # 甲板ライン
    rect(d, (128, 56, 156, 82), NAVY_D, 4)                              # アイランド艦橋
    d.line([(142, 40), (142, 58)], fill=GUN, width=4)
    save(img, "carrier")

def gunboat():
    img, d = canvas()
    hull(d, 108, 40, 26, 164)                                          # 小型船体
    rect(d, (78, 76, 118, 110), NAVY_L, 6)                              # 操舵室
    d.line([(96, 94), (150, 58)], fill=GUN, width=8)                    # 仰角砲
    save(img, "gunboat")

def missile_boat():
    img, d = canvas()
    poly(d, [(24, 104), (176, 96), (160, 128), (40, 128)], NAVY)        # 鋭い船体
    rect(d, (70, 80, 110, 108), NAVY_L, 4)                              # 操舵室
    d.rectangle((116, 84, 150, 100), fill=GUN, outline=OUTLINE, width=4)  # ミサイル箱
    d.line([(120, 88), (150, 80)], fill=(210, 90, 60, 255), width=6)
    save(img, "missile_boat")

# ---- 拡充ユニット ----
GRAY_U  = (150, 148, 138, 255)   # 民兵の服

def militia():
    img, d = canvas()
    d.line([(118, 82), (172, 66)], fill=GUN, width=9)       # 猟銃
    d.ellipse((72, 34, 122, 84), fill=GRAY_U, outline=OUTLINE, width=OW)  # 帽子
    d.ellipse((84, 64, 112, 92), fill=SKIN, outline=OUTLINE, width=4)
    poly(d, [(62, 92), (130, 92), (138, 170), (54, 170)], GRAY_U)
    rect(d, (58, 152, 92, 184), (110, 108, 100, 255), 6)
    rect(d, (100, 152, 134, 184), (110, 108, 100, 255), 6)
    save(img, "militia")

def mech_inf():
    img, d = canvas()
    # ハーフトラック + 兵員
    d.ellipse((96, 40, 128, 72), fill=OLIVE_D, outline=OUTLINE, width=4)  # 兵員頭
    poly(d, [(88, 66), (136, 66), (140, 92), (84, 92)], OLIVE_D)
    poly(d, [(24, 92), (170, 92), (180, 124), (14, 124)], OLIVE)          # 車体
    d.rectangle((30, 100, 62, 116), fill=GLASS, outline=OUTLINE, width=4)
    wheels(d, 132, (44,), 16)
    tracks(d, (76, 118, 182, 152))
    save(img, "mech_inf")

def ltank():
    img, d = canvas()
    d.line([(116, 88), (172, 82)], fill=GUN, width=8)
    poly(d, [(68, 70), (122, 70), (132, 100), (58, 100)], OLIVE_L)
    poly(d, [(36, 100), (158, 100), (168, 126), (26, 126)], OLIVE)
    tracks(d, (28, 120, 166, 156))
    save(img, "ltank")

def rocket():
    img, d = canvas()
    # 傾いたロケットランチャー箱
    d.polygon([(70, 96), (160, 36), (184, 58), (94, 118)], fill=OLIVE_D,
              outline=OUTLINE, width=4)
    for k in range(3):
        x0 = 92 + k * 26
        d.line([(x0, 92 - k * 17), (x0 + 16, 80 - k * 17)], fill=(210, 90, 60, 255), width=7)
    poly(d, [(24, 108), (150, 108), (160, 134), (14, 134)], OLIVE)
    wheels(d, 142, (48, 96, 138))
    save(img, "rocket")

def aa_gun():
    img, d = canvas()
    d.line([(92, 96), (140, 22)], fill=GUN, width=10)        # 高角砲身
    d.line([(92, 96), (120, 26)], fill=GUN, width=7)
    poly(d, [(58, 88), (126, 88), (136, 120), (48, 120)], OLIVE_L)  # 砲架
    d.line([(30, 140), (96, 104)], fill=GUN, width=8)        # 脚
    d.line([(160, 140), (96, 104)], fill=GUN, width=8)
    wheels(d, 132, (66, 118), 15)
    save(img, "aa_gun")

def dive_bomber():
    img, d = canvas()
    poly(d, [(96, 22), (112, 98), (96, 172), (80, 98)], OLIVE_L)        # 胴体
    poly(d, [(18, 78), (94, 100), (98, 122), (30, 108)], OLIVE_D)       # ガル翼左
    poly(d, [(174, 78), (98, 100), (94, 122), (162, 108)], OLIVE_D)     # ガル翼右
    poly(d, [(70, 156), (122, 156), (110, 176), (82, 176)], OLIVE_D)
    d.ellipse((86, 56, 106, 90), fill=GLASS, outline=OUTLINE, width=4)
    save(img, "dive_bomber")

def scout_plane():
    img, d = canvas()
    poly(d, [(96, 30), (108, 96), (96, 166), (84, 96)], AIRGRAY)
    poly(d, [(30, 84), (162, 84), (150, 104), (42, 104)], AIRGRAY)      # 直線翼
    poly(d, [(76, 152), (116, 152), (106, 170), (86, 170)], AIR_D)
    d.ellipse((88, 54, 104, 82), fill=GLASS, outline=OUTLINE, width=4)
    d.ellipse((80, 96, 112, 128), fill=(255, 255, 255, 60))             # カメラ窓風
    save(img, "scout_plane")

def supply_air():
    img, d = canvas()
    poly(d, [(96, 22), (114, 100), (96, 178), (78, 100)], OLIVE_L)       # 太めの輸送機胴体
    poly(d, [(14, 86, ), (182, 86), (166, 116), (30, 116)], OLIVE_D)     # 高翼
    rect(d, (44, 86, 62, 122), GUN, 6)                                   # エンジン
    rect(d, (134, 86, 152, 122), GUN, 6)
    poly(d, [(66, 158), (126, 158), (112, 180), (80, 180)], OLIVE_D)     # 尾翼
    d.ellipse((86, 48, 106, 78), fill=GLASS, outline=OUTLINE, width=4)
    d.rectangle((84, 108, 108, 140), fill=(210, 200, 170, 255),
                outline=OUTLINE, width=4)                                # 物資ハッチ
    save(img, "supply_air")

def t_plane():
    img, d = canvas()
    poly(d, [(96, 20), (116, 100), (96, 180), (76, 100)], OLIVE_L)       # 太い胴体
    poly(d, [(10, 82), (182, 82), (168, 114), (24, 114)], OLIVE_D)       # 高翼
    rect(d, (40, 82, 60, 120), GUN, 6)                                   # エンジン
    rect(d, (132, 82, 152, 120), GUN, 6)
    poly(d, [(64, 158), (128, 158), (112, 182), (80, 182)], OLIVE_D)     # 尾翼
    d.ellipse((86, 44, 106, 74), fill=GLASS, outline=OUTLINE, width=4)
    # 後部ランプ（空挺降下の目印）
    poly(d, [(80, 150), (112, 150), (120, 172), (72, 172)], (196, 186, 156, 255))
    # 降下傘
    d.pieslice((116, 116, 168, 156), 180, 360, fill=(226, 226, 214, 255),
               outline=OUTLINE, width=3)
    d.line([(122, 138), (142, 160)], fill=OUTLINE, width=3)
    d.line([(162, 138), (142, 160)], fill=OUTLINE, width=3)
    d.ellipse((134, 158, 150, 174), fill=OLIVE, outline=OUTLINE, width=3)
    save(img, "t_plane")

for fn in (infantry, at_infantry, recon, tank, htank, artillery, aa_tank,
           truck, supply, fighter, bomber, heli, t_copter,
           destroyer, cruiser, submarine, t_ship,
           militia, mech_inf, ltank, rocket, aa_gun, dive_bomber, scout_plane,
           supply_air, battleship, carrier, gunboat, missile_boat,
           supply_ship, t_plane):
    fn()
print("OK")
