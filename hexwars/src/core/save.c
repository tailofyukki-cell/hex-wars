/* save.c - 独自バイナリ形式のセーブ/ロード
 * ヘッダ: magic"HXWS" / version / payload長 / CRC32
 * 構造体を直接 fwrite せず、フィールド毎に little-endian で書く（仕様書 9章） */
#include "save.h"
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#endif

/* ------------------------------------------------------------------ */
/* CRC32 (IEEE 802.3, ビット毎)                                        */
/* ------------------------------------------------------------------ */
static uint32_t crc32_calc(const uint8_t *data, size_t len)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

/* ------------------------------------------------------------------ */
/* シリアライズバッファ                                                */
/* ------------------------------------------------------------------ */
#define SAVE_BUF_MAX (256 * 1024)
static uint8_t s_buf[SAVE_BUF_MAX];

typedef struct { uint8_t *p; size_t pos, cap; int overflow; } Wb;
typedef struct { const uint8_t *p; size_t pos, len; int underflow; } Rb;

static void w_u8(Wb *w, uint8_t v)
{
    if (w->pos + 1 > w->cap) { w->overflow = 1; return; }
    w->p[w->pos++] = v;
}
static void w_u16(Wb *w, uint16_t v) { w_u8(w, (uint8_t)v); w_u8(w, (uint8_t)(v >> 8)); }
static void w_u32(Wb *w, uint32_t v) { w_u16(w, (uint16_t)v); w_u16(w, (uint16_t)(v >> 16)); }
static void w_i8(Wb *w, int8_t v)    { w_u8(w, (uint8_t)v); }
static void w_i16(Wb *w, int16_t v)  { w_u16(w, (uint16_t)v); }
static void w_i32(Wb *w, int32_t v)  { w_u32(w, (uint32_t)v); }
static void w_str(Wb *w, const char *s, size_t n)
{
    for (size_t i = 0; i < n; i++)
        w_u8(w, (uint8_t)s[i]);
}

static uint8_t r_u8(Rb *r)
{
    if (r->pos + 1 > r->len) { r->underflow = 1; return 0; }
    return r->p[r->pos++];
}
static uint16_t r_u16(Rb *r) { uint16_t v = r_u8(r); return (uint16_t)(v | ((uint16_t)r_u8(r) << 8)); }
static uint32_t r_u32(Rb *r) { uint32_t v = r_u16(r); return v | ((uint32_t)r_u16(r) << 16); }
static int8_t  r_i8(Rb *r)   { return (int8_t)r_u8(r); }
static int16_t r_i16(Rb *r)  { return (int16_t)r_u16(r); }
static int32_t r_i32(Rb *r)  { return (int32_t)r_u32(r); }
static void r_str(Rb *r, char *s, size_t n)
{
    for (size_t i = 0; i < n; i++)
        s[i] = (char)r_u8(r);
    s[n - 1] = '\0';
}

/* ------------------------------------------------------------------ */
static void serialize(const Game *g, const CampaignState *cs, Wb *w)
{
    /* マップ */
    w_u16(w, (uint16_t)g->w);
    w_u16(w, (uint16_t)g->h);
    w_str(w, g->map_name, sizeof g->map_name);
    w_i32(w, g->turn);          /* peek 用に先頭側へ */
    w_i32(w, g->turn_limit);
    w_i32(w, g->timeout_winner);
    w_i32(w, g->income_scale);
    w_i32(w, g->objective_count);
    w_i32(w, g->objective_player);
    for (int y = 0; y < g->h; y++)
        for (int x = 0; x < g->w; x++) {
            const Tile *t = &g->tiles[y][x];
            w_u8(w, t->terrain);
            w_i8(w, t->owner);
            w_u8(w, t->cap_hp);
            w_i16(w, t->capturer);
        }
    /* ユニット */
    w_u16(w, (uint16_t)g->n_units);
    for (int i = 0; i < g->n_units; i++) {
        const Unit *u = &g->units[i];
        w_u8(w, u->type);
        w_u8(w, u->owner);
        w_u8(w, u->pos.x);
        w_u8(w, u->pos.y);
        w_i8(w, u->hp);
        w_u8(w, u->fuel);
        w_u8(w, u->ammo);
        w_u8(w, u->exp);
        w_u8(w, u->flags);
        for (int k = 0; k < MAX_CARGO; k++)
            w_i16(w, u->cargo[k]);
    }
    /* 進行 */
    w_i32(w, g->funds[0]);
    w_i32(w, g->funds[1]);
    w_u8(w, (uint8_t)g->current);
    w_i32(w, g->winner);
    w_u32(w, g->rng.s);
    w_u8(w, g->fog ? 1 : 0);
    w_u8(w, g->ctrl[0]);
    w_u8(w, g->ctrl[1]);
    w_i32(w, g->lost_units[0]);
    w_i32(w, g->lost_units[1]);
    /* 天候（v5） */
    w_u8(w, g->weather);
    w_u8(w, g->weather_next);
    w_i8(w, g->weather_left);
    w_u8(w, g->weather_on);
    for (int i = 0; i < WX_COUNT; i++) w_i16(w, g->wx_pct[i]);
    w_u8(w, g->night_on);            /* v9 以降 */
    /* v10: 3陣営目以降。陣営0/1 は上で既に書いているので続きだけ。
     * この位置に入れるのは、下の指揮官ループの長さ自体が
     * 版によって変わるため（旧版は2、v10は MAX_PLAYERS）。 */
    for (int p = 2; p < MAX_PLAYERS; p++) {
        w_i32(w, g->funds[p]);
        w_u8(w, g->ctrl[p]);
        w_i32(w, g->lost_units[p]);
    }
    for (int p = 0; p < MAX_PLAYERS; p++) w_u8(w, g->in_play[p]);
    /* 指揮官（v4） */
    for (int p = 0; p < MAX_PLAYERS; p++) {
        w_u8(w, (uint8_t)(int8_t)g->co_id[p]);
        w_i32(w, g->co_gauge[p]);
        w_u8(w, (uint8_t)g->co_power_turns[p]);
    }
    /* キャンペーン */
    w_u8(w, (cs && cs->active) ? 1 : 0);
    if (cs && cs->active) {
        w_str(w, cs->file, sizeof cs->file);
        w_str(w, cs->node, sizeof cs->node);
        w_i32(w, cs->funds_carry);
        w_u32(w, cs->cleared);
        w_u8(w, (uint8_t)cs->n_carry);
        for (int i = 0; i < cs->n_carry; i++) {
            w_u8(w, cs->carry[i].type);
            w_u8(w, cs->carry[i].exp);
        }
        w_u8(w, (uint8_t)(int8_t)cs->player_co);   /* 指揮官（v4） */
        for (int i = 0; i < MAX_CAMPAIGN_MAPS; i++) w_u8(w, cs->rank[i]); /* v6 */
        /* 倉庫（v3） */
        w_u8(w, (uint8_t)cs->n_store);
        for (int i = 0; i < cs->n_store; i++) {
            w_u8(w, cs->store[i].type);
            w_u8(w, cs->store[i].exp);
        }
    }
    /* --- ここから先は v7 で追加した欄 ---
     * 古いセーブには存在しないので必ず末尾に足すこと（途中に入れると
     * 旧版データの読み位置がずれて丸ごと読めなくなる）。 */
    /* マップイベント（発火済みビットも保存。ロード後に再発火させないため） */
    w_u16(w, (uint16_t)g->n_events);
    w_u32(w, g->events_fired);
    for (int i = 0; i < g->n_events; i++) {
        const MapEvent *e = &g->events[i];
        w_u8(w, e->cond);
        w_i16(w, e->c1); w_i16(w, e->c2); w_i16(w, e->c3); w_i16(w, e->c4);
        w_u8(w, e->act);
        w_i16(w, e->a1); w_i16(w, e->a2); w_i16(w, e->a3);
        w_i16(w, e->a4); w_i16(w, e->a5);
        w_str(w, e->msg, sizeof e->msg);
    }

}

static int deserialize(Game *g, CampaignState *cs, Rb *r, uint32_t ver)
{
    g->w = r_u16(r);
    g->h = r_u16(r);
    if (g->w > MAX_MAP_W || g->h > MAX_MAP_H) return -1;
    r_str(r, g->map_name, sizeof g->map_name);
    g->turn = r_i32(r);
    g->turn_limit = r_i32(r);
    g->timeout_winner = r_i32(r);
    g->income_scale = r_i32(r);
    g->objective_count = r_i32(r);
    g->objective_player = r_i32(r);
    memset(g->tiles, 0, sizeof g->tiles);
    for (int y = 0; y < g->h; y++)
        for (int x = 0; x < g->w; x++) {
            Tile *t = &g->tiles[y][x];
            t->terrain = r_u8(r);
            t->owner = r_i8(r);
            t->cap_hp = r_u8(r);
            t->capturer = r_i16(r);
            if (t->terrain >= g->n_terrains) return -1;
        }
    g->n_units = r_u16(r);
    if (g->n_units > MAX_UNITS) return -1;
    memset(g->units, 0, sizeof g->units);
    for (int i = 0; i < g->n_units; i++) {
        Unit *u = &g->units[i];
        u->type = r_u8(r);
        u->owner = r_u8(r);
        u->pos.x = r_u8(r);
        u->pos.y = r_u8(r);
        u->hp = r_i8(r);
        u->fuel = r_u8(r);
        u->ammo = r_u8(r);
        u->exp = r_u8(r);
        u->flags = r_u8(r);
        /* v8 で搭載枠が 2→4 になった。v7 以前は2枠しか書かれていないので、
         * 残りを空にして読み込む（古いセーブもそのまま開ける）。 */
        int slots = (ver >= 8) ? MAX_CARGO : 2;
        for (int k = 0; k < MAX_CARGO; k++)
            u->cargo[k] = (k < slots) ? r_i16(r) : -1;
        if (u->type >= g->n_types) return -1;
    }
    g->funds[0] = r_i32(r);
    g->funds[1] = r_i32(r);
    g->current = r_u8(r);
    g->winner = r_i32(r);
    g->rng.s = r_u32(r);
    g->fog = r_u8(r) != 0;
    g->ctrl[0] = r_u8(r);
    g->ctrl[1] = r_u8(r);
    g->lost_units[0] = r_i32(r);
    g->lost_units[1] = r_i32(r);
    /* 天候（v5） */
    g->weather = r_u8(r);
    g->weather_next = r_u8(r);
    g->weather_left = r_i8(r);
    g->weather_on = r_u8(r);
    for (int i = 0; i < WX_COUNT; i++) g->wx_pct[i] = r_i16(r);
    /* v9 で昼夜を追加。古いセーブはマップの既定（有効）のまま読む */
    if (ver >= 9) g->night_on = r_u8(r);
    if (ver >= 10) {
        for (int p = 2; p < MAX_PLAYERS; p++) {
            g->funds[p] = r_i32(r);
            g->ctrl[p] = r_u8(r);
            g->lost_units[p] = r_i32(r);
        }
        for (int p = 0; p < MAX_PLAYERS; p++) g->in_play[p] = r_u8(r);
    }
    /* 指揮官（v4）。**旧版は2陣営分しか書いていない**ので長さを分ける。
     * ここを間違えると以降の読み取りがすべてずれる。 */
    int n_co = (ver >= 10) ? MAX_PLAYERS : 2;
    for (int p = 0; p < n_co; p++) {
        g->co_id[p] = (int8_t)r_u8(r);
        g->co_gauge[p] = (int16_t)r_i32(r);
        g->co_power_turns[p] = (int8_t)r_u8(r);
    }
    if (cs) {
        memset(cs, 0, sizeof *cs);
        cs->active = r_u8(r) != 0;
        if (cs->active) {
            r_str(r, cs->file, sizeof cs->file);
            r_str(r, cs->node, sizeof cs->node);
            cs->funds_carry = r_i32(r);
            cs->cleared = r_u32(r);
            cs->n_carry = r_u8(r);
            if (cs->n_carry > MAX_CARRY_UNITS) return -1;
            for (int i = 0; i < cs->n_carry; i++) {
                cs->carry[i].type = r_u8(r);
                cs->carry[i].exp = r_u8(r);
            }
            cs->player_co = (int8_t)r_u8(r);       /* 指揮官（v4） */
            for (int i = 0; i < MAX_CAMPAIGN_MAPS; i++) cs->rank[i] = r_u8(r); /* v6 */
            /* 倉庫（v3） */
            cs->n_store = r_u8(r);
            if (cs->n_store > MAX_STORE_UNITS) return -1;
            for (int i = 0; i < cs->n_store; i++) {
                cs->store[i].type = r_u8(r);
                cs->store[i].exp = r_u8(r);
            }
        }
    }

    /* --- v7 で追加した欄（末尾に置くこと。理由は serialize 側のコメント参照） ---
     * v6 以前のセーブにはこの欄が無いので、イベント無しとして読み込む
     * （進行中のキャンペーンを無駄にしないための下位互換）。 */
    g->n_events = 0;
    g->events_fired = 0;
    memset(g->events, 0, sizeof g->events);
    if (ver >= 7) {
        g->n_events = r_u16(r);
        if (g->n_events < 0 || g->n_events > MAX_EVENTS) return -1;
        g->events_fired = r_u32(r);
        for (int i = 0; i < g->n_events; i++) {
            MapEvent *e = &g->events[i];
            e->cond = r_u8(r);
            e->c1 = r_i16(r); e->c2 = r_i16(r);
            e->c3 = r_i16(r); e->c4 = r_i16(r);
            e->act = r_u8(r);
            e->a1 = r_i16(r); e->a2 = r_i16(r); e->a3 = r_i16(r);
            e->a4 = r_i16(r); e->a5 = r_i16(r);
            r_str(r, e->msg, sizeof e->msg);
        }
    }

    return r->underflow ? -1 : 0;
}

/* ------------------------------------------------------------------ */
int save_game(const Game *g, const CampaignState *cs, const char *path,
              char *err, int errlen)
{
    Wb w = { s_buf, 0, sizeof s_buf, 0 };
    serialize(g, cs, &w);
    if (w.overflow) {
        if (err) snprintf(err, (size_t)errlen, "セーブデータが大きすぎます");
        return -1;
    }

    FILE *f = fopen(path, "wb");
    if (!f) {
        if (err) snprintf(err, (size_t)errlen, "%s: 書き込めません", path);
        return -1;
    }
    uint8_t hdr[16];
    Wb hw = { hdr, 0, sizeof hdr, 0 };
    w_str(&hw, SAVE_MAGIC, 4);
    w_u32(&hw, SAVE_VERSION);
    w_u32(&hw, (uint32_t)w.pos);
    w_u32(&hw, crc32_calc(s_buf, w.pos));

    int ok = fwrite(hdr, 1, sizeof hdr, f) == sizeof hdr &&
             fwrite(s_buf, 1, w.pos, f) == w.pos;
    fclose(f);
    if (!ok) {
        if (err) snprintf(err, (size_t)errlen, "%s: 書き込み失敗", path);
        return -1;
    }
    return 0;
}

static int read_header(FILE *f, uint32_t *len, uint32_t *crc, uint32_t *out_ver,
                       char *err, int errlen, const char *path)
{
    uint8_t hdr[16];
    if (fread(hdr, 1, sizeof hdr, f) != sizeof hdr) {
        if (err) snprintf(err, (size_t)errlen, "%s: ヘッダ読込失敗", path);
        return -1;
    }
    if (memcmp(hdr, SAVE_MAGIC, 4) != 0) {
        if (err) snprintf(err, (size_t)errlen, "%s: セーブデータではありません", path);
        return -1;
    }
    Rb r = { hdr + 4, 0, 12, 0 };
    uint32_t ver = r_u32(&r);
    *len = r_u32(&r);
    *crc = r_u32(&r);
    if (ver < SAVE_VERSION_MIN || ver > SAVE_VERSION) {
        if (err) snprintf(err, (size_t)errlen, "%s: バージョン不一致 (v%u)", path, ver);
        return -1;
    }
    if (out_ver) *out_ver = ver;
    if (*len > SAVE_BUF_MAX) {
        if (err) snprintf(err, (size_t)errlen, "%s: データ長が不正", path);
        return -1;
    }
    return 0;
}

int load_game(Game *g, CampaignState *cs, const char *path,
              char *err, int errlen)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        if (err) snprintf(err, (size_t)errlen, "%s: 開けません", path);
        return -1;
    }
    uint32_t len, crc, ver = SAVE_VERSION;
    if (read_header(f, &len, &crc, &ver, err, errlen, path) != 0) {
        fclose(f);
        return -1;
    }
    if (fread(s_buf, 1, len, f) != len) {
        if (err) snprintf(err, (size_t)errlen, "%s: 本体読込失敗", path);
        fclose(f);
        return -1;
    }
    fclose(f);
    if (crc32_calc(s_buf, len) != crc) {
        if (err) snprintf(err, (size_t)errlen, "%s: チェックサム不一致（破損）", path);
        return -1;
    }
    Rb r = { s_buf, 0, len, 0 };
    if (deserialize(g, cs, &r, ver) != 0) {
        if (err) snprintf(err, (size_t)errlen, "%s: データが不正です", path);
        return -1;
    }
    /* v9以前は参加陣営を持たないので盤面から算出し直す。
     * これを忘れると全陣営が「不参加」になり、ロード直後に引き分けになる。 */
    if (ver < 10) game_recompute_in_play(g);
    game_update_vision(g);
    return 0;
}

int save_peek(const char *path, char *map_name, int name_len, int *turn)
{
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    uint32_t len, crc;
    if (read_header(f, &len, &crc, NULL, NULL, 0, path) != 0) {
        fclose(f);
        return -1;
    }
    /* 先頭フィールド（w,h,map_name,turn）だけ読む */
    uint8_t head[4 + 64 + 4];
    if (fread(head, 1, sizeof head, f) != sizeof head) {
        fclose(f);
        return -1;
    }
    fclose(f);
    Rb r = { head, 0, sizeof head, 0 };
    (void)r_u16(&r);
    (void)r_u16(&r);
    char name[64];
    r_str(&r, name, sizeof name);
    if (map_name) snprintf(map_name, (size_t)name_len, "%s", name);
    if (turn) *turn = r_i32(&r);
    return 0;
}

void save_ensure_dir(const char *dir)
{
#ifdef _WIN32
    _mkdir(dir);
#else
    mkdir(dir, 0755);
#endif
}
