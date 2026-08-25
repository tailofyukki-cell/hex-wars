/* campaign.h - キャンペーン定義と進行状態（仕様書 6章） */
#ifndef HW_CAMPAIGN_H
#define HW_CAMPAIGN_H

#include "game.h"

#define MAX_BRIEF_LINES 8
#define MAX_SUBS 3          /* 1作戦あたりの副目標の数 */

/* 副目標の種類。.cpn には `sub = 種類:数値:説明文` の形で書く。
 * 例) sub = MAX_LOSS:3:損失3部隊以内で勝利せよ
 * 達成すると評価ボーナス資金が入る（勝利が条件。敗北時は評価しない）。 */
typedef enum {
    SUB_NONE = 0,
    SUB_MAX_LOSS,    /* 自軍の損失が param 体以下 */
    SUB_MAX_TURNS,   /* param ターン以内に勝利 */
    SUB_MIN_KILLS,   /* 敵を param 体以上撃破 */
    SUB_KEEP_BLD,    /* 自軍の建物が param 個以上残っている */
    SUB_CAPTURE,     /* 自軍の建物が param 個以上（占領して増やす） */
    SUB_SURVIVE      /* unit で指定した種別が param 体以上生存 */
} SubType;

typedef struct {
    uint8_t type;        /* SubType */
    int16_t param;
    char    unit[24];    /* SUB_SURVIVE のときのユニットID（空=不問） */
    char    desc[96];    /* 画面に出す説明文 */
} SubObjective;

typedef struct {
    char id[24];
    char title[64];
    char map[64];
    char art[64];             /* ブリーフィング1枚絵（assets/ 相対。空=なし） */
    char reward[64];          /* クリア時のご褒美画像（assets/ 相対。空=なし） */
    char reward_video[64];    /* クリア時の動画（mp4/GIF。指定時は画像より優先） */
    char brief[MAX_BRIEF_LINES][128];
    int  n_brief;
    char next_win[24];
    char next_win_fast[24];   /* 早期勝利ルート（空文字=なし） */
    int  fast_turns;          /* このターン以内の勝利で fast ルート */
    char next_lose[24];
    int  carry;               /* 1=ユニット持越し有効 */
    int  bonus;               /* 勝利ボーナス資金 */
    uint8_t enemy;            /* PlayerCtrl（P1のCPU難易度） */
    char    enemy_co[24];     /* 敵指揮官のID（空=既定） */
    int     par_turns;        /* 作戦評価の基準ターン（0=マップから自動算出） */
    int     no_reinforce;     /* 1=この作戦では敵に増援を与えない */
    SubObjective subs[MAX_SUBS];
    int          n_subs;
    /* マップイベント。ユニットIDは文字列のまま持ち、開戦時に型indexへ解決する */
    MapEvent evs[MAX_EVENTS];
    char     ev_unit[MAX_EVENTS][24];
    int      n_evs;
} CpnNode;

/* 作戦評価 */
typedef enum { RANK_NONE = 0, RANK_S, RANK_A, RANK_B, RANK_C } CpnRank;

/* 評価の内訳（各0〜100）。rank は総合から決まる */
typedef struct {
    int speed;    /* 速さ（基準ターン比） */
    int loss;     /* 戦力温存（自軍の損失の少なさ） */
    int power;    /* 戦果（敵を減らした量） */
    int total;    /* 3項目の平均 */
    CpnRank rank;
} CpnScore;

/* 勝利時の作戦評価を計算する（盤面は変更しない） */
void campaign_evaluate(const Game *g, const CpnNode *node, CpnScore *out);
/* ランクに応じたボーナス資金 */
int  campaign_rank_bonus(CpnRank r);

/* --- 副目標 --- */
/* 副目標 i を現時点の盤面で達成しているか（戦闘中の進捗表示にも使う） */
bool campaign_sub_done(const Game *g, const CpnNode *node, int i);
/* 達成した副目標の数 */
int  campaign_sub_count_done(const Game *g, const CpnNode *node);
/* 副目標1個あたりのボーナス資金 */
int  campaign_sub_bonus(void);
/* ランクの表示文字（"S"/"A"/"B"/"C"、未取得は "-"） */
const char *campaign_rank_str(CpnRank r);

typedef struct {
    char    name[64];
    char    start[24];
    CpnNode nodes[MAX_CAMPAIGN_MAPS];
    int     n_nodes;
} Campaign;

/* 進行状態（セーブ対象） */
typedef struct {
    bool active;
    char file[64];            /* .cpn の data/ 相対パス */
    char node[24];            /* 現在ノードid */
    int  funds_carry;         /* 次マップに加算する資金 */
    int  n_carry;
    struct { uint8_t type; uint8_t exp; } carry[MAX_CARRY_UNITS];
    uint32_t cleared;         /* クリア済みノードのビットマスク（全体マップ表示用） */
    int8_t player_co;         /* 自軍指揮官の cos[] index（0=既定） */
    uint8_t rank[MAX_CAMPAIGN_MAPS];  /* 各ノードで取得したランク（CpnRank） */
    /* 倉庫: 持越しできなかった（上限超過・配置先なし）ユニットを保管。
     * 生産拠点で無料で引き出せる（経験値も保持）。 */
    int  n_store;
    struct { uint8_t type; uint8_t exp; } store[MAX_STORE_UNITS];
} CampaignState;

/* 倉庫へユニットを1体保管（満杯時は最も経験値の低い枠と入れ替え）。 */
void campaign_store_push(CampaignState *s, int type, int exp);

/* 倉庫の slot 番目を取り出して詰める（配置は呼び出し側が済ませる）。 */
void campaign_store_remove(CampaignState *s, int slot);

/* .cpn 読込。成功=0 */
int campaign_load(Campaign *c, const char *path, char *err, int errlen);

const CpnNode *campaign_find_node(const Campaign *c, const char *id);

/* 現在ノードのマップを読み込んで開戦状態にする。
 * base_path は data/ の親ディレクトリ（"" 可）。成功=0 */
int campaign_setup_battle(Game *g, const Campaign *c, CampaignState *s,
                          const char *base_path, uint32_t seed,
                          char *err, int errlen);

/* --- 出撃部隊を手動で選ぶ場合の2段構え --- */
/* (1) マップと陣営設定だけ用意する（持越しの展開も game_start もしない）。成功=0 */
int  campaign_setup_map(Game *g, const Campaign *c, const CampaignState *s,
                        const char *base_path, char *err, int errlen);
/* (2) このマップに展開できる持越しユニット数の上限（setup_map の後に呼ぶ） */
int  campaign_deploy_limit(const Game *g);
/* (3) 持越しを展開して開戦する。
 *     sel は s->carry[] と同じ並びの「出撃させる」フラグ（NULL なら経験値順に自動）。
 *     出撃しなかった部隊は倉庫へ送られる（消えない）。 */
void campaign_begin(Game *g, const Campaign *c, CampaignState *s,
                    uint32_t seed, const uint8_t *sel);

/* 勝利処理: 持越しユニット・資金を計算し、s->node を次ノードへ進める。
 * 戻り値: 0=次ノードあり 1=キャンペーンクリア(次がWIN) */
int campaign_on_victory(const Game *g, const Campaign *c, CampaignState *s);

#endif
