# -*- coding: utf-8 -*-
"""差し替え可能な画像の一覧を docs/asset_list.md に書き出す。
データ定義を実際に読むので、ユニットや地形を足しても再実行すれば追従する。"""
import io, os, re
from collections import OrderedDict, Counter
# 他の tools/*.py と同じくこのファイルからの相対で見る
# （公開リポジトリなので個人のフルパスを直書きしない）
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
try:
    from PIL import Image
except ImportError:
    Image = None

refs = []

def scan(deffile, header_re, keys, category):
    cur, disp = "?", "?"
    for ln in io.open(os.path.join(ROOT, "data", deffile), encoding="utf-8-sig"):
        t = ln.split("#")[0].strip()
        m = re.match(header_re, t)
        if m:
            cur = m.group(1); disp = cur
        elif t.startswith(("name", "title")) and "=" in t:
            v = t.split("=", 1)[1].strip()
            if v and cur != "?": disp = "%s（%s）" % (cur, v)
        for k in keys:
            mm = re.match(r"^%s\s*=\s*(\S.*)$" % k, t)
            if mm:
                refs.append([category, disp, k, mm.group(1).strip(), deffile])

scan("terrain.def",       r"\[terrain\s+(\w+)\]",   ["image"], "terrain")
scan("units.def",         r"\[unit\s+(\w+)\]",
     ["image", "image0", "image1", "anim", "cutin"], "unit")
scan("commanders.def",    r"\[commander\s+(\w+)\]", ["image", "cutin"], "co")
scan("campaign/main.cpn", r"\[node\s+(\w+)\]",
     ["art", "reward", "reward_video"], "cpn")

def info(p):
    full = os.path.join(ROOT, "assets", p.replace("/", os.sep))
    if not os.path.isfile(full): return False, ""
    if Image and p.lower().endswith((".png", ".jpg", ".gif")):
        try:
            with Image.open(full) as im: return True, "%dx%d" % im.size
        except Exception: return True, "?"
    return True, ""

# パス単位にまとめる（同じ絵を複数が共有していることがあるため）
uniq = OrderedDict()
for cat, who, key, p, src in refs:
    e = uniq.setdefault(p, {"cat": cat, "key": key, "users": [], "src": src})
    e["users"].append(who)
for p, e in uniq.items():
    e["exists"], e["size"] = info(p)

SEC = [
    ("terrain", "地形（マップのセル）", "`data/terrain.def` の `image =`",
     "真上から見た地面の模様。六角形に切り抜いて貼られるので、正方形の"
     "タイル画像でよい。斜め見下ろし表示でも同じ画像を使う（別途用意は不要）。"),
    ("unit",    "ユニット", "`data/units.def` の `image = / image0 = / image1 =`",
     "`image` は両陣営共通、`image0`/`image1` は陣営ごとに分けたいとき。"
     "背景は透過。`anim` は戦闘アニメ、`cutin` は攻撃時の1枚絵。"),
    ("co",      "指揮官", "`data/commanders.def` の `image = / cutin =`",
     "`image` は指揮官選択と全体図に出す顔絵（縦長）。`cutin` は必殺技の1枚絵。"),
    ("cpn",     "キャンペーン", "`data/campaign/main.cpn` の `art = / reward = / reward_video =`",
     "`art` はブリーフィングの1枚絵（横800基準）。`reward` はクリア後のご褒美画面。"
     "`reward_video` を書くと mp4/GIF が優先される。"),
]

out = []
out.append("# 差し替えできる画像の一覧\n")
out.append("すべて `hexwars/assets/` からの相対パスで、**データ定義に書いた"
           "パスがそのまま読まれる**。同じ名前で置き換えれば再ビルドなしで反映される"
           "（`assets/` は実行フォルダへ自動同期される）。\n")
out.append("ファイルが無い場合は落ちずにフォールバックする"
           "（地形は `color` の単色、指揮官は名前の文字、ユニットは図形描画）。"
           "**先に絵の無いものから用意すれば、それだけ見た目が上がる。**\n")
n_all = len(uniq)
n_missing = sum(1 for e in uniq.values() if not e["exists"])
out.append("| | 数 |\n|---|---|\n| 参照されているパス | %d |\n"
           "| うち実体あり | %d |\n| うち**未用意** | **%d** |\n"
           % (n_all, n_all - n_missing, n_missing))

for cat, title, where, note in SEC:
    items = [(p, e) for p, e in uniq.items() if e["cat"] == cat]
    if not items: continue
    out.append("\n## %s（%d本）\n" % (title, len(items)))
    out.append("定義場所: %s\n" % where)
    out.append("%s\n" % note)
    out.append("| パス | 用途 | 現物 | 実寸 |")
    out.append("|---|---|---|---|")
    for p, e in items:
        users = e["users"]
        u = users[0] if len(users) == 1 else "%s ほか%d件" % (users[0], len(users)-1)
        out.append("| `%s` | %s | %s | %s |" %
                   (p, u, "○" if e["exists"] else "**未**", e["size"] or "-"))

out.append("\n## 未用意のもの\n")
miss = [(p, e) for p, e in uniq.items() if not e["exists"]]
if miss:
    out.append("| パス | 用途 |\n|---|---|")
    for p, e in miss:
        out.append("| `%s` | %s |" % (p, "／".join(e["users"])))
else:
    out.append("なし。\n")

out.append("\n## 寸法の目安\n")
out.append("既存ファイルの実寸から。厳密でなくてよく、"
           "縦横比を保って収まるように拡縮される。\n")
sizes = {}
for p, e in uniq.items():
    if e["exists"] and e["size"] and e["size"] != "?":
        sizes.setdefault(e["cat"], Counter())[e["size"]] += 1
LBL = {"terrain": "地形タイル", "unit": "ユニット/カットイン",
       "co": "指揮官の顔絵/カットイン", "cpn": "ブリーフィング/ご褒美"}
out.append("| 種別 | よくある実寸 |\n|---|---|")
for cat in ("terrain", "unit", "co", "cpn"):
    if cat in sizes:
        top = "、".join("%s (%d本)" % (s, n) for s, n in sizes[cat].most_common(4))
        out.append("| %s | %s |" % (LBL[cat], top))

out.append("\n---\n")
out.append("この一覧は `tools/gen_asset_list.py` で生成している。"
           "ユニットや地形を足したら再実行すること。\n")

os.makedirs(os.path.join(ROOT, "docs"), exist_ok=True)
io.open(os.path.join(ROOT, "docs", "asset_list.md"), "w",
        encoding="utf-8", newline="\n").write("\n".join(out) + "\n")
print("docs/asset_list.md: %d paths, %d missing" % (n_all, n_missing))
