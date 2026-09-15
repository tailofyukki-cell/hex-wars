# -*- coding: utf-8 -*-
"""陣営ごとのユニット名（units.def の name1〜name4）を生成する。

    python tools/gen_unit_names.py            # 貼る行を画面に出すだけ
    python tools/gen_unit_names.py --apply    # units.def に直接書き込む
    python tools/gen_unit_names.py --check    # 長さと重複だけ見る

陣営0（西方同盟）は units.def の `name =` をそのまま使うのでここには書かない。
`name` は全陣営に入り、`name1`〜`name4` が後勝ちで上書きする仕組み。

--------------------------------------------------------------------
陣営ごとの命名規則（**足すときはこの規則を崩さないこと**）
--------------------------------------------------------------------
陣営0 西方同盟 … 機能名＋「英字-数字型」。技術官僚的で無個性。（現行のまま）
陣営1 東方連邦 … 設計局コード式。陸=ZK(装甲)/SV(自走)/ZB(装輪)、
                 空=YA(戦闘・偵察)/IB(攻撃・輸送)/MV(回転翼)、艦=「計画n号」。
                 歩兵系は「人民」「労農」「親衛」を冠し、進化で「親衛」が付く。
陣営2 南方連合 … 生物名のみ。陸の装甲=獣、空=鳥、海=魚と海獣、歩兵=虫と小獣。
                 進化すると頭に「大」が付く。型式番号を一切持たない。
陣営3 北方同盟 … 「n式」の通し番号だけ。固有名を持たない。進化は「n式改」。
陣営4 自由軍   … 通称・あだ名。型式番号も番号も持たない。鹵獲品の寄せ集めを
                 兵が呼び習わしたもの、という設定。
--------------------------------------------------------------------
"""
import io
import os
import re
import sys

UNITS = os.path.join(os.path.dirname(__file__), "..", "data", "units.def")

# id: (陣営1 東方連邦, 陣営2 南方連合, 陣営3 北方同盟, 陣営4 自由軍)
NAMES = {
    # ---- 生産できるユニット ----
    "INFANTRY":      ("人民歩兵",              "野犬兵",   "一式歩兵",        "義勇兵"),
    "AT_INFANTRY":   ("破甲兵",                "蠍兵",     "二式対戦車兵",    "鉄拳持ち"),
    "RECON":         ("偵察装甲車「ZB-1」",     "鼬",       "三式偵察車",      "早駆け車"),
    "TANK":          ("中戦車「ZK-40」",        "豹",       "四式中戦車",      "鉄牛"),
    "HTANK":         ("重戦車「ZK-60」",        "犀",       "五式重戦車",      "山親父"),
    "ARTILLERY":     ("自走砲「SV-15」",        "象",       "六式自走砲",      "遠雷"),
    "AA_TANK":       ("対空自走砲「SV-8」",     "豪猪",     "七式対空戦車",    "弾幕車"),
    "TRUCK":         ("輸送車「ZB-3」",         "駄馬",     "八式輸送車",      "荷馬車"),
    "T_COPTER":      ("輸送回転翼機「MV-4」",   "鵜",       "九式輸送ヘリ",    "荷運び鳥"),
    "FIGHTER":       ("戦闘機「YA-9」",         "隼",       "十式戦闘機",      "風切り"),
    "BOMBER":        ("爆撃機「IB-17」",        "鷲",       "十一式爆撃機",    "鉄の雨"),
    "HELI":          ("攻撃回転翼機「MV-2」",   "蜂鷹",     "十二式攻撃ヘリ",  "羽虫"),
    "DESTROYER":     ("駆逐艦「計画3号」",      "鰹",       "十三式駆逐艦",    "走り舟"),
    "CRUISER":       ("巡洋艦「計画11号」",     "鮪",       "十四式巡洋艦",    "姉御"),
    "SUBMARINE":     ("潜水艦「計画21号」",     "鮫",       "十五式潜水艦",    "潜り屋"),
    "T_SHIP":        ("揚陸艦「計画32号」",     "鯨",       "十六式輸送艦",    "渡し舟"),
    "SUPPLY":        ("補給車「ZB-5」",         "駱駝",     "十七式補給車",    "炊き出し車"),
    "SUPPLY_AIR":    ("補給機「IB-25」",        "鵲",       "十八式補給機",    "差し入れ便"),
    "MILITIA":       ("労農兵",                "村衆",     "十九式郷土兵",    "町衆"),
    "MECH_INF":      ("自動車化狙撃兵",         "狼騎兵",   "二十式機械化歩兵", "相乗り兵"),
    "ENGINEER":      ("人民工兵",              "蟻兵",     "二十一式工兵",    "穴掘り"),
    "LTANK":         ("軽戦車「ZK-20」",        "山猫",     "二十二式軽戦車",  "跳ね兎"),
    "ROCKET":        ("噴進砲「SV-8」",         "猪",       "二十三式噴進砲",  "火吹き車"),
    "AA_GUN":        ("高射砲「SV-2」",         "針鼠",     "二十四式高射砲",  "空狙い"),
    "DIVE_BOMBER":   ("襲撃機「IB-6」",         "鳶",       "二十五式襲撃機",  "落とし屋"),
    "SCOUT_PLANE":   ("偵察機「YA-3」",         "雲雀",     "二十六式偵察機",  "見張り鳥"),
    "BATTLESHIP":    ("戦艦「計画9号」",        "鯱",       "二十七式戦艦",    "老大公"),
    "CARRIER":       ("航空母艦「計画41号」",   "鱏",       "二十八式空母",    "鳥籠"),
    "GUNBOAT":       ("砲艦「計画12号」",       "河豚",     "二十九式砲艦",    "番船"),
    "MISSILE_BOAT":  ("噴進艇「計画15号」",     "飛魚",     "三十式噴進艇",    "一撃舟"),
    "SUPPLY_SHIP":   ("補給艦「計画26号」",     "海豚",     "三十一式補給艦",  "母船"),
    "T_PLANE":       ("輸送機「IB-8」",         "鸛",       "三十二式輸送機",  "空の渡し"),
    "NIGHT_INF":     ("夜襲兵",                "蝙蝠兵",   "三十三式夜戦兵",  "闇討ち"),
    "NIGHT_BOAT":    ("夜襲艇「計画44号」",     "烏賊",     "三十四式夜戦艇",  "闇舟"),
    "NIGHT_FIGHTER": ("夜間迎撃機「YA-24」",    "梟",       "三十五式夜戦機",  "夜鷹"),

    # ---- 進化専用 ----
    "INFANTRY_V":      ("親衛歩兵",                "猟犬兵",   "一式改歩兵",        "古参兵"),
    "AT_INFANTRY_V":   ("親衛破甲兵",              "大蠍兵",   "二式改対戦車兵",    "戦車殺し"),
    "RECON_V":         ("装甲偵察車「ZB-6」",      "大鼬",     "三式改偵察車",      "先駆け"),
    "TANK_V":          ("主力戦車「ZK-55」",       "黒豹",     "四式改中戦車",      "大鉄牛"),
    "HTANK_V":         ("超重戦車「ZK-90」",       "大犀",     "五式改重戦車",      "動く砦"),
    "ARTILLERY_V":     ("重自走砲「SV-24」",       "大象",     "六式改自走砲",      "大遠雷"),
    "AA_TANK_V":       ("対空機関砲「SV-18」",     "大豪猪",   "七式改対空戦車",    "弾幕改車"),
    "TRUCK_V":         ("装甲輸送車「ZB-8」",      "大駄馬",   "八式改輸送車",      "鉄の荷車"),
    "T_COPTER_V":      ("強襲回転翼機「MV-9」",    "大鵜",     "九式改輸送ヘリ",    "大荷運び鳥"),
    "FIGHTER_V":       ("迎撃機「YA-12」",         "大隼",     "十式改戦闘機",      "風斬り"),
    "BOMBER_V":        ("重爆撃機「IB-24」",       "大鷲",     "十一式改爆撃機",    "鉄の驟雨"),
    "HELI_V":          ("重攻撃回転翼機「MV-8」",  "大蜂鷹",   "十二式改攻撃ヘリ",  "大羽虫"),
    "DESTROYER_V":     ("重駆逐艦「計画8号」",     "大鰹",     "十三式改駆逐艦",    "大走り舟"),
    "CRUISER_V":       ("重巡洋艦「計画61号」",    "大鮪",     "十四式改巡洋艦",    "大姉御"),
    "SUBMARINE_V":     ("大型潜水艦「計画24号」",  "大鮫",     "十五式改潜水艦",    "深潜り"),
    "T_SHIP_V":        ("強襲揚陸艦「計画36号」",  "大鯨",     "十六式改輸送艦",    "大渡し舟"),
    "SUPPLY_V":        ("重補給車「ZB-9」",        "大駱駝",   "十七式改補給車",    "大炊き出し車"),
    "SUPPLY_AIR_V":    ("大型補給機「IB-29」",     "大鵲",     "十八式改補給機",    "大差し入れ便"),
    "MILITIA_V":       ("労農親衛隊",              "番犬衆",   "十九式改郷土兵",    "町の守り"),
    "MECH_INF_V":      ("親衛狙撃兵",              "大狼騎兵", "二十式改機械化歩兵", "鉄箱乗り"),
    "ENGINEER_V":      ("親衛工兵",                "大蟻兵",   "二十一式改工兵",    "大穴掘り"),
    "LTANK_V":         ("騎兵戦車「ZK-24」",       "大山猫",   "二十二式改軽戦車",  "跳ね馬"),
    "ROCKET_V":        ("多連装噴進砲「SV-16」",   "大猪",     "二十三式改噴進砲",  "火柱車"),
    "AA_GUN_V":        ("重高射砲「SV-12」",       "大針鼠",   "二十四式改高射砲",  "天突き"),
    "DIVE_BOMBER_V":   ("対地攻撃機「IB-9」",      "大鳶",     "二十五式改襲撃機",  "地均し"),
    "SCOUT_PLANE_V":   ("高高度偵察機「YA-8」",    "大雲雀",   "二十六式改偵察機",  "高見の鳥"),
    "BATTLESHIP_V":    ("超弩級戦艦「計画14号」",  "大鯱",     "二十七式改戦艦",    "海の主"),
    "CARRIER_V":       ("大型航空母艦「計画49号」", "大鱏",    "二十八式改空母",    "大鳥籠"),
    "GUNBOAT_V":       ("重砲艦「計画16号」",      "大河豚",   "二十九式改砲艦",    "大番船"),
    "MISSILE_BOAT_V":  ("高速噴進艇「計画19号」",  "大飛魚",   "三十式改噴進艇",    "早舟"),
    "SUPPLY_SHIP_V":   ("高速補給艦「計画29号」",  "大海豚",   "三十一式改補給艦",  "大母船"),
    "T_PLANE_V":       ("大型輸送機「IB-14」",     "大鸛",     "三十二式改輸送機",  "大空の渡し"),
    "NIGHT_INF_V":     ("親衛夜襲兵",              "大蝙蝠兵", "三十三式改夜戦兵",  "闇の手練"),
    "NIGHT_BOAT_V":    ("夜襲強襲艇「計画64号」",  "大烏賊",   "三十四式改夜戦艇",  "黒舟"),
    "NIGHT_FIGHTER_V": ("高高度夜戦機「YA-36」",   "大梟",     "三十五式改夜戦機",  "夜の主"),
}

MAXLEN = 47   # name[48] の終端ぶんを引いた上限（parser がこれを超えると読込エラー）
FACTIONS = ["西方同盟", "東方連邦", "南方連合", "北方同盟", "自由軍"]


def read_units():
    return io.open(UNITS, encoding="utf-8").read()


def roster(src):
    """units.def に出てくる順で (id, 陣営0の名前) を返す。"""
    out = []
    for b in re.split(r"^\[unit ", src, flags=re.M)[1:]:
        uid = b.split("]")[0].strip()
        m = re.search(r"^name\s*=\s*(.+?)\s*$", b, re.M)
        out.append((uid, m.group(1) if m else ""))
    return out


def check(src):
    """長さ・重複・取りこぼしを見る。問題があれば False。"""
    ok = True
    ids = [u for u, _ in roster(src)]
    missing = [u for u in ids if u not in NAMES]
    extra = [u for u in NAMES if u not in ids]
    if missing:
        print("× 名前が未設定:", ", ".join(missing)); ok = False
    if extra:
        print("× units.def に無いID:", ", ".join(extra)); ok = False

    for uid, n0 in roster(src):
        for i, nm in enumerate([n0] + list(NAMES.get(uid, ("",) * 4))):
            b = len(nm.encode("utf-8"))
            if b > MAXLEN:
                print("× 長すぎ %dB  %s 陣営%d %s" % (b, uid, i, nm)); ok = False

    # 同じ陣営の中で名前がぶつかっていないか（どれを選んだか分からなくなる）
    for f in range(1, 5):
        seen = {}
        for uid in ids:
            nm = NAMES.get(uid, ("",) * 4)[f - 1]
            if nm in seen:
                print("× 陣営%d で重複: %s（%s と %s）" % (f, nm, seen[nm], uid)); ok = False
            seen[nm] = uid
    print("OK" if ok else "NG")
    return ok


def lines_for(uid):
    return ["name%d     = %s" % (i + 1, nm) for i, nm in enumerate(NAMES[uid])]


def apply_to_units():
    src = read_units()
    if not check(src):
        print("問題があるので書き込みませんでした")
        return 1
    out, n = [], 0
    for chunk in re.split(r"(^\[unit .*?\]$)", src, flags=re.M):
        m = re.match(r"^\[unit (.+?)\]$", chunk)
        out.append(chunk)
        if not m:
            continue
        uid = m.group(1).strip()
        # すでに name1 があるものは触らない（何度実行しても壊れない）
        n += 1
    # セクション単位で name 行の直後に差し込む
    text = src
    for uid in NAMES:
        blk = re.search(r"(^\[unit %s\]\n(?:.*\n)*?name\s*=.*\n)" % re.escape(uid),
                        text, re.M)
        if not blk:
            print("? 見つからない:", uid); continue
        after = text[blk.end(1):blk.end(1) + 400]
        if re.match(r"name[1-4]\s*=", after):
            continue                      # すでに入っている
        ins = "".join(l + "\n" for l in lines_for(uid))
        text = text[:blk.end(1)] + ins + text[blk.end(1):]
    io.open(UNITS, "w", encoding="utf-8", newline="").write(text)
    print("units.def に書き込みました（%d 種）" % len(NAMES))
    return 0


def preview():
    src = read_units()
    w = 26
    print("  ".join(["%-18s" % "ID"] + ["%-*s" % (w, f) for f in FACTIONS]))
    print("-" * 150)
    for uid, n0 in roster(src):
        row = [n0] + list(NAMES.get(uid, ("",) * 4))
        print("  ".join(["%-18s" % uid] + ["%-*s" % (w, x) for x in row]))
    print()
    check(src)


if __name__ == "__main__":
    arg = sys.argv[1] if len(sys.argv) > 1 else ""
    if arg == "--apply":
        sys.exit(apply_to_units())
    elif arg == "--check":
        sys.exit(0 if check(read_units()) else 1)
    else:
        preview()
