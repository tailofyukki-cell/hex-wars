/* rules.c - ZOC・攻撃対象などのルール判定 */
#include "rules.h"
#include "hex.h"

bool rules_in_enemy_zoc(const Game *g, int player, int x, int y)
{
    for (int d = 0; d < HEX_DIRS; d++) {
        int nx, ny;
        hex_neighbor(x, y, d, &nx, &ny);
        if (!game_in_bounds(g, nx, ny)) continue;
        /* ZOCは地表レイヤーの現象。発生源は地上ユニット(FOOT/WHEEL/TRACK)のみ。
         * 立体化: 地表レイヤーの占有だけ見る（同セルに空/海中が居ても無関係）。 */
        int ui = game_unit_at_layer(g, nx, ny, LAYER_SURFACE);
        if (ui < 0) continue;
        const Unit *u = &g->units[ui];
        if (u->owner == player) continue;
        MoveClass mc = (MoveClass)g->types[u->type].mclass;
        if (mc == MC_FOOT || mc == MC_WHEEL || mc == MC_TRACK)
            return true;
    }
    return false;
}

int rules_move_cost(const Game *g, MoveClass mc, int x, int y)
{
    return g->terrains[g->tiles[y][x].terrain].mcost[mc];
}

int rules_list_targets(const Game *g, int ui, int fx, int fy,
                       int *out, int max_out)
{
    const Unit *a = &g->units[ui];
    const UnitType *at = &g->types[a->type];
    int n = 0;
    for (int i = 0; i < g->n_units && n < max_out; i++) {
        const Unit *d = &g->units[i];
        if (!(d->flags & UF_ALIVE) || (d->flags & UF_LOADED)) continue;
        if (d->owner == a->owner) continue;
        int dist = hex_distance(fx, fy, d->pos.x, d->pos.y);
        /* 立体化(確定): 射程は水平距離のみ。高さ差は無視。
         * 同一セル・別レイヤー(距離0)は直射(range_min<=1)のみ攻撃可、間接は自セル不可。 */
        if (dist == 0) {
            if (at->range_min > 1) continue;
        } else if (dist < at->range_min || dist > game_range_max(g, at)) {
            continue;
        }
        if (!unit_can_attack_target(g, a, d)) continue;
        if (!game_unit_visible_to(g, a->owner, d)) continue;
        out[n++] = i;
    }
    return n;
}

bool rules_can_attack_now(const Game *g, int ui, bool has_moved)
{
    const Unit *u = &g->units[ui];
    const UnitType *t = &g->types[u->type];
    if (u->ammo <= 0) return false;
    if (has_moved && !t->move_and_fire) return false; /* FAM式（仕様書 5.5） */
    return true;
}
