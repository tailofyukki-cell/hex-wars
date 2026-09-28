# -*- coding: utf-8 -*-
"""place_labs.py - 首都のあるマップに、陣営ごとの研究所（L）を1つずつ置く。

    python tools/place_labs.py           # 置く場所を表示するだけ
    python tools/place_labs.py --apply   # .map を書き換える

決まり:
  - 首都（H）の持ち主ごとに、首都から2マス離れた平地（.）に1つ。
    2マスに平地が無ければ3マスまで広げる。見つからなければ置かない。
  - 候補が複数あるときは、マップの中心から遠い（＝自陣の奥の）マスを選ぶ。
    前線に置くと、開幕すぐに奪い合いになってしまうため。
  - 180度回転で首都どうしが重なるマップ（対称な対戦マップ）では、
    片方の首都について決めた場所を回転して、もう片方にも置く（公平さを保つ）。
  - すでに研究所があるマップ、首都の無いマップ、test_arena は触らない。
"""
import io
import os
import re
import sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
MAPS = os.path.join(ROOT, "data", "maps")


def hexdist(x0, y0, x1, y1):
    q0 = x0 - (y0 - (y0 & 1)) // 2
    q1 = x1 - (y1 - (y1 & 1)) // 2
    dq, dr = q0 - q1, y0 - y1
    return (abs(dq) + abs(dr) + abs(dq + dr)) // 2


def plan(text):
    rows = re.findall(r"^row\s*=\s*(\S+)\s*$", text, re.M)
    if not rows or any("L" in r for r in rows):
        return []
    H, W = len(rows), len(rows[0])
    owners = {(int(x), int(y)): int(o) for x, y, o in
              re.findall(r"^own\s*=\s*(\d+),(\d+),(\d+)", text, re.M)}
    units = {(int(x), int(y)) for x, y in
             re.findall(r"^unit\s*=\s*\d+,\w+,(\d+),(\d+)", text, re.M)}
    hqs = [(x, y, owners.get((x, y), -1)) for y, r in enumerate(rows)
           for x, c in enumerate(r) if c == "H"]
    hqs = [h for h in hqs if h[2] >= 0]
    if not hqs:
        return []
    cx, cy = (W - 1) / 2.0, (H - 1) / 2.0
    taken = set()
    out = []

    def pick(hx, hy):
        for d in (2, 3):
            cands = [(x, y) for y in range(H) for x in range(W)
                     if rows[y][x] == "." and hexdist(hx, hy, x, y) == d
                     and (x, y) not in taken and (x, y) not in units]
            if cands:
                return max(cands, key=lambda p: ((p[0] - cx) ** 2 + (p[1] - cy) ** 2, -p[1], -p[0]))
        return None

    done = set()
    for hx, hy, o in hqs:
        if o in done:
            continue
        p = pick(hx, hy)
        if not p:
            continue
        out.append((p[0], p[1], o)); taken.add(p); done.add(o)
        # 回転で重なる相手の首都があれば、同じ場所を回転して置く
        mx, my = W - 1 - hx, H - 1 - hy
        for hx2, hy2, o2 in hqs:
            if (hx2, hy2) == (mx, my) and o2 not in done:
                q = (W - 1 - p[0], H - 1 - p[1])
                if rows[q[1]][q[0]] == "." and q not in taken and q not in units:
                    out.append((q[0], q[1], o2)); taken.add(q); done.add(o2)
    return out


def apply(path, text, labs):
    nl = "\r\n" if "\r\n" in text else "\n"
    lines = text.split(nl)
    row_i = [i for i, l in enumerate(lines) if re.match(r"^row\s*=", l)]
    for x, y, o in labs:
        i = row_i[y]
        pre, row = re.match(r"^(row\s*=\s*)(\S+)", lines[i]).groups()
        lines[i] = pre + row[:x] + "L" + row[x + 1:]
    text = nl.join(lines)
    add = nl.join("own = %d,%d,%d" % (x, y, o) for x, y, o in labs)
    m = re.search(r"^\[owners\][^\n]*\n", text, re.M)
    if m:
        text = text[:m.end()] + add + nl + text[m.end():]
    else:
        text = text.rstrip(nl) + nl + nl + "[owners]" + nl + add + nl
    io.open(path, "w", encoding="utf-8", newline="").write(text)


def main():
    do_apply = "--apply" in sys.argv
    for name in sorted(os.listdir(MAPS)):
        if not name.endswith(".map") or name == "test_arena.map":
            continue
        path = os.path.join(MAPS, name)
        text = io.open(path, encoding="utf-8", newline="").read()
        labs = plan(text)
        if not labs:
            continue
        print("%-26s %s" % (name, " ".join("陣営%d(%d,%d)" % (o, x, y) for x, y, o in labs)))
        if do_apply:
            apply(path, text, labs)
    if not do_apply:
        print("--apply を付けると書き込む")


main()
