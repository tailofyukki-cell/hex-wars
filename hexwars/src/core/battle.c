/* battle.c - 戦闘計算（仕様書 5.5、整数演算のみ） */
#include "game.h"
#include "hex.h"

/*
 * base     = ATK(対象カテゴリ別) × 攻撃側HP / 10
 * defmod   = 100 + 地形防御% + 防御側経験ランク×3
 * atkmod   = 100 + 攻撃側経験ランク×3
 * dmg_x10  = base × atkmod / defmod × 天候% × 射程減衰%
 * 射程減衰 = 100 - 5 × (距離 - range_min)   （遠いほど当たらない）
 * 乱数     = ±15%
 * 最低保証 = ATK>0 なら 1
 */

/* 射程が最短から1セル伸びるごとに落ちる攻撃力(%)と、その下限。
 * 間接砲の「遠射は当たりにくい」を表す。射程を伸ばす進化の歯止めでもあり、
 * 例: 超弩級戦艦(2-7)の最遠射は 75% で、元の戦艦(2-5)の最遠射 85% と
 * 実威力がほぼ並ぶ＝威力据え置きで射程だけ伸びる、という設計になっている。 */
#define RANGE_FALLOFF_PCT 5
#define RANGE_FALLOFF_MIN 50

static int base_damage_x10(const Game *g, const Unit *atk, const Unit *def)
{
    const UnitType *at = unit_type(g, atk);
    const UnitType *dt = unit_type(g, def);
    int a = at->atk[dt->armor];
    if (a <= 0) return 0;
    /* 夜は領域をまたぐ攻撃が成立しない。雨と同じくここでも0を返す。
     * 見ないと、実際には撃てない組み合わせに予想ダメージが出てしまう。 */
    if (game_night_blocks(g, atk, def)) return 0;

    int base = a * atk->hp / 10;
    const TerrainType *terr = game_terrain_at(g, def->pos.x, def->pos.y);
    /* 立体化(確定): 地形防御は全レイヤーに適用（空・海中も自セルの防御を受ける） */
    (void)dt;
    int terrain_def = terr->def_bonus;
    /* 夜は身を隠せる地形（hide=1 の森・都市）の防御が上がる。
     * 夜を「例えば森に引いて立て直す時間」にするための後押し。 */
    if (game_is_night(g) && terr->hide) terrain_def += terrain_def / 2;

    /* 指揮官（CO）の常時効果。攻撃側の atk_pct と防御側の def_pct を反映 */
    int co_atk = game_co_atk_pct(g, atk->owner, atk);
    int co_def = game_co_def_pct(g, def->owner, def);

    int defmod = 100 + terrain_def + unit_rank(def) * 3 + co_def;
    if (defmod < 20) defmod = 20;            /* 0除算・極端値の保険 */
    int atkmod = 100 + unit_rank(atk) * 3 + co_atk;
    if (atkmod < 10) atkmod = 10;

    /* 天候: 曇=空↔地上の攻撃半減 / 雨=同攻撃不可（0を返す） */
    int wx = game_weather_atk_pct(g, atk, def);
    if (wx <= 0) return 0;
    /* 夜: 夜間ユニットは+50%、それ以外は-20% */
    int nt = game_night_atk_pct(g, atk);

    /* 射程減衰: 最短射程から1セル遠ざかるごとに -5%（命中率が落ちるイメージ）。
     * 直射(射程1)は差が0なので影響を受けない。同一セル(距離0)は差が負になるため
     * 0で下限を切る——切らないと近距離ボーナスになってしまう。 */
    int over = hex_distance(atk->pos.x, atk->pos.y, def->pos.x, def->pos.y)
               - at->range_min;
    if (over < 0) over = 0;
    int rng = 100 - RANGE_FALLOFF_PCT * over;
    if (rng < RANGE_FALLOFF_MIN) rng = RANGE_FALLOFF_MIN;

    return base * atkmod / defmod * wx / 100 * rng / 100 * nt / 100;
}

int battle_expect_damage_x10(const Game *g, const Unit *atk, const Unit *def)
{
    return base_damage_x10(g, atk, def);
}

/* 期待ダメージ(×10) を HP 単位へ（roll と同じ丸め: 四捨五入・最低1・最大10） */
static int to_hp_damage(int d10)
{
    if (d10 <= 0) return 0;
    int dmg = (d10 + 5) / 10;
    if (dmg < 1) dmg = 1;
    if (dmg > 10) dmg = 10;
    return dmg;
}

void battle_forecast(const Game *g, int atk_i, int def_i,
                     int *out_dmg, int *out_def_hp,
                     int *out_counter, int *out_atk_hp)
{
    const Unit *a = &g->units[atk_i];
    const Unit *d = &g->units[def_i];
    const UnitType *at = unit_type(g, a);
    const UnitType *dt = unit_type(g, d);

    int dmg = to_hp_damage(battle_expect_damage_x10(g, a, d));
    int dhp = d->hp - dmg;
    if (dhp < 0) dhp = 0;

    /* 反撃判定は game_attack と同一の規則にそろえる */
    int counter = 0;
    if (dhp > 0) {
        int dist = hex_distance(a->pos.x, a->pos.y, d->pos.x, d->pos.y);
        bool atk_indirect = at->range_min >= 2;
        bool in_range = (dist == 0)
            ? (dt->range_min <= 1)
            : (dist >= dt->range_min && dist <= game_range_max(g, dt));
        if (!atk_indirect && in_range && unit_can_attack_target(g, d, a)) {
            Unit tmp = *d;                 /* 被弾後のHPで反撃威力を見積もる */
            tmp.hp = (int8_t)dhp;
            counter = to_hp_damage(battle_expect_damage_x10(g, &tmp, a));
        }
    }
    int ahp = a->hp - counter;
    if (ahp < 0) ahp = 0;

    if (out_dmg)     *out_dmg = dmg;
    if (out_def_hp)  *out_def_hp = dhp;
    if (out_counter) *out_counter = counter;
    if (out_atk_hp)  *out_atk_hp = ahp;
}

int battle_roll_damage(Game *g, const Unit *atk, const Unit *def)
{
    int d10 = base_damage_x10(g, atk, def);
    if (d10 <= 0) return 0;

    int r = rng_range(&g->rng, -15, 15);
    d10 = d10 * (100 + r) / 100;

    int dmg = (d10 + 5) / 10; /* 四捨五入でHP段階へ */
    if (dmg < 1) dmg = 1;     /* 最低保証 */
    if (dmg > 10) dmg = 10;
    return dmg;
}
