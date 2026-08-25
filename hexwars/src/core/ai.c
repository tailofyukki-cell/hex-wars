/* ai.c - CPU思考ルーチン（脅威マップ + 役割別評価。仕様書 7章） */
#include "ai.h"
#include "hex.h"
#include "path.h"
#include "rules.h"
#include <string.h>

/* ------------------------------------------------------------------ */
/* 脅威マップ: 敵ユニットの攻撃到達範囲（移動+射程を距離近似）を加算   */
/* ------------------------------------------------------------------ */
static void build_threat(Game *g, AiState *s)
{
    int me = g->current;
    memset(s->threat, 0, sizeof s->threat);
    for (int i = 0; i < g->n_units; i++) {
        const Unit *u = &g->units[i];
        if (!(u->flags & UF_ALIVE) || u->owner == me) continue;
        if (!game_unit_visible_to(g, me, u)) continue;
        const UnitType *t = &g->types[u->type];
        int reach = t->move + t->range_max;
        int power = 0;
        for (int c = 0; c < ARMOR_COUNT; c++)
            if (t->atk[c] > power) power = t->atk[c];
        for (int y = 0; y < g->h; y++)
            for (int x = 0; x < g->w; x++)
                if (hex_distance(u->pos.x, u->pos.y, x, y) <= reach)
                    s->threat[y][x] += power;
    }
}

/* ------------------------------------------------------------------ */
/* 目標地点: 占領対象建物 / 敵ユニット                                 */
/* ------------------------------------------------------------------ */
static int building_value(const Game *g, int x, int y)
{
    const TerrainType *t = game_terrain_at(g, x, y);
    if (t->is_hq) return 2000;
    if (t->produces == PROD_LAND) return 900;   /* 工場 */
    if (t->produces != PROD_NONE) return 700;   /* 空港・港 */
    return 500;                                  /* 都市 */
}

/* 最寄りの奪取対象建物までの距離（capture役割の目標） */
static int nearest_capture_goal(const Game *g, int me, int fx, int fy)
{
    int best = 9999;
    for (int y = 0; y < g->h; y++)
        for (int x = 0; x < g->w; x++) {
            const TerrainType *t = game_terrain_at(g, x, y);
            if (!t->capturable || g->tiles[y][x].owner == me) continue;
            int d = hex_distance(fx, fy, x, y);
            /* 価値の高い建物を優先（距離を割引） */
            d -= building_value(g, x, y) / 500;
            if (d < best) best = d;
        }
    return best;
}

/* 最寄りの可視敵ユニット/敵首都までの距離（攻撃役割の目標） */
static int nearest_enemy_goal(const Game *g, int me, int fx, int fy)
{
    int best = 9999;
    for (int i = 0; i < g->n_units; i++) {
        const Unit *u = &g->units[i];
        if (!(u->flags & UF_ALIVE) || u->owner == me) continue;
        if (!game_unit_visible_to(g, me, u)) continue;
        int d = hex_distance(fx, fy, u->pos.x, u->pos.y);
        if (d < best) best = d;
    }
    for (int y = 0; y < g->h; y++)
        for (int x = 0; x < g->w; x++)
            if (g->terrains[g->tiles[y][x].terrain].is_hq &&
                g->tiles[y][x].owner != me) {
                int d = hex_distance(fx, fy, x, y);
                if (d < best) best = d;
            }
    return best;
}

/* 最寄りの「補給・回復が必要な味方」までの距離（補給車の目標）。
 * 燃料/弾薬切れに加え、損傷ユニット（HP<8）も対象にする（回復用）。 */
static int nearest_needy(const Game *g, int me, int self, int fx, int fy)
{
    int best = 9999;
    for (int i = 0; i < g->n_units; i++) {
        const Unit *u = &g->units[i];
        if (!(u->flags & UF_ALIVE) || (u->flags & UF_LOADED)) continue;
        if (u->owner != me || i == self) continue;
        const UnitType *t = &g->types[u->type];
        if (t->supply) continue; /* 補給車同士は追わない */
        bool needy = u->fuel < t->fuel / 2 ||
                     (t->ammo > 0 && u->ammo < (t->ammo + 1) / 2) ||
                     u->hp < 8;
        if (!needy) continue;
        int d = hex_distance(fx, fy, u->pos.x, u->pos.y);
        if (d < best) best = d;
    }
    return best;
}

/* 最寄りの味方戦闘ユニットまでの距離（補給対象がないときの随伴用） */
static int nearest_friend(const Game *g, int me, int self, int fx, int fy)
{
    int best = 9999;
    for (int i = 0; i < g->n_units; i++) {
        const Unit *u = &g->units[i];
        if (!(u->flags & UF_ALIVE) || (u->flags & UF_LOADED)) continue;
        if (u->owner != me || i == self) continue;
        if (g->types[u->type].supply) continue;
        int d = hex_distance(fx, fy, u->pos.x, u->pos.y);
        if (d < best) best = d;
    }
    return best;
}

/* 最寄りの自軍補給施設までの距離（HARDの退却用） */
static int nearest_supply(const Game *g, int me, MoveClass mc, int fx, int fy)
{
    int best = 9999;
    for (int y = 0; y < g->h; y++)
        for (int x = 0; x < g->w; x++) {
            const Tile *tl = &g->tiles[y][x];
            const TerrainType *t = &g->terrains[tl->terrain];
            if (tl->owner != me || !(t->supplies & (1u << mc))) continue;
            int d = hex_distance(fx, fy, x, y);
            if (d < best) best = d;
        }
    return best;
}

/* ------------------------------------------------------------------ */
/* 手番開始                                                            */
/* ------------------------------------------------------------------ */
/* ------------------------------------------------------------------ */
/* 上陸作戦（陸路で届かない敵拠点へ輸送ユニットで部隊を送る）          */
/* ------------------------------------------------------------------ */

/* 機動ドメイン 0=陸 1=空 2=海（game.c の move_domain と同じ分類） */
static int move_domain_of(const Game *g, const Unit *u)
{
    int mc = g->types[u->type].mclass;
    if (mc == MC_AIR) return 1;
    if (mc == MC_SEA || mc == MC_SUB) return 2;
    return 0;
}

/* 自軍の陸ユニット・陸上生産拠点から、陸伝いに到達できる範囲を塗る */
static void build_land_reach(Game *g, AiState *s)
{
    int me = g->current;
    static int qx[MAX_MAP_W * MAX_MAP_H], qy[MAX_MAP_W * MAX_MAP_H];
    int head = 0, tail = 0;
    memset(s->land_reach, 0, sizeof s->land_reach);

    /* 起点は「本拠地」に限定する。
     * 上陸した1体を起点に含めてしまうと、敵拠点が急に「陸路で行ける」判定になり
     * 上陸作戦が途中で打ち切られてしまうため（本拠地は動かないので判定が安定する）。
     * 首都 → 陸上生産拠点 → （どちらも無ければ）陸ユニット の順に採用する。 */
    for (int y = 0; y < g->h; y++)
        for (int x = 0; x < g->w; x++) {
            const TerrainType *t = &g->terrains[g->tiles[y][x].terrain];
            if (g->tiles[y][x].owner == me && t->is_hq && !s->land_reach[y][x]) {
                s->land_reach[y][x] = 1;
                qx[tail] = x; qy[tail] = y; tail++;
            }
        }
    if (tail == 0) {
        for (int y = 0; y < g->h; y++)
            for (int x = 0; x < g->w; x++) {
                const TerrainType *t = &g->terrains[g->tiles[y][x].terrain];
                if (g->tiles[y][x].owner == me && t->produces == PROD_LAND &&
                    !s->land_reach[y][x]) {
                    s->land_reach[y][x] = 1;
                    qx[tail] = x; qy[tail] = y; tail++;
                }
            }
    }
    if (tail == 0) {
        for (int i = 0; i < g->n_units; i++) {
            const Unit *u = &g->units[i];
            if (!(u->flags & UF_ALIVE) || (u->flags & UF_LOADED)) continue;
            if (u->owner != me || move_domain_of(g, u) != 0) continue;
            if (!s->land_reach[u->pos.y][u->pos.x]) {
                s->land_reach[u->pos.y][u->pos.x] = 1;
                qx[tail] = u->pos.x; qy[tail] = u->pos.y; tail++;
            }
        }
    }

    /* 歩兵が通れる地形を陸路とみなして広げる */
    while (head < tail) {
        int cx = qx[head], cy = qy[head]; head++;
        for (int d = 0; d < HEX_DIRS; d++) {
            int nx, ny;
            hex_neighbor(cx, cy, d, &nx, &ny);
            if (!game_in_bounds(g, nx, ny) || s->land_reach[ny][nx]) continue;
            if (g->terrains[g->tiles[ny][nx].terrain].mcost[MC_FOOT] <= 0) continue;
            s->land_reach[ny][nx] = 1;
            qx[tail] = nx; qy[tail] = ny; tail++;
        }
    }
}

/* 陸路で行けない敵/中立拠点のうち、最も近いものを上陸目標にする */
static void pick_invasion_goal(Game *g, AiState *s)
{
    int me = g->current;
    s->amphib = false;
    s->inv_x = s->inv_y = -1;

    /* 自軍に港か輸送ユニットが無ければ上陸は狙わない */
    bool can_ship = false;
    for (int i = 0; i < g->n_units && !can_ship; i++) {
        const Unit *u = &g->units[i];
        if ((u->flags & UF_ALIVE) && u->owner == me &&
            g->types[u->type].capacity > 0 && move_domain_of(g, u) == 2)
            can_ship = true;
    }
    for (int y = 0; y < g->h && !can_ship; y++)
        for (int x = 0; x < g->w && !can_ship; x++)
            if (g->tiles[y][x].owner == me &&
                g->terrains[g->tiles[y][x].terrain].produces == PROD_SEA)
                can_ship = true;
    if (!can_ship) return;

    int best = 1 << 30;
    for (int y = 0; y < g->h; y++)
        for (int x = 0; x < g->w; x++) {
            const TerrainType *t = &g->terrains[g->tiles[y][x].terrain];
            if (!t->capturable || g->tiles[y][x].owner == me) continue;
            if (s->land_reach[y][x]) continue;         /* 陸路で行ける */
            /* 首都を最優先、次に生産拠点 */
            int pri = t->is_hq ? 0 : (t->produces != PROD_NONE ? 1 : 2);
            int sc = pri * 1000 + x + y;               /* 安定した順序付け */
            if (sc < best) { best = sc; s->inv_x = x; s->inv_y = y; }
        }
    s->amphib = (s->inv_x >= 0);
}

/* 行動順を組み直す: 間接 → 直接戦闘 → 占領系（間接が先に撃ってから前進） */
static void build_order(const Game *g, AiState *s)
{
    int me = g->current;
    s->n_order = 0;
    s->idx = 0;
    int pass_order[3] = {0, 1, 2};
    for (int pass = 0; pass < 3; pass++) {
        for (int i = 0; i < g->n_units; i++) {
            const Unit *u = &g->units[i];
            if (!(u->flags & UF_ALIVE) || u->owner != me) continue;
            if (u->flags & UF_DONE) continue;
            const UnitType *t = &g->types[u->type];
            int cls;
            if (t->range_min >= 2) cls = 0;
            else if (t->can_capture) cls = 2;
            else cls = 1;
            if (cls == pass_order[pass])
                s->order[s->n_order++] = i;
        }
    }
}

/* --- 指揮官の必殺技 ---
 * 技ごとに一番効く時点が違うので phase で撃ち時を分ける。
 *   phase 0 = 手番開始（HEAL/STRIKE/SCOUT: この手番の戦闘を強くする）
 *   phase 1 = 生産の直前（FUNDS: もらった資金をその場で使い切れる）
 *   phase 2 = 全ユニットが動き終えた後（RUSH: 再行動が最大限に活きる）
 * ゲージは power_cost で頭打ちになるため、貯め込んでも損。控えめな条件で撃つ。 */
static bool ai_try_co_power(Game *g, AiState *s, int phase)
{
    int me = g->current;
    if (s->co_used) return false;
    const CommanderType *c = game_co(g, me);
    if (!c || !game_co_power_ready(g, me)) return false;
    /* EASY は撃ち時を外すことがある（弱さの演出） */
    if (g->ctrl[me] == CTRL_CPU_EASY && rng_range(&g->rng, 0, 99) < 50)
        return false;

    bool fire = false;
    switch (c->power_type) {
    case CO_POW_HEAL: {
        if (phase != 0) break;
        /* 回復しろが3体分以上あるときだけ（満タン揃いで撃っても無駄） */
        int gain = 0;
        for (int i = 0; i < g->n_units; i++) {
            const Unit *u = &g->units[i];
            if (!(u->flags & UF_ALIVE) || u->owner != me) continue;
            int room = 10 - u->hp;
            gain += room < c->power_val ? room : c->power_val;
        }
        fire = (gain >= c->power_val * 3);
        break;
    }
    case CO_POW_STRIKE: {
        if (phase != 0) break;
        /* この手番に殴り合える部隊が3体以上いるときだけ */
        int n = 0;
        for (int i = 0; i < g->n_units; i++) {
            const Unit *u = &g->units[i];
            if (!(u->flags & UF_ALIVE) || (u->flags & UF_LOADED)) continue;
            if (u->owner != me || u->ammo <= 0) continue;
            const UnitType *t = &g->types[u->type];
            if (nearest_enemy_goal(g, me, u->pos.x, u->pos.y) <= t->move + t->range_max)
                n++;
        }
        fire = (n >= 3);
        break;
    }
    case CO_POW_SCOUT:
        fire = (phase == 0);          /* 視界と資金。撃ち時を選ばない */
        break;
    case CO_POW_FUNDS:
        fire = (phase == 1);
        break;
    case CO_POW_RUSH: {
        if (phase != 2) break;
        int n = 0;
        for (int i = 0; i < g->n_units; i++) {
            const Unit *u = &g->units[i];
            if ((u->flags & UF_ALIVE) && !(u->flags & UF_LOADED) && u->owner == me)
                n++;
        }
        fire = (n >= 3);
        break;
    }
    default:
        break;
    }
    if (!fire || !game_co_activate(g, me)) return false;
    s->co_used = true;
    s->co_fired = true;    /* UI 側がバナーを出して false に戻す */
    return true;
}

void ai_begin_turn(Game *g, AiState *s)
{
    memset(s, 0, sizeof *s);
    s->last_unit = s->last_target = -1;
    /* 回復・攻撃強化はこの手番の判断に影響するので、脅威マップより先に撃つ */
    ai_try_co_power(g, s, 0);
    build_threat(g, s);
    build_land_reach(g, s);
    pick_invasion_goal(g, s);
    build_order(g, s);
}

/* ------------------------------------------------------------------ */
/* 1ユニットの行動決定・実行                                           */
/* ------------------------------------------------------------------ */
typedef struct {
    int score;
    int mx, my;        /* 移動先 */
    int action;        /* 0=待機 1=攻撃 2=占領 3=補給 4=搭乗 5=降ろす 6=合流 */
    int target;        /* 攻撃対象/搭乗先 unit index */
    int ux, uy;        /* action=5 のときの降ろし先ヘクス */
} Plan;

static MoveRange s_mr; /* 静的確保 */

static void act_unit(Game *g, AiState *s, int ui)
{
    Unit *u = &g->units[ui];
    const UnitType *ut = &g->types[u->type];
    MoveClass mc = (MoveClass)ut->mclass;
    int me = g->current;
    bool easy = g->ctrl[me] == CTRL_CPU_EASY;
    bool hard = g->ctrl[me] == CTRL_CPU_HARD;

    path_move_range(g, ui, &s_mr);

    Plan best = { -1000000, u->pos.x, u->pos.y, 0, -1, -1, -1 };
    bool retreat = hard && u->hp <= 3; /* HARD: 損傷ユニットは補給地点へ */

    /* このユニットが上陸作戦に関わるか */
    int dom = move_domain_of(g, u);
    bool is_transport = (ut->capacity > 0 && dom == 2);
    int cargo_n = 0;
    for (int cs = 0; cs < 2; cs++) if (u->cargo[cs] >= 0) cargo_n++;
    /* まだ積めるうえに「すぐ乗れる位置」に味方がいるなら、あと1ターンだけ待って
     * 満載にする（1体ずつ運ぶと往復が多くなり上陸戦力が揃わないため）。
     * 待つ条件を隣接圏に絞らないと、母港の味方に釣られて永久に出航しなくなる。 */
    bool more_riders_near = false;
    if (is_transport && cargo_n > 0 && cargo_n < ut->capacity) {
        for (int i = 0; i < g->n_units && !more_riders_near; i++) {
            const Unit *f = &g->units[i];
            if (!(f->flags & UF_ALIVE) || (f->flags & UF_LOADED)) continue;
            if (f->owner != me || !game_can_board(g, i, ui)) continue;
            if (hex_distance(u->pos.x, u->pos.y, f->pos.x, f->pos.y) <= 6)
                more_riders_near = true;
        }
    }
    bool has_cargo = (cargo_n > 0) && !more_riders_near;
    /* 陸ユニットが「陸路では目標に行けない」＝船に乗るべき状況か */
    bool wants_ride = s->amphib && dom == 0 && !ut->supply &&
                      s->inv_x >= 0 && !s->land_reach[s->inv_y][s->inv_x];

    for (int y = 0; y < g->h; y++) {
        for (int x = 0; x < g->w; x++) {
            if (s_mr.cost[y][x] < 0 || !s_mr.stop[y][x]) continue;
            /* 自レイヤーの占有は避ける。ただし搭乗できる味方輸送の上は候補にする */
            int board_t = -1, join_t = -1;
            {
                int occ = game_unit_at_layer(g, x, y, unit_layer(mc));
                if (occ >= 0 && occ != ui) {
                    if (wants_ride && g->units[occ].owner == me &&
                        game_can_board(g, ui, occ))
                        board_t = occ;
                    /* 瀕死の部隊は合流して1個の使える部隊にまとめる。
                     * HP合計が11以下＝はみ出しがほぼ無いときだけ狙う（無駄撃ちを避ける）。
                     * hp<=2 という「瀕死」の線引きが要。ここを4まで緩めると、手負いが
                     * 攻めずに合流相手を探しに行くようになり決着が大幅に遅くなる
                     * （sim_ai 実測: 総ターン 1698→1818、総損失 6062→6615）。 */
                    else if (u->hp <= 2 && g->units[occ].owner == me &&
                             game_can_join(g, ui, occ) &&
                             u->hp + g->units[occ].hp <= 11)
                        join_t = occ;
                    else
                        continue;
                }
            }
            /* 搭乗プラン: 乗れるなら高い優先度で乗る（上陸の足を確保する） */
            if (board_t >= 0) {
                int sc = 4000 - hex_distance(x, y, s->inv_x, s->inv_y) * 10;
                if (sc > best.score) {
                    best.score = sc; best.mx = x; best.my = y;
                    best.action = 4; best.target = board_t;
                }
                continue;
            }
            bool moved = !(x == u->pos.x && y == u->pos.y);
            const TerrainType *terr = game_terrain_at(g, x, y);
            /* 立体化(確定): 地形防御は全レイヤーに適用（航空機も地形防御を得る） */
            int tdef = terr->def_bonus;

            /* 共通項: 地形防御 − 脅威 */
            int base = tdef * 2 - s->threat[y][x] / 8;

            /* --- 合流評価: 傷むほど魅力的。ただし良い攻撃手には負ける値にする --- */
            if (join_t >= 0) {
                int sc = base + 40 + (10 - u->hp) * 20;
                /* 相手が行動済みなら手番を無駄にしないぶん有利 */
                if (g->units[join_t].flags & UF_DONE) sc += 80;
                if (sc > best.score) {
                    best.score = sc; best.mx = x; best.my = y;
                    best.action = 6; best.target = join_t;
                }
                continue;
            }

            if (retreat) {
                int d = nearest_supply(g, me, mc, x, y);
                int sc = base - d * 60;
                if (sc > best.score) {
                    best.score = sc; best.mx = x; best.my = y;
                    best.action = 0; best.target = -1;
                }
                continue;
            }

            /* --- 輸送艦: 積んで運ぶ / 空なら味方を拾いに行く --- */
            if (is_transport && s->amphib) {
                int sc;
                if (has_cargo) {
                    /* 積荷あり: 上陸目標に最も近い「降ろせる隣接陸地」を探す */
                    int bestu = -1, bux = -1, buy = -1;
                    for (int d = 0; d < HEX_DIRS; d++) {
                        int nx, ny;
                        hex_neighbor(x, y, d, &nx, &ny);
                        if (!game_in_bounds(g, nx, ny)) continue;
                        if (!game_can_unload_to(g, ui, nx, ny)) continue;
                        int dd = hex_distance(nx, ny, s->inv_x, s->inv_y);
                        if (bestu < 0 || dd < bestu) { bestu = dd; bux = nx; buy = ny; }
                    }
                    if (bestu >= 0) {
                        /* 目標に近い岸へ降ろせるほど高評価 */
                        sc = 6000 - bestu * 60 - s->threat[y][x] / 8;
                        if (sc > best.score) {
                            best.score = sc; best.mx = x; best.my = y;
                            best.action = 5; best.target = -1;
                            best.ux = bux; best.uy = buy;
                        }
                    }
                    /* 降ろせないなら、とにかく目標へ近づく */
                    sc = 3000 - hex_distance(x, y, s->inv_x, s->inv_y) * 50
                         - s->threat[y][x] / 8;
                    if (sc > best.score) {
                        best.score = sc; best.mx = x; best.my = y;
                        best.action = 0; best.target = -1;
                    }
                } else {
                    /* 空: 乗せられる味方陸ユニットの隣へ着ける */
                    int nd = 9999;
                    for (int i = 0; i < g->n_units; i++) {
                        const Unit *f = &g->units[i];
                        if (!(f->flags & UF_ALIVE) || (f->flags & UF_LOADED)) continue;
                        if (f->owner != me) continue;
                        if (!game_can_board(g, i, ui)) continue;
                        int dd = hex_distance(x, y, f->pos.x, f->pos.y);
                        if (dd < nd) nd = dd;
                    }
                    sc = (nd < 9999) ? (3500 - nd * 70) : 0;
                    sc -= s->threat[y][x] / 8;
                    if (sc > best.score) {
                        best.score = sc; best.mx = x; best.my = y;
                        best.action = 0; best.target = -1;
                    }
                }
                continue;
            }

            /* --- 補給車: 燃料/弾薬切れの味方へ向かい隣接する --- */
            if (ut->supply) {
                int d = nearest_needy(g, me, ui, x, y);
                int sc;
                if (d < 9999) {
                    sc = base - d * 80;
                    if (d <= 1) sc += 500; /* 隣接すれば補給できる */
                } else {
                    /* 補給対象なし: 前線の味方に随伴しつつ脅威を避ける */
                    sc = base - nearest_friend(g, me, ui, x, y) * 30 - 300;
                }
                if (sc > best.score) {
                    best.score = sc; best.mx = x; best.my = y;
                    best.action = 3; best.target = -1;
                }
                continue;
            }

            /* --- 占領評価 --- */
            if (ut->can_capture && terr->capturable &&
                g->tiles[y][x].owner != me) {
                int sc = base + building_value(g, x, y);
                if (sc > best.score) {
                    best.score = sc; best.mx = x; best.my = y;
                    best.action = 2; best.target = -1;
                }
            }

            /* --- 攻撃評価 --- */
            if (rules_can_attack_now(g, ui, moved)) {
                int targets[32];
                int nt = rules_list_targets(g, ui, x, y, targets, 32);
                for (int k = 0; k < nt; k++) {
                    const Unit *d = &g->units[targets[k]];
                    const UnitType *dt = &g->types[d->type];
                    int exp = battle_expect_damage_x10(g, u, d);
                    int cap = d->hp * 10;
                    if (exp > cap) exp = cap;
                    int gain = dt->cost * exp / 100;
                    if (exp >= cap) gain += dt->cost / 2; /* 撃破ボーナス */

                    /* 被反撃見込み */
                    int loss = 0;
                    int dist = hex_distance(x, y, d->pos.x, d->pos.y);
                    if (ut->range_min < 2 && exp < cap &&
                        dist >= dt->range_min && dist <= dt->range_max &&
                        d->ammo > 0 && dt->atk[ut->armor] > 0) {
                        int cx10 = battle_expect_damage_x10(g, d, u);
                        loss = ut->cost * cx10 / 100;
                    }
                    int sc = base + gain - loss * 2 / 3;
                    if (sc > best.score) {
                        best.score = sc; best.mx = x; best.my = y;
                        best.action = 1; best.target = targets[k];
                    }
                }
            }

            /* --- 前進評価 --- */
            {
                int d = ut->can_capture
                          ? nearest_capture_goal(g, me, x, y)
                          : nearest_enemy_goal(g, me, x, y);
                int sc = base - d * 40 - 200; /* 攻撃・占領より弱い基本値 */
                if (sc > best.score) {
                    best.score = sc; best.mx = x; best.my = y;
                    best.action = 0; best.target = -1;
                }
            }
        }
    }

    /* EASY: 最適手からランダムにずらす（±30%相当の雑さ→時々何もしない） */
    if (easy && rng_range(&g->rng, 0, 99) < 25) {
        best.action = 0;
        best.mx = u->pos.x; best.my = u->pos.y;
    }

    /* 実行 */
    s->last_unit = ui;
    s->last_target = -1;
    int fx, fy, fuel;
    path_walk(g, ui, &s_mr, best.mx, best.my, &fx, &fy, &fuel);
    bool ambushed = (fx != best.mx || fy != best.my);
    game_move_unit(g, ui, fx, fy, fuel);

    if (!ambushed && best.action == 1 && best.target >= 0 &&
        (g->units[best.target].flags & UF_ALIVE)) {
        s->last_target = best.target;
        game_attack(g, ui, best.target, NULL, NULL, NULL);
    } else if (!ambushed && best.action == 4 && best.target >= 0 &&
               (g->units[best.target].flags & UF_ALIVE) &&
               game_can_board(g, ui, best.target)) {
        game_load_unit(g, ui, best.target);          /* 輸送に乗り込む */
    } else if (!ambushed && best.action == 5) {
        /* 上陸: 積んでいる部隊を岸へ降ろす（複数積んでいれば可能な限り） */
        bool any = false;
        while (game_first_cargo(g, ui) >= 0 &&
               game_unload_unit(g, ui, best.ux, best.uy) == 0) {
            any = true;
            /* 同じヘクスには1体しか置けないので、次の降ろし先を探し直す */
            int nx = -1, ny = -1;
            for (int d = 0; d < HEX_DIRS; d++) {
                int tx2, ty2;
                hex_neighbor(u->pos.x, u->pos.y, d, &tx2, &ty2);
                if (game_in_bounds(g, tx2, ty2) &&
                    game_can_unload_to(g, ui, tx2, ty2)) { nx = tx2; ny = ty2; break; }
            }
            if (nx < 0) break;
            best.ux = nx; best.uy = ny;
        }
        if (!any) game_wait_unit(g, ui);
    } else if (!ambushed && best.action == 6 && best.target >= 0 &&
               (g->units[best.target].flags & UF_ALIVE) &&
               game_can_join(g, ui, best.target)) {
        game_join_units(g, ui, best.target);         /* 手負い同士を合流 */
    } else if (!ambushed && best.action == 2) {
        game_capture(g, ui);
    } else if (!ambushed && best.action == 3) {
        /* 補給車: 損傷した味方が隣接していれば回復を優先、なければ補給 */
        if (game_can_heal(g, ui))
            game_supply_heal(g, ui);
        else if (game_can_supply(g, ui))
            game_supply_adjacent(g, ui);
        else
            game_wait_unit(g, ui);
    } else {
        game_wait_unit(g, ui);
    }
}

/* ------------------------------------------------------------------ */
/* 生産（仕様書 7.1-4: 序盤は歩兵・占領重視、以降は装甲・航空へ）      */
/* ------------------------------------------------------------------ */
static int pick_production(Game *g, AiState *s, int x, int y)
{
    int me = g->current;

    /* 自軍構成を数える */
    int n_cap = 0, n_combat = 0, n_supplier = 0;
    int n_transport = 0, n_riders = 0;
    for (int i = 0; i < g->n_units; i++) {
        const Unit *u = &g->units[i];
        if (!(u->flags & UF_ALIVE) || u->owner != me) continue;
        const UnitType *t = &g->types[u->type];
        if (t->supply) n_supplier++;
        else if (t->can_capture) n_cap++;
        else n_combat++;
        if (t->capacity > 0 && move_domain_of(g, u) == 2) n_transport++;
        /* 船に乗せられる陸ユニット（上陸要員の在庫） */
        if (move_domain_of(g, u) == 0 && t->n_transport_by > 0) n_riders++;
    }

    /* 奪える建物が残っているなら占領要員を最低3体維持 */
    int open_bldgs = 0;
    for (int yy = 0; yy < g->h; yy++)
        for (int xx = 0; xx < g->w; xx++)
            if (g->terrains[g->tiles[yy][xx].terrain].capturable &&
                g->tiles[yy][xx].owner != me)
                open_bldgs++;

    bool want_capture = (n_cap < 3 && open_bldgs > 0);

    int best = -1, best_sc = -1;
    for (int t = 0; t < g->n_types; t++) {
        const UnitType *ut = &g->types[t];
        if (!game_type_buildable_at(g, x, y, t)) continue;
        if (ut->cost > g->funds[me]) continue;

        int sc;
        if (s->amphib && ut->capacity > 0 && ut->mclass == MC_SEA) {
            /* 上陸作戦が必要: 輸送艦を2隻まで確保（乗せる部隊がいる時だけ） */
            sc = (n_transport < 2 && n_riders >= 1) ? 12000 - ut->cost : 0;
        } else if (s->amphib && ut->can_capture && n_riders < 4) {
            /* 上陸させる占領要員を優先的に揃える */
            sc = 9500 - ut->cost;
        } else if (ut->supply) {
            /* 戦闘部隊が育ってきたら補給車を1台だけ確保 */
            sc = (n_supplier == 0 && n_combat >= 6) ? 9000 : 0;
        } else if (want_capture && ut->can_capture) {
            sc = 10000 - ut->cost; /* 安い占領ユニット優先 */
        } else if (ut->can_capture) {
            sc = 100; /* 占領枠は足りている */
        } else {
            /* 火力とコストで選ぶ（序盤=ターン数少は安め優先） */
            int power = 0;
            for (int c = 0; c < ARMOR_COUNT; c++) power += ut->atk[c];
            sc = power + (g->turn >= 6 ? ut->cost / 4 : -ut->cost / 4);
        }
        if (sc > best_sc) { best_sc = sc; best = t; }
    }
    return best;
}

static void do_production(Game *g, AiState *s)
{
    for (int y = 0; y < g->h; y++) {
        for (int x = 0; x < g->w; x++) {
            if (!game_can_produce_at(g, g->current, x, y)) continue;
            int t = pick_production(g, s, x, y);
            if (t >= 0)
                game_produce(g, x, y, t);
        }
    }
}

/* ------------------------------------------------------------------ */
int ai_step(Game *g, AiState *s)
{
    if (g->winner != WINNER_NONE) return 0;

    while (s->idx < s->n_order) {
        int ui = s->order[s->idx++];
        Unit *u = &g->units[ui];
        if (!(u->flags & UF_ALIVE) || (u->flags & UF_DONE)) continue;
        act_unit(g, s, ui);
        return 1;
    }
    /* 全ユニットが動き終えた: 再行動系はここで撃つと全部隊がもう一度動ける */
    if (!s->produced && ai_try_co_power(g, s, 2)) {
        build_order(g, s);      /* 行動済みが解除されたので並べ直す */
        return 1;
    }
    if (!s->produced) {
        s->produced = true;
        s->last_unit = s->last_target = -1;
        ai_try_co_power(g, s, 1);   /* 資金系は生産の直前に */
        do_production(g, s);
        return 1;
    }
    game_end_turn(g);
    return 0;
}
