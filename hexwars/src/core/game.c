/* game.c - ゲーム状態・コマンド実行・ターンエンジン */
#include "game.h"
#include "hex.h"
#include <string.h>

/* 機動ドメイン判定（定義は下方の補給セクション）。指揮官のドメイン判定で先に使う */
static int move_domain(int mclass);

/* ------------------------------------------------------------------ */
/* 参照系                                                              */
/* ------------------------------------------------------------------ */

const TerrainType *game_terrain_at(const Game *g, int x, int y)
{
    return &g->terrains[g->tiles[y][x].terrain];
}

int game_unit_at(const Game *g, int x, int y)
{
    for (int i = 0; i < g->n_units; i++) {
        const Unit *u = &g->units[i];
        if ((u->flags & UF_ALIVE) && !(u->flags & UF_LOADED) &&
            u->pos.x == x && u->pos.y == y)
            return i;
    }
    return -1;
}

/* 指定レイヤーに居るユニットの index（居なければ -1）。占有・移動・戦闘の基礎。
 * 立体化: 高さが違えば同一セルに共存するので、レイヤーで絞って引く。 */
int game_unit_at_layer(const Game *g, int x, int y, Layer L)
{
    for (int i = 0; i < g->n_units; i++) {
        const Unit *u = &g->units[i];
        if ((u->flags & UF_ALIVE) && !(u->flags & UF_LOADED) &&
            u->pos.x == x && u->pos.y == y &&
            unit_layer(g->types[u->type].mclass) == L)
            return i;
    }
    return -1;
}

/* セル (x,y) に居る全ユニットをレイヤー別に列挙。out[LAYER_*]=index（不在は-1）。
 * 戻り値=そのセルの総数（0..LAYER_COUNT）。UI/AI の「このセルに何が居るか」に使う。 */
int game_units_at(const Game *g, int x, int y, int out[LAYER_COUNT])
{
    for (int L = 0; L < LAYER_COUNT; L++) out[L] = -1;
    int n = 0;
    for (int i = 0; i < g->n_units; i++) {
        const Unit *u = &g->units[i];
        if ((u->flags & UF_ALIVE) && !(u->flags & UF_LOADED) &&
            u->pos.x == x && u->pos.y == y) {
            Layer L = unit_layer(g->types[u->type].mclass);
            if (out[L] < 0) { out[L] = i; n++; }
        }
    }
    return n;
}

Unit *game_unit(Game *g, int i)
{
    return &g->units[i];
}

const UnitType *unit_type(const Game *g, const Unit *u)
{
    return &g->types[u->type];
}

int unit_rank(const Unit *u)
{
    int r = u->exp / 20;
    return r > 5 ? 5 : r;
}

/* 対潜制限: 潜水艦は anti_sub 持ちからのみ攻撃可能（仕様書 5.5） */
bool unit_can_attack_target(const Game *g, const Unit *atk, const Unit *def)
{
    const UnitType *at = unit_type(g, atk);
    const UnitType *dt = unit_type(g, def);
    if (at->atk[dt->armor] <= 0) return false;
    if (atk->ammo <= 0) return false;
    if (dt->is_sub && !at->anti_sub) return false;
    /* 夜: 陸VS陸・海VS海・空VS空だけ。領域をまたぐ攻撃は成立しない */
    if (game_night_blocks(g, atk, def)) return false;
    /* 天候: 雨は空↔地上の攻撃そのものを不可にする
     * （ここで弾くと対象一覧・AI・実攻撃のすべてに一括で効く） */
    if (game_weather_atk_pct(g, atk, def) <= 0) return false;
    return true;
}

/* ------------------------------------------------------------------ */
/* 視界（仕様書 5.8）                                                  */
/* ------------------------------------------------------------------ */

static void reveal_around(Game *g, int player, int cx, int cy, int radius)
{
    for (int y = 0; y < g->h; y++) {
        for (int x = 0; x < g->w; x++) {
            if (hex_distance(cx, cy, x, y) <= radius)
                g->visible[player][y][x] = 1;
        }
    }
}

void game_update_vision(Game *g)
{
    if (!g->fog) {
        memset(g->visible, 1, sizeof g->visible);
        return;
    }
    memset(g->visible, 0, sizeof g->visible);
    for (int p = 0; p < MAX_PLAYERS; p++) {
        int bonus = (g->ctrl[p] == CTRL_CPU_HARD) ? 1 : 0; /* HARDのみ視界+1 */
        const CommanderType *co = game_co(g, p);
        /* SCOUT の必殺技発動中はこのターン全マップ視認 */
        if (co && co->power_type == CO_POW_SCOUT && g->co_power_turns[p] > 0) {
            memset(g->visible[p], 1, sizeof g->visible[p]);
            continue;
        }
        for (int i = 0; i < g->n_units; i++) {
            const Unit *u = &g->units[i];
            if (!(u->flags & UF_ALIVE) || (u->flags & UF_LOADED) ||
                u->owner != p) continue;
            int vb = bonus;
            if (co && co->vision_bonus && game_co_affects(g, p, u))
                vb += co->vision_bonus;
            /* 天候による視界低下（下限1は必ず確保する） */
            int vis = unit_type(g, u)->vision + vb + game_weather_vision_mod(g);
            /* 夜は見えなくなるが、夜間ユニットだけは落ちない。
             * この非対称が夜間ユニットを「偵察役」にする。 */
            if (!unit_type(g, u)->night) vis += game_night_vision_mod(g);
            if (vis < 1) vis = 1;
            reveal_around(g, p, u->pos.x, u->pos.y, vis);
        }
        /* 所有建物: 視界2 */
        for (int y = 0; y < g->h; y++)
            for (int x = 0; x < g->w; x++)
                if (g->terrains[g->tiles[y][x].terrain].capturable &&
                    g->tiles[y][x].owner == p)
                    reveal_around(g, p, x, y, 2 + bonus);
    }
}

/* viewer から見てユニット u が見えるか（森・都市の隠蔽と潜水艦を考慮） */
bool game_unit_visible_to(const Game *g, int viewer, const Unit *u)
{
    if (!(u->flags & UF_ALIVE)) return false;
    if (u->flags & UF_LOADED) return false; /* 搭載中は盤上に存在しない */
    if (u->owner == viewer) return true;
    if (!g->fog) {
        /* 索敵OFFでも潜水艦の隠密は維持しない（全て可視） */
        return true;
    }
    if (!g->visible[viewer][u->pos.y][u->pos.x]) return false;

    const TerrainType *t = game_terrain_at(g, u->pos.x, u->pos.y);
    const UnitType *ut = unit_type(g, u);
    bool need_adjacent = t->hide || ut->is_sub;
    if (!need_adjacent) return true;

    /* 隣接する viewer ユニットがあれば視認可。潜水艦は対潜ユニット限定 */
    for (int i = 0; i < g->n_units; i++) {
        const Unit *v = &g->units[i];
        if (!(v->flags & UF_ALIVE) || v->owner != viewer) continue;
        if (hex_distance(v->pos.x, v->pos.y, u->pos.x, u->pos.y) > 1) continue;
        if (ut->is_sub && !unit_type(g, v)->anti_sub) continue;
        return true;
    }
    return false;
}

/* ------------------------------------------------------------------ */
/* コマンド                                                            */
/* ------------------------------------------------------------------ */

int game_spawn_unit(Game *g, int owner, int type, int x, int y, int hp)
{
    /* 空きスロット再利用 */
    int idx = -1;
    for (int i = 0; i < g->n_units; i++) {
        if (!(g->units[i].flags & UF_ALIVE)) { idx = i; break; }
    }
    if (idx < 0) {
        if (g->n_units >= MAX_UNITS) return -1;
        idx = g->n_units++;
    }
    Unit *u = &g->units[idx];
    memset(u, 0, sizeof *u);
    u->type = (uint8_t)type;
    u->owner = (uint8_t)owner;
    u->pos.x = (uint8_t)x;
    u->pos.y = (uint8_t)y;
    u->hp = (int8_t)(hp > 0 ? hp : 10);
    u->fuel = g->types[type].fuel;
    u->ammo = g->types[type].ammo;
    u->flags = UF_ALIVE;
    for (int s = 0; s < MAX_CARGO; s++) u->cargo[s] = -1;
    return idx;
}

/* マップ読込後の安全網: 進入不可地形に置かれたユニットを最寄りの適地へ寄せる。
 * 例）艦船が誤って陸に配置された場合、最寄りの海ヘクスへ自動移動させる。
 * データ（手編集/生成）のミスでユニットが動けなくなる事故を防ぐ。 */
void game_fixup_unit_terrain(Game *g)
{
    for (int i = 0; i < g->n_units; i++) {
        Unit *u = &g->units[i];
        if (!(u->flags & UF_ALIVE) || (u->flags & UF_LOADED)) continue;
        MoveClass mc = (MoveClass)g->types[u->type].mclass;
        Layer L = unit_layer(mc);
        int cx = u->pos.x, cy = u->pos.y;
        /* 立体化: 進入可能地形 かつ 自レイヤーの占有が自分自身なら適正配置。
         * 同一(セル,レイヤー)に別ユニットが重なっている場合も後発を寄せる。 */
        if (g->terrains[g->tiles[cy][cx].terrain].mcost[mc] > 0 &&
            game_unit_at_layer(g, cx, cy, L) == i)
            continue;

        /* BFS で最寄りの「進入可能かつ自レイヤーが空き」ヘクスを探す */
        static uint8_t seen[MAX_MAP_H][MAX_MAP_W];
        static int qx[MAX_MAP_W * MAX_MAP_H], qy[MAX_MAP_W * MAX_MAP_H];
        memset(seen, 0, sizeof seen);
        int head = 0, tail = 0;
        qx[tail] = cx; qy[tail] = cy; tail++;
        seen[cy][cx] = 1;
        while (head < tail) {
            int px = qx[head], py = qy[head]; head++;
            if (g->terrains[g->tiles[py][px].terrain].mcost[mc] > 0) {
                int at = game_unit_at_layer(g, px, py, L);
                if (at < 0 || at == i) { /* 自レイヤーが空き（自分自身は可） */
                    u->pos.x = (uint8_t)px;
                    u->pos.y = (uint8_t)py;
                    break;
                }
            }
            for (int d = 0; d < HEX_DIRS; d++) {
                int nx, ny;
                hex_neighbor(px, py, d, &nx, &ny);
                if (!game_in_bounds(g, nx, ny) || seen[ny][nx]) continue;
                seen[ny][nx] = 1;
                qx[tail] = nx; qy[tail] = ny; tail++;
            }
        }
    }
}

static void clear_capture_by(Game *g, int ui)
{
    /* ユニットが移動/死亡したら占領進行をリセット（仕様書 5.7） */
    for (int y = 0; y < g->h; y++) {
        for (int x = 0; x < g->w; x++) {
            if (g->tiles[y][x].capturer == ui) {
                g->tiles[y][x].capturer = -1;
                g->tiles[y][x].cap_hp = CAPTURE_HP;
            }
        }
    }
}

/* 役割ごとの経験値配点（docs/evolution_spec.md）。
 * 戦えないユニットには戦闘経験値の代わりに、自分の仕事が経験値になる。
 * 占領は歩兵が戦闘でも稼げるので「代わり」ではなく上乗せ。 */
#define EXP_UNLOAD   10   /* 輸送: 1体降ろす */
#define EXP_SUPPLY    1   /* 補給: 物資1消費 */
#define EXP_RECON     1   /* 偵察: 新しい土地を明かした移動 */
#define EXP_CARRIER   2   /* 空母: 艦載機1機を整備 */
#define EXP_CAPTURE  15   /* 占領完了（戦闘ぶんへの上乗せ） */

static void gain_exp(Unit *u, int amount)
{
    int e = u->exp + amount;
    u->exp = (uint8_t)(e > 100 ? 100 : e);
}

void game_move_unit(Game *g, int ui, int x, int y, int fuel_cost)
{
    Unit *u = &g->units[ui];
    if (u->pos.x != x || u->pos.y != y) {
        clear_capture_by(g, ui);
        u->flags |= UF_MOVED;
    }
    u->pos.x = (uint8_t)x;
    u->pos.y = (uint8_t)y;
    u->fuel = (uint8_t)(u->fuel > fuel_cost ? u->fuel - fuel_cost : 0);
    /* 搭載ユニットも一緒に移動 */
    for (int s = 0; s < MAX_CARGO; s++)
        if (u->cargo[s] >= 0) {
            g->units[u->cargo[s]].pos.x = (uint8_t)x;
            g->units[u->cargo[s]].pos.y = (uint8_t)y;
        }

    /* 偵察部隊は「今まで見えなかった土地を明かした移動」で経験値を得る。
     * 戦えない偵察機にとっては唯一の成長手段。visible[] は今見えているかだけを
     * 持つので、往復すると同じ土地で何度も稼げる——現状はそれを許容している
     * （偵察の仕事は見えない場所を見ることなので、遊んでみて問題なら見直す）。 */
    bool was_visible[MAX_MAP_H][MAX_MAP_W];
    const UnitType *mt = unit_type(g, u);
    bool count_recon = mt->recon && g->fog;
    if (count_recon)
        for (int yy = 0; yy < g->h; yy++)
            for (int xx = 0; xx < g->w; xx++)
                was_visible[yy][xx] = g->visible[u->owner][yy][xx] != 0;

    game_update_vision(g);

    if (count_recon) {
        for (int yy = 0; yy < g->h; yy++)
            for (int xx = 0; xx < g->w; xx++)
                if (!was_visible[yy][xx] && g->visible[u->owner][yy][xx]) {
                    gain_exp(u, EXP_RECON);
                    return;                  /* 1回の移動につき1回だけ */
                }
    }
}

static void kill_unit(Game *g, int ui)
{
    Unit *u = &g->units[ui];
    /* 輸送ユニット撃破時、搭載ユニットも喪失（仕様書 5.9） */
    for (int s = 0; s < MAX_CARGO; s++)
        if (u->cargo[s] >= 0) {
            Unit *c = &g->units[u->cargo[s]];
            g->lost_units[c->owner]++;
            c->flags = 0;
            c->hp = 0;
            u->cargo[s] = -1;
        }
    g->lost_units[u->owner]++;
    u->flags = 0;
    u->hp = 0;
    clear_capture_by(g, ui);
}

/* ------------------------------------------------------------------ */
/* 天候                                                                */
/* ------------------------------------------------------------------ */
Weather game_weather(const Game *g)
{
    if (!g->weather_on) return WX_CLEAR;
    return (Weather)(g->weather < WX_COUNT ? g->weather : WX_CLEAR);
}

/* 空 ↔ 空以外 の攻撃だけが悪天候の影響を受ける（空戦どうしは影響なし）。
 * 地上と海上は同じ SURFACE レイヤーなので、対艦爆撃も自然に含まれる。 */
bool game_weather_hits(const Game *g, const Unit *atk, const Unit *def)
{
    if (!atk || !def) return false;
    Layer la = unit_layer(g->types[atk->type].mclass);
    Layer ld = unit_layer(g->types[def->type].mclass);
    bool a_air = (la == LAYER_AIR), d_air = (ld == LAYER_AIR);
    return a_air != d_air;      /* 片方だけが空 */
}

int game_weather_atk_pct(const Game *g, const Unit *atk, const Unit *def)
{
    Weather w = game_weather(g);
    if (w == WX_CLEAR) return 100;
    if (!game_weather_hits(g, atk, def)) return 100;
    return (w == WX_RAIN) ? 0 : 50;   /* 雨=攻撃不可 / 曇=半減 */
}

int game_weather_vision_mod(const Game *g)
{
    switch (game_weather(g)) {
    case WX_CLOUDY: return -1;
    case WX_RAIN:   return -2;
    default:        return 0;
    }
}

int game_weather_move_mod(const Game *g, const Unit *u)
{
    if (game_weather(g) != WX_RAIN) return 0;
    /* 地上部隊のみぬかるみで鈍る（空・海は影響なし） */
    return (move_domain(g->types[u->type].mclass) == 0) ? -1 : 0;
}

/* ラウンド開始時に天候を進める（両陣営で同じ天候・数ターン継続） */
/* 天候を wx_pct の重みで引く。ただし except だけは選ばない。
 * 今の天候と同じものを予報に出してしまうと「晴（次:晴）」のような
 * 意味のない表示になり、切り替わっても見た目が変わらないので除外する。 */
/* --- 昼夜 ---
 * 周期が固定なのでターン数から引ける。状態を持たないので
 * セーブ形式も変わらず、ロード後にズレる心配もない。 */
bool game_is_night(const Game *g)
{
    if (!g->night_on) return false;
    int cyc = DAY_TURNS + NIGHT_TURNS;
    int pos = (g->turn - 1) % cyc;
    if (pos < 0) pos += cyc;
    return pos >= DAY_TURNS;
}

int game_phase_left(const Game *g)
{
    if (!g->night_on) return 0;
    int cyc = DAY_TURNS + NIGHT_TURNS;
    int pos = (g->turn - 1) % cyc;
    if (pos < 0) pos += cyc;
    return (pos < DAY_TURNS) ? (DAY_TURNS - pos) : (cyc - pos);
}

/* 夜の攻撃補正。
 * 夜間ユニットは夜に+50%、通常ユニットは夜に-20%。
 * 夜を「殺し合いの時間」ではなく「補給と回復の時間」にするため。 */
int game_night_atk_pct(const Game *g, const Unit *atk)
{
    if (!game_is_night(g)) return 100;
    return unit_type(g, atk)->night ? 150 : 80;
}

int game_night_vision_mod(const Game *g)
{
    if (!game_is_night(g)) return 0;
    return -2;                       /* 夜間ユニットは呼び出し側で除外する */
}

/* ユニットの活動領域（0=陸 1=空 2=海）。
 * 「陸VS陸・海VS海・空VS空」を見るところはこれを使う。 */
int game_unit_domain(const Game *g, const Unit *u)
{
    return move_domain(g->types[u->type].mclass);
}

/* 夜の領域制限。**この一つだけを見ること**。
 * 以前は unit_can_attack_target だけに書いていて、ダメージ計算側は
 * 見ていなかったため、「起こり得ない攻撃」に非0の見積もりが出ていた。 */
bool game_night_blocks(const Game *g, const Unit *atk, const Unit *def)
{
    if (!atk || !def) return false;
    if (!game_is_night(g)) return false;
    return game_unit_domain(g, atk) != game_unit_domain(g, def);
}

int game_range_max(const Game *g, const UnitType *t)
{
    int r = t->range_max;
    /* 夜は着弾を見て修正できないので間接攻撃の射程が縮む。
     * 直射（range_min<=1）は目視で撃つので影響しない。 */
    if (game_is_night(g) && t->range_min >= 2 && r > t->range_min) r--;
    return r;
}

static uint8_t weather_pick(Game *g, int except)
{
    int total = 0;
    for (int i = 0; i < WX_COUNT; i++)
        if (i != except) total += g->wx_pct[i];
    if (total <= 0) return (uint8_t)(except == WX_CLEAR ? WX_CLOUDY : WX_CLEAR);
    int r = rng_range(&g->rng, 0, total - 1), acc = 0;
    for (int i = 0; i < WX_COUNT; i++) {
        if (i == except) continue;
        acc += g->wx_pct[i];
        if (r < acc) return (uint8_t)i;
    }
    return (uint8_t)(except == WX_CLEAR ? WX_CLOUDY : WX_CLEAR);  /* 保険 */
}

static void weather_advance(Game *g)
{
    if (!g->weather_on) return;
    if (g->weather_left > 0) { g->weather_left--; return; }
    /* 予報していた天候へ切り替え、次の予報を引く。
     * **次の予報は今の天候と別のものにする。** 同じ天候を引き直せてしまうと
     * 「切り替わったのに見た目が変わらない」が頻発し（晴が60%なので特に）、
     * 天候が止まって見える。重みは維持したまま、今の天候だけ除いて引く。 */
    g->weather = g->weather_next;
    g->weather_next = weather_pick(g, g->weather);
    /* この先 2〜4 回は切り替えを見送る。今ラウンドを含めるので
     * 同じ天候が続くのは **3〜5ラウンド**（実測でも 3R/4R/5R のほぼ均等）。
     * 3ラウンド連続は最短ケースであって、珍しい事象ではない。 */
    g->weather_left = (int8_t)rng_range(&g->rng, 2, 4);
}

/* ------------------------------------------------------------------ */
/* 指揮官（CO）                                                        */
/* ------------------------------------------------------------------ */
/* ゲージの溜まる速さ（HPダメージ1あたり）。
 * 大きくすると必殺技が早く回る。commanders.def の power_cost と合わせて調整する。 */
#define CO_GAUGE_PER_DMG_ATK 2   /* 与ダメージ側 */
#define CO_GAUGE_PER_DMG_DEF 1   /* 被ダメージ側（劣勢でも溜まるように加算する） */

const CommanderType *game_co(const Game *g, int p)
{
    if (p < 0 || p >= MAX_PLAYERS) return NULL;
    if (g->n_cos <= 0) return NULL;
    int id = g->co_id[p];
    if (id < 0 || id >= g->n_cos) return NULL;
    return &g->cos[id];
}

bool game_co_affects(const Game *g, int p, const Unit *u)
{
    const CommanderType *c = game_co(g, p);
    if (!c || !u) return false;
    if (u->owner != p) return false;
    if (c->domain == CO_DOM_ALL) return true;
    /* CO_DOM_LAND/AIR/SEA は move_domain() の 0/1/2 に +1 して対応させる */
    return (int)c->domain == move_domain(g->types[u->type].mclass) + 1;
}

int game_co_atk_pct(const Game *g, int p, const Unit *u)
{
    const CommanderType *c = game_co(g, p);
    if (!c) return 0;
    bool mine = game_co_affects(g, p, u);
    int pct = mine ? c->atk_pct : 0;
    /* STRIKE 発動中はこのターンだけ上乗せ。
     * **常時効果と同じくドメインを見る。** 以前は見ていなかったため、
     * 「陸上部隊には補正がない」はずの海専門指揮官の必殺技が
     * 陸上部隊まで強化してしまっていた。 */
    if (mine && g->co_power_turns[p] > 0 && c->power_type == CO_POW_STRIKE)
        pct += c->power_val;
    return pct;
}

int game_co_def_pct(const Game *g, int p, const Unit *u)
{
    const CommanderType *c = game_co(g, p);
    if (!c) return 0;
    bool mine = game_co_affects(g, p, u);
    int pct = mine ? c->def_pct : 0;
    /* SHIELD 発動中はこのターンだけ上乗せ（STRIKE の防御版） */
    if (mine && g->co_power_turns[p] > 0 && c->power_type == CO_POW_SHIELD)
        pct += c->power_val;
    return pct;
}

/* このターンだけの移動力加算（ADVANCE）。
 * 常時効果の move_bonus とは別に乗せる。 */
int game_co_move_bonus(const Game *g, int p, const Unit *u)
{
    const CommanderType *c = game_co(g, p);
    if (!c) return 0;
    int b = game_co_affects(g, p, u) ? c->move_bonus : 0;
    if (g->co_power_turns[p] > 0 && c->power_type == CO_POW_ADVANCE)
        b += c->power_val;
    return b;
}

void game_co_add_gauge(Game *g, int p, int amount)
{
    const CommanderType *c = game_co(g, p);
    if (!c || amount <= 0) return;
    int v = g->co_gauge[p] + amount;
    if (v > c->power_cost) v = c->power_cost;
    g->co_gauge[p] = (int16_t)v;
}

bool game_co_power_ready(const Game *g, int p)
{
    const CommanderType *c = game_co(g, p);
    return c && g->co_gauge[p] >= c->power_cost;
}

bool game_co_activate(Game *g, int p)
{
    const CommanderType *c = game_co(g, p);
    if (!game_co_power_ready(g, p)) return false;
    g->co_gauge[p] = 0;

    switch (c->power_type) {
    case CO_POW_HEAL:
        for (int i = 0; i < g->n_units; i++) {
            Unit *u = &g->units[i];
            if (!(u->flags & UF_ALIVE) || u->owner != p) continue;
            int hp = u->hp + c->power_val;
            u->hp = (int8_t)(hp > 10 ? 10 : hp);
        }
        break;
    case CO_POW_RUSH:
        for (int i = 0; i < g->n_units; i++) {
            Unit *u = &g->units[i];
            if (!(u->flags & UF_ALIVE) || u->owner != p) continue;
            if (u->flags & UF_LOADED) continue;
            u->flags &= (uint8_t)~(UF_DONE | UF_MOVED);
        }
        break;
    case CO_POW_STRIKE:
        g->co_power_turns[p] = 1;      /* このターンのみ攻撃補正 */
        break;
    case CO_POW_FUNDS:
        g->funds[p] += c->power_val;
        break;
    case CO_POW_SCOUT:
        g->co_power_turns[p] = 1;
        g->funds[p] += c->power_val;
        break;
    case CO_POW_SHIELD:
    case CO_POW_ADVANCE:
        g->co_power_turns[p] = 1;      /* このターンのみ防御/移動補正 */
        break;
    case CO_POW_RESUPPLY:
        /* 拠点に戻らなくても全軍が満タンになる。前線を維持するための技 */
        for (int i = 0; i < g->n_units; i++) {
            Unit *u = &g->units[i];
            if (!(u->flags & UF_ALIVE) || u->owner != p) continue;
            const UnitType *ut = unit_type(g, u);
            u->fuel = ut->fuel;
            u->ammo = ut->ammo;
        }
        break;
    case CO_POW_VETERAN:
        /* 経験値を一気に上げて進化を前倒しする */
        for (int i = 0; i < g->n_units; i++) {
            Unit *u = &g->units[i];
            if (!(u->flags & UF_ALIVE) || u->owner != p) continue;
            gain_exp(u, c->power_val);      /* 上限100の丸めを共通化する */
        }
        break;
    case CO_POW_BARRAGE: {
        /* 自軍に隣接している敵全部にダメージ。
         * 目標を選ばせるとUIが増えるので、「接触している所を叩く」形にした。
         * 抜き手ではなく、自ら切り込んでから使う技になる。 */
        for (int i = 0; i < g->n_units; i++) {
            Unit *e = &g->units[i];
            if (!(e->flags & UF_ALIVE) || (e->flags & UF_LOADED)) continue;
            if (e->owner == p) continue;
            /* **領域（陸/海/空）も天候も夜も見ないのは意図的**。
             * 通常の攻撃は夜に領域をまたげないが、必殺技はその例外として
             * 隣接する敵を一括で削る。「夜なのに艦砲が飛行機を削った」は
             * 不具合ではないので、領域判定を足さないこと。 */
            bool adjacent = false;
            for (int j = 0; j < g->n_units && !adjacent; j++) {
                const Unit *m = &g->units[j];
                if (!(m->flags & UF_ALIVE) || (m->flags & UF_LOADED)) continue;
                if (m->owner != p) continue;
                if (hex_distance(m->pos.x, m->pos.y, e->pos.x, e->pos.y) == 1)
                    adjacent = true;
            }
            if (!adjacent) continue;
            int hp = e->hp - c->power_val;
            e->hp = (int8_t)(hp < 1 ? 1 : hp);   /* とどめは刷す。直接の撃破はさせない */
        }
        break;
    }
    default:
        break;
    }
    game_update_vision(g);
    return true;
}

int game_attack(Game *g, int atk_i, int def_i, int *counter_dmg,
                bool *def_killed, bool *atk_killed)
{
    Unit *a = &g->units[atk_i];
    Unit *d = &g->units[def_i];
    const UnitType *at = unit_type(g, a);
    const UnitType *dt = unit_type(g, d);

    if (counter_dmg) *counter_dmg = 0;
    if (def_killed)  *def_killed = false;
    if (atk_killed)  *atk_killed = false;
    int dmg = battle_roll_damage(g, a, d);
    a->ammo--;
    gain_exp(a, 2 * dmg);
    d->hp = (int8_t)(d->hp - dmg);
    /* 指揮官ゲージ: 与ダメージで攻撃側、被ダメージで防御側が溜まる
     * （劣勢側も溜まるので逆転の目が残る）。
     * 溜まる速さの調整はこの倍率と commanders.def の power_cost で行う。 */
    game_co_add_gauge(g, a->owner, dmg * CO_GAUGE_PER_DMG_ATK);
    game_co_add_gauge(g, d->owner, dmg * CO_GAUGE_PER_DMG_DEF);

    if (d->hp <= 0) {
        gain_exp(a, 20);
        kill_unit(g, def_i);
        if (def_killed) *def_killed = true;
    } else {
        /* 反撃: 生存 + 射程内 + 直射（射程2以上の間接攻撃には反撃不可）。
         * 立体化(確定): 同一セル・別レイヤー(距離0)は防御側が直射なら反撃可。 */
        int dist = hex_distance(a->pos.x, a->pos.y, d->pos.x, d->pos.y);
        bool attacker_indirect = at->range_min >= 2;
        bool in_counter_range = (dist == 0)
            ? (dt->range_min <= 1)
            : (dist >= dt->range_min && dist <= game_range_max(g, dt));
        if (!attacker_indirect && in_counter_range &&
            unit_can_attack_target(g, d, a)) {
            int c = battle_roll_damage(g, d, a);
            d->ammo--;
            gain_exp(d, 2 * c);
            a->hp = (int8_t)(a->hp - c);
            if (counter_dmg) *counter_dmg = c;
            if (a->hp <= 0) {
                gain_exp(d, 20);
                kill_unit(g, atk_i);
                if (atk_killed) *atk_killed = true;
            }
        }
    }

    if (a->flags & UF_ALIVE) a->flags |= UF_DONE;
    game_update_vision(g);
    game_check_victory(g);
    return dmg;
}

int game_capture(Game *g, int ui)
{
    Unit *u = &g->units[ui];
    Tile *t = &g->tiles[u->pos.y][u->pos.x];
    const TerrainType *tt = &g->terrains[t->terrain];

    if (!tt->capturable || t->owner == u->owner) return 0;
    if (!unit_type(g, u)->can_capture) return 0;

    if (t->capturer != ui) {
        t->capturer = (int16_t)ui;
        t->cap_hp = CAPTURE_HP;
    }
    int reduce = u->hp;
    u->flags |= UF_DONE;

    if (t->cap_hp <= reduce) {
        t->cap_hp = CAPTURE_HP;
        t->capturer = -1;
        t->owner = (int8_t)u->owner;
        gain_exp(u, EXP_CAPTURE);        /* 拠点を取るのも歩兵の手柄 */
        game_update_vision(g);
        game_check_victory(g);
        return 1;
    }
    t->cap_hp = (uint8_t)(t->cap_hp - reduce);
    return 0;
}

void game_wait_unit(Game *g, int ui)
{
    g->units[ui].flags |= UF_DONE;
}

bool game_type_deployable_at(const Game *g, int x, int y, int type)
{
    const TerrainType *t = game_terrain_at(g, x, y);
    MoveClass mc = (MoveClass)g->types[type].mclass;
    bool cat_ok;
    switch (t->produces) {
    case PROD_LAND: cat_ok = (mc == MC_FOOT || mc == MC_WHEEL || mc == MC_TRACK); break;
    case PROD_AIR:  cat_ok = (mc == MC_AIR); break;
    case PROD_SEA:  cat_ok = (mc == MC_SEA || mc == MC_SUB); break;
    default:        return false;
    }
    if (!cat_ok) return false;
    /* 立体化: 出てくるユニットのレイヤーさえ空いていればよい。
     * 上空を飛んでいる航空機が工場の戦車生産を塞ぐ、といったことが起きないようにする。
     * 港では海面が塞がっていても海中（潜水艦）は出せる、という判定にもなる。 */
    return game_unit_at_layer(g, x, y, unit_layer(mc)) < 0;
}

bool game_type_buildable_at(const Game *g, int x, int y, int type)
{
    /* 「買える」かどうか。進化でのみ入手できる種別はここで弾く。
     * 倉庫から戻すだけの経路は game_type_deployable_at を使うこと
     * （こちらで弾くと、進化した部隊が倉庫から二度と出せなくなる）。 */
    if (g->types[type].no_produce) return false;
    return game_type_deployable_at(g, x, y, type);
}

bool game_can_produce_at(const Game *g, int player, int x, int y)
{
    const Tile *tile = &g->tiles[y][x];
    const TerrainType *t = &g->terrains[tile->terrain];
    if (t->produces == PROD_NONE) return false;
    if (tile->owner != player) return false;
    /* 立体化: 「何か1種類でも出せるか」で判断する。実際にどのレイヤーが空いて
     * いるかは game_type_buildable_at が種別ごとに見るので、ここは入口の判定。 */
    for (int i = 0; i < g->n_types; i++)
        if (game_type_buildable_at(g, x, y, i)) return true;
    return false;
}

int game_produce(Game *g, int x, int y, int type)
{
    int p = g->current;
    if (!game_can_produce_at(g, p, x, y)) return -1;
    /* 有料生産なので buildable（進化専用ユニットは買えない）で見る */
    if (!game_type_buildable_at(g, x, y, type)) return -1;
    if (g->funds[p] < g->types[type].cost) return -1;

    int ui = game_spawn_unit(g, p, type, x, y, 10);
    if (ui < 0) return -1;
    g->funds[p] -= g->types[type].cost;
    g->units[ui].flags |= UF_DONE; /* 生産ターンは行動済み */
    game_update_vision(g);
    return ui;
}

/* 倉庫からの引き出し: 生産と同じ配置条件だが資金不要。exp を引き継ぐ。成功=idx / 失敗=-1 */
int game_deploy_free(Game *g, int x, int y, int type, int exp)
{
    int p = g->current;
    if (!game_can_produce_at(g, p, x, y)) return -1;
    /* 倉庫から戻すだけなので deployable で見る。buildable だと no_produce に
     * 弾かれて、進化した部隊が倉庫から二度と出せなくなる。 */
    if (!game_type_deployable_at(g, x, y, type)) return -1;
    int ui = game_spawn_unit(g, p, type, x, y, 10);
    if (ui < 0) return -1;
    g->units[ui].exp = (uint8_t)exp;
    g->units[ui].flags |= UF_DONE;
    game_update_vision(g);
    return ui;
}

/* ------------------------------------------------------------------ */
/* 補給ユニット（補給車等: 隣接味方の燃料・弾薬を回復）                */
/* ------------------------------------------------------------------ */

/* 補給ユニットは ammo スロットを「補給物資」として使う。
 * 1ユニット補給で物資-1、HP1回復で物資-10（HP回復はコマンド時のみ）。 */
#define SUPPLY_HEAL_COST 10
/* 隣接6マス × 各レイヤー。重なりセルがあると6を超えるので余裕を持たせる */
#define MAX_SUPPLY_TARGETS (HEX_DIRS * LAYER_COUNT)

/* 機動ドメイン: 0=陸(FOOT/WHEEL/TRACK) 1=空(AIR) 2=海(SEA/SUB) */
static int move_domain(int mclass)
{
    if (mclass == MC_AIR) return 1;
    if (mclass == MC_SEA || mclass == MC_SUB) return 2;
    return 0;
}

/* 補給ユニット s が対象 t を補給・回復できるか。
 * 補給車は陸部隊のみ、補給機は空部隊のみ（自分と同じドメインに限る）。 */
static bool supply_domain_ok(const Game *g, const Unit *s, const Unit *t)
{
    return move_domain(g->types[s->type].mclass)
        == move_domain(g->types[t->type].mclass);
}

/* 補給ユニットへの補給は燃料のみ（物資=ammo は施設でしか補充しない） */
static bool supply_needs(const Game *g, const Unit *u)
{
    const UnitType *t = &g->types[u->type];
    if (u->fuel < t->fuel) return true;
    if (!t->supply && u->ammo < t->ammo) return true; /* 物資は対象外 */
    return false;
}

/* self_i の隣接で「補給が必要かつ同ドメインの味方」を列挙する内部処理。
 * 補給ユニットどうしも同ドメインなら燃料は補給し合える（物資と HP は対象外）。
 * 立体化: 隣接セルは全レイヤーを見る。1体だけ見ていると、味方機が上空にいる
 * 地上部隊がドメイン違いで弾かれて永久に補給されない。 */
static int supply_targets_at(const Game *g, int self_i, int *out, int max_out)
{
    const Unit *s = &g->units[self_i];
    int n = 0;
    for (int d = 0; d < HEX_DIRS && n < max_out; d++) {
        int nx, ny;
        hex_neighbor(s->pos.x, s->pos.y, d, &nx, &ny);
        if (!game_in_bounds(g, nx, ny)) continue;
        int cell[LAYER_COUNT];
        game_units_at(g, nx, ny, cell);
        for (int L = 0; L < LAYER_COUNT && n < max_out; L++) {
            int i = cell[L];
            if (i < 0 || i == self_i) continue;
            const Unit *u = &g->units[i];
            if (u->owner != s->owner) continue;
            if (!supply_domain_ok(g, s, u)) continue;   /* 陸⇔空 は補給しない */
            if (supply_needs(g, u))
                out[n++] = i;
        }
    }
    return n;
}

bool game_can_supply(const Game *g, int ui)
{
    const Unit *u = &g->units[ui];
    if (!g->types[u->type].supply) return false;
    if (u->ammo == 0) return false;              /* 補給物資切れ */
    int tmp[MAX_SUPPLY_TARGETS];
    return supply_targets_at(g, ui, tmp, MAX_SUPPLY_TARGETS) > 0;
}

/* 隣接味方を補給。1ユニットにつき物資-1、物資が尽きたら打ち切り。
 * 補給対象が補給ユニットのときは燃料のみ回復（物資=ammo は補充しない）。
 * 戻り値=補給できたユニット数。 */
int game_supply_adjacent(Game *g, int ui)
{
    Unit *u = &g->units[ui];
    if (!g->types[u->type].supply) return 0;
    int tg[MAX_SUPPLY_TARGETS];
    int n = supply_targets_at(g, ui, tg, MAX_SUPPLY_TARGETS);
    int done = 0;
    for (int k = 0; k < n; k++) {
        if (u->ammo == 0) break;                 /* 物資切れ */
        Unit *v = &g->units[tg[k]];
        const UnitType *t = &g->types[v->type];
        v->fuel = t->fuel;
        if (!t->supply) v->ammo = t->ammo;       /* 補給ユニットの物資は補充しない */
        u->ammo--;                               /* 物資-1 */
        gain_exp(u, EXP_SUPPLY);
        done++;
    }
    u->flags |= UF_DONE;
    return done;
}

/* 隣接に「HP回復可能な味方（補給車以外）」がいるか（物資10以上必要） */
bool game_can_heal(const Game *g, int ui)
{
    const Unit *u = &g->units[ui];
    if (!g->types[u->type].supply) return false;
    if (u->ammo < SUPPLY_HEAL_COST) return false;
    for (int d = 0; d < HEX_DIRS; d++) {
        int nx, ny;
        hex_neighbor(u->pos.x, u->pos.y, d, &nx, &ny);
        if (!game_in_bounds(g, nx, ny)) continue;
        int cell[LAYER_COUNT];
        game_units_at(g, nx, ny, cell);            /* 立体化: 全レイヤーを見る */
        for (int L = 0; L < LAYER_COUNT; L++) {
            int i = cell[L];
            if (i < 0 || i == ui) continue;
            const Unit *v = &g->units[i];
            if (v->owner != u->owner) continue;
            if (g->types[v->type].supply) continue;
            if (!supply_domain_ok(g, u, v)) continue;   /* 同ドメインのみ回復 */
            if (v->hp < 10) return true;
        }
    }
    return false;
}

/* 隣接味方（補給車以外）で最も傷んだ1体を1HPだけ回復。物資10を消費。
 * 1回のコマンドで回復するのはHP1のみ。コマンド選択時のみ（自動回復なし）。
 * 戻り値=回復したHP（0か1）。 */
int game_supply_heal(Game *g, int ui)
{
    Unit *u = &g->units[ui];
    if (!g->types[u->type].supply) return 0;
    u->flags |= UF_DONE;
    if (u->ammo < SUPPLY_HEAL_COST) return 0;

    int best = -1, best_hp = 10;
    for (int d = 0; d < HEX_DIRS; d++) {
        int nx, ny;
        hex_neighbor(u->pos.x, u->pos.y, d, &nx, &ny);
        if (!game_in_bounds(g, nx, ny)) continue;
        int cell[LAYER_COUNT];
        game_units_at(g, nx, ny, cell);            /* 立体化: 全レイヤーを見る */
        for (int L = 0; L < LAYER_COUNT; L++) {
            int i = cell[L];
            if (i < 0 || i == ui) continue;
            Unit *v = &g->units[i];
            if (v->owner != u->owner) continue;
            if (g->types[v->type].supply) continue;
            if (!supply_domain_ok(g, u, v)) continue;   /* 同ドメインのみ回復 */
            if (v->hp < best_hp) { best_hp = v->hp; best = i; }
        }
    }
    if (best < 0) return 0;                       /* 回復対象なし */
    g->units[best].hp++;                          /* 1回につきHP1のみ */
    u->ammo -= SUPPLY_HEAL_COST;
    gain_exp(u, EXP_SUPPLY * SUPPLY_HEAL_COST);   /* 消費した物資ぶん */
    return 1;
}

/* ------------------------------------------------------------------ */
/* 輸送（仕様書 5.9）                                                  */
/* ------------------------------------------------------------------ */

int game_first_cargo(const Game *g, int transport)
{
    const Unit *t = &g->units[transport];
    for (int s = 0; s < MAX_CARGO; s++)
        if (t->cargo[s] >= 0) return s;
    (void)g;
    return -1;
}

/* ------------------------------------------------------------------ */
/* 進化（docs/evolution_spec.md）                                      */
/* ------------------------------------------------------------------ */
/* 進化に要る資金は元ユニットの価格の何倍か。
 * キャンペーン終盤は資金が余りがちなので、その使い道にもなっている。 */
#define EVOLVE_COST_MUL 2

int game_evolve_cost(const Game *g, int ui)
{
    if (ui < 0 || ui >= g->n_units) return 0;
    const Unit *u = &g->units[ui];
    const UnitType *t = unit_type(g, u);
    if (!t->evolve_to[0]) return 0;
    return t->cost * EVOLVE_COST_MUL;
}

int game_evolve_target(const Game *g, int ui)
{
    if (ui < 0 || ui >= g->n_units) return -1;
    const Unit *u = &g->units[ui];
    if (!(u->flags & UF_ALIVE) || (u->flags & UF_LOADED)) return -1;
    if (u->exp < 100) return -1;                  /* rank5 になってから */
    const UnitType *t = unit_type(g, u);
    if (!t->evolve_to[0]) return -1;
    /* 輸送中のユニットを抱えたままの進化は不可（積荷の扱いが曖昧になる） */
    for (int s = 0; s < MAX_CARGO; s++)
        if (u->cargo[s] >= 0) return -1;

    /* 自軍の補給拠点の上でのみ。terrain.def の supplies をそのまま使うので、
     * 陸は街/工場/首都、空は飛行場、海は港（と潜水艦の港）になる。 */
    const Tile *tile = &g->tiles[u->pos.y][u->pos.x];
    const TerrainType *terr = &g->terrains[tile->terrain];
    if (tile->owner != (int8_t)u->owner) return -1;
    if (!(terr->supplies & (1u << t->mclass))) return -1;

    if (g->funds[u->owner] < game_evolve_cost(g, ui)) return -1;   /* 資金不足 */

    for (int i = 0; i < g->n_types; i++)
        if (!strcmp(g->types[i].id, t->evolve_to)) return i;
    return -1;                                     /* 進化先が見つからない */
}

bool game_can_evolve(const Game *g, int ui)
{
    return game_evolve_target(g, ui) >= 0;
}

int game_evolve_unit(Game *g, int ui)
{
    int nt = game_evolve_target(g, ui);
    if (nt < 0) return -1;
    Unit *u = &g->units[ui];
    const UnitType *t = &g->types[nt];

    g->funds[u->owner] -= game_evolve_cost(g, ui);   /* 型を変える前に引く */
    u->type = (uint8_t)nt;
    u->exp = 0;                    /* 熟練度は振り出しに戻る＝これが進化の代償 */
    /* HP は据え置き（満タンにすると前線での回復手段になってしまう）。
     * 燃料・弾薬は新しい上限で満タン（上位種は上限が上がることが多く、
     * 比率のままだと進化した瞬間に息切れするため）。 */
    u->fuel = t->fuel;
    u->ammo = t->ammo;
    if (u->hp > 10) u->hp = 10;
    u->flags |= UF_DONE;           /* 移動＋進化＋攻撃を1手番でやらせない */
    game_update_vision(g);         /* 視界が変わる進化先があるため */
    return 0;
}

/* ------------------------------------------------------------------ */
/* 合流                                                                */
/* ------------------------------------------------------------------ */
bool game_can_join(const Game *g, int mover, int target)
{
    if (mover == target || mover < 0 || target < 0) return false;
    const Unit *m = &g->units[mover];
    const Unit *t = &g->units[target];
    if (!(m->flags & UF_ALIVE) || !(t->flags & UF_ALIVE)) return false;
    if ((m->flags & UF_LOADED) || (t->flags & UF_LOADED)) return false;
    if (m->owner != t->owner) return false;
    if (m->type != t->type) return false;
    /* 満タンの部隊に重ねても得がない（誤操作で部隊数だけ減るのを防ぐ） */
    if (t->hp >= 10) return false;
    /* どちらかが輸送中なら不可（搭載ユニットを巻き添えで消さない） */
    for (int s = 0; s < MAX_CARGO; s++)
        if (m->cargo[s] >= 0 || t->cargo[s] >= 0) return false;
    return true;
}

int game_join_units(Game *g, int mover, int target)
{
    if (!game_can_join(g, mover, target)) return 0;
    Unit *m = &g->units[mover];
    Unit *t = &g->units[target];
    const UnitType *ut = &g->types[t->type];

    int hp = m->hp + t->hp;
    int over = hp > 10 ? hp - 10 : 0;
    t->hp = (int8_t)(hp > 10 ? 10 : hp);

    int fuel = (int)m->fuel + (int)t->fuel;
    t->fuel = (uint8_t)(fuel > ut->fuel ? ut->fuel : fuel);
    int ammo = (int)m->ammo + (int)t->ammo;
    t->ammo = (uint8_t)(ammo > ut->ammo ? ut->ammo : ammo);
    if (m->exp > t->exp) t->exp = m->exp;   /* 熟練度は高い方を引き継ぐ */

    /* 合流先はこのターン行動済みにする（2部隊分は動けない） */
    t->flags |= UF_DONE;

    /* mover は盤上から消えるが撃破ではないので lost_units には数えない
     * （作戦評価の「戦力温存」が合流のたびに下がってしまうため） */
    clear_capture_by(g, mover);
    m->flags = 0;
    m->hp = 0;

    /* HP上限を超えた分はユニット価格に応じて払い戻す */
    int refund = over > 0 ? ut->cost * over / 10 : 0;
    if (refund > 0) g->funds[t->owner] += refund;
    return refund;
}

bool game_can_board(const Game *g, int passenger, int transport)
{
    const Unit *p = &g->units[passenger];
    const Unit *t = &g->units[transport];
    if (!(t->flags & UF_ALIVE) || (t->flags & UF_LOADED)) return false;
    if (p->owner != t->owner) return false;
    const UnitType *tt = &g->types[t->type];
    const UnitType *pt = &g->types[p->type];
    if (tt->capacity == 0) return false;
    /* 空きスロット */
    int used = 0;
    for (int s = 0; s < MAX_CARGO; s++)
        if (t->cargo[s] >= 0) used++;
    if (used >= tt->capacity) return false;
    /* 搭載中の輸送ユニットは載せない（入れ子禁止） */
    for (int s = 0; s < MAX_CARGO; s++)
        if (p->cargo[s] >= 0) return false;
    /* transport_by リストに輸送手段IDが含まれるか。
     * 進化した輸送手段（CARRIER_V など）は ID が変わるうえ、transport_by には
     * 進化前のID しか書かれていない。しかも transport_by は4件までで歩兵は既に
     * 使い切っているため、進化後のIDを書き足すこともできない。
     * そこで**進化前のIDでも一致する**ようにして、大型空母に艦載機が乗らない、
     * 装甲兵員輸送車に歩兵が乗らない、といった事故を防ぐ。 */
    const char *base = NULL;
    for (int i = 0; i < g->n_types; i++)
        if (g->types[i].evolve_to[0] && !strcmp(g->types[i].evolve_to, tt->id)) {
            base = g->types[i].id;
            break;
        }
    for (int i = 0; i < pt->n_transport_by; i++) {
        if (!strcmp(pt->transport_by[i], tt->id)) return true;
        if (base && !strcmp(pt->transport_by[i], base)) return true;
    }
    return false;
}

void game_load_unit(Game *g, int passenger, int transport)
{
    Unit *p = &g->units[passenger];
    Unit *t = &g->units[transport];
    for (int s = 0; s < MAX_CARGO; s++) {
        if (t->cargo[s] < 0) {
            t->cargo[s] = (int16_t)passenger;
            break;
        }
    }
    p->flags |= UF_LOADED | UF_DONE;
    p->pos = t->pos;
    clear_capture_by(g, passenger);
    game_update_vision(g);
}

bool game_can_unload_to(const Game *g, int transport, int x, int y)
{
    const Unit *t = &g->units[transport];
    int slot = game_first_cargo(g, transport);
    if (slot < 0) return false;
    if (!game_in_bounds(g, x, y)) return false;
    /* 隣接判定は呼び出し側（UI/AI）が近傍列挙で保証する */
    const Unit *p = &g->units[t->cargo[slot]];
    MoveClass mc = (MoveClass)g->types[p->type].mclass;
    if (g->terrains[g->tiles[y][x].terrain].mcost[mc] <= 0) return false;
    /* 立体化: 降りる側のレイヤーが空いていればよい
     * （地上が空いているのに上空の航空機で塞がれる、という誤判定を防ぐ） */
    if (game_unit_at_layer(g, x, y, unit_layer(mc)) >= 0) return false;
    return true;
}

int game_unload_unit(Game *g, int transport, int x, int y)
{
    if (!game_can_unload_to(g, transport, x, y)) return -1;
    Unit *t = &g->units[transport];
    int slot = game_first_cargo(g, transport);
    int pi = t->cargo[slot];
    Unit *p = &g->units[pi];
    t->cargo[slot] = -1;
    p->flags &= (uint8_t)~UF_LOADED;
    p->flags |= UF_DONE;
    p->pos.x = (uint8_t)x;
    p->pos.y = (uint8_t)y;
    t->flags |= UF_DONE;
    gain_exp(t, EXP_UNLOAD);      /* 部隊を送り届けるのが輸送の仕事 */
    game_update_vision(g);
    return 0;
}

/* ------------------------------------------------------------------ */
/* ターン処理（仕様書 5.1）                                            */
/* ------------------------------------------------------------------ */

static int income_for(const Game *g, int player)
{
    int sum = 0;
    for (int y = 0; y < g->h; y++)
        for (int x = 0; x < g->w; x++)
            if (g->tiles[y][x].owner == player)
                sum += g->terrains[g->tiles[y][x].terrain].income;
    sum = sum * g->income_scale / 100;
    /* 難易度補正（仕様書 7.2） */
    if (g->ctrl[player] == CTRL_CPU_EASY) sum = sum * 80 / 100;
    if (g->ctrl[player] == CTRL_CPU_HARD) sum = sum * 120 / 100;
    /* 指揮官の収入補正 */
    {
        const CommanderType *c = game_co(g, player);
        if (c && c->income_pct) sum = sum * (100 + c->income_pct) / 100;
    }
    return sum;
}

/* 収入 + 補給 + 行動フラグリセット */
static void begin_player_turn(Game *g)
{
    int p = g->current;
    g->funds[p] += income_for(g, p);

    for (int i = 0; i < g->n_units; i++) {
        Unit *u = &g->units[i];
        if (!(u->flags & UF_ALIVE) || u->owner != p) continue;
        if (u->flags & UF_LOADED) continue; /* 搭載中は行動不可のまま */
        u->flags &= (uint8_t)~(UF_DONE | UF_MOVED);

        const UnitType *ut = unit_type(g, u);
        const Tile *tile = &g->tiles[u->pos.y][u->pos.x];
        const TerrainType *t = &g->terrains[tile->terrain];

        /* 補給: 自軍施設上で燃料・弾薬全回復 + 修理HP+2（コスト有: 仕様書 5.6） */
        if (tile->owner == p && (t->supplies & (1u << ut->mclass))) {
            u->fuel = ut->fuel;
            u->ammo = ut->ammo;
            if (u->hp < 10) {
                int heal = 10 - u->hp < 2 ? 10 - u->hp : 2;
                int cost = ut->cost * heal / 10 * 20 / 100;
                if (g->funds[p] >= cost) {
                    g->funds[p] -= cost;
                    u->hp = (int8_t)(u->hp + heal);
                }
            }
        }

        /* 空母の搭載ユニット（航空機）を空港と同様に補給・修理 */
        if (ut->resupply_cargo) {
            for (int s = 0; s < MAX_CARGO; s++) {
                if (u->cargo[s] < 0) continue;
                Unit *c = &g->units[u->cargo[s]];
                const UnitType *ct = unit_type(g, c);
                c->fuel = ct->fuel;
                c->ammo = ct->ammo;
                if (c->hp < 10) {
                    int heal = 10 - c->hp < 2 ? 10 - c->hp : 2;
                    int cost = ct->cost * heal / 10 * 20 / 100;
                    if (g->funds[p] >= cost) {
                        g->funds[p] -= cost;
                        c->hp = (int8_t)(c->hp + heal);
                        gain_exp(u, EXP_CARRIER);   /* 整備も空母の手柄 */
                    }
                }
            }
        }
    }

    /* 補給ユニット: 隣接味方の燃料・弾薬を自動回復（補給フェイズ）。
     * 物資を1ユニットにつき-1消費する。HP回復は自動では行わない。
     * ※施設上の物資補充は上のループで済んでいるので、その後に消費する。 */
    for (int i = 0; i < g->n_units; i++) {
        Unit *u = &g->units[i];
        if (!(u->flags & UF_ALIVE) || (u->flags & UF_LOADED) ||
            u->owner != p) continue;
        if (!g->types[u->type].supply) continue;
        int tg[MAX_SUPPLY_TARGETS];
        int n = supply_targets_at(g, i, tg, MAX_SUPPLY_TARGETS);
        for (int k = 0; k < n; k++) {
            if (u->ammo == 0) break;             /* 物資切れ */
            Unit *v = &g->units[tg[k]];
            const UnitType *t = &g->types[v->type];
            v->fuel = t->fuel;
            if (!t->supply) v->ammo = t->ammo;   /* 補給ユニットの物資は補充しない */
            u->ammo--;
        }
    }
    game_update_vision(g);
}

/* 手番終了時の燃料切れ判定（仕様書 5.1-4） */
static void end_player_turn(Game *g)
{
    int p = g->current;
    /* 「このターンだけ」の必殺技効果（STRIKE/SCOUT）を手番終了で切る */
    if (g->co_power_turns[p] > 0) {
        g->co_power_turns[p]--;
        if (g->co_power_turns[p] == 0) game_update_vision(g);
    }
    for (int i = 0; i < g->n_units; i++) {
        Unit *u = &g->units[i];
        if (!(u->flags & UF_ALIVE) || (u->flags & UF_LOADED) ||
            u->owner != p) continue;
        const UnitType *ut = unit_type(g, u);
        /* 維持コスト: 自分を補給できる施設の上にいる間は消費しない
         * （既定では航空機だけ upkeep=4。飛竜等はデータで 0 にできる） */
        if (ut->upkeep > 0) {
            const Tile *tile = &g->tiles[u->pos.y][u->pos.x];
            const TerrainType *t = &g->terrains[tile->terrain];
            bool supplied = tile->owner == p && (t->supplies & (1u << ut->mclass));
            if (!supplied)
                u->fuel = (uint8_t)(u->fuel > ut->upkeep ? u->fuel - ut->upkeep : 0);
        }
        /* 燃料切れの扱い（既定では航空機=墜落 / 艦船=漂流 / それ以外=なし） */
        if (u->fuel == 0) {
            if (ut->no_fuel == NOFUEL_DIE) {
                kill_unit(g, i);
            } else if (ut->no_fuel == NOFUEL_DAMAGE) {
                u->hp = (int8_t)(u->hp - 1);
                if (u->hp <= 0) kill_unit(g, i);
            }
        }
    }
}

void game_start(Game *g, uint32_t seed)
{
    rng_seed(&g->rng, seed);
    g->winner = WINNER_NONE;
    g->turn = 1;
    g->current = 0;
    if (g->income_scale <= 0) g->income_scale = 100;
    /* 天候: 確率未設定なら既定 60/30/10。初手は必ず晴から始めて、
     * 2〜4ラウンド後に予報の天候へ変わる（開幕から雨だと理不尽なため） */
    if (g->wx_pct[WX_CLEAR] + g->wx_pct[WX_CLOUDY] + g->wx_pct[WX_RAIN] <= 0) {
        g->wx_pct[WX_CLEAR] = 60;
        g->wx_pct[WX_CLOUDY] = 30;
        g->wx_pct[WX_RAIN] = 10;
    }
    g->weather = WX_CLEAR;
    g->weather_left = (int8_t)(g->weather_on ? rng_range(&g->rng, 2, 4) : 0);
    /* 最初の予報も晴以外から引く（開幕から「晴（次:晴）」では予報の意味がない） */
    g->weather_next = weather_pick(g, WX_CLEAR);
    for (int y = 0; y < g->h; y++)
        for (int x = 0; x < g->w; x++) {
            g->tiles[y][x].cap_hp = CAPTURE_HP;
            g->tiles[y][x].capturer = -1;
        }

    game_recompute_in_play(g);
    begin_player_turn(g);
}

void game_end_turn(Game *g)
{
    if (g->winner != WINNER_NONE) return;
    end_player_turn(g);
    game_check_victory(g);
    if (g->winner != WINNER_NONE) return;

    /* 次の手番へ。**参加していない陣営と脱落した陣営は飛ばす**。
     * MAX_PLAYERS を増やしたので飛ばさないと2陣営マップで
     * 空の手番が3回挑さまる。ターン数は0を通過したときに進めるので、
     * 陣営0が脱落してもカウンタは止まらない。 */
    for (int step = 0; step < MAX_PLAYERS; step++) {
        g->current = (g->current + 1) % MAX_PLAYERS;
        if (g->current == 0) {
            g->turn++;
            /* 天候はラウンド単位で進める（全陣営が同じ天候になるように） */
            weather_advance(g);
            if (g->turn_limit > 0 && g->turn > g->turn_limit) {
                g->winner = g->timeout_winner;
                return;
            }
        }
        if (game_player_in_play(g, g->current) &&
            !game_player_defeated(g, g->current)) break;
    }
    begin_player_turn(g);
    game_check_victory(g);
}

/* ------------------------------------------------------------------ */
/* 勝利判定（仕様書 5.10: 首都陥落 / 全滅）                            */
/* ------------------------------------------------------------------ */

int game_count_buildings(const Game *g, int player)
{
    int n = 0;
    for (int y = 0; y < g->h; y++)
        for (int x = 0; x < g->w; x++)
            if (g->terrains[g->tiles[y][x].terrain].capturable &&
                g->tiles[y][x].owner == player)
                n++;
    return n;
}

/* ------------------------------------------------------------------ */
/* マップイベント                                                      */
/* ------------------------------------------------------------------ */
static bool ev_cond_met(const Game *g, const MapEvent *e)
{
    switch (e->cond) {
    case EV_C_TURN: return g->turn >= e->c1;
    case EV_C_BLD:
        if (e->c1 < 0 || e->c1 >= MAX_PLAYERS) return false;
        return game_count_buildings(g, e->c1) >= e->c2;
    case EV_C_LOSS:
        if (e->c1 < 0 || e->c1 >= MAX_PLAYERS) return false;
        return g->lost_units[e->c1] >= e->c2;
    case EV_C_AREA:
        for (int i = 0; i < g->n_units; i++) {
            const Unit *u = &g->units[i];
            if (!(u->flags & UF_ALIVE) || (u->flags & UF_LOADED)) continue;
            if (u->owner != e->c1) continue;
            if (hex_distance(u->pos.x, u->pos.y, e->c2, e->c3) <= e->c4)
                return true;
        }
        return false;
    case EV_C_CAPTURED:
        if (!game_in_bounds(g, e->c2, e->c3)) return false;
        return g->tiles[e->c3][e->c2].owner == (int8_t)e->c1;
    case EV_C_WEATHER:
        return g->weather_on && (int)game_weather(g) == e->c1;
    default: return false;
    }
}

/* (x,y) から最寄りの進入可能かつ空きレイヤーへ1体出す（増援の湧き出し用） */
static int ev_spawn_near(Game *g, int owner, int type, int ox, int oy)
{
    if (type < 0 || type >= g->n_types) return -1;
    if (!game_in_bounds(g, ox, oy)) return -1;
    MoveClass mc = (MoveClass)g->types[type].mclass;
    Layer L = unit_layer(mc);

    static uint8_t seen[MAX_MAP_H][MAX_MAP_W];
    static int qx[MAX_MAP_W * MAX_MAP_H], qy[MAX_MAP_W * MAX_MAP_H];
    memset(seen, 0, sizeof seen);
    int head = 0, tail = 0;
    qx[tail] = ox; qy[tail] = oy; tail++;
    seen[oy][ox] = 1;
    while (head < tail) {
        int cx = qx[head], cy = qy[head]; head++;
        if (g->terrains[g->tiles[cy][cx].terrain].mcost[mc] > 0 &&
            game_unit_at_layer(g, cx, cy, L) < 0)
            return game_spawn_unit(g, owner, type, cx, cy, 10);
        for (int d = 0; d < HEX_DIRS; d++) {
            int nx, ny;
            hex_neighbor(cx, cy, d, &nx, &ny);
            if (!game_in_bounds(g, nx, ny) || seen[ny][nx]) continue;
            seen[ny][nx] = 1;
            qx[tail] = nx; qy[tail] = ny; tail++;
        }
    }
    return -1;
}

static void ev_run(Game *g, const MapEvent *e)
{
    switch (e->act) {
    case EV_A_SPAWN: {
        if (e->a1 < 0 || e->a1 >= MAX_PLAYERS) break;
        int n = e->a5 > 0 ? e->a5 : 1;
        for (int i = 0; i < n; i++)
            ev_spawn_near(g, e->a1, e->a2, e->a3, e->a4);
        break;
    }
    case EV_A_FUNDS:
        if (e->a1 >= 0 && e->a1 < MAX_PLAYERS) g->funds[e->a1] += e->a2;
        break;
    case EV_A_WEATHER:
        if (!g->weather_on) break;
        if (e->a1 < 0 || e->a1 >= WX_COUNT) break;
        g->weather = (uint8_t)e->a1;
        g->weather_left = (int8_t)(e->a2 > 0 ? e->a2 : 3);
        /* この天候が明けたあとの予報も引き直す */
        g->weather_next = weather_pick(g, g->weather);
        break;
    case EV_A_TERRAIN: {
        if (!game_in_bounds(g, e->a1, e->a2)) break;
        int nt = -1;
        for (int i = 0; i < g->n_terrains; i++)
            if (g->terrains[i].chr == (char)e->a3) { nt = i; break; }
        if (nt < 0) break;                       /* 未定義の文字は無視 */
        Tile *t = &g->tiles[e->a2][e->a1];
        t->terrain = (uint8_t)nt;
        /* 拠点でなくなったら所有と占領進捗を消す。
         * 残しておくと「平地なのに所有者がいて収入が入る」になる。 */
        if (!g->terrains[nt].capturable) {
            t->owner = -1;
            t->capturer = -1;
            t->cap_hp = CAPTURE_HP;
        }
        break;
    }
    case EV_A_COPOWER:
        if (e->a1 < 0 || e->a1 >= MAX_PLAYERS) break;
        if (game_co(g, e->a1)) {
            /* ゲージを満タンにしてから発動させる（通常の経路を通す） */
            g->co_gauge[e->a1] = game_co(g, e->a1)->power_cost;
            game_co_activate(g, e->a1);
        }
        break;
    case EV_A_HP:
        if (e->a1 < 0 || e->a1 >= MAX_PLAYERS) break;
        for (int i = 0; i < g->n_units; i++) {
            Unit *u = &g->units[i];
            if (!(u->flags & UF_ALIVE) || u->owner != e->a1) continue;
            int hp = u->hp + e->a2;
            if (hp > 10) hp = 10;
            if (hp < 1) hp = 1;                  /* とどめは刷さない */
            u->hp = (int8_t)hp;
        }
        break;
    case EV_A_MSG:
    default:
        break;
    }
}

int game_check_events(Game *g, const char *msgs[], int max)
{
    int fired = 0;
    for (int i = 0; i < g->n_events && i < MAX_EVENTS; i++) {
        if (g->events_fired & (1u << i)) continue;      /* 1回だけ */
        const MapEvent *e = &g->events[i];
        if (e->cond == EV_C_NONE || !ev_cond_met(g, e)) continue;
        g->events_fired |= 1u << i;
        ev_run(g, e);
        if (msgs && fired < max && e->msg[0]) msgs[fired] = e->msg;
        fired++;
    }
    if (fired > 0) game_update_vision(g);
    return fired;
}

/* 参加陣営を算出する。ユニットか建物を一つでも持っていれば参加。
 * 古いセーブを読んだときにも使う（v9以前はこの情報を持たない）。 */
void game_recompute_in_play(Game *g)
{
    memset(g->in_play, 0, sizeof g->in_play);
    for (int i = 0; i < g->n_units; i++) {
        const Unit *u = &g->units[i];
        if ((u->flags & UF_ALIVE) && u->owner >= 0 && u->owner < MAX_PLAYERS)
            g->in_play[u->owner] = 1;
    }
    for (int y = 0; y < g->h; y++)
        for (int x = 0; x < g->w; x++) {
            int o = g->tiles[y][x].owner;
            if (o >= 0 && o < MAX_PLAYERS &&
                g->terrains[g->tiles[y][x].terrain].capturable)
                g->in_play[o] = 1;
        }
}

bool game_player_in_play(const Game *g, int p)
{
    if (p < 0 || p >= MAX_PLAYERS) return false;
    return g->in_play[p] != 0;
}

/* その陣営の生存ユニットが1つでもあるか */
static bool player_has_units(const Game *g, int p)
{
    for (int i = 0; i < g->n_units; i++)
        if ((g->units[i].flags & UF_ALIVE) && g->units[i].owner == p) return true;
    return false;
}

/* 全滅した陣営の拠点を中立に戻す。
 * 部隊が居ないのに収入だけ入り続け、他陣営が占領し直す必要もある
 * 状態を残さないため。中立に戻せば誰でも採りに行ける。 */
static void neutralize_buildings(Game *g, int p)
{
    for (int y = 0; y < g->h; y++)
        for (int x = 0; x < g->w; x++) {
            Tile *t = &g->tiles[y][x];
            if (t->owner != (int8_t)p) continue;
            t->owner = -1;
            t->capturer = -1;
            t->cap_hp = CAPTURE_HP;
        }
}

bool game_player_defeated(const Game *g, int p)
{
    if (!game_player_in_play(g, p)) return true;
    /* 全滅したらその時点で敗北確定（拠点が残っていても復帰させない） */
    if (!player_has_units(g, p)) return true;

    /* 首都: 自陣営所有の首都が1つもなければ敗北（マップに首都がある場合） */
    bool any_hq = false;
    int mine = 0;
    for (int y = 0; y < g->h; y++)
        for (int x = 0; x < g->w; x++)
            if (g->terrains[g->tiles[y][x].terrain].is_hq) {
                any_hq = true;
                if (g->tiles[y][x].owner == (int8_t)p) mine++;
            }
    if (any_hq && mine == 0) return true;
    return false;
}

void game_check_victory(Game *g)
{
    if (g->winner != WINNER_NONE) return;

    /* 拠点n個確保（仕様書 5.10） */
    if (g->objective_count > 0 &&
        game_count_buildings(g, g->objective_player) >= g->objective_count) {
        g->winner = g->objective_player;
        return;
    }

    /* 全滅した陣営の拠点を中立に戻す。残りの陣営が採りに行けるようにする。 */
    for (int p = 0; p < MAX_PLAYERS; p++)
        if (game_player_in_play(g, p) && !player_has_units(g, p))
            neutralize_buildings(g, p);

    /* 参加陣営のうち、敗北していないものが1つだけになったらその勝ち。
     * 参加していない陣営（ユニットも建物も持たずに始まった）は数えない。 */
    int remaining = 0, last = -1;
    for (int p = 0; p < MAX_PLAYERS; p++) {
        if (!game_player_in_play(g, p)) continue;
        if (game_player_defeated(g, p)) continue;
        remaining++;
        last = p;
    }
    if (remaining == 1)      g->winner = last;
    else if (remaining == 0) g->winner = WINNER_DRAW;   /* 相打ちの共倒れ */
}
