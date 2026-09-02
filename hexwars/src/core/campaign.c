/* campaign.c - キャンペーン進行（.cpn 読込・持越し・分岐） */
#include "campaign.h"
#include "hex.h"
#include "../data/parser.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* 前後の空白を落とす（'|' 区切りの各項目を読みやすく書けるように） */
static char *trim_str(char *s)
{
    while (*s == ' ' || *s == '\t') s++;
    char *e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' ||
                     e[-1] == '\r' || e[-1] == '\n'))
        *--e = 0;
    return s;
}

/* `event = 条件 | 動作 | メッセージ` を1件読む。
 *   条件: TURN:n / BLD:陣営:n / LOSS:陣営:n / AREA:陣営:x,y:半径
 *   動作: MSG / SPAWN:陣営:ユニットID:x,y[:体数] / FUNDS:陣営:金額
 * 例) event = TURN:8 | SPAWN:1:TANK:20,7:2 | 敵の増援が到着した！ */
static int parse_event(CpnNode *n, const char *val)
{
    if (n->n_evs >= MAX_EVENTS) return -1;
    char buf[320];
    snprintf(buf, sizeof buf, "%s", val);

    char *bar1 = strchr(buf, '|');
    if (!bar1) return -1;
    *bar1++ = 0;
    char *bar2 = strchr(bar1, '|');
    if (!bar2) return -1;
    *bar2++ = 0;

    char *cond = trim_str(buf), *act = trim_str(bar1), *msg = trim_str(bar2);
    MapEvent *e = &n->evs[n->n_evs];
    memset(e, 0, sizeof *e);
    n->ev_unit[n->n_evs][0] = 0;

    /* --- 条件 --- */
    char *p = strchr(cond, ':');
    if (p) *p++ = 0;
    if (!strcmp(cond, "TURN")) {
        if (!p) return -1;
        e->cond = EV_C_TURN; e->c1 = (int16_t)atoi(p);
    } else if (!strcmp(cond, "BLD") || !strcmp(cond, "LOSS")) {
        if (!p) return -1;
        char *q = strchr(p, ':');
        if (!q) return -1;
        *q++ = 0;
        e->cond = !strcmp(cond, "BLD") ? EV_C_BLD : EV_C_LOSS;
        e->c1 = (int16_t)atoi(p); e->c2 = (int16_t)atoi(q);
    } else if (!strcmp(cond, "AREA")) {
        /* AREA:陣営:x,y:半径 */
        if (!p) return -1;
        char *q = strchr(p, ':');
        if (!q) return -1;
        *q++ = 0;
        char *r = strchr(q, ':');
        if (!r) return -1;
        *r++ = 0;
        char *comma = strchr(q, ',');
        if (!comma) return -1;
        *comma++ = 0;
        e->cond = EV_C_AREA;
        e->c1 = (int16_t)atoi(p);
        e->c2 = (int16_t)atoi(q); e->c3 = (int16_t)atoi(comma);
        e->c4 = (int16_t)atoi(r);
    } else if (!strcmp(cond, "CAPTURED")) {
        /* CAPTURED:陣営:x,y — その拠点をその陣営が持ったら */
        if (!p) return -1;
        char *q = strchr(p, ':');
        if (!q) return -1;
        *q++ = 0;
        char *comma = strchr(q, ',');
        if (!comma) return -1;
        *comma++ = 0;
        e->cond = EV_C_CAPTURED;
        e->c1 = (int16_t)atoi(p);
        e->c2 = (int16_t)atoi(q); e->c3 = (int16_t)atoi(comma);
    } else if (!strcmp(cond, "WEATHER")) {
        /* WEATHER:0|1|2 — 雨が降り出したら伏兵が動く、など */
        if (!p) return -1;
        e->cond = EV_C_WEATHER; e->c1 = (int16_t)atoi(p);
    } else return -1;

    /* --- 動作 --- */
    p = strchr(act, ':');
    if (p) *p++ = 0;
    if (!strcmp(act, "MSG")) {
        e->act = EV_A_MSG;
    } else if (!strcmp(act, "FUNDS")) {
        if (!p) return -1;
        char *q = strchr(p, ':');
        if (!q) return -1;
        *q++ = 0;
        e->act = EV_A_FUNDS;
        e->a1 = (int16_t)atoi(p); e->a2 = (int16_t)atoi(q);
    } else if (!strcmp(act, "SPAWN")) {
        /* SPAWN:陣営:ユニットID:x,y[:体数] */
        if (!p) return -1;
        char *q = strchr(p, ':');
        if (!q) return -1;
        *q++ = 0;
        char *r = strchr(q, ':');
        if (!r) return -1;
        *r++ = 0;
        char *cnt = strchr(r, ':');
        if (cnt) *cnt++ = 0;
        char *comma = strchr(r, ',');
        if (!comma) return -1;
        *comma++ = 0;
        e->act = EV_A_SPAWN;
        e->a1 = (int16_t)atoi(p);
        snprintf(n->ev_unit[n->n_evs], sizeof n->ev_unit[0], "%s", q);
        e->a3 = (int16_t)atoi(r); e->a4 = (int16_t)atoi(comma);
        e->a5 = (int16_t)(cnt ? atoi(cnt) : 1);
    } else if (!strcmp(act, "WEATHER")) {
        /* WEATHER:天候[:ラウンド数] — 「嵐が来た」のような演出 */
        if (!p) return -1;
        char *q = strchr(p, ':');
        if (q) *q++ = 0;
        e->act = EV_A_WEATHER;
        e->a1 = (int16_t)atoi(p);
        e->a2 = (int16_t)(q ? atoi(q) : 3);
    } else if (!strcmp(act, "TERRAIN")) {
        /* TERRAIN:x,y:地形文字 — 「敵が橋を落とした」など */
        if (!p) return -1;
        char *q = strchr(p, ':');
        if (!q) return -1;
        *q++ = 0;
        char *comma = strchr(p, ',');
        if (!comma) return -1;
        *comma++ = 0;
        e->act = EV_A_TERRAIN;
        e->a1 = (int16_t)atoi(p); e->a2 = (int16_t)atoi(comma);
        e->a3 = (int16_t)(unsigned char)q[0];
    } else if (!strcmp(act, "COPOWER")) {
        /* COPOWER:陣営 — ボスが必殺技を撃ってくる */
        if (!p) return -1;
        e->act = EV_A_COPOWER; e->a1 = (int16_t)atoi(p);
    } else if (!strcmp(act, "HP")) {
        /* HP:陣営:変化量 — 負の値で疫病・爆撃の表現にも使える */
        if (!p) return -1;
        char *q = strchr(p, ':');
        if (!q) return -1;
        *q++ = 0;
        e->act = EV_A_HP;
        e->a1 = (int16_t)atoi(p); e->a2 = (int16_t)atoi(q);
    } else return -1;

    snprintf(e->msg, sizeof e->msg, "%s", msg);
    n->n_evs++;
    return 0;
}

/* `sub = 種類:数値:説明文` を1件読む。種類は SubType の名前。
 * SURVIVE だけは `SURVIVE:数値:ユニットID:説明文` の4項目。 */
static int parse_sub(CpnNode *n, const char *val)
{
    if (n->n_subs >= MAX_SUBS) return -1;
    char buf[256];
    snprintf(buf, sizeof buf, "%s", val);

    char *p1 = strchr(buf, ':');
    if (!p1) return -1;
    *p1++ = '\0';
    char *p2 = strchr(p1, ':');
    if (!p2) return -1;
    *p2++ = '\0';

    SubObjective *o = &n->subs[n->n_subs];
    memset(o, 0, sizeof *o);
    if      (!strcmp(buf, "MAX_LOSS"))  o->type = SUB_MAX_LOSS;
    else if (!strcmp(buf, "MAX_TURNS")) o->type = SUB_MAX_TURNS;
    else if (!strcmp(buf, "MIN_KILLS")) o->type = SUB_MIN_KILLS;
    else if (!strcmp(buf, "KEEP_BLD"))  o->type = SUB_KEEP_BLD;
    else if (!strcmp(buf, "CAPTURE"))   o->type = SUB_CAPTURE;
    else if (!strcmp(buf, "SURVIVE"))   o->type = SUB_SURVIVE;
    else return -1;
    o->param = (int16_t)atoi(p1);

    if (o->type == SUB_SURVIVE) {
        char *p3 = strchr(p2, ':');
        if (!p3) return -1;
        *p3++ = '\0';
        snprintf(o->unit, sizeof o->unit, "%s", p2);
        p2 = p3;
    }
    snprintf(o->desc, sizeof o->desc, "%s", p2);
    n->n_subs++;
    return 0;
}

/* .cpn の EASY/NORMAL/HARD を PlayerCtrl に直す。誤記は普通扱い。 */
static uint8_t ctrl_from_word(const char *val)
{
    if (!strcmp(val, "EASY")) return CTRL_CPU_EASY;
    if (!strcmp(val, "HARD")) return CTRL_CPU_HARD;
    return CTRL_CPU_NORMAL;
}

int campaign_load(Campaign *c, const char *path, char *err, int errlen)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        if (err) snprintf(err, (size_t)errlen, "%s: 開けません", path);
        return -1;
    }
    memset(c, 0, sizeof *c);
    CpnNode *cur = NULL;
    char line[512];
    int ln = 0;

    while (fgets(line, sizeof line, f)) {
        ln++;
        char *key, *val;
        int kind = parser_split_line(line, &key, &val);
        if (kind == 0) continue;
        if (kind == 2) {
            if (!strcmp(key, "campaign")) {
                cur = NULL;
                continue;
            }
            if (strncmp(key, "node ", 5) == 0) {
                if (c->n_nodes >= MAX_CAMPAIGN_MAPS) {
                    if (err) snprintf(err, (size_t)errlen, "%s:%d: ノード数上限超過", path, ln);
                    fclose(f); return -1;
                }
                cur = &c->nodes[c->n_nodes++];
                memset(cur, 0, sizeof *cur);
                for (int p = 0; p < MAX_PLAYERS; p++) cur->ctrl[p] = CTRL_CPU_NORMAL;
                snprintf(cur->id, sizeof cur->id, "%s", key + 5);
                continue;
            }
            if (err) snprintf(err, (size_t)errlen, "%s:%d: 不明なセクション", path, ln);
            fclose(f); return -1;
        }
        if (!cur) {
            if      (!strcmp(key, "name"))  snprintf(c->name, sizeof c->name, "%s", val);
            else if (!strcmp(key, "start")) snprintf(c->start, sizeof c->start, "%s", val);
            continue;
        }
        if      (!strcmp(key, "map"))   snprintf(cur->map, sizeof cur->map, "%s", val);
        else if (!strcmp(key, "title")) snprintf(cur->title, sizeof cur->title, "%s", val);
        else if (!strcmp(key, "art"))    snprintf(cur->art, sizeof cur->art, "%s", val);
        else if (!strcmp(key, "reward")) snprintf(cur->reward, sizeof cur->reward, "%s", val);
        else if (!strcmp(key, "reward_video"))
            snprintf(cur->reward_video, sizeof cur->reward_video, "%s", val);
        else if (!strcmp(key, "brief")) {
            if (cur->n_brief < MAX_BRIEF_LINES)
                snprintf(cur->brief[cur->n_brief++], sizeof cur->brief[0], "%s", val);
        }
        else if (!strcmp(key, "next_win"))      snprintf(cur->next_win, sizeof cur->next_win, "%s", val);
        else if (!strcmp(key, "next_win_fast")) snprintf(cur->next_win_fast, sizeof cur->next_win_fast, "%s", val);
        else if (!strcmp(key, "fast_turns"))    cur->fast_turns = atoi(val);
        else if (!strcmp(key, "par_turns"))     cur->par_turns = atoi(val);
        else if (!strcmp(key, "no_reinforce"))  cur->no_reinforce = atoi(val);
        else if (!strcmp(key, "event")) {
            if (parse_event(cur, val) != 0) {
                if (err) snprintf(err, (size_t)errlen,
                                  "%s:%d: event の書式が不正（条件 | 動作 | メッセージ）", path, ln);
                fclose(f); return -1;
            }
        }
        else if (!strcmp(key, "sub")) {
            if (parse_sub(cur, val) != 0) {
                if (err) snprintf(err, (size_t)errlen,
                                  "%s:%d: sub の書式が不正（種類:数値:説明）", path, ln);
                fclose(f); return -1;
            }
        }
        else if (!strcmp(key, "next_lose"))     snprintf(cur->next_lose, sizeof cur->next_lose, "%s", val);
        else if (!strcmp(key, "enemy_co"))
            snprintf(cur->co[1], sizeof cur->co[1], "%s", val);
        else if (!strcmp(key, "carry"))         cur->carry = atoi(val);
        else if (!strcmp(key, "bonus"))         cur->bonus = atoi(val);
        else if (!strcmp(key, "enemy")) cur->ctrl[1] = ctrl_from_word(val);
        /* ctrl2=HARD / co2=WOLF のように、陣営2以降を番号付きで指定する。
         * 陣営0は常に人間なので受け付けない。 */
        else if ((!strncmp(key, "ctrl", 4) || !strncmp(key, "co", 2)) &&
                 key[strlen(key) - 1] >= '1' && key[strlen(key) - 1] <= '4' &&
                 (strlen(key) == 5 || strlen(key) == 3)) {
            int p = key[strlen(key) - 1] - '0';
            if (key[0] == 'c' && key[1] == 't') cur->ctrl[p] = ctrl_from_word(val);
            else snprintf(cur->co[p], sizeof cur->co[p], "%s", val);
        }
        else {
            if (err) snprintf(err, (size_t)errlen, "%s:%d: 不明なキー", path, ln);
            fclose(f); return -1;
        }
    }
    fclose(f);
    if (c->n_nodes == 0 || !c->start[0]) {
        if (err) snprintf(err, (size_t)errlen, "%s: ノードまたは start がありません", path);
        return -1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* 作戦評価（S/A/B/C）                                                 */
/* ------------------------------------------------------------------ */
static int clamp100(int v) { return v < 0 ? 0 : (v > 100 ? 100 : v); }

int campaign_sub_bonus(void) { return 800; }


/* 種別 type が「name の系統」か。進化すると ID が変わる（T_SHIP → T_SHIP_V）ので、
 * 進化後も同じ系統として数える。そうしないと
 * 「輸送艦を1隻以上生存させる」のような副目標が、進化させた途端に達成不能になる。 */
static bool type_is_kind(const Game *g, int type, const char *name)
{
    if (!strcmp(g->types[type].id, name)) return true;
    for (int i = 0; i < g->n_types; i++)
        if (!strcmp(g->types[i].id, name))
            return g->types[i].evolve_to[0] &&
                   !strcmp(g->types[i].evolve_to, g->types[type].id);
    return false;
}

/* 自軍の敵に回る陣営の損失合計。三つ巴や援軍マップでは
 * 敵が陣営1だけとは限らないので、戦果は全部を足して数える。 */
static int enemy_losses(const Game *g)
{
    int n = 0;
    for (int p = 0; p < MAX_PLAYERS; p++)
        if (game_is_enemy(g, 0, p)) n += g->lost_units[p];
    return n;
}

bool campaign_sub_done(const Game *g, const CpnNode *node, int i)
{
    if (!g || !node || i < 0 || i >= node->n_subs) return false;
    const SubObjective *o = &node->subs[i];
    switch (o->type) {
    case SUB_MAX_LOSS:  return g->lost_units[0] <= o->param;
    case SUB_MAX_TURNS: return g->turn <= o->param;
    case SUB_MIN_KILLS: return enemy_losses(g) >= o->param;
    case SUB_KEEP_BLD:
    case SUB_CAPTURE:   return game_count_buildings(g, 0) >= o->param;
    case SUB_SURVIVE: {
        int n = 0;
        for (int u = 0; u < g->n_units; u++) {
            const Unit *un = &g->units[u];
            if (!(un->flags & UF_ALIVE) || un->owner != 0) continue;
            if (o->unit[0] && !type_is_kind(g, un->type, o->unit)) continue;
            n++;
        }
        return n >= o->param;
    }
    default: return false;
    }
}

int campaign_sub_count_done(const Game *g, const CpnNode *node)
{
    if (!node) return 0;
    int n = 0;
    for (int i = 0; i < node->n_subs; i++)
        if (campaign_sub_done(g, node, i)) n++;
    return n;
}

void campaign_evaluate(const Game *g, const CpnNode *node, CpnScore *out)
{
    if (!out) return;
    memset(out, 0, sizeof *out);

    /* 基準ターン: .cpn の par_turns、無ければ制限ターンの半分（既定20） */
    int par = (node && node->par_turns > 0) ? node->par_turns
            : (g->turn_limit > 0 ? g->turn_limit / 2 : 20);
    if (par < 1) par = 1;

    /* 速さ: 基準どおりで100、超過するほど下がる */
    int over = g->turn - par;
    out->speed = clamp100(over <= 0 ? 100 : 100 - over * 100 / par);

    /* 戦力温存: losses が少ないほど高い（1体につき-10） */
    out->loss = clamp100(100 - g->lost_units[0] * 10);

    /* 戦果: 敵を減らした量（1体につき+10、10体で満点） */
    out->power = clamp100(enemy_losses(g) * 10);

    out->total = (out->speed + out->loss + out->power) / 3;
    out->rank = (out->total >= 90) ? RANK_S
              : (out->total >= 75) ? RANK_A
              : (out->total >= 60) ? RANK_B : RANK_C;
}

int campaign_rank_bonus(CpnRank r)
{
    switch (r) {
    case RANK_S: return 2000;
    case RANK_A: return 1200;
    case RANK_B: return 600;
    default:     return 0;
    }
}

const char *campaign_rank_str(CpnRank r)
{
    switch (r) {
    case RANK_S: return "S";
    case RANK_A: return "A";
    case RANK_B: return "B";
    case RANK_C: return "C";
    default:     return "-";
    }
}

const CpnNode *campaign_find_node(const Campaign *c, const char *id)
{
    for (int i = 0; i < c->n_nodes; i++)
        if (!strcmp(c->nodes[i].id, id)) return &c->nodes[i];
    return NULL;
}

void campaign_store_push(CampaignState *s, int type, int exp)
{
    if (s->n_store < MAX_STORE_UNITS) {
        s->store[s->n_store].type = (uint8_t)type;
        s->store[s->n_store].exp = (uint8_t)exp;
        s->n_store++;
        return;
    }
    /* 満杯: 最も経験値の低い枠を探し、新規の方が高ければ入れ替える */
    int lo = 0;
    for (int i = 1; i < s->n_store; i++)
        if (s->store[i].exp < s->store[lo].exp) lo = i;
    if (exp > s->store[lo].exp) {
        s->store[lo].type = (uint8_t)type;
        s->store[lo].exp = (uint8_t)exp;
    }
}

void campaign_store_remove(CampaignState *s, int slot)
{
    if (slot < 0 || slot >= s->n_store) return;
    for (int i = slot; i < s->n_store - 1; i++)
        s->store[i] = s->store[i + 1];
    s->n_store--;
}

/* owner の首都（無ければ所有建物、無ければ既存ユニット）を展開の起点として返す */
static void find_origin(const Game *g, int owner, int *ox, int *oy)
{
    *ox = -1; *oy = -1;
    for (int y = 0; y < g->h && *ox < 0; y++)
        for (int x = 0; x < g->w && *ox < 0; x++)
            if (g->tiles[y][x].owner == owner &&
                g->terrains[g->tiles[y][x].terrain].is_hq) { *ox = x; *oy = y; }
    for (int y = 0; y < g->h && *ox < 0; y++)
        for (int x = 0; x < g->w && *ox < 0; x++)
            if (g->tiles[y][x].owner == owner &&
                g->terrains[g->tiles[y][x].terrain].capturable) { *ox = x; *oy = y; }
    for (int i = 0; i < g->n_units && *ox < 0; i++)
        if ((g->units[i].flags & UF_ALIVE) && g->units[i].owner == owner)
            { *ox = g->units[i].pos.x; *oy = g->units[i].pos.y; }
    if (*ox < 0) { *ox = 1; *oy = 1; }
}

/* (ox,oy) から最寄りの「進入可能かつ自レイヤーが空き」ヘクスへ1体配置する。
 * 成功=unit index / 失敗=-1（盤上に適地が無い場合） */
static int place_unit_near(Game *g, int owner, int type, int ox, int oy)
{
    if (type < 0 || type >= g->n_types) return -1;
    MoveClass mc = (MoveClass)g->types[type].mclass;
    Layer L = unit_layer(mc);

    static uint8_t seen[MAX_MAP_H][MAX_MAP_W];
    static int qx[MAX_MAP_W * MAX_MAP_H], qy[MAX_MAP_W * MAX_MAP_H];
    memset(seen, 0, sizeof seen);
    int head = 0, tail = 0;
    if (!game_in_bounds(g, ox, oy)) { ox = 1; oy = 1; }
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
            /* 距離制限なしの全面BFS（船は最寄りの海、空は最寄りの空きへ確実に届く） */
            seen[ny][nx] = 1;
            qx[tail] = nx; qy[tail] = ny; tail++;
        }
    }
    return -1;
}

/* 持越しユニットをP0首都（なければ最初の自軍建物）周辺に展開。
 * 初期配置できるのはマップ本来の自軍ユニット数の2倍まで（戦力の雪だるま防止）。
 * 上限超過分と、配置先が無い（例: 海の無いマップに艦船）ユニットは倉庫へ保管する。
 * s->carry は経験値の高い順に並んでいるので、上限内には精鋭が優先的に配置される。 */
static int deploy_carry(Game *g, CampaignState *s, const uint8_t *sel)
{
    int deploy_limit = campaign_deploy_limit(g);
    int deployed = 0;
    int ox, oy;
    find_origin(g, 0, &ox, &oy);

    for (int i = 0; i < s->n_carry; i++) {
        int type = s->carry[i].type;
        if (type >= g->n_types) continue;
        /* 手動選択がある場合は「出撃させない」部隊を倉庫へ。
         * 選択が無い場合は経験値順に上限まで出し、残りを倉庫へ。 */
        bool take = sel ? (sel[i] != 0) : (deployed < deploy_limit);
        if (take && deployed >= deploy_limit) take = false;  /* 上限は必ず守る */
        if (!take) {
            campaign_store_push(s, type, s->carry[i].exp);
            continue;
        }
        int ui = place_unit_near(g, 0, type, ox, oy);
        if (ui >= 0) {
            g->units[ui].exp = s->carry[i].exp;
            deployed++;
        } else {
            /* 盤上に適地が全く無い（例: 海の無いマップに艦船）→ 倉庫へ */
            campaign_store_push(s, type, s->carry[i].exp);
        }
    }
    return deployed;
}

/* 自軍が持越しで増えたぶん、敵にも増援を与えて戦力差を埋める。
 * 増援数は「実際に展開した持越しの数」に合わせ、元の敵戦力の
 * (CAMPAIGN_ENEMY_MAX_RATIO-1) 倍を上限とする。
 * 持越しゼロなら増援もゼロなので、負けが込んでいるときに詰まない。 */
static void reinforce_enemy(Game *g, int player_extra)
{
    if (player_extra <= 0) return;

    /* 自軍の敵に回る陣営全部に分けて増援を出す。
     * 2陣営なら従来どおり陣営1だけ。三つ巴や援軍マップでは
     * 敵側が複数になるので、一方だけを肥やすと力関係が崩れる。 */
    /* **居る陣営だけ数えること**。既定の team[p]=p ではマップに居ない
     * 陣営も「敵」になるので、そのまま割ると増援が発生しない分に消える。 */
    int foes[MAX_PLAYERS], n_foes = 0;
    for (int p = 0; p < MAX_PLAYERS; p++) {
        if (!game_is_enemy(g, 0, p)) continue;
        for (int i = 0; i < g->n_units; i++)
            if ((g->units[i].flags & UF_ALIVE) && g->units[i].owner == p) {
                foes[n_foes++] = p; break;
            }
    }
    if (n_foes <= 0) return;

    for (int k = 0; k < n_foes; k++) {
        int who = foes[k];
        /* 既存の編成を集める（同じ顔ぶれを増やす） */
        int types[MAX_UNITS], n_types_seen = 0;
        for (int i = 0; i < g->n_units; i++) {
            const Unit *u = &g->units[i];
            if (!(u->flags & UF_ALIVE) || (u->flags & UF_LOADED)) continue;
            if (u->owner != who) continue;
            types[n_types_seen++] = u->type;
        }
        if (n_types_seen <= 0) continue;   /* この陣営が居ないマップ */

        /* 人数割り。余りは前の陣営から順に引き受ける。 */
        int share = player_extra / n_foes + (k < player_extra % n_foes ? 1 : 0);
        int cap = n_types_seen * (CAMPAIGN_ENEMY_MAX_RATIO - 1);
        int add = share < cap ? share : cap;

        int ox, oy;
        find_origin(g, who, &ox, &oy);
        for (int i = 0; i < add; i++)
            place_unit_near(g, who, types[i % n_types_seen], ox, oy);
    }
}

/* place_unit_near の BFS は全面をなめる（距離制限なし・進入不可セルも踏む）ので、
 * 「盤上のどこかに進入できるセルが1つでもあるか」と同じ意味になる。
 * 占有は見ない（選択時点ではまだ持越しを並べていないため）。 */
bool campaign_type_placeable(const Game *g, int type)
{
    if (type < 0 || type >= g->n_types) return false;
    MoveClass mc = (MoveClass)g->types[type].mclass;
    for (int y = 0; y < g->h; y++)
        for (int x = 0; x < g->w; x++)
            if (g->terrains[g->tiles[y][x].terrain].mcost[mc] > 0) return true;
    return false;
}

int campaign_deploy_limit(const Game *g)
{
    /* マップ本来の自軍ユニット数 × 倍率（持越し展開の前に数えること） */
    int base_units = 0;
    for (int i = 0; i < g->n_units; i++)
        if ((g->units[i].flags & UF_ALIVE) && g->units[i].owner == 0)
            base_units++;
    return base_units * DEPLOY_CARRY_RATIO;
}

int campaign_setup_map(Game *g, const Campaign *c, const CampaignState *s,
                       const char *base_path, char *err, int errlen)
{
    const CpnNode *node = campaign_find_node(c, s->node);
    if (!node) {
        if (err) snprintf(err, (size_t)errlen, "ノード %s が見つかりません", s->node);
        return -1;
    }
    char path[600];
    snprintf(path, sizeof path, "%sdata/%s", base_path, node->map);
    if (data_load_map(g, path, err, errlen) != 0)
        return -1;

    g->fog = true;                 /* キャンペーンは常時ON（仕様書 5.8） */
    /* 陣営0 は常に人間。残りはノードの指定（無指定はCPU普通）。
     * 多陣営マップでは援軍も敵の援軍もここで難易度を決める。
     * チーム分け（誰が味方か）は .map の team0..4 側の責任。 */
    g->ctrl[0] = CTRL_HUMAN;
    for (int p = 1; p < MAX_PLAYERS; p++) g->ctrl[p] = node->ctrl[p];
    /* 指揮官: 自軍はキャンペーンで選んだもの、他はノード指定（無ければ既定） */
    g->co_id[0] = (int8_t)(g->n_cos > 0 ? (s->player_co >= 0 ? s->player_co : 0) : -1);
    for (int p = 1; p < MAX_PLAYERS; p++) {
        if (g->n_cos <= 0) { g->co_id[p] = -1; continue; }
        int e = node->co[p][0] ? data_find_commander(g, node->co[p]) : -1;
        g->co_id[p] = (int8_t)(e >= 0 ? e : 0);
    }
    /* マップイベントを流し込む。ユニットIDはここで型indexへ解決する
     * （見つからない指定は SPAWN を無効化し、メッセージだけ出す） */
    g->n_events = 0;
    g->events_fired = 0;
    memset(g->events, 0, sizeof g->events);
    for (int i = 0; i < node->n_evs && i < MAX_EVENTS; i++) {
        MapEvent e = node->evs[i];
        if (e.act == EV_A_SPAWN) {
            int t = data_find_unit_type(g, node->ev_unit[i]);
            if (t < 0) e.act = EV_A_MSG;      /* 誤記でも落とさずメッセージ扱い */
            else e.a2 = (int16_t)t;
        }
        g->events[g->n_events++] = e;
    }
    g->co_gauge[0] = g->co_gauge[1] = 0;
    g->co_power_turns[0] = g->co_power_turns[1] = 0;
    g->funds[0] += s->funds_carry;
    return 0;
}

void campaign_begin(Game *g, const Campaign *c, CampaignState *s,
                    uint32_t seed, const uint8_t *sel)
{
    const CpnNode *node = campaign_find_node(c, s->node);
    int deployed = 0;
    if (node && node->carry)
        deployed = deploy_carry(g, s, sel);
    /* 持越しで増えたぶんだけ敵にも増援を出す（2マップ目以降の戦力差対策） */
    if (node && !node->no_reinforce)
        reinforce_enemy(g, deployed);
    game_start(g, seed);
}

int campaign_setup_battle(Game *g, const Campaign *c, CampaignState *s,
                          const char *base_path, uint32_t seed,
                          char *err, int errlen)
{
    if (campaign_setup_map(g, c, s, base_path, err, errlen) != 0)
        return -1;
    campaign_begin(g, c, s, seed, NULL);   /* 選択なし＝経験値順に自動 */
    return 0;
}

int campaign_on_victory(const Game *g, const Campaign *c, CampaignState *s)
{
    const CpnNode *node = campaign_find_node(c, s->node);
    if (!node) return 1;

    /* クリア記録（全体マップ表示用） */
    for (int i = 0; i < c->n_nodes; i++)
        if (&c->nodes[i] == node)
            s->cleared |= 1u << i;

    /* 持越しユニット: 生存P0ユニットを「経験値の高い順」に**全員**引き継ぐ
     * （MAX_CARRY_UNITS は1マップに置ける最大数＝実質無制限）。
     * 経験値が同じ場合はHPの高い（消耗の少ない）方を優先する。
     * 並び順が意味を持つのは次の作戦の出撃枠が足りないときで、
     * 先頭から順に出撃し、あぶれた分が倉庫へ回る＝精鋭から戦場に出る。 */
    s->n_carry = 0;
    if (node->carry) {
        int idx[MAX_UNITS];
        int n = 0;
        for (int i = 0; i < g->n_units; i++)
            if ((g->units[i].flags & UF_ALIVE) && g->units[i].owner == 0)
                idx[n++] = i;
        /* 「価値の高い順」に並べる: 価格 → 経験値 → HP の順で比較する。
         * **経験値だけで並べてはいけない。** 進化すると経験値が0に戻るので、
         * 進化したばかりの精鋭が必ず最後尾に回り、出撃枠から溢れて倉庫に沈む。
         * 一度沈むと戦えないので経験値も戻らず、永久に控えのままになってしまう。
         * 価格を第一の物差しにすると、進化後（元より高価）が前に来る。 */
        for (int i = 0; i < n; i++)
            for (int j = i + 1; j < n; j++) {
                const Unit *uj = &g->units[idx[j]], *ui = &g->units[idx[i]];
                int cj = g->types[uj->type].cost, ci = g->types[ui->type].cost;
                bool better = (cj > ci) ||
                              (cj == ci && uj->exp > ui->exp) ||
                              (cj == ci && uj->exp == ui->exp && uj->hp > ui->hp);
                if (better) {
                    int t = idx[i]; idx[i] = idx[j]; idx[j] = t;
                }
            }
        int i = 0;
        for (; i < n && s->n_carry < MAX_CARRY_UNITS; i++) {
            s->carry[s->n_carry].type = g->units[idx[i]].type;
            s->carry[s->n_carry].exp = g->units[idx[i]].exp;
            s->n_carry++;
        }
        /* 持越し上限を超えた生存ユニットは倉庫へ保管（消失させない） */
        for (; i < n; i++)
            campaign_store_push(s, g->units[idx[i]].type, g->units[idx[i]].exp);
    }

    /* 作戦評価を記録し、ランクに応じたボーナス資金を上乗せする */
    CpnScore sc;
    campaign_evaluate(g, node, &sc);
    for (int i = 0; i < c->n_nodes; i++)
        if (&c->nodes[i] == node) {
            /* 再挑戦で下がらないよう、より良いランクだけ残す（S=1が最良） */
            if (s->rank[i] == RANK_NONE || (uint8_t)sc.rank < s->rank[i])
                s->rank[i] = (uint8_t)sc.rank;
        }

    /* 資金持越し: 残額50% + ボーナス + 評価ボーナス（仕様書 6.1） */
    s->funds_carry = g->funds[0] / 2 + node->bonus + campaign_rank_bonus(sc.rank)
                   + campaign_sub_count_done(g, node) * campaign_sub_bonus();

    /* 分岐: 規定ターン以内なら fast ルート */
    const char *next = node->next_win;
    if (node->next_win_fast[0] && node->fast_turns > 0 &&
        g->turn <= node->fast_turns)
        next = node->next_win_fast;

    if (!strcmp(next, "WIN") || !campaign_find_node(c, next)) {
        return 1; /* クリア */
    }
    snprintf(s->node, sizeof s->node, "%s", next);
    return 0;
}
