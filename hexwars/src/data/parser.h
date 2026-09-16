/* parser.h - INI風独自テキスト形式の読込（仕様書 3章） */
#ifndef HW_PARSER_H
#define HW_PARSER_H

#include "../core/game.h"

/* 行を key/value に分解（共有ユーティリティ）。
 * 戻り値: 0=空行 1=key=value 2=[section]（key にセクション名）
 *
 * **言語の上書き行**: キーの後ろに @言語コード を付けた行（例 `name@en = Tank`）は、
 * 表示言語がその言語のときだけ @ 以降を切り落とした普通の行として返し、
 * それ以外の言語では空行（0）として読み飛ばす。どのデータファイルでも同じ。
 * 同じキーは後に書いた方が勝つので、**上書き行は元の行のあとに書くこと。** */
int parser_split_line(char *line, char **key, char **val);

/* 表示言語（"ja" / "en"）。既定は "ja" */
void data_set_lang(const char *code);
const char *data_lang(void);
/* 直前に parser_split_line が返した行が言語の上書き行だったか。
 * 複数行を積むキー（ブリーフィング等）で、日本語の行を捨てて差し替えるのに使う */
int parser_last_was_lang(void);

/* 各ローダ: 成功=0、失敗=-1（err に行番号付きメッセージ） */
int data_load_terrain(Game *g, const char *path, char *err, int errlen);
int data_load_units(Game *g, const char *path, char *err, int errlen);
int data_load_map(Game *g, const char *path, char *err, int errlen);

/* ユニット定義 id 検索。-1=なし */
int data_find_unit_type(const Game *g, const char *id);
/* 指揮官定義。ファイルが無い場合も 0 を返す（指揮官なしで動作する） */
int data_load_commanders(Game *g, const char *path, char *err, int errlen);
int data_find_commander(const Game *g, const char *id);

/* マップ一覧（maplist.txt: 「ファイル名|表示名」行） */
/* フリー対戦で選べるマップの上限。
 * 超えた分は黙って捨てられるので、maplist.txt を増やしたらここも見ること。 */
#define MAX_MAPLIST 32
typedef struct {
    char file[MAX_MAPLIST][64];
    char name[MAX_MAPLIST][64];   /* 表示言語の名前（3列目に英語があれば英語） */
    int  n;
} MapList;
int data_load_maplist(MapList *ml, const char *path);

#endif
