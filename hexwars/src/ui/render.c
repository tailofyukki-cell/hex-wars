/* render.c - フラットベクタ風のマップ・ユニット描画 */
#include "app.h"
#include "sprites.h"
#include <math.h>
#include <stdio.h>

const SDL_Color COL_P[2] = {
    { 58, 110, 200, 255 },   /* P0 西方同盟: 青 */
    { 205,  72,  58, 255 },  /* P1 東方連邦: 赤 */
};
const SDL_Color COL_WHITE  = { 240, 240, 235, 255 };
const SDL_Color COL_BLACK  = {  25,  28,  30, 255 };
const SDL_Color COL_YELLOW = { 240, 210,  80, 255 };
const SDL_Color COL_GRAY   = { 150, 150, 148, 255 };
const SDL_Color COL_DIM    = {  90,  92,  95, 255 };

static const float ZOOM_SIZES[3] = { 16.0f, 24.0f, 32.0f };
#define SQRT3 1.7320508f

float hex_size(const App *a) { return ZOOM_SIZES[a->zoom]; }

void hex_center_px(const App *a, int x, int y, float *px, float *py)
{
    float s = hex_size(a);
    *px = SQRT3 * s * ((float)x + 0.5f * (float)(y & 1)) + SQRT3 * s * 0.5f
          - a->cam_x;
    *py = 1.5f * s * (float)y + s - a->cam_y;
}

bool px_to_hex(const App *a, int mx, int my, int *hx, int *hy)
{
    float s = hex_size(a);
    int gy = (int)floorf(((float)my + a->cam_y) / (1.5f * s));
    int gx = (int)floorf(((float)mx + a->cam_x) / (SQRT3 * s));
    float bd = 1e30f;
    int bx = -1, by = -1;
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            int x = gx + dx, y = gy + dy;
            if (!game_in_bounds(&a->game, x, y)) continue;
            float cx, cy;
            hex_center_px(a, x, y, &cx, &cy);
            float d = (cx - mx) * (cx - mx) + (cy - my) * (cy - my);
            if (d < bd) { bd = d; bx = x; by = y; }
        }
    }
    if (bx < 0 || bd > s * s * 1.2f) return false;
    *hx = bx; *hy = by;
    return true;
}

/* 頂点計算（ポイントトップ: 上が尖る） */
static void hex_corners(float cx, float cy, float size, SDL_FPoint out[6])
{
    for (int i = 0; i < 6; i++) {
        float ang = (float)(3.14159265 / 180.0) * (60.0f * (float)i - 90.0f);
        out[i].x = cx + size * cosf(ang);
        out[i].y = cy + size * sinf(ang);
    }
}

void render_fill_hex(App *a, float cx, float cy, float size, SDL_Color c)
{
    SDL_FPoint p[6];
    hex_corners(cx, cy, size, p);
    /* コーナーファン: (0,1,2)(0,2,3)(0,3,4)(0,4,5) */
    int tri[4][3] = { {0,1,2},{0,2,3},{0,3,4},{0,4,5} };
    SDL_Vertex verts[12];
    for (int t = 0; t < 4; t++) {
        for (int k = 0; k < 3; k++) {
            SDL_Vertex *vv = &verts[t * 3 + k];
            vv->position.x = p[tri[t][k]].x;
            vv->position.y = p[tri[t][k]].y;
            vv->color = c;
            vv->tex_coord.x = vv->tex_coord.y = 0;
        }
    }
    SDL_RenderGeometry(a->ren, NULL, verts, 12, NULL, 0);
}

void render_hex_outline(App *a, float cx, float cy, float size, SDL_Color c)
{
    SDL_FPoint p[7];
    hex_corners(cx, cy, size, p);
    p[6] = p[0];
    SDL_SetRenderDrawColor(a->ren, c.r, c.g, c.b, c.a);
    SDL_RenderDrawLinesF(a->ren, p, 7);
}

void fill_rect(App *a, int x, int y, int w, int h, SDL_Color c)
{
    SDL_SetRenderDrawColor(a->ren, c.r, c.g, c.b, c.a);
    SDL_Rect r = { x, y, w, h };
    SDL_RenderFillRect(a->ren, &r);
}

void outline_rect(App *a, int x, int y, int w, int h, SDL_Color c)
{
    SDL_SetRenderDrawColor(a->ren, c.r, c.g, c.b, c.a);
    SDL_Rect r = { x, y, w, h };
    SDL_RenderDrawRect(a->ren, &r);
}

static SDL_Color rgb(uint32_t v)
{
    SDL_Color c = { (Uint8)(v >> 16), (Uint8)(v >> 8), (Uint8)v, 255 };
    return c;
}

static SDL_Color darken(SDL_Color c, int pct)
{
    c.r = (Uint8)(c.r * pct / 100);
    c.g = (Uint8)(c.g * pct / 100);
    c.b = (Uint8)(c.b * pct / 100);
    return c;
}

/* 塗りつぶし円（ユニット本体用） */
static void fill_circle(App *a, float cx, float cy, float r, SDL_Color c)
{
#define SEG 20
    SDL_Vertex verts[SEG * 3];
    for (int i = 0; i < SEG; i++) {
        float a0 = 6.2831853f * (float)i / SEG;
        float a1 = 6.2831853f * (float)(i + 1) / SEG;
        SDL_Vertex *v = &verts[i * 3];
        v[0].position.x = cx; v[0].position.y = cy;
        v[1].position.x = cx + r * cosf(a0); v[1].position.y = cy + r * sinf(a0);
        v[2].position.x = cx + r * cosf(a1); v[2].position.y = cy + r * sinf(a1);
        for (int k = 0; k < 3; k++) {
            v[k].color = c;
            v[k].tex_coord.x = v[k].tex_coord.y = 0;
        }
    }
    SDL_RenderGeometry(a->ren, NULL, verts, SEG * 3, NULL, 0);
}

/* 塗りつぶし菱形（P1ユニット用: 色覚配慮の形状差） */
static void fill_diamond(App *a, float cx, float cy, float r, SDL_Color c)
{
    SDL_Vertex verts[6];
    SDL_FPoint p[4] = {
        { cx, cy - r }, { cx + r, cy }, { cx, cy + r }, { cx - r, cy }
    };
    int tri[2][3] = { {0,1,2},{0,2,3} };
    for (int t = 0; t < 2; t++)
        for (int k = 0; k < 3; k++) {
            SDL_Vertex *v = &verts[t * 3 + k];
            v->position.x = p[tri[t][k]].x;
            v->position.y = p[tri[t][k]].y;
            v->color = c;
            v->tex_coord.x = v->tex_coord.y = 0;
        }
    SDL_RenderGeometry(a->ren, NULL, verts, 6, NULL, 0);
}

static void draw_unit(App *a, const Unit *u, float cx, float cy)
{
    const Game *g = &a->game;
    const UnitType *t = &g->types[u->type];
    Layer L = unit_layer(t->mclass);
    float s = hex_size(a);
    float r = s * 0.62f;

    /* 立体化: 高度でヘクス内の描画位置をずらし、重なりを見分けられるようにする。
     * 空=上に浮かせ地面に影 / 海中=下に沈め半透明 / 地表海面=中央。 */
    float oy = (L == LAYER_AIR) ? -s * 0.30f
             : (L == LAYER_UNDER) ? s * 0.26f : 0.0f;
    if (L == LAYER_AIR)
        fill_circle(a, cx, cy + s * 0.18f, s * 0.22f, (SDL_Color){ 0, 0, 0, 90 });
    cy += oy;
    Uint8 alpha = (L == LAYER_UNDER) ? 170 : 255;   /* 海中は半透明 */

    SDL_Color body = COL_P[u->owner];
    bool done = (u->flags & UF_DONE) && u->owner == g->current;
    if (done) body = darken(body, 55);
    body.a = alpha;

    SDL_Texture *spr = sprite_get(a, u->type, u->owner);
    if (spr) {
        /* 画像スプライト: 陣営は下部のチップ（色+形状）で判別 */
        float side = s * 1.6f;
        SDL_FRect dst = { cx - side / 2, cy - side / 2, side, side };
        Uint8 mod = done ? 130 : 255;
        SDL_SetTextureColorMod(spr, mod, mod, mod);
        SDL_SetTextureAlphaMod(spr, alpha);
        SDL_RenderCopyF(a->ren, spr, NULL, &dst);
        SDL_SetTextureAlphaMod(spr, 255);
        if (u->owner == 0)
            fill_circle(a, cx - r * 0.75f, cy + r * 0.7f, s * 0.18f, body);
        else
            fill_diamond(a, cx - r * 0.75f, cy + r * 0.7f, s * 0.24f, body);
    } else {
        SDL_Color edge = darken(body, 60); edge.a = alpha;
        if (u->owner == 0) {
            fill_circle(a, cx, cy, r, edge);
            fill_circle(a, cx, cy, r - 2.0f, body);
        } else {
            fill_diamond(a, cx, cy, r + 2.0f, edge);
            fill_diamond(a, cx, cy, r, body);
        }
        /* 兵科アイコン文字。2文字以上のアイコンは円からはみ出すので1段小さい
         * フォントで描く（UTF-8の先頭バイト数＝文字数で判定）。 */
        int nchar = 0;
        for (const char *p = t->icon; *p; p++)
            if ((*p & 0xC0) != 0x80) nchar++;
        TTF_Font *f = a->zoom == 0 ? a->font_s : (a->zoom == 1 ? a->font_m : a->font_l);
        int dy = (a->zoom == 0 ? 9 : a->zoom == 1 ? 12 : 16);
        if (nchar > 1) {
            f = a->zoom == 0 ? a->font_s : (a->zoom == 1 ? a->font_s : a->font_m);
            dy = (a->zoom == 0 ? 7 : a->zoom == 1 ? 8 : 12);
        }
        draw_text_center(a, f, (int)cx, (int)(cy - dy), COL_WHITE, t->icon);
    }

    /* レイヤーバッジ: 右上に小さな色ドット（空=水色 / 海面=白 / 海中=青） */
    {
        SDL_Color badge = (L == LAYER_AIR)   ? (SDL_Color){ 120, 220, 255, 255 }
                        : (L == LAYER_UNDER) ? (SDL_Color){  70, 130, 255, 255 }
                                             : (SDL_Color){ 235, 235, 235, 255 };
        fill_circle(a, cx + r * 0.72f, cy - r * 0.72f, s * 0.12f, badge);
    }

    /* HP（10のときは省略） */
    if (u->hp < 10) {
        char hp[8];
        snprintf(hp, sizeof hp, "%d", u->hp);
        draw_text(a, a->font_s, (int)(cx + r * 0.35f), (int)(cy + r * 0.15f),
                  COL_YELLOW, hp);
    }
    /* 経験ランク */
    int rank = u->exp / 20;
    if (rank > 5) rank = 5;
    if (rank > 0) {
        char st[8];
        snprintf(st, sizeof st, "%d", rank);
        draw_text(a, a->font_s, (int)(cx - r), (int)(cy + r * 0.15f),
                  (SDL_Color){ 160, 240, 160, 255 }, st);
    }
    /* 搭載中マーク */
    if (u->cargo[0] >= 0 || u->cargo[1] >= 0)
        fill_circle(a, cx, cy + r * 0.85f, s * 0.11f, COL_YELLOW);
}

void render_map(App *a)
{
    Game *g = &a->game;
    float s = hex_size(a);
    int viewer = g->current;
    /* 人間視点: CPU手番中は人間側の視界で描画 */
    for (int p = 0; p < MAX_PLAYERS; p++)
        if (g->ctrl[g->current] != CTRL_HUMAN && g->ctrl[p] == CTRL_HUMAN)
            viewer = p;

    int ww, wh;
    SDL_GetRendererOutputSize(a->ren, &ww, &wh);

    for (int y = 0; y < g->h; y++) {
        for (int x = 0; x < g->w; x++) {
            float cx, cy;
            hex_center_px(a, x, y, &cx, &cy);
            if (cx < -2 * s || cy < -2 * s || cx > ww + 2 * s || cy > wh + 2 * s)
                continue;

            const Tile *tile = &g->tiles[y][x];
            const TerrainType *t = &g->terrains[tile->terrain];
            if (t->chr == 'x')
                continue;   /* 圏外は描かない（マップ輪郭を自由な形にする） */
            SDL_Color col = rgb(t->color);
            bool vis = !g->fog || g->visible[viewer][y][x];
            if (!vis) col = darken(col, 45);

            render_fill_hex(a, cx, cy, s - 0.5f, col);
            render_hex_outline(a, cx, cy, s - 0.5f, darken(col, 70));

            /* 建物: 所有者色の屋根形マーク + 地形1文字 */
            if (t->capturable) {
                SDL_Color oc = tile->owner >= 0 ? COL_P[tile->owner] : COL_GRAY;
                if (!vis) oc = darken(oc, 50);
                fill_circle(a, cx, cy - s * 0.45f, s * 0.2f, oc);
                TTF_Font *f = a->zoom == 0 ? a->font_s : a->font_m;
                char icon[8] = { 0 };
                /* 地形名の先頭1文字(UTF-8 3バイト想定) */
                icon[0] = t->name[0]; icon[1] = t->name[1]; icon[2] = t->name[2];
                SDL_Color tc = darken(rgb(t->color), 45);
                if (t->is_hq) tc = (SDL_Color){ 120, 60, 20, 255 };
                draw_text_center(a, f, (int)cx, (int)(cy - s * 0.32f), tc, icon);
                /* 占領進行中 */
                if (vis && tile->capturer >= 0 && tile->cap_hp < CAPTURE_HP) {
                    char pr[16];
                    snprintf(pr, sizeof pr, "%d/20", tile->cap_hp);
                    draw_text_center(a, a->font_s, (int)cx, (int)(cy + s * 0.15f),
                                     COL_YELLOW, pr);
                }
            }
        }
    }

    /* ユニット: 立体化のため高度順（海中→海面→空）に描き、空を最前面にする */
    static const Layer draw_order[LAYER_COUNT] = {
        LAYER_UNDER, LAYER_SURFACE, LAYER_AIR
    };
    for (int pass = 0; pass < LAYER_COUNT; pass++) {
        Layer L = draw_order[pass];
        for (int i = 0; i < g->n_units; i++) {
            const Unit *u = &g->units[i];
            if (!(u->flags & UF_ALIVE)) continue;
            if (unit_layer(g->types[u->type].mclass) != L) continue;
            if (!game_unit_visible_to(g, viewer, u)) continue;
            float cx, cy;
            hex_center_px(a, u->pos.x, u->pos.y, &cx, &cy);
            if (cx < -2 * s || cy < -2 * s || cx > ww + 2 * s || cy > wh + 2 * s)
                continue;
            draw_unit(a, u, cx, cy);
        }
    }
}

void battle_add_popup(App *a, int hx, int hy, const char *text, SDL_Color c)
{
    for (int i = 0; i < MAX_POPUPS; i++) {
        Popup *p = &a->popups[i];
        if (p->timer > 0) continue;
        p->x = (float)hx; p->y = (float)hy;
        p->timer = 70;
        snprintf(p->text, sizeof p->text, "%s", text);
        p->color = c;
        return;
    }
}
