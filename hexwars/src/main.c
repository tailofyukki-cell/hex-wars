/* main.c - エントリ・ゲームループ */
#include <SDL.h>
#include <SDL_ttf.h>
#include <SDL_image.h>   /* HWSHOT のスクリーンショット保存用 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "ui/app.h"
#include "ui/text.h"
#include "ui/sound.h"
#include "ui/sprites.h"
#include "ui/anim.h"

static App s_app; /* 静的確保（仕様書 1.2） */

/* 今の描画内容を HWSHOT のパスへ PNG で落として終了を予約する */
static void save_shot(App *a)
{
    SDL_Surface *sf = SDL_CreateRGBSurfaceWithFormat(0, WIN_W, WIN_H, 32,
                                                     SDL_PIXELFORMAT_RGBA32);
    if (sf && SDL_RenderReadPixels(a->ren, NULL, SDL_PIXELFORMAT_RGBA32,
                                   sf->pixels, sf->pitch) == 0)
        IMG_SavePNG(sf, getenv("HWSHOT"));
    if (sf) SDL_FreeSurface(sf);
    a->quit = true;
}

static int load_defs(App *a)
{
    char path[600], err[256];

    snprintf(path, sizeof path, "%sdata/terrain.def", a->base_path);
    if (data_load_terrain(&a->game, path, err, sizeof err) != 0) {
        SDL_Log("terrain.def 読込失敗: %s", err);
        return -1;
    }
    snprintf(path, sizeof path, "%sdata/units.def", a->base_path);
    if (data_load_units(&a->game, path, err, sizeof err) != 0) {
        SDL_Log("units.def 読込失敗: %s", err);
        return -1;
    }
    snprintf(path, sizeof path, "%sdata/commanders.def", a->base_path);
    if (data_load_commanders(&a->game, path, err, sizeof err) != 0) {
        SDL_Log("commanders.def 読込失敗: %s", err);
        return -1;
    }
    snprintf(path, sizeof path, "%sdata/maps/maplist.txt", a->base_path);
    if (data_load_maplist(&a->maps, path) != 0) {
        SDL_Log("maplist.txt 読込失敗");
        return -1;
    }
    return 0;
}

int main(int argc, char *argv[])
{
    (void)argc; (void)argv;
    App *a = &s_app;

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        SDL_Log("SDL_Init: %s", SDL_GetError());
        return 1;
    }
    if (TTF_Init() != 0) {
        SDL_Log("TTF_Init: %s", TTF_GetError());
        return 1;
    }

    /* 初期化（フォント・音声・画像の読込）に1秒前後かかるため、その間ウィンドウを
     * 出しっぱなしにするとOSが再描画してチカチカする。最初の1フレームを描き終える
     * まで隠しておき、準備ができてから表示する。 */
    /* この時点では text_ja.def を読んでいないので仮題。読込後に付け替える */
    a->win = SDL_CreateWindow("...",
                              SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                              WIN_W, WIN_H, SDL_WINDOW_HIDDEN);
    if (!a->win) { SDL_Log("CreateWindow: %s", SDL_GetError()); return 1; }

    a->ren = SDL_CreateRenderer(a->win, -1,
                                SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!a->ren)
        a->ren = SDL_CreateRenderer(a->win, -1, 0);
    if (!a->ren) { SDL_Log("CreateRenderer: %s", SDL_GetError()); return 1; }
    SDL_SetRenderDrawBlendMode(a->ren, SDL_BLENDMODE_BLEND);
    {   /* どの描画バックエンドになったかを記録（software だと遅く、ちらつきやすい） */
        SDL_RendererInfo ri;
        if (SDL_GetRendererInfo(a->ren, &ri) == 0)
            SDL_Log("renderer=%s accelerated=%s vsync=%s", ri.name,
                    (ri.flags & SDL_RENDERER_ACCELERATED) ? "yes" : "no",
                    (ri.flags & SDL_RENDERER_PRESENTVSYNC) ? "yes" : "no");
    }

    if (assets_init(a) != 0) return 1;
    if (load_defs(a) != 0) return 1;
    {
        char dir[600];
        snprintf(dir, sizeof dir, "%ssaves", a->base_path);
        save_ensure_dir(dir);
        snprintf(dir, sizeof dir, "%sdata/text_ja.def", a->base_path);
        if (text_load(dir) != 0)
            SDL_Log("text_ja.def が読めません（キー名を直接表示します）");
    }
    /* ウィンドウのタイトルは text_ja.def の WINDOW_TITLE から取る。
     * 別ゲームに作り替えるときに再ビルドしなくても変えられるようにするため。 */
    {
        const char *title = tx("WINDOW_TITLE");
        SDL_SetWindowTitle(a->win, (title && strcmp(title, "WINDOW_TITLE")) ? title
                                                                           : "HEX WARS");
    }
    snd_init(a->base_path);
    sprites_init();
    uanim_init();
    options_load(a);
    progress_load(a);
    /* 未解禁の指揮官が初期選択にならないようにする */
    a->sel_co0 = co_is_unlocked(a, 0) ? 0 : co_next_unlocked(a, 0, 1);
    a->sel_co1 = a->sel_co0;
    /* フリー対戦の初期値: 陣営0を人間、他はCPU普通。
     * CTRL_HUMAN 相当の 3 を入れ忘れると「人間が居ない」で開始できなくなる。 */
    a->sel_ctrl[0] = 3;
    for (int p = 1; p < MAX_PLAYERS; p++) a->sel_ctrl[p] = 1;
    for (int p = 0; p < MAX_PLAYERS; p++) a->sel_co[p] = a->sel_co0;
    a->sel_p2 = 1;

    a->screen = SCREEN_TITLE;
    a->next_screen = SCREEN_TITLE;
    /* デバッグ用: --screen options 等で任意画面から起動 */
    if (argc >= 3 && strcmp(argv[1], "--screen") == 0) {
        if      (!strcmp(argv[2], "options")) a->next_screen = SCREEN_OPTIONS;
        else if (!strcmp(argv[2], "setup")) {
            /* --screen setup [maplistの番号] [行] でマップとカーソル位置を
             * 指定できる。行を指定するのは、指揮官の行を選んだときだけ出る
             * 右ペインを確かめるため（setup_enter が拾って一度だけ適用する）。 */
            if (argc >= 4) {
                int mi = atoi(argv[3]);
                if (mi >= 0 && mi < a->maps.n) a->sel_map = mi;
            }
            if (argc >= 5) a->setup_row_boot = atoi(argv[4]);
            if (argc >= 6) {           /* 指揮官も選んだ状態にできる */
                int ci = atoi(argv[5]);
                for (int p = 0; p < MAX_PLAYERS; p++) a->sel_co[p] = ci;
                a->sel_co0 = a->sel_co1 = ci;
            }
            a->next_screen = SCREEN_SETUP;
        }
        else if (!strcmp(argv[2], "load"))    a->next_screen = SCREEN_LOAD;
        else if (!strcmp(argv[2], "endroll")) a->next_screen = SCREEN_ENDROLL;
        else if (!strcmp(argv[2], "result")) {
            /* 結果画面のレイアウト確認用。数値は仮（一部は画面側で再計算される） */
            char path[600], err[256];
            snprintf(path, sizeof path, "%sdata/campaign/main.cpn", a->base_path);
            if (campaign_load(&a->cpn, path, err, sizeof err) == 0) {
                memset(&a->cps, 0, sizeof a->cps);
                a->cps.active = true;
                a->campaign_mode = true;
                /* 第3作戦「河川突破」をクリアした直後を再現（副目標2個） */
                snprintf(a->cps_backup.node, sizeof a->cps_backup.node, "M03");
                snprintf(a->cps.node, sizeof a->cps.node, "M04");
                a->cps.n_carry = 30;
                a->cps.funds_carry = 9330;
                a->cpn_result = 1;
                a->game.winner = 0;
                a->game.turn = 25;
                a->game.lost_units[0] = 2;
                a->game.lost_units[1] = 33;
                a->next_screen = SCREEN_RESULT;
            }
        }
        else if (!strcmp(argv[2], "battle")) {
            /* マップ描画（斜め見下ろし表示など）の確認用。
             *   --screen battle [maplistの番号]   索敵OFFで全体が見える状態にする */
            char path[600], err[256];
            int mi = (argc >= 4) ? atoi(argv[3]) : 0;
            if (mi < 0 || mi >= a->maps.n) mi = 0;
            snprintf(path, sizeof path, "%sdata/%s", a->base_path,
                     a->maps.file[mi]);
            if (data_load_map(&a->game, path, err, sizeof err) == 0) {
                a->campaign_mode = false;
                a->cps.active = false;
                a->game.fog = false;
                a->game.ctrl[0] = CTRL_HUMAN;
                a->game.ctrl[1] = CTRL_CPU_NORMAL;
                a->game.co_id[0] = a->game.co_id[1] = -1;
                game_start(&a->game, 12345u);
                /* 見た目確認用:
                 *   --screen battle <map> <0=晴 1=曇 2=雨> [ターン]
                 * ターンを指定すると昼夜を選べる（昼3・夜2の固定周期なので
                 * 1..3=昼 / 4..5=夜）。 */
                if (argc >= 5) {
                    int wx = atoi(argv[4]);
                    if (wx >= 0 && wx < WX_COUNT) {
                        a->game.weather_on = true;
                        a->game.weather = (uint8_t)wx;
                        a->game.weather_left = 3;
                    }
                }
                if (argc >= 6) {
                    int t = atoi(argv[5]);
                    if (t >= 1) a->game.turn = t;
                }
                a->next_screen = SCREEN_BATTLE;
            } else {
                SDL_Log("マップ読込失敗: %s", err);
            }
        }
        else if (!strcmp(argv[2], "cpn")) {
            /* キャンペーンの任意の作戦をその場から始める（デバッグ用）。
             *   --screen cpn <ノードID> [持越し数]
             *   例) --screen cpn M10 30
             * 終盤の作戦を確認するのに13回勝ち上がるのは現実的でないため、
             * 「そこまで進んだ状態」を作ってブリーフィングから入る。
             * 持越し部隊・資金・クリア記録・指揮官の解禁もそれらしく埋める。 */
            char path[600], err[256];
            snprintf(path, sizeof path, "%sdata/campaign/main.cpn", a->base_path);
            if (campaign_load(&a->cpn, path, err, sizeof err) != 0) {
                SDL_Log("キャンペーン読込失敗: %s", err);
            } else {
                const char *want = (argc >= 4) ? argv[3] : a->cpn.start;
                int n_carry = (argc >= 5) ? atoi(argv[4]) : 24;
                memset(&a->cps, 0, sizeof a->cps);
                a->cps.active = true;
                a->campaign_mode = true;
                snprintf(a->cps.file, sizeof a->cps.file, "campaign/main.cpn");
                snprintf(a->cps.node, sizeof a->cps.node, "%s", want);
                /* 本線をたどり、目的のノードより手前を「制圧済み」にする */
                {
                    char cur[24];
                    snprintf(cur, sizeof cur, "%s", a->cpn.start);
                    for (int step = 0; step < a->cpn.n_nodes; step++) {
                        int idx = -1;
                        for (int i = 0; i < a->cpn.n_nodes; i++)
                            if (!strcmp(a->cpn.nodes[i].id, cur)) { idx = i; break; }
                        if (idx < 0 || !strcmp(cur, want)) break;
                        a->cps.cleared |= 1u << idx;
                        if (idx < MAX_CAMPAIGN_MAPS) a->cps.rank[idx] = RANK_A;
                        snprintf(cur, sizeof cur, "%s", a->cpn.nodes[idx].next_win);
                    }
                }
                /* 持越し部隊: 陸・海・空をひと通り、経験値もばらけさせる。
                 * 進化済みが混ざるよう、一部は経験値100にしておく。 */
                {
                    static const char *ROSTER[] = {
                        "INFANTRY", "INFANTRY", "AT_INFANTRY", "MECH_INF",
                        "TANK", "TANK", "HTANK", "ARTILLERY", "ROCKET",
                        "AA_TANK", "RECON", "TRUCK", "SUPPLY",
                        "FIGHTER", "BOMBER", "DIVE_BOMBER", "HELI",
                        "DESTROYER", "CRUISER", "BATTLESHIP", "CARRIER",
                        "T_SHIP", "SUPPLY_SHIP", "SUBMARINE",
                    };
                    int n_roster = (int)(sizeof ROSTER / sizeof ROSTER[0]);
                    if (n_carry > MAX_CARRY_UNITS) n_carry = MAX_CARRY_UNITS;
                    for (int i = 0; i < n_carry; i++) {
                        int t = data_find_unit_type(&a->game, ROSTER[i % n_roster]);
                        if (t < 0) continue;
                        a->cps.carry[a->cps.n_carry].type = (uint8_t)t;
                        a->cps.carry[a->cps.n_carry].exp =
                            (uint8_t)((i % 5 == 0) ? 100 : (i * 17) % 100);
                        a->cps.n_carry++;
                    }
                    a->cps.funds_carry = 6000;
                }
                a->next_screen = SCREEN_BRIEFING;
            }
        }
        else if (!strcmp(argv[2], "story")) {
            /* 幕間の確認用: --screen story [ノードID] */
            char path[600], err[256];
            snprintf(path, sizeof path, "%sdata/campaign/main.cpn", a->base_path);
            if (campaign_load(&a->cpn, path, err, sizeof err) == 0) {
                memset(&a->cps, 0, sizeof a->cps);
                a->cps.active = true;
                a->campaign_mode = true;
                snprintf(a->cps.node, sizeof a->cps.node, "%s",
                         (argc >= 4) ? argv[3] : a->cpn.start);
                /* 第3引数に win を足すと勝利直後の幕間を出す */
                a->story_is_win = (argc >= 5 && !strcmp(argv[4], "win"));
                a->next_screen = SCREEN_STORY;
            }
        }
        else if (!strcmp(argv[2], "cpnmap")) {
            /* 作戦全体図の確認用: --screen cpnmap [現在ノードID] */
            char path[600], err[256];
            snprintf(path, sizeof path, "%sdata/campaign/main.cpn", a->base_path);
            if (campaign_load(&a->cpn, path, err, sizeof err) == 0) {
                memset(&a->cps, 0, sizeof a->cps);
                a->cps.active = true;
                a->campaign_mode = true;
                snprintf(a->cps.node, sizeof a->cps.node, "%s",
                         (argc >= 4) ? argv[3] : a->cpn.start);
                a->cps.cleared = 0x3f;    /* 途中まで制圧済みの見た目にする */
                a->next_screen = SCREEN_CPNMAP;
            }
        }
        else if (!strcmp(argv[2], "reward")) {
            /* キャンペーンを読み込み、先頭ノードのご褒美画面を表示 */
            char path[600], err[256];
            snprintf(path, sizeof path, "%sdata/campaign/main.cpn", a->base_path);
            if (campaign_load(&a->cpn, path, err, sizeof err) == 0) {
                memset(&a->cps, 0, sizeof a->cps);
                a->cps.active = true;
                snprintf(a->cps.node, sizeof a->cps.node, "%s", a->cpn.start);
                a->campaign_mode = true;
                a->game.winner = 0;
                a->next_screen = SCREEN_REWARD;
            }
        }
        a->screen = a->next_screen;
    }
    a->sel_unit = -1;
    SCREENS[a->screen].enter(a);

    /* 中身のある画面を1枚用意してから見せる（黒画面のちらつきを避ける） */
    SDL_SetRenderDrawColor(a->ren, 0, 0, 0, 255);
    SDL_RenderClear(a->ren);
    SCREENS[a->screen].draw(a);
    SDL_RenderPresent(a->ren);
    /* 環境変数 HWSHOT にパスを入れて起動すると、最初の1フレームを
     * PNG に保存して即終了する。画面レイアウトの確認に使う。
     *   例) set HWSHOT=shot.png && hexwars.exe --screen result
     * HWSHOT_FRAME=N を付けると N フレーム後に撮る。
     * 幕間のタイプ表示のような「時間で変わる画面」の途中を見るため。 */
    if (getenv("HWSHOT") && !getenv("HWSHOT_FRAME"))
        save_shot(a);
    SDL_ShowWindow(a->win);
    SDL_RaiseWindow(a->win);

    while (!a->quit) {
        uint32_t frame_start = SDL_GetTicks();
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) a->quit = true;
            else SCREENS[a->screen].event(a, &e);
        }
        SCREENS[a->screen].update(a);

        if (a->next_screen != a->screen) {
            a->screen = a->next_screen;
            SCREENS[a->screen].enter(a);
        }

        SDL_SetRenderDrawColor(a->ren, 0, 0, 0, 255);
        SDL_RenderClear(a->ren);
        SCREENS[a->screen].draw(a);
        SDL_RenderPresent(a->ren);
        a->frame++;
        if (getenv("HWSHOT") && getenv("HWSHOT_FRAME") &&
            (int)a->frame >= atoi(getenv("HWSHOT_FRAME")))
            save_shot(a);
        /* vsync が効かない環境（ソフトウェア描画など）では毎フレーム全力で回って
         * しまい、ちらつきやCPU浪費の原因になるので約60fpsに抑える。 */
        uint32_t el = SDL_GetTicks() - frame_start;
        if (el < 16) SDL_Delay(16 - el);
    }

    sprites_quit();
    uanim_quit();
    snd_quit();
    assets_quit(a);
    TTF_Quit();
    SDL_DestroyRenderer(a->ren);
    SDL_DestroyWindow(a->win);
    SDL_Quit();
    return 0;
}
