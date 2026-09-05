/* screens.c - タイトル / セットアップ / オプション / ロード / ブリーフィング /
 *             キャンペーン全体マップ / 結果 */
#include "app.h"
#include "text.h"
#include "sound.h"
#include "sprites.h"
#include "anim.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

const char *faction_name(int p)
{
    static const char *K[MAX_PLAYERS] = {
        "FACTION0", "FACTION1", "FACTION2", "FACTION3", "FACTION4"
    };
    if (p < 0 || p >= MAX_PLAYERS) return tx("FACTION1");
    return tx(K[p]);
}

/* battle 画面（screen_battle.c） */
void battle_enter(App *a);
void battle_event(App *a, const SDL_Event *e);
void battle_update(App *a);
void battle_draw(App *a);

/* ------------------------------------------------------------------ */
/* 共通: セーブパス・オプション永続化                                  */
/* ------------------------------------------------------------------ */
void ui_save_path(const App *a, int slot, char *out, int outlen)
{
    snprintf(out, (size_t)outlen, "%ssaves/slot%02d.sav", a->base_path, slot);
}

void options_load(App *a)
{
    /* 音量カーブの版。0/未記載=旧(線形)。新カーブ(2乗)へ一度だけ換算する */
    int curve = 0;
    /* 既定音量は控えめに。2乗カーブと合わせて、初回起動でうるさくならないようにする */
    a->opt_bgm = 3;
    a->opt_se = 4;
    a->opt_bgm_track = -1;   /* 戦闘BGMはマップごとに自動 */
    a->opt_se_set = 0;
    a->opt_anim = 1;
    a->opt_anim_video = 0;   /* 既定OFF（同梱の動画がまだ無いため） */
    a->opt_tilt = 1;         /* 既定は斜め見下ろし。平面に戻せる */
    a->opt_cutin = 1;        /* 既定は毎回。設定で「撃破時のみ」「出さない」に変えられる */
    a->opt_weather_fx = 1;   /* 既定はON。雨の雨脚が邃しければ切れる */
    char path[600];
    snprintf(path, sizeof path, "%soptions.cfg", a->base_path);
    FILE *f = fopen(path, "rb");
    if (f) {
        char line[128];
        while (fgets(line, sizeof line, f)) {
            char *key, *val;
            if (parser_split_line(line, &key, &val) != 1) continue;
            if      (!strcmp(key, "bgm"))  a->opt_bgm = atoi(val);
            else if (!strcmp(key, "se"))   a->opt_se = atoi(val);
            else if (!strcmp(key, "bgm_track")) a->opt_bgm_track = atoi(val);
            else if (!strcmp(key, "se_set"))    a->opt_se_set = atoi(val);
            else if (!strcmp(key, "volcurve"))  curve = atoi(val);
            else if (!strcmp(key, "anim")) a->opt_anim = atoi(val);
            else if (!strcmp(key, "anim_video")) a->opt_anim_video = atoi(val);
            else if (!strcmp(key, "tilt")) a->opt_tilt = atoi(val) ? 1 : 0;
            else if (!strcmp(key, "cutin")) a->opt_cutin = atoi(val);
            else if (!strcmp(key, "weather_fx")) a->opt_weather_fx = atoi(val) ? 1 : 0;
        }
        fclose(f);
    }
    if (a->opt_bgm < 0) a->opt_bgm = 0;
    if (a->opt_bgm > 10) a->opt_bgm = 10;
    if (a->opt_cutin < 0 || a->opt_cutin > 2) a->opt_cutin = 1;
    if (a->opt_se < 0) a->opt_se = 0;
    if (a->opt_se > 10) a->opt_se = 10;
    /* 旧設定の引き継ぎ: 線形10段階での聞こえ方を2乗カーブでも保つ。
     * 例) 旧1(=10%) → 新3(=9%)。これをしないと既存の設定が極端に小さくなる。 */
    if (curve < 2) {
        int b = a->opt_bgm, e = a->opt_se;
        a->opt_bgm = (int)(sqrt((double)b * 10.0) + 0.5);
        a->opt_se  = (int)(sqrt((double)e * 10.0) + 0.5);
        if (a->opt_bgm > 10) a->opt_bgm = 10;
        if (a->opt_se  > 10) a->opt_se  = 10;
    }
    /* audio.def の曲数・セット数は編集で変わりうるので、実数で丸める */
    if (a->opt_bgm_track < -1) a->opt_bgm_track = -1;
    if (a->opt_bgm_track >= snd_battle_track_count()) a->opt_bgm_track = -1;
    if (a->opt_se_set < 0 || a->opt_se_set >= snd_se_set_count()) a->opt_se_set = 0;
    if (a->opt_cutin < 0 || a->opt_cutin > 2) a->opt_cutin = 1;
    snd_apply_volumes(a->opt_bgm, a->opt_se);
    snd_set_se_set(a->opt_se_set);
    if (curve < 2) options_save(a);   /* 換算結果を書き戻して二重換算を防ぐ */
}

void options_save(App *a)
{
    char path[600];
    snprintf(path, sizeof path, "%soptions.cfg", a->base_path);
    FILE *f = fopen(path, "wb");
    if (!f) return;
    /* cutin は読み込みだけで書き出していなかったため、起動のたびに既定へ戻っていた */
    fprintf(f, "bgm = %d\nse = %d\nanim = %d\nanim_video = %d\nbgm_track = %d\n"
               "se_set = %d\ntilt = %d\ncutin = %d\nweather_fx = %d\nvolcurve = 2\n",
            a->opt_bgm, a->opt_se, a->opt_anim, a->opt_anim_video,
            a->opt_bgm_track, a->opt_se_set, a->opt_tilt,
            a->opt_cutin, a->opt_weather_fx);
    fclose(f);
}

/* ------------------------------------------------------------------ */
/* 進捗（クリア数・最高ランク）— 指揮官の解禁に使う                    */
/* ------------------------------------------------------------------ */
/* クリア済みの作戦「種類数」（解禁の基準）。同じ作戦を何度クリアしても増えない */
int progress_count_cleared(const App *a)
{
    int n = 0;
    for (int i = 0; i < MAX_CAMPAIGN_MAPS; i++)
        if (a->progress_best[i] != RANK_NONE) n++;
    return n;
}

void progress_load(App *a)
{
    a->progress_clears = 0;
    for (int i = 0; i < MAX_CAMPAIGN_MAPS; i++) a->progress_best[i] = RANK_NONE;

    char path[600];
    snprintf(path, sizeof path, "%sprogress.cfg", a->base_path);
    FILE *f = fopen(path, "rb");
    if (!f) return;
    char line[128];
    while (fgets(line, sizeof line, f)) {
        char *key, *val;
        if (parser_split_line(line, &key, &val) != 1) continue;
        if (!strcmp(key, "clears")) a->progress_clears = atoi(val);
        else if (!strncmp(key, "rank", 4)) {
            int i = atoi(key + 4);
            if (i >= 0 && i < MAX_CAMPAIGN_MAPS) a->progress_best[i] = atoi(val);
        }
    }
    fclose(f);
    /* clears は記録済みランクから導出する（ファイルの値は参考情報）。
     * こうしておくと古い延べ回数の記録もそのまま整合する。 */
    a->progress_clears = progress_count_cleared(a);
}

void progress_save(App *a)
{
    char path[600];
    snprintf(path, sizeof path, "%sprogress.cfg", a->base_path);
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "# 進捗（指揮官の解禁に使用）\nclears = %d\n",
            a->progress_clears);
    for (int i = 0; i < MAX_CAMPAIGN_MAPS; i++)
        if (a->progress_best[i] != RANK_NONE)
            fprintf(f, "rank%d = %d\n", i, a->progress_best[i]);
    fclose(f);
}

bool co_is_unlocked(const App *a, int idx)
{
    if (idx < 0 || idx >= a->game.n_cos) return false;
    return a->game.cos[idx].unlock_clears <= a->progress_clears;
}

int co_next_unlocked(const App *a, int from, int dir)
{
    int n = a->game.n_cos;
    if (n <= 0) return from;
    if (dir == 0) dir = 1;
    for (int k = 1; k <= n; k++) {
        int i = ((from + dir * k) % n + n) % n;
        if (co_is_unlocked(a, i)) return i;
    }
    return from;
}

/* ------------------------------------------------------------------ */
/* タイトル                                                            */
/* ------------------------------------------------------------------ */
static const char *TITLE_KEYS[] = {
    "TITLE_CAMPAIGN", "TITLE_FREE", "TITLE_LOAD", "TITLE_OPTIONS", "TITLE_QUIT"
};
#define N_TITLE_ITEMS 5

static void title_enter(App *a)
{
    a->menu_idx = 0;
    a->campaign_mode = false;
    snd_music(HWM_TITLE, true);
}

static SDL_Rect title_item_rect(int i)
{
    SDL_Rect r = { WIN_W / 2 - 140, 380 + i * 58, 280, 46 };
    return r;
}

static void start_campaign(App *a)
{
    char path[600], err[256];
    snprintf(path, sizeof path, "%sdata/campaign/main.cpn", a->base_path);
    if (campaign_load(&a->cpn, path, err, sizeof err) != 0) {
        SDL_Log("キャンペーン読込失敗: %s", err);
        return;
    }
    memset(&a->cps, 0, sizeof a->cps);
    a->cps.active = true;
    snprintf(a->cps.file, sizeof a->cps.file, "campaign/main.cpn");
    snprintf(a->cps.node, sizeof a->cps.node, "%s", a->cpn.start);
    a->campaign_mode = true;
    a->next_screen = SCREEN_CPNMAP;
}

static void title_select(App *a)
{
    snd_se(SE_OK);
    switch (a->menu_idx) {
    case 0: start_campaign(a); break;
    case 1: a->next_screen = SCREEN_SETUP; break;
    case 2: a->next_screen = SCREEN_LOAD; break;
    case 3: a->next_screen = SCREEN_OPTIONS; break;
    default: a->quit = true; break;
    }
}

static void title_event(App *a, const SDL_Event *e)
{
    if (e->type == SDL_KEYDOWN) {
        SDL_Keycode k = e->key.keysym.sym;
        if (k == SDLK_UP) {
            a->menu_idx = (a->menu_idx + N_TITLE_ITEMS - 1) % N_TITLE_ITEMS;
            snd_se(SE_CURSOR);
        }
        if (k == SDLK_DOWN) {
            a->menu_idx = (a->menu_idx + 1) % N_TITLE_ITEMS;
            snd_se(SE_CURSOR);
        }
        if (k == SDLK_z || k == SDLK_RETURN) title_select(a);
    }
    if (e->type == SDL_MOUSEMOTION) {
        for (int i = 0; i < N_TITLE_ITEMS; i++) {
            SDL_Rect r = title_item_rect(i);
            SDL_Point p = { e->motion.x, e->motion.y };
            if (SDL_PointInRect(&p, &r)) a->menu_idx = i;
        }
    }
    if (e->type == SDL_MOUSEBUTTONDOWN && e->button.button == SDL_BUTTON_LEFT) {
        for (int i = 0; i < N_TITLE_ITEMS; i++) {
            SDL_Rect r = title_item_rect(i);
            SDL_Point p = { e->button.x, e->button.y };
            if (SDL_PointInRect(&p, &r)) { a->menu_idx = i; title_select(a); }
        }
    }
}

static void title_update(App *a) { (void)a; }

static void title_draw(App *a)
{
    fill_rect(a, 0, 0, WIN_W, WIN_H, (SDL_Color){ 32, 42, 52, 255 });

    for (int i = 0; i < 7; i++) {
        float cx = 160.0f + i * 165.0f;
        SDL_Color c = (i % 2) ? COL_P[1] : COL_P[0];
        c.a = 40;
        render_fill_hex(a, cx, 140.0f + (i % 3) * 30.0f, 56.0f, c);
    }

    draw_text_center(a, a->font_xl, WIN_W / 2, 200, COL_WHITE, "HEX WARS");
    draw_text_center(a, a->font_m, WIN_W / 2, 270, COL_GRAY, tx("TITLE_SUB1"));
    draw_text_center(a, a->font_s, WIN_W / 2, 310, COL_DIM, tx("TITLE_SUB2"));

    for (int i = 0; i < N_TITLE_ITEMS; i++) {
        SDL_Rect r = title_item_rect(i);
        bool sel = (a->menu_idx == i);
        fill_rect(a, r.x, r.y, r.w, r.h,
                  sel ? (SDL_Color){ 70, 100, 150, 255 }
                      : (SDL_Color){ 45, 55, 68, 255 });
        outline_rect(a, r.x, r.y, r.w, r.h, sel ? COL_YELLOW : COL_DIM);
        draw_text_center(a, a->font_l, r.x + r.w / 2, r.y + 7,
                         sel ? COL_WHITE : COL_GRAY, tx(TITLE_KEYS[i]));
    }
    draw_text_center(a, a->font_s, WIN_W / 2, WIN_H - 40, COL_DIM, tx("TITLE_HINT"));
}

/* ------------------------------------------------------------------ */
/* フリー対戦セットアップ                                              */
/* ------------------------------------------------------------------ */
static const char *P2_KEYS[] = {
    "OPP_CPU_EASY", "OPP_CPU_NORMAL", "OPP_CPU_HARD", "OPP_HUMAN"
};
#define SETUP_CTRL_HUMAN 3      /* P2_KEYS の「人間」 */

/* 行は「マップ・索敵・(参加陣営ごとの操作者)・(参加陣営ごとの指揮官)・開始・戻る」。
 * 参加陣営数はマップで変わるので行数も変わる。 */
#define SETUP_FIXED_TOP  2      /* マップ・索敵 */
#define SETUP_FIXED_BOT  2      /* 開始・戻る */

/* .map を軽く読んで参加陣営のビットを返す。
 * 本読込（data_load_map）は Game 一式を要するので、選択中マップの下見には重い。
 * own= と unit= の所有者だけ見れば参加陣営は分かる。 */
static unsigned map_participants(const char *path)
{
    unsigned bits = 0;
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    char line[512];
    while (fgets(line, sizeof line, f)) {
        char *hash = strchr(line, '#');
        if (hash) *hash = '\0';
        char *s = line;
        while (*s == ' ' || *s == '\t') s++;
        int o = -1;
        if (!strncmp(s, "own", 3)) {
            char *eq = strchr(s, '=');
            if (eq) { int x, y; if (sscanf(eq + 1, "%d,%d,%d", &x, &y, &o) != 3) o = -1; }
        } else if (!strncmp(s, "unit", 4)) {
            char *eq = strchr(s, '=');
            if (eq) o = atoi(eq + 1);
        }
        if (o >= 0 && o < MAX_PLAYERS) bits |= 1u << o;
    }
    fclose(f);
    return bits;
}

static int setup_nparts(const App *a)
{
    int n = 0;
    for (int p = 0; p < MAX_PLAYERS; p++)
        if (a->setup_parts & (1u << p)) n++;
    return n < 2 ? 2 : n;          /* 最低2陣営として扱う（読めなかった場合の保険） */
}

/* i 番目の参加陣営の陣営番号 */
static int setup_part_at(const App *a, int i)
{
    for (int p = 0; p < MAX_PLAYERS; p++)
        if (a->setup_parts & (1u << p)) {
            if (i == 0) return p;
            i--;
        }
    return i + 1 < MAX_PLAYERS ? i + 1 : 1;   /* 保険 */
}

static int setup_rows(const App *a)
{
    return SETUP_FIXED_TOP + setup_nparts(a) * 2 + SETUP_FIXED_BOT;
}
static int setup_start_row(const App *a) { return setup_rows(a) - 2; }
static int setup_back_row(const App *a)  { return setup_rows(a) - 1; }

/* 選択中マップの参加陣営を読み直す（マップを変えたら呼ぶ） */
static void setup_refresh_parts(App *a)
{
    a->setup_parts = 0;
    if (a->maps.n > 0) {
        char path[600];
        snprintf(path, sizeof path, "%sdata/%s", a->base_path,
                 a->maps.file[a->sel_map]);
        a->setup_parts = map_participants(path);
    }
    if (a->setup_parts == 0) a->setup_parts = 0x3;   /* 読めなければ2陣営扱い */
    /* 陣営0は人間、それ以外はCPU普通を既定にする */
    for (int p = 0; p < MAX_PLAYERS; p++)
        if (a->sel_ctrl[p] > SETUP_CTRL_HUMAN) a->sel_ctrl[p] = 1;
    if (a->setup_row >= setup_rows(a)) a->setup_row = 0;
}

static void setup_enter(App *a)
{
    a->setup_row = 0;
    /* 旧フィールドから引き継ぐ（初回や他画面から戻ってきたとき） */
    a->sel_ctrl[0] = SETUP_CTRL_HUMAN;
    if (a->sel_ctrl[1] > SETUP_CTRL_HUMAN) a->sel_ctrl[1] = (uint8_t)a->sel_p2;
    for (int p = 0; p < MAX_PLAYERS; p++)
        if (a->sel_co[p] < 0 || a->sel_co[p] >= a->game.n_cos) a->sel_co[p] = a->sel_co0;
    setup_refresh_parts(a);
}

static SDL_Rect setup_row_rect(const App *a, int i)
{
    int n = setup_rows(a);
    int gap = (n <= 8) ? 62 : 46;
    int h   = (n <= 8) ? 48 : 38;
    int top = (n <= 8) ? 210 : 158;
    SDL_Rect r = { WIN_W / 2 - 320, top + i * gap, 640, h };
    return r;
}

/* 指揮官名（未読込なら「なし」） */
/* 指揮官の顔絵を (x,y,w,h) の枠に、縦横比を保って収めて描く。
 * 画像が無い/読めない指揮官は枠だけ出して名前を添える（データ未整備でも崩れない）。 */
static void draw_co_portrait(App *a, int ci, int x, int y, int w, int h)
{
    fill_rect(a, x, y, w, h, (SDL_Color){ 26, 32, 42, 255 });
    if (ci < 0 || ci >= a->game.n_cos) {
        outline_rect(a, x, y, w, h, COL_DIM);
        draw_text_center(a, a->font_s, x + w / 2, y + h / 2 - 8, COL_DIM,
                         tx("CO_NONE"));
        return;
    }
    const CommanderType *c = &a->game.cos[ci];
    int iw = 0, ih = 0;
    SDL_Texture *tex = sprite_get_path(a, c->image, &iw, &ih);
    if (tex && iw > 0 && ih > 0) {
        float k = (float)w / (float)iw;
        float k2 = (float)h / (float)ih;
        if (k2 < k) k = k2;                     /* 収まるほうに合わせる */
        float dw = iw * k, dh = ih * k;
        SDL_FRect dst = { x + (w - dw) / 2.0f, y + (h - dh) / 2.0f, dw, dh };
        SDL_RenderCopyF(a->ren, tex, NULL, &dst);
    } else {
        draw_text_center(a, a->font_s, x + w / 2, y + h / 2 - 8, COL_DIM,
                         c->name);
    }
    outline_rect(a, x, y, w, h, COL_DIM);
}

static const char *co_label(App *a, int idx)
{
    if (a->game.n_cos <= 0 || idx < 0 || idx >= a->game.n_cos)
        return tx("CO_NONE");
    return a->game.cos[idx].name;
}

static void setup_change(App *a, int dir)
{
    snd_se(SE_CURSOR);
    int n = setup_nparts(a);
    if (a->setup_row == 0) {
        a->sel_map = (a->sel_map + a->maps.n + dir) % (a->maps.n ? a->maps.n : 1);
        setup_refresh_parts(a);          /* マップで参加陣営数が変わる */
        return;
    }
    if (a->setup_row == 1) {
        a->sel_fog = (a->sel_fog + 2 + dir) % 2;
        return;
    }
    int r = a->setup_row - SETUP_FIXED_TOP;
    if (r < n) {                          /* 操作者 */
        int p = setup_part_at(a, r);
        a->sel_ctrl[p] = (uint8_t)((a->sel_ctrl[p] + 4 + dir) % 4);
        if (p == 1) a->sel_p2 = a->sel_ctrl[1];   /* 旧フィールドを追随させる */
        return;
    }
    r -= n;
    if (r < n) {                          /* 指揮官 */
        int p = setup_part_at(a, r);
        a->sel_co[p] = co_next_unlocked(a, a->sel_co[p], dir);
        if (p == 0) a->sel_co0 = a->sel_co[0];
        if (p == 1) a->sel_co1 = a->sel_co[1];
    }
}

/* 人間の陣営が1つも無ければ開始できない（誰も操作できない対戦になるため） */
static bool setup_has_human(const App *a)
{
    int n = setup_nparts(a);
    for (int i = 0; i < n; i++)
        if (a->sel_ctrl[setup_part_at(a, i)] == SETUP_CTRL_HUMAN) return true;
    return false;
}

static void setup_start(App *a)
{
    Game *g = &a->game;
    char path[600], err[256];
    /* 人間が1つも無ければ開始しない（誰も操作できない対戦になるため）。
     * 理由は setup_draw が画面下に常時出している。 */
    if (!setup_has_human(a)) { snd_se(SE_CANCEL); return; }
    snprintf(path, sizeof path, "%sdata/%s", a->base_path,
             a->maps.file[a->sel_map]);
    if (data_load_map(g, path, err, sizeof err) != 0) {
        SDL_Log("マップ読込失敗: %s", err);
        return;
    }
    a->campaign_mode = false;
    a->cps.active = false;
    g->fog = (a->sel_fog == 0);
    /* data_load_map が全陣営をCPU普通で埋めているので、選んだぶんだけ上書きする */
    int n = setup_nparts(a);
    for (int i = 0; i < n; i++) {
        int p = setup_part_at(a, i);
        g->ctrl[p] = (a->sel_ctrl[p] == 0) ? CTRL_CPU_EASY
                   : (a->sel_ctrl[p] == 1) ? CTRL_CPU_NORMAL
                   : (a->sel_ctrl[p] == 2) ? CTRL_CPU_HARD
                   : CTRL_HUMAN;
        g->co_id[p] = (int8_t)(g->n_cos > 0 ? a->sel_co[p] : -1);
    }
    game_start(g, SDL_GetTicks() | 1u);
    a->next_screen = SCREEN_BATTLE;
}

static void setup_select(App *a)
{
    snd_se(SE_OK);
    if (a->setup_row == setup_start_row(a)) setup_start(a);
    else if (a->setup_row == setup_back_row(a)) a->next_screen = SCREEN_TITLE;
    else setup_change(a, 1);
}

static void setup_update(App *a) { (void)a; }

static void setup_event(App *a, const SDL_Event *e)
{
    int rows = setup_rows(a);
    if (e->type == SDL_KEYDOWN) {
        SDL_Keycode k = e->key.keysym.sym;
        if (k == SDLK_UP) {
            a->setup_row = (a->setup_row + rows - 1) % rows;
            snd_se(SE_CURSOR);
        }
        if (k == SDLK_DOWN) {
            a->setup_row = (a->setup_row + 1) % rows;
            snd_se(SE_CURSOR);
        }
        if (k == SDLK_LEFT)  setup_change(a, -1);
        if (k == SDLK_RIGHT) setup_change(a, 1);
        if (k == SDLK_z || k == SDLK_RETURN) setup_select(a);
        if (k == SDLK_x || k == SDLK_ESCAPE) {
            snd_se(SE_CANCEL);
            a->next_screen = SCREEN_TITLE;
        }
    }
    if (e->type == SDL_MOUSEMOTION) {
        for (int i = 0; i < rows; i++) {
            SDL_Rect r = setup_row_rect(a, i);
            SDL_Point p = { e->motion.x, e->motion.y };
            if (SDL_PointInRect(&p, &r)) a->setup_row = i;
        }
    }
    if (e->type == SDL_MOUSEBUTTONDOWN) {
        SDL_Point p = { e->button.x, e->button.y };
        if (e->button.button == SDL_BUTTON_RIGHT) {
            snd_se(SE_CANCEL);
            a->next_screen = SCREEN_TITLE;
            return;
        }
        for (int i = 0; i < rows; i++) {
            SDL_Rect r = setup_row_rect(a, i);
            if (SDL_PointInRect(&p, &r)) { a->setup_row = i; setup_select(a); }
        }
    }
}

static void setup_draw(App *a)
{
    fill_rect(a, 0, 0, WIN_W, WIN_H, (SDL_Color){ 32, 42, 52, 255 });
    draw_text_center(a, a->font_xl, WIN_W / 2, 100, COL_WHITE, tx("SETUP_TITLE"));

    int n = setup_nparts(a);
    int rows = setup_rows(a);
    bool small = (rows > 8);
    TTF_Font *f = small ? a->font_s : a->font_m;

    for (int i = 0; i < rows; i++) {
        SDL_Rect r = setup_row_rect(a, i);
        bool sel = (a->setup_row == i);
        fill_rect(a, r.x, r.y, r.w, r.h,
                  sel ? (SDL_Color){ 70, 100, 150, 255 }
                      : (SDL_Color){ 45, 55, 68, 255 });
        outline_rect(a, r.x, r.y, r.w, r.h, sel ? COL_YELLOW : COL_DIM);

        int ty = r.y + (small ? 8 : 10);
        if (i == setup_start_row(a) || i == setup_back_row(a)) {
            const char *lbl = (i == setup_start_row(a)) ? tx("SETUP_START")
                                                        : tx("SETUP_BACK");
            SDL_Color c = sel ? COL_WHITE : COL_GRAY;
            /* 人間が0なら開始は押せないと分かるように暗く出す */
            if (i == setup_start_row(a) && !setup_has_human(a)) c = COL_DIM;
            draw_text_center(a, f, r.x + r.w / 2, ty, c, lbl);
            continue;
        }

        char lbuf[96], vbuf[96];
        const char *label = lbuf;
        const char *val = "";
        lbuf[0] = 0;
        if (i == 0) {
            snprintf(lbuf, sizeof lbuf, "%s", tx("SETUP_MAP"));
            val = a->maps.n ? a->maps.name[a->sel_map] : tx("SETUP_NOMAP");
        } else if (i == 1) {
            snprintf(lbuf, sizeof lbuf, "%s", tx("SETUP_FOG"));
            val = tx(a->sel_fog == 0 ? "ON" : "OFF");
        } else {
            int rr = i - SETUP_FIXED_TOP;
            if (rr < n) {
                int p = setup_part_at(a, rr);
                snprintf(lbuf, sizeof lbuf, tx("SETUP_CTRL_FMT"), faction_name(p));
                val = tx(P2_KEYS[a->sel_ctrl[p]]);
            } else {
                int p = setup_part_at(a, rr - n);
                snprintf(lbuf, sizeof lbuf, tx("SETUP_CO_FMT"), faction_name(p));
                val = co_label(a, a->sel_co[p]);
            }
        }
        /* 陣営の行はその陣営の色でラベルを出す（対応が一目で分かる） */
        SDL_Color lc = sel ? COL_WHITE : COL_GRAY;
        if (i >= SETUP_FIXED_TOP) {
            int rr = i - SETUP_FIXED_TOP;
            int p = setup_part_at(a, rr < n ? rr : rr - n);
            lc = COL_P[p];
        }
        draw_text(a, f, r.x + 20, ty, lc, label);
        snprintf(vbuf, sizeof vbuf, "◀ %s ▶", val);
        draw_text(a, f, r.x + 300, ty, sel ? COL_YELLOW : COL_WHITE, vbuf);
    }

    /* 指揮官の行を選んでいるときだけ、その陣営の顔絵を出す */
    if (a->game.n_cos > 0) {
        int rr = a->setup_row - SETUP_FIXED_TOP;
        if (rr >= n && rr < n * 2) {
            int p = setup_part_at(a, rr - n);
            draw_co_portrait(a, a->sel_co[p], 985, 246, 264, 352);
            char buf[64];
            snprintf(buf, sizeof buf, tx("SETUP_CO_FMT"), faction_name(p));
            draw_text_center(a, a->font_s, 985 + 132, 214, COL_GRAY, buf);
        }
    }
    if (!setup_has_human(a))
        draw_text_center(a, a->font_s, WIN_W / 2, WIN_H - 60,
                         (SDL_Color){ 230, 150, 150, 255 }, tx("SETUP_NEED_HUMAN"));
    draw_text_center(a, a->font_s, WIN_W / 2, WIN_H - 34, COL_DIM, tx("SETUP_HINT"));
}

/* ------------------------------------------------------------------ */
/* オプション画面（仕様書 10章: 音量、8.2: 戦闘アニメOFF）             */
/* ------------------------------------------------------------------ */
#define OPT_ROWS 10
#define OPT_BACK_ROW (OPT_ROWS - 1)
#define OPT_TILT_ROW 6
#define OPT_CUTIN_ROW 7
#define OPT_WXFX_ROW 8

static void opt_enter(App *a) { a->opt_row = 0; }

/* 戦闘BGMを試聴したまま画面を出るとタイトルで戦闘曲が鳴り続けるので戻す */
static void opt_leave(App *a)
{
    options_save(a);
    snd_music(HWM_TITLE, true);
}

static SDL_Rect opt_row_rect(int i)
{
    SDL_Rect r = { WIN_W / 2 - 300, 196 + i * 54, 600, 44 };
    return r;
}

static void opt_change(App *a, int dir)
{
    switch (a->opt_row) {
    case 0:
        a->opt_bgm += dir;
        if (a->opt_bgm < 0) a->opt_bgm = 0;
        if (a->opt_bgm > 10) a->opt_bgm = 10;
        break;
    case 1:
        a->opt_se += dir;
        if (a->opt_se < 0) a->opt_se = 0;
        if (a->opt_se > 10) a->opt_se = 10;
        break;
    case 2:
        a->opt_anim = !a->opt_anim;
        break;
    case 3:
        a->opt_anim_video = !a->opt_anim_video;
        break;
    case 4: {
        /* 戦闘BGM: -1(自動) → 0 → 1 → ... → 最後 → -1 と巡回。曲数は audio.def 次第 */
        int n = snd_battle_track_count();
        if (n <= 0) return;
        int v = a->opt_bgm_track + dir;
        if (v < -1) v = n - 1;
        if (v >= n) v = -1;
        a->opt_bgm_track = v;
        break;
    }
    case 5: {
        int n = snd_se_set_count();
        if (n <= 0) return;
        int v = (a->opt_se_set + dir) % n;
        if (v < 0) v += n;
        a->opt_se_set = v;
        snd_set_se_set(a->opt_se_set);
        break;
    }
    case OPT_TILT_ROW:
        a->opt_tilt = !a->opt_tilt;   /* 斜め見下ろし ⇔ 平面（見た目のみ） */
        break;
    case OPT_CUTIN_ROW: {
        /* 出さない → 毎回 → 撃破時のみ → …（左右で巡回） */
        int v = (a->opt_cutin + dir) % 3;
        if (v < 0) v += 3;
        a->opt_cutin = v;
        break;
    }
    case OPT_WXFX_ROW:
        a->opt_weather_fx = !a->opt_weather_fx;   /* 天候の演出（見た目のみ） */
        break;
    default:
        return;
    }
    snd_apply_volumes(a->opt_bgm, a->opt_se);
    /* 選んだ音をその場で確かめられるように鳴らす */
    if (a->opt_row == 4) {
        int m = snd_battle_music(a->opt_bgm_track, "");
        if (m != HWM_NONE) snd_music(m, true);
    } else {
        snd_se(a->opt_row == 5 ? SE_OK : SE_CURSOR);
    }
    options_save(a);
}

static void opt_select(App *a)
{
    if (a->opt_row == OPT_BACK_ROW) {
        snd_se(SE_CANCEL);
        opt_leave(a);
        a->next_screen = SCREEN_TITLE;
    } else {
        opt_change(a, 1);
    }
}

static void opt_event(App *a, const SDL_Event *e)
{
    if (e->type == SDL_KEYDOWN) {
        SDL_Keycode k = e->key.keysym.sym;
        if (k == SDLK_UP) {
            a->opt_row = (a->opt_row + OPT_ROWS - 1) % OPT_ROWS;
            snd_se(SE_CURSOR);
        }
        if (k == SDLK_DOWN) {
            a->opt_row = (a->opt_row + 1) % OPT_ROWS;
            snd_se(SE_CURSOR);
        }
        if (k == SDLK_LEFT)  opt_change(a, -1);
        if (k == SDLK_RIGHT) opt_change(a, 1);
        if (k == SDLK_z || k == SDLK_RETURN) opt_select(a);
        if (k == SDLK_x || k == SDLK_ESCAPE) {
            snd_se(SE_CANCEL);
            opt_leave(a);
            a->next_screen = SCREEN_TITLE;
        }
    }
    if (e->type == SDL_MOUSEMOTION) {
        for (int i = 0; i < OPT_ROWS; i++) {
            SDL_Rect r = opt_row_rect(i);
            SDL_Point p = { e->motion.x, e->motion.y };
            if (SDL_PointInRect(&p, &r)) a->opt_row = i;
        }
    }
    if (e->type == SDL_MOUSEBUTTONDOWN) {
        if (e->button.button == SDL_BUTTON_RIGHT) {
            snd_se(SE_CANCEL);
            opt_leave(a);
            a->next_screen = SCREEN_TITLE;
            return;
        }
        SDL_Point p = { e->button.x, e->button.y };
        for (int i = 0; i < OPT_ROWS; i++) {
            SDL_Rect r = opt_row_rect(i);
            if (SDL_PointInRect(&p, &r)) { a->opt_row = i; opt_select(a); }
        }
    }
}

static void opt_update(App *a) { (void)a; }

static void draw_volume_bar(App *a, int x, int y, int val)
{
    for (int i = 0; i < 10; i++) {
        SDL_Color c = i < val ? COL_YELLOW : (SDL_Color){ 60, 66, 76, 255 };
        fill_rect(a, x + i * 18, y, 14, 18, c);
    }
    char buf[8];
    snprintf(buf, sizeof buf, "%d", val);
    draw_text(a, a->font_m, x + 190, y - 4, COL_WHITE, buf);
}

static void opt_draw(App *a)
{
    fill_rect(a, 0, 0, WIN_W, WIN_H, (SDL_Color){ 32, 42, 52, 255 });
    draw_text_center(a, a->font_xl, WIN_W / 2, 100, COL_WHITE, tx("OPT_TITLE"));

    const char *labels[OPT_ROWS] = {
        tx("OPT_BGM"), tx("OPT_SE"), tx("OPT_ANIM"), tx("OPT_ANIM_VIDEO"),
        tx("OPT_BGM_TRACK"), tx("OPT_SE_SET"), tx("OPT_TILT"), tx("OPT_CUTIN"),
        tx("OPT_WEATHER_FX"), tx("OPT_BACK")
    };
    for (int i = 0; i < OPT_ROWS; i++) {
        SDL_Rect r = opt_row_rect(i);
        bool sel = (a->opt_row == i);
        fill_rect(a, r.x, r.y, r.w, r.h,
                  sel ? (SDL_Color){ 70, 100, 150, 255 }
                      : (SDL_Color){ 45, 55, 68, 255 });
        outline_rect(a, r.x, r.y, r.w, r.h, sel ? COL_YELLOW : COL_DIM);

        if (i == OPT_BACK_ROW) {
            draw_text_center(a, a->font_m, r.x + r.w / 2, r.y + 11,
                             sel ? COL_WHITE : COL_GRAY, labels[i]);
            continue;
        }
        draw_text(a, a->font_m, r.x + 20, r.y + 11,
                  sel ? COL_WHITE : COL_GRAY, labels[i]);
        if (i == 0) draw_volume_bar(a, r.x + 320, r.y + 15, a->opt_bgm);
        if (i == 1) draw_volume_bar(a, r.x + 320, r.y + 15, a->opt_se);
        if (i == 2)
            draw_text(a, a->font_m, r.x + 320, r.y + 11, COL_YELLOW,
                      tx(a->opt_anim ? "ON" : "OFF"));
        if (i == 3)
            draw_text(a, a->font_m, r.x + 320, r.y + 11,
                      a->opt_anim ? COL_YELLOW : COL_DIM,
                      tx(a->opt_anim_video ? "ON" : "OFF"));
        if (i == 4) {
            char buf[80];
            int n = snd_battle_track_count();
            if (n <= 0)
                snprintf(buf, sizeof buf, "%s", tx("OPT_TRACK_NONE"));
            else if (a->opt_bgm_track < 0)
                snprintf(buf, sizeof buf, "%s", tx("OPT_TRACK_AUTO"));
            else
                snprintf(buf, sizeof buf, tx("OPT_TRACK_FMT"),
                         a->opt_bgm_track + 1, n,
                         snd_battle_track_name(a->opt_bgm_track));
            draw_text(a, a->font_m, r.x + 320, r.y + 11, COL_YELLOW, buf);
        }
        if (i == 5)
            draw_text(a, a->font_m, r.x + 320, r.y + 11, COL_YELLOW,
                      snd_se_set_name(a->opt_se_set));
        if (i == OPT_WXFX_ROW)
            draw_text(a, a->font_m, r.x + 320, r.y + 11,
                      a->opt_weather_fx ? COL_YELLOW : COL_DIM,
                      tx(a->opt_weather_fx ? "OPT_WXFX_ON" : "OPT_WXFX_OFF"));
        if (i == OPT_TILT_ROW)
            draw_text(a, a->font_m, r.x + 320, r.y + 11, COL_YELLOW,
                      tx(a->opt_tilt ? "OPT_TILT_ON" : "OPT_TILT_OFF"));
        if (i == OPT_CUTIN_ROW) {
            static const char *K[3] = {
                "OPT_CUTIN_OFF", "OPT_CUTIN_ALWAYS", "OPT_CUTIN_KILL"
            };
            int v = a->opt_cutin;
            if (v < 0 || v > 2) v = 0;
            draw_text(a, a->font_m, r.x + 320, r.y + 11,
                      v ? COL_YELLOW : COL_DIM, tx(K[v]));
        }
    }
    draw_text_center(a, a->font_s, WIN_W / 2, WIN_H - 40, COL_DIM, tx("OPT_HINT"));
}

/* ------------------------------------------------------------------ */
/* ロード画面                                                          */
/* ------------------------------------------------------------------ */
static void load_enter(App *a)
{
    a->load_idx = 0;
}

static SDL_Rect load_row_rect(int i)
{
    SDL_Rect r = { WIN_W / 2 - 300, 130 + i * 50, 600, 44 };
    return r;
}

static void load_slot_label(App *a, int slot, char *out, int outlen)
{
    char path[600], name[64];
    int turn = -1;
    ui_save_path(a, slot, path, sizeof path);
    const char *tag = slot == 0 ? tx("LOAD_AUTO") : "";
    if (save_peek(path, name, sizeof name, &turn) == 0)
        snprintf(out, (size_t)outlen, tx("LOAD_SLOT_FMT"), slot, tag, name, turn);
    else
        snprintf(out, (size_t)outlen, tx("LOAD_EMPTY_FMT"), slot, tag);
}

static void load_select(App *a)
{
    char path[600], err[256];
    ui_save_path(a, a->load_idx, path, sizeof path);
    if (load_game(&a->game, &a->cps, path, err, sizeof err) != 0) {
        SDL_Log("ロード失敗: %s", err);
        snd_se(SE_CANCEL);
        return;
    }
    snd_se(SE_OK);
    a->campaign_mode = a->cps.active;
    if (a->campaign_mode) {
        char cpath[600];
        snprintf(cpath, sizeof cpath, "%sdata/%s", a->base_path, a->cps.file);
        if (campaign_load(&a->cpn, cpath, err, sizeof err) != 0) {
            SDL_Log("キャンペーン読込失敗: %s", err);
            a->campaign_mode = false;
        }
        a->cps_backup = a->cps;
    }
    a->next_screen = SCREEN_BATTLE;
}

static void load_event(App *a, const SDL_Event *e)
{
    if (e->type == SDL_KEYDOWN) {
        SDL_Keycode k = e->key.keysym.sym;
        if (k == SDLK_UP) {
            a->load_idx = (a->load_idx + SAVE_SLOTS - 1) % SAVE_SLOTS;
            snd_se(SE_CURSOR);
        }
        if (k == SDLK_DOWN) {
            a->load_idx = (a->load_idx + 1) % SAVE_SLOTS;
            snd_se(SE_CURSOR);
        }
        if (k == SDLK_z || k == SDLK_RETURN) load_select(a);
        if (k == SDLK_x || k == SDLK_ESCAPE) {
            snd_se(SE_CANCEL);
            a->next_screen = SCREEN_TITLE;
        }
    }
    if (e->type == SDL_MOUSEMOTION) {
        SDL_Point p = { e->motion.x, e->motion.y };
        for (int i = 0; i < SAVE_SLOTS; i++) {
            SDL_Rect r = load_row_rect(i);
            if (SDL_PointInRect(&p, &r)) a->load_idx = i;
        }
    }
    if (e->type == SDL_MOUSEBUTTONDOWN) {
        if (e->button.button == SDL_BUTTON_RIGHT) {
            snd_se(SE_CANCEL);
            a->next_screen = SCREEN_TITLE;
            return;
        }
        SDL_Point p = { e->button.x, e->button.y };
        for (int i = 0; i < SAVE_SLOTS; i++) {
            SDL_Rect r = load_row_rect(i);
            if (SDL_PointInRect(&p, &r)) { a->load_idx = i; load_select(a); }
        }
    }
}

static void load_update(App *a) { (void)a; }

static void load_draw(App *a)
{
    fill_rect(a, 0, 0, WIN_W, WIN_H, (SDL_Color){ 32, 42, 52, 255 });
    draw_text_center(a, a->font_l, WIN_W / 2, 60, COL_WHITE, tx("LOAD_TITLE"));
    for (int i = 0; i < SAVE_SLOTS; i++) {
        SDL_Rect r = load_row_rect(i);
        bool sel = a->load_idx == i;
        fill_rect(a, r.x, r.y, r.w, r.h,
                  sel ? (SDL_Color){ 70, 100, 150, 255 }
                      : (SDL_Color){ 45, 55, 68, 255 });
        outline_rect(a, r.x, r.y, r.w, r.h, sel ? COL_YELLOW : COL_DIM);
        char buf[128];
        load_slot_label(a, i, buf, sizeof buf);
        draw_text(a, a->font_m, r.x + 16, r.y + 8,
                  sel ? COL_WHITE : COL_GRAY, buf);
    }
    draw_text_center(a, a->font_s, WIN_W / 2, WIN_H - 36, COL_DIM, tx("LOAD_HINT"));
}

/* ------------------------------------------------------------------ */
/* キャンペーン全体マップ（仕様書 6.2: 進行状況の可視化）              */
/* ------------------------------------------------------------------ */
static int cpnmap_find_index(const Campaign *c, const char *id)
{
    for (int i = 0; i < c->n_nodes; i++)
        if (!strcmp(c->nodes[i].id, id)) return i;
    return -1;
}

/* 作戦を並べる帯。この外（上の題・下の情報欄）にはノードを描かない */
#define CPNMAP_TOP 136
#define CPNMAP_BOT (WIN_H - 176)

/* 並べる位置を「ファイルに書いた順」ではなく「作戦の進行順」にする。
 * 途中にノードを挿し込むと配置が進行順とずれ、接続線が画面を
 * 横断するような見た目になってしまうため。 */
static int cpnmap_slot(const Campaign *c, int i)
{
    char cur[32];
    snprintf(cur, sizeof cur, "%s", c->start);
    for (int slot = 0; slot < c->n_nodes; slot++) {
        int idx = cpnmap_find_index(c, cur);
        if (idx < 0) break;
        if (idx == i) return slot;
        snprintf(cur, sizeof cur, "%s", c->nodes[idx].next_win);
    }
    return c->n_nodes + i;   /* 本線に載らないノードは後ろへ */
}

/* スクロールを含まない素の位置。高さの計算にも使う */
static void cpnmap_node_pos(const Campaign *c, int i, int *x, int *y)
{
    /* 5列のジグザグ配置。作戦が増えると段が増えて縦に伸びる。
     * 段ごとに進行方向を反転させる（牛耕式）。全段を左→右にすると
     * 段の繋ぎ目が「右端→左端」になり、接続線が画面を横断してしまう。 */
    int slot = cpnmap_slot(c, i);
    int col = slot % 5, row = slot / 5;
    if (row % 2) col = 4 - col;
    *x = 160 + col * 240;
    *y = 240 + row * 260 + ((col % 2) ? 36 : 0);
}

/* 全作戦を収めるのに必要な高さと、スクロールできる上限 */
static int cpnmap_max_scroll(const Campaign *c)
{
    int bottom = 0;
    for (int i = 0; i < c->n_nodes; i++) {
        int x, y;
        cpnmap_node_pos(c, i, &x, &y);
        if (y + 80 > bottom) bottom = y + 80;   /* +80 = ヘクス下の作戦名まで */
    }
    int m = bottom - CPNMAP_BOT;
    return m > 0 ? m : 0;
}

static void cpnmap_clamp_scroll(App *a)
{
    int m = cpnmap_max_scroll(&a->cpn);
    if (a->cpn_scroll > m) a->cpn_scroll = m;
    if (a->cpn_scroll < 0) a->cpn_scroll = 0;
}

/* 画面上の位置（スクロール適用後） */
static void cpnmap_node_screen(const App *a, int i, int *x, int *y)
{
    cpnmap_node_pos(&a->cpn, i, x, y);
    *y -= a->cpn_scroll;
}

static void cpnmap_enter(App *a)
{
    /* 進行中の作戦が帯の真ん中に来るよう送る。
     * 作戦が増えて縦に伸びたとき、下の段が画面外になって見えなかった。 */
    int cur = cpnmap_find_index(&a->cpn, a->cps.node);
    a->cpn_scroll = 0;
    if (cur >= 0) {
        int x, y;
        cpnmap_node_pos(&a->cpn, cur, &x, &y);
        a->cpn_scroll = y - (CPNMAP_TOP + CPNMAP_BOT) / 2;
    }
    cpnmap_clamp_scroll(a);
    snd_music(HWM_TITLE, true);
}

/* 全体マップ下部の「指揮官」行。クリックでも切り替えられるようにする */
static SDL_Rect cpnmap_co_rect(void)
{
    SDL_Rect r = { WIN_W / 2 - 300, WIN_H - 118, 600, 48 };
    return r;
}

/* 解禁済みの次の指揮官へ切り替える */
static void cpnmap_cycle_co(App *a)
{
    if (a->game.n_cos <= 0) return;
    int cur = a->cps.player_co;
    if (cur < 0 || cur >= a->game.n_cos) cur = 0;
    a->cps.player_co = (int8_t)co_next_unlocked(a, cur, 1);
    snd_se(SE_CURSOR);
}

static void cpnmap_event(App *a, const SDL_Event *e)
{
    /* 縦に長いのでホイールと↑↓で送れるようにする */
    if (e->type == SDL_MOUSEWHEEL) {
        a->cpn_scroll -= e->wheel.y * 60;
        cpnmap_clamp_scroll(a);
        return;
    }
    if (e->type == SDL_KEYDOWN) {
        SDL_Keycode k = e->key.keysym.sym;
        if (k == SDLK_UP || k == SDLK_DOWN) {
            a->cpn_scroll += (k == SDLK_DOWN ? 60 : -60);
            cpnmap_clamp_scroll(a);
            return;
        }
    }
    if (e->type == SDL_MOUSEBUTTONDOWN && e->button.button == SDL_BUTTON_LEFT) {
        SDL_Point p = { e->button.x, e->button.y };
        SDL_Rect cr = cpnmap_co_rect();
        if (SDL_PointInRect(&p, &cr)) { cpnmap_cycle_co(a); return; }
        snd_se(SE_OK);
        a->story_is_win = false;
        a->next_screen = SCREEN_STORY;
        return;
    }
    if (e->type == SDL_KEYDOWN &&
        (e->key.keysym.sym == SDLK_z || e->key.keysym.sym == SDLK_RETURN)) {
        snd_se(SE_OK);
        a->story_is_win = false;
        a->next_screen = SCREEN_STORY;
    }
    if (e->type == SDL_KEYDOWN &&
        (e->key.keysym.sym == SDLK_x || e->key.keysym.sym == SDLK_ESCAPE)) {
        snd_se(SE_CANCEL);
        a->next_screen = SCREEN_TITLE;
    }
    /* C: 指揮官を切り替える（次の作戦から反映される） */
    if (e->type == SDL_KEYDOWN && e->key.keysym.sym == SDLK_c)
        cpnmap_cycle_co(a);
}

static void cpnmap_update(App *a) { (void)a; }

static void draw_link(App *a, int x0, int y0, int x1, int y1, SDL_Color c)
{
    SDL_SetRenderDrawColor(a->ren, c.r, c.g, c.b, c.a);
    /* 少し太く見せるため2本 */
    SDL_RenderDrawLine(a->ren, x0, y0, x1, y1);
    SDL_RenderDrawLine(a->ren, x0, y0 + 1, x1, y1 + 1);
}

static void cpnmap_draw(App *a)
{
    const Campaign *c = &a->cpn;
    fill_rect(a, 0, 0, WIN_W, WIN_H, (SDL_Color){ 24, 30, 38, 255 });
    draw_text_center(a, a->font_s, WIN_W / 2, 60, COL_DIM, tx("CPNMAP_TITLE"));
    draw_text_center(a, a->font_xl, WIN_W / 2, 84, COL_WHITE, c->name);

    int cur = cpnmap_find_index(c, a->cps.node);

    /* 作戦を並べる帯だけに描く。こうしないとスクロールしたときに
     * ノードが上の題や下の情報欄に重なってしまう。 */
    SDL_Rect band = { 0, CPNMAP_TOP, WIN_W, CPNMAP_BOT - CPNMAP_TOP };
    SDL_RenderSetClipRect(a->ren, &band);

    /* 接続線（通常ルート=灰、早期勝利ルート=黄） */
    for (int i = 0; i < c->n_nodes; i++) {
        int x0, y0;
        cpnmap_node_screen(a, i, &x0, &y0);
        int j = cpnmap_find_index(c, c->nodes[i].next_win);
        if (j >= 0) {
            int x1, y1;
            cpnmap_node_screen(a, j, &x1, &y1);
            draw_link(a, x0, y0, x1, y1, COL_DIM);
        }
        if (c->nodes[i].next_win_fast[0]) {
            j = cpnmap_find_index(c, c->nodes[i].next_win_fast);
            if (j >= 0) {
                int x1, y1;
                cpnmap_node_screen(a, j, &x1, &y1);
                draw_link(a, x0, y0, x1, y1,
                          (SDL_Color){ 200, 180, 80, 200 });
            }
        }
    }

    /* ノード */
    for (int i = 0; i < c->n_nodes; i++) {
        int x, y;
        cpnmap_node_screen(a, i, &x, &y);
        if (y < CPNMAP_TOP - 90 || y > CPNMAP_BOT + 90) continue;   /* 帯の外 */
        bool done = (a->cps.cleared >> i) & 1;
        uint8_t nrank = (i < MAX_CAMPAIGN_MAPS) ? a->cps.rank[i] : RANK_NONE;
        bool current = (i == cur);

        SDL_Color fill = done ? COL_P[0]
                       : current ? (SDL_Color){ 90, 120, 170, 255 }
                       : (SDL_Color){ 50, 58, 70, 255 };
        render_fill_hex(a, (float)x, (float)y, 46.0f, (SDL_Color){ 30, 34, 42, 255 });
        render_fill_hex(a, (float)x, (float)y, 40.0f, fill);
        if (current) {
            float pulse = 1.0f + 0.05f * sinf((float)a->frame * 0.12f);
            render_hex_outline(a, (float)x, (float)y, 46.0f * pulse, COL_YELLOW);
        }
        draw_text_center(a, a->font_m, x, y - 12,
                         done || current ? COL_WHITE : COL_GRAY, c->nodes[i].id);
        if (done) {
            /* 制圧済みは取得ランクを併記（未評価なら従来どおり「制圧」） */
            if (nrank != RANK_NONE) {
                char rb[32];
                snprintf(rb, sizeof rb, "%s  %s", tx("CPNMAP_DONE"),
                         campaign_rank_str((CpnRank)nrank));
                draw_text_center(a, a->font_s, x, y + 10,
                                 nrank == RANK_S ? COL_YELLOW : COL_WHITE, rb);
            } else {
                draw_text_center(a, a->font_s, x, y + 10, COL_YELLOW,
                                 tx("CPNMAP_DONE"));
            }
        }

        /* タイトル（下に小さく） */
        draw_text_center(a, a->font_s, x, y + 58,
                         current ? COL_WHITE : COL_DIM, c->nodes[i].title);
    }

    SDL_RenderSetClipRect(a->ren, NULL);

    /* まだ先があることを示す矢印 */
    {
        int m = cpnmap_max_scroll(c);
        if (a->cpn_scroll > 0)
            draw_text_center(a, a->font_m, WIN_W - 40, CPNMAP_TOP + 4,
                             COL_YELLOW, "▲");
        if (a->cpn_scroll < m)
            draw_text_center(a, a->font_m, WIN_W - 40, CPNMAP_BOT - 30,
                             COL_YELLOW, "▼");
    }

    /* 現在ノードの情報 */
    if (cur >= 0) {
        char buf[128];
        snprintf(buf, sizeof buf, tx("CPNMAP_NEXT_FMT"), c->nodes[cur].title);
        draw_text_center(a, a->font_m, WIN_W / 2, WIN_H - 172, COL_WHITE, buf);
    }
    if (a->cps.n_carry > 0) {
        char buf[96];
        snprintf(buf, sizeof buf, tx("BRIEF_CARRY_FMT"),
                 a->cps.n_carry, a->cps.funds_carry);
        draw_text_center(a, a->font_s, WIN_W / 2, WIN_H - 146, COL_YELLOW, buf);
    }
    draw_text_center(a, a->font_s, WIN_W / 2, WIN_H - 34, COL_YELLOW,
                     tx("CPNMAP_HINT"));
    /* 選択中の指揮官を表示（C で変更） */
    if (a->game.n_cos > 0) {
        int ci = a->cps.player_co;
        if (ci < 0 || ci >= a->game.n_cos) ci = 0;
        SDL_Rect cr = cpnmap_co_rect();
        fill_rect(a, cr.x, cr.y, cr.w, cr.h, (SDL_Color){ 40, 48, 60, 220 });
        outline_rect(a, cr.x, cr.y, cr.w, cr.h, COL_DIM);
        /* 左端に顔絵の小さなサムネ（誰を選んでいるか一目で分かるように） */
        draw_co_portrait(a, ci, cr.x + 4, cr.y + 3, 32, 42);
        const CommanderType *co = &a->game.cos[ci];
        char cb[192];
        snprintf(cb, sizeof cb, tx("CPNMAP_CO_FMT"), co->name);
        draw_text_center(a, a->font_s, WIN_W / 2, WIN_H - 112, COL_YELLOW, cb);
        snprintf(cb, sizeof cb, "【%s】%s", co->title, co->desc);
        draw_text_center(a, a->font_s, WIN_W / 2, WIN_H - 90, COL_GRAY, cb);
    }
}

/* ------------------------------------------------------------------ */
/* ブリーフィング画面                                                  */
/* ------------------------------------------------------------------ */
static void brief_free_art(App *a)
{
    if (a->brief_tex) {
        SDL_DestroyTexture(a->brief_tex);
        a->brief_tex = NULL;
    }
}

static void brief_enter(App *a)
{
    snd_music(HWM_TITLE, true);
    brief_free_art(a);
    const CpnNode *node = campaign_find_node(&a->cpn, a->cps.node);
    if (node && node->art[0])
        a->brief_tex = sprite_load_file(a, node->art,
                                        &a->brief_tex_w, &a->brief_tex_h);
}

static void brief_start_battle(App *a)
{
    char err[256];
    a->cps_backup = a->cps;
    snd_se(SE_OK);
    brief_free_art(a);
    a->dep_seed = SDL_GetTicks() | 1u;

    /* まずマップだけ用意して、持越しが出撃上限を超えるか調べる */
    if (campaign_setup_map(&a->game, &a->cpn, &a->cps, a->base_path,
                           err, sizeof err) != 0) {
        SDL_Log("マップ読込失敗: %s", err);
        a->next_screen = SCREEN_TITLE;
        return;
    }
    a->dep_limit = campaign_deploy_limit(&a->game);
    const CpnNode *node = campaign_find_node(&a->cpn, a->cps.node);
    bool need_pick = node && node->carry && a->cps.n_carry > a->dep_limit;
    if (need_pick) {
        a->next_screen = SCREEN_DEPLOY;   /* どの部隊を出すか選ばせる */
        return;
    }
    campaign_begin(&a->game, &a->cpn, &a->cps, a->dep_seed, NULL);
    a->next_screen = SCREEN_BATTLE;
}

static void brief_event(App *a, const SDL_Event *e)
{
    if ((e->type == SDL_KEYDOWN &&
         (e->key.keysym.sym == SDLK_z || e->key.keysym.sym == SDLK_RETURN)) ||
        (e->type == SDL_MOUSEBUTTONDOWN && e->button.button == SDL_BUTTON_LEFT)) {
        brief_start_battle(a);
    }
    if (e->type == SDL_KEYDOWN && e->key.keysym.sym == SDLK_ESCAPE) {
        brief_free_art(a);
        a->next_screen = SCREEN_TITLE;
    }
}

static void brief_update(App *a) { (void)a; }

static void brief_draw(App *a)
{
    fill_rect(a, 0, 0, WIN_W, WIN_H, (SDL_Color){ 24, 30, 38, 255 });
    const CpnNode *node = campaign_find_node(&a->cpn, a->cps.node);
    if (!node) {
        draw_text_center(a, a->font_l, WIN_W / 2, 300, COL_GRAY, tx("BRIEF_NONODE"));
        return;
    }
    if (a->brief_tex) {
        /* 1枚絵（横800基準を中央に。仕様書 6.2） */
        int dw = 800;
        int dh = a->brief_tex_h * dw / (a->brief_tex_w ? a->brief_tex_w : 1);
        SDL_Rect dst = { WIN_W / 2 - dw / 2, 60, dw, dh };
        SDL_RenderCopy(a->ren, a->brief_tex, NULL, &dst);
        outline_rect(a, dst.x, dst.y, dst.w, dst.h, COL_DIM);
    } else {
        for (int i = 0; i < 5; i++) {
            SDL_Color c = COL_P[i % 2];
            c.a = 30;
            render_fill_hex(a, 220.0f + i * 210.0f, 120.0f, 64.0f, c);
        }
    }
    draw_text_center(a, a->font_s, WIN_W / 2, 300, COL_DIM, a->cpn.name);
    draw_text_center(a, a->font_xl, WIN_W / 2, 324, COL_WHITE, node->title);

    /* 行数に応じて詰める。作戦説明が長いと副目標が下の
     * 「Z/クリックで出撃」に重なるので、必要な高さを先に測って
     * 開始位置と行間を決める。 */
    int line_h = node->n_brief >= 5 ? 30 : 36;
    int need = node->n_brief * line_h + 8;
    if (a->cps.n_carry > 0) need += 24;
    if (a->cps.n_store > 0) need += 24;
    if (node->n_subs > 0)   need += 26 + node->n_subs * 22;
    int y = 410;
    if (y + need > WIN_H - 100) y = WIN_H - 100 - need;
    if (y < 382) y = 382;                  /* 題名に被せない */

    for (int i = 0; i < node->n_brief; i++)
        draw_text_center(a, a->font_m, WIN_W / 2, y + i * line_h, COL_GRAY,
                         node->brief[i]);
    y += node->n_brief * line_h + 8;

    if (a->cps.n_carry > 0) {
        char buf[96];
        snprintf(buf, sizeof buf, tx("BRIEF_CARRY_FMT"),
                 a->cps.n_carry, a->cps.funds_carry);
        draw_text_center(a, a->font_s, WIN_W / 2, y, COL_YELLOW, buf);
        y += 24;
    }
    if (a->cps.n_store > 0) {
        char buf[96];
        snprintf(buf, sizeof buf, tx("BRIEF_STORE_FMT"), a->cps.n_store);
        draw_text_center(a, a->font_s, WIN_W / 2, y,
                         (SDL_Color){ 130, 220, 130, 255 }, buf);
        y += 24;
    }
    /* 副目標（達成すると評価ボーナス）。作戦説明の下に並べる */
    if (node->n_subs > 0) {
        char buf[128];
        snprintf(buf, sizeof buf, tx("SUB_TITLE_FMT"), campaign_sub_bonus());
        draw_text_center(a, a->font_s, WIN_W / 2, y, COL_YELLOW, buf);
        for (int i = 0; i < node->n_subs; i++) {
            snprintf(buf, sizeof buf, tx("SUB_ROW_FMT"), node->subs[i].desc);
            draw_text_center(a, a->font_s, WIN_W / 2, y + 26 + i * 22,
                             COL_WHITE, buf);
        }
    }
    draw_text_center(a, a->font_m, WIN_W / 2, WIN_H - 80, COL_YELLOW,
                     tx("BRIEF_GO"));
}

/* ------------------------------------------------------------------ */
/* ご褒美画像（キャンペーンのマップをクリアした時）                    */
/* ------------------------------------------------------------------ */

/* ご褒美画面へ遷移すべきか（battle からの遷移判定に使う）。
 * キャンペーン: プレイヤー(P0)勝利かつノードに reward 画像がある時。
 * フリー対戦: 人間側が勝利した時（汎用の勝利画像を表示）。 */
bool reward_available(App *a)
{
    Game *g = &a->game;
    if (g->winner < 0) return false;                 /* 引き分け等は対象外 */
    if (a->campaign_mode) {
        if (g->winner != 0) return false;
        const CpnNode *node = campaign_find_node(&a->cpn, a->cps.node);
        return node && (node->reward[0] || node->reward_video[0]);
    }
    return g->ctrl[g->winner] == CTRL_HUMAN;          /* 人間の勝利 */
}

static void reward_free(App *a)
{
    if (a->reward_tex) {
        SDL_DestroyTexture(a->reward_tex);
        a->reward_tex = NULL;
    }
}

/* この画面で再生する動画（無ければ NULL）。mp4/GIF どちらでも可 */
static const char *reward_video_rel(App *a)
{
    if (!a->campaign_mode) return NULL;
    const CpnNode *node = campaign_find_node(&a->cpn, a->cps.node);
    return (node && node->reward_video[0]) ? node->reward_video : NULL;
}

static void reward_enter(App *a)
{
    reward_free(a);
    a->endroll_start_ms = SDL_GetTicks();   /* 動画再生の基準時刻に流用 */
    const char *vid = reward_video_rel(a);
    if (vid) {
        /* 動画を使う場合は静止画を読まない（変換は初回のみキャッシュされる） */
        uanim_get_path(a, vid);
        snd_music(HWM_VICTORY, false);
        return;
    }
    const char *img = NULL;
    if (a->campaign_mode) {
        const CpnNode *node = campaign_find_node(&a->cpn, a->cps.node);
        if (node && node->reward[0]) img = node->reward;
    } else {
        img = "gfx/reward/free_win.png";   /* フリー対戦の汎用勝利画像 */
    }
    if (img && img[0])
        a->reward_tex = sprite_load_file(a, img,
                                         &a->reward_tex_w, &a->reward_tex_h);
    snd_music(HWM_VICTORY, false);
}

static void reward_event(App *a, const SDL_Event *e)
{
    if ((e->type == SDL_KEYDOWN &&
         (e->key.keysym.sym == SDLK_z || e->key.keysym.sym == SDLK_RETURN ||
          e->key.keysym.sym == SDLK_x || e->key.keysym.sym == SDLK_ESCAPE)) ||
        (e->type == SDL_MOUSEBUTTONDOWN)) {
        snd_se(SE_OK);
        reward_free(a);
        a->next_screen = SCREEN_RESULT;   /* 続けて結果画面へ */
    }
}

static void reward_update(App *a) { (void)a; }

static void reward_draw(App *a)
{
    fill_rect(a, 0, 0, WIN_W, WIN_H, (SDL_Color){ 20, 24, 30, 255 });
    const CpnNode *node = campaign_find_node(&a->cpn, a->cps.node);

    draw_text_center(a, a->font_xl, WIN_W / 2, 30, COL_YELLOW, tx("REWARD_TITLE"));
    if (a->campaign_mode && node) {
        draw_text_center(a, a->font_m, WIN_W / 2, 84, COL_WHITE, node->title);
    } else if (!a->campaign_mode && a->game.winner >= 0) {
        /* フリー対戦: 勝者陣営名を副題に */
        char buf[96];
        snprintf(buf, sizeof buf, tx("RESULT_WIN_FMT"),
                 faction_name(a->game.winner));
        draw_text_center(a, a->font_m, WIN_W / 2, 84, COL_WHITE, buf);
    }

    const char *vid = reward_video_rel(a);
    UnitAnim *rv = vid ? uanim_get_path(a, vid) : NULL;
    if (rv) {
        int elapsed = (int)(SDL_GetTicks() - a->endroll_start_ms);
        SDL_Texture *fr = uanim_frame_at(a, rv, elapsed);
        int maxw = 900, maxh = 560;
        int iw = rv->w ? rv->w : 1, ih = rv->h ? rv->h : 1;
        double sc = (double)maxw / iw;
        if ((double)ih * sc > maxh) sc = (double)maxh / ih;
        int dw = (int)(iw * sc), dh = (int)(ih * sc);
        SDL_Rect dst = { WIN_W / 2 - dw / 2, 130, dw, dh };
        if (fr) SDL_RenderCopy(a->ren, fr, NULL, &dst);
        outline_rect(a, dst.x - 2, dst.y - 2, dst.w + 4, dst.h + 4, COL_DIM);
    } else if (a->reward_tex) {
        /* 画面中央に大きく表示（縦横比を保ち、最大 720x560 に収める） */
        int maxw = 760, maxh = 560;
        int iw = a->reward_tex_w ? a->reward_tex_w : 1;
        int ih = a->reward_tex_h ? a->reward_tex_h : 1;
        double sc = (double)maxw / iw;
        if ((double)ih * sc > maxh) sc = (double)maxh / ih;
        int dw = (int)(iw * sc), dh = (int)(ih * sc);
        SDL_Rect dst = { WIN_W / 2 - dw / 2, 130, dw, dh };
        SDL_RenderCopy(a->ren, a->reward_tex, NULL, &dst);
        outline_rect(a, dst.x - 2, dst.y - 2, dst.w + 4, dst.h + 4, COL_DIM);
    } else {
        draw_text_center(a, a->font_m, WIN_W / 2, WIN_H / 2, COL_DIM,
                         tx("REWARD_NONE"));
    }
    draw_text_center(a, a->font_m, WIN_W / 2, WIN_H - 50, COL_YELLOW,
                     tx("REWARD_HINT"));
}

/* ------------------------------------------------------------------ */
/* 結果                                                                */
/* ------------------------------------------------------------------ */
static void result_enter(App *a)
{
    a->menu_idx = 0;
    a->cpn_result = 0;
    if (a->campaign_mode) {
        if (a->game.winner == 0) {
            /* 評価は campaign_on_victory の中で cps.rank[] に記録される */
            int r = campaign_on_victory(&a->game, &a->cpn, &a->cps);
            a->cpn_result = (r == 1) ? 2 : 1;
            /* 進捗（最高ランク）を更新してから、解禁の基準となる
             * 「クリアした作戦の種類数」を数え直す。
             * 延べ回数にすると同じ作戦の周回で解禁できてしまうため。 */
            for (int i = 0; i < a->cpn.n_nodes && i < MAX_CAMPAIGN_MAPS; i++)
                if (a->cps.rank[i] != RANK_NONE &&
                    (a->progress_best[i] == RANK_NONE ||
                     a->cps.rank[i] < a->progress_best[i]))
                    a->progress_best[i] = a->cps.rank[i];
            a->progress_clears = progress_count_cleared(a);
            progress_save(a);
        } else {
            a->cpn_result = 3;
        }
    }
    /* 勝敗ジングル: 人間側が勝ったら勝利、それ以外は敗北 */
    Game *g = &a->game;
    bool human_won = g->winner >= 0 && g->ctrl[g->winner] == CTRL_HUMAN;
    snd_music(human_won ? HWM_VICTORY : HWM_DEFEAT, false);
}

static void result_event(App *a, const SDL_Event *e)
{
    bool confirm =
        (e->type == SDL_KEYDOWN &&
         (e->key.keysym.sym == SDLK_z || e->key.keysym.sym == SDLK_RETURN)) ||
        (e->type == SDL_MOUSEBUTTONDOWN && e->button.button == SDL_BUTTON_LEFT);
    bool cancel =
        (e->type == SDL_KEYDOWN &&
         (e->key.keysym.sym == SDLK_x || e->key.keysym.sym == SDLK_ESCAPE)) ||
        (e->type == SDL_MOUSEBUTTONDOWN && e->button.button == SDL_BUTTON_RIGHT);

    switch (a->cpn_result) {
    case 1:
        if (confirm) a->next_screen = SCREEN_CPNMAP; /* 全体マップ経由で次作戦へ */
        if (cancel)  a->next_screen = SCREEN_TITLE;
        break;
    case 3:
        if (confirm) {
            a->cps = a->cps_backup;
            a->next_screen = SCREEN_BRIEFING;
        }
        if (cancel) a->next_screen = SCREEN_TITLE;
        break;
    case 2:
        /* キャンペーン全クリア → エンドロールへ */
        if (confirm || cancel) a->next_screen = SCREEN_ENDROLL;
        break;
    default:
        if (confirm || cancel) a->next_screen = SCREEN_TITLE;
        break;
    }
}

static void result_update(App *a) { (void)a; }

static void result_draw(App *a)
{
    Game *g = &a->game;
    fill_rect(a, 0, 0, WIN_W, WIN_H, (SDL_Color){ 32, 42, 52, 255 });

    /* 上から順に積む方式。固定座標を並べると、副目標の数などで行が増えたときに
     * 重なるので、y を進めながら描く（行が増えても崩れない）。 */
    const int cx = WIN_W / 2;
    int y = 170;   /* 画面の縦位置。行が増えても下の余白で吸収できるだけ余裕を取る */
    char buf[160];

    if (a->human_out) {
        /* 人間の陣営が全滅。引き分けではないので別の文言にする */
        draw_text_center(a, a->font_xl, cx, y,
                         (SDL_Color){ 220, 130, 130, 255 }, tx("RESULT_HUMAN_OUT"));
    } else if (g->winner >= 0) {
        SDL_Color c = COL_P[g->winner];
        snprintf(buf, sizeof buf, tx("RESULT_WIN_FMT"), faction_name(g->winner));
        draw_text_center(a, a->font_xl, cx, y, c, buf);
    } else {
        draw_text_center(a, a->font_xl, cx, y, COL_GRAY, tx("RESULT_DRAW"));
    }
    y += 72;

    /* 作戦評価（キャンペーンで勝利した時） */
    const CpnNode *done = NULL;
    if (a->campaign_mode && g->winner == 0 &&
        (a->cpn_result == 1 || a->cpn_result == 2)) {
        done = campaign_find_node(&a->cpn, a->cps_backup.node);
        CpnScore sc;
        campaign_evaluate(g, done, &sc);
        SDL_Color rc = (sc.rank == RANK_S) ? COL_YELLOW
                     : (sc.rank == RANK_A) ? (SDL_Color){ 140, 220, 255, 255 }
                     : (sc.rank == RANK_B) ? COL_WHITE : COL_GRAY;
        snprintf(buf, sizeof buf, tx("RANK_TITLE_FMT"), campaign_rank_str(sc.rank));
        draw_text_center(a, a->font_l, cx, y, rc, buf);
        y += 36;

        int bonus = campaign_rank_bonus(sc.rank);
        if (bonus > 0)
            snprintf(buf, sizeof buf, tx("RANK_DETAIL_BONUS_FMT"),
                     sc.speed, sc.loss, sc.power, sc.total, bonus);
        else
            snprintf(buf, sizeof buf, tx("RANK_DETAIL_FMT"),
                     sc.speed, sc.loss, sc.power, sc.total);
        draw_text_center(a, a->font_s, cx, y, COL_GRAY, buf);
        y += 28;
    }

    snprintf(buf, sizeof buf, tx("RESULT_TURNS_FMT"), g->turn);
    draw_text_center(a, a->font_m, cx, y, COL_WHITE, buf);
    y += 28;

    /* 損失は参加した陣営を全部並べる。三つ巴や援軍マップだと
     * 2陣営分しか出さないと、誰がどれだけ失ったのか分からない。 */
    {
        int n = 0;
        buf[0] = 0;
        for (int p = 0; p < MAX_PLAYERS; p++) {
            if (!game_player_in_play(g, p) && g->lost_units[p] == 0) continue;
            char one[48];
            snprintf(one, sizeof one, tx("RESULT_LOSS_ONE_FMT"),
                     faction_name(p), g->lost_units[p]);
            if (n++) strncat(buf, "   ", sizeof buf - strlen(buf) - 1);
            strncat(buf, one, sizeof buf - strlen(buf) - 1);
        }
        char line[192];
        snprintf(line, sizeof line, "%s  %s", tx("RESULT_LOSSES_HEAD"), buf);
        draw_text_center(a, n > 2 ? a->font_s : a->font_m, cx, y, COL_WHITE, line);
    }
    y += 34;

    /* 副目標の達成状況（達成分はボーナス資金に加算済み） */
    if (done && done->n_subs > 0) {
        int ndone = campaign_sub_count_done(g, done);
        snprintf(buf, sizeof buf, tx("SUB_RESULT_FMT"),
                 ndone, done->n_subs, ndone * campaign_sub_bonus());
        draw_text_center(a, a->font_s, cx, y,
                         ndone > 0 ? COL_YELLOW : COL_GRAY, buf);
        y += 24;
        for (int i = 0; i < done->n_subs; i++) {
            bool ok = campaign_sub_done(g, done, i);
            snprintf(buf, sizeof buf, ok ? tx("SUB_OK_FMT") : tx("SUB_NG_FMT"),
                     done->subs[i].desc);
            draw_text_center(a, a->font_s, cx, y,
                             ok ? (SDL_Color){ 130, 220, 130, 255 } : COL_DIM, buf);
            y += 20;
        }
    }
    y += 14;

    switch (a->cpn_result) {
    case 1: {
        const CpnNode *next = campaign_find_node(&a->cpn, a->cps.node);
        if (next) {
            snprintf(buf, sizeof buf, tx("RESULT_NEXT_FMT"), next->title);
            draw_text_center(a, a->font_m, cx, y, COL_WHITE, buf);
            y += 32;
        }
        snprintf(buf, sizeof buf, tx("RESULT_CARRY_FMT"),
                 a->cps.n_carry, a->cps.funds_carry);
        draw_text_center(a, a->font_s, cx, y, COL_YELLOW, buf);
        y += 44;
        draw_text_center(a, a->font_m, cx, y, COL_YELLOW, tx("RESULT_NEXT_HINT"));
        break;
    }
    case 2:
        draw_text_center(a, a->font_xl, cx, y, COL_YELLOW, tx("RESULT_CLEAR"));
        y += 72;
        draw_text_center(a, a->font_m, cx, y, COL_WHITE, tx("RESULT_CLEAR_SUB"));
        break;
    case 3:
        draw_text_center(a, a->font_m, cx, y, COL_GRAY, tx("RESULT_LOSE"));
        y += 44;
        draw_text_center(a, a->font_m, cx, y, COL_YELLOW, tx("RESULT_RETRY_HINT"));
        break;
    default:
        y += 24;
        draw_text_center(a, a->font_m, cx, y, COL_YELLOW, tx("RESULT_HINT"));
        break;
    }
}

/* ------------------------------------------------------------------ */
/* エンドロール（キャンペーン全クリア後）                              */
/* ------------------------------------------------------------------ */

/* クレジット行。text_ja.def の END_L01.. を順に読み、空になったら終わり。
 * 行頭が '@' の行は見出し（大きい文字）として描く。 */
#define ENDROLL_MAX_LINES 64
#define ENDROLL_SPEED_PXS 42       /* スクロール速度 px/秒 */
#define ENDROLL_LINE_H    44

static int endroll_lines(App *a, const char **out)
{
    (void)a;
    int n = 0;
    for (int i = 1; i <= ENDROLL_MAX_LINES; i++) {
        char key[16];
        snprintf(key, sizeof key, "END_L%02d", i);
        const char *s = tx(key);
        /* 未定義キーは tx() がキー名を返す実装なので、それを終端とみなす。
         * 値が空（"END_L03 ="）の行は「空行」として有効に扱う。 */
        if (!s || !strcmp(s, key)) break;
        out[n++] = s;
    }
    return n;
}

static void endroll_enter(App *a)
{
    a->endroll_start_ms = SDL_GetTicks();
    a->endroll_done = 0;
    /* 背景動画（任意）。assets/gfx/anim/endroll.mp4 か .gif があれば再生 */
    if (!uanim_get_path(a, "gfx/anim/endroll.mp4"))
        uanim_get_path(a, "gfx/anim/endroll.gif");
    /* エンディング曲（audio.def の [music ENDING]）があればそれをループさせる。
     * 無ければ従来どおり勝利ジングルを1回だけ。
     * **ループさせるのはエンディング曲のときだけ**。短いジングルを
     * 繰り返すとスタッフロールの間々と合わず逃しくなる。 */
    if (snd_music_available(HWM_ENDING)) snd_music(HWM_ENDING, true);
    else                                 snd_music(HWM_VICTORY, false);
}

static UnitAnim *endroll_video(App *a)
{
    UnitAnim *v = uanim_get_path(a, "gfx/anim/endroll.mp4");
    if (!v) v = uanim_get_path(a, "gfx/anim/endroll.gif");
    return v;
}

static void endroll_event(App *a, const SDL_Event *e)
{
    bool press =
        (e->type == SDL_KEYDOWN &&
         (e->key.keysym.sym == SDLK_z || e->key.keysym.sym == SDLK_RETURN ||
          e->key.keysym.sym == SDLK_x || e->key.keysym.sym == SDLK_ESCAPE)) ||
        (e->type == SDL_MOUSEBUTTONDOWN);
    if (!press) return;
    if (a->endroll_done) {
        snd_se(SE_OK);
        a->next_screen = SCREEN_TITLE;
    } else {
        /* 1回目の入力は「早送り（最後まで飛ばす）」 */
        a->endroll_done = 1;
    }
}

static void endroll_update(App *a) { (void)a; }

static void endroll_draw(App *a)
{
    fill_rect(a, 0, 0, WIN_W, WIN_H, (SDL_Color){ 12, 14, 20, 255 });

    /* 背景動画があれば薄く敷く */
    UnitAnim *v = endroll_video(a);
    if (v) {
        int elapsed = (int)(SDL_GetTicks() - a->endroll_start_ms);
        SDL_Texture *fr = uanim_frame_at(a, v, elapsed);
        if (fr) {
            int iw = v->w ? v->w : 1, ih = v->h ? v->h : 1;
            /* 画面を覆うように等比拡大（はみ出しは切る） */
            double sc = (double)WIN_W / iw;
            if ((double)ih * sc < WIN_H) sc = (double)WIN_H / ih;
            int dw = (int)(iw * sc), dh = (int)(ih * sc);
            SDL_Rect dst = { WIN_W / 2 - dw / 2, WIN_H / 2 - dh / 2, dw, dh };
            SDL_SetTextureAlphaMod(fr, 150);
            SDL_RenderCopy(a->ren, fr, NULL, &dst);
            SDL_SetTextureAlphaMod(fr, 255);
        }
    }

    const char *lines[ENDROLL_MAX_LINES];
    int n = endroll_lines(a, lines);
    int total_h = n * ENDROLL_LINE_H;

    /* スクロール位置: 画面下から現れて上へ抜ける */
    int elapsed = (int)(SDL_GetTicks() - a->endroll_start_ms);
    int scrolled = elapsed * ENDROLL_SPEED_PXS / 1000;
    int end_scroll = total_h + WIN_H;
    if (a->endroll_done) scrolled = end_scroll;      /* 早送り */
    if (scrolled >= end_scroll) { scrolled = end_scroll; a->endroll_done = 1; }

    int y0 = WIN_H - scrolled;
    for (int i = 0; i < n; i++) {
        int y = y0 + i * ENDROLL_LINE_H;
        if (y < -ENDROLL_LINE_H || y > WIN_H) continue;
        const char *s = lines[i];
        bool head = (s[0] == '@');
        if (head) s++;
        /* 上下端でフェードさせる */
        int alpha = 255;
        if (y < 80)            alpha = 255 * (y + ENDROLL_LINE_H) / (80 + ENDROLL_LINE_H);
        if (y > WIN_H - 120)   alpha = 255 * (WIN_H - y) / 120;
        if (alpha < 0) alpha = 0;
        if (alpha > 255) alpha = 255;
        SDL_Color c = head ? COL_YELLOW : COL_WHITE;
        c.a = (Uint8)alpha;
        draw_text_center(a, head ? a->font_l : a->font_m, WIN_W / 2, y, c, s);
    }

    /* 流し終わったら操作ヒント */
    if (a->endroll_done)
        draw_text_center(a, a->font_m, WIN_W / 2, WIN_H - 46, COL_YELLOW,
                         tx("END_HINT_DONE"));
    else
        draw_text_center(a, a->font_s, WIN_W / 2, WIN_H - 30, COL_DIM,
                         tx("END_HINT_SKIP"));
}

/* ------------------------------------------------------------------ */
/* 出撃部隊の編成（持越しが出撃上限を超えたとき）                      */
/* ------------------------------------------------------------------ */
#define DEPLOY_VISIBLE 11

static SDL_Rect deploy_row_rect(App *a, int i)
{
    SDL_Rect r = { WIN_W / 2 - 320, 168 + (i - a->dep_scroll) * 40, 640, 36 };
    return r;
}

/* このマップでは出撃させられない部隊（例: 海の無いマップの艦船）。
 * 選んでも deploy_carry で倒庫に戻されるだけなので、最初から選べなくする。 */
static bool deploy_row_blocked(const App *a, int i)
{
    if (i < 0 || i >= a->cps.n_carry) return true;
    return !campaign_type_placeable(&a->game, a->cps.carry[i].type);
}

static int deploy_count(const App *a)
{
    int n = 0;
    for (int i = 0; i < a->cps.n_carry; i++) if (a->dep_sel[i]) n++;
    return n;
}

static void deploy_enter(App *a)
{
    /* 既定は「価値の高い順に上限まで」。
     * carry[] は価格→経験値→HP の順に並んでいるので先頭から埋めればよい
     * （経験値だけで並べると、進化直後で経験値0の精鋭が最後尾に沈む）。 */
    memset(a->dep_sel, 0, sizeof a->dep_sel);
    int n = 0;
    for (int i = 0; i < a->cps.n_carry && n < a->dep_limit; i++) {
        if (deploy_row_blocked(a, i)) continue;   /* 配置先が無い兵种は飛ばす */
        a->dep_sel[i] = 1;
        n++;
    }
    a->dep_idx = 0;
    a->dep_scroll = 0;
}

static void deploy_keep_visible(App *a)
{
    if (a->dep_idx < a->dep_scroll) a->dep_scroll = a->dep_idx;
    else if (a->dep_idx >= a->dep_scroll + DEPLOY_VISIBLE)
        a->dep_scroll = a->dep_idx - DEPLOY_VISIBLE + 1;
    int maxs = a->cps.n_carry - DEPLOY_VISIBLE;
    if (maxs < 0) maxs = 0;
    if (a->dep_scroll > maxs) a->dep_scroll = maxs;
    if (a->dep_scroll < 0) a->dep_scroll = 0;
}

/* 選択を切り替える（上限を超える選択は弾く） */
static void deploy_toggle(App *a, int i)
{
    if (i < 0 || i >= a->cps.n_carry) return;
    if (deploy_row_blocked(a, i)) { snd_se(SE_CANCEL); return; }
    if (a->dep_sel[i]) {
        a->dep_sel[i] = 0;
        snd_se(SE_CURSOR);
    } else if (deploy_count(a) < a->dep_limit) {
        a->dep_sel[i] = 1;
        snd_se(SE_CURSOR);
    } else {
        snd_se(SE_CANCEL);      /* 上限に達している */
    }
}

static void deploy_confirm(App *a)
{
    snd_se(SE_OK);
    campaign_begin(&a->game, &a->cpn, &a->cps, a->dep_seed, a->dep_sel);
    a->next_screen = SCREEN_BATTLE;
}

static void deploy_event(App *a, const SDL_Event *e)
{
    if (e->type == SDL_MOUSEWHEEL) {
        int maxs = a->cps.n_carry - DEPLOY_VISIBLE;
        if (maxs < 0) maxs = 0;
        a->dep_scroll += (e->wheel.y > 0 ? -1 : 1);
        if (a->dep_scroll < 0) a->dep_scroll = 0;
        if (a->dep_scroll > maxs) a->dep_scroll = maxs;
        return;
    }
    if (e->type == SDL_KEYDOWN) {
        SDL_Keycode k = e->key.keysym.sym;
        if (k == SDLK_UP || k == SDLK_DOWN) {
            int n = a->cps.n_carry;
            if (n > 0) {
                a->dep_idx = (a->dep_idx + n + (k == SDLK_DOWN ? 1 : -1)) % n;
                deploy_keep_visible(a);
            }
        }
        if (k == SDLK_SPACE || k == SDLK_z || k == SDLK_RETURN)
            deploy_toggle(a, a->dep_idx);
        if (k == SDLK_a) {          /* A: 経験値順に自動で埋め直す */
            deploy_enter(a);
            snd_se(SE_CURSOR);
        }
        if (k == SDLK_x || k == SDLK_ESCAPE) deploy_confirm(a);  /* この編成で出撃 */
        return;
    }
    if (e->type == SDL_MOUSEBUTTONDOWN && e->button.button == SDL_BUTTON_LEFT) {
        SDL_Point p = { e->button.x, e->button.y };
        /* 「出撃」ボタン */
        SDL_Rect go = { WIN_W / 2 - 130, WIN_H - 74, 260, 44 };
        if (SDL_PointInRect(&p, &go)) { deploy_confirm(a); return; }
        int end = a->dep_scroll + DEPLOY_VISIBLE;
        if (end > a->cps.n_carry) end = a->cps.n_carry;
        for (int i = a->dep_scroll; i < end; i++) {
            SDL_Rect r = deploy_row_rect(a, i);
            if (SDL_PointInRect(&p, &r)) { a->dep_idx = i; deploy_toggle(a, i); return; }
        }
    }
}

static void deploy_update(App *a) { (void)a; }

static void deploy_draw(App *a)
{
    fill_rect(a, 0, 0, WIN_W, WIN_H, (SDL_Color){ 30, 38, 48, 255 });
    char buf[192];

    draw_text_center(a, a->font_l, WIN_W / 2, 56, COL_WHITE, tx("DEPLOY_TITLE"));
    snprintf(buf, sizeof buf, tx("DEPLOY_COUNT_FMT"),
             deploy_count(a), a->dep_limit, a->cps.n_carry);
    draw_text_center(a, a->font_m, WIN_W / 2, 100,
                     deploy_count(a) >= a->dep_limit ? COL_YELLOW : COL_WHITE, buf);
    draw_text_center(a, a->font_s, WIN_W / 2, 136, COL_GRAY, tx("DEPLOY_NOTE"));

    int end = a->dep_scroll + DEPLOY_VISIBLE;
    if (end > a->cps.n_carry) end = a->cps.n_carry;
    for (int i = a->dep_scroll; i < end; i++) {
        SDL_Rect r = deploy_row_rect(a, i);
        bool sel = (a->dep_idx == i);
        bool go = a->dep_sel[i] != 0;
        bool blocked = deploy_row_blocked(a, i);
        fill_rect(a, r.x, r.y, r.w, r.h,
                  blocked ? (SDL_Color){ 38, 34, 34, 255 }
                          : (sel ? (SDL_Color){ 80, 110, 160, 255 }
                                 : (go ? (SDL_Color){ 46, 62, 54, 255 }
                                       : (SDL_Color){ 44, 46, 52, 255 })));
        outline_rect(a, r.x, r.y, r.w, r.h,
                     blocked ? (SDL_Color){ 90, 70, 70, 255 }
                             : (sel ? COL_YELLOW
                                    : (go ? (SDL_Color){ 120, 200, 130, 255 }
                                          : COL_DIM)));
        /* 出撃するかのマーク（配置先が無い場合は理由を出す） */
        draw_text(a, a->font_s, r.x + 12, r.y + 8,
                  blocked ? (SDL_Color){ 200, 120, 120, 255 }
                          : (go ? (SDL_Color){ 150, 240, 150, 255 } : COL_DIM),
                  blocked ? tx("DEPLOY_MARK_NA")
                          : tx(go ? "DEPLOY_MARK_GO" : "DEPLOY_MARK_KEEP"));

        int t = a->cps.carry[i].type;
        const char *nm = (t < a->game.n_types) ? a->game.types[t].name : "?";
        int exp = a->cps.carry[i].exp;
        int rank = exp / 20; if (rank > 5) rank = 5;
        snprintf(buf, sizeof buf, "%s", nm);
        draw_text(a, a->font_s, r.x + 110, r.y + 8,
                  blocked ? COL_DIM : (go ? COL_WHITE : COL_GRAY), buf);
        snprintf(buf, sizeof buf, tx("DEPLOY_EXP_FMT"), exp, rank);
        draw_text(a, a->font_s, r.x + r.w - 220, r.y + 8,
                  blocked ? COL_DIM
                          : (rank > 0 ? (SDL_Color){ 160, 240, 160, 255 }
                                      : (go ? COL_WHITE : COL_GRAY)), buf);
    }
    if (a->dep_scroll > 0)
        draw_text_center(a, a->font_s, WIN_W / 2 + 300, 148, COL_YELLOW, "▲");
    if (a->dep_scroll + DEPLOY_VISIBLE < a->cps.n_carry)
        draw_text_center(a, a->font_s, WIN_W / 2, WIN_H - 104, COL_YELLOW, "▼");

    /* 出撃ボタン */
    SDL_Rect go = { WIN_W / 2 - 130, WIN_H - 74, 260, 44 };
    fill_rect(a, go.x, go.y, go.w, go.h, (SDL_Color){ 70, 100, 150, 255 });
    outline_rect(a, go.x, go.y, go.w, go.h, COL_YELLOW);
    draw_text_center(a, a->font_m, WIN_W / 2, go.y + 10, COL_WHITE,
                     tx("DEPLOY_GO"));
    draw_text_center(a, a->font_s, WIN_W / 2, WIN_H - 24, COL_DIM,
                     tx("DEPLOY_HINT"));
}

/* ------------------------------------------------------------------ */
/* 幕間（作戦前のひとこま）                                       */
/* ------------------------------------------------------------------ */
/* 全行を一枚に出して、進むのは1キーだけ。
 * 1行ずつ送る形式にしないのは、読み飛ばしたい人に
 * 連打を強いないため。話が無いノードはそのままブリーフィングへ抜ける。 */
/* タイプ表示は**文字単位**で切ること。バイトで切ると
 * 日本語（3バイト）の途中で切れて文字化けになる。 */
static int utf8_step(unsigned char c)
{
    return (c < 0x80) ? 1 : (c < 0xE0) ? 2 : (c < 0xF0) ? 3 : 4;
}
static int utf8_len(const char *s)
{
    int n = 0;
    for (int i = 0; s[i]; ) { i += utf8_step((unsigned char)s[i]); n++; }
    return n;
}
/* 先頭 n 文字分のバイト数 */
static int utf8_prefix(const char *s, int n)
{
    int i = 0;
    while (n > 0 && s[i]) { i += utf8_step((unsigned char)s[i]); n--; }
    return i;
}

#define STORY_FRAMES_PER_CHAR 2   /* 60fps で毎秒30文字 */

/* 今出すべき行群（作戦前 / 勝利直後） */
static const CpnLine *story_lines(const App *a, const CpnNode *node, int *n)
{
    *n = a->story_is_win ? node->n_story_win : node->n_story;
    return a->story_is_win ? node->story_win : node->story;
}

static int story_total_chars(const App *a, const CpnNode *node)
{
    int n = 0, cnt = 0;
    const CpnLine *L = story_lines(a, node, &cnt);
    for (int i = 0; i < cnt; i++) n += utf8_len(L[i].text);
    return n;
}

/* 今何文字まで出していいか */
static int story_shown(const App *a, const CpnNode *node)
{
    if (a->story_all) return story_total_chars(a, node);
    return a->story_frames / STORY_FRAMES_PER_CHAR;
}

static bool story_finished(const App *a, const CpnNode *node)
{
    return story_shown(a, node) >= story_total_chars(a, node);
}

static void story_go(App *a);

static void story_enter(App *a)
{
    snd_music(HWM_TITLE, true);
    brief_free_art(a);
    a->story_frames = 0;
    a->story_all = false;
    const CpnNode *node = campaign_find_node(&a->cpn, a->cps.node);
    int cnt = 0;
    if (node) story_lines(a, node, &cnt);
    if (!node || cnt == 0) {
        story_go(a);
        return;
    }
    if (node->art[0])
        a->brief_tex = sprite_load_file(a, node->art,
                                        &a->brief_tex_w, &a->brief_tex_h);
}

static void story_go(App *a)
{
    brief_free_art(a);
    /* 作戦前はブリーフィングへ、勝利直後はご褪美/結果へ戻る。 */
    if (!a->story_is_win) { a->next_screen = SCREEN_BRIEFING; return; }
    a->story_is_win = false;
    a->next_screen = reward_available(a) ? SCREEN_REWARD : SCREEN_RESULT;
}

static void story_event(App *a, const SDL_Event *e)
{
    bool pressed =
        (e->type == SDL_KEYDOWN &&
         (e->key.keysym.sym == SDLK_z || e->key.keysym.sym == SDLK_RETURN ||
          e->key.keysym.sym == SDLK_KP_ENTER ||
          e->key.keysym.sym == SDLK_x || e->key.keysym.sym == SDLK_ESCAPE ||
          e->key.keysym.sym == SDLK_SPACE)) ||
        e->type == SDL_MOUSEBUTTONDOWN;
    if (!pressed) return;

    const CpnNode *node = campaign_find_node(&a->cpn, a->cps.node);
    /* 流れている途中ならまず全文を出す。
     * この一拍が無いと、連打したときに読まずに飛ばしてしまう。 */
    if (node && !story_finished(a, node)) {
        a->story_all = true;
        snd_se(SE_CURSOR);
        return;
    }
    snd_se(SE_OK);
    story_go(a);
}

static void story_update(App *a)
{
    if (!a->story_all) a->story_frames++;
}

static void story_draw(App *a)
{
    fill_rect(a, 0, 0, WIN_W, WIN_H, (SDL_Color){ 18, 22, 28, 255 });
    const CpnNode *node = campaign_find_node(&a->cpn, a->cps.node);
    /* 話が無いノードは story_enter が即ブリーフィングへ送る。
     * 切り替わるのは次のフレームなので、その1枚を空の箱で出さない。 */
    if (!node) return;
    int cnt = 0;
    const CpnLine *L = story_lines(a, node, &cnt);

    /* 作戦の1枚絵を背景に敷く（暗く落とす）。
     * 話専用の絵を別に用意しなくても雰囲気が出る。 */
    if (a->brief_tex) {
        int dw = WIN_W;
        int dh = a->brief_tex_h * dw / (a->brief_tex_w ? a->brief_tex_w : 1);
        SDL_Rect dst = { 0, 60, dw, dh };
        SDL_SetTextureColorMod(a->brief_tex, 90, 96, 110);
        SDL_RenderCopy(a->ren, a->brief_tex, NULL, &dst);
        SDL_SetTextureColorMod(a->brief_tex, 255, 255, 255);
    }

    draw_text_center(a, a->font_s, WIN_W / 2, 40, COL_DIM, a->cpn.name);
    draw_text_center(a, a->font_l, WIN_W / 2, 62, COL_WHITE, node->title);
    if (cnt == 0) return;

    /* 本文の箱。行数に合わせて高さを決め、下寄せで置く。 */
    const int lh = 40;
    int bh = 40 + cnt * lh;
    int bx = 140, by = WIN_H - 118 - bh, bw = WIN_W - 280;
    if (by < 150) by = 150;
    fill_rect(a, bx, by, bw, bh, (SDL_Color){ 22, 26, 34, 235 });
    outline_rect(a, bx, by, bw, bh, COL_DIM);

    const SDL_Color WHO = { 150, 210, 255, 255 };   /* 話者名 */
    const SDL_Color LINE = { 232, 232, 226, 255 };  /* セリフ */
    const SDL_Color NARR = { 168, 172, 180, 255 };  /* 地の文 */
    int budget = story_shown(a, node);
    for (int i = 0; i < cnt; i++) {
        int len = utf8_len(L[i].text);
        if (budget <= 0) break;                 /* まだこの行は出ない */
        int show = budget < len ? budget : len;
        budget -= show;
        char part[160];
        int nb = utf8_prefix(L[i].text, show);
        if (nb > (int)sizeof part - 1) nb = (int)sizeof part - 1;
        memcpy(part, L[i].text, (size_t)nb);
        part[nb] = 0;

        int y = by + 20 + i * lh;
        if (L[i].who[0]) {
            char who[40];
            snprintf(who, sizeof who, "%s", L[i].who);
            draw_text(a, a->font_m, bx + 24, y, WHO, who);
            int w = text_width(a, a->font_m, who);
            char buf[200];
            /* 閉じ括弧は出し切ってから。途中で出すと行が伸び縮みして見える。 */
            snprintf(buf, sizeof buf, "「%s%s", part, show == len ? "」" : "");
            draw_text(a, a->font_m, bx + 24 + w + 6, y, LINE, buf);
        } else {
            draw_text(a, a->font_m, bx + 40, y, NARR, part);
        }
    }

    draw_text_center(a, a->font_s, WIN_W / 2, WIN_H - 60, COL_YELLOW,
                     story_finished(a, node)
                         ? tx(a->story_is_win ? "STORY_WIN_GO" : "STORY_GO")
                         : tx("STORY_SKIP"));
}

/* ------------------------------------------------------------------ */
const Screen SCREENS[SCREEN_COUNT] = {
    { title_enter,  title_event,  title_update,  title_draw  },
    { setup_enter,  setup_event,  setup_update,  setup_draw  },
    { load_enter,   load_event,   load_update,   load_draw   },
    { brief_enter,  brief_event,  brief_update,  brief_draw  },
    { opt_enter,    opt_event,    opt_update,    opt_draw    },
    { battle_enter, battle_event, battle_update, battle_draw },
    { result_enter, result_event, result_update, result_draw },
    { cpnmap_enter, cpnmap_event, cpnmap_update, cpnmap_draw },
    { reward_enter, reward_event, reward_update, reward_draw },
    { endroll_enter, endroll_event, endroll_update, endroll_draw },
    { deploy_enter,  deploy_event,  deploy_update,  deploy_draw  },
    { story_enter,   story_event,   story_update,   story_draw   },
};
