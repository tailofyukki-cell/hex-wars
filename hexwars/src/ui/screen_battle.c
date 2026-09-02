/* screen_battle.c - 戦闘画面（サブ状態機械。仕様書 8章） */
#include <stdlib.h>
#include "app.h"
#include "text.h"
#include "sound.h"
#include "sprites.h"
#include "anim.h"
#include "../core/path.h"
#include "../core/rules.h"
#include "../core/hex.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

enum { ACT_ATTACK = 0, ACT_CAPTURE, ACT_WAIT, ACT_CANCEL, ACT_UNLOAD,
       ACT_SUPPLY, ACT_HEAL, ACT_EVOLVE, ACT_WORK };
enum { LP_SELECT = 0, LP_ATTACK };   /* レイヤー選択ポップアップの用途 */
static const char *ACT_KEYS[] = {
    "ACT_ATTACK", "ACT_CAPTURE", "ACT_WAIT", "ACT_CANCEL", "ACT_UNLOAD",
    "ACT_SUPPLY", "ACT_HEAL", "ACT_EVOLVE", "ACT_WORK"
};

/* ターンメニューの項目（順番を変えたらここだけ直せば済むように名前を付ける） */
enum { TM_UNITLIST = 0, TM_END, TM_SAVE, TM_TILT, TM_TITLE, TM_CLOSE, TMENU_ITEMS };

#define UNITLIST_VISIBLE 10   /* 未行動一覧に一度に表示する行数 */
/* 重なりセルの情報パネルを切り替える間隔（フレーム。60fps想定で約1.5秒） */
#define PANEL_CYCLE_FRAMES 90u

static void snd_move_se(const Game *g, int ui)
{
    /* units.def の move_se で指定できる（未指定なら class から自動で入っている） */
    int se = g->types[g->units[ui].type].move_se;
    if (se < 0 || se >= SE_COUNT) se = SE_MOVE_VEHICLE;
    snd_se(se);
}

#define TOPBAR_H TOPBAR_FX   /* 天候演出と共有（app.h） */
#define PANEL_H 116

/* ------------------------------------------------------------------ */
/* カメラ                                                              */
/* ------------------------------------------------------------------ */
static void clamp_camera(App *a)
{
    float s = hex_size(a);
    float map_w = 1.7320508f * s * (a->game.w + 1);
    /* 斜め見下ろし表示ではマップ全体がY方向に潰れるので、可動域も同じだけ縮める */
    float map_h = (1.5f * s * a->game.h + s) * hex_tilt_squash(a);
    float max_x = map_w - WIN_W;
    float max_y = map_h - (WIN_H - TOPBAR_H - PANEL_H);
    if (max_x < 0) max_x = 0;
    if (max_y < 0) max_y = 0;
    if (a->cam_x < -s * 2) a->cam_x = -s * 2;
    if (a->cam_y < -s * 2 - TOPBAR_H) a->cam_y = -s * 2 - TOPBAR_H;
    if (a->cam_x > max_x + s * 2) a->cam_x = max_x + s * 2;
    if (a->cam_y > max_y + s * 2) a->cam_y = max_y + s * 2;
}

static void center_camera(App *a, int hx, int hy)
{
    float cx, cy;
    a->cam_x = 0; a->cam_y = 0;
    hex_center_px(a, hx, hy, &cx, &cy);
    a->cam_x = cx - WIN_W / 2.0f;
    a->cam_y = cy - WIN_H / 2.0f;
    clamp_camera(a);
}

static void ensure_cursor_visible(App *a)
{
    float cx, cy;
    hex_center_px(a, a->cur_x, a->cur_y, &cx, &cy);
    float s = hex_size(a);
    float mgn = s * 2.5f;
    if (cx < mgn) a->cam_x -= (mgn - cx);
    if (cx > WIN_W - mgn) a->cam_x += (cx - (WIN_W - mgn));
    if (cy < TOPBAR_H + mgn) a->cam_y -= (TOPBAR_H + mgn - cy);
    if (cy > WIN_H - PANEL_H - mgn) a->cam_y += (cy - (WIN_H - PANEL_H - mgn));
    clamp_camera(a);
}

/* ------------------------------------------------------------------ */
/* 手番開始・終了                                                      */
/* ------------------------------------------------------------------ */
static void show_ai_co_power(App *a);

static void set_banner(App *a, const char *text, int frames)
{
    snprintf(a->banner, sizeof a->banner, "%s", text);
    a->banner_timer = frames;
}

/* 見ている側（viewer）から見た関係を返す。
 * 多陣営だと陣営色だけでは援軍か敵か判別できないので、文字でも示す。 */
static const char *rel_tag(const Game *g, int viewer, int owner)
{
    if (owner < 0 || viewer < 0) return "";
    if (owner == viewer)                    return tx("REL_SELF");
    if (game_same_team(g, viewer, owner))   return tx("REL_ALLY");
    return tx("REL_ENEMY");
}

/* 生き残っている人間の陣営があるか */
static bool any_human_alive(const Game *g)
{
    for (int p = 0; p < MAX_PLAYERS; p++) {
        if (g->ctrl[p] != CTRL_HUMAN) continue;
        if (!game_player_in_play(g, p)) continue;
        if (!game_player_defeated(g, p)) return true;
    }
    return false;
}

static void check_over(App *a)
{
    Game *g = &a->game;
    /* 人間の陣営が全滅したら、AI同士の決着を待たずに終わる。
     * 3陣営以上だと、自分が倒れてから残りAIが数十ターン戦い続けることになる。 */
    if (g->winner == WINNER_NONE) {
        if (a->human_out || any_human_alive(g)) return;
        a->human_out = true;
        a->bs = BS_GAMEOVER;
        a->sel_unit = -1;
        set_banner(a, tx("RESULT_HUMAN_OUT"), 180);
        return;
    }
    a->bs = BS_GAMEOVER;
    a->sel_unit = -1;
    char buf[64];
    if (a->game.winner >= 0)
        snprintf(buf, sizeof buf, tx("RESULT_WIN_FMT"),
                 faction_name(a->game.winner));
    else
        snprintf(buf, sizeof buf, "%s", tx("RESULT_DRAW"));
    set_banner(a, buf, 160);
}

static void focus_own_unit(App *a)
{
    Game *g = &a->game;
    for (int i = 0; i < g->n_units; i++) {
        const Unit *u = &g->units[i];
        if ((u->flags & UF_ALIVE) && u->owner == g->current &&
            !(u->flags & UF_DONE)) {
            a->cur_x = u->pos.x; a->cur_y = u->pos.y;
            center_camera(a, u->pos.x, u->pos.y);
            return;
        }
    }
}

/* 手番開始時にマップイベントを判定し、起きたことをプレイヤーに知らせる。
 * イベントは盤面を変える（増援が湧く等）ので、黙って進めると理不尽に見える。
 * 複数同時に起きた場合は先頭をバナーに出し、残りはログ代わりにポップアップする。 */
static void run_map_events(App *a)
{
    Game *g = &a->game;
    const char *msgs[MAX_EVENTS];
    for (int i = 0; i < MAX_EVENTS; i++) msgs[i] = NULL;
    int n = game_check_events(g, msgs, MAX_EVENTS);
    if (n <= 0) return;
    for (int i = 0; i < n && i < MAX_EVENTS; i++) {
        if (!msgs[i]) continue;
        if (i == 0) set_banner(a, msgs[i], 140);
        battle_add_popup(a, a->cur_x, a->cur_y, msgs[i], COL_YELLOW);
    }
    snd_se(SE_CAPTURE);
}

static void begin_side(App *a)
{
    Game *g = &a->game;
    a->sel_unit = -1;
    /* **先に BS_IDLE へ戻すこと**。直前の戦闘が BS_GAMEOVER のまま
     * 次のマップに入ると、下の return で手番を始めずに抜け、
     * battle_update がそのまま結果画面へ飛ばす（開幕で引き分けになる）。 */
    a->bs = BS_IDLE;
    /* winner だけでなく「人間が全滅」も見るので無条件に呼ぶ */
    check_over(a);
    if (a->bs == BS_GAMEOVER) return;

    char buf[64];
    snprintf(buf, sizeof buf, tx("BANNER_TURN_FMT"),
             g->turn, faction_name(g->current));

    if (g->ctrl[g->current] != CTRL_HUMAN) {
        ai_begin_turn(g, &a->ai);
        a->bs = BS_CPU_TURN;
        a->cpu_wait = 30;
        set_banner(a, buf, 90);
        run_map_events(a);     /* 増援などのイベント（バナーを上書きして知らせる） */
        show_ai_co_power(a);   /* 手番開始に撃った必殺技を通知 */
    } else {
        /* 手番開始時にオートセーブ（仕様書 9章、slot 0） */
        char path[600];
        ui_save_path(a, 0, path, sizeof path);
        save_game(g, &a->cps, path, NULL, 0);

        /* 人間が2人以上のときだけ手番交代画面を挑む（盤面を見せないため）。
         * 多陣営でも人間は1陣営なので通常は挑まない。 */
        int humans = 0;
        for (int p = 0; p < MAX_PLAYERS; p++)
            if (game_player_in_play(g, p) && g->ctrl[p] == CTRL_HUMAN) humans++;
        a->bs = (g->fog && humans >= 2) ? BS_HANDOVER : BS_IDLE;
        set_banner(a, buf, 120);
        snd_se(SE_TURN);
        focus_own_unit(a);
        run_map_events(a);     /* 増援などのイベント（ターン表示の後に上書き） */
    }
}

static void do_end_turn(App *a)
{
    game_end_turn(&a->game);
    check_over(a);
    if (a->bs == BS_GAMEOVER) return;
    begin_side(a);
}

/* ------------------------------------------------------------------ */
/* メニュー矩形                                                        */
/* ------------------------------------------------------------------ */
static SDL_Rect amenu_rect(App *a, int i)
{
    float cx, cy;
    Unit *u = &a->game.units[a->sel_unit];
    hex_center_px(a, u->pos.x, u->pos.y, &cx, &cy);
    int x = (int)cx + 40;
    int y = (int)cy - 20;
    if (x + 150 > WIN_W) x = (int)cx - 190;
    if (y + a->amenu_n * 40 > WIN_H - PANEL_H) y = WIN_H - PANEL_H - a->amenu_n * 40 - 8;
    if (y < TOPBAR_H) y = TOPBAR_H + 4;
    SDL_Rect r = { x, y + i * 40, 150, 36 };
    return r;
}

/* 上部バーの指揮官ゲージ（クリックで必殺技発動） */
static SDL_Rect co_gauge_rect(void)
{
    SDL_Rect r = { WIN_W - 380, 4, 240, 28 };
    return r;
}

static void start_co_cutin(App *a, const CommanderType *co, int player);

/* 必殺技を発動する（使えないときは何もしない） */
/* CPUが必殺技を撃ったらプレイヤーにも見えるようにバナー＋SEで知らせる */
static void show_ai_co_power(App *a)
{
    if (!a->ai.co_fired) return;
    a->ai.co_fired = false;
    Game *g = &a->game;
    const CommanderType *co = game_co(g, g->current);
    if (!co) return;
    char buf[96];
    snprintf(buf, sizeof buf, tx("CO_POWER_BANNER_FMT"), co->name, co->power_name);
    set_banner(a, buf, 120);
    battle_add_popup(a, a->cur_x, a->cur_y, co->power_name, COL_YELLOW);
    start_co_cutin(a, co, g->current);
    snd_se(SE_CAPTURE);
}

static void try_co_power(App *a)
{
    Game *g = &a->game;
    const CommanderType *co = game_co(g, g->current);
    if (!co || !game_co_power_ready(g, g->current)) {
        snd_se(SE_CANCEL);
        return;
    }
    if (game_co_activate(g, g->current)) {
        char buf[96];
        snprintf(buf, sizeof buf, tx("CO_POWER_BANNER_FMT"), co->name, co->power_name);
        set_banner(a, buf, 100);
        battle_add_popup(a, a->cur_x, a->cur_y, co->power_name, COL_YELLOW);
        start_co_cutin(a, co, g->current);
        snd_se(SE_CAPTURE);
    }
}

/* 進化の確認ボックスの「はい/いいえ」 */
static SDL_Rect evo_rect(int i)
{
    SDL_Rect r = { WIN_W / 2 - 150 + i * 156, WIN_H / 2 + 34, 144, 44 };
    return r;
}

static SDL_Rect tmenu_rect(int i)
{
    SDL_Rect r = { WIN_W / 2 - 130, 260 + i * 52, 260, 44 };
    return r;
}

static SDL_Rect smenu_rect(int i)
{
    SDL_Rect r = { WIN_W / 2 - 280, 120 + i * 46, 560, 42 };
    return r;
}

/* 手動セーブ実行（slot 1..10） */
static void do_save_slot(App *a, int slot)
{
    char path[600], err[256];
    ui_save_path(a, slot, path, sizeof path);
    if (save_game(&a->game, &a->cps, path, err, sizeof err) == 0) {
        battle_add_popup(a, a->cur_x, a->cur_y, tx("POP_SAVED"), COL_YELLOW);
        snd_se(SE_OK);
        a->bs = BS_IDLE;
    } else {
        SDL_Log("セーブ失敗: %s", err);
        battle_add_popup(a, a->cur_x, a->cur_y, tx("POP_SAVEFAIL"), COL_GRAY);
        snd_se(SE_CANCEL);
    }
}

#define PROD_VISIBLE 10   /* 生産メニューに一度に表示する行数 */

/* i 番目の項目の矩形（スクロール位置を考慮した画面上の行） */
static SDL_Rect prod_rect(App *a, int i)
{
    SDL_Rect r = { WIN_W / 2 - 260, 140 + (i - a->prod_scroll) * 44, 520, 40 };
    return r;
}

/* 選択中の項目 prod_idx が表示範囲に入るよう prod_scroll を調整 */
static void prod_keep_visible(App *a)
{
    if (a->prod_idx < a->prod_scroll)
        a->prod_scroll = a->prod_idx;
    else if (a->prod_idx >= a->prod_scroll + PROD_VISIBLE)
        a->prod_scroll = a->prod_idx - PROD_VISIBLE + 1;
    int maxscroll = a->prod_n - PROD_VISIBLE;
    if (maxscroll < 0) maxscroll = 0;
    if (a->prod_scroll > maxscroll) a->prod_scroll = maxscroll;
    if (a->prod_scroll < 0) a->prod_scroll = 0;
}

/* ------------------------------------------------------------------ */
/* 行動メニュー構築                                                    */
/* ------------------------------------------------------------------ */
static bool unit_has_moved_pending(App *a)
{
    Unit *u = &a->game.units[a->sel_unit];
    return u->pos.x != a->undo_x || u->pos.y != a->undo_y;
}

/* 選択中の輸送ユニットが降車できるヘクスを列挙する（複数降車で毎回再計算）。
 * 通常は隣接6方向だが、空挺降下できる輸送機だけは「真下（自分と同じヘクス）」も
 * 候補に入る。輸送機は空レイヤーにいるので地表が空いていれば降ろせる。 */
static void compute_unload_targets(App *a)
{
    Game *g = &a->game;
    Unit *u = &g->units[a->sel_unit];
    a->n_unload = 0;
    if (game_first_cargo(g, a->sel_unit) < 0) return;
    if (g->types[u->type].paradrop &&
        game_can_unload_to(g, a->sel_unit, u->pos.x, u->pos.y)) {
        a->unload_x[a->n_unload] = u->pos.x;
        a->unload_y[a->n_unload] = u->pos.y;
        a->n_unload++;
    }
    for (int d = 0; d < HEX_DIRS; d++) {
        int nx, ny;
        hex_neighbor(u->pos.x, u->pos.y, d, &nx, &ny);
        if (!game_in_bounds(g, nx, ny)) continue;
        if (game_can_unload_to(g, a->sel_unit, nx, ny)) {
            a->unload_x[a->n_unload] = nx;
            a->unload_y[a->n_unload] = ny;
            a->n_unload++;
        }
    }
}

/* 工兵が今工作できる隣接ヘクスを集める */
static void compute_work_targets(App *a)
{
    a->n_work = game_work_targets(&a->game, a->sel_unit,
                                  a->work_x, a->work_y, HEX_DIRS);
}

static void build_action_menu(App *a)
{
    Game *g = &a->game;
    Unit *u = &g->units[a->sel_unit];
    const TerrainType *t = game_terrain_at(g, u->pos.x, u->pos.y);
    bool moved = unit_has_moved_pending(a);

    a->amenu_n = 0;
    if (rules_can_attack_now(g, a->sel_unit, moved)) {
        int tg[32];
        if (rules_list_targets(g, a->sel_unit, u->pos.x, u->pos.y, tg, 32) > 0)
            a->amenu_items[a->amenu_n++] = ACT_ATTACK;
    }
    if (t->capturable && g->tiles[u->pos.y][u->pos.x].owner != u->owner &&
        g->types[u->type].can_capture)
        a->amenu_items[a->amenu_n++] = ACT_CAPTURE;
    /* 輸送ユニット: 降ろせる隣接ヘクスがあれば「降ろす」 */
    if (game_first_cargo(g, a->sel_unit) >= 0) {
        compute_unload_targets(a);
        if (a->n_unload > 0)
            a->amenu_items[a->amenu_n++] = ACT_UNLOAD;
    }
    /* 補給車: 隣接に燃料/弾薬切れの味方がいれば「補給」 */
    if (game_can_supply(g, a->sel_unit))
        a->amenu_items[a->amenu_n++] = ACT_SUPPLY;
    /* 補給車: 隣接に損傷した味方がいて物資10以上あれば「回復」 */
    if (game_can_heal(g, a->sel_unit))
        a->amenu_items[a->amenu_n++] = ACT_HEAL;
    /* 工兵: 隣接に壊せる/直せる地形があれば「工作」。
     * 移動後でもできる（前線まで出て壊すのが仕事なため）。 */
    if (game_unit_is_engineer(g, a->sel_unit)) {
        compute_work_targets(a);
        if (a->n_work > 0) a->amenu_items[a->amenu_n++] = ACT_WORK;
    }
    /* 経験値が満タンで、自軍の補給拠点の上にいるなら「進化」 */
    if (game_can_evolve(g, a->sel_unit))
        a->amenu_items[a->amenu_n++] = ACT_EVOLVE;
    a->amenu_items[a->amenu_n++] = ACT_WAIT;
    a->amenu_items[a->amenu_n++] = ACT_CANCEL;
    a->amenu_idx = 0;
    a->bs = BS_ACTION_MENU;
}

/* ------------------------------------------------------------------ */
/* アクション実行                                                      */
/* ------------------------------------------------------------------ */
/* 演出終了後のポップアップと後始末 */
static void finish_attack_effects(App *a)
{
    BattleAnim *an = &a->anim;
    char buf[48];
    snprintf(buf, sizeof buf, "-%d", an->dmg);
    battle_add_popup(a, an->dx, an->dy, buf, (SDL_Color){ 255, 120, 100, 255 });
    if (an->def_killed)
        battle_add_popup(a, an->dx, an->dy - 1 >= 0 ? an->dy - 1 : an->dy,
                         tx("POP_KILL"), COL_YELLOW);
    if (an->counter > 0) {
        snprintf(buf, sizeof buf, "-%d", an->counter);
        battle_add_popup(a, an->ax, an->ay, buf,
                         (SDL_Color){ 255, 200, 120, 255 });
        if (an->atk_killed)
            battle_add_popup(a, an->ax, an->ay - 1 >= 0 ? an->ay - 1 : an->ay,
                             tx("POP_LOST"), (SDL_Color){ 255, 160, 160, 255 });
    }
    a->sel_unit = -1;
    a->bs = BS_IDLE;
    check_over(a);
}

static void execute_attack(App *a, int target)
{
    Game *g = &a->game;
    int atk = a->sel_unit;
    Unit *au = &g->units[atk];
    Unit *du = &g->units[target];

    BattleAnim *an = &a->anim;
    memset(an, 0, sizeof *an);
    an->atk_type = au->type;
    an->atk_owner = au->owner;
    an->def_type = du->type;
    an->def_owner = du->owner;
    an->atk_hp0 = au->hp;
    an->def_hp0 = du->hp;
    an->ax = au->pos.x; an->ay = au->pos.y;
    an->dx = du->pos.x; an->dy = du->pos.y;

    int cd = 0;
    bool dk = false, ak = false;
    int dmg = game_attack(g, atk, target, &cd, &dk, &ak);

    an->dmg = dmg;
    an->counter = cd;
    an->def_killed = dk;
    an->atk_killed = ak;
    an->def_hp1 = dk ? 0 : du->hp;
    an->atk_hp1 = ak ? 0 : au->hp;

    if (a->opt_anim) {
        an->total = an->timer = (cd > 0 || ak) ? 150 : 95;
        an->start_ms = SDL_GetTicks();
        an->use_video = false;
        /* カットイン: 攻撃側ユニットの cutin を優先し、無ければ攻撃側指揮官のものを使う。
         * opt_cutin: 0=出さない / 1=毎回 / 2=撃破したときだけ */
        an->cutin[0] = '\0';
        if (a->opt_cutin == 1 || (a->opt_cutin == 2 && dk)) {
            const char *rel = g->types[an->atk_type].cutin;
            if (!rel[0]) {
                const CommanderType *co = game_co(g, an->atk_owner);
                if (co) rel = co->cutin;
            }
            if (rel[0])
                snprintf(an->cutin, sizeof an->cutin, "%s", rel);
        }
        /* 動画モードON かつ 攻撃側ユニットに動画が指定・読込できる場合のみ動画演出。
         * 動画が無ければ従来のHPバー演出にフォールバックする。 */
        if (a->opt_anim_video) {
            UnitAnim *ua = uanim_get(a, an->atk_type);
            if (ua) {
                an->use_video = true;
                /* 動画の長さに合わせて演出時間を決める（60fps換算、1〜10秒に制限） */
                int frames = ua->total_ms * 60 / 1000;
                if (frames < 60)  frames = 60;
                if (frames > 600) frames = 600;
                an->total = an->timer = frames;
            }
        }
        a->bs = BS_BATTLE_ANIM;
    } else {
        snd_se(SE_SHOT);
        snd_se(SE_EXPLOSION);
        finish_attack_effects(a);
    }
}

static void select_action(App *a, int act)
{
    Game *g = &a->game;
    Unit *u = &g->units[a->sel_unit];
    switch (act) {
    case ACT_ATTACK:
        a->n_targets = rules_list_targets(g, a->sel_unit, u->pos.x, u->pos.y,
                                          a->targets, 32);
        if (a->n_targets > 0) {
            a->target_idx = 0;
            a->cur_x = g->units[a->targets[0]].pos.x;
            a->cur_y = g->units[a->targets[0]].pos.y;
            a->bs = BS_TARGET_SELECT;
        }
        break;
    case ACT_CAPTURE: {
        int done = game_capture(g, a->sel_unit);
        battle_add_popup(a, u->pos.x, u->pos.y,
                         tx(done ? "POP_CAP_DONE" : "POP_CAP_PROG"),
                         done ? COL_YELLOW : COL_WHITE);
        snd_se(done ? SE_CAPTURE : SE_OK);
        a->sel_unit = -1;
        a->bs = BS_IDLE;
        check_over(a);
        break;
    }
    case ACT_WORK:
        a->bs = BS_WORK;
        if (a->n_work > 0) { a->cur_x = a->work_x[0]; a->cur_y = a->work_y[0]; }
        break;
    case ACT_UNLOAD:
        a->bs = BS_UNLOAD;
        a->unload_count = 0;
        if (a->n_unload > 0) {
            a->cur_x = a->unload_x[0];
            a->cur_y = a->unload_y[0];
        }
        break;
    case ACT_SUPPLY: {
        int n = game_supply_adjacent(g, a->sel_unit);
        char buf[48];
        snprintf(buf, sizeof buf, tx("POP_SUPPLY_FMT"), n);
        battle_add_popup(a, u->pos.x, u->pos.y, buf, COL_YELLOW);
        snd_se(SE_CAPTURE);
        a->sel_unit = -1;
        a->bs = BS_IDLE;
        break;
    }
    case ACT_EVOLVE:
        /* 不可逆なので必ず確認を挟む。ここではまだ何も変えない */
        a->amenu_idx = 1;              /* 既定は「いいえ」に置く */
        a->bs = BS_EVOLVE_CONFIRM;
        return;
    case ACT_HEAL: {
        int hp = game_supply_heal(g, a->sel_unit);
        char buf[48];
        snprintf(buf, sizeof buf, tx("POP_HEAL_FMT"), hp);
        battle_add_popup(a, u->pos.x, u->pos.y, buf,
                         (SDL_Color){ 140, 240, 140, 255 });
        snd_se(SE_CAPTURE);
        a->sel_unit = -1;
        a->bs = BS_IDLE;
        break;
    }
    case ACT_WAIT:
        game_wait_unit(g, a->sel_unit);
        a->sel_unit = -1;
        a->bs = BS_IDLE;
        break;
    case ACT_CANCEL: {
        /* 移動を巻き戻す */
        Unit *uu = &g->units[a->sel_unit];
        uu->pos.x = (uint8_t)a->undo_x;
        uu->pos.y = (uint8_t)a->undo_y;
        uu->fuel = (uint8_t)a->undo_fuel;
        uu->flags = a->undo_flags;
        game_update_vision(g);
        a->bs = BS_UNIT_SELECTED;
        break;
    }
    }
}

/* 移動先決定 */
static void try_move_to(App *a, int hx, int hy)
{
    Game *g = &a->game;
    int ui = a->sel_unit;
    Unit *u = &g->units[ui];

    if (a->mr.cost[hy][hx] < 0 || !a->mr.stop[hy][hx]) return;
    /* 立体化: 自レイヤーの占有と、搭乗可能な味方輸送(レイヤー跨ぎ=空母含む)を分けて見る */
    Layer ml = unit_layer(g->types[u->type].mclass);
    int same = game_unit_at_layer(g, hx, hy, ml);
    int board_t = -1;
    {
        int cell[LAYER_COUNT];
        game_units_at(g, hx, hy, cell);
        for (int L = 0; L < LAYER_COUNT; L++)
            if (cell[L] >= 0 && cell[L] != ui &&
                g->units[cell[L]].owner == u->owner &&
                game_can_board(g, ui, cell[L])) board_t = cell[L];
    }
    bool boarding = (board_t >= 0);
    /* 搭乗できないときだけ、同レイヤーの同種の味方への「合流」を見る */
    int join_t = (!boarding && same >= 0 && same != ui &&
                  game_can_join(g, ui, same)) ? same : -1;
    int occ = boarding ? board_t : same;
    if (!boarding && join_t < 0 && same >= 0 && same != ui)
        return;   /* 自レイヤーが塞がっていて搭乗も合流もできない */

    a->undo_x = u->pos.x; a->undo_y = u->pos.y;
    a->undo_fuel = u->fuel; a->undo_flags = u->flags;

    int fx, fy, fuel;
    int ambush = path_walk(g, ui, &a->mr, hx, hy, &fx, &fy, &fuel);
    game_move_unit(g, ui, fx, fy, fuel);

    if (fx != a->undo_x || fy != a->undo_y)
        snd_move_se(g, ui);

    if (ambush && (fx != hx || fy != hy)) {
        battle_add_popup(a, fx, fy, tx("POP_AMBUSH"), COL_YELLOW);
        game_wait_unit(g, ui);
        a->sel_unit = -1;
        a->bs = BS_IDLE;
        return;
    }
    if (join_t >= 0 && fx == hx && fy == hy) {
        /* 合流は片方が盤上から消えるので、実行前に一度確認する。
         * ここでは移動だけ済ませた状態（相手と同じセルに重なっている）で止め、
         * やめたときは ACT_CANCEL と同じように移動を巻き戻す。 */
        a->join_target = join_t;
        a->amenu_idx = 0;
        a->bs = BS_JOIN_CONFIRM;
        return;
    }
    if (boarding && fx == hx && fy == hy) {
        game_load_unit(g, ui, occ);
        battle_add_popup(a, hx, hy, tx("POP_BOARD"), COL_WHITE);
        snd_se(SE_OK);
        a->sel_unit = -1;
        a->bs = BS_IDLE;
        return;
    }
    build_action_menu(a);
}

/* ------------------------------------------------------------------ */
/* 生産メニュー                                                        */
/* ------------------------------------------------------------------ */
static void open_production(App *a, int x, int y)
{
    Game *g = &a->game;
    a->prod_x = x; a->prod_y = y;
    a->prod_n = 0;
    /* 通常生産（有料） */
    for (int t = 0; t < g->n_types; t++)
        if (game_type_buildable_at(g, x, y, t)) {
            a->prod_store[a->prod_n] = -1;
            a->prod_items[a->prod_n++] = t;
        }
    /* 倉庫からの引き出し（無料。キャンペーンのみ。この拠点で生産可能な種別のみ） */
    if (a->campaign_mode) {
        for (int k = 0; k < a->cps.n_store; k++) {
            int t = a->cps.store[k].type;
            /* 倉庫は「買う」のではなく「戻す」ので deployable で見る。
             * buildable だと進化後のユニットが一生引き出せなくなる。 */
            if (t < g->n_types && game_type_deployable_at(g, x, y, t)) {
                a->prod_store[a->prod_n] = k;
                a->prod_items[a->prod_n++] = t;
            }
        }
    }
    if (a->prod_n == 0) return;
    a->prod_idx = 0;
    a->prod_scroll = 0;
    a->bs = BS_PRODUCTION;
}

static void prod_select(App *a)
{
    Game *g = &a->game;
    int t = a->prod_items[a->prod_idx];
    int slot = a->prod_store[a->prod_idx];

    if (slot >= 0) {
        /* 倉庫から無料で引き出し（経験値保持）。成功したら倉庫から除去 */
        int exp = a->cps.store[slot].exp;
        if (game_deploy_free(g, a->prod_x, a->prod_y, t, exp) >= 0) {
            campaign_store_remove(&a->cps, slot);
            battle_add_popup(a, a->prod_x, a->prod_y, tx("POP_WITHDRAW"), COL_WHITE);
            snd_se(SE_OK);
            a->bs = BS_IDLE;
        }
        return;
    }

    if (g->funds[g->current] < g->types[t].cost) {
        battle_add_popup(a, a->prod_x, a->prod_y, tx("POP_NOFUNDS"), COL_GRAY);
        snd_se(SE_CANCEL);
        return;
    }
    if (game_produce(g, a->prod_x, a->prod_y, t) >= 0) {
        battle_add_popup(a, a->prod_x, a->prod_y, tx("POP_PROD"), COL_WHITE);
        snd_se(SE_OK);
        a->bs = BS_IDLE;
    }
}

/* ------------------------------------------------------------------ */
/* 次の未行動ユニット（Nキー）                                          */
/* ------------------------------------------------------------------ */
static void next_unit(App *a)
{
    Game *g = &a->game;
    int start = 0;
    int cur = game_unit_at(g, a->cur_x, a->cur_y);
    if (cur >= 0) start = cur + 1;
    for (int k = 0; k < g->n_units; k++) {
        int i = (start + k) % g->n_units;
        const Unit *u = &g->units[i];
        if ((u->flags & UF_ALIVE) && u->owner == g->current &&
            !(u->flags & UF_DONE)) {
            a->cur_x = u->pos.x; a->cur_y = u->pos.y;
            ensure_cursor_visible(a);
            center_camera(a, u->pos.x, u->pos.y);
            return;
        }
    }
}

/* ------------------------------------------------------------------ */
/* 未行動ユニット一覧                                                   */
/* ------------------------------------------------------------------ */
static SDL_Rect ulist_rect(App *a, int i)
{
    SDL_Rect r = { WIN_W / 2 - 300, 150 + (i - a->ulist_scroll) * 42, 600, 38 };
    return r;
}

/* 手番プレイヤーの未行動ユニットを集める（搭載中は盤上にいないので除く） */
static void build_unit_list(App *a)
{
    Game *g = &a->game;
    a->ulist_n = 0;
    for (int i = 0; i < g->n_units; i++) {
        const Unit *u = &g->units[i];
        if (!(u->flags & UF_ALIVE) || (u->flags & UF_LOADED)) continue;
        if (u->owner != g->current || (u->flags & UF_DONE)) continue;
        a->ulist[a->ulist_n++] = i;
    }
    a->ulist_idx = 0;
    a->ulist_scroll = 0;
}

static void ulist_keep_visible(App *a)
{
    if (a->ulist_idx < a->ulist_scroll)
        a->ulist_scroll = a->ulist_idx;
    else if (a->ulist_idx >= a->ulist_scroll + UNITLIST_VISIBLE)
        a->ulist_scroll = a->ulist_idx - UNITLIST_VISIBLE + 1;
    int maxs = a->ulist_n - UNITLIST_VISIBLE;
    if (maxs < 0) maxs = 0;
    if (a->ulist_scroll > maxs) a->ulist_scroll = maxs;
    if (a->ulist_scroll < 0) a->ulist_scroll = 0;
}

/* 一覧で選んだユニットへカーソルを移動して一覧を閉じる */
static void ulist_confirm(App *a)
{
    Game *g = &a->game;
    if (a->ulist_idx < 0 || a->ulist_idx >= a->ulist_n) return;
    int ui = a->ulist[a->ulist_idx];
    if (!(g->units[ui].flags & UF_ALIVE)) return;
    const Unit *u = &g->units[ui];
    a->cur_x = u->pos.x;
    a->cur_y = u->pos.y;
    center_camera(a, u->pos.x, u->pos.y);
    ensure_cursor_visible(a);
    snd_se(SE_OK);
    a->bs = BS_IDLE;
}

/* ターンメニューの決定（キー・クリック共通） */
static void tmenu_select(App *a)
{
    switch (a->tmenu_idx) {
    case TM_UNITLIST:
        build_unit_list(a);
        a->bs = BS_UNITLIST;
        snd_se(SE_CURSOR);
        break;
    case TM_END:
        a->bs = BS_IDLE;
        do_end_turn(a);
        break;
    case TM_SAVE:
        a->smenu_idx = 0;
        a->bs = BS_SAVE_MENU;
        break;
    case TM_TILT: {
        /* マップ表示の切替。切り替えるとY方向の縮尺が変わるので、画面中央が
         * 同じ場所を向いたままになるようカメラも合わせて伸縮させる
         * （ズーム時と同じ考え方。やらないと視点が飛ぶ）。 */
        float before = hex_tilt_squash(a);
        a->opt_tilt = !a->opt_tilt;
        float ratio = hex_tilt_squash(a) / before;
        a->cam_y = (a->cam_y + WIN_H / 2.0f) * ratio - WIN_H / 2.0f;
        clamp_camera(a);
        options_save(a);        /* 次回起動にも引き継ぐ */
        snd_se(SE_CURSOR);
        break;                  /* メニューは開いたまま＝続けて見比べられる */
    }
    case TM_TITLE:
        a->next_screen = SCREEN_TITLE;
        break;
    default:
        a->bs = BS_IDLE;
        break;
    }
}

/* ------------------------------------------------------------------ */
/* 決定（Z / 左クリック）の状態別処理                                   */
/* ------------------------------------------------------------------ */
/* レイヤー選択ポップアップ */
static const char *layer_tag(Layer L)
{
    return tx(L == LAYER_AIR ? "LAYER_AIR"
            : L == LAYER_UNDER ? "LAYER_UNDER" : "LAYER_SURFACE");
}

static SDL_Rect lpick_rect(App *a, int i)
{
    float cx, cy;
    hex_center_px(a, a->lpick_x, a->lpick_y, &cx, &cy);
    int x = (int)cx + 40;
    int y = (int)cy - 20;
    if (x + 240 > WIN_W) x = (int)cx - 280;
    if (x < 0) x = 4;
    if (y + a->lpick_n * 36 > WIN_H - PANEL_H) y = WIN_H - PANEL_H - a->lpick_n * 36 - 8;
    if (y < TOPBAR_H) y = TOPBAR_H + 4;
    SDL_Rect r = { x, y + i * 36, 240, 32 };
    return r;
}

static void open_layer_pick(App *a, const int *units, int n, int mode)
{
    for (int i = 0; i < n && i < LAYER_COUNT; i++) a->lpick_unit[i] = units[i];
    a->lpick_n = n; a->lpick_idx = 0; a->lpick_mode = mode;
    a->lpick_x = a->cur_x; a->lpick_y = a->cur_y;   /* 開いたセルに固定（追従させない） */
    a->bs = BS_LAYER_PICK;
}

static void select_own_unit(App *a, int ui)
{
    Game *g = &a->game;
    a->sel_unit = ui;
    path_move_range(g, ui, &a->mr);
    a->undo_x = g->units[ui].pos.x;
    a->undo_y = g->units[ui].pos.y;
    a->undo_fuel = g->units[ui].fuel;
    a->undo_flags = g->units[ui].flags;
    a->bs = BS_UNIT_SELECTED;
}

static void lpick_confirm(App *a)
{
    int ui = a->lpick_unit[a->lpick_idx];
    snd_se(SE_OK);
    if (a->lpick_mode == LP_SELECT) select_own_unit(a, ui);
    else                            execute_attack(a, ui);
}

static void confirm_at(App *a, int hx, int hy)
{
    Game *g = &a->game;
    switch (a->bs) {
    case BS_IDLE: {
        /* 立体化: 重なりセルは自軍の行動可能ユニットをレイヤーで列挙して選ぶ */
        int cell[LAYER_COUNT], own[LAYER_COUNT], n = 0;
        game_units_at(g, hx, hy, cell);
        for (int L = 0; L < LAYER_COUNT; L++) {
            int u = cell[L];
            if (u >= 0 && g->units[u].owner == g->current &&
                !(g->units[u].flags & UF_DONE))
                own[n++] = u;
        }
        if (n == 1) {
            select_own_unit(a, own[0]);
        } else if (n >= 2) {
            open_layer_pick(a, own, n, LP_SELECT);
        } else if (game_can_produce_at(g, g->current, hx, hy)) {
            open_production(a, hx, hy);
        }
        break;
    }
    case BS_UNIT_SELECTED:
        try_move_to(a, hx, hy);
        break;
    case BS_TARGET_SELECT: {
        /* 立体化: 同一セルに複数の敵が居れば、どのレイヤーを撃つか選ばせる */
        int hits[LAYER_COUNT], n = 0;
        for (int i = 0; i < a->n_targets; i++) {
            const Unit *t = &g->units[a->targets[i]];
            if ((t->flags & UF_ALIVE) && t->pos.x == hx && t->pos.y == hy)
                if (n < LAYER_COUNT) hits[n++] = a->targets[i];
        }
        if (n == 1)      execute_attack(a, hits[0]);
        else if (n >= 2) open_layer_pick(a, hits, n, LP_ATTACK);
        break;
    }
    case BS_WORK:
        for (int i = 0; i < a->n_work; i++) {
            if (a->work_x[i] != hx || a->work_y[i] != hy) continue;
            int kind = game_work_kind_at(g, a->sel_unit, hx, hy);
            int cost = game_work_cost(g, hx, hy);
            if (kind == WORK_REPAIR && g->funds[g->units[a->sel_unit].owner] < cost) {
                snd_se(SE_CANCEL);
                set_banner(a, tx("WORK_NO_FUNDS"), 90);
                return;
            }
            kind = game_do_work(g, a->sel_unit, hx, hy);
            if (kind == WORK_NONE) { snd_se(SE_CANCEL); return; }
            if (kind == WORK_REPAIR) {
                char buf[48];
                snprintf(buf, sizeof buf, tx("POP_REPAIR_FMT"), cost);
                battle_add_popup(a, hx, hy, buf, COL_YELLOW);
            } else {
                battle_add_popup(a, hx, hy, tx("POP_DEMOLISH"), COL_WHITE);
            }
            snd_se(SE_EXPLOSION);
            a->sel_unit = -1;
            a->bs = BS_IDLE;
            return;
        }
        break;
    case BS_UNLOAD:
        for (int i = 0; i < a->n_unload; i++) {
            if (a->unload_x[i] == hx && a->unload_y[i] == hy) {
                if (game_unload_unit(g, a->sel_unit, hx, hy) == 0) {
                    battle_add_popup(a, hx, hy, tx("POP_UNBOARD"), COL_WHITE);
                    snd_se(SE_OK);
                    a->unload_count++;
                    /* 積荷が残っていて降ろせる隣接があれば続けて降ろす（複数降車） */
                    compute_unload_targets(a);
                    if (a->n_unload > 0) {
                        a->cur_x = a->unload_x[0];
                        a->cur_y = a->unload_y[0];
                        return;                  /* BS_UNLOAD を継続 */
                    }
                    /* もう降ろせない → 輸送ユニットの手番を確定 */
                    g->units[a->sel_unit].flags |= UF_DONE;
                    a->sel_unit = -1;
                    a->bs = BS_IDLE;
                }
                return;
            }
        }
        break;
    default:
        break;
    }
}

/* 進化を実行してポップアップで知らせる */
static void do_evolve(App *a)
{
    Game *g = &a->game;
    int ui = a->sel_unit;
    if (ui < 0 || game_evolve_unit(g, ui) != 0) {
        a->sel_unit = -1;
        a->bs = BS_IDLE;
        return;
    }
    const Unit *u = &g->units[ui];
    battle_add_popup(a, u->pos.x, u->pos.y, tx("POP_EVOLVE"), COL_YELLOW);
    snd_se(SE_CAPTURE);
    a->sel_unit = -1;
    a->bs = BS_IDLE;
}

/* 合流を実行する（確認で「はい」を選んだとき） */
static void do_join(App *a)
{
    Game *g = &a->game;
    int ui = a->sel_unit, tgt = a->join_target;
    if (ui < 0 || tgt < 0 || !game_can_join(g, ui, tgt)) {
        a->sel_unit = -1;
        a->bs = BS_IDLE;
        return;
    }
    int hx = g->units[tgt].pos.x, hy = g->units[tgt].pos.y;
    int refund = game_join_units(g, ui, tgt);
    char msg[48];
    snprintf(msg, sizeof msg, tx("POP_JOIN_FMT"), g->units[tgt].hp);
    battle_add_popup(a, hx, hy, msg, COL_WHITE);
    if (refund > 0) {
        snprintf(msg, sizeof msg, tx("POP_REFUND_FMT"), refund);
        battle_add_popup(a, hx, hy, msg, COL_YELLOW);
    }
    snd_se(SE_OK);
    a->sel_unit = -1;
    a->join_target = -1;
    a->bs = BS_IDLE;
}

static void cancel_action(App *a)
{
    switch (a->bs) {
    case BS_UNIT_SELECTED:
        a->sel_unit = -1;
        a->bs = BS_IDLE;
        break;
    case BS_ACTION_MENU:
        select_action(a, ACT_CANCEL);
        break;
    case BS_WORK:
        build_action_menu(a);        /* 何もしていないのでメニューへ戻すだけ */
        break;
    case BS_UNLOAD:
        /* 既に1体以上降ろしていたら、その時点で輸送ユニットの手番を確定 */
        if (a->unload_count > 0) {
            a->game.units[a->sel_unit].flags |= UF_DONE;
            a->sel_unit = -1;
            a->bs = BS_IDLE;
        } else {
            build_action_menu(a);
        }
        break;
    case BS_TARGET_SELECT:
        build_action_menu(a);
        break;
    case BS_UNITLIST:
        a->tmenu_idx = TM_UNITLIST;
        a->bs = BS_TURN_MENU;       /* 一覧を閉じてメニューへ戻る */
        break;
    case BS_LAYER_PICK:
        /* 自軍選択のキャンセルは待機解除、攻撃対象選択のキャンセルは対象選択へ戻す */
        if (a->lpick_mode == LP_ATTACK) a->bs = BS_TARGET_SELECT;
        else { a->sel_unit = -1; a->bs = BS_IDLE; }
        break;
    case BS_EVOLVE_CONFIRM:
        build_action_menu(a);        /* 行動メニューへ戻す（何も変えない） */
        break;
    case BS_JOIN_CONFIRM: {
        /* 相手と重なったまま止めているので、移動を巻き戻してから選び直させる */
        if (a->sel_unit >= 0) {
            Unit *uu = &a->game.units[a->sel_unit];
            uu->pos.x = (uint8_t)a->undo_x;
            uu->pos.y = (uint8_t)a->undo_y;
            uu->fuel = (uint8_t)a->undo_fuel;
            uu->flags = a->undo_flags;
            game_update_vision(&a->game);
        }
        a->join_target = -1;
        a->bs = BS_UNIT_SELECTED;
        break;
    }
    case BS_PRODUCTION:
    case BS_TURN_MENU:
        a->bs = BS_IDLE;
        break;
    case BS_SAVE_MENU:
        a->bs = BS_TURN_MENU;
        break;
    default:
        break;
    }
}

/* ------------------------------------------------------------------ */
/* 画面API: enter / event / update / draw                              */
/* ------------------------------------------------------------------ */
void battle_enter(App *a)
{
    a->human_out = false;   /* 前の戦闘の結果を引きずらない */
    a->zoom = 1;
    a->sel_unit = -1;
    a->join_target = -1;
    a->dragging = false;
    memset(a->popups, 0, sizeof a->popups);
    a->cur_x = a->cur_y = 0;
    /* 戦闘BGM: オプションで曲を指定できる（-1 ならマップ名から自動で選ぶ） */
    snd_music(snd_battle_music(a->opt_bgm_track, a->game.map_name), true);
    begin_side(a);
    /* 環境変数 HWCAM=x,y で見る位置を指定できる（画面確認用）。
     * begin_side が自軍ユニットへ寄せるので、その後に上書きする。 */
    {
        const char *cam = getenv("HWCAM");
        int hx = 0, hy = 0;
        if (cam && sscanf(cam, "%d,%d", &hx, &hy) == 2 &&
            game_in_bounds(&a->game, hx, hy)) {
            a->cur_x = hx; a->cur_y = hy;
            center_camera(a, hx, hy);
        }
    }
}

static void menu_key_nav(SDL_Keycode k, int *idx, int n)
{
    if (k == SDLK_UP)   *idx = (*idx + n - 1) % n;
    if (k == SDLK_DOWN) *idx = (*idx + 1) % n;
}

void battle_event(App *a, const SDL_Event *e)
{
    Game *g = &a->game;
    bool human_turn = g->ctrl[g->current] == CTRL_HUMAN &&
                      a->bs != BS_GAMEOVER && a->bs != BS_CPU_TURN &&
                      a->bs != BS_HANDOVER;

    /* --- ホットシート交代画面 --- */
    if (a->bs == BS_HANDOVER) {
        if ((e->type == SDL_KEYDOWN &&
             (e->key.keysym.sym == SDLK_z || e->key.keysym.sym == SDLK_RETURN)) ||
            e->type == SDL_MOUSEBUTTONDOWN) {
            a->bs = BS_IDLE;
            focus_own_unit(a);
        }
        return;
    }
    /* --- 未行動一覧表示中はホイールでリストをスクロール --- */
    if (e->type == SDL_MOUSEWHEEL && a->bs == BS_UNITLIST) {
        int maxs = a->ulist_n - UNITLIST_VISIBLE;
        if (maxs < 0) maxs = 0;
        a->ulist_scroll += (e->wheel.y > 0 ? -1 : 1);
        if (a->ulist_scroll < 0) a->ulist_scroll = 0;
        if (a->ulist_scroll > maxs) a->ulist_scroll = maxs;
        return;
    }
    /* --- 生産メニュー表示中はホイールでリストをスクロール --- */
    if (e->type == SDL_MOUSEWHEEL && a->bs == BS_PRODUCTION) {
        int maxscroll = a->prod_n - PROD_VISIBLE;
        if (maxscroll < 0) maxscroll = 0;
        a->prod_scroll += (e->wheel.y > 0 ? -1 : 1);
        if (a->prod_scroll < 0) a->prod_scroll = 0;
        if (a->prod_scroll > maxscroll) a->prod_scroll = maxscroll;
        return;
    }
    /* --- ズーム（CPU手番中も許可） --- */
    if (e->type == SDL_MOUSEWHEEL) {
        int nz = a->zoom + (e->wheel.y > 0 ? 1 : -1);
        if (nz < 0) nz = 0;
        if (nz > 2) nz = 2;
        if (nz != a->zoom) {
            float ratio_old = hex_size(a);
            a->zoom = nz;
            float ratio = hex_size(a) / ratio_old;
            float wx = a->cam_x + WIN_W / 2.0f;
            float wy = a->cam_y + WIN_H / 2.0f;
            a->cam_x = wx * ratio - WIN_W / 2.0f;
            a->cam_y = wy * ratio - WIN_H / 2.0f;
            clamp_camera(a);
        }
        return;
    }
    if (a->bs == BS_BATTLE_ANIM) {
        /* Z/クリックでスキップ */
        if ((e->type == SDL_KEYDOWN &&
             (e->key.keysym.sym == SDLK_z || e->key.keysym.sym == SDLK_RETURN ||
              e->key.keysym.sym == SDLK_x)) ||
            e->type == SDL_MOUSEBUTTONDOWN)
            a->anim.timer = 0;
        return;
    }
    if (a->bs == BS_GAMEOVER || a->bs == BS_CPU_TURN)
        return;
    if (e->type == SDL_MOUSEBUTTONDOWN && e->button.button == SDL_BUTTON_MIDDLE) {
        a->dragging = true;
        a->drag_sx = e->button.x; a->drag_sy = e->button.y;
        a->drag_cx = a->cam_x; a->drag_cy = a->cam_y;
        return;
    }
    if (e->type == SDL_MOUSEBUTTONUP && e->button.button == SDL_BUTTON_MIDDLE) {
        a->dragging = false;
        return;
    }
    if (e->type == SDL_MOUSEMOTION) {
        if (a->dragging) {
            a->cam_x = a->drag_cx - (e->motion.x - a->drag_sx);
            a->cam_y = a->drag_cy - (e->motion.y - a->drag_sy);
            clamp_camera(a);
        } else if (human_turn) {
            /* 地図カーソルは「地図操作中」の状態でのみマウス追従。
             * メニュー/ポップアップ表示中は動かさない（ポップアップが逃げるのを防ぐ）。 */
            bool map_cursor = (a->bs == BS_IDLE || a->bs == BS_UNIT_SELECTED ||
                               a->bs == BS_TARGET_SELECT || a->bs == BS_UNLOAD ||
                               a->bs == BS_WORK);
            int hx, hy;
            if (map_cursor && px_to_hex(a, e->motion.x, e->motion.y, &hx, &hy)) {
                a->cur_x = hx; a->cur_y = hy;
            }
            /* メニューのホバー */
            SDL_Point p = { e->motion.x, e->motion.y };
            if (a->bs == BS_EVOLVE_CONFIRM || a->bs == BS_JOIN_CONFIRM) {
                for (int i = 0; i < 2; i++) {
                    SDL_Rect r = evo_rect(i);
                    if (SDL_PointInRect(&p, &r)) a->amenu_idx = i;
                }
            }
            if (a->bs == BS_ACTION_MENU) {
                for (int i = 0; i < a->amenu_n; i++) {
                    SDL_Rect r = amenu_rect(a, i);
                    if (SDL_PointInRect(&p, &r)) a->amenu_idx = i;
                }
            } else if (a->bs == BS_PRODUCTION) {
                int end = a->prod_scroll + PROD_VISIBLE;
                if (end > a->prod_n) end = a->prod_n;
                for (int i = a->prod_scroll; i < end; i++) {
                    SDL_Rect r = prod_rect(a, i);
                    if (SDL_PointInRect(&p, &r)) a->prod_idx = i;
                }
            } else if (a->bs == BS_TURN_MENU) {
                for (int i = 0; i < TMENU_ITEMS; i++) {
                    SDL_Rect r = tmenu_rect(i);
                    if (SDL_PointInRect(&p, &r)) a->tmenu_idx = i;
                }
            } else if (a->bs == BS_SAVE_MENU) {
                for (int i = 0; i < 10; i++) {
                    SDL_Rect r = smenu_rect(i);
                    if (SDL_PointInRect(&p, &r)) a->smenu_idx = i;
                }
            } else if (a->bs == BS_LAYER_PICK) {
                for (int i = 0; i < a->lpick_n; i++) {
                    SDL_Rect r = lpick_rect(a, i);
                    if (SDL_PointInRect(&p, &r)) a->lpick_idx = i;
                }
            } else if (a->bs == BS_UNITLIST) {
                int end = a->ulist_scroll + UNITLIST_VISIBLE;
                if (end > a->ulist_n) end = a->ulist_n;
                for (int i = a->ulist_scroll; i < end; i++) {
                    SDL_Rect r = ulist_rect(a, i);
                    if (SDL_PointInRect(&p, &r)) a->ulist_idx = i;
                }
            }
        }
        return;
    }

    if (!human_turn) return;

    if (e->type == SDL_MOUSEBUTTONDOWN && e->button.button == SDL_BUTTON_LEFT) {
        SDL_Point p = { e->button.x, e->button.y };
        if (a->bs == BS_ACTION_MENU) {
            for (int i = 0; i < a->amenu_n; i++) {
                SDL_Rect r = amenu_rect(a, i);
                if (SDL_PointInRect(&p, &r)) {
                    select_action(a, a->amenu_items[i]);
                    return;
                }
            }
            return;
        }
        if (a->bs == BS_PRODUCTION) {
            int end = a->prod_scroll + PROD_VISIBLE;
            if (end > a->prod_n) end = a->prod_n;
            for (int i = a->prod_scroll; i < end; i++) {
                SDL_Rect r = prod_rect(a, i);
                if (SDL_PointInRect(&p, &r)) {
                    a->prod_idx = i;
                    prod_select(a);
                    return;
                }
            }
            return;
        }
        if (a->bs == BS_TURN_MENU) {
            for (int i = 0; i < TMENU_ITEMS; i++) {
                SDL_Rect r = tmenu_rect(i);
                if (SDL_PointInRect(&p, &r)) {
                    a->tmenu_idx = i;
                    tmenu_select(a);
                    return;
                }
            }
            return;
        }
        if (a->bs == BS_SAVE_MENU) {
            for (int i = 0; i < 10; i++) {
                SDL_Rect r = smenu_rect(i);
                if (SDL_PointInRect(&p, &r)) {
                    a->smenu_idx = i;
                    do_save_slot(a, i + 1);
                    return;
                }
            }
            return;
        }
        if (a->bs == BS_UNITLIST) {
            int end = a->ulist_scroll + UNITLIST_VISIBLE;
            if (end > a->ulist_n) end = a->ulist_n;
            for (int i = a->ulist_scroll; i < end; i++) {
                SDL_Rect r = ulist_rect(a, i);
                if (SDL_PointInRect(&p, &r)) { a->ulist_idx = i; ulist_confirm(a); return; }
            }
            cancel_action(a);   /* 一覧の外をクリックしたら閉じる */
            return;
        }
        if (a->bs == BS_EVOLVE_CONFIRM || a->bs == BS_JOIN_CONFIRM) {
            bool join = (a->bs == BS_JOIN_CONFIRM);
            for (int i = 0; i < 2; i++) {
                SDL_Rect r = evo_rect(i);
                if (SDL_PointInRect(&p, &r)) {
                    a->amenu_idx = i;
                    if (i != 0) cancel_action(a);
                    else if (join) do_join(a);
                    else do_evolve(a);
                    return;
                }
            }
            cancel_action(a);   /* 外をクリックしたら閉じる（詰まらせない） */
            return;
        }
        if (a->bs == BS_LAYER_PICK) {
            for (int i = 0; i < a->lpick_n; i++) {
                SDL_Rect r = lpick_rect(a, i);
                if (SDL_PointInRect(&p, &r)) { a->lpick_idx = i; lpick_confirm(a); return; }
            }
            /* ポップアップの外をクリックしたら閉じる（操作不能に陥らないように） */
            cancel_action(a);
            return;
        }
        /* メニューボタン（右上） */
        SDL_Rect mb = { WIN_W - 130, 4, 126, 28 };
        if (SDL_PointInRect(&p, &mb) && a->bs == BS_IDLE) {
            a->tmenu_idx = 0;
            a->bs = BS_TURN_MENU;
            return;
        }
        /* 指揮官ゲージ（右上）クリックで必殺技 */
        {
            SDL_Rect cg = co_gauge_rect();
            if (SDL_PointInRect(&p, &cg) && a->bs == BS_IDLE) {
                try_co_power(a);
                return;
            }
        }
        int hx, hy;
        if (px_to_hex(a, e->button.x, e->button.y, &hx, &hy)) {
            a->cur_x = hx; a->cur_y = hy;
            confirm_at(a, hx, hy);
        }
        return;
    }
    if (e->type == SDL_MOUSEBUTTONDOWN && e->button.button == SDL_BUTTON_RIGHT) {
        cancel_action(a);
        return;
    }

    /* --- キーボード --- */
    if (e->type == SDL_KEYDOWN) {
        SDL_Keycode k = e->key.keysym.sym;

        /* E: ターンメニュー。ユニット選択中やレイヤー選択中でも開けるようにする
         * （まだ移動を確定していない状態なので、選択を捨てて開いて良い）。
         * これらの状態でEが無反応だと「メニューが出せない」ように見えるため。 */
        if (k == SDLK_e && (a->bs == BS_IDLE || a->bs == BS_UNIT_SELECTED ||
                            (a->bs == BS_LAYER_PICK && a->lpick_mode == LP_SELECT))) {
            a->sel_unit = -1;
            a->tmenu_idx = 0;
            a->bs = BS_TURN_MENU;
            return;
        }
        if (a->bs == BS_JOIN_CONFIRM) {
            if (k == SDLK_LEFT)  a->amenu_idx = 0;
            if (k == SDLK_RIGHT) a->amenu_idx = 1;
            if (k == SDLK_z || k == SDLK_RETURN) {
                if (a->amenu_idx == 0) do_join(a);
                else cancel_action(a);
            }
            if (k == SDLK_x || k == SDLK_ESCAPE) cancel_action(a);
            return;
        }
        if (a->bs == BS_EVOLVE_CONFIRM) {
            if (k == SDLK_LEFT)  a->amenu_idx = 0;
            if (k == SDLK_RIGHT) a->amenu_idx = 1;
            if (k == SDLK_z || k == SDLK_RETURN) {
                if (a->amenu_idx == 0) do_evolve(a);
                else cancel_action(a);
            }
            if (k == SDLK_x || k == SDLK_ESCAPE) cancel_action(a);
            return;
        }
        if (a->bs == BS_UNITLIST) {
            if (a->ulist_n > 0) {
                menu_key_nav(k, &a->ulist_idx, a->ulist_n);
                ulist_keep_visible(a);
                if (k == SDLK_z || k == SDLK_RETURN) ulist_confirm(a);
            } else if (k == SDLK_z || k == SDLK_RETURN) {
                a->bs = BS_IDLE;
            }
            if (k == SDLK_x || k == SDLK_ESCAPE) cancel_action(a);
            return;
        }
        if (a->bs == BS_LAYER_PICK) {
            menu_key_nav(k, &a->lpick_idx, a->lpick_n);
            if (k == SDLK_z || k == SDLK_RETURN) lpick_confirm(a);
            if (k == SDLK_x || k == SDLK_ESCAPE) cancel_action(a);
            return;
        }
        if (a->bs == BS_ACTION_MENU) {
            menu_key_nav(k, &a->amenu_idx, a->amenu_n);
            if (k == SDLK_z || k == SDLK_RETURN)
                select_action(a, a->amenu_items[a->amenu_idx]);
            if (k == SDLK_x || k == SDLK_ESCAPE) cancel_action(a);
            return;
        }
        if (a->bs == BS_PRODUCTION) {
            menu_key_nav(k, &a->prod_idx, a->prod_n);
            prod_keep_visible(a);
            if (k == SDLK_z || k == SDLK_RETURN) prod_select(a);
            if (k == SDLK_x || k == SDLK_ESCAPE) cancel_action(a);
            return;
        }
        if (a->bs == BS_TURN_MENU) {
            menu_key_nav(k, &a->tmenu_idx, TMENU_ITEMS);
            if (k == SDLK_z || k == SDLK_RETURN) tmenu_select(a);
            if (k == SDLK_x || k == SDLK_ESCAPE) cancel_action(a);
            return;
        }
        if (a->bs == BS_SAVE_MENU) {
            menu_key_nav(k, &a->smenu_idx, 10);
            if (k == SDLK_z || k == SDLK_RETURN)
                do_save_slot(a, a->smenu_idx + 1);
            if (k == SDLK_x || k == SDLK_ESCAPE) cancel_action(a);
            return;
        }
        if (a->bs == BS_TARGET_SELECT &&
            (k == SDLK_LEFT || k == SDLK_RIGHT || k == SDLK_UP || k == SDLK_DOWN)) {
            /* 対象巡回 */
            int dir = (k == SDLK_LEFT || k == SDLK_UP) ? -1 : 1;
            a->target_idx = (a->target_idx + a->n_targets + dir) % a->n_targets;
            const Unit *t = &a->game.units[a->targets[a->target_idx]];
            a->cur_x = t->pos.x; a->cur_y = t->pos.y;
            ensure_cursor_visible(a);
            return;
        }

        if (a->bs == BS_WORK && a->n_work > 0 &&
            (k == SDLK_LEFT || k == SDLK_RIGHT || k == SDLK_UP || k == SDLK_DOWN)) {
            /* 工作先巡回。候補は多くても6つなので、
             * 自由カーソルで狙うより順に送る方が早い。 */
            int dir = (k == SDLK_LEFT || k == SDLK_UP) ? -1 : 1;
            int cur = 0;
            for (int i = 0; i < a->n_work; i++)
                if (a->work_x[i] == a->cur_x && a->work_y[i] == a->cur_y) cur = i;
            cur = (cur + a->n_work + dir) % a->n_work;
            a->cur_x = a->work_x[cur];
            a->cur_y = a->work_y[cur];
            ensure_cursor_visible(a);
            snd_se(SE_CURSOR);
            return;
        }
        if (k == SDLK_LEFT || k == SDLK_RIGHT || k == SDLK_UP || k == SDLK_DOWN) {
            int nx = a->cur_x + (k == SDLK_RIGHT) - (k == SDLK_LEFT);
            int ny = a->cur_y + (k == SDLK_DOWN) - (k == SDLK_UP);
            if (game_in_bounds(g, nx, ny)) {
                a->cur_x = nx; a->cur_y = ny;
                ensure_cursor_visible(a);
                snd_se(SE_CURSOR);
            }
            return;
        }
        if (k == SDLK_z || k == SDLK_RETURN) {
            confirm_at(a, a->cur_x, a->cur_y);
            return;
        }
        if (k == SDLK_x || k == SDLK_ESCAPE) { cancel_action(a); return; }
        if (k == SDLK_n && a->bs == BS_IDLE) { next_unit(a); return; }
        if (k == SDLK_p && a->bs == BS_IDLE) { try_co_power(a); return; }
        if (k == SDLK_e && a->bs == BS_IDLE) {
            a->tmenu_idx = 0;
            a->bs = BS_TURN_MENU;
            return;
        }
    }
}

/* 画面端スクロール: マウスをウィンドウ端に寄せると地図がスクロール。
 * 反応幅は狭め(端16px)にして、少し内側に戻せばすぐ止まるようにする。
 * ドラッグ中とメニュー表示中は無効（メニュー操作の邪魔をしない）。 */
static void edge_scroll(App *a)
{
    if (a->dragging) return;
    if (SDL_GetMouseFocus() != a->win) return;
    int mx, my;
    SDL_GetMouseState(&mx, &my);
    const int MARGIN = 16;
    const float SPEED = 10.0f;
    float dx = 0, dy = 0;
    if (mx < MARGIN)          dx = -SPEED;
    if (mx > WIN_W - MARGIN)  dx = SPEED;
    if (my < MARGIN)          dy = -SPEED;
    if (my > WIN_H - MARGIN)  dy = SPEED;
    if (dx != 0 || dy != 0) {
        a->cam_x += dx;
        a->cam_y += dy;
        clamp_camera(a);
    }
}

void battle_update(App *a)
{
    Game *g = &a->game;

    /* ポップアップ */
    for (int i = 0; i < MAX_POPUPS; i++)
        if (a->popups[i].timer > 0) a->popups[i].timer--;
    if (a->banner_timer > 0) a->banner_timer--;
    if (a->co_cutin_timer > 0) a->co_cutin_timer--;

    /* カーソルが別のセルへ移ったら、情報パネルの巡回を先頭から始める */
    if (a->cur_x != a->panel_cx || a->cur_y != a->panel_cy) {
        a->panel_cx = a->cur_x;
        a->panel_cy = a->cur_y;
        a->panel_base_frame = a->frame;
    }

    /* マウスを画面端に寄せて地図スクロール（地図操作中の状態のみ。
     * メニュー/生産/ポップアップ表示中は無効） */
    if (a->bs == BS_IDLE || a->bs == BS_UNIT_SELECTED ||
        a->bs == BS_TARGET_SELECT || a->bs == BS_UNLOAD ||
        a->bs == BS_WORK)
        edge_scroll(a);

    if (a->bs == BS_BATTLE_ANIM) {
        BattleAnim *an = &a->anim;
        int t = an->total - an->timer;
        if (t >= 5 && !an->shot_played)   { an->shot_played = true;  snd_se(SE_SHOT); }
        if (t >= 30 && !an->hit_played)   { an->hit_played = true;   snd_se(SE_EXPLOSION); }
        if (an->counter > 0 || an->atk_killed) {
            if (t >= 75 && !an->cshot_played) { an->cshot_played = true; snd_se(SE_SHOT); }
            if (t >= 100 && !an->chit_played) { an->chit_played = true;  snd_se(SE_EXPLOSION); }
        }
        if (an->timer > 0) an->timer--;
        else finish_attack_effects(a);
        return;
    }

    if (a->bs == BS_GAMEOVER) {
        if (a->banner_timer == 0) {
            /* 勝利時はご褒美画面を挟む（キャンペーン=作戦別画像 / フリー=汎用画像） */
            if (reward_available(a))
                a->next_screen = SCREEN_REWARD;
            else
                a->next_screen = SCREEN_RESULT;
        }
        return;
    }

    if (a->bs == BS_CPU_TURN) {
        if (a->cpu_wait > 0) { a->cpu_wait--; return; }
        int cont = ai_step(g, &a->ai);
        show_ai_co_power(a);
        /* CPUの行動で自軍が全滅することがあるのでここも無条件に */
        check_over(a);
        if (a->bs == BS_GAMEOVER) return;
        if (a->ai.last_unit >= 0 &&
            (g->units[a->ai.last_unit].flags & UF_ALIVE)) {
            const Unit *u = &g->units[a->ai.last_unit];
            a->cur_x = u->pos.x; a->cur_y = u->pos.y;
            center_camera(a, u->pos.x, u->pos.y);
        }
        if (!cont) {
            begin_side(a);
        } else {
            a->cpu_wait = 14;
        }
        return;
    }

    /* 人間手番: 全ユニット行動済みでもターン終了は手動（Eキー） */
}

/* ------------------------------------------------------------------ */
/* 描画                                                                */
/* ------------------------------------------------------------------ */
static void draw_overlays(App *a)
{
    Game *g = &a->game;
    float s = hex_size(a);

    if (a->bs == BS_UNIT_SELECTED) {
        const Unit *su = &g->units[a->sel_unit];
        const UnitType *st = &g->types[su->type];
        /* 間接攻撃ユニット（射程2以上）は、どこまで撃てるかを先に見せる。
         * 大半は move_and_fire=0（移動したら撃てない）なので、いま居る場所からの
         * 射程を赤で示せばそのまま「今ターンの射界」になる。
         * 高射砲のように移動後も撃てる機種だけは「ここからなら」の目安という位置づけ。
         * 移動範囲（白）より先に塗って、白が上に来るようにする。 */
        if (st->range_min >= 2) {
            for (int y = 0; y < g->h; y++)
                for (int x = 0; x < g->w; x++) {
                    int d = hex_distance(su->pos.x, su->pos.y, x, y);
                    if (d < st->range_min || d > game_range_max(g, st)) continue;
                    if (g->terrains[g->tiles[y][x].terrain].chr == 'x') continue;
                    float cx, cy;
                    hex_center_px(a, x, y, &cx, &cy);
                    render_fill_hex_map(a, cx, cy, s - 1.5f,
                                        (SDL_Color){ 220, 70, 50, 58 });
                }
        }
        for (int y = 0; y < g->h; y++)
            for (int x = 0; x < g->w; x++) {
                if (a->mr.cost[y][x] < 0 || !a->mr.stop[y][x]) continue;
                float cx, cy;
                hex_center_px(a, x, y, &cx, &cy);
                render_fill_hex_map(a, cx, cy, s - 1.5f,
                                (SDL_Color){ 255, 255, 255, 70 });
            }
    }
    if (a->bs == BS_TARGET_SELECT) {
        for (int i = 0; i < a->n_targets; i++) {
            const Unit *t = &g->units[a->targets[i]];
            if (!(t->flags & UF_ALIVE)) continue;
            float cx, cy;
            hex_center_px(a, t->pos.x, t->pos.y, &cx, &cy);
            render_fill_hex_map(a, cx, cy, s - 1.5f,
                            (SDL_Color){ 255, 60, 40, 90 });
            render_hex_outline_map(a, cx, cy, s - 1.0f,
                               (SDL_Color){ 255, 80, 60, 255 });
            /* 各対象に予想ダメージを小さく出して比較しやすくする */
            if (a->sel_unit >= 0) {
                int dmg = 0;
                battle_forecast(g, a->sel_unit, a->targets[i], &dmg, NULL, NULL, NULL);
                char nb[16];
                snprintf(nb, sizeof nb, "-%d", dmg);
                draw_text_center(a, a->font_s, (int)cx, (int)(cy + s * 0.18f),
                                 COL_WHITE, nb);
            }
        }
    }
    if (a->bs == BS_WORK) {
        for (int i = 0; i < a->n_work; i++) {
            float cx, cy;
            hex_center_px(a, a->work_x[i], a->work_y[i], &cx, &cy);
            /* 壊すのか直すのかを色で分ける。押してから違ったとは言わせない。 */
            bool rep = (game_work_kind_at(g, a->sel_unit,
                                          a->work_x[i], a->work_y[i]) == WORK_REPAIR);
            SDL_Color fill = rep ? (SDL_Color){  80, 200, 120, 90 }
                                 : (SDL_Color){ 210,  90,  70, 90 };
            SDL_Color line = rep ? (SDL_Color){ 100, 230, 140, 255 }
                                 : (SDL_Color){ 240, 120,  90, 255 };
            render_fill_hex_map(a, cx, cy, s - 1.5f, fill);
            render_hex_outline_map(a, cx, cy, s - 1.0f, line);
        }
    }
    if (a->bs == BS_UNLOAD) {
        for (int i = 0; i < a->n_unload; i++) {
            float cx, cy;
            hex_center_px(a, a->unload_x[i], a->unload_y[i], &cx, &cy);
            render_fill_hex_map(a, cx, cy, s - 1.5f,
                            (SDL_Color){ 80, 200, 120, 90 });
            render_hex_outline_map(a, cx, cy, s - 1.0f,
                               (SDL_Color){ 100, 230, 140, 255 });
        }
    }
    /* 選択ユニット */
    if (a->sel_unit >= 0 && (g->units[a->sel_unit].flags & UF_ALIVE)) {
        const Unit *u = &g->units[a->sel_unit];
        float cx, cy;
        hex_center_px(a, u->pos.x, u->pos.y, &cx, &cy);
        render_hex_outline_map(a, cx, cy, s - 1.0f, COL_WHITE);
    }
    /* カーソル */
    {
        float cx, cy;
        hex_center_px(a, a->cur_x, a->cur_y, &cx, &cy);
        float pulse = 1.0f + 0.06f * sinf((float)a->frame * 0.15f);
        render_hex_outline_map(a, cx, cy, (s - 1.0f) * pulse, COL_YELLOW);
        render_hex_outline_map(a, cx, cy, (s - 2.5f) * pulse, COL_YELLOW);
    }
}

static void draw_popups(App *a)
{
    for (int i = 0; i < MAX_POPUPS; i++) {
        Popup *p = &a->popups[i];
        if (p->timer <= 0) continue;
        float cx, cy;
        hex_center_px(a, (int)p->x, (int)p->y, &cx, &cy);
        float rise = (70 - p->timer) * 0.5f;
        draw_text_center(a, a->font_m, (int)cx, (int)(cy - 20 - rise),
                         p->color, p->text);
    }
}

static void draw_topbar(App *a)
{
    Game *g = &a->game;
    fill_rect(a, 0, 0, WIN_W, TOPBAR_H, (SDL_Color){ 28, 32, 38, 235 });

    char buf[160];
    if (g->turn_limit > 0)
        snprintf(buf, sizeof buf, tx("TOP_TURNLIM_FMT"), g->map_name, g->turn, g->turn_limit);
    else
        snprintf(buf, sizeof buf, tx("TOP_TURN_FMT"), g->map_name, g->turn);
    draw_text(a, a->font_m, 10, 6, COL_WHITE, buf);

    snprintf(buf, sizeof buf, tx("TOP_FACTION_FMT"), faction_name(g->current));
    draw_text(a, a->font_m, 360, 6, COL_P[g->current], buf);

    snprintf(buf, sizeof buf, tx("TOP_FUNDS_FMT"), g->funds[g->current]);
    draw_text(a, a->font_m, 500, 6, COL_YELLOW, buf);

    /* 昼夜。周期が固定なので残りターン数を出すと作戦が立てられる */
    if (g->night_on) {
        bool nite = game_is_night(g);
        snprintf(buf, sizeof buf, tx("TOP_PHASE_FMT"),
                 tx(nite ? "PHASE_NIGHT" : "PHASE_DAY"), game_phase_left(g));
        draw_text(a, a->font_s, 862, 9,
                  nite ? (SDL_Color){ 150, 170, 235, 255 }
                       : (SDL_Color){ 250, 220, 130, 255 }, buf);
    }

    /* 天候と予報（悪天候は色を変えて気づけるように） */
    if (g->weather_on) {
        static const char *WXK[WX_COUNT] = { "WX_CLEAR", "WX_CLOUDY", "WX_RAIN" };
        Weather w = game_weather(g);
        Weather nx = (Weather)(g->weather_next < WX_COUNT ? g->weather_next : WX_CLEAR);
        SDL_Color wc = (w == WX_RAIN)   ? (SDL_Color){ 120, 180, 255, 255 }
                     : (w == WX_CLOUDY) ? (SDL_Color){ 200, 200, 205, 255 }
                                        : (SDL_Color){ 250, 220, 120, 255 };
        /* 「次」は「次のターン」ではなく「今の天候が終わったあと」。
         * 残りターン数を出さないと「次は晴なのに雨が続く」と誤読される。
         * weather_left は「この先何回 continue するか」なので、今ターンを含めて +1。 */
        int wleft = g->weather_left + 1;
        snprintf(buf, sizeof buf, tx("TOP_WEATHER_FMT"),
                 tx(WXK[w]), wleft, tx(WXK[nx]));
        render_weather_icon(a, 620, 9, (int)w);
        draw_text(a, a->font_s, 644, 9, wc, buf);
    }

    /* 拠点確保条件（仕様書 5.10） */
    if (g->objective_count > 0) {
        snprintf(buf, sizeof buf, tx("TOP_OBJ_FMT"),
                 faction_name(g->objective_player),
                 game_count_buildings(g, g->objective_player),
                 g->objective_count);
        /* 天候・昼夜と並ぶのでさらに右へ */
        draw_text(a, a->font_s, 980, 9, COL_P[g->objective_player], buf);
    }

    /* 指揮官ゲージ（手番プレイヤーのもの）。満タンなら光らせて P で発動できる */
    {
        const CommanderType *co = game_co(g, g->current);
        if (co) {
            SDL_Rect r = co_gauge_rect();
            bool ready = game_co_power_ready(g, g->current);
            int cost = co->power_cost > 0 ? co->power_cost : 100;
            int cur = g->co_gauge[g->current];
            if (cur > cost) cur = cost;

            fill_rect(a, r.x, r.y, r.w, r.h, (SDL_Color){ 40, 46, 56, 255 });
            /* 溜まり具合 */
            int fw = r.w * cur / cost;
            SDL_Color fc = ready ? (SDL_Color){ 250, 210, 70, 255 }
                                 : (SDL_Color){ 90, 140, 210, 255 };
            if (fw > 0) fill_rect(a, r.x, r.y, fw, r.h, fc);
            outline_rect(a, r.x, r.y, r.w, r.h, ready ? COL_YELLOW : COL_DIM);

            char cb[96];
            if (ready)
                snprintf(cb, sizeof cb, "%s  %s", co->name, tx("CO_READY"));
            else
                snprintf(cb, sizeof cb, "%s  %d%%", co->name, cur * 100 / cost);
            draw_text(a, a->font_s, r.x + 8, r.y + 5,
                      ready ? COL_BLACK : COL_WHITE, cb);
        }
    }

    /* メニューボタン */
    fill_rect(a, WIN_W - 130, 4, 126, 28, (SDL_Color){ 55, 65, 80, 255 });
    outline_rect(a, WIN_W - 130, 4, 126, 28, COL_DIM);
    draw_text_center(a, a->font_s, WIN_W - 67, 9, COL_WHITE, tx("BTN_MENU"));
}

/* 搭載中ユニットの状態（HP・燃料・弾薬）を右パネルの上に一覧表示する。
 * 空母の艦載機修理や輸送中の回復具合を確認するためのもの。
 * 敵の積荷を覗けないよう、自軍の輸送ユニットのみ詳細を出す。 */
static void draw_cargo_detail(App *a, int ui, int py)
{
    Game *g = &a->game;
    const Unit *u = &g->units[ui];
    int n = 0;
    for (int s = 0; s < MAX_CARGO; s++)
        if (u->cargo[s] >= 0) n++;
    if (n == 0) return;

    int bw = 420, bh = 28 + n * 22;
    int bx = WIN_W - 420, by = py - bh - 6;
    fill_rect(a, bx, by, bw, bh, (SDL_Color){ 24, 28, 34, 242 });
    outline_rect(a, bx, by, bw, bh, COL_DIM);
    draw_text(a, a->font_s, bx + 12, by + 5, COL_YELLOW, tx("CARGO_TITLE"));

    int row = 0;
    for (int s = 0; s < MAX_CARGO; s++) {
        if (u->cargo[s] < 0) continue;
        const Unit *c = &g->units[u->cargo[s]];
        const UnitType *ct = &g->types[c->type];
        char buf[192];
        /* 補給ユニットは ammo が「補給物資」なので表記を分ける */
        snprintf(buf, sizeof buf,
                 ct->supply ? tx("CARGO_ROW_SUP_FMT") : tx("CARGO_ROW_FMT"),
                 ct->name, c->hp, c->fuel, ct->fuel, c->ammo, ct->ammo);
        /* 満タンでなければ黄色 = 回復途中がひと目で分かる */
        bool full = c->hp >= 10 && c->fuel >= ct->fuel && c->ammo >= ct->ammo;
        draw_text(a, a->font_s, bx + 12, by + 26 + row * 22,
                  full ? COL_WHITE : COL_YELLOW, buf);
        row++;
    }
}

static void draw_panels(App *a)
{
    Game *g = &a->game;
    int py = WIN_H - PANEL_H;

    /* 左: 地形情報 */
    fill_rect(a, 0, py, 300, PANEL_H, (SDL_Color){ 28, 32, 38, 235 });
    outline_rect(a, 0, py, 300, PANEL_H, COL_DIM);
    const Tile *tile = &g->tiles[a->cur_y][a->cur_x];
    const TerrainType *t = &g->terrains[tile->terrain];
    char buf[160];
    draw_text(a, a->font_l, 14, py + 8, COL_WHITE, t->name);
    snprintf(buf, sizeof buf, tx("PANEL_DEF_FMT"), t->def_bonus);
    draw_text(a, a->font_s, 14, py + 48, COL_GRAY, buf);
    if (t->income > 0) {
        snprintf(buf, sizeof buf, tx("PANEL_INCOME_FMT"),
                 t->income * g->income_scale / 100);
        draw_text(a, a->font_s, 14, py + 68, COL_GRAY, buf);
    }
    /* CPU手番中も人間側の視点で表示する。地形・ユニットの両方で使う。 */
    int viewer = g->current;
    if (g->ctrl[viewer] != CTRL_HUMAN)
        for (int p = 0; p < MAX_PLAYERS; p++)
            if (g->ctrl[p] == CTRL_HUMAN && game_player_in_play(g, p)) {
                viewer = p; break;
            }

    if (t->capturable) {
        char ownbuf[64];
        const char *own = ownbuf;
        if (tile->owner < 0) {
            snprintf(ownbuf, sizeof ownbuf, "%s", tx("NEUTRAL"));
        } else {
            snprintf(ownbuf, sizeof ownbuf, "%s%s", faction_name(tile->owner),
                     rel_tag(g, viewer, tile->owner));
        }
        snprintf(buf, sizeof buf, tx("PANEL_OWNER_FMT"), own);
        draw_text(a, a->font_s, 14, py + 88,
                  tile->owner < 0 ? COL_GRAY : COL_P[tile->owner], buf);
    }

    /* 右: ユニット情報 */
    int ux = WIN_W - 420;
    fill_rect(a, ux, py, 420, PANEL_H, (SDL_Color){ 28, 32, 38, 235 });
    outline_rect(a, ux, py, 420, PANEL_H, COL_DIM);

    /* 立体化: 同じセルに複数いる場合は、一定間隔で表示を切り替える
     * （空/海面/海中が重なっていても全員の状態を確認できるように） */
    int stack[LAYER_COUNT], n_stack = 0;
    {
        int cell[LAYER_COUNT];
        game_units_at(g, a->cur_x, a->cur_y, cell);
        for (int L = 0; L < LAYER_COUNT; L++)
            if (cell[L] >= 0 && game_unit_visible_to(g, viewer, &g->units[cell[L]]))
                stack[n_stack++] = cell[L];
    }
    int scyc = 0;
    if (n_stack > 1) {
        uint32_t el = a->frame - a->panel_base_frame;
        scyc = (int)((el / PANEL_CYCLE_FRAMES) % (uint32_t)n_stack);
    }
    int ui = (n_stack > 0) ? stack[scyc] : -1;
    a->panel_unit = ui;

    if (ui >= 0) {
        const Unit *u = &g->units[ui];
        const UnitType *ut = &g->types[u->type];
        /* 重なっている時は高度と「何体中の何番目か」を出す */
        if (n_stack > 1) {
            snprintf(buf, sizeof buf, "[%s] %s",
                     layer_tag(unit_layer(ut->mclass)), ut->name);
            draw_text(a, a->font_l, ux + 14, py + 8, COL_P[u->owner], buf);
            char cb[32];
            snprintf(cb, sizeof cb, "%d/%d", scyc + 1, n_stack);
            draw_text(a, a->font_s, ux + 420 - 52, py + 14, COL_YELLOW, cb);
        } else {
            snprintf(buf, sizeof buf, "%s%s", ut->name,
                     rel_tag(g, viewer, u->owner));
            draw_text(a, a->font_l, ux + 14, py + 8, COL_P[u->owner], buf);
        }
        if (ut->supply)
            /* 補給車: ammo は「補給物資」なので専用表示 */
            snprintf(buf, sizeof buf, tx("PANEL_HP_SUP_FMT"),
                     u->hp, u->fuel, ut->fuel, u->ammo, ut->ammo);
        else
            snprintf(buf, sizeof buf, tx("PANEL_HP_FMT"),
                     u->hp, u->fuel, ut->fuel, u->ammo, ut->ammo);
        draw_text(a, a->font_s, ux + 14, py + 48, COL_WHITE, buf);
        int rank = u->exp / 20; if (rank > 5) rank = 5;
        snprintf(buf, sizeof buf, tx("PANEL_EXP_FMT"), u->exp, rank, ut->move);
        draw_text(a, a->font_s, ux + 14, py + 68, COL_GRAY, buf);
        if (ut->supply) {
            snprintf(buf, sizeof buf, tx("PANEL_SUPPLY_HINT"));
            draw_text(a, a->font_s, ux + 14, py + 88, COL_GRAY, buf);
        } else if (game_first_cargo(g, ui) >= 0) {
            char names[64] = "";
            for (int s = 0; s < 2; s++)
                if (u->cargo[s] >= 0) {
                    if (names[0]) strncat(names, "、", sizeof names - strlen(names) - 1);
                    strncat(names, g->types[g->units[u->cargo[s]].type].name,
                            sizeof names - strlen(names) - 1);
                }
            snprintf(buf, sizeof buf, tx("PANEL_CARGO_FMT"), names);
            draw_text(a, a->font_s, ux + 14, py + 88, COL_YELLOW, buf);
            /* 自軍の輸送ユニットなら搭載中の各ユニットの状態も出す */
            if (u->owner == viewer)
                draw_cargo_detail(a, ui, py);
        } else {
            snprintf(buf, sizeof buf, tx("PANEL_ATK_FMT"),
                     ut->atk[0], ut->atk[1], ut->atk[2], ut->atk[3],
                     ut->range_min, game_range_max(g, ut));
            draw_text(a, a->font_s, ux + 14, py + 88, COL_GRAY, buf);
        }
    } else {
        draw_text(a, a->font_s, ux + 14, py + 8, COL_DIM, tx("PANEL_NOUNIT"));
    }

    /* 中央下: 操作ヒント */
    const char *hint = "";
    switch (a->bs) {
    case BS_IDLE:          hint = tx("HINT_IDLE"); break;
    case BS_UNIT_SELECTED: hint = tx("HINT_MOVE"); break;
    case BS_ACTION_MENU:   hint = tx("HINT_MENU"); break;
    case BS_TARGET_SELECT: hint = tx("HINT_TARGET"); break;
    case BS_WORK:          hint = tx("HINT_WORK"); break;
    case BS_UNLOAD:        hint = tx("HINT_UNLOAD"); break;
    case BS_LAYER_PICK:    hint = tx("HINT_LAYER_PICK"); break;
    case BS_EVOLVE_CONFIRM: hint = tx("HINT_EVOLVE"); break;
    case BS_JOIN_CONFIRM:  hint = tx("HINT_JOIN"); break;
    case BS_UNITLIST:      hint = tx("HINT_UNITLIST"); break;
    case BS_PRODUCTION:    hint = tx("HINT_PROD"); break;
    case BS_SAVE_MENU:     hint = tx("HINT_SAVE"); break;
    case BS_BATTLE_ANIM:   hint = tx("HINT_ANIM"); break;
    default: break;
    }
    if (*hint)
        draw_text_center(a, a->font_s, WIN_W / 2, WIN_H - 24, COL_GRAY, hint);
}

static void draw_menus(App *a)
{
    Game *g = &a->game;

    if (a->bs == BS_LAYER_PICK) {
        for (int i = 0; i < a->lpick_n; i++) {
            SDL_Rect r = lpick_rect(a, i);
            bool sel = a->lpick_idx == i;
            fill_rect(a, r.x, r.y, r.w, r.h,
                      sel ? (SDL_Color){ 80, 110, 160, 250 }
                          : (SDL_Color){ 38, 44, 56, 250 });
            outline_rect(a, r.x, r.y, r.w, r.h, sel ? COL_YELLOW : COL_DIM);
            const Unit *u = &g->units[a->lpick_unit[i]];
            const UnitType *ut = &g->types[u->type];
            char buf[96];
            snprintf(buf, sizeof buf, "[%s] %s",
                     layer_tag(unit_layer(ut->mclass)), ut->name);
            draw_text(a, a->font_s, r.x + 12, r.y + 7,
                      sel ? COL_WHITE : COL_GRAY, buf);
            /* HPが満タンでなければ右端に表示 */
            if (u->hp < 10) {
                char hp[16]; snprintf(hp, sizeof hp, "HP%d", u->hp);
                draw_text(a, a->font_s, r.x + r.w - 52, r.y + 7, COL_YELLOW, hp);
            }
        }
    }
    /* 戦闘予測: 攻撃対象を選んでいる間、注目中の相手との読みを出す */
    if (a->bs == BS_TARGET_SELECT && a->sel_unit >= 0 && a->n_targets > 0) {
        /* カーソル上の対象を優先。無ければ巡回中の対象 */
        int ti = -1;
        for (int i = 0; i < a->n_targets; i++) {
            const Unit *t = &g->units[a->targets[i]];
            if (t->pos.x == a->cur_x && t->pos.y == a->cur_y) { ti = a->targets[i]; break; }
        }
        if (ti < 0 && a->target_idx >= 0 && a->target_idx < a->n_targets)
            ti = a->targets[a->target_idx];

        if (ti >= 0 && (g->units[ti].flags & UF_ALIVE)) {
            int dmg = 0, dhp = 0, cnt = 0, ahp = 0;
            battle_forecast(g, a->sel_unit, ti, &dmg, &dhp, &cnt, &ahp);
            const Unit *au = &g->units[a->sel_unit];
            const Unit *du = &g->units[ti];

            int bw = 330, bh = 108;
            float px, py;
            hex_center_px(a, du->pos.x, du->pos.y, &px, &py);
            int bx = (int)px + 46, by = (int)py - bh / 2;
            if (bx + bw > WIN_W) bx = (int)px - bw - 46;
            if (bx < 4) bx = 4;
            if (by < TOPBAR_H + 4) by = TOPBAR_H + 4;
            if (by + bh > WIN_H - PANEL_H) by = WIN_H - PANEL_H - bh - 4;

            fill_rect(a, bx, by, bw, bh, (SDL_Color){ 26, 30, 38, 248 });
            outline_rect(a, bx, by, bw, bh, COL_YELLOW);
            char buf[128];
            snprintf(buf, sizeof buf, "%s", g->types[du->type].name);
            draw_text(a, a->font_s, bx + 12, by + 8, COL_P[du->owner], buf);

            /* 与ダメージ（撃破できるなら強調） */
            if (dhp <= 0)
                snprintf(buf, sizeof buf, tx("FC_DMG_KILL_FMT"), dmg, du->hp);
            else
                snprintf(buf, sizeof buf, tx("FC_DMG_FMT"), dmg, du->hp, dhp);
            draw_text(a, a->font_s, bx + 12, by + 34,
                      dhp <= 0 ? COL_YELLOW : COL_WHITE, buf);

            /* 反撃 */
            if (cnt > 0) {
                if (ahp <= 0)
                    snprintf(buf, sizeof buf, tx("FC_CNT_KILL_FMT"), cnt, au->hp);
                else
                    snprintf(buf, sizeof buf, tx("FC_CNT_FMT"), cnt, au->hp, ahp);
                draw_text(a, a->font_s, bx + 12, by + 58,
                          ahp <= 0 ? (SDL_Color){ 255, 130, 120, 255 }
                                   : (SDL_Color){ 255, 200, 140, 255 }, buf);
            } else {
                draw_text(a, a->font_s, bx + 12, by + 58, COL_GRAY,
                          tx("FC_NO_COUNTER"));
            }
            draw_text(a, a->font_s, bx + 12, by + 82, COL_DIM, tx("FC_NOTE"));
        }
    }
    if (a->bs == BS_ACTION_MENU) {
        for (int i = 0; i < a->amenu_n; i++) {
            SDL_Rect r = amenu_rect(a, i);
            bool sel = a->amenu_idx == i;
            fill_rect(a, r.x, r.y, r.w, r.h,
                      sel ? (SDL_Color){ 80, 110, 160, 245 }
                          : (SDL_Color){ 40, 48, 60, 245 });
            outline_rect(a, r.x, r.y, r.w, r.h, sel ? COL_YELLOW : COL_DIM);
            draw_text(a, a->font_m, r.x + 14, r.y + 5,
                      sel ? COL_WHITE : COL_GRAY, tx(ACT_KEYS[a->amenu_items[i]]));
        }
    }
    if (a->bs == BS_PRODUCTION) {
        bool scrollable = a->prod_n > PROD_VISIBLE;
        int shown = scrollable ? PROD_VISIBLE : a->prod_n;
        /* 行領域(40+shown*44) + 下部余白(スクロール時は▼用に広めに) */
        int ph = 40 + shown * 44 + (scrollable ? 24 : 12);
        fill_rect(a, WIN_W / 2 - 280, 100, 560, ph, (SDL_Color){ 28, 32, 38, 245 });
        outline_rect(a, WIN_W / 2 - 280, 100, 560, ph, COL_DIM);
        char title[64];
        if (scrollable)
            snprintf(title, sizeof title, "%s  (%d/%d)", tx("PROD_TITLE"),
                     a->prod_idx + 1, a->prod_n);
        else
            snprintf(title, sizeof title, "%s", tx("PROD_TITLE"));
        draw_text_center(a, a->font_m, WIN_W / 2, 106, COL_WHITE, title);
        /* 上下に隠れた項目があれば ▲▼ を表示 */
        if (a->prod_scroll > 0)
            draw_text_center(a, a->font_s, WIN_W / 2 + 250, 106, COL_YELLOW, "▲");
        if (a->prod_scroll + PROD_VISIBLE < a->prod_n)
            draw_text_center(a, a->font_s, WIN_W / 2, 100 + ph - 20, COL_YELLOW, "▼");
        int end = a->prod_scroll + PROD_VISIBLE;
        if (end > a->prod_n) end = a->prod_n;
        for (int i = a->prod_scroll; i < end; i++) {
            SDL_Rect r = prod_rect(a, i);
            int t = a->prod_items[i];
            int slot = a->prod_store[i];
            bool from_store = slot >= 0;
            bool afford = from_store || g->funds[g->current] >= g->types[t].cost;
            bool sel = a->prod_idx == i;
            fill_rect(a, r.x, r.y, r.w, r.h,
                      sel ? (SDL_Color){ 80, 110, 160, 255 }
                          : (SDL_Color){ 40, 48, 60, 255 });
            outline_rect(a, r.x, r.y, r.w, r.h, sel ? COL_YELLOW : COL_DIM);
            char buf[96];
            /* 倉庫からの引き出しは名前頭に[倉]を付け、右端は無料表示 */
            if (from_store)
                snprintf(buf, sizeof buf, "[%s] %s", tx("PROD_STORE_TAG"),
                         g->types[t].name);
            else
                snprintf(buf, sizeof buf, "%s", g->types[t].name);
            draw_text(a, a->font_m, r.x + 14, r.y + 7,
                      afford ? COL_WHITE : COL_DIM, buf);
            if (from_store)
                snprintf(buf, sizeof buf, "%s", tx("PROD_FREE"));
            else
                snprintf(buf, sizeof buf, "%dG", g->types[t].cost);
            draw_text(a, a->font_m, r.x + r.w - 90, r.y + 7,
                      from_store ? (SDL_Color){ 130, 220, 130, 255 }
                                 : (afford ? COL_YELLOW : COL_DIM), buf);
        }
    }
    if (a->bs == BS_UNITLIST) {
        bool scrollable = a->ulist_n > UNITLIST_VISIBLE;
        int shown = scrollable ? UNITLIST_VISIBLE : a->ulist_n;
        if (shown < 1) shown = 1;
        int ph = 50 + shown * 42 + (scrollable ? 24 : 12);
        fill_rect(a, WIN_W / 2 - 320, 100, 640, ph, (SDL_Color){ 28, 32, 38, 246 });
        outline_rect(a, WIN_W / 2 - 320, 100, 640, ph, COL_DIM);
        char title[96];
        snprintf(title, sizeof title, tx("ULIST_TITLE_FMT"), a->ulist_n);
        draw_text_center(a, a->font_m, WIN_W / 2, 108, COL_WHITE, title);

        if (a->ulist_n == 0) {
            draw_text_center(a, a->font_m, WIN_W / 2, 160, COL_GRAY,
                             tx("ULIST_EMPTY"));
        } else {
            if (a->ulist_scroll > 0)
                draw_text_center(a, a->font_s, WIN_W / 2 + 290, 108, COL_YELLOW, "▲");
            if (a->ulist_scroll + UNITLIST_VISIBLE < a->ulist_n)
                draw_text_center(a, a->font_s, WIN_W / 2, 100 + ph - 20, COL_YELLOW, "▼");
            int end = a->ulist_scroll + UNITLIST_VISIBLE;
            if (end > a->ulist_n) end = a->ulist_n;
            for (int i = a->ulist_scroll; i < end; i++) {
                SDL_Rect r = ulist_rect(a, i);
                const Unit *u = &g->units[a->ulist[i]];
                const UnitType *ut = &g->types[u->type];
                bool sel = (a->ulist_idx == i);
                fill_rect(a, r.x, r.y, r.w, r.h,
                          sel ? (SDL_Color){ 80, 110, 160, 255 }
                              : (SDL_Color){ 40, 48, 60, 255 });
                outline_rect(a, r.x, r.y, r.w, r.h, sel ? COL_YELLOW : COL_DIM);
                char buf[160];
                snprintf(buf, sizeof buf, "[%s] %s",
                         layer_tag(unit_layer(ut->mclass)), ut->name);
                draw_text(a, a->font_s, r.x + 12, r.y + 9,
                          sel ? COL_WHITE : COL_GRAY, buf);
                /* 位置と状態（HPが減っていれば黄色で警告的に） */
                snprintf(buf, sizeof buf, tx("ULIST_ROW_FMT"),
                         u->hp, game_terrain_at(g, u->pos.x, u->pos.y)->name,
                         u->pos.x, u->pos.y);
                draw_text(a, a->font_s, r.x + r.w - 300, r.y + 9,
                          u->hp < 10 ? COL_YELLOW : (sel ? COL_WHITE : COL_GRAY), buf);
            }
        }
    }
    /* 進化の確認。不可逆なので「何になるか」と「経験値が0に戻る」ことを明示する */
    if (a->bs == BS_EVOLVE_CONFIRM && a->sel_unit >= 0) {
        int to = game_evolve_target(g, a->sel_unit);
        const char *from = g->types[g->units[a->sel_unit].type].name;
        const char *toname = (to >= 0) ? g->types[to].name : "";
        int bw = 500, bh = 208;
        int bx = WIN_W / 2 - bw / 2, by = WIN_H / 2 - 108;
        fill_rect(a, 0, 0, WIN_W, WIN_H, (SDL_Color){ 8, 10, 14, 130 });
        fill_rect(a, bx, by, bw, bh, (SDL_Color){ 28, 32, 38, 248 });
        outline_rect(a, bx, by, bw, bh, COL_YELLOW);

        char buf[192];
        snprintf(buf, sizeof buf, tx("EVOLVE_TITLE_FMT"), from, toname);
        draw_text_center(a, a->font_m, WIN_W / 2, by + 16, COL_WHITE, buf);
        snprintf(buf, sizeof buf, tx("EVOLVE_COST_FMT"),
                 game_evolve_cost(g, a->sel_unit), g->funds[g->current]);
        draw_text_center(a, a->font_s, WIN_W / 2, by + 50, COL_YELLOW, buf);
        draw_text_center(a, a->font_s, WIN_W / 2, by + 72, COL_GRAY,
                         tx("EVOLVE_WARN1"));
        draw_text_center(a, a->font_s, WIN_W / 2, by + 92, COL_GRAY,
                         tx("EVOLVE_WARN2"));
        for (int i = 0; i < 2; i++) {
            SDL_Rect r = evo_rect(i);
            bool sel = (a->amenu_idx == i);
            fill_rect(a, r.x, r.y, r.w, r.h,
                      sel ? (SDL_Color){ 80, 110, 160, 255 }
                          : (SDL_Color){ 40, 48, 60, 255 });
            outline_rect(a, r.x, r.y, r.w, r.h, sel ? COL_YELLOW : COL_DIM);
            draw_text_center(a, a->font_m, r.x + r.w / 2, r.y + 8,
                             sel ? COL_WHITE : COL_GRAY,
                             tx(i == 0 ? "EVOLVE_YES" : "EVOLVE_NO"));
        }
    }
    /* 合流の確認。合流後のHPと払い戻しを先に見せる（片方は盤上から消えるため） */
    if (a->bs == BS_JOIN_CONFIRM && a->sel_unit >= 0 && a->join_target >= 0) {
        const Unit *m = &g->units[a->sel_unit];
        const Unit *t = &g->units[a->join_target];
        const UnitType *ut = &g->types[t->type];
        int sum = m->hp + t->hp;
        int hp = sum > 10 ? 10 : sum;
        int over = sum > 10 ? sum - 10 : 0;
        int refund = over > 0 ? ut->cost * over / 10 : 0;

        int bw = 500, bh = 208;
        int bx = WIN_W / 2 - bw / 2, by = WIN_H / 2 - 108;
        fill_rect(a, 0, 0, WIN_W, WIN_H, (SDL_Color){ 8, 10, 14, 130 });
        fill_rect(a, bx, by, bw, bh, (SDL_Color){ 28, 32, 38, 248 });
        outline_rect(a, bx, by, bw, bh, COL_YELLOW);

        char buf[192];
        snprintf(buf, sizeof buf, tx("JOIN_TITLE_FMT"), ut->name);
        draw_text_center(a, a->font_m, WIN_W / 2, by + 16, COL_WHITE, buf);
        snprintf(buf, sizeof buf, tx("JOIN_HP_FMT"), m->hp, t->hp, hp);
        draw_text_center(a, a->font_s, WIN_W / 2, by + 50, COL_YELLOW, buf);
        if (refund > 0) {
            snprintf(buf, sizeof buf, tx("JOIN_REFUND_FMT"), refund);
            draw_text_center(a, a->font_s, WIN_W / 2, by + 72, COL_YELLOW, buf);
        }
        draw_text_center(a, a->font_s, WIN_W / 2, by + 92, COL_GRAY,
                         tx("JOIN_WARN"));
        for (int i = 0; i < 2; i++) {
            SDL_Rect r = evo_rect(i);
            bool sel = (a->amenu_idx == i);
            fill_rect(a, r.x, r.y, r.w, r.h,
                      sel ? (SDL_Color){ 80, 110, 160, 255 }
                          : (SDL_Color){ 40, 48, 60, 255 });
            outline_rect(a, r.x, r.y, r.w, r.h, sel ? COL_YELLOW : COL_DIM);
            draw_text_center(a, a->font_m, r.x + r.w / 2, r.y + 8,
                             sel ? COL_WHITE : COL_GRAY,
                             tx(i == 0 ? "JOIN_YES" : "JOIN_NO"));
        }
    }
    if (a->bs == BS_TURN_MENU) {
        char tiltbuf[64];
        snprintf(tiltbuf, sizeof tiltbuf, tx("TMENU_TILT_FMT"),
                 tx(a->opt_tilt ? "OPT_TILT_ON" : "OPT_TILT_OFF"));
        const char *items[TMENU_ITEMS] = {
            tx("TMENU_UNITLIST"), tx("TMENU_END"), tx("TMENU_SAVE"),
            tiltbuf, tx("TMENU_TITLE"), tx("TMENU_CLOSE")
        };
        fill_rect(a, WIN_W / 2 - 150, 240, 300, TMENU_ITEMS * 52 + 30,
                  (SDL_Color){ 28, 32, 38, 245 });
        outline_rect(a, WIN_W / 2 - 150, 240, 300, TMENU_ITEMS * 52 + 30, COL_DIM);
        for (int i = 0; i < TMENU_ITEMS; i++) {
            SDL_Rect r = tmenu_rect(i);
            bool sel = a->tmenu_idx == i;
            fill_rect(a, r.x, r.y, r.w, r.h,
                      sel ? (SDL_Color){ 80, 110, 160, 255 }
                          : (SDL_Color){ 40, 48, 60, 255 });
            outline_rect(a, r.x, r.y, r.w, r.h, sel ? COL_YELLOW : COL_DIM);
            draw_text_center(a, a->font_m, r.x + r.w / 2, r.y + 8,
                             sel ? COL_WHITE : COL_GRAY, items[i]);
        }
    }
    if (a->bs == BS_SAVE_MENU) {
        fill_rect(a, WIN_W / 2 - 300, 80, 600, 40 + 10 * 46 + 12,
                  (SDL_Color){ 28, 32, 38, 245 });
        outline_rect(a, WIN_W / 2 - 300, 80, 600, 40 + 10 * 46 + 12, COL_DIM);
        draw_text_center(a, a->font_m, WIN_W / 2, 88, COL_WHITE, tx("SAVE_TITLE"));
        for (int i = 0; i < 10; i++) {
            SDL_Rect r = smenu_rect(i);
            bool sel = a->smenu_idx == i;
            fill_rect(a, r.x, r.y, r.w, r.h,
                      sel ? (SDL_Color){ 80, 110, 160, 255 }
                          : (SDL_Color){ 40, 48, 60, 255 });
            outline_rect(a, r.x, r.y, r.w, r.h, sel ? COL_YELLOW : COL_DIM);
            char path[600], name[64], buf[128];
            int turn = -1;
            ui_save_path(a, i + 1, path, sizeof path);
            if (save_peek(path, name, sizeof name, &turn) == 0)
                snprintf(buf, sizeof buf, tx("SAVE_SLOT_FMT"), i + 1, name, turn);
            else
                snprintf(buf, sizeof buf, tx("SAVE_EMPTY_FMT"), i + 1);
            draw_text(a, a->font_m, r.x + 14, r.y + 7,
                      sel ? COL_WHITE : COL_GRAY, buf);
        }
    }
}

/* HP減少をアニメ補間して表示 */
static int anim_hp(int hp0, int hp1, int t, int hit_t)
{
    if (t < hit_t) return hp0;
    int k = t - hit_t;
    if (k > 20) k = 20;
    return hp0 - (hp0 - hp1) * k / 20;
}

static void draw_anim_card(App *a, int x, int y, int owner, int type, int hp)
{
    const UnitType *ut = &a->game.types[type];
    /* ユニットアイコン */
    SDL_Color body = COL_P[owner];
    render_fill_hex(a, (float)(x + 40), (float)(y + 44), 34.0f,
                    (SDL_Color){ 40, 48, 60, 255 });
    SDL_Texture *spr = sprite_get(a, type, owner);
    if (spr) {
        SDL_FRect dst = { (float)(x + 40 - 26), (float)(y + 44 - 26), 52, 52 };
        SDL_SetTextureColorMod(spr, 255, 255, 255);
        SDL_RenderCopyF(a->ren, spr, NULL, &dst);
        render_hex_outline(a, (float)(x + 40), (float)(y + 44), 33.0f, body);
    } else {
        render_fill_hex(a, (float)(x + 40), (float)(y + 44), 26.0f, body);
        draw_text_center(a, a->font_l, x + 40, y + 28, COL_WHITE, ut->icon);
    }
    draw_text(a, a->font_m, x + 88, y + 8, COL_WHITE, ut->name);
    /* HPバー 10セグメント */
    for (int i = 0; i < 10; i++) {
        SDL_Color c = i < hp ? (SDL_Color){ 120, 220, 120, 255 }
                             : (SDL_Color){ 55, 60, 70, 255 };
        if (hp <= 3 && i < hp) c = (SDL_Color){ 230, 120, 80, 255 };
        fill_rect(a, x + 88 + i * 20, y + 46, 16, 20, c);
    }
    char buf[16];
    snprintf(buf, sizeof buf, "%d/10", hp);
    draw_text(a, a->font_s, x + 88 + 205, y + 46, COL_WHITE, buf);
}

/* 動画演出: 画面中央に動画を等比で最大表示し、下に戦況テキストを重ねる */
static void draw_battle_anim_video(App *a, UnitAnim *ua)
{
    BattleAnim *an = &a->anim;
    int t = an->total - an->timer;
    int elapsed = (int)(SDL_GetTicks() - an->start_ms);
    SDL_Texture *fr = uanim_frame_at(a, ua, elapsed);

    fill_rect(a, 0, 0, WIN_W, WIN_H, (SDL_Color){ 8, 10, 14, 235 });
    if (fr && ua->w > 0 && ua->h > 0) {
        int maxw = WIN_W - 80, maxh = WIN_H - 180;
        double sc = (double)maxw / ua->w;
        if ((double)ua->h * sc > maxh) sc = (double)maxh / ua->h;
        int dw = (int)(ua->w * sc), dh = (int)(ua->h * sc);
        SDL_Rect dst = { WIN_W / 2 - dw / 2, 60, dw, dh };
        SDL_RenderCopy(a->ren, fr, NULL, &dst);
        outline_rect(a, dst.x - 2, dst.y - 2, dst.w + 4, dst.h + 4, COL_DIM);
    }

    /* 戦況（攻撃側→防御側のHP推移）を動画の下に表示 */
    int def_hp = anim_hp(an->def_hp0, an->def_hp1, t, an->total / 3);
    int atk_hp = anim_hp(an->atk_hp0, an->atk_hp1, t, an->total * 2 / 3);
    char buf[160];
    snprintf(buf, sizeof buf, "%s  HP%d  →  %s  HP%d",
             a->game.types[an->atk_type].name, atk_hp,
             a->game.types[an->def_type].name, def_hp);
    draw_text_center(a, a->font_m, WIN_W / 2, WIN_H - 92, COL_WHITE, buf);
    draw_text_center(a, a->font_s, WIN_W / 2, WIN_H - 56, COL_DIM, tx("HINT_ANIM"));
}

/* 攻撃時のカットイン1枚絵。左からスライドインし、最後にスライドアウトする。
 * 画像が無い/読めない場合は何も描かない（従来の演出だけになる）。 */
/* カットインの描画本体。from_right で右から出す（敵の必殺技用）。 */
static void draw_cutin_img(App *a, const char *path, int total, int timer,
                           bool from_right)
{
    if (!path || !path[0]) return;
    int iw = 0, ih = 0;
    SDL_Texture *tex = sprite_get_path(a, path, &iw, &ih);
    if (!tex || iw <= 0 || ih <= 0) return;

    const int IN = 10, OUT = 12;          /* スライドイン/アウトのフレーム数 */
    int t = total - timer;
    float k = 1.0f;                        /* 0=画面外 1=定位置 */
    if (t < IN)            k = (float)t / (float)IN;
    else if (timer < OUT)  k = (float)timer / (float)OUT;
    if (k < 0.0f) k = 0.0f;
    if (k > 1.0f) k = 1.0f;
    /* 行き過ぎてから戻る動き（イーズアウト）で勢いを出す */
    float e = 1.0f - (1.0f - k) * (1.0f - k);

    /* 下端に接地させる（下で切れないよう下端合わせ） */
    float dh = (float)WIN_H * 0.78f;
    float dw = dh * (float)iw / (float)ih;
    float x0, x1;
    if (from_right) { x0 = (float)WIN_W + 20.0f; x1 = (float)WIN_W - dw - 4.0f; }
    else            { x0 = -dw - 20.0f;          x1 = 4.0f; }
    SDL_FRect dst = { x0 + (x1 - x0) * e, (float)WIN_H - dh, dw, dh };
    SDL_SetTextureAlphaMod(tex, (Uint8)(255.0f * e));
    if (from_right)   /* 右から出すときは左右反転して盤面を向かせる */
        SDL_RenderCopyExF(a->ren, tex, NULL, &dst, 0.0, NULL, SDL_FLIP_HORIZONTAL);
    else
        SDL_RenderCopyF(a->ren, tex, NULL, &dst);
    SDL_SetTextureAlphaMod(tex, 255);
}

static void draw_cutin(App *a, BattleAnim *an)
{
    draw_cutin_img(a, an->cutin, an->total, an->timer, false);
}

/* 必殺技のカットインを始める。**敵味方を問わず出す**。
 * 敵の必殺技は盤面が大きく動くのにバナーだけだと見逃しやすいため。 */
static void start_co_cutin(App *a, const CommanderType *co, int player)
{
    a->co_cutin[0] = 0;
    a->co_cutin_timer = 0;
    if (!co || !co->cutin[0]) return;
    if (a->opt_cutin == 0) return;      /* カットインを切っているなら出さない */
    snprintf(a->co_cutin, sizeof a->co_cutin, "%s", co->cutin);
    a->co_cutin_total = 100;
    a->co_cutin_timer = a->co_cutin_total;
    a->co_cutin_p = player;
}

static void draw_battle_anim(App *a)
{
    BattleAnim *an = &a->anim;
    int t = an->total - an->timer;

    if (an->use_video) {
        UnitAnim *ua = uanim_get(a, an->atk_type);
        if (ua) { draw_battle_anim_video(a, ua); draw_cutin(a, an); return; }
    }

    fill_rect(a, 0, 0, WIN_W, WIN_H, (SDL_Color){ 10, 12, 16, 120 });
    /* カットインは左に立つので、その間はパネルを右へ寄せて重ならないようにする */
    int shift = an->cutin[0] ? 80 : 0;
    int px = WIN_W / 2 - 360 + shift, py = WIN_H / 2 - 110, pw = 720, ph = 220;
    fill_rect(a, px, py, pw, ph, (SDL_Color){ 28, 32, 38, 245 });
    outline_rect(a, px, py, pw, ph, COL_DIM);

    int def_hp = anim_hp(an->def_hp0, an->def_hp1, t, 30);
    int atk_hp = anim_hp(an->atk_hp0, an->atk_hp1, t, 100);

    draw_anim_card(a, px + 24, py + 30, an->atk_owner, an->atk_type, atk_hp);
    draw_anim_card(a, px + 24, py + 124, an->def_owner, an->def_type, def_hp);

    /* 攻撃方向マーク */
    draw_text_center(a, a->font_l, px + pw - 60, py + 40, COL_YELLOW,
                     t < 75 ? "▼" : "▲");
    draw_text_center(a, a->font_s, px + pw / 2, py + ph - 4, COL_DIM,
                     tx("HINT_ANIM"));
    draw_cutin(a, an);
}

/* 副目標の進捗を左上（トップバー直下）に常時表示する。
 * 達成/未達が戦闘中に分かるようにしないと、狙って動きようがないため。
 * キャンペーンで副目標のある作戦のときだけ出る。 */
static void draw_sub_objectives(App *a)
{
    if (!a->campaign_mode) return;
    const CpnNode *node = campaign_find_node(&a->cpn, a->cps.node);
    if (!node || node->n_subs <= 0) return;

    const int x = 8, w = 300;
    const int y = TOPBAR_H + 8;
    const int h = 24 + node->n_subs * 20;
    fill_rect(a, x, y, w, h, (SDL_Color){ 22, 26, 34, 200 });
    outline_rect(a, x, y, w, h, COL_DIM);
    draw_text(a, a->font_s, x + 8, y + 4, COL_YELLOW, tx("SUB_HUD_TITLE"));

    Game *g = &a->game;
    for (int i = 0; i < node->n_subs; i++) {
        bool ok = campaign_sub_done(g, node, i);
        char buf[128];
        snprintf(buf, sizeof buf, ok ? tx("SUB_OK_FMT") : tx("SUB_NG_FMT"),
                 node->subs[i].desc);
        draw_text(a, a->font_s, x + 8, y + 24 + i * 20,
                  ok ? (SDL_Color){ 130, 220, 130, 255 } : COL_GRAY, buf);
    }
}

void battle_draw(App *a)
{
    Game *g = &a->game;
    fill_rect(a, 0, 0, WIN_W, WIN_H, (SDL_Color){ 22, 26, 32, 255 });

    if (a->bs == BS_HANDOVER) {
        char buf[96];
        snprintf(buf, sizeof buf, tx("HANDOVER_FMT"), faction_name(g->current));
        draw_text_center(a, a->font_xl, WIN_W / 2, 300, COL_P[g->current], buf);
        draw_text_center(a, a->font_m, WIN_W / 2, 400, COL_WHITE,
                         tx("HANDOVER_SUB"));
        return;
    }

    render_map(a);
    /* マップの上、選択表示やUIの下に天候を重ねる */
    render_weather_fx(a, (int)game_weather(g), a->frame);
    draw_overlays(a);
    draw_popups(a);
    draw_topbar(a);
    draw_sub_objectives(a);
    draw_panels(a);
    draw_menus(a);

    /* CPU手番表示 */
    if (a->bs == BS_CPU_TURN) {
        fill_rect(a, WIN_W / 2 - 120, TOPBAR_H + 8, 240, 34,
                  (SDL_Color){ 28, 32, 38, 220 });
        draw_text_center(a, a->font_m, WIN_W / 2, TOPBAR_H + 12,
                         COL_P[g->current], tx("CPU_THINKING"));
    }

    /* 戦闘アニメ（仕様書 8.2: HPバー減少 + SE） */
    if (a->bs == BS_BATTLE_ANIM)
        draw_battle_anim(a);

    /* 必殺技のカットイン（自軍は左から、敵は右から）。
     * バナーの前に描いて、技名の文字が上に乗るようにする。 */
    if (a->co_cutin_timer > 0 && a->co_cutin[0])
        draw_cutin_img(a, a->co_cutin, a->co_cutin_total, a->co_cutin_timer,
                       a->co_cutin_p != 0);

    /* バナー */
    if (a->banner_timer > 0 && a->banner[0]) {
        int alpha = a->banner_timer > 30 ? 255 : a->banner_timer * 255 / 30;
        SDL_Color bg = { 20, 24, 30, (Uint8)(alpha * 4 / 5) };
        fill_rect(a, 0, WIN_H / 2 - 50, WIN_W, 100, bg);
        SDL_Color fg = COL_WHITE;
        fg.a = (Uint8)alpha;
        draw_text_center(a, a->font_xl, WIN_W / 2, WIN_H / 2 - 32, fg, a->banner);
    }
}

