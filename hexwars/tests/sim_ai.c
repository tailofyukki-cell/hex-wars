/* sim_ai.c - CPU同士の自動対戦シミュレーション（ヘッドレス統合テスト） */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "../src/core/game.h"
#include "../src/core/ai.h"
#include "../src/core/campaign.h"
#include "../src/data/parser.h"

static Game s_game;
static AiState s_ai;

static int run_match_co(const char *map, uint32_t seed, int ctrl0, int ctrl1,
                        const char *co0, const char *co1);

static int run_match(const char *map, uint32_t seed, int ctrl0, int ctrl1)
{
    return run_match_co(map, seed, ctrl0, ctrl1, NULL, NULL);
}

/* 環境変数 HWSIM で回すマップを絞る（部分一致）。
 * 1枚だけ調整したいときに全部回すと十分以上かかるため。
 *   例) HWSIM=f01 sim_ai.exe */
static int sim_skip(const char *map)
{
    const char *only = getenv("HWSIM");
    return only && *only && !strstr(map, only);
}

/* 多陣営の乱戦。参加している陣営をすべてCPUにして回す。 */
static int run_match_ffa(const char *map, uint32_t seed, int ctrl)
{
    if (sim_skip(map)) return 0;
    Game *g = &s_game;
    memset(g, 0, sizeof *g);
    char err[256];
    if (data_load_terrain(g, "data/terrain.def", err, sizeof err) != 0 ||
        data_load_units(g, "data/units.def", err, sizeof err) != 0 ||
        data_load_commanders(g, "data/commanders.def", err, sizeof err) != 0 ||
        data_load_map(g, map, err, sizeof err) != 0) {
        printf("LOAD ERROR: %s\n", err);
        return -100;
    }
    g->fog = true;
    for (int p = 0; p < MAX_PLAYERS; p++) {
        g->co_id[p] = -1;
        g->ctrl[p] = (uint8_t)ctrl;
    }
    game_start(g, seed);

    int n_play = 0;
    for (int p = 0; p < MAX_PLAYERS; p++) if (game_player_in_play(g, p)) n_play++;

    int guard = 0;
    while (g->winner == WINNER_NONE && guard < 300000) {
        ai_begin_turn(g, &s_ai);
        while (ai_step(g, &s_ai) && guard < 300000) guard++;
        guard++;
    }
    if (g->winner == WINNER_NONE) {
        printf("  打ち切り（無限ループ疑い） turn=%d\n", g->turn);
        return -100;
    }
    printf("  %s: seed=%u 参加%d陣営 winner=%d turn=%d 損失=",
           map, seed, n_play, g->winner, g->turn);
    for (int p = 0; p < MAX_PLAYERS; p++)
        if (game_player_in_play(g, p)) printf("%d ", g->lost_units[p]);
    printf("\n");
    return g->winner;
}

static int run_match_co(const char *map, uint32_t seed, int ctrl0, int ctrl1,
                        const char *co0, const char *co1)
{
    if (sim_skip(map)) return 0;
    Game *g = &s_game;
    memset(g, 0, sizeof *g);
    char err[256];
    if (data_load_terrain(g, "data/terrain.def", err, sizeof err) != 0 ||
        data_load_units(g, "data/units.def", err, sizeof err) != 0 ||
        data_load_commanders(g, "data/commanders.def", err, sizeof err) != 0 ||
        data_load_map(g, map, err, sizeof err) != 0) {
        printf("LOAD ERROR: %s\n", err);
        return -100;
    }
    /* 指揮官なし(-1)が既定。co0/co1 指定時のみ両軍に指揮官を付ける */
    g->co_id[0] = (int8_t)(co0 ? data_find_commander(g, co0) : -1);
    g->co_id[1] = (int8_t)(co1 ? data_find_commander(g, co1) : -1);
    g->fog = true;
    g->ctrl[0] = (uint8_t)ctrl0;
    g->ctrl[1] = (uint8_t)ctrl1;
    game_start(g, seed);

    int guard = 0;
    int amphib_turns = 0, max_loaded = 0, unloads = 0, transports_made = 0;
    int co_fires[2] = {0, 0};
    int ev_fired = 0;
    int prev_loaded = 0;
    while (g->winner == WINNER_NONE && guard < 200000) {
        int side = g->current;
        ev_fired += game_check_events(g, NULL, 0);   /* マップイベント（増援など） */
        ai_begin_turn(g, &s_ai);
        if (s_ai.amphib) amphib_turns++;
        while (ai_step(g, &s_ai) && guard < 200000)
            guard++;
        guard++;
        if (s_ai.co_used && side >= 0 && side < 2) co_fires[side]++;
        int loaded = 0, tr = 0;
        for (int i = 0; i < g->n_units; i++) {
            const Unit *u = &g->units[i];
            if (!(u->flags & UF_ALIVE)) continue;
            if (u->flags & UF_LOADED) loaded++;
            if (g->types[u->type].capacity > 0 &&
                (g->types[u->type].mclass == MC_SEA)) tr++;
        }
        if (loaded > max_loaded) max_loaded = loaded;
        if (loaded < prev_loaded) unloads += (prev_loaded - loaded);
        prev_loaded = loaded;
        if (tr > transports_made) transports_made = tr;
    }
    /* 上陸作戦が発動したマップだけ、その活動量を出す（AI改良時の指標） */
    if (amphib_turns > 0)
        printf("    [上陸] 判定%dターン 輸送%d隻 積載最大%d 揚陸%d回\n",
               amphib_turns, transports_made, max_loaded, unloads);
    if (g->winner == WINNER_NONE) {
        printf("  打ち切り（無限ループ疑い） turn=%d\n", g->turn);
        return -100;
    }
    printf("  %s: seed=%u winner=%d turn=%d 損失=%d/%d",
           map, seed, g->winner, g->turn,
           g->lost_units[0], g->lost_units[1]);
    if (co0 || co1) printf(" 必殺技=%d/%d", co_fires[0], co_fires[1]);
    if (ev_fired > 0) printf(" イベント=%d", ev_fired);
    printf("\n");
    return g->winner;
}


/* キャンペーンのノードを実際に開戦して回し、マップイベントが発火するか見る。
 * sim の他の節は .map を直読みするのでイベントが載らず、ここでしか検証できない。 */
static int run_campaign_node(const Campaign *c, const char *node_id, uint32_t seed)
{
    Game *g = &s_game;
    char err[256];
    memset(g, 0, sizeof *g);
    if (data_load_terrain(g, "data/terrain.def", err, sizeof err) != 0 ||
        data_load_units(g, "data/units.def", err, sizeof err) != 0 ||
        data_load_commanders(g, "data/commanders.def", err, sizeof err) != 0) {
        printf("LOAD ERROR: %s\n", err);
        return -100;
    }
    CampaignState st;
    memset(&st, 0, sizeof st);
    snprintf(st.node, sizeof st.node, "%s", node_id);
    if (campaign_setup_map(g, c, &st, "", err, sizeof err) != 0) {
        printf("SETUP ERROR: %s\n", err);
        return -100;
    }
    int n_defined = g->n_events;
    campaign_begin(g, c, &st, seed, NULL);
    g->ctrl[0] = CTRL_CPU_NORMAL;
    g->ctrl[1] = CTRL_CPU_NORMAL;

    int guard = 0, fired = 0;
    while (g->winner == WINNER_NONE && guard < 200000) {
        fired += game_check_events(g, NULL, 0);
        ai_begin_turn(g, &s_ai);
        while (ai_step(g, &s_ai) && guard < 200000) guard++;
        guard++;
    }
    printf("  %-4s %-24s turn=%3d 勝者=%2d 損失=%3d/%3d イベント %d/%d 発火\n",
           node_id, g->map_name, g->turn, g->winner,
           g->lost_units[0], g->lost_units[1], fired, n_defined);
    return fired;
}

int main(void)
{
    int fail = 0;
    printf("== NORMAL vs NORMAL ==\n");
    for (uint32_t s = 1; s <= 3; s++)
        if (run_match("data/maps/m01_border_hills.map", s * 1000 + 7,
                      CTRL_CPU_NORMAL, CTRL_CPU_NORMAL) == -100) fail++;
    printf("== HARD vs EASY ==\n");
    if (run_match("data/maps/m01_border_hills.map", 4242,
                  CTRL_CPU_HARD, CTRL_CPU_EASY) == -100) fail++;
    printf("== m02（海空マップ） ==\n");
    for (uint32_t s = 1; s <= 2; s++)
        if (run_match("data/maps/m02_channel.map", s * 77 + 5,
                      CTRL_CPU_NORMAL, CTRL_CPU_NORMAL) == -100) fail++;
    printf("== m04（立体戦: 空・海面・海中が重なるマップ） ==\n");
    for (uint32_t s = 1; s <= 2; s++)
        if (run_match("data/maps/m04_layers.map", s * 211 + 9,
                      CTRL_CPU_NORMAL, CTRL_CPU_NORMAL) == -100) fail++;
    printf("== m03/c11/c12（海戦・上陸マップ） ==\n");
    for (uint32_t s = 1; s <= 2; s++)
        if (run_match("data/maps/m03_archipelago.map", s * 131 + 3,
                      CTRL_CPU_NORMAL, CTRL_CPU_NORMAL) == -100) fail++;
    if (run_match("data/maps/c11_seacontrol.map", 137, CTRL_CPU_NORMAL, CTRL_CPU_NORMAL) == -100) fail++;
    if (run_match("data/maps/c12_dday.map", 149, CTRL_CPU_NORMAL, CTRL_CPU_HARD) == -100) fail++;
    printf("== 指揮官あり（必殺技をAIが使うか） ==\n");
    /* 技種別ごとに1戦ずつ。行末の「必殺技=P0回数/P1回数」が0だと撃てていない */
    if (run_match_co("data/maps/m01_border_hills.map", 5101,
                     CTRL_CPU_NORMAL, CTRL_CPU_NORMAL, "GRAF", "LIESE") == -100) fail++;
    if (run_match_co("data/maps/m01_border_hills.map", 5102,
                     CTRL_CPU_NORMAL, CTRL_CPU_NORMAL, "BALT", "KARLA") == -100) fail++;
    if (run_match_co("data/maps/c09_cities.map", 5103,
                     CTRL_CPU_NORMAL, CTRL_CPU_NORMAL, "GRAF", "BALT") == -100) fail++;
    /* 後から追加した技も CPU が撃てることを見る（回数 0 なら発動条件が厳すぎ） */
    if (run_match_co("data/maps/c13_delta.map", 5104,
                     CTRL_CPU_NORMAL, CTRL_CPU_NORMAL, "WOLF", "HERTA") == -100) fail++;
    if (run_match_co("data/maps/m01_border_hills.map", 5105,
                     CTRL_CPU_NORMAL, CTRL_CPU_NORMAL, "DIETER", "NOEL") == -100) fail++;
    if (run_match_co("data/maps/c09_cities.map", 5106,
                     CTRL_CPU_NORMAL, CTRL_CPU_NORMAL, "EAGLE", "GRAF") == -100) fail++;

    printf("== フリー対戦専用マップ ==\n");
    /* 陸だけ / 海だけ / 空だけは「占領で盤面が動く」という AI の前提が
     * 崩れるので、止まらない・落ちないことを見ておく。 */
    if (run_match("data/maps/f02_defile.map", 201,
                  CTRL_CPU_NORMAL, CTRL_CPU_NORMAL) == -100) fail++;
    if (run_match("data/maps/f03_wolfpack.map", 202,
                  CTRL_CPU_NORMAL, CTRL_CPU_NORMAL) == -100) fail++;
    if (run_match("data/maps/f04_skyline.map", 203,
                  CTRL_CPU_NORMAL, CTRL_CPU_NORMAL) == -100) fail++;
    if (run_match("data/maps/f05_endlessnight.map", 204,
                  CTRL_CPU_NORMAL, CTRL_CPU_NORMAL) == -100) fail++;
    if (run_match("data/maps/f06_blitz.map", 205,
                  CTRL_CPU_NORMAL, CTRL_CPU_NORMAL) == -100) fail++;
    for (int s5 = 0; s5 < 5; s5++)
        if (run_match_ffa("data/maps/f01_lastStand.map", 210 + (uint32_t)s5,
                          CTRL_CPU_NORMAL) == -100) fail++;

    printf("== 多陣営の乱戦 ==\n");
    for (int s3 = 0; s3 < 3; s3++)
        if (run_match_ffa("data/maps/m05_threeway.map", 900 + (uint32_t)s3,
                          CTRL_CPU_NORMAL) == -100) fail++;
    /* 完全対称なので、偏りが出れば手番順かAIの振る舞いが原因。
     * 2シードだと先手有利と区別できないので多めに回す。 */
    for (int s3 = 0; s3 < 6; s3++)
        if (run_match_ffa("data/maps/m06_alliance.map", 950 + (uint32_t)s3,
                          CTRL_CPU_NORMAL) == -100) fail++;

    printf("== キャンペーン実戦（マップイベントの発火） ==\n");
    {
        Campaign cc;
        char e2[256];
        if (campaign_load(&cc, "data/campaign/main.cpn", e2, sizeof e2) != 0) {
            printf("  CPN LOAD ERROR: %s\n", e2);
            fail++;
        } else {
            /* N3/N4 は後から追加したイベント種別（TERRAIN/COPOWER/WEATHER）を
             * 使っているので、実戦で発火するか見ておく */
            /* M11(2対2のチーム戦) と M12(三つ巴) は多陣営を
             * キャンペーンに組み込んだノード。決着するかを見ておく。 */
            const char *nodes[] = { "M01", "M05", "M09", "M11", "M12",
                                    "N3", "N4", "M10" };
            int n_nodes = (int)(sizeof nodes / sizeof nodes[0]);
            for (int i = 0; i < n_nodes; i++)
                if (run_campaign_node(&cc, nodes[i], 700 + (uint32_t)i) == -100) fail++;
        }
    }
    printf("== キャンペーンマップ ==\n");
    if (run_match("data/maps/c01_border.map", 11, CTRL_CPU_NORMAL, CTRL_CPU_EASY) == -100) fail++;
    if (run_match("data/maps/c02_forest.map", 22, CTRL_CPU_NORMAL, CTRL_CPU_NORMAL) == -100) fail++;
    if (run_match("data/maps/c03_channel.map", 33, CTRL_CPU_NORMAL, CTRL_CPU_NORMAL) == -100) fail++;
    if (run_match("data/maps/c04_capital.map", 44, CTRL_CPU_NORMAL, CTRL_CPU_HARD) == -100) fail++;
    if (run_match("data/maps/c05_river.map", 55, CTRL_CPU_NORMAL, CTRL_CPU_NORMAL) == -100) fail++;
    if (run_match("data/maps/c06_beachhead.map", 66, CTRL_CPU_NORMAL, CTRL_CPU_NORMAL) == -100) fail++;
    if (run_match("data/maps/c07_pass.map", 77, CTRL_CPU_NORMAL, CTRL_CPU_NORMAL) == -100) fail++;
    if (run_match("data/maps/c08_airfields.map", 88, CTRL_CPU_NORMAL, CTRL_CPU_NORMAL) == -100) fail++;
    if (run_match("data/maps/c09_cities.map", 99, CTRL_CPU_NORMAL, CTRL_CPU_NORMAL) == -100) fail++;
    if (run_match("data/maps/c10_plains.map", 110, CTRL_CPU_NORMAL, CTRL_CPU_NORMAL) == -100) fail++;
    if (run_match("data/maps/c13_delta.map", 130, CTRL_CPU_NORMAL, CTRL_CPU_NORMAL) == -100) fail++;
    if (run_match("data/maps/c14_bastion.map", 140, CTRL_CPU_NORMAL, CTRL_CPU_HARD) == -100) fail++;

    if (fail == 0) { printf("SIMULATION OK\n"); return 0; }
    printf("%d FAILURE(S)\n", fail);
    return 1;
}
