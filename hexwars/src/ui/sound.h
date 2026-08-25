/* sound.h - BGM/SE 再生とオプション永続化（仕様書 10章）
 *
 * 音源は data/audio.def で差し替えられる（コード変更不要）。
 * 対応形式は SDL2_mixer 依存で WAV / OGG / MP3 / FLAC。 */
#ifndef HW_SOUND_H
#define HW_SOUND_H

#include <stdbool.h>

typedef enum {
    SE_CURSOR = 0,
    SE_OK,
    SE_CANCEL,
    SE_MOVE_FOOT,
    SE_MOVE_VEHICLE,
    SE_MOVE_AIR,
    SE_SHOT,
    SE_EXPLOSION,
    SE_CAPTURE,
    SE_TURN,
    SE_COUNT
} SeId;

/* 戦闘BGMと効果音セットは audio.def の記述数で決まる。ここは器の上限 */
#define MAX_BATTLE_TRACKS 12
#define MAX_SE_SETS        6

/* 固定枠（TITLE/VICTORY/DEFEAT）＋ 戦闘BGM（可変長） */
typedef enum {
    HWM_NONE = -1,
    HWM_TITLE = 0,
    HWM_VICTORY,
    HWM_DEFEAT,
    HWM_BATTLE0,
    HWM_COUNT = HWM_BATTLE0 + MAX_BATTLE_TRACKS
} MusId;

/* 失敗しても続行可能（無音動作）。戻り値 0=音あり */
int  snd_init(const char *base_path);
void snd_quit(void);

void snd_se(int id);
/* 同じ曲が再生中なら何もしない。loop=false は1回のみ */
void snd_music(int id, bool loop);
void snd_music_stop(void);

/* 音量 0..10（仕様書 10章）。人の耳に合わせて2乗カーブで効かせる */
void snd_apply_volumes(int bgm, int se);

/* --- audio.def から読み込んだ内容の問い合わせ --- */
/* 戦闘BGMの曲数（0 なら戦闘BGMなし） */
int  snd_battle_track_count(void);
/* 戦闘BGMの表示名（範囲外は "-"） */
const char *snd_battle_track_name(int i);
/* 効果音セットの数（最低1） */
int  snd_se_set_count(void);
/* 効果音セットの表示名（範囲外は "-"） */
const char *snd_se_set_name(int i);

/* 効果音セットを切り替える。読み直しに失敗した音は無音になる */
void snd_set_se_set(int set);

/* 戦闘BGMのID。track<0 ならマップ名から自動で選ぶ。曲が無ければ HWM_NONE */
int  snd_battle_music(int track, const char *map_name);

#endif
