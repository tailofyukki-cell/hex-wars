/* sprites.c - units.def の image= で指定されたPNGを読み込んで描画に使う */
#include "sprites.h"
#include "app.h"
#include <SDL_image.h>
#include <stdio.h>
#include <string.h>

static bool s_img_ready = false;

/* キャッシュ: 0=未試行 / 1=読込済み / -1=失敗（毎フレームの再試行を防ぐ） */
static SDL_Texture *s_tex[MAX_UNIT_TYPES][2];
static int8_t       s_state[MAX_UNIT_TYPES][2];

/* 地形セル画像のキャッシュ（ユニットの s_tex と同じ 0/1/-1 方式） */
static SDL_Texture *s_terr_tex[MAX_TERRAIN];
static int8_t       s_terr_state[MAX_TERRAIN];

/* パス指定の1枚絵（カットイン等）のキャッシュ。読めなかったパスも
 * tex=NULL のまま覚えておき、毎フレーム読み直さないようにする。 */
#define MAX_PATH_TEX 16
static struct {
    char         rel[64];
    SDL_Texture *tex;
    int          w, h;
} s_ptex[MAX_PATH_TEX];
static int s_ptex_n;

int sprites_init(void)
{
    int want = IMG_INIT_PNG;
    if ((IMG_Init(want) & want) != want) {
        SDL_Log("SDL2_image 初期化失敗: %s（画像なしで続行）", IMG_GetError());
        s_img_ready = false;
        return -1;
    }
    s_img_ready = true;
    return 0;
}

void sprites_clear(void)
{
    for (int i = 0; i < s_ptex_n; i++)
        if (s_ptex[i].tex) SDL_DestroyTexture(s_ptex[i].tex);
    memset(s_ptex, 0, sizeof s_ptex);
    s_ptex_n = 0;
    for (int t = 0; t < MAX_TERRAIN; t++) {
        if (s_terr_tex[t]) SDL_DestroyTexture(s_terr_tex[t]);
        s_terr_tex[t] = NULL;
        s_terr_state[t] = 0;
    }
    for (int t = 0; t < MAX_UNIT_TYPES; t++)
        for (int o = 0; o < 2; o++) {
            if (s_tex[t][o]) SDL_DestroyTexture(s_tex[t][o]);
            s_tex[t][o] = NULL;
            s_state[t][o] = 0;
        }
}

void sprites_quit(void)
{
    sprites_clear();
    if (s_img_ready) IMG_Quit();
    s_img_ready = false;
}

SDL_Texture *sprite_load_file(App *a, const char *rel, int *w, int *h)
{
    if (!s_img_ready || !rel || !rel[0]) return NULL;
    char path[600];
    snprintf(path, sizeof path, "%sassets/%s", a->base_path, rel);
    SDL_Surface *surf = IMG_Load(path);
    if (!surf) return NULL;
    if (w) *w = surf->w;
    if (h) *h = surf->h;
    SDL_Texture *tex = SDL_CreateTextureFromSurface(a->ren, surf);
    SDL_FreeSurface(surf);
    if (tex) {
        SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(tex, SDL_ScaleModeLinear);
    }
    return tex;
}

SDL_Texture *sprite_get(App *a, int type, int owner)
{
    if (!s_img_ready) return NULL;
    if (type < 0 || type >= a->game.n_types) return NULL;
    owner = owner ? 1 : 0;

    if (s_state[type][owner] != 0)
        return s_tex[type][owner];

    const char *rel = a->game.types[type].image[owner];
    if (!rel[0]) {
        s_state[type][owner] = -1;
        return NULL;
    }
    char path[600];
    snprintf(path, sizeof path, "%sassets/%s", a->base_path, rel);
    SDL_Surface *surf = IMG_Load(path);
    if (!surf) {
        SDL_Log("ユニット画像読込失敗: %s (%s)", path, IMG_GetError());
        s_state[type][owner] = -1;
        return NULL;
    }
    SDL_Texture *tex = SDL_CreateTextureFromSurface(a->ren, surf);
    SDL_FreeSurface(surf);
    if (!tex) {
        s_state[type][owner] = -1;
        return NULL;
    }
    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(tex, SDL_ScaleModeLinear);
    s_tex[type][owner] = tex;
    s_state[type][owner] = 1;
    return tex;
}

SDL_Texture *sprite_get_path(App *a, const char *rel, int *w, int *h)
{
    if (!s_img_ready || !rel || !rel[0]) return NULL;
    for (int i = 0; i < s_ptex_n; i++)
        if (!strcmp(s_ptex[i].rel, rel)) {
            if (w) *w = s_ptex[i].w;
            if (h) *h = s_ptex[i].h;
            return s_ptex[i].tex;        /* 失敗済みなら NULL が返る */
        }
    if (s_ptex_n >= MAX_PATH_TEX) return NULL;

    int tw = 0, th = 0;
    SDL_Texture *tex = sprite_load_file(a, rel, &tw, &th);
    if (!tex) SDL_Log("カットイン画像読込失敗: assets/%s", rel);
    snprintf(s_ptex[s_ptex_n].rel, sizeof s_ptex[s_ptex_n].rel, "%s", rel);
    s_ptex[s_ptex_n].tex = tex;
    s_ptex[s_ptex_n].w = tw;
    s_ptex[s_ptex_n].h = th;
    s_ptex_n++;
    if (w) *w = tw;
    if (h) *h = th;
    return tex;
}

SDL_Texture *terrain_tex_get(App *a, int terrain)
{
    if (!s_img_ready) return NULL;
    if (terrain < 0 || terrain >= a->game.n_terrains) return NULL;
    if (s_terr_state[terrain] != 0) return s_terr_tex[terrain];

    const char *rel = a->game.terrains[terrain].image;
    if (!rel[0]) { s_terr_state[terrain] = -1; return NULL; }

    SDL_Texture *tex = sprite_load_file(a, rel, NULL, NULL);
    if (!tex) {
        SDL_Log("地形画像読込失敗: assets/%s", rel);
        s_terr_state[terrain] = -1;
        return NULL;
    }
    s_terr_tex[terrain] = tex;
    s_terr_state[terrain] = 1;
    return tex;
}
