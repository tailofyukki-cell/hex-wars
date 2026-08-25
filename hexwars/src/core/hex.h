/* hex.h - ポイントトップ / odd-r offset ヘクス座標系（仕様書 4.1） */
#ifndef HW_HEX_H
#define HW_HEX_H

#include "types.h"

/* 近傍方向数 */
#define HEX_DIRS 6

/* offset(x,y) -> axial(q,r) */
Axial hex_to_axial(int x, int y);

/* axial -> offset */
Cell hex_to_offset(Axial a);

/* ヘクス距離（axial 変換後） */
int hex_distance(int x0, int y0, int x1, int y1);

/* dir=0..5 の近傍 offset 座標を得る。結果を *nx,*ny に格納 */
void hex_neighbor(int x, int y, int dir, int *nx, int *ny);

#endif
