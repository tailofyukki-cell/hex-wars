# gen_unit_tint.py - ユニットのスプライトを陣営色に染めた版を作る（Pillow）
#
# units.def の `image = gfx/units/xxx.png` を読み、陣営ごとの色違いを
#   assets/gfx/units/xxx_p0.png … xxx_p4.png
# として書き出し、units.def に貼れる image0〜image4 の行を出力する。
#
#   python tools/gen_unit_tint.py            # 生成して、貼る行を画面に出す
#   python tools/gen_unit_tint.py --apply    # units.def に直接書き込む
#   python tools/gen_unit_tint.py --only INFANTRY,TANK
#
# 描画は画像に陣営色を乗せない（陣営は足元のチップで示す）ので、
# 色分けされた絵が欲しければこうして用意して image0〜image4 で指定する。
#
# 既存の絵の明度を保ったまま色相だけ寄せるので、元絵の陰影は残る。
# あくまで仮絵。ちゃんとした絵を描いたら同じ名前で上書きすればいい。
import io
import os
import re
import sys

try:
    from PIL import Image
except ImportError:
    sys.exit("Pillow が要る: pip install Pillow")

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
DEF = os.path.join(ROOT, "data", "units.def")
ASSETS = os.path.join(ROOT, "assets")

# render.c の COL_P に寄せた陣営色（0=自軍）
TINT = [(90, 140, 235), (225, 85, 80), (95, 190, 120),
        (240, 200, 80), (185, 120, 220)]

# 明度をこの値で割って色に掛ける。小さいほど明るく濃く出る。
LUM_DIV = 160


def tint(src_path, dst_path, rgb):
    im = Image.open(src_path).convert("RGBA")
    px = im.load()
    r0, g0, b0 = rgb
    for y in range(im.height):
        for x in range(im.width):
            r, g, b, a = px[x, y]
            if a == 0:
                continue
            lum = (r * 299 + g * 587 + b * 114) // 1000
            px[x, y] = (min(255, r0 * lum // LUM_DIV),
                        min(255, g0 * lum // LUM_DIV),
                        min(255, b0 * lum // LUM_DIV), a)
    im.save(dst_path)


def parse_units(text):
    """[unit ID] と、その中の image= を拾う。image0..4 が既にある節は飛ばす。"""
    out = []
    uid = None
    img = None
    has_per = False
    for line in text.split("\n"):
        t = line.strip()
        m = re.match(r"\[unit (\S+)\]", t)
        if m:
            if uid and img and not has_per:
                out.append((uid, img))
            uid, img, has_per = m.group(1), None, False
            continue
        if not uid:
            continue
        m = re.match(r"image\s*=\s*(\S+)", t)
        if m:
            img = m.group(1)
        elif re.match(r"image[0-4]\s*=", t):
            has_per = True
    if uid and img and not has_per:
        out.append((uid, img))
    return out


def main():
    only = None
    apply_to_def = "--apply" in sys.argv
    if "--only" in sys.argv:
        only = set(sys.argv[sys.argv.index("--only") + 1].split(","))

    text = io.open(DEF, encoding="utf-8").read()
    units = parse_units(text)
    if only:
        units = [(u, i) for (u, i) in units if u in only]
    if not units:
        sys.exit("対象が無い（既に image0..4 が書いてあるか、image= が無い）")

    lines_for = {}
    made = 0
    for uid, rel in units:
        src = os.path.join(ASSETS, rel.replace("/", os.sep))
        if not os.path.exists(src):
            print("  元絵なし、飛ばす: %s (%s)" % (uid, rel))
            continue
        stem, ext = os.path.splitext(rel)
        rows = []
        for p, rgb in enumerate(TINT):
            out_rel = "%s_p%d%s" % (stem, p, ext)
            tint(src, os.path.join(ASSETS, out_rel.replace("/", os.sep)), rgb)
            rows.append("image%d    = %s" % (p, out_rel))
            made += 1
        lines_for[uid] = rows
        print("  %s -> %s_p0..p4%s" % (uid, stem, ext))

    print("\n%d 枚生成した。" % made)
    if not apply_to_def:
        print("units.def に貼る行:\n")
        for uid, rows in lines_for.items():
            print("[unit %s]" % uid)
            for r in rows:
                print(r)
            print("")
        print("--apply を付ければ units.def に直接書き込む。")
        return

    # image= の直後に image0..4 を挿す。あとに書いた方が勝つので順序が要る。
    out, uid = [], None
    for line in text.split("\n"):
        out.append(line)
        m = re.match(r"\[unit (\S+)\]", line.strip())
        if m:
            uid = m.group(1)
            continue
        if uid and uid in lines_for and re.match(r"image\s*=", line.strip()):
            out.extend(lines_for[uid])
            del lines_for[uid]
    io.open(DEF, "w", encoding="utf-8", newline="\n").write("\n".join(out))
    print("units.def を更新した。")


main()
