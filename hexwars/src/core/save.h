/* save.h - セーブ/ロード（仕様書 9章） */
#ifndef HW_SAVE_H
#define HW_SAVE_H

#include "game.h"
#include "campaign.h"

#define SAVE_MAGIC   "HXWS"
#define SAVE_VERSION 8   /* v8: 搭載枠を2→4に拡張（大型空母用）。v7以前も読める */
/* この版以降は読める（古いセーブは欠けている項目を既定値で埋める）。
 * 進行中のキャンペーンを無駄にしないための下位互換。 */
#define SAVE_VERSION_MIN 6
#define SAVE_SLOTS   11   /* 0=オートセーブ, 1..10=手動 */

/* 成功=0。err は省略可 */
int save_game(const Game *g, const CampaignState *cs, const char *path,
              char *err, int errlen);
int load_game(Game *g, CampaignState *cs, const char *path,
              char *err, int errlen);

/* スロット一覧表示用: マップ名とターン数だけ読む。成功=0 */
int save_peek(const char *path, char *map_name, int name_len, int *turn);

/* saves ディレクトリを（無ければ）作る */
void save_ensure_dir(const char *dir);

#endif
