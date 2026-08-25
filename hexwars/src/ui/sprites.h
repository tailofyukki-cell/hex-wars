/* sprites.h - ユニット画像（PNG）の読込とキャッシュ */
#ifndef HW_SPRITES_H
#define HW_SPRITES_H

#include <SDL.h>

typedef struct App App;

/* SDL2_image 初期化（失敗しても続行可能: 全て図形描画になる） */
int  sprites_init(void);
void sprites_quit(void);

/* type/owner のスプライトを返す（遅延読込）。なければ NULL */
SDL_Texture *sprite_get(App *a, int type, int owner);

/* 定義再読込などでキャッシュを破棄したい場合に呼ぶ */
void sprites_clear(void);

/* 任意画像をその場で読み込む（キャッシュなし。呼び出し側が Destroy する）。
 * rel は assets/ 相対パス。失敗時 NULL */
SDL_Texture *sprite_load_file(App *a, const char *rel, int *w, int *h);

#endif
