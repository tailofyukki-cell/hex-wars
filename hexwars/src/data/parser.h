/* parser.h - INI風独自テキスト形式の読込（仕様書 3章） */
#ifndef HW_PARSER_H
#define HW_PARSER_H

#include "../core/game.h"

/* 行を key/value に分解（共有ユーティリティ）。
 * 戻り値: 0=空行 1=key=value 2=[section]（key にセクション名） */
int parser_split_line(char *line, char **key, char **val);

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
    char name[MAX_MAPLIST][64];
    int  n;
} MapList;
int data_load_maplist(MapList *ml, const char *path);

#endif
