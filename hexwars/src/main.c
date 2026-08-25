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

    a->screen = SCREEN_TITLE;
    a->next_screen = SCREEN_TITLE;
    /* デバッグ用: --screen options 等で任意画面から起動 */
    if (argc >= 3 && strcmp(argv[1], "--screen") == 0) {
        if      (!strcmp(argv[2], "options")) a->next_screen = SCREEN_OPTIONS;
        else if (!strcmp(argv[2], "setup"))   a->next_screen = SCREEN_SETUP;
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
     *   例) set HWSHOT=shot.png && hexwars.exe --screen result   */
    if (getenv("HWSHOT")) {
        SDL_Surface *sf = SDL_CreateRGBSurfaceWithFormat(0, WIN_W, WIN_H, 32,
                                                         SDL_PIXELFORMAT_RGBA32);
        if (sf && SDL_RenderReadPixels(a->ren, NULL, SDL_PIXELFORMAT_RGBA32,
                                       sf->pixels, sf->pitch) == 0)
            IMG_SavePNG(sf, getenv("HWSHOT"));
        if (sf) SDL_FreeSurface(sf);
        a->quit = true;
    }
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
