/* path.c - Dijkstra による移動範囲計算・経路辿り */
#include "path.h"
#include "rules.h"
#include "hex.h"
#include <string.h>

/* ---- 単純二分ヒープ（lazy decrease-key） ---- */
typedef struct { int cost, x, y; } HeapNode;

typedef struct {
    HeapNode a[MAX_MAP_W * MAX_MAP_H * 4];
    int n;
} Heap;

static void heap_push(Heap *h, int cost, int x, int y)
{
    int i = h->n++;
    h->a[i].cost = cost; h->a[i].x = x; h->a[i].y = y;
    while (i > 0) {
        int p = (i - 1) / 2;
        if (h->a[p].cost <= h->a[i].cost) break;
        HeapNode t = h->a[p]; h->a[p] = h->a[i]; h->a[i] = t;
        i = p;
    }
}

static HeapNode heap_pop(Heap *h)
{
    HeapNode top = h->a[0];
    h->a[0] = h->a[--h->n];
    int i = 0;
    for (;;) {
        int l = i * 2 + 1, r = l + 1, m = i;
        if (l < h->n && h->a[l].cost < h->a[m].cost) m = l;
        if (r < h->n && h->a[r].cost < h->a[m].cost) m = r;
        if (m == i) break;
        HeapNode t = h->a[m]; h->a[m] = h->a[i]; h->a[i] = t;
        i = m;
    }
    return top;
}

static Heap s_heap; /* 静的確保（再入なし前提） */

void path_move_range(const Game *g, int ui, MoveRange *out)
{
    const Unit *u = &g->units[ui];
    const UnitType *ut = &g->types[u->type];
    MoveClass mc = (MoveClass)ut->mclass;
    Layer ml = unit_layer(mc);        /* 立体化: 占有は自分のレイヤーだけ見る */
    int owner = u->owner;

    memset(out->cost, -1, sizeof out->cost);
    memset(out->parent, -1, sizeof out->parent);
    memset(out->stop, 0, sizeof out->stop);

    static uint8_t settled[MAX_MAP_H][MAX_MAP_W];
    static uint8_t after_zoc[MAX_MAP_H][MAX_MAP_W];
    memset(settled, 0, sizeof settled);
    memset(after_zoc, 0, sizeof after_zoc);

    /* 指揮官の移動力ボーナス（対象ドメインのみ）。燃料は超えられない */
    int base_move = ut->move;
    {
        /* 常時の move_bonus に加えて、ADVANCE 発動中の上乗せも含む */
        base_move += game_co_move_bonus(g, owner, u);
        /* 天候: 雨は地上部隊の移動を1減らす（下限1） */
        base_move += game_weather_move_mod(g, u);
        if (base_move < 1) base_move = 1;
    }
    int mv = base_move < u->fuel ? base_move : u->fuel;
    int budget = mv * 2;

    int sx = u->pos.x, sy = u->pos.y;
    out->cost[sy][sx] = 0;
    s_heap.n = 0;
    heap_push(&s_heap, 0, sx, sy);

    while (s_heap.n > 0) {
        HeapNode nd = heap_pop(&s_heap);
        int x = nd.x, y = nd.y;
        if (settled[y][x]) continue;
        settled[y][x] = 1;

        bool is_start = (x == sx && y == sy);
        /* ZOCを受けるのは地表レイヤーの移動のみ（空・海中は無関係） */
        bool in_zoc = (ml == LAYER_SURFACE) && rules_in_enemy_zoc(g, owner, x, y);

        /* 展開可否: ZOC進入で移動終了。FOOTのみ1歩だけ抜け可 */
        bool expand = true;
        if (!is_start) {
            if (after_zoc[y][x]) expand = false;
            else if (in_zoc && mc != MC_FOOT) expand = false;
        }
        if (!expand) continue;
        bool exiting_zoc = !is_start && in_zoc; /* FOOTのZOC離脱1歩 */

        for (int d = 0; d < HEX_DIRS; d++) {
            int nx, ny;
            hex_neighbor(x, y, d, &nx, &ny);
            if (!game_in_bounds(g, nx, ny)) continue;
            if (settled[ny][nx]) continue;

            int mcost = rules_move_cost(g, mc, nx, ny);
            if (mcost <= 0) continue; /* 進入不可地形 */

            /* 視認できる敵ユニットは進入不可（隠れた敵はアンブッシュで解決）。
             * 立体化: 同じレイヤーの敵のみ通過を妨げる（別の高さは通り抜けられる）。 */
            int occ = game_unit_at_layer(g, nx, ny, ml);
            if (occ >= 0 && g->units[occ].owner != owner &&
                game_unit_visible_to(g, owner, &g->units[occ]))
                continue;

            int nc = nd.cost + mcost;
            if (nc > budget) continue;
            if (out->cost[ny][nx] >= 0 && out->cost[ny][nx] <= nc) continue;

            out->cost[ny][nx] = (int16_t)nc;
            out->parent[ny][nx] = (int8_t)d;
            after_zoc[ny][nx] = exiting_zoc ? 1 : 0;
            heap_push(&s_heap, nc, nx, ny);
        }
    }

    /* 停止可能判定: 自分のレイヤーが空いていれば停止可（別の高さの他ユニットは無関係）。
     * ただし、そのセルに搭載可能な味方輸送ユニットがあれば「乗り込み先」として停止可
     * （空母=海面 に 艦載機=空 が着艦する等、レイヤーを跨ぐ搭乗もここで許可）。 */
    for (int y = 0; y < g->h; y++) {
        for (int x = 0; x < g->w; x++) {
            if (out->cost[y][x] < 0) continue;
            int same = game_unit_at_layer(g, x, y, ml);   /* 自レイヤーの占有 */
            int cell[LAYER_COUNT];
            game_units_at(g, x, y, cell);
            bool can_board_here = false;
            for (int L = 0; L < LAYER_COUNT; L++)
                if (cell[L] >= 0 && g->units[cell[L]].owner == owner &&
                    game_can_board(g, ui, cell[L])) can_board_here = true;
            /* 同レイヤーに「合流できる同種の味方」がいれば停止可（重ねて統合する） */
            bool can_join_here = (same >= 0 && same != ui && game_can_join(g, ui, same));
            bool blocked = (same >= 0 && same != ui);
            if (blocked && !can_board_here && !can_join_here) continue;
            out->stop[y][x] = 1;
        }
    }
    /* 自分の現在地は常に停止可 */
    out->stop[sy][sx] = 1;
}

int path_walk(const Game *g, int ui, const MoveRange *mr, int tx, int ty,
              int *ox, int *oy, int *ofuel)
{
    const Unit *u = &g->units[ui];
    Layer ml = unit_layer(g->types[u->type].mclass);  /* 立体化: 自レイヤーで占有判定 */
    int sx = u->pos.x, sy = u->pos.y;

    /* 経路復元（dest→start を逆順に） */
    static int px[MAX_MAP_W * MAX_MAP_H];
    static int py[MAX_MAP_W * MAX_MAP_H];
    int n = 0;
    int x = tx, y = ty;
    while (!(x == sx && y == sy) && n < MAX_MAP_W * MAX_MAP_H) {
        px[n] = x; py[n] = y; n++;
        int d = mr->parent[y][x];
        if (d < 0) break;
        /* parent[d] は「進入してきた方向」なので逆向きに戻る */
        int bx, by;
        hex_neighbor(x, y, (d + 3) % HEX_DIRS, &bx, &by);
        x = bx; y = by;
    }

    /* 前進シミュレート: 隠れた敵に遮られたら手前で停止 */
    int cx = sx, cy = sy;
    int ambush = 0;
    for (int i = n - 1; i >= 0; i--) {
        int occ = game_unit_at_layer(g, px[i], py[i], ml);
        if (occ >= 0 && g->units[occ].owner != u->owner) {
            ambush = 1;
            break;
        }
        cx = px[i]; cy = py[i];
    }

    /* アンブッシュ停止位置に他ユニットがいたらさらに手前へ
     * （目的地の味方輸送ユニットへの乗り込みは例外） */
    while (!(cx == sx && cy == sy)) {
        /* 自レイヤーが空（または自分）なら停止可。空母(海面)への艦載機(空)着艦は
         * 空レイヤーが空くので自然にここで成立する。 */
        int occ = game_unit_at_layer(g, cx, cy, ml);
        if (occ < 0 || occ == ui) break;
        /* 自レイヤーに味方輸送(トラック/輸送艦など同レイヤー)があり搭乗可なら停止可 */
        if (!ambush && cx == tx && cy == ty && g->units[occ].owner == u->owner &&
            (game_can_board(g, ui, occ) || game_can_join(g, ui, occ)))
            break;
        int d = mr->parent[cy][cx];
        if (d < 0) break;
        int bx, by;
        hex_neighbor(cx, cy, (d + 3) % HEX_DIRS, &bx, &by);
        cx = bx; cy = by;
    }

    *ox = cx; *oy = cy;
    int c = mr->cost[cy][cx];
    *ofuel = (c + 1) / 2;
    return ambush;
}
