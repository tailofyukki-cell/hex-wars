/* ai.c - CPU思考ルーチン（脅威マップ + 役割別評価。仕様書 7章） */
#include "ai.h"
#include "hex.h"
#include "path.h"
#include "rules.h"
#include <string.h>

/* 脅威をスコアから差し引くときの割り算。小さいほど臆病になる。
 * 装甲カテゴリ別に持つようになって数値が下がった（従来は全カテゴリの最大値を
 * 使っていたので、当たらない相手の高い攻撃力まで数えていた）。実測でおよそ
 * 6割程度に縮んだので、同じ警戒度になるよう 8 から下げてある。
 * ここを大きくすると突っ込みやすく損失が増え、小さくすると慎重になり決着が遅くなる。 */
#define THREAT_DIV 8

/* ------------------------------------------------------------------ */
/* 脅威マップ: 敵ユニットの攻撃到達範囲（移動+射程を距離近似）を加算   */
/* ------------------------------------------------------------------ */
/* その拠点を me が欲しがるか（中立か敵のものだけ）。
 * 味方・援軍の拠点を目標にしないための共通判定。 */
static bool ai_can_take(const Game *g, int me, int owner)
{
    if (owner < 0) return true;               /* 中立 */
    return game_is_enemy(g, me, owner);
}

static void build_threat(Game *g, AiState *s)
{
    int me = g->current;
    memset(s->threat, 0, sizeof s->threat);
    for (int i = 0; i < g->n_units; i++) {
        const Unit *u = &g->units[i];
        /* 援軍は脅威ではないので除外する（味方を怖がって逃げ回らないため） */
        if (!(u->flags & UF_ALIVE) || !game_is_enemy(g, me, u->owner)) continue;
        if (!game_unit_visible_to(g, me, u)) continue;
        const UnitType *t = &g->types[u->type];
        int reach = t->move + game_range_max(g, t);   /* 夜は間接の射程が短い */
        if (u->ammo <= 0 && t->ammo > 0) continue;   /* 弾切れは脅威にならない */
        for (int y = 0; y < g->h; y++)
            for (int x = 0; x < g->w; x++) {
                if (hex_distance(u->pos.x, u->pos.y, x, y) > reach) continue;
                for (int c = 0; c < ARMOR_COUNT; c++)
                    s->threat[c][y][x] += t->atk[c];
            }
    }
}

/* ------------------------------------------------------------------ */
/* 目標地点: 占領対象建物 / 敵ユニット                                 */
/* ------------------------------------------------------------------ */
static int building_value(const Game *g, int x, int y)
{
    const TerrainType *t = game_terrain_at(g, x, y);
    /* 敵が超兵器を建造中の拠点は最優先。奪えば建造は最初からやり直しになる */
    if (game_sw_building_at(g, x, y) && g->tiles[y][x].owner != g->current) return 5000;
    if (t->is_hq) return 2000;
    if (t->is_lab) return 1500;                 /* 研究所（相手のポイント源） */
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
            /* 自軍と援軍の拠点は目標にしない（中立と敵のみ） */
            if (!t->capturable || !ai_can_take(g, me, g->tiles[y][x].owner)) continue;
            int d = hex_distance(fx, fy, x, y);
            /* 価値の高い建物を優先（距離を割引） */
            d -= building_value(g, x, y) / 500;
            if (d < best) best = d;
        }
    return best;
}

/* 最寄りの敵/中立の生産拠点までの距離。
 * 首都の無いマップ（海戦マップ・空戦マップ）で前進の向きを作るためのもの。 */
static int nearest_enemy_base(const Game *g, int me, int fx, int fy)
{
    int best = 9999;
    for (int y = 0; y < g->h; y++)
        for (int x = 0; x < g->w; x++) {
            const TerrainType *t = &g->terrains[g->tiles[y][x].terrain];
            if (!t->capturable || t->produces == PROD_NONE) continue;
            if (!ai_can_take(g, me, g->tiles[y][x].owner)) continue;
            int d = hex_distance(fx, fy, x, y);
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
        if (!(u->flags & UF_ALIVE) || !game_is_enemy(g, me, u->owner)) continue;
        if (!game_unit_visible_to(g, me, u)) continue;
        int d = hex_distance(fx, fy, u->pos.x, u->pos.y);
        if (d < best) best = d;
    }
    for (int y = 0; y < g->h; y++)
        for (int x = 0; x < g->w; x++)
            if (g->terrains[g->tiles[y][x].terrain].is_hq &&
                ai_can_take(g, me, g->tiles[y][x].owner)) {
                int d = hex_distance(fx, fy, x, y);
                if (d < best) best = d;
            }
    /* 見える敵も、狙える敵首都も無いときだけ、敵の生産拠点を目標にする。
     *
     * 首都の無いマップ（群狼の海・蒼穹の戦線）では、敵が視界に入るまで
     * ここが全マスで 9999 を返していた。距離の項が一律になると移動先の
     * 点数が地形防御だけで決まり、海や空の上で防御を持つのは自軍の港・
     * 空港しかないので、部隊が母港に貼り付いて動かなくなる。そのまま
     * 生産マスを塞ぐので増援も止まり、両軍が何もしないまま時間切れに
     * なっていた（実測: 群狼の海で最初の5ターン、資金4900を抱えたまま
     * 1隻も出せていなかった）。
     *
     * **常に候補へ入れてはいけない。** 首都より近い工場に釣られて進軍が
     * 逸れる。ここは「目標がゼロ」のときだけ効く受け皿にとどめること。 */
    if (best >= 9999) best = nearest_enemy_base(g, me, fx, fy);
    return best;
}

/* 最寄りの「この部隊が実際に damage を与えられる敵」までの距離。
 * 立体戦では射程内に居ても手が出ない組み合わせが多い（対空戦車→戦車、
 * 戦闘機→歩兵、対潜装備なし→潜水艦）。全部の敵を等しく追いかけると、
 * 手の出ない相手に貼り付いて撃たれるだけになるので、狙える相手を優先する。
 * 狙える敵が1体も居なければ -1 を返し、呼び出し側が従来の判断に戻す。 */
static int nearest_target_goal(const Game *g, int me, const Unit *self,
                               int fx, int fy)
{
    const UnitType *st = &g->types[self->type];
    int best = -1;
    for (int i = 0; i < g->n_units; i++) {
        const Unit *d = &g->units[i];
        if (!(d->flags & UF_ALIVE) || (d->flags & UF_LOADED)) continue;
        if (!game_is_enemy(g, me, d->owner)) continue;
        if (!game_unit_visible_to(g, me, d)) continue;
        if (unit_atk_vs(st, &g->types[d->type]) <= 0) continue;
        if (!unit_can_attack_target(g, self, d)) continue;
        int dd = hex_distance(fx, fy, d->pos.x, d->pos.y);
        if (best < 0 || dd < best) best = dd;
    }
    return best;
}

/* 最寄りの敵首都までの距離。前進の「引力」の要。 */
static int nearest_enemy_hq(const Game *g, int me, int fx, int fy)
{
    int best = 9999;
    for (int y = 0; y < g->h; y++)
        for (int x = 0; x < g->w; x++)
            if (g->terrains[g->tiles[y][x].terrain].is_hq &&
                ai_can_take(g, me, g->tiles[y][x].owner)) {
                int d = hex_distance(fx, fy, x, y);
                if (d < best) best = d;
            }
    return best;
}

/* 前進の目標距離。狙える敵と敵首都の近いほうへ向かう。
 * **敵首都を必ず候補に残すこと。** 「倒せる敵」だけを見ると部隊が殴り合いに
 * 終始して首都へ進まなくなり、決着が減って損失だけ増える
 * （sim_ai 実測: 首都を外したら 決着6→4・総損失 9595→12098 に悪化した）。 */
static int advance_goal(const Game *g, int me, const Unit *self, int fx, int fy)
{
    int d = nearest_target_goal(g, me, self, fx, fy);
    if (d < 0) return nearest_enemy_goal(g, me, fx, fy);
    int h = nearest_enemy_hq(g, me, fx, fy);
    return d < h ? d : h;
}

/* 目指す先が1つも無いか（見える敵も、狙える敵首都も、敵の生産拠点も無い）。
 * 拠点の無いマップ（涸れた戦線）で、敵がまだ視界に入っていない状態。
 * このとき前進の距離はどのマスでも 9999 になり、部隊は防御地形に座った
 * まま動かなくなる。nearest_enemy_goal は目標が無いときだけ 9999 を返すので、
 * どこから測っても同じ（座標は何でもいい）。 */
static bool nothing_to_head_for(const Game *g, int me)
{
    return nearest_enemy_goal(g, me, 0, 0) >= 9999;
}

/* 索敵の向き。まずマップ中央を目指し、中央が見えたら最寄りの
 * 「まだ見えていない、この部隊が入れるマス」を目指す。
 * 中央を先に置くのは、見えていないマスは自軍の背後にも広がっていて、
 * 最寄りの霧だけを追うと部隊が四方に散って後ろへ下がる者まで出るため。
 * 対称なマップなら敵も中央へ出てくるので、そこで接触できる。 */
static int scout_goal(const Game *g, int me, MoveClass mc, int x, int y)
{
    int cx = g->w / 2, cy = g->h / 2;
    if (!g->visible[me][cy][cx]) return hex_distance(x, y, cx, cy);
    int best = 9999;
    for (int yy = 0; yy < g->h; yy++)
        for (int xx = 0; xx < g->w; xx++) {
            if (g->visible[me][yy][xx]) continue;
            if (g->terrains[g->tiles[yy][xx].terrain].mcost[mc] <= 0) continue;
            int d = hex_distance(x, y, xx, yy);
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

/* 最寄りの「降りられる」補給地点までの距離。そのレイヤーが空いているか、
 * 自分自身が乗っている補給地点だけを数える。空いている所が1つも無ければ
 * 埋まっている所も含めた距離を返す（とにかく帰る向きは作る）。
 *
 * 空港1マスに航空機は1機しか入れない。空いているかを見ずに最寄りへ戻すと、
 * 先客のいる空港の隣で着陸待ちのまま燃料が尽きる。実測では燃料切れで
 * 落ちた964機のうち870機が「最寄りの空港が埋まっていた」ものだった。 */
static int nearest_free_supply(const Game *g, int me, int ui, MoveClass mc,
                               int fx, int fy)
{
    Layer L = unit_layer(mc);
    int best = 9999;
    for (int y = 0; y < g->h; y++)
        for (int x = 0; x < g->w; x++) {
            const Tile *tl = &g->tiles[y][x];
            const TerrainType *t = &g->terrains[tl->terrain];
            if (tl->owner != me || !(t->supplies & (1u << mc))) continue;
            int occ = game_unit_at_layer(g, x, y, L);
            if (occ >= 0 && occ != ui) continue;
            int d = hex_distance(fx, fy, x, y);
            if (d < best) best = d;
        }
    return best < 9999 ? best : nearest_supply(g, me, mc, fx, fy);
}

/* 燃料が帰り道ぎりぎりか。行動順を決めるのに使う（build_order で最後に回す）。
 * 移動先ごとの判定は act_unit の「燃料の安全圏」が別にやっている。
 *
 * 航空機は空中にいるだけで毎ターン upkeep（既定4）を払い、0 になると墜落する。
 * 燃料を見る判断が何も無かったころは、短縮コースのCPU対戦1回で 605 機が
 * 燃料切れで落ちていた。
 *
 * 帰り道に要る燃料 = 距離ぶんの移動燃料 + 着くまでの手番ぶんの維持燃料。
 * 1手番ぶん余裕を持たせる。移動燃料はその移動種別の最も重い地形で見積もる
 * （経路の地形までは追わない。多めに見れば早めに帰るだけで済む）。 */
static bool needs_fuel_return(const Game *g, int me, const Unit *u)
{
    const UnitType *ut = &g->types[u->type];
    if (ut->no_fuel != NOFUEL_DIE && ut->no_fuel != NOFUEL_DAMAGE) return false;
    MoveClass mc = (MoveClass)ut->mclass;
    int d = nearest_free_supply(g, me, (int)(u - g->units), mc, u->pos.x, u->pos.y);
    if (d <= 0 || d >= 9999) return false;   /* 補給地点の上にいる / 戻る先が無い */

    int max_cost = 2;
    for (int t = 0; t < g->n_terrains; t++)
        if (g->terrains[t].mcost[mc] > max_cost) max_cost = g->terrains[t].mcost[mc];
    int per_hex = (max_cost + 1) / 2 * (ut->fuel_rate > 1 ? ut->fuel_rate : 1);
    int move = ut->move > 0 ? ut->move : 1;
    int turns = (d + move - 1) / move;
    int need = d * per_hex + (turns + 1) * ut->upkeep;
    return u->fuel <= need;
}

/* ------------------------------------------------------------------ */
/* 手番開始                                                            */
/* ------------------------------------------------------------------ */
/* ------------------------------------------------------------------ */
/* 上陸作戦（陸路で届かない敵拠点へ輸送ユニットで部隊を送る）          */
/* ------------------------------------------------------------------ */

/* 機動ドメイン 0=陸 1=空 2=海（game.c の move_domain と同じ分類） */
static int domain_from_mclass(int mc)
{
    if (mc == MC_AIR) return 1;
    if (mc == MC_SEA || mc == MC_SUB) return 2;
    return 0;
}
static int move_domain_of(const Game *g, const Unit *u)
{
    return domain_from_mclass(g->types[u->type].mclass);
}

/* 無武装（攻撃力オール0）か。輸送艦/輸送ヘリ/トラックは無武装だが、
 * 空母(CARRIER)は艦載機を積んでも対空攻撃力を持つため輸送とは区別する
 * （区別しないと上陸作戦中に空母が一切攻撃しなくなる）。 */
static bool type_unarmed(const UnitType *t)
{
    for (int c = 0; c < ARMOR_COUNT; c++)
        if (t->atk[c] > 0) return false;
    return true;
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

    /* 自軍に港/空港か輸送艦・輸送ヘリが無ければ上陸は狙わない
     * （輸送ヘリは地形を無視して飛べるので、港が無く空港しか無いマップでも
     *   上陸作戦の足になれる） */
    bool can_ship = false;
    for (int i = 0; i < g->n_units && !can_ship; i++) {
        const Unit *u = &g->units[i];
        if ((u->flags & UF_ALIVE) && u->owner == me &&
            g->types[u->type].capacity > 0 && type_unarmed(&g->types[u->type]) &&
            move_domain_of(g, u) != 0)
            can_ship = true;
    }
    for (int y = 0; y < g->h && !can_ship; y++)
        for (int x = 0; x < g->w && !can_ship; x++)
            if (g->tiles[y][x].owner == me &&
                (g->terrains[g->tiles[y][x].terrain].produces == PROD_SEA ||
                 g->terrains[g->tiles[y][x].terrain].produces == PROD_AIR))
                can_ship = true;
    if (!can_ship) return;

    int best = 1 << 30;
    for (int y = 0; y < g->h; y++)
        for (int x = 0; x < g->w; x++) {
            const TerrainType *t = &g->terrains[g->tiles[y][x].terrain];
            if (!t->capturable || !ai_can_take(g, me, g->tiles[y][x].owner)) continue;
            if (s->land_reach[y][x]) continue;         /* 陸路で行ける */
            /* 首都を最優先、次に生産拠点 */
            int pri = t->is_hq ? 0 : (t->produces != PROD_NONE ? 1 : 2);
            int sc = pri * 1000 + x + y;               /* 安定した順序付け */
            if (sc < best) { best = sc; s->inv_x = x; s->inv_y = y; }
        }
    s->amphib = (s->inv_x >= 0);
}

/* 集中攻撃の計画。
 *
 * 攻撃は1体ずつ「その場で一番得な攻撃」を選んでいたので、1体では沈められない
 * 敵に傷だけ付けて散ることが多かった。港の上の艦は毎手番HPが2戻るので、散った
 * 傷は翌手番には消える。群狼の海では17隻で囲んでも最後の2隻を30手番沈められず、
 * 時間切れになっていた。
 *
 * 手番の頭に「この手番に誰がどの敵を撃てるか」を全部数え、味方の見込み
 * ダメージの合計で沈められる敵から順に攻撃役を割り当てる。沈めきれない敵には
 * 割り当てない（傷を散らすだけになるため）。見込みには2割5分の余裕を見て、
 * 直接攻撃は相手の隣に空いているマスの数までしか割り当てない。
 * 実際に撃つかは act_unit が決める（割り当てた敵を撃つ手に少し加点するだけ）。
 *
 * 旧AIとの直接対戦 276 試合で 64勝57敗、損失は同じ（41033 対 41072）。
 * CPU同士では大差にならないが、傷ついた部隊を前に出したままにすると
 * 確実に狩られるので、人間相手には陣形を崩せない圧になる。 */
static MoveRange s_plan_mr;
static struct {
    int n;
    int16_t t[32];
    int16_t e[32];    /* 見込みダメージ×10 */
} s_can[MAX_UNITS];

static void plan_focus(const Game *g, AiState *s)
{
    int me = g->current;
    for (int i = 0; i < MAX_UNITS; i++) { s->focus[i] = -1; s->focused[i] = 0; }
    if (g->ctrl[me] == CTRL_CPU_EASY) return;          /* EASY は撃ち方を揃えない */

    /* 1) 味方ごとに、この手番に撃てる敵と見込みダメージ */
    for (int a = 0; a < g->n_units; a++) {
        s_can[a].n = 0;
        const Unit *u = &g->units[a];
        if (!(u->flags & UF_ALIVE) || u->owner != me) continue;
        if (u->flags & (UF_DONE | UF_LOADED)) continue;
        const UnitType *ut = &g->types[u->type];
        if (type_unarmed(ut)) continue;
        path_move_range(g, a, &s_plan_mr);
        Layer L = unit_layer(ut->mclass);
        for (int y = 0; y < g->h; y++)
            for (int x = 0; x < g->w; x++) {
                if (s_plan_mr.cost[y][x] < 0 || !s_plan_mr.stop[y][x]) continue;
                int occ = game_unit_at_layer(g, x, y, L);
                if (occ >= 0 && occ != a) continue;
                bool moved = !(x == u->pos.x && y == u->pos.y);
                if (!rules_can_attack_now(g, a, moved)) continue;
                int tg[32];
                int nt = rules_list_targets(g, a, x, y, tg, 32);
                for (int k = 0; k < nt; k++) {
                    int e = battle_expect_damage_x10(g, u, &g->units[tg[k]]);
                    int j = 0;
                    while (j < s_can[a].n && s_can[a].t[j] != tg[k]) j++;
                    if (j == s_can[a].n) {
                        if (j >= 32) continue;
                        s_can[a].t[j] = (int16_t)tg[k]; s_can[a].e[j] = 0; s_can[a].n++;
                    }
                    if (e > s_can[a].e[j]) s_can[a].e[j] = (int16_t)e;
                }
            }
    }

    /* 2) 沈められる敵を「HP1あたりの値打ち」の高い順に並べる */
    static int cand[MAX_UNITS];
    static int prio[MAX_UNITS];
    int nc = 0;
    for (int t = 0; t < g->n_units; t++) {
        const Unit *d = &g->units[t];
        if (!(d->flags & UF_ALIVE) || !game_is_enemy(g, me, d->owner)) continue;
        int sum = 0;
        for (int a = 0; a < g->n_units; a++)
            for (int j = 0; j < s_can[a].n; j++)
                if (s_can[a].t[j] == t) sum += s_can[a].e[j];
        if (sum * 4 < d->hp * 10 * 5) continue;         /* 全員で撃っても沈まない（2割5分の余裕） */
        cand[nc] = t;
        prio[nc] = g->types[d->type].cost * 10 / d->hp;
        nc++;
    }
    for (int i = 1; i < nc; i++) {                       /* 値打ちの高い順（挿入ソート） */
        int c = cand[i], p = prio[i], j = i - 1;
        while (j >= 0 && prio[j] < p) { cand[j + 1] = cand[j]; prio[j + 1] = prio[j]; j--; }
        cand[j + 1] = c; prio[j + 1] = p;
    }

    /* 3) 敵ごとに、まだ役の無い味方を見込みダメージの大きい順に足していく */
    static uint8_t used[MAX_UNITS];
    memset(used, 0, sizeof used);
    for (int i = 0; i < nc; i++) {
        int t = cand[i];
        const Unit *tu = &g->units[t];
        int need = tu->hp * 10 * 5 / 4;
        /* 直接攻撃で隣に付けるのは、そのレイヤーで空いている（か味方のいる）隣接マスの数まで */
        int free_adj[LAYER_COUNT];
        for (int L = 0; L < LAYER_COUNT; L++) {
            free_adj[L] = 0;
            for (int dd = 0; dd < HEX_DIRS; dd++) {
                int nx, ny;
                hex_neighbor(tu->pos.x, tu->pos.y, dd, &nx, &ny);
                if (!game_in_bounds(g, nx, ny)) continue;
                int occ = game_unit_at_layer(g, nx, ny, (Layer)L);
                if (occ < 0 || g->units[occ].owner == me) free_adj[L]++;
            }
        }
        int direct_n[LAYER_COUNT] = { 0 };
        int pick[MAX_UNITS], np = 0, acc = 0;
        while (acc < need) {
            int best_a = -1, best_e = 0;
            for (int a = 0; a < g->n_units; a++) {
                if (used[a]) continue;
                bool already = false;
                for (int q = 0; q < np; q++) if (pick[q] == a) already = true;
                if (already) continue;
                const UnitType *at = &g->types[g->units[a].type];
                if (at->range_min < 2 &&
                    direct_n[unit_layer(at->mclass)] >= free_adj[unit_layer(at->mclass)])
                    continue;
                for (int j = 0; j < s_can[a].n; j++)
                    if (s_can[a].t[j] == t && s_can[a].e[j] > best_e) {
                        best_e = s_can[a].e[j]; best_a = a;
                    }
            }
            if (best_a < 0) break;
            pick[np++] = best_a;
            acc += best_e;
            const UnitType *bt = &g->types[g->units[best_a].type];
            if (bt->range_min < 2) direct_n[unit_layer(bt->mclass)]++;
        }
        if (acc < need) continue;                        /* 残りの味方では足りない */
        for (int q = 0; q < np; q++) { used[pick[q]] = 1; s->focus[pick[q]] = (int16_t)t; }
        s->focused[t] = 1;
    }
}

/* 行動順を組み直す: 間接 → 直接戦闘 → 占領系（間接が先に撃ってから前進） */
static void build_order(const Game *g, AiState *s)
{
    int me = g->current;
    s->n_order = 0;
    s->idx = 0;
    /* 3 = 燃料が帰り道ぎりぎりの部隊。**最後に動かす**。先に動く部隊（補給を
     * 終えた機体や、前の手番に生産した機体）が空港を空けてから降りるため。
     * 先に動かすと埋まった空港の隣で着陸待ちになり、そのまま墜落する。 */
    plan_focus(g, s);
    int pass_order[4] = {0, 1, 2, 3};
    int n_front = 0;   /* 燃料の苦しい部隊（最後に回す）より前の数 */
    for (int pass = 0; pass < 4; pass++) {
        if (pass_order[pass] == 3) n_front = s->n_order;
        /* 同じ組の中では、集中攻撃を割り当てた部隊を先に動かす
         * （あとの部隊に攻撃位置を取られる前に、決めた相手を撃たせる） */
        for (int sub = 0; sub < 2; sub++)
        for (int i = 0; i < g->n_units; i++) {
            if ((s->focus[i] >= 0) != (sub == 0)) continue;
            const Unit *u = &g->units[i];
            if (!(u->flags & UF_ALIVE) || u->owner != me) continue;
            if (u->flags & UF_DONE) continue;
            /* 積まれている部隊は動かせない。空母の艦載機だけは出撃できる */
            if ((u->flags & UF_LOADED) && !game_can_launch(g, i)) continue;
            const UnitType *t = &g->types[u->type];
            int cls;
            if (needs_fuel_return(g, me, u)) cls = 3;
            else if (t->range_min >= 2) cls = 0;
            else if (t->can_capture) cls = 2;
            else cls = 1;
            if (cls == pass_order[pass])
                s->order[s->n_order++] = i;
        }
    }
    /* 索敵中（目指す先が1つも無い）は、視界の広い部隊から動かす。
     * 先に動いた部隊が見える範囲を広げ、あとの部隊はその広がった範囲の中で
     * 動ける（act_unit は索敵中、今見えているマスにしか動かない）。
     * 挿入ソートで安定に並べる。燃料の苦しい部隊は最後のまま。 */
    if (g->fog && nothing_to_head_for(g, me)) {
        for (int i = 1; i < n_front; i++) {
            int ui = s->order[i];
            int vi = game_unit_vision(g, &g->units[ui]);
            int j = i - 1;
            while (j >= 0 && game_unit_vision(g, &g->units[s->order[j]]) < vi) {
                s->order[j + 1] = s->order[j];
                j--;
            }
            s->order[j + 1] = ui;
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
            if (advance_goal(g, me, u, u->pos.x, u->pos.y) <= t->move + t->range_max)
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
    /* --- 後から追加した必殺技。
     * ここを書かないと default に落ちて CPU が一度も撃たなくなり、
     * その指揮官を選んだ CPU が一方的に弱くなってしまう。 --- */
    case CO_POW_SHIELD: {
        if (phase != 0) break;
        /* 敵の間合いに入っている自軍が3体以上＝この手番に叩かれる */
        int n = 0;
        for (int i = 0; i < g->n_units; i++) {
            const Unit *u = &g->units[i];
            if (!(u->flags & UF_ALIVE) || (u->flags & UF_LOADED)) continue;
            if (u->owner != me) continue;
            for (int j = 0; j < g->n_units; j++) {
                const Unit *e = &g->units[j];
                if (!(e->flags & UF_ALIVE) || (e->flags & UF_LOADED)) continue;
                if (!game_is_enemy(g, me, e->owner)) continue;
                const UnitType *et = &g->types[e->type];
                if (hex_distance(e->pos.x, e->pos.y, u->pos.x, u->pos.y)
                    <= et->move + et->range_max) { n++; break; }
            }
        }
        fire = (n >= 3);
        break;
    }
    case CO_POW_ADVANCE: {
        if (phase != 0) break;      /* 動く前に撃つことに意味がある */
        /* まだ進軍途中の部隊が3体以上いるとき */
        int n = 0;
        for (int i = 0; i < g->n_units; i++) {
            const Unit *u = &g->units[i];
            if (!(u->flags & UF_ALIVE) || (u->flags & UF_LOADED)) continue;
            if (u->owner != me || !game_co_affects(g, me, u)) continue;
            const UnitType *t = &g->types[u->type];
            if (advance_goal(g, me, u, u->pos.x, u->pos.y) > t->move + t->range_max)
                n++;
        }
        fire = (n >= 3);
        break;
    }
    case CO_POW_RESUPPLY: {
        if (phase != 1) break;
        /* 燃料か弾薬が半分以下の部隊が3体以上 */
        int n = 0;
        for (int i = 0; i < g->n_units; i++) {
            const Unit *u = &g->units[i];
            if (!(u->flags & UF_ALIVE) || u->owner != me) continue;
            const UnitType *t = &g->types[u->type];
            if ((t->fuel > 0 && u->fuel * 2 <= t->fuel) ||
                (t->ammo > 0 && u->ammo * 2 <= t->ammo)) n++;
        }
        fire = (n >= 3);
        break;
    }
    case CO_POW_VETERAN: {
        if (phase != 1) break;
        /* これで進化のしきい値（経験100）に届く部隊が2体以上。
         * 届かなくても大軍なら経験の総量で元が取れるので保険を入れる */
        int ready = 0, gain = 0;
        for (int i = 0; i < g->n_units; i++) {
            const Unit *u = &g->units[i];
            if (!(u->flags & UF_ALIVE) || u->owner != me) continue;
            if (u->exp < 100 && u->exp + c->power_val >= 100) ready++;
            int room = 100 - u->exp;
            gain += room < c->power_val ? room : c->power_val;
        }
        fire = (ready >= 2 || gain >= c->power_val * 5);
        break;
    }
    case CO_POW_BARRAGE: {
        if (phase != 1) break;      /* 接触してから撃つ */
        int n = 0;
        for (int i = 0; i < g->n_units; i++) {
            const Unit *e = &g->units[i];
            if (!(e->flags & UF_ALIVE) || (e->flags & UF_LOADED)) continue;
            if (!game_is_enemy(g, me, e->owner) || e->hp <= 1) continue;
            for (int j = 0; j < g->n_units; j++) {
                const Unit *u = &g->units[j];
                if (!(u->flags & UF_ALIVE) || (u->flags & UF_LOADED)) continue;
                if (u->owner != me) continue;
                if (hex_distance(u->pos.x, u->pos.y, e->pos.x, e->pos.y) == 1)
                    { n++; break; }
            }
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

/* 超兵器の計画: どの超兵器を目指すか、研究所で誰を解隊するか、資金を貯めるか。
 *
 * 解隊は戦力で勝っているときだけ。陸・空それぞれで、自軍の戦力（値段×HP）が
 * 見えている敵の1.5倍を超える分だけを「余り」とし、安い・傷ついた部隊から
 * 研究所へ向かわせる。敵が1体も見えていない領域では解隊しない（霧の中で戦力を
 * 捨てないため）。余りを全部解隊しても目標のポイントに届かないなら、始めない
 * （遠い目標のために戦力を削らない）。海の部隊は研究所へ行けないので対象外。 */
static int sw_goal_type(const Game *g, int me, int sw)
{
    for (int t = 0; t < g->n_types; t++) {
        if (g->types[t].superweapon != sw || game_sw_taken(g, me, sw)) continue;
        /* その領域の自軍の研究所を持っていること（無ければ作れない） */
        for (int y = 0; y < g->h; y++)
            for (int x = 0; x < g->w; x++)
                if (game_sw_check(g, me, x, y, t) != 1) return t;
    }
    return -1;
}

/* 領域ごと（sw=1 陸 / 2 空 / 3 海）に、研究所へ向かわせて解隊する部隊を決める。
 * 領域 dom = sw-1 の戦力（値段×HP）が、見えている敵の同じ領域の戦力の1.5倍を
 * 超える分だけを余りとする。敵が見えていない領域では解隊しない。 */
static void plan_lab_domain(Game *g, AiState *s, int sw)
{
    int me = g->current, d = sw - 1;
    s->lab_x[d] = s->lab_y[d] = -1;
    int goal = sw_goal_type(g, me, sw);
    if (goal < 0) return;
    int need = g->types[goal].cost;

    /* ポイントは足りている: 資金が3手番以内に貯まるなら普通の生産を控える */
    if (g->lab_points[me][d] >= need) {
        int inc = 0;
        for (int y = 0; y < g->h; y++)
            for (int x = 0; x < g->w; x++)
                if (g->tiles[y][x].owner == me) inc += g->terrains[g->tiles[y][x].terrain].income;
        if (g->funds[me] < need && g->funds[me] + inc * 3 >= need) s->sw_saving = true;
        return;
    }
    /* 自軍のその領域の研究所（首都に近いもの） */
    int best = 1 << 30;
    for (int y = 0; y < g->h; y++)
        for (int x = 0; x < g->w; x++) {
            const Tile *t = &g->tiles[y][x];
            if (t->owner != me || g->terrains[t->terrain].is_lab != sw) continue;
            int dist = 0;
            for (int yy = 0; yy < g->h; yy++)
                for (int xx = 0; xx < g->w; xx++)
                    if (g->tiles[yy][xx].owner == me && g->terrains[g->tiles[yy][xx].terrain].is_hq)
                        dist = hex_distance(x, y, xx, yy);
            if (dist < best) { best = dist; s->lab_x[d] = x; s->lab_y[d] = y; }
        }
    if (s->lab_x[d] < 0) return;

    long mine = 0, foe = 0;
    for (int i = 0; i < g->n_units; i++) {
        const Unit *u = &g->units[i];
        if (!(u->flags & UF_ALIVE) || (u->flags & UF_LOADED)) continue;
        if (game_unit_lab_domain(g, i) != sw) continue;
        long v = (long)g->types[u->type].cost * u->hp / 10;
        if (u->owner == me) mine += v;
        else if (game_is_enemy(g, me, u->owner) && game_unit_visible_to(g, me, u)) foe += v;
    }
    /* 敵の同じ領域の部隊が見えているときは、こちらの戦力がその1.5倍を超えた分が余り。
     * 空は敵機が霧の中で見えないことが多く、見えるまで待つと一度も解隊しない。
     * 見えていない間は、自軍の戦力の1/4までを余りとする（見えないだけで敵が
     * いないとは限らないので、全部は使わない）。 */
    long surplus = (foe > 0) ? mine - foe * 3 / 2 : mine / 4;
    if (surplus <= 0) return;

    /* 候補: 超兵器・占領役・補給・輸送は残す。研究所から遠すぎる部隊は外す */
    int cand[MAX_UNITS], nc = 0;
    for (int i = 0; i < g->n_units; i++) {
        const Unit *u = &g->units[i];
        if (!(u->flags & UF_ALIVE) || (u->flags & UF_LOADED) || u->owner != me) continue;
        if (game_unit_lab_domain(g, i) != sw) continue;
        const UnitType *t = &g->types[u->type];
        if (t->superweapon || t->can_capture || t->supply || type_unarmed(t)) continue;
        if (hex_distance(u->pos.x, u->pos.y, s->lab_x[d], s->lab_y[d]) > t->move * 3) continue;
        cand[nc++] = i;
    }
    for (int i = 1; i < nc; i++) {                 /* 値打ちの小さい順 */
        int c = cand[i], j = i - 1;
        long vc = (long)g->types[g->units[c].type].cost * g->units[c].hp;
        while (j >= 0 && (long)g->types[g->units[cand[j]].type].cost * g->units[cand[j]].hp > vc) {
            cand[j + 1] = cand[j]; j--;
        }
        cand[j + 1] = c;
    }
    long pts = g->lab_points[me][d];
    int pick[MAX_UNITS], np = 0;
    for (int i = 0; i < nc && pts < need; i++) {
        const Unit *u = &g->units[cand[i]];
        long v = (long)g->types[u->type].cost * u->hp / 10;
        if (v > surplus) continue;
        surplus -= v;
        pts += game_disband_refund(g, cand[i]);
        pick[np++] = cand[i];
    }
    if (pts < need) return;                        /* 余りを全部使っても届かない */
    for (int i = 0; i < np; i++) s->to_lab[pick[i]] = 1;
}

static void plan_lab(Game *g, AiState *s)
{
    memset(s->to_lab, 0, sizeof s->to_lab);
    s->sw_saving = false;
    for (int sw = 1; sw <= 3; sw++) plan_lab_domain(g, s, sw);
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
    plan_lab(g, s);
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
    bool hard = CTRL_IS_HARD(g->ctrl[me]);

    /* 研究所へ向かう部隊: 研究所に一番近い所まで動き、着いたら解隊する */
    if (s->to_lab[ui]) {
        int dm = game_unit_lab_domain(g, ui) - 1;
        int lx = s->lab_x[dm], ly = s->lab_y[dm];
        if (lx >= 0) {
            s->last_unit = ui;
            s->last_target = -1;
            if (!(u->pos.x == lx && u->pos.y == ly)) {
                path_move_range(g, ui, &s_mr);
                Layer L = unit_layer(mc);
                int bx = u->pos.x, by = u->pos.y;
                int bd = hex_distance(bx, by, lx, ly);
                for (int y = 0; y < g->h; y++)
                    for (int x = 0; x < g->w; x++) {
                        if (s_mr.cost[y][x] < 0 || !s_mr.stop[y][x]) continue;
                        int occ = game_unit_at_layer(g, x, y, L);
                        if (occ >= 0 && occ != ui) continue;
                        int dd = hex_distance(x, y, lx, ly);
                        if (dd < bd) { bd = dd; bx = x; by = y; }
                    }
                int fx, fy, fuel;
                path_walk(g, ui, &s_mr, bx, by, &fx, &fy, &fuel);
                game_move_unit(g, ui, fx, fy, fuel);
            }
            if (game_on_own_lab(g, ui)) game_disband_unit(g, ui);
            else if (g->units[ui].flags & UF_ALIVE) game_wait_unit(g, ui);
            return;
        }
    }

    /* 進化: できるなら進化する（rank5 は上限で、貯め続けても無駄になるため）。
     * ただし HP が高いときだけ。瀕死のまま進化すると熟練度を失って即撃破される。
     * EASY は半分の確率で見送る（弱さの演出）。 */
    if (game_can_evolve(g, ui) && u->hp >= 7 &&
        !(easy && rng_range(&g->rng, 0, 99) < 50)) {
        game_evolve_unit(g, ui);
        s->last_unit = ui;
        s->last_target = -1;
        return;
    }

    /* 工兵: 隣接に自陣営側の壊れた地形があって資金が届くなら直す。
     * **破壊はCPUにやらせない**。自分の進路を自分で潰しかねないし、
     * 壊す価値のある地形を見分けるには盤面全体の読みが要る。
     * 復旧だけなら、やって損をすることがない。 */
    if (game_unit_is_engineer(g, ui)) {
        for (int d = 0; d < HEX_DIRS; d++) {
            int nx, ny;
            hex_neighbor(u->pos.x, u->pos.y, d, &nx, &ny);
            if (game_work_kind_at(g, ui, nx, ny) != WORK_REPAIR) continue;
            if (g->funds[me] < game_work_cost(g, nx, ny)) continue;
            if (game_do_work(g, ui, nx, ny) != WORK_NONE) {
                s->last_unit = ui;
                s->last_target = -1;
                return;
            }
        }
    }

    path_move_range(g, ui, &s_mr);

    /* 索敵中か。前の部隊が動いて敵が見えたら、この部隊からは通常の判断に戻る */
    bool scouting = g->fog && nothing_to_head_for(g, me);

    /* 燃料の安全圏。空中にいるだけで燃料を食い、尽きると失う部隊（航空機）は、
     * 「そのマスで手番を終えても、次の手番以降に降りられる補給地点まで
     * 帰り着けるか」を候補マスごとに見る。帰れないマスへ「ただ動く」のは
     * 強く減点する（攻撃は除く。理由は減点を掛ける所に書いた）。
     *
     * 手番の頭で1回だけ「まだ帰れるか」を判定していたときは、判定を通った機体が
     * その手番のうちに7マス前進して帰れない所まで出てしまい、爆撃機を中心に
     * 大量に落ちていた。帰り道に要る燃料は 距離×1マスの燃料 ＋ 着くまでの
     * 手番数×維持燃料。難易度に関係なく掛ける。
     *
     * 航空機の生産に「空港1つにつき3機まで」の上限を掛ける案も試したが、
     * 旧AIとの直接対戦で 13勝23敗と負け越したので採っていない。 */
    /* 潜水艦の待ち伏せ。潜水艦は対潜ユニットにしか見つからない（視界の半分の
     * 距離まで）。見えている敵の対潜ユニットの探知範囲へ「ただ動いて」入るのは
     * 避け、見つからないまま獲物が射程に来るのを待つ。攻撃はこれに縛られない。 */
    int det_x[64], det_y[64], det_r[64], n_det = 0;
    if (ut->is_sub) {
        for (int i = 0; i < g->n_units && n_det < 64; i++) {
            const Unit *e = &g->units[i];
            if (!(e->flags & UF_ALIVE) || (e->flags & UF_LOADED)) continue;
            if (!game_is_enemy(g, me, e->owner)) continue;
            if (!g->types[e->type].anti_sub) continue;
            if (!game_unit_visible_to(g, me, e)) continue;
            det_x[n_det] = e->pos.x; det_y[n_det] = e->pos.y;
            det_r[n_det] = game_sonar_range(g, e);
            n_det++;
        }
    }

    int home_x[64], home_y[64], n_home = 0;
    int fuel_per_hex = 1;
    bool fuel_guard = ut->upkeep > 0 &&
                      (ut->no_fuel == NOFUEL_DIE || ut->no_fuel == NOFUEL_DAMAGE);
    if (fuel_guard) {
        Layer L = unit_layer(mc);
        /* まず先客のいない補給地点だけを数える。全部埋まっているときは
         * 埋まっている所も数える——次の手番には空くかもしれず、ここで判定を
         * 切ると帰る気のないまま飛んでいって落ちる。 */
        for (int pass = 0; pass < 2 && n_home == 0; pass++)
            for (int y = 0; y < g->h && n_home < 64; y++)
                for (int x = 0; x < g->w && n_home < 64; x++) {
                    const Tile *tl = &g->tiles[y][x];
                    if (tl->owner != me ||
                        !(g->terrains[tl->terrain].supplies & (1u << mc))) continue;
                    int occ = game_unit_at_layer(g, x, y, L);
                    if (pass == 0 && occ >= 0 && occ != ui) continue;
                    home_x[n_home] = x; home_y[n_home] = y; n_home++;
                }
        int max_cost = 2;
        for (int t = 0; t < g->n_terrains; t++)
            if (g->terrains[t].mcost[mc] > max_cost) max_cost = g->terrains[t].mcost[mc];
        fuel_per_hex = (max_cost + 1) / 2 * (ut->fuel_rate > 1 ? ut->fuel_rate : 1);
        /* 空きのある自軍の空母も帰る先になる（着艦すれば次の手番に補給される） */
        for (int i = 0; i < g->n_units && n_home < 64; i++) {
            const Unit *c = &g->units[i];
            if (!(c->flags & UF_ALIVE) || (c->flags & UF_LOADED) || c->owner != me) continue;
            if (!g->types[c->type].resupply_cargo || i == ui) continue;
            if (!game_can_board(g, ui, i)) continue;
            home_x[n_home] = c->pos.x; home_y[n_home] = c->pos.y; n_home++;
        }
        if (n_home == 0) fuel_guard = false;   /* 帰る先が無いなら判定しても仕方ない */
    }

    /* このユニットが受ける脅威は「自分の装甲カテゴリ」で引く。
     * 立体戦では相手によって危険度がまるで違うのでここを取り違えると、
     * 戦車が高射砲を恐れて動けない・戦闘機が対空砲に突っ込む、といった挙動になる。 */
    const int (*threat)[MAX_MAP_W] = s->threat[ut->armor];

    Plan best = { -1000000, u->pos.x, u->pos.y, 0, -1, -1, -1 };
    /* HARD: 損傷ユニットは補給地点へ。
     * 夜は攻撃力が-20%になり領域をまたぐ攻撃もできないので、
     * 通常ユニットは夜のうちに戻って立て直すのが合理的。
     * 夜間ユニットは夜こそ出番なので下がらせない。 */
    bool retreat = hard && (u->hp <= 3 ||
                            (game_is_night(g) && !ut->night && u->hp <= 6));

    /* このユニットが上陸作戦・輸送任務に関わるか（輸送艦/輸送ヘリ/トラック。
     * 空母は capacity>0 でも武装しているので type_unarmed で除外する） */
    int dom = move_domain_of(g, u);
    bool is_transport = (ut->capacity > 0 && type_unarmed(ut));
    int cargo_n = 0;
    for (int cs = 0; cs < MAX_CARGO; cs++) if (u->cargo[cs] >= 0) cargo_n++;
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
    /* 陸ユニットが「陸路では目標に行けない」＝船・輸送ヘリに乗るべき状況か */
    bool wants_ride = s->amphib && dom == 0 && !ut->supply &&
                      s->inv_x >= 0 && !s->land_reach[s->inv_y][s->inv_x];
    /* 足の遅い陸ユニットが、遠い目的地へトラックで速く運んでもらいたい状況か
     * （上陸作戦とは無関係。トラック側の「拾いに行く」ロジックと対になる判定）。
     * move*4 という長い距離を要求するのは、近場の攻撃・占領の好機をトラック待ちで
     * 逃さないようにするため（sim_ai実測: move*2 だと m01 のような純陸マップでも
     * NORMAL同士の対戦が軒並みターン切れの引き分けになった＝乗車が交戦を阻害した）。 */
    bool wants_speed_ride = dom == 0 && !ut->supply && ut->move <= 3 &&
                             ut->n_transport_by > 0 &&
                             (ut->can_capture
                                ? nearest_capture_goal(g, me, u->pos.x, u->pos.y)
                                : nearest_enemy_goal(g, me, u->pos.x, u->pos.y))
                               >= ut->move * 4;

    for (int y = 0; y < g->h; y++) {
        for (int x = 0; x < g->w; x++) {
            if (s_mr.cost[y][x] < 0 || !s_mr.stop[y][x]) continue;
            /* 索敵中は今見えている範囲の中でだけ動く（見えない所へ踏み込まない） */
            if (scouting && !g->visible[me][y][x]) continue;
            /* 自レイヤーの占有は避ける。ただし搭乗できる味方輸送の上は候補にする */
            int board_t = -1, join_t = -1;
            {
                int occ = game_unit_at_layer(g, x, y, unit_layer(mc));
                if (occ >= 0 && occ != ui) {
                    int occ_dom = move_domain_of(g, &g->units[occ]);
                    /* 上陸: トラック(陸)には乗らない（海・空隙は越えられない）。
                     * 速度支援: トラックにだけ乗る（船・ヘリに乗せてもらう理由がない）。 */
                    bool ride_ok = (wants_ride && occ_dom != 0) ||
                                   (wants_speed_ride && occ_dom == 0);
                    if (ride_ok && g->units[occ].owner == me &&
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
                /* 輸送ヘリは LAYER_AIR にいるため、陸ユニット自身のレイヤー
                 * （LAYER_SURFACE）の占有チェックには映らない。地表が空いていて
                 * 頭上にヘリがいる、というケースを別途拾う。 */
                if (board_t < 0 && wants_ride) {
                    int air = game_unit_at_layer(g, x, y, LAYER_AIR);
                    if (air >= 0 && air != ui && g->units[air].owner == me &&
                        game_can_board(g, ui, air))
                        board_t = air;
                }
            }
            /* 搭乗プラン: 乗れるなら高い優先度で乗る（上陸の足を確保する）。
             * トラックへの速度支援搭乗は「上陸の足」ほど絶対に必要ではない
             * （歩いても行ける）ので、前進評価と同スケールにわずかな優遇を
             * 乗せるだけに留める（攻撃・占領の好機を奪わないため）。 */
            if (board_t >= 0) {
                int sc;
                if (move_domain_of(g, &g->units[board_t]) == 0) {
                    const TerrainType *bterr = game_terrain_at(g, x, y);
                    int bbase = bterr->def_bonus * 2 - threat[y][x] / THREAT_DIV;
                    int goal = ut->can_capture ? nearest_capture_goal(g, me, x, y)
                                                : nearest_enemy_goal(g, me, x, y);
                    sc = bbase - goal * 40 - 150; /* 前進評価(-200)よりわずかに優遇するだけ */
                } else {
                    sc = 4000 - hex_distance(x, y, s->inv_x, s->inv_y) * 10;
                }
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
            int base = tdef * 2 - threat[y][x] / THREAT_DIV;

            /* 燃料の安全圏の外か。外なら「ただ動くだけ」の手を強く減点する
             * （前進評価で引く）。**攻撃には掛けない**——帰れなくても撃つ価値が
             * あるなら撃たせる。攻撃にも掛けた版は墜落が 605→220 まで減ったが、
             * 旧AIとの直接対戦で 13勝26敗と大きく負け越した。片道の攻撃で与える
             * 打撃が、落ちる機体の損より大きい。攻撃を除いた今の形で 19勝20敗、
             * 墜落は 605→518。帰り道に近いほど減点を軽くして、どこも安全で
             * ないときは少しでも家に近い所を選ばせる。 */
            int unsafe_pen = 0;
            for (int k = 0; k < n_det; k++)
                if (hex_distance(x, y, det_x[k], det_y[k]) <= det_r[k]) {
                    unsafe_pen += 600;   /* 見つかる所へは出ない */
                    break;
                }
            if (fuel_guard) {
                int dh = 9999;
                for (int k = 0; k < n_home; k++) {
                    int dd = hex_distance(x, y, home_x[k], home_y[k]);
                    if (dd < dh) dh = dd;
                }
                if (dh > 0) {
                    int umove = ut->move > 0 ? ut->move : 1;
                    int f = u->fuel - (s_mr.cost[y][x] + 1) / 2;
                    int turns = (dh + umove - 1) / umove;
                    /* +1手番ぶんの維持燃料は、着いた空港に先客がいて1手番
                     * 待たされる分の余裕 */
                    if (f < (turns + 1) * ut->upkeep + dh * fuel_per_hex + 1)
                        unsafe_pen += 100000 + dh * 100;
                }
            }

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
                int d = nearest_free_supply(g, me, ui, mc, x, y);
                int sc = base - d * 60;
                if (sc > best.score) {
                    best.score = sc; best.mx = x; best.my = y;
                    best.action = 0; best.target = -1;
                }
                continue;
            }

            /* --- 輸送艦・輸送ヘリ: 積んで運ぶ / 空なら味方を拾いに行く ---
             * dom!=0 でトラックを除外（トラックは陸路しか走れず上陸作戦の役に立たない） */
            if (is_transport && dom != 0 && s->amphib) {
                int sc;
                if (has_cargo) {
                    /* 積荷あり: 上陸目標に最も近い「降ろせる陸地」を探す。
                     * 空挺降下できる輸送機は真下（自分と同じヘクス）も候補になるので、
                     * 目標のヘクスまで飛んでそのまま降ろせる＝内陸へ直接送り込める。 */
                    int bestu = -1, bux = -1, buy = -1;
                    if (ut->paradrop && game_can_unload_to(g, ui, x, y)) {
                        bestu = hex_distance(x, y, s->inv_x, s->inv_y);
                        bux = x; buy = y;
                    }
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
                        sc = 6000 - bestu * 60 - threat[y][x] / THREAT_DIV;
                        if (sc > best.score) {
                            best.score = sc; best.mx = x; best.my = y;
                            best.action = 5; best.target = -1;
                            best.ux = bux; best.uy = buy;
                        }
                    }
                    /* 降ろせないなら、とにかく目標へ近づく */
                    sc = 3000 - hex_distance(x, y, s->inv_x, s->inv_y) * 50
                         - threat[y][x] / THREAT_DIV;
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
                    sc -= threat[y][x] / THREAT_DIV;
                    if (sc > best.score) {
                        best.score = sc; best.mx = x; best.my = y;
                        best.action = 0; best.target = -1;
                    }
                }
                continue;
            }

            /* --- トラック: ノロマな味方を目的地の近くまで速く運ぶ。
             * 上陸作戦とは無関係に、常時この役割で動く（陸路のみの速度支援）。 */
            if (is_transport && dom == 0) {
                int sc;
                if (has_cargo) {
                    int cslot = u->cargo[game_first_cargo(g, ui)];
                    const UnitType *put = &g->types[g->units[cslot].type];
                    int here_goal = put->can_capture
                                      ? nearest_capture_goal(g, me, x, y)
                                      : nearest_enemy_goal(g, me, x, y);
                    /* 積荷の目的地に一番近い「降ろせる隣接地」を探す */
                    int bestu = -1, bux = -1, buy = -1;
                    for (int d = 0; d < HEX_DIRS; d++) {
                        int nx, ny;
                        hex_neighbor(x, y, d, &nx, &ny);
                        if (!game_in_bounds(g, nx, ny)) continue;
                        if (!game_can_unload_to(g, ui, nx, ny)) continue;
                        int gd = put->can_capture
                                   ? nearest_capture_goal(g, me, nx, ny)
                                   : nearest_enemy_goal(g, me, nx, ny);
                        if (bestu < 0 || gd < bestu) { bestu = gd; bux = nx; buy = ny; }
                    }
                    /* 降ろすことで目的地に近づく場合だけ降ろす（無駄な即降ろしを防ぐ） */
                    if (bestu >= 0 && bestu < here_goal) {
                        sc = 5000 - bestu * 60 - threat[y][x] / THREAT_DIV;
                        if (sc > best.score) {
                            best.score = sc; best.mx = x; best.my = y;
                            best.action = 5; best.target = -1;
                            best.ux = bux; best.uy = buy;
                        }
                    }
                    /* まだ近づけるなら、積んだまま目的地へ走る */
                    sc = 2500 - here_goal * 50 - threat[y][x] / THREAT_DIV;
                    if (sc > best.score) {
                        best.score = sc; best.mx = x; best.my = y;
                        best.action = 0; best.target = -1;
                    }
                } else {
                    /* 空: 遠出が必要なノロマな味方の隣へ拾いに行く */
                    int nd = 9999;
                    for (int i = 0; i < g->n_units; i++) {
                        const Unit *f = &g->units[i];
                        if (!(f->flags & UF_ALIVE) || (f->flags & UF_LOADED)) continue;
                        if (f->owner != me || !game_can_board(g, i, ui)) continue;
                        const UnitType *ft = &g->types[f->type];
                        if (ft->move > 3) continue; /* 元々速い部隊は自走に任せる */
                        int fgoal = ft->can_capture
                                      ? nearest_capture_goal(g, me, f->pos.x, f->pos.y)
                                      : nearest_enemy_goal(g, me, f->pos.x, f->pos.y);
                        if (fgoal < ft->move * 4) continue; /* 自走で十分近い（wants_speed_rideと同じ基準） */
                        int dd = hex_distance(x, y, f->pos.x, f->pos.y);
                        if (dd < nd) nd = dd;
                    }
                    sc = (nd < 9999) ? (2000 - nd * 70) : 0;
                    sc -= threat[y][x] / THREAT_DIV;
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
                ai_can_take(g, me, g->tiles[y][x].owner)) {
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
                        dist >= dt->range_min && dist <= game_range_max(g, dt) &&
                        d->ammo > 0 && unit_atk_vs(dt, ut) > 0 &&
                        !game_night_blocks(g, d, u)) {   /* 夜は反撃されない組み合わせがある */
                        int cx10 = battle_expect_damage_x10(g, d, u);
                        loss = ut->cost * cx10 / 100;
                    }
                    int sc = base + gain - loss * 2 / 3;
                    /* 集中攻撃: 割り当てた相手を撃つ手を少し推す。大きく推すと、危険な
                     * 位置からでも撃ちに行って返り討ちに遭う（最初に試した +300+値段/2 と
                     * 他の味方の割り当て先にも +150 の版は、旧AIに 16勝22敗だった） */
                    if (s->focus[ui] == targets[k]) sc += 200;
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
                          : advance_goal(g, me, u, x, y);
                if (scouting && d >= 9999) d = scout_goal(g, me, mc, x, y);
                int sc = base - d * 40 - 200 - unsafe_pen; /* 攻撃・占領より弱い基本値 */
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
    /* 合流のはみ出しを戻す先に要るので、動く前の位置を控えておく */
    int ox = u->pos.x, oy = u->pos.y;
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
            if (g->types[u->type].paradrop &&
                game_can_unload_to(g, ui, u->pos.x, u->pos.y)) {
                nx = u->pos.x; ny = u->pos.y;      /* 空挺: 真下へ続けて降ろす */
            }
            for (int d = 0; d < HEX_DIRS && nx < 0; d++) {
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
        game_join_units(g, ui, best.target, ox, oy); /* 手負い同士を合流 */
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

    /* 自軍構成を数えると同時に、敵の装甲構成と
     * 「そのカテゴリにこちらが出せる火力」も集める。 */
    int foe[ARMOR_COUNT] = { 0 }, mine[ARMOR_COUNT] = { 0 }, foe_total = 0;
    /* 潜水艦は別勘定。装甲区分は水上艦と同じ「海」だが、撃てるのは対潜装備の
     * ある部隊（駆逐艦・攻撃ヘリなど）だけ。同じ「海」で数えていたので、潜水艦を
     * 見つけても対艦攻撃力の高い戦艦や巡洋艦——潜水艦を撃てない艦——を買って
     * 応えていた。 */
    int foe_sub = 0, mine_sub = 0;
    /* 対潜の護衛（1b）に使う: 自軍の武装した艦、対潜ユニット、敵に艦があり得るか */
    int n_warship = 0, n_antisub = 0;
    bool enemy_navy = false;

    int n_cap = 0, n_combat = 0, n_supplier = 0;
    int n_transport = 0, n_riders = 0, n_truck = 0, n_slow = 0;
    for (int i = 0; i < g->n_units; i++) {
        const Unit *u = &g->units[i];
        if (!(u->flags & UF_ALIVE)) continue;
        if (u->owner != me) {
            const UnitType *et = &g->types[u->type];
            /* 見えていない敵は数えない（脅威マップと揃える） */
            if (!game_is_enemy(g, me, u->owner)) continue;
            if (!game_unit_visible_to(g, me, u)) continue;
            if (domain_from_mclass(et->mclass) == 2) enemy_navy = true;
            if (et->is_sub) foe_sub += et->cost;
            else foe[et->armor] += et->cost;
            foe_total += et->cost;
            continue;
        }
        const UnitType *t = &g->types[u->type];
        for (int c = 0; c < ARMOR_COUNT; c++) mine[c] += t->atk[c];
        if (t->anti_sub) { mine_sub += t->atk_sub; n_antisub++; }
        if (domain_from_mclass(t->mclass) == 2 && !type_unarmed(t) && !t->supply) n_warship++;
        if (t->supply) n_supplier++;
        else if (t->can_capture) n_cap++;
        else n_combat++;
        int ud = move_domain_of(g, u);
        if (t->capacity > 0 && type_unarmed(t)) {
            if (ud == 0) n_truck++;          /* トラック */
            else n_transport++;              /* 輸送艦・輸送ヘリ（上陸作戦の足） */
        }
        /* 船・ヘリに乗せられる陸ユニット（上陸要員の在庫） */
        if (ud == 0 && t->n_transport_by > 0) n_riders++;
        /* トラックに乗せる価値がある、足の遅い陸ユニット */
        if (ud == 0 && !t->supply && t->move <= 3 && t->n_transport_by > 0) n_slow++;
    }

    /* 奪える建物が残っているなら占領要員を最低3体維持 */
    int open_bldgs = 0;
    for (int yy = 0; yy < g->h; yy++)
        for (int xx = 0; xx < g->w; xx++)
            if (g->terrains[g->tiles[yy][xx].terrain].capturable &&
                ai_can_take(g, me, g->tiles[yy][xx].owner))
                open_bldgs++;

    bool want_capture = (n_cap < 3 && open_bldgs > 0);

    /* 対潜の護衛。艦隊には常に対潜ユニットを4隻に1隻は混ぜておく。
     * 潜水艦は対潜ユニットにしか見えないので、「見えたら駆逐艦を作る」では
     * 間に合わない——駆逐艦を失った側は潜水艦が一生見えず、作る理由も生まれない。
     * 群狼の海では片方が対潜0隻のまま、見えない潜水艦4隻に沈められ続けていた。
     * 敵が艦を出せる（港を持っている／艦が見えている）ときだけ効かせる。
     *
     * 潜水艦しか作らない相手と群狼の海で16戦させると、沈めた潜水艦が 153→229
     * （+50%）に増えた。ただしそれでも潜水艦1隻に3隻を失う。潜水艦が駆逐艦より
     * 安くて強いという性能の差で、比率を2隻に1隻に上げても 0.32→0.33 と
     * 変わらなかった（4隻に1隻のままにしている）。 */
    for (int yy = 0; yy < g->h && !enemy_navy; yy++)
        for (int xx = 0; xx < g->w && !enemy_navy; xx++) {
            const Tile *tl = &g->tiles[yy][xx];
            if (tl->owner >= 0 && game_is_enemy(g, me, tl->owner) &&
                g->terrains[tl->terrain].produces == PROD_SEA)
                enemy_navy = true;
        }
    bool want_escort = enemy_navy && n_warship >= 2 &&
                       n_antisub < (n_warship + 3) / 4;


    /* 装甲カテゴリごとの「需要」。
     * **4カテゴリの攻撃力を単純に足してはいけない**。足すと
     * 何にでも当たる万能機ばかりが高得点になり、戦闘機や対空車のような
     * 一点特化が永久に選ばれなくなる（敵の航空部隊が放置される）。 */
    int dem[ARMOR_COUNT];
    for (int c = 0; c < ARMOR_COUNT; c++) {
        if (foe_total <= 0) { dem[c] = 25; continue; }  /* 何も見えていない間は横並び */
        int share = foe[c] * 100 / foe_total;
        /* そのカテゴリを撃てる手札が薄いほど需要を上げる。
         * 「敵に航空が居るのにこちらは対空手段ゼロ」を拾うのが目的。 */
        int mult = (mine[c] < 60) ? 4 : (mine[c] < 150) ? 2 : 1;
        dem[c] = share * mult;
    }
    int dem_sub = 0;
    if (foe_total > 0 && foe_sub > 0) {
        int mult = (mine_sub < 60) ? 4 : (mine_sub < 150) ? 2 : 1;
        dem_sub = foe_sub * 100 / foe_total * mult;
    }

    /* **点が 0 以下のものは買わない**（best_sc を 0 から始める）。
     * 各分岐は「今は要らない」を 0 で表している（乗せる部隊の無い輸送艦、
     * 足りている補給車など）。以前は -1 から始めていたので、資金が細って
     * 高いものが買えないターンに、その「要らない」ものが唯一の候補として
     * 選ばれていた。群狼の海では陸兵が1体もいないのに輸送艦を3隻買っていた。
     * 買わなかったお金は次の手番に持ち越され、役に立つものを買う足しになる。 */
    int best = -1, best_sc = 0;
    for (int t = 0; t < g->n_types; t++) {
        const UnitType *ut = &g->types[t];
        if (!game_type_buildable_at(g, x, y, t)) continue;
        if (ut->cost > g->funds[me]) continue;

        int sc;
        bool ut_unarmed = type_unarmed(ut);
        int ut_dom = domain_from_mclass(ut->mclass);
        if (s->amphib && ut->capacity > 0 && ut_unarmed && ut_dom != 0) {
            /* 上陸作戦が必要: 輸送艦・輸送ヘリ・輸送機を2隻(機)まで確保
             * （乗せる部隊がいる時だけ）。空挺降下できる機体は定員が多く内陸へ
             * 直接降ろせるぶん有利なので、価格差ぶん少しだけ優先する。 */
            sc = (n_transport < 2 && n_riders >= 1)
                     ? 12000 - ut->cost + (ut->paradrop ? 200 : 0) : 0;
        } else if (s->amphib && ut->can_capture && n_riders < 4) {
            /* 上陸させる占領要員を優先的に揃える */
            sc = 9500 - ut->cost;
        } else if (ut->supply) {
            /* 戦闘部隊が育ってきたら補給車を1台だけ確保 */
            sc = (n_supplier == 0 && n_combat >= 6) ? 9000 : 0;
        } else if (want_capture && ut->can_capture) {
            sc = 10000 - ut->cost; /* 安い占領ユニット優先 */
        } else if (ut->capacity > 0 && ut_unarmed && ut_dom == 0) {
            /* トラック: 足の遅い部隊が2体以上育ったら1台だけ確保（速度支援） */
            sc = (n_truck < 1 && n_slow >= 3) ? 4000 - ut->cost : 0;
        } else if (ut->can_capture) {
            sc = 100; /* 占領枠は足りている */
        } else if (want_escort && ut->anti_sub && !ut_unarmed) {
            sc = 8500 - ut->cost;  /* 対潜の護衛が足りない（安いものから） */
        } else {
            /* 敵の構成に対する有効打で選ぶ（序盤=ターン数少は安め優先）。
             * 価格項は控えめにすること。大きくすると、有効打を見ずに
             * 高いユニットを選ぶだけの挙動に戻ってしまう。 */
            int power = 0;
            for (int c = 0; c < ARMOR_COUNT; c++) power += ut->atk[c] * dem[c];
            if (ut->anti_sub) power += ut->atk_sub * dem_sub;
            power /= 100;
            /* 今いる敵のどれにも有効打が無い（丸腰、または撃てない相手ばかり）なら
             * 戦闘部隊としては買う価値が無い。値段の項だけで正の点が付いて
             * 選ばれてしまうので、ここで 0 にしておく。 */
            if (power <= 0) sc = 0;
            else sc = power * 3 + (g->turn >= 6 ? ut->cost / 8 : -ut->cost / 8);
        }
        if (sc > best_sc) { best_sc = sc; best = t; }
    }
    return best;
}

static void do_production(Game *g, AiState *s)
{
    /* 超兵器: 建造できるならする（領域ごとに1体。安いものから決めてある） */
    int me = g->current;
    for (int t = 0; t < g->n_types; t++) {
        if (!g->types[t].superweapon) continue;
        for (int y = 0; y < g->h; y++)
            for (int x = 0; x < g->w; x++)
                if (game_sw_check(g, me, x, y, t) == 0) game_sw_order(g, me, x, y, t);
    }
    if (s->sw_saving) return;                   /* 超兵器のために資金を貯める */
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
        /* 空母の艦載機は、空母のマスから飛び立ってから考える。動かなかったら
         * そのまま着艦し直す（空母の上に浮いたまま燃料を払わせない） */
        int carrier = -1;
        if (u->flags & UF_LOADED) {
            carrier = game_launch(g, ui);
            if (carrier < 0) { u->flags |= UF_DONE; continue; }
        }
        act_unit(g, s, ui);
        if (carrier >= 0 && (u->flags & UF_ALIVE) && !(u->flags & UF_LOADED) &&
            u->pos.x == g->units[carrier].pos.x && u->pos.y == g->units[carrier].pos.y &&
            (g->units[carrier].flags & UF_ALIVE))
            game_reembark(g, ui, carrier, true);
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
