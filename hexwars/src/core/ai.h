/* ai.h - CPU思考ルーチン（仕様書 7章） */
#ifndef HW_AI_H
#define HW_AI_H

#include "game.h"

typedef struct {
    int  order[MAX_UNITS];
    int  n_order;
    int  idx;
    int  threat[MAX_MAP_H][MAX_MAP_W]; /* 敵攻撃到達範囲の脅威値 */
    /* 上陸作戦（仕様書 7.1 の拡張）:
     * land_reach = 自軍の陸ユニットが陸路で到達できる範囲。
     * ここから外れた敵拠点があるとき amphib=true になり、
     * 輸送ユニットで inv_x/inv_y へ部隊を送り込む。 */
    uint8_t land_reach[MAX_MAP_H][MAX_MAP_W];
    bool    amphib;
    int     inv_x, inv_y;   /* 上陸目標（陸路で行けない敵拠点）。-1=なし */
    bool produced;
    /* 指揮官(CO)の必殺技: 手番中に1回だけ撃つ。co_fired は UI 通知用で、
     * バナーを出した側が false に戻す。 */
    bool co_used;
    bool co_fired;
    /* 直近の行動（UI表示用） */
    int  last_unit;    /* -1=なし */
    int  last_target;  /* 攻撃対象 unit index / -1 */
} AiState;

/* 手番開始時に呼ぶ（脅威マップ生成・行動順決定） */
void ai_begin_turn(Game *g, AiState *s);

/* 1ステップ（1ユニットの行動 or 生産 or ターン終了）を実行。
 * 戻り値: 1=継続、0=手番終了（game_end_turn 済み） */
int ai_step(Game *g, AiState *s);

#endif
