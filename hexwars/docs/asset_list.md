# 差し替えできる画像の一覧

すべて `hexwars/assets/` からの相対パスで、**データ定義に書いたパスがそのまま読まれる**。同じ名前で置き換えれば再ビルドなしで反映される（`assets/` は実行フォルダへ自動同期される）。

ファイルが無い場合は落ちずにフォールバックする（地形は `color` の単色、指揮官は名前の文字、ユニットは図形描画）。**先に絵の無いものから用意すれば、それだけ見た目が上がる。**

| | 数 |
|---|---|
| 参照されているパス | 85 |
| うち実体あり | 77 |
| うち**未用意** | **8** |


## 地形（マップのセル）（14本）

定義場所: `data/terrain.def` の `image =`

真上から見た地面の模様。六角形に切り抜いて貼られるので、正方形のタイル画像でよい。斜め見下ろし表示でも同じ画像を使う（別途用意は不要）。

| パス | 用途 | 現物 | 実寸 |
|---|---|---|---|
| `gfx/terrain/plain.png` | PLAIN（平地） | ○ | 192x192 |
| `gfx/terrain/road.png` | ROAD（道路） | ○ | 192x192 |
| `gfx/terrain/forest.png` | FOREST（森） | ○ | 192x192 |
| `gfx/terrain/mountain.png` | MOUNTAIN（山） | ○ | 192x192 |
| `gfx/terrain/river.png` | RIVER（川） | ○ | 192x192 |
| `gfx/terrain/sea.png` | SEA（海） | ○ | 192x192 |
| `gfx/terrain/city.png` | CITY（都市） | ○ | 192x192 |
| `gfx/terrain/factory.png` | FACTORY（工場） | ○ | 192x192 |
| `gfx/terrain/airport.png` | AIRPORT（空港） | ○ | 192x192 |
| `gfx/terrain/port.png` | PORT（港） | ○ | 192x192 |
| `gfx/terrain/hill.png` | HILL（丘） | ○ | 192x192 |
| `gfx/terrain/hq.png` | HQ（首都） | ○ | 192x192 |
| `gfx/terrain/airstrip.png` | AIRSTRIP（野戦飛行場） | **未** | - |
| `gfx/terrain/anchorage.png` | ANCHORAGE（泊地） | **未** | - |

## ユニット（40本）

定義場所: `data/units.def` の `image = / image0 = / image1 =`

`image` は両陣営共通、`image0`/`image1` は陣営ごとに分けたいとき。背景は透過。`anim` は戦闘アニメ、`cutin` は攻撃時の1枚絵。

| パス | 用途 | 現物 | 実寸 |
|---|---|---|---|
| `gfx/units/infantry.png` | INFANTRY ほか1件 | ○ | 96x96 |
| `gfx/cutin/infantry.png` | INFANTRY ほか7件 | ○ | 520x900 |
| `gfx/units/at_infantry.png` | AT_INFANTRY ほか1件 | ○ | 96x96 |
| `gfx/units/recon.png` | RECON ほか1件 | ○ | 96x96 |
| `gfx/units/tank.png` | TANK ほか1件 | ○ | 96x96 |
| `gfx/cutin/tank.png` | TANK ほか7件 | ○ | 520x900 |
| `gfx/anim/sample_land.mp4` | TANK | ○ | - |
| `gfx/units/htank.png` | HTANK ほか1件 | ○ | 96x96 |
| `gfx/units/artillery.png` | ARTILLERY ほか1件 | ○ | 96x96 |
| `gfx/units/aa_tank.png` | AA_TANK ほか1件 | ○ | 96x96 |
| `gfx/units/truck.png` | TRUCK ほか1件 | ○ | 96x96 |
| `gfx/units/t_copter.png` | T_COPTER ほか1件 | ○ | 96x96 |
| `gfx/units/fighter.png` | FIGHTER ほか1件 | ○ | 96x96 |
| `gfx/cutin/air.png` | FIGHTER ほか9件 | ○ | 520x900 |
| `gfx/units/bomber.png` | BOMBER ほか1件 | ○ | 96x96 |
| `gfx/units/heli.png` | HELI ほか1件 | ○ | 96x96 |
| `gfx/units/destroyer.png` | DESTROYER ほか1件 | ○ | 96x96 |
| `gfx/cutin/ship.png` | DESTROYER ほか7件 | ○ | 520x900 |
| `gfx/units/cruiser.png` | CRUISER ほか1件 | ○ | 96x96 |
| `gfx/units/submarine.png` | SUBMARINE ほか1件 | ○ | 96x96 |
| `gfx/units/t_ship.png` | T_SHIP ほか1件 | ○ | 96x96 |
| `gfx/units/supply.png` | SUPPLY ほか1件 | ○ | 96x96 |
| `gfx/units/supply_air.png` | SUPPLY_AIR ほか1件 | ○ | 96x96 |
| `gfx/units/militia.png` | MILITIA ほか1件 | ○ | 96x96 |
| `gfx/units/mech_inf.png` | MECH_INF ほか1件 | ○ | 96x96 |
| `gfx/units/ltank.png` | LTANK ほか1件 | ○ | 96x96 |
| `gfx/units/rocket.png` | ROCKET ほか1件 | ○ | 96x96 |
| `gfx/units/aa_gun.png` | AA_GUN ほか1件 | ○ | 96x96 |
| `gfx/units/dive_bomber.png` | DIVE_BOMBER ほか1件 | ○ | 96x96 |
| `gfx/units/scout_plane.png` | SCOUT_PLANE ほか1件 | ○ | 96x96 |
| `gfx/units/battleship.png` | BATTLESHIP ほか1件 | ○ | 96x96 |
| `gfx/anim/sample_sea.gif` | BATTLESHIP | ○ | 640x360 |
| `gfx/units/carrier.png` | CARRIER ほか1件 | ○ | 96x96 |
| `gfx/units/gunboat.png` | GUNBOAT ほか1件 | ○ | 96x96 |
| `gfx/units/missile_boat.png` | MISSILE_BOAT ほか1件 | ○ | 96x96 |
| `gfx/units/supply_ship.png` | SUPPLY_SHIP ほか1件 | ○ | 96x96 |
| `gfx/units/t_plane.png` | T_PLANE ほか1件 | ○ | 96x96 |
| `gfx/units/night_inf.png` | NIGHT_INF（夜間忍兵） | **未** | - |
| `gfx/units/night_boat.png` | NIGHT_BOAT（夜間突撃艦） | **未** | - |
| `gfx/units/night_fighter.png` | NIGHT_FIGHTER（夜間迎撃機） | **未** | - |

## 指揮官（11本）

定義場所: `data/commanders.def` の `image = / cutin =`

`image` は指揮官選択と全体図に出す顔絵（縦長）。`cutin` は必殺技の1枚絵。

| パス | 用途 | 現物 | 実寸 |
|---|---|---|---|
| `gfx/cutin/co_west.png` | GRAF（グレーフ将軍） ほか4件 | ○ | 520x900 |
| `gfx/co/graf.png` | GRAF（グレーフ将軍） | ○ | 480x640 |
| `gfx/co/liese.png` | LIESE（リーゼ中佐） | ○ | 480x640 |
| `gfx/cutin/co_east.png` | BALT（バルト大佐） ほか3件 | ○ | 520x900 |
| `gfx/co/balt.png` | BALT（バルト大佐） | ○ | 480x640 |
| `gfx/co/wolf.png` | WOLF（ヴォルフ提督） | ○ | 480x640 |
| `gfx/co/karla.png` | KARLA（カーラ参謀） | ○ | 480x640 |
| `gfx/co/eagle.png` | EAGLE（アドラー中佐） | ○ | 480x640 |
| `gfx/co/herta.png` | HERTA（ヘルタ主計大尉） | **未** | - |
| `gfx/co/noel.png` | NOEL（ノエル教官） | **未** | - |
| `gfx/co/dieter.png` | DIETER（ディーター大尉） | **未** | - |

## キャンペーン（20本）

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
| `gfx/brief/m08.png` | M08 | ○ | 800x220 |
| `gfx/reward/m08.png` | M08 | ○ | 640x480 |
| `gfx/brief/m09.png` | M09 ほか1件 | ○ | 800x220 |
| `gfx/reward/m09.png` | M09 ほか1件 | ○ | 640x480 |
| `gfx/brief/m10.png` | M10 | ○ | 800x220 |
| `gfx/reward/m10.png` | M10 | ○ | 640x480 |

## 未用意のもの

| パス | 用途 |
|---|---|
| `gfx/terrain/airstrip.png` | AIRSTRIP（野戦飛行場） |
| `gfx/terrain/anchorage.png` | ANCHORAGE（泊地） |
| `gfx/units/night_inf.png` | NIGHT_INF（夜間忍兵） |
| `gfx/units/night_boat.png` | NIGHT_BOAT（夜間突撃艦） |
| `gfx/units/night_fighter.png` | NIGHT_FIGHTER（夜間迎撃機） |
| `gfx/co/herta.png` | HERTA（ヘルタ主計大尉） |
| `gfx/co/noel.png` | NOEL（ノエル教官） |
| `gfx/co/dieter.png` | DIETER（ディーター大尉） |

## 寸法の目安

既存ファイルの実寸から。厳密でなくてよく、縦横比を保って収まるように拡縮される。

| 種別 | よくある実寸 |
|---|---|
| 地形タイル | 192x192 (12本) |
| ユニット/カットイン | 96x96 (31本)、520x900 (4本)、640x360 (1本) |
| 指揮官の顔絵/カットイン | 480x640 (6本)、520x900 (2本) |
| ブリーフィング/ご褒美 | 800x220 (10本)、640x480 (10本) |

---

この一覧は `tools/gen_asset_list.py` で生成している。ユニットや地形を足したら再実行すること。

