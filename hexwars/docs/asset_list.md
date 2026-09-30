# 差し替えできる画像の一覧

すべて `hexwars/assets/` からの相対パスで、**データ定義に書いたパスがそのまま読まれる**。同じ名前で置き換えれば再ビルドなしで反映される（`assets/` は実行フォルダへ自動同期される）。

ファイルが無い場合は落ちずにフォールバックする（地形は `color` の単色、指揮官は名前の文字、ユニットは図形描画）。**先に絵の無いものから用意すれば、それだけ見た目が上がる。**

| | 数 |
|---|---|
| 参照されているパス | 107 |
| うち実体あり | 78 |
| うち**未用意** | **29** |


## 地形（マップのセル）（19本）

定義場所: `data/terrain.def` の `image =`

真上から見た地面の模様。六角形に切り抜いて貼られるので、正方形のタイル画像でよい。斜め見下ろし表示でも同じ画像を使う（別途用意は不要）。

| パス | 用途 | 現物 | 実寸 |
|---|---|---|---|
| `gfx/terrain/plain.png` | PLAIN（Plains） | ○ | 192x192 |
| `gfx/terrain/road.png` | ROAD（Road） | ○ | 192x192 |
| `gfx/terrain/forest.png` | FOREST（Forest） | ○ | 192x192 |
| `gfx/terrain/mountain.png` | MOUNTAIN（Mountain） | ○ | 192x192 |
| `gfx/terrain/river.png` | RIVER（River） | ○ | 192x192 |
| `gfx/terrain/sea.png` | SEA（Sea） | ○ | 192x192 |
| `gfx/terrain/city.png` | CITY（City） | ○ | 192x192 |
| `gfx/terrain/lab.png` | LAB_LAND（Land Laboratory） | **未** | - |
| `gfx/terrain/lab_air.png` | LAB_AIR（Air Laboratory） | **未** | - |
| `gfx/terrain/lab_sea.png` | LAB_SEA（Naval Laboratory） | **未** | - |
| `gfx/terrain/factory.png` | FACTORY（Factory） | ○ | 192x192 |
| `gfx/terrain/airport.png` | AIRPORT（Airport） | ○ | 192x192 |
| `gfx/terrain/port.png` | PORT（Port） | ○ | 192x192 |
| `gfx/terrain/hill.png` | HILL（Hill） | ○ | 192x192 |
| `gfx/terrain/hq.png` | HQ（HQ） | ○ | 192x192 |
| `gfx/terrain/airstrip.png` | AIRSTRIP（Airstrip） | **未** | - |
| `gfx/terrain/anchorage.png` | ANCHORAGE（Anchorage） | **未** | - |
| `gfx/terrain/rubble.png` | RUBBLE（Rubble） | **未** | - |
| `gfx/terrain/rubble_port.png` | RUBBLE_PORT（Ruined Port） | **未** | - |

## ユニット（45本）

定義場所: `data/units.def` の `image = / image0 =` 〜 `image4 =`

`image` は全陣営共通で `gfx/units/common/` に置く。`image0`〜`image4` はその陣営だけ差し替えたいときで、`gfx/units/faction0/`〜`faction4/` に置く。背景は透過。`anim` は戦闘アニメ、`cutin` は攻撃時の1枚絵。

| パス | 用途 | 現物 | 実寸 |
|---|---|---|---|
| `gfx/units/common/infantry.png` | INFANTRY ほか1件 | ○ | 96x96 |
| `gfx/cutin/infantry.png` | INFANTRY ほか7件 | ○ | 520x900 |
| `gfx/units/common/at_infantry.png` | AT_INFANTRY ほか1件 | ○ | 96x96 |
| `gfx/units/common/recon.png` | RECON ほか1件 | ○ | 96x96 |
| `gfx/units/common/tank.png` | TANK ほか1件 | ○ | 96x96 |
| `gfx/cutin/tank.png` | TANK ほか9件 | ○ | 520x900 |
| `gfx/anim/sample_land.mp4` | TANK | ○ | - |
| `gfx/units/common/htank.png` | HTANK ほか1件 | ○ | 96x96 |
| `gfx/units/common/artillery.png` | ARTILLERY ほか1件 | ○ | 96x96 |
| `gfx/units/common/aa_tank.png` | AA_TANK ほか1件 | ○ | 96x96 |
| `gfx/units/common/truck.png` | TRUCK ほか1件 | ○ | 96x96 |
| `gfx/units/common/t_copter.png` | T_COPTER ほか1件 | ○ | 96x96 |
| `gfx/units/common/fighter.png` | FIGHTER ほか1件 | ○ | 96x96 |
| `gfx/cutin/air.png` | FIGHTER ほか11件 | ○ | 520x900 |
| `gfx/units/common/bomber.png` | BOMBER ほか1件 | ○ | 96x96 |
| `gfx/units/common/heli.png` | HELI ほか1件 | ○ | 96x96 |
| `gfx/units/common/destroyer.png` | DESTROYER ほか1件 | ○ | 96x96 |
| `gfx/cutin/ship.png` | DESTROYER ほか11件 | ○ | 520x900 |
| `gfx/units/common/cruiser.png` | CRUISER ほか1件 | ○ | 96x96 |
| `gfx/units/common/submarine.png` | SUBMARINE ほか1件 | ○ | 96x96 |
| `gfx/units/common/t_ship.png` | T_SHIP ほか1件 | ○ | 96x96 |
| `gfx/units/common/supply.png` | SUPPLY ほか1件 | ○ | 96x96 |
| `gfx/units/common/supply_air.png` | SUPPLY_AIR ほか1件 | ○ | 96x96 |
| `gfx/units/common/militia.png` | MILITIA ほか1件 | ○ | 96x96 |
| `gfx/units/common/mech_inf.png` | MECH_INF ほか1件 | ○ | 96x96 |
| `gfx/units/common/engineer.png` | ENGINEER ほか1件 | **未** | - |
| `gfx/units/common/ltank.png` | LTANK ほか1件 | ○ | 96x96 |
| `gfx/units/common/rocket.png` | ROCKET ほか1件 | ○ | 96x96 |
| `gfx/units/common/aa_gun.png` | AA_GUN ほか1件 | ○ | 96x96 |
| `gfx/units/common/dive_bomber.png` | DIVE_BOMBER ほか1件 | ○ | 96x96 |
| `gfx/units/common/scout_plane.png` | SCOUT_PLANE ほか1件 | ○ | 96x96 |
| `gfx/units/common/battleship.png` | BATTLESHIP ほか1件 | ○ | 96x96 |
| `gfx/anim/sample_sea.gif` | BATTLESHIP | ○ | 640x360 |
| `gfx/units/common/carrier.png` | CARRIER ほか1件 | ○ | 96x96 |
| `gfx/units/common/gunboat.png` | GUNBOAT ほか1件 | ○ | 96x96 |
| `gfx/units/common/missile_boat.png` | MISSILE_BOAT ほか1件 | ○ | 96x96 |
| `gfx/units/common/supply_ship.png` | SUPPLY_SHIP ほか1件 | ○ | 96x96 |
| `gfx/units/common/t_plane.png` | T_PLANE ほか1件 | ○ | 96x96 |
| `gfx/units/common/night_inf.png` | NIGHT_INF（Night Blades） ほか1件 | **未** | - |
| `gfx/units/common/night_boat.png` | NIGHT_BOAT（Dark Boat） ほか1件 | **未** | - |
| `gfx/units/common/night_fighter.png` | NIGHT_FIGHTER（Nighthawk） ほか1件 | **未** | - |
| `gfx/units/common/subchaser.png` | SUBCHASER ほか1件 | ○ | 96x96 |
| `gfx/units/common/sw_tank.png` | SW_TANK ほか1件 | **未** | - |
| `gfx/units/common/sw_fortress.png` | SW_FORTRESS ほか1件 | **未** | - |
| `gfx/units/common/sw_battleship.png` | SW_BATTLESHIP ほか1件 | **未** | - |

## 指揮官（11本）

定義場所: `data/commanders.def` の `image = / cutin =`

`image` は指揮官選択と全体図に出す顔絵（縦長）。`cutin` は必殺技の1枚絵。

| パス | 用途 | 現物 | 実寸 |
|---|---|---|---|
| `gfx/cutin/co_west.png` | GRAF（General Graf） ほか4件 | ○ | 520x900 |
| `gfx/co/graf.png` | GRAF（General Graf） | ○ | 480x640 |
| `gfx/co/liese.png` | LIESE（Lt. Col. Liese） | ○ | 480x640 |
| `gfx/cutin/co_east.png` | BALT（Colonel Balt） ほか3件 | ○ | 520x900 |
| `gfx/co/balt.png` | BALT（Colonel Balt） | ○ | 480x640 |
| `gfx/co/wolf.png` | WOLF（Admiral Wolf） | ○ | 480x640 |
| `gfx/co/karla.png` | KARLA（Staff Officer Karla） | ○ | 480x640 |
| `gfx/co/eagle.png` | EAGLE（Lt. Col. Adler） | ○ | 480x640 |
| `gfx/co/herta.png` | HERTA（Paymaster Capt. Herta） | **未** | - |
| `gfx/co/noel.png` | NOEL（Instructor Noel） | **未** | - |
| `gfx/co/dieter.png` | DIETER（Captain Dieter） | **未** | - |

## キャンペーン（32本）

定義場所: `data/campaign/main.cpn` の `art = / reward = / reward_video =`

`art` はブリーフィングの1枚絵（横800基準）。`reward` はクリア後のご褒美画面。`reward_video` を書くと mp4/GIF が優先される。

| パス | 用途 | 現物 | 実寸 |
|---|---|---|---|
| `gfx/brief/m01.png` | M01 | ○ | 800x220 |
| `gfx/reward/m01.png` | M01 | ○ | 640x480 |
| `gfx/brief/m02.png` | M02 ほか1件 | ○ | 800x220 |
| `gfx/reward/m02.png` | M02 ほか1件 | ○ | 640x480 |
| `gfx/brief/m03.png` | M03 ほか1件 | ○ | 800x220 |
| `gfx/reward/m03.png` | M03 ほか1件 | ○ | 640x480 |
| `gfx/brief/m04.png` | M04 ほか1件 | ○ | 800x220 |
| `gfx/reward/m04.png` | M04 ほか1件 | ○ | 640x480 |
| `gfx/brief/m05.png` | M05 | ○ | 800x220 |
| `gfx/reward/m05.png` | M05 | ○ | 640x480 |
| `gfx/brief/m06.png` | M06 | ○ | 800x220 |
| `gfx/reward/m06.png` | M06 | ○ | 640x480 |
| `gfx/brief/m07.png` | M07 | ○ | 800x220 |
| `gfx/reward/m07.png` | M07 | ○ | 640x480 |
| `gfx/brief/m13.png` | M13 | **未** | - |
| `gfx/reward/m13.png` | M13 | **未** | - |
| `gfx/brief/m11.png` | M11 | **未** | - |
| `gfx/reward/m11.png` | M11 | **未** | - |
| `gfx/brief/m08.png` | M08 | ○ | 800x220 |
| `gfx/reward/m08.png` | M08 | ○ | 640x480 |
| `gfx/brief/m09.png` | M09 ほか1件 | ○ | 800x220 |
| `gfx/reward/m09.png` | M09 ほか1件 | ○ | 640x480 |
| `gfx/brief/m14.png` | M14 | **未** | - |
| `gfx/reward/m14.png` | M14 | **未** | - |
| `gfx/brief/m12.png` | M12 | **未** | - |
| `gfx/reward/m12.png` | M12 | **未** | - |
| `gfx/brief/m10.png` | M10 | ○ | 800x220 |
| `gfx/reward/m10.png` | M10 | ○ | 640x480 |
| `gfx/brief/m19.png` | M19 | **未** | - |
| `gfx/reward/m19.png` | M19 | **未** | - |
| `gfx/brief/m20.png` | M20 | **未** | - |
| `gfx/reward/m20.png` | M20 | **未** | - |

## 未用意のもの

| パス | 用途 | 描き方のメモ |
|---|---|---|
| `gfx/terrain/lab.png` | LAB_LAND（Land Laboratory） | 陸の研究所。192x192の正方形、真上から見た屋根。超兵器の研究施設なので、都市より無機質でアンテナや円形の設備があるとよい。基調色は terrain.def の color（B8C8D8） |
| `gfx/terrain/lab_air.png` | LAB_AIR（Air Laboratory） | 空の研究所。192x192の正方形、真上から見た絵。滑走路と格納庫がそろった、空港より研究施設らしい見た目（レーダー、風洞など）。基調色は C4C8E0 |
| `gfx/terrain/lab_sea.png` | LAB_SEA（Naval Laboratory） | 海の研究所。192x192の正方形、真上から見た絵。海の上に建つ施設なので、海の波の上に桟橋と建物が載る形（周りは海として描く）。基調色は A8C0D0 |
| `gfx/terrain/airstrip.png` | AIRSTRIP（Airstrip） |  |
| `gfx/terrain/anchorage.png` | ANCHORAGE（Anchorage） |  |
| `gfx/terrain/rubble.png` | RUBBLE（Rubble） |  |
| `gfx/terrain/rubble_port.png` | RUBBLE_PORT（Ruined Port） |  |
| `gfx/units/common/engineer.png` | ENGINEER／ENGINEER_V（Master Diggers） |  |
| `gfx/units/common/night_inf.png` | NIGHT_INF（Night Blades）／NIGHT_INF_V（Masters of the Dark） |  |
| `gfx/units/common/night_boat.png` | NIGHT_BOAT（Dark Boat）／NIGHT_BOAT_V（Black Boat） |  |
| `gfx/units/common/night_fighter.png` | NIGHT_FIGHTER（Nighthawk）／NIGHT_FIGHTER_V（Lord of the Night） |  |
| `gfx/units/common/sw_tank.png` | SW_TANK／SW_TANK_V | 超重戦車（陸の超兵器）。96x96の透過PNG。重戦車の一回り以上大きい車体に長砲身。移動2の遅い壁なので、どっしりした形にする。全陣営共通 |
| `gfx/units/common/sw_fortress.png` | SW_FORTRESS／SW_FORTRESS_V | 飛行要塞（空の超兵器）。96x96の透過PNG。爆撃機より巨大で、多発エンジンと銃座が並ぶ大型機。全陣営共通 |
| `gfx/units/common/sw_battleship.png` | SW_BATTLESHIP／SW_BATTLESHIP_V | 決戦航空戦艦（海の超兵器）。96x96の透過PNG。戦艦の船体に後部の飛行甲板（艦載機2機ぶん）を持つ、砲と甲板の両方が見える形。全陣営共通 |
| `gfx/co/herta.png` | HERTA（Paymaster Capt. Herta） |  |
| `gfx/co/noel.png` | NOEL（Instructor Noel） |  |
| `gfx/co/dieter.png` | DIETER（Captain Dieter） |  |
| `gfx/brief/m13.png` | M13 |  |
| `gfx/reward/m13.png` | M13 |  |
| `gfx/brief/m11.png` | M11 |  |
| `gfx/reward/m11.png` | M11 |  |
| `gfx/brief/m14.png` | M14 |  |
| `gfx/reward/m14.png` | M14 |  |
| `gfx/brief/m12.png` | M12 |  |
| `gfx/reward/m12.png` | M12 |  |
| `gfx/brief/m19.png` | M19 |  |
| `gfx/reward/m19.png` | M19 |  |
| `gfx/brief/m20.png` | M20 |  |
| `gfx/reward/m20.png` | M20 |  |

## 寸法の目安

既存ファイルの実寸から。厳密でなくてよく、縦横比を保って収まるように拡縮される。

| 種別 | よくある実寸 |
|---|---|
| 地形タイル | 192x192 (12本) |
| ユニット/カットイン | 96x96 (32本)、520x900 (4本)、640x360 (1本) |
| 指揮官の顔絵/カットイン | 480x640 (6本)、520x900 (2本) |
| ブリーフィング/ご褒美 | 800x220 (10本)、640x480 (10本) |

---

この一覧は `tools/gen_asset_list.py` で生成している。ユニットや地形を足したら再実行すること。

