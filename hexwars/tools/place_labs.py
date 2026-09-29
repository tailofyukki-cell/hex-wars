# -*- coding: utf-8 -*-
"""place_labs.py - 首都のあるマップに、陣営ごとに研究所を陸・空・海の3つ置く。

    python tools/place_labs.py           # 置く場所を表示するだけ
    python tools/place_labs.py --apply   # .map を書き換える
    python tools/place_labs.py --clear   # 置いた研究所を全部消す（置き直す前に）

研究所の種類と置き場所:
  L 陸の研究所 … 首都のすぐ横（隣接する平地）
  M 空の研究所 … 首都から一番近い自軍の空港の横（隣接する平地）
  N 海の研究所 … 首都から一番近い自軍の港の横の海（船が入れる）

決まり:
  - 空港・港が無い陣営は、その領域の研究所を置かない（その領域の超兵器は作れない）。
  - 横に置ける空きが無ければ、2マス先まで広げる。それでも無ければ置かない。
  - 候補が複数あるときは、マップの中心から遠い（自陣の奥の）マスを選ぶ。
  - 180度回転で首都どうしが重なるマップ（対称な対戦マップ）では、片方について
    決めた場所を回転してもう片方にも置く（公平さを保つ）。
  - test_arena は触らない。
"""
import io
import os
import re
import sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
MAPS = os.path.join(ROOT, "data", "maps")
LAB_CHARS = "LMN"
MAX_FAR = 8    # 首都からこれより遠い空港・港の横には、その領域の研究所を置かない


def hexdist(x0, y0, x1, y1):
    q0 = x0 - (y0 - (y0 & 1)) // 2
    q1 = x1 - (y1 - (y1 & 1)) // 2
    dq, dr = q0 - q1, y0 - y1
    return (abs(dq) + abs(dr) + abs(dq + dr)) // 2


NB_E = [(1, 0), (0, -1), (-1, -1), (-1, 0), (-1, 1), (0, 1)]
NB_O = [(1, 0), (1, -1), (0, -1), (-1, 0), (0, 1), (1, 1)]


def nbrs(x, y):
    return [(x + dx, y + dy) for dx, dy in (NB_O if (y & 1) else NB_E)]


def parse(text):
    rows = re.findall(r"^row\s*=\s*(\S+)\s*$", text, re.M)
    owners = {(int(x), int(y)): int(o) for x, y, o in
              re.findall(r"^own\s*=\s*(\d+),(\d+),(\d+)", text, re.M)}
    units = {(int(x), int(y)) for x, y in
             re.findall(r"^unit\s*=\s*\d+,\w+,(\d+),(\d+)", text, re.M)}
    return rows, owners, units


def plan(text):
    rows, owners, units = parse(text)
    if not rows or any(c in r for r in rows for c in LAB_CHARS):
        return []
    H, W = len(rows), len(rows[0])
    hqs = [(x, y, owners.get((x, y), -1)) for y, r in enumerate(rows)
           for x, c in enumerate(r) if c == "H"]
    hqs = [h for h in hqs if h[2] >= 0]
    if not hqs:
        return []
    cx, cy = (W - 1) / 2.0, (H - 1) / 2.0
    taken = set()
    out = []

    def ok(x, y, terrain):
        return (0 <= x < W and 0 <= y < H and rows[y][x] in terrain
                and (x, y) not in taken and (x, y) not in units)

    def pick_around(bx, by, terrain):
        for d in (1, 2, 3):
            cands = [(x, y) for y in range(H) for x in range(W)
                     if hexdist(bx, by, x, y) == d and ok(x, y, terrain)]
            if cands:
                return max(cands, key=lambda p: ((p[0] - cx) ** 2 + (p[1] - cy) ** 2, -p[1], -p[0]))
        return None

    def nearest(hx, hy, o, ch):
        c = [(hexdist(hx, hy, x, y), x, y) for y, r in enumerate(rows)
             for x, cc in enumerate(r) if cc == ch and owners.get((x, y), -1) == o]
        return min(c) if c else None

    done = set()
    for hx, hy, o in hqs:
        if o in done:
            continue
        found = []
        # 陸→空→海の順に決め、見つけたマスはすぐ使用済みにする
        # （同じマスに2つ置かないため）。首都から遠い基地の横は置かない
        # （MAX_FAR）。遠くに置くと、守れず奪い合いの的になるだけ。
        p = pick_around(hx, hy, ".")
        if p:
            found.append(("L", p)); taken.add(p)
        a = nearest(hx, hy, o, "a")
        if a and a[0] <= MAX_FAR:
            p = pick_around(a[1], a[2], ".")
            if p:
                found.append(("M", p)); taken.add(p)
        pt = nearest(hx, hy, o, "p")
        if pt and pt[0] <= MAX_FAR:
            p = pick_around(pt[1], pt[2], "~")
            if p:
                found.append(("N", p)); taken.add(p)
        for ch, p in found:
            out.append((p[0], p[1], o, ch))
        done.add(o)
        # 回転で重なる相手の首都があれば、同じ配置を回転して置く
        mx, my = W - 1 - hx, H - 1 - hy
        for hx2, hy2, o2 in hqs:
            if (hx2, hy2) == (mx, my) and o2 not in done:
                for ch, p in found:
                    q = (W - 1 - p[0], H - 1 - p[1])
                    tr = "~" if ch == "N" else "."
                    if ok(q[0], q[1], tr):
                        out.append((q[0], q[1], o2, ch)); taken.add(q)
                done.add(o2)
    return out


def apply(path, text, labs):
    nl = "\r\n" if "\r\n" in text else "\n"
    lines = text.split(nl)
    row_i = [i for i, l in enumerate(lines) if re.match(r"^row\s*=", l)]
    for x, y, o, ch in labs:
        i = row_i[y]
        pre, row = re.match(r"^(row\s*=\s*)(\S+)", lines[i]).groups()
        lines[i] = pre + row[:x] + ch + row[x + 1:]
    text = nl.join(lines)
    add = nl.join("own = %d,%d,%d" % (x, y, o) for x, y, o, ch in labs)
    m = re.search(r"^\[owners\][^\n]*\n", text, re.M)
    if m:
        text = text[:m.end()] + add + nl + text[m.end():]
    else:
        text = text.rstrip(nl) + nl + nl + "[owners]" + nl + add + nl
    io.open(path, "w", encoding="utf-8", newline="").write(text)


def clear(path, text):
    """置いた研究所を消す。L/M/N を元の地形（陸は平地、海の研究所は海）に戻す。"""
    nl = "\r\n" if "\r\n" in text else "\n"
    rows, owners, _ = parse(text)
    lines = text.split(nl)
    changed = False
    for i, l in enumerate(lines):
        m = re.match(r"^(row\s*=\s*)(\S+)", l)
        if not m:
            continue
        row = m.group(2)
        new = row.replace("L", ".").replace("M", ".").replace("N", "~")
        if new != row:
            lines[i] = m.group(1) + new
            changed = True
    text = nl.join(lines)
    # 研究所の own 行を消す（研究所の座標だけ）
    lab_pos = {(x, y) for y, r in enumerate(rows) for x, c in enumerate(r) if c in LAB_CHARS}
    keep = []
    for l in text.split(nl):
        m = re.match(r"^own\s*=\s*(\d+),(\d+),(\d+)", l)
        if m and (int(m.group(1)), int(m.group(2))) in lab_pos:
            continue
        keep.append(l)
    io.open(path, "w", encoding="utf-8", newline="").write(nl.join(keep))
    return changed


def main():
    do_apply = "--apply" in sys.argv
    do_clear = "--clear" in sys.argv
    for name in sorted(os.listdir(MAPS)):
        if not name.endswith(".map") or name == "test_arena.map":
            continue
        path = os.path.join(MAPS, name)
        text = io.open(path, encoding="utf-8", newline="").read()
        if do_clear:
            if clear(path, text):
                print("消した:", name)
            continue
        labs = plan(text)
        if not labs:
            continue
        kinds = {"L": "陸", "M": "空", "N": "海"}
        print("%-26s %s" % (name, " ".join("陣営%d%s(%d,%d)" % (o, kinds[ch], x, y)
                                           for x, y, o, ch in labs)))
        if do_apply:
            apply(path, text, labs)
    if not (do_apply or do_clear):
        print("--apply を付けると書き込む")


main()
