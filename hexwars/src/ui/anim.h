/* anim.h - 動画/アニメーションの読込・再生（戦闘アニメ・クリア報酬・エンドロール）
 *
 * 対応形式:
 *   - アニメーションGIF … SDL2_image で直接読む（メモリ保持）
 *   - mp4 / webm / mov / mkv / avi … ffmpeg で PNG 連番に展開してキャッシュし、
 *     再生時に1枚ずつ読み込む（ディスク保持。メモリを食わない）
 *     ※ ffmpeg.exe が PATH か <base>tools/ffmpeg/ に必要
 */
#ifndef HW_ANIM_H
#define HW_ANIM_H

#include <SDL.h>
#include <stdbool.h>

typedef struct App App;

typedef struct {
    int  count;               /* フレーム数 */
    int  w, h;                /* 原寸 */
    int  total_ms;            /* 全体の再生時間 */

    /* --- メモリ保持（GIF） --- */
    SDL_Texture **frames;     /* count 個。ディスクモードでは NULL */
    int          *delays;     /* 各フレームの表示ms */

    /* --- ディスク保持（ffmpeg 展開の PNG 連番） --- */
    char dir[512];            /* 空でなければディスクモード */
    int  fps;
    int  cached_idx;          /* 現在テクスチャ化しているフレーム番号（-1=なし） */
    SDL_Texture *cached_tex;
} UnitAnim;

void uanim_init(void);
void uanim_clear(void);
void uanim_quit(void);

/* ユニット種別の戦闘アニメ（units.def の anim=）。無指定/失敗は NULL */
UnitAnim *uanim_get(App *a, int type);

/* 任意パスの動画（クリア報酬・エンドロール用）。assets/ 相対。失敗は NULL */
UnitAnim *uanim_get_path(App *a, const char *rel);

/* 経過ms のフレームを返す（末尾で停止）。ディスクモードでは遅延読込する */
SDL_Texture *uanim_frame_at(App *a, UnitAnim *ua, int elapsed_ms);

/* rel が動画として扱える拡張子か（存在確認はしない） */
bool uanim_is_video_path(const char *rel);

#endif /* HW_ANIM_H */
