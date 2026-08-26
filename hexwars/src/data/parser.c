/* parser.c - INI風独自テキスト形式の1パスパーサ */
#include "parser.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define LINE_MAX_LEN 512

/* ---- 文字列ユーティリティ ---- */
static char *trim(char *s)
{
    while (*s == ' ' || *s == '\t') s++;
    char *e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' ||
                     e[-1] == '\r' || e[-1] == '\n'))
        *--e = '\0';
    return s;
}

/* 行を key/value に分解。コメント(#)除去。戻り値: 0=空行 1=kv 2=[section] */
int parser_split_line(char *line, char **key, char **val)
{
    /* コメント除去（行頭でなくても # 以降を落とす） */
    char *hash = strchr(line, '#');
    if (hash) *hash = '\0';
    char *s = trim(line);
    if (!*s) return 0;
    if (*s == '[') {
        char *e = strchr(s, ']');
        if (e) *e = '\0';
        *key = trim(s + 1);
        *val = NULL;
        return 2;
    }
    char *eq = strchr(s, '=');
    if (!eq) return 0;
    *eq = '\0';
    *key = trim(s);
    *val = trim(eq + 1);
    return 1;
}

static void set_err(char *err, int errlen, const char *path, int line,
                    const char *msg)
{
    if (err && errlen > 0)
        snprintf(err, (size_t)errlen, "%s:%d: %s", path, line, msg);
}

/* ---- terrain.def ---- */
static int mc_supply_mask(const char *list)
{
    int mask = 0;
    char buf[128];
    snprintf(buf, sizeof buf, "%s", list);
    char *tok = strtok(buf, ",");
    while (tok) {
        char *t = trim(tok);
        if (!strcmp(t, "FOOT"))  mask |= 1 << MC_FOOT;
        if (!strcmp(t, "WHEEL")) mask |= 1 << MC_WHEEL;
        if (!strcmp(t, "TRACK")) mask |= 1 << MC_TRACK;
        if (!strcmp(t, "AIR"))   mask |= 1 << MC_AIR;
        if (!strcmp(t, "SEA"))   mask |= 1 << MC_SEA;
        if (!strcmp(t, "SUB"))   mask |= 1 << MC_SUB;
        if (!strcmp(t, "LAND"))
            mask |= (1 << MC_FOOT) | (1 << MC_WHEEL) | (1 << MC_TRACK);
        tok = strtok(NULL, ",");
    }
    return mask;
}

int data_load_terrain(Game *g, const char *path, char *err, int errlen)
{
    FILE *f = fopen(path, "rb");
    if (!f) { set_err(err, errlen, path, 0, "開けません"); return -1; }

    g->n_terrains = 0;
    TerrainType *cur = NULL;
    char line[LINE_MAX_LEN];
    int ln = 0;

    while (fgets(line, sizeof line, f)) {
        ln++;
        char *key, *val;
        int kind = parser_split_line(line, &key, &val);
        if (kind == 0) continue;
        if (kind == 2) {
            if (strncmp(key, "terrain ", 8) != 0) {
                set_err(err, errlen, path, ln, "不明なセクション");
                fclose(f); return -1;
            }
            if (g->n_terrains >= MAX_TERRAIN) {
                set_err(err, errlen, path, ln, "地形数が上限超過");
                fclose(f); return -1;
            }
            cur = &g->terrains[g->n_terrains++];
            memset(cur, 0, sizeof *cur);
            snprintf(cur->id, sizeof cur->id, "%s", trim(key + 8));
            continue;
        }
        if (!cur) {
            set_err(err, errlen, path, ln, "セクション外のキー");
            fclose(f); return -1;
        }
        if      (!strcmp(key, "name"))  snprintf(cur->name, sizeof cur->name, "%s", val);
        else if (!strcmp(key, "char"))  cur->chr = val[0];
        else if (!strcmp(key, "def"))   cur->def_bonus = (int16_t)atoi(val);
        else if (!strcmp(key, "cost_foot"))  cur->mcost[MC_FOOT]  = (int16_t)atoi(val);
        else if (!strcmp(key, "cost_wheel")) cur->mcost[MC_WHEEL] = (int16_t)atoi(val);
        else if (!strcmp(key, "cost_track")) cur->mcost[MC_TRACK] = (int16_t)atoi(val);
        else if (!strcmp(key, "cost_air"))   cur->mcost[MC_AIR]   = (int16_t)atoi(val);
        else if (!strcmp(key, "cost_sea"))   cur->mcost[MC_SEA]   = (int16_t)atoi(val);
        else if (!strcmp(key, "cost_sub"))   cur->mcost[MC_SUB]   = (int16_t)atoi(val);
        else if (!strcmp(key, "income"))     cur->income = (int16_t)atoi(val);
        else if (!strcmp(key, "capturable")) cur->capturable = (uint8_t)atoi(val);
        else if (!strcmp(key, "hide"))       cur->hide = (uint8_t)atoi(val);
        else if (!strcmp(key, "hq"))         cur->is_hq = (uint8_t)atoi(val);
        else if (!strcmp(key, "height"))     cur->height = (int16_t)atoi(val);
        else if (!strcmp(key, "color"))      cur->color = (uint32_t)strtoul(val, NULL, 16);
        else if (!strcmp(key, "supplies"))   cur->supplies = (uint8_t)mc_supply_mask(val);
        else if (!strcmp(key, "produces")) {
            if      (!strcmp(val, "LAND")) cur->produces = PROD_LAND;
            else if (!strcmp(val, "AIR"))  cur->produces = PROD_AIR;
            else if (!strcmp(val, "SEA"))  cur->produces = PROD_SEA;
            else                           cur->produces = PROD_NONE;
        }
        else { set_err(err, errlen, path, ln, "不明なキー"); fclose(f); return -1; }
    }
    fclose(f);
    return 0;
}

/* ---- units.def ---- */
static int parse_mclass(const char *v)
{
    if (!strcmp(v, "LAND_FOOT"))  return MC_FOOT;
    if (!strcmp(v, "LAND_WHEEL")) return MC_WHEEL;
    if (!strcmp(v, "LAND_TRACK")) return MC_TRACK;
    if (!strcmp(v, "AIR"))        return MC_AIR;
    if (!strcmp(v, "SEA"))        return MC_SEA;
    if (!strcmp(v, "SEA_SUB"))    return MC_SUB;
    return -1;
}

/* no_fuel の値。未知の文字列は -1 */
static int parse_nofuel(const char *v)
{
    if (!strcmp(v, "NONE"))   return NOFUEL_NONE;
    if (!strcmp(v, "DIE"))    return NOFUEL_DIE;
    if (!strcmp(v, "DAMAGE")) return NOFUEL_DAMAGE;
    return -1;
}

/* 効果音の役割名 → SeId。sound.h の SE_KEYS と同じ並び。未知は -1 */
static int parse_se_name(const char *v)
{
    static const char *names[] = {
        "CURSOR", "OK", "CANCEL",
        "MOVE_FOOT", "MOVE_VEHICLE", "MOVE_AIR",
        "SHOT", "EXPLOSION", "CAPTURE", "TURN",
    };
    for (int i = 0; i < (int)(sizeof names / sizeof names[0]); i++)
        if (!strcmp(v, names[i])) return i;
    return -1;
}

static int parse_armor(const char *v)
{
    if (!strcmp(v, "SOFT")) return ARMOR_SOFT;
    if (!strcmp(v, "HARD")) return ARMOR_HARD;
    if (!strcmp(v, "AIR"))  return ARMOR_AIR;
    if (!strcmp(v, "SEA"))  return ARMOR_SEA;
    return -1;
}

int data_load_units(Game *g, const char *path, char *err, int errlen)
{
    FILE *f = fopen(path, "rb");
    if (!f) { set_err(err, errlen, path, 0, "開けません"); return -1; }

    g->n_types = 0;
    UnitType *cur = NULL;
    char line[LINE_MAX_LEN];
    int ln = 0;

    while (fgets(line, sizeof line, f)) {
        ln++;
        char *key, *val;
        int kind = parser_split_line(line, &key, &val);
        if (kind == 0) continue;
        if (kind == 2) {
            if (strncmp(key, "unit ", 5) != 0) {
                set_err(err, errlen, path, ln, "不明なセクション");
                fclose(f); return -1;
            }
            if (g->n_types >= MAX_UNIT_TYPES) {
                set_err(err, errlen, path, ln, "ユニット種が上限超過");
                fclose(f); return -1;
            }
            cur = &g->types[g->n_types++];
            memset(cur, 0, sizeof *cur);
            cur->range_min = cur->range_max = 1;
            cur->move_and_fire = 1;
            /* 未指定を表す値。あとで class に応じた既定値に置き換える */
            cur->upkeep  = UPKEEP_AUTO;
            cur->no_fuel = NOFUEL_AUTO;
            cur->move_se = -1;
            snprintf(cur->id, sizeof cur->id, "%s", trim(key + 5));
            continue;
        }
        if (!cur) {
            set_err(err, errlen, path, ln, "セクション外のキー");
            fclose(f); return -1;
        }
        if      (!strcmp(key, "name")) snprintf(cur->name, sizeof cur->name, "%s", val);
        else if (!strcmp(key, "icon")) snprintf(cur->icon, sizeof cur->icon, "%s", val);
        else if (!strcmp(key, "class")) {
            int m = parse_mclass(val);
            if (m < 0) { set_err(err, errlen, path, ln, "不正な class"); fclose(f); return -1; }
            cur->mclass = (uint8_t)m;
        }
        else if (!strcmp(key, "armor")) {
            int a = parse_armor(val);
            if (a < 0) { set_err(err, errlen, path, ln, "不正な armor"); fclose(f); return -1; }
            cur->armor = (uint8_t)a;
        }
        else if (!strcmp(key, "cost"))      cur->cost   = (int16_t)atoi(val);
        else if (!strcmp(key, "move"))      cur->move   = (uint8_t)atoi(val);
        else if (!strcmp(key, "fuel"))      cur->fuel   = (uint8_t)atoi(val);
        else if (!strcmp(key, "ammo"))      cur->ammo   = (uint8_t)atoi(val);
        else if (!strcmp(key, "vision"))    cur->vision = (uint8_t)atoi(val);
        else if (!strcmp(key, "atk_soft"))  cur->atk[ARMOR_SOFT] = (int16_t)atoi(val);
        else if (!strcmp(key, "atk_hard"))  cur->atk[ARMOR_HARD] = (int16_t)atoi(val);
        else if (!strcmp(key, "atk_air"))   cur->atk[ARMOR_AIR]  = (int16_t)atoi(val);
        else if (!strcmp(key, "atk_sea"))   cur->atk[ARMOR_SEA]  = (int16_t)atoi(val);
        else if (!strcmp(key, "def"))       cur->def_ = (int16_t)atoi(val);
        else if (!strcmp(key, "range_min")) cur->range_min = (uint8_t)atoi(val);
        else if (!strcmp(key, "range_max")) cur->range_max = (uint8_t)atoi(val);
        else if (!strcmp(key, "can_capture"))   cur->can_capture = (uint8_t)atoi(val);
        else if (!strcmp(key, "move_and_fire")) cur->move_and_fire = (uint8_t)atoi(val);
        else if (!strcmp(key, "anti_sub"))      cur->anti_sub = (uint8_t)atoi(val);
        else if (!strcmp(key, "is_sub"))        cur->is_sub = (uint8_t)atoi(val);
        else if (!strcmp(key, "capacity"))      cur->capacity = (uint8_t)atoi(val);
        else if (!strcmp(key, "resupply_cargo")) cur->resupply_cargo = (uint8_t)atoi(val);
        else if (!strcmp(key, "upkeep"))    cur->upkeep = (uint8_t)atoi(val);
        else if (!strcmp(key, "no_fuel")) {
            int nf = parse_nofuel(val);
            if (nf < 0) { set_err(err, errlen, path, ln, "不正な no_fuel"); fclose(f); return -1; }
            cur->no_fuel = (uint8_t)nf;
        }
        else if (!strcmp(key, "move_se")) {
            int se = parse_se_name(val);
            if (se < 0) { set_err(err, errlen, path, ln, "不正な move_se"); fclose(f); return -1; }
            cur->move_se = (int8_t)se;
        }
        /* stealth / detect は is_sub / anti_sub の別名。
         * 効果は同じで、世界観に縛られない名前でも書けるようにしてある。 */
        else if (!strcmp(key, "stealth"))  cur->is_sub   = (uint8_t)atoi(val);
        else if (!strcmp(key, "detect"))   cur->anti_sub = (uint8_t)atoi(val);
        else if (!strcmp(key, "supply"))        cur->supply = (uint8_t)atoi(val);
        else if (!strcmp(key, "image")) {
            snprintf(cur->image[0], sizeof cur->image[0], "%s", val);
            snprintf(cur->image[1], sizeof cur->image[1], "%s", val);
        }
        else if (!strcmp(key, "image0")) snprintf(cur->image[0], sizeof cur->image[0], "%s", val);
        else if (!strcmp(key, "image1")) snprintf(cur->image[1], sizeof cur->image[1], "%s", val);
        else if (!strcmp(key, "anim"))   snprintf(cur->anim, sizeof cur->anim, "%s", val);
        else if (!strcmp(key, "cutin"))  snprintf(cur->cutin, sizeof cur->cutin, "%s", val);
        else if (!strcmp(key, "transport_by")) {
            char buf[128];
            snprintf(buf, sizeof buf, "%s", val);
            char *tok = strtok(buf, ",");
            while (tok && cur->n_transport_by < 4) {
                snprintf(cur->transport_by[cur->n_transport_by],
                         sizeof cur->transport_by[0], "%s", trim(tok));
                cur->n_transport_by++;
                tok = strtok(NULL, ",");
            }
        }
        else { set_err(err, errlen, path, ln, "不明なキー"); fclose(f); return -1; }
    }
    fclose(f);

    /* 未指定（AUTO）の項目を class から埋める。
     * こうしておくと既存の units.def は1行も変えずに従来どおり動き、
     * 現代戦以外のゲームでは .def で明示して上書きできる。 */
    for (int i = 0; i < g->n_types; i++) {
        UnitType *t = &g->types[i];
        if (t->upkeep == UPKEEP_AUTO)
            t->upkeep = (t->mclass == MC_AIR) ? 4 : 0;   /* 航空機だけ空中維持コスト */
        if (t->no_fuel == NOFUEL_AUTO) {
            if (t->mclass == MC_AIR)                     t->no_fuel = NOFUEL_DIE;
            else if (t->mclass == MC_SEA || t->mclass == MC_SUB)
                                                         t->no_fuel = NOFUEL_DAMAGE;
            else                                         t->no_fuel = NOFUEL_NONE;
        }
        if (t->move_se < 0) {
            /* SeId: 3=MOVE_FOOT 4=MOVE_VEHICLE 5=MOVE_AIR */
            if (t->mclass == MC_FOOT)     t->move_se = 3;
            else if (t->mclass == MC_AIR) t->move_se = 5;
            else                          t->move_se = 4;
        }
    }
    return 0;
}

int data_find_unit_type(const Game *g, const char *id)
{
    for (int i = 0; i < g->n_types; i++)
        if (!strcmp(g->types[i].id, id)) return i;
    return -1;
}

static int find_terrain_by_char(const Game *g, char c)
{
    for (int i = 0; i < g->n_terrains; i++)
        if (g->terrains[i].chr == c) return i;
    return -1;
}

/* ---- .map ---- */
int data_load_map(Game *g, const char *path, char *err, int errlen)
{
    FILE *f = fopen(path, "rb");
    if (!f) { set_err(err, errlen, path, 0, "開けません"); return -1; }

    g->w = g->h = 0;
    g->n_units = 0;
    g->turn_limit = 0;
    g->timeout_winner = -1;
    g->income_scale = 100;
    g->objective_count = 0;
    /* 天候は既定で有効・確率60/30/10。マップ側で weather=0 なら無効化できる */
    g->weather_on = 1;
    g->wx_pct[WX_CLEAR] = 60;
    g->wx_pct[WX_CLOUDY] = 30;
    g->wx_pct[WX_RAIN] = 10;
    g->objective_player = 0;
    g->map_name[0] = '\0';
    memset(g->tiles, 0, sizeof g->tiles);
    memset(g->lost_units, 0, sizeof g->lost_units);
    for (int y = 0; y < MAX_MAP_H; y++)
        for (int x = 0; x < MAX_MAP_W; x++) {
            g->tiles[y][x].owner = -1;
            g->tiles[y][x].capturer = -1;
            g->tiles[y][x].cap_hp = CAPTURE_HP;
        }

    enum { S_NONE, S_MAP, S_TERRAIN, S_OWNERS, S_UNITS } sec = S_NONE;
    int row = 0;
    char line[LINE_MAX_LEN];
    int ln = 0;

    while (fgets(line, sizeof line, f)) {
        ln++;
        char *key, *val;
        int kind = parser_split_line(line, &key, &val);
        if (kind == 0) continue;
        if (kind == 2) {
            if      (!strcmp(key, "map"))     sec = S_MAP;
            else if (!strcmp(key, "terrain")) sec = S_TERRAIN;
            else if (!strcmp(key, "owners"))  sec = S_OWNERS;
            else if (!strcmp(key, "units"))   sec = S_UNITS;
            else { set_err(err, errlen, path, ln, "不明なセクション"); fclose(f); return -1; }
            continue;
        }
        switch (sec) {
        case S_MAP:
            if      (!strcmp(key, "name"))   snprintf(g->map_name, sizeof g->map_name, "%s", val);
            else if (!strcmp(key, "width"))  g->w = atoi(val);
            else if (!strcmp(key, "height")) g->h = atoi(val);
            else if (!strcmp(key, "turns"))  g->turn_limit = atoi(val);
            else if (!strcmp(key, "timeout_winner")) g->timeout_winner = atoi(val);
            else if (!strcmp(key, "funds0")) g->funds[0] = atoi(val);
            else if (!strcmp(key, "funds1")) g->funds[1] = atoi(val);
            else if (!strcmp(key, "income_scale")) g->income_scale = atoi(val);
            else if (!strcmp(key, "weather"))        g->weather_on = (uint8_t)(atoi(val) != 0);
            else if (!strcmp(key, "weather_clear"))  g->wx_pct[WX_CLEAR]  = (int16_t)atoi(val);
            else if (!strcmp(key, "weather_cloudy")) g->wx_pct[WX_CLOUDY] = (int16_t)atoi(val);
            else if (!strcmp(key, "weather_rain"))   g->wx_pct[WX_RAIN]   = (int16_t)atoi(val);
            else if (!strcmp(key, "objective_count"))  g->objective_count = atoi(val);
            else if (!strcmp(key, "objective_player")) g->objective_player = atoi(val);
            else { set_err(err, errlen, path, ln, "不明なキー"); fclose(f); return -1; }
            if (g->w > MAX_MAP_W || g->h > MAX_MAP_H) {
                set_err(err, errlen, path, ln, "マップサイズ上限超過");
                fclose(f); return -1;
            }
            break;
        case S_TERRAIN:
            if (!strcmp(key, "row")) {
                if (row >= g->h) { set_err(err, errlen, path, ln, "行が多すぎます"); fclose(f); return -1; }
                int len = (int)strlen(val);
                if (len != g->w) { set_err(err, errlen, path, ln, "行の長さが width と不一致"); fclose(f); return -1; }
                for (int x = 0; x < g->w; x++) {
                    int t = find_terrain_by_char(g, val[x]);
                    if (t < 0) { set_err(err, errlen, path, ln, "未定義の地形文字"); fclose(f); return -1; }
                    g->tiles[row][x].terrain = (uint8_t)t;
                }
                row++;
            }
            break;
        case S_OWNERS:
            if (!strcmp(key, "own")) {
                int x, y, o;
                if (sscanf(val, "%d,%d,%d", &x, &y, &o) != 3 ||
                    x < 0 || y < 0 || x >= g->w || y >= g->h) {
                    set_err(err, errlen, path, ln, "own の書式エラー");
                    fclose(f); return -1;
                }
                g->tiles[y][x].owner = (int8_t)o;
            }
            break;
        case S_UNITS:
            if (!strcmp(key, "unit")) {
                int o, x, y, hp = 10;
                char tid[24];
                int nf = sscanf(val, "%d,%23[^,],%d,%d,%d", &o, tid, &x, &y, &hp);
                if (nf < 4 || x < 0 || y < 0 || x >= g->w || y >= g->h) {
                    set_err(err, errlen, path, ln, "unit の書式エラー");
                    fclose(f); return -1;
                }
                int t = data_find_unit_type(g, tid);
                if (t < 0) { set_err(err, errlen, path, ln, "未定義のユニットID"); fclose(f); return -1; }
                if (game_spawn_unit(g, o, t, x, y, hp) < 0) {
                    set_err(err, errlen, path, ln, "ユニット数上限超過");
                    fclose(f); return -1;
                }
            }
            break;
        default:
            set_err(err, errlen, path, ln, "セクション外のキー");
            fclose(f); return -1;
        }
    }
    fclose(f);
    if (row != g->h) {
        set_err(err, errlen, path, 0, "terrain 行数が height と不一致");
        return -1;
    }
    /* 安全網: 進入不可地形に配置されたユニット（例: 陸上の艦船）を最寄りの適地へ寄せる */
    game_fixup_unit_terrain(g);
    return 0;
}

/* ---- maplist.txt ---- */
/* ---- commanders.def ---- */
int data_find_commander(const Game *g, const char *id)
{
    for (int i = 0; i < g->n_cos; i++)
        if (!strcmp(g->cos[i].id, id)) return i;
    return -1;
}

int data_load_commanders(Game *g, const char *path, char *err, int errlen)
{
    g->n_cos = 0;
    FILE *f = fopen(path, "rb");
    if (!f) return 0;   /* 指揮官なしでも動く（任意ファイル扱い） */

    CommanderType *cur = NULL;
    char line[512];
    int ln = 0;
    while (fgets(line, sizeof line, f)) {
        ln++;
        char *key, *val;
        int kind = parser_split_line(line, &key, &val);
        if (kind == 0) continue;
        if (kind == 2) {
            if (strncmp(key, "commander ", 10) != 0) {
                set_err(err, errlen, path, ln, "不明なセクション");
                fclose(f); return -1;
            }
            if (g->n_cos >= MAX_COMMANDERS) {
                set_err(err, errlen, path, ln, "指揮官数の上限超過");
                fclose(f); return -1;
            }
            cur = &g->cos[g->n_cos++];
            memset(cur, 0, sizeof *cur);
            snprintf(cur->id, sizeof cur->id, "%s", key + 10);
            cur->power_cost = 100;
            continue;
        }
        if (!cur) { set_err(err, errlen, path, ln, "セクション外のキー");
                    fclose(f); return -1; }

        if      (!strcmp(key, "name"))  snprintf(cur->name, sizeof cur->name, "%s", val);
        else if (!strcmp(key, "title")) snprintf(cur->title, sizeof cur->title, "%s", val);
        else if (!strcmp(key, "desc"))  snprintf(cur->desc, sizeof cur->desc, "%s", val);
        else if (!strcmp(key, "domain")) {
            if      (!strcmp(val, "LAND")) cur->domain = CO_DOM_LAND;
            else if (!strcmp(val, "AIR"))  cur->domain = CO_DOM_AIR;
            else if (!strcmp(val, "SEA"))  cur->domain = CO_DOM_SEA;
            else                           cur->domain = CO_DOM_ALL;
        }
        else if (!strcmp(key, "atk_pct"))      cur->atk_pct = (int16_t)atoi(val);
        else if (!strcmp(key, "def_pct"))      cur->def_pct = (int16_t)atoi(val);
        else if (!strcmp(key, "move_bonus"))   cur->move_bonus = (int8_t)atoi(val);
        else if (!strcmp(key, "vision_bonus")) cur->vision_bonus = (int8_t)atoi(val);
        else if (!strcmp(key, "income_pct"))   cur->income_pct = (int16_t)atoi(val);
        else if (!strcmp(key, "power_name"))
            snprintf(cur->power_name, sizeof cur->power_name, "%s", val);
        else if (!strcmp(key, "power_desc"))
            snprintf(cur->power_desc, sizeof cur->power_desc, "%s", val);
        else if (!strcmp(key, "power_cost"))   cur->power_cost = (int16_t)atoi(val);
        else if (!strcmp(key, "unlock_clears")) cur->unlock_clears = (int16_t)atoi(val);
        else if (!strcmp(key, "power_val"))    cur->power_val = (int16_t)atoi(val);
        else if (!strcmp(key, "cutin"))        snprintf(cur->cutin, sizeof cur->cutin, "%s", val);
        else if (!strcmp(key, "power_type")) {
            if      (!strcmp(val, "HEAL"))   cur->power_type = CO_POW_HEAL;
            else if (!strcmp(val, "RUSH"))   cur->power_type = CO_POW_RUSH;
            else if (!strcmp(val, "STRIKE")) cur->power_type = CO_POW_STRIKE;
            else if (!strcmp(val, "FUNDS"))  cur->power_type = CO_POW_FUNDS;
            else if (!strcmp(val, "SCOUT"))  cur->power_type = CO_POW_SCOUT;
            else { set_err(err, errlen, path, ln, "不明な power_type");
                   fclose(f); return -1; }
        }
        else { set_err(err, errlen, path, ln, "不明なキー");
               fclose(f); return -1; }
    }
    fclose(f);
    for (int i = 0; i < g->n_cos; i++)
        if (g->cos[i].power_cost <= 0) g->cos[i].power_cost = 100;
    return 0;
}

int data_load_maplist(MapList *ml, const char *path)
{
    ml->n = 0;
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    char line[LINE_MAX_LEN];
    while (fgets(line, sizeof line, f) && ml->n < MAX_MAPLIST) {
        char *hash = strchr(line, '#');
        if (hash) *hash = '\0';
        char *s = trim(line);
        if (!*s) continue;
        char *bar = strchr(s, '|');
        if (!bar) continue;
        *bar = '\0';
        snprintf(ml->file[ml->n], sizeof ml->file[0], "%s", trim(s));
        snprintf(ml->name[ml->n], sizeof ml->name[0], "%s", trim(bar + 1));
        ml->n++;
    }
    fclose(f);
    return ml->n > 0 ? 0 : -1;
}
