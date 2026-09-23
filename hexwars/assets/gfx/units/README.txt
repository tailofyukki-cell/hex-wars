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
