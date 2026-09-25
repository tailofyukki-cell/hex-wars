# -*- coding: utf-8 -*-
"""陣営ごとのユニット名（units.def の name1〜name4）を生成する。

    python tools/gen_unit_names.py            # 貼る行を画面に出すだけ
    python tools/gen_unit_names.py --apply    # units.def に直接書き込む
    python tools/gen_unit_names.py --check    # 長さと重複だけ見る（日本語・英語とも）
    python tools/gen_unit_names.py --apply-en # 英語名（name@en 等）を units.def に書き込む

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
    # 駆潜艇。東方は実在の命名（小型対潜艦→大型対潜艦）に寄せ、南方は水中の気配を
    # ひげで探る海豹、自由軍は「潜り屋」を狩る者、進化形は耳の良さから「地獄耳」。
    "SUBCHASER":     ("小型対潜艦「計画52号」", "海豹",     "三十六式駆潜艇",  "潜り狩り"),

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
    "SUBCHASER_V":     ("大型対潜艦「計画56号」",  "大海豹",   "三十六式改駆潜艇",  "地獄耳"),
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


# ====================================================================
# 英語名（name@en / name1@en 〜 name4@en）
# 各陣営の命名規則は日本語と同じ考え方で英語にしてある:
#   西方 = 型式＋機能名 / 東方 = 設計局コード・Project n / 南方 = 生き物（進化で Great）
#   北方 = Type n（進化で Type n Kai）/ 自由軍 = あだ名
# ====================================================================
# id: (陣営0 西方, 陣営1 東方, 陣営2 南方, 陣営3 北方, 陣営4 自由軍)
NAMES_EN = {
    "INFANTRY":      ("Infantry", "People's Infantry", "Wild Dog Troops", "Type 1 Infantry", "Volunteers"),
    "AT_INFANTRY":   ("Anti-Tank Infantry", "Armor Breakers", "Scorpion Troops", "Type 2 AT Infantry", "Iron Fists"),
    "RECON":         ("Recon Car", "ZB-1 Scout Car", "Weasel", "Type 3 Recon Car", "Fast Runner"),
    "TANK":          ("T-40 Medium Tank", "ZK-40 Medium Tank", "Leopard", "Type 4 Medium Tank", "Iron Ox"),
    "HTANK":         ("G-6 Heavy Tank", "ZK-60 Heavy Tank", "Rhino", "Type 5 Heavy Tank", "Old Bear"),
    "ARTILLERY":     ("H-15 SP Gun", "SV-15 SP Gun", "Elephant", "Type 6 SP Gun", "Distant Thunder"),
    "AA_TANK":       ("F-8 AA Tank", "SV-8 SP AA Gun", "Porcupine", "Type 7 AA Tank", "Flak Wagon"),
    "TRUCK":         ("Truck", "ZB-3 Transport", "Mule", "Type 8 Transport", "Horse Cart"),
    "T_COPTER":      ("Hv-4 Transport Heli", "MV-4 Transport Rotorcraft", "Cormorant", "Type 9 Transport Heli", "Pack Bird"),
    "FIGHTER":       ("Fk-9 Fighter", "YA-9 Fighter", "Falcon", "Type 10 Fighter", "Windcutter"),
    "BOMBER":        ("Bx-17 Bomber", "IB-17 Bomber", "Eagle", "Type 11 Bomber", "Iron Rain"),
    "HELI":          ("Hv-2 Attack Heli", "MV-2 Attack Rotorcraft", "Honey Buzzard", "Type 12 Attack Heli", "Gnat"),
    "DESTROYER":     ("Dn-3 Destroyer", "Project 3 Destroyer", "Bonito", "Type 13 Destroyer", "Runner Boat"),
    "CRUISER":       ("Kv-1 Cruiser", "Project 11 Cruiser", "Tuna", "Type 14 Cruiser", "Big Sister"),
    "SUBMARINE":     ("U-11 Submarine", "Project 21 Submarine", "Shark", "Type 15 Submarine", "Diver"),
    "T_SHIP":        ("Ln-2 Transport Ship", "Project 32 Landing Ship", "Whale", "Type 16 Transport Ship", "Ferry"),
    "SUPPLY":        ("Supply Truck", "ZB-5 Supply Truck", "Camel", "Type 17 Supply Truck", "Field Kitchen"),
    "SUPPLY_AIR":    ("Cg-5 Supply Plane", "IB-25 Supply Plane", "Magpie", "Type 18 Supply Plane", "Care Package"),
    "MILITIA":       ("Militia", "Worker-Peasant Troops", "Village Band", "Type 19 Home Guard", "Townsfolk"),
    "MECH_INF":      ("Mechanized Infantry", "Motor Rifles", "Wolf Riders", "Type 20 Mech Infantry", "Hitchhikers"),
    "ENGINEER":      ("Engineers", "People's Engineers", "Ant Troops", "Type 21 Engineers", "Diggers"),
    "LTANK":         ("T-20 Light Tank", "ZK-20 Light Tank", "Wildcat", "Type 22 Light Tank", "Jackrabbit"),
    "ROCKET":        ("R-8 Rocket Artillery", "SV-8 Rocket Artillery", "Boar", "Type 23 Rocket Artillery", "Fire Breather"),
    "AA_GUN":        ("AA Gun", "SV-2 AA Gun", "Hedgehog", "Type 24 AA Gun", "Sky Sniper"),
    "DIVE_BOMBER":   ("Sk-6 Attacker", "IB-6 Attacker", "Kite", "Type 25 Attacker", "Dropper"),
    "SCOUT_PLANE":   ("Mk-3 Recon Plane", "YA-3 Recon Plane", "Skylark", "Type 26 Recon Plane", "Lookout Bird"),
    "BATTLESHIP":    ("Rz-9 Battleship", "Project 9 Battleship", "Orca", "Type 27 Battleship", "Old Duke"),
    "CARRIER":       ("Hy-4 Carrier", "Project 41 Carrier", "Manta", "Type 28 Carrier", "Birdcage"),
    "GUNBOAT":       ("Bt-2 Gunboat", "Project 12 Gunboat", "Pufferfish", "Type 29 Gunboat", "Watch Boat"),
    "MISSILE_BOAT":  ("Fk-5 Missile Boat", "Project 15 Rocket Boat", "Flying Fish", "Type 30 Rocket Boat", "One-Shot Boat"),
    "SUPPLY_SHIP":   ("Ho-6 Supply Ship", "Project 26 Supply Ship", "Dolphin", "Type 31 Supply Ship", "Mother Ship"),
    "T_PLANE":       ("Cd-8 Transport Plane", "IB-8 Transport Plane", "Stork", "Type 32 Transport Plane", "Sky Ferry"),
    "NIGHT_INF":     ("Night Stalkers", "Night Assault Troops", "Bat Troops", "Type 33 Night Infantry", "Night Blades"),
    "NIGHT_BOAT":    ("Night Assault Boat", "Project 44 Night Boat", "Squid", "Type 34 Night Boat", "Dark Boat"),
    "NIGHT_FIGHTER": ("Night Interceptor", "YA-24 Night Interceptor", "Owl", "Type 35 Night Fighter", "Nighthawk"),
    "SUBCHASER":     ("Uj-2 Subchaser", "Project 52 Small ASW Ship", "Seal", "Type 36 Subchaser", "Sub Hunter"),

    "INFANTRY_V":      ("Elite Jaegers", "Guards Infantry", "Hound Troops", "Type 1 Kai Infantry", "Veterans"),
    "AT_INFANTRY_V":   ("Tank Hunters", "Guards Armor Breakers", "Great Scorpion Troops", "Type 2 Kai AT Infantry", "Tank Killers"),
    "RECON_V":         ("Armored Recon Car", "ZB-6 Armored Scout Car", "Great Weasel", "Type 3 Kai Recon Car", "Vanguard"),
    "TANK_V":          ("T-55 Main Battle Tank", "ZK-55 Main Battle Tank", "Black Panther", "Type 4 Kai Medium Tank", "Great Iron Ox"),
    "HTANK_V":         ("G-9 Super-Heavy Tank", "ZK-90 Super-Heavy Tank", "Great Rhino", "Type 5 Kai Heavy Tank", "Moving Fortress"),
    "ARTILLERY_V":     ("H-24 Heavy SP Gun", "SV-24 Heavy SP Gun", "Great Elephant", "Type 6 Kai SP Gun", "Great Thunder"),
    "AA_TANK_V":       ("SP AA Autocannon", "SV-18 AA Autocannon", "Great Porcupine", "Type 7 Kai AA Tank", "Wall of Flak"),
    "TRUCK_V":         ("Armored Personnel Carrier", "ZB-8 Armored Transport", "Great Mule", "Type 8 Kai Transport", "Iron Wagon"),
    "T_COPTER_V":      ("Hv-9 Assault Transport Heli", "MV-9 Assault Rotorcraft", "Great Cormorant", "Type 9 Kai Transport Heli", "Big Pack Bird"),
    "FIGHTER_V":       ("Fk-12 Interceptor", "YA-12 Interceptor", "Great Falcon", "Type 10 Kai Fighter", "Windslicer"),
    "BOMBER_V":        ("Bx-24 Heavy Bomber", "IB-24 Heavy Bomber", "Great Eagle", "Type 11 Kai Bomber", "Iron Downpour"),
    "HELI_V":          ("Hv-8 Heavy Attack Heli", "MV-8 Heavy Rotorcraft", "Great Honey Buzzard", "Type 12 Kai Attack Heli", "Big Gnat"),
    "DESTROYER_V":     ("Dn-8 Heavy Destroyer", "Project 8 Heavy Destroyer", "Great Bonito", "Type 13 Kai Destroyer", "Big Runner Boat"),
    "CRUISER_V":       ("Kv-6 Heavy Cruiser", "Project 61 Heavy Cruiser", "Great Tuna", "Type 14 Kai Cruiser", "Grand Dame"),
    "SUBMARINE_V":     ("U-24 Large Submarine", "Project 24 Large Submarine", "Great Shark", "Type 15 Kai Submarine", "Deep Diver"),
    "T_SHIP_V":        ("Ln-6 Assault Landing Ship", "Project 36 Assault Ship", "Great Whale", "Type 16 Kai Transport Ship", "Big Ferry"),
    "SUPPLY_V":        ("Heavy Supply Truck", "ZB-9 Heavy Supply Truck", "Great Camel", "Type 17 Kai Supply Truck", "Big Field Kitchen"),
    "SUPPLY_AIR_V":    ("Cg-9 Large Supply Plane", "IB-29 Large Supply Plane", "Great Magpie", "Type 18 Kai Supply Plane", "Big Care Package"),
    "MILITIA_V":       ("Home Defense Corps", "Worker-Peasant Guards", "Watchdogs", "Type 19 Kai Home Guard", "Town Wardens"),
    "MECH_INF_V":      ("Panzergrenadiers", "Guards Rifles", "Great Wolf Riders", "Type 20 Kai Mech Infantry", "Tin Can Riders"),
    "ENGINEER_V":      ("Armored Engineers", "Guards Engineers", "Great Ant Troops", "Type 21 Kai Engineers", "Master Diggers"),
    "LTANK_V":         ("T-24 Cavalry Tank", "ZK-24 Cavalry Tank", "Great Wildcat", "Type 22 Kai Light Tank", "Bucking Horse"),
    "ROCKET_V":        ("R-16 Multiple Rocket Launcher", "SV-16 Multiple Rocket Launcher", "Great Boar", "Type 23 Kai Rocket Artillery", "Pillar of Fire"),
    "AA_GUN_V":        ("Type 88 Heavy AA Gun", "SV-12 Heavy AA Gun", "Great Hedgehog", "Type 24 Kai AA Gun", "Sky Piercer"),
    "DIVE_BOMBER_V":   ("Sk-9 Ground Attacker", "IB-9 Ground Attacker", "Great Kite", "Type 25 Kai Attacker", "Steamroller"),
    "SCOUT_PLANE_V":   ("Mk-8 High-Altitude Recon", "YA-8 High-Altitude Recon", "Great Skylark", "Type 26 Kai Recon Plane", "High Watcher"),
    "BATTLESHIP_V":    ("Rz-14 Super-Dreadnought", "Project 14 Super-Dreadnought", "Great Orca", "Type 27 Kai Battleship", "Lord of the Sea"),
    "CARRIER_V":       ("Hy-9 Fleet Carrier", "Project 49 Fleet Carrier", "Great Manta", "Type 28 Kai Carrier", "Great Birdcage"),
    "GUNBOAT_V":       ("Bt-6 Heavy Gunboat", "Project 16 Heavy Gunboat", "Great Pufferfish", "Type 29 Kai Gunboat", "Big Watch Boat"),
    "MISSILE_BOAT_V":  ("Fk-9 Fast Missile Boat", "Project 19 Fast Rocket Boat", "Great Flying Fish", "Type 30 Kai Rocket Boat", "Quick Boat"),
    "SUPPLY_SHIP_V":   ("Ho-9 Fast Supply Ship", "Project 29 Fast Supply Ship", "Great Dolphin", "Type 31 Kai Supply Ship", "Big Mother Ship"),
    "T_PLANE_V":       ("Cd-14 Heavy Transport", "IB-14 Heavy Transport", "Great Stork", "Type 32 Kai Transport Plane", "Big Sky Ferry"),
    "NIGHT_INF_V":     ("Shadow Operatives", "Guards Night Assault Troops", "Great Bat Troops", "Type 33 Kai Night Infantry", "Masters of the Dark"),
    "NIGHT_BOAT_V":    ("Black Tide Raider", "Project 64 Night Assault Boat", "Great Squid", "Type 34 Kai Night Boat", "Black Boat"),
    "NIGHT_FIGHTER_V": ("Night Sky Interceptor", "YA-36 High-Altitude Night Fighter", "Great Owl", "Type 35 Kai Night Fighter", "Lord of the Night"),
    "SUBCHASER_V":     ("Uj-7 Large Subchaser", "Project 56 Large ASW Ship", "Great Seal", "Type 36 Kai Subchaser", "Devil's Ear"),
}


def check_en(src):
    ok = True
    ids = [u for u, _ in roster(src)]
    for u in ids:
        if u not in NAMES_EN:
            print("x 英語名が未設定:", u); ok = False
    for uid, row in NAMES_EN.items():
        if uid not in ids:
            print("x units.def に無いID:", uid); ok = False
        for i, nm in enumerate(row):
            if len(nm.encode("utf-8")) > MAXLEN:
                print("x 長すぎ", uid, i, nm); ok = False
            if "#" in nm:
                print("x # はコメント扱いになる", uid, nm); ok = False
    for f in range(5):
        seen = {}
        for uid in ids:
            nm = NAMES_EN.get(uid, ("",) * 5)[f]
            if nm in seen:
                print("x EN 陣営%d で重複: %s（%s と %s）" % (f, nm, seen[nm], uid)); ok = False
            seen[nm] = uid
    return ok


def apply_en():
    """name@en / name1@en 〜 name4@en を、その兵種の最後の name 行の直後に入れる。"""
    src = read_units()
    if not check_en(src):
        print("問題があるので書き込みませんでした")
        return 1
    text = src
    n = 0
    for uid, row in NAMES_EN.items():
        blk = re.search(r"^\[unit %s\]\n((?:(?!\[unit ).*\n)*)" % re.escape(uid), text, re.M)
        if not blk:
            print("? 見つからない:", uid); continue
        body = blk.group(1)
        if re.search(r"^name@en\s*=", body, re.M):
            continue                                  # すでに入っている
        last = None
        for m in re.finditer(r"^name[0-4]?\s*=.*\n", body, re.M):
            last = m
        if not last:
            print("? name が無い:", uid); continue
        ins = "name@en   = %s\n" % row[0] + "".join(
            "name%d@en  = %s\n" % (i, nm) for i, nm in enumerate(row) if i > 0)
        pos = blk.start(1) + last.end()
        text = text[:pos] + ins + text[pos:]
        n += 1
    io.open(UNITS, "w", encoding="utf-8", newline="").write(text)
    print("units.def に英語名を書き込みました（%d 種）" % n)
    return 0


if __name__ == "__main__":
    arg = sys.argv[1] if len(sys.argv) > 1 else ""
    if arg == "--apply":
        sys.exit(apply_to_units())
    elif arg == "--apply-en":
        sys.exit(apply_en())
    elif arg == "--check":
        src = read_units()
        ok = check(src)
        ok = check_en(src) and ok
        sys.exit(0 if ok else 1)
    else:
        preview()
