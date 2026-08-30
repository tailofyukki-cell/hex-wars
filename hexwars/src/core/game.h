/* game.h - ゲーム状態とコマンドAPI（ターンエンジン） */
#ifndef HW_GAME_H
#define HW_GAME_H

#include "types.h"
#include "rng.h"

/* 勝敗状態 */
#define WINNER_NONE (-2)
#define WINNER_DRAW (-1)

/* プレイヤー制御 */
typedef enum {
    CTRL_HUMAN = 0,
    CTRL_CPU_EASY,
    CTRL_CPU_NORMAL,
    CTRL_CPU_HARD
} PlayerCtrl;

typedef struct Game {
    /* 定義データ */
    TerrainType terrains[MAX_TERRAIN];
    int         n_terrains;
    UnitType    types[MAX_UNIT_TYPES];
    int         n_types;
    CommanderType cos[MAX_COMMANDERS];
    int           n_cos;

    /* マップ */
    char  map_name[64];
    int   w, h;
    Tile  tiles[MAX_MAP_H][MAX_MAP_W];
    int   turn_limit;      /* 0=無制限 */
    int   timeout_winner;  /* ターン切れ時の勝者 (-1=引き分け) */
    int   income_scale;    /* 収入倍率 % (既定100) */
    /* 拠点確保勝利（仕様書 5.10「指定ターン内の拠点n個確保」。0=無効）
     * objective_player が capturable な建物を n 個以上所有した時点で勝利 */
    int   objective_count;
    int   objective_player;

    /* マップイベント（.cpn から流し込まれる。発火済みは events_fired のビットで管理） */
    MapEvent events[MAX_EVENTS];
    int      n_events;
    uint32_t events_fired;

    /* ユニット */
    Unit  units[MAX_UNITS];
    int   n_units;

    /* 進行 */
    int   funds[MAX_PLAYERS];
    int   turn;            /* 1開始 */
    int   current;         /* 手番プレイヤー */
    int   winner;          /* WINNER_NONE / WINNER_DRAW / 0..1 */
    Rng   rng;

    /* 指揮官（CO）。co_id は cos[] の index（-1 または n_cos 超過で「なし」） */
    int8_t  co_id[MAX_PLAYERS];
    int16_t co_gauge[MAX_PLAYERS];       /* 必殺技ゲージ（power_cost で満タン） */
    int8_t  co_power_turns[MAX_PLAYERS]; /* 効果継続ターン数（STRIKE/SCOUT用） */

    /* 天候（ラウンド単位で抽選し weather_left ターン継続。両陣営に同じ天候） */
    uint8_t weather;        /* Weather */
    uint8_t weather_next;   /* 次に変わる天候（予報として表示する） */
    int8_t  weather_left;   /* 残りラウンド数（0で weather_next へ切替） */
    uint8_t weather_on;     /* 0=このマップは天候なし（.map の weather=0） */
    /* 天候の抽選重み（既定 60/30/10。.map の weather_clear/cloudy/rain で上書き可）。
     * 切り替え時は「今の天候を除いて」この重みで引くので、
     * 実際の出現率はこの値そのものではなく、差が小さくなる方向になる（実測 48/34/17）。 */
    int16_t wx_pct[WX_COUNT];

    /* 設定 */
    bool  fog;             /* 索敵 ON/OFF */
    uint8_t ctrl[MAX_PLAYERS]; /* PlayerCtrl */

    /* 視界（fog=ON時のみ有効） */
    uint8_t visible[MAX_PLAYERS][MAX_MAP_H][MAX_MAP_W];

    /* 統計 */
    int   lost_units[MAX_PLAYERS];
} Game;

/* --- 参照系 --- */
static inline bool game_in_bounds(const Game *g, int x, int y)
{
    return x >= 0 && y >= 0 && x < g->w && y < g->h;
}
const TerrainType *game_terrain_at(const Game *g, int x, int y);
int  game_unit_at(const Game *g, int x, int y);        /* unit index / -1（レイヤー無視・最初の1体） */
/* 立体化: 指定レイヤーの占有ユニット / -1 */
int  game_unit_at_layer(const Game *g, int x, int y, Layer L);
/* 立体化: セルの全ユニットをレイヤー別に列挙。out[LAYER_COUNT]、戻り値=体数 */
int  game_units_at(const Game *g, int x, int y, int out[LAYER_COUNT]);
Unit *game_unit(Game *g, int i);
const UnitType *unit_type(const Game *g, const Unit *u);
int  unit_rank(const Unit *u);                          /* 経験ランク 0..5 */
bool unit_can_attack_target(const Game *g, const Unit *atk, const Unit *def);
bool game_unit_visible_to(const Game *g, int viewer, const Unit *u);

/* --- 状態変更コマンド（UI / AI 共用） --- */
int  game_spawn_unit(Game *g, int owner, int type, int x, int y, int hp);
/* 進入不可地形上のユニットを最寄りの適地へ寄せる（マップ読込直後の安全網） */
void game_fixup_unit_terrain(Game *g);
void game_move_unit(Game *g, int ui, int x, int y, int fuel_cost);
/* 攻撃実行。反撃込み。戻り値: 与ダメージ。counter_dmg / killed に結果格納（NULL可） */
int  game_attack(Game *g, int atk_i, int def_i, int *counter_dmg,
                 bool *def_killed, bool *atk_killed);
/* 占領コマンド。戻り値: 1=占領完了 */
int  game_capture(Game *g, int ui);
void game_wait_unit(Game *g, int ui);                   /* 行動終了 */
/* 生産。戻り値: 生成 unit index / -1 */
int  game_produce(Game *g, int x, int y, int type);
/* 倉庫からの無料引き出し（exp引継ぎ）。戻り値: 生成 unit index / -1 */
int  game_deploy_free(Game *g, int x, int y, int type, int exp);
bool game_can_produce_at(const Game *g, int player, int x, int y);
bool game_type_buildable_at(const Game *g, int x, int y, int type);
/* その拠点に「置ける」種別か（種別カテゴリとレイヤーの空きだけを見る）。
 * game_type_buildable_at と違い no_produce を弾かないので、進化後のユニットも真になる。
 * 倉庫からの引き出しのように「買うのではなく戻す」経路で使う。 */
bool game_type_deployable_at(const Game *g, int x, int y, int type);

/* --- 補給ユニット（ammo スロットを「補給物資」として使う） --- */
/* ui の隣接味方で燃料/弾薬が減っているユニットがあるか（物資1以上必要） */
bool game_can_supply(const Game *g, int ui);
/* 隣接味方の燃料・弾薬を回復。1ユニットにつき物資-1。戻り値=補給数 */
int  game_supply_adjacent(Game *g, int ui);
/* ui の隣接に HP 回復できる味方がいるか（物資10以上必要） */
bool game_can_heal(const Game *g, int ui);
/* 隣接味方を回復。物資10で1HP。戻り値=回復した合計HP。コマンド時のみ */
int  game_supply_heal(Game *g, int ui);

/* --- 輸送（仕様書 5.9） --- */
bool game_can_board(const Game *g, int passenger, int transport);

/* --- 進化（docs/evolution_spec.md） ---
 * 経験値が満タン(rank5)の部隊を、特性を伸ばした上位種へ任意で作り替える。
 * 進化先は生産できず（no_produce）、元には戻せない。経験値は0に戻る。
 * 自軍の「補給できる拠点」の上でのみ行える（陸=街/工場/首都、空=飛行場、海=港）。 */
bool game_can_evolve(const Game *g, int ui);
/* 進化先のユニット型 index / -1（条件を満たさない・進化先が無い） */
int  game_evolve_target(const Game *g, int ui);
/* 進化に必要な資金（元ユニットの価格×EVOLVE_COST_MUL）。進化先が無ければ0 */
int  game_evolve_cost(const Game *g, int ui);
/* 進化を実行する。戻り値 0=成功 */
int  game_evolve_unit(Game *g, int ui);

/* --- 合流（同種の傷ついた味方2部隊を1部隊に統合） --- */
/* mover を target に合流できるか（同陣営・同種別・target が損傷・双方とも非搭載/非搭載中） */
bool game_can_join(const Game *g, int mover, int target);
/* 合流を実行する。HP/燃料/弾薬を合算（各上限で頭打ち）、熟練度は高い方を継承。
 * target は行動終了になり mover は盤上から消える（撃破ではないので損失に数えない）。
 * 戻り値: HP上限を超えた分の払戻し資金（target 側の funds に加算済み） */
int  game_join_units(Game *g, int mover, int target);
void game_load_unit(Game *g, int passenger, int transport);
/* transport の先頭搭載ユニットを (x,y) に降ろす。戻り値 0=成功 */
int  game_unload_unit(Game *g, int transport, int x, int y);
int  game_first_cargo(const Game *g, int transport);       /* slot index / -1 */
bool game_can_unload_to(const Game *g, int transport, int x, int y);

/* --- ターン制御 --- */
void game_start(Game *g, uint32_t seed);   /* 初期化後に呼ぶ（初回収入等） */
void game_end_turn(Game *g);               /* 手番終了→次プレイヤー開始処理 */
void game_check_victory(Game *g);

/* --- マップイベント ---
 * 手番開始時に呼ぶ。条件を満たした未発火イベントを全て実行し、発生した数を返す。
 * msgs には表示すべきメッセージへのポインタを最大 max 件入れる（NULL可）。 */
int  game_check_events(Game *g, const char *msgs[], int max);
/* player が所有する占領対象建物の数 */
int  game_count_buildings(const Game *g, int player);

/* --- 視界 --- */
void game_update_vision(Game *g);

/* --- 天候 --- */
/* 現在の天候（無効マップでは常に WX_CLEAR） */
Weather game_weather(const Game *g);
/* atk が def を攻撃するとき、天候の影響を受ける組み合わせか
 * （空 ↔ 空以外。空戦どうしは影響なし） */
bool game_weather_hits(const Game *g, const Unit *atk, const Unit *def);
/* 天候による攻撃力の倍率%（100=等倍、50=半減、0=攻撃不可） */
int  game_weather_atk_pct(const Game *g, const Unit *atk, const Unit *def);
/* 天候による視界の増減（0以下）。ユニット毎に下限1は呼び出し側で担保 */
int  game_weather_vision_mod(const Game *g);
/* 天候による地上ユニットの移動増減（0以下） */
int  game_weather_move_mod(const Game *g, const Unit *u);

/* --- 指揮官（CO） --- */
/* プレイヤー p の指揮官（未設定/未読込なら NULL） */
const CommanderType *game_co(const Game *g, int p);
/* ユニット u に p の常時効果が乗るか（ドメイン一致判定） */
bool game_co_affects(const Game *g, int p, const Unit *u);
/* 攻撃/防御の補正％（指揮官なし・対象外なら0） */
int  game_co_atk_pct(const Game *g, int p, const Unit *u);
int  game_co_def_pct(const Game *g, int p, const Unit *u);
/* 必殺技が使えるか（ゲージ満タン） */
bool game_co_power_ready(const Game *g, int p);
/* 必殺技を発動。成功=true（ゲージを消費し効果を適用） */
bool game_co_activate(Game *g, int p);
/* ゲージを加算（内部でも使うが、UI/テストから確認できるよう公開） */
void game_co_add_gauge(Game *g, int p, int amount);

/* --- 戦闘計算（battle.c） --- */
/* 期待ダメージ（乱数なし、HP単位×10 の固定小数）。atk が def を攻撃した場合 */
int  battle_expect_damage_x10(const Game *g, const Unit *atk, const Unit *def);
/* 実ダメージ（乱数込み、HP単位） */
int  battle_roll_damage(Game *g, const Unit *atk, const Unit *def);

/* 攻撃前の戦闘予測（UI表示用。乱数を引かないので盤面を変えない）。
 * atk_i が def_i を攻撃したときの期待値を返す。NULL 可。
 *   dmg      … 与ダメージ（HP単位、四捨五入・最低1・最大10）
 *   def_hp   … 攻撃後の防御側HP
 *   counter  … 予想反撃ダメージ（反撃が起きない場合 0）
 *   atk_hp   … 反撃後の攻撃側HP
 * 反撃の有無は game_attack と同じ規則（間接攻撃には反撃なし・射程・対潜など）。 */
void battle_forecast(const Game *g, int atk_i, int def_i,
                     int *dmg, int *def_hp, int *counter, int *atk_hp);

#endif
