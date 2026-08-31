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

/* --- 斜め見下ろし表示（2.5D） ---
 * 真上からの平面図を「Y軸だけ潰す」ことで斜めから見たように見せ、さらに地形ごとの
 * 起伏（terrain.def の height）でタイルを持ち上げて側面（崖）を描く。
 * ルールには一切影響しない純粋な見た目の変換で、a->opt_tilt==0 なら従来の平面図。
 * 起伏は height を「ヘクス半径32px基準のpx」として扱い、ズーム倍率で拡縮する。 */
#define TILT_SQUASH  0.60f   /* Y軸の圧縮率 */
#define TILT_DEPTH   10.0f   /* 全タイル共通の厚み（s=32基準px） */

float hex_size(const App *a) { return ZOOM_SIZES[a->zoom]; }

/* Y方向の圧縮率（平面表示なら1.0） */
static float tilt_squash(const App *a) { return a->opt_tilt ? TILT_SQUASH : 1.0f; }
float hex_tilt_squash(const App *a) { return tilt_squash(a); }

/* ヘクス(x,y)の起伏を画面px換算で返す（平面表示なら0） */
static float tile_lift(const App *a, int x, int y)
{
    if (!a->opt_tilt) return 0.0f;
    const Game *g = &a->game;
    if (!game_in_bounds(g, x, y)) return 0.0f;
    const TerrainType *t = &g->terrains[g->tiles[y][x].terrain];
    return (float)t->height * (hex_size(a) / 32.0f);
}

/* 起伏を無視した素の中心（ピック計算やカメラ基準に使う） */
static void hex_center_flat(const App *a, int x, int y, float *px, float *py)
{
    float s = hex_size(a);
    *px = SQRT3 * s * ((float)x + 0.5f * (float)(y & 1)) + SQRT3 * s * 0.5f
          - a->cam_x;
    *py = (1.5f * s * (float)y + s) * tilt_squash(a) - a->cam_y;
}

void hex_center_px(const App *a, int x, int y, float *px, float *py)
{
    hex_center_flat(a, x, y, px, py);
    *py -= tile_lift(a, x, y);   /* 高い地形ほど上へ持ち上がる */
}

/* 頂点計算（ポイントトップ: 上が尖る）。squash はY方向の圧縮率。 */
static void hex_corners_sq(float cx, float cy, float size, float squash,
                           SDL_FPoint out[6])
{
    for (int i = 0; i < 6; i++) {
        float ang = (float)(3.14159265 / 180.0) * (60.0f * (float)i - 90.0f);
        out[i].x = cx + size * cosf(ang);
        out[i].y = cy + size * sinf(ang) * squash;
    }
}

/* 点(mx,my)が中心(cx,cy)の（潰した）六角形の内側か */
static bool point_in_hex(float mx, float my, float cx, float cy,
                         float size, float squash)
{
    float dx = fabsf(mx - cx);
    float dy = fabsf((my - cy) / (squash > 0.01f ? squash : 1.0f));
    if (dx > size * 0.8660254f || dy > size) return false;
    /* 斜辺: y = size - (dx / (√3/2*size)) * (size/2) の内側 */
    return dy <= size - dx * 0.5773503f;
}

/* 描画時にユニットをヘクス中心からどれだけずらすか（draw_unit と同じ規則）。
 * クリック判定を見た目に一致させるため両者で共有する。 */
static float unit_draw_oy(const App *a, Layer L, float s)
{
    float oy = (L == LAYER_AIR) ? -s * 0.30f
             : (L == LAYER_UNDER) ? s * 0.26f : 0.0f;
    if (a->opt_tilt && L == LAYER_SURFACE) oy -= s * 0.26f;
    return oy;
}

/* 描画に使う視点プレイヤー（CPU手番中は人間側の視界で描く） */
static int render_viewer(const Game *g)
{
    int viewer = g->current;
    for (int p = 0; p < MAX_PLAYERS; p++)
        if (g->ctrl[g->current] != CTRL_HUMAN && g->ctrl[p] == CTRL_HUMAN)
            viewer = p;
    return viewer;
}

/* 斜め表示ではヘクスがY方向に潰れる一方でユニットの絵は縮まないため、絵が
 * タイルからはみ出す。絵の上をクリックしたら「そのユニットのヘクス」を返さないと
 * 1つ奥のヘクスが選ばれてしまうので、タイル判定より先にユニットを当たり判定する。
 * 見えている順（空→海面→海中、同レイヤーなら手前=y大）に見る。 */
static bool pick_unit_hex(const App *a, int mx, int my, int *hx, int *hy)
{
    const Game *g = &a->game;
    float s = hex_size(a);
    int viewer = render_viewer(g);
    static const Layer order[LAYER_COUNT] = {
        LAYER_AIR, LAYER_SURFACE, LAYER_UNDER
    };
    for (int pass = 0; pass < LAYER_COUNT; pass++) {
        int best = -1, besty = -1;
        for (int i = 0; i < g->n_units; i++) {
            const Unit *u = &g->units[i];
            if (!(u->flags & UF_ALIVE) || (u->flags & UF_LOADED)) continue;
            if (unit_layer(g->types[u->type].mclass) != order[pass]) continue;
            if (!game_unit_visible_to(g, viewer, u)) continue;
            float cx, cy;
            hex_center_px(a, u->pos.x, u->pos.y, &cx, &cy);
            cy += unit_draw_oy(a, order[pass], s);
            /* 絵の当たり判定は本体まわりだけに絞る（スプライトの余白で
             * 隣のヘクスを奪わないよう、描画サイズより一回り小さく取る） */
            float rx = s * 0.62f, ry = s * 0.62f;
            if (mx < cx - rx || mx > cx + rx || my < cy - ry || my > cy + ry)
                continue;
            if (u->pos.y > besty) { besty = u->pos.y; best = i; }
        }
        if (best >= 0) {
            *hx = g->units[best].pos.x;
            *hy = g->units[best].pos.y;
            return true;
        }
    }
    return false;
}

bool px_to_hex(const App *a, int mx, int my, int *hx, int *hy)
{
    /* 斜め表示のみ: まず「絵の上をクリックしたか」を見る（上のコメント参照）。
     * 平面表示は絵がヘクスに収まるので従来どおりタイルだけで判定する。 */
    if (a->opt_tilt && pick_unit_hex(a, mx, my, hx, hy)) return true;

    float s = hex_size(a);
    float sq = tilt_squash(a);
    int gy = (int)floorf(((float)my + a->cam_y) / (1.5f * s * sq));
    int gx = (int)floorf(((float)mx + a->cam_x) / (SQRT3 * s));

    /* 起伏があるとタイルが持ち上がって手前の行に食い込むので、探索窓を上下に広げ、
     * 手前(y大)から奥(y小)へ＝描画順の逆に見て最初に当たったものを選ぶ
     * （＝実際に画面で一番手前に見えているタイルが取れる）。 */
    int up = a->opt_tilt ? 4 : 1;
    for (int dy = up; dy >= -up; dy--) {
        for (int dx = -1; dx <= 1; dx++) {
            int x = gx + dx, y = gy + dy;
            if (!game_in_bounds(&a->game, x, y)) continue;
            if (a->game.terrains[a->game.tiles[y][x].terrain].chr == 'x') continue;
            float cx, cy;
            hex_center_px(a, x, y, &cx, &cy);
            if (point_in_hex((float)mx, (float)my, cx, cy, s - 0.5f, sq)) {
                *hx = x; *hy = y;
                return true;
            }
        }
    }
    /* どの六角形にも入らなかった: 従来どおり最寄り中心で拾う（辺の隙間の保険） */
    float bd = 1e30f;
    int bx = -1, by = -1;
    for (int dy = -up; dy <= up; dy++) {
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

static void fill_hex_sq(App *a, float cx, float cy, float size, float squash,
                        SDL_Color c)
{
    SDL_FPoint p[6];
    hex_corners_sq(cx, cy, size, squash, p);
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

/* 画像を六角形の内側にクリップして貼る。
 * UVは角度だけで決まる（六角形の外接矩形に画像を合わせる）ので定数表で持てる。
 * 画像は正方形で、中央にセル1枚ぶんを描いたものを想定する。
 * 頂点色は乗算されるので、索敵で暗くするときはここに暗い色を渡す。 */
static void fill_hex_tex(App *a, float cx, float cy, float size, float squash,
                         SDL_Texture *tex, SDL_Color mod)
{
    /* corner i の uv = (0.5 + cos(a)/√3, 0.5 + sin(a)/2), a = 60i-90° */
    static const SDL_FPoint UV[6] = {
        { 0.5f, 0.0f }, { 1.0f, 0.25f }, { 1.0f, 0.75f },
        { 0.5f, 1.0f }, { 0.0f, 0.75f }, { 0.0f, 0.25f },
    };
    SDL_FPoint p[6];
    hex_corners_sq(cx, cy, size, squash, p);
    int tri[4][3] = { {0,1,2},{0,2,3},{0,3,4},{0,4,5} };
    SDL_Vertex verts[12];
    for (int t = 0; t < 4; t++) {
        for (int k = 0; k < 3; k++) {
            int ci = tri[t][k];
            SDL_Vertex *vv = &verts[t * 3 + k];
            vv->position.x = p[ci].x;
            vv->position.y = p[ci].y;
            vv->color = mod;
            vv->tex_coord = UV[ci];
        }
    }
    SDL_RenderGeometry(a->ren, tex, verts, 12, NULL, 0);
}

static void hex_outline_sq(App *a, float cx, float cy, float size, float squash,
                           SDL_Color c)
{
    SDL_FPoint p[7];
    hex_corners_sq(cx, cy, size, squash, p);
    p[6] = p[0];
    SDL_SetRenderDrawColor(a->ren, c.r, c.g, c.b, c.a);
    SDL_RenderDrawLinesF(a->ren, p, 7);
}

/* UI装飾用（タイトル画面・作戦全体図など）。常に潰さない真円状の六角形。 */
void render_fill_hex(App *a, float cx, float cy, float size, SDL_Color c)
{
    fill_hex_sq(a, cx, cy, size, 1.0f, c);
}

void render_hex_outline(App *a, float cx, float cy, float size, SDL_Color c)
{
    hex_outline_sq(a, cx, cy, size, 1.0f, c);
}

/* マップ上のヘクス用。斜め表示ならY方向に潰れる。 */
void render_fill_hex_map(App *a, float cx, float cy, float size, SDL_Color c)
{
    fill_hex_sq(a, cx, cy, size, tilt_squash(a), c);
}

void render_hex_outline_map(App *a, float cx, float cy, float size, SDL_Color c)
{
    hex_outline_sq(a, cx, cy, size, tilt_squash(a), c);
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

/* darken と違い pct>100（明るくする）でも 255 で頭打ちにする。
 * 起伏の陰影で天面を明るくする用途では飽和させないと Uint8 が巻き戻る。 */
static SDL_Color shade(SDL_Color c, int pct)
{
    int r = c.r * pct / 100, g = c.g * pct / 100, b = c.b * pct / 100;
    c.r = (Uint8)(r > 255 ? 255 : r);
    c.g = (Uint8)(g > 255 ? 255 : g);
    c.b = (Uint8)(b > 255 ? 255 : b);
    return c;
}

/* タイルの手前側面（崖）。ポイントトップ六角形の手前3辺
 * （右上→右下→下→左下）を下方向へ dep px 押し出した帯を塗る。 */
static void draw_tile_side(App *a, float cx, float cy, float size, float dep,
                           SDL_Color c)
{
    SDL_FPoint p[6];
    hex_corners_sq(cx, cy, size, TILT_SQUASH, p);
    SDL_Vertex verts[18];
    int n = 0;
    for (int e = 1; e <= 3; e++) {          /* 辺 p1-p2, p2-p3, p3-p4 */
        SDL_FPoint t0 = p[e], t1 = p[e + 1];
        SDL_FPoint b0 = { t0.x, t0.y + dep }, b1 = { t1.x, t1.y + dep };
        SDL_FPoint quad[6] = { t0, t1, b1, t0, b1, b0 };
        for (int k = 0; k < 6; k++) {
            verts[n].position.x = quad[k].x;
            verts[n].position.y = quad[k].y;
            verts[n].color = c;
            verts[n].tex_coord.x = verts[n].tex_coord.y = 0;
            n++;
        }
    }
    SDL_RenderGeometry(a->ren, NULL, verts, n, NULL, 0);
}

/* 塗りつぶし楕円（接地影など） */
static void fill_ellipse(App *a, float cx, float cy, float rx, float ry,
                         SDL_Color c)
{
#define SEG 20
    SDL_Vertex verts[SEG * 3];
    for (int i = 0; i < SEG; i++) {
        float a0 = 6.2831853f * (float)i / SEG;
        float a1 = 6.2831853f * (float)(i + 1) / SEG;
        SDL_Vertex *v = &verts[i * 3];
        v[0].position.x = cx; v[0].position.y = cy;
        v[1].position.x = cx + rx * cosf(a0); v[1].position.y = cy + ry * sinf(a0);
        v[2].position.x = cx + rx * cosf(a1); v[2].position.y = cy + ry * sinf(a1);
        for (int k = 0; k < 3; k++) {
            v[k].color = c;
            v[k].tex_coord.x = v[k].tex_coord.y = 0;
        }
    }
    SDL_RenderGeometry(a->ren, NULL, verts, SEG * 3, NULL, 0);
}

/* 塗りつぶし円（ユニット本体用） */
static void fill_circle(App *a, float cx, float cy, float r, SDL_Color c)
{
    fill_ellipse(a, cx, cy, r, r, c);
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
    float oy = unit_draw_oy(a, L, s);
    if (L == LAYER_AIR)
        fill_ellipse(a, cx, cy + s * 0.18f, s * 0.22f,
                     s * 0.22f * hex_tilt_squash(a),
                     (SDL_Color){ 0, 0, 0, 90 });
    /* 斜め表示: 地表のユニットはタイルの上に「立たせ」、足元に潰した影を落とす。
     * 空・海中は上の oy で既にずらしているので二重に持ち上げない。 */
    if (a->opt_tilt && L == LAYER_SURFACE)
        fill_ellipse(a, cx, cy + s * 0.10f, r * 0.92f, r * 0.32f,
                     (SDL_Color){ 0, 0, 0, 95 });
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
    {
        bool carrying = false;
        for (int cs = 0; cs < MAX_CARGO && !carrying; cs++)
            if (u->cargo[cs] >= 0) carrying = true;
        if (carrying)
            fill_circle(a, cx, cy + r * 0.85f, s * 0.11f, COL_YELLOW);
    }
}

void render_map(App *a)
{
    Game *g = &a->game;
    float s = hex_size(a);
    int viewer = render_viewer(g);   /* CPU手番中は人間側の視界で描画 */

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
            /* 明暗は「％」で一本化する。単色なら色に、画像なら頂点色に同じ％を掛ける
             * （色どうしの比を取ると明るい側で桁溢れするので、％で持つのが安全）。 */
            int pct = 100;
            if (a->opt_tilt && t->height != 0) {
                int h = t->height;
                pct += (h > 0 ? (h > 18 ? 18 : h) : (h < -6 ? -12 : h * 2));
            }
            if (!vis) pct = pct * 45 / 100;
            if (!vis) col = darken(col, 45);

            /* 斜め表示: タイルの側面（崖）を先に描いて厚みを出す。
             * 起伏が高いほど側面が長くなり、山や丘が盛り上がって見える。 */
            if (a->opt_tilt) {
                float lift = tile_lift(a, x, y);
                float dep = TILT_DEPTH * (s / 32.0f) + lift;
                if (dep > 1.0f)
                    draw_tile_side(a, cx, cy, s - 0.5f, dep,
                                   shade(col, lift > s * 0.25f ? 66 : 52));
            }

            /* 天面: 高い地形は少し明るく、沈む地形は少し暗く＝陰影で起伏を強調。
             * col は索敵の暗転を織り込み済みなので、ここでは起伏ぶんだけ掛ける。 */
            SDL_Color top = col;
            if (a->opt_tilt && t->height != 0) {
                int h = t->height;
                top = shade(col, 100 + (h > 0 ? (h > 18 ? 18 : h)
                                              : (h < -6 ? -12 : h * 2)));
            }
            /* セル画像（terrain.def の image=）があればそれを六角形に貼る。
             * 無ければ従来どおり color の単色で塗る。頂点色は乗算なので、
             * 起伏の陰影も索敵の暗転も同じ％で画像のまま効く。 */
            SDL_Texture *ttex = terrain_tex_get(a, tile->terrain);
            SDL_Color tmod = shade((SDL_Color){ 255, 255, 255, 255 }, pct);
            if (a->opt_tilt) {
                /* 斜め表示では六角形の上下の辺が寝るので、線で縁取ると
                 * ジャギーが目立つ。一回り大きい六角形を縁色で塗り、その上に
                 * 天面を重ねてリング状の境界を作る（面で塗るほうが滑らか）。 */
                render_fill_hex_map(a, cx, cy, s - 0.5f, shade(col, 74));
                if (ttex)
                    fill_hex_tex(a, cx, cy, s - 0.5f - s * 0.06f, TILT_SQUASH,
                                 ttex, tmod);
                else
                    render_fill_hex_map(a, cx, cy, s - 0.5f - s * 0.06f, top);
            } else if (ttex) {
                fill_hex_tex(a, cx, cy, s - 0.5f, 1.0f, ttex, tmod);
                render_hex_outline_map(a, cx, cy, s - 0.5f, shade(col, 70));
            } else {
                render_fill_hex_map(a, cx, cy, s - 0.5f, top);
                render_hex_outline_map(a, cx, cy, s - 0.5f, shade(col, 70));
            }

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

/* --- 天候の見た目（仕様書外の演出。ルールには一切影響しない） ---
 * 天候は上のバーの文字だけだと見落とすので、画面全体の色味と
 * 動く要素で「パッと見て分かる」ようにする。
 *   晴  … 暖色の淡いウォッシュ＋右上の陽光
 *   曇り… 寒色の淡いウォッシュ＋ゆっくり流れる雲の影
 *   雨  … さらに暗い寒色＋濃い雲の影＋斜めに降る雨
 * 雲と雨は乱数を持たずに index から決めるので、状態を持たず毎フレーム同じ
 * 見た目を再現できる（セーブ・ロードでも破綻しない）。 */

static uint32_t wx_hash(uint32_t x)
{
    x ^= x >> 16; x *= 0x7feb352dU;
    x ^= x >> 15; x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

/* 中心が濃く外周が透明になる円のテクスチャ。雲の影と陽光に使い回す。 */
static SDL_Texture *wx_blob_tex(App *a)
{
    if (a->wx_blob) return a->wx_blob;
    const int N = 128;
    SDL_Surface *sf = SDL_CreateRGBSurfaceWithFormat(0, N, N, 32,
                                                     SDL_PIXELFORMAT_RGBA32);
    if (!sf) return NULL;
    Uint32 *px = (Uint32 *)sf->pixels;
    int pitch = sf->pitch / 4;
    for (int y = 0; y < N; y++)
        for (int x = 0; x < N; x++) {
            float dx = (x - N / 2.0f) / (N / 2.0f);
            float dy = (y - N / 2.0f) / (N / 2.0f);
            float d = sqrtf(dx * dx + dy * dy);
            /* 中心付近は濃いまま、外周だけなだらかに消す。
             * (1-d)^2 だと平均の濃さが足りず、画面上でほぼ見えなかった。 */
            float v = d >= 1.0f ? 0.0f : (1.0f - d) * 1.7f;
            if (v > 1.0f) v = 1.0f;
            Uint8 al = (Uint8)(v * 255.0f);
            px[y * pitch + x] = ((Uint32)al << 24) | 0x00ffffffU;
        }
    a->wx_blob = SDL_CreateTextureFromSurface(a->ren, sf);
    SDL_FreeSurface(sf);
    if (a->wx_blob) SDL_SetTextureBlendMode(a->wx_blob, SDL_BLENDMODE_BLEND);
    return a->wx_blob;
}

static void wx_blob(App *a, float cx, float cy, float rx, float ry, SDL_Color c)
{
    SDL_Texture *t = wx_blob_tex(a);
    if (!t) return;
    SDL_SetTextureColorMod(t, c.r, c.g, c.b);
    SDL_SetTextureAlphaMod(t, c.a);
    SDL_FRect d = { cx - rx, cy - ry, rx * 2, ry * 2 };
    SDL_RenderCopyF(a->ren, t, NULL, &d);
}

/* 雲の影を横に流す。n を増やすほど厚い雲になる。 */
static void wx_clouds(App *a, int n, SDL_Color c, uint32_t frame)
{
    for (int i = 0; i < n; i++) {
        uint32_t h = wx_hash((uint32_t)i * 2654435761U);
        float rx = 120.0f + (float)(h % 170);
        float ry = rx * 0.40f;
        float sp = 0.20f + (float)((h >> 8) % 100) / 260.0f;   /* 大きい雲ほど遅くはしない */
        float span = WIN_W + rx * 2;
        float x = fmodf((float)frame * sp + (float)((h >> 4) % 4096), span) - rx;
        float y = (float)(TOPBAR_FX + (int)((h >> 16) % (uint32_t)(WIN_H - TOPBAR_FX)));
        wx_blob(a, x, y, rx, ry, c);
    }
}

/* 画面全体の色調を乗算で変える。
 * 半透明の板を重ねるだけだと色が浅くなるだけで暗くならず、
 * 晴と曇りの区別がつかなかった。乗算なら地形の緑が実際にくすむ。 */
static void wx_grade(App *a, SDL_Rect v, Uint8 r, Uint8 g, Uint8 b)
{
    SDL_SetRenderDrawBlendMode(a->ren, SDL_BLENDMODE_MOD);
    SDL_SetRenderDrawColor(a->ren, r, g, b, 255);
    SDL_RenderFillRect(a->ren, &v);
    SDL_SetRenderDrawBlendMode(a->ren, SDL_BLENDMODE_BLEND);
}

void render_weather_fx(App *a, int weather, uint32_t frame)
{
    if (!a->opt_weather_fx) return;
    SDL_Rect view = { 0, TOPBAR_FX, WIN_W, WIN_H - TOPBAR_FX };

    /* 夜は深い青を乗せる。月明かりを左上に置いて真っ黒を避ける。 */
    bool nite = game_is_night(&a->game);
    if (nite) {
        wx_grade(a, view, 86, 104, 158);
        wx_blob(a, 150.0f, TOPBAR_FX + 40.0f, 300.0f, 220.0f,
                (SDL_Color){ 150, 175, 235, 26 });
    }

    if (!a->game.weather_on) return;

    /* **夜は天候の色調を重ねない**。乗算が二重にかかって
     * 「雨の夜」がユニットを見失うほど暗くなっていた。
     * 暗さの上限は夜の分だけにし、天候は雲の影と雨脚で見分ける。 */
    if (weather == WX_CLEAR) {
        if (!nite) {
            wx_grade(a, view, 255, 247, 232);      /* ほんのわずか暖色寄り */
            wx_blob(a, WIN_W - 170.0f, TOPBAR_FX + 40.0f, 340.0f, 260.0f,
                    (SDL_Color){ 255, 238, 180, 46 }); /* 右上の陽光 */
        }
        return;
    }

    if (weather == WX_CLOUDY) {
        if (!nite) wx_grade(a, view, 172, 180, 198);   /* 寒色へ寄せて一段暗く */
        wx_clouds(a, 9, nite ? (SDL_Color){ 40, 48, 64, 54 }
                             : (SDL_Color){ 46, 54, 70, 96 }, frame);
        return;
    }

    /* 雨: 濃い雲の影 + 斜めの雨脚。昼はさらに暗く青くする */
    if (!nite) wx_grade(a, view, 112, 134, 176);
    wx_clouds(a, 12, nite ? (SDL_Color){ 20, 26, 40, 62 }
                          : (SDL_Color){ 22, 28, 42, 120 }, frame);

    SDL_SetRenderDrawBlendMode(a->ren, SDL_BLENDMODE_BLEND);
    const int SPAN = WIN_H - TOPBAR_FX + 140;
    for (int i = 0; i < 300; i++) {
        uint32_t h = wx_hash((uint32_t)i * 40503U + 17U);
        int speed = 16 + (int)((h >> 8) % 12);
        int len   = 11 + (int)((h >> 20) % 12);
        int y = TOPBAR_FX - 70
              + (int)(((uint32_t)frame * (uint32_t)speed + (h >> 3) % (uint32_t)SPAN)
                      % (uint32_t)SPAN);
        int x = (int)(((h % (uint32_t)WIN_W) + (uint32_t)frame * 4U)
                      % (uint32_t)(WIN_W + 60)) - 30;
        Uint8 al = (Uint8)(70 + (h >> 26) % 70);
        SDL_SetRenderDrawColor(a->ren, 190, 215, 245, al);
        SDL_RenderDrawLine(a->ren, x, y, x - len / 3, y + len);
    }
}

/* 上のバーに出す天候アイコン。文字だけだと目に入らないので絵も添える。
 * 夜の晴天は太陽ではなく月を描く（夜なのに太陽が出ていると矛盾する）。 */
void render_weather_icon(App *a, int x, int y, int weather)
{
    const SDL_Color SUN   = { 250, 205,  90, 255 };
    const SDL_Color MOON  = { 205, 216, 245, 255 };
    const SDL_Color CLOUD = { 205, 210, 218, 255 };
    const SDL_Color DROP  = { 120, 180, 250, 255 };
    float cx = (float)x + 9.0f, cy = (float)y + 9.0f;
    bool nite = game_is_night(&a->game);

    if (weather == WX_CLEAR) {
        if (nite) {
            /* 三日月: 円を描いて背景色の円で削る */
            fill_circle(a, cx, cy, 6.0f, MOON);
            fill_circle(a, cx + 3.6f, cy - 2.0f, 5.2f,
                        (SDL_Color){ 22, 26, 32, 255 });
            return;
        }
        for (int i = 0; i < 8; i++) {          /* 光条 */
            float an = (float)i * 3.14159f / 4.0f;
            fill_circle(a, cx + cosf(an) * 8.0f, cy + sinf(an) * 8.0f, 1.6f, SUN);
        }
        fill_circle(a, cx, cy, 5.0f, SUN);
        return;
    }
    if (weather == WX_CLOUDY) {
        fill_circle(a, cx - 4.0f, cy + 1.0f, 4.5f, CLOUD);
        fill_circle(a, cx + 1.0f, cy - 2.0f, 5.5f, CLOUD);
        fill_circle(a, cx + 6.0f, cy + 1.0f, 4.0f, CLOUD);
        return;
    }
    fill_circle(a, cx - 4.0f, cy - 2.0f, 4.0f, CLOUD);
    fill_circle(a, cx + 1.0f, cy - 4.0f, 5.0f, CLOUD);
    fill_circle(a, cx + 6.0f, cy - 2.0f, 3.5f, CLOUD);
    for (int i = 0; i < 3; i++) {              /* 雨脚 */
        float dx = cx - 4.0f + (float)i * 5.0f;
        SDL_SetRenderDrawColor(a->ren, DROP.r, DROP.g, DROP.b, DROP.a);
        SDL_RenderDrawLine(a->ren, (int)dx, (int)(cy + 4.0f),
                           (int)(dx - 2.0f), (int)(cy + 10.0f));
    }
}
