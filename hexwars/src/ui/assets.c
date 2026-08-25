/* assets.c - フォント読込・テキスト描画（キャッシュ付き） */
#include "app.h"
#include <stdio.h>
#include <string.h>

static TTF_Font *open_font(App *a, int pt)
{
    char path[600];
    snprintf(path, sizeof path, "%sassets/font/NotoSansJP.ttf", a->base_path);
    TTF_Font *f = TTF_OpenFont(path, pt);
    if (f) return f;
    /* フォールバック: Windows システムフォント */
    f = TTF_OpenFont("C:\\Windows\\Fonts\\meiryo.ttc", pt);
    if (f) return f;
    f = TTF_OpenFont("C:\\Windows\\Fonts\\YuGothM.ttc", pt);
    if (f) return f;
    /* Linux フォールバック */
    f = TTF_OpenFont("/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc", pt);
    return f;
}

int assets_init(App *a)
{
    char *bp = SDL_GetBasePath();
    if (bp) {
        snprintf(a->base_path, sizeof a->base_path, "%s", bp);
        SDL_free(bp);
    } else {
        a->base_path[0] = '\0';
    }
    /* 開発時: exe と同階層に data が無ければカレントを使う */
    {
        char probe[600];
        snprintf(probe, sizeof probe, "%sdata/units.def", a->base_path);
        FILE *f = fopen(probe, "rb");
        if (f) { fclose(f); }
        else   { a->base_path[0] = '\0'; }
    }

    a->font_s  = open_font(a, 13);
    a->font_m  = open_font(a, 17);
    a->font_l  = open_font(a, 24);
    a->font_xl = open_font(a, 40);
    if (!a->font_s || !a->font_m || !a->font_l || !a->font_xl) {
        SDL_Log("フォントを読み込めません: %s", TTF_GetError());
        return -1;
    }
    return 0;
}

/* ---- テキストキャッシュ ---- */
#define TC_SIZE 192
typedef struct {
    char key[96];
    TTF_Font *font;
    SDL_Color col;
    SDL_Texture *tex;
    int w, h;
    uint32_t last_used;
} TextEntry;

static TextEntry s_cache[TC_SIZE];
static uint32_t s_tick;

static TextEntry *get_text(App *a, TTF_Font *f, SDL_Color c, const char *s)
{
    s_tick++;
    int free_i = -1, oldest_i = 0;
    uint32_t oldest = 0xFFFFFFFFu;
    for (int i = 0; i < TC_SIZE; i++) {
        TextEntry *e = &s_cache[i];
        if (!e->tex) { if (free_i < 0) free_i = i; continue; }
        if (e->font == f && e->col.r == c.r && e->col.g == c.g &&
            e->col.b == c.b && !strncmp(e->key, s, sizeof e->key - 1)) {
            e->last_used = s_tick;
            return e;
        }
        if (e->last_used < oldest) { oldest = e->last_used; oldest_i = i; }
    }
    int slot = free_i >= 0 ? free_i : oldest_i;
    TextEntry *e = &s_cache[slot];
    if (e->tex) { SDL_DestroyTexture(e->tex); e->tex = NULL; }

    SDL_Surface *surf = TTF_RenderUTF8_Blended(f, s, c);
    if (!surf) return NULL;
    e->tex = SDL_CreateTextureFromSurface(a->ren, surf);
    e->w = surf->w; e->h = surf->h;
    SDL_FreeSurface(surf);
    if (!e->tex) return NULL;
    snprintf(e->key, sizeof e->key, "%s", s);
    e->font = f; e->col = c; e->last_used = s_tick;
    return e;
}

void draw_text(App *a, TTF_Font *f, int x, int y, SDL_Color c, const char *s)
{
    if (!s || !*s) return;
    TextEntry *e = get_text(a, f, c, s);
    if (!e) return;
    SDL_Rect dst = { x, y, e->w, e->h };
    SDL_RenderCopy(a->ren, e->tex, NULL, &dst);
}

void draw_text_center(App *a, TTF_Font *f, int cx, int y, SDL_Color c, const char *s)
{
    if (!s || !*s) return;
    TextEntry *e = get_text(a, f, c, s);
    if (!e) return;
    SDL_Rect dst = { cx - e->w / 2, y, e->w, e->h };
    SDL_RenderCopy(a->ren, e->tex, NULL, &dst);
}

int text_width(App *a, TTF_Font *f, const char *s)
{
    (void)a;
    int w = 0, h = 0;
    TTF_SizeUTF8(f, s, &w, &h);
    return w;
}

void assets_quit(App *a)
{
    for (int i = 0; i < TC_SIZE; i++)
        if (s_cache[i].tex) { SDL_DestroyTexture(s_cache[i].tex); s_cache[i].tex = NULL; }
    if (a->font_s)  TTF_CloseFont(a->font_s);
    if (a->font_m)  TTF_CloseFont(a->font_m);
    if (a->font_l)  TTF_CloseFont(a->font_l);
    if (a->font_xl) TTF_CloseFont(a->font_xl);
}
