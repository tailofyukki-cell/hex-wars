/* types.h - 共通型・上限定数（仕様書 2.4） */
#ifndef HW_TYPES_H
#define HW_TYPES_H

#include <stdint.h>
#include <stdbool.h>

#define MAX_MAP_W        64
#define MAX_MAP_H        64
/* 最大5陣営。実際に何陣営で戦うかはマップが決める
 * （開幕時にユニットか建物を持っている陣営だけが参加する）。
 * 2陣営の既存マップはそのまま動く。 */
#define MAX_PLAYERS       5
#define MAX_UNITS       200
#define MAX_UNIT_TYPES   96   /* 種類数。索引は1バイトなので255まで増やせる */
#define MAX_TERRAIN      32   /* 同上。ファンタジー等で地形を増やす余地 */
#define MAX_COMMANDERS   16
#define MAX_CAMPAIGN_MAPS 20
/* 次の作戦へ引き継げるユニット数と、倉庫の容量。
 * 生き残った部隊は上限なく引き継げる方針なので、1マップに存在しうる最大数
 * （MAX_UNITS）まで取ってある＝実質無制限。セーブは件数を先に書く形式なので、
 * ここを増やしてもセーブ形式は変わらず、古いセーブもそのまま読める
 * （件数は u8 で書くので 255 までが上限）。 */
#define MAX_CARRY_UNITS  MAX_UNITS
#define MAX_STORE_UNITS  MAX_UNITS   /* 倉庫（出撃枠に入らなかった部隊の保管庫） */
/* 持越しユニットの初期配置上限 = マップ本来の自軍ユニット数 × この倍率。
 * 超過分は倉庫行き（生産拠点で無料で引き出せる） */
#define DEPLOY_CARRY_RATIO 2
/* キャンペーンで敵に与える増援の上限倍率。
 * 自軍は持越しで最大3倍（元の戦力＋DEPLOY_CARRY_RATIO倍）まで増えるので、
 * 敵にも「実際に持ち込んだ数」に応じた増援を出して戦力差を埋める。
 * 2 なら敵は最大で元の2倍まで。 */
#define CAMPAIGN_ENEMY_MAX_RATIO 2
/* 1ユニットが搭載できる最大数。capacity はこれを超えられない。
 * 大型空母（進化後）が4機積むために2→4へ広げた。セーブ形式に出るので
 * 変えるときは SAVE_VERSION も上げること。 */
#define MAX_CARGO 4
/* --- 昼夜（固定周期） ---
 * 天候がランダムなのに対し、昼夜は周期が固定なので「夜明けまで耐える」
 * 「日沒を待って仕掛ける」という予定が立つ。
 * **ターン数から一意に決まるので状態を持たない**（セーブも増えない）。 */
#define DAY_TURNS   3
#define NIGHT_TURNS 2

#define MAX_EVENTS 16     /* 1作戦に仕込めるイベント数（events_fired が32bitなので32が上限） */

/* 移動タイプ（仕様書 5.2） */
typedef enum {
    MC_FOOT = 0,
    MC_WHEEL,
    MC_TRACK,
    MC_AIR,
    MC_SEA,
    MC_SUB,
    MC_COUNT
} MoveClass;

/* 高度レイヤー（マップ立体化。docs/layering_spec.md）。
 * 高さが違えば同一セルに共存でき、各(セル,レイヤー)に最大1体。
 * レイヤーは MoveClass から一意に決まる（Unit には保存しない＝セーブ互換）。 */
typedef enum {
    LAYER_AIR = 0,   /* 空: 航空機 */
    LAYER_SURFACE,   /* 地表・海面: 歩兵/車両/戦車 と 水上艦 */
    LAYER_UNDER,     /* 海中: 潜水艦 */
    LAYER_COUNT
} Layer;

static inline Layer unit_layer(int mclass)
{
    if (mclass == MC_AIR) return LAYER_AIR;
    if (mclass == MC_SUB) return LAYER_UNDER;
    return LAYER_SURFACE;   /* FOOT/WHEEL/TRACK/SEA */
}

/* 装甲カテゴリ（攻撃側がどの ATK 値を使うか） */
typedef enum {
    ARMOR_SOFT = 0,
    ARMOR_HARD,
    ARMOR_AIR,
    ARMOR_SEA,
    ARMOR_COUNT
} ArmorCat;

/* 燃料切れになったときの扱い（.def の no_fuel）。
 * 現代戦では航空機=墜落・艦船=漂流だが、飛竜や浮遊要塞には不要なのでデータで選ぶ。 */
typedef enum {
    NOFUEL_NONE = 0,   /* 何も起きない */
    NOFUEL_DIE,        /* その場で失われる（墜落） */
    NOFUEL_DAMAGE,     /* 毎ターン HP-1（漂流） */
    NOFUEL_AUTO = 255  /* 未指定: class から従来どおりに決める */
} NoFuelKind;

/* upkeep 未指定を表す値（0 は「消費しない」という有効な指定なので分ける） */
#define UPKEEP_AUTO 255

/* 施設の生産カテゴリ */
typedef enum {
    PROD_NONE = 0,
    PROD_LAND,
    PROD_AIR,
    PROD_SEA
} ProduceCat;

typedef struct { int8_t q, r; } Axial;
typedef struct { uint8_t x, y; } Cell;

/* ユニット定義（units.def の1エントリ） */
typedef struct {
    char     id[24];
    char     name[32];
    char     icon[8];        /* 1文字表示用 (UTF-8) */
    uint8_t  mclass;         /* MoveClass */
    uint8_t  armor;          /* ArmorCat: 被弾時カテゴリ */
    int16_t  cost;
    uint8_t  move;
    uint8_t  fuel;
    uint8_t  ammo;
    uint8_t  vision;
    int16_t  atk[ARMOR_COUNT]; /* 対 SOFT/HARD/AIR/SEA 攻撃力 */
    int16_t  def_;
    uint8_t  range_min;
    uint8_t  range_max;
    uint8_t  can_capture;
    uint8_t  move_and_fire;  /* 1=移動後攻撃可（直射） */
    uint8_t  anti_sub;       /* 1=対潜能力あり */
    uint8_t  is_sub;         /* 1=潜水艦 */
    uint8_t  supply;         /* 1=隣接味方に燃料・弾薬を補給できる（補給車等） */
    uint8_t  engineer;       /* 1=隣接ヘクスの地形を破壊・復旧できる（工兵） */
    /* 維持コストと燃料切れの扱い。既定は class から決まる（従来の挙動）ので、
     * 現代戦以外（飛竜・浮遊要塞など）では .def で明示して上書きする。 */
    uint8_t  upkeep;         /* 補給施設の外にいる間、毎ターン減る燃料。UPKEEP_AUTO=未指定 */
    uint8_t  no_fuel;        /* NoFuelKind。NOFUEL_AUTO=未指定 */
    int8_t   move_se;        /* 移動時の効果音（SeId）。-1=class から自動 */
    /* 輸送（仕様書 5.9） */
    uint8_t  capacity;              /* 搭載可能数 (0=輸送不可) */
    uint8_t  resupply_cargo;        /* 1=搭載中ユニットをターン開始時に補給（空母） */
    /* 1=空挺降下できる（輸送機）。隣接ではなく「自分がいるヘクスの真下」へ降ろせる。
     * 降りた部隊はその手番行動できない（game_unload_unit が UF_DONE を付ける）。 */
    uint8_t  paradrop;
    char     transport_by[4][24];   /* このユニットを搭載できる輸送手段ID */
    uint8_t  n_transport_by;
    /* スプライト画像（assets/ 相対パス。空=図形描画にフォールバック）
     * image[0]=P0用 / image[1]=P1用（units.def の image は両方に設定） */
    char     image[2][64];
    /* 戦闘アニメ動画（assets/ 相対パス。アニメーションGIF/WebP）。
     * units.def の `anim =` で指定。空=動画なし（従来のHPバー演出にフォールバック） */
    char     anim[64];
    /* 攻撃時のカットイン1枚絵（assets/ 相対パス）。units.def の `cutin =`。
     * 空なら指揮官の cutin にフォールバックし、それも無ければ出さない。 */
    char     cutin[64];
    /* 1=偵察部隊。移動して新しい土地を明かすと経験値が入る（戦えない偵察機の
     * 唯一の成長手段。戦える偵察車には戦闘ぶんへの上乗せになる）。 */
    /* 夜間ユニット。夜に攻撃力が上がり、夜でも視界が落ちない。
     * その分だけ素の性能は同価帯の通常ユニットより低めにしてある。 */
    uint8_t  night;
    uint8_t  recon;
    /* 進化（docs/evolution_spec.md）。
     * evolve_to … 経験値満タンで進化できる相手のユニットID。空=進化しない
     * no_produce … 1=生産メニューに出さない（進化でのみ入手できる） */
    char     evolve_to[24];
    uint8_t  no_produce;
} UnitType;

/* 地形定義（terrain.def の1エントリ） */
typedef struct {
    char     id[24];
    char     name[32];
    char     chr;                 /* マップファイル上の1文字 */
    int16_t  def_bonus;           /* 防御補正 % */
    int16_t  mcost[MC_COUNT];     /* 移動コスト（2倍整数、0=進入不可） */
    int16_t  income;              /* 収入 */
    uint8_t  capturable;          /* 占領対象か */
    uint8_t  produces;            /* ProduceCat */
    uint8_t  supplies;            /* 補給対象 MoveClass のビットマスク */
    uint8_t  hide;                /* 1=隣接しないと中の敵を視認不可 */
    uint8_t  is_hq;               /* 1=首都 */
    /* 工兵による破壊と復旧。breaks_to が空の地形は壊せない。
     * repair_cost は「この地形に戻す」のに要る資金（元の地形側に書く）。 */
    char     breaks_to[24];       /* 壊されたときになる地形ID（空=破壊不可） */
    int16_t  breaks_idx;          /* 上を解決した index（-1=なし） */
    int16_t  repair_cost;         /* この地形へ復旧する費用 */
    uint32_t color;               /* 0xRRGGBB 描画色 */
    /* 斜め見下ろし表示での起伏（見た目のみ。ルールには一切影響しない）。
     * ヘクス半径32px基準のpx値で、山=高く/海=負=沈む。描画時にズーム倍率で拡縮する。 */
    int16_t  height;
    /* セルの画像（assets/ 相対パス。空=color での単色描画にフォールバック）。
     * ユニットの image= と同じ考え方で、PNGを差し替えるだけで見た目が変わる。
     * 六角形の内側にクリップして貼るので、画像は正方形で中央にタイルを描くとよい。 */
    char     image[64];
} TerrainType;

/* 天候（仕様: 晴=変化なし / 曇=空↔地上の攻撃半減・視界-1 / 雨=同攻撃不可・
 * 視界-2・地上移動-1）。ラウンド単位で抽選し数ターン継続する。 */
typedef enum {
    WX_CLEAR = 0,   /* 晴 */
    WX_CLOUDY,      /* 曇り */
    WX_RAIN,        /* 雨 */
    WX_COUNT
} Weather;

/* 指揮官（CO）の必殺技の種類 */
typedef enum {
    CO_POW_HEAL = 0,   /* 全軍HP+val */
    CO_POW_RUSH,       /* 全軍が再行動 */
    CO_POW_STRIKE,     /* このターン攻撃+val% */
    CO_POW_FUNDS,      /* 資金+val */
    CO_POW_SCOUT,      /* このターン全マップ視認 + 資金val */
    /* --- ここからは後から追加した種類。
     * STRIKE だけだと「攻撃力が上がる」の値違いで指揮官が似通ってしまうため。 --- */
    CO_POW_SHIELD,     /* このターン防御+val% */
    CO_POW_ADVANCE,    /* このターン移動力+val */
    CO_POW_RESUPPLY,   /* 全軍の燃料・弾薬を全回復（位置を問わない） */
    CO_POW_VETERAN,    /* 全軍の経験値+val（進化を前倒しする） */
    CO_POW_BARRAGE     /* 自軍に隣接する敵全部に val ダメージ（狙いを選ばない形） */
} CoPowerType;

/* 常時効果の対象ドメイン（0=全部 / 以降は move_domain()+1 と対応） */
typedef enum {
    CO_DOM_ALL = 0, CO_DOM_LAND, CO_DOM_AIR, CO_DOM_SEA
} CoDomain;

/* 指揮官定義（commanders.def の1エントリ） */
typedef struct {
    char     id[24];
    char     name[32];
    char     title[48];
    char     desc[128];
    uint8_t  domain;          /* CoDomain */
    int16_t  atk_pct;
    int16_t  def_pct;
    int8_t   move_bonus;
    int8_t   vision_bonus;
    int16_t  income_pct;
    char     power_name[32];
    char     power_desc[128];
    int16_t  power_cost;
    uint8_t  power_type;      /* CoPowerType */
    int16_t  power_val;
    int16_t  unlock_clears;   /* 解禁に必要なクリア数（0=最初から使える） */
    /* 攻撃時のカットイン1枚絵（assets/ 相対パス）。commanders.def の `cutin =`。
     * ユニット側に cutin が無いときのフォールバックとして使う。 */
    char     cutin[64];
    /* 指揮官の顔絵（assets/ 相対パス）。commanders.def の `image =`。
     * 指揮官選択の画面に出す。空なら名前と説明だけの従来表示になる。 */
    char     image[64];
} CommanderType;

/* --- マップイベント（作戦途中で起きる出来事） ---
 * .cpn に `event = 条件 | 動作 | メッセージ` と書く。各イベントは1回だけ発火する。
 * 条件・動作とも数値化して Game が持つので、セーブ/ロードをまたいでも状態が保たれる。 */
typedef enum {
    EV_C_NONE = 0,
    EV_C_TURN,      /* c1 ターン目に到達した */
    EV_C_BLD,       /* 陣営 c1 の建物が c2 個以上 */
    EV_C_LOSS,      /* 陣営 c1 の損失が c2 体以上 */
    EV_C_AREA,      /* 陣営 c1 のユニットが (c2,c3) から半径 c4 以内にいる */
    EV_C_CAPTURED,  /* (c2,c3) の拠点を陣営 c1 が所有している */
    EV_C_WEATHER    /* 現在の天候が c1（0=晴 1=曇 2=雨） */
} EvCond;

typedef enum {
    EV_A_NONE = 0,
    EV_A_MSG,       /* メッセージだけ */
    EV_A_SPAWN,     /* 陣営 a1 のユニット種別 a2 を (a3,a4) 付近に a5 体出す */
    EV_A_FUNDS,     /* 陣営 a1 に資金 a2 を与える */
    EV_A_WEATHER,   /* 天候を a1 に変え、a2 ラウンド固定する */
    EV_A_TERRAIN,   /* (a1,a2) の地形を文字 a3 の地形に差し替える */
    EV_A_COPOWER,   /* 陣営 a1 の指揮官の必殺技を強制発動する */
    EV_A_HP         /* 陣営 a1 の全ユニットの HP を a2 変化させる（負も可） */
} EvAct;

typedef struct {
    uint8_t cond;              /* EvCond */
    int16_t c1, c2, c3, c4;
    uint8_t act;               /* EvAct */
    int16_t a1, a2, a3, a4, a5;
    char    msg[96];
} MapEvent;

/* マップ1ヘクス */
typedef struct {
    uint8_t terrain;   /* TerrainType のインデックス */
    int8_t  owner;     /* -1=中立 / 0..1=陣営 */
    uint8_t cap_hp;    /* 占領残耐久（満タン=CAPTURE_HP） */
    int16_t capturer;  /* 占領中ユニット index、-1=なし */
    /* マップ本来の地形。工兵の破壊やイベントで terrain が変わっても
     * ここは動かない。terrain != orig_terrain なら「壊れている」状態で、
     * 工兵の復旧でこの地形に戻る。 */
    uint8_t orig_terrain;
} Tile;

#define CAPTURE_HP 20

/* ユニットフラグ */
#define UF_ALIVE   0x01
#define UF_DONE    0x02   /* このターン行動済み */
#define UF_MOVED   0x04   /* このターン移動した（間接攻撃判定用） */
#define UF_LOADED  0x08   /* 輸送ユニットに搭載中（盤上に存在しない） */

typedef struct {
    uint8_t  type;
    uint8_t  owner;
    Cell     pos;
    int8_t   hp;          /* 1..10 */
    uint8_t  fuel;
    uint8_t  ammo;
    uint8_t  exp;         /* 0..100 */
    uint8_t  flags;
    int16_t  cargo[MAX_CARGO];  /* 搭載ユニットの index、-1で空 */
} Unit;

#endif /* HW_TYPES_H */
