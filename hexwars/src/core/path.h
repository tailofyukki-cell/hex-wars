/* path.h - Dijkstra 移動範囲計算（仕様書 5.4） */
#ifndef HW_PATH_H
#define HW_PATH_H

#include "game.h"

typedef struct {
    int16_t cost[MAX_MAP_H][MAX_MAP_W];   /* 到達コスト(2倍整数)、-1=不可 */
    int8_t  parent[MAX_MAP_H][MAX_MAP_W]; /* 進入してきた方向 dir、-1=なし */
    uint8_t stop[MAX_MAP_H][MAX_MAP_W];   /* 1=そこで停止可能 */
} MoveRange;

/* ユニット ui の移動範囲を計算 */
void path_move_range(const Game *g, int ui, MoveRange *out);

/* 移動実行用: (tx,ty) までの経路を start から辿り、
 * 索敵で隠れていた敵に遮られたら手前で停止（アンブッシュ）。
 * 実際の到達点を *ox,*oy、消費燃料を *ofuel に格納。
 * 戻り値: 1=アンブッシュ発生 */
int path_walk(const Game *g, int ui, const MoveRange *mr, int tx, int ty,
              int *ox, int *oy, int *ofuel);

#endif
