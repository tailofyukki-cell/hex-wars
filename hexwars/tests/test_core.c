/* test_core.c - core層の単体テスト（SDL不要。仕様書 2.3 / 13） */
#include <stdio.h>
#include <string.h>
#include "../src/core/hex.h"
#include "../src/core/game.h"
#include "../src/core/path.h"
#include "../src/core/rules.h"
#include "../src/core/save.h"
#include "../src/core/campaign.h"
#include "../src/core/ai.h"
#include "../src/data/parser.h"
#include "../src/ui/sound.h"   /* SeId（move_se の検証用） */

static int s_fail = 0;
#define CHECK(cond) do { \
    if (!(cond)) { \
        printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        s_fail++; \
    } \
} while (0)

static Game s_game;
static Game s_game2;   /* セーブ復元の比較用（Game は大きいので静的確保） */

static void test_hex(void)
{
    /* 距離: 同一点=0、隣接=1 */
    CHECK(hex_distance(3, 3, 3, 3) == 0);
    for (int d = 0; d < HEX_DIRS; d++) {
        int nx, ny;
        hex_neighbor(5, 4, d, &nx, &ny);
        CHECK(hex_distance(5, 4, nx, ny) == 1);
        hex_neighbor(5, 5, d, &nx, &ny); /* 奇数行 */
        CHECK(hex_distance(5, 5, nx, ny) == 1);
    }
    /* odd-r: 直線距離 */
    CHECK(hex_distance(0, 0, 5, 0) == 5);
    CHECK(hex_distance(0, 0, 0, 6) == 6);
    /* offset<->axial 往復 */
    for (int y = 0; y < 10; y++)
        for (int x = 0; x < 10; x++) {
            Cell c = hex_to_offset(hex_to_axial(x, y));
            CHECK(c.x == x && c.y == y);
        }
}

static void test_rng_deterministic(void)
{
    Rng r1, r2;
    rng_seed(&r1, 12345);
    rng_seed(&r2, 12345);
    for (int i = 0; i < 100; i++)
        CHECK(rng_next(&r1) == rng_next(&r2));
    rng_seed(&r1, 777);
    for (int i = 0; i < 1000; i++) {
        int v = rng_range(&r1, -15, 15);
        CHECK(v >= -15 && v <= 15);
    }
}

static void test_data_and_battle(void)
{
    Game *g = &s_game;
    memset(g, 0, sizeof *g);
    char err[256];
    CHECK(data_load_terrain(g, "data/terrain.def", err, sizeof err) == 0);
    if (s_fail) { printf("  %s\n", err); return; }
    CHECK(data_load_units(g, "data/units.def", err, sizeof err) == 0);
    if (s_fail) { printf("  %s\n", err); return; }
    CHECK(g->n_terrains == 17);   /* 地形12種 + 圏外 + 生産できない飛行場/泊地 + 瓦眦2種 */
    {   /* 野戦飛行場・泊地は「都市の空・海版」。
         * 補給（回復・進化）はできるが生産はできないことを固める。 */
        int as_ = -1, an_ = -1;
        for (int i = 0; i < g->n_terrains; i++) {
            if (!strcmp(g->terrains[i].id, "AIRSTRIP"))  as_ = i;
            if (!strcmp(g->terrains[i].id, "ANCHORAGE")) an_ = i;
        }
        CHECK(as_ >= 0 && an_ >= 0);
        if (as_ >= 0 && an_ >= 0) {
            CHECK(g->terrains[as_].produces == PROD_NONE);
            CHECK(g->terrains[an_].produces == PROD_NONE);
            CHECK(g->terrains[as_].supplies & (1u << MC_AIR));
            CHECK(g->terrains[an_].supplies & (1u << MC_SEA));
            CHECK(g->terrains[as_].capturable && g->terrains[an_].capturable);
        }
    }
    /* 生産できる35種 + 進化先35種。進化先は no_produce なので生産に出ない。
     * 生産側の35には夜間ユニットの3種を含み、それらも進化先を持つ。 */
    CHECK(g->n_types == 70);
    {
        int producible = 0, evo = 0;
        for (int i = 0; i < g->n_types; i++) {
            if (g->types[i].no_produce) evo++;
            else producible++;
        }
        CHECK(producible == 35 && evo == 35);
        /* 進化先を持つ種は、その進化先が実在し、生産不可であること */
        for (int i = 0; i < g->n_types; i++) {
            if (!g->types[i].evolve_to[0]) continue;
            int to = -1;
            for (int k = 0; k < g->n_types; k++)
                if (!strcmp(g->types[k].id, g->types[i].evolve_to)) to = k;
            CHECK(to >= 0);
            if (to >= 0) {
                CHECK(g->types[to].no_produce == 1);
                /* 進化は片道1段（進化先はさらに進化しない） */
                CHECK(g->types[to].evolve_to[0] == 0);
                /* 移動クラスは変わらない（陸が空になったりしない） */
                CHECK(g->types[to].mclass == g->types[i].mclass);
                /* **夜間ユニットの進化先は night を引き継ぐこと**。
                 * 落とすと進化した途端に夜の+50%と視界維持を失い、
                 * 育てたほど弱くなるという気づきにくい退化になる。 */
                CHECK(g->types[to].night == g->types[i].night);
            }
        }
    }
    {
        int land = 0, air = 0, sea = 0;
        for (int i = 0; i < g->n_types; i++) {
            switch ((MoveClass)g->types[i].mclass) {
            case MC_AIR: air++; break;
            case MC_SEA: case MC_SUB: sea++; break;
            default: land++; break;
            }
        }
        /* 進化先も同じ内訳で増えるので倍になる。
         * 夜間ユニットは陸・空・海に1つずつ。 */
        CHECK(land == 32 && air == 18 && sea == 20);
    }
    /* 画像指定（image=）が両陣営分に読めていること */
    {
        int inf_t = data_find_unit_type(g, "INFANTRY");
        CHECK(inf_t >= 0);
        CHECK(strcmp(g->types[inf_t].image[0], "gfx/units/infantry.png") == 0);
        CHECK(strcmp(g->types[inf_t].image[1], "gfx/units/infantry.png") == 0);
    }

    CHECK(data_load_map(g, "data/maps/test_arena.map", err, sizeof err) == 0);
    if (s_fail) { printf("  %s\n", err); return; }
    CHECK(g->w == 32 && g->h == 22);
    CHECK(g->n_units == 10);

    game_start(g, 42);
    CHECK(g->winner == WINNER_NONE);
    CHECK(g->turn == 1 && g->current == 0);
    /* 初期収入: 首都300+都市100x4+工場150x2+空港100x2 = 1200 → 1700+1200 */
    CHECK(g->funds[0] == 2900);

    /* 戦闘ゴールデンテスト: 歩兵(HP10,平地) vs 歩兵(HP10,平地想定) */
    int inf = data_find_unit_type(g, "INFANTRY");
    int tank = data_find_unit_type(g, "TANK");
    CHECK(inf >= 0 && tank >= 0);
    int u1 = game_spawn_unit(g, 0, inf, 9, 4, 10);
    int u2 = game_spawn_unit(g, 1, inf, 10, 4, 10);
    CHECK(u1 >= 0 && u2 >= 0);
    /* 期待ダメージ: atk55 * hp10/10 = 55 → defmod100 → 55(x10) = 5.5HP */
    int e = battle_expect_damage_x10(g, &g->units[u1], &g->units[u2]);
    CHECK(e == 55);
    /* 対戦車兵→戦車: atk_hard60 */
    int u3 = game_spawn_unit(g, 1, tank, 11, 4, 10);
    int at_inf = data_find_unit_type(g, "AT_INFANTRY");
    int u4 = game_spawn_unit(g, 0, at_inf, 12, 4, 10);
    e = battle_expect_damage_x10(g, &g->units[u4], &g->units[u3]);
    CHECK(e == 60);
    /* 実ダメージは 1..10 の範囲 */
    for (int i = 0; i < 50; i++) {
        int d = battle_roll_damage(g, &g->units[u1], &g->units[u2]);
        CHECK(d >= 1 && d <= 10);
    }

    /* 戦闘予測（UI表示用）: 盤面を変えずに与ダメ・反撃・残HPを返す */
    {
        int dmg = -1, dhp = -1, cnt = -1, ahp = -1;
        int hp_before_a = g->units[u1].hp, hp_before_d = g->units[u2].hp;
        battle_forecast(g, u1, u2, &dmg, &dhp, &cnt, &ahp);
        CHECK(dmg >= 1 && dmg <= 10);
        CHECK(dhp == hp_before_d - dmg);           /* 残HPが整合 */
        CHECK(cnt >= 0 && ahp == hp_before_a - cnt);
        /* 予測しても盤面は一切変わらない */
        CHECK(g->units[u1].hp == hp_before_a && g->units[u2].hp == hp_before_d);
        CHECK(g->units[u1].ammo == g->types[inf].ammo);

        /* 間接攻撃（自走砲 range_min=2）は反撃を受けない */
        int arty_t = data_find_unit_type(g, "ARTILLERY");
        int ua = game_spawn_unit(g, 0, arty_t, 9, 6, 10);
        int ud = game_spawn_unit(g, 1, inf, 11, 6, 10);
        game_update_vision(g);
        int c2 = -1;
        battle_forecast(g, ua, ud, NULL, NULL, &c2, NULL);
        CHECK(c2 == 0);                            /* 間接には反撃なし */

        /* 撃破できる場合は残HP0・反撃0 */
        g->units[ud].hp = 1;
        int d3 = 0, dhp3 = -1, c3 = -1;
        battle_forecast(g, ua, ud, &d3, &dhp3, &c3, NULL);
        CHECK(dhp3 == 0 && c3 == 0);

        /* 表示する予測値が実際の乱数結果と整合するか（嘘を表示しない）。
         * 実ダメージは ±15% ぶれるので、200回の平均が予測の±1に収まること。 */
        g->units[ud].hp = 10;
        int fc = 0;
        battle_forecast(g, u1, u2, &fc, NULL, NULL, NULL);
        int sum = 0, lo = 99, hi = -1;
        for (int i = 0; i < 200; i++) {
            int d = battle_roll_damage(g, &g->units[u1], &g->units[u2]);
            sum += d;
            if (d < lo) lo = d;
            if (d > hi) hi = d;
        }
        int mean10 = sum * 10 / 200;               /* 平均×10 */
        CHECK(mean10 >= (fc - 1) * 10 && mean10 <= (fc + 1) * 10);
        CHECK(lo >= 1 && hi <= 10);
    }

    /* 攻撃で弾薬減・経験値増 */
    int ammo0 = g->units[u1].ammo;
    game_attack(g, u1, u2, NULL, NULL, NULL);
    CHECK(g->units[u1].ammo == ammo0 - 1);
    CHECK(g->units[u1].exp > 0);
}

static void test_move_range_zoc(void)
{
    Game *g = &s_game;
    memset(g, 0, sizeof *g);
    char err[256];
    CHECK(data_load_terrain(g, "data/terrain.def", err, sizeof err) == 0);
    CHECK(data_load_units(g, "data/units.def", err, sizeof err) == 0);
    CHECK(data_load_map(g, "data/maps/test_arena.map", err, sizeof err) == 0);
    g->fog = false;
    game_start(g, 1);

    /* 平地上の戦車: 移動5 → コスト10/2=5ヘクス到達 */
    int tank = data_find_unit_type(g, "TANK");
    g->n_units = 0;
    int u = game_spawn_unit(g, 0, tank, 9, 4, 10);
    MoveRange mr;
    path_move_range(g, u, &mr);
    CHECK(mr.cost[4][9] == 0);
    /* 平地直進で5ヘクス先まで（同じ行） */
    CHECK(mr.cost[4][14] == 10);
    CHECK(mr.cost[4][15] < 0);

    /* ZOC: 敵歩兵を隣接配置すると、そのZOCヘクスで停止する */
    int inf = data_find_unit_type(g, "INFANTRY");
    game_spawn_unit(g, 1, inf, 12, 4, 10);
    game_update_vision(g);
    path_move_range(g, u, &mr);
    /* (11,4) は敵(12,4)のZOC → そこから先(13,4等)へは抜けられない */
    CHECK(mr.cost[4][11] >= 0);
    CHECK(mr.cost[4][12] < 0);  /* 敵ユニット上は進入不可 */

    /* 山は装軌進入不可（新m01: (16,2)が山） */
    CHECK(rules_move_cost(g, MC_TRACK, 16, 2) == 0);
    /* 道路は0.5(=1)（新m01: (8,11)が道路） */
    CHECK(rules_move_cost(g, MC_TRACK, 8, 11) == 1);
}

static void test_capture(void)
{
    Game *g = &s_game;
    memset(g, 0, sizeof *g);
    char err[256];
    CHECK(data_load_terrain(g, "data/terrain.def", err, sizeof err) == 0);
    CHECK(data_load_units(g, "data/units.def", err, sizeof err) == 0);
    CHECK(data_load_map(g, "data/maps/test_arena.map", err, sizeof err) == 0);
    g->fog = false;
    game_start(g, 1);

    /* 中立都市(14,3)を歩兵で占領: HP10 → 20耐久なので2回 */
    int inf = data_find_unit_type(g, "INFANTRY");
    g->n_units = 0;
    int u = game_spawn_unit(g, 0, inf, 14, 3, 10);
    CHECK(g->terrains[g->tiles[3][14].terrain].capturable);
    CHECK(g->tiles[3][14].owner == -1);
    CHECK(game_capture(g, u) == 0);      /* 1回目: 20->10 */
    g->units[u].flags &= (uint8_t)~UF_DONE;
    CHECK(game_capture(g, u) == 1);      /* 2回目: 完了 */
    CHECK(g->tiles[3][14].owner == 0);

    /* 首都に工場機能: 自軍首都(ユニット不在)で陸ユニットを生産できる */
    {
        int hx = -1, hy = -1;
        for (int y = 0; y < g->h && hx < 0; y++)
            for (int x = 0; x < g->w && hx < 0; x++)
                if (g->terrains[g->tiles[y][x].terrain].is_hq &&
                    g->tiles[y][x].owner == 0) { hx = x; hy = y; }
        CHECK(hx >= 0);                              /* 自軍首都あり */
        int tank = data_find_unit_type(g, "TANK");
        CHECK(game_type_buildable_at(g, hx, hy, tank));   /* 陸ユニット生産可 */
        CHECK(game_can_produce_at(g, 0, hx, hy));         /* 不在なので生産可 */
        int fighter = data_find_unit_type(g, "FIGHTER");
        CHECK(!game_type_buildable_at(g, hx, hy, fighter)); /* 空ユニットは不可 */
    }
}

/* 立体化 L0: レイヤー導出と (セル,レイヤー) 別の占有クエリ */
static void test_layers(void)
{
    Game *g = &s_game;
    memset(g, 0, sizeof *g);
    char err[256];
    CHECK(data_load_terrain(g, "data/terrain.def", err, sizeof err) == 0);
    CHECK(data_load_units(g, "data/units.def", err, sizeof err) == 0);
    CHECK(data_load_map(g, "data/maps/test_arena.map", err, sizeof err) == 0);
    g->fog = false;
    game_start(g, 1);
    g->n_units = 0;

    /* MoveClass → Layer の対応 */
    CHECK(unit_layer(MC_FOOT)  == LAYER_SURFACE);
    CHECK(unit_layer(MC_WHEEL) == LAYER_SURFACE);
    CHECK(unit_layer(MC_TRACK) == LAYER_SURFACE);
    CHECK(unit_layer(MC_SEA)   == LAYER_SURFACE);
    CHECK(unit_layer(MC_AIR)   == LAYER_AIR);
    CHECK(unit_layer(MC_SUB)   == LAYER_UNDER);

    /* 同一セルに 空/海面/海中 の3体を重ねて配置し、レイヤー別に引けること */
    int fighter = data_find_unit_type(g, "FIGHTER");    /* 空 */
    int destroyer = data_find_unit_type(g, "DESTROYER");/* 海面 */
    int sub = data_find_unit_type(g, "SUBMARINE");      /* 海中 */
    int ua = game_spawn_unit(g, 0, fighter, 8, 6, 10);
    int us = game_spawn_unit(g, 0, destroyer, 8, 6, 10);
    int uu = game_spawn_unit(g, 0, sub, 8, 6, 10);
    CHECK(ua >= 0 && us >= 0 && uu >= 0);

    CHECK(game_unit_at_layer(g, 8, 6, LAYER_AIR)     == ua);
    CHECK(game_unit_at_layer(g, 8, 6, LAYER_SURFACE) == us);
    CHECK(game_unit_at_layer(g, 8, 6, LAYER_UNDER)   == uu);

    int out[LAYER_COUNT];
    CHECK(game_units_at(g, 8, 6, out) == 3);
    CHECK(out[LAYER_AIR] == ua && out[LAYER_SURFACE] == us && out[LAYER_UNDER] == uu);

    /* 空きセル・空きレイヤーは -1 */
    CHECK(game_unit_at_layer(g, 9, 6, LAYER_AIR) < 0);
    CHECK(game_units_at(g, 9, 6, out) == 0);

    /* L1 占有: 別レイヤーの敵が居るセルへは進入・停止でき、同レイヤーは不可 */
    g->n_units = 0;
    int tank = data_find_unit_type(g, "TANK");
    int tk = game_spawn_unit(g, 0, tank, 9, 4, 10);       /* 自軍戦車(海面) */
    game_spawn_unit(g, 1, fighter, 10, 4, 10);            /* 敵戦闘機(空) 隣接 */
    game_spawn_unit(g, 1, tank, 8, 4, 10);               /* 敵戦車(海面) 隣接 */
    game_update_vision(g);
    MoveRange mr;
    path_move_range(g, tk, &mr);
    CHECK(mr.cost[4][10] >= 0 && mr.stop[4][10] == 1);    /* 空の敵の下(海面)へ停止可 */
    CHECK(mr.cost[4][8] < 0);                             /* 同レイヤーの敵へは進入不可 */

    /* L2 戦闘: 同一セル・別レイヤー(距離0)を直射で攻撃できる。
     * 対空戦車(海面)の真上に敵戦闘機(空)が居る状況を作る。 */
    g->n_units = 0;
    int aa = data_find_unit_type(g, "AA_TANK");
    int aatk = game_spawn_unit(g, 0, aa, 12, 6, 10);      /* 対空戦車(海面) */
    int efi  = game_spawn_unit(g, 1, fighter, 12, 6, 10); /* 敵戦闘機(空) 同一セル */
    game_update_vision(g);
    int tg[32];
    int nt = rules_list_targets(g, aatk, 12, 6, tg, 32);
    bool hit_air = false;
    for (int k = 0; k < nt; k++) if (tg[k] == efi) hit_air = true;
    CHECK(hit_air);                                       /* 真上の戦闘機を狙える */

    /* 間接ユニット(自走砲 range_min=2)は同一セル(距離0)を攻撃できない */
    g->n_units = 0;
    int arty = data_find_unit_type(g, "ARTILLERY");
    int art = game_spawn_unit(g, 0, arty, 14, 6, 10);
    int efi2 = game_spawn_unit(g, 1, fighter, 14, 6, 10);
    game_update_vision(g);
    nt = rules_list_targets(g, art, 14, 6, tg, 32);
    bool hit0 = false;
    for (int k = 0; k < nt; k++) if (tg[k] == efi2) hit0 = true;
    CHECK(!hit0);                                         /* 間接は自セル直上を撃てない */
}

/* 空挺降下: 輸送機は「真下（自分と同じヘクス）」へ降ろせる。
 * 輸送ヘリ等は真下へは降ろせない（従来どおり隣接のみ）。 */
static void test_paradrop(void)
{
    Game *g = &s_game;
    memset(g, 0, sizeof *g);
    char err[256];
    CHECK(data_load_terrain(g, "data/terrain.def", err, sizeof err) == 0);
    CHECK(data_load_units(g, "data/units.def", err, sizeof err) == 0);
    CHECK(data_load_map(g, "data/maps/test_arena.map", err, sizeof err) == 0);
    g->fog = false;
    game_start(g, 1);
    g->n_units = 0;

    int plane = data_find_unit_type(g, "T_PLANE");
    int copter = data_find_unit_type(g, "T_COPTER");
    int inf = data_find_unit_type(g, "INFANTRY");
    CHECK(plane >= 0 && copter >= 0 && inf >= 0);
    if (plane < 0 || copter < 0 || inf < 0) return;

    /* データ側のフラグ: 輸送機だけが空挺降下できる */
    CHECK(g->types[plane].paradrop == 1);
    CHECK(g->types[copter].paradrop == 0);
    CHECK(g->types[plane].capacity == 2);

    /* 歩兵が輸送機に乗れること（transport_by に T_PLANE がある） */
    int pi = game_spawn_unit(g, 0, inf, 3, 3, 10);
    int ti = game_spawn_unit(g, 0, plane, 3, 3, 10);
    CHECK(pi >= 0 && ti >= 0);
    CHECK(game_can_board(g, pi, ti));
    game_load_unit(g, pi, ti);
    CHECK((g->units[pi].flags & UF_LOADED) != 0);

    /* 真下（輸送機と同じヘクス）へ降ろせる＝地表レイヤーが空いているため */
    CHECK(game_can_unload_to(g, ti, g->units[ti].pos.x, g->units[ti].pos.y));
    CHECK(game_unload_unit(g, ti, g->units[ti].pos.x, g->units[ti].pos.y) == 0);
    CHECK((g->units[pi].flags & UF_LOADED) == 0);
    CHECK(g->units[pi].pos.x == g->units[ti].pos.x &&
          g->units[pi].pos.y == g->units[ti].pos.y);
    /* 降りた部隊はその手番は行動できない */
    CHECK((g->units[pi].flags & UF_DONE) != 0);

    /* 同じ地表に既に居るので、もう真下へは降ろせない */
    int pi2 = game_spawn_unit(g, 0, inf, 5, 5, 10);
    CHECK(pi2 >= 0);
    game_load_unit(g, pi2, ti);
    CHECK(!game_can_unload_to(g, ti, g->units[ti].pos.x, g->units[ti].pos.y));

    /* 輸送ヘリは真下に地表が空いていても paradrop でないので、
     * UI/AI 側が真下を候補に入れない（フラグで区別できることを確認） */
    int ci = game_spawn_unit(g, 0, copter, 8, 8, 10);
    CHECK(ci >= 0);
    CHECK(g->types[g->units[ci].type].paradrop == 0);
}

/* 立体化 L5: 生産の占有判定はレイヤー別。
 * 上空の航空機は地上ユニットの生産を塞がず、港では海面が塞がっていても
 * 海中（潜水艦）は出せる。 */
static void test_produce_layers(void)
{
    Game *g = &s_game;
    memset(g, 0, sizeof *g);
    char err[256];
    CHECK(data_load_terrain(g, "data/terrain.def", err, sizeof err) == 0);
    CHECK(data_load_units(g, "data/units.def", err, sizeof err) == 0);
    CHECK(data_load_map(g, "data/maps/test_arena.map", err, sizeof err) == 0);
    g->fog = false;
    game_start(g, 1);
    g->n_units = 0;

    int t_fac = -1, t_port = -1;
    for (int i = 0; i < g->n_terrains; i++) {
        if (g->terrains[i].produces == PROD_LAND && !g->terrains[i].is_hq) t_fac = i;
        if (g->terrains[i].produces == PROD_SEA) t_port = i;
    }
    CHECK(t_fac >= 0 && t_port >= 0);
    if (t_fac < 0 || t_port < 0) return;

    int tank = data_find_unit_type(g, "TANK");
    int fighter = data_find_unit_type(g, "FIGHTER");
    int sub = data_find_unit_type(g, "SUBMARINE");
    int destroyer = data_find_unit_type(g, "DESTROYER");
    CHECK(tank >= 0 && fighter >= 0 && sub >= 0 && destroyer >= 0);
    if (tank < 0 || fighter < 0 || sub < 0 || destroyer < 0) return;

    /* --- 工場: 上空に航空機が居ても戦車は作れる --- */
    g->tiles[2][2].terrain = (uint8_t)t_fac;
    g->tiles[2][2].owner = 0;
    g->current = 0;
    CHECK(game_can_produce_at(g, 0, 2, 2));
    int fi = game_spawn_unit(g, 0, fighter, 2, 2, 10);   /* 空レイヤーを占有 */
    CHECK(fi >= 0);
    CHECK(game_type_buildable_at(g, 2, 2, tank));        /* 地上は空いている */
    CHECK(game_can_produce_at(g, 0, 2, 2));
    /* 地上を塞ぐと作れなくなる */
    int ti = game_spawn_unit(g, 0, tank, 2, 2, 10);
    CHECK(ti >= 0);
    CHECK(!game_type_buildable_at(g, 2, 2, tank));

    /* --- 港: 海面が塞がっていても海中（潜水艦）は出せる --- */
    g->tiles[4][4].terrain = (uint8_t)t_port;
    g->tiles[4][4].owner = 0;
    int di = game_spawn_unit(g, 0, destroyer, 4, 4, 10); /* 海面を占有 */
    CHECK(di >= 0);
    CHECK(!game_type_buildable_at(g, 4, 4, destroyer));  /* 海面は埋まった */
    CHECK(game_type_buildable_at(g, 4, 4, sub));         /* 海中は空いている */
    CHECK(game_can_produce_at(g, 0, 4, 4));
    /* 海中も埋めると港からは何も出せない */
    int si = game_spawn_unit(g, 0, sub, 4, 4, 10);
    CHECK(si >= 0);
    CHECK(!game_can_produce_at(g, 0, 4, 4));
}

/* 進化（docs/evolution_spec.md）: 経験値満タン＋自軍の補給拠点の上でのみ。
 * 進化すると経験値は0に戻り、進化先は生産できない。 */
static void test_evolve(void)
{
    Game *g = &s_game;
    memset(g, 0, sizeof *g);
    char err[256];
    CHECK(data_load_terrain(g, "data/terrain.def", err, sizeof err) == 0);
    CHECK(data_load_units(g, "data/units.def", err, sizeof err) == 0);
    CHECK(data_load_map(g, "data/maps/test_arena.map", err, sizeof err) == 0);
    g->fog = false;
    game_start(g, 1);
    g->n_units = 0;

    int inf = data_find_unit_type(g, "INFANTRY");
    CHECK(inf >= 0);
    if (inf < 0) return;
    int to = -1;
    for (int i = 0; i < g->n_types; i++)
        if (!strcmp(g->types[i].id, g->types[inf].evolve_to)) to = i;
    CHECK(g->types[inf].evolve_to[0] != 0);   /* 歩兵に進化先が定義されている */
    CHECK(to >= 0);
    if (to < 0) return;
    CHECK(g->types[to].no_produce == 1);      /* 進化先は生産できない */

    /* 街（LANDを補給できる拠点）を用意して自軍のものにする */
    int t_city = -1;
    for (int i = 0; i < g->n_terrains; i++)
        if (g->terrains[i].supplies & (1u << MC_FOOT)) { t_city = i; break; }
    CHECK(t_city >= 0);
    if (t_city < 0) return;

    int ui = game_spawn_unit(g, 0, inf, 3, 3, 8);
    CHECK(ui >= 0);
    if (ui < 0) return;

    /* 経験値が足りない間は進化できない */
    g->tiles[3][3].terrain = (uint8_t)t_city;
    g->tiles[3][3].owner = 0;
    g->funds[0] = 99999;
    g->units[ui].exp = 99;
    CHECK(!game_can_evolve(g, ui));
    g->units[ui].exp = 100;
    CHECK(game_can_evolve(g, ui));

    /* 資金は元ユニットの価格の2倍。足りなければ進化できない */
    int cost = game_evolve_cost(g, ui);
    CHECK(cost == g->types[inf].cost * 2);
    g->funds[0] = cost - 1;
    CHECK(!game_can_evolve(g, ui));
    g->funds[0] = cost;
    CHECK(game_can_evolve(g, ui));

    /* 拠点の外／敵の拠点では進化できない */
    g->tiles[3][3].owner = 1;
    CHECK(!game_can_evolve(g, ui));
    g->tiles[3][3].owner = 0;
    g->tiles[3][3].terrain = 0;               /* 平地に戻す */
    CHECK(!game_can_evolve(g, ui));
    g->tiles[3][3].terrain = (uint8_t)t_city;
    CHECK(game_can_evolve(g, ui));

    /* 進化を実行。資金がちょうど引かれること */
    int hp_before = g->units[ui].hp;
    g->funds[0] = cost + 500;
    CHECK(game_evolve_unit(g, ui) == 0);
    CHECK(g->funds[0] == 500);
    CHECK(g->units[ui].type == to);
    CHECK(g->units[ui].exp == 0);                     /* 経験値はリセット */
    CHECK(g->units[ui].hp == hp_before);              /* HPは据え置き */
    CHECK(g->units[ui].fuel == g->types[to].fuel);    /* 燃料・弾薬は満タン */
    CHECK(g->units[ui].ammo == g->types[to].ammo);
    CHECK((g->units[ui].flags & UF_DONE) != 0);       /* そのターンは行動終了 */

    /* 進化先はさらに進化しない（片道・1段だけ） */
    g->units[ui].exp = 100;
    CHECK(!game_can_evolve(g, ui));

    /* 生産メニューに進化先が出ないこと */
    int t_fac = -1;
    for (int i = 0; i < g->n_terrains; i++)
        if (g->terrains[i].produces == PROD_LAND && !g->terrains[i].is_hq) t_fac = i;
    CHECK(t_fac >= 0);
    if (t_fac >= 0) {
        g->tiles[6][6].terrain = (uint8_t)t_fac;
        g->tiles[6][6].owner = 0;
        CHECK(game_type_buildable_at(g, 6, 6, inf));
        CHECK(!game_type_buildable_at(g, 6, 6, to));
    }
}

/* 進化したユニットがキャンペーンで失われないこと。
 * (1) 進化後は経験値0なので、経験値だけで並べると出撃枠から溢れて倉庫に沈む
 * (2) 倉庫からの引き出しは「買う」ではなく「戻す」なので no_produce でも通る
 * この2つが揃わないと、進化した精鋭が二度と戦場に出られなくなる。 */
static void test_evolved_carry(void)
{
    Game *g = &s_game;
    memset(g, 0, sizeof *g);
    char err[256];
    CHECK(data_load_terrain(g, "data/terrain.def", err, sizeof err) == 0);
    CHECK(data_load_units(g, "data/units.def", err, sizeof err) == 0);
    CHECK(data_load_map(g, "data/maps/test_arena.map", err, sizeof err) == 0);
    g->fog = false;
    game_start(g, 5);
    g->n_units = 0;

    int inf = data_find_unit_type(g, "INFANTRY");
    int evo = -1;
    for (int i = 0; i < g->n_types; i++)
        if (!strcmp(g->types[i].id, g->types[inf].evolve_to)) evo = i;
    CHECK(inf >= 0 && evo >= 0);
    if (inf < 0 || evo < 0) return;

    /* (2) 倉庫から進化後のユニットを引き出せる（買えはしない） */
    int t_fac = -1;
    for (int i = 0; i < g->n_terrains; i++)
        if (g->terrains[i].produces == PROD_LAND && !g->terrains[i].is_hq) t_fac = i;
    CHECK(t_fac >= 0);
    if (t_fac >= 0) {
        g->tiles[5][5].terrain = (uint8_t)t_fac;
        g->tiles[5][5].owner = 0;
        g->current = 0;
        CHECK(!game_type_buildable_at(g, 5, 5, evo));   /* 生産はできない */
        CHECK(game_type_deployable_at(g, 5, 5, evo));   /* 倉庫からは戻せる */
        /* 有料生産では買えないままであること（進化でのみ入手の原則） */
        g->funds[0] = 99999;
        CHECK(game_produce(g, 5, 5, evo) < 0);
        int ui = game_deploy_free(g, 5, 5, evo, 40);
        CHECK(ui >= 0);
        if (ui >= 0) {
            CHECK(g->units[ui].type == evo);
            CHECK(g->units[ui].exp == 40);              /* 経験値も保たれる */
        }
    }

    /* (1) 持越しの並び: 進化後（経験値0）が、未進化の熟練兵より前に来る */
    g->n_units = 0;
    Campaign c;
    CHECK(campaign_load(&c, "data/campaign/main.cpn", err, sizeof err) == 0);
    for (int i = 0; i < 5; i++) {                       /* 熟練の歩兵たち */
        int u = game_spawn_unit(g, 0, inf, 1, 1, 10);
        g->units[u].exp = 90;
    }
    int ev = game_spawn_unit(g, 0, evo, 2, 2, 10);      /* 進化直後（経験値0） */
    CHECK(ev >= 0);
    if (ev >= 0) g->units[ev].exp = 0;

    CampaignState st;
    memset(&st, 0, sizeof st);
    snprintf(st.node, sizeof st.node, "M01");
    g->winner = 0; g->turn = 40;
    CHECK(campaign_on_victory(g, &c, &st) == 0);
    CHECK(st.n_carry == 6);
    /* 先頭が進化後（価格が高い）＝出撃枠が少なくても真っ先に出る */
    CHECK(st.carry[0].type == evo);
}

/* 進化した輸送手段にも従来どおり搭載できること。
 * transport_by には進化前のID（CARRIER 等）しか書かれておらず、しかも4件までで
 * 歩兵は使い切っているため、進化後のIDを書き足すことができない。
 * 進化前のIDでも一致させていないと、大型空母に艦載機が乗らなくなる。 */
static void test_evolved_transport(void)
{
    Game *g = &s_game;
    memset(g, 0, sizeof *g);
    char err[256];
    CHECK(data_load_terrain(g, "data/terrain.def", err, sizeof err) == 0);
    CHECK(data_load_units(g, "data/units.def", err, sizeof err) == 0);
    CHECK(data_load_map(g, "data/maps/test_arena.map", err, sizeof err) == 0);
    g->fog = false;
    game_start(g, 7);
    g->n_units = 0;

    /* 進化先を持つ全ての輸送手段について、進化後も同じ客を積めること */
    int checked = 0;
    for (int base = 0; base < g->n_types; base++) {
        if (g->types[base].capacity == 0) continue;
        if (!g->types[base].evolve_to[0]) continue;
        int evo = -1;
        for (int i = 0; i < g->n_types; i++)
            if (!strcmp(g->types[i].id, g->types[base].evolve_to)) evo = i;
        CHECK(evo >= 0);
        if (evo < 0) continue;

        /* この輸送手段に乗れる客を1つ探す */
        int pass = -1;
        for (int i = 0; i < g->n_types && pass < 0; i++)
            for (int k = 0; k < g->types[i].n_transport_by; k++)
                if (!strcmp(g->types[i].transport_by[k], g->types[base].id)) {
                    pass = i; break;
                }
        CHECK(pass >= 0);
        if (pass < 0) continue;

        g->n_units = 0;
        int pi = game_spawn_unit(g, 0, pass, 3, 3, 10);
        int ti = game_spawn_unit(g, 0, base, 3, 3, 10);
        int vi = game_spawn_unit(g, 0, evo,  4, 4, 10);
        CHECK(pi >= 0 && ti >= 0 && vi >= 0);
        if (pi < 0 || ti < 0 || vi < 0) continue;
        CHECK(game_can_board(g, pi, ti));   /* 進化前に乗れる */
        CHECK(game_can_board(g, pi, vi));   /* 進化後にも乗れる（本題） */
        checked++;
    }
    CHECK(checked >= 5);   /* トラック・輸送ヘリ・輸送艦・輸送機・空母 */

    /* 副目標「T_SHIP を生存させる」は、進化させた強襲揚陸艦でも達成できること
     * （ID一致だけで見ていると、進化した途端に達成不能になってしまう） */
    {
        Campaign c;
        CHECK(campaign_load(&c, "data/campaign/main.cpn", err, sizeof err) == 0);
        const CpnNode *node = NULL;
        int oi = -1;
        for (int i = 0; i < c.n_nodes && oi < 0; i++)
            for (int k = 0; k < c.nodes[i].n_subs; k++)
                if (!strcmp(c.nodes[i].subs[k].unit, "T_SHIP")) {
                    node = &c.nodes[i]; oi = k; break;
                }
        CHECK(node != NULL && oi >= 0);
        int ts = data_find_unit_type(g, "T_SHIP");
        int tsv = data_find_unit_type(g, "T_SHIP_V");
        if (node && oi >= 0 && ts >= 0 && tsv >= 0) {
            g->n_units = 0;
            CHECK(!campaign_sub_done(g, node, oi));          /* 1隻も居ない */
            game_spawn_unit(g, 0, ts, 3, 3, 10);
            CHECK(campaign_sub_done(g, node, oi));           /* 進化前でOK */
            g->n_units = 0;
            game_spawn_unit(g, 0, tsv, 3, 3, 10);
            CHECK(campaign_sub_done(g, node, oi));           /* 進化後でもOK */
        }
    }

    /* 大型空母は4機まで積める */
    int cv = -1, ft = data_find_unit_type(g, "FIGHTER");
    for (int i = 0; i < g->n_types; i++)
        if (!strcmp(g->types[i].id, "CARRIER_V")) cv = i;
    CHECK(cv >= 0 && ft >= 0);
    if (cv >= 0 && ft >= 0) {
        CHECK(g->types[cv].capacity == 4);
        CHECK(g->types[cv].resupply_cargo == 1);
        g->n_units = 0;
        int ci = game_spawn_unit(g, 0, cv, 6, 6, 10);
        CHECK(ci >= 0);
        for (int k = 0; k < 4 && ci >= 0; k++) {
            int ai = game_spawn_unit(g, 0, ft, 6, 6, 10);
            CHECK(ai >= 0);
            CHECK(game_can_board(g, ai, ci));
            if (ai >= 0) game_load_unit(g, ai, ci);
        }
        /* 5機目は満載で乗れない */
        int extra = game_spawn_unit(g, 0, ft, 7, 7, 10);
        CHECK(extra >= 0);
        if (extra >= 0 && ci >= 0) CHECK(!game_can_board(g, extra, ci));
    }
}

static void test_transport(void)
{
    Game *g = &s_game;
    memset(g, 0, sizeof *g);
    char err[256];
    CHECK(data_load_terrain(g, "data/terrain.def", err, sizeof err) == 0);
    CHECK(data_load_units(g, "data/units.def", err, sizeof err) == 0);
    CHECK(data_load_map(g, "data/maps/test_arena.map", err, sizeof err) == 0);
    g->fog = false;
    game_start(g, 1);
    g->n_units = 0;

    int inf = data_find_unit_type(g, "INFANTRY");
    int truck = data_find_unit_type(g, "TRUCK");
    int tank = data_find_unit_type(g, "TANK");
    CHECK(truck >= 0);
    int up = game_spawn_unit(g, 0, inf, 9, 4, 10);
    int ut = game_spawn_unit(g, 0, truck, 10, 4, 10);
    int uk = game_spawn_unit(g, 0, tank, 11, 4, 10);

    /* 歩兵はトラックに搭載可、戦車は不可 */
    CHECK(game_can_board(g, up, ut));
    CHECK(!game_can_board(g, uk, ut));

    game_load_unit(g, up, ut);
    CHECK(g->units[up].flags & UF_LOADED);
    CHECK(game_unit_at(g, 10, 4) == ut);      /* 搭載中の歩兵は盤上にいない */
    CHECK(g->units[ut].cargo[0] == up);

    /* 満載のトラックには載らない */
    int up2 = game_spawn_unit(g, 0, inf, 9, 5, 10);
    CHECK(!game_can_board(g, up2, ut));

    /* トラック移動→搭載歩兵も追従 */
    game_move_unit(g, ut, 12, 4, 2);
    CHECK(g->units[up].pos.x == 12);

    /* 降車 */
    CHECK(game_can_unload_to(g, ut, 13, 4));
    CHECK(game_unload_unit(g, ut, 13, 4) == 0);
    CHECK(!(g->units[up].flags & UF_LOADED));
    CHECK(game_unit_at(g, 13, 4) == up);

    /* 輸送撃破で搭載も喪失 */
    game_load_unit(g, up2, ut);
    g->units[ut].hp = 1;
    int enemy = game_spawn_unit(g, 1, tank, 11, 4, 10);
    bool dk = false;
    game_attack(g, enemy, ut, NULL, &dk, NULL);
    if (dk)
        CHECK(!(g->units[up2].flags & UF_ALIVE));

    /* 定員2の輸送艦は1手番で2体とも降ろせる（複数降車） */
    g->n_units = 0;
    int tship = data_find_unit_type(g, "T_SHIP");
    CHECK(g->types[tship].capacity == 2);
    int ts  = game_spawn_unit(g, 0, tship, 11, 4, 10);
    int pa  = game_spawn_unit(g, 0, inf, 20, 10, 10);
    int pb  = game_spawn_unit(g, 0, inf, 21, 10, 10);
    game_load_unit(g, pa, ts);
    game_load_unit(g, pb, ts);
    CHECK(g->units[ts].cargo[0] == pa && g->units[ts].cargo[1] == pb);
    /* 1体目を降ろしても、まだ2体目が残り降車可能 */
    CHECK(game_unload_unit(g, ts, 10, 4) == 0);
    CHECK(!(g->units[pa].flags & UF_LOADED) && game_unit_at(g, 10, 4) == pa);
    CHECK(game_first_cargo(g, ts) >= 0);                 /* 2体目が残っている */
    CHECK(game_unload_unit(g, ts, 12, 4) == 0);          /* 2体目も降ろせる */
    CHECK(!(g->units[pb].flags & UF_LOADED) && game_unit_at(g, 12, 4) == pb);
    CHECK(game_first_cargo(g, ts) < 0);                  /* 空になった */

    /* 空母に載せた航空機はターン開始時に空港と同様に補給・修理される */
    g->n_units = 0;
    int carrier = data_find_unit_type(g, "CARRIER");
    int fighter = data_find_unit_type(g, "FIGHTER");
    CHECK(carrier >= 0 && g->types[carrier].resupply_cargo == 1);
    int cv  = game_spawn_unit(g, 0, carrier, 15, 10, 10);
    int air = game_spawn_unit(g, 0, fighter, 16, 10, 8);   /* HP8 損傷 */
    game_spawn_unit(g, 1, tank, 4, 4, 10);                 /* 敵（即時勝利回避） */
    CHECK(game_can_board(g, air, cv));                     /* 空母に着艦可 */
    game_load_unit(g, air, cv);                            /* (乗員, 輸送) */
    CHECK(g->units[air].flags & UF_LOADED);
    g->units[air].fuel = 10;                               /* 燃料・弾薬を減らす */
    g->units[air].ammo = 1;
    game_end_turn(g);   /* P0 → P1 */
    game_end_turn(g);   /* P1 → P0: begin_player_turn で空母が搭載機を補給 */
    CHECK(g->units[air].fuel == g->types[fighter].fuel);   /* 燃料が回復 */
    CHECK(g->units[air].ammo == g->types[fighter].ammo);   /* 弾薬が回復 */
    CHECK(g->units[air].hp == 10);                         /* HP 8→10 に修理 */
}

static void test_supply(void)
{
    Game *g = &s_game;
    memset(g, 0, sizeof *g);
    char err[256];
    CHECK(data_load_terrain(g, "data/terrain.def", err, sizeof err) == 0);
    CHECK(data_load_units(g, "data/units.def", err, sizeof err) == 0);
    CHECK(data_load_map(g, "data/maps/test_arena.map", err, sizeof err) == 0);
    g->fog = false;
    game_start(g, 1);
    g->n_units = 0;

    int sup = data_find_unit_type(g, "SUPPLY");
    int supair = data_find_unit_type(g, "SUPPLY_AIR");
    int tank = data_find_unit_type(g, "TANK");
    int arty = data_find_unit_type(g, "ARTILLERY");
    CHECK(sup >= 0 && g->types[sup].supply == 1);
    CHECK(supair >= 0 && g->types[supair].supply == 1);   /* 補給機も補給ユニット */
    int mat = g->types[sup].ammo;                         /* ammo=補給物資の最大量 */
    CHECK(mat >= 10 && g->types[supair].ammo == mat);

    /* 補給車の隣に燃料/弾薬の減った戦車・自走砲、離れた位置に満タン戦車 */
    int us = game_spawn_unit(g, 0, sup, 9, 4, 10);
    int u1 = game_spawn_unit(g, 0, tank, 10, 4, 10);   /* 隣接 */
    int u2 = game_spawn_unit(g, 0, arty, 8, 4, 10);    /* 隣接 */
    int u3 = game_spawn_unit(g, 0, tank, 13, 4, 10);   /* 距離3: 対象外 */
    int ue = game_spawn_unit(g, 1, tank, 10, 6, 10);   /* 敵: 対象外 */
    CHECK(g->units[us].ammo == mat);                   /* 生成時は物資満タン */
    g->units[u1].fuel = 5;
    g->units[u1].ammo = 1;
    g->units[u2].ammo = 0;
    g->units[u3].fuel = 5;
    g->units[ue].fuel = 5;

    CHECK(game_can_supply(g, us));
    CHECK(game_supply_adjacent(g, us) == 2);
    CHECK(g->units[u1].fuel == g->types[tank].fuel);
    CHECK(g->units[u1].ammo == g->types[tank].ammo);
    CHECK(g->units[u2].ammo == g->types[arty].ammo);
    CHECK(g->units[us].ammo == mat - 2);               /* 2体補給で物資-2 */
    CHECK(g->units[u3].fuel == 5);                     /* 離れていれば回復しない */
    CHECK(g->units[ue].fuel == 5);                     /* 敵は回復しない */
    CHECK(g->units[us].flags & UF_DONE);               /* 補給後は行動済み */
    CHECK(!game_can_supply(g, us));                    /* もう対象がない */

    /* HP回復コマンド: 物資10で1HP。1回のコマンドで回復するのは1HPのみ */
    g->units[us].flags &= (uint8_t)~UF_DONE;
    g->units[us].ammo = 10;                            /* 満タン=1回分 */
    g->units[u1].hp = 6;                               /* -4（最も損傷） */
    g->units[u2].hp = 8;                               /* -2 */
    CHECK(game_can_heal(g, us));
    int healed = game_supply_heal(g, us);
    CHECK(healed == 1);                                /* 1回=1HPのみ */
    CHECK(g->units[us].ammo == 0);                     /* 物資10を消費 */
    CHECK(g->units[u1].hp == 7);                        /* 最も傷んだu1が+1 */
    CHECK(g->units[u2].hp == 8);                        /* 他は回復しない */
    CHECK(!game_can_heal(g, us));                       /* 物資0で回復不可 */

    /* 物資が9以下では回復できない（10必要） */
    g->units[us].flags &= (uint8_t)~UF_DONE;
    g->units[us].ammo = 9;
    CHECK(!game_can_heal(g, us));
    CHECK(game_supply_heal(g, us) == 0);

    /* 補給車どうしは「燃料」だけ補給し合える（物資とHPは対象外） */
    g->n_units = 0;
    int ta = game_spawn_unit(g, 0, sup, 9, 4, 10);
    int tb = game_spawn_unit(g, 0, sup, 10, 4, 6);     /* 隣接・損傷・燃料切れ */
    g->units[ta].ammo = 10;
    g->units[tb].ammo = 3;                             /* 物資は少ない */
    g->units[tb].fuel = 5;                             /* 燃料は減っている */
    CHECK(game_can_supply(g, ta));                     /* 隣の補給車を給油できる */
    CHECK(game_supply_adjacent(g, ta) == 1);
    CHECK(g->units[tb].fuel == g->types[sup].fuel);    /* 燃料は満タンに回復 */
    CHECK(g->units[tb].ammo == 3);                     /* 物資は補充されない */
    CHECK(g->units[ta].ammo == 9);                     /* 供給側は物資-1 */
    CHECK(!game_can_heal(g, ta));                      /* 補給車のHPは回復対象外 */

    /* 補給車の燃料が満タンなら、もう補給対象にならない */
    CHECK(!game_can_supply(g, tb));                    /* tb視点: ta は満タンで対象外 */

    /* ドメイン分離: 補給車=陸のみ / 補給機=空のみ。
     * 中心(10,4)の周囲6ヘクスはすべて隣接なので、陸(11,4)と空(10,3)を並べる */
    int fighter = data_find_unit_type(g, "FIGHTER");

    /* 補給車: 隣の戦車(陸)だけ補給、戦闘機(空)は対象外 */
    g->n_units = 0;
    int gtruck = game_spawn_unit(g, 0, sup, 10, 4, 10);
    int gt_tk  = game_spawn_unit(g, 0, tank, 11, 4, 10);
    int gt_air = game_spawn_unit(g, 0, fighter, 10, 3, 10);
    g->units[gtruck].ammo = 10;
    g->units[gt_tk].fuel = 3;  g->units[gt_tk].hp = 5;
    g->units[gt_air].fuel = 3; g->units[gt_air].hp = 5;
    CHECK(game_can_supply(g, gtruck));
    CHECK(game_supply_adjacent(g, gtruck) == 1);           /* 陸1体のみ */
    CHECK(g->units[gt_tk].fuel == g->types[tank].fuel);    /* 戦車は回復 */
    CHECK(g->units[gt_air].fuel == 3);                     /* 戦闘機は回復しない */
    g->units[gtruck].ammo = 10;
    g->units[gtruck].flags &= (uint8_t)~UF_DONE;
    CHECK(game_can_heal(g, gtruck));
    CHECK(game_supply_heal(g, gtruck) == 1);
    CHECK(g->units[gt_tk].hp == 6);                        /* 戦車のHPが+1 */
    CHECK(g->units[gt_air].hp == 5);                       /* 戦闘機は回復しない */

    /* 補給機: 隣の戦闘機(空)だけ補給、戦車(陸)は対象外 */
    g->n_units = 0;
    int gplane = game_spawn_unit(g, 0, supair, 10, 4, 10);
    int gp_tk  = game_spawn_unit(g, 0, tank, 11, 4, 10);
    int gp_air = game_spawn_unit(g, 0, fighter, 10, 3, 10);
    g->units[gplane].ammo = 10;
    g->units[gp_tk].fuel = 3;  g->units[gp_tk].hp = 5;
    g->units[gp_air].fuel = 3; g->units[gp_air].hp = 5;
    CHECK(game_can_supply(g, gplane));
    CHECK(game_supply_adjacent(g, gplane) == 1);           /* 空1体のみ */
    CHECK(g->units[gp_air].fuel == g->types[fighter].fuel);/* 戦闘機は回復 */
    CHECK(g->units[gp_tk].fuel == 3);                      /* 戦車は回復しない */
    g->units[gplane].ammo = 10;
    g->units[gplane].flags &= (uint8_t)~UF_DONE;
    CHECK(game_can_heal(g, gplane));
    CHECK(game_supply_heal(g, gplane) == 1);
    CHECK(g->units[gp_air].hp == 6);                       /* 戦闘機のHPが+1 */
    CHECK(g->units[gp_tk].hp == 5);                        /* 戦車は回復しない */

    /* 補給艦: 隣の駆逐艦(海)だけ補給・回復、戦車(陸)は対象外 */
    int supship = data_find_unit_type(g, "SUPPLY_SHIP");
    int destroyer = data_find_unit_type(g, "DESTROYER");
    CHECK(supship >= 0 && g->types[supship].supply == 1);
    CHECK(g->types[supship].mclass == MC_SEA);             /* 海ドメイン */
    g->n_units = 0;
    int gship = game_spawn_unit(g, 0, supship, 10, 4, 10);
    int gs_dd = game_spawn_unit(g, 0, destroyer, 11, 4, 10);
    int gs_tk = game_spawn_unit(g, 0, tank, 10, 3, 10);
    g->units[gship].ammo = 10;
    g->units[gs_dd].fuel = 3; g->units[gs_dd].hp = 5;
    g->units[gs_tk].fuel = 3; g->units[gs_tk].hp = 5;
    CHECK(game_can_supply(g, gship));
    CHECK(game_supply_adjacent(g, gship) == 1);            /* 海1体のみ */
    CHECK(g->units[gs_dd].fuel == g->types[destroyer].fuel); /* 駆逐艦は回復 */
    CHECK(g->units[gs_tk].fuel == 3);                      /* 戦車は回復しない */
    g->units[gship].ammo = 10;
    g->units[gship].flags &= (uint8_t)~UF_DONE;
    CHECK(game_can_heal(g, gship));
    CHECK(game_supply_heal(g, gship) == 1);
    CHECK(g->units[gs_dd].hp == 6);                        /* 駆逐艦のHPが+1 */
    CHECK(g->units[gs_tk].hp == 5);                        /* 戦車は回復しない */

    /* 物資切れなら補給できない */
    g->n_units = 0;
    int es = game_spawn_unit(g, 0, sup, 9, 4, 10);
    int e1 = game_spawn_unit(g, 0, tank, 10, 4, 10);
    g->units[es].ammo = 0;
    g->units[e1].fuel = 3;
    CHECK(!game_can_supply(g, es));
    CHECK(game_supply_adjacent(g, es) == 0);

    /* 手番開始時の自動補給（補給フェイズ、物資を消費） */
    g->n_units = 0;
    int as = game_spawn_unit(g, 0, sup, 8, 5, 10);
    int a3 = game_spawn_unit(g, 0, tank, 9, 5, 10);    /* 隣接 */
    game_spawn_unit(g, 1, tank, 20, 10, 10);           /* 敵（即時勝利回避） */
    g->units[as].ammo = 10;
    g->units[a3].fuel = 3;
    game_end_turn(g);   /* P0 → P1 */
    game_end_turn(g);   /* P1 → P0: begin_player_turn で自動補給 */
    CHECK(g->units[a3].fuel == g->types[tank].fuel);
    CHECK(g->units[as].ammo == 9);                     /* 自動補給でも物資-1 */
}

static void test_objective(void)
{
    Game *g = &s_game;
    memset(g, 0, sizeof *g);
    char err[256];
    CHECK(data_load_terrain(g, "data/terrain.def", err, sizeof err) == 0);
    CHECK(data_load_units(g, "data/units.def", err, sizeof err) == 0);
    CHECK(data_load_map(g, "data/maps/test_arena.map", err, sizeof err) == 0);
    g->fog = false;
    game_start(g, 1);

    /* P0 は 首都+都市4+工場2+空港2 = 9拠点所有 */
    int have = game_count_buildings(g, 0);
    CHECK(have == 9);

    /* 目標9: まだ勝利しない */
    g->objective_count = have + 1;
    g->objective_player = 0;
    game_check_victory(g);
    CHECK(g->winner == WINNER_NONE);

    /* 中立都市(14,3)を占領して9拠点 → 即時勝利 */
    int inf = data_find_unit_type(g, "INFANTRY");
    int u = game_spawn_unit(g, 0, inf, 14, 3, 10);
    game_capture(g, u);
    g->units[u].flags &= (uint8_t)~UF_DONE;
    game_capture(g, u);
    CHECK(g->tiles[3][14].owner == 0);
    CHECK(g->winner == 0);

    /* c09 に objective が定義されていること */
    memset(g, 0, sizeof *g);
    CHECK(data_load_terrain(g, "data/terrain.def", err, sizeof err) == 0);
    CHECK(data_load_units(g, "data/units.def", err, sizeof err) == 0);
    CHECK(data_load_map(g, "data/maps/c09_cities.map", err, sizeof err) == 0);
    CHECK(g->objective_count > 0 && g->objective_player == 0);
}

static void test_save_load(void)
{
    Game *g = &s_game;
    memset(g, 0, sizeof *g);
    char err[256];
    CHECK(data_load_terrain(g, "data/terrain.def", err, sizeof err) == 0);
    CHECK(data_load_units(g, "data/units.def", err, sizeof err) == 0);
    CHECK(data_load_map(g, "data/maps/test_arena.map", err, sizeof err) == 0);
    g->fog = true;
    g->ctrl[1] = CTRL_CPU_NORMAL;
    game_start(g, 999);

    /* 状態を少し進める */
    g->funds[0] = 4321;
    g->units[0].hp = 7;
    g->units[0].exp = 44;
    g->tiles[2][9].owner = 0;

    CampaignState cs;
    memset(&cs, 0, sizeof cs);
    cs.active = true;
    snprintf(cs.file, sizeof cs.file, "campaign/main.cpn");
    snprintf(cs.node, sizeof cs.node, "M02");
    cs.funds_carry = 777;
    cs.n_carry = 2;
    cs.carry[0].type = 0; cs.carry[0].exp = 40;
    cs.carry[1].type = 3; cs.carry[1].exp = 20;
    cs.rank[0] = RANK_S; cs.rank[3] = RANK_B;   /* 作戦評価（v6） */
    cs.n_store = 2;      /* 倉庫（v3） */
    cs.store[0].type = 5; cs.store[0].exp = 33;
    cs.store[1].type = 7; cs.store[1].exp = 11;
    cs.cleared = 0x15;   /* M01/M03/M05 クリア済みの想定 */
    g->objective_count = 16;
    g->objective_player = 0;
    /* 壊れた地形（v12）。現地形だけではなく本来の地形も保存されないと、
     * ロード後に工兵で復旧できなくなる。 */
    {
        int city = -1, rubble = -1;
        for (int i = 0; i < g->n_terrains; i++) {
            if (!strcmp(g->terrains[i].id, "CITY"))   city = i;
            if (!strcmp(g->terrains[i].id, "RUBBLE")) rubble = i;
        }
        CHECK(city >= 0 && rubble >= 0);
        g->tiles[3][4].terrain = (uint8_t)rubble;
        g->tiles[3][4].orig_terrain = (uint8_t)city;
    }

    CHECK(save_game(g, &cs, "tests/tmp_test.sav", err, sizeof err) == 0);

    Game g2;
    memcpy(&g2, g, sizeof g2); /* 定義データを引き継ぐ */
    memset(g2.units, 0, sizeof g2.units);
    g2.funds[0] = 0;
    CampaignState cs2;
    CHECK(load_game(&g2, &cs2, "tests/tmp_test.sav", err, sizeof err) == 0);

    CHECK(g2.w == g->w && g2.h == g->h);
    CHECK(g2.funds[0] == 4321);
    CHECK(g2.turn == g->turn);
    CHECK(g2.units[0].hp == 7 && g2.units[0].exp == 44);
    CHECK(g2.tiles[2][9].owner == 0);
    CHECK(g2.tiles[3][4].terrain == g->tiles[3][4].terrain);
    CHECK(g2.tiles[3][4].orig_terrain == g->tiles[3][4].orig_terrain);
    CHECK(g2.tiles[3][4].terrain != g2.tiles[3][4].orig_terrain);   /* 壊れたまま */
    CHECK(g2.rng.s == g->rng.s);
    CHECK(g2.fog == g->fog);
    CHECK(g2.ctrl[1] == CTRL_CPU_NORMAL);
    CHECK(cs2.active && !strcmp(cs2.node, "M02"));
    CHECK(cs2.funds_carry == 777 && cs2.n_carry == 2);
    CHECK(cs2.carry[1].type == 3);
    CHECK(cs2.rank[0] == RANK_S && cs2.rank[3] == RANK_B);  /* ランクが復元される */
    CHECK(cs2.n_store == 2);                                /* 倉庫が復元される */
    CHECK(cs2.store[0].type == 5 && cs2.store[0].exp == 33);
    CHECK(cs2.store[1].type == 7 && cs2.store[1].exp == 11);
    CHECK(cs2.cleared == 0x15);
    CHECK(g2.objective_count == 16 && g2.objective_player == 0);

    /* 破損検知: 1バイト書き換えたらCRC不一致 */
    {
        FILE *f = fopen("tests/tmp_test.sav", "r+b");
        CHECK(f != NULL);
        if (f) {
            fseek(f, 40, SEEK_SET);
            int c = fgetc(f);
            fseek(f, 40, SEEK_SET);
            fputc(c ^ 0xFF, f);
            fclose(f);
        }
        CHECK(load_game(&g2, &cs2, "tests/tmp_test.sav", err, sizeof err) != 0);
    }
    remove("tests/tmp_test.sav");
}

static void test_campaign(void)
{
    Game *g = &s_game;
    memset(g, 0, sizeof *g);
    char err[256];
    CHECK(data_load_terrain(g, "data/terrain.def", err, sizeof err) == 0);
    CHECK(data_load_units(g, "data/units.def", err, sizeof err) == 0);

    Campaign c;
    CHECK(campaign_load(&c, "data/campaign/main.cpn", err, sizeof err) == 0);
    if (s_fail) { printf("  %s\n", err); return; }
    CHECK(c.n_nodes == 16);   /* M01-M10 + 海戦/複合4ノード(N1/N2/N3/N4) */
    CHECK(!strcmp(c.start, "M01"));
    const CpnNode *n1 = campaign_find_node(&c, "M01");
    CHECK(n1 && !strcmp(n1->next_win, "M02"));
    CHECK(n1 && !strcmp(n1->next_win_fast, "M03") && n1->fast_turns == 10);

    /* 全ノードのマップが読み込めること */
    for (int i = 0; i < c.n_nodes; i++) {
        char path[128];
        snprintf(path, sizeof path, "data/%s", c.nodes[i].map);
        CHECK(data_load_map(g, path, err, sizeof err) == 0);
        if (s_fail) { printf("  %s\n", err); return; }
    }

    /* next_win をたどると必ず WIN に着き、全ノードを通ること。
     * ノードを挿し込んだときに鎖が切れたり孤立したりするのを防ぐ。 */
    {
        int visited = 0;
        char cur[32];
        snprintf(cur, sizeof cur, "%s", c.start);
        while (strcmp(cur, "WIN") && visited <= c.n_nodes) {
            const CpnNode *n = campaign_find_node(&c, cur);
            CHECK(n != NULL);
            if (!n) break;
            visited++;
            snprintf(cur, sizeof cur, "%s", n->next_win);
        }
        CHECK(!strcmp(cur, "WIN"));
    }

    /* 艦船を置けるマップかどうかの判定（出撃選択画面のグレーアウトに使う）。
     * 終盤に艦船を出せる作戦があること＝育てた艦隊が死に札にならないこと。 */
    {
        int t_ship = data_find_unit_type(g, "BATTLESHIP");
        int t_tank = data_find_unit_type(g, "TANK");
        CHECK(t_ship >= 0 && t_tank >= 0);

        CHECK(data_load_map(g, "data/maps/c10_plains.map", err, sizeof err) == 0);
        CHECK(!campaign_type_placeable(g, t_ship));   /* 海が1マスも無いマップ */
        CHECK(campaign_type_placeable(g, t_tank));

        int naval_late = 0;
        const char *late[] = { "N3", "N2", "N4", "M10" };
        for (int i = 0; i < (int)(sizeof late / sizeof late[0]); i++) {
            const CpnNode *n = campaign_find_node(&c, late[i]);
            CHECK(n != NULL);
            if (!n) continue;
            char path[128];
            snprintf(path, sizeof path, "data/%s", n->map);
            CHECK(data_load_map(g, path, err, sizeof err) == 0);
            if (campaign_type_placeable(g, t_ship)) naval_late++;
        }
        CHECK(naval_late >= 3);   /* 終盤4作戦のうち3つ以上で艦隊が使える */
    }

    /* 開戦セットアップ + 持越し展開 */
    CampaignState s;
    memset(&s, 0, sizeof s);
    s.active = true;
    snprintf(s.node, sizeof s.node, "M01");
    s.funds_carry = 300;
    s.n_carry = 2;
    s.carry[0].type = (uint8_t)data_find_unit_type(g, "TANK");
    s.carry[0].exp = 60;
    s.carry[1].type = (uint8_t)data_find_unit_type(g, "INFANTRY");
    s.carry[1].exp = 20;
    CHECK(campaign_setup_battle(g, &c, &s, "", 5, err, sizeof err) == 0);
    CHECK(g->fog == true);
    /* 初期4 + 敵4 + 持越し2 + 敵増援2（持越し分と同数）= 12ユニット */
    CHECK(g->n_units == 12);
    /* 持越しユニットの経験値が維持される */
    bool found_vet = false;
    for (int i = 0; i < g->n_units; i++)
        if ((g->units[i].flags & UF_ALIVE) && g->units[i].exp == 60)
            found_vet = true;
    CHECK(found_vet);

    /* 勝利処理: 早期勝利分岐 */
    g->winner = 0;
    g->turn = 8; /* fast_turns=10 以内 */
    CHECK(campaign_on_victory(g, &c, &s) == 0);
    CHECK(!strcmp(s.node, "M03"));
    CHECK(s.n_carry > 0);

    /* 持越しは「経験値の高い順」。上限を超える数の生存ユニットを用意し、
     * carry[] が経験値降順で、かつ倉庫行きより必ず経験値が高いことを確認する。 */
    {
        Game g3;
        memset(&g3, 0, sizeof g3);
        CHECK(data_load_terrain(&g3, "data/terrain.def", err, sizeof err) == 0);
        CHECK(data_load_units(&g3, "data/units.def", err, sizeof err) == 0);
        CHECK(data_load_map(&g3, "data/maps/test_arena.map", err, sizeof err) == 0);
        game_start(&g3, 3);
        g3.n_units = 0;
        int inf3 = data_find_unit_type(&g3, "INFANTRY");
        /* 引き継ぎに上限は無いので、生存した全員が carry に入る */
        int total = 48;
        for (int i = 0; i < total; i++) {
            int u = game_spawn_unit(&g3, 0, inf3, 1, 1, 10);
            g3.units[u].exp = (uint8_t)(i * 3 % 100);   /* バラバラな経験値 */
        }
        CampaignState s3;
        memset(&s3, 0, sizeof s3);
        snprintf(s3.node, sizeof s3.node, "M01");
        g3.winner = 0; g3.turn = 50;
        CHECK(campaign_on_victory(&g3, &c, &s3) == 0);
        CHECK(s3.n_carry == total);      /* 全員が次の作戦へ */
        CHECK(s3.n_store == 0);          /* 溢れないので倉庫は使わない */
        /* carry[] は経験値降順（出撃枠が足りないときに精鋭から並ぶ） */
        for (int i = 1; i < s3.n_carry; i++)
            CHECK(s3.carry[i - 1].exp >= s3.carry[i].exp);
    }

    /* 持越しの初期配置はマップ本来の自軍ユニット数の DEPLOY_CARRY_RATIO 倍まで。
     * 超過分は倉庫へ回り、盤上には出ない。 */
    {
        Game g2;
        memset(&g2, 0, sizeof g2);
        CHECK(data_load_terrain(&g2, "data/terrain.def", err, sizeof err) == 0);
        CHECK(data_load_units(&g2, "data/units.def", err, sizeof err) == 0);
        /* まず持越し無しで M01 を読み、本来の自軍ユニット数を数える */
        CampaignState s0;
        memset(&s0, 0, sizeof s0);
        snprintf(s0.node, sizeof s0.node, "M01");
        CHECK(campaign_setup_battle(&g2, &c, &s0, "", 5, err, sizeof err) == 0);
        int base0 = 0;
        for (int i = 0; i < g2.n_units; i++)
            if ((g2.units[i].flags & UF_ALIVE) && g2.units[i].owner == 0) base0++;
        CHECK(base0 > 0);

        /* 上限を大きく超える持越しを与える */
        CampaignState s1;
        memset(&s1, 0, sizeof s1);
        snprintf(s1.node, sizeof s1.node, "M01");
        int inf_t = data_find_unit_type(&g2, "INFANTRY");
        s1.n_carry = MAX_CARRY_UNITS;
        for (int i = 0; i < s1.n_carry; i++) {
            s1.carry[i].type = (uint8_t)inf_t;
            s1.carry[i].exp = (uint8_t)(MAX_CARRY_UNITS - i);  /* 経験値降順 */
        }
        CHECK(campaign_setup_battle(&g2, &c, &s1, "", 5, err, sizeof err) == 0);
        int p0 = 0;
        for (int i = 0; i < g2.n_units; i++)
            if ((g2.units[i].flags & UF_ALIVE) && g2.units[i].owner == 0) p0++;
        int limit = base0 * DEPLOY_CARRY_RATIO;
        CHECK(p0 == base0 + limit);                    /* 配置は上限まで */
        CHECK(s1.n_store == s1.n_carry - limit);       /* 残りは全部倉庫へ */
        /* 上限内に配置されたのは経験値の高い側 */
        CHECK(s1.store[0].exp <= s1.carry[limit].exp);
    }
}

static void test_warehouse(void)
{
    Game *g = &s_game;
    memset(g, 0, sizeof *g);
    char err[256];
    CHECK(data_load_terrain(g, "data/terrain.def", err, sizeof err) == 0);
    CHECK(data_load_units(g, "data/units.def", err, sizeof err) == 0);
    CHECK(data_load_map(g, "data/maps/test_arena.map", err, sizeof err) == 0);
    game_start(g, 1);
    g->current = 0;
    g->n_units = 0;
    int inf = data_find_unit_type(g, "INFANTRY");

    /* 倉庫 push / remove の基本 */
    CampaignState s;
    memset(&s, 0, sizeof s);
    campaign_store_push(&s, 3, 50);
    campaign_store_push(&s, 4, 20);
    CHECK(s.n_store == 2 && s.store[0].type == 3 && s.store[0].exp == 50);
    campaign_store_remove(&s, 0);
    CHECK(s.n_store == 1 && s.store[0].type == 4);

    /* 満杯時は最も経験値の低い枠と入れ替え（高経験値のみ残す） */
    memset(&s, 0, sizeof s);
    for (int i = 0; i < MAX_STORE_UNITS; i++) campaign_store_push(&s, inf, 10);
    CHECK(s.n_store == MAX_STORE_UNITS);
    campaign_store_push(&s, inf, 99);                  /* 高経験値 → 入替 */
    CHECK(s.n_store == MAX_STORE_UNITS);
    bool has99 = false;
    for (int i = 0; i < s.n_store; i++) if (s.store[i].exp == 99) has99 = true;
    CHECK(has99);
    campaign_store_push(&s, inf, 1);                   /* 低経験値 → 無視 */
    bool has1 = false;
    for (int i = 0; i < s.n_store; i++) if (s.store[i].exp == 1) has1 = true;
    CHECK(!has1);

    /* 倉庫からの無料引き出し: 生産拠点で exp を保持して配置、資金は減らない */
    int fac = -1;
    for (int t = 0; t < g->n_terrains; t++)
        if (g->terrains[t].produces == PROD_LAND) { fac = t; break; }
    CHECK(fac >= 0);
    int fx = 6, fy = 6;
    g->tiles[fy][fx].terrain = (uint8_t)fac;
    g->tiles[fy][fx].owner = 0;
    g->funds[0] = 100;
    int ui = game_deploy_free(g, fx, fy, inf, 55);
    CHECK(ui >= 0);
    CHECK(g->units[ui].exp == 55);                     /* exp 引継ぎ */
    CHECK(g->funds[0] == 100);                         /* 無料 */
    CHECK(g->units[ui].flags & UF_DONE);               /* 生産同様 行動済み */
    CHECK(game_deploy_free(g, fx, fy, inf, 10) < 0);   /* 既にユニットが居るので不可 */

    /* 生き残った部隊は上限なく次の作戦へ引き継がれる（倉庫送りにならない） */
    g->n_units = 0;
    Campaign c;
    CHECK(campaign_load(&c, "data/campaign/main.cpn", err, sizeof err) == 0);
    int many = 60;                                     /* 旧上限(30)より多く */
    for (int i = 0; i < many; i++) {
        int u = game_spawn_unit(g, 0, inf, 1, 1, 10);  /* 位置は重複可 */
        g->units[u].exp = (uint8_t)i;
    }
    CampaignState s2;
    memset(&s2, 0, sizeof s2);
    snprintf(s2.node, sizeof s2.node, "M01");
    g->winner = 0; g->turn = 50;
    CHECK(campaign_on_victory(g, &c, &s2) == 0);
    CHECK(s2.n_carry == many);                         /* 全員引き継ぐ */
    CHECK(s2.n_store == 0);                            /* 倉庫は使われない */
    /* 経験値降順に並ぶ（出撃枠が足りないとき精鋭から出せるように） */
    for (int i = 1; i < s2.n_carry; i++)
        CHECK(s2.carry[i - 1].exp >= s2.carry[i].exp);
}

/* 指揮官（CO）: 常時効果・ゲージ・必殺技 */
static void test_commanders(void)
{
    Game *g = &s_game;
    memset(g, 0, sizeof *g);
    char err[256];
    CHECK(data_load_terrain(g, "data/terrain.def", err, sizeof err) == 0);
    CHECK(data_load_units(g, "data/units.def", err, sizeof err) == 0);
    CHECK(data_load_commanders(g, "data/commanders.def", err, sizeof err) == 0);
    if (s_fail) { printf("  %s\n", err); return; }
    CHECK(g->n_cos >= 5);

    /* 解禁条件: 最初から使えるのが3人、クリア数で段階的に増える */
    {
        int free_now = 0;
        for (int i = 0; i < g->n_cos; i++)
            if (g->cos[i].unlock_clears <= 0) free_now++;
        CHECK(free_now == 3);                       /* 初期解禁は3人 */
        CHECK(g->cos[data_find_commander(g, "KARLA")].unlock_clears == 2);
        CHECK(g->cos[data_find_commander(g, "WOLF")].unlock_clears == 4);
        CHECK(g->cos[data_find_commander(g, "EAGLE")].unlock_clears == 6);
        /* 段階的に増えることを見る。人数を直書きすると指揮官を
         * 追加するたびに失敗するので、「増える」こと自体を確かめる。 */
        int at0 = 0, at5 = 0;
        for (int i = 0; i < g->n_cos; i++) {
            if (g->cos[i].unlock_clears <= 0) at0++;
            if (g->cos[i].unlock_clears <= 5) at5++;
        }
        CHECK(at0 == free_now);
        CHECK(at5 > free_now);              /* 進めると選択肢が増える */
        CHECK(at5 < g->n_cos);              /* だが全員ではない（終盤用が残る） */
    }

    int graf = data_find_commander(g, "GRAF");    /* 防御+15 / HEAL */
    int balt = data_find_commander(g, "BALT");    /* 攻撃+15 / STRIKE */
    int wolf = data_find_commander(g, "WOLF");    /* 海のみ強化 */
    int liese = data_find_commander(g, "LIESE");  /* 移動+1 / RUSH */
    CHECK(graf >= 0 && balt >= 0 && wolf >= 0 && liese >= 0);

    CHECK(data_load_map(g, "data/maps/test_arena.map", err, sizeof err) == 0);
    g->fog = false;
    game_start(g, 7);
    g->n_units = 0;
    int inf = data_find_unit_type(g, "INFANTRY");
    int dd  = data_find_unit_type(g, "DESTROYER");

    /* 指揮官なしを基準にする */
    g->co_id[0] = -1; g->co_id[1] = -1;
    int ua = game_spawn_unit(g, 0, inf, 9, 4, 10);
    int ub = game_spawn_unit(g, 1, inf, 10, 4, 10);
    int base = battle_expect_damage_x10(g, &g->units[ua], &g->units[ub]);
    CHECK(base > 0);

    /* 攻撃側にBALT（攻撃+15%）→ 期待ダメージが増える */
    g->co_id[0] = (int8_t)balt;
    int with_atk = battle_expect_damage_x10(g, &g->units[ua], &g->units[ub]);
    CHECK(with_atk > base);

    /* 防御側にGRAF（防御+15）→ 期待ダメージが減る */
    g->co_id[0] = -1; g->co_id[1] = (int8_t)graf;
    int with_def = battle_expect_damage_x10(g, &g->units[ua], &g->units[ub]);
    CHECK(with_def < base);

    /* WOLF は海ドメイン限定: 歩兵には効かない／艦船には効く */
    g->co_id[0] = (int8_t)wolf; g->co_id[1] = -1;
    CHECK(battle_expect_damage_x10(g, &g->units[ua], &g->units[ub]) == base);
    CHECK(!game_co_affects(g, 0, &g->units[ua]));
    {
        g->n_units = 0;
        int sa = game_spawn_unit(g, 0, dd, 20, 10, 10);
        int sb = game_spawn_unit(g, 1, inf, 9, 4, 10);
        CHECK(game_co_affects(g, 0, &g->units[sa]));   /* 艦船には効く */
        g->co_id[0] = -1;
        int nb = battle_expect_damage_x10(g, &g->units[sa], &g->units[sb]);
        g->co_id[0] = (int8_t)wolf;
        CHECK(battle_expect_damage_x10(g, &g->units[sa], &g->units[sb]) > nb);
    }

    /* ゲージ: 攻撃でたまり、満タンで必殺技が使える
     * （両陣営に指揮官を設定する。指揮官なしの側はゲージが溜まらない仕様） */
    g->n_units = 0;
    g->co_id[0] = (int8_t)graf; g->co_id[1] = (int8_t)balt;
    g->co_gauge[0] = g->co_gauge[1] = 0;
    CHECK(!game_co_power_ready(g, 0));
    int ha = game_spawn_unit(g, 0, inf, 9, 4, 10);
    int hb = game_spawn_unit(g, 1, inf, 10, 4, 10);
    game_update_vision(g);
    game_attack(g, ha, hb, NULL, NULL, NULL);
    CHECK(g->co_gauge[0] > 0);                         /* 与ダメージで溜まる */
    CHECK(g->co_gauge[1] > 0);                         /* 被ダメージでも溜まる */
    /* 指揮官がいない陣営はゲージが溜まらない */
    {
        int8_t save1 = g->co_id[1];
        g->co_id[1] = -1; g->co_gauge[1] = 0;
        game_co_add_gauge(g, 1, 50);
        CHECK(g->co_gauge[1] == 0);
        g->co_id[1] = save1;
    }

    /* HEAL: 全軍のHPが回復し、ゲージは0に戻る */
    g->units[ha].hp = 5;
    g->co_gauge[0] = g->cos[graf].power_cost;
    CHECK(game_co_power_ready(g, 0));
    CHECK(game_co_activate(g, 0));
    CHECK(g->units[ha].hp == 8);                       /* 5 + power_val(3) */
    CHECK(g->co_gauge[0] == 0);
    CHECK(!game_co_power_ready(g, 0));

    /* RUSH: 行動済みフラグが解除される */
    g->co_id[0] = (int8_t)liese;
    g->units[ha].flags |= UF_DONE;
    g->co_gauge[0] = g->cos[liese].power_cost;
    CHECK(game_co_activate(g, 0));
    CHECK(!(g->units[ha].flags & UF_DONE));

    /* STRIKE: 発動中だけ攻撃が上がり、手番終了で切れる */
    g->co_id[0] = (int8_t)balt;
    g->co_power_turns[0] = 0;
    int before = battle_expect_damage_x10(g, &g->units[ha], &g->units[hb]);
    g->co_gauge[0] = g->cos[balt].power_cost;
    CHECK(game_co_activate(g, 0));
    CHECK(g->co_power_turns[0] == 1);
    CHECK(battle_expect_damage_x10(g, &g->units[ha], &g->units[hb]) > before);
    g->current = 0;
    game_end_turn(g);                                  /* 手番終了で効果切れ */
    CHECK(g->co_power_turns[0] == 0);

    /* --- ここからは後から追加した必殺技 --- */

    /* STRIKE はドメインを見ること。
     * 以前は見ておらず、海専門の WOLF の技で陸上部隊まで強化されていた。 */
    {
        int eagle = data_find_commander(g, "EAGLE");   /* AIR 限定の STRIKE */
        CHECK(eagle >= 0);
        g->co_id[0] = (int8_t)eagle;
        g->co_power_turns[0] = 0;
        int flat = battle_expect_damage_x10(g, &g->units[ha], &g->units[hb]);
        g->co_power_turns[0] = 1;                      /* 発動中にする */
        /* ha は歩兵（陸）なので、航空限定の技では変わらない */
        CHECK(battle_expect_damage_x10(g, &g->units[ha], &g->units[hb]) == flat);
        g->co_power_turns[0] = 0;
    }

    /* RESUPPLY: 拠点の外でも燃料・弾薬が満タンに戻る */
    {
        int herta = data_find_commander(g, "HERTA");
        CHECK(herta >= 0);
        g->co_id[0] = (int8_t)herta;
        g->units[ha].fuel = 1;
        g->units[ha].ammo = 0;
        g->co_gauge[0] = g->cos[herta].power_cost;
        CHECK(game_co_activate(g, 0));
        const UnitType *ut = &g->types[g->units[ha].type];
        CHECK(g->units[ha].fuel == ut->fuel);
        CHECK(g->units[ha].ammo == ut->ammo);
    }

    /* VETERAN: 経験値が上がり、上限100を超えない */
    {
        int noel = data_find_commander(g, "NOEL");
        CHECK(noel >= 0);
        g->co_id[0] = (int8_t)noel;
        g->units[ha].exp = 10;
        g->co_gauge[0] = g->cos[noel].power_cost;
        CHECK(game_co_activate(g, 0));
        CHECK(g->units[ha].exp == 50);                 /* 10 + power_val(40) */
        g->units[ha].exp = 90;
        g->co_gauge[0] = g->cos[noel].power_cost;
        CHECK(game_co_activate(g, 0));
        CHECK(g->units[ha].exp == 100);                /* 上限で止まる */
    }

    /* BARRAGE: 隣接している敵だけ削る。HP1 を下回らない */
    {
        int wolf2 = data_find_commander(g, "WOLF");
        g->co_id[0] = (int8_t)wolf2;
        int far_enemy = game_spawn_unit(g, 1, inf, 20, 14, 10);
        CHECK(far_enemy >= 0);
        g->units[hb].hp = 10;                          /* ha に隣接している */
        g->co_gauge[0] = g->cos[wolf2].power_cost;
        CHECK(game_co_activate(g, 0));
        CHECK(g->units[hb].hp == 7);                   /* 10 - power_val(3) */
        CHECK(g->units[far_enemy].hp == 10);           /* 離れている敌は無事 */
        /* **必殺技は領域の規則を意図的に無視する**。
         * 通常の攻撃は夜に陸↔空が成立しないが、艦砲射撃は隣接する敵を
         * 領域を問わず削る。「バグに見えるが仕様」なのでここで固めておく。 */
        {
            int air = data_find_unit_type(g, "FIGHTER");
            CHECK(air >= 0);
            /* 自軍の隣のヘクスの上空。真上（距離0）だと隣接判定に入らない */
            int ea = game_spawn_unit(g, 1, air,
                                     g->units[hb].pos.x, g->units[hb].pos.y, 10);
            CHECK(ea >= 0);
            CHECK(hex_distance(g->units[ha].pos.x, g->units[ha].pos.y,
                               g->units[ea].pos.x, g->units[ea].pos.y) == 1);
            g->turn = 4;                               /* 夜にする */
            CHECK(game_is_night(g));
            g->units[hb].hp = 10;
            g->co_gauge[0] = g->cos[wolf2].power_cost;
            CHECK(game_co_activate(g, 0));
            CHECK(g->units[hb].hp == 7);               /* 陸の敵 */
            CHECK(g->units[ea].hp == 7);               /* 空の敵も削れる */
            g->turn = 1;
            g->units[ea].flags &= (uint8_t)~UF_ALIVE;
        }
        g->units[hb].hp = 2;
        g->co_gauge[0] = g->cos[wolf2].power_cost;
        CHECK(game_co_activate(g, 0));
        CHECK(g->units[hb].hp == 1);                   /* とどめは刷さない */
    }

    /* ADVANCE: このターンだけ移動力が伸びる */
    {
        int dieter = data_find_commander(g, "DIETER");
        CHECK(dieter >= 0);
        g->co_id[0] = (int8_t)dieter;
        g->co_power_turns[0] = 0;
        int base_b = game_co_move_bonus(g, 0, &g->units[ha]);
        g->co_gauge[0] = g->cos[dieter].power_cost;
        CHECK(game_co_activate(g, 0));
        CHECK(game_co_move_bonus(g, 0, &g->units[ha])
              == base_b + g->cos[dieter].power_val);
    }
}

/* フリー対戦のマップ一覧。全部読めて、開幕で決着しないこと。
 * **首都が1つでもあるマップでは、参加全陣営が首都を持つ必要がある**。
 * 持たない陣営は game_player_defeated が即敗北と見なし、
 * 手番すら回ってこない（マップを作るときに踏みやすい罠）。 */
static void test_maplist(void)
{
    Game *g = &s_game;
    char err[256];
    FILE *f = fopen("data/maps/maplist.txt", "rb");
    CHECK(f != NULL);
    if (!f) return;

    char line[512];
    int n_maps = 0;
    while (fgets(line, sizeof line, f)) {
        char *hash = strchr(line, '#');
        if (hash) *hash = 0;
        char *s2 = line;
        while (*s2 == ' ' || *s2 == 0x09) s2++;
        char *bar = strchr(s2, '|');
        if (!bar) continue;
        *bar = 0;
        char *e = s2 + strlen(s2);
        while (e > s2 && (e[-1] == ' ' || e[-1] == 0x09)) *--e = 0;
        if (!*s2) continue;

        char path[256];
        snprintf(path, sizeof path, "data/%s", s2);
        memset(g, 0, sizeof *g);
        CHECK(data_load_terrain(g, "data/terrain.def", err, sizeof err) == 0);
        CHECK(data_load_units(g, "data/units.def", err, sizeof err) == 0);
        if (data_load_map(g, path, err, sizeof err) != 0) {
            printf("  %s: %s\n", path, err);
            CHECK(0);
            continue;
        }
        n_maps++;
        g->fog = false;
        game_start(g, 9);

        int parts = 0;
        for (int p = 0; p < MAX_PLAYERS; p++) if (game_player_in_play(g, p)) parts++;
        if (parts < 2) printf("  %s: 参加陣営が %d\n", path, parts);
        CHECK(parts >= 2);

        bool any_hq = false;
        for (int y = 0; y < g->h && !any_hq; y++)
            for (int x = 0; x < g->w && !any_hq; x++)
                if (g->terrains[g->tiles[y][x].terrain].is_hq) any_hq = true;
        for (int p = 0; p < MAX_PLAYERS; p++) {
            if (!game_player_in_play(g, p)) continue;
            if (any_hq) {
                int mine = 0;
                for (int y = 0; y < g->h; y++)
                    for (int x = 0; x < g->w; x++)
                        if (g->terrains[g->tiles[y][x].terrain].is_hq &&
                            g->tiles[y][x].owner == (int8_t)p) mine++;
                if (mine == 0) printf("  %s: 陣営%d に首都が無い\n", path, p);
                CHECK(mine > 0);
            }
            if (game_player_defeated(g, p))
                printf("  %s: 陣営%d が開幕で敗北扱い\n", path, p);
            CHECK(!game_player_defeated(g, p));
        }
        game_check_victory(g);
        if (g->winner != WINNER_NONE)
            printf("  %s: 開幕で決着している (winner=%d)\n", path, g->winner);
        CHECK(g->winner == WINNER_NONE);
    }
    fclose(f);
    CHECK(n_maps >= 15);

    /* **一覧の上限を超えていないこと**。data_load_maplist は
     * MAX_MAPLIST を超えた行を黙って捨てるので、
     * マップを追加してもゲームに出てこない事故になる。 */
    {
        MapList ml;
        CHECK(data_load_maplist(&ml, "data/maps/maplist.txt") == 0);
        CHECK(ml.n == n_maps);
    }
}

/* CPUの生産判断: 敵の装甲構成に対する有効打で選ぶこと。
 * 4カテゴリの攻撃力を単純に足していた頃は、何にでも当たる
 * 爆撃機が常に勝ち、敵に航空がいても戦闘機を一度も作らなかった。 */
static void test_ai_production_mix(void)
{
    Game *g = &s_game;
    char err[256];
    memset(g, 0, sizeof *g);
    CHECK(data_load_terrain(g, "data/terrain.def", err, sizeof err) == 0);
    CHECK(data_load_units(g, "data/units.def", err, sizeof err) == 0);
    CHECK(data_load_map(g, "data/maps/test_arena.map", err, sizeof err) == 0);
    if (s_fail) { printf("  %s\n", err); return; }

    int airport = -1;
    for (int i = 0; i < g->n_terrains; i++)
        if (!strcmp(g->terrains[i].id, "AIRPORT")) airport = i;
    int bomber = data_find_unit_type(g, "BOMBER");
    int inf    = data_find_unit_type(g, "INFANTRY");
    CHECK(airport >= 0 && bomber >= 0 && inf >= 0);
    if (s_fail) return;

    g->fog = false;
    g->ctrl[0] = CTRL_CPU_NORMAL;
    g->ctrl[1] = CTRL_CPU_NORMAL;
    game_start(g, 3);

    /* 陣営0 の飛行場を用意し、資金を潤沢にする */
    g->tiles[5][5].terrain = (uint8_t)airport;
    g->tiles[5][5].owner = 0;
    g->funds[0] = 20000;
    g->turn = 8;                       /* 序盤補正を抜けたところ */

    /* 自軍は対空手段の無い歩兵だけ。敵は爆撃機を揃えている。 */
    g->n_units = 0;
    for (int i = 0; i < 4; i++) CHECK(game_spawn_unit(g, 0, inf, 3 + i, 3, 10) >= 0);
    for (int i = 0; i < 4; i++) CHECK(game_spawn_unit(g, 1, bomber, 20 + i, 12, 10) >= 0);
    game_update_vision(g);
    int before = g->n_units;

    AiState ai;
    memset(&ai, 0, sizeof ai);
    g->current = 0;
    ai_begin_turn(g, &ai);
    for (int step = 0; step < 200 && ai_step(g, &ai); step++) { }

    /* 作られた航空ユニットに、対空できるものが含まれること。 */
    int made_air = 0, made_anti_air = 0;
    for (int i = before; i < g->n_units; i++) {
        if (g->units[i].owner != 0) continue;
        const UnitType *t = &g->types[g->units[i].type];
        if (t->mclass != MC_AIR) continue;
        made_air++;
        if (t->atk[ARMOR_AIR] > 0) made_anti_air++;
    }
    CHECK(made_air > 0);          /* 飛行場があるので何かは作る */
    CHECK(made_anti_air > 0);     /* 敵が航空なら対空できる機を選ぶ */
}

/* 幕間（キャンペーンのひとこま）。
 * 読み飛ばせるのが前提なので、無いノードがあっても壊れてはいけない。 */
static void test_campaign_story(void)
{
    Campaign c;
    char err[256];
    CHECK(campaign_load(&c, "data/campaign/main.cpn", err, sizeof err) == 0);
    if (s_fail) { printf("  %s\n", err); return; }

    int with_story = 0, with_win = 0;
    for (int i = 0; i < c.n_nodes; i++) {
        const CpnNode *n = &c.nodes[i];
        CHECK(n->n_story >= 0 && n->n_story <= MAX_STORY_LINES);
        CHECK(n->n_story_win >= 0 && n->n_story_win <= MAX_STORY_LINES);
        if (n->n_story > 0) with_story++;
        if (n->n_story_win > 0) with_win++;
        for (int k = 0; k < n->n_story_win; k++) {
            CHECK(n->story_win[k].text[0] != 0);
            CHECK(strlen(n->story_win[k].who) +
                  strlen(n->story_win[k].text) <= 120);
        }
        for (int k = 0; k < n->n_story; k++) {
            /* 本文が空の行は無い（話者だけ書いてしまった事故を拾う） */
            CHECK(n->story[k].text[0] != 0);
            /* 一枚に全行出すので、長すぎる行は箱からはみ出す。
             * UTF-8の日本語は1字3バイトなので、話者＋本文でおおよそ40字以内。 */
            CHECK(strlen(n->story[k].who) + strlen(n->story[k].text) <= 120);
        }
    }
    /* 全作戦にひとこまがあること。抜けるとその作戦だけ話が飛ぶ。 */
    CHECK(with_story == c.n_nodes);
    CHECK(with_win == c.n_nodes);   /* 勝利直後のひとこまも全作戦分 */
}

/* キャンペーンの多陣営ノード（第3段階）。
 * .cpn の ctrl2/co2 が読めて、マップ側の team と組み合わさること。 */
static void test_campaign_multi(void)
{
    Game *g = &s_game;
    char err[256];
    Campaign c;
    CampaignState cs;
    memset(g, 0, sizeof *g);
    CHECK(data_load_terrain(g, "data/terrain.def", err, sizeof err) == 0);
    CHECK(data_load_units(g, "data/units.def", err, sizeof err) == 0);
    CHECK(data_load_commanders(g, "data/commanders.def", err, sizeof err) == 0);
    CHECK(campaign_load(&c, "data/campaign/main.cpn", err, sizeof err) == 0);
    if (s_fail) { printf("  %s\n", err); return; }

    /* --- M11: 2対2のチーム戦 --- */
    const CpnNode *n = campaign_find_node(&c, "M11");
    CHECK(n != NULL);
    if (!n) return;
    CHECK(n->ctrl[1] == CTRL_CPU_NORMAL);
    CHECK(n->ctrl[2] == CTRL_CPU_NORMAL);
    CHECK(n->ctrl[3] == CTRL_CPU_EASY);      /* ctrl3 = EASY が読めている */
    CHECK(n->co[2][0] != 0 && n->co[3][0] != 0);
    CHECK(data_find_commander(g, n->co[2]) >= 0);
    CHECK(data_find_commander(g, n->co[3]) >= 0);

    memset(&cs, 0, sizeof cs);
    cs.active = true;
    cs.player_co = 0;
    snprintf(cs.node, sizeof cs.node, "M11");
    CHECK(campaign_setup_map(g, &c, &cs, "", err, sizeof err) == 0);
    if (s_fail) { printf("  %s\n", err); return; }
    campaign_begin(g, &c, &cs, 11, NULL);

    CHECK(g->ctrl[0] == CTRL_HUMAN);         /* キャンペーンの自軍は常に人間 */
    for (int p = 1; p < MAX_PLAYERS; p++) CHECK(g->ctrl[p] != CTRL_HUMAN);
    CHECK(g->ctrl[3] == CTRL_CPU_EASY);
    CHECK(g->co_id[2] == (int8_t)data_find_commander(g, n->co[2]));
    /* チームは .map 側が決める */
    CHECK(game_same_team(g, 0, 2) && game_same_team(g, 1, 3));
    CHECK(game_is_enemy(g, 0, 1) && game_is_enemy(g, 2, 3));
    CHECK(!game_is_enemy(g, 0, 2));
    CHECK(game_team_leader(g, game_team_of(g, 0)) == 0);
    CHECK(game_team_leader(g, game_team_of(g, 1)) == 1);
    for (int p = 0; p < 4; p++) CHECK(game_player_in_play(g, p));
    CHECK(g->winner == WINNER_NONE);         /* 開幕で決着しない */

    /* 援軍(3)を消しても主力(1)が健在なら続く */
    for (int i = 0; i < g->n_units; i++)
        if (g->units[i].owner == 3) g->units[i].flags &= (uint8_t)~UF_ALIVE;
    game_check_victory(g);
    CHECK(g->winner == WINNER_NONE);

    /* --- M12: 3陣営の乱戦 --- */
    n = campaign_find_node(&c, "M12");
    CHECK(n != NULL);
    if (!n) return;
    memset(&cs, 0, sizeof cs);
    cs.active = true;
    cs.player_co = 0;
    snprintf(cs.node, sizeof cs.node, "M12");
    CHECK(campaign_setup_map(g, &c, &cs, "", err, sizeof err) == 0);
    if (s_fail) { printf("  %s\n", err); return; }
    campaign_begin(g, &c, &cs, 12, NULL);

    int np = 0;
    for (int p = 0; p < MAX_PLAYERS; p++) if (game_player_in_play(g, p)) np++;
    CHECK(np == 3);
    /* team 指定が無いので全員が敵同士（乱戦） */
    CHECK(game_is_enemy(g, 0, 1) && game_is_enemy(g, 0, 2) && game_is_enemy(g, 1, 2));
    CHECK(g->winner == WINNER_NONE);
    /* 拠点確保による勝利口がある（三つ巴の引き分け対策） */
    CHECK(g->objective_count > 0 && g->objective_player == 0);
}

/* 工作（地形の破壊と復旧）。工兵が隣接ヘクスに対して行う。 */
static void test_terrain_work(void)
{
    Game *g = &s_game;
    memset(g, 0, sizeof *g);
    char err[256];
    CHECK(data_load_terrain(g, "data/terrain.def", err, sizeof err) == 0);
    CHECK(data_load_units(g, "data/units.def", err, sizeof err) == 0);
    CHECK(data_load_map(g, "data/maps/test_arena.map", err, sizeof err) == 0);
    if (s_fail) return;
    g->fog = false;
    game_start(g, 1);
    g->n_units = 0;

    int city = -1, hq = -1, rubble = -1, plain = -1, road = -1;
    for (int i = 0; i < g->n_terrains; i++) {
        if (!strcmp(g->terrains[i].id, "CITY"))   city = i;
        if (!strcmp(g->terrains[i].id, "HQ"))     hq = i;
        if (!strcmp(g->terrains[i].id, "RUBBLE")) rubble = i;
        if (!strcmp(g->terrains[i].id, "PLAIN"))  plain = i;
        if (!strcmp(g->terrains[i].id, "ROAD"))   road = i;
    }
    CHECK(city >= 0 && hq >= 0 && rubble >= 0 && plain >= 0 && road >= 0);
    if (s_fail) return;

    /* 地形定義側: 拠点は瓦礫になり、首都と瓦礫は壊せない */
    CHECK(g->terrains[city].breaks_idx == rubble);
    /* **破壊で通行可否を変えないこと**。港を陸の瓦礫にしてしまうと
     * 艦船が入れなくなり、海峡や運河を工兵一つで永久に塞げてしまう。 */
    for (int i = 0; i < g->n_terrains; i++) {
        int to = g->terrains[i].breaks_idx;
        if (to < 0) continue;
        for (int mc = 0; mc < MC_COUNT; mc++)
            CHECK((g->terrains[i].mcost[mc] > 0) == (g->terrains[to].mcost[mc] > 0));
    }
    CHECK(g->terrains[road].breaks_idx == plain);
    CHECK(g->terrains[hq].breaks_idx < 0);
    CHECK(g->terrains[rubble].breaks_idx < 0);
    CHECK(g->terrains[city].repair_cost > 0);
    CHECK(!g->terrains[rubble].capturable);

    int eng = data_find_unit_type(g, "ENGINEER");
    int inf = data_find_unit_type(g, "INFANTRY");
    CHECK(eng >= 0 && inf >= 0);
    if (s_fail) return;
    CHECK(g->types[eng].engineer == 1);
    CHECK(g->types[eng].can_capture == 0);   /* 占領はさせない */
    CHECK(g->types[inf].engineer == 0);

    /* 盤面を作る: (5,5)に工兵、隣の(6,5)に自軍の都市 */
    g->tiles[5][6].terrain = (uint8_t)city;
    g->tiles[5][6].orig_terrain = (uint8_t)city;
    g->tiles[5][6].owner = 0;
    int ui = game_spawn_unit(g, 0, eng, 5, 5, 10);
    CHECK(ui >= 0);
    if (ui < 0) return;

    CHECK(game_unit_is_engineer(g, ui));
    CHECK(game_work_kind_at(g, ui, 6, 5) == WORK_DEMOLISH);
    /* 自分の足元は対象外（壊して自滕する事故を防ぐ） */
    CHECK(game_work_kind_at(g, ui, 5, 5) == WORK_NONE);
    /* 隣接していないヘクスも不可 */
    CHECK(game_work_kind_at(g, ui, 9, 9) == WORK_NONE);
    /* 人の居るヘクスは触らない */
    {
        int pi = game_spawn_unit(g, 1, inf, 6, 5, 10);
        CHECK(pi >= 0);
        CHECK(game_work_kind_at(g, ui, 6, 5) == WORK_NONE);
        g->units[pi].flags &= (uint8_t)~UF_ALIVE;
    }
    /* 首都は壊せない。壊せると工兵一つで勝敗がついてしまう。 */
    {
        uint8_t save = g->tiles[5][6].terrain;
        g->tiles[5][6].terrain = (uint8_t)hq;
        g->tiles[5][6].orig_terrain = (uint8_t)hq;
        CHECK(game_work_kind_at(g, ui, 6, 5) == WORK_NONE);
        g->tiles[5][6].terrain = save;
        g->tiles[5][6].orig_terrain = save;
    }

    /* 破壊: 瓦礫になり、所有も消え、工兵の手番が終わる */
    int before_funds = g->funds[0];
    CHECK(game_do_work(g, ui, 6, 5) == WORK_DEMOLISH);
    CHECK(g->tiles[5][6].terrain == rubble);
    CHECK(g->tiles[5][6].orig_terrain == city);   /* 本来の地形は覚えている */
    CHECK(g->tiles[5][6].owner == -1);
    CHECK(g->funds[0] == before_funds);           /* 破壊は無料 */
    CHECK(g->units[ui].flags & UF_DONE);

    /* 復旧: 資金が要る。足りなければできない。 */
    g->units[ui].flags &= (uint8_t)~UF_DONE;
    CHECK(game_work_kind_at(g, ui, 6, 5) == WORK_REPAIR);
    int cost = game_work_cost(g, 6, 5);
    CHECK(cost == g->terrains[city].repair_cost);
    g->funds[0] = cost - 1;
    CHECK(game_do_work(g, ui, 6, 5) == WORK_NONE);
    CHECK(g->tiles[5][6].terrain == rubble);      /* 失敗しても盤面は変わらない */
    g->funds[0] = cost + 100;
    CHECK(game_do_work(g, ui, 6, 5) == WORK_REPAIR);
    CHECK(g->tiles[5][6].terrain == city);
    CHECK(g->funds[0] == 100);
    /* 戻しても持ち主は中立。改めて占領し直す必要がある。 */
    CHECK(g->tiles[5][6].owner == -1);

    /* 工兵以外は何もできない */
    {
        g->n_units = 0;
        int ii = game_spawn_unit(g, 0, inf, 5, 5, 10);
        CHECK(ii >= 0);
        CHECK(!game_unit_is_engineer(g, ii));
        CHECK(game_work_kind_at(g, ii, 6, 5) == WORK_NONE);
        uint8_t xs[8], ys[8];
        CHECK(game_work_targets(g, ii, xs, ys, 8) == 0);
    }
}

/* チーム戦: 敵味方判定・視界共有・主力の首都で決着 */
static void test_teams(void)
{
    Game *g = &s_game;
    char err[256];

    memset(g, 0, sizeof *g);
    CHECK(data_load_terrain(g, "data/terrain.def", err, sizeof err) == 0);
    CHECK(data_load_units(g, "data/units.def", err, sizeof err) == 0);
    CHECK(data_load_map(g, "data/maps/m06_alliance.map", err, sizeof err) == 0);
    if (s_fail) return;

    /* .map のチーム指定が読めていること */
    CHECK(game_team_of(g, 0) == 0 && game_team_of(g, 2) == 0);
    CHECK(game_team_of(g, 1) == 1 && game_team_of(g, 3) == 1);
    CHECK(game_same_team(g, 0, 2));
    CHECK(!game_is_enemy(g, 0, 2));
    CHECK(game_is_enemy(g, 0, 1) && game_is_enemy(g, 0, 3));
    CHECK(game_is_enemy(g, 2, 1));

    g->fog = false;
    game_start(g, 5);
    CHECK(game_team_leader(g, 0) == 0);
    CHECK(game_team_leader(g, 1) == 1);

    /* --- 援軍は撃てない --- */
    {
        int inf = data_find_unit_type(g, "INFANTRY");
        g->n_units = 0;
        int a0 = game_spawn_unit(g, 0, inf, 9, 4, 10);   /* 自軍 */
        int a2 = game_spawn_unit(g, 2, inf, 10, 4, 10);  /* 援軍 */
        int e1 = game_spawn_unit(g, 1, inf, 8, 4, 10);   /* 敵 */
        game_update_vision(g);
        CHECK(!unit_can_attack_target(g, &g->units[a0], &g->units[a2]));
        CHECK(!unit_can_attack_target(g, &g->units[a2], &g->units[a0]));
        CHECK(unit_can_attack_target(g, &g->units[a0], &g->units[e1]));
        CHECK(unit_can_attack_target(g, &g->units[a2], &g->units[e1]));
        /* 対象一覧にも援軍は出ない */
        int t[32];
        int n = rules_list_targets(g, a0, g->units[a0].pos.x,
                                   g->units[a0].pos.y, t, 32);
        for (int i = 0; i < n; i++)
            CHECK(g->units[t[i]].owner != 2);
    }

    /* --- 援軍の拠点は占領しない --- */
    {
        int inf = data_find_unit_type(g, "INFANTRY");
        int city = -1;
        for (int i = 0; i < g->n_terrains; i++)
            if (!strcmp(g->terrains[i].id, "CITY")) city = i;
        CHECK(city >= 0);
        g->n_units = 0;
        g->tiles[6][6].terrain = (uint8_t)city;
        g->tiles[6][6].owner = 2;                        /* 援軍の都市 */
        int u0 = game_spawn_unit(g, 0, inf, 6, 6, 10);
        /* game_capture は「占領完了」でのみ1を返すので、進捗は cap_hp で見る */
        g->tiles[6][6].capturer = -1;
        g->tiles[6][6].cap_hp = CAPTURE_HP;
        CHECK(game_capture(g, u0) == 0);
        CHECK(g->tiles[6][6].cap_hp == CAPTURE_HP);      /* 援軍の拠点は進まない */

        g->tiles[6][6].owner = 1;                        /* 敵の都市なら進む */
        game_capture(g, u0);
        CHECK(g->tiles[6][6].cap_hp < CAPTURE_HP);

        g->tiles[6][6].owner = -1;                       /* 中立でも進む */
        g->tiles[6][6].capturer = -1;
        g->tiles[6][6].cap_hp = CAPTURE_HP;
        game_capture(g, u0);
        CHECK(g->tiles[6][6].cap_hp < CAPTURE_HP);
    }

    /* --- 視界はチーム内で共有される --- */
    {
        int inf = data_find_unit_type(g, "INFANTRY");
        g->fog = true;
        g->n_units = 0;
        /* 援軍だけを盤の隅に置く。自軍(0)からもそこが見えるはず */
        int a2 = game_spawn_unit(g, 2, inf, 30, 20, 10);
        CHECK(a2 >= 0);
        game_update_vision(g);
        CHECK(g->visible[0][20][30]);      /* 援軍の足元が自軍にも見える */
        CHECK(g->visible[2][20][30]);
        CHECK(!g->visible[1][20][30]);     /* 敵には見えない */
        g->fog = false;
    }

    /* --- 主力の首都が落ちたらチームの負け。援軍が健在でも決着 --- */
    {
        memset(g, 0, sizeof *g);
        CHECK(data_load_terrain(g, "data/terrain.def", err, sizeof err) == 0);
        CHECK(data_load_units(g, "data/units.def", err, sizeof err) == 0);
        CHECK(data_load_map(g, "data/maps/m06_alliance.map", err, sizeof err) == 0);
        if (s_fail) return;
        g->fog = false;
        game_start(g, 5);
        CHECK(g->winner == WINNER_NONE);

        /* 援軍(3)を丸ごと消しても、主力(1)が無事なら team1 は続く */
        for (int i = 0; i < g->n_units; i++)
            if (g->units[i].owner == 3) g->units[i].flags &= (uint8_t)~UF_ALIVE;
        game_check_victory(g);
        CHECK(g->winner == WINNER_NONE);
        CHECK(!game_team_defeated(g, 1));

        /* 主力(1)の首都を奪うと、援軍(3)の拠点が残っていても team1 の負け */
        for (int y = 0; y < g->h; y++)
            for (int x = 0; x < g->w; x++)
                if (g->terrains[g->tiles[y][x].terrain].is_hq &&
                    g->tiles[y][x].owner == 1)
                    g->tiles[y][x].owner = 0;
        CHECK(game_team_defeated(g, 1));
        game_check_victory(g);
        CHECK(g->winner == 0);             /* 勝者は team0 の主力 */
    }
}


/* 多陣営: 参加判定・勝敗・手番送り */
/* 常夜マップ（day_turns=0）。周期をマップごとに持つようにしたので、
 * ゼロ除算や「実は昼になるターンがある」を拾っておく。 */
static void test_endless_night(void)
{
    Game *g = &s_game;
    char err[256];
    memset(g, 0, sizeof *g);
    CHECK(data_load_terrain(g, "data/terrain.def", err, sizeof err) == 0);
    CHECK(data_load_units(g, "data/units.def", err, sizeof err) == 0);
    CHECK(data_load_map(g, "data/maps/f05_endlessnight.map", err, sizeof err) == 0);
    if (s_fail) { printf("  %s\n", err); return; }

    CHECK(g->night_on == 1);
    CHECK(g->day_turns == 0 && g->night_turns == 5);
    for (int t = 1; t <= 40; t++) {
        g->turn = t;
        CHECK(game_is_night(g));          /* 一度も昼にならない */
        CHECK(game_phase_left(g) >= 1);   /* 残りターンは常に正 */
    }
    /* 周期が 0/0 でも落ちないこと（手書き .map への保険） */
    g->day_turns = 0; g->night_turns = 0;
    g->turn = 7;
    (void)game_is_night(g);
    (void)game_phase_left(g);
}

static void test_multiplayer(void)
{
    Game *g = &s_game;
    char err[256];

    /* --- 2陣営マップ: 居ない陣営が「開幕即敗北」にならないこと --- */
    {
        memset(g, 0, sizeof *g);
        CHECK(data_load_terrain(g, "data/terrain.def", err, sizeof err) == 0);
        CHECK(data_load_units(g, "data/units.def", err, sizeof err) == 0);
        CHECK(data_load_map(g, "data/maps/m01_border_hills.map", err, sizeof err) == 0);
        if (s_fail) return;
        game_start(g, 5);
        CHECK(game_player_in_play(g, 0));
        CHECK(game_player_in_play(g, 1));
        for (int p = 2; p < MAX_PLAYERS; p++)
            CHECK(!game_player_in_play(g, p));   /* 居ない陣営は参加扱いしない */
        /* **居ない陣営の ctrl を CTRL_HUMAN(=0) のままにしないこと**。
         * 描画は「CPU手番中は人間の視界で描く」ために ctrl を見るので、
         * 幽霊の「人間」がいるとその空の視界で盤面が真っ暗になる。 */
        for (int p = 2; p < MAX_PLAYERS; p++)
            CHECK(g->ctrl[p] != CTRL_HUMAN);
        game_check_victory(g);
        CHECK(g->winner == WINNER_NONE);         /* 開幕で決着しない */

        /* 手番送り: 居ない陣営を飛ばして 0→1→0 と回ること */
        CHECK(g->current == 0);
        game_end_turn(g);
        CHECK(g->current == 1);
        int t0 = g->turn;
        game_end_turn(g);
        CHECK(g->current == 0);
        CHECK(g->turn == t0 + 1);                /* 一周でターンが1進む */
    }

    /* --- 操作者の既定がCPUであること --- */
    {
        /* CTRL_HUMAN は 0 なので、マップ読込で埋めないと
         * 設定し忘れた陣営が入力待ちで固まる。
         * 3陣営以上のマップで現実に起き得るので固めておく。 */
        memset(g, 0, sizeof *g);
        CHECK(data_load_terrain(g, "data/terrain.def", err, sizeof err) == 0);
        CHECK(data_load_units(g, "data/units.def", err, sizeof err) == 0);
        CHECK(data_load_map(g, "data/maps/m05_threeway.map", err, sizeof err) == 0);
        if (s_fail) return;
        for (int p = 0; p < MAX_PLAYERS; p++)
            CHECK(g->ctrl[p] != CTRL_HUMAN);
    }

    /* --- 全滅したら敗北確定、拠点は中立に戻る --- */
    {
        memset(g, 0, sizeof *g);
        CHECK(data_load_terrain(g, "data/terrain.def", err, sizeof err) == 0);
        CHECK(data_load_units(g, "data/units.def", err, sizeof err) == 0);
        CHECK(data_load_map(g, "data/maps/m05_threeway.map", err, sizeof err) == 0);
        if (s_fail) return;
        g->fog = false;
        game_start(g, 5);

        int before = 0;
        for (int y = 0; y < g->h; y++)
            for (int x = 0; x < g->w; x++)
                if (g->tiles[y][x].owner == 2) before++;
        CHECK(before > 0);                       /* 陣営2 は拠点を持っている */

        /* ユニットだけ全滅させる（拠点は触らない） */
        for (int i = 0; i < g->n_units; i++)
            if (g->units[i].owner == 2) g->units[i].flags &= (uint8_t)~UF_ALIVE;
        CHECK(game_player_defeated(g, 2));       /* 拠点が残っていても敗北 */

        game_check_victory(g);
        int after = 0, neutral_hq = 0;
        for (int y = 0; y < g->h; y++)
            for (int x = 0; x < g->w; x++) {
                if (g->tiles[y][x].owner == 2) after++;
                if (g->terrains[g->tiles[y][x].terrain].is_hq &&
                    g->tiles[y][x].owner < 0) neutral_hq++;
            }
        CHECK(after == 0);                       /* 拠点はすべて中立へ */
        CHECK(neutral_hq >= 1);                  /* 首都も中立になる */
        CHECK(g->winner == WINNER_NONE);         /* 残り2陣営なので続行 */
    }

    /* --- 3陣営マップ: 1つ倒れても続き、最後の1つが勝つ --- */
    {
        memset(g, 0, sizeof *g);
        CHECK(data_load_terrain(g, "data/terrain.def", err, sizeof err) == 0);
        CHECK(data_load_units(g, "data/units.def", err, sizeof err) == 0);
        CHECK(data_load_map(g, "data/maps/m05_threeway.map", err, sizeof err) == 0);
        if (s_fail) return;
        g->fog = false;
        game_start(g, 5);

        int n = 0;
        for (int p = 0; p < MAX_PLAYERS; p++) if (game_player_in_play(g, p)) n++;
        CHECK(n == 3);
        CHECK(g->winner == WINNER_NONE);

        /* 陣営2を消す（ユニット全滅＋首都を陣営0へ）。まだ2陣営残るので続く */
        for (int i = 0; i < g->n_units; i++)
            if (g->units[i].owner == 2) g->units[i].flags &= (uint8_t)~UF_ALIVE;
        for (int y = 0; y < g->h; y++)
            for (int x = 0; x < g->w; x++)
                if (g->tiles[y][x].owner == 2) g->tiles[y][x].owner = 0;
        CHECK(game_player_defeated(g, 2));
        game_check_victory(g);
        CHECK(g->winner == WINNER_NONE);         /* まだ 0 と 1 が残っている */

        /* 陣営1も消す → 陣営0の勝ち */
        for (int i = 0; i < g->n_units; i++)
            if (g->units[i].owner == 1) g->units[i].flags &= (uint8_t)~UF_ALIVE;
        for (int y = 0; y < g->h; y++)
            for (int x = 0; x < g->w; x++)
                if (g->tiles[y][x].owner == 1) g->tiles[y][x].owner = 0;
        game_check_victory(g);
        CHECK(g->winner == 0);
    }

    /* --- 手番送りが脱落陣営を飛ばすこと（3陣営で中央が脱落） --- */
    {
        memset(g, 0, sizeof *g);
        CHECK(data_load_terrain(g, "data/terrain.def", err, sizeof err) == 0);
        CHECK(data_load_units(g, "data/units.def", err, sizeof err) == 0);
        CHECK(data_load_map(g, "data/maps/m05_threeway.map", err, sizeof err) == 0);
        if (s_fail) return;
        g->fog = false;
        game_start(g, 5);
        /* 陣営1だけ全滅させる（首都は残すので勝敗はつかない） */
        for (int i = 0; i < g->n_units; i++)
            if (g->units[i].owner == 1) g->units[i].flags &= (uint8_t)~UF_ALIVE;
        CHECK(game_player_defeated(g, 1));
        CHECK(g->current == 0);
        game_end_turn(g);
        CHECK(g->current == 2);          /* 1 を飛ばして 2 へ */
    }
}

/* 昼夜: 固定周期・領域制限・攻撃補正・視界・射程・地形防御 */
static void test_daynight(void)
{
    Game *g = &s_game;
    char err[256];
    memset(g, 0, sizeof *g);
    CHECK(data_load_terrain(g, "data/terrain.def", err, sizeof err) == 0);
    CHECK(data_load_units(g, "data/units.def", err, sizeof err) == 0);
    CHECK(data_load_map(g, "data/maps/test_arena.map", err, sizeof err) == 0);
    if (s_fail) return;
    CHECK(g->night_on == 1);              /* 既定で有効 */
    CHECK(g->day_turns == DAY_TURNS && g->night_turns == NIGHT_TURNS);
    g->fog = false;
    game_start(g, 3);

    /* 周期: 昼3ターン → 夜2ターン の繰り返し。ターン数から一意に決まる */
    {
        static const int NIGHT[12] = { 0,0,0,1,1, 0,0,0,1,1, 0,0 };
        static const int LEFT[12]  = { 3,2,1,2,1, 3,2,1,2,1, 3,2 };
        for (int t = 1; t <= 12; t++) {
            g->turn = t;
            CHECK(game_is_night(g) == (bool)NIGHT[t - 1]);
            CHECK(game_phase_left(g) == LEFT[t - 1]);
        }
        g->night_on = 0;
        g->turn = 4;
        CHECK(!game_is_night(g));         /* 切ってあるマップは常に昼 */
        g->night_on = 1;
    }

    int inf   = data_find_unit_type(g, "INFANTRY");
    int fight = data_find_unit_type(g, "FIGHTER");
    int aa    = data_find_unit_type(g, "AA_TANK");
    int ninja = data_find_unit_type(g, "NIGHT_INF");
    int arty  = data_find_unit_type(g, "ARTILLERY");
    CHECK(inf >= 0 && fight >= 0 && aa >= 0 && ninja >= 0 && arty >= 0);
    if (s_fail) return;

    /* 領域制限: 夜は陸VS陸・空VS空だけ。またぐ攻撃は成立しない */
    {
        g->n_units = 0;
        int ga = game_spawn_unit(g, 0, aa,    9, 4, 10);   /* 地上の対空 */
        int pa = game_spawn_unit(g, 1, fight, 10, 4, 10);  /* 空 */
        int gb = game_spawn_unit(g, 1, inf,   8, 4, 10);   /* 地上 */
        game_update_vision(g);

        g->turn = 1;                                       /* 昼 */
        CHECK(unit_can_attack_target(g, &g->units[ga], &g->units[pa]));
        CHECK(unit_can_attack_target(g, &g->units[ga], &g->units[gb]));

        g->turn = 4;                                       /* 夜 */
        CHECK(!unit_can_attack_target(g, &g->units[ga], &g->units[pa]));  /* 地→空 不可 */
        CHECK(unit_can_attack_target(g, &g->units[ga], &g->units[gb]));   /* 地→地 可 */
        CHECK(!unit_can_attack_target(g, &g->units[pa], &g->units[gb]));  /* 空→地 不可 */
    }

    /* 攻撃補正: 夜間ユニット+50% / 通常-20% */
    {
        g->n_units = 0;
        int na = game_spawn_unit(g, 0, ninja, 9, 4, 10);
        int nb = game_spawn_unit(g, 0, inf,  11, 4, 10);
        int td = game_spawn_unit(g, 1, inf,  10, 4, 10);
        game_update_vision(g);

        g->turn = 1;
        CHECK(game_night_atk_pct(g, &g->units[na]) == 100);
        CHECK(game_night_atk_pct(g, &g->units[nb]) == 100);
        int day_night_unit = battle_expect_damage_x10(g, &g->units[na], &g->units[td]);
        int day_normal     = battle_expect_damage_x10(g, &g->units[nb], &g->units[td]);

        g->turn = 4;
        CHECK(game_night_atk_pct(g, &g->units[na]) == 150);
        CHECK(game_night_atk_pct(g, &g->units[nb]) == 80);
        CHECK(battle_expect_damage_x10(g, &g->units[na], &g->units[td]) > day_night_unit);
        CHECK(battle_expect_damage_x10(g, &g->units[nb], &g->units[td]) < day_normal);
    }

    /* 視界: 夜は-2。ただし夜間ユニットは落ちない */
    {
        g->turn = 4;
        CHECK(game_night_vision_mod(g) == -2);
        g->turn = 1;
        CHECK(game_night_vision_mod(g) == 0);

        g->fog = true;
        g->n_units = 0;
        game_spawn_unit(g, 0, inf, 10, 10, 10);
        g->turn = 1; game_update_vision(g);
        int day_seen = 0;
        for (int y = 0; y < g->h; y++) for (int x = 0; x < g->w; x++)
            day_seen += g->visible[0][y][x];
        g->turn = 4; game_update_vision(g);
        int night_seen = 0;
        for (int y = 0; y < g->h; y++) for (int x = 0; x < g->w; x++)
            night_seen += g->visible[0][y][x];
        CHECK(night_seen < day_seen);          /* 通常兵は夜に見えなくなる */

        g->n_units = 0;
        game_spawn_unit(g, 0, ninja, 10, 10, 10);
        g->turn = 1; game_update_vision(g);
        int nd = 0;
        for (int y = 0; y < g->h; y++) for (int x = 0; x < g->w; x++)
            nd += g->visible[0][y][x];
        g->turn = 4; game_update_vision(g);
        int nn = 0;
        for (int y = 0; y < g->h; y++) for (int x = 0; x < g->w; x++)
            nn += g->visible[0][y][x];
        CHECK(nn == nd);                       /* 夜間兵は夜でも同じ */
        g->fog = false;
    }

    /* 射程: 夜は間接攻撃の最大射程が1減る。直射は変わらない */
    {
        const UnitType *at = &g->types[arty];
        const UnitType *it = &g->types[inf];
        g->turn = 1;
        CHECK(game_range_max(g, at) == at->range_max);
        CHECK(game_range_max(g, it) == it->range_max);
        g->turn = 4;
        CHECK(game_range_max(g, at) == at->range_max - 1);
        CHECK(game_range_max(g, it) == it->range_max);
    }

    /* 反撃は「反撃する側の残りHP」に比例する。
     * このため夜は、与ダメージが減って相手が多くHPを残す分、
     * 反撃が“絶対値で”強くなる。バグに見えやすいので意図した振る舞いとして固める。 */
    {
        g->n_units = 0;
        int ia = game_spawn_unit(g, 0, inf, 9, 4, 10);
        int ib = game_spawn_unit(g, 1, inf, 10, 4, 10);
        game_update_vision(g);
        int d_dmg, d_hp, d_cnt, d_ahp;
        int n_dmg, n_hp, n_cnt, n_ahp;
        g->turn = 1;
        battle_forecast(g, ia, ib, &d_dmg, &d_hp, &d_cnt, &d_ahp);
        g->turn = 4;
        battle_forecast(g, ia, ib, &n_dmg, &n_hp, &n_cnt, &n_ahp);
        CHECK(n_dmg < d_dmg);      /* 夜は与ダメージが減る */
        CHECK(n_hp  > d_hp);       /* 相手の残りHPは多い */
        CHECK(n_cnt >= d_cnt);     /* その分反撃は弱くならない */
    }

    /* 地形防御: 夜は森・都市（hide=1）だけ余分に硬くなる。
     * 平地と比べて「夜の落ち込み幅」が森のほうが大きいことを見る。 */
    {
        int plain = -1, forest = -1;
        for (int i = 0; i < g->n_terrains; i++) {
            if (!strcmp(g->terrains[i].id, "PLAIN"))  plain = i;
            if (!strcmp(g->terrains[i].id, "FOREST")) forest = i;
        }
        CHECK(plain >= 0 && forest >= 0);
        g->n_units = 0;
        int at2 = game_spawn_unit(g, 0, inf, 9, 4, 10);
        int df  = game_spawn_unit(g, 1, inf, 10, 4, 10);
        game_update_vision(g);

        g->tiles[4][10].terrain = (uint8_t)forest;
        g->turn = 1;
        int day_forest = battle_expect_damage_x10(g, &g->units[at2], &g->units[df]);
        g->turn = 4;
        int night_forest = battle_expect_damage_x10(g, &g->units[at2], &g->units[df]);

        g->tiles[4][10].terrain = (uint8_t)plain;
        g->turn = 1;
        int day_plain = battle_expect_damage_x10(g, &g->units[at2], &g->units[df]);
        g->turn = 4;
        int night_plain = battle_expect_damage_x10(g, &g->units[at2], &g->units[df]);

        CHECK(night_forest < day_forest);
        CHECK(night_plain < day_plain);
        /* night_forest/day_forest < night_plain/day_plain を整数で比較する */
        CHECK(night_forest * day_plain < night_plain * day_forest);
    }
}

/* 天候: 曇=空↔地上の攻撃半減 / 雨=同攻撃不可・視界-2・地上移動-1 */
static void test_weather(void)
{
    Game *g = &s_game;
    memset(g, 0, sizeof *g);
    char err[256];
    CHECK(data_load_terrain(g, "data/terrain.def", err, sizeof err) == 0);
    CHECK(data_load_units(g, "data/units.def", err, sizeof err) == 0);
    CHECK(data_load_map(g, "data/maps/test_arena.map", err, sizeof err) == 0);
    CHECK(g->weather_on == 1);                 /* 既定で有効 */
    g->fog = false;
    game_start(g, 5);
    CHECK(game_weather(g) == WX_CLEAR);        /* 開幕は必ず晴 */
    g->n_units = 0;

    int inf = data_find_unit_type(g, "INFANTRY");
    int aa  = data_find_unit_type(g, "AA_TANK");
    int fig = data_find_unit_type(g, "FIGHTER");
    int dd  = data_find_unit_type(g, "DESTROYER");

    int ga = game_spawn_unit(g, 0, aa, 9, 4, 10);    /* 対空戦車(地上) */
    int ea = game_spawn_unit(g, 1, fig, 10, 4, 10);  /* 敵戦闘機(空) */
    int gf = game_spawn_unit(g, 0, fig, 9, 5, 10);   /* 味方戦闘機(空) */
    int gi = game_spawn_unit(g, 1, inf, 10, 5, 10);  /* 敵歩兵(地上) */
    game_update_vision(g);

    /* 晴: 通常どおり */
    int clear_ground_to_air = battle_expect_damage_x10(g, &g->units[ga], &g->units[ea]);
    int clear_air_to_air    = battle_expect_damage_x10(g, &g->units[gf], &g->units[ea]);
    int clear_air_to_ground = battle_expect_damage_x10(g, &g->units[gf], &g->units[gi]);
    CHECK(clear_ground_to_air > 0 && clear_air_to_air > 0 && clear_air_to_ground > 0);

    /* 曇: 空↔地上は半減、空↔空は変化なし */
    g->weather = WX_CLOUDY;
    CHECK(battle_expect_damage_x10(g, &g->units[ga], &g->units[ea])
          == clear_ground_to_air / 2);
    CHECK(battle_expect_damage_x10(g, &g->units[gf], &g->units[gi])
          == clear_air_to_ground / 2);
    CHECK(battle_expect_damage_x10(g, &g->units[gf], &g->units[ea])
          == clear_air_to_air);                /* 空戦は影響なし */
    CHECK(game_weather_vision_mod(g) == -1);

    /* 雨: 空↔地上は攻撃不可（対象一覧にも出ない）、空戦は可能 */
    g->weather = WX_RAIN;
    CHECK(!unit_can_attack_target(g, &g->units[ga], &g->units[ea]));
    CHECK(!unit_can_attack_target(g, &g->units[gf], &g->units[gi]));
    CHECK(unit_can_attack_target(g, &g->units[gf], &g->units[ea]));
    {
        int tg[32];
        int n = rules_list_targets(g, ga, 9, 4, tg, 32);
        for (int i = 0; i < n; i++) CHECK(tg[i] != ea);  /* 空は狙えない */
    }
    CHECK(game_weather_vision_mod(g) == -2);

    /* 雨: 地上ユニットだけ移動-1（空・海は不変） */
    g->n_units = 0;
    int tk = game_spawn_unit(g, 0, inf, 9, 4, 10);
    int sh = game_spawn_unit(g, 0, dd, 20, 10, 10);
    g->weather = WX_CLEAR;
    MoveRange mr0, mr1;
    path_move_range(g, tk, &mr0);
    int sea_clear = 0;
    path_move_range(g, sh, &mr1);
    for (int y = 0; y < g->h; y++) for (int x = 0; x < g->w; x++)
        if (mr1.cost[y][x] >= 0) sea_clear++;
    int land_clear = 0;
    for (int y = 0; y < g->h; y++) for (int x = 0; x < g->w; x++)
        if (mr0.cost[y][x] >= 0) land_clear++;
    g->weather = WX_RAIN;
    path_move_range(g, tk, &mr0);
    int land_rain = 0;
    for (int y = 0; y < g->h; y++) for (int x = 0; x < g->w; x++)
        if (mr0.cost[y][x] >= 0) land_rain++;
    path_move_range(g, sh, &mr1);
    int sea_rain = 0;
    for (int y = 0; y < g->h; y++) for (int x = 0; x < g->w; x++)
        if (mr1.cost[y][x] >= 0) sea_rain++;
    CHECK(land_rain < land_clear);             /* 地上は狭まる */
    CHECK(sea_rain == sea_clear);              /* 艦船は不変 */

    /* 視界の下限1は割り込まない */
    g->fog = true;
    g->weather = WX_RAIN;
    game_update_vision(g);                     /* 落ちないこと */

    /* weather=0 のマップでは常に晴 */
    g->weather_on = 0;
    g->weather = WX_RAIN;
    CHECK(game_weather(g) == WX_CLEAR);
    CHECK(game_weather_vision_mod(g) == 0);

    /* 天候の推移: ラウンド単位で変化し、両陣営が同じ天候を共有すること。
     * 長く回せば3種類とも出現し、晴が最も多くなる（60/30/10）。 */
    {
        Game g2;
        memset(&g2, 0, sizeof g2);
        CHECK(data_load_terrain(&g2, "data/terrain.def", err, sizeof err) == 0);
        CHECK(data_load_units(&g2, "data/units.def", err, sizeof err) == 0);
        CHECK(data_load_map(&g2, "data/maps/test_arena.map", err, sizeof err) == 0);
        g2.turn_limit = 0;
        game_start(&g2, 12345);
        int seen[WX_COUNT] = { 0, 0, 0 };
        int changes = 0;
        Weather prev = game_weather(&g2);
        for (int r = 0; r < 400; r++) {
            Weather at_p0 = game_weather(&g2);
            game_end_turn(&g2);                 /* P0 → P1（同じラウンド内） */
            CHECK(game_weather(&g2) == at_p0);  /* 手番が移っても天候は同じ */
            game_end_turn(&g2);                 /* P1 → P0（ラウンド更新） */
            Weather now = game_weather(&g2);
            seen[now]++;
            if (now != prev) changes++;
            prev = now;
            if (g2.winner != WINNER_NONE) break;
        }
        CHECK(changes > 0);                     /* 変化はする */
        CHECK(seen[WX_CLEAR] > 0 && seen[WX_CLOUDY] > 0 && seen[WX_RAIN] > 0);
        CHECK(seen[WX_CLEAR] > seen[WX_RAIN]);  /* 晴が雨より多い */
        /* 2〜4ラウンド continue するので、毎ラウンド変わるわけではない */
        CHECK(changes < 400 / 2);
    }
}

/* 作戦評価（S/A/B/C）とランク記録 */
static void test_rank(void)
{
    Game *g = &s_game;
    memset(g, 0, sizeof *g);
    char err[256];
    CHECK(data_load_terrain(g, "data/terrain.def", err, sizeof err) == 0);
    CHECK(data_load_units(g, "data/units.def", err, sizeof err) == 0);
    CHECK(data_load_map(g, "data/maps/test_arena.map", err, sizeof err) == 0);
    game_start(g, 1);

    CpnNode node;
    memset(&node, 0, sizeof node);
    node.par_turns = 20;

    CpnScore sc;
    /* 理想的な戦い: 基準ターン内・無損失・敵を10体撃破 → S */
    g->turn = 15; g->lost_units[0] = 0; g->lost_units[1] = 10;
    campaign_evaluate(g, &node, &sc);
    CHECK(sc.speed == 100 && sc.loss == 100 && sc.power == 100);
    CHECK(sc.rank == RANK_S);
    CHECK(campaign_rank_bonus(sc.rank) == 2000);
    CHECK(!strcmp(campaign_rank_str(sc.rank), "S"));

    /* 大幅超過・大損害・戦果ゼロ → C */
    g->turn = 80; g->lost_units[0] = 20; g->lost_units[1] = 0;
    campaign_evaluate(g, &node, &sc);
    CHECK(sc.speed == 0 && sc.loss == 0 && sc.power == 0);
    CHECK(sc.rank == RANK_C);
    CHECK(campaign_rank_bonus(sc.rank) == 0);

    /* 各項目が単調: 遅いほど speed が下がる／損失が多いほど loss が下がる */
    g->turn = 25; g->lost_units[0] = 2; g->lost_units[1] = 5;
    campaign_evaluate(g, &node, &sc);
    int s1 = sc.speed, l1 = sc.loss;
    g->turn = 30; g->lost_units[0] = 4;
    campaign_evaluate(g, &node, &sc);
    CHECK(sc.speed < s1);
    CHECK(sc.loss < l1);
    /* 総合は3項目の平均、ランクは総合と整合 */
    CHECK(sc.total == (sc.speed + sc.loss + sc.power) / 3);
    CHECK(sc.rank >= RANK_S && sc.rank <= RANK_C);

    /* par_turns 未指定なら turn_limit の半分が基準 */
    memset(&node, 0, sizeof node);
    g->turn_limit = 40;
    g->turn = 20; g->lost_units[0] = 0; g->lost_units[1] = 10;
    campaign_evaluate(g, &node, &sc);
    CHECK(sc.speed == 100);          /* 20 <= 40/2 */

    /* 勝利時にランクが記録され、再挑戦で悪化しないこと */
    {
        Campaign c;
        CHECK(campaign_load(&c, "data/campaign/main.cpn", err, sizeof err) == 0);
        CampaignState st;
        memset(&st, 0, sizeof st);
        snprintf(st.node, sizeof st.node, "M01");
        CHECK(st.rank[0] == RANK_NONE);

        /* まず好成績でクリア */
        g->winner = 0; g->turn = 5; g->lost_units[0] = 0; g->lost_units[1] = 10;
        campaign_on_victory(g, &c, &st);
        uint8_t good = st.rank[0];
        CHECK(good == RANK_S || good == RANK_A);

        /* 同じ作戦を悪い成績でやり直しても、良い方が残る */
        snprintf(st.node, sizeof st.node, "M01");
        g->winner = 0; g->turn = 99; g->lost_units[0] = 30; g->lost_units[1] = 0;
        campaign_on_victory(g, &c, &st);
        CHECK(st.rank[0] == good);
    }
}

/* 出撃部隊の手動選択（上限超過時にどの部隊を出すか選べる） */
static void test_deploy_select(void)
{
    Game *g = &s_game;
    char err[256];
    Campaign c;
    memset(g, 0, sizeof *g);
    CHECK(data_load_terrain(g, "data/terrain.def", err, sizeof err) == 0);
    CHECK(data_load_units(g, "data/units.def", err, sizeof err) == 0);
    CHECK(campaign_load(&c, "data/campaign/main.cpn", err, sizeof err) == 0);
    if (s_fail) return;

    int inf = data_find_unit_type(g, "INFANTRY");
    int tank = data_find_unit_type(g, "TANK");

    /* まず上限を調べる（マップだけ用意した状態で数える） */
    CampaignState s0;
    memset(&s0, 0, sizeof s0);
    snprintf(s0.node, sizeof s0.node, "M01");
    CHECK(campaign_setup_map(g, &c, &s0, "", err, sizeof err) == 0);
    int limit = campaign_deploy_limit(g);
    CHECK(limit > 0);

    /* 上限より多い持越しを用意する。前半=歩兵 / 後半=戦車 で見分ける */
    CampaignState s1;
    memset(&s1, 0, sizeof s1);
    snprintf(s1.node, sizeof s1.node, "M01");
    s1.n_carry = limit + 4;
    if (s1.n_carry > MAX_CARRY_UNITS) s1.n_carry = MAX_CARRY_UNITS;
    for (int i = 0; i < s1.n_carry; i++) {
        s1.carry[i].type = (uint8_t)((i < s1.n_carry / 2) ? inf : tank);
        s1.carry[i].exp = (uint8_t)(100 - i);   /* 経験値降順 */
    }

    /* 「後半（戦車）だけ出す」と明示選択する。
     * 自動（経験値順）なら前半の歩兵が出るはずなので、選択が効いていれば
     * 盤上の持越しは戦車だけになる。 */
    uint8_t sel[MAX_CARRY_UNITS];
    memset(sel, 0, sizeof sel);
    int want = 0;
    for (int i = s1.n_carry - 1; i >= 0 && want < limit; i--)
        if (s1.carry[i].type == tank) { sel[i] = 1; want++; }
    CHECK(want > 0);

    memset(g, 0, sizeof *g);
    CHECK(data_load_terrain(g, "data/terrain.def", err, sizeof err) == 0);
    CHECK(data_load_units(g, "data/units.def", err, sizeof err) == 0);
    CHECK(campaign_setup_map(g, &c, &s1, "", err, sizeof err) == 0);
    int base = 0;
    for (int i = 0; i < g->n_units; i++)
        if ((g->units[i].flags & UF_ALIVE) && g->units[i].owner == 0) base++;
    campaign_begin(g, &c, &s1, 5, sel);

    /* 選んだぶんだけ増えている */
    int p0 = 0, extra_inf = 0;
    for (int i = 0; i < g->n_units; i++) {
        const Unit *u = &g->units[i];
        if (!(u->flags & UF_ALIVE) || u->owner != 0) continue;
        p0++;
    }
    CHECK(p0 == base + want);
    /* 選ばなかった部隊は消えず倉庫へ */
    CHECK(s1.n_store == s1.n_carry - want);
    (void)extra_inf;

    /* 選択なし（NULL）なら従来どおり経験値順に上限まで */
    CampaignState s2;
    memset(&s2, 0, sizeof s2);
    snprintf(s2.node, sizeof s2.node, "M01");
    s2.n_carry = limit + 4;
    if (s2.n_carry > MAX_CARRY_UNITS) s2.n_carry = MAX_CARRY_UNITS;
    for (int i = 0; i < s2.n_carry; i++) {
        s2.carry[i].type = (uint8_t)inf;
        s2.carry[i].exp = (uint8_t)(100 - i);
    }
    memset(g, 0, sizeof *g);
    CHECK(data_load_terrain(g, "data/terrain.def", err, sizeof err) == 0);
    CHECK(data_load_units(g, "data/units.def", err, sizeof err) == 0);
    CHECK(campaign_setup_map(g, &c, &s2, "", err, sizeof err) == 0);
    campaign_begin(g, &c, &s2, 5, NULL);
    int p2 = 0;
    for (int i = 0; i < g->n_units; i++)
        if ((g->units[i].flags & UF_ALIVE) && g->units[i].owner == 0) p2++;
    CHECK(p2 == base + limit);
    CHECK(s2.n_store == s2.n_carry - limit);

    /* 上限を超える選択をしても上限は必ず守られる（全部を選んだ場合） */
    CampaignState s3;
    memset(&s3, 0, sizeof s3);
    snprintf(s3.node, sizeof s3.node, "M01");
    s3.n_carry = limit + 4;
    if (s3.n_carry > MAX_CARRY_UNITS) s3.n_carry = MAX_CARRY_UNITS;
    for (int i = 0; i < s3.n_carry; i++) {
        s3.carry[i].type = (uint8_t)inf;
        s3.carry[i].exp = 50;
    }
    uint8_t all[MAX_CARRY_UNITS];
    memset(all, 1, sizeof all);
    memset(g, 0, sizeof *g);
    CHECK(data_load_terrain(g, "data/terrain.def", err, sizeof err) == 0);
    CHECK(data_load_units(g, "data/units.def", err, sizeof err) == 0);
    CHECK(campaign_setup_map(g, &c, &s3, "", err, sizeof err) == 0);
    campaign_begin(g, &c, &s3, 5, all);
    int p3 = 0;
    for (int i = 0; i < g->n_units; i++)
        if ((g->units[i].flags & UF_ALIVE) && g->units[i].owner == 0) p3++;
    CHECK(p3 == base + limit);                 /* 上限超過は起きない */
}

/* 持越しで自軍が増えた分、敵にも増援が入って戦力差が開きすぎないこと */
static void test_enemy_reinforce(void)
{
    Game *g = &s_game;
    char err[256];
    Campaign c;
    memset(g, 0, sizeof *g);
    CHECK(data_load_terrain(g, "data/terrain.def", err, sizeof err) == 0);
    CHECK(data_load_units(g, "data/units.def", err, sizeof err) == 0);
    CHECK(campaign_load(&c, "data/campaign/main.cpn", err, sizeof err) == 0);
    if (s_fail) return;

    int inf = data_find_unit_type(g, "INFANTRY");

    /* マップ本来の戦力を数える */
    CampaignState s0;
    memset(&s0, 0, sizeof s0);
    snprintf(s0.node, sizeof s0.node, "M01");
    CHECK(campaign_setup_map(g, &c, &s0, "", err, sizeof err) == 0);
    int base_p0 = 0, base_p1 = 0;
    for (int i = 0; i < g->n_units; i++) {
        if (!(g->units[i].flags & UF_ALIVE)) continue;
        if (g->units[i].owner == 0) base_p0++; else base_p1++;
    }
    int limit = campaign_deploy_limit(g);
    CHECK(base_p1 > 0);

    /* (1) 持越しゼロなら敵の増援もゼロ（負けが込んでいるときに詰まないこと） */
    CampaignState sz;
    memset(&sz, 0, sizeof sz);
    snprintf(sz.node, sizeof sz.node, "M01");
    memset(g, 0, sizeof *g);
    CHECK(data_load_terrain(g, "data/terrain.def", err, sizeof err) == 0);
    CHECK(data_load_units(g, "data/units.def", err, sizeof err) == 0);
    CHECK(campaign_setup_map(g, &c, &sz, "", err, sizeof err) == 0);
    campaign_begin(g, &c, &sz, 5, NULL);
    int z1 = 0;
    for (int i = 0; i < g->n_units; i++)
        if ((g->units[i].flags & UF_ALIVE) && g->units[i].owner == 1) z1++;
    CHECK(z1 == base_p1);

    /* (2) 上限いっぱい持ち越すと敵にも増援が入る */
    CampaignState s1;
    memset(&s1, 0, sizeof s1);
    snprintf(s1.node, sizeof s1.node, "M01");
    s1.n_carry = limit;
    if (s1.n_carry > MAX_CARRY_UNITS) s1.n_carry = MAX_CARRY_UNITS;
    for (int i = 0; i < s1.n_carry; i++) {
        s1.carry[i].type = (uint8_t)inf;
        s1.carry[i].exp = 50;
    }
    memset(g, 0, sizeof *g);
    CHECK(data_load_terrain(g, "data/terrain.def", err, sizeof err) == 0);
    CHECK(data_load_units(g, "data/units.def", err, sizeof err) == 0);
    CHECK(campaign_setup_map(g, &c, &s1, "", err, sizeof err) == 0);
    campaign_begin(g, &c, &s1, 5, NULL);
    int p0 = 0, p1 = 0;
    for (int i = 0; i < g->n_units; i++) {
        if (!(g->units[i].flags & UF_ALIVE)) continue;
        if (g->units[i].owner == 0) p0++; else p1++;
    }
    int carried = p0 - base_p0;
    CHECK(carried > 0);
    int added = p1 - base_p1;
    CHECK(added > 0);                                    /* 増援が入った */
    CHECK(added <= carried);                             /* 増やしすぎない */
    CHECK(p1 <= base_p1 * CAMPAIGN_ENEMY_MAX_RATIO);     /* 上限倍率を守る */

    /* (3) 増援を含めても敵ユニットは全て進入可能な地形の上にいる
     *     （艦船が陸に湧く等の配置ミスがないこと） */
    for (int i = 0; i < g->n_units; i++) {
        const Unit *u = &g->units[i];
        if (!(u->flags & UF_ALIVE) || (u->flags & UF_LOADED)) continue;
        MoveClass mc = (MoveClass)g->types[u->type].mclass;
        CHECK(g->terrains[g->tiles[u->pos.y][u->pos.x].terrain].mcost[mc] > 0);
    }
}

/* 合流: HP/燃料/弾薬の合算、払戻し、熟練度継承、禁止条件 */
static void test_join(void)
{
    Game *g = &s_game;
    char err[256];
    memset(g, 0, sizeof *g);
    CHECK(data_load_terrain(g, "data/terrain.def", err, sizeof err) == 0);
    CHECK(data_load_units(g, "data/units.def", err, sizeof err) == 0);
    CHECK(data_load_map(g, "data/maps/test_arena.map", err, sizeof err) == 0);
    if (s_fail) return;

    int inf  = data_find_unit_type(g, "INFANTRY");
    int tank = data_find_unit_type(g, "TANK");
    const UnitType *it = &g->types[inf];

    /* --- 基本: HP4 + HP3 = HP7、はみ出し無しなので払戻しも無し --- */
    g->n_units = 0;
    int a1 = game_spawn_unit(g, 0, inf, 3, 3, 4);
    int b1 = game_spawn_unit(g, 0, inf, 3, 4, 3);
    g->units[a1].fuel = 2; g->units[b1].fuel = 3;
    g->units[a1].ammo = 1; g->units[b1].ammo = 2;
    g->units[a1].exp  = 80; g->units[b1].exp = 10;   /* 高い方(80)が残るはず */
    int lost0 = g->lost_units[0];
    CHECK(game_can_join(g, a1, b1) == true);
    CHECK(game_join_units(g, a1, b1) == 0);
    CHECK(g->units[b1].hp == 7);
    CHECK(g->units[b1].fuel == 5);
    CHECK(g->units[b1].ammo == 3);
    CHECK(g->units[b1].exp == 80);                       /* 熟練度は高い方を継承 */
    CHECK((g->units[b1].flags & UF_DONE) != 0);          /* 合流先は行動終了 */
    CHECK(!(g->units[a1].flags & UF_ALIVE));             /* 動いた側は消える */
    CHECK(g->lost_units[0] == lost0);                    /* 撃破ではないので損失に数えない */

    /* --- はみ出し分は資金で払い戻される --- */
    g->n_units = 0;
    g->funds[0] = 0;
    int a2 = game_spawn_unit(g, 0, inf, 3, 3, 8);
    int b2 = game_spawn_unit(g, 0, inf, 3, 4, 6);
    int refund = game_join_units(g, a2, b2);
    CHECK(g->units[b2].hp == 10);
    CHECK(refund == it->cost * 4 / 10);                  /* 超過4HP分 */
    CHECK(g->funds[0] == refund);

    /* --- 燃料・弾薬は種別の上限を超えない --- */
    g->n_units = 0;
    int a3 = game_spawn_unit(g, 0, inf, 3, 3, 5);
    int b3 = game_spawn_unit(g, 0, inf, 3, 4, 5);
    g->units[a3].fuel = it->fuel; g->units[b3].fuel = it->fuel;
    g->units[a3].ammo = it->ammo; g->units[b3].ammo = it->ammo;
    game_join_units(g, a3, b3);
    CHECK(g->units[b3].fuel == it->fuel);
    CHECK(g->units[b3].ammo == it->ammo);

    /* --- 禁止条件 --- */
    g->n_units = 0;
    int x1 = game_spawn_unit(g, 0, inf,  3, 3, 5);
    int y1 = game_spawn_unit(g, 1, inf,  3, 4, 5);   /* 敵 */
    int y2 = game_spawn_unit(g, 0, tank, 3, 5, 5);   /* 別種別 */
    int y3 = game_spawn_unit(g, 0, inf,  3, 6, 10);  /* 満タン */
    CHECK(game_can_join(g, x1, y1) == false);
    CHECK(game_can_join(g, x1, y2) == false);
    CHECK(game_can_join(g, x1, y3) == false);
    CHECK(game_can_join(g, x1, x1) == false);
    CHECK(game_join_units(g, x1, y1) == 0);          /* 実行しても何も起きない */
    CHECK((g->units[x1].flags & UF_ALIVE) != 0);

    /* --- 輸送中のユニットを巻き添えにしない（積載側は合流できない） --- */
    int tr = data_find_unit_type(g, "TRUCK");
    if (tr >= 0) {
        g->n_units = 0;
        int t1 = game_spawn_unit(g, 0, tr, 3, 3, 5);
        int t2 = game_spawn_unit(g, 0, tr, 3, 4, 5);
        int rider = game_spawn_unit(g, 0, inf, 3, 5, 10);
        if (game_can_board(g, rider, t2)) {
            game_load_unit(g, rider, t2);
            CHECK(game_can_join(g, t1, t2) == false);
            CHECK((g->units[rider].flags & UF_ALIVE) != 0);
        }
    }

    /* --- 移動範囲: 合流できる味方のマスは停止可、できない味方のマスは不可 --- */
    g->n_units = 0;
    int mv  = game_spawn_unit(g, 0, inf, 3, 3, 5);
    int ok  = game_spawn_unit(g, 0, inf, 4, 3, 5);    /* 同種・損傷 → 乗れる */
    int ng  = game_spawn_unit(g, 0, tank, 3, 4, 5);   /* 別種 → 乗れない */
    static MoveRange mr;
    path_move_range(g, mv, &mr);
    CHECK(mr.stop[3][4] == 1);
    CHECK(mr.stop[4][3] == 0);
    (void)ok; (void)ng;
}

/* AIが指揮官の必殺技を撃つこと（技ごとに撃ち時が違うので種類別に確認） */
static void test_ai_co_power(void)
{
    Game *g = &s_game;
    char err[256];

    /* AI(P1)の1手番を最後まで回すヘルパ相当の処理を各ケースで行う */
    struct { const char *co; const char *what; } cases[] = {
        { "GRAF",  "HEAL"  },   /* 手番開始に撃つ */
        { "LIESE", "RUSH"  },   /* 全ユニット行動後に撃つ */
        { "BALT",  "STRIKE"},   /* 手番開始に撃つ */
        { "KARLA", "SCOUT" },   /* 手番開始に撃つ */
    };

    for (int k = 0; k < (int)(sizeof cases / sizeof cases[0]); k++) {
        memset(g, 0, sizeof *g);
        CHECK(data_load_terrain(g, "data/terrain.def", err, sizeof err) == 0);
        CHECK(data_load_units(g, "data/units.def", err, sizeof err) == 0);
        CHECK(data_load_commanders(g, "data/commanders.def", err, sizeof err) == 0);
        CHECK(data_load_map(g, "data/maps/test_arena.map", err, sizeof err) == 0);
        if (s_fail) return;

        int co = data_find_commander(g, cases[k].co);
        CHECK(co >= 0);
        if (co < 0) return;

        g->fog = false;
        g->ctrl[0] = CTRL_HUMAN;
        g->ctrl[1] = CTRL_CPU_NORMAL;
        game_start(g, 7);
        g->n_units = 0;

        int inf = data_find_unit_type(g, "INFANTRY");
        /* 自軍(P1)は4体、うち数体を負傷させる（HEALの発動条件を満たすため）。
         * 敵(P0)も近くに置いて交戦状態にする（STRIKEの発動条件） */
        for (int i = 0; i < 4; i++) {
            int u = game_spawn_unit(g, 1, inf, 8 + i, 6, 4);
            CHECK(u >= 0);
        }
        for (int i = 0; i < 2; i++)
            game_spawn_unit(g, 0, inf, 8 + i, 5, 10);

        g->co_id[1] = (int8_t)co;
        g->current = 1;
        g->co_gauge[1] = g->cos[co].power_cost;      /* ゲージ満タン */
        CHECK(game_co_power_ready(g, 1) == true);

        /* AIの手番を最後まで回す */
        static AiState ai;
        ai_begin_turn(g, &ai);
        int guard = 0;
        while (ai_step(g, &ai) && guard++ < 5000) { }

        /* ゲージが消費されている＝必殺技を撃った */
        CHECK(g->co_gauge[1] < g->cos[co].power_cost);
        CHECK(ai.co_used == true);
    }

    /* 指揮官がいなければ当然何も起きない（撃てないのにフラグが立たないこと） */
    memset(g, 0, sizeof *g);
    CHECK(data_load_terrain(g, "data/terrain.def", err, sizeof err) == 0);
    CHECK(data_load_units(g, "data/units.def", err, sizeof err) == 0);
    CHECK(data_load_commanders(g, "data/commanders.def", err, sizeof err) == 0);
    CHECK(data_load_map(g, "data/maps/test_arena.map", err, sizeof err) == 0);
    g->fog = false;
    g->ctrl[1] = CTRL_CPU_NORMAL;
    game_start(g, 7);
    g->n_units = 0;
    int inf2 = data_find_unit_type(g, "INFANTRY");
    for (int i = 0; i < 4; i++) game_spawn_unit(g, 1, inf2, 8 + i, 6, 4);
    g->co_id[1] = -1;
    g->current = 1;
    {
        static AiState ai2;
        ai_begin_turn(g, &ai2);
        int guard = 0;
        while (ai_step(g, &ai2) && guard++ < 5000) { }
        CHECK(ai2.co_used == false);
    }
}

/* キャンペーンの各作戦に敵指揮官が設定され、正しく解決されること。
 * enemy_co が未指定/誤記だと黙って既定(cos[0])にフォールバックして
 * 全作戦の敵が同じ指揮官になってしまうので、ここで固定しておく。 */
static void test_campaign_enemy_co(void)
{
    Game *g = &s_game;
    char err[256];
    Campaign c;
    memset(g, 0, sizeof *g);
    CHECK(data_load_terrain(g, "data/terrain.def", err, sizeof err) == 0);
    CHECK(data_load_units(g, "data/units.def", err, sizeof err) == 0);
    CHECK(data_load_commanders(g, "data/commanders.def", err, sizeof err) == 0);
    CHECK(campaign_load(&c, "data/campaign/main.cpn", err, sizeof err) == 0);
    if (s_fail) return;

    int seen[MAX_COMMANDERS];
    memset(seen, 0, sizeof seen);
    int n_nodes = 0, n_distinct = 0;

    for (int i = 0; i < c.n_nodes; i++) {
        const CpnNode *nd = &c.nodes[i];
        if (!nd->map[0]) continue;      /* WIN/LOSE などの終端ノード */
        n_nodes++;
        /* 指揮官IDが書かれていること（空だと既定に落ちる） */
        CHECK(nd->co[1][0] != 0);
        /* commanders.def に実在すること（誤記だと -1 → 既定に落ちる） */
        int co = data_find_commander(g, nd->co[1]);
        CHECK(co >= 0);
        if (co < 0) continue;
        if (!seen[co]) { seen[co] = 1; n_distinct++; }

        /* 実際に開戦準備すると敵(P1)にその指揮官が入ること */
        CampaignState st;
        memset(&st, 0, sizeof st);
        snprintf(st.node, sizeof st.node, "%s", nd->id);
        memset(g, 0, sizeof *g);
        CHECK(data_load_terrain(g, "data/terrain.def", err, sizeof err) == 0);
        CHECK(data_load_units(g, "data/units.def", err, sizeof err) == 0);
        CHECK(data_load_commanders(g, "data/commanders.def", err, sizeof err) == 0);
        CHECK(campaign_setup_map(g, &c, &st, "", err, sizeof err) == 0);
        CHECK(g->co_id[1] == (int8_t)co);
    }
    CHECK(n_nodes >= 10);
    /* 全作戦が同じ敵指揮官になっていないこと（設定漏れの検知） */
    CHECK(n_distinct >= 4);
}

/* 副目標: .cpn の読み込みと種類別の判定、勝利ボーナスへの加算 */
static void test_sub_objectives(void)
{
    Game *g = &s_game;
    char err[256];
    Campaign c;
    memset(g, 0, sizeof *g);
    CHECK(data_load_terrain(g, "data/terrain.def", err, sizeof err) == 0);
    CHECK(data_load_units(g, "data/units.def", err, sizeof err) == 0);
    CHECK(data_load_commanders(g, "data/commanders.def", err, sizeof err) == 0);
    CHECK(campaign_load(&c, "data/campaign/main.cpn", err, sizeof err) == 0);
    if (s_fail) return;

    /* 全作戦に副目標があり、説明文と種類が入っていること */
    int n_map_nodes = 0;
    for (int i = 0; i < c.n_nodes; i++) {
        const CpnNode *nd = &c.nodes[i];
        if (!nd->map[0]) continue;
        n_map_nodes++;
        CHECK(nd->n_subs > 0);
        CHECK(nd->n_subs <= MAX_SUBS);
        for (int k = 0; k < nd->n_subs; k++) {
            CHECK(nd->subs[k].type != SUB_NONE);
            CHECK(nd->subs[k].desc[0] != 0);
            /* SURVIVE はユニットIDが実在すること（誤記は永久に達成不能になる） */
            if (nd->subs[k].type == SUB_SURVIVE) {
                CHECK(nd->subs[k].unit[0] != 0);
                CHECK(data_find_unit_type(g, nd->subs[k].unit) >= 0);
            }
        }
    }
    CHECK(n_map_nodes >= 10);

    /* --- 種類別の判定を人工的な盤面で確かめる --- */
    CpnNode nd;
    memset(&nd, 0, sizeof nd);
    memset(g, 0, sizeof *g);
    CHECK(data_load_terrain(g, "data/terrain.def", err, sizeof err) == 0);
    CHECK(data_load_units(g, "data/units.def", err, sizeof err) == 0);
    CHECK(data_load_map(g, "data/maps/test_arena.map", err, sizeof err) == 0);
    if (s_fail) return;

    nd.n_subs = 3;
    nd.subs[0].type = SUB_MAX_LOSS;  nd.subs[0].param = 2;
    nd.subs[1].type = SUB_MAX_TURNS; nd.subs[1].param = 10;
    nd.subs[2].type = SUB_MIN_KILLS; nd.subs[2].param = 5;

    g->lost_units[0] = 2; g->turn = 10; g->lost_units[1] = 5;
    CHECK(campaign_sub_done(g, &nd, 0) == true);      /* ちょうど上限=達成 */
    CHECK(campaign_sub_done(g, &nd, 1) == true);
    CHECK(campaign_sub_done(g, &nd, 2) == true);
    CHECK(campaign_sub_count_done(g, &nd) == 3);

    g->lost_units[0] = 3; g->turn = 11; g->lost_units[1] = 4;
    CHECK(campaign_sub_done(g, &nd, 0) == false);     /* 1つ超えたら未達 */
    CHECK(campaign_sub_done(g, &nd, 1) == false);
    CHECK(campaign_sub_done(g, &nd, 2) == false);
    CHECK(campaign_sub_count_done(g, &nd) == 0);

    /* SURVIVE: 指定種別の生存数 */
    memset(&nd, 0, sizeof nd);
    nd.n_subs = 1;
    nd.subs[0].type = SUB_SURVIVE;
    nd.subs[0].param = 2;
    snprintf(nd.subs[0].unit, sizeof nd.subs[0].unit, "TANK");
    int tank = data_find_unit_type(g, "TANK");
    int inf  = data_find_unit_type(g, "INFANTRY");
    g->n_units = 0;
    game_spawn_unit(g, 0, tank, 3, 3, 10);
    game_spawn_unit(g, 0, inf,  3, 4, 10);   /* 別種別は数えない */
    game_spawn_unit(g, 1, tank, 3, 5, 10);   /* 敵は数えない */
    CHECK(campaign_sub_done(g, &nd, 0) == false);
    game_spawn_unit(g, 0, tank, 3, 6, 10);
    CHECK(campaign_sub_done(g, &nd, 0) == true);

    /* KEEP_BLD / CAPTURE: 自軍の建物数 */
    memset(&nd, 0, sizeof nd);
    nd.n_subs = 1;
    nd.subs[0].type = SUB_KEEP_BLD;
    nd.subs[0].param = (int16_t)game_count_buildings(g, 0);
    CHECK(campaign_sub_done(g, &nd, 0) == true);
    nd.subs[0].param = (int16_t)(game_count_buildings(g, 0) + 1);
    CHECK(campaign_sub_done(g, &nd, 0) == false);

    /* 範囲外・NULL は false（落ちないこと） */
    CHECK(campaign_sub_done(g, &nd, -1) == false);
    CHECK(campaign_sub_done(g, &nd, 99) == false);
    CHECK(campaign_sub_done(g, NULL, 0) == false);
    CHECK(campaign_sub_count_done(g, NULL) == 0);

    /* --- 勝利時に達成数ぶんのボーナスが資金持越しに乗ること --- */
    {
        Campaign c2;
        memset(&c2, 0, sizeof c2);
        c2.n_nodes = 1;
        snprintf(c2.nodes[0].id, sizeof c2.nodes[0].id, "T1");
        snprintf(c2.nodes[0].map, sizeof c2.nodes[0].map, "maps/test_arena.map");
        snprintf(c2.nodes[0].next_win, sizeof c2.nodes[0].next_win, "WIN");
        c2.nodes[0].n_subs = 2;
        c2.nodes[0].subs[0].type = SUB_MAX_LOSS;  c2.nodes[0].subs[0].param = 99;
        c2.nodes[0].subs[1].type = SUB_MIN_KILLS; c2.nodes[0].subs[1].param = 9999;

        CampaignState st;
        memset(&st, 0, sizeof st);
        snprintf(st.node, sizeof st.node, "T1");
        g->funds[0] = 0;
        g->turn = 1;
        g->lost_units[0] = 0; g->lost_units[1] = 0;
        campaign_on_victory(g, &c2, &st);
        /* 1つだけ達成 → ボーナス1個ぶん */
        CpnScore sc;
        campaign_evaluate(g, &c2.nodes[0], &sc);
        int expect = g->funds[0] / 2 + c2.nodes[0].bonus +
                     campaign_rank_bonus(sc.rank) + campaign_sub_bonus();
        CHECK(campaign_sub_count_done(g, &c2.nodes[0]) == 1);
        CHECK(st.funds_carry == expect);
    }
}

static void test_sub_feasible(void)
{
    Game *g = &s_game;
    char err[256];
    Campaign c;
    memset(g, 0, sizeof *g);
    CHECK(campaign_load(&c, "data/campaign/main.cpn", err, sizeof err) == 0);
    if (s_fail) return;

    /* --- 実現可能性: マップに存在しない数を要求していないか ---
     * CAPTURE/KEEP_BLD が全拠点数を超えると永久に達成不能になる。
     * MAX_TURNS が制限ターンを超えていると自動達成になってしまう。
     * マップを作り直したときに黙って壊れるのでここで固定する。 */
    for (int i = 0; i < c.n_nodes; i++) {
        const CpnNode *nd = &c.nodes[i];
        if (!nd->map[0] || nd->n_subs <= 0) continue;
        CampaignState st;
        memset(&st, 0, sizeof st);
        snprintf(st.node, sizeof st.node, "%s", nd->id);
        memset(g, 0, sizeof *g);
        CHECK(data_load_terrain(g, "data/terrain.def", err, sizeof err) == 0);
        CHECK(data_load_units(g, "data/units.def", err, sizeof err) == 0);
        CHECK(data_load_commanders(g, "data/commanders.def", err, sizeof err) == 0);
        CHECK(campaign_setup_map(g, &c, &st, "", err, sizeof err) == 0);
        if (s_fail) return;

        int total_bld = 0;
        for (int y = 0; y < g->h; y++)
            for (int x = 0; x < g->w; x++)
                if (g->terrains[g->tiles[y][x].terrain].capturable) total_bld++;
        int start_bld = game_count_buildings(g, 0);

        for (int k = 0; k < nd->n_subs; k++) {
            const SubObjective *o = &nd->subs[k];
            if (o->type == SUB_CAPTURE || o->type == SUB_KEEP_BLD) {
                CHECK(o->param <= total_bld);          /* 達成不能でないこと */
                if (o->type == SUB_CAPTURE)
                    CHECK(o->param > start_bld);       /* 開始時点で達成済みでないこと */
            }
            if (o->type == SUB_MAX_TURNS && g->turn_limit > 0)
                CHECK(o->param < g->turn_limit);       /* 自動達成でないこと */
            if (o->type == SUB_SURVIVE)
                CHECK(o->param >= 1);
        }
    }
}

/* マップイベント: 条件・動作・1回だけ発火・セーブ復元 */
static void test_map_events(void)
{
    Game *g = &s_game;
    char err[256];
    memset(g, 0, sizeof *g);
    CHECK(data_load_terrain(g, "data/terrain.def", err, sizeof err) == 0);
    CHECK(data_load_units(g, "data/units.def", err, sizeof err) == 0);
    CHECK(data_load_map(g, "data/maps/test_arena.map", err, sizeof err) == 0);
    if (s_fail) return;

    int inf = data_find_unit_type(g, "INFANTRY");
    g->n_units = 0;
    g->turn = 1;
    g->funds[0] = 0;
    g->n_events = 0;
    g->events_fired = 0;

    /* (1) TURN 条件 + SPAWN 動作 */
    MapEvent *e = &g->events[g->n_events++];
    memset(e, 0, sizeof *e);
    e->cond = EV_C_TURN; e->c1 = 3;
    e->act = EV_A_SPAWN; e->a1 = 1; e->a2 = (int16_t)inf; e->a3 = 5; e->a4 = 5; e->a5 = 2;
    snprintf(e->msg, sizeof e->msg, "増援");

    /* (2) LOSS 条件 + FUNDS 動作 */
    e = &g->events[g->n_events++];
    memset(e, 0, sizeof *e);
    e->cond = EV_C_LOSS; e->c1 = 1; e->c2 = 3;
    e->act = EV_A_FUNDS; e->a1 = 0; e->a2 = 500;
    snprintf(e->msg, sizeof e->msg, "追加予算");

    /* (3) AREA 条件 + MSG 動作 */
    e = &g->events[g->n_events++];
    memset(e, 0, sizeof *e);
    e->cond = EV_C_AREA; e->c1 = 0; e->c2 = 9; e->c3 = 9; e->c4 = 2;
    e->act = EV_A_MSG;
    snprintf(e->msg, sizeof e->msg, "接近");

    const char *msgs[MAX_EVENTS];
    /* まだどの条件も満たさない */
    CHECK(game_check_events(g, msgs, MAX_EVENTS) == 0);
    CHECK(g->n_units == 0);

    /* ターン3で(1)が発火し、敵が2体湧く */
    g->turn = 3;
    CHECK(game_check_events(g, msgs, MAX_EVENTS) == 1);
    int enemies = 0;
    for (int i = 0; i < g->n_units; i++)
        if ((g->units[i].flags & UF_ALIVE) && g->units[i].owner == 1) enemies++;
    CHECK(enemies == 2);

    /* 同じ条件でも2回目は発火しない（1回だけ） */
    CHECK(game_check_events(g, msgs, MAX_EVENTS) == 0);
    int enemies2 = 0;
    for (int i = 0; i < g->n_units; i++)
        if ((g->units[i].flags & UF_ALIVE) && g->units[i].owner == 1) enemies2++;
    CHECK(enemies2 == enemies);

    /* 敵の損失が3体になると(2)が発火して資金が入る */
    g->lost_units[1] = 3;
    CHECK(game_check_events(g, msgs, MAX_EVENTS) == 1);
    CHECK(g->funds[0] == 500);

    /* 自軍が(9,9)に近づくと(3)が発火 */
    CHECK(game_spawn_unit(g, 0, inf, 9, 9, 10) >= 0);
    CHECK(game_check_events(g, msgs, MAX_EVENTS) == 1);
    CHECK(g->events_fired == 0x7u);      /* 3件とも発火済み */
    CHECK(game_check_events(g, msgs, MAX_EVENTS) == 0);

    /* (4) セーブ→ロードで発火済み状態が保たれる（再発火して増援が二重に湧かない） */
    {
        CampaignState cs;
        memset(&cs, 0, sizeof cs);
        save_ensure_dir("saves");
        CHECK(save_game(g, &cs, "saves/_evtest.sav", err, sizeof err) == 0);

        Game *g2 = &s_game2;
        CampaignState cs2;
        memset(g2, 0, sizeof *g2);
        CHECK(data_load_terrain(g2, "data/terrain.def", err, sizeof err) == 0);
        CHECK(data_load_units(g2, "data/units.def", err, sizeof err) == 0);
        CHECK(load_game(g2, &cs2, "saves/_evtest.sav", err, sizeof err) == 0);
        CHECK(g2->n_events == g->n_events);
        CHECK(g2->events_fired == g->events_fired);
        CHECK(g2->events[0].cond == EV_C_TURN);
        CHECK(g2->events[0].act == EV_A_SPAWN);
        CHECK(strcmp(g2->events[0].msg, "増援") == 0);
        int before = g2->n_units;
        CHECK(game_check_events(g2, msgs, MAX_EVENTS) == 0);   /* 再発火しない */
        CHECK(g2->n_units == before);
        remove("saves/_evtest.sav");
    }

    /* --- 後から追加した条件と動作 --- */
    {
        g->n_events = 0; g->events_fired = 0;
        g->n_units = 0;
        int ua = game_spawn_unit(g, 0, inf, 6, 6, 10);
        int ub = game_spawn_unit(g, 1, inf, 8, 6, 10);
        CHECK(ua >= 0 && ub >= 0);

        /* WEATHER 動作: 天候を雨に固定する */
        g->weather_on = 1;
        g->weather = WX_CLEAR;
        MapEvent *w = &g->events[g->n_events++];
        memset(w, 0, sizeof *w);
        w->cond = EV_C_TURN; w->c1 = 1;
        w->act = EV_A_WEATHER; w->a1 = WX_RAIN; w->a2 = 5;
        snprintf(w->msg, sizeof w->msg, "嵐");
        CHECK(game_check_events(g, msgs, MAX_EVENTS) == 1);
        CHECK(game_weather(g) == WX_RAIN);
        CHECK(g->weather_left == 5);
        CHECK(g->weather_next != WX_RAIN);      /* 明けた先は別の天候 */

        /* WEATHER 条件: 雨になったことを引き金にできる */
        g->n_events = 0; g->events_fired = 0;
        g->funds[1] = 0;
        MapEvent *c = &g->events[g->n_events++];
        memset(c, 0, sizeof *c);
        c->cond = EV_C_WEATHER; c->c1 = WX_RAIN;
        c->act = EV_A_FUNDS; c->a1 = 1; c->a2 = 500;
        CHECK(game_check_events(g, msgs, MAX_EVENTS) == 1);
        CHECK(g->funds[1] == 500);

        /* TERRAIN 動作: 拠点を壊すと所有も消える（収入が残らない） */
        g->n_events = 0; g->events_fired = 0;
        {
            int city = -1, plain = -1;
            for (int i = 0; i < g->n_terrains; i++) {
                if (!strcmp(g->terrains[i].id, "CITY"))  city = i;
                if (!strcmp(g->terrains[i].id, "PLAIN")) plain = i;
            }
            CHECK(city >= 0 && plain >= 0);
            g->tiles[7][7].terrain = (uint8_t)city;
            g->tiles[7][7].owner = 0;
            MapEvent *t = &g->events[g->n_events++];
            memset(t, 0, sizeof *t);
            t->cond = EV_C_TURN; t->c1 = 1;
            t->act = EV_A_TERRAIN;
            t->a1 = 7; t->a2 = 7; t->a3 = g->terrains[plain].chr;
            CHECK(game_check_events(g, msgs, MAX_EVENTS) == 1);
            CHECK(g->tiles[7][7].terrain == (uint8_t)plain);
            CHECK(g->tiles[7][7].owner == -1);
        }

        /* CAPTURED 条件: その拠点を持っている間だけ成立 */
        g->n_events = 0; g->events_fired = 0;
        {
            int city = -1;
            for (int i = 0; i < g->n_terrains; i++)
                if (!strcmp(g->terrains[i].id, "CITY")) city = i;
            g->tiles[9][9].terrain = (uint8_t)city;
            g->tiles[9][9].owner = -1;
            MapEvent *cp = &g->events[g->n_events++];
            memset(cp, 0, sizeof *cp);
            cp->cond = EV_C_CAPTURED; cp->c1 = 0; cp->c2 = 9; cp->c3 = 9;
            cp->act = EV_A_FUNDS; cp->a1 = 0; cp->a2 = 700;
            g->funds[0] = 0;
            CHECK(game_check_events(g, msgs, MAX_EVENTS) == 0);   /* まだ中立 */
            g->tiles[9][9].owner = 0;
            CHECK(game_check_events(g, msgs, MAX_EVENTS) == 1);
            CHECK(g->funds[0] == 700);
        }

        /* HP 動作: 全軍を削るが HP1 を下回らない */
        g->n_events = 0; g->events_fired = 0;
        g->units[ub].hp = 2;
        {
            MapEvent *h = &g->events[g->n_events++];
            memset(h, 0, sizeof *h);
            h->cond = EV_C_TURN; h->c1 = 1;
            h->act = EV_A_HP; h->a1 = 1; h->a2 = -5;
            CHECK(game_check_events(g, msgs, MAX_EVENTS) == 1);
            CHECK(g->units[ub].hp == 1);
            CHECK(g->units[ua].hp == 10);        /* 自軍は無事 */
        }

        /* COPOWER 動作: 指揮官の技が強制発動される */
        g->n_events = 0; g->events_fired = 0;
        CHECK(data_load_commanders(g, "data/commanders.def", err, sizeof err) == 0);
        {
            int graf = data_find_commander(g, "GRAF");   /* HEAL +3 */
            CHECK(graf >= 0);
            g->co_id[1] = (int8_t)graf;
            g->co_gauge[1] = 0;
            g->units[ub].hp = 4;
            MapEvent *cp = &g->events[g->n_events++];
            memset(cp, 0, sizeof *cp);
            cp->cond = EV_C_TURN; cp->c1 = 1;
            cp->act = EV_A_COPOWER; cp->a1 = 1;
            CHECK(game_check_events(g, msgs, MAX_EVENTS) == 1);
            CHECK(g->units[ub].hp == 7);         /* 4 + 3 */
        }
    }
}

/* キャンペーンのイベント定義が実際に動く形になっているか（誤記の検知） */
static void test_campaign_events(void)
{
    Game *g = &s_game;
    char err[256];
    Campaign c;
    memset(g, 0, sizeof *g);
    CHECK(data_load_terrain(g, "data/terrain.def", err, sizeof err) == 0);
    CHECK(data_load_units(g, "data/units.def", err, sizeof err) == 0);
    CHECK(data_load_commanders(g, "data/commanders.def", err, sizeof err) == 0);
    CHECK(campaign_load(&c, "data/campaign/main.cpn", err, sizeof err) == 0);
    if (s_fail) return;

    int total = 0;
    for (int i = 0; i < c.n_nodes; i++) {
        const CpnNode *nd = &c.nodes[i];
        if (!nd->map[0] || nd->n_evs <= 0) continue;

        CampaignState st;
        memset(&st, 0, sizeof st);
        snprintf(st.node, sizeof st.node, "%s", nd->id);
        memset(g, 0, sizeof *g);
        CHECK(data_load_terrain(g, "data/terrain.def", err, sizeof err) == 0);
        CHECK(data_load_units(g, "data/units.def", err, sizeof err) == 0);
        CHECK(data_load_commanders(g, "data/commanders.def", err, sizeof err) == 0);
        CHECK(campaign_setup_map(g, &c, &st, "", err, sizeof err) == 0);
        if (s_fail) return;

        CHECK(g->n_events == nd->n_evs);
        for (int k = 0; k < nd->n_evs; k++) {
            const MapEvent *ev = &g->events[k];
            CHECK(ev->cond != EV_C_NONE);
            CHECK(ev->msg[0] != 0);
            /* SPAWN は種別が解決済みで、湧き出し座標がマップ内にあること
             * （範囲外だと何も起きず、原因が分からないまま消える） */
            if (ev->act == EV_A_SPAWN) {
                CHECK(data_find_unit_type(g, nd->ev_unit[k]) >= 0);
                CHECK(ev->a2 >= 0 && ev->a2 < g->n_types);
                CHECK(game_in_bounds(g, ev->a3, ev->a4));
                CHECK(ev->a5 >= 1);
                CHECK(ev->a1 >= 0 && ev->a1 < MAX_PLAYERS);
            }
            /* AREA の中心座標もマップ内であること */
            if (ev->cond == EV_C_AREA)
                CHECK(game_in_bounds(g, ev->c2, ev->c3));
            /* CAPTURED は「占領できるマス」を指していないと永遠に発火しない。
             * 座標を書き間違えても「たまたま達成しなかった」と見分けがつかないので固める。 */
            if (ev->cond == EV_C_CAPTURED) {
                CHECK(game_in_bounds(g, ev->c2, ev->c3));
                if (game_in_bounds(g, ev->c2, ev->c3))
                    CHECK(g->terrains[g->tiles[ev->c3][ev->c2].terrain].capturable);
                CHECK(ev->c1 >= 0 && ev->c1 < MAX_PLAYERS);
            }
            /* 天候を条件にしているのに天候が切られているマップだと発火しない */
            if (ev->cond == EV_C_WEATHER) {
                CHECK(ev->c1 >= 0 && ev->c1 < WX_COUNT);
                CHECK(g->weather_on);
            }
            if (ev->act == EV_A_WEATHER) {
                CHECK(ev->a1 >= 0 && ev->a1 < WX_COUNT);
                CHECK(g->weather_on);
            }
            /* TERRAIN の行先が未定義の文字だと黙って無視される */
            if (ev->act == EV_A_TERRAIN) {
                CHECK(game_in_bounds(g, ev->a1, ev->a2));
                int found = 0;
                for (int t = 0; t < g->n_terrains; t++)
                    if (g->terrains[t].chr == (char)ev->a3) found = 1;
                CHECK(found);
            }
            /* COPOWER はその陣営に指揮官がいないと何も起きない */
            if (ev->act == EV_A_COPOWER) {
                CHECK(ev->a1 >= 0 && ev->a1 < MAX_PLAYERS);
                if (ev->a1 >= 0 && ev->a1 < MAX_PLAYERS)
                    CHECK(game_co(g, ev->a1) != NULL);
            }
            total++;
        }
    }
    CHECK(total >= 10);
}

/* v6 (マップイベント導入前) のセーブが引き続き読めること。
 * 進行中のキャンペーンを壊さないための下位互換。 */
static void test_save_v6_compat(void)
{
    Game *g = &s_game;
    char err[256];
    memset(g, 0, sizeof *g);
    CHECK(data_load_terrain(g, "data/terrain.def", err, sizeof err) == 0);
    CHECK(data_load_units(g, "data/units.def", err, sizeof err) == 0);
    CHECK(data_load_map(g, "data/maps/test_arena.map", err, sizeof err) == 0);
    if (s_fail) return;
    game_start(g, 3);

    CampaignState cs;
    memset(&cs, 0, sizeof cs);
    save_ensure_dir("saves");
    CHECK(save_game(g, &cs, "saves/_v6test.sav", err, sizeof err) == 0);

    /* 現行版の往復。搭載枠が2→4になった v8 でも壊れないことを見る。
     * かつては「ヘッダの版だけ 6 に書き換えて読めるか」を見ていたが、
     * v8 で本体の並び（搭載枠の数）が版によって変わるようになったため、
     * ヘッダだけ古く偽装したファイルは実在しない組み合わせになってしまう。
     * 実ファイルの後方互換は deserialize の `ver >= 8 ? 4 : 2` が担う。 */
    Game *g2 = &s_game2;
    CampaignState cs2;
    memset(g2, 0, sizeof *g2);
    CHECK(data_load_terrain(g2, "data/terrain.def", err, sizeof err) == 0);
    CHECK(data_load_units(g2, "data/units.def", err, sizeof err) == 0);
    CHECK(load_game(g2, &cs2, "saves/_v6test.sav", err, sizeof err) == 0);
    CHECK(g2->turn == g->turn);

    /* 版が範囲外なら弾く（古すぎ・新しすぎ） */
    for (unsigned char bad = 0; bad <= 200; bad += 200) {
        FILE *f = fopen("saves/_v6test.sav", "r+b");
        CHECK(f != NULL);
        if (f) {
            unsigned char v[4] = { bad, 0, 0, 0 };
            fseek(f, 4, SEEK_SET);
            fwrite(v, 1, 4, f);
            fclose(f);
        }
        CHECK(load_game(g2, &cs2, "saves/_v6test.sav", err, sizeof err) != 0);
    }
    remove("saves/_v6test.sav");

    /* 対応範囲外の古い版はこれまで通り拒否される */
    CHECK(save_game(g, &cs, "saves/_v1test.sav", err, sizeof err) == 0);
    {
        FILE *f = fopen("saves/_v1test.sav", "r+b");
        if (f) {
            unsigned char v1[4] = { 1, 0, 0, 0 };
            fseek(f, 4, SEEK_SET);
            fwrite(v1, 1, 4, f);
            fclose(f);
        }
    }
    CHECK(load_game(g2, &cs2, "saves/_v1test.sav", err, sizeof err) != 0);
    remove("saves/_v1test.sav");
}

/* 補給の立体化対応: 同じマスに別レイヤーのユニットが重なっていても、
 * 地上の味方をきちんと補給・回復できること。
 * （重なりセルで「最初に見つかった1体」だけを見ていると、
 *   上空を味方機が飛んでいる地上部隊が永久に補給されなくなる） */
static void test_supply_layers(void)
{
    Game *g = &s_game;
    char err[256];
    memset(g, 0, sizeof *g);
    CHECK(data_load_terrain(g, "data/terrain.def", err, sizeof err) == 0);
    CHECK(data_load_units(g, "data/units.def", err, sizeof err) == 0);
    CHECK(data_load_map(g, "data/maps/test_arena.map", err, sizeof err) == 0);
    if (s_fail) return;
    g->fog = false;
    game_start(g, 5);

    int sup   = data_find_unit_type(g, "SUPPLY");
    int tank  = data_find_unit_type(g, "TANK");
    int fight = data_find_unit_type(g, "FIGHTER");
    CHECK(sup >= 0 && tank >= 0 && fight >= 0);
    if (s_fail) return;

    g->n_units = 0;
    int us = game_spawn_unit(g, 0, sup, 9, 4, 10);

    /* 隣接マスに「地上の戦車」と「その上空の戦闘機」を重ねて置く。
     * 戦闘機を先に生成して配列の若い番号にし、
     * 「最初の1体」しか見ない実装だと戦車が隠れる状況を作る。 */
    int uf = game_spawn_unit(g, 0, fight, 10, 4, 10);
    int ut = game_spawn_unit(g, 0, tank,  10, 4, 10);
    CHECK(us >= 0 && uf >= 0 && ut >= 0);
    if (s_fail) return;
    CHECK(game_unit_at_layer(g, 10, 4, LAYER_AIR) == uf);
    CHECK(game_unit_at_layer(g, 10, 4, LAYER_SURFACE) == ut);

    /* 戦車を消耗させる（燃料・弾薬・HP） */
    g->units[ut].fuel = 5;
    g->units[ut].ammo = 0;
    g->units[ut].hp   = 5;
    g->units[uf].fuel = g->types[fight].fuel;   /* 戦闘機は満タン=補給対象外 */
    g->units[uf].ammo = g->types[fight].ammo;

    /* 補給車（陸）は上空の戦闘機ではなく、地上の戦車を補給できるはず */
    CHECK(game_can_supply(g, us) == true);
    int n = game_supply_adjacent(g, us);
    CHECK(n == 1);
    CHECK(g->units[ut].fuel == g->types[tank].fuel);
    CHECK(g->units[ut].ammo == g->types[tank].ammo);

    /* 回復も同様に、重なっていても地上の戦車を対象にできること */
    g->units[us].flags &= (uint8_t)~UF_DONE;
    g->units[us].ammo = g->types[sup].ammo;
    CHECK(game_can_heal(g, us) == true);
    int hp0 = g->units[ut].hp;
    CHECK(game_supply_heal(g, us) == 1);
    CHECK(g->units[ut].hp == hp0 + 1);

    /* 逆向き: 補給機（空）は重なっていても上空の戦闘機を補給できること */
    int supair = data_find_unit_type(g, "SUPPLY_AIR");
    CHECK(supair >= 0);
    if (supair >= 0) {
        g->n_units = 0;
        int usa = game_spawn_unit(g, 0, supair, 9, 6, 10);
        int gt  = game_spawn_unit(g, 0, tank,  10, 6, 10);   /* 地上（先に生成） */
        int fa  = game_spawn_unit(g, 0, fight, 10, 6, 10);   /* 上空 */
        CHECK(usa >= 0 && gt >= 0 && fa >= 0);
        if (!s_fail) {
            g->units[fa].fuel = 5;
            g->units[fa].ammo = 0;
            CHECK(game_can_supply(g, usa) == true);
            CHECK(game_supply_adjacent(g, usa) == 1);
            CHECK(g->units[fa].fuel == g->types[fight].fuel);
            CHECK(g->units[fa].ammo == g->types[fight].ammo);
        }
    }

    /* 手番開始の自動補給フェイズでも重なりセルが取りこぼされないこと */
    g->n_units = 0;
    int us2 = game_spawn_unit(g, 0, sup, 9, 8, 10);
    int af2 = game_spawn_unit(g, 0, fight, 10, 8, 10);   /* 上空（若い番号） */
    int tk2 = game_spawn_unit(g, 0, tank, 10, 8, 10);    /* 地上 */
    /* 敵を1体置かないと全滅勝利が確定して手番が進まない */
    game_spawn_unit(g, 1, tank, 20, 20, 10);
    CHECK(us2 >= 0 && af2 >= 0 && tk2 >= 0);
    if (!s_fail) {
        g->units[tk2].fuel = 3;
        g->units[tk2].ammo = 0;
        g->units[af2].fuel = g->types[fight].fuel;
        g->units[af2].ammo = g->types[fight].ammo;
        g->current = 1;
        game_end_turn(g);      /* → P0 の手番開始（補給フェイズが走る） */
        CHECK(g->current == 0);
        CHECK(g->units[tk2].fuel == g->types[tank].fuel);
        CHECK(g->units[tk2].ammo == g->types[tank].ammo);
    }
}

/* 維持コスト・燃料切れ・移動音がデータで決まること。
 * 既定値は class から埋まり、現行の units.def は挙動が変わらないこと（回帰防止）。 */
static void test_upkeep_data(void)
{
    Game *g = &s_game;
    char err[256];
    memset(g, 0, sizeof *g);
    CHECK(data_load_terrain(g, "data/terrain.def", err, sizeof err) == 0);
    CHECK(data_load_units(g, "data/units.def", err, sizeof err) == 0);
    CHECK(data_load_map(g, "data/maps/test_arena.map", err, sizeof err) == 0);
    if (s_fail) return;

    /* --- 既定値: class から従来どおりに埋まる --- */
    int fig = data_find_unit_type(g, "FIGHTER");
    int dd  = data_find_unit_type(g, "DESTROYER");
    int sub = data_find_unit_type(g, "SUBMARINE");
    int inf = data_find_unit_type(g, "INFANTRY");
    int tk  = data_find_unit_type(g, "TANK");
    CHECK(fig >= 0 && dd >= 0 && sub >= 0 && inf >= 0 && tk >= 0);
    if (s_fail) return;

    CHECK(g->types[fig].upkeep == 4);                  /* 航空機だけ空中維持コスト */
    CHECK(g->types[fig].no_fuel == NOFUEL_DIE);        /* 墜落 */
    CHECK(g->types[fig].move_se == SE_MOVE_AIR);
    CHECK(g->types[dd].upkeep == 0);
    CHECK(g->types[dd].no_fuel == NOFUEL_DAMAGE);      /* 漂流 */
    CHECK(g->types[sub].no_fuel == NOFUEL_DAMAGE);
    CHECK(g->types[inf].upkeep == 0);
    CHECK(g->types[inf].no_fuel == NOFUEL_NONE);       /* 陸は燃料切れでも死なない */
    CHECK(g->types[inf].move_se == SE_MOVE_FOOT);
    CHECK(g->types[tk].move_se == SE_MOVE_VEHICLE);

    /* --- 挙動: 空港の外にいる航空機は毎ターン燃料が減り、0で失われる --- */
    g->fog = false;
    game_start(g, 4);
    g->n_units = 0;
    int ua = game_spawn_unit(g, 0, fig, 9, 4, 10);
    game_spawn_unit(g, 1, inf, 20, 20, 10);   /* 敵を置かないと勝敗が確定する */
    CHECK(ua >= 0);
    if (s_fail) return;
    g->units[ua].fuel = 9;
    g->current = 0;
    game_end_turn(g);                 /* P0手番終了 → 維持コスト -4 */
    CHECK(g->units[ua].fuel == 5);
    g->current = 0;
    game_end_turn(g);
    CHECK(g->units[ua].fuel == 1);
    g->current = 0;
    game_end_turn(g);                 /* 1 -> 0 になり墜落 */
    CHECK(!(g->units[ua].flags & UF_ALIVE));

    /* --- 上書き: upkeep=0 / no_fuel=NONE なら燃料0でも失われない --- */
    g->types[fig].upkeep = 0;
    g->types[fig].no_fuel = NOFUEL_NONE;
    g->n_units = 0;
    int ub = game_spawn_unit(g, 0, fig, 9, 4, 10);
    game_spawn_unit(g, 1, inf, 20, 20, 10);
    CHECK(ub >= 0);
    if (s_fail) return;
    g->units[ub].fuel = 0;
    g->current = 0;
    game_end_turn(g);
    CHECK((g->units[ub].flags & UF_ALIVE) != 0);       /* 落ちない */
    CHECK(g->units[ub].hp == 10);                      /* 削られもしない */
    CHECK(g->units[ub].fuel == 0);

    /* --- stealth / detect が is_sub / anti_sub の別名として読めること --- */
    {
        save_ensure_dir("saves");
        FILE *f = fopen("saves/_alias.def", "wb");
        CHECK(f != NULL);
        if (f) {
            fprintf(f,
                "[unit GHOST]\n"
                "name = 幽鬼\n"
                "class = LAND_FOOT\n"
                "armor = SOFT\n"
                "stealth = 1\n"
                "upkeep = 0\n"
                "no_fuel = NONE\n"
                "move_se = CAPTURE\n"
                "[unit SEER]\n"
                "name = 賢者\n"
                "class = LAND_FOOT\n"
                "armor = SOFT\n"
                "detect = 1\n");
            fclose(f);
        }
        Game *g2 = &s_game2;
        memset(g2, 0, sizeof *g2);
        CHECK(data_load_units(g2, "saves/_alias.def", err, sizeof err) == 0);
        CHECK(g2->n_types == 2);
        if (g2->n_types == 2) {
            CHECK(g2->types[0].is_sub == 1);            /* stealth → is_sub */
            CHECK(g2->types[0].anti_sub == 0);
            CHECK(g2->types[0].upkeep == 0);
            CHECK(g2->types[0].no_fuel == NOFUEL_NONE);
            CHECK(g2->types[0].move_se == SE_CAPTURE);  /* 任意の効果音を指定できる */
            CHECK(g2->types[1].anti_sub == 1);          /* detect → anti_sub */
            CHECK(g2->types[1].is_sub == 0);
        }
        remove("saves/_alias.def");
    }
}

int main(void)
{
    test_hex();
    test_rng_deterministic();
    test_data_and_battle();
    test_move_range_zoc();
    test_capture();
    test_layers();
    test_transport();
    test_paradrop();
    test_produce_layers();
    test_evolve();
    test_evolved_carry();
    test_evolved_transport();
    test_supply();
    test_supply_layers();
    test_upkeep_data();
    test_objective();
    test_save_load();
    test_campaign();
    test_warehouse();
    test_commanders();
    test_weather();
    test_daynight();
    test_endless_night();
    test_multiplayer();
    test_teams();
    test_maplist();
    test_ai_production_mix();
    test_campaign_story();
    test_campaign_multi();
    test_terrain_work();
    test_enemy_reinforce();
    test_join();
    test_ai_co_power();
    test_campaign_enemy_co();
    test_sub_objectives();
    test_sub_feasible();
    test_map_events();
    test_campaign_events();
    test_save_v6_compat();
    test_rank();
    test_deploy_select();

    if (s_fail == 0) {
        printf("ALL TESTS PASSED\n");
        return 0;
    }
    printf("%d FAILURE(S)\n", s_fail);
    return 1;
}
