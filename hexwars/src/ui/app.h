/* app.h - アプリ全体状態（SDL依存層） */
#ifndef HW_APP_H
#define HW_APP_H

#include <SDL.h>
#include <SDL_ttf.h>
#include "../core/game.h"
#include "../core/hex.h"
#include "../core/ai.h"
#include "../core/path.h"
#include "../core/campaign.h"
#include "../core/save.h"
#include "../data/parser.h"

#define WIN_W 1280
#define WIN_H 800

typedef enum {
    SCREEN_TITLE = 0,
    SCREEN_SETUP,
    SCREEN_LOAD,
    SCREEN_BRIEFING,
    SCREEN_OPTIONS,
    SCREEN_BATTLE,
    SCREEN_RESULT,
    SCREEN_CPNMAP,     /* キャンペーン全体マップ（仕様書 6.2） */
    SCREEN_REWARD,     /* クリア時のご褒美画像/動画 */
    SCREEN_ENDROLL,    /* キャンペーン全クリア後のエンドロール */
    SCREEN_DEPLOY,     /* 出撃部隊の編成（持越しが上限を超えるとき） */
    SCREEN_COUNT
} ScreenId;

/* 戦闘画面サブ状態（仕様書 8.1） */
typedef enum {
    BS_IDLE = 0,
    BS_UNIT_SELECTED,
    BS_ACTION_MENU,
    BS_TARGET_SELECT,
    BS_PRODUCTION,
    BS_TURN_MENU,
    BS_SAVE_MENU,      /* セーブスロット選択 */
    BS_UNLOAD,         /* 輸送ユニットから降ろす先の選択 */
    BS_WORK,           /* 工兵の工作先の選択（破壊・復旧） */
    BS_LAYER_PICK,     /* 重なりセルのユニット選択（空/海面/海中） */
    BS_UNITLIST,       /* 未行動ユニット一覧（選ぶとカーソルが飛ぶ） */
    BS_EVOLVE_CONFIRM, /* 進化の確認（不可逆なので必ず一度止める） */
    BS_JOIN_CONFIRM,   /* 合流の確認（片方が盤上から消えるので一度止める） */
    BS_BATTLE_ANIM,    /* 戦闘アニメ（仕様書 8.2） */
    BS_CPU_TURN,
    BS_HANDOVER,       /* ホットシート交代画面 */
    BS_GAMEOVER
} BattleState;

#define MAX_POPUPS 16
typedef struct {
    float x, y;       /* マップヘクス座標(小数) */
    int   timer;      /* 残フレーム */
    char  text[48];
    SDL_Color color;
} Popup;

/* 戦闘アニメの再生データ（結果確定後に演出だけ行う） */
typedef struct {
    int  timer;                 /* 残フレーム */
    int  total;
    uint8_t atk_type, atk_owner, def_type, def_owner;
    int  atk_hp0, atk_hp1;      /* 演出前→演出後HP */
    int  def_hp0, def_hp1;
    int  dmg, counter;
    bool def_killed, atk_killed;
    int  ax, ay, dx, dy;        /* ポップアップ用ヘクス座標 */
    bool shot_played, hit_played, cshot_played, chit_played;
    uint32_t start_ms;          /* 動画再生の基準時刻（SDL_GetTicks） */
    bool use_video;             /* この演出で動画を再生するか */
    /* 攻撃時カットイン（assets/相対パス。空=この演出では出さない）。
     * 演出開始時に「出すか」を決めて焼き付けるので、途中で設定が変わってもぶれない。 */
    char cutin[64];
} BattleAnim;

typedef struct App App;

/* 画面ハンドラ（enter/update/draw は関数ポインタテーブル） */
typedef struct {
    void (*enter)(App *a);
    void (*event)(App *a, const SDL_Event *e);
    void (*update)(App *a);
    void (*draw)(App *a);
} Screen;

struct App {
    SDL_Window   *win;
    SDL_Renderer *ren;
    TTF_Font *font_s, *font_m, *font_l, *font_xl;
    char base_path[512];

    bool     quit;
    ScreenId screen;
    ScreenId next_screen;
    uint32_t frame;

    Game    game;
    MapList maps;
    AiState ai;

    /* セットアップ画面の選択 */
    int sel_map;
    /* 参加陣営ごとの操作者（0=CPU弱 1=CPU普 2=CPU強 3=人間）。
     * マップによって何陣営参加するかが変わるので、
     * setup_parts に立っている陣営の分だけ使う。 */
    uint8_t sel_ctrl[MAX_PLAYERS];
    unsigned setup_parts;   /* 選択中マップの参加陣営ビット */
    /* 人間の陣営が全て倒れて終了したか。
     * AI同士の決着を待っても操作できることはないのでその時点で終わる。
     * winner は WINNER_NONE のままなので、結果画面はこの旗で判別する。 */
    bool  human_out;
    int sel_p2;      /* 旧: 互換のため残す（sel_ctrl[1] と連動） */
    int sel_fog;     /* 0=ON 1=OFF */
    int sel_co[MAX_PLAYERS];  /* 陣営ごとの指揮官 cos[] のindex */
    int sel_co0;     /* 旧: sel_co[0] の別名（main.c から使われる） */
    int sel_co1;     /* 旧: sel_co[1] の別名 */
    int setup_row;

    /* タイトル/結果のメニュー選択 */
    int menu_idx;

    /* キャンペーン */
    bool          campaign_mode;
    Campaign      cpn;
    CampaignState cps;
    CampaignState cps_backup;   /* 再挑戦用（開戦直前の状態） */
    int           cpn_result;   /* 0=通常 1=次ノードへ 2=クリア 3=敗北 */

    /* 出撃部隊の編成（持越しが上限超過のとき） */
    uint8_t dep_sel[MAX_CARRY_UNITS];  /* 1=出撃させる */
    int     dep_limit;                 /* このマップの出撃上限 */
    int     dep_idx, dep_scroll;
    uint32_t dep_seed;                 /* 開戦に使う乱数シード */

    /* ロード画面 */
    int load_idx;

    /* オプション（永続化: options.cfg） */
    int opt_bgm;    /* 0..10 */
    int opt_se;     /* 0..10 */
    int opt_anim;   /* 0/1 戦闘アニメ */
    int opt_anim_video; /* 0/1 戦闘アニメを動画で再生（既定OFF。動画未指定なら従来演出） */
    int opt_bgm_track;  /* 戦闘BGM: -1=自動(マップ毎) / 0..SND_BATTLE_TRACKS-1=曲指定 */
    int opt_se_set;     /* 効果音セット: 0=標準 1=レトロ 2=重厚 */
    int opt_tilt;       /* 0=平面(真上) / 1=斜め見下ろし（Y圧縮＋地形の起伏）。見た目のみ */
    int opt_cutin;      /* 攻撃時カットイン: 0=出さない / 1=毎回 / 2=撃破時のみ */
    int opt_weather_fx; /* 天候の画面演出: 0=切る / 1=出す。見た目のみ */
    int cpn_scroll;     /* 作戦全体図の縦スクロールpx */
    /* 進捗（progress.cfg に永続化。指揮官の解禁条件に使う） */
    int progress_clears;   /* これまでにクリアした作戦の総数 */
    int progress_best[MAX_CAMPAIGN_MAPS]; /* ノード毎の最高ランク（CpnRank） */
    int opt_row;

    /* 戦闘アニメ */
    BattleAnim anim;

    /* ブリーフィング1枚絵（enter時に読込、exit相当で破棄） */
    SDL_Texture *wx_blob;      /* 天候演出のやわらかい円（雲の影・陽光） */
    SDL_Texture *brief_tex;
    int brief_tex_w, brief_tex_h;
    /* クリア報酬画像（enter時に読込） */
    SDL_Texture *reward_tex;
    int reward_tex_w, reward_tex_h;
    /* エンドロール */
    uint32_t endroll_start_ms;   /* 開始時刻（スクロール・動画再生の基準） */
    int      endroll_done;       /* 1=最後まで流し終えた（Zで抜けられる） */

    /* --- 戦闘画面状態 --- */
    BattleState bs;
    float cam_x, cam_y;      /* カメラ左上（ワールドpx） */
    int   zoom;              /* 0..2 */
    int   cur_x, cur_y;      /* カーソルヘクス */
    int   sel_unit;          /* 選択中 unit index / -1 */
    MoveRange mr;
    /* 移動アンドゥ */
    int   undo_x, undo_y, undo_fuel;
    uint8_t undo_flags;
    bool  moved_pending;     /* 移動済みで行動選択待ち */
    /* 行動メニュー */
    int   amenu_items[8];    /* 行動ID列 */
    int   amenu_n, amenu_idx;
    /* 攻撃対象 */
    int   targets[32];
    int   n_targets, target_idx;
    /* 生産（通常生産＋倉庫からの引き出しを同一メニューに列挙） */
    int   prod_x, prod_y;
    int   prod_items[MAX_UNIT_TYPES + MAX_STORE_UNITS];  /* ユニット型index */
    int   prod_store[MAX_UNIT_TYPES + MAX_STORE_UNITS];  /* 倉庫slot（-1=通常生産） */
    int   prod_n, prod_idx;
    int   prod_scroll;       /* 生産リストの先頭表示index（スクロール） */
    /* 重なりセルの情報パネル巡回（カーソルを当てている間、一定間隔で切替） */
    int      panel_cx, panel_cy;   /* 巡回の基準セル（変わったら最初から） */
    int      panel_unit;           /* 情報パネルに今出しているユニット（-1=なし） */
    uint32_t panel_base_frame;
    /* ターンメニュー */
    int   tmenu_idx;
    /* 未行動ユニット一覧 */
    int   ulist[MAX_UNITS];
    int   ulist_n, ulist_idx, ulist_scroll;
    /* セーブメニュー */
    int   smenu_idx;
    int   join_target;       /* 合流先 unit index（BS_JOIN_CONFIRM 中のみ有効） */
    /* 降車先候補。隣接6方向 + 空挺降下の「真下」1つで最大7 */
    int   unload_x[7], unload_y[7];
    int   n_unload;
    /* 工兵の工作先（隣接の最大6ヘクス） */
    uint8_t work_x[HEX_DIRS], work_y[HEX_DIRS];
    int   n_work;
    int   unload_count;   /* 今回の降車で既に降ろした数（複数降車の確定判定用） */
    /* レイヤー選択ポップアップ（重なりセルの選択・攻撃対象選択） */
    int   lpick_unit[LAYER_COUNT];   /* 候補ユニットindex（表示順） */
    int   lpick_n, lpick_idx;
    int   lpick_mode;                /* LP_SELECT=自軍選択 / LP_ATTACK=攻撃対象 */
    int   lpick_x, lpick_y;          /* ポップアップの表示基準セル（マウスで動かさない） */
    /* CPU */
    uint32_t cpu_wait;
    /* 演出 */
    Popup popups[MAX_POPUPS];
    char  banner[64];
    int   banner_timer;
    /* 必殺技のカットイン。攻撃時のカットインは戦闘演出（BattleAnim）に
     * 乗っているが、必殺技は戦闘を伴わないので別に持つ。
     * co_cutin_p は発動した陣営（0=自軍は左から / 1=敵は右から出す）。 */
    char  co_cutin[64];
    int   co_cutin_timer;
    int   co_cutin_total;
    int   co_cutin_p;
    /* マウスドラッグスクロール */
    bool  dragging;
    int   drag_sx, drag_sy;
    float drag_cx, drag_cy;
};

/* assets.c */
int  assets_init(App *a);
void assets_quit(App *a);
void draw_text(App *a, TTF_Font *f, int x, int y, SDL_Color c, const char *s);
/* 天候の見た目（マップの上に重ねる層）。ルールには影響しない */
#define TOPBAR_FX 36        /* 上のバーの高さ（ここより下だけに演出をかける） */
void render_weather_fx(App *a, int weather, uint32_t frame);
void render_weather_icon(App *a, int x, int y, int weather);
void draw_text_center(App *a, TTF_Font *f, int cx, int y, SDL_Color c, const char *s);
int  text_width(App *a, TTF_Font *f, const char *s);

/* render.c */
extern const SDL_Color COL_P[MAX_PLAYERS];
extern const SDL_Color COL_WHITE, COL_BLACK, COL_YELLOW, COL_GRAY, COL_DIM;
float hex_size(const App *a);
void  hex_center_px(const App *a, int x, int y, float *px, float *py);
bool  px_to_hex(const App *a, int mx, int my, int *hx, int *hy);
void  render_fill_hex(App *a, float cx, float cy, float size, SDL_Color c);
void  render_hex_outline(App *a, float cx, float cy, float size, SDL_Color c);
/* マップ上のヘクス用（斜め見下ろし表示ならY方向に潰れる）。
 * タイトル画面等のUI装飾は潰してはいけないので上の非 _map 版を使う。 */
void  render_fill_hex_map(App *a, float cx, float cy, float size, SDL_Color c);
void  render_hex_outline_map(App *a, float cx, float cy, float size, SDL_Color c);
float hex_tilt_squash(const App *a);   /* Y圧縮率（平面表示なら1.0） */
void  render_map(App *a);
void  fill_rect(App *a, int x, int y, int w, int h, SDL_Color c);
void  outline_rect(App *a, int x, int y, int w, int h, SDL_Color c);
void  battle_add_popup(App *a, int hx, int hy, const char *text, SDL_Color c);

/* screens */
extern const Screen SCREENS[SCREEN_COUNT];

/* セーブスロットのフルパスを組み立てる（slot 0=オートセーブ） */
void ui_save_path(const App *a, int slot, char *out, int outlen);

/* 陣営名（text_ja.def から） */
const char *faction_name(int p);

/* 現在のキャンペーンノードにクリア報酬画像が設定されているか */
bool reward_available(App *a);

/* オプション永続化（options.cfg） */
void options_load(App *a);
void options_save(App *a);

/* 進捗（クリア数・各作戦の最高ランク）の永続化 */
/* クリア済みの作戦「種類数」（指揮官の解禁基準。周回では増えない） */
int  progress_count_cleared(const App *a);
void progress_load(App *a);
void progress_save(App *a);
/* 指揮官 idx が解禁済みか（commanders.def の unlock_clears と進捗で判定） */
bool co_is_unlocked(const App *a, int idx);
/* 解禁済みの指揮官を dir 方向に探す（見つからなければ from を返す） */
int  co_next_unlocked(const App *a, int from, int dir);

#endif
