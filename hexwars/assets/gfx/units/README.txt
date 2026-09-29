ユニットの絵の置き場所
======================

フォルダは陣営ごとに分けてあります。

  common/    全陣営で使う絵。units.def の `image =` はここを指しています。
  faction0/  陣営0（西方連合。1人プレイでは自軍）だけの絵
  faction1/  陣営1（東方連邦）だけの絵
  faction2/  陣営2（南方同盟）だけの絵
  faction3/  陣営3（北方協商）だけの絵
  faction4/  陣営4（自由軍）だけの絵

faction0〜4 は空のままでかまいません。置いた陣営だけが差し替わります。

差し替え方
----------

絵を描き替えるだけなら、common/ の中のファイルを同じ名前で上書きします。
data/units.def は触らなくて大丈夫です。

陣営ごとに別の絵にしたいときは、faction0〜4 に絵を置いて、
data/units.def の該当ユニットの `image =` の **あと** に行を足します。

  [unit TANK]
  image  = gfx/units/common/tank.png     全陣営これ
  image0 = gfx/units/faction0/tank.png   陣営0だけ差し替え
  image1 = gfx/units/faction1/tank.png   陣営1だけ差し替え

あとに書いた行が勝つので、順番を入れ替えないでください。
書かなかった陣営は common/ の絵のままです。

絵の決まり
----------

  ・96×96 ピクセルの透過PNG
  ・ゲーム側は絵に陣営色を乗せません（陣営は足元のチップの色と形で示します）。
    色で見分けたいなら、絵そのものを陣営ごとに分けてください。

仮絵を一括で作る（要 Pillow）
-----------------------------

  python tools/gen_units_gfx.py            common/ の絵を作り直す
  python tools/gen_unit_tint.py --apply    common/ の絵を陣営色に染めて
                                           faction0〜4 に置き、units.def に
                                           image0〜image4 の行を書き足す

くわしくは docs/データ編集の手引き.md を見てください。

ファイル名とユニットの対応
--------------------------

「/」の左が基本形、右が進化後です。
進化前と進化後は同じ絵を使うので、1つ差し替えると両方に効きます。

ここに書いてある名前は既定の呼び名です。陣営ごとに呼び名を変えてあるので
（units.def の name0〜name4）、ゲーム中の表示と違って見えることがあります。
どの絵がどのユニットかはこの表で引いてください。

  《陸上》
    infantry.png     歩兵 / 猟兵「精鋭歩兵」（進化後）
    at_infantry.png  対戦車兵 / 戦車猟兵（進化後）
    recon.png        偵察車 / 装甲偵察車（進化後）
    tank.png         中戦車「T-40型」 / 主力戦車「T-55型」（進化後）
    htank.png        重戦車「G-6型」 / 超重戦車「G-9型」（進化後）
    artillery.png    自走砲「H-15型」 / 重自走砲「H-24型」（進化後）
    aa_tank.png      対空戦車「F-8型」 / 自走対空機関砲（進化後）
    truck.png        トラック / 装甲兵員輸送車（進化後）
    supply.png       補給車 / 重補給車（進化後）
    militia.png      民兵 / 郷土防衛隊（進化後）
    mech_inf.png     機械化歩兵 / 装甲擲弾兵（進化後）
    engineer.png     工兵 / 機甲工兵（進化後）   ※まだ絵がありません
    ltank.png        軽戦車「T-20型」 / 騎兵戦車「T-24型」（進化後）
    rocket.png       自走ロケット砲「R-8型」 / 多連装ロケット「R-16型」（進化後）
    aa_gun.png       高射砲 / 重高射砲「88式」（進化後）
    night_inf.png    夜間忍兵 / 闇夜特務兵（進化後）   ※まだ絵がありません
    sw_tank.png      超重戦車「Gx-1型」（超兵器・進化なし）   ※まだ絵がありません

  《航空》
    t_copter.png       輸送ヘリ「Hv-4型」 / 強襲輸送ヘリ「Hv-9型」（進化後）
    fighter.png        戦闘機「Fk-9型」 / 迎撃機「Fk-12型」（進化後）
    bomber.png         爆撃機「Bx-17型」 / 重爆撃機「Bx-24型」（進化後）
    heli.png           攻撃ヘリ「Hv-2型」 / 重攻撃ヘリ「Hv-8型」（進化後）
    supply_air.png     補給機「Cg-5型」 / 大型補給機「Cg-9型」（進化後）
    dive_bomber.png    襲撃機「Sk-6型」 / 対地攻撃機「Sk-9型」（進化後）
    scout_plane.png    偵察機「Mk-3型」 / 高高度偵察機「Mk-8型」（進化後）
    t_plane.png        輸送機「Cd-8型」 / 大型輸送機「Cd-14型」（進化後）
    night_fighter.png  夜間迎撃機 / 夜天迎撃機（進化後）   ※まだ絵がありません
    sw_fortress.png    飛行要塞「Bx-50型」（超兵器・進化なし）   ※まだ絵がありません

  《海上》
    destroyer.png     駆逐艦「Dn-3型」 / 重駆逐艦「Dn-8型」（進化後）
    cruiser.png       巡洋艦「Kv-1型」 / 重巡洋艦「Kv-6型」（進化後）
    submarine.png     潜水艦「U-11型」 / 大型潜水艦「U-24型」（進化後）
    t_ship.png        輸送艦「Ln-2型」 / 強襲揚陸艦「Ln-6型」（進化後）
    battleship.png    戦艦「Rz-9型」 / 超弩級戦艦「Rz-14型」（進化後）
    carrier.png       空母「Hy-4型」 / 大型空母「Hy-9型」（進化後）
    gunboat.png       砲艦「Bt-2型」 / 重砲艦「Bt-6型」（進化後）
    missile_boat.png  ミサイル艇「Fk-5型」 / 高速ミサイル艇「Fk-9型」（進化後）
    supply_ship.png   補給艦「Ho-6型」 / 高速補給艦「Ho-9型」（進化後）
    night_boat.png    夜間突撃艦 / 黒潮強襲艦（進化後）   ※まだ絵がありません
    subchaser.png     駆潜艇「Uj-2型」 / 大型駆潜艇「Uj-7型」（進化後）
    sw_battleship.png 決戦航空戦艦「Rz-30型」（超兵器・進化なし）   ※まだ絵がありません

※ 印のものは、まだ絵を用意していないので図形で描画されます。
ここに同じ名前のPNGを置けば、そのまま使われるようになります。

この対応表は data/units.def から作っています。ユニットを足したり
画像ファイル名を変えたりしたときは、python tools/gen_asset_list.py で
docs/asset_list.md を作り直すと最新の一覧が出ます。
