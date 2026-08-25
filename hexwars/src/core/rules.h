/* rules.h - ZOC・攻撃対象などのルール判定 */
#ifndef HW_RULES_H
#define HW_RULES_H

#include "game.h"

/* (x,y) が player の敵地上ユニットのZOC内か（仕様書 5.4） */
bool rules_in_enemy_zoc(const Game *g, int player, int x, int y);

/* 地形への進入コスト（2倍整数）。0=進入不可 */
int rules_move_cost(const Game *g, MoveClass mc, int x, int y);

/* (fx,fy) から攻撃側 ui が狙える敵ユニット index を列挙。戻り値=件数 */
int rules_list_targets(const Game *g, int ui, int fx, int fy,
                       int *out, int max_out);

/* ユニットがこのターン攻撃可能か（間接=移動後不可 等） */
bool rules_can_attack_now(const Game *g, int ui, bool has_moved);

#endif
